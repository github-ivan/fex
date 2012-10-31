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
#include "config.h"
#include "logging.h"
#include "filelistener.h"
#include "configfile.h"
#include "imonitor.h"
#include <fstream>
#include "nmstl/netioevent"
#include "nmstl/tqueue"
#include <signal.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <cstdatomic>


using namespace std;
using namespace nmstl;


extern io_event_loop MainLoop;

/*
  An active class for detecting changes in the file locks.
*/
class FileListener::LockPoll : public timer
{
public:
  LockPoll(FileListener& listener);
  ~LockPoll();

  void
  resendFileLocks(WatchPoint* wp, ConnectedWatchPoint* arg);

private:
  struct lock {
    ino_t        inode;
    dev_t        device;
    WatchPoint*  wp;
    string       path;
    char         type;
    int          tag;

    bool operator<(const lock& cmp) const
    { return inode == cmp.inode ? device < cmp.device : inode < cmp.inode; }
  };

  typedef vector<lock> locks_v;

  virtual void
  fire();

  void
  test_locks(char* buffer, ssize_t size);

  char*   _M_Buffer;
  size_t  _M_BufferSize;
  int     _M_LockFd;
  locks_v _M_Locks;
  off_t   _M_LastSize;

  friend class FileListener;
};

/***************************************************************************/


#ifdef USE_DNOTIFY

#if HAVE_EXT_MALLOC_ALLOCATOR_H
   #include <ext/malloc_allocator.h>
   #define MALLOCATOR __gnu_cxx::malloc_allocator<int>
#else
   #define MALLOCATOR __malloc_alloc_template<0>
#endif


/*
  A Monitor using the dnotify mechanism to detect filesystem changes
*/
class DNotifyMonitor : public MonitorInterface, private timer
{
public:
  DNotifyMonitor();
  ~DNotifyMonitor();

  virtual void
  setup_handler();

  virtual void
  shutdown_handler();

  virtual int
  start_monitor(const string& dir);

  virtual void
  stop_monitor(const string& dir, int fd);

  virtual void 
  handle_event();

private:
  static 
  void 
  dnotify_handler(int sig, siginfo_t *si, void *data);

  static 
  void 
  io_signal(int sig, siginfo_t *si, void *data);

  virtual void
  fire();

  typedef MALLOCATOR                malloc_alloc;
  typedef vector<int, malloc_alloc> fdbuffer_v;
  typedef pair<iohandle,iohandle>   pipeends_t;

  //static std::atomic<fdbuffer_v*> _S_SignalBuffer;
  static std::atomic<void*> _S_SignalBuffer;
  static sig_atomic_t _S_RenewAll;
  static pipeends_t   _S_WakeUpPipe;

  fdbuffer_v*   _M_ReadBuffer;
  fdbuffer_v    _M_FDBuffer[2];
};

#endif




#ifdef USE_POLLING

/*
  A Monitor using a polling mechanism to detect filesystem changes
*/

class PollingMonitor : public MonitorInterface, private timer
{
public:
  PollingMonitor();
  ~PollingMonitor();

  virtual void
  setup_handler();

  virtual void
  shutdown_handler()
  { }

  virtual int
  start_monitor(const string& dir)
  { return ++_M_Id; }

  virtual void
  stop_monitor(const string& dir, int fd)
  { }

  virtual void 
  handle_event()
  { }

private:
  virtual void
  fire();

  int _M_Id;
};

#endif


/***************************************************************************/

FileListener::LockPoll::
LockPoll(FileListener& listener) : timer(MainLoop)
{
  _M_LockFd = open("/proc/locks", O_RDONLY);
  _M_BufferSize = 1024;
  _M_Buffer = (char*)malloc(_M_BufferSize);
}

FileListener::LockPoll::
~LockPoll()
{
  close(_M_LockFd);
  free(_M_Buffer);
}

