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
 * disclaimerDlg.hh --
 *
 *    Shows disclaimer for user to accept.
 */

#ifndef DISCLAIMER_DLG_HH
#define DISCLAIMER_DLG_HH


#include <boost/signal.hpp>
#include <gtk/gtktextview.h>


#include "dlg.hh"
#include "util.hh"


namespace cdk {


class DisclaimerDlg
   : public Dlg
{
public:
   DisclaimerDlg();
   ~DisclaimerDlg() { }

   void SetText(const Util::string text);

   // Overridden from Dlg
   bool IsResizable() const { return true; }

   boost::signal0<void> accepted;

private:
   static void OnAccept(GtkButton *button, gpointer userData);

   GtkTextView *mView;
};


} // namespace cdk


#endif // DISCLAIMER_DLG_HH
