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
 * transitionDlg.cc --
 *
 *      Shows animation while a desktop connection is established.
 */


#include <gtk/gtk.h>


#include "util.hh"
#include "transitionDlg.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::TransitionDlg::TransitionDlg --
 *
 *      Constructor
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

TransitionDlg::TransitionDlg(TransitionType type,         // IN
                             const Util::string &message) // IN
   : mImage(gtk_image_new()),
     mFrame(0),
     mRate(0),
     mTimeout(0),
     mTransitionType(type)
{
   GtkWidget *box = gtk_vbox_new(false, 3 * VM_SPACING);
   Init(box);
   gtk_container_set_border_width(GTK_CONTAINER(box), VM_SPACING);

   gtk_widget_show(mImage);
   gtk_box_pack_start_defaults(GTK_BOX(box), mImage);
   gtk_misc_set_padding(GTK_MISC(mImage), 2 * VM_SPACING, 2 * VM_SPACING);
   g_signal_connect(mImage, "realize",
                    G_CALLBACK(&TransitionDlg::OnImageRealized), this);
   g_signal_connect(mImage, "unrealize",
                    G_CALLBACK(&TransitionDlg::OnImageUnrealized), this);

   GtkWidget *label = gtk_label_new(message.c_str());
   gtk_widget_show(label);
   gtk_box_pack_start_defaults(GTK_BOX(box), label);
   gtk_label_set_line_wrap(GTK_LABEL(label), true);

   GtkWidget *actionArea;
   if (type == TRANSITION_PROGRESS) {
      actionArea = Util::CreateActionArea(GetCancelButton(), NULL);
   } else {
      GtkButton *retry = Util::CreateButton(
         GTK_STOCK_REDO, CDK_MSG(transitionRetry, "_Retry"));
      GTK_WIDGET_SET_FLAGS(retry, GTK_CAN_DEFAULT);
      SetForwardButton(retry);
      g_signal_connect(retry, "clicked",
                       G_CALLBACK(&TransitionDlg::OnRetryClicked), this);

      actionArea = Util::CreateActionArea(retry, GetCancelButton(), NULL);
   }
   gtk_widget_show(actionArea);
   gtk_box_pack_start_defaults(GTK_BOX(box), actionArea);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::TransitionDlg::~TransitionDlg --
 *
 *      Deconstructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Animation pixbufs are unrefed.
 *
 *-----------------------------------------------------------------------------
 */

TransitionDlg::~TransitionDlg()
{
   for (std::vector<GdkPixbuf *>::iterator i = mPixbufs.begin();
        i != mPixbufs.end(); i++) {
      g_object_unref(*i);
   }
   mPixbufs.clear();
   if (mTimeout) {
      g_source_remove(mTimeout);
      mTimeout = 0;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::TransitionDlg::SetAnimation --
 *
 *      Display a pixbuf animation.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The dialog has an animation on it.
 *
 *-----------------------------------------------------------------------------
 */

void
TransitionDlg::SetAnimation(GdkPixbufAnimation *animation) // IN
{
   g_object_set(mImage, "pixbuf-animation", animation, NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::TransitionDlg::SetAnimation --
 *
 *      Display an animation.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The dialog has an animation in it.
 *
 *-----------------------------------------------------------------------------
 */

void
TransitionDlg::SetAnimation(std::vector<GdkPixbuf *> pixbufs, float rate)
{
   ASSERT(rate > 0);
   mFrame = 0;
   mRate = rate;
   for (std::vector<GdkPixbuf *>::iterator i = pixbufs.begin();
        i != pixbufs.end(); i++) {
      g_object_ref(*i);
      mPixbufs.push_back(*i);
   }
   SetImage(mPixbufs[0]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::TransitionDlg::SetImage --
 *
 *      Display a pixbuf.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The dialog has an image on it.
 *
 *-----------------------------------------------------------------------------
 */

void
TransitionDlg::SetImage(GdkPixbuf *pixbuf) // IN
{
   g_object_set(mImage, "pixbuf", pixbuf, NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::TransitionDlg::SetStock --
 *
 *      Display a stock image.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The dialog has a stock image on it.
 *
 *-----------------------------------------------------------------------------
 */

void
TransitionDlg::SetStock(const Util::string &stockId) // IN
{
   g_object_set(mImage, "stock", stockId.c_str(),
                "icon-size", GTK_ICON_SIZE_DIALOG, NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::TransitionDlg::OnRetryClicked --
 *
 *      Click handler for the retry button.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Emits retry signal.
 *
 *-----------------------------------------------------------------------------
 */

void
TransitionDlg::OnRetryClicked(GtkWidget *button, // IN/UNUSED
                              gpointer userData) // IN
{
   TransitionDlg *that = reinterpret_cast<TransitionDlg *>(userData);
   ASSERT(that);
   that->retry();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::TransitionDlg::OnImageRealized --
 *
 *      When the image is visible, we should start animating (if we
 *      need to).
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Animation may be started.
 *
 *-----------------------------------------------------------------------------
 */

void
TransitionDlg::OnImageRealized(GtkWidget *widget, gpointer userData)
{
   TransitionDlg *that = reinterpret_cast<TransitionDlg *>(userData);
   ASSERT(that);

   if (!that->mPixbufs.empty()) {
      that->StartAnimating();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::TransitionDlg::OnImageUnrealized --
 *
 *      When the image is hidden, stop animating.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      No more animating.
 *
 *-----------------------------------------------------------------------------
 */

void
cdk::TransitionDlg::OnImageUnrealized(GtkWidget *widget, // IN/UNUSED
                                      gpointer userData) // IN
{
   TransitionDlg *that = reinterpret_cast<TransitionDlg *>(userData);
   ASSERT(that);

   if (!that->mPixbufs.empty()) {
      that->StopAnimating();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::TransitionDlg::StartAnimating --
 *
 *      Add a timer which will change the image to the next frame.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Timeout source added to main loop.
 *
 *-----------------------------------------------------------------------------
 */

void
TransitionDlg::StartAnimating()
{
   ASSERT(!mPixbufs.empty());
   ASSERT(mTimeout == 0);

   // rate is f/s, we want s/f (and of course timeout is in ms)
   mTimeout = g_timeout_add((unsigned int)(1000 / mRate),
                            (GSourceFunc)&TransitionDlg::Animate, this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::TransitionDlg::StopAnimating --
 *
 *      Remove animation timer.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Timeout source removed from main loop.
 *
 *-----------------------------------------------------------------------------
 */

void
TransitionDlg::StopAnimating()
{
   ASSERT(!mPixbufs.empty());
   if (mTimeout != 0) {
      g_source_remove(mTimeout);
      mTimeout = 0;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::TransitionDlg::Animate --
 *
 *      Timeout handler for animation.  Display a new frame.
 *
 * Results:
 *      true if this source should be kept in the main loop.
 *
 * Side effects:
 *      Image is updated.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
TransitionDlg::Animate(gpointer userData)
{
   TransitionDlg *that = reinterpret_cast<TransitionDlg *>(userData);
   ASSERT(that);

   that->mFrame = (that->mFrame + 1) % that->mPixbufs.size();
   that->SetImage(that->mPixbufs[that->mFrame]);

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::TransitionDlg::LoadAnimation --
 *
 *      Load an inlined pixbuf source into a vector of pixbuf frames.
 *
 * Results:
 *      A vector of GdkPixbuf frames.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

std::vector<GdkPixbuf *>
TransitionDlg::LoadAnimation(int data_length,     // IN
                             const guint8 *data,  // IN
                             bool copy_pixels,    // IN
                             unsigned int frames) // IN
{
   std::vector<GdkPixbuf *> ret;
   GdkPixbuf *pb = gdk_pixbuf_new_from_inline(data_length, data, copy_pixels,
                                              NULL);
   if (!pb) {
      return ret;
   }

   GdkPixbuf *sub;
   int height = gdk_pixbuf_get_height(pb) / frames;
   int width = gdk_pixbuf_get_width(pb);
   for (unsigned int frame = 0; frame < frames; frame++) {
      sub = gdk_pixbuf_new_subpixbuf(pb, 0, height * frame, width, height);
      ret.push_back(sub);
   }
   gdk_pixbuf_unref(pb);
   return ret;
}


} // namespace cdk
