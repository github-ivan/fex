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
#include "configfile.h"
#include "filelistener.h"
#include "watchpoint.h"
#include "serial.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <fnmatch.h>
#include <assert.h>
#include <confuse.h>
#include <utime.h>
#include <dirent.h>
#include <pwd.h>

using namespace std;
using namespace nmstl;

extern io_event_loop MainLoop;

static
string 
check_keys(const string& dir, uid_t uid)
{
  assert(uid != 0);

  stringstream ssh_dir;
  ssh_dir << dir << "/.ssh";
  mkdir(ssh_dir.str().c_str(), 0700);
  chown(ssh_dir.str().c_str(), uid, (gid_t)-1);

  ssh_dir << "/id_rsa.pub";

  for(int i = 0; i < 2; i++) {
    ifstream in(ssh_dir.str().c_str());
    string   key;

    in >> noskipws;
    while(in.good()) {
      char buffer[1024];
      if (! in.getline(buffer, sizeof(buffer)).good())
	break;

      key += buffer;
    }

    if (key.empty()) {
      stringstream cmd;
      cmd << "ssh-keygen"
	  << " -f" << dir << "/.ssh/id_rsa"
	  << " -q"
	  << " -t rsa"
	  << " -N \"\"";

      if (system(cmd.str().c_str()) != 0)
	lc.error("ssh-keygen command failed: %s", strerror(errno));
      else {
	chown(ssh_dir.str().c_str(), uid, (gid_t)-1);
	string tmp(dir + "/.ssh/id_rsa");
  	chown(tmp.c_str(), uid, (gid_t)-1);
      }

      continue;
    }
    return key;
  }

  return string();
}


static 
void 
rmtree(string& full_path)
{
  int length = full_path.length();
  DIR* dirf = opendir(full_path.c_str());
  if (dirf) {
    struct dirent *item;

    while(item = readdir(dirf)) {
      if (item->d_name[0] == '.' && item->d_name[1] == '.' ||
	  item->d_name[0] == '.' && item->d_name[1] == 0)
	continue;

      full_path.erase(length);
      full_path += item->d_name;

      struct stat buf;
      lstat(full_path.c_str(), &buf);
      if (S_ISDIR(buf.st_mode)) {
	full_path += "/";
	rmtree(full_path);
      }
      else 
	unlink(full_path.c_str());
    }

    closedir(dirf);
  }
	
  full_path.erase(length);
  ::remove(full_path.c_str());
}

static
bool
mktree(const string& full_path, int mode=0751)
{
  size_t pos = full_path.find('/', 1);
  for(; pos != string::npos; pos = full_path.find('/', pos + 1)) {
    string dir(full_path.substr(0, pos));
    ::mkdir(dir.c_str(), mode);
  }

  return ::mkdir(full_path.c_str(), mode) == 0;
}


/***************************************************************************/

WatchPoint::
WatchPoint() : timer(MainLoop)
{
  _M_ImportToInspect = 0;
}

WatchPoint::
WatchPoint(const WatchPoint& wp) : timer(MainLoop)
{
  _M_Path            = wp._M_Path;
  _M_Export          = wp._M_Export;
  _M_Imports         = wp._M_Imports;
  _M_Excludes        = wp._M_Excludes;
  _M_Includes        = wp._M_Includes;
  _M_ImportToInspect = 0;
}

bool WatchPoint::
isValidPath(const string& path) const
{
  if (path.find("/.fextmp") != string::npos)
    return false;

  string_v::const_iterator i;
  for(i = _M_Includes.begin(); i != _M_Includes.end(); i++) {
    if (fnmatch(i->c_str(), path.c_str(), 0) == 0)
      return true;
  }

  for(i = _M_Excludes.begin(); i != _M_Excludes.end(); i++) {
    if (fnmatch(i->c_str(), path.c_str(), 0) == 0)
      return false;
  }

  return true;
}

void WatchPoint::
change(const Path& path, const State& state)
{
  void* lock_id = FileListener::get().notifyChange(this, path, state);
  string p(path);
  p = p.substr(_M_Path.length());
  sink_set::iterator i;
  for(i = _M_Sinks.begin(); i != _M_Sinks.end(); i++) {
    (*i)->file_changed(p, (State::State&)state, lock_id);
  }
}

void WatchPoint::
backup(const string& path)
{
  StateLog::backup(_M_Path + path);
}


