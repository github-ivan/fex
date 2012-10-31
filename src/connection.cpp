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
#include "connection.h"
#include "configfile.h"
#include "client.h"
#include "server.h"
#include "rsync.h"
#include "filelistener.h"
#include "serial.h"
#include <algorithm>
#include <fstream>
#include <signal.h>
#include <wait.h>
#include <zlib.h>


using namespace std;
using namespace nmstl;
using namespace log4cpp;

extern io_event_loop MainLoop;
extern char* version_string;


Connection::
Connection(io_event_loop& loop, iohandle ioh)
  : parent(loop, ioh, true)
{
  init();
  write(fex_header(ME_Start),
	constbuf(version_string, strlen(version_string) + 1));
  lc.notice("got connection (%x) from: %s", 
	    this,
	    get_socket().getpeername().as_string().c_str());
}

Connection::
Connection(io_event_loop& loop)
  : parent(loop)
{
  init();
}

void Connection::
init()
{
  _M_DownloadSpeed    = 0;
  _M_UploadSpeed      = 0;
  _M_CompressionLevel = 0;
  ConnectionPool::get().add_connection();
  set_owned(false);
}

Connection::
~Connection()
{
  lc.notice("Connection (%x) destroyed", this);
  ConnectionPool::get().remove_connection();

  WatchPoints_v::iterator i;
  for(i = _M_WatchPoints.begin(); i != _M_WatchPoints.end(); i++) {
    if (*i)
      delete *i;
  }
  _M_WatchPoints.clear();

  while(! _M_LockedFiles.empty()) {
    unlockFile(NULL, _M_LockedFiles.front().path);
  }
}


void Connection::
lockFile(ConnectedWatchPoint* wp, const string& path, char type)
{
  lock tmp;
  tmp.path = path;

  lock_v::iterator f = lower_bound(_M_LockedFiles.begin(),
				   _M_LockedFiles.end(),
				   tmp);

  if (f != _M_LockedFiles.end() && f->path == path)
    // no secod lock
    return;

  lc.info("try to lock %s for %s", 
	  path.c_str(), 
	  type == 'w' ? "writing" : "reading");

  tmp.fd = open(path.c_str(), O_RDWR);
  tmp.wp = wp->wp();

  struct flock fl;
  fl.l_type   = type == 'r' ? F_RDLCK : F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start  = 0;
  fl.l_len    = 0;
  fcntl(tmp.fd, F_SETLK,  &fl);

  _M_LockedFiles.insert(f, tmp);

  // notify all other watchpoints for the lock
  wp->wp()->notifyFileLock(path, type, NULL, wp);
}

void Connection::
unlockFile(ConnectedWatchPoint* wp, const string& path)
{
  lock tmp;
  tmp.path = path;
  
  lock_v::iterator f = lower_bound(_M_LockedFiles.begin(),
				   _M_LockedFiles.end(),
				   tmp);

  if (f != _M_LockedFiles.end() && f->path == path) {
    close(f->fd);
    f->wp->notifyFileLock(path, 'u', NULL, wp);
    _M_LockedFiles.erase(f);
    lc.info("unlock file %s", path.c_str());
  }
}


void Connection::
end_messages(unsigned int remaining)
{
  set_socket(nmstl::socket());
}

bool Connection::
write(const fex_header& head, nmstl::constbuf payload)
{ 
  if (_M_CompressionLevel > 0 && payload.length() > 1024) {
    fex_header h(head);
    h.type |= COMPRESS_BIT;

    uLongf size = compressBound(payload.length());
    _M_CompressionBuffer.resize(size + sizeof(size_t));
    char *pbuf = &_M_CompressionBuffer.front();

    *(size_t*)pbuf = payload.length();

    int result = compress2((Bytef*)pbuf + sizeof(uLongf),
			   &size,
			   (const Bytef*)payload.data(),
			   payload.length(),
			   _M_CompressionLevel);

    if (result == Z_OK)
      return parent::write(h, constbuf(pbuf, size + sizeof(size_t)));
  }
  return parent::write(head, payload); 
}

