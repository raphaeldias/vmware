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
 * brokerDlg.hh --
 *
 *    Broker selection dialog.
 */

#ifndef BROKER_DLG_HH
#define BROKER_DLG_HH


#include <boost/signal.hpp>

extern "C" {
#include <gtk/gtk.h>
#include <libxml/uri.h>
}


#include "dlg.hh"
#include "util.hh"


namespace cdk {


class BrokerDlg
   : public Dlg
{
public:
   BrokerDlg(Util::string initialBroker = "");
   ~BrokerDlg() { }

   Util::string GetBroker() const { return mServer; }
   short unsigned int GetPort() const { return mPort; }
   bool GetSecure() const { return mSecure; }
   virtual void SetSensitive(bool sensitive);

   // overrides Dlg::Cancel()
   void Cancel();

   boost::signal0<void> connect;

private:
   static void OnConnect(GtkButton *button, gpointer userData);
   static void OnChanged(GtkComboBox *combo, gpointer userData);

   void ParseBroker();

   GtkTable *mTable;
   GtkComboBoxEntry *mBroker;
   GtkButton *mConnect;
   GtkButton *mQuit;

   Util::string mServer;
   short unsigned int mPort;
   bool mSecure;
};


} // namespace cdk


#endif // BROKER_DLG_HH
