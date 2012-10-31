/***************************************************************************
 *   Copyright (C) 2004 by Michael Reithinger                              *
 *   mreithinger@web.de                                                    *
 *                                                                         *
 *   This file is part of fex.                                             *
 *                                                                         *
 *   fex is free software; you can redistribute it and/or modify           *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   fex is distributed in the hope that it will be useful,                *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "logging.h"
#include "rsync.h"
#include "configfile.h"
#include <utime.h>
extern "C" {
#include <librsync.h>

typedef struct rs_filebuf rs_filebuf_t;

rs_filebuf_t *rs_filebuf_new(FILE *f, size_t buf_len);

void rs_filebuf_free(rs_filebuf_t *fb);

rs_result rs_infilebuf_fill(rs_job_t *, rs_buffers_t *buf, void *fb);

rs_result rs_outfilebuf_drain(rs_job_t *, rs_buffers_t *, void *fb);
}

using namespace std;
using namespace nmstl;

static
string 
get_file_name(const string& path) 
{
  size_t pos = path.rfind('/');

  if (pos != string::npos)
    return path.substr(pos + 1);

  return path;
}

struct send_buf
{
  char                 buffer[MAX_COPY_SIZE];
  ConnectedWatchPoint* wp;
  unsigned int         message;
};


static
rs_result 
rs_outnetbuf_drain(rs_job_t *job, rs_buffers_t *buf, void *opaque)
{
  int       present;
  send_buf* sb = (send_buf*)opaque;

  /* This is only allowed if either the buf has no output buffer
   * yet, or that buffer could possibly be BUF. */
  if (buf->next_out == NULL) {
    assert(buf->avail_out == 0);
    buf->next_out  = sb->buffer;
    buf->avail_out = sizeof(sb->buffer);
    return RS_DONE;
  }
        
  assert(buf->avail_out <= sizeof(sb->buffer));
  assert(buf->next_out >= sb->buffer);
  assert(buf->next_out <= sb->buffer + sizeof(sb->buffer));

  present = buf->next_out - sb->buffer;
  if (present > 0) {
    int result;
                
    assert(present > 0);

    sb->wp->write(fex_header(sb->message), constbuf(sb->buffer, present));

    buf->next_out  = sb->buffer;
    buf->avail_out = sizeof(sb->buffer);

    if (sb->wp->write_bytes_pending())
      return RS_BLOCKED;
  }
        
  return RS_DONE;
}


/***************************************************************************/

struct RsyncSendDialog::Context 
{
  rs_job_t*       job;
  rs_result       result;
  rs_buffers_t    rsbuf;
  FILE*           base_file;
  FILE*           new_file;
  rs_filebuf_t*   fb;
  send_buf        sb;
};

static 
void
clearContext(RsyncSendDialog::Context* context)
{
  if (context->job)
    rs_job_free(context->job);

  if (context->base_file)
    fclose(context->base_file);

  if (context->new_file)
    fclose(context->new_file);

  if (context->fb)
    rs_filebuf_free(context->fb);

  memset(context, 0, sizeof(*context));
}


RsyncSendDialog::
RsyncSendDialog(ConnectedWatchPoint& wp, 
		const string& file, 
		const State& state)
  : ConnectedWatchPoint::Dialog(wp)
{
  _M_File    = file;
  _M_State   = state;
  _M_Context = new Context;
  memset(_M_Context, 0, sizeof(Context));
}

RsyncSendDialog::
~RsyncSendDialog()
{
  clearContext(_M_Context);
  delete _M_Context;

  string tmp2(parent().wp()->tmp_dir() + get_file_name(_M_File) + "trans");
  ::unlink(tmp2.c_str());
}

void RsyncSendDialog::
start()
{
  sendSigsBegin();
}


void RsyncSendDialog::
incoming_message(const fex_header &head, constbuf buf)
{
  switch(head.type) {
  case ME_RsyncAbort:
    lc.notice("rsync for %s aborted", _M_File.c_str());
    endDialog();
    return;

  case ME_RsyncDeltaBlock:
    patchFile(buf);
    return;

  case ME_RsyncDeltaEnd:
    patchFile(constbuf());
    return;

  case ME_Reject:
    return;

  case ME_wavail:
    sendSigsIter();
    return;
  }

#ifndef NDEBUG
  if (head.type != ME_wavail)
    lc.info("RsyncSendDialog didn't accept %s", message_str(head.type));
#endif

  ConnectedWatchPoint::Dialog::incoming_message(head, buf);
}