void Connection::
calcSpeed(const fex_header &head)
{
  if (! _M_TimerStart) {
    if (head.type == ME_RsyncDeltaBlock  ||
	head.type == ME_FullSyncLog      ||
	head.type == ME_RsyncSigBlock    ||
	head.type == ME_SyncLogBlock) {

      _M_TimerSize       = head.length + sizeof(head);
      _M_TimerWatchPoint = head.wp_id;
      _M_TimerStart      = ntime::now();
      return;
    }
  }
  else {
    _M_TimerSize += head.length + sizeof(head);

    if (head.wp_id != _M_TimerWatchPoint)
      return;

    if (head.type == ME_RsyncAbort     ||
	head.type == ME_RsyncDeltaEnd  ||
	head.type == ME_RsyncSigEnd    ||
	head.type == ME_FullSyncLogEnd ||
	head.type == ME_SyncLogEnd) {

      ntime diff = ntime::now() - _M_TimerStart;
      _M_TimerStart = ntime::none();
      if (_M_TimerSize <= 2 * MAX_COPY_SIZE)
	return;

      unsigned int speed;
      if (diff.to_msecs() > 0)
	speed = _M_TimerSize * 1000 / diff.to_msecs();
      else 
	speed = (unsigned long)-1;

      if (speed > 1000000)
	speed = 1000000;
      
      if (speed < (_M_DownloadSpeed * 8) / 10 ||
	  (_M_DownloadSpeed * 12) / 10 < speed) {
	lc.info("adjust speed from %i to %i", _M_DownloadSpeed, speed);
	int delta = speed - _M_DownloadSpeed;
	write(fex_header(ME_AdjustSpeed), constbuf(&delta, sizeof(delta)));
	_M_DownloadSpeed = speed;
      }
    }
  }
}

void Connection::
incoming_message(const fex_header &head, constbuf buf)
{
  if (head.type == ME_AdjustSpeed) {
    int delta = *(int*)buf.data();
    _M_UploadSpeed += *(int*)buf.data();

    if (_M_UploadSpeed < 1000000) {
      if (delta > 0 || _M_CompressionLevel == 0) {
	if (_M_CompressionLevel < 4)
	  _M_CompressionLevel = 4;
	else if (_M_CompressionLevel < 9) {
	  _M_CompressionLevel++;
	  lc.info("changed compression to %i", _M_CompressionLevel);
	}
      }
      else {
	if (_M_CompressionLevel > 4) {
	  _M_CompressionLevel--;
	  lc.info("changed compression to %i", _M_CompressionLevel);
	}
      }
    }
    else
      _M_CompressionLevel = 0;

    return;
  }

  fex_header ihead(head);
  constbuf   ibuf(buf);

  if (ihead.type & COMPRESS_BIT) {
    ihead.type &= ~COMPRESS_BIT;
    size_t size = *(size_t*)buf.data();
    _M_CompressionBuffer.resize(size);

    int result = 
      uncompress((Bytef*)&_M_CompressionBuffer.front(),
		 (uLongf*)&size,
		 (const Bytef*)(buf.data() + sizeof(uLongf)),
		 buf.length() - sizeof(uLongf));
    
    if (result != Z_OK) {
      lc.fatal("error in decompressing buffer: %i", result);
      set_socket(tcpsocket()); // disconnect
      return;
    }

    ibuf = constbuf(&_M_CompressionBuffer.front(),
		    _M_CompressionBuffer.size());
    ihead.length = ibuf.length();
  }

  calcSpeed(ihead);


  switch(ihead.type) {
  case ME_RegisterWatchPoint:
    registerWatchPoint(ihead.wp_id, buf);
    return;

  case ME_ClientKey:
    Configuration::get().ssh_add_key(buf.data());
    return;
  }

  if (_M_WatchPoints.size() > ihead.wp_id &&
      _M_WatchPoints[ihead.wp_id] != NULL)
    _M_WatchPoints[ihead.wp_id]->incoming_message(ihead, ibuf);
  else
    write(fex_header(ME_Reject, ihead.wp_id));
}