void WatchPoint::
notifyFileLock(const string& path, 
	       const char locktype, 
	       ConnectedWatchPoint* only,
	       ConnectedWatchPoint* but) const
{
  string p = path.substr(_M_Path.length());

  if (only) {
    only->filelock_changed(p, locktype);
    return;
  }

  sink_set::const_iterator i;
  for(i = _M_Sinks.begin(); i != _M_Sinks.end(); i++) {
    if (*i != but)
      (*i)->filelock_changed(p, locktype);
  }
}

void WatchPoint::
connect(ConnectedWatchPoint* sink)
{ 
  _M_Sinks.insert(sink); 
  FileListener::get().resendFileLocks(this, sink);
}


static bool
is_dir(const string& test) 
{
  struct stat buf;
  if (lstat(test.c_str(), &buf) == 0)
    return S_ISDIR(buf.st_mode);
  
  return false;
}



bool WatchPoint::
isWriteable(const string& path, State* state) const
{
  string full_path = _M_Path + path;

  struct stat buf;
  lstat(full_path.c_str(), &buf);

  if (state) {
    state->uid    = buf.st_uid;
    state->gid    = buf.st_gid;
    state->mode   = buf.st_mode;
    state->mtime  = buf.st_mtime;
    state->ctime  = buf.st_ctime;
    state->size   = buf.st_size;
    state->action = State::changed;
  }

  return ! _M_Readonly 
    && (buf.st_uid != 0 
	|| (buf.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)));
}

void WatchPoint::
remove(const string& path) const
{
  string full_path = _M_Path + path;
  ::remove(full_path.c_str());
  full_path += "/";
  rmtree(full_path);
}

void WatchPoint::
mkdir(const string& path, State& state) const
{
  string full_path = _M_Path + path;
  if (::mkdir(full_path.c_str(), state.mode) < 0)
    lc.error("could not mkdir %s: %s", full_path.c_str(), strerror(errno));
  
  changeAccess(path, state);
}


void WatchPoint::
changeAccess(const string& path, State& state) const
{
  string full_path = _M_Path + path;

  chmod(full_path.c_str(), state.mode);
  chown(full_path.c_str(), state.uid, state.gid);
  struct utimbuf buf;
  buf.actime = buf.modtime = state.mtime;
  utime(full_path.c_str(), &buf);
}

void WatchPoint::
validateValues()
{
  if (mktree(_M_Path))
    lc.notice("%s was created", _M_Path.c_str());

  if (! is_dir(_M_Path)) {
    lc.fatal("%s is not a directory", _M_Path.c_str());
    exit(1);
  }

  // TmpDir
  _M_TmpDir = "/.fextmp";
  remove(_M_TmpDir);
  _M_TmpDir = _M_Path + _M_TmpDir + "/";
  ::mkdir(_M_TmpDir.c_str(), 0700);

  // StateDir
  string subdir = _M_Path;
  string::iterator i;
  for(i = subdir.begin(); i != subdir.end(); i++) {
    if (*i == '/')
      *i = '_';
  }

  _M_StateDir = FEX_STATE"/" + subdir;
  mktree(_M_StateDir);
}

void WatchPoint::
fire()
{
  size_t size = _M_Imports.size();

  if (ntime::now_plus_secs(-20) > _M_NextTry)
    _M_Timeout = 20;

  for(; _M_ImportToInspect < size; _M_ImportToInspect++) {
    const Import& import = _M_Imports[_M_ImportToInspect];
    string key = (import.user + '@' + import.gateway 
		  + '/' + import.server + ':' + to_string(import.port));

    ClientConnection* con = ConnectionPool::get().get_client_connection(key);

    int state = con->connect(import.ssh, import.user, import.gateway, 
			     import.server, import.port);

    _M_NextTry = ntime::now_plus_secs(_M_Timeout);
      
    switch(state) {
    case ClientConnection::failed:
      lc.info("connection failed");
      continue;
      
    case ClientConnection::ssh_started:
      // two stage connect for ssh, because ssh needs some
      // time for setup
      arm(ntime::now_plus_secs(10));
      return;

    case ClientConnection::connected:
      lc.notice("connection established");
      con->addWatchPoint(this, import.translator, import.name);
      _M_ImportToInspect++;
      return;
	
    default:
      assert(0);
    }
  }

  _M_ImportToInspect = 0;
  lc.info("try reconnect in %i seconds", _M_Timeout);
  _M_NextTry = ntime::now_plus_secs(_M_Timeout);
  _M_Timeout += 20;
  _M_Timeout = min(_M_Timeout, (unsigned int)60 * 10); // timeout of 10 minutes
  arm(_M_NextTry);
}


