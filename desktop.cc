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
 * desktop.cc --
 *
 *    Desktop info representing a possibly connected desktop exposed by the Broker.
 */


#include <boost/bind.hpp>

#include "desktop.hh"
#include "broker.hh"
#include "util.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::Desktop --
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

Desktop::Desktop(BrokerXml &xml,                  // IN
                 BrokerXml::Desktop &desktopInfo) // IN
   : mXml(xml),
     mDesktopInfo(desktopInfo),
     mConnectionState(STATE_DISCONNECTED),
     mRDesktop(NULL)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::~Desktop --
 *
 *      Desktop destructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      May disconnect.
 *
 *-----------------------------------------------------------------------------
 */

Desktop::~Desktop()
{
   if (mConnectionState == STATE_CONNECTED) {
      Disconnect();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::Connect --
 *
 *      Ask the broker to start a connection to this desktop, by calling the
 *      "get-desktop-connection" XML API method.
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
Desktop::Connect(Util::AbortSlot onAbort, // IN
                 Util::DoneSlot onDone)   // IN
{
   ASSERT(mConnectionState == STATE_DISCONNECTED);
   ASSERT(!GetID().empty());

   mConnectionState = STATE_CONNECTING;
   mXml.GetDesktopConnection(GetID(),
      boost::bind(&Desktop::OnGetDesktopConnectionAbort, this, _1, _2, onAbort),
      boost::bind(&Desktop::OnGetDesktopConnectionDone, this, _1, _2, onDone));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::Disconnect --
 *
 *      TBI.
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
Desktop::Disconnect()
{
   ASSERT(mConnectionState == STATE_CONNECTED);
   mConnectionState = STATE_DISCONNECTED;
   if (mRDesktop) {
      mRDesktop->Kill();
      mRDesktop = NULL;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::OnGetDesktopConnectionDone --
 *
 *      Success handler for "get-desktop-connection" XML API request.  Store the
 *      broker's connected info.
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
Desktop::OnGetDesktopConnectionDone(BrokerXml::Result &result,          // IN
                                    BrokerXml::DesktopConnection &conn, // IN
                                    Util::DoneSlot onDone)              // IN
{
   ASSERT(mConnectionState == STATE_CONNECTING);

   mConnectionState = STATE_CONNECTED;
   mDesktopConn = conn;

   onDone();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::OnGetDesktopConnectionAbort --
 *
 *      Failure handler for "get-desktop-connection" XML API request.  Just
 *      invoke the initially passed abort handler with a more friendly error.
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
Desktop::OnGetDesktopConnectionAbort(bool cancelled,          // IN
                                     Util::exception err,     // IN
                                     Util::AbortSlot onAbort) // IN
{
   ASSERT(mConnectionState == STATE_CONNECTING);
   mConnectionState = STATE_DISCONNECTED;
   Util::exception myErr(
      Util::Format("Unable to connect to desktop \"%s\": %s",
                   GetName().c_str(), err.what()),
      err.code());
   onAbort(cancelled, myErr);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::GetDesktopType --
 *
 *      Get the Desktop type as a DesktopType enum value.
 *
 * Results:
 *      DesktopType in the desktop info.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Desktop::DesktopType
Desktop::GetDesktopType()
   const
{
   if (mDesktopInfo.type == "free") {
      return TYPE_FREE;
   } else if (mDesktopInfo.type == "sticky") {
      return TYPE_STICKY;
   } else if (mDesktopInfo.type == "auto") {
      return TYPE_AUTO;
   } else {
      NOT_REACHED();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::GetAutoConnect --
 *
 *      Returns whether or not the user preference "alwaysConnect" is true.
 *
 * Results:
 *      true if "alwaysconnect" = "true"; false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Desktop::GetAutoConnect()
   const
{
   std::vector<BrokerXml::Preference> prefs =
      mDesktopInfo.userPreferences.preferences;
   for (std::vector<BrokerXml::Preference>::iterator i = prefs.begin();
        i != prefs.end(); i++) {
      if (i->first == "alwaysConnect") {
         return i->second == "true";
      }
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::GetRDesktop --
 *
 *      Returns the connected and running RDesktop object.
 *
 * Results:
 *      RDesktop member if desktop is connected, or NULL.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

RDesktop*
Desktop::GetRDesktop()
{
   if (!mRDesktop) {
      mRDesktop = new RDesktop();
      mRDesktop->onExit.connect(boost::bind(&Desktop::Disconnect, this));
   }
   return mRDesktop;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::StartRDesktop --
 *
 *      Calls Start on mRDesktop.
 *
 * Results:
 *      RDesktop member if desktop is connected, or NULL.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Desktop::StartRDesktop(const std::vector<Util::string> &devRedirectArgs) // IN
{
   if (mConnectionState == STATE_CONNECTED && mRDesktop) {
      Warning("Connecting rdesktop to %s:%d.\n", mDesktopConn.address.c_str(),
              mDesktopConn.port);
      mRDesktop->Start(mDesktopConn.address, mDesktopConn.username,
                       mDesktopConn.domainName, mDesktopConn.password,
                       mDesktopConn.port, devRedirectArgs);
      return true;
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::ResetDesktop --
 *
 *      Proxy for BrokerXml::ResetDesktop (restart VM).
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
Desktop::ResetDesktop(Util::AbortSlot onAbort, // IN
                      Util::DoneSlot onDone)   // IN
{
   mXml.ResetDesktop(GetID(),
                     boost::bind(&Desktop::OnResetDesktopAbort, this, _1, _2, onAbort),
                     boost::bind(onDone));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::OnResetDesktopAbort --
 *
 *      Error handler for reset desktop RPC.
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
Desktop::OnResetDesktopAbort(bool cancelled,
                             Util::exception err,
                             Util::AbortSlot onAbort)
{
   Util::exception myErr(
      Util::Format(CDK_MSG(errorResetAbort, 
                            "Unable to restart desktop \"%s\": %s").c_str(),
                   GetName().c_str(), err.what()),
      err.code());
   onAbort(cancelled, myErr);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::KillSession --
 *
 *      Proxy for BrokerXml::KillSession (log off).
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
Desktop::KillSession(Util::AbortSlot onAbort, // IN
                     Util::DoneSlot onDone)   // IN
{
   mXml.KillSession(GetSessionID(),
                    boost::bind(&Desktop::OnKillSessionAbort, this, _1, _2, onAbort),
                    boost::bind(onDone));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::OnKillSessionAbort --
 *
 *      Error handler for KillSession RPC.
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
Desktop::OnKillSessionAbort(bool cancelled,          // IN
                            Util::exception err,     // IN
                            Util::AbortSlot onAbort) // IN
{
   Util::exception myErr(
      Util::Format(CDK_MSG(errorKillSessionAbort,
                            "Unable to log out of \"%s\": %s").c_str(),
                   GetName().c_str(), err.what()),
      err.code());
   onAbort(cancelled, myErr);
}


} // namespace cdk
