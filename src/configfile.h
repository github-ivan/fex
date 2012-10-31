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
#ifndef CONFIGFILE_H
#define CONFIGFILE_H

#include "modlog.h"
#include <string>
#include <vector>
#include <map>
#include <nmstl/ioevent>

/*

   IDTranslator is a map for translating client user ids to server 
   user ids and vice versa

*/
class IDTranslator
{
public:
  typedef uid_t id_t;

  id_t 
  getServerUid(id_t client) const
  { id_m::const_iterator f = _M_UserClientToServer.find(client);
    return f != _M_UserClientToServer.end() ? f->second : client; }

  id_t 
  getClientUid(id_t server) const
  { id_m::const_iterator f = _M_UserServerToClient.find(server);
    return f != _M_UserServerToClient.end() ? f->second : server; }

  id_t 
  getServerGid(id_t client) const
  { id_m::const_iterator f = _M_GroupClientToServer.find(client);
    return f != _M_GroupClientToServer.end() ? f->second : client; }

  id_t 
  getClientGid(id_t server) const
  { id_m::const_iterator f = _M_GroupServerToClient.find(server);
    return f != _M_GroupServerToClient.end() ? f->second : server; }

  void
  addUid(id_t server, id_t client)
  { _M_UserClientToServer[client] = server; 
    _M_UserServerToClient[server] = client; }

  void
  addGid(id_t server, id_t client)
  { _M_GroupClientToServer[client] = server; 
    _M_GroupServerToClient[server] = client; }

  size_t
  uid_size() const
  { return _M_UserClientToServer.size(); }

  size_t
  gid_size() const
  { return _M_GroupClientToServer.size(); }


private:
  typedef std::map<id_t, id_t> id_m;
  
  id_m _M_UserClientToServer;
  id_m _M_UserServerToClient;
  id_m _M_GroupClientToServer;
  id_m _M_GroupServerToClient;
};


class ConnectedWatchPoint;
class ClientWatchPoint;

/*
  A WachtPoint as described in the configuration file.  It gets all
  file events and relays them to all ConnectedWatchoints
*/
class WatchPoint : public StateLog, public nmstl::timer
{
public:
  struct Import
  {
    bool          ssh;
    std::string   server;
    std::string   gateway;
    std::string   name;
    std::string   user;
    std::string   port;
    IDTranslator* translator;
  };

  typedef std::vector<Import> Import_v;
  typedef std::vector<std::string> string_v;

  WatchPoint();
  WatchPoint(const WatchPoint& wp);

  void
  replacePathToName(std::string& path);

  bool 
  replaceNameToPath(std::string& path);

  virtual bool
  isValidPath(const std::string& path) const;

  const std::string&
  path() const
  { return _M_Path; }

  const std::string&
  export_name() const
  { return _M_Export; }

  const std::string&
  state_dir() const
  { return _M_StateDir; }

  void
  backup(const std::string& path);

  void
  notifyFileLock(const std::string& path, 
		 char locktype, 
		 ConnectedWatchPoint* only = NULL,
		 ConnectedWatchPoint* but = NULL) const;

  bool
  isWriteable(const std::string& path, State* state = NULL) const;

  void
  changeAccess(const std::string& path, State& state) const;

  void
  remove(const std::string& path) const;

  void 
  mkdir(const std::string& path, State& state) const;

  const Import_v& 
  imports() const
  { return _M_Imports; }

  void
  validateValues();

  size_t
  createStateFile(void* id, std::string* filename) const;

  const std::string&
  tmp_dir() const
  { return _M_TmpDir; }

  void
  connect(ConnectedWatchPoint* sink);

  void
  disconnect(ConnectedWatchPoint* sink)
  { _M_Sinks.erase(sink); }
  

protected:
  virtual void
  change(const Path& path, const State& state);


private:
  typedef std::set<ConnectedWatchPoint*> sink_set;

  virtual void 
  fire();

  std::string  _M_StateDir;
  std::string  _M_TmpDir;
  std::string  _M_Path;
  std::string  _M_Export;
  bool         _M_Readonly;
  Import_v     _M_Imports;
  size_t       _M_ImportToInspect;
  string_v     _M_Excludes;
  string_v     _M_Includes;
  sink_set     _M_Sinks;
  nmstl::ntime _M_NextTry;
  unsigned int _M_Timeout;
  
  friend class Configuration;
};


/*
  Container of all configuration entries
 */

class Configuration
{
public:
  typedef std::vector<WatchPoint*> WatchPoint_v;
  
  void
  parse(const char* file);

  const WatchPoint_v&
  watch_points()
  { return _M_WatchPoints; }

  const std::string& 
  user() const
  { return _M_User; }

  const std::string&
  ssh_command() const
  { return _M_SSHCommand; }

  const std::string
  port() const
  { return _M_Port; }

  uid_t
  find_user_id(const std::string& user) const;

  void
  ssh_add_key(const char* key);

  const std::string&
  ssh_key() const
  { return _M_SSHKey; }

  static
  Configuration&
  get();

  IDTranslator&
  translator(const std::string& id)
  { return _M_Translators[id]; }
  

private:
  typedef std::map<std::string, IDTranslator> IDTranslator_m;

  Configuration();
  ~Configuration();

  void
  check_user();

  static Configuration* _S_Configuration;

  WatchPoint_v   _M_WatchPoints;
  std::string    _M_Port;
  std::string    _M_User;
  std::string    _M_UserHome;
  std::string    _M_SSHKey;
  std::string    _M_SSHCommand;
  bool           _M_AcceptKeys;
  bool           _M_CreateUser;
  IDTranslator_m _M_Translators;
};



#endif

/** EMACS **
 * Local variables:
 * mode: c++
 * End:
 */
