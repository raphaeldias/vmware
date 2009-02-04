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
 * passwordDlg.cc --
 *
 *    Password change dialog.
 */


#include "passwordDlg.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::PasswordDlg::PasswordDlg --
 *
 *      Constructor. Assemble the widgets for changing a user password
 *      in the table.  Connects the login signal to the clicked signal of 
 *      the Change button.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

PasswordDlg::PasswordDlg()
   : LoginDlg(GTK_TABLE(gtk_table_new(6, 2, false)),
              GTK_ENTRY(gtk_entry_new()),
              GTK_ENTRY(gtk_entry_new()),
              GTK_COMBO_BOX(gtk_combo_box_new_text()),
              Util::CreateButton(GTK_STOCK_OK,
                                 CDK_MSG(changeButton, "C_hange").c_str()),
              true),
     mNew(GTK_ENTRY(gtk_entry_new())),
     mConfirm(GTK_ENTRY(gtk_entry_new()))
{
   GtkLabel *l;

   Init(GTK_WIDGET(mTable));
   gtk_container_set_border_width(GTK_CONTAINER(mTable), VM_SPACING);
   gtk_table_set_row_spacings(mTable, VM_SPACING);
   gtk_table_set_col_spacings(mTable, VM_SPACING);

   l = GTK_LABEL(gtk_label_new(CDK_MSG(passwordDlgUsername,
                                        "Username:").c_str()));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 1, 0, 1, GTK_FILL, GTK_FILL,
                    0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);

   gtk_widget_show(GTK_WIDGET(mUsername));
   gtk_table_attach_defaults(mTable, GTK_WIDGET(mUsername), 1, 2, 0, 1);
   gtk_widget_set_sensitive(GTK_WIDGET(mUsername), false);

   l = GTK_LABEL(gtk_label_new(CDK_MSG(passwordDlgDomain, "Domain:").c_str()));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 1, 1, 2, GTK_FILL, GTK_FILL,
                    0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);

   gtk_widget_show(GTK_WIDGET(mDomain));
   gtk_table_attach_defaults(mTable, GTK_WIDGET(mDomain), 1, 2, 1, 2);
   gtk_widget_set_sensitive(GTK_WIDGET(mDomain), false);

   l = GTK_LABEL(gtk_label_new_with_mnemonic(
      CDK_MSG(passwordDlgOldPassword, "Old _Password:").c_str()));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 1, 2, 3, GTK_FILL, GTK_FILL,
                    0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mPasswd));

   gtk_widget_show(GTK_WIDGET(mPasswd));
   gtk_table_attach_defaults(mTable, GTK_WIDGET(mPasswd), 1, 2, 2, 3);
   gtk_entry_set_visibility(mPasswd, false);
   AddSensitiveWidget(GTK_WIDGET(mPasswd));
   gtk_entry_set_activates_default(mPasswd, true);
   AddRequiredEntry(mPasswd);

   l = GTK_LABEL(gtk_label_new_with_mnemonic(
      CDK_MSG(passwordDlgNewPassword, "_New Password:").c_str()));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 1, 3, 4, GTK_FILL, GTK_FILL,
                    0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mNew));

   gtk_widget_show(GTK_WIDGET(mNew));
   gtk_table_attach_defaults(mTable, GTK_WIDGET(mNew), 1, 2, 3, 4);
   gtk_entry_set_visibility(mNew, false);
   AddSensitiveWidget(GTK_WIDGET(mNew));
   gtk_entry_set_activates_default(mNew, true);
   AddRequiredEntry(mNew);

   l = GTK_LABEL(gtk_label_new_with_mnemonic(
      CDK_MSG(passwordDlgConfirmPassword, "Con_firm:").c_str()));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 1, 4, 5, GTK_FILL, GTK_FILL,
                    0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mConfirm));

   gtk_widget_show(GTK_WIDGET(mConfirm));
   gtk_table_attach_defaults(mTable, GTK_WIDGET(mConfirm), 1, 2, 4, 5);
   gtk_entry_set_visibility(mConfirm, false);
   AddSensitiveWidget(GTK_WIDGET(mConfirm));
   gtk_entry_set_activates_default(mConfirm, true);
   AddRequiredEntry(mConfirm);

   gtk_widget_show(GTK_WIDGET(mLogin));
   GTK_WIDGET_SET_FLAGS(mLogin, GTK_CAN_DEFAULT);
   SetForwardButton(mLogin);
   g_signal_connect(G_OBJECT(mLogin), "clicked",
                    G_CALLBACK(&PasswordDlg::OnLogin), this);

   GtkWidget *actionArea = Util::CreateActionArea(mLogin, GetCancelButton(),
                                                  NULL);
   gtk_widget_show(actionArea);
   gtk_table_attach_defaults(mTable, GTK_WIDGET(actionArea), 0, 2, 5, 6);

   UpdateForwardButton(this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::PasswordDlg::GetNewPassword --
 *
 *      Returns the pair of strings corresponding to the desired new password
 *      and the re-typed confirmation of the password.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

std::pair<Util::string, Util::string>
PasswordDlg::GetNewPassword()
   const
{
   return std::pair<Util::string, Util::string>(
      gtk_entry_get_text(mNew), gtk_entry_get_text(mConfirm));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::PasswordDlg::ClearAndFocusPassword --
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
PasswordDlg::ClearAndFocusPassword()
{
   gtk_entry_set_text(mPasswd, "");
   gtk_entry_set_text(mNew, "");
   gtk_entry_set_text(mConfirm, "");

   SetFocusWidget(GTK_WIDGET(mPasswd));
}


} // namespace cdk
