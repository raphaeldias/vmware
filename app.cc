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
 * app.cc --
 *
 *    Application singleton object. It handles initialization of global
 *    libraries and resources.
 *
 */


#include <boost/bind.hpp>


#include "app.hh"
#include "disclaimerDlg.hh"
#include "icons/spinner_anim.h"
#define SPINNER_ANIM_N_FRAMES 20
#define SPINNER_ANIM_FPS_RATE 10
#include "view_16x.h"
#include "view_32x.h"
#include "view_48x.h"
#include "view_client_banner.h"
#include "loginDlg.hh"
#include "passwordDlg.hh"
#include "prefs.hh"
#include "securIDDlg.hh"
#include "transitionDlg.hh"
#include "tunnel.hh"

#include "basicHttp.h"

#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>

extern "C" {
#include "vm_basic_types.h"
#include "vm_version.h"
#include "log.h"
#include "msg.h"
#include "poll.h"
#include "preference.h"
#include "productState.h"
#include "sig.h"
#include "ssl.h"
#include "unicode.h"
#include "vmlocale.h"
#include "vthread.h"
}


/*
 * Define and use these alternate PRODUCT_VIEW_* names until vm_version.h
 * uses the View naming scheme.
 */
#define PRODUCT_VIEW_SHORT_NAME "View"
#define PRODUCT_VIEW_NAME MAKE_NAME("View Manager")
#define PRODUCT_VIEW_CLIENT_NAME_FOR_LICENSE PRODUCT_VIEW_CLIENT_NAME