void Connection::
registerWatchPoint(size_t wp_id, constbuf buf) 
{
  _M_WatchPoints.resize(max(wp_id + 1, _M_WatchPoints.size()));
  assert(_M_WatchPoints[wp_id] == NULL);
  
  string request = buf;

  typedef Configuration::WatchPoint_v WatchPoint_v;
  const WatchPoint_v& wps = Configuration::get().watch_points();
  WatchPoint_v::const_iterator i;
  for(i = wps.begin(); i != wps.end(); i++) {
    string export_name = (*i)->export_name();
    if (export_name == request) {
      _M_WatchPoints[wp_id] = new ConnectedWatchPoint(MainLoop, 
						      *i, this, wp_id);
      write(fex_header(ME_Accept, wp_id));
      lc.notice("Watchpoint %s accepted from %s",
		request.c_str(),
		get_socket().getpeername().as_string().c_str());
      return;
    }
  }

  lc.notice("Watchpoint %s from %s rejected",
	    request.c_str(),
	    get_socket().getpeername().as_string().c_str());

  write(fex_header(ME_Reject, wp_id));
}


void Connection::
all_written()
{
  fex_header hd(ME_wavail);
  constbuf   buf;
  size_t     id;

  WatchPoints_v::iterator i;
  for(i = _M_WatchPoints.begin(); i != _M_WatchPoints.end(); i++) {
    hd.wp_id = id++;
    if (*i)
      (*i)->incoming_message(hd, buf);
  }
}


/***************************************************************************/
ClientConnection::
ClientConnection(io_event_loop& loop) : Connection(loop)
{ 
  _M_SSH = 0;
}

ClientConnection::
~ClientConnection()
{
  stop_ssh();
  ConnectionPool::get().remove_client_connection(this);
}

int ClientConnection::
connect(bool ssh, 
	const string& user, 
	const string& gw, 
	const string& server, 
	const string& port)
{
  if (is_connected())
    return connected;

  if (ssh && _M_SSH == 0) {
    string local_port = start_ssh(user, gw, server, port);
    if (local_port.empty())
      goto ConnectionFailed;

    _M_Address = inet_address("localhost", local_port);
    return ssh_started;
  }

  if (ssh) {
    assert(_M_SSH != 0);
    if (waitpid(_M_SSH, NULL, WNOHANG) == _M_SSH) {
      _M_SSH = 0;
      goto ConnectionFailed;
    }

    lc.info("try to connect to %s", _M_Address.as_string().c_str());
    tcpsocket socket(_M_Address);
    if (socket.getpeername()) {
      set_socket(socket, true);
      return connected;
    }

    stop_ssh();
  } 
  else {
    lc.info("try to connect to %s:%s", server.c_str(), port.c_str());
    _M_Address = inet_address(server, port);
    tcpsocket socket(_M_Address);
    if (socket.getpeername()) {
      set_socket(socket, true);
      return connected;
    }
  }

 ConnectionFailed:
  delete this;
  return failed;
}


