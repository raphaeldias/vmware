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
 * brokerDlg.cc --
 *
 *    Broker selection dialog.
 */


#include "brokerDlg.hh"
#include "prefs.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::BrokerDlg --
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

BrokerDlg::BrokerDlg(Util::string initialBroker) // IN/OPT
   : Dlg(),
     mTable(GTK_TABLE(gtk_table_new(2, 2, false))),
     mBroker(GTK_COMBO_BOX_ENTRY(gtk_combo_box_entry_new_text())),
     mConnect(Util::CreateButton(GTK_STOCK_OK,
                                 CDK_MSG(connectBrokerDlg, "C_onnect"))),
     mQuit(Util::CreateButton(GTK_STOCK_QUIT)),
     mServer(""),
     mPort(443),
     mSecure(true)
{
   GtkLabel *l;

   Init(GTK_WIDGET(mTable));
   gtk_container_set_border_width(GTK_CONTAINER(mTable), VM_SPACING);
   gtk_table_set_row_spacings(mTable, VM_SPACING);
   gtk_table_set_col_spacings(mTable, VM_SPACING);

   l = GTK_LABEL(gtk_label_new_with_mnemonic(
      CDK_MSG(vdmServer, "_Connection Server:").c_str()));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 1, 0, 1,
                    GtkAttachOptions(0), GtkAttachOptions(0), 0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mBroker));

   gtk_widget_show(GTK_WIDGET(mBroker));
   gtk_table_attach_defaults(mTable, GTK_WIDGET(mBroker), 1, 2, 0, 1);
   g_signal_connect(G_OBJECT(mBroker), "changed",
                    G_CALLBACK(&BrokerDlg::OnChanged), this);
   // Child of a ComboBoxEntry is the entry, which has the "activate" signal.
   GtkEntry *entry = GTK_ENTRY(gtk_bin_get_child(GTK_BIN(mBroker)));
   ASSERT(entry);
   gtk_entry_set_activates_default(entry, true);
   SetFocusWidget(GTK_WIDGET(mBroker));
   AddSensitiveWidget(GTK_WIDGET(mBroker));
   AddRequiredEntry(entry);

   gtk_widget_show(GTK_WIDGET(mConnect));
   GTK_WIDGET_SET_FLAGS(mConnect, GTK_CAN_DEFAULT);
   SetForwardButton(mConnect);
   g_signal_connect(G_OBJECT(mConnect), "clicked",
                    G_CALLBACK(&BrokerDlg::OnConnect), this);

   GtkButton *cancel = GetCancelButton();

   // Quit just invokes the regular cancel button handling
   gtk_widget_show(GTK_WIDGET(mQuit));
   g_signal_connect_swapped(G_OBJECT(mQuit), "clicked",
                            G_CALLBACK(gtk_button_clicked), cancel);

   gtk_widget_hide(GTK_WIDGET(cancel));

   GtkWidget *actionArea = Util::CreateActionArea(mConnect, mQuit, cancel,
                                                  NULL);
   gtk_widget_show(actionArea);
   gtk_table_attach_defaults(mTable, GTK_WIDGET(actionArea), 0, 2, 1, 2);

   if (!initialBroker.empty()) {
      gtk_combo_box_append_text(GTK_COMBO_BOX(mBroker), initialBroker.c_str());
   }

   // Load the broker MRU list from preferences
   std::vector<Util::string> brokerMru = Prefs::GetPrefs()->GetBrokerMRU();
   for (std::vector<Util::string>::iterator i = brokerMru.begin();
        i != brokerMru.end(); i++) {
      // Don't add passed-in broker twice.
      if (*i != initialBroker) {
         gtk_combo_box_append_text(GTK_COMBO_BOX(mBroker), (*i).c_str());
      }
   }

   // Select the first entry if we added any.
   if (brokerMru.size() > 0 || !initialBroker.empty()) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(mBroker), 0);
   }

   UpdateForwardButton(this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::ParseBroker --
 *
 *      Parses the broker combo-entry.
 *
 * Results:
 *      The broker, port, and secure state are stored in mServer, mPort,
 *      and mSecure respectively.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
BrokerDlg::ParseBroker()
{
   Util::string text = Util::GetComboBoxEntryText(mBroker);
   if (text.empty()) {
      return;
   }
   xmlURIPtr parsed = xmlParseURI(text.c_str());
   if (parsed == NULL || parsed->server == NULL) {
      text = "https://" + text;
      /* xmlParseURI requires that the protocol be specified in order to parse
       * correctly.  In the event that the parsing fails, this line will add
       * the protocol in as "https://" and attempt to reparse.  If it fails
       * the second time, it will fall back to default values.
       */
      parsed = xmlParseURI(text.c_str());
   }
   if (parsed != NULL) {
      mSecure = !parsed->scheme || !strcmp(parsed->scheme, "https");
      mServer = parsed->server ? parsed->server : "";
      mPort = parsed->port ? parsed->port : (mSecure ? 443 : 80);
   } else {
      // Need some default values if parser returns NULL
      mSecure = true;
      mServer = "";
      mPort = 443;
   }
   free(parsed);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::OnChanged --
 *
 *      Callback for combo-entry changed signal.
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
BrokerDlg::OnChanged(GtkComboBox *combo, // IN/UNUSED
                     gpointer userData)  // IN
{
   BrokerDlg *that = reinterpret_cast<BrokerDlg*>(userData);
   ASSERT(that);
   that->ParseBroker();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::OnConnect --
 *
 *      Callback for Connect button click. Emits the connect signal and adds
 *      the currently entered broker text as the first broker MRU entry in
 *      the preferences.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      connect signal emitted.
 *
 *-----------------------------------------------------------------------------
 */

void
BrokerDlg::OnConnect(GtkButton *button, // IN/UNUSED
                     gpointer userData) // IN
{
   BrokerDlg *that = reinterpret_cast<BrokerDlg*>(userData);
   ASSERT(that);

   Util::string text = Util::GetComboBoxEntryText(that->mBroker);
   if (!text.empty()) {
      Prefs::GetPrefs()->AddBrokerMRU(text);
      that->connect();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::SetSensitive --
 *
 *      Calls Dlg::SetSensitive, and shows/hides the Quit/Cancel buttons based
 *      on sensitivity.
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
BrokerDlg::SetSensitive(bool sensitive) // IN
{
   Dlg::SetSensitive(sensitive);
   GtkButton *cancel = GetCancelButton();
   if (sensitive) {
      gtk_widget_show(GTK_WIDGET(mQuit));
      gtk_widget_hide(GTK_WIDGET(cancel));
   } else {
      gtk_widget_hide(GTK_WIDGET(mQuit));
      gtk_widget_show(GTK_WIDGET(cancel));
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::brokerDlg::Cancel --
 *
 *      Overrides Dlg::Cancel: emit the activate signal on a button.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Cancel/Quit button animates and cancel signal is emitted.
 *
 *-----------------------------------------------------------------------------
 */

void
BrokerDlg::Cancel()
{
   gtk_widget_activate(GTK_WIDGET(GTK_WIDGET_VISIBLE(mQuit)
                                  ? mQuit : GetCancelButton()));
}


} // namespace cdk
