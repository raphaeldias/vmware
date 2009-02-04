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
 * tunnel.hh --
 *
 *    Tunnel wrapper API.
 */

#ifndef TUNNEL_HH
#define TUNNEL_HH


#include <boost/signal.hpp>


#include "brokerXml.hh"
#include "procHelper.hh"
#include "util.hh"


namespace cdk {


class Tunnel
{
public:
   Tunnel();
   ~Tunnel();

   bool GetIsConnected() const;
   void Connect(const BrokerXml::Tunnel &tunnelInfo);
   void Disconnect() { mProc.Kill(); }

   Util::string GetConnectionId() const { return mTunnelInfo.connectionId; }
   Util::string GetTunnelUrl() const { return mTunnelInfo.server1; }

   boost::signal0<void> onReady;
   boost::signal2<void, int, Util::string> onDisconnect;

private:
   void OnDisconnect(int status);
   void OnErr(Util::string line);

   BrokerXml::Tunnel mTunnelInfo;
   bool mIsConnected;
   Util::string mDisconnectReason;
   ProcHelper mProc;
};


} // namespace cdk


#endif // TUNNEL_HH
