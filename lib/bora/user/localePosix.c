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

/*
 * localePosix.c --
 *
 *	Host locale interface
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>

#include "vmware.h"
#include "vmlocale.h"
#include "str.h"
#include "util.h"

/*
 * Local data
 */

typedef struct LocaleMap {
   const char *standardName;
   const char *systemName;
} LocaleMap;

static LocaleMap const localeMap[] = {
#if 0 // XXX no English dictionary yet
   { "en", "en_AU" },
   { "en", "en_CA" },
   { "en", "en_DK" },
   { "en", "en_GB" },
   { "en", "en_IE" },
   { "en", "en_RN" },
   { "en", "en_UK" },
   { "en", "en_US" },
#endif
   { "ja", "ja_JP" },
   { NULL }
};


/*
 *-----------------------------------------------------------------------------
 *
 * LocaleGetLocaleForCategory --
 *
 *      Get the user's locale for the specified category.
 *
 * Results:
 *      The user's locale in a malloc()ed NUL-terminated string,
 *      or NULL if the user's locale couldn't be determined.
 *      If non-NULL, caller must free.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

char *
LocaleGetLocaleForCategory(int category) // IN
{
   const char *locale;
   char *savedLocale = NULL;
   char *retval;

   /*
    * Get the current locale with setlocale().
    * If we get "C" or "POSIX", then we set locale to ""
    * and try one more time.  In this case, we save the original
    * locale in savedLocale and restore it before we return.
    */
   locale = setlocale(category, NULL);
   if (locale == NULL) {
      Log("LOCALE cannot get initial locale for category %d.\n", category);
   } else if (strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0) {
      savedLocale = Util_SafeStrdup(locale);
      locale = setlocale(category, "");
      if (locale == NULL) {
         Log("LOCALE cannot set default locale for category %d.\n", category);
      } else if (strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0) {
         locale = NULL;
      }
   }
   retval = Util_SafeStrdup(locale);

   /*
    * Return, but first restore original locale if we've disturbed it.
    */
   if (savedLocale != NULL) {
      setlocale(category, savedLocale);
      free(savedLocale);
   }

   return retval;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Locale_GetUserLanguage --
 *
 *      Get the user's message language.
 *
 * Results:
 *      The user's language in a malloc()ed NUL-terminated string,
 *      or NULL if the user's language couldn't be determined.
 *      If non-NULL, caller must free.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

char *
Locale_GetUserLanguage(void)
{
   char *locale;
   LocaleMap const *map;

   /*
    * Get the current message locale and map to our standard language name.
    */
   locale = LocaleGetLocaleForCategory(LC_MESSAGES);
   if (locale != NULL) {
      int codesetStart = Str_Strcspn(locale, ".");

      for (map = localeMap;
	   map->standardName != NULL &&
	   Str_Strncasecmp(locale, map->standardName, codesetStart) != 0 &&
	   Str_Strncasecmp(locale, map->systemName, codesetStart) != 0;
	   map++) {
      }
      Log("LOCALE %s -> %s\n", locale,
          map->standardName == NULL ? "NULL" : map->standardName);
      free(locale);
      locale = Util_SafeStrdup(map->standardName);
   }

   return locale;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Locale_GetUserNumericLocale --
 *
 *      Get the user's numeric locale.
 *
 * Results:
 *      The user's numeric locale in a malloc()ed NUL-terminated string,
 *      or NULL if the user's numeric locale couldn't be determined.
 *      If non-NULL, caller must free.
 
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

char *
Locale_GetUserNumericLocale(void)
{
   return LocaleGetLocaleForCategory(LC_NUMERIC);
}
