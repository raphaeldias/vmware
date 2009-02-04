/*********************************************************
 * Copyright (C) 1998 VMware, Inc. All rights reserved.
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


#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "vmware.h"
#include "vm_version.h"
#include "str.h"
#include "url.h"


static struct {
   const char *name;
   char *value;
} appends[] = {
#define URLAPPENDENTRY(codename, name) { name, NULL, },
#   include "urlAppend.h"
#undef URLAPPENDENTRY
};


static struct {
   /*
    * The ops group, which generate those IDs for the engineering group,
    * guarantees that:
    * . An ID will never be 0
    * . A deprecated ID will never be re-used in the future
    *
    *   --hpreg
    */
   UrlId id;

   unsigned int appends;
} URLs[] = {
#define URLTABLEENTRY(codename, id, append, name) { id, append, },
#   include "urlTable.h"
#undef URLTABLEENTRY
};


/*
 *-----------------------------------------------------------------------------
 *
 * URL_SetAppend --
 *
 *    Assign a value to append to URLs (use NULL if there is none) --hpreg
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 * Bugs:
 *    Maybe we should use a special dictionary to store those name/value
 *    pairs. It is well suited for that. And we can easily ensure that all
 *    pairs will be shared between processes.
 *
 *-----------------------------------------------------------------------------
 */

void
URL_SetAppend(UrlAppend index,   // IN
              char const *value) // IN
{
   ASSERT(index >= 0 && index < sizeof(appends) / sizeof(appends[0]));

   free(appends[index].value);
   appends[index].value = value ? strdup(value) : NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * URL_Get --
 *
 *    Dynamically build a URL --hpreg
 *
 * Results:
 *    On success: The allocated URL string
 *    On failure: NULL
 *
 * Side effects:
 *    None
 *
 *-----------------------------------------------------------------------------
 */

char *
URL_Get(UrlId id,    // IN
        Bool append) // IN: Allow things to be appended
{
   unsigned int index;
   char *result;
   
   if (id == 0) {
      /* Invalid ID --hpreg */
      return NULL;
   }
   /* Lookup the URLs array for a URL ID --hpreg */
   for (index = 0; index < sizeof(URLs) / sizeof(URLs[0]); index++) {
      if (URLs[index].id == id) {
         break;
      }
   }
   if (index == sizeof(URLs) / sizeof(URLs[0])) {
      /* Invalid ID --hpreg */
      return NULL;
   }

   result = Str_Asprintf(NULL, "http://%s.com/info?id=%u",
                         PRODUCT_GENERIC_NAME_LOWER,
                         URLs[index].id);
   if (result == NULL) {
      return NULL;
   }

   if (append) {
      unsigned int i;

      for (i = 0; i < sizeof(appends) / sizeof(appends[0]); i++) {
         if ((URLs[index].appends & (1 << i)) && appends[i].value) {
            char *tmpResult;
            char encName[1024];
            char encValue[1024];

            URL_EncodeURL(appends[i].name, encName, sizeof(encName));
            URL_EncodeURL(appends[i].value, encValue, sizeof(encValue));
            tmpResult = Str_Asprintf(NULL, "%1$s&%2$s=%3$s", result, encName,
                                     encValue);
            free(result);
            if (tmpResult == NULL) {
               return NULL;
            }
            result = tmpResult;
         }
      }
   }

   return result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * URL_Destroy --
 *
 *    Free the memory allocated to the appends array --hpreg
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *-----------------------------------------------------------------------------
 */

void
URL_Destroy(void)
{
   unsigned int i;

   for (i = 0; i < sizeof(appends) / sizeof(appends[0]); i++) {
      free(appends[i].value);
      appends[i].value = NULL;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * URL_EncodeURL --
 *
 *    URL-encode a string, as described in RFC 1738
 *
 * Results:
 *    The quoted argument.
 *
 * Side effects:
 *    None.
 *
 *    XXX This function should be implemented in terms of Escape_Do().
 *
 *-----------------------------------------------------------------------------
 */

void
URL_EncodeURL(const char *in, // IN
              char *out,      // OUT
              int outlength)  // IN
{
   char *end = out + outlength - 4;	// octet (3) + null byte (1)

   while (*in != '\0' && out < end) {
      if (('a' <= *in && *in <= 'z') ||
	  ('A' <= *in && *in <= 'Z') ||
	  ('0' <= *in && *in <= '9')) {
	 /*
	  * Safe (conservatively).
	  */
	 *out++ = *in;
      } else {
         // The cast to unsigned is required to avoid mangling utf8.
	 Str_Sprintf(out, end - out + 3, "%%%02x", (unsigned char)*in);
	 out += 3;
      }

      in++;
   }
   *out = '\0';
}
