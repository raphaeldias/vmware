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
 * disclaimerDlg.cc --
 *
 *    Shows disclaimer for user to accept.
 */


#include <gtk/gtkhbox.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkvbox.h>


#include "disclaimerDlg.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DisclaimerDlg::DisclaimerDlg --
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

DisclaimerDlg::DisclaimerDlg()
   : Dlg(),
     mView(GTK_TEXT_VIEW(gtk_text_view_new()))
{
   GtkVBox *box = GTK_VBOX(gtk_vbox_new(false, VM_SPACING));
   Init(GTK_WIDGET(box));
   gtk_container_set_border_width(GTK_CONTAINER(box), VM_SPACING);

   GtkScrolledWindow *swin =
      GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
   gtk_widget_show(GTK_WIDGET(swin));
   gtk_box_pack_start_defaults(GTK_BOX(box), GTK_WIDGET(swin));
   g_object_set(swin, "height-request", 200, NULL);
   gtk_scrolled_window_set_policy(swin, GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_shadow_type(swin, GTK_SHADOW_IN);
   AddSensitiveWidget(GTK_WIDGET(swin));

   gtk_widget_show(GTK_WIDGET(mView));
   gtk_container_add(GTK_CONTAINER(swin), GTK_WIDGET(mView));
   gtk_text_view_set_editable(mView, false);
   gtk_text_view_set_wrap_mode(mView, GTK_WRAP_WORD);
   AddSensitiveWidget(GTK_WIDGET(mView));

   GtkButton *accept = Util::CreateButton(GTK_STOCK_OK);
   gtk_widget_show(GTK_WIDGET(accept));
   GTK_WIDGET_SET_FLAGS(accept, GTK_CAN_DEFAULT);
   SetForwardButton(accept);
   g_signal_connect(G_OBJECT(accept), "clicked",
                    G_CALLBACK(&DisclaimerDlg::OnAccept), this);

   GtkWidget *actionArea = Util::CreateActionArea(accept, GetCancelButton(),
                                                  NULL);
   gtk_widget_show(actionArea);
   gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(actionArea), false, true, 0);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DisclaimerDlg::SetText --
 *
 *      Sets the text displayed in mView.
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
DisclaimerDlg::SetText(const Util::string text) // IN
{
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(mView);
   gtk_text_buffer_set_text(buffer, text.c_str(), -1);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DisclaimerDlg::OnAccept --
 *
 *      Callback for "OK" button click.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      accepted signal emitted.
 *
 *-----------------------------------------------------------------------------
 */

void
DisclaimerDlg::OnAccept(GtkButton *button, // IN/UNUSED
                        gpointer userData) // IN
{
   DisclaimerDlg *that = reinterpret_cast<DisclaimerDlg*>(userData);
   ASSERT(that);

   that->accepted();
}


} // namespace cdk
