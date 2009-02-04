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
 * util.hh --
 *
 *    CDK utilities.
 */

#ifndef UTIL_HH
#define UTIL_HH


#include <boost/function.hpp>
#include <exception>
#include <gtk/gtkbutton.h>
#include <gtk/gtkcomboboxentry.h>
#include <gtk/gtkmessagedialog.h>

#ifdef UTF_STRING
#include "stringxx/string.hh"
#else
#include <string>
#endif


extern "C" {
#include "vm_assert.h"
#include "msg.h"
#include "log.h"
#include "util.h"
}


namespace cdk {
namespace Util {


#define CDK_MSG(_id, _str) \
   cdk::Util::GetLocalString(MSGID("cdk.linux." #_id) _str)


// From lui/vm_gtk.h
#define VM_SPACING 10


#ifdef UTF_STRING
typedef utf::string string;
#else
typedef std::string string;
#endif


class exception
   : public std::exception
{
public:
   explicit exception(const string &msg, const string &code = "")
      : mMsg(msg),
        mCode(code) {}

   virtual ~exception() throw () {}
   virtual const char *what() const throw () { return mMsg.c_str(); }
   const string &code() const throw() { return mCode; }

private:
   string mMsg;
   string mCode;
};


typedef boost::function0<void> DoneSlot;
typedef boost::function2<void, bool /* cancelled */, exception> AbortSlot;

DoneSlot EmptyDoneSlot();
AbortSlot LogAbortSlot();


Util::string GetComboBoxEntryText(GtkComboBoxEntry *combo);
Util::string GetComboBoxText(GtkComboBox *combo);
GtkButton *CreateButton(const string stockId, string label = "");
GtkWidget *CreateActionArea(GtkButton *button1, ...);
void OverrideWindowUserTime(GtkWindow *window);
void UserWarning(const char *format, ...);


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetLocalString --
 *
 *      Returns a localized string from the specified msg ID.  Callers should
 *      use the CDK_MSG wrapper if possible.
 *
 *      XXX: typedef to utf::LocalizedString once it's moved there from cui.
 *
 * Results:
 *      The localized string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

string inline
GetLocalString(const char *msgID) // IN
{
   ASSERT(msgID != NULL);
   char *msg = Msg_GetString(msgID);
   ASSERT(msg != NULL);
   string s(msg);
   free(msg);
   return s;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::FormatV --
 *
 *      Format a string (va_list version).  From cui/cui.cc.
 *
 * Results:
 *      The formatted string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

string inline
FormatV(char const *format, // IN
        va_list arguments)  // IN
{
   size_t length;
   char *cresult;

   cresult = Str_Vasprintf(&length, format, arguments);

   /* This properly raises an exception if cresult == NULL. */
   string result(cresult);
   free(cresult);

   return result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::Format --
 *
 *      Format a string.  From cui/cui.cc.
 *
 * Results:
 *      The formatted string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

string inline
Format(char const *format, // IN
       ...)
{
   va_list arguments;
   va_start(arguments, format);
   string result = FormatV(format, arguments);
   va_end(arguments);
   return result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::Throw --
 *
 *      Throws an exception and in debug builds create a backtrace for it.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

template<typename T>
inline NORETURN void
Throw(const T& e) // IN: exception
{
   DEBUG_ONLY(Util_Backtrace(0));
   throw e;
}


} // namespace Util
} // namespace cdk


#endif // UTIL_HH