namespace cdk {


/*
 * Initialise static data.
 */

App *App::sApp = NULL;

gchar *App::sOptBroker = NULL;
gchar *App::sOptUser = NULL;
gchar *App::sOptPassword = NULL;
gchar *App::sOptDomain = NULL;
gchar *App::sOptDesktop = NULL;
gboolean App::sOptNonInteractive = false;
gboolean App::sOptFullscreen = false;
gchar *App::sOptBackground = NULL;
gchar *App::sOptFile = NULL;
gchar **App::sOptRedirect = NULL;
gboolean App::sOptVersion = false;


GOptionEntry App::sOptEntries[] =
{
   { "serverURL", 's', 0, G_OPTION_ARG_STRING, &sOptBroker,
     "Specify connection broker.", "<broker URL>" },
   { "userName", 'u', 0, G_OPTION_ARG_STRING, &sOptUser,
     "Specify user name for password authentication.", "<user name>" },
   { "password", 'p', 0, G_OPTION_ARG_STRING, &sOptPassword,
     "Specify password for password authentication.", "<password>" },
   { "domainName", 'd', 0, G_OPTION_ARG_STRING, &sOptDomain,
     "Specify domain for password authentication.", "<domain name>" },
   { "desktopName", 'n', 0, G_OPTION_ARG_STRING, &sOptDesktop,
     "Specify desktop by name.", "<desktop name>" },
   { "nonInteractive", 'q', 0, G_OPTION_ARG_NONE, &sOptNonInteractive,
     "Connect automatically if enough values are given on the command line.",
     NULL },
   { "fullscreen", '\0', 0, G_OPTION_ARG_NONE, &sOptFullscreen,
     "Enable fullscreen mode.", NULL },
   { "background", 'b', 0 , G_OPTION_ARG_STRING, &sOptBackground,
     "Image file to use as background in fullscreen mode.", "<image>" },
   { "redirect", 'r', 0 , G_OPTION_ARG_STRING_ARRAY, &sOptRedirect,
     "Forward device redirection to rdesktop", "<device info>" },
   { "version", '\0', 0, G_OPTION_ARG_NONE, &sOptVersion,
     "Display version information and exit.", NULL },
   { NULL }
};


GOptionEntry App::sOptFileEntries[] =
{
   { "file", 'f', 0 , G_OPTION_ARG_STRING, &sOptFile,
     "File containing additional command line arguments.", "<file path>" },
   { NULL }
};


/*
 *-------------------------------------------------------------------
 *
 * cdk::App::App --
 *
 *      Constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-------------------------------------------------------------------
 */

App::App(int argc,    // IN:
         char **argv) // IN:
   : mWindow(GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL))),
     mToplevelBox(GTK_VBOX(gtk_vbox_new(false, 0))),
     mContentBox(NULL),
     mFullscreenAlign(NULL),
     mBackgroundImage(NULL),
     mDlg(NULL)
{
#ifdef USE_GLIB_THREADS
   if (!g_thread_supported()) {
      g_thread_init(NULL);
   }
#endif
   VThread_Init(VTHREAD_UI_ID, VMWARE_VIEW);

   /*
    * XXX: Should use PRODUCT_VERSION_STRING for the third arg, but
    * that doesn't know about the vdi version.
    */
   ProductState_Set(PRODUCT_VDM_CLIENT, PRODUCT_VIEW_CLIENT_NAME,
                    VIEW_CLIENT_VERSION_NUMBER " " BUILD_NUMBER,
                    BUILD_NUMBER_NUMERIC, 0,
                    PRODUCT_VIEW_CLIENT_NAME_FOR_LICENSE,
                    PRODUCT_VERSION_STRING_FOR_LICENSE);

   Poll_InitGtk();
   Preference_Init();
   Sig_Init();

   Log_Init(NULL, VMWARE_VIEW ".log.filename", VMWARE_VIEW);
   IntegrateGLibLogging();
   printf("Using log file %s\n", Log_GetFileName());

   Log("Command line: ");
   for (int i = 0; i < argc; i++) {
      if (i > 1 && (Str_Strcmp(argv[i - 1], "-p") == 0 ||
                     Str_Strcmp(argv[i - 1], "--password") == 0)) {
         Log("[password omitted] ");
      } else if (strstr(argv[i], "--password=") == argv[i]) {
         Log("--password=[password omitted] ");
      } else {
         Log("%s ", argv[i]);
      }
   }
   Log("\n");

   InitLocalization();

   /* Try the system library, but don't do a version check */
   SSL_InitEx(NULL, NULL, NULL, TRUE, FALSE, FALSE);

   BasicHttp_Init(Poll_Callback, Poll_CallbackRemove);

   sApp = this;

   GOptionContext *context =
      g_option_context_new("- connect to VMware View desktops");
   g_option_context_add_main_entries(context, sOptFileEntries, NULL);

#if GTK_CHECK_VERSION(2, 6, 0)
   g_option_context_add_group(context, gtk_get_option_group(true));
#endif

   /*
    * Only the --file argument will be known to the context when it first
    * parses argv, so we should ignore other arguments (and leave them be)
    * until after the file argument has been fully dealt with.
    */
   g_option_context_set_ignore_unknown_options(context, true);

   g_option_context_set_help_enabled(context, false);

   // First, we only want to parse out the --file option.
   GError *fileError = NULL;
   if (!g_option_context_parse(context, &argc, &argv, &fileError)) {
      Util::UserWarning("Error parsing command line: %s\n", fileError->message);
   }
   /*
    * Hold on to the error--we might get the same message the next time we
    * parse, and we only want to show it once.
    */

   g_option_context_add_main_entries(context, sOptEntries, NULL);

   // If --file was specified and it exists, it will be opened and parsed.
   if (sOptFile) {
      ParseFileArgs();
   }

   /*
    * Now, parse the rest of the options out of argv.  By doing this parsing
    * here, it will allows the commandline options to override the config
    * file options.
    */
   g_option_context_set_ignore_unknown_options(context, false);
   g_option_context_set_help_enabled(context, true);
   GError *error = NULL;
   // Show the error message only if it's not the same as the one shown above.
   if (!g_option_context_parse(context, &argc, &argv, &error) &&
       (!fileError || Str_Strcmp(fileError->message, error->message) != 0)) {
      Util::UserWarning("Error parsing command line: %s\n", error->message);
   }
   g_clear_error(&fileError);
   g_clear_error(&error);

   if (sOptVersion) {
      /*
       * XXX; This should PRODUCT_VERSION_STRING once vdi has its own
       * vm_version.h.
       */
      printf(PRODUCT_VIEW_CLIENT_NAME " " VIEW_CLIENT_VERSION_NUMBER " "
             BUILD_NUMBER "\n\n"
"VMware and the VMware \"boxes\" logo and design are registered\n"
"trademarks or trademarks (the \"Marks\") of VMware, Inc. in the United\n"
"States and/or other jurisdictions and are not licensed to you under\n"
"the terms of the LGPL version 2.1. If you distribute VMware View Open\n"
"Client unmodified in either binary or source form or the accompanying\n"
"documentation unmodified, you may not remove, change, alter or\n"
"otherwise modify the Marks in any manner.  If you make minor\n"
"modifications to VMware View Open Client or the accompanying\n"
"documentation, you may, but are not required to, continue to\n"
"distribute the unaltered Marks with your binary or source\n"
"distributions.  If you make major functional changes to VMware View\n"
"Open Client or the accompanying documentation, you may not distribute\n"
"the Marks with your binary or source distribution and you must remove\n"
"all references to the Marks contained in your distribution.  All other\n"
"use or distribution of the Marks requires the prior written consent of\n"
"VMware.  All rights reserved.\n"
"\n"
"Copyright (c) 1998-2009 VMware, Inc. All rights reserved. Protected\n"
"by one or more U.S. Patent Nos. 6,397,242, 6,496,847, 6,704,925, 6,711,672,\n"
"6,725,289, 6,735,601, 6,785,886, 6,789,156, 6,795,966, 6,880,022, 6,944,699,\n"
"6,961,806, 6,961,941, 7,069,413, 7,082,598, 7,089,377, 7,111,086, 7,111,145,\n"
"7,117,481, 7,149,843, 7,155,558, 7,222,221, 7,260,815, 7,260,820, 7,269,683,\n"
"7,275,136, 7,277,998, 7,277,999, 7,278,030, 7,281,102, 7,290,253, 7,356,679,\n"
"7,409,487, 7,412,492, 7,412,702, 7,424,710, and 7,428,636; patents pending.\n");
      exit(0);
   }

   if (sOptPassword && Str_Strcmp(sOptPassword, "-") == 0) {
      sOptPassword = getpass(CDK_MSG(password, "Password: ").c_str());
   }

   if (sOptNonInteractive) {
      Log("Using non-interactive mode.\n");
   }

   gtk_widget_show(GTK_WIDGET(mToplevelBox));
   gtk_container_add(GTK_CONTAINER(mWindow), GTK_WIDGET(mToplevelBox));
   g_signal_connect(GTK_WIDGET(mToplevelBox), "size-allocate",
                    G_CALLBACK(&App::OnSizeAllocate), this);

   GList *li = NULL;
#define ADD_ICON(icon)                                                  \
   {                                                                    \
      GdkPixbuf *pb = gdk_pixbuf_new_from_inline(-1, icon, false, NULL); \
      if (pb) {                                                         \
         li = g_list_prepend(li, pb);                                   \
      }                                                                 \
   }

   ADD_ICON(view_16x);
   ADD_ICON(view_32x);
   ADD_ICON(view_48x);
#undef ADD_ICON

   gtk_window_set_default_icon_list(li);
   g_list_foreach(li, (GFunc)g_object_unref, NULL);
   g_list_free(li);
   li = NULL;

   // Quit when window closes.
   g_signal_connect(G_OBJECT(mWindow), "destroy",
                    G_CALLBACK(&gtk_main_quit), NULL);
   g_object_add_weak_pointer(G_OBJECT(mWindow), (gpointer *)&mWindow);

   RequestBroker();

   // Set the window's _NET_WM_USER_TIME from an X server roundtrip.
   Util::OverrideWindowUserTime(mWindow);
   gtk_window_present(mWindow);
}


/*
 *-------------------------------------------------------------------
 *
 * cdk::App::~App --
 *
 *      Destructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Global libraries and resources are shutdown and released
 *
 *-------------------------------------------------------------------
 */