size_t WatchPoint::
createStateFile(void* id, string* filename) const
{
  string path;

  if (filename) {
    stringstream str;
    str << tmp_dir() << ".fex-state-" << getpid() << "-" << id;
    path = str.str();
    *filename = path.substr(_M_Path.length());
  }
  else
    path = state_dir() + "/last-sync-state";

  ofstream out(path.c_str(), ios_base::binary|ios_base::out);
  
  if (! out.good()) {
    lc.error("could not create %s", path.c_str());
    return 0;
  }
  
  typedef Serializer<ostream> Serializer;
  Serializer serializer(out);

  for(const_iterator i = begin(); i != end(); i++) {
    string key = i->first.str();
    serializer.write(key.substr(_M_Path.length()), i->second);
  }

  size_t size = out.tellp();
  out.close();
  
  return size;
}



/***************************************************************************/

Configuration::
Configuration()
{
  _M_Port       = "3025";
  _M_User       = "fex";
  _M_AcceptKeys = true;
  _M_CreateUser = true;
}

Configuration::
~Configuration()
{
  WatchPoint_v::iterator i;
  for(i = _M_WatchPoints.begin(); i != _M_WatchPoints.end(); i++) {
    // start monitoring the Watchpoint
    delete *i;
  }
}


int 
cb_gid_replace(cfg_t *cfg, cfg_opt_t *opt, int argc, const char **argv)
{
  int i;
  
  if(argc != 2) {
    cfg_error(cfg, "%s needs 2 parameters",  opt->name);
    return -1;
  }
  
  IDTranslator& trans = Configuration::get().translator(cfg->title);
  IDTranslator::id_t client = atoi(argv[0]);
  IDTranslator::id_t server = atoi(argv[1]);
  trans.addGid(server, client);
  return 0;
}

int 
cb_uid_replace(cfg_t *cfg, cfg_opt_t *opt, int argc, const char **argv)
{
  if(argc != 2) {
    cfg_error(cfg, "%s needs 2 parameters",  opt->name);
    return -1;
  }
  
  IDTranslator& trans = Configuration::get().translator(cfg->title);
  IDTranslator::id_t client = atoi(argv[0]);
  IDTranslator::id_t server = atoi(argv[1]);
  trans.addUid(server, client);
  return 0;
}

static
cfg_opt_t translate_opts[] = {
  CFG_FUNC("gid", &cb_gid_replace),
  CFG_FUNC("uid", &cb_uid_replace),
  CFG_END()
};

static
cfg_opt_t import_opts[] = {
  CFG_BOOL("ssh"      , cfg_false, CFGF_NONE),
  CFG_STR ("server"   , ""       , CFGF_NONE),
  CFG_STR ("gateway"  , ""       , CFGF_NONE),
  CFG_STR ("user"     , "fex"    , CFGF_NONE),
  CFG_STR ("name"     , ""       , CFGF_NONE),
  CFG_STR ("translate", ""       , CFGF_NONE),
  CFG_STR ("port"     , "3025"   , CFGF_NONE),
  CFG_END()
};


static
cfg_opt_t watchpoint_opts[] = {
  CFG_SEC     ("import"  , import_opts, CFGF_MULTI),
  CFG_BOOL    ("readonly", cfg_false  , CFGF_NONE),
  CFG_STR     ("export"  , ""         , CFGF_NONE),
  CFG_STR_LIST("exclude" , ""         , CFGF_NONE),
  CFG_STR_LIST("include" , ""         , CFGF_NONE),
  CFG_END()
};


