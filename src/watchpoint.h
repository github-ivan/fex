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
#ifndef WATCHPOINT_H
#define WATCHPOINT_H

#include "connection.h"
#include "configfile.h"
#include "modlog.h"
#include <stack>


/*
  The connected part of a watchpoint. For each Watchpoint and each
  Connection exits one ConnectedWatchpoint.
  ConnectedWatchPoint is the main message driver in the communication
  between server an client.
*/

class ConnectedWatchPoint : public nmstl::timer
{
public:
  typedef nmstl::guard<> guard;
  class Dialog;

  ConnectedWatchPoint(nmstl::io_event_loop& loop, 
		      WatchPoint* wp,
		      Connection* con,
		      unsigned char id = 0);
  virtual ~ConnectedWatchPoint();


  virtual void 
  incoming_message(const fex_header &head, nmstl::constbuf buf);

  bool 
  write(const fex_header& head);

  bool
  write(const fex_header& head, nmstl::constbuf payload);

  bool
  write_bytes_pending() const
  { return _M_Connection->write_bytes_pending(); }

  void 
  disconnect()
  { _M_Connection->set_socket(nmstl::tcpsocket()); }

  WatchPoint*
  wp() const
  { return _M_WatchPoint; }

  void 
  file_changed(const std::string& key, 
	       const State& state, 
	       void* lock_id);

  void 
  filelock_changed(const std::string& key, char locktype);

  void
  addToLog(const std::string& key, 
	   const State& state, 
	   void* lock_id,
	   bool do_sync);

  State*
  findInLog(const std::string& key);

  void
  receiveLog(nmstl::constbuf buf, ModLog *log);

  void
  undoSync();

  void
  requireSync();

  void
  pushDialog(Dialog* dialog);

  void
  pushDialog(Dialog* dialog, const fex_header &head, nmstl::constbuf buf);

  void 
  popDialog();

  void
  pushSendLogDialog(int msg_type, ModLog* log);

  virtual void
  pushSendDialog();

  virtual void
  pushReceiveDialog(const fex_header &head, nmstl::constbuf buf);

  virtual void
  save_state()
  { }

  virtual void
  translateReceivedState(State& state)
  { }

  virtual void
  translateSendState(State& state)
  { }
 
  void
  setPendingSync(bool sync) 
  { _M_PendingSync = sync; }

  ModLog* sendLog()
  { return _M_SendLog; }

  void 
  receiveWriteLog(nmstl::constbuf buf)
  { receiveLog(buf, _M_WriteLog); }

protected:
  typedef std::vector<Dialog*> Dialog_v;

  virtual void
  fire();

  enum Mode {
    MO_start = 0,
    MO_fullsynched,
  };

  WatchPoint*   _M_WatchPoint;
  Connection*   _M_Connection;
  unsigned char _M_Id;
  Dialog_v      _M_DialogStack;
  int           _M_Mode;

private:
  void
  startSync();

  ModLog   _M_Log[2];
  bool     _M_PendingSync;
  ModLog*  _M_WriteLog;
  ModLog*  _M_SendLog;
};


class IDTranslator;


/*
  A specialisation for the client side of a connection
*/
class ClientWatchPoint : public ConnectedWatchPoint
{
public:
  ClientWatchPoint(nmstl::io_event_loop& loop, 
		   WatchPoint* wp,
		   Connection* con,
		   unsigned char id, 
		   IDTranslator* translator);
  virtual ~ClientWatchPoint();

  virtual void
  pushSendDialog();

  virtual void
  pushReceiveDialog(const fex_header &head, nmstl::constbuf buf);

  virtual void
  save_state();

  virtual void
  translateReceivedState(State& state);

  virtual void
  translateSendState(State& state);

private:
  IDTranslator *_M_Translator;
};


/*
  The base class of all message dialogs between server an client
*/
class ConnectedWatchPoint::Dialog
{
public:
  virtual 
  ~Dialog()
  { }
  
protected:
  Dialog(ConnectedWatchPoint& wp)
    : _M_WatchPoint(wp)
  { }
  
  virtual void 
  incoming_message(const fex_header &head, nmstl::constbuf buf)
  { if (head.type != ME_wavail) parent().write(fex_header(ME_Reject)); }

  virtual void
  start() 
  { }

  virtual void
  popUp()
  { }

  ConnectedWatchPoint& 
  parent()
  { return _M_WatchPoint; }
  
  void 
  pushDialog(Dialog* dialog) 
  { _M_WatchPoint.pushDialog(dialog); }

  void
  pushDialog(Dialog* dialog, const fex_header &head, nmstl::constbuf buf)
  { _M_WatchPoint.pushDialog(dialog, head, buf); }

  void endDialog()
  { _M_WatchPoint.popDialog(); }

  bool
  write(const fex_header& head, nmstl::constbuf payload)
  { return parent().write(head, payload); }

  template <typename _Type>
  void
  write(unsigned short type, _Type& buffer)
  { write(fex_header(type), nmstl::constbuf(&buffer, sizeof(_Type))); }

private:
  ConnectedWatchPoint& _M_WatchPoint;

  friend class ClientWatchPoint;
  friend class ConnectedWatchPoint;
};



inline bool ConnectedWatchPoint::
write(const fex_header& head) 
{ 
  fex_header ihead(head);
  ihead.wp_id = _M_Id; 
  return _M_Connection->write(ihead); 
}

inline bool ConnectedWatchPoint::
write(const fex_header& head, nmstl::constbuf payload)
{ 
  fex_header ihead(head);
  ihead.wp_id = _M_Id; 
  return _M_Connection->write(ihead, payload); 
}


#endif

/** EMACS **
 * Local variables:
 * mode: c++
 * End:
 */
