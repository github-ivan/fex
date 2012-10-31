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
#include "client.h"
#include "serial.h"
#include "rsync.h"
#include "configfile.h"
#include <fstream>

using namespace nmstl;
using namespace std;

namespace client
{

const unsigned char LAST_KEY_C[] = { 0xFF, 0 };
const string LAST_KEY((const char *) LAST_KEY_C);

/***************************************************************************/

FullSyncDialog::
FullSyncDialog(ConnectedWatchPoint& wp)
  : ConnectedWatchPoint::Dialog(wp)
{
  _M_RequireResync = false;
}

FullSyncDialog::
~FullSyncDialog()
{
}
  
void FullSyncDialog::
start()
{
  lc.info("start fullsync");
  parent().write(fex_header(ME_FullSyncStart));
  parent().wp()->createStateFile(this, &_M_ClientFile);
}
  
void FullSyncDialog::
incoming_message(const fex_header &head, constbuf buf)
{
  switch(head.type) {
  case ME_FullSyncState:
    {
      imessage msg(buf);
      size_t   size;

      _M_Mode = WaitForSyncData;

      msg >> _M_ServerFile >> size;

      State state;
      state.mode = 0666;
      state.mtime = time(NULL);

      string server = parent().wp()->path() + _M_ServerFile;
      string client = parent().wp()->path() + _M_ClientFile;

      ifstream in (client.c_str(), ios_base::binary|ios_base::in);
      ofstream out(server.c_str(), ios_base::binary|ios_base::out);
      out << in.rdbuf();
      in.close();
      out.close();
      pushDialog(new RsyncSendDialog(parent(), _M_ServerFile, state));
    }
    return;


  case ME_Reject:
    lc.error("server reported an error");
    parent().disconnect();
    endDialog();
    return;
  }

#ifndef NDEBUG
  if (head.type != ME_wavail)
    lc.info("FullSyncDialog(client) didn't accept %s", message_str(head.type));
#endif

  ConnectedWatchPoint::Dialog::incoming_message(head, buf);
}

  
void FullSyncDialog::
popUp() 
{
  if (_M_Mode == WaitForSyncData) {
    compareState();
    
    string client = parent().wp()->path() + _M_ClientFile;
    string server = parent().wp()->path() + _M_ServerFile;
    ::unlink(client.c_str());
    ::unlink(server.c_str());
    
    if (! _M_ServerLog.empty()) {
      _M_Mode = WaitForSendLogComplete;
      parent().pushSendLogDialog(ME_FullSyncLog, &_M_ServerLog);
      return;
    }
  }

  if (_M_Mode == WaitForSendLogComplete)
    parent().write(fex_header(ME_FullSyncLogEnd));

  parent().write(fex_header(ME_FullSyncComplete));
  if (_M_RequireResync) 
    parent().requireSync();

  endDialog();
  lc.info("end fullsync");
}


inline 
const State&
set(State& state, char action) 
{
  switch(action) {
  case 'N':
    if (S_ISDIR(state.mode))
      state.action = State::mkdired;
    else if (S_ISLNK(state.mode))
      state.action = State::newlink;
    else if (S_ISREG(state.mode))
      state.action = State::created;
    else 
      state.action = 0;
    break;

  case 'C':
    if (S_ISDIR(state.mode))
      state.action = State::mkdired;
    else if (S_ISLNK(state.mode))
      state.action = State::newlink;
    else if (S_ISREG(state.mode))
      state.action = State::changed;
    else 
      state.action = 0;
    break;

  case 'D':
    state.action = S_ISDIR(state.mode) ? State::rmdired : State::removed;
    break;

  case 'A':
    state.action = State::newaccess;
    break;
  }

  return state;
}

void FullSyncDialog::
compareState()
{
  typedef Serializer<ifstream> Serializer;

  string     server = parent().wp()->path() + _M_ServerFile;
  string     client = parent().wp()->path() + _M_ClientFile;
  string     lsynst = parent().wp()->state_dir() +  "/last-sync-state";
  string     key_client;
  string     key_server;
  string     key_lsynst;
  State      state_client;
  State      state_server;
  State      state_lsynst;
  bool       inc_client = true;
  bool       inc_server = true;
  bool       inc_lsynst = true;
  ifstream   in_client(client.c_str(), ios_base::binary|ios_base::in);
  ifstream   in_server(server.c_str(), ios_base::binary|ios_base::in);
  ifstream   in_lsynst(lsynst.c_str() , ios_base::binary|ios_base::in);
  Serializer reader_client(in_client);
  Serializer reader_server(in_server);
  Serializer reader_lsynst(in_lsynst);


  while(true) {
    if (inc_client) 
      reader_client.read(&key_client, &state_client);

    if (inc_server) {
      reader_server.read(&key_server, &state_server);
      parent().translateReceivedState(state_server);
    }

    if (inc_lsynst)
      reader_lsynst.read(&key_lsynst, &state_lsynst);

    if(! in_client.good())
      key_client = LAST_KEY;

    if (! in_server.good())
      key_server = LAST_KEY;

    if (! in_lsynst.good())
      key_lsynst = LAST_KEY;

    if (key_server == LAST_KEY && key_client == LAST_KEY)
      break;

    inc_client = inc_server = inc_lsynst = false;

    if (key_lsynst < key_client && key_lsynst < key_server) {
      // a deleted file just existing in the memory of last sync state
      inc_lsynst = true;
      continue;
    }

    if (key_client < key_server) {
      // key does not exist at server
      inc_client = true;

      if (key_lsynst == key_client) {
	// file is inside last sync state ==> file was deleted at server
	inc_lsynst = true;
	_M_ServerLog.insert(key_client, set(state_client, 'D'));
      }
      else
	addToLog(key_client, set(state_client, 'N'));

      continue;
    }

    if (key_server < key_client) {
      // key does not exist at client
      inc_server = true;

      if (key_lsynst == key_server) {
	// file is inside last sync state ==> file was deleted at client
	inc_lsynst = true;
	addToLog(key_server, set(state_server, 'D'));
      }
      else
	_M_ServerLog.insert(key_server, set(state_server, 'N'));

      continue;
    }

    assert(key_server == key_client);

    inc_client = inc_server = true;

    if (key_lsynst == key_client)
      inc_lsynst = true;
    else
      state_lsynst.mtime = 0;

    if (S_ISDIR(state_server.mode) && 
	S_ISDIR(state_client.mode))
      goto check_access;

    if (state_client.mtime > state_lsynst.mtime &&
	state_server.mtime > state_lsynst.mtime &&
	state_client.mtime != state_server.mtime) {

      // conflict
      char type = 'A';

      if (memcmp(state_client.md4, state_server.md4, 16)) {
	parent().wp()->backup(key_client);
	type = 'C';
      }

      if (type == 'A' &&
	  S_ISLNK(state_server.mode) && 
	  S_ISLNK(state_client.mode))
	continue;

      _M_ServerLog.insert(key_server, set(state_server, type));
      continue;
    }
     
    if (state_client.mtime > state_server.mtime) {
      char type = memcmp(state_client.md4, state_server.md4, 16) ? 'C' : 'A';

      if (type == 'A' &&
	  S_ISLNK(state_server.mode) && 
	  S_ISLNK(state_client.mode))
	continue;

      addToLog(key_client, set(state_client, type)); 
      continue;
    }

    if (state_server.mtime > state_client.mtime) {
      char type = memcmp(state_client.md4, state_server.md4, 16) ? 'C' : 'A';

      if (type == 'A' &&
	  S_ISLNK(state_server.mode) && 
	  S_ISLNK(state_client.mode))
	continue;

      _M_ServerLog.insert(key_server, set(state_server, type));
      continue;
    }

  check_access:
    if (state_client.mode != state_server.mode ||
	state_client.uid  != state_server.uid  ||
	state_client.gid  != state_server.gid) {

      if (state_client.ctime > state_server.ctime) 
	addToLog(key_client, set(state_client, 'A'));
      else 
	_M_ServerLog.insert(key_server, set(state_server, 'A'));
    }

    if (memcmp(&state_server.md4, &state_client.md4, 16)) {
      parent().wp()->backup(key_client);
      _M_ServerLog.insert(key_server, set(state_server, 'C'));
    }
  }
}


}
