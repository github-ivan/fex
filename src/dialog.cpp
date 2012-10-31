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
#include "dialog.h"
#include "rsync.h"
#include "configfile.h"

using namespace std;
using namespace nmstl;

StackedDialog::
StackedDialog(ConnectedWatchPoint& wp)
  : ConnectedWatchPoint::Dialog(wp)
{
}

StackedDialog::
~StackedDialog()
{
  iterator i;
  for(i = begin(); i != end(); i++) {
    delete *i;
  }
}

void StackedDialog::
popUp() 
{
  if (empty()) {
    endDialog();
    return;
  }

  ConnectedWatchPoint::Dialog* dialog = back();
  pop_back();
  pushDialog(dialog);
}

void StackedDialog::
start()
{
  popUp();
}

/***************************************************************************/

SyncSendDialog::
SyncSendDialog(ConnectedWatchPoint& wp, bool as_client)
  : ConnectedWatchPoint::Dialog(wp)
{
  _M_AsClient = as_client;
  _M_Mode     = SSD_Start;
}

SyncSendDialog::
~SyncSendDialog()
{
  lc.debug("SyncSendDialog end");
}


void SyncSendDialog::
start()
{
  parent().write(fex_header(ME_SyncStart));
  parent().setPendingSync(false);
  lc.debug("SyncSendDialog start");
}

void SyncSendDialog::
popUp()
{
  if (_M_Mode == SSD_SendingSyncLog) {
    parent().write(fex_header(ME_SyncLogEnd));
    _M_Mode = SSD_WaitForComplete;
    return;
  }

  if (_M_Mode == SSD_Receive) {
    _M_Mode = SSD_Start;
    parent().write(fex_header(ME_SyncStart));
  }
}

void SyncSendDialog::
incoming_message(const fex_header &head, constbuf buf)
{
  switch(head.type) {
  case ME_Reject:
    // the peer is not ready to receive the synchronisation data
    if (_M_Mode == SSD_WaitForComplete) {
      parent().undoSync();
      endDialog();
    }
    return;

  case ME_SyncStart:
    assert(_M_Mode == SSD_Start);
    if (_M_AsClient) {
      _M_Mode = SSD_Receive;
      pushDialog(new SyncReceiveDialog(parent(), true), head, buf);
      return;
    }
    break;

  case ME_SyncStartOk:
    _M_Mode = SSD_SendingSyncLog;
    parent().pushSendLogDialog(ME_SyncLogBlock, parent().sendLog());
    return;

  case ME_SyncComplete:
    parent().sendLog()->clear();
    parent().save_state();
    endDialog();
    return;

  case ME_RsyncStart:
    assert(_M_Mode == SSD_WaitForComplete);
    pushDialog(new RsyncReceiveDialog(parent()), head, buf);
    return;

  case ME_Backup:
    assert(_M_Mode == SSD_WaitForComplete);
    parent().wp()->backup(buf);
    return;

  case ME_GetLink:
    assert(_M_Mode == SSD_WaitForComplete);
    {
      char buffer[MAX_COPY_SIZE];
      string tmp = parent().wp()->path() + buf.data();
      int bytes = readlink(tmp.c_str(), buffer, sizeof(buffer));
      bytes = max((int)0, bytes);
      tmp.clear();
      tmp.append(buffer, bytes);
      parent().write(fex_header(ME_LinkDest), constbuf(tmp));
    }
    return;
  }

#ifndef NDEBUG
  if (head.type != ME_wavail)
    lc.info("SyncSendDialog didn't accept %s", message_str(head.type));
#endif

  ConnectedWatchPoint::Dialog::incoming_message(head, buf);
}


/***************************************************************************/

SyncReceiveDialog::
SyncReceiveDialog(ConnectedWatchPoint& wp, bool as_client)
  : ConnectedWatchPoint::Dialog(wp)
{
  _M_AsClient = as_client;
}

SyncReceiveDialog::
~SyncReceiveDialog()
{
  unlock();
  lc.debug("SyncReceiveDialog end");
}


void SyncReceiveDialog::
start()
{
  lc.debug("SyncReceiveDialog start");
}

void SyncReceiveDialog::
incoming_message(const fex_header &head, constbuf buf)
{
  switch(head.type) {
  case ME_SyncStart:
    parent().write(fex_header(ME_SyncStartOk));
    return;

  case ME_SyncLogBlock:
    parent().receiveLog(buf, &_M_Log);
    return;

  case ME_SyncLogEnd:
    doSync();
    return;
  }

  if (head.type == ME_Reject)
    return;

#ifndef NDEBUG
  if (head.type != ME_wavail)
    lc.info("SyncReceiveDialog didn't accept %s", message_str(head.type));
#endif

  ConnectedWatchPoint::Dialog::incoming_message(head, buf);
}


bool SyncReceiveDialog::
lock()
{
  ModLog::iterator i;
  for(i = _M_Log.begin(); i != _M_Log.end(); i++) {
    if (! FileListener::get().lock(parent().wp()->path() + i->first.str(), 
				   &parent())) {
      ModLog::iterator j;
      for(j = _M_Log.begin(); j != i; j++) {
	FileListener::get().unlock(parent().wp(), 
				   parent().wp()->path() + j->first.str(), 
				   j->second);
      }
      return false;
    }
  }
  return true;
}

void SyncReceiveDialog::
popUp() 
{
  unlock();
  parent().write(fex_header(ME_SyncComplete));
  parent().save_state();
  endDialog();
}

void SyncReceiveDialog::
unlock()
{
  ModLog::iterator i;
  for(i = _M_Log.begin(); i != _M_Log.end(); i++) {
    FileListener::get().unlock(parent().wp(), 
			       parent().wp()->path() + i->first.str(), 
			       i->second);
  }

  _M_Log.clear();
}


