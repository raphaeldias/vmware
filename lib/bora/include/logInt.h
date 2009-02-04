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

#ifndef _LOG_INT_H_
#define _LOG_INT_H_

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#include "log.h"
#include "loglevel_tools.h"
#include "syncRecMutex.h"

/*
 * Automagic log throttling:
 *
 * throttleThreshold = start log throttling only after
 * this many bytes have been logged (allows initial vmx
 * startup spew).
 *
 * throttleBPS = start throttling if more than this
 * many bytes per second are logged.  Continue throttling
 * until rate drops below this value.
 *
 * bytesLogged = total bytes logged.
 *
 * logging rate =  (bytesLogged - lastBytesSample)/(curTime - lastSampleTime)
 */
typedef struct LogThrottleInfo {
   Bool        throttled;
   uint32      throttleThreshold;
   uint32      throttleBPS;
   uint64      bytesLogged;
   VmTimeType  lastSampleTime;
   uint64      lastBytesSample;
} LogThrottleInfo;


/*
 * Log state
 */
typedef struct LogState {
   /*
    * The first four entries are statically initialized.
    * Don't change their position.
    * See definition of logState in log.c.
    */

   int fd;
   Bool quietWarning;
   LogBasicFunc *basicFuncLog;
   LogBasicFunc *basicFuncWarning;

   char *fileName;
   char *appName;
   char *appVersion;
   char *suffix;
   Bool initialized;
   Bool enable;
   Bool append;
   Bool timeStamp;
   Bool millisec;
   int keep;			// a config tristate value
   Bool alwaysKeep;
   Bool isTemp;
   Bool notBOL;
   Bool threadName;
   int keepOld;
   int fastRotation;  // Use a log rotation scheme optimized for vmfs
   char *dir;

   void (*lockFunc)(Bool locking);	// TRUE to lock, FALSE to unlock

   SyncRecMutex lockMutex;  // default lock mutex
   Bool lockMutexInited;

   LogThrottleInfo   throttleInfo;

   /* 
    * Automatic rotation of run-time logs:
    * If logSize exceeds rotateSize, rotate the current log out and
    * use a new log file. This is to prevent log files from growing
    * excessively.
    * Set rotateSize to 0 to prevent this from happening.
    */

   uint64 logSize;
   uint64 rotateSize;
   Bool rotating;
} LogState;

extern LogState logState;


/*
 * Functions
 */

Bool LogInitEx(LogState *log,
               const char *fileName, const char *config, const char *suffix,
               const char *appName, const char *appVersion,
               Bool logging, Bool append,
               unsigned int keepOld, unsigned int throttleThreshold, 
               unsigned int throttleBytesPerSec, Bool switchFile, 
               unsigned int rotateSize);
void LogExit(LogState *log);
void LogUpdateState(LogState *log, Bool enable, Bool append,
                    unsigned keepOld, size_t rotateSize, Bool fastRotation);
Bool LogSwitchFile(LogState *log, const char *fileName, const char *config,
                   Bool copy);
void LogWork(LogState *log, Bool isGuestLog, const char *fmt, va_list args);
void LogSetDir(LogState *log, const char *dir);


/*
 *-----------------------------------------------------------------------------
 *
 * LogLock --
 *
 *      Lock or unlock.
 *
 *	Locking is indirectly supported via logState.lockFunc, which
 *	is provided by the user of this module. If no lock function is
 *	specified, a default mechanism is used (SyncRecMutex).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Lock is taken.
 *
 *-----------------------------------------------------------------------------
 */

static INLINE void
LogLock(LogState *log,
        Bool locking)  // IN: TRUE to lock, FALSE to unlock
{
   if (log->lockFunc != NULL) {
      log->lockFunc(locking);
   } else if (log->lockMutexInited) {
      if (locking) {
         SyncRecMutex_Lock(&log->lockMutex);
      } else {
         SyncRecMutex_Unlock(&log->lockMutex);
      }
   }
}


/*
 * Debugging
 */

#define LOGWARN(args)
//#define LOGWARN(args) Warning args

#endif /* _LOG_INT_H_ */
