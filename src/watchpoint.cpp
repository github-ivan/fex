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
#include "watchpoint.h"
#include "dialog.h"
#include "configfile.h"
#include "server.h"
#include "client.h"

using namespace std;
using namespace nmstl;

ConnectedWatchPoint::
ConnectedWatchPoint(io_event_loop& loop, 
		    WatchPoint* wp, 
		    Connection* con,
		    unsigned char id)
  : timer(loop)
{
  assert(wp != NULL);
  _M_Mode        = MO_start;
  _M_WatchPoint  = wp;
  _M_Connection  = con;
  _M_Id          = id;
  _M_WriteLog    = &_M_Log[0];
  _M_SendLog     = &_M_Log[1];
  _M_PendingSync = false;
  _M_WatchPoint->connect(this);
}

ConnectedWatchPoint::
~ConnectedWatchPoint()
{
  while (! _M_DialogStack.empty()) {
    delete _M_DialogStack.back();
    _M_DialogStack.pop_back();
  }

  _M_WatchPoint->disconnect(this);
}

void ConnectedWatchPoint::
file_changed(const string& key, 
	     const State& state, 
	     void* lock_id)
{
  addToLog(key, state, lock_id, true);
}

void ConnectedWatchPoint::
filelock_changed(const string& key, char locktype)
{
  switch(locktype) {
  case 'w':
    write(ME_CreateWriteLock, constbuf(key));
    return;

  case 'r':
    write(ME_CreateReadLock, constbuf(key));
    return;

  default:
    write(ME_ReleaseLock, constbuf(key));
  }
}


void ConnectedWatchPoint::
startSync()
{
  assert(_M_SendLog->empty());

  if (_M_WriteLog == &_M_Log[0]) {
    _M_WriteLog = &_M_Log[1];
    _M_SendLog  = &_M_Log[0];
  }
  else {
    _M_WriteLog = &_M_Log[0];
    _M_SendLog  = &_M_Log[1];
  }

  pushSendDialog();
  _M_PendingSync = false;
}

void ConnectedWatchPoint::
requireSync()
{
  if (! is_armed())
    arm(ntime::now_plus_secs(1));
}


void ConnectedWatchPoint::
fire()
{
  if (_M_Mode < MO_fullsynched)
    return;

  if (! _M_DialogStack.empty())
    _M_PendingSync = true;
  else
    startSync();
}

void ConnectedWatchPoint::
pushSendDialog()
{
  pushDialog(new SyncSendDialog(*this, false));
}

void ConnectedWatchPoint::
pushReceiveDialog(const fex_header &head, constbuf buf)
{
  pushDialog(new SyncReceiveDialog(*this, false), head, buf);
}


void ConnectedWatchPoint::
incoming_message(const fex_header &head, constbuf buf)
{
  switch(head.type) {
  case ME_CreateWriteLock:
    _M_Connection->lockFile(this, wp()->path() + buf.data(), 'w');
    return;

  case ME_CreateReadLock:
    _M_Connection->lockFile(this, wp()->path() + buf.data(), 'r');
    return;

  case ME_ReleaseLock:
    _M_Connection->unlockFile(this, wp()->path() + buf.data());
    return;
  }

  if (_M_DialogStack.empty()) {
    switch(head.type) {
    case ME_FullSyncStart:
      pushDialog(new server::FullSyncDialog(*this), head, buf);
      _M_Mode = MO_fullsynched;
      break;
      
    case ME_SyncStart:
      pushReceiveDialog(head, buf);
      break;

    case ME_Reject:
      lc.notice("Server rejected WatchPoint");
      break;

    case ME_Accept:
      pushDialog(new client::FullSyncDialog(*this));
      _M_Mode = MO_fullsynched;
      break;
    }
  }
  else
    _M_DialogStack.back()->incoming_message(head, buf);

  if (_M_DialogStack.empty() && _M_PendingSync)
    startSync();
}



