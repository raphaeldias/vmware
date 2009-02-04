/*********************************************************
 * Copyright (C) 2008 VMware, Inc. All rights reserved.
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
 * msgid.h --
 *
 *	Message ID magic
 */

#ifndef _MSGID_H_
#define _MSGID_H_

#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE
#include "includeCheck.h"


/*
 * Message ID macros
 *
 * Use as in
 *	Msg_Append(MSGID(file.openFailed) "Failed to open file %s: %s.\n"
 *		   fileName, Msg_ErrString())
 *	Msg_Append(MSGID(mks.powerOnFailed) "Power on failed.\n")
 * or
 *	Msg_Hint(TRUE, HINT_OK,
 *		 MSGID(mks.noDGA) "No full screen mode.\n").
 *
 * Don't make MSG_MAGIC_LEN (sizeof MSG_MAGIC - 1), since
 * that may cause the string to be in the object file, even
 * when it's not used at run time.  And we are trying
 * to avoid littering the output with the magic string.
 *
 * -- edward
 */

#define MSG_MAGIC	"@&!*@*@"
#define MSG_MAGIC_LEN	7
#define MSGID(id)	MSG_MAGIC "(msg." #id ")"
#define MSG_BUTTON_ID "(button."
#define MSG_BUTTON_ID_LEN 8
#define BUTTONID(id)	MSG_MAGIC MSG_BUTTON_ID #id ")"

// the X hides MSG_MAGIC so it won't appear in the object file
#define MSG_MAGICAL(s)	(strncmp(s, MSG_MAGIC"X", MSG_MAGIC_LEN) == 0)

// Start after MSG_MAGIC so it won't appear in the object file either.
#define MSG_HAS_BUTTONID(s) \
   (MSG_MAGICAL(s) && \
    (strncmp(&(s)[MSG_MAGIC_LEN], MSG_BUTTON_ID, MSG_BUTTON_ID_LEN) == 0))


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_StripMSGID --
 *
 *      Returns the string that is inside the MSGID() or if it doesn't
 *      have a MSGID just return the string.
 *
 * Results:
 *      The unlocalized string.
 *
 * Side Effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static INLINE const char *
Msg_StripMSGID(const char *idString)    // IN
{
   if (idString != NULL && MSG_MAGICAL(idString)) {
      idString += MSG_MAGIC_LEN;
      idString++;
      idString = strchr(idString, ')');
      idString++;
   }
   return idString;
}


#endif // ifndef _MSGID_H_
