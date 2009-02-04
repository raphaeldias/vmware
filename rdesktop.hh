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
 * rdesktop.hh --
 *
 *    Simple command line wrapper for rdesktop.
 *
 */

#ifndef RDESKTOP_HH
#define RDESKTOP_HH


#include <boost/signal.hpp>
#include <gtk/gtksocket.h>
#include <vector>


#include "dlg.hh"
#include "procHelper.hh"
#include "util.hh"


namespace cdk {


class RDesktop
   : public Dlg,
     public ProcHelper
{
public:
   RDesktop();

   void Start(const Util::string hostname,
              const Util::string username,
              const Util::string domain,
              const Util::string password,
              unsigned int port = 3389,
              const std::vector<Util::string> &devRedirectArgs =
                 std::vector<Util::string>());
   bool IsResizable() const { return true; }

   bool GetHasConnected() const { return mHasConnected; }

   boost::signal0<void> onConnect;

private:
   static void OnPlugAdded(GtkSocket *s, gpointer userData);
   static gboolean OnPlugRemoved(GtkSocket *s, gpointer userData);
   static gboolean KeyboardGrab(gpointer userData);
   static gboolean OnKeyPress(GtkWidget *widget, GdkEventKey *evt,
                              gpointer userData);
   static void SetMetacityKeybindingsEnabled(bool enabled);
   static void OnMetacityMessageExit(ProcHelper *helper);
   static bool GetDisableMetacityKeybindings();

   static bool sWarnedMetacityKeybindings;

   GtkSocket *mSocket;
   guint mGrabTimeoutId;
   bool mHasConnected;
};


} // namespace cdk


#endif // RDESKTOP_HH
