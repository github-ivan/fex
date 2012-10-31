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
#ifndef CONNECTION_H
#define CONNECTION_H

#include "filelistener.h"
#include "nmstl/serial"
#include "nmstl/netioevent"
#include <fstream>
#include <map>

const size_t MAX_COPY_SIZE = 1024 * 16;

#define COMPRESS_BIT 0x80

enum {
  ME_Start = 'A',
  ME_Reject,
  ME_Accept,
  ME_Backup,

  ME_RegisterWatchPoint,

  ME_FullSyncStart,  // client request states of server
  ME_FullSyncState,  // server sends states to client
  ME_FullSyncLog,    // client sends modification log to server
  ME_FullSyncLogEnd, // client ends sending modification log to server
  ME_FullSyncComplete,  // client sending server

  ME_SyncStart,     // peer I starts synchronisation
  ME_SyncStartOk,   // peer II acknowledges beginning of synchronisation
  ME_SyncLogBlock,  // peer I send Log block
  ME_SyncLogEnd,    // peer I ends sending log blocks
  ME_SyncComplete,  // peer II completed synchronisation

  ME_RsyncStart,
  ME_RsyncAbort,
  ME_RsyncSigBlock, 
  ME_RsyncSigEnd,
  ME_RsyncDeltaBlock,
  ME_RsyncDeltaEnd,

  ME_GetLink,
  ME_LinkDest,

  ME_ClientKey,

  ME_wavail,

  ME_AdjustSpeed,

  ME_CreateWriteLock,
  ME_CreateReadLock,
  ME_ReleaseLock
};

#ifndef NDEBUG
inline 
const char*
message_str(unsigned int type)
{
  switch(type) {
  case ME_Start : return "ME_Start";
  case ME_Reject: return "ME_Reject";
  case ME_Accept: return "ME_Accept";
  case ME_Backup: return "ME_Backup";

  case ME_RegisterWatchPoint: return "ME_RegisterWatchPoint";

  case ME_FullSyncStart   : return "ME_FullSyncStart";
  case ME_FullSyncState   : return "ME_FullSyncState";
  case ME_FullSyncLog     : return "ME_FullSyncLog";
  case ME_FullSyncLogEnd  : return "ME_FullSyncLogEnd";
  case ME_FullSyncComplete: return "ME_FullSyncComplete";

  case ME_SyncStart    : return "ME_SyncStart";
  case ME_SyncStartOk  : return "ME_SyncStartOk";
  case ME_SyncLogBlock : return "ME_SyncLogBlock";
  case ME_SyncLogEnd   : return "ME_SyncLogEnd";
  case ME_SyncComplete : return "ME_SyncComplete";

  case ME_RsyncStart   : return "ME_RsyncStart";
  case ME_RsyncAbort   : return "ME_RsyncAbort";
  case ME_RsyncSigBlock: return "ME_RsyncSigBlock,";
  case ME_RsyncSigEnd  : return "ME_RsyncSigEnd";

  case ME_RsyncDeltaBlock: return "ME_RsyncDeltaBlock";
  case ME_RsyncDeltaEnd  : return "ME_RsyncDeltaEnd";

  case ME_GetLink : return "ME_GetLink";
  case ME_LinkDest: return "ME_LinkDest";


  case ME_ClientKey: return "ME_ClientKey";

  case ME_wavail: return "ME_wavail";

  case ME_AdjustSpeed: return "ME_AdjustSpeed";


  case ME_CreateWriteLock: return "ME_CreateWriteLock";
  case ME_CreateReadLock: return "ME_CreateReadLock";
  case ME_ReleaseLock: return "ME_ReleaseLock";
  }
  assert(0);
}
#endif

/*
  The head of all fex messages.
*/

struct fex_header 
{
  unsigned char  type;
  unsigned char  wp_id;
  unsigned short length;

  fex_header() 
  {}
  