void FileListener::LockPoll::
fire()
{
  const int MIN_LINE_SIZE = 26; // minimal size of a lock line
  static int count = 0;
  ssize_t size;

  lseek(_M_LockFd, 0, SEEK_SET);
  while((size = read(_M_LockFd, _M_Buffer, _M_BufferSize)) >= _M_BufferSize) {
    _M_BufferSize += 1024;
    _M_Buffer = (char*)realloc(_M_Buffer, _M_BufferSize);
    lseek(_M_LockFd, 0, SEEK_SET);
  }

  /* count++ % 10:
     if a lock was released and another was aquired within one second
     fex will not register it. Therefore check all locks all 10 seconds.
  */
  if (abs(_M_LastSize - size) > MIN_LINE_SIZE || count++ % 10 == 0) {
    test_locks(_M_Buffer, size);
    _M_LastSize = size;
  }

  arm(ntime::now_plus_secs(1));
}

inline int
inttok(char *str, const char *delim) 
{
  char* tmp = strtok(str, delim);
  return tmp ? atoi(tmp) : 0;
}

void FileListener::LockPoll::
test_locks(char* buffer, ssize_t size)
{
  static int tag = 0;
  tag++;

  buffer[size] = 0;
  char *lastread = buffer;
  char *line = strtok(lastread, "\n");

  while(line && line < buffer + size) {
    lastread = line + strlen(line) + 1;
    char* no    = strtok(line, " ");
    char* clas  = strtok(NULL, " ");
    char* mode  = strtok(NULL, " ");
    char* type  = strtok(NULL, " ");
    pid_t pid   = inttok(NULL, " ");
    int   major = inttok(NULL, ":");
    int   minor = inttok(NULL, ":");
    ino_t inode = inttok(NULL, " ");

    line = strtok(lastread, "\n");

    if (! type || ! pid) 
      // a wrong line
      continue;

    if (pid == getpid())
      // skip my own locks
      continue;
    
    lock l;

    l.inode  = inode;
    l.device = (major << 8) | minor;
    l.type   = tolower(type[0]);
    
    locks_v::iterator f = lower_bound(_M_Locks.begin(), 
				      _M_Locks.end(),
				      l);

    if (f != _M_Locks.end() && 
	f->inode == l.inode && 
	f->device == l.device) {
      f->tag = tag;
      continue;
    }

    typedef Configuration::WatchPoint_v WatchPoint_v;
    const WatchPoint_v& wps = Configuration::get().watch_points();
    WatchPoint_v::const_iterator i;
    for(i = wps.begin(); i != wps.end(); i++) {
      if ((*i)->find_path(l.inode, l.device, l.path)) {
	//insert new locks
	(*i)->notifyFileLock(l.path, l.type);
	l.wp = *i;
	lc.info("found new lock: %s", l.path.c_str());
	break;
      }
    }
    
    l.tag = tag;
    _M_Locks.insert(f, l);
  }

  // notify and delete removed locks
  locks_v::iterator i;
  for(i = _M_Locks.begin(); i != _M_Locks.end();) {
    if (i->tag != tag) {
      if (! i->path.empty()) {
	i->wp->notifyFileLock(i->path, 'u');
	lc.info("lock was released: %s", i->path.c_str());
      }
      i =_M_Locks.erase(i);
      continue;
    }
    i++;
  }
}


void FileListener::LockPoll::
resendFileLocks(WatchPoint* wp, ConnectedWatchPoint* arg)
{
  locks_v::iterator i;
  for(i = _M_Locks.begin(); i != _M_Locks.end(); i++) {
    if (! i->path.empty() && i->wp == wp)
      i->wp->notifyFileLock(i->path, i->type, arg);
  }
}



/***************************************************************************/

#ifdef USE_DNOTIFY
const int MinFDBufferSize = 100;
//std::atomic<DNotifyMonitor::fdbuffer_v*> DNotifyMonitor::_S_SignalBuffer(NULL);
std::atomic<void*> DNotifyMonitor::_S_SignalBuffer(NULL);
sig_atomic_t DNotifyMonitor::_S_RenewAll = false;
DNotifyMonitor::pipeends_t DNotifyMonitor::_S_WakeUpPipe;

