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
 * securIDDlg.hh --
 *
 *    SecurID authentication dialog.
 */

#ifndef SECURID_DLG_HH
#define SECURID_DLG_HH


#include <boost/signal.hpp>
#include <gtk/gtkbutton.h>
#include <gtk/gtkentry.h>
#include <gtk/gtklabel.h>


#include "dlg.hh"
#include "util.hh"


namespace cdk {


class SecurIDDlg
   : public Dlg
{
public:
   enum State {
      STATE_PASSCODE,
      STATE_NEXT_TOKEN,
      STATE_SET_PIN,
   };

   SecurIDDlg();
   ~SecurIDDlg() { }

   void SetState(State state, Util::string first,
                 Util::string message = "", bool userSelectable = true);

   Util::string GetUsername() const { return gtk_entry_get_text(mFirstEntry); }
   Util::string GetPasscode() const { return gtk_entry_get_text(mSecondEntry); }
   std::pair<Util::string, Util::string> GetPins() const;

   boost::signal0<void> authenticate;

private:
   static void OnAuthenticate(GtkButton *button, gpointer userData);

   GtkLabel *mLabel;
   GtkLabel *mFirstLabel;
   GtkEntry *mFirstEntry;
   GtkLabel *mSecondLabel;
   GtkEntry *mSecondEntry;
};


} // namespace cdk


#endif // SECURID_DLG_HH
