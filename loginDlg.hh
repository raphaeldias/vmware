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
 * loginDlg.hh --
 *
 *    Login control.
 */

#ifndef LOGIN_DLG_HH
#define LOGIN_DLG_HH


#include <boost/signal.hpp>

extern "C" {
#include <gtk/gtk.h>
}


#include "dlg.hh"
#include "util.hh"


namespace cdk {


class LoginDlg
   : public Dlg
{
public:
   LoginDlg();
   virtual ~LoginDlg() { }

   Util::string GetUsername() const { return gtk_entry_get_text(mUsername); }
   Util::string GetPassword() const { return gtk_entry_get_text(mPasswd); }
   void SetFields(Util::string user, bool userReadOnly, Util::string password,
                  std::vector<Util::string> domains, Util::string domain);
   virtual void ClearAndFocusPassword();
   Util::string GetDomain() const;
   void SetSensitive(bool sensitive);

   boost::signal0<void> login;

protected:
   LoginDlg(GtkTable *table, GtkEntry *username, GtkEntry *passwd,
            GtkComboBox *domain, GtkButton *login, bool userReadOnly);

   static void OnLogin(GtkButton *button, gpointer userData);

   GtkTable *mTable;
   GtkEntry *mUsername;
   GtkEntry *mPasswd;
   GtkComboBox *mDomain;
   GtkButton *mLogin;

private:
   bool mUserReadOnly;
};


} // namespace cdk


#endif // LOGIN_DLG_HH