DNotifyMonitor::
DNotifyMonitor() : timer(MainLoop)
{
}

DNotifyMonitor::
~DNotifyMonitor()
{
}

void DNotifyMonitor::
setup_handler()
{
  lc.notice("use dnotify for monitoring files");

  _M_FDBuffer[0].reserve(MinFDBufferSize);
  _M_FDBuffer[1].reserve(MinFDBufferSize);
  _S_SignalBuffer.store(&_M_FDBuffer[0]);
  _M_ReadBuffer   = &_M_FDBuffer[1];

  assert(! _S_WakeUpPipe.first);
  assert(! _S_WakeUpPipe.second);
  _S_WakeUpPipe = iohandle::pipe();
  _S_WakeUpPipe.first.set_blocking(false);
  _M_Handler->set_ioh(_S_WakeUpPipe.first);
  _M_Handler->want_read(true);
  _M_Handler->want_write(false);

  struct sigaction act;
  act.sa_sigaction = dnotify_handler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = SA_SIGINFO;
  sigaction(SIGRTMIN + 1, &act, NULL);

  act.sa_sigaction = io_signal;
  act.sa_flags = SA_SIGINFO;
  sigaction(SIGIO, &act, NULL);
}

void DNotifyMonitor::
shutdown_handler()
{
  struct sigaction act;
  act.sa_handler = SIG_DFL;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGRTMIN + 1, &act, NULL);

  act.sa_handler = SIG_DFL;
  act.sa_flags = 0;
  sigaction(SIGIO, &act, NULL);
}

int DNotifyMonitor::
start_monitor(const string& dir)
{
  fdbuffer_v* sb = (fdbuffer_v*)&_S_SignalBuffer;
  if (sb->capacity() <= _M_Handler->size()) {
    // to increase the _S_SignalBuffer
    fire();
  }

  int fd = open(dir.c_str(), O_RDONLY);

  if (fd > 0) {
    fcntl(fd, F_SETSIG, SIGRTMIN + 1);
    fcntl(fd, F_NOTIFY, (DN_MODIFY|DN_CREATE|DN_DELETE|
			 DN_RENAME|DN_ATTRIB|DN_MULTISHOT));
  }
  else {
    lc.error("open %s, failed (%s)", dir.c_str(), strerror(errno));
    return -1;
  }

  return fd;
}

void DNotifyMonitor::
stop_monitor(const string& dir, int fd)
{
  close(fd);
}

void DNotifyMonitor::
handle_event()
{
  char c;
  // empty pipe
  while(_M_Handler->get_ioh().read(&c, sizeof(c)) > 0);

  ntime next = ntime::now_plus_secs(1);
  if (next > get_when())
    arm(next);
}

void DNotifyMonitor::
fire()
{
  typedef FileListener::FileEvent::reqs_m::iterator iterator;

  _M_ReadBuffer->reserve(_M_Handler->size() + MinFDBufferSize);
  _S_SignalBuffer.store(_M_ReadBuffer);
  _M_ReadBuffer   = &_M_FDBuffer[_M_ReadBuffer == &_M_FDBuffer[0] ? 1 : 0];

  if (_S_RenewAll) {
    iterator i = _M_Handler->reqs().begin();
    for(;i != _M_Handler->reqs().end(); i++) {
      i->second.wp->changeDB(*i->second.path);
    }
    _S_RenewAll = false;
  }
  else {
    fdbuffer_v::iterator i;
    for(i = _M_ReadBuffer->begin(); i != _M_ReadBuffer->end(); i++) {
      iterator item = _M_Handler->reqs().find(*i);
      
      if (item == _M_Handler->reqs().end())
	continue;
     
      item->second.wp->changeDB(*item->second.path);
    }
  }

  _M_ReadBuffer->clear();
}



