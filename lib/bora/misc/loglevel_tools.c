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
 *  loglevel_tools.c --
 *
 *	This file contains code which handles creation and destruction
 *	of the loglevel extensions.
 *	It also defines the "user" loglevel extension.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmware.h"
#include "str.h"

/*
 * Globals after this point are defined locally, so we want to disable
 * the effects of VMX86_IMPORT_DLLDATA. Otherwise, we get an
 * "inconsistent DLL linkage" error from MSVC.
 */
#undef VMX86_EXTERN_DATA
#define VMX86_EXTERN_DATA  extern

#include "loglevel_tools.h"

#define LOGLEVEL_MODULE none
#include "loglevel_user.h"


/*
 * Global state
 */

LogLevelState logLevelState;

const int8 *logLevelPtr = logLevelState.initialLevels;

LOGLEVEL_EXTENSION_DEFINE(user);


/*
 *------------------------------------------------------------------------
 * 
 * LogLevel_UserExtensionCreate --
 *
 *      Initialize the loglevel extension called "user". This function gets
 *      called from Log_Init() when initializing the UI, the MKS and the VMX.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Malloc.
 *
 *------------------------------------------------------------------------
 */

void
LogLevel_UserExtensionCreate(void)
{
#ifdef VMX86_LOG
   /*
    * Register the extension "user"
    */

   /* We should be the first one to register any extension */
   ASSERT(logLevelState.extensionsList == NULL);

   logLevelState.lastLogLevelOffset = LOGLEVEL_MAX_MONITOR_LOGLEVELS;
   LOGLEVEL_EXTENSION_CREATE(user, LOGLEVEL_USER, 0);

   LOGLEVEL_BYEXTNAME_SET(user, mks, 1);
   LOGLEVEL_BYEXTNAME_SET(user, vga, 1);
   LOGLEVEL_BYEXTNAME_SET(user, svga,  1);
#endif
}


/*
 *------------------------------------------------------------------------
 * 
 * LogLevel_UserExtensionDestroy --
 *      
 *      Unregister the extension "user" from the list of loglevel extensions
 *      and free the memory associated with it.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

void
LogLevel_UserExtensionDestroy(void)
{
   LOGLEVEL_EXTENSION_DESTROY(user);
}


#ifdef VMX86_LOG
/*
 *----------------------------------------------------------------------------
 *
 *  LogLevel_ExtensionCreate --
 *
 *      Add a cell to the linked list of loglevel extensions.
 *      Note: 
 *           we currently don't handle interleaved creation and destruction of
 *           extensions.
 *
 *  Results:
 *      - The offset (>= 0) of the extension in the chunk,
 *      - -1 if there is not enough memory.
 *
 *  Side effects:
 *      Space is allocated in the shared area.
 *
 *----------------------------------------------------------------------------
 */

int
LogLevel_ExtensionCreate(const char *name,    // IN: the extension
                         const char **table,  // IN: the array of module name
                         int size,            // IN: the number of modules
                         int monitor)         // IN: needs access from monitor
{
#define LOGLEVEL_OFFSET 1
   LogLevelState *ll = &logLevelState;
   LogLevelExtensionCell *cell;
   int *count;
   int i;
 
   ASSERT(name != NULL);
   ASSERT(table != NULL && table[0] != NULL);

   ASSERT(0 < size && size <= LOGLEVEL_MAX_NUM_LOGLEVELS);
   if (monitor) {
      ASSERT(ll->lastLogLevelOffset >= LOGLEVEL_MAX_MONITOR_LOGLEVELS);
      count = &ll->lastMonitorLogLevelOffset;
      if (*count + size > LOGLEVEL_MAX_MONITOR_LOGLEVELS) {
         Warning("LOGLEVEL: No room left in shared area for %s's loglevels "
                 "(add %u).\n",
	         name, *count + size - LOGLEVEL_MAX_MONITOR_LOGLEVELS);
         return 0;
      }
   } else {
      count = &ll->lastLogLevelOffset;
      if (*count + size > LOGLEVEL_MAX_NUM_LOGLEVELS) {
         Warning("LOGLEVEL: No room left in loglevel array for %s's loglevels "
                 "(add %u).\n",
	         name, *count + size - LOGLEVEL_MAX_NUM_LOGLEVELS);
         return 0;
      }
   }

   /*
    * Allocate the new cell and add it to the list of registered extensions
    */

   cell = calloc(1, sizeof *cell);
   ASSERT_MEM_ALLOC(cell != NULL);

   cell->name = strdup(name);
   ASSERT_MEM_ALLOC(cell->name);
   cell->size = size;
   cell->offset = *count;

   cell->table = malloc(size * sizeof *cell->table);
   ASSERT_MEM_ALLOC(cell->table != NULL);
   for (i = 0; i < size; i++) {
      cell->table[i] = strdup(table[i]);
      ASSERT_MEM_ALLOC(cell->table[i] != NULL);
   }

   cell->next = ll->extensionsList;
   ll->extensionsList = cell;

   *count += size; 

   return cell->offset;
#undef LOGLEVEL_OFFSET
}