static string 
findFreeListenPort()
{
  unsigned int port;
  for(port = 3025; port < 5000; port++) {
    inet_address addr("localhost", port);
    tcpsocket    sock(addr, tcpsocket::acceptor);
    if (sock.stat())
      break;
  }

  return to_string(port);
}

  
string ClientConnection::
start_ssh(const string& user, 
	  const string& gateway,
	  const string& server,
	  const string& port)
{
  uid_t uid = Configuration::get().find_user_id(user);
  string local_port = findFreeListenPort();
  string ssh_command = Configuration::get().ssh_command();

  _M_SSH = fork();
  if (_M_SSH < 0) {
    lc.error("cannot execute ssh");
    local_port.clear();
    return local_port;
  }

  if (_M_SSH == 0) {
    string port_arg(local_port + ":" + server + ":" + port);
    string host_arg(user + "@" + gateway);

    char* arglist[] = {
      "ssh",
      "-c",
      "blowfish",
      "-N",
      "-q",
      "-o",
      "StrictHostKeyChecking=no",
      "-L",
      const_cast<char*>(port_arg.c_str()),
      const_cast<char*>(host_arg.c_str()),
      NULL
    };

    lc.info("start ssh as user %i", uid);
    lc.info("%s -c blowfish -N -q -L %s %s",
	    ssh_command.c_str(), port_arg.c_str(), host_arg.c_str());
    setuid(uid);
    execvp(ssh_command.c_str(), arglist);
    exit(1);
  }

  lc.info("ssh started: %i", _M_SSH);
  return local_port;
}

void ClientConnection::
stop_ssh()
{
  if (_M_SSH > 0) {
    lc.info("stop ssh: %i", _M_SSH);
    ::kill(_M_SSH, SIGTERM);
    waitpid(_M_SSH, NULL, 0);
  }

  _M_SSH = 0;
}

void ClientConnection::
addWatchPoint(WatchPoint* wp, IDTranslator* translator, 
	      const string& import_name)
{
  size_t index = _M_WatchPoints.size();
  ClientWatchPoint* cwp = new ClientWatchPoint(MainLoop, wp, this,
					       index, translator);

  _M_WatchPoints.push_back(cwp);
  write(fex_header(ME_RegisterWatchPoint, index), constbuf(import_name));
}


void ClientConnection::
incoming_message(const fex_header &head, constbuf buf)
{
  if (head.type == ME_Start) {
    if (verifyServer(buf)) {
      write(fex_header(ME_ClientKey), 
	    constbuf(Configuration::get().ssh_key()));
    }
    return;
  }

  Connection::incoming_message(head, buf);
}

bool ClientConnection::
verifyServer(constbuf buf) 
{
  //compare only the first two version numbers
  int length = rindex(version_string, '.') - version_string;
  if (strncmp(buf.data(), version_string, length)) {
    lc.notice("server %s to old for connection", buf.data());
    set_socket(tcpsocket()); // disconnect
    return false;
  }
  else
    lc.info("server version %s is ok", buf.data());

  return true;
}

/***************************************************************************/

ConnectionPool::
ConnectionPool()
{ 
  _M_ConnectionCount = 0;
}

ConnectionPool::
~ConnectionPool()
{ 
}


void ConnectionPool::
start_listening()
{
  unsigned int port = atoi(Configuration::get().port().c_str());
  if (port) {
    typedef nmstl::tcp_acceptor<Connection> Acceptor;
    Acceptor* acc = new Acceptor(MainLoop, port);
    lc.notice("server is listening on port %i", port);
    acc->set_owned(false);
    // acc is owned by MainLoop!
  }
}


ConnectionPool&
ConnectionPool::
get()
{
  static ConnectionPool _S_Pool;
  return _S_Pool;
}


ClientConnection* ConnectionPool::
get_client_connection(const string& key)
{
  ClientConnection* con = _M_Clients[key];
  if (! con) 
    _M_Clients[key] = con = new ClientConnection(MainLoop);

  return con;
}

void ConnectionPool::
remove_client_connection(ClientConnection* con)
{
  ClientConnections_m::iterator i = _M_Clients.begin();
  for(; i != _M_Clients.end(); i++) {
    if (i->second == con) {
      _M_Clients.erase(i);
      break;
    }
  }
}

void ConnectionPool::
add_connection()
{
  if (_M_ConnectionCount++ == 0)
    FileListener::get().startLockPoll();
}
  
void ConnectionPool::
remove_connection()
{
  if (--_M_ConnectionCount == 0)
    FileListener::get().stopLockPoll();

  assert(_M_ConnectionCount >= 0);
}

