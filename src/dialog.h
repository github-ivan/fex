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
#ifndef DIALOG_H
#define DIALOG_H

#include "watchpoint.h"
#include "serial.h"

/*
  A stack of message dialogs for a recursive communication
*/
class StackedDialog : public ConnectedWatchPoint::Dialog, 
		      public std::vector<ConnectedWatchPoint::Dialog*>
{
public:
  StackedDialog(ConnectedWatchPoint& wp);

  virtual
  ~StackedDialog();

protected:
  virtual void
  start();

  virtual void
  popUp();
};


/*
  A dialog to start sending synchronisation data of changed files
*/
class SyncSendDialog : public ConnectedWatchPoint::Dialog
{
public:
  SyncSendDialog(ConnectedWatchPoint& wp, bool as_client);

  virtual
  ~SyncSendDialog();


protected:
  virtual void
  start();

  void 
  incoming_message(const fex_header &head, nmstl::constbuf buf);

  virtual void
  popUp();


private:
  enum {
    SSD_Start,
    SSD_SendingSyncLog,
    SSD_WaitForComplete,
    SSD_Receive
  };

  bool _M_AsClient;
  int  _M_Mode;
};


/*
  A dialog to receive synchronsation data for changed files.
  It is the counterpart SyncSendDialog.
*/
class SyncReceiveDialog : public ConnectedWatchPoint::Dialog
{
public:
  SyncReceiveDialog(ConnectedWatchPoint& wp, bool as_client);

  virtual
  ~SyncReceiveDialog();

  void
  doSync();

protected:
  virtual void
  start();

  void 
  incoming_message(const fex_header &head, nmstl::constbuf buf);

  void
  popUp();

private:
  bool
  lock();
  
  void
  unlock();

  bool
  checkBackup(const std::string& path, State& state);

  bool   _M_AsClient;
  ModLog _M_Log;
};

/*
  A dialog to send filestates (file modification log) blockwise to the
  peer.
*/
class SendLogDialog : public ConnectedWatchPoint::Dialog
{
public:
  SendLogDialog(ConnectedWatchPoint& wp, int msg_type, ModLog* log);
  ~SendLogDialog();

  virtual void
  start();

  void 
  incoming_message(const fex_header &head, nmstl::constbuf buf);

private:
  std::stringstream             _M_msg;
  Serializer<std::stringstream> _M_writer;  
  ModLog::iterator              _M_iter;
  ModLog*                       _M_Log;
  int                           _M_msg_type;
};

#endif

/** EMACS **
 * Local variables:
 * mode: c++
 * End:
 */
