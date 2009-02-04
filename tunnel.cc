/*********************************************************
 * Copyright (C) 2008 VMware, Inc. All rights reserved.
 *
 * This file is part of VMware View Open Client.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/

/*
 * tunnel.cc --
 *
 *    Tunnel wrapper API.
 */


#include <boost/bind.hpp>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "tunnel.hh"
#include "app.hh"

extern "C" {
#include "file.h"
#include "posix.h"
#include "util.h" /* For DIRSEPS */
}


#define VMWARE_VIEW_TUNNEL "vmware-view-tunnel"

// NOTE: Keep up to date with strings in tunnelMain.c
#define TUNNEL_READY "TUNNEL READY"
#define TUNNEL_STOPPED "TUNNEL STOPPED: "
#define TUNNEL_DISCONNECT "TUNNEL DISCONNECT: "
#define TUNNEL_SYSTEM_MESSAGE "TUNNEL SYSTEM MESSAGE: "
#define TUNNEL_ERROR "TUNNEL ERROR: "


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Tunnel::Tunnel --
 *
 *      Constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Tunnel::Tunnel()
   : mIsConnected(false)
{
   mProc.onExit.connect(boost::bind(&Tunnel::OnDisconnect, this, _1));
   mProc.onErr.connect(boost::bind(&Tunnel::OnErr, this, _1));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Tunnel::~Tunnel --
 *
 *      Destructor.  Calls Disconnect.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Tunnel::~Tunnel()
{
   Disconnect();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Tunnel::GetIsConnected --
 *
 *      Returns whether this tunnel is logically connected.
 *
 * Results:
 *      true if the tunnel is connected, or no tunnel is needed.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Tunnel::GetIsConnected() const
{
   return mTunnelInfo.bypassTunnel || mIsConnected;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Tunnel::Connect --
 *
 *      Fork and exec vmware-view-tunnel.  The binary must exist in the same
 *      directory as the vmware-view binary.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Creates two pipes for stdio, and forks a child.
 *
 *-----------------------------------------------------------------------------
 */

void
Tunnel::Connect(const BrokerXml::Tunnel &tunnelInfo) // IN
{
   ASSERT(!mIsConnected);
   ASSERT(!mProc.IsRunning());

   mTunnelInfo = tunnelInfo;
   if (mTunnelInfo.bypassTunnel) {
      Log("Direct connection to desktop enabled; bypassing tunnel "
          "connection.\n");
      onReady();
      return;
   }

   char *self;

#if defined(__APPLE__)
   uint32_t pathSize = FILE_MAXPATH;
   self = (char *)malloc(pathSize);
   ASSERT(self);
   if (_NSGetExecutablePath(self, &pathSize)) {
      Warning("1%s: _NSGetExecutablePath failed.\n", __FUNCTION__);
      ASSERT(false);
   }
#else
   self = Posix_ReadLink("/proc/self/exe");
   ASSERT(self);
#endif

   char *dirname = NULL;
   File_GetPathName(self, &dirname, NULL);
#if defined(__APPLE__)
   free(self);
#endif
   ASSERT(dirname);

   Util::string tunnelPath(dirname);
   tunnelPath += DIRSEPS VMWARE_VIEW_TUNNEL;
   Log("Executing secure HTTP tunnel: %s\n", tunnelPath.c_str());

   std::vector<Util::string> args;
   args.push_back(GetTunnelUrl());
   args.push_back(GetConnectionId());

   mProc.Start(VMWARE_VIEW_TUNNEL, tunnelPath, args);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Tunnel::OnDisconnect --
 *
 *      Callback for when the tunnel has disconnected.  If we have
 *      some status text, pass it on.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Emits onDisconnect signal.
 *
 *-----------------------------------------------------------------------------
 */

void
Tunnel::OnDisconnect(int status) // IN
{
   mIsConnected = false;
   onDisconnect(status, mDisconnectReason);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Tunnel::OnErr --
 *
 *      Stderr callback for the vmware-view-tunnel child process.  If the line
 *      matches the magic TUNNEL_READY string, emit onReady.  For tunnel system
 *      messages and errors, calls App::ShowDialog to display a dialog.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
Tunnel::OnErr(Util::string line) // IN: this
{
   if (line == TUNNEL_READY) {
      mIsConnected = true;
      onReady();
   } else if (line.find(TUNNEL_STOPPED, 0, strlen(TUNNEL_STOPPED)) == 0) {
      mDisconnectReason = Util::string(line, strlen(TUNNEL_STOPPED));
   } else if (line.find(TUNNEL_DISCONNECT, 0, strlen(TUNNEL_DISCONNECT)) == 0) {
      mDisconnectReason = Util::string(line, strlen(TUNNEL_DISCONNECT));
   } else if (line.find(TUNNEL_SYSTEM_MESSAGE, 0,
                        strlen(TUNNEL_SYSTEM_MESSAGE)) == 0) {
      Util::string msg = Util::string(line, strlen(TUNNEL_SYSTEM_MESSAGE));
      Log("Tunnel system message: %s\n", msg.c_str());
      App::ShowDialog(GTK_MESSAGE_INFO,
                      CDK_MSG(systemMessage, "Message from View Server: %s"),
                      msg.c_str());
   } else if (line.find(TUNNEL_ERROR, 0, strlen(TUNNEL_ERROR)) == 0) {
      Util::string err = Util::string(line, strlen(TUNNEL_ERROR));
      Log("Tunnel error message: %s\n", err.c_str());
      App::ShowDialog(GTK_MESSAGE_ERROR,
                      CDK_MSG(errorMessage, "Error from View Server: %s"),
                      err.c_str());
   }
}


} // namespace cdk
