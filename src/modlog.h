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
#ifndef MODLOG_H
#define MODLOG_H

#include <string.h>
#include <map>


/*
  The State of a file
*/
struct State
{
  enum ChangeTypes
    {
      removed    = 0x01,
      newaccess  = 0x02,
      created    = 0x04,
      changed    = 0x08,
      mkdired    = 0x10,
      rmdired    = 0x20,
      // dirchanged = 0x40
      // dirchanged is not in use anymore, changeing the enum would
      // change the protocol, so newlink is still 0x80
      newlink    = 0x80,
    };

  unsigned char  md4[16];
  uid_t          uid;
  gid_t          gid;
  mode_t         mode;
  time_t         mtime;
  time_t         ctime;
  off_t          size;
  unsigned short action;
};

const char*
action_str(unsigned short action);


/*
  A memory optimized path of the filesystem
*/
class Path 
{
public:
  Path()
  { }

  Path(const Path& src);
  Path(const std::string tail);
  Path(const Path* parent, const std::string& tail);

  bool 
  operator<(const Path& cmp) const
  { return str() < cmp.str(); }

  bool 
  operator==(const Path& cmp) const
  { return str() == cmp.str(); }

  operator std::string() const
  { return str(); }
  
  std::string
  str() const
  { return _M_Parent ? (_M_Parent->str() + _M_Tail) : _M_Tail; }

  bool
  isParentOf(const std::string& cmp) const
  { std::string tmp = str(); 
    return ::strncmp(tmp.c_str(), cmp.c_str(), tmp.length()) == 0; }

  const Path*
  getMatchingParent(const std::string& path) const;

private:
  std::string _M_Tail;
  const Path* _M_Parent;
};


/*
  A container for file states. 
 */
class ModLog : private std::map<Path, State>
{
public:
  typedef std::map<Path, State> parent;
  typedef parent::iterator iterator;
  typedef parent::const_iterator const_iterator;

  parent::begin;
  parent::end;
  parent::clear;
  parent::erase;
  parent::empty;

  ModLog();

  std::pair<iterator, bool> 
  insert(const std::string& path, const State& state);

  void
  insert(iterator begin, iterator end);

  iterator
  erase(iterator i);

  iterator
  find(const std::string& path)
  { Path tmp(path); return parent::find(tmp); }

  iterator
  lower_bound(const std::string& path)
  { Path tmp(path); return parent::lower_bound(tmp); }

};

/*
  A container for the filestates of an whole directory tree.
  Used to check all files of a watchpoint.
*/
class StateLog : protected ModLog
{
public:
  StateLog();

  void 
  changeDB(const std::string& path, const unsigned char* md4 = NULL);

  bool
  find_path(ino_t inode, dev_t device, std::string& path) const;

#ifndef NDEBUG
  std::ostream&
  dump(std::ostream& out);
#endif

protected:
  virtual void
  change(const Path& path, const State& state) = 0;

  virtual bool
  isValidPath(const std::string& path) const = 0;

  void
  backup(const std::string& path);

private:
  typedef parent::iterator iterator;

  unsigned int
  renewState(const std::string& key, iterator& item);

  void 
  walkTree(std::string& path);

  unsigned int
  testPath(const std::string& path);
  
  void
  validateMD4(const std::string& path, const unsigned char* md4);
};


#endif

/** EMACS **
 * Local variables:
 * mode: c++
 * End:
 */