void RsyncSendDialog::
patchFile(constbuf buf)
{
  if (_M_Context->result != RS_DONE && _M_Context->result != RS_BLOCKED)
    return;

  if (! _M_Context->job) {
    string tmp1(parent().wp()->path() + _M_File);
    string tmp2(parent().wp()->tmp_dir() + get_file_name(_M_File) + "trans");

    _M_Context->base_file = fopen(tmp1.c_str(), "rb");
    if (! _M_Context->base_file) {
      lc.error("Could not open base_file %s for rsync patch (%s) ", 
	       _M_File.c_str(), strerror(errno));
      endDialog();
      return;
    }

    _M_Context->new_file = fopen(tmp2.c_str(), "wb");
    if (! _M_Context->new_file) {
      lc.error("Could not open new_file %s for rsync(%s) ", 
	       _M_File.c_str(), strerror(errno));
      endDialog();
      return;
    }

    _M_Context->fb  = rs_filebuf_new(_M_Context->new_file, 
				     rs_outbuflen);
    _M_Context->job = rs_patch_begin(rs_file_copy_cb, 
				     _M_Context->base_file);
    memset(&_M_Context->rsbuf, 0, sizeof(_M_Context->rsbuf));
  }

  _M_Context->rsbuf.next_in  = const_cast<char*>(buf.data());
  _M_Context->rsbuf.avail_in = buf.length();

  do {
    if (_M_Context->rsbuf.avail_in == 0)
      break;

    _M_Context->result = rs_job_iter(_M_Context->job, &_M_Context->rsbuf);
    if (_M_Context->result != RS_DONE && _M_Context->result != RS_BLOCKED) {
      lc.error("error patching %s (%s)", _M_File.c_str(), 
	       rs_strerror(_M_Context->result));
      endDialog();
      return;
    }
    
    int iores = rs_outfilebuf_drain(_M_Context->job, 
				    &_M_Context->rsbuf, 
				    _M_Context->fb);
    if (iores != RS_DONE) {
      lc.error("error patching %s (%s)", _M_File.c_str(), 
	       rs_strerror(_M_Context->result));
      endDialog();
      return;
    }
  }
  while(_M_Context->result != RS_DONE);
  
  if (buf.length() == 0) {
    clearContext(_M_Context);

    string tmp1 = parent().wp()->tmp_dir() + get_file_name(_M_File) + "trans";
    string tmp2 = parent().wp()->path() + _M_File;
    parent().wp()->remove(_M_File);
    ::rename(tmp1.c_str(), tmp2.c_str());
    parent().wp()->changeAccess(_M_File, _M_State);
    lc.info("rsynched file to: %s", tmp2.c_str());
    endDialog();
  }
}

void RsyncSendDialog::
sendSigsBegin()
{
  string tmp(parent().wp()->path() + _M_File);

  _M_Context->sb.wp      = &parent();
  _M_Context->sb.message = ME_RsyncSigBlock;
  _M_Context->base_file = fopen(tmp.c_str(), "rb");

    if (! _M_Context->base_file) {
      int fd = ::open(tmp.c_str(), O_CREAT|O_APPEND);
      ::close(fd);
      _M_Context->base_file = fopen(tmp.c_str(), "rb");
    }

  if (! _M_Context->base_file) {
    lc.error("Could not open base_file %s for rsync send (%s)", 
	     tmp.c_str(), strerror(errno));
    endDialog();
    return;
  }

  _M_Context->job = rs_sig_begin(RS_DEFAULT_BLOCK_LEN, RS_DEFAULT_STRONG_LEN);
  _M_Context->fb  = rs_filebuf_new(_M_Context->base_file, rs_inbuflen);
  parent().write(fex_header(ME_RsyncStart), constbuf(_M_File));  
  sendSigsIter();
}


void RsyncSendDialog::
sendSigsIter()
{
  _M_Context->result = rs_job_drive(_M_Context->job, &_M_Context->rsbuf,
			rs_infilebuf_fill, _M_Context->fb,
			rs_outnetbuf_drain, &_M_Context->sb);

  if (_M_Context->result != RS_BLOCKED) {
    if (_M_Context->result != RS_DONE) {
      lc.error("error building sig blocks for %s (%s)",
		_M_File.c_str(),
		rs_strerror(_M_Context->result));
      parent().write(fex_header(ME_RsyncAbort));
      endDialog();
    }
    else
      parent().write(fex_header(ME_RsyncSigEnd));

    clearContext(_M_Context);
  }
}

/***************************************************************************/

struct RsyncReceiveDialog::Context 
{
  rs_job_t*       job;
  rs_result       result;
  rs_buffers_t    rsbuf;
  rs_signature_t* sumset;
  FILE*           src_file;
  rs_filebuf_t*   fb;
  send_buf        sb;
};


static 
void
clearContext(RsyncReceiveDialog::Context* context)
{
  if (context->job)
    rs_job_free(context->job);

  if (context->sumset)
    rs_free_sumset(context->sumset);

  if (context->src_file)
    fclose(context->src_file);

  if (context->fb)
    rs_filebuf_free(context->fb);

  memset(context, 0, sizeof(*context));
}


RsyncReceiveDialog::
RsyncReceiveDialog(ConnectedWatchPoint& wp)
  : ConnectedWatchPoint::Dialog(wp)
{
  _M_Context = new Context;
  memset(_M_Context, 0, sizeof(Context));
}

RsyncReceiveDialog::
~RsyncReceiveDialog()
{
  clearContext(_M_Context);
  delete _M_Context;
}

