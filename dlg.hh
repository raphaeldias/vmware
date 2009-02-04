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
 * dlg.hh --
 *
 *    Base class for client dialogs.
 */

#ifndef DLG_HH
#define DLG_HH


#include <list>
#include <boost/signal.hpp>

#include <gtk/gtkentry.h>
#include <gtk/gtkwidget.h>


#include "util.hh"


namespace cdk {


class Dlg
{
public:
   Dlg();
   virtual ~Dlg();

   GtkWidget *GetContent() const { return mContent; }
   virtual void SetSensitive(bool sensitive);
   bool IsSensitive() const { return mSensitive; }
   virtual bool IsResizable() const { return false; }

   virtual void Cancel();
   boost::signal0<void> cancel;

protected:
   static void UpdateForwardButton(Dlg *that);

   void Init(GtkWidget *widget);
   void SetFocusWidget(GtkWidget *widget);
   virtual void AddSensitiveWidget(GtkWidget *widget)
      { mSensitiveWidgets.push_back(widget); }
   void SetForwardButton(GtkButton *button) { mForwardButton = button; }
   void AddRequiredEntry(GtkEntry *entry);
   GtkButton *GetCancelButton();

private:
   static void OnContentHierarchyChanged(GtkWidget *widget,
                                         GtkWidget *oldToplevel,
                                         gpointer userData);
   static void OnCancel(GtkButton *button, gpointer userData);
   static void OnTreeViewRealizeGrabFocus(GtkWidget *widget);

   void GrabFocus();

   GtkWidget *mContent;
   GtkWidget *mFocusWidget;
   GtkButton *mCancel;
   GtkButton *mForwardButton;
   std::list<GtkEntry *> mRequiredEntries;
   std::list<GtkWidget *> mSensitiveWidgets;
   bool mSensitive;
};


} // namespace cdk


#endif // DLG_HH