  // Allow implicit construction by type only
  fex_header(unsigned char type, 
	     unsigned char wp_id = 0,
	     unsigned short length = 0) 
    : type(type), wp_id(wp_id), length(length) 
  { }

  NMSTL_SIMPLY_SERIALIZABLE(fex_header, << type << wp_id << length);
};


class ConnectionPool;
class ConnectedWatchPoint;
class ClientWatchPoint;


/*
  An ip connection. A Connection handles all (Connected)Watchpoints,
  which are adressing the same peer.
*/

class Connection : protected nmstl::msg_handler<fex_header>
{
public:
  typedef std::vector<char>              buffer_t;
  typedef nmstl::msg_handler<fex_header> parent;

  parent::write_bytes_pending;
  parent::set_owned;
  parent::set_socket;
  parent::is_connected;
  parent::is_owned;

  Connection(nmstl::io_event_loop& loop, 
	     nmstl::iohandle ioh);
  Connection(nmstl::io_event_loop& loop);
  virtual 
  ~Connection();

  bool 
  write(const fex_header& head) 
  { return parent::write(head); }

  bool
  write(const fex_header& head, nmstl::constbuf payload);

  ConnectionPool& 
  listener();

  void
  lockFile(ConnectedWatchPoint* wp, const std::string& path, char type);

  void
  unlockFile(ConnectedWatchPoint* wp, const std::string& path);


protected:
  typedef std::vector<ConnectedWatchPoint*> WatchPoints_v;

  virtual void 
  end_messages(unsigned int remaining);

  virtual void 
  incoming_message(const fex_header &head, nmstl::constbuf buf);

  virtual void 
  all_written();

  WatchPoints_v _M_WatchPoints;


private:
  struct lock
  {
    std::string path;
    int         fd;
    WatchPoint* wp;

    bool operator<(const lock& l) const
    { return path < l.path; }
  };

  typedef std::vector<lock> lock_v;

  void
  calcSpeed(const fex_header &head);

  void
  init();

  void
  sendWatchPoints();

  void
  registerWatchPoint(size_t wp_id, nmstl::constbuf buf);

  size_t              _M_DownloadSpeed;
  size_t              _M_UploadSpeed;
  nmstl::ntime        _M_TimerStart;
  size_t              _M_TimerSize;
  size_t              _M_TimerWatchPoint;
  int                 _M_CompressionLevel;
  buffer_t            _M_CompressionBuffer;
  lock_v              _M_LockedFiles;
};


class IDTranslator;

/*
  The Client side of a connection.
*/ 
class ClientConnection : public Connection
{
public:
  enum { failed, ssh_started, connected };

  ClientConnection(nmstl::io_event_loop& loop);
  ~ClientConnection();

  int
  connect(bool ssh, 
	  const std::string& user, 
	  const std::string& gateway, 
	  const std::string& server,
	  const std::string& port);


  void
  addWatchPoint(WatchPoint* wp, IDTranslator* translator, 
		const std::string& import_name);

private:
  virtual void 
  incoming_message(const fex_header &head, nmstl::constbuf buf);

  std::string
  start_ssh(const std::string& user, 
	    const std::string& gateway,
	    const std::string& server,
	    const std::string& port);
    
  void
  stop_ssh();

  bool
  verifyServer(nmstl::constbuf buf);

  pid_t               _M_SSH;
  nmstl::inet_address _M_Address;
};


/*
  A container for all active Connections
*/

class ConnectionPool
{
public:
  typedef std::map<std::string, ClientConnection*> ClientConnections_m;

  static
  ConnectionPool&
  get();

  ClientConnection*
  get_client_connection(const std::string& key);

  void
  remove_client_connection(ClientConnection* con);

  void
  add_connection();

  void
  remove_connection();

  void
  start_listening();

private:
  ConnectionPool();
  ~ConnectionPool();
  
  int                 _M_ConnectionCount;
  ClientConnections_m _M_Clients;
};

#endif

/** EMACS **
 * Local variables:
 * mode: c++
 * End:
 */
