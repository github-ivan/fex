/***************************************************************************
 *   Copyright (C) 2004, 2005 by Michael Reithinger                        *
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
#ifndef __FILELISTENER_H__
#define __FILELISTENER_H__

#include "nmstl/ntime"
#include "nmstl/ioevent"
#include <string>

class ConnectedWatchPoint;
class WatchPoint;
class State;
class Path;


/*
  Public interface to the mechanism which monitors the files of a
  watchpoints.
*/

class FileListener
{
public:
  class FileEvent;

  bool
  lock(const std::string& path, void* id);

  void
  unlock(WatchPoint* wp, const std::string& path, const State& state);

  void*
  notifyChange(WatchPoint* wp, const Path& path, const State& state);

  void
  startLockPoll();

  void
  stopLockPoll();

  void
  resendFileLocks(WatchPoint* wp, ConnectedWatchPoint* arg);

  static 
  FileListener& 
  get();

private:
  class LockPoll;

  typedef std::map<std::string, void*> lock_m;

  FileListener();
  ~FileListener();

  LockPoll*    _M_LockPoll;
  FileEvent*   _M_FileEvent;
  lock_m       _M_Lock;
};


/*
  Base class for all Monitors. Defines the interface for notifing
  the system if changes of the filesystem appear.
*/
class MonitorInterface 
{
public:
  virtual 
  ~MonitorInterface() 
  { }

  void
  set_handler(FileListener::FileEvent* handler)
  { _M_Handler = handler; }

  virtual void
  setup_handler() = 0;

  virtual void
  shutdown_handler() = 0;

  virtual int
  start_monitor(const std::string& dir) = 0;

  virtual void
  stop_monitor(const std::string& dir, int fd) = 0;

  virtual void 
  handle_event() = 0;

protected:
  FileListener::FileEvent* _M_Handler;
};


/*
  This class activates and deactivates directories 
  for file event monitoring
*/

class FileListener::FileEvent : public nmstl::io_handler
{
public:
  struct dir_data 
  {
    const std::string* path;
    WatchPoint*        wp;
  };

  typedef std::map<int, dir_data>    reqs_m;
  typedef std::map<std::string, int> path_m;

  nmstl::io_handler::get_ioh;
  nmstl::io_handler::set_ioh;
  nmstl::io_handler::want_read;
  nmstl::io_handler::want_write;


  FileEvent(FileListener& listener);
  ~FileEvent();

  void
  insert(WatchPoint* wp, const Path& path);

  void
  remove(const Path& path);

  void
  clear();

  static MonitorInterface* _S_Monitor;

  size_t
  size() const
  { return _M_Dirs.size(); }

  path_m& 
  dirs()
  { return _M_Dirs; }

  reqs_m& 
  reqs()
  { return _M_Reqs; }


private:
  virtual void 
  ravail();

  void
  createMonitor();

  path_m        _M_Dirs;
  reqs_m        _M_Reqs;
  FileListener& _M_Listener;
};


#endif

/** EMACS **
 * Local variables:
 * mode: c++
 * End:
 */