void SyncReceiveDialog::
doSync()
{
  if (! lock()) {
    parent().write(fex_header(ME_Reject));
    endDialog();
    return;
  }

  ModLog::iterator i;

  StackedDialog* dialog = new StackedDialog(parent());
  for(i = _M_Log.begin(); i != _M_Log.end(); i++) {
    if (! checkBackup(i->first, i->second))
      continue;

    State state;
    if (! _M_AsClient && ! parent().wp()->isWriteable(i->first, &state)) {
      lc.notice("Synchronisation denied: file %s is readonly", 
		i->first.str().c_str());

      switch(i->second.action) {
      case State::removed:  
	state.action = S_ISLNK(state.mode) 
	  ? State::newlink 
	  : State::created; 
	break;
      case State::newlink: 
      case State::created: state.action = State::removed; break;
      case State::mkdired: state.action = State::rmdired; break;
      case State::rmdired: state.action = State::mkdired; break;
      default: state.action = i->second.action; break;
      }
      parent().addToLog(i->first, state, 0, true);
      continue;
    }
    
    string path = parent().wp()->path() + i->first.str();

    switch(i->second.action) {
    case State::removed:
      lc.info("Sync remove file: %s", path.c_str());
      parent().wp()->remove(i->first);
      break;

    case State::newlink:
      lc.info("Sync newlink: %s", path.c_str());
      dialog->push_back(new LinkDialog(parent(), i->first, i->second));
      break;

    case State::newaccess:
      lc.info("Sync change access: %s", path.c_str());
      parent().wp()->changeAccess(i->first, i->second);
      break;

    case State::created:
      lc.info("Sync create file: %s", path.c_str());
      dialog->push_back(new RsyncSendDialog(parent(), 
					    i->first, 
					    i->second));
      break;

    case State::changed:
      lc.info("Sync change file: %s", path.c_str());
      dialog->push_back(new RsyncSendDialog(parent(), 
					    i->first, 
					    i->second));
      break;

    case State::mkdired:
      lc.info("Sync create dir: %s", path.c_str());
      parent().wp()->remove(i->first);
      parent().wp()->mkdir(i->first, i->second);
      break;

    case State::rmdired:
      lc.info("Sync remove dir: %s", path.c_str());
      parent().wp()->remove(i->first);
      break;

    case 0:
      break;

    default:
      lc.error("SyncReceiveDialog::doSync() wrong action: %i", i->second.action);
      assert(0);
    }
  }

  if (dialog->empty()) {
    delete dialog;
    popUp();
    return;
  }

  pushDialog(dialog);
}


bool SyncReceiveDialog::
checkBackup(const string& path, State& state)
{
  State *log_state = parent().findInLog(path);
  if (log_state == NULL)
    return true;

  // conflict 
  // peer an I changed the file at the same time
  switch(log_state->action) {
  case State::newaccess:
    // continue if just the rights changed
    state.uid   = log_state->uid;
    state.gid   = log_state->gid;
    state.mode  = log_state->mode;
    state.mtime = log_state->mtime;
    state.ctime = log_state->ctime;
    return true;

  case State::removed:
  case State::rmdired:
    if (state.action != log_state->action)
      parent().write(fex_header(ME_Backup), constbuf(path));
    return false;

  case State::created:
  case State::changed:
    if (state.action == State::removed) {
      parent().wp()->backup(path);
      return true;
    }

    if (memcmp(log_state->md4, state.md4, sizeof(state.md4))) {
      parent().write(fex_header(ME_Backup), constbuf(path));
      return false;
    }
    state.action = State::newaccess;
    return true;

  case State::mkdired:
    if (state.action != State::rmdired)
      parent().write(fex_header(ME_Backup), constbuf(path));

    return false;
  }

  return true;
}


/***************************************************************************/

SendLogDialog::
SendLogDialog(ConnectedWatchPoint& wp, int msg_type, ModLog* log)
  : ConnectedWatchPoint::Dialog(wp), _M_writer(_M_msg)
{
  _M_msg_type = msg_type;
  _M_Log      = log;
  _M_iter     = log->begin();
}

SendLogDialog::
~SendLogDialog()
{
}

void SendLogDialog::
start()
{
  while(_M_iter != _M_Log->end()) {
    State state(_M_iter->second);
    parent().translateSendState(state);

    _M_writer.write(_M_iter->first, state);

    if (_M_iter->second.action == State::rmdired) {
      // remove children 
      ModLog::iterator start = _M_iter;
      start++;
      ModLog::iterator j;
      for(j = start; j != _M_Log->end(); j++) {
	if (! _M_iter->first.isParentOf(j->first))
	  break;
      }
      
      _M_Log->erase(start, j);
    }

    _M_iter++;
    if (_M_msg.str().size() >= MAX_COPY_SIZE) {
      parent().write(fex_header(_M_msg_type), constbuf(_M_msg.str()));
      _M_msg.str(string());
      _M_writer.reset();

      if (parent().write_bytes_pending())
	return;
    }
  }

  write(fex_header(_M_msg_type), constbuf(_M_msg.str()));
  endDialog();
}

void SendLogDialog::
incoming_message(const fex_header &head, constbuf buf)
{
  if (head.type == ME_wavail) {
    start();
    return;
  }

#ifndef NDEBUG
  if (head.type != ME_wavail)
    lc.info("SendLogDialog didn't accept %s", message_str(head.type));
#endif

  ConnectedWatchPoint::Dialog::incoming_message(head, buf);
}

