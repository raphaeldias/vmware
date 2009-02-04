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
 * loginDlg.cc --
 *
 *    Login control.
 */


#include "loginDlg.hh"
#include "util.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::LoginDlg --
 *
 *      Constructor.  Assemble the login widgets in the table.  Connects the
 *      login signal to the clicked signal of the Login button.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LoginDlg::LoginDlg()
   : Dlg(),
     mTable(GTK_TABLE(gtk_table_new(4, 2, false))),
     mUsername(GTK_ENTRY(gtk_entry_new())),
     mPasswd(GTK_ENTRY(gtk_entry_new())),
     mDomain(GTK_COMBO_BOX(gtk_combo_box_new_text())),
     mLogin(Util::CreateButton(GTK_STOCK_OK,
                               CDK_MSG(login, "_Login").c_str()))
{
   GtkLabel *l;

   Init(GTK_WIDGET(mTable));
   gtk_container_set_border_width(GTK_CONTAINER(mTable), VM_SPACING);
   gtk_table_set_row_spacings(mTable, VM_SPACING);
   gtk_table_set_col_spacings(mTable, VM_SPACING);

   l = GTK_LABEL(gtk_label_new_with_mnemonic(
      CDK_MSG(username, "_Username:").c_str()));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 1, 0, 1, GTK_FILL, GTK_FILL,
                    0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mUsername));

   gtk_widget_show(GTK_WIDGET(mUsername));
   gtk_table_attach_defaults(mTable, GTK_WIDGET(mUsername), 1, 2, 0, 1);
   gtk_entry_set_activates_default(mUsername, true);
   AddRequiredEntry(mUsername);

   l = GTK_LABEL(gtk_label_new_with_mnemonic(
      CDK_MSG(password, "_Password:").c_str()));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 1, 1, 2, GTK_FILL, GTK_FILL,
                    0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mPasswd));

   gtk_widget_show(GTK_WIDGET(mPasswd));
   gtk_table_attach_defaults(mTable, GTK_WIDGET(mPasswd), 1, 2, 1, 2);
   gtk_entry_set_visibility(mPasswd, false);
   AddSensitiveWidget(GTK_WIDGET(mPasswd));
   gtk_entry_set_activates_default(mPasswd, true);
   AddRequiredEntry(mPasswd);

   l = GTK_LABEL(gtk_label_new_with_mnemonic(
      CDK_MSG(domain, "_Domain:").c_str()));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 1, 2, 3, GTK_FILL, GTK_FILL,
                    0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mDomain));

   gtk_widget_show(GTK_WIDGET(mDomain));
   gtk_table_attach_defaults(mTable, GTK_WIDGET(mDomain), 1, 2, 2, 3);
   AddSensitiveWidget(GTK_WIDGET(mDomain));

   gtk_widget_show(GTK_WIDGET(mLogin));
   GTK_WIDGET_SET_FLAGS(mLogin, GTK_CAN_DEFAULT);
   SetForwardButton(mLogin);
   g_signal_connect(G_OBJECT(mLogin), "clicked",
                    G_CALLBACK(&LoginDlg::OnLogin), this);

   GtkWidget *actionArea = Util::CreateActionArea(mLogin, GetCancelButton(),
                                                  NULL);
   gtk_widget_show(actionArea);
   gtk_table_attach_defaults(mTable, GTK_WIDGET(actionArea), 0, 2, 3, 4);

   UpdateForwardButton(this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::LoginDlg --
 *
 *      Protected constructor for subclasses. Calls the Dlg() constructor
 *      and sets member values as given.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LoginDlg::LoginDlg(GtkTable *table,
                   GtkEntry *username,
                   GtkEntry *passwd,
                   GtkComboBox *domain,
                   GtkButton *login,
                   bool userReadOnly)
   : Dlg(),
     mTable(table),
     mUsername(username),
     mPasswd(passwd),
     mDomain(domain),
     mLogin(login),
     mUserReadOnly(userReadOnly)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::SetFields --
 *
 *      Sets the widgets of the dialog with username, password, and domain
 *      values as necessary. Username field may be read-only. Sets focus
 *      to the first widget whose value wasn't supplied.
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
LoginDlg::SetFields(Util::string user,                 // IN
                    bool userReadOnly,                 // IN
                    Util::string password,             // IN
                    std::vector<Util::string> domains, // IN
                    Util::string domain)               // IN
{
   gtk_entry_set_text(mUsername, user.c_str());
   mUserReadOnly = userReadOnly;
   SetSensitive(IsSensitive());

   gtk_entry_set_text(mPasswd, password.c_str());

   unsigned int selectIndex = 0;
   unsigned int i;
   for (i = 0; i < domains.size(); i++) {
      // Add domains from the list; remember domain to select.
      gtk_combo_box_insert_text(mDomain, i, domains[i].c_str());
      if (domains[i] == domain) {
         selectIndex = i;
      }
   }
   // There must be something in the combo if we incremented i; select it.
   if (i > 0) {
      gtk_combo_box_set_active(mDomain, selectIndex);
   }

   SetFocusWidget(GTK_WIDGET(user.empty() ? mUsername : mPasswd));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::ClearAndFocusPassword --
 *
 *      Clears and focuses the password entry, so the user can try again.
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
LoginDlg::ClearAndFocusPassword()
{
   gtk_entry_set_text(mPasswd, "");
   SetFocusWidget(GTK_WIDGET(mPasswd));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::GetDomain --
 *
 *      Accessor for the domain combo-entry.
 *
 * Results:
 *      The entered/selected domain, or "" if no element of the combo is active.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
LoginDlg::GetDomain()
   const
{
   return Util::GetComboBoxText(mDomain);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::OnLogin --
 *
 *      Callback for Login button click. Emits the login signal and sets the
 *      default username & domain preferences to the currently entered values.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      login signal emitted.
 *
 *-----------------------------------------------------------------------------
 */

void
LoginDlg::OnLogin(GtkButton *button, // IN/UNUSED
                  gpointer userData) // IN
{
   LoginDlg *that = reinterpret_cast<LoginDlg*>(userData);
   ASSERT(that);
   that->login();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::SetSensitive --
 *
 *      Overrides Dlg::SetSensitive() since we need to handle
 *      mUsername's sensitivity ourself according to the most recent
 *      SetFields() call.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Widgets are sensitive - or not
 *
 *-----------------------------------------------------------------------------
 */

void
LoginDlg::SetSensitive(bool sensitive) // IN
{
   Dlg::SetSensitive(sensitive);
   gtk_widget_set_sensitive(GTK_WIDGET(mUsername),
                            !mUserReadOnly && IsSensitive());
}


} // namespace cdk