void RsyncReceiveDialog::
incoming_message(const fex_header &head, constbuf buf)
{
  switch(head.type) {
  case ME_RsyncStart:
    _M_File = buf;
    return;

  case ME_RsyncAbort:
    lc.notice("rsync for %s aborted", _M_File.c_str());
    endDialog();
    return;

  case ME_RsyncSigBlock:
    buildSignatures(buf);
    return;

  case ME_RsyncSigEnd:
    buildSignatures(constbuf());
    return;

  case ME_wavail:
    deltaFileIter();
    return;

  case ME_Reject:
    return;
  }

#ifndef NDEBUG
  if (head.type != ME_wavail)
    lc.info("RsyncReceiveDialog didn't accept %s", message_str(head.type));
#endif

  ConnectedWatchPoint::Dialog::incoming_message(head, buf);
}

void RsyncReceiveDialog::
buildSignatures(constbuf buf)
{
  if (_M_Context->result != RS_DONE && _M_Context->result != RS_BLOCKED) {
    // In case of error, wait for the last signature block
    if (buf.length() == 0) {
      parent().write(fex_header(ME_RsyncAbort));
      endDialog();
    }
    return;
  }

  if (! _M_Context->job) 
    _M_Context->job = rs_loadsig_begin(&_M_Context->sumset);

  memset(&_M_Context->rsbuf, 0, sizeof(_M_Context->rsbuf));
  _M_Context->rsbuf.next_in  = const_cast<char*>(buf.data());
  _M_Context->rsbuf.avail_in = buf.length();

  do {
    if (_M_Context->rsbuf.avail_in == 0)
      break;

    _M_Context->result = rs_job_iter(_M_Context->job, &_M_Context->rsbuf);
    if (_M_Context->result != RS_DONE && _M_Context->result != RS_BLOCKED) {
      lc.error("error building sig_sum_set for %s (%s)",
	       _M_File.c_str(),
	       rs_strerror(_M_Context->result));
      rs_job_free(_M_Context->job);
      _M_Context->job = NULL;
      return;
    }
  }
  while(_M_Context->result != RS_DONE);
  
  if (buf.length() == 0) {
    rs_job_free(_M_Context->job);
    _M_Context->job = NULL;
    deltaFileBegin();
  }
}

void RsyncReceiveDialog::
deltaFileBegin()
{
  string tmp(parent().wp()->path() + _M_File);

  if (rs_build_hash_table(_M_Context->sumset) != RS_DONE) {
    lc.error("Cannot build rsync hashtable for %s", _M_File.c_str());
    parent().write(fex_header(ME_RsyncAbort));
    endDialog();
    return;
  }

  _M_Context->src_file = fopen(tmp.c_str(), "rb");
  if (! _M_Context->src_file) {
    lc.error("Could not open src_file %s for rsync (%s)",
	     _M_File.c_str(),
	     strerror(errno));
    parent().write(fex_header(ME_RsyncAbort));
    endDialog();
    return;
  }

  _M_Context->sb.wp      = &parent();
  _M_Context->sb.message = ME_RsyncDeltaBlock;

  _M_Context->job = rs_delta_begin(_M_Context->sumset);
  _M_Context->fb  = rs_filebuf_new(_M_Context->src_file, rs_inbuflen);
  deltaFileIter();
}

void RsyncReceiveDialog::
deltaFileIter()
{
  _M_Context->result = rs_job_drive(_M_Context->job, &_M_Context->rsbuf,
				    rs_infilebuf_fill, _M_Context->fb,
				    rs_outnetbuf_drain, &_M_Context->sb);

  if (_M_Context->result != RS_BLOCKED) {
    if (_M_Context->result != RS_DONE) {
      lc.error("error building delta blocks for %s (%s)",
	       _M_File.c_str(),
	       rs_strerror(_M_Context->result));
      parent().write(fex_header(ME_RsyncAbort));
    }
    else
      parent().write(fex_header(ME_RsyncDeltaEnd));

    endDialog();
  }
}

/***************************************************************************/

LinkDialog::
LinkDialog(ConnectedWatchPoint& wp, const string& file, const State& state)
  : ConnectedWatchPoint::Dialog(wp)
{
  _M_File    = file;
  _M_State   = state;
}

LinkDialog::
~LinkDialog()
{
}

void LinkDialog::
start()
{
  parent().write(fex_header(ME_GetLink), constbuf(_M_File));
}

void LinkDialog::
incoming_message(const fex_header &head, constbuf buf)
{
  switch(head.type) {
  case ME_LinkDest:
    {
      parent().wp()->remove(_M_File);
      string tmp1 = parent().wp()->path() + _M_File;
      string tmp2 = buf.data();
      ::symlink(tmp2.c_str(), tmp1.c_str());
      parent().wp()->changeAccess(_M_File, _M_State);
      lc.info("link created from %s to %s", tmp1.c_str(), tmp2.c_str()); 
    }
    endDialog();
    return;

  case ME_Reject:
    return;
  }

#ifndef NDEBUG
  if (head.type != ME_wavail)
    lc.info("LinkDialog didn't accept %s", message_str(head.type));
#endif

  ConnectedWatchPoint::Dialog::incoming_message(head, buf);
}


