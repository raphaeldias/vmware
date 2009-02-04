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
 * util.cc --
 *
 *    CDK utilities.
 */


#include <gtk/gtkalignment.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkhbbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkimage.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkstock.h>


#include "util.hh"
#include "app.hh"


// Here to avoid conflict with vm_basic_types.h
#include <gdk/gdkx.h>


namespace cdk {
namespace Util {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::DoNothing --
 *
 *      Implementation for the slot returned by EmptyDoneSlot.
 *      Does nothing.
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
DoNothing()
{
   // Do nothing.
}


static DoneSlot sEmptyDoneSlot(DoNothing);


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::EmptyDoneSlot --
 *
 *      Simple DoneSlot implementation that does nothing.
 *
 * Results:
 *      A DoneSlot.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

DoneSlot
EmptyDoneSlot()
{
   return sEmptyDoneSlot;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::DoLogAbort --
 *
 *      Implementation for the slot returned by MakeLogAbortSlot.  Logs the
 *      exception text if the handler was not called due to cancellation.
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
DoLogAbort(bool cancelled, // IN
           exception err)  // IN
{
   if (!cancelled) {
      Log("Unhandled abort: %s", err.what());
      DEBUG_ONLY(Util_Backtrace(0));
   }
}


static AbortSlot sLogAbortSlot(DoLogAbort);


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::LogAbortSlot --
 *
 *      Simple AbortSlot implementation that logs the abort exception's what
 *      string.
 *
 * Results:
 *      An AbortSlot.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

AbortSlot
LogAbortSlot()
{
   return sLogAbortSlot;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetComboBoxEntryText --
 *
 *      Hack around the missing gtk_combo_box_get_active_text in Gtk2.4, by
 *      getting the combo's child entry widget and returning its text.
 *
 * Results:
 *      Active text of the combobox.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
GetComboBoxEntryText(GtkComboBoxEntry *combo) // IN
{
   GtkWidget *w = gtk_bin_get_child(GTK_BIN(combo));
   ASSERT(GTK_IS_ENTRY(w));
   return gtk_entry_get_text(GTK_ENTRY(w));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetComboBoxText --
 *
 *      Hack around the missing gtk_combo_box_get_active_text in Gtk2.4, by
 *      getting the combo's currently selected iterator and returning its text.
 *
 * Results:
 *      Active text of the combobox.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
GetComboBoxText(GtkComboBox *combo) // IN
{
   GtkTreeIter iter;
   Util::string result = "";
   if (gtk_combo_box_get_active_iter(combo, &iter)) {
      gchar *text = NULL;
      gtk_tree_model_get(gtk_combo_box_get_model(combo), &iter, 0, &text, -1);
      result = text;
      g_free(text);
   }
   return result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::CreateButton --
 *
 *      Hack around the missing gtk_button_set_image in Gtk2.4. Creates a
 *      GtkButton with the given stock ID and optionally override the default
 *      label.  The button contents have VM_SPACING padding on either side and
 *      between them.
 *
 *      Copied and converted to Gtk-C from bora/apps/lib/lui/button.cc.
 *
 * Results:
 *      A new Gtk button.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GtkButton*
CreateButton(const string stockId, // IN
             string label)         // IN: optional
{
   GtkWidget *button = gtk_button_new();
   ASSERT_MEM_ALLOC(button);
   gtk_widget_show(button);

   GtkWidget *align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
   ASSERT_MEM_ALLOC(align);
   gtk_widget_show(align);
   gtk_container_add(GTK_CONTAINER(button), align);
   gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0, 0,
                             VM_SPACING, VM_SPACING);

   GtkWidget *contents = gtk_hbox_new(false, VM_SPACING);
   ASSERT_MEM_ALLOC(contents);
   gtk_widget_show(contents);
   gtk_container_add(GTK_CONTAINER(align), contents);

   GtkWidget *img = gtk_image_new_from_stock(stockId.c_str(),
                                             GTK_ICON_SIZE_BUTTON);
   ASSERT_MEM_ALLOC(img);
   gtk_widget_show(img);
   gtk_box_pack_start(GTK_BOX(contents), img, false, false, 0);

   if (label.empty()) {
      GtkStockItem item = { 0, };
      gboolean found = gtk_stock_lookup(stockId.c_str(), &item);
      ASSERT(found);
      if (found) {
         label = item.label;
      }
   }

   GtkWidget *l = gtk_label_new_with_mnemonic(label.c_str());
   ASSERT_MEM_ALLOC(l);
   gtk_widget_show(l);
   gtk_box_pack_start(GTK_BOX(contents), l, false, false, 0);

   AtkObject *atk = gtk_widget_get_accessible(GTK_WIDGET(button));
   atk_object_set_name(atk, gtk_label_get_text(GTK_LABEL(l)));

   return GTK_BUTTON(button);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::CreateActionArea --
 *
 *      Creates an action area containing the passed-in buttons.
 *
 * Results:
 *      A GtkHButtonBox.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GtkWidget *
CreateActionArea(GtkButton *button1, // IN
                 ...)
{
   GtkWidget *actionArea = gtk_hbutton_box_new();
   gtk_box_set_spacing(GTK_BOX(actionArea), VM_SPACING);
   gtk_button_box_set_layout(GTK_BUTTON_BOX(actionArea),
                             GTK_BUTTONBOX_END);
   va_list args;
   va_start(args, button1);
   for (GtkWidget *button = GTK_WIDGET(button1);
        button; button = va_arg(args, GtkWidget *)) {
      gtk_box_pack_start(GTK_BOX(actionArea), button, false, true, 0);
   }
   va_end(args);
   return actionArea;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::OverrideWindowUserTime --
 *
 *      Older versions of Metacity can avoid raising windows due to
 *      focus-stealing avoidance.  This causes an X server roundtrip to get a
 *      valid timestamp, allowing us to set the window's _NET_WM_USER_TIME to
 *      trick Metacity.
 *
 *      Code originally from tomboy/libtomboy/tomboyutil.c, licensed X11.
 *      Updated to conditionally call gdk_property_change instead of
 *      gdk_x11_window_set_user_time, which doesn't exist in Gtk 2.4.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Synchronous X server roundtrip.
 *
 *-----------------------------------------------------------------------------
 */

void
OverrideWindowUserTime(GtkWindow *window) // IN
{
   if (!GTK_WIDGET_REALIZED(window)) {
      gtk_widget_realize(GTK_WIDGET(window));
   }

   GdkWindow *gdkWin = GTK_WIDGET(window)->window;
   guint32 evTime = gtk_get_current_event_time();

   if (evTime == 0) {
      // Last resort for non-interactive openings.  Causes roundtrip to server.
      gint evMask = gtk_widget_get_events(GTK_WIDGET(window));
      if (!(evMask & GDK_PROPERTY_CHANGE_MASK)) {
         gtk_widget_add_events(GTK_WIDGET(window),
                               GDK_PROPERTY_CHANGE_MASK);
      }
      evTime = gdk_x11_get_server_time(gdkWin);
   }

   DEBUG_ONLY(Log("Setting _NET_WM_USER_TIME to: %d\n", evTime));

#if GTK_CHECK_VERSION(2, 6, 0)
   gdk_x11_window_set_user_time(gdkWin, evTime);
#else
   gdk_property_change(gdkWin, gdk_atom_intern("_NET_WM_USER_TIME", false),
                       gdk_atom_intern("CARDINAL", true), 32,
                       GDK_PROP_MODE_REPLACE, (guchar*)&evTime, 1);
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::UserWarning --
 *
 *      Prints a warning to the console and logs it. Currently this is the
 *      only way to display text on the console in released builds.
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
UserWarning(const char *format, // IN
            ...)
{
   va_list arguments;
   va_start(arguments, format);
   string line = FormatV(format, arguments);
   va_end(arguments);
   fprintf(stderr, line.c_str());
   Log(line.c_str());
}


} // namespace Util
} // namespace cdk
