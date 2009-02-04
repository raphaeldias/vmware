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
 * broker.cc --
 *
 *    Broker control.
 */


#include <boost/bind.hpp>


#include "app.hh"
#include "broker.hh"
#include "desktop.hh"
#include "tunnel.hh"
#include "util.hh"

extern "C" {
#include "vmlocale.h"
}


#define ERR_UNSUPPORTED_VERSION "UNSUPPORTED_VERSION"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::Broker --
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

Broker::Broker()
   : mXml(NULL),
     mTunnel(NULL),
     mDesktop(NULL)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::~Broker --
 *
 *      Destructor.  Deletes all connected desktops asynchronously.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Broker::~Broker()
{
   Reset();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::Reset --
 *
 *      Reset state to allow a new login.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      State is restored as if a new broker was created.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::Reset()
{
   for (std::vector<Desktop*>::iterator i = mDesktops.begin();
        i != mDesktops.end(); i++) {
      delete *i;
   }
   mDesktops.clear();
   // already deleted above
   mDesktop = NULL;

   mTunnelDisconnectCnx.disconnect();
   mTunnelMonitor.Reset();
   delete mTunnel;
   mTunnel = NULL;

   delete mXml;
   mXml = NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::Initialize --
 *
 *      Initialize the broker connection, calling BrokerXml::SetLocale and
 *      BrokerXml::GetConfiguration.
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
Broker::Initialize(const Util::string &hostname,      // IN
                   int port,                          // IN
                   bool secure,                       // IN
                   const Util::string &defaultUser,   // IN
                   const Util::string &defaultDomain) // IN
{
   ASSERT(!mXml);
   ASSERT(!mTunnel);

   Log("Initialzing connection to broker %s://%s:%d\n",
       secure ? "https" : "http", hostname.c_str(), port);

   mXml = new BrokerXml(hostname, port, secure);
   mUsername = defaultUser;
   mDomain = defaultDomain;
   /*
    * The SetLocale RPC is only supported by protocol 2.0, so it'd be
    * nice not to send it to 1.0 servers.  Sadly, it's the first RPC
    * we send, so we don't know what version the server is... yet.
    */
   char *locale = Locale_GetUserLanguage();
   if (locale) {
      Util::string localeUtf = locale;
      free(locale);

      SetBusy(CDK_MSG(settingLocale, "Setting client locale..."));
      mXml->SetLocale(localeUtf,
                      boost::bind(&Broker::OnInitialRPCAbort, this, _1, _2),
                      boost::bind(&Broker::GetConfiguration, this));
   } else {
      GetConfiguration();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::AcceptDisclaimer --
 *
 *      Notify the broker that the user has accepted the disclaimer.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::AcceptDisclaimer()
{
   SetBusy(CDK_MSG(acceptingDisclaimer, "Accepting disclaimer..."));
   mXml->AcceptDisclaimer(
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::SubmitPasscode --
 *
 *      Attempt authentication using a SecurID passcode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::SubmitPasscode(const Util::string &username, // IN
                       const Util::string &passcode) // IN
{
   SetBusy(CDK_MSG(authenticatingPasscode, "Logging in..."));
   mUsername = username;
   mXml->SecurIDUsernamePasscode(
      username, passcode,
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::SubmitNextTokencode --
 *
 *      Continue authentication by providing the next tokencode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::SubmitNextTokencode(const Util::string &tokencode) // IN
{
   SetBusy(CDK_MSG(authenticatingNextTokencode, "Logging in..."));
   mXml->SecurIDNextTokencode(
      tokencode,
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::SubmitPins --
 *
 *      Continue authentication by providing new PINs.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::SubmitPins(const Util::string &pin1, // IN
                   const Util::string &pin2) // IN
{
   SetBusy(CDK_MSG(authenticatingPins, "Logging in..."));
   mXml->SecurIDPins(
      pin1, pin2,
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::SubmitPassword --
 *
 *      Authenticate with a windows username and password.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::SubmitPassword(const Util::string &username, // IN
                       const Util::string &password, // IN
                       const Util::string &domain)   // IN
{
   SetBusy(CDK_MSG(authenticatingPassword, "Logging in..."));
   mUsername = username;
   mDomain = domain;
   mXml->PasswordAuthentication(
      username, password, domain,
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::ChangePassword --
 *
 *      Provide a new password for the user.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::ChangePassword(const Util::string &oldPassword, // IN
                       const Util::string &newPassword, // IN
                       const Util::string &confirm)     // IN
{
   SetBusy(CDK_MSG(changingPassword, "Changing password..."));
   mXml->ChangePassword(oldPassword, newPassword, confirm,
                        boost::bind(&Broker::OnAbort, this, _1,_2),
                        boost::bind(&Broker::OnAuthResult, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::GetDesktopConnection --
 *
 *      Begin connecting to a desktop.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::ConnectDesktop(Desktop *desktop) // IN
{
   ASSERT(desktop);
   ASSERT(desktop->GetConnectionState() == Desktop::STATE_DISCONNECTED);

   mDesktop = desktop;
   RequestTransition(CDK_MSG(transitionConnecting,
                             "Connecting to the desktop..."));
   if (!mTunnel) {
      InitTunnel();
   } else if (mTunnel->GetIsConnected()) {
      /*
       * Connecting to the desktop before the tunnel is connected
       * results in DESKTOP_NOT_AVAILABLE.
       */
      desktop->Connect(boost::bind(&Broker::OnAbort, this, _1, _2),
                       boost::bind(&Broker::MaybeLaunchDesktop, this));
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::ReconnectDesktop --
 *
 *      Initiate a desktop reconnection (rdesktop or the tunnel may
 *      have died).  If this is called because the tunnel died, the
 *      server would tell us that it's unable to reconnect to the
 *      desktop, so we defer the actual connection until we reconnect
 *      the tunnel.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop may be connected.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::ReconnectDesktop()
{
   ASSERT(mDesktop);
   ASSERT(mDesktop->GetConnectionState() != Desktop::STATE_CONNECTING);

   if (mDesktop->GetConnectionState() == Desktop::STATE_CONNECTED) {
      mDesktop->Disconnect();
   }
   ConnectDesktop(mDesktop);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::Logout --
 *
 *      Notify the broker that we are done with this session.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::Logout()
{
   SetBusy(CDK_MSG(logginOut, "Logging out..."));
   mXml->Logout(boost::bind(&Broker::OnAbort, this, _1, _2),
                boost::bind(&Broker::OnLogoutResult, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::GetConfiguration --
 *
 *      Initiate a GetConfiguration RPC to the broker.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      A GetConfiguration RPC in flight, or onAbort is called.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::GetConfiguration()
{
   SetBusy(CDK_MSG(gettingConfiguration, "Getting server configuration..."));
   mXml->GetConfiguration(
      boost::bind(&Broker::OnInitialRPCAbort, this, _1, _2),
      boost::bind(&Broker::OnConfigurationDone, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnAuthResult --
 *
 *      Handle an AuthResult from the broker.  Simply pass the
 *      AuthInfo off to OnAuthInfo.
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
Broker::OnAuthResult(BrokerXml::Result &result,   // IN
                     BrokerXml::AuthResult &auth) // IN
{
   OnAuthInfo(result, auth.authInfo);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnConfigurationDone --
 *
 *      Handle a Configuration from the broker.  Simply pass the
 *      AuthInfo off to OnAuthInfo.
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
Broker::OnConfigurationDone(BrokerXml::Result &result,        // IN
                            BrokerXml::Configuration &config) // IN
{
   OnAuthInfo(result, config.authInfo, true);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnAuthInfo --
 *
 *      Handles the reply to an RPC that returns AuthInfo.  Either
 *      displays an error and stays on the same state, or continues
 *      based on what the server requires next.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      New UI page may be requested and enables the UI.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnAuthInfo(BrokerXml::Result &result,     // IN
                   BrokerXml::AuthInfo &authInfo, // IN
                   bool treatOkAsPartial)         // IN
{
   Log("Auth Info: Name: %s, result: %s\n",
       authInfo.name.c_str(), result.result.c_str());
   if (result.result == "ok" && !treatOkAsPartial) {
         /*
          * If this is a 1.0 broker, it'll get upset if we send both
          * the tunnel and desktop list requests at the same time. So
          * we'll set up the tunnel after we get the desktop list. See
          * bug 311999.
          */
      if (mXml->GetBrokerVersion() != BrokerXml::VERSION_1) {
         InitTunnel();
      }
      mXml->GetDesktops(
         boost::bind(&Broker::OnAbort, this, _1, _2),
         boost::bind(&Broker::OnGetDesktopsDone, this, _2));
      SetBusy(CDK_MSG(gettingDesktops, "Getting desktop list..."));
   } else if (result.result == "partial" ||
              (result.result == "ok" && treatOkAsPartial)) {
      SetReady();
      const Util::string error = authInfo.GetError();
      if (!error.empty()) {
         App::ShowDialog(GTK_MESSAGE_ERROR,
                         CDK_MSG(authError, "Error authenticating: %s"),
                         error.c_str());
      }
      switch (authInfo.GetAuthType()) {
      case BrokerXml::AUTH_DISCLAIMER:
         RequestDisclaimer(authInfo.GetDisclaimer());
         break;
      case BrokerXml::AUTH_SECURID_PASSCODE:
         RequestPasscode(mUsername);
         break;
      case BrokerXml::AUTH_SECURID_NEXTTOKENCODE:
         RequestNextTokencode(mUsername);
         break;
      case BrokerXml::AUTH_SECURID_PINCHANGE:
         // This is a bit complicated, so defer to another function
         OnAuthInfoPinChange(authInfo.params);
         break;
      case BrokerXml::AUTH_SECURID_WAIT:
         App::ShowDialog(GTK_MESSAGE_INFO,
                         CDK_MSG(securIDWait,
                                 "Your new RSA SecurID PIN has been set.\n\n"
                                 "Please wait for the next tokencode to appear"
                                 " on your RSA SecurID token, then continue."));
         RequestPasscode(mUsername);
         break;
      case BrokerXml::AUTH_WINDOWS_PASSWORD: {
         bool readOnly = false;
         Util::string user = authInfo.GetUsername(&readOnly);
         RequestPassword(user.empty() ? mUsername : user, readOnly,
                         authInfo.GetDomains(), mDomain);
         break;
      }
      case BrokerXml::AUTH_WINDOWS_PASSWORD_EXPIRED:
         mUsername = authInfo.GetUsername();
         RequestPasswordChange(mUsername, mDomain);
         break;
      default:
         App::ShowDialog(
            GTK_MESSAGE_ERROR,
            CDK_MSG(unknownAuthType,
                     "Unknown authentication method requested: %s"),
            authInfo.name.c_str());
         RequestBroker();
         break;
      }
   } else {
      App::ShowDialog(
         GTK_MESSAGE_ERROR,
         CDK_MSG(unknownResult,
                 "Unknown result returned: %s"),
         result.result.c_str());
      SetReady();
      RequestBroker();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnAuthInfoPinChange --
 *
 *      Handle a response indicating the user needs to change their
 *      PIN.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Displays PIN change page.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnAuthInfoPinChange(std::vector<BrokerXml::Param> &params) // IN
{
   Util::string message = "";
   Util::string pin = "";
   bool userSelectable = true;
   for (std::vector<BrokerXml::Param>::iterator i = params.begin();
        i != params.end(); i++) {
      // Just assume a single value; that's currently always the case.
      if (i->values.size() != 1) {
         break;
      }
      Util::string value = i->values[0];
      if (i->name == "user-selectable") {
         userSelectable = value != "CANNOT_CHOOSE_PIN";
      } else if (i->name == "message") {
         message = value;
      } else if (i->name == "pin1") {
         pin = value;
      }
      // Ignore other param names, like "error" (which we've already handled).
   }
   if (!userSelectable && pin.empty()) {
      App::ShowDialog(
         GTK_MESSAGE_ERROR,
         CDK_MSG(invalidParams,
                  "Invalid PIN Change response sent by server."));
   } else {
      RequestPinChange(pin, message, userSelectable);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::InitTunnel --
 *
 *      Get tunnel connection info from the broker.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      get-tunnel-connection RPC dispatched.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::InitTunnel()
{
   ASSERT(!mTunnel);

   mTunnel = new Tunnel();
   mTunnel->onReady.connect(boost::bind(&Broker::OnTunnelConnected, this));
   mTunnelDisconnectCnx = mTunnel->onDisconnect.connect(
      boost::bind(&Broker::OnTunnelDisconnect, this, _1, _2));

   mXml->GetTunnelConnection(
      boost::bind(&Broker::OnTunnelRPCAbort, this, _1, _2),
      boost::bind(&Tunnel::Connect, mTunnel, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnTunnelConnected --
 *
 *      Callback when a tunnel has been created and connected.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The tunnel disconnecting will now exit; if the user was
 *      waiting for the tunnel, connect to the desktop.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnTunnelConnected()
{
   ASSERT(mTunnel);
   ASSERT(mTunnel->GetIsConnected());
   if (mDesktop &&
       mDesktop->GetConnectionState() == Desktop::STATE_DISCONNECTED) {
      ConnectDesktop(mDesktop);
   } else {
      MaybeLaunchDesktop();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnTunnelDisconnect --
 *
 *      Handler for the tunnel exiting.  If it was due to an error,
 *      restart it (if it isn't throttled).
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
Broker::OnTunnelDisconnect(int status,                    // IN
                           Util::string disconnectReason) // IN
{
   delete mTunnel;
   mTunnel = NULL;
   if (disconnectReason.empty() && status &&
       !mTunnelMonitor.ShouldThrottle()) {
      InitTunnel();
   } else {
      TunnelDisconnected(disconnectReason);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnGetDesktopsDone --
 *
 *      Handler for getting the list of desktops.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Displays desktop list, enables UI.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnGetDesktopsDone(BrokerXml::EntitledDesktops &desktops) // IN
{
   // Since we didn't do this in OnAuthInfo, let's do it now.
   if (mXml->GetBrokerVersion() == BrokerXml::VERSION_1 && !mTunnel) {
      InitTunnel();
   }

   std::vector<Desktop*> newDesktops;
   for (BrokerXml::DesktopList::iterator i = desktops.desktops.begin();
        i != desktops.desktops.end(); i++) {
      newDesktops.push_back(new Desktop(*mXml, *i));
   }

   mDesktops = newDesktops;
   SetReady();
   RequestDesktop(mDesktops);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::MaybeLaunchDesktop --
 *
 *      Callback for getting a desktop connection.  If we are all set
 *      to start the desktop, do so.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      rdesktop session may be requested.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::MaybeLaunchDesktop()
{
   if (GetTunnelReady() && GetDesktopReady()) {
      RequestLaunchDesktop(mDesktop);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnLogoutResult --
 *
 *      Handler for Logout RPC.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Exits.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnLogoutResult()
{
   SetReady();
   Quit();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnAbort --
 *
 *      Handle an error from an RPC.  This could be the user
 *      cancelling, a network error, an error returned by the broker
 *      to an RPC, or some unhandled or unexpected response by the
 *      broker.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Enables the UI.  If the user didn't cancel, displays an error
 *      and may go back to the broker page.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnAbort(bool cancelled,      // IN
                Util::exception err) // IN
{
   SetReady();
   if (cancelled) {
      return;
   }
   if (err.code() == "AUTHENTICATION_FAILED") {
      RequestBroker();
      App::ShowDialog(GTK_MESSAGE_ERROR,
         CDK_MSG(brokerAuthenticationFailed,
                 "Maximum authentication attempts reached. "
                 "The View server has logged you out."));
   } else if (err.code() == "NOT_AUTHENTICATED") {
      RequestBroker();
      CDK_MSG(brokerNotAuthenticated,
              "The View server has logged you out.");
   } else {
      App::ShowDialog(GTK_MESSAGE_ERROR, "%s", err.what());
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnInitialRPCAbort --
 *
 *      Failure handler for our first request.  If the server doesn't
 *      support this RPC, drop the broker into protocol 1.0 mode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      mXml may be set to protocol 1.0.
 *      GetConfiguration RPC is issued.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnInitialRPCAbort(bool cancelled,      // IN
                          Util::exception err) // IN
{
   if (!cancelled && err.code() == ERR_UNSUPPORTED_VERSION &&
       mXml->GetBrokerVersion() == BrokerXml::VERSION_2) {
      // Don't retry, as 1.0 doesn't support SetLocale at all.
      mXml->SetBrokerVersion(BrokerXml::VERSION_1);
      GetConfiguration();
   } else {
      Reset();
      OnAbort(cancelled, err);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnTunnelRPCAbort --
 *
 *      RPC abort handler for tunnel connection RPCs.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Deletes mTunnel.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnTunnelRPCAbort(bool cancelled,      // IN
                         Util::exception err) // IN
{
   delete mTunnel;
   mTunnel = NULL;
   OnAbort(cancelled, err);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::GetTunnelReady --
 *
 *      Determine whether we're waiting on a tunnel connection.
 *
 * Results:
 *      true if the tunnel is not required, or is connected.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Broker::GetTunnelReady()
{
   return mTunnel && mTunnel->GetIsConnected();
}

/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::GetDesktopReady --
 *
 *      Determine whether we're waiting on a desktop connection.
 *
 * Results:
 *      true if we have a connected desktop.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Broker::GetDesktopReady()
{
   return mDesktop &&
      mDesktop->GetConnectionState() == Desktop::STATE_CONNECTED;
}


} // namespace cdk