static
cfg_opt_t opts[] = {
  CFG_STR ("port"          , "3025"         , CFGF_NONE),
  CFG_STR ("ssh_command"   , "/usr/bin/ssh" , CFGF_NONE),
  CFG_STR ("ssh_user"      , "fex"          , CFGF_NONE),
  CFG_BOOL("accept_keys"   , cfg_true       , CFGF_NONE),
  CFG_BOOL("create_user"   , cfg_true       , CFGF_NONE),
  CFG_SEC ("watchpoint"    , watchpoint_opts, CFGF_MULTI | CFGF_TITLE),
  CFG_SEC ("translate"     , translate_opts , CFGF_MULTI | CFGF_TITLE),
  CFG_FUNC("include"       , &cfg_include),
  CFG_END()
};

  
void Configuration::
parse(const char* file)
{
  cfg_t *cfg = cfg_init(opts, CFGF_NOCASE);

  int ret = cfg_parse(cfg, file);
  if(ret == CFG_FILE_ERROR) {
    perror(file);
    exit(1);
  } else if(ret == CFG_PARSE_ERROR) {
    cerr << "parse error" << endl;
    exit(1);
  }

  _M_Port         = cfg_getstr (cfg, "port");
  _M_SSHCommand   = cfg_getstr (cfg, "ssh_command");
  _M_User         = cfg_getstr (cfg, "ssh_user");
  _M_AcceptKeys   = cfg_getbool(cfg, "accept_keys");
  _M_CreateUser   = cfg_getbool(cfg, "create_user");

  size_t n = cfg_size(cfg, "watchpoint");
  for(size_t i = 0; i < n; i++) {
    WatchPoint *tmp = new WatchPoint();
    cfg_t*     wp = cfg_getnsec(cfg, "watchpoint", i);

    tmp->_M_Path         = cfg_title  (wp);
    tmp->_M_Export       = cfg_getstr (wp, "export");
    tmp->_M_Readonly     = cfg_getbool(wp, "readonly");

    size_t m = cfg_size(wp, "import");
    for(size_t j = 0; j < m; j++) {
      cfg_t* imp       = cfg_getnsec(wp,  "import", j);
      string translate = cfg_getstr (imp, "translate");

      WatchPoint::Import import;
      import.ssh        = cfg_getbool(imp, "ssh");
      import.server     = cfg_getstr (imp, "server");
      import.user       = cfg_getstr (imp, "user");
      import.gateway    = cfg_getstr (imp, "gateway");
      import.name       = cfg_getstr (imp, "name");
      import.port       = cfg_getstr (imp, "port");
      import.translator = &translator(translate);
      if (import.gateway.empty())
	import.gateway = import.server;

      tmp->_M_Imports.push_back(import);
    }

    m = cfg_size(wp, "exclude");
    for(size_t j = 0; j < m; j++) {
      tmp->_M_Excludes.push_back(cfg_getnstr(wp, "exclude", j));
    }

    m = cfg_size(wp, "include");
    for(size_t j = 0; j < m; j++) {
      tmp->_M_Includes.push_back(cfg_getnstr(wp, "include", j));
    }

    _M_WatchPoints.push_back(tmp);
    _M_WatchPoints.back()->validateValues();
  }

  WatchPoint_v::iterator i;
  for(i = _M_WatchPoints.begin(); i != _M_WatchPoints.end(); i++) {
    // start monitoring the Watchpoint
    (*i)->changeDB((*i)->path());

    if (! (*i)->_M_Imports.empty())
      (*i)->arm(ntime::now());
  }

  check_user();
}


Configuration& Configuration::
get()
{
  static Configuration _S_Configuration;
  return _S_Configuration;
}


uid_t Configuration::
find_user_id(const string& user) const
{
  struct passwd *pw = getpwnam(user.c_str());
  return pw ? pw->pw_uid : 0;
}


void Configuration::
check_user()
{
  uid_t          user_id;
  struct passwd* pw;
  
 check_user_again:
  pw = getpwnam(_M_User.c_str());
  
  if (pw == NULL) {
    if (_M_CreateUser) {
      ::mkdir(FEX_STATE"/users", 0751);
      _M_UserHome = FEX_STATE"/users/" + _M_User;
      ::mkdir(_M_UserHome.c_str(), 0751);

      for(user_id = 50; user_id < 1000; user_id++) {
	if (getpwuid(user_id) == NULL)
	  break;
      }

      stringstream cmd;
      cmd << "useradd "
	  << " -d " << _M_UserHome
	  << " -s /bin/false"
	  << " -g 0"
	  << " -p $1$5yB4oJiU$PFWifMMb5vVCJ1yagV3rc1";

      if (user_id < 1000)
	cmd << " -u " << user_id;

      cmd << " " << _M_User;

      string db = cmd.str().c_str();
      const char *s = db.c_str();

      if (system(cmd.str().c_str()) != 0)
	lc.error("useradd command failed: %s", strerror(errno));
      
      _M_CreateUser = false;
      goto check_user_again;
    }
    else {
      lc.notice("user %s does not exist=> Key exchange disabled",
		_M_User.c_str());
     _M_AcceptKeys = false;
    }
  }
  else {
    _M_UserHome = pw->pw_dir;
    _M_SSHKey = check_keys(_M_UserHome, pw->pw_uid);
  }
}

void Configuration::
ssh_add_key(const char* key)
{
  if (_M_AcceptKeys) {
    string authfile = _M_UserHome + "/.ssh/authorized_keys";
    ifstream in(authfile.c_str());
    while(in.good()) {
      char buffer[1024];
      if (! in.getline(buffer, sizeof(buffer)).good())
	break;

      if (strcmp(buffer, key) == 0)
	// key already exists
	return;
    }
    in.close();

    ofstream out(authfile.c_str(), ios_base::out|ios_base::app);
    out << key << endl;
  }
}