/*
 *----------------------------------------------------------------------------
 *
 *  LogLevel_ExtensionDestroy --
 *
 *      Destroys a loglevel extension identified by its name.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

void
LogLevel_ExtensionDestroy(const char *extension) // IN
{
   LogLevelExtensionCell *cell, **cellp;
   int i;

   for (cellp = &logLevelState.extensionsList;
	(cell = *cellp) != NULL && strcmp(cell->name, extension) != 0;
	cellp = &cell->next) {
   }
   ASSERT(cell != NULL);

   *cellp = cell->next;

   for (i = 0; i < cell->size; i++) {
      free(cell->table[i]);
   }
   free(cell->table);
   free(cell->name);
   free(cell);
}


/*
 *----------------------------------------------------------------------------
 *
 *  LogLevel_LookUpOffset --
 *
 *      Look for the specified module in the extension given and give the
 *      offset of this module in the shared area.
 *
 *  Results:
 *      . The log level offset when found
 *      . -1 otherwise.
 *
 *  Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

int
LogLevel_LookUpOffset(const char *extension,   // IN
                      const char *module)      // IN
{
   LogLevelExtensionCell *cell;
   int i;

   for (cell = logLevelState.extensionsList; cell != NULL; cell = cell->next) {
      if (extension == NULL ||
	  Str_Strcasecmp(cell->name, extension) == 0) {
         if (module == NULL) {
            return cell->offset;
         }
         for (i = 0; i < cell->size; i++) {
            if (Str_Strcasecmp(cell->table[i], module) == 0) {
               return cell->offset + i;
            }
         }
	 if (extension != NULL) {
	    break;
	 }
      }
   }
   return 0;
}


/*
 *----------------------------------------------------------------------------
 *
 *  LogLevel_LookUpVar --
 *
 *      Look for the specified module in the extension given and
 *      return a pointer to the log level variable.
 *
 *  Results:
 *      . Pointer to the log level variable if found.
 *      . NULL otherwise.
 *
 *  Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

const int8 *
LogLevel_LookUpVar(const char *extension,	// IN
                   const char *module)		// IN
{
   int offset = LogLevel_LookUpOffset(extension, module);

   return offset < 0 ? NULL : logLevelPtr + offset;
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * LogLevel_Set --
 *
 *      Takes an optional extension name and a non-optional loglevel and
 *      sets it, if found, in all extensions that have a level of the given
 *      name.
 *
 * Results:
 *      The previous value of the specified loglevel or -1 if no match was
 *      found.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
LogLevel_Set(const char *extension,	// IN: extension name
             const char *module,	// IN: module name
             int value)			// IN: new value
{
#ifdef VMX86_LOG
   int offset = LogLevel_LookUpOffset(extension, module);

   if (offset >= 0) {
      int oldValue = logLevelPtr[offset];

      logLevelState.initialLevels[offset] = (int8)value;
      if (logLevelState.monitorLogLevels && 
          offset < LOGLEVEL_MAX_MONITOR_LOGLEVELS) {
         logLevelState.monitorLogLevels[offset] = (int8)value;
      }
      return oldValue;
   }
#endif
   return -1;
}
