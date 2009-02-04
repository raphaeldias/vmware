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

#ifndef _LOGLEVEL_TOOLS_H_
#define _LOGLEVEL_TOOLS_H_

#define INCLUDE_ALLOW_VMCORE
#define INCLUDE_ALLOW_VMMEXT
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_VMMON
#include "includeCheck.h"


/*
 * As the space in the shared area is very precious, the maximum number of
 * modules (in all the extensions) is set to 256 so that we are sure to never
 * overflow it. If someday someone needs more space for the modules in the
 * shared area, be sure to check that the shared area is large enough before
 * bumping this number. --Maxime
 */

#define LOGLEVEL_MAX_MONITOR_LOGLEVELS 192
#define LOGLEVEL_MAX_NUM_LOGLEVELS 512

#define LOGLEVEL_MAX_EXTENSION_NAME_LEN 64
#define LOGLEVEL_MAX_MODULE_NAME_LEN    64


/*
 * This structure contains all the informations for a specific loglevel
 * extension.
 */

typedef struct LogLevelExtensionCell {
   char *name;            /* Name of the extension */
   char **table;          /* Names of the loglevels */
   int offset;            /* Offset into shared area loglevels */
   int size;              /* Number of loglevel for this extension */
   struct LogLevelExtensionCell *next; /* Next cell */
} LogLevelExtensionCell;


/*
 * Loglevel state
 */

typedef struct LogLevelState {
   LogLevelExtensionCell *extensionsList;
   int8 initialLevels[LOGLEVEL_MAX_NUM_LOGLEVELS];
   int8 *monitorLogLevels; /* LOGLEVEL_MAX_MONITOR_LOGLEVELS array */
   int lastLogLevelOffset;
   int lastMonitorLogLevelOffset;
} LogLevelState;

extern LogLevelState logLevelState;


/*
 * The current log levels
 */

VMX86_EXTERN_DATA const int8 *logLevelPtr;


/*
 * Loglevel definition and initialization
 */

#define LOGLEVEL_EXTENSION_DEFINE(ext) \
        int LOGLEVEL_EXTOFFSET(ext)

#ifdef VMX86_LOG // {

#ifdef VMM // {

#define LOGLEVEL_EXTENSION_INIT(ext) \
        do { \
           LOGLEVEL_EXTOFFSET(ext) = LogLevel_LookUpOffset(XSTR(ext), NULL); \
           ASSERT(LOGLEVEL_EXTOFFSET(ext) >= 0); \
        } while (0) 

#else // ifdef VMM } else {

#define LOGLEVEL_EXTENSION_CREATE(ext, list, mon) \
        do { \
	   static const char *_strtbl[] = { list(XSTR) }; \
           LOGLEVEL_EXTOFFSET(ext) = \
	      LogLevel_ExtensionCreate(XSTR(ext), _strtbl, \
				       sizeof _strtbl / sizeof *_strtbl, mon); \
           ASSERT_NOT_IMPLEMENTED(LOGLEVEL_EXTOFFSET(ext) >= 0); \
        } while (0)

#define LOGLEVEL_EXTENSION_DESTROY(ext) \
        LogLevel_ExtensionDestroy(XSTR(ext))

#endif // }

#else // ifdef VMX86_LOG } else {

#ifdef VMM
#define LOGLEVEL_EXTENSION_INIT(ext)
#else
#define LOGLEVEL_EXTENSION_CREATE(ext, list, mon)
#define LOGLEVEL_EXTENSION_DESTROY(ext)
#endif

#endif // }


/*
 * Functions
 */

#if !defined(VMM)
void LogLevel_UserExtensionCreate(void);
void LogLevel_UserExtensionDestroy(void);
#endif

#ifdef VMX86_LOG
int LogLevel_ExtensionCreate(const char *name, const char **table, int size, int mon);
void LogLevel_ExtensionDestroy(const char *extension);
int LogLevel_LookUpOffset(const char *extension, const char *module);
#endif
int LogLevel_Set(const char *extension, const char *module, int value);


#endif /* _LOGLEVEL_TOOLS_H_ */