void DNotifyMonitor::
io_signal(int sig, siginfo_t *si, void *data)
{
  // A signal says, that we got an overflow.
  // ==> check all watchpoints
  _S_RenewAll = true;
}

void DNotifyMonitor::
dnotify_handler(int sig, siginfo_t *si, void *data)
{
  //fdbuffer_v* buffer  = (fdbuffer_v*)&_S_SignalBuffer;
  fdbuffer_v* buffer  = (fdbuffer_v*)_S_SignalBuffer.load();
  bool        wake_up = buffer->empty();

  fdbuffer_v::iterator i = lower_bound(buffer->begin(), 
				       buffer->end(),
				       si->si_fd);

  if (i == buffer->end() || *i != si->si_fd)
    buffer->insert(i, si->si_fd);
  
  if (wake_up) {
    char c(0);
    write(_S_WakeUpPipe.second.get_fd(), &c, sizeof(c));
  }
}

#endif


/***************************************************************************/

#ifdef USE_POLLING

PollingMonitor::
PollingMonitor() : timer(MainLoop)
{
  _M_Id = 0;
}

PollingMonitor::
~PollingMonitor()
{
}

void PollingMonitor::
setup_handler()
{
  lc.notice("use polling for monitoring files");
  ntime next = ntime::now_plus_secs(10);
  arm(next);
}


void PollingMonitor::
fire() {
  typedef FileListener::FileEvent::reqs_m::iterator iterator;
  iterator i = _M_Handler->reqs().begin();
  for(;i != _M_Handler->reqs().end(); i++) {
    i->second.wp->changeDB(*i->second.path);
  }
  ntime next = ntime::now_plus_secs(10);
  arm(next);
}


#endif

/***************************************************************************/

MonitorInterface* FileListener::FileEvent::_S_Monitor = NULL;

FileListener::FileEvent::
FileEvent(FileListener& listener)
  : _M_Listener(listener), 
    io_handler(MainLoop)
{
  createMonitor();  
  _S_Monitor->set_handler(this);
  _S_Monitor->setup_handler();
}


FileListener::FileEvent::
~FileEvent()
{
  _S_Monitor->shutdown_handler();
  delete _S_Monitor;
}


void FileListener::FileEvent::
clear()
{
  path_m::iterator i;

  for(i = _M_Dirs.begin(); i != _M_Dirs.end(); i++) {
    _S_Monitor->stop_monitor(i->first, i->second);
  }

  _M_Dirs.clear();
  _M_Reqs.clear();
}


void FileListener::FileEvent::
insert(WatchPoint* wp, const Path& path)
{
  string str_path(path);

  assert(_M_Dirs.size() == _M_Reqs.size());

  std::pair<path_m::iterator, bool> res = 
    _M_Dirs.insert(path_m::value_type(path, 0));

  if (res.second) {
    int fd = _S_Monitor->start_monitor(str_path);

    if (fd < 0) {
      _M_Dirs.erase(res.first);
      return;
    }

    dir_data dd;
    dd.wp   = wp;
    dd.path = &res.first->first;

    std::pair<reqs_m::iterator, bool> res1 = 
      _M_Reqs.insert(reqs_m::value_type(fd, dd));
    res.first->second = fd;

    if (res1.second == false) {
      // the fd exists already ==> probably a rename 
      // remove old fd and retry
      Path p(*res1.first->second.path);
      remove(p);
      res1 = _M_Reqs.insert(reqs_m::value_type(fd, dd));
    }

    assert(res1.second == true);
    wp->changeDB(res.first->first);
  }
  assert(_M_Dirs.size() == _M_Reqs.size());
}


