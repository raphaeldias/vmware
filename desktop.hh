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
 * desktop.hh --
 *
 *    Desktop info representing a possibly connected desktop exposed by the Broker.
 */

#ifndef DESKTOP_HH
#define DESKTOP_HH


#include "brokerXml.hh"
#include "rdesktop.hh"
#include "util.hh"


namespace cdk {


class Desktop
{
public:
   enum DesktopType {
      TYPE_FREE,
      TYPE_STICKY,
      TYPE_AUTO
   };

   enum ScreenSize {
      SIZE_WINDOWED,
      SIZE_FULL,
      SIZE_FULL_MULTI
   };

   enum ConnectionState {
      STATE_DISCONNECTED,
      STATE_CONNECTING,
      STATE_CONNECTED
   };

   Desktop(BrokerXml &xml, BrokerXml::Desktop &desktopInfo);
   ~Desktop();

   ConnectionState GetConnectionState() const { return mConnectionState; }

   void Connect(Util::AbortSlot onAbort, Util::DoneSlot onDone);
   void Disconnect();

   RDesktop* GetRDesktop();
   bool StartRDesktop(const std::vector<Util::string> &devRedirectArgs =
                         std::vector<Util::string>());

   bool CanReset() const { return mDesktopInfo.resetAllowed; }
   bool CanResetSession() const { return mDesktopInfo.resetAllowedOnSession; }
   void ResetDesktop(Util::AbortSlot onAbort, Util::DoneSlot onDone);

   Util::string GetID() const { return mDesktopInfo.id; }
   Util::string GetName() const { return mDesktopInfo.name; }
   Util::string GetSessionID() const { return mDesktopInfo.sessionId; }
   void KillSession(Util::AbortSlot onAbort, Util::DoneSlot onDone);

   DesktopType GetDesktopType() const;
   Util::string GetState() const { return mDesktopInfo.state; }

   bool GetIsUSBEnabled() const { return mDesktopConn.enableUSB; }

   bool GetAutoConnect() const;
   // Desktop preferences not yet supported...
   /*
   void SetAutoConnect(bool val);

   ScreenSize GetScreenSize() const;
   void SetScreenSize(ScreenSize size);
   */

private:
   void OnGetDesktopConnectionDone(BrokerXml::Result &result,
                                   BrokerXml::DesktopConnection &conn,
                                   Util::DoneSlot onDone);

   void OnGetDesktopConnectionAbort(bool cancelled, Util::exception err,
                                    Util::AbortSlot onAbort);

   void OnResetDesktopAbort(bool canceled, Util::exception err,
                            Util::AbortSlot onAbort);

   void OnKillSessionAbort(bool canceled, Util::exception err,
                           Util::AbortSlot onAbort);

   BrokerXml &mXml;
   BrokerXml::Desktop mDesktopInfo;

   ConnectionState mConnectionState;
   BrokerXml::DesktopConnection mDesktopConn;

   RDesktop *mRDesktop;
};


} // namespace cdk


#endif // DESKTOP_HH
