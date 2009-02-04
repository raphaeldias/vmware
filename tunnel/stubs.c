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
 * stubs.c --
 *
 *      Provides bora/lib stubs and basic implementation for functions
 *      needed (currently) by the bora/libs the tunnel code actually uses.
 */


#include <dlfcn.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__APPLE__) && !defined(PATH_MAX)
#include <sys/syslimits.h>
#endif


#include "vm_assert.h"
#include "err.h"
#include "loglevel_tools.h"
#include "msg.h"
#include "posix.h"
#include "syncMutex.h"
#include "syncRecMutex.h"


#if !defined(__linux__) && !defined(__APPLE__)
#error "These stubs only work on Linux (and maybe Mac OS)"
#endif


Bool AtomicUseFence = FALSE;


LogLevelState logLevelState;
const int8 *logLevelPtr = logLevelState.initialLevels;

/* Log everything */
int _loglevel_offset_user = 0;


void
Log(const char *fmt, // IN
    ...)             // IN
{
   va_list args;
   va_start(args, fmt);
   vfprintf(stderr, fmt, args);
   va_end(args);
}


void
Warning(const char *fmt, // IN
        ...)             // IN
{
   va_list args;
   va_start(args, fmt);
   vfprintf(stderr, fmt, args);
   va_end(args);
}


void
Panic(const char *fmt, // IN
      ...)             // IN
{
   va_list args;
   va_start(args, fmt);
   vfprintf(stderr, fmt, args);
   va_end(args);

   exit(1);
}


char *
Msg_GetString(const char *idString) // IN
{
   ASSERT(idString != NULL);
   ASSERT(MSG_MAGICAL(idString));
   return strdup(Msg_StripMSGID(idString));
}


Bool
Preference_Init(void)
{
   return FALSE;
}


Bool
Log_Init(const char *fileName, // IN
         const char *config,   // IN
         const char *suffix)   // IN
{
   return FALSE;
}


VmTimeType
Hostinfo_SystemTimerUS(void)
{
   struct timeval tval;

   /* Read the time from the operating system */
   if (gettimeofday(&tval, NULL) != 0) {
      Panic("gettimeofday failed!\n");
   }

   /* Convert into microseconds */
   return (((VmTimeType)tval.tv_sec) * 1000000 + tval.tv_usec);
}


const char *
Err_Errno2String(Err_Number errorNumber) // IN
{
   return strerror(errorNumber);
}


const char *
Err_ErrString(void)
{
   return Err_Errno2String(errno);
}


Unicode
Posix_ReadLink(ConstUnicode pathName)  // IN:
{
   ssize_t bytes;
   char link[PATH_MAX];
   Unicode result = NULL;

   bytes = readlink(pathName, link, sizeof(link));
   if (bytes != -1) {
      /* add the missing NUL character to path */
      link[bytes] = '\0';
      result = strdup(link);
   }

   return result;
}


void *
Posix_Dlopen(ConstUnicode pathName,  // IN:
             int flag)               // IN:
{
   return dlopen(pathName, flag);
}


Bool
SyncRecMutex_Trylock(SyncRecMutex *that) // IN
{
   return TRUE;
}


Bool
SyncRecMutex_Unlock(SyncRecMutex *that) // IN
{
   return TRUE;
}


Bool
SyncRecMutex_Lock(SyncRecMutex *that) // IN
{
   return TRUE;
}


void
SyncRecMutex_Destroy(SyncRecMutex *that) // IN
{
   // Do nothing.
}


Bool
SyncRecMutex_Init(SyncRecMutex *that, // IN
                  char const *path)   // IN
{
   return TRUE;
}


Bool
SyncMutex_Init(SyncMutex *that,  // IN
               char const *path) // IN
{
   return TRUE;
}


void
SyncMutex_Destroy(SyncMutex *that) // IN
{
   // Do nothing.
}


Bool
SyncMutex_Unlock(SyncMutex *that) // IN
{
   return TRUE;
}


Bool
SyncMutex_Lock(SyncMutex *that) // IN
{
   return TRUE;
}


Bool
CryptoFips_FipsModeEnabled(void)
{
   return FALSE;
}


Util_ThreadID
Util_GetCurrentThreadId(void)
{
   return (Util_ThreadID)getpid();
}


int
Id_SetRESUid(uid_t uid,  // IN
             uid_t euid, // IN
             uid_t suid) // IN
{
   NOT_IMPLEMENTED();
}


Unicode
Unicode_AllocWithLength(const void *buffer,      // IN
                        ssize_t lengthInBytes,   // IN
                        StringEncoding encoding) // IN
{
   void *mem = calloc(1, lengthInBytes);
   return memcpy(mem, buffer, lengthInBytes);
}


Bool
CodeSet_Utf8ToCurrent(char const *bufIn, // IN
                      size_t sizeIn,     // IN
                      char **bufOut,     // OUT
                      size_t *sizeOut)   // OUT
{
   NOT_IMPLEMENTED();
}


#if defined(__APPLE__)
void
Id_SetSuperUser(Bool yes) // IN: TRUE to acquire super user, FALSE to release
{
}


Bool
Config_GetBool(Bool defaultValue,
               const char *fmt,
               ...)
{
    return defaultValue;
}
#endif
