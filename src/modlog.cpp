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
#include "modlog.h"
#include "nmstl/debug"
#include <fstream>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
extern "C" {
#include <librsync.h>
}

using namespace std;
using namespace nmstl;

// stolen from librsync
struct rs_mdfour {
    int                 A, B, C, D;
#if HAVE_UINT64
    uint64_t            totalN;
#else
    __uint32_t          totalN_hi, totalN_lo;
#endif
    int                 tail_len;
    unsigned char       tail[64];
};

// declared but not implemented in librsync
static void
mdfour_file(ifstream& in, unsigned char *result)
{
  rs_mdfour_t md;
  char        buffer[1024];
  size_t      size;

  rs_mdfour_begin(&md);

  while(in.good()) {
    in.read(buffer, sizeof(buffer));
    size = in.gcount();
    rs_mdfour_update(&md, buffer, size);
  }

  rs_mdfour_result(&md, result);
}


const char*
action_str(unsigned short action) 
{
  switch(action) {
  case State::removed:    return "removed";
  case State::newaccess:  return "newaccess";
  case State::created:    return "created";
  case State::changed:    return "changed";
  case State::mkdired:    return "mkdired";
  case State::rmdired:    return "rmdired";
  //case State::dirchanged: return "dirchanged"; // see modlog.h
  case State::newlink:    return "newlink";
  }

  assert(0);
  return "";
}


/***************************************************************************/


Path::
Path(const Path& src) 
  : _M_Tail(src._M_Tail), _M_Parent(src._M_Parent)
{
}

Path::
Path(const string tail)
{
  _M_Parent = NULL;
  _M_Tail   = tail;
}

Path::
Path(const Path* parent, const std::string& tail)
{
  if (parent) {
    assert(tail.find(parent->str()) == 0);
    _M_Tail = tail.substr(parent->str().length());
  }
  else
    _M_Tail = tail;

  _M_Parent = parent;
}

const Path* Path::
getMatchingParent(const string& path) const
{
  string tmp(str());

  if (strncmp(tmp.c_str(), path.c_str(), tmp.length()) == 0
      && path[tmp.length()] == '/')
    return this;

  if (! _M_Parent)
    return NULL;

  return _M_Parent->getMatchingParent(path);
}


/***************************************************************************/


ModLog::
ModLog()
{
}

pair<ModLog::iterator, bool> ModLog::
insert(const string& path, const State& state)
{
  Path     tmp(path);
  iterator j(parent::lower_bound(tmp));

  if (j != begin()) {
    j--;
    const Path* par = j->first.getMatchingParent(path);
    Path        child(par, path);
    return parent::insert(value_type(child, state));
  }
  return parent::insert(value_type(tmp, state));
}

void ModLog::
insert(iterator begin, iterator end)
{
  iterator i;
  for(i = begin; i != end; i++) {
    insert(i->first.str(), i->second);
  }
}


ModLog::iterator ModLog::
erase(iterator i)
{
  assert(i != end());
  iterator j = i;
  for(j++; j != end(); j++) {
    if (! i->first.isParentOf(j->first.str()))
      break;
  }

  parent::erase(i, j);
  return j;
}



/***************************************************************************/

inline
void 
nullfunc(const string& path, const State& type) 
{
}


StateLog::
StateLog()
{
}


/*
  renames full_path.

  dirs/filename.ext becomes dirs/filename-<n>.ext.  where <n> is the
  revision number, which ist 1 when no backup file exists.  If there
  are already several backup file, then <n> will be the highest
  revision number + 1.

*/

void StateLog::
backup(const string& full_path)
{
  size_t ext_pos  = full_path.rfind('.');
  string base     = full_path.substr(0, ext_pos) + "-";
  int    revision = 0;
  State  state;

  iterator i = find(full_path);
  assert(i != end());
  memcpy(&state, &i->second, sizeof(state));

  // find other backup files and extract the highest revision number
  for(i = lower_bound(base); i != end(); i++) {
    string pa(i->first.str());

    if (strncmp(pa.c_str(), base.c_str(), base.length()))
      break;

    const char* p = pa.c_str() + base.length();
    const char* s = p;
    p--;
    while(isdigit(*(++p)));

    if (ext_pos == string::npos || full_path.substr(ext_pos) == p) {
      // found a revision
      revision = max(atoi(s), revision);
      if (memcmp(state.md4, i->second.md4, sizeof(state.md4)) == 0) {
	// the backup does already exists ==> do not backup again
	lc.debug("don't backup %s because %s has same content", 
		 full_path.c_str(), pa.c_str());
	return;
      }
    }
  }
  
  // construct new file name and rename the file
  base += to_string(revision + 1);
  if (ext_pos != string::npos)
    base += full_path.substr(ext_pos);

  if (S_ISDIR(state.mode)) 
    ::rename(full_path.c_str(), base.c_str());
  else {
    ifstream in (full_path.c_str(), ios_base::binary|ios_base::in);
    ofstream out(base.c_str(), ios_base::binary|ios_base::out);
    out << in.rdbuf();
    in.close();
    out.close();
  }

  iterator item = find(full_path);
  renewState(full_path, item);
  state.mode &= ~(S_IWUSR | S_IWGRP | S_IROTH);
  chmod(base.c_str(), state.mode);
  chown(base.c_str(), state.uid, state.gid);

  lc.notice("conflicting files! created backup %s --> %s", 
	    full_path.c_str(), base.c_str());
}


