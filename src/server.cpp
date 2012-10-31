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
#include "server.h"
#include "configfile.h"
#include "rsync.h"

using namespace nmstl;
using namespace std;

namespace server 
{

FullSyncDialog::FullSyncDialog(ConnectedWatchPoint& wp)
  : ConnectedWatchPoint::Dialog(wp)
{
}
  
FullSyncDialog::
~FullSyncDialog()
{
  string tmp = parent().wp()->path() + _M_StateFile;
  ::unlink(tmp.c_str());
}

 
void FullSyncDialog::
incoming_message(const fex_header &head, constbuf buf)
{
  switch(head.type) {
  case ME_FullSyncStart:
    sendStatFile();
    return;

  case ME_FullSyncLog:
    parent().receiveWriteLog(buf);
    return;

  case ME_FullSyncLogEnd:
    parent().requireSync();
    return;

  case ME_FullSyncComplete:
    endDialog();
    return;

  case ME_RsyncStart:
    pushDialog(new RsyncReceiveDialog(parent()), head, buf);
    return;
  }

#ifndef NDEBUG
  if (head.type != ME_wavail)
    lc.info("FullSyncDialog(server) didn't accept %s", message_str(head.type));
#endif

  ConnectedWatchPoint::Dialog::incoming_message(head, buf);
}

  
void FullSyncDialog::
sendStatFile() 
{
  size_t size;
  size = parent().wp()->createStateFile(this, &_M_StateFile);
  omessage msg;
  msg << _M_StateFile << size;
  write(fex_header(ME_FullSyncState), msg);
}


}