void FileListener::FileEvent::
remove(const Path& path)
{
  path_m::iterator item = _M_Dirs.find(path);
  if (item != _M_Dirs.end()) {
    // erase this path and all children
    path_m::iterator i = item;
    
    for(i = item; i != _M_Dirs.end(); i++) {
      const char* parent = item->first.c_str();
      const char* child  = i->first.c_str();
      
      if (strncmp(parent, child, item->first.length()))
	break;
	  
      _S_Monitor->stop_monitor(i->first, i->second);
      _M_Reqs.erase(i->second);
    }

    _M_Dirs.erase(item, i);
  }
}
 

void 
FileListener::FileEvent::
ravail()
{
  _S_Monitor->handle_event();
}

#if defined(USE_DNOTIFY)
#define HAS_DO_INOTIFY 1
extern bool do_inotify;
#endif

#if defined(USE_POLLING)
#if ! defined(USE_DNOTIFY)
extern bool do_inotify;
#else
#define HAS_DO_DNOTIFY 1
extern bool do_dnotify;
#endif
#endif

void 
FileListener::FileEvent::
createMonitor()
{
  _S_Monitor = NULL;

  int fd = open("/dev/inotify", O_RDONLY);
  if (fd > 0) {
    close(fd);
#ifdef HAS_DO_INOTIFY
    if (do_inotify)
#endif
      _S_Monitor = INotifyMonitor::createMonitor();
  }

#ifdef USE_DNOTIFY
  if (! _S_Monitor) {
    int fd = open(".", O_RDONLY);
    bool has_dnotify = fcntl(fd, F_NOTIFY, 
			     (DN_MODIFY|DN_CREATE|DN_DELETE|
			      DN_RENAME|DN_ATTRIB|DN_MULTISHOT)) >= 0;
    close(fd);

#ifdef HAS_DO_DNOTIFY
    has_dnotify = has_dnotify && do_dnotify;
#endif

    if (has_dnotify)
      _S_Monitor = new DNotifyMonitor;
  }
#endif

#ifdef USE_POLLING
  if (! _S_Monitor) {
    _S_Monitor = new PollingMonitor;
#else
    lc.fatal("no file-listening monitor can be created");
    exit(1);
#endif
  }
}


/***************************************************************************/

extern bool do_lock_polling;

FileListener::
FileListener()
{
  _M_FileEvent = new FileEvent(*this);
  if (do_lock_polling)
    _M_LockPoll  = new LockPoll (*this);
}

FileListener::
~FileListener()
{
  //no delete _M_FileEvent because it is owned by MainLoop
  if (do_lock_polling)
    delete _M_LockPoll;
}


void FileListener::
resendFileLocks(WatchPoint* wp, ConnectedWatchPoint* arg)
{
  if (do_lock_polling)
    _M_LockPoll->resendFileLocks(wp, arg);
}

void* FileListener::
notifyChange(WatchPoint* wp, const Path& path, const State& state)
{
  switch(state.action) {
  case State::mkdired:
    _M_FileEvent->insert(wp, path);
    break;

  case State::rmdired:
    _M_FileEvent->remove(path);
    break;
  }
  
  string str_path(path);

  if (lc.isDebugEnabled()) 
    lc.debug("notifyChange %s %s", str_path.c_str(), action_str(state.action));

  lock_m::iterator iter = _M_Lock.find(str_path);
  if (iter != _M_Lock.end())
    return iter->second;

  return 0;
}


bool FileListener::
lock(const string& path, void* id)
{
  return _M_Lock.insert(lock_m::value_type(path, id)).second;
}

void FileListener::
unlock(WatchPoint* wp, const string& path, const State& state)
{
  wp->changeDB(path, state.md4);
  _M_Lock.erase(path);
}

void FileListener::
startLockPoll()
{
  if (do_lock_polling) {
    _M_LockPoll->_M_LastSize = 0;
    _M_LockPoll->arm(ntime::now());
  }
}

void FileListener::
stopLockPoll()
{
  if (do_lock_polling)
    _M_LockPoll->disarm();
}

FileListener& 
FileListener::get()
{
  static FileListener _S_Listener;
  return _S_Listener;
}