void ConnectedWatchPoint::
addToLog(const string& key, 
	 const State& state, 
	 void* lock_id,
	 bool do_sync)
{
  if (lock_id == this)
    return;

  pair<ModLog::iterator, bool> res = _M_WriteLog->insert(key, state);

  if (! res.second) {
    State& org(res.first->second);
    int action = state.action;
    
    switch(state.action) {
    case State::newaccess:
      action = org.action;
      break;
      
    case State::changed:
      if (org.action == State::created)
	action = State::created;
      break;
    }
    
    memcpy(&org, &state, sizeof(state));
    org.action = action;
  }

  if (do_sync)
    requireSync();
}

State* ConnectedWatchPoint::
findInLog(const string& key)
{
  ModLog::iterator item;

  if ((item = _M_SendLog->find(key)) != _M_SendLog->end())
    return &item->second;
  
  if ((item = _M_WriteLog->find(key)) != _M_WriteLog->end())
    return &item->second;

  return NULL;
}


void ConnectedWatchPoint::
undoSync()
{
  _M_WriteLog->insert(_M_SendLog->begin(), _M_SendLog->end());
  _M_SendLog->clear();
  requireSync();
}


void ConnectedWatchPoint::
receiveLog(constbuf buf, ModLog* log)
{
  stringstream             in((string)buf);
  Serializer<stringstream> serializer(in);

  while(in.good()) {
    string key;
    State  state;
    if (serializer.read(&key, &state)) {
      string path = wp()->path() + key;

      if (! wp()->isValidPath(path)) {
	lc.notice("file %s is not valid", path.c_str());
	continue;
      }

      translateReceivedState(state);
      log->insert(key, state);
    }
  }
}

void ConnectedWatchPoint::
pushSendLogDialog(int msg_type, ModLog* log)
{
  pushDialog(new SendLogDialog(*this, msg_type, log));
}


void ConnectedWatchPoint::
pushDialog(Dialog* dialog)
{
  _M_DialogStack.push_back(dialog);
  dialog->start();
}

void ConnectedWatchPoint::
pushDialog(Dialog* dialog, const fex_header &head, constbuf buf)
{
  _M_DialogStack.push_back(dialog);
  dialog->start();
  dialog->incoming_message(head, buf);
}


void ConnectedWatchPoint::
popDialog()
{
  Dialog *back = NULL;

  Dialog* dialog = _M_DialogStack.back();
  _M_DialogStack.pop_back();

  delete dialog;
  if (! _M_DialogStack.empty()) 
    back = _M_DialogStack.back();
  
  if (back)
    back->popUp();
}


/***************************************************************************/

ClientWatchPoint::
ClientWatchPoint(nmstl::io_event_loop& loop, 
		 WatchPoint* wp,
		 Connection* con,
		 unsigned char id, 
		 IDTranslator* translator)
  : ConnectedWatchPoint(loop, wp, con, id)
{
  _M_Translator = translator;
}


ClientWatchPoint::
~ClientWatchPoint()
{
  wp()->arm(ntime::now());
}

void ClientWatchPoint::
save_state()
{
  if (_M_Mode >= MO_fullsynched)
    wp()->createStateFile(this, NULL);
}


void ClientWatchPoint::
pushSendDialog()
{
  pushDialog(new SyncSendDialog(*this, true));
}

void ClientWatchPoint::
pushReceiveDialog(const fex_header &head, constbuf buf)
{
  pushDialog(new SyncReceiveDialog(*this, true), head, buf);
}

void ClientWatchPoint::
translateReceivedState(State& state)
{ 
  assert(_M_Translator != NULL);
  state.uid = _M_Translator->getClientUid(state.uid);
  state.gid = _M_Translator->getClientGid(state.gid);
}

void ClientWatchPoint::
translateSendState(State& state)
{ 
  assert(_M_Translator != NULL);
  state.uid = _M_Translator->getServerUid(state.uid);
  state.gid = _M_Translator->getServerGid(state.gid);
}