App::~App()
{
   mRDesktopExitCnx.disconnect();
   delete mDlg;
   if (mWindow) {
      gtk_widget_destroy(GTK_WIDGET(mWindow));
   }
   Log_Exit();
   Sig_Exit();
}


/*
 *-------------------------------------------------------------------
 *
 * cdk::App::ParseFileArgs --
 *
 *      Parses additional options from a file.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-------------------------------------------------------------------
 */

void
App::ParseFileArgs()
{
   GOptionContext *context =
      g_option_context_new("- connect to VMware View desktops");
   g_option_context_add_main_entries(context, sOptEntries, NULL);

   gchar *contents = NULL;
   gsize length = 0;

   GError *error = NULL;
   gint argcp = 0;
   gchar **argvp = NULL;

   if (!g_file_get_contents(sOptFile, &contents, &length, &error) ||
       !g_shell_parse_argv(Util::Format(VMWARE_VIEW " %s", contents).c_str(),
            &argcp, &argvp, &error) ||
       !g_option_context_parse(context, &argcp, &argvp, &error)) {
      Util::UserWarning("Error parsing %s: %s\n", sOptFile, error->message ?
         error->message : "Unknown error");
   }

   g_strfreev(argvp);
   g_free(contents);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::IntegrateGLibLogging --
 *
 *      Replace the default GLib printerr and log handlers with our own
 *      functions so that these will be logged and/or suppressed like our
 *      internal messages.
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
App::IntegrateGLibLogging(void)
{
   g_set_printerr_handler((GPrintFunc)Warning);
   g_log_set_default_handler((GLogFunc)App::OnGLibLog, NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::OnGLibLog --
 *
 *      Our replacement for GLib's default log handler.
 *
 *      Ripped from bora/apps/lib/lui/utils.cc.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Application will be aborted if a fatal error is passed.
 *
 *-----------------------------------------------------------------------------
 */

void
App::OnGLibLog(const gchar *domain,  // IN
               GLogLevelFlags level, // IN
               const gchar *message) // IN
{
   // Both Panic and Warning implicitly log.
   if (level & (G_LOG_FLAG_FATAL | G_LOG_LEVEL_ERROR)) {
      Panic("%s: %s\n", domain, message);
   } else {
      Warning("%s: %s\n", domain, message);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::InitLocalization --
 *
 *      Calls Msg_SetLocale for the user's current locale, as determined by
 *      Locale_GetUserLanguage.
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
App::InitLocalization()
{
   char *userLanguage = Locale_GetUserLanguage();
   if (userLanguage) {
      const gchar *procName = g_get_prgname();

      Log("%s: Setting message locale to \"%s\" for process %s.\n",
          __func__, userLanguage, procName);

      Msg_SetLocale(userLanguage, procName);
      free(userLanguage);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::InitWindow --
 *
 *      Set up the main UI to either be a fullscreen window that the dialogs
 *      are placed over, or a regular window that dialogs go into.
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
App::InitWindow()
{
   mContentBox = GTK_VBOX(gtk_vbox_new(false, VM_SPACING));
   gtk_widget_show(GTK_WIDGET(mContentBox));
   g_object_add_weak_pointer(G_OBJECT(mContentBox) , (gpointer *)&mContentBox);

   // If a background image was specified, go into fullscreen mode.
   if (sOptFullscreen || sOptBackground) {
      /*
       * http://www.vmware.com/files/pdf/VMware_Logo_Usage_and_Trademark_Guidelines_Q307.pdf
       *
       * VMware Blue is Pantone 645 C or 645 U (R 116, G 152, B 191 = #7498bf).
       */
      GdkColor blue;
      gdk_color_parse("#7498bf", &blue);
      gtk_widget_modify_bg(GTK_WIDGET(mWindow), GTK_STATE_NORMAL, &blue);

      g_signal_connect(GTK_WIDGET(mWindow), "realize",
         G_CALLBACK(&App::FullscreenWindow), NULL);

      GtkFixed *fixed = GTK_FIXED(gtk_fixed_new());
      gtk_widget_show(GTK_WIDGET(fixed));
      gtk_box_pack_start(GTK_BOX(mToplevelBox), GTK_WIDGET(fixed), true, true,
                         0);

      if (sOptBackground) {
         mBackgroundImage = GTK_IMAGE(gtk_image_new());
         gtk_widget_show(GTK_WIDGET(mBackgroundImage));
         gtk_fixed_put(fixed, GTK_WIDGET(mBackgroundImage), 0, 0);
         g_object_add_weak_pointer(G_OBJECT(mBackgroundImage),
                                   (gpointer *)&mBackgroundImage);
      }

      mFullscreenAlign = GTK_ALIGNMENT(gtk_alignment_new(0.5,0.5,0,0));
      gtk_widget_show(GTK_WIDGET(mFullscreenAlign));
      gtk_fixed_put(fixed, GTK_WIDGET(mFullscreenAlign), 0, 0);
      g_object_add_weak_pointer(G_OBJECT(mFullscreenAlign),
                                (gpointer *)&mFullscreenAlign);
      OnSizeAllocate(NULL, &GTK_WIDGET(mWindow)->allocation, this);

      /*
       * Use a GtkEventBox to get the default background color.
       */
      GtkEventBox *eventBox = GTK_EVENT_BOX(gtk_event_box_new());
      gtk_widget_show(GTK_WIDGET(eventBox));
      gtk_container_add(GTK_CONTAINER(mFullscreenAlign), GTK_WIDGET(eventBox));

      GtkFrame *frame = GTK_FRAME(gtk_frame_new(NULL));
      gtk_widget_show(GTK_WIDGET(frame));
      gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
      gtk_container_add(GTK_CONTAINER(eventBox), GTK_WIDGET(frame));

      gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(mContentBox));
   } else {
      gtk_window_unfullscreen(mWindow);
      gtk_window_set_position(mWindow, GTK_WIN_POS_CENTER);
      gtk_window_set_gravity(mWindow, GDK_GRAVITY_CENTER);
      gtk_box_pack_start(GTK_BOX(mToplevelBox), GTK_WIDGET(mContentBox),
                         true, true, 0);
   }

   GdkPixbuf *pb = gdk_pixbuf_new_from_inline(-1, view_client_banner, false,
                                              NULL);
   ASSERT(pb);

   GtkWidget *img = gtk_image_new_from_pixbuf(pb);
   gtk_widget_show(img);
   gtk_box_pack_start(GTK_BOX(mContentBox), img, false, false, 0);
   gtk_misc_set_alignment(GTK_MISC(img), 0.0, 0.5);
   // Sets the minimum width, to avoid clipping banner logo text
   gtk_widget_set_size_request(GTK_WIDGET(img), 480, -1);
   g_signal_connect(img, "size-allocate",
                    G_CALLBACK(&App::OnBannerSizeAllocate), NULL);

   gdk_pixbuf_unref(pb);
   pb = NULL;

   gtk_window_set_title(mWindow,
                        CDK_MSG(windowTitle, PRODUCT_VIEW_CLIENT_NAME).c_str());
   g_signal_connect(mWindow, "key-press-event",
                    G_CALLBACK(&App::OnKeyPress), this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::SetContent --
 *
 *      Removes previous mDlg, if necessary, and puts the dialog's
 *      content in its place.
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
App::SetContent(Dlg *dlg) // IN
{
   ASSERT(dlg != mDlg);
   if (mDlg) {
      if (dynamic_cast<RDesktop *>(mDlg)) {
         mRDesktopExitCnx.disconnect();
      }
      delete mDlg;
   }
   mDlg = dlg;
   GtkWidget *content = mDlg->GetContent();
   gtk_widget_show(content);

   if (dynamic_cast<RDesktop *>(mDlg)) {
      if (mContentBox) {
         GList *children =
            gtk_container_get_children(GTK_CONTAINER(mToplevelBox));
         for (GList *li = children; li; li = li->next) {
            if (GTK_WIDGET(li->data) != content) {
               gtk_widget_destroy(GTK_WIDGET(li->data));
            }
         }
         g_list_free(children);
         ASSERT(!mContentBox);
      }
      // The widget was added before rdesktop was launched.
      ASSERT(gtk_widget_get_parent(content) == GTK_WIDGET(mToplevelBox));
   } else {
      if (!mContentBox) {
         InitWindow();
      }
      gtk_box_pack_start(GTK_BOX(mContentBox), content, true, true, 0);
   }
   /*
    * From bora/apps/lib/lui/window.cc:
    *
    * Some window managers (Metacity in particular) refuse to go
    * fullscreen if the window is not resizable (i.e. if the window has
    * the max size hint set), which is reasonable. So we need to make the
    * window resizable first.  This happens in a few different places
    * throughout these transitions.
    *
    * In GTK+ 2.2 and 2.4, gtk_window_set_resizable() sets the internal
    * state to resizable, and then queues a resize. That ends up calling
    * the check_resize method of the window, which updates the window
    * manager hints according to the internal state. The bug is that this
    * update happens asynchronously.
    *
    * We want the update to happen now, so we workaround the issue by
    * synchronously calling the check_resize method of the window
    * ourselves.
    */
   if (!sOptFullscreen && !sOptBackground) {
      gtk_window_set_resizable(GTK_WINDOW(mWindow), mDlg->IsResizable());
      gtk_container_check_resize(GTK_CONTAINER(mWindow));
   }
   if (dynamic_cast<RDesktop *>(mDlg)) {
      g_signal_handlers_disconnect_by_func(
         mWindow, (gpointer)&App::OnKeyPress, this);
      // XXX: This call may fail.  Should monitor the
      //      window_state_event signal, and either restart rdesktop
      //      if we exit fullscreen, or don't start it until we enter
      //      fullscreen.
      FullscreenWindow(mWindow);
   }
   mDlg->cancel.connect(boost::bind(&App::OnCancel, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::SetBusy --
 *
 *      Called when we are awaiting a response from the broker.
 *
 *      TBD: display the message.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Disables UI.
 *
 *-----------------------------------------------------------------------------
 */

void
App::SetBusy(const Util::string &message) // IN
{
   Log("Busy: %s\n", message.c_str());
   mDlg->SetSensitive(false);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::SetReady --
 *
 *      Called when we are awaiting input from the user.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Enables UI.
 *
 *-----------------------------------------------------------------------------
 */

void
App::SetReady()
{
   mDlg->SetSensitive(true);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestBroker --
 *
 *      Set up the broker connection dialog.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Resets the broker state.
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestBroker()
{
   Reset();
   BrokerDlg *brokerDlg = new BrokerDlg(sOptBroker ? sOptBroker : "");
   SetContent(brokerDlg);
   brokerDlg->connect.connect(boost::bind(&App::DoInitialize, this));

   // Hit the Connect button if broker was supplied and we're non-interactive.
   if (sOptBroker && sOptNonInteractive) {
      DoInitialize();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestDisclaimer --
 *
 *      Sets up the given DisclaimerDlg to accept/cancel the disclaimer.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Disclaimer page is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestDisclaimer(const Util::string &disclaimer) // IN
{
   DisclaimerDlg *dlg = new DisclaimerDlg();
   SetContent(dlg);
   dlg->SetText(disclaimer);
   dlg->accepted.connect(boost::bind(&App::AcceptDisclaimer, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestPasscode --
 *
 *      Prompt the user for their username and passcode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Passcode page is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestPasscode(const Util::string &username) // IN
{
   SecurIDDlg *dlg = new SecurIDDlg();
   SetContent(dlg);
   dlg->SetState(SecurIDDlg::STATE_PASSCODE, username);
   dlg->authenticate.connect(boost::bind(&App::DoSubmitPasscode, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestNextTokencode --
 *
 *      Prompt the user for their next tokencode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Tokencode page is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestNextTokencode(const Util::string &username) // IN
{
   SecurIDDlg *dlg = new SecurIDDlg();
   SetContent(dlg);
   dlg->SetState(SecurIDDlg::STATE_NEXT_TOKEN, username);
   dlg->authenticate.connect(boost::bind(&App::DoSubmitNextTokencode, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestPinChange --
 *
 *      Prompt the user for a new PIN.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Pin change dialog is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestPinChange(const Util::string &pin,     // IN
                      const Util::string &message, // IN
                      bool userSelectable)         // IN
{
   SecurIDDlg *dlg = new SecurIDDlg();
   SetContent(dlg);
   dlg->SetState(SecurIDDlg::STATE_SET_PIN, pin, message, userSelectable);
   dlg->authenticate.connect(boost::bind(&App::DoSubmitPins, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestPassword --
 *
 *      Prompt the user for their password.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Password dialog is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestPassword(const Util::string &username,             // IN
                     bool readOnly,                            // IN
                     const std::vector<Util::string> &domains, // IN
                     const Util::string &suggestedDomain)      // IN
{
   LoginDlg *dlg = new LoginDlg();
   SetContent(dlg);

   /*
    * Turn off non-interactive mode if the suggested username differs from
    * the one passed on the command line. We want to use the username
    * returned by the server, but should let the user change it before
    * attempting to authenticate.
    */
   if (sOptUser && Str_Strcasecmp(username.c_str(), sOptUser) != 0) {
      sOptNonInteractive = false;
   }

   /*
    * Try to find the suggested domain in the list returned by the server.
    * If it's found, use it. If it's not and it was passed via the command
    * line, show a warning. Use the pref if it's in the list. If all else
    * fails, use the first domain in the list. Only go non-interactive if
    * the domain  was given on the command line and it was found, or if
    * there's only one domain in the list.
    */
   Util::string domain = "";
   bool domainFound = false;
   Util::string domainPref = Prefs::GetPrefs()->GetDefaultDomain();
   for (std::vector<Util::string>::const_iterator i = domains.begin();
        i != domains.end(); i++) {
      if (Str_Strcasecmp(i->c_str(), suggestedDomain.c_str()) == 0) {
         // Use value in the list so the case matches.
         domain = *i;
         domainFound = true;
         break;
      } else if (Str_Strcasecmp(i->c_str(), domainPref.c_str()) == 0) {
         domain = *i;
      }
   }

   if (!domainFound && sOptDomain &&
       Str_Strcasecmp(suggestedDomain.c_str(), sOptDomain) == 0) {
      Util::UserWarning("Command-line option domain \"%s\" is not in the list "
                        "returned by the server.\n", sOptDomain);
   }
   if (domain.empty() && domains.size() > 0) {
      domain = domains[0];
   }

   dlg->SetFields(username, readOnly, sOptPassword ? sOptPassword : "",
                  domains, domain);
   dlg->login.connect(boost::bind(&App::DoSubmitPassword, this));
   if (sOptNonInteractive && !username.empty() &&
       ((sOptDomain && domainFound) || domains.size() == 1) && sOptPassword) {
      DoSubmitPassword();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestPasswordChange --
 *
 *      Prompt the user for a new password.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Password change dialog is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestPasswordChange(const Util::string &username, // IN
                           const Util::string &domain)   // IN
{
   PasswordDlg *dlg = new PasswordDlg();
   SetContent(dlg);

   // Domain is locked, so just create a vector with it as the only value.
   std::vector<Util::string> domains;
   domains.push_back(domain);

   dlg->SetFields(username, true, "", domains, domain);
   dlg->login.connect(boost::bind(&App::DoChangePassword, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestDesktop --
 *
 *      Prompt the user for a desktop with which to connect.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop selection dialog is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestDesktop(std::vector<Desktop *> &desktops) // IN
{
   Util::string initialDesktop = "";
   /*
    * Iterate through desktops. If the passed-in desktop name is found,
    * pass it as initially-selected. Otherwise use a desktop with the
    * "alwaysConnect" user preference.
    */
   for (std::vector<Desktop*>::iterator i = desktops.begin();
        i != desktops.end(); i++) {
      Util::string name = (*i)->GetName();
      if (sOptDesktop && name == sOptDesktop) {
         initialDesktop = sOptDesktop;
         break;
      } else if ((*i)->GetAutoConnect()) {
         initialDesktop = name;
      }
   }
   if (sOptDesktop && initialDesktop != sOptDesktop) {
      Util::UserWarning("Command-line option desktop \"%s\" is not in the list "
                        "returned by the server.\n",
                        sOptDesktop);
   }

   DesktopSelectDlg *dlg = new DesktopSelectDlg(desktops, initialDesktop);
   SetContent(dlg);
   dlg->connect.connect(boost::bind(&App::DoConnectDesktop, this));

   // Hit Connect button when non-interactive
   if (sOptNonInteractive &&
       (!initialDesktop.empty() || desktops.size() == 1)) {
      dlg->connect();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestTransition --
 *
 *      Show the transition dialog; a message with a spinner.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Transition dialog is displayed.
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestTransition(const Util::string &message)
{
   Log("Transitioning: %s\n", message.c_str());
   TransitionDlg *dlg = new TransitionDlg(TransitionDlg::TRANSITION_PROGRESS,
                                          message);

   std::vector<GdkPixbuf *> pixbufs = TransitionDlg::LoadAnimation(
      -1, spinner_anim, false, SPINNER_ANIM_N_FRAMES);
   dlg->SetAnimation(pixbufs, SPINNER_ANIM_FPS_RATE);
   for (std::vector<GdkPixbuf *>::iterator i = pixbufs.begin();
        i != pixbufs.end(); i++) {
      g_object_unref(*i);
   }

   SetContent(dlg);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::Quit --
 *
 *      Handle succesful logout command.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Destroys main window, which exits the app.
 *
 *-----------------------------------------------------------------------------
 */

void
App::Quit()
{
   gtk_widget_destroy(GTK_WIDGET(mWindow));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoInitialize --
 *
 *      Handle a Connect button click in the broker entry control.  Invoke the
 *      async broker Initialize.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      See Broker::Initialize().
 *
 *-----------------------------------------------------------------------------
 */

void
App::DoInitialize()
{
   ASSERT(mDlg);

   BrokerDlg *brokerDlg = dynamic_cast<BrokerDlg *>(mDlg);
   ASSERT(brokerDlg);
   if (brokerDlg->GetBroker().empty()) {
      return;
   }
   Prefs *prefs = Prefs::GetPrefs();
   Initialize(brokerDlg->GetBroker(), brokerDlg->GetPort(),
              brokerDlg->GetSecure(),
              sOptUser ? sOptUser : prefs->GetDefaultUser(),
              // We'll use the domain pref later if need be.
              sOptDomain ? sOptDomain : "");
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoSubmitPasscode --
 *
 *      Attempt to authenticate using a username and passcode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      See Broker::SubmitPasscode().
 *
 *-----------------------------------------------------------------------------
 */

void
App::DoSubmitPasscode()
{
   SecurIDDlg *dlg = dynamic_cast<SecurIDDlg *>(mDlg);
   ASSERT(dlg);

   Util::string user = dlg->GetUsername();
   Prefs::GetPrefs()->SetDefaultUser(user);

   SubmitPasscode(user, dlg->GetPasscode());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoSubmitNextTokencode --
 *
 *      Continues authentication using a tokencode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      See Broker::SubmitNextTokencode().
 *
 *-----------------------------------------------------------------------------
 */

void
App::DoSubmitNextTokencode()
{
   SecurIDDlg *dlg = dynamic_cast<SecurIDDlg *>(mDlg);
   ASSERT(dlg);
   SubmitNextTokencode(dlg->GetPasscode());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoSubmitPins --
 *
 *      Continue authentication by submitting new PINs.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      See Broker::SubmitPins().
 *
 *-----------------------------------------------------------------------------
 */

void
App::DoSubmitPins()
{
   SecurIDDlg *dlg = dynamic_cast<SecurIDDlg *>(mDlg);
   ASSERT(dlg);
   std::pair<Util::string, Util::string> pins = dlg->GetPins();
   if (pins.first != pins.second) {
      App::ShowDialog(GTK_MESSAGE_ERROR,
                      CDK_MSG(securIDPinMismatch, "The PINs do not match."));
   } else {
      SubmitPins(pins.first, pins.second);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoSubmitPassword --
 *
 *      Authenticate using a username and password.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      See Broker::SubmitPassword()
 *
 *-----------------------------------------------------------------------------
 */

void
App::DoSubmitPassword()
{
   LoginDlg *dlg = dynamic_cast<LoginDlg *>(mDlg);
   ASSERT(dlg);

   Util::string user = dlg->GetUsername();
   Util::string domain = dlg->GetDomain();

   Prefs *prefs = Prefs::GetPrefs();
   prefs->SetDefaultUser(user);
   prefs->SetDefaultDomain(domain);

   SubmitPassword(user, dlg->GetPassword(), domain);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoChangePassword --
 *
 *      Continue authentication by choosing a new password.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      See Broker::ChangePassword()
 *
 *-----------------------------------------------------------------------------
 */

void
cdk::App::DoChangePassword()
{
   PasswordDlg *dlg = dynamic_cast<PasswordDlg *>(mDlg);
   ASSERT(dlg);

   std::pair<Util::string, Util::string> pwords = dlg->GetNewPassword();
   if (pwords.first != pwords.second) {
      App::ShowDialog(GTK_MESSAGE_ERROR,
                      CDK_MSG(securIDPasswordMismatch,
                              "The Passwords do not match."));
   } else {
      ChangePassword(dlg->GetPassword(), pwords.first, pwords.second);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoConnectDesktop --
 *
 *      Begin connecting to a desktop.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      See Broker::ConnectDesktop()
 *
 *-----------------------------------------------------------------------------
 */

void
App::DoConnectDesktop()
{
   DesktopSelectDlg *dlg = dynamic_cast<DesktopSelectDlg *>(mDlg);
   ASSERT(dlg);
   ConnectDesktop(dlg->GetDesktop());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::TunnelDisconnected --
 *
 *      Tunnel onDisconnect signal handler.  Shows an error dialog to the user.
 *      Clicking 'Ok' in the dialog destroys mWindow, which quits the client.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      mWindow is insensitive.
 *      We will not exit if rdesktop exits.
 *
 *-----------------------------------------------------------------------------
 */

void
App::TunnelDisconnected(Util::string disconnectReason) // IN
{
   /*
    * rdesktop will probably exit shortly, and we want the user to see
    * our dialog before we exit
    */
   mRDesktopExitCnx.disconnect();

   Util::string message = CDK_MSG(tunnelDisconnected,
                                  "The secure connection to the View Server has"
                                  " unexpectedly disconnected.");
   if (disconnectReason != "") {
      message += "\n\n" + Util::Format(CDK_MSG(tunnelDisconnectedReason,
                                               "Reason: %s.").c_str(),
				       disconnectReason.c_str());
   }

   ShowDialog(GTK_MESSAGE_ERROR, "%s", message.c_str());
   /*
    * If the tunnel really exited, it's probably not going to let us
    * get a new one until we log in again.  If we're at the desktop
    * selection page, that means we should restart.
    */
   if (!dynamic_cast<TransitionDlg *>(mDlg)) {
      RequestBroker();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::OnSizeAllocate --
 *
 *      Resize the GtkAlignment to fill available space, and possibly
 *      the background image as well.
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
App::OnSizeAllocate(GtkWidget *widget,         // IN/UNUSED
                    GtkAllocation *allocation, // IN
                    gpointer userData)         // IN
{
   App *that = reinterpret_cast<App *>(userData);
   ASSERT(that);
   if (that->mFullscreenAlign) {
      /*
       * This really does need to be a _set_size_request(), and not
       * _size_allocate(), otherwise there is some resize flickering
       * at startup (and quitting, if that happens).
       */
      gtk_widget_set_size_request(GTK_WIDGET(that->mFullscreenAlign),
                                  allocation->width, allocation->height);
   }
   if (that->mBackgroundImage) {
      that->ResizeBackground(allocation);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::OnBannerSizeAllocate --
 *
 *      If the GtkImage is resized larger than its pixbuf, stretch it
 *      out by copying the last column of pixels.
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
App::OnBannerSizeAllocate(GtkWidget *image,          // IN
                          GtkAllocation *allocation, // IN
                          gpointer userData)         // IN/UNUSED
{
   GdkPixbuf *pb;
   g_object_get(image, "pixbuf", &pb, NULL);
   if (!pb) {
      Log("No pixbuf for image, can't resize it.");
      return;
   }
   int old_width = gdk_pixbuf_get_width(pb);
   if (allocation->width <= old_width) {
      g_object_unref(pb);
      return;
   }
   GdkPixbuf *newPb = gdk_pixbuf_new(gdk_pixbuf_get_colorspace(pb),
                                     gdk_pixbuf_get_has_alpha(pb),
                                     gdk_pixbuf_get_bits_per_sample(pb),
                                     allocation->width,
                                     gdk_pixbuf_get_height(pb));
   gdk_pixbuf_copy_area(pb, 0, 0, gdk_pixbuf_get_width(pb),
                        gdk_pixbuf_get_height(pb), newPb, 0, 0);
   int old_height = gdk_pixbuf_get_height(pb);
   for (int y = old_width; y < allocation->width; y++) {
      gdk_pixbuf_copy_area(pb, old_width - 1, 0, 1, old_height, newPb, y, 0);
   }
   g_object_set(image, "pixbuf", newPb, NULL);
   g_object_unref(pb);
   g_object_unref(newPb);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::ResizeBackground --
 *
 *      Load and scale the background to fill the screen, maintaining
 *      aspect ratio.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop has a nice background image.
 *
 *-----------------------------------------------------------------------------
 */

void
App::ResizeBackground(GtkAllocation *allocation) // IN
{
   ASSERT(mBackgroundImage);

   if (allocation->width <= 1 || allocation->height <= 1) {
      return;
   }

   GdkPixbuf *pixbuf;
   g_object_get(G_OBJECT(mBackgroundImage), "pixbuf", &pixbuf, NULL);
   if (pixbuf &&
       gdk_pixbuf_get_width(pixbuf) == allocation->width &&
       gdk_pixbuf_get_height(pixbuf) == allocation->height) {
      gdk_pixbuf_unref(pixbuf);
      return;
   }
   if (pixbuf) {
      gdk_pixbuf_unref(pixbuf);
   }
   GError *error = NULL;
   pixbuf = gdk_pixbuf_new_from_file_at_size(sOptBackground, -1,
                                             allocation->height, &error);
   if (error) {
      Util::UserWarning(CDK_MSG(backgroundError,
                                "Unable to load background image "
                                "'%s': %s\n").c_str(),
                        sOptBackground,
                        error->message ? error->message :
                           CDK_MSG(unknownBackgroundError,
                                   "Unknown error").c_str());
      g_error_free(error);
      return;
   }
   if (gdk_pixbuf_get_width(pixbuf) < allocation->width) {
      GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
         pixbuf,
         allocation->width,
         allocation->height * allocation->width / gdk_pixbuf_get_width(pixbuf),
         GDK_INTERP_BILINEAR);
      gdk_pixbuf_unref(pixbuf);
      pixbuf = scaled;
   }
   GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf(
      pixbuf,
      (gdk_pixbuf_get_width(pixbuf) - allocation->width) / 2,
      (gdk_pixbuf_get_height(pixbuf) - allocation->height) / 2,
      allocation->width,
      allocation->height);
   gdk_pixbuf_unref(pixbuf);
   g_object_set(G_OBJECT(mBackgroundImage), "pixbuf", sub, NULL);
   gdk_pixbuf_unref(sub);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestLaunchDesktop --
 *
 *      Starts an rdesktop session and embeds it into the main window, and
 *      causes the main window to enter fullscreen.
 *
 * Results:
 *      false
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestLaunchDesktop(Desktop *desktop) // IN
{
   ASSERT(desktop);

   SetReady();
   Log("Desktop connect successful.  Starting rdesktop...\n");
   if (sOptNonInteractive) {
      Log("Disabling non-interactive mode.\n");
      sOptNonInteractive = false;
   }

   RequestTransition(CDK_MSG(appConnecting,
                             "Connecting to the desktop..."));

   RDesktop *dlg = desktop->GetRDesktop();
   mDlg->cancel.connect(boost::bind(&App::OnRDesktopCancel, this, dlg));
   dlg->onConnect.connect(boost::bind(&App::SetContent, this, dlg));
   gtk_box_pack_start(GTK_BOX(mToplevelBox), dlg->GetContent(), false, false,
                      0);
   gtk_widget_realize(dlg->GetContent());

   mRDesktopExitCnx = dlg->onExit.connect(
      boost::bind(&App::OnRDesktopExit, this, dlg, _1));

   UpdateDisplayEnvironment();

   // Collect all the -r options
   std::vector<Util::string> devRedirects;
   for (gchar **redir = sOptRedirect; redir && *redir; redir++) {
      devRedirects.push_back(*redir);
   }

   desktop->StartRDesktop(devRedirects);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::FullscreenWindow --
 *
 *      Checks if the window manager supports fullscreen, then either calls
 *      gtk_window_fullscreen() or manually sets the size and position of
 *      mWindow.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The window is fullscreened.
 *
 *-----------------------------------------------------------------------------
 */

void
App::FullscreenWindow(GtkWindow *win) // IN
{
   if (gdk_net_wm_supports(gdk_atom_intern("_NET_WM_STATE_FULLSCREEN",
                                           FALSE))) {
      Log("Attempting to fullscreen window using _NET_WM_STATE_FULLSCREEN"
          " hint.\n");
      // The window manager supports fullscreening the window on its own.
      gtk_window_fullscreen(win);
   } else {
      /*
       * The window manager does not support fullscreening the window, so
       * we must set the size and position manually.
       */
      GdkScreen *screen = gtk_window_get_screen(win);
      ASSERT(screen);
      GdkRectangle geometry;
      gdk_screen_get_monitor_geometry(
         screen,
         gdk_screen_get_monitor_at_window(screen, GTK_WIDGET(win)->window),
         &geometry);

      Log("Attempting to manually fullscreen window: %d, %d %d x %d\n",
          geometry.x, geometry.y, geometry.width, geometry.height);

      gtk_window_move(win, geometry.x, geometry.y);
      gtk_window_resize(win, geometry.width, geometry.height);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::ShowDialog --
 *
 *      Pops up a dialog or shows a transition error message.  The
 *      format argument is a printf format string.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      A new dialog, or the error transition page, is displayed.
 *
 *-----------------------------------------------------------------------------
 */

void
App::ShowDialog(GtkMessageType type,       // IN
                const Util::string format, // IN
                ...)
{
   ASSERT(sApp);
   /*
    * It would be nice if there was a va_list variant of
    * gtk_message_dialog_new().
    */
   va_list args;
   va_start(args, format);
   Util::string label = Util::FormatV(format.c_str(), args);
   va_end(args);

   if (sOptNonInteractive) {
      Log("ShowDialog: %s; Turning off non-interactive mode.\n", label.c_str());
      sOptNonInteractive = false;
   }

   /*
    * If we're trying to connect, or have already connected, show the
    * error using the transition page.
    */
   if (dynamic_cast<TransitionDlg *>(sApp->mDlg) ||
       dynamic_cast<RDesktop *>(sApp->mDlg)) {
      TransitionDlg *dlg = new TransitionDlg(TransitionDlg::TRANSITION_ERROR,
                                             label);
      dlg->SetStock(GTK_STOCK_DIALOG_ERROR);
      sApp->SetContent(dlg);
      dlg->retry.connect(boost::bind(&App::ReconnectDesktop, sApp));
   } else {
      GtkWidget *dialog = gtk_message_dialog_new(
         sApp->mWindow, GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_OK,
         "%s", label.c_str());
      gtk_widget_show(dialog);
      gtk_window_set_title(GTK_WINDOW(dialog),
                           gtk_window_get_title(sApp->mWindow));
      g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy),
                       NULL);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::OnCancel --
 *
 *      Handler for the various dialogs' cancel button.
 *      Turns off non-interactive mode, allowing users to interact with
 *      dialogs that would otherwise be skipped.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The cancel button does one of three things:
 *
 *      1.  On the broker page, with no RPC in-flight, Quits.
 *
 *      2.  If RPCs are in-flight, cancel them (which re-sensitizes
 *      the page).
 *
 *      3.  Otherwise, goes back to the broker page.
 *
 *-----------------------------------------------------------------------------
 */

void
App::OnCancel()
{
   if (sOptNonInteractive) {
      Log("User cancelled; turning off non-interactive mode.\n");
      sOptNonInteractive = false;
   }
   Log("User cancelled.\n");
   if (mDlg->IsSensitive()) {
      TransitionDlg *dlg = dynamic_cast<TransitionDlg *>(mDlg);
      if (dynamic_cast<BrokerDlg *>(mDlg)) {
         Quit();
      } else if (dlg) {
         if (dlg->GetTransitionType() == TransitionDlg::TRANSITION_PROGRESS) {
            CancelRequests();
         }
         LoadDesktops();
      } else {
         RequestBroker();
      }
   } else {
      CancelRequests();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::UpdateDisplayEnvironment --
 *
 *      Update the DISPLAY environment variable according to the
 *      GdkScreen mWindow is on.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      DISPLAY is updated
 *
 *-----------------------------------------------------------------------------
 */

void
App::UpdateDisplayEnvironment()
{
   char *dpy = gdk_screen_make_display_name(gtk_window_get_screen(mWindow));
   setenv("DISPLAY", dpy, true);
   g_free(dpy);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::OnKeyPress --
 *
 *      Handle keypress events.
 *
 * Results:
 *      true to stop other handlers from being invoked for the event.
 *      false to propogate the event further.
 *
 * Side effects:
 *      May press cancel button.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
App::OnKeyPress(GtkWidget *widget, // IN/UNUSED
                GdkEventKey *evt,  // IN
                gpointer userData) // IN
{
   App *that = reinterpret_cast<App*>(userData);
   ASSERT(that);

   if (GDK_Escape == evt->keyval && !evt->state) {
      ASSERT(that->mDlg);
      that->mDlg->Cancel();
      return true;
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::OnRDesktopExit --
 *
 *      Handle rdesktop exiting.  If it has exited too many times
 *      recently, give up and exit.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      May restart RDesktop, display an error, or exit.
 *
 *-----------------------------------------------------------------------------
 */

void
App::OnRDesktopExit(RDesktop *rdesktop, // IN
                    int status)         // IN
{
   if (status && rdesktop->GetHasConnected() && !mRDesktopMonitor.ShouldThrottle()) {
      ReconnectDesktop();
   } else if (!status) {
      Quit();
   } else {
      // The ShowDialog() below will delete rdesktop if it is mDlg.
      if (rdesktop != dynamic_cast<RDesktop *>(mDlg)) {
         delete rdesktop;
      }
      mRDesktopMonitor.Reset();
      ShowDialog(GTK_MESSAGE_ERROR,
                 CDK_MSG(rdesktopDisconnected,
                         "The desktop has unexpectedly disconnected."));
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::OnRDesktopCancel --
 *
 *      Extra handler for the "Connecting to desktop..." transition's
 *      cancel handler, to free the rdesktop associated with it.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Kills associated rdesktop.
 *
 *-----------------------------------------------------------------------------
 */

void
App::OnRDesktopCancel(RDesktop *rdesktop) // IN
{
   mRDesktopExitCnx.disconnect();
   delete rdesktop;
}


} // namespace cdk
