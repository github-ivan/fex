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
#ifndef CLIENT_H
#define CLIENT_H

#include "watchpoint.h"

namespace client
{

/*
  A dialog to start the synchronisation of the whole directory tree of
  all watchpoints.
*/  
class FullSyncDialog : public ConnectedWatchPoint::Dialog
{
public:
  FullSyncDialog(ConnectedWatchPoint &wp);
  
  virtual
  ~FullSyncDialog();
  
  virtual void
  start();
  
  virtual void 
  incoming_message(const fex_header &head, nmstl::constbuf buf);
  
  virtual void
  popUp();

private:
  void
  compareState();

  void 
  addToLog(const std::string& key, const State& state)
  {  parent().addToLog(key, state, 0, false); _M_RequireResync = true; }


  enum {
    WaitForSyncData = 0,
    WaitForSendLogComplete = 1
  };


  std::string _M_ServerFile;
  std::string _M_ClientFile;
  ModLog      _M_ServerLog;
  bool        _M_RequireResync;
  int         _M_Mode;
};


}


#endif


/** EMACS **
 * Local variables:
 * mode: c++
 * End:
 */
