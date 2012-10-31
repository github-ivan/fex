/***************************************************************************
 *   Copyright (C) 2004,2005 by Michael Reithinger                         *
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
#ifndef __IMONITOR_H__
#define __IMONITOR_H__

#include "config.h"

#ifdef USE_DNOTIFY

#include "filelistener.h"
//#include "nmstl/io"

/*
  A Monitor using the inotify mechanism to detect filesystem changes
  patch 17 (up to kernel 2.6.10)
*/
class INotifyMonitor : public MonitorInterface
{
public:
  INotifyMonitor();
  ~INotifyMonitor();

  virtual void
  setup_handler();

  virtual void
  shutdown_handler();

  virtual int
  start_monitor(const std::string& dir);

  virtual void
  stop_monitor(const std::string& dir, int fd);

  virtual void 
  handle_event();

  static INotifyMonitor* createMonitor();

protected:
  nmstl::iohandle _M_inotify;
};








#endif


#endif
/** EMACS **
 * Local variables:
 * mode: c++
 * End:
 */
