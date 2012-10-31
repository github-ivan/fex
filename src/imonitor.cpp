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

#ifdef USE_DNOTIFY

#include "logging.h"
#include "imonitor.h"
#include "configfile.h"
#include "nmstl/netioevent"
#include "nmstl/tqueue"
#include <sys/ioctl.h>
#include <sys/utsname.h> 
#include <linux/types.h>
#include <linux/limits.h>

using namespace std;
using namespace nmstl;


// a mix of different inotify headers

/* this size could limit things, since technically we could need PATH_MAX */
#define INOTIFY_FILENAME_MAX	256

/*
 * struct inotify_event - structure read from the inotify device for each event
 *
 * When you are watching a directory, you will receive the filename for events
 * such as IN_CREATE, IN_DELETE, IN_OPEN, IN_CLOSE, ...
 *
 * Note: When reading from the device you must provide a buffer that is a
 * multiple of sizeof(struct inotify_event)
 */
struct inotify_event17 {
	__s32 wd;
	__u32 mask;
	__u32 cookie;
	char filename[INOTIFY_FILENAME_MAX];
};

struct inotify_event20 {
	__s32		wd;		/* watch descriptor */
	__u32		mask;		/* watch mask */
	__u32		cookie;		/* cookie to synchronize two events */
	size_t		len;		/* length (including nulls) of name */
	char		name[0];	/* stub for possible name */
};


/*
 * struct inotify_watch_request - represents a watch request
 *
 * Pass to the inotify device via the INOTIFY_WATCH ioctl
 */

struct inotify_watch_request20 {
	char *dirname;		/* directory name */
	__u32 mask;		/* event mask */
};

struct inotify_watch_request21 {
	int		fd;		/* fd of filename to watch */
	__u32		mask;		/* event mask */
};



/* the following are legal, implemented events */
#define IN_ACCESS		0x00000001	/* File was accessed */
#define IN_MODIFY		0x00000002	/* File was modified */
#define IN_ATTRIB		0x00000004	/* File changed attributes */
#define IN_CLOSE_WRITE		0x00000008	/* Writtable file was closed */
#define IN_CLOSE_NOWRITE	0x00000010	/* Unwrittable file closed */
#define IN_OPEN			0x00000020	/* File was opened */
#define IN_MOVED_FROM		0x00000040	/* File was moved from X */
#define IN_MOVED_TO		0x00000080	/* File was moved to Y */
#define IN_DELETE_SUBDIR	0x00000100	/* Subdir was deleted */ 
#define IN_DELETE_FILE		0x00000200	/* Subfile was deleted */
#define IN_CREATE_SUBDIR	0x00000400	/* Subdir was created */
#define IN_CREATE_FILE		0x00000800	/* Subfile was created */
#define IN_DELETE_SELF		0x00001000	/* Self was deleted */
#define IN_UNMOUNT		0x00002000	/* Backing fs was unmounted */
#define IN_Q_OVERFLOW		0x00004000	/* Event queued overflowed */
#define IN_IGNORED		0x00008000	/* File was ignored */

/* special flags */
#define IN_ALL_EVENTS		0xffffffff	/* All the events */
#define IN_CLOSE		(IN_CLOSE_WRITE | IN_CLOSE_NOWRITE)

#define INOTIFY_IOCTL_MAGIC	'Q'
#define INOTIFY_IOCTL_MAXNR	2

#define INOTIFY_WATCH20  	_IOR(INOTIFY_IOCTL_MAGIC, 1, struct inotify_watch_request20)
#define INOTIFY_WATCH21  	_IOR(INOTIFY_IOCTL_MAGIC, 1, struct inotify_watch_request21)
#define INOTIFY_IGNORE 		_IOR(INOTIFY_IOCTL_MAGIC, 2, int)




INotifyMonitor* INotifyMonitor::
createMonitor()
{
  return new INotifyMonitor();
}


INotifyMonitor::
INotifyMonitor() 
{
}

INotifyMonitor::
~INotifyMonitor()
{
}

void INotifyMonitor::
setup_handler()
{
  lc.notice("use inotify for monitoring files");

  int fd = open("/dev/inotify", O_RDONLY);
  if (fd < 0) {
    lc.fatal("open /dev/inotify, failed (%s)", strerror(errno));
    exit(1);
  }

  _M_inotify = iohandle(fd);
  _M_inotify.set_blocking(false);
  _M_Handler->set_ioh(_M_inotify);
  _M_Handler->want_read(true);
  _M_Handler->want_write(false);
}

void INotifyMonitor::
shutdown_handler()
{
}

int INotifyMonitor::
start_monitor(const string& dir)
{
  __u32 mask = ( IN_MODIFY | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO |
	       IN_DELETE_SUBDIR | IN_DELETE_FILE | IN_CREATE_SUBDIR |
	       IN_CREATE_FILE | IN_Q_OVERFLOW );

  inotify_watch_request20 req;
  req.dirname = const_cast<char*>(dir.c_str());
  req.mask = mask;

  int wd = ioctl(_M_inotify.get_fd(), INOTIFY_WATCH20, &req);

  if (wd < 0 && errno == EBADF) {
    inotify_watch_request21 req;
    req.fd = open(dir.c_str(), O_RDONLY);
    req.mask = mask;
    wd = ioctl(_M_inotify.get_fd(), INOTIFY_WATCH21, &req);
    close(req.fd);
  }

  if (wd < 0) {
    lc.error("ioctl for %s failed (%s)", dir.c_str(), strerror(errno));
    exit(0);
  }

  return wd;
}

void INotifyMonitor::
stop_monitor(const string& dir, int fd)
{
  ioctl(_M_inotify.get_fd(), INOTIFY_IGNORE, fd);
}

void INotifyMonitor::
handle_event()
{
  typedef FileListener::FileEvent::reqs_m::iterator iterator;

  inotify_event17 event;
  while(_M_Handler->get_ioh().read(&event, sizeof(event)) > 0) {
    if (event.mask & IN_Q_OVERFLOW) {
      // to many changes for the queue ==> test all watchpoints for
      // changes

      // empty queue
      while(_M_Handler->get_ioh().read(&event, sizeof(event)) > 0);

      iterator i = _M_Handler->reqs().begin();
      for(;i != _M_Handler->reqs().end(); i++) {
	i->second.wp->changeDB(*i->second.path);
      }

      break;
    }

    iterator item = _M_Handler->reqs().find(event.wd);
      
    if (item == _M_Handler->reqs().end())
      continue;
     
    item->second.wp->changeDB(*item->second.path);
  }
}

#endif
