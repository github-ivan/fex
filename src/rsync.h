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
#ifndef RSYNC_H
#define RSYNC_H

#include "watchpoint.h"
#include <fstream>

/*
  A dialog to start synchronisation of changed file using rsync.
*/
class RsyncSendDialog : public ConnectedWatchPoint::Dialog
{
public:
  struct Context;

  RsyncSendDialog(ConnectedWatchPoint& wp, 
		  const std::string& file, 
		  const State& state);

  virtual
  ~RsyncSendDialog();

protected:
  virtual void
  start();

  void 
  incoming_message(const fex_header &head, nmstl::constbuf buf);

private:
  void
  sendSigsBegin();

  void
  sendSigsIter();

  void
  sendSigsEnd();

  void
  patchFile(nmstl::constbuf buf);
 
  Context*    _M_Context;
  State       _M_State;
  std::string _M_File;
};


/*
  A dialog to receive rsync data to synchronize a file changed at the peer.
*/
class RsyncReceiveDialog : public ConnectedWatchPoint::Dialog
{
public:
  struct Context;

  RsyncReceiveDialog(ConnectedWatchPoint& wp);

  virtual
  ~RsyncReceiveDialog();

protected:
  void 
  incoming_message(const fex_header &head, nmstl::constbuf buf);

private:
  void
  buildSignatures(nmstl::constbuf buf);

  void
  deltaFileBegin();

  void
  deltaFileIter();

  Context*    _M_Context;
  std::string _M_File;
};


/*
  A dialog to receive the destination of a symlink.
 */
class LinkDialog : public ConnectedWatchPoint::Dialog
{
public:
  LinkDialog(ConnectedWatchPoint& wp, 
	     const std::string& file, 
	     const State& state);

  virtual
  ~LinkDialog();

protected:
  virtual void
  start();

  void 
  incoming_message(const fex_header &head, nmstl::constbuf buf);

private:
  State         _M_State;
  std::string   _M_File;
};



#endif

/** EMACS **
 * Local variables:
 * mode: c++
 * End:
 */
