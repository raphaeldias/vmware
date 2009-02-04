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
 * broker.hh --
 *
 *    Broker control.
 */

#ifndef BROKER_HH
#define BROKER_HH


#include <vector>


#include "brokerXml.hh"
#include "util.hh"
#include "restartMonitor.hh"
#include "tunnel.hh"


namespace cdk {


class Desktop;


class Broker
{
protected:
   Broker();
   virtual ~Broker();

protected:
   void Reset();
   // Broker XML API   
   void Initialize(const Util::string &hostname,
                   int port, bool secure,
                   const Util::string &defaultUser,
                   const Util::string &defaultDomain);
   void AcceptDisclaimer();
   void SubmitPasscode(const Util::string &username,
                       const Util::string &passcode);
   void SubmitNextTokencode(const Util::string &tokencode);
   void SubmitPins(const Util::string &pin1,
                   const Util::string &pin2);
   void SubmitPassword(const Util::string &username,
                       const Util::string &password,
                       const Util::string &domain);
   void ChangePassword(const Util::string &oldPassword,
                       const Util::string &newPassword,
                       const Util::string &confirm);
   void LoadDesktops() { RequestDesktop(mDesktops); }
   void ConnectDesktop(Desktop *desktop);
   void ReconnectDesktop();
   void Logout();

   // Status notifications
   virtual void SetBusy(const Util::string &message) = 0;
   virtual void SetReady() = 0;

   // State change notifications
   virtual void RequestBroker() = 0;
   virtual void RequestDisclaimer(const Util::string &disclaimer) = 0;
   virtual void RequestPasscode(const Util::string &username) = 0;
   virtual void RequestNextTokencode(const Util::string &username) = 0;
   virtual void RequestPinChange(const Util::string &pin,
                                 const Util::string &message,
                                 bool userSelectable) = 0;
   virtual void RequestPassword(const Util::string &username,
                                bool readOnly,
                                const std::vector<Util::string> &domains,
                                const Util::string &domain) = 0;
   virtual void RequestPasswordChange(const Util::string &username,
                                      const Util::string &domain) = 0;
   virtual void RequestDesktop(std::vector<Desktop *> &desktops) = 0;
   virtual void RequestTransition(const Util::string &message) = 0;
   virtual void RequestLaunchDesktop(Desktop *desktop) = 0;
   virtual void Quit() = 0;

   /*
    * We can't totally handle this here, as App may have an RDesktop
    * session open that it needs to ignore an exit from while it
    * displays this message.
    */
   virtual void TunnelDisconnected(Util::string disconnectReason) = 0;

   // Global preferences not yet supported...
   /*
   bool GetDoAutoLaunch() const;
   void SetDoAutoLaunch(bool enabled) const;
   */
   void CancelRequests() { ASSERT(mXml); mXml->CancelRequests(); }

private:
   bool GetTunnelReady();
   bool GetDesktopReady();
   void MaybeLaunchDesktop();

   // brokerXml onDone handlers
   void GetConfiguration();
   void OnAuthResult(BrokerXml::Result &result, BrokerXml::AuthResult &auth);
   void OnConfigurationDone(BrokerXml::Result &result,
                            BrokerXml::Configuration &config);
   void OnAuthInfo(BrokerXml::Result &result, BrokerXml::AuthInfo &authInfo,
                   bool treatOkAsPartial = false);
   void OnAuthInfoPinChange(std::vector<BrokerXml::Param> &params);
   void InitTunnel();
   void OnGetTunnelConnectionDone(BrokerXml::Tunnel &tunnel);
   void OnTunnelConnected();
   void OnTunnelDisconnect(int status, Util::string disconnectReason);
   void OnGetDesktopsDone(BrokerXml::EntitledDesktops &desktops);
   void OnLogoutResult();

   void OnAbort(bool cancelled, Util::exception err);
   void OnInitialRPCAbort(bool cancelled, Util::exception err);
   void OnTunnelRPCAbort(bool cancelled, Util::exception err);

   BrokerXml *mXml;
   std::vector<Desktop*> mDesktops;
   Tunnel *mTunnel;
   Desktop *mDesktop;
   Util::string mUsername;
   Util::string mDomain;
   boost::signals::connection mTunnelDisconnectCnx;
   RestartMonitor mTunnelMonitor;
};


} // namespace cdk


#endif // BROKER_HH
