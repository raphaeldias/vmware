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
 * rdesktop.cc --
 *
 *    Simple command line wrapper for rdesktop.
 */


#include <boost/bind.hpp>
#include <gtk/gtkmain.h>
#include <gdk/gdkkeysyms.h>
#include <stdlib.h>


#include "rdesktop.hh"


#define GRAB_RETRY_TIMEOUT_MS 250
#define SLED_10_SP2_PATCHLEVEL 2
#define PATCHLEVEL_STR "PATCHLEVEL = "
#define PATCHLEVEL_LEN strlen(PATCHLEVEL_STR)
#define CTRL_ALT_MASK (GDK_CONTROL_MASK | GDK_MOD1_MASK)


namespace cdk {


bool RDesktop::sWarnedMetacityKeybindings = false;


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RDesktop::RDesktop --
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

RDesktop::RDesktop()
   : Dlg(),
     ProcHelper(),
     mSocket(GTK_SOCKET(gtk_socket_new())),
     mGrabTimeoutId(0),
     mHasConnected(false)
{
   Init(GTK_WIDGET(mSocket));
   SetFocusWidget(GTK_WIDGET(mSocket));

   // Avoid a grey->black transition while rdesktop is starting
   GtkStyle *style = gtk_widget_get_style(GTK_WIDGET(mSocket));
   ASSERT(style);
   gtk_widget_modify_bg(GTK_WIDGET(mSocket), GTK_STATE_NORMAL, &style->black);

   g_signal_connect_after(G_OBJECT(mSocket), "plug_added",
                          G_CALLBACK(&RDesktop::OnPlugAdded), this);
   g_signal_connect_after(G_OBJECT(mSocket), "plug_removed",
                          G_CALLBACK(&RDesktop::OnPlugRemoved), this);

   g_signal_connect(G_OBJECT(mSocket), "key-press-event",
                    G_CALLBACK(&RDesktop::OnKeyPress), this);

   if (GetDisableMetacityKeybindings()) {
      onConnect.connect(boost::bind(SetMetacityKeybindingsEnabled, false));
      onExit.connect(boost::bind(SetMetacityKeybindingsEnabled, true));
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RDesktop::Start --
 *
 *      Forks & spawns the rdesktop process (respects $PATH for finding
 *      rdesktop) using ProcHelper::Start.
 *
 *      The "-p -" argument tells rdesktop to read password from stdin.  The
 *      parent process writes the password to its side of the socket along
 *      with a newline.  This avoids passing it on the command line.
 *
 *      XXX: Just use glib's gspawn?
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Spawns a child process.
 *
 *-----------------------------------------------------------------------------
 */

void
RDesktop::Start(const Util::string hostname,                       // IN
                const Util::string username,                       // IN
                const Util::string domain,                         // IN
                const Util::string password,                       // IN
                unsigned int port,                                 // IN
                const  std::vector<Util::string>& devRedirectArgs) // IN
{
   ASSERT(GTK_WIDGET_REALIZED(mSocket));
   ASSERT(!hostname.empty());

   /*
    * Set the socket "hidden" initially.  We don't want this widget to
    * resize our window before it sets itself as "fullscreen"; see bug
    * #329941.
    */
   gtk_widget_set_size_request(GTK_WIDGET(mSocket), 0, 0);

   Util::string xidArg = Util::Format("%d", gtk_socket_get_id(mSocket));
   Util::string hostPortArg = Util::Format("%s:%d", hostname.c_str(), port);

   GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(mSocket));
   ASSERT(screen);

   GdkRectangle geometry;
   gdk_screen_get_monitor_geometry(
      screen,
      gdk_screen_get_monitor_at_window(
         screen,
         gtk_widget_get_parent_window(GTK_WIDGET(mSocket))),
      &geometry);

   Util::string geomArg =
      Util::Format("%dx%d", geometry.width, geometry.height);

   Util::string depthArg;
   int depth = gdk_visual_get_best_depth();
   /*
    * rdesktop 1.6 only supports 8, 15, 16, 24, or 32, so avoid
    * passing in any values it won't understand.
    */
   switch (depth) {
   case 32:
      // but 1.4 doesn't support 32, so cap that.
      depth = 24;
      // fall through...
   case 24:
   case 16:
   case 15:
   case 8:
      depthArg = Util::Format("%d", depth);
      break;
   }

   /*
    * NOTE: Not using -P arg (store bitmap cache on disk).  It slows
    * startup with NFS home directories and can cause considerable
    * disk space usage.
    */
   std::vector<Util::string> args;
   args.push_back("-z");                                   // compress
   args.push_back("-g"); args.push_back(geomArg.c_str());  // WxH geometry
   args.push_back("-X"); args.push_back(xidArg.c_str());   // XWin to use
   args.push_back("-u"); args.push_back(username.c_str()); // Username
   args.push_back("-d"); args.push_back(domain.c_str());   // Domain
   args.push_back("-p"); args.push_back("-");              // Read passwd from stdin
   if (!depthArg.empty()) {
      // Connection colour depth
      args.push_back("-a"); args.push_back(depthArg.c_str());
   }

   args.push_back(hostPortArg.c_str());                    // hostname:port

   // Append device redirect at the end, in case of some hinky shell args
   for (std::vector<Util::string>::const_iterator i = devRedirectArgs.begin();
        i != devRedirectArgs.end(); i++) {
      args.push_back("-r"); args.push_back(*i); // device redirect
   }

   ProcHelper::Start("rdesktop", "rdesktop", args, password + "\n");
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RDesktop::OnPlugAdded --
 *
 *      Callback for "plug_added" GtkSocket signal, fired when the rdesktop
 *      embeds its window as a child of mSocket.  Calls KeyboardGrab to grab X
 *      keyboard focus, so the window manager does not capture keys that should
 *      be sent to rdesktop.
 *
 *      If KeyboardGrab did not grab input, add a timeout callback to reinvoke
 *      it until successful, every GRAB_RETRY_TIMEOUT_MS.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Keyboard is grabbed.
 *
 *-----------------------------------------------------------------------------
 */

void
RDesktop::OnPlugAdded(GtkSocket *s,      // IN
                      gpointer userData) // IN: this
{
   RDesktop *that = reinterpret_cast<RDesktop*>(userData);
   ASSERT(that);

   that->mHasConnected = true;
   that->onConnect();

   /*
    * Now that onConnect() is called, the window should be fullscreen,
    * and we should allocate our full size.
    */
   gtk_widget_set_size_request(GTK_WIDGET(s), -1, -1);

   if (that->mGrabTimeoutId > 0) {
      g_source_remove(that->mGrabTimeoutId);
      that->mGrabTimeoutId = 0;
   }

   if (that->KeyboardGrab(that)) {
      that->mGrabTimeoutId =
         g_timeout_add(GRAB_RETRY_TIMEOUT_MS, &RDesktop::KeyboardGrab, that);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RDesktop::OnPlugRemoved --
 *
 *      Callback for "plug_removed" GtkSocket signal, fired when the rdesktop's
 *      embedded window disappears. Ungrabs X keyboard focus.
 *
 * Results:
 *      false to let other handlers run.  The default destroys the socket.
 *
 * Side effects:
 *      Keyboard is ungrabbed.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
RDesktop::OnPlugRemoved(GtkSocket *s,      // IN
                        gpointer userData) // IN: this
{
   RDesktop *that = reinterpret_cast<RDesktop*>(userData);
   ASSERT(that);

   if (that->mGrabTimeoutId > 0) {
      g_source_remove(that->mGrabTimeoutId);
      that->mGrabTimeoutId = 0;
   }

   gdk_keyboard_ungrab(gtk_get_current_event_time());

   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RDesktop::KeyboardGrab --
 *
 *      Timeout callback to call gdk_keyboard_grab for rdesktop's GtkSocket.
 *      Specify false for owner_events so that hooked keys on the root window
 *      are not forwarded (e.g. SuSE's Computer menu).  The GtkSocket will
 *      receive all key events and forward them on to the embedded rdesktop
 *      window.
 *
 * Results:
 *      true if grabbing failed (reinvoke callback), false if successful.
 *
 * Side effects:
 *      Keyboard may be grabbed.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
RDesktop::KeyboardGrab(gpointer userData) // IN: this
{
   RDesktop *that = reinterpret_cast<RDesktop*>(userData);
   ASSERT(that);

   GdkGrabStatus res = gdk_keyboard_grab(GTK_WIDGET(that->mSocket)->window,
                                         false, gtk_get_current_event_time());
   switch (res) {
   case GDK_GRAB_SUCCESS:
      that->mGrabTimeoutId = 0;
      return false; // success
   case GDK_GRAB_ALREADY_GRABBED:
      Log("Keyboard grab failed (already grabbed). Retrying after timeout.\n");
      return true; // retry
   default:
      NOT_IMPLEMENTED();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RDesktop::OnKeyPress --
 *
 *      Handle keypress events on the GtkSocket window.  Passes through all
 *      events except Ctrl-Alt-Enter, which will cause rdesktop to crash.
 *
 * Results:
 *      true to stop other handlers from being invoked for the event.
 *      false to propogate the event further.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

gboolean
RDesktop::OnKeyPress(GtkWidget *widget, // IN/UNUSED
                     GdkEventKey *evt,  // IN
                     gpointer userData) // IN
{
   /*
    * NOTE: rdesktop checks for Ctrl_L/R and Alt_L/R non-exclusively, so we
    * match this behavior here. Unfortunately, this means we'll inhibit more
    * events than we would prefer.
    */
   if (GDK_Return == evt->keyval &&
       (evt->state & CTRL_ALT_MASK) == CTRL_ALT_MASK) {
      Util::UserWarning("Inhibiting Ctrl-Alt-Enter keypress, to avoid "
                        "rdesktop exit.\n");
      return true;
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RDesktop::SetMetacityKeybindingsEnabled --
 *
 *      Disable or enable Metacity's keybindings using the
 *      "metacity-message" tool.
 *
 *      This is due to metacity-dnd-swithing.diff in SLED 10 SP2
 *      (metacity-2.12.3-0.15).  This patch makes metacity use the XKB
 *      for some of its bindings, and it is difficult to prevent
 *      metacity from handling them while rdesktop is running.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Keybindings may be dis/enabled.
 *
 *-----------------------------------------------------------------------------
 */

void
RDesktop::SetMetacityKeybindingsEnabled(bool enabled) // IN
{
   Log("%s Metacity keybindings using metacity-message.\n",
       enabled ? "Enabling" : "Disabling");
   ProcHelper *mmsg = new ProcHelper();
   mmsg->onExit.connect(boost::bind(OnMetacityMessageExit, mmsg));
   std::vector<Util::string> args;
   args.push_back(enabled ? "enable-keybindings" : "disable-keybindings");
   mmsg->Start("metacity-message", "metacity-message", args);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RDesktop::OnMetacityMessageExit --
 *
 *      Exit handler for Metacity message ProcHelper: delete the
 *      helper.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Helper is deleted.
 *
 *-----------------------------------------------------------------------------
 */

void
RDesktop::OnMetacityMessageExit(ProcHelper *helper) // IN
{
   delete helper;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RDesktop::GetDisableMetacityKeybindings --
 *
 *      Determines whether this system's Metacity is likely to be
 *      broken and needs its keybindings disabled manually.
 *
 * Results:
 *      true if Metacity has a broken keybindings patch.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
RDesktop::GetDisableMetacityKeybindings()
{
   char *contents = NULL;
   bool ret = false;

   if (g_file_get_contents("/etc/SuSE-release", &contents, NULL, NULL) &&
       strstr(contents, "SUSE Linux Enterprise Desktop 10")) {

      char *relStr = strstr(contents, PATCHLEVEL_STR);
      if (relStr) {
         uint32 rel = strtoul(relStr + PATCHLEVEL_LEN, NULL, 10);
         ret = rel >= SLED_10_SP2_PATCHLEVEL;
      }
      if (ret && !sWarnedMetacityKeybindings) {
         Util::UserWarning("Metacity keybings will be temporarily disabled "
                           "on SLED 10 SP2.\n");
         sWarnedMetacityKeybindings = true;
      }
   }

   g_free(contents);
   return ret;
}


} // namespace cdk