void StateLog::
changeDB(const string& path, const unsigned char* md4)
{
  string buffer(path);
  if (md4) validateMD4(buffer, md4);
  testPath(buffer);
  buffer += "/";
  walkTree(buffer);
}


unsigned int StateLog::
renewState(const string& key, iterator& item)
{
  unsigned int result = 0;
  State        sbuf;
  State*       state = &sbuf;

  if (item != end()) {
    state = &item->second;
    assert(item->first.str() == key);
  }
  else 
    memset(state, 0, sizeof(*state));


  struct stat buf;
  if (lstat(key.c_str(), &buf) < 0) {
    // path was removed
    if (state->mode != 0) {
      state->action = result = 
	S_ISDIR(state->mode) ? State::rmdired : State::removed;
      return result;
    }
    return 0;
  }

  if (buf.st_mode != state->mode ||
      buf.st_gid  != state->gid  ||
      buf.st_uid  != state->uid) {
    state->gid   = buf.st_gid;
    state->uid   = buf.st_uid;
    state->ctime = buf.st_ctime;
    state->mode  = buf.st_mode;
    result       = State::newaccess;
  }

  if (buf.st_mtime > state->mtime ||
      buf.st_size != state->size) {
    if (! S_ISDIR(state->mode)) {
      ifstream in(key.c_str(), ios::in|ios::binary);
      mdfour_file(in, state->md4);
      result = S_ISLNK(state->mode) ? State::newlink : State::changed;
    }

    state->mtime = buf.st_mtime;
    state->size  = buf.st_size;
  }
      
  if (item == end()) {
    if (S_ISDIR(state->mode))
      result = State::mkdired;
    else if (S_ISLNK(state->mode))
      result = State::newlink;
    else if (S_ISREG(state->mode))
      result = State::created;
    else 
      result = 0;
  }

  if (result) {
    state->action = result;
    if (item == end()) {
      pair<iterator, bool> res = insert(key, *state);
      assert(res.second == true);
      item = res.first;
    }
  }

  return result;
}

void StateLog::
walkTree(string& full_path)
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

      if (! isValidPath(full_path))
	continue;

      int result = testPath(full_path);

      struct stat buf;
      lstat(full_path.c_str(), &buf);
      if (S_ISDIR(buf.st_mode)) {
	if ((result & (State::mkdired)) == 0) {
	  // test if the next item in this directory is removed
	  full_path += (char)('/' + 1);
	  iterator i = lower_bound(full_path);
	  if (i != end())
	    testPath(i->first.str());

	  continue;
	}

	full_path += "/";
	walkTree(full_path);
      }
    }

    closedir(dirf);
  }
		
  full_path.erase(length);
}

unsigned int StateLog::
testPath(const string& path)
{
  State state;

  iterator item = find(path);
  int result = renewState(path, item);
  
  if (item == end()) {
    // only possible if peer sends a wrong notifcation
    return result;
  }

  if (result)
    change(item->first, item->second);

  if (result & (State::rmdired | State::removed))
    item = erase(item);
  else
    item++;

  while(item != end()) {
    // find removed items after me
    int res = renewState(item->first.str(), item);
    if (res)
      change(item->first, item->second);

    if (res & (State::rmdired | State::removed))
      item = erase(item);
    else
      break;
  }

  return result;
}
  
void StateLog::
validateMD4(const string& path, const unsigned char* md4)
{
  iterator i = find(path);
  if (i != end() && S_ISREG(i->second.mode)) {
    if (memcmp(i->second.md4, md4, sizeof(i->second.md4))) 
      i->second.mtime = 0;
  }
}

#ifndef NDEBUG
ostream& StateLog::
dump(ostream& out)
{
  for(iterator i = begin(); i != end(); i++) {
    if (i->second.mode == 0)
      continue;

    out << i->first.str() << endl;// << " " << endl;
      /*	<< hex << state->uid << " "
	<< hex << state->gid << " "
	<< hex << state->mode << " "
	<< ctime(&state->mtime);*/
  }
  return out;
}
#endif

bool StateLog::
find_path(ino_t inode, dev_t device, std::string& path) const
{
  const_iterator i;
  for(i = begin(); i != end(); i++) {
    struct stat buf;
    string p = i->first.str();
    lstat(p.c_str(), &buf);
    if (buf.st_dev = device && buf.st_ino == inode) {
      path = p;
      return true;
    }
  }

  return false;
}
