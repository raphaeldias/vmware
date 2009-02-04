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
 *  log.c --
 *
 *	Generic logging code
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include "safetime.h"
#ifndef _WIN32
#  include <unistd.h>
#  include <sys/uio.h>
#  include <sys/time.h>
#else
#  include <io.h>
#  include <process.h>
#  include <direct.h>
#  include <sys/types.h>
#  include <sys/timeb.h>
#endif
#include <errno.h>
#include <limits.h>

#include "vmware.h"
#include "productState.h"
#include "vthreadBase.h"
#include "vm_version.h"
#include "config.h"
#include "hostinfo.h"
#include "msg.h"
#include "util.h"
#include "str.h"
#include "file.h"
#include "url.h"
#include "logInt.h"
#include "posix.h"
#ifdef _WIN32
#include "win32u.h"
#include "win32util.h"
#endif

/*
 *  Define FLUSH_LOG_TO_DISK in order to sync after each write to the
 *  log file, which may be useful for debugging.
 */

// #define FLUSH_LOG_TO_DISK

#ifndef FLUSH_LOG_TO_DISK
#define FLUSH_LOGFD(_fd)
#else  /* ifndef FLUSH_LOG_TO_DISK */
#ifdef linux
#define FLUSH_LOGFD(_fd) fsync(_fd)
#elif _WIN32
#define FLUSH_LOGFD(_fd) FlushFileBuffers((HANDLE) _get_osfhandle(_fd))
#else
#error FLUSH_LOG_TO_DISK not defined for this platform
#endif
#endif /* ifndef FLUSH_LOG_TO_DISK else */

#define LOG_THROTTLE_PERIOD     (5 * 1000 * 1000)
#define LOG_UNTHROTTLE_PERIOD   (60 * 1000 * 1000)


/*
 * Module state
 */

LogState logState = {
   -1,		// fd
   /* quiet warnings don't make sense on server products or developer/qa builds */
#if defined VMX86_DEVEL || defined VMX86_DEBUG || defined VMX86_SERVER
   FALSE,	// quietWarning
#else
   TRUE,	// quietWarning
#endif
   NULL,        // basicFuncLog
   NULL,        // basicFuncWarning
};

LogThrottleInfo guestLogThrottleInfo = { FALSE, 0, 0, 0, 0, 0 };

/*
 *  Local functions
 */

static size_t LogMakeTimeString(Bool millisec, char *buf, size_t max);
static char *LogComputeFileName(LogState *log, const char *config,
                                Bool *isTemp);
static Bool LogCopyFile(LogState *log, int destFd, int srcFd,
			const char *fileName);
static int LogOpenNoSymlinkAttack(const char *fileName);
static void LogBackupOldLogs(const char *fileName, int n, Bool noRename);
static void LogWriteTagString(LogState *log);
static Bool LogRotateFile(LogState *log);

/*
 * Compatibility
 */

#ifdef _WIN32

static INLINE int
ftruncate(int fd, off_t pos)
{
   HANDLE h = (HANDLE) _get_osfhandle(fd);

   return SetFilePointer(h, pos, NULL, FILE_BEGIN) == pos &&
	  SetEndOfFile(h) ?
	    0 : -1;
}

#endif // ifdef WIN32


/*
 *----------------------------------------------------------------------
 *
 * GuestLog_Init --
 *
 *      Initializes the throttling configuration for guest logs.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
void
GuestLog_Init(void)
{
   guestLogThrottleInfo.throttled = FALSE;
   guestLogThrottleInfo.throttleThreshold = Config_GetLong(LOG_DEFAULT_THROTTLE_THRESHOLD / 2,
                                                           "log.guestThrottleThreshold");
   guestLogThrottleInfo.throttleBPS = Config_GetLong(0, "log.guestThrottleBytesPerSec");
   guestLogThrottleInfo.bytesLogged = 0;
   guestLogThrottleInfo.lastSampleTime = 0;
   guestLogThrottleInfo.lastBytesSample = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * Log_Init --
 *
 *      Initialization.
 *
 *	This can (and should) be called very early, to establish
 *	the initial (possibly temporary) log file.
 *	Because we are called so early, only local config and preferences
 *	are available.
 *
 * Results:
 *      TRUE if success, FALSE if failure.
 *
 * Side effects:
 *      Create initial log file.
 *	Enable logging.
 *
 *----------------------------------------------------------------------
 */

Bool
Log_Init(const char *fileName, // IN: file name, if known
	 const char *config,   // IN: config variable to look up
	 const char *suffix)   // IN: suffix to generate log file name
{
   return Log_InitForApp(fileName, config, suffix, ProductState_GetName(),
                         ProductState_GetVersion());
}


/*
 *----------------------------------------------------------------------
 *
 * Log_InitForApp --
 *
 *      (Similar to Log_Init) Takes additional appName and appVersion
 *      parameters. Use this for applications that are built as part of
 *      a product but which want to report their own name and version
 *      in their own log.
 *
 * Results:
 *      TRUE if success, FALSE if failure.
 *
 * Side effects:
 *      Create initial log file.
 *	Enable logging.
 *
 *----------------------------------------------------------------------
 */

Bool
Log_InitForApp(const char *fileName,   // IN: file name, if known
               const char *config,     // IN: config variable to look up
               const char *suffix,     // IN: suffix to generate log file name
               const char *appName,    // IN: Application name for log header
               const char *appVersion) // IN: Application version for log header
{
   LogLevel_UserExtensionCreate();
   return LogInitEx(&logState, fileName, config, suffix, appName, appVersion,
                    Config_GetBool(TRUE, "logging"),
                    Config_GetBool(FALSE, "log.append"),
                    Config_GetLong(LOG_DEFAULT_KEEPOLD, "log.keepold"),
                    Config_GetLong(LOG_DEFAULT_THROTTLE_THRESHOLD,
                                   "log.throttleThreshold"),
                    Config_GetLong(LOG_DEFAULT_THROTTLE_BPS,
                                   "log.throttleBytesPerSec"),
                    fileName == NULL,
                    Config_GetLong(0, "log.rotateSize"));
}


/*
 *----------------------------------------------------------------------------
 *
 * Log_InitEx --
 *
 *	(Similar to Log_Init) Takes extended set of parameters.
 *	This can (and should be called) very early, to establish
 *	the initial (possibly temporary) log file.
 *	because we are called so early, only local config and preferences
 *	are available.
 *	The use of rotation size is only valid in single process use of
 *	logging. If multiple processes are sharing the same log file, log
 *	rotating will screw up due to races since there is no locking
 *	between them.
 *
 * Results:
 *	TRUE if success, FALSE if failure.
 *
 * Side effects:
 *	Create initial log file
 *	Enable logging
 *
 *----------------------------------------------------------------------------
 */

INLINE Bool
Log_InitEx(const char  *fileName,            // IN: file name, if known
           const char  *config,              // IN: config variable to look up
           const char  *suffix,              // IN: suffix to generate
                                             //     log file name
           const char  *appName,             // IN: App name for log header
           const char  *appVersion,          // IN: App version for log header
           Bool         logging,             // IN: Logging is enabled or not
           Bool         append,              // IN: Append to log file
           unsigned int keepOld,             // IN: Number of old logs to keep
           unsigned int throttleThreshold,   // IN: Threshold for throttling
           unsigned int throttleBytesPerSec, // IN: BPS for throttle
           Bool         switchFile,          // IN: Switch the initial log file
           unsigned int rotateSize)          // IN: Size at which log
                                             //     should be rotated
{
   LogLevel_UserExtensionCreate();
   return LogInitEx(&logState, fileName, config, suffix, appName, appVersion,
                    logging, append, keepOld, throttleThreshold,
                    throttleBytesPerSec, switchFile, rotateSize);
}

Bool
LogInitEx(LogState    *log,                 // IN: ptr to log state
          const char  *fileName,            // IN: file name, if known
          const char  *config,              // IN: config variable to look up
          const char  *suffix,              // IN: suffix to generate
                                            //     log file name
          const char  *appName,             // IN: App name for log header
          const char  *appVersion,          // IN: App version for log header
          Bool         logging,             // IN: Logging is enabled or not
          Bool         append,              // IN: Append to log file
          unsigned int keepOld,             // IN: Number of old logs to keep
          unsigned int throttleThreshold,   // IN: Threshold for throttling
          unsigned int throttleBytesPerSec, // IN: BPS for throttle
          Bool         switchFile,          // IN: Switch the initial log file
          unsigned int rotateSize)          // IN: Size at which log
                                            //     should be rotated
{
   ASSERT(!log->initialized);

   /*
    * Set logging options
    */

   log->appName = Util_SafeStrdup(appName);
   log->appVersion = Util_SafeStrdup(appVersion);
   log->suffix = Util_SafeStrdup(suffix);
   LogUpdateState(log, logging, append, keepOld, rotateSize, FALSE);
   log->throttleInfo.throttleThreshold = throttleThreshold;
   log->throttleInfo.throttleBPS = throttleBytesPerSec;
   log->rotating = FALSE;

   /*
    * Create recursive mutex for default locking mechanism.
    */

   if (!SyncRecMutex_Init(&log->lockMutex, NULL)) {
      LogExit(log);
      return FALSE;
   }

   log->lockMutexInited = TRUE;

   /*
    * Open log file
    *
    * If we're called with NULL NULL, then don't open a log file.
    */

   if (fileName != NULL || config != NULL) {
      if (!LogSwitchFile(log, fileName, config, switchFile)) {
         LogExit(log);
         return FALSE;
      }
   }

   /*
    * Finish
    */
   log->initialized = TRUE;

   return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * Log_Exit --
 *
 *      Clean up.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Logging is turned off; The current log file is closed and all state
 *      is cleaned up.
 *
 *----------------------------------------------------------------------
 */

void
Log_Exit(void)
{
   LogExit(&logState);
   LogLevel_UserExtensionDestroy();
}

void
LogExit(LogState *log)
{
   log->initialized = FALSE;

   /*
    * Prevent file rotation.
    * After LogExit(), we can still write to the log file
    * (log->fd is still valid) but there isn't enough state
    * left to do much else, including rotating log files.
    * So disable that.  See bug 102718.
    * I can say a lot more about the choices available to us
    * here, but I don't want to.  -- edward
    */

   log->rotateSize = 0;
   log->lockFunc = NULL;

   free(log->fileName);
   log->fileName = NULL;

   free(log->dir);
   log->dir = NULL;

   free(log->appName);
   log->appName = NULL;

   free(log->appVersion);
   log->appVersion = NULL;

   free(log->suffix);
   log->suffix = NULL;

   log->lockMutexInited = FALSE;
   SyncRecMutex_Destroy(&log->lockMutex);
}


/*
 *----------------------------------------------------------------------
 *
 * Log_UpdateState --
 *
 *	(Possibly) change logging state which depends on
 *	configuration variables.
 *
 * Results:
 *	none
 *
 * Side effects:
 *      Change logging status.
 *
 *----------------------------------------------------------------------
 */

void
Log_UpdateState(Bool      enable,       // IN
                Bool      append,       // IN
                unsigned  keepOld,      // IN: number of old logs to keep
                size_t    rotateSize,   // IN: file size before rotation
                Bool      fastRotation) // IN: optimize rotation for vmfs
{
   LogUpdateState(&logState, enable, append, keepOld,
		  rotateSize, fastRotation);
}


void
LogUpdateState(LogState *log,          // IN
               Bool      enable,       // IN
               Bool      append,       // IN
               unsigned  keepOld,      // IN: number of old logs to keep
               size_t    rotateSize,   // IN: file size before rotation
               Bool      fastRotation) // IN: optimize rotation for vmfs
{
   LOGWARN(("LOG %s %sable%s\n", VThread_CurName(),
	    enable ? "en" : "dis", append ? " append" : ""));

   LogLock(log, TRUE);

   /*
    * Logging options.
    *
    * The stats scripts don't like lines with prefixes.
    */

   log->enable = enable;
   log->append = append;
   log->keepOld = keepOld;
   log->keep = Config_GetTriState(-1, "log.keep");
   log->timeStamp = Config_GetBool(TRUE, "log.timeStamp");
   log->millisec = Config_GetBool(TRUE, "log.millisec");
   log->threadName = Config_GetBool(TRUE, "log.threadName");
   log->rotateSize = rotateSize;
   log->fastRotation = fastRotation;

   log->throttleInfo.throttleThreshold = 
      Config_GetLong(LOG_DEFAULT_THROTTLE_THRESHOLD, "log.throttleThreshold");
   log->throttleInfo.throttleBPS = Config_GetLong(LOG_DEFAULT_THROTTLE_BPS,
                                                  "log.throttleBytesPerSec");

#ifdef VMX86_LOG
   {
      int i;
      LogLevelExtensionCell *cell;
      LogLevelExtensionCell *extList = logLevelState.extensionsList;
      ASSERT(extList != NULL);
      for (cell = extList; cell != NULL; cell = cell->next) {
	 for (i = 0; i < cell->size; i++) {
            int level =
	       Config_GetLong(0, "loglevel.%s", cell->table[i]);
	    if (!Config_NotSet("loglevel.%s.%s",
			       cell->name, cell->table[i])) {
	       level =
		  Config_GetLong(0, "loglevel.%s.%s",
				 cell->name, cell->table[i]);
	    }
            LogLevel_Set(cell->name, cell->table[i], level);
	 }
      }
   }
#endif /* VMX86_LOG */

   LogLock(log, FALSE);
}


/*
 *----------------------------------------------------------------------
 *
 * Log_Enabled --
 *
 *	Is logging enabled?
 *
 * Results:
 *	TRUE if logging enabled.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Bool
Log_Enabled(void)
{
   return logState.enable;
}


/*
 *----------------------------------------------------------------------
 *
 * Log_SetConfigDir --
 *
 *      Adjust the dir of the log file based on the
 *      directory of the .vmx file.
 *	Put to use the next time we compute the log file name.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Log_SetConfigDir(const char *configDir)	// IN: directory of config file
{
   LogSetDir(&logState, configDir);
}

/*
 *----------------------------------------------------------------------
 *
 * LogSetDir --
 *
 *      Set the directory of the log file.
 *	We use it later when computing the log file name.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
void LogSetDir(LogState *log, const char *dir) {
   LogLock(log, TRUE);

   free(log->dir);

   if (dir == NULL) {
      log->dir = NULL;
   } else {
      log->dir = Util_SafeStrdup(dir);
    }

   LogLock(log, FALSE);
}


/*
 *----------------------------------------------------------------------
 *
 * LogComputeFileName --
 *
 *      Figure out what file name we should be using.
 *
 * Results:
 *      The log file name is allocated and returned.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static char *
LogComputeFileName(LogState *log,       // IN: log state
                   const char *config,	// IN: config variable to look up
		   Bool *isTemp)	// OUT: this is a temporary log file
{
   char *fileName = NULL;

   if (config != NULL) {
      fileName = Config_GetPathName(NULL, config);
   }
   if (fileName != NULL) {
      *isTemp = FALSE;
   } else {
      char *buffer = NULL;
      char *tmpDir;

      if ((tmpDir = log->dir) != NULL) {
	 *isTemp = FALSE;
         buffer = Str_SafeAsprintf(NULL, "%s%s%s%s.log",
                     tmpDir,
                     DIRSEPS PRODUCT_GENERIC_NAME_LOWER,
                     *log->suffix ? "-" : "",
                     log->suffix);
      } else {
         tmpDir = Util_GetSafeTmpDir(TRUE);
	 if (tmpDir == NULL) {
	    Panic("Cannot get temporary directory for log file.\n");
	 }
	 *isTemp = TRUE;
#ifdef __linux__
         buffer = Str_SafeAsprintf(NULL, "%s/%s%s$PID.log",
                     tmpDir,
                     log->suffix,
                     *log->suffix ? "-" : "");
#else
         buffer = Str_SafeAsprintf(NULL, "%s%s%s%s-$USER-$PID.log",
                     tmpDir,
                     DIRSEPS PRODUCT_GENERIC_NAME_LOWER,
                     *log->suffix ? "-" : "",
                     log->suffix);
#endif
	 free(tmpDir);
      }

      fileName = Util_ExpandString(buffer);
      free(buffer);
#ifdef _WIN32
      if (fileName != NULL) {
	 char *tmp = W32Util_RobustGetLongPath(fileName);
	 free(fileName);
	 fileName = tmp;
      }
#endif
      if (fileName == NULL) {
	 Msg_Reset(TRUE);
	 Panic("Cannot get log file name.\n");
      }
   }

   ASSERT(fileName != NULL);
   return fileName;
}


/*
 *----------------------------------------------------------------------------
 *
 * Log_SetLockFunc --
 *
 *     Set the log lock funciton.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *----------------------------------------------------------------------------
 */

void
Log_SetLockFunc(void (*f)(Bool locking))
{
   ASSERT(f == NULL ? logState.lockFunc != NULL : logState.lockFunc == NULL);
   logState.lockFunc = f;
}


/*
 *----------------------------------------------------------------------------
 *
 * LogRotateFile --
 *
 *     Rotate the current log file to the next one in the middle of a run.
 *     Assumes locking is done by caller.
 *     This function is NOT safe if multiple processes are using the same
 *     log file
 *
 * Results:
 *     Returns TRUE if the switch was successful.
 *
 * Side effects:
 *     Potentially creates a new file and updates global logging state.
 *
 *----------------------------------------------------------------------------
 */

Bool
LogRotateFile(LogState *log)
{
   Bool retval = TRUE;
   int newLogFD = -1;

   /* Handle recursive calls to log rotate */
   if (log->rotating) {
      return TRUE;
   }
   log->rotating = TRUE;

   /* you cant rotate out if you dont have a current log */
   ASSERT(log->fileName);

   /* Windows will fail to rename if the fd is open */

   /*
    * On Linux, closing before dup2() will result in a race
    * where some other code opens a file between the close()
    * and dup2() (PR 46028).  Just let the dup2() close the file.
    * Due to the comment above, it appears as though this approach
    * won't work for Windows.
    */

   ASSERT(log->fd >= 0);
#ifdef _WIN32
   close(log->fd);
#endif

   if (log->keepOld > 0) {
      LogBackupOldLogs(log->fileName, log->keepOld, log->fastRotation);
   }

   /*
    * Open new file
    */

   newLogFD = LogOpenNoSymlinkAttack(log->fileName);
#ifdef _WIN32

   /*
    * If we can't create the log file, try to create its path.
    * XXX should ask the user first.
    */

   if (newLogFD < 0) {
      char *path;
      File_GetPathName(log->fileName, &path, NULL);
      File_CreateDirectoryHierarchy(path);
      free(path);
      Msg_Reset(FALSE);
      newLogFD = LogOpenNoSymlinkAttack(log->fileName);
   }
#endif

   if (newLogFD < 0) {
      retval = FALSE;
      goto done;
   }

   log->logSize = 0;
   if (newLogFD != log->fd) {
      dup2(newLogFD, log->fd);
      (void) close(newLogFD);
   }
   LogWriteTagString(log);

done:
   if (!retval) {
      Msg_Append(MSGID(log.switchFailed)
		 "Unable to open log file \"%s\". Check your configuration to "
		 "make sure that the path specified for the log file is valid, "
		 "and that you have write privileges in this directory.\n",
		 log->fileName);

      /* XXX Windows always closes log->fd, but Linux relies on the dup2(). */
#ifndef _WIN32
      close(log->fd);
#endif
      log->fd = -1;
   }
   log->rotating = FALSE;
   return retval;
}


/*
 *----------------------------------------------------------------------
 *
 * Log_SwitchFile --
 *
 *      Switch logging to a new file, or just start the initial file.
 *
 * Results:
 *      returns TRUE if the switch was succesful or if no
 *	switch was needed
 *
 * Side effects:
 *      potentially creates a new file and updates the global
 *	logging state; e.g. log->fd
 *
 *----------------------------------------------------------------------
 */

Bool
Log_SwitchFile(const char *fileName, // IN: file name
	       const char *config,   // IN: config variable for file name
	       Bool copy)            // IN: should copy old to new
{
   return LogSwitchFile(&logState, fileName, config, copy);
}

Bool
LogSwitchFile(LogState *log,        // IN: log state
              const char *fileName, // IN: file name
	      const char *config,   // IN: config variable for file name
	      Bool copy)            // IN: should copy old to new
{
   Bool isTemp = FALSE;
   Bool remove = FALSE;
   int newLogFD = -1;
   Bool retval = TRUE;
   char *computedFileName = NULL;

   LogLock(log, TRUE);

   /*
    * Don't do any things if disabled.
    * This means we'll stay at an old log file (or no file at all)
    * while disabled.  Call us again on reenabling.
    */

   if (!log->enable) {
      goto skip;
   }

   if (fileName == NULL) {
      fileName = computedFileName = LogComputeFileName(log, config, &isTemp);
   }
   ASSERT(fileName != NULL);

   /*
    * Do a quick check for file identity.
    *
    * There is a real check later, because we don't deal too well
    * with switching to the same file.
    */

   if (log->fileName != NULL && strcmp(fileName, log->fileName) == 0) {
      goto done;
   }

   LOGWARN(("LOG %s using %s\n", VThread_CurName(), fileName));

   /*
    * Keep backup copies of the log as required.
    */

   if (log->keepOld > 0 && !isTemp && copy && !log->append) {
      LogBackupOldLogs(fileName, log->keepOld, log->fastRotation);
   }

   /*
    * If we can, try to rename the old file to the new name.
    * If that works, then we're all done.
    */

   if (copy && !log->append && log->fileName != NULL) {
      if (Posix_Rename(log->fileName, fileName) >= 0) {
	 LOGWARN(("LOG renamed %s -> %s\n", log->fileName, fileName));
	 goto done;
      }
      LOGWARN(("LOG rename %s -> %s failed: %s\n",
	       log->fileName, fileName, Msg_ErrString()));
   }

   /*
    * Open new file
    */

   newLogFD = LogOpenNoSymlinkAttack(fileName);

#ifdef _WIN32
   /*
    * If we can't create the log file, try to create its path.
    * XXX should ask the user first.
    */
   if (newLogFD < 0) {
      char *path;
      File_GetPathName(fileName, &path, NULL);
      File_CreateDirectoryHierarchy(path);
      free(path);
      Msg_Reset(FALSE);
      newLogFD = LogOpenNoSymlinkAttack(fileName);
   }
#endif

   if (newLogFD < 0) {
      retval = FALSE;
      goto done;
   }

   /*
    * If no old file, then it's easy.
    *
    * Also, this is the first time we are logging,
    * so write out identification information.
    */

   if (log->fileName == NULL) {
      ASSERT(log->fd < 0);
      log->fd = newLogFD;
      if (copy && !log->append) {
	 ftruncate(log->fd, 0);
      }
      LogWriteTagString(log);
      goto done;
   }

   /*
    * Copy old file to new, if we need to.
    */

   if (copy) {
      struct stat srcStat, destStat;

      /*
       * Before copying, make sure we don't have the same file.
       * If we do, just pretend we've already copied.
       */

      fstat(log->fd, &srcStat);
      fstat(newLogFD, &destStat);
      if (srcStat.st_size == destStat.st_size) {
	 write(log->fd, "\n", 1);
	 fstat(log->fd, &srcStat);
	 fstat(newLogFD, &destStat);
	 ftruncate(log->fd, srcStat.st_size - 1);
	 if (srcStat.st_size == destStat.st_size) {
	    LOGWARN(("LOG switching to same file %s -> %s\n",
		     log->fileName, fileName));
	    close(newLogFD);
	    goto done;
	 }
      }

      /*
       * Need to copy and remove the old one.
       * If the copy fails then revert back to the
       * current file (better than aborting).
       */

      if (!LogCopyFile(log, newLogFD, log->fd, fileName)) {

	 /*
	  * Copy failed, junk the new file and use the old.
	  * Note that copyfile generates the warning message
	  * so it can identify why the copy failed.
	  */

	 close(newLogFD);
	 if (Posix_Unlink(fileName) < 0) {
	    Msg_Append(MSGID(log.switchUnlinkFailed)
		       "Failed to remove log file '%s': %s\n",
		       log->fileName, Msg_ErrString());
	 }
	 retval = FALSE;
	 goto done;
      }
      remove = TRUE;
   }

   /*
    * We dup the new descriptor to the old index
    * 'cuz otherwise we screw up shared state.
    * [guess where we mysteriously die and win a prize]
    */

   ASSERT(log->fd >= 0);
   dup2(newLogFD, log->fd);
   (void) close(newLogFD);

   /*
    * Remove old file after we've closed it.
    */

   if (remove) {
      if (Posix_Unlink(log->fileName) < 0) {
	 Warning("Failed to unlink log file '%s': %s\n",
		 log->fileName, Msg_ErrString());
      }
   }

done:
   if (retval) {
      struct stat statBuf;

      free(log->fileName);
      log->fileName = Util_SafeStrdup(fileName);
      if (log == &logState) {
	 URL_SetAppend(URLAPPEND_LOGFILE, log->fileName);
      }
      log->isTemp = isTemp;
      if (fstat(log->fd, &statBuf) != 0) {
         int error = Err_Errno();

         Log("LOG fstat failed: %s\n", Err_Errno2String(error));
         log->logSize = 0;
      } else {
         log->logSize = statBuf.st_size;
      }
   } else {
      Msg_Append(MSGID(log.switchFailed)
		 "Unable to open log file \"%s\". Check your configuration to "
		 "make sure that the path specified for the log file is valid, "
		 "and that you have write privileges in this directory.\n",
		 fileName);
      if (log->fileName != NULL) {
	 Msg_Append(MSGID(log.switchFailedHasOldFile)
		    "The current log file is still '%s'.\n",
		    log->fileName);
      }
   }
skip:
   LogLock(log, FALSE);
   free(computedFileName);
   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * LogWriteTagString --
 *
 *     Writes a log tag to the log file.
 *
 * Results:
 *     none.
 *
 * Side effects:
 *     none.
 *
 *----------------------------------------------------------------------------
 */

void
LogWriteTagString(LogState *log)
{

   /*
    * Do not change this line, as various tools parse it (Feb, 2006):
    *   - incident tracking
    *   - vmm profiling gunk
    *   - build scripts
    */

   Log("Log for %s pid=%d version=%s build=%s option=%s\n",
       log->appName, getpid(), log->appVersion, BUILD_NUMBER,
       COMPILATION_OPTION);

   /*
    * Report host locale.
    */
   Log("Host codepage=%s encoding=%s\n", CodeSet_GetCurrentCodeSet(),
       Unicode_EncodingEnumToName(Unicode_GetCurrentEncoding()));
}

/*
 *----------------------------------------------------------------------
 *
 * Log_GetFileName --
 *
 * Results:
 *    The name of the log file
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */

const char *
Log_GetFileName(void)
{
   LogState *log = &logState;

   return log->fileName;
}


/*
 *----------------------------------------------------------------------
 *
 * LogBackupByRename --
 *
 *      The oldest indexed file should be removed so that the
 *      consequent rename succeeds.
 *
 *      The last dst is 'fileName' and should not be deleted.
 *
 * Results:
 *      none.
 *
 * Side effects:
 *      Rename backup old log files kept so far.
 *
 *----------------------------------------------------------------------
 */

static void
LogBackupByRename(const char *fileName,      // IN: full path to file
                  const char *baseName,      // IN: log filename w/o extension.
                  const char *ext,           // IN: log extension
                  int n)                     // IN: number of old logs to keep
{
   char *src = NULL;
   char *dst = NULL;
   int i;

   for (i = n; i >= 0; i--) {
      src = i == 0 ?
         (char*) fileName :
         Str_Asprintf(NULL, "%s-%d%s", baseName, i-1, ext);
      if (dst != NULL && Posix_Rename(src, dst) < 0) {
         int error = Err_Errno();

         if (error != ENOENT) { 
            Log("LOG failed to rename %s -> %s failed: %s\n",
                src, dst, Err_Errno2String(error));
         }
      } else if (dst == NULL && Posix_Unlink(src) < 0) {
         Log("LOG failed to remove %s failed: %s\n",
             src, Msg_ErrString());
      }
      ASSERT(dst != fileName);
      free(dst);
      dst = src;
   }
}


/*
 *----------------------------------------------------------------------
 *
 * LogBackupByRenumber --
 *
 *      Log rotation scheme optimized for vmfs:
 *        1) find highest and lowest numbered log files (maxNr)
 *        2) rename vmware.log to 1 + <highest log file number>
 *        3) delete all logs numbered lower than (maxNr - numToKeep)
 *
 *        Wrap around is handled incorrectly.
 *
 * Results:
 *      none.
 *
 * Side effects:
 *     Log files renamed / deleted.
 *
 *----------------------------------------------------------------------
 */

static void
LogBackupByRenumber(const char *filePath,      // IN: full path to file
                    const char *filePathNoExt, // IN: log filename w/o extension.
                    const char *ext,           // IN: log extension
                    int n)                     // IN: number of old logs to keep
{
   char *baseDir = NULL, *fmtString = NULL, *baseName = NULL, *tmp;
   char *fullPathNoExt = NULL;
   uint32 maxNr = 0, minNr = 0xffffffff;
   int i, nrFiles, nFound = 0;
   char **fileList = NULL;

   fullPathNoExt = File_FullPath(filePathNoExt);
   if (fullPathNoExt == NULL) {
      Log("LogBackupByRenumber: failed to get full path for '%s'.\n",
	  filePathNoExt);
      goto cleanup;
   }

   File_GetPathName(fullPathNoExt, &baseDir, &baseName);
   if (baseDir[0] == '\0' || baseName[0] == '\0') {
      Log("LogBackupByRenumber: failed to get base dir for path '%s'.\n",
	  filePathNoExt);
      goto cleanup;
   }

   fmtString = Str_Asprintf(NULL, "%s-%%d%s%%n", baseName, ext);

   nrFiles = File_ListDirectory(baseDir, &fileList);
   if (nrFiles == -1) {
      Log("LogBackupByRenumber: failed to read the directory '%s'.\n", baseDir);
      goto cleanup;
   }

   for (i = 0; i < nrFiles; i++) {
      uint32 curNr;
      int bytesProcessed = 0;

      /* 
       * Make sure the whole file name matched what we expect for
       * a log file
       */
      if (sscanf(fileList[i], fmtString, &curNr, &bytesProcessed) >= 1 && 
          bytesProcessed == strlen(fileList[i])) {
         nFound++;
         maxNr = (curNr > maxNr) ? curNr : maxNr;
         minNr = (curNr < minNr) ? curNr : minNr;
      }
      free(fileList[i]);
   }

   /* rename the existing log file to the next number */
   tmp = Str_Asprintf(NULL, "%s/%s-%d%s", baseDir, baseName, maxNr + 1, ext);
   if (Posix_Rename(filePath, tmp) < 0) {
      int error = Err_Errno();
      if (error != ENOENT) {
         Log("LogBackupByRenumber: failed to rename %s -> %s failed: %s\n",
             filePath, tmp, Err_Errno2String(error));
      }
   }
   free(tmp);

   if (nFound >= n) {
      /* Delete the extra log files.  Slightly wrong if there are gaps. */
      for (i = minNr; i <= minNr + nFound - n; i++) {
         tmp = Str_Asprintf(NULL, "%s/%s-%d%s", baseDir, baseName, i, ext);
         if (Posix_Unlink(tmp) < 0) {
            Log("LogBackupByRenumber: failed to remove %s: %s\n",
                tmp, Msg_ErrString());
         }
         free(tmp);
      }
   }

cleanup:
   free(fileList);
   free(fmtString);
   free(baseDir);
   free(baseName);
   free(fullPathNoExt);
}


/*
 *----------------------------------------------------------------------
 *
 * LogBackupOldLogs --
 *
 *      Backup old logs.  The 'noRename' option is useful for filesystems
 *      where rename is hideously expensive (*cough* vmfs).
 *
 * Results:
 *      none.
 *
 * Side effects:
 *      Log files are renamed / deleted.
 *
 *----------------------------------------------------------------------
 */

void
LogBackupOldLogs(const char *fileName, // IN: original file
                 int n,                // IN: number of backup files
                 Bool noRename)        // IN: don't rename all log files
{
   const char *ext;
   size_t baseLen;
   char *baseName;

   if ((ext = Str_Strrchr(fileName, '.')) == NULL) {
      ext = fileName + strlen(fileName);
   }
   baseLen = ext - fileName;

   /*
    * Backup base of file name.
    *
    * Since the Str_Asprintf(...) doesn't like format of %.*s and crashes
    * in Windows 2000. (Daniel Liu)
    */

   baseName = Util_SafeStrdup(fileName);
   baseName[baseLen] = '\0';

   if (noRename) {
      LogBackupByRenumber(fileName, baseName, ext, n);
   } else {
      LogBackupByRename(fileName, baseName, ext, n);
   }

   free(baseName);
}

void
Log_BackupOldFiles(const char *fileName)
{
   int keepOld = Config_GetLong(LOG_DEFAULT_KEEPOLD, "log.keepold");
   if (keepOld > 0) {
      LogBackupOldLogs(fileName,
                       keepOld,
                       Config_GetBool(vmx86_server, "log.fastRotation"));
   }
}


/*
 *----------------------------------------------------------------------
 *
 * LogOpenNoSymlinkAttack --
 *
 *    Open a log file in a way that can not be symlink attacked (in case
 *    the log file is opened in a world writable directory, like a temporary
 *    directory)
 *
 * Results:
 *    On success: A valid posix file descriptor
 *    On failure: -1. Use the errno mechanism to retrieve the exact error
 *
 * Side effects:
 *    Create a log file, possibly removing an existing one
 *
 *----------------------------------------------------------------------
 */

int
LogOpenNoSymlinkAttack(const char *fileName) // IN
{
   int fd;
#ifndef _WIN32
   struct stat fd_stat;
   struct stat link_stat;
#endif

   /*
    *  Open the file and set for append mode.  We want to share writing
    *  to the file between the VMX and the UI process.
    *  Open for reading too because we may have to copy the file.
    */

   /*
    * XXX As explained in SR 132805, a malicious attacker can still mount a
    *     symlink attack to create empty files on behalf of the user who runs
    *     this code. Now that we use safe temporary directories, we can get
    *     rid of somewhat redundant, complicated function. --hpreg
    */
   fd = Posix_Open(fileName, O_CREAT | O_APPEND | O_RDWR, 0644);
   if (fd < 0) {
      Msg_Append(MSGID(log.openFailed)
                 "Cannot open/create log file '%s': %s\n",
		 fileName,
                 Msg_ErrString());
      return -1;
   }

#ifndef _WIN32

   /*
    *  Stat the file using fstat to guarantee that it is the file
    *  we just opened, then use lstat on the file name to see if
    *  it was a symbolic link.  Compare the device and inode numbers
    *  to guarantee that the two stats were to the same file.
    */

   if (fstat(fd, &fd_stat) < 0) {
      Msg_Append(MSGID(log.openFstatFailed)
                 "Cannot fstat file %s: '%s'\n",
                 fileName,
                 Msg_ErrString());
      close(fd);
      return -1;
   }

   if (Posix_Lstat(fileName, &link_stat) < 0) {
      Msg_Append(MSGID(log.openLstatFailed)
                 "Cannot stat file %s: '%s'\n",
                 fileName,
                 Msg_ErrString());
      close(fd);
      return -1;
   }

   if (S_ISLNK(link_stat.st_mode)) {
      Msg_Append(MSGID(log.openIsLink)
		 "Log file '%s' is a symbolic link.\n", fileName);
      close(fd);
      return -1;
   }

   if ((fd_stat.st_dev != link_stat.st_dev) ||
       (fd_stat.st_ino != link_stat.st_ino)) {
      Msg_Append(MSGID(log.openChanged)
		 "Log file '%s' has changed.\n", fileName);
      close(fd);
      return -1;
   }

#endif

   return fd;
}


/*
 *----------------------------------------------------------------------
 *
 * LogCopyFile --
 *
 * Results:
 *    Copy the file from the source to the destination
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */

Bool
LogCopyFile(LogState *log, int destFd, int srcFd, const char *fileName)
{
   char buf[16*1024];
   size_t n;

   LOGWARN(("LOG copying %s -> %s\n", log->fileName, fileName));

   Log_Flush();

   /*
    * Truncate the destination file.
    */

   if (!log->append && ftruncate(destFd, 0) < 0) {
      Msg_Append(MSGID(log.copyFtruncateFailed)
		 "Cannot truncate log file '%s': %s\n",
		 fileName, Msg_ErrString());
      return FALSE;
   }

   if (lseek(srcFd, 0, SEEK_SET) != 0) {
      Msg_Append(MSGID(log.copyLseekFailed)
		 "Cannot seek to start of file '%s': %s\n",
		 log->fileName, Msg_ErrString());
      return FALSE;
   }

   while ((n = read(srcFd, buf, sizeof (buf))) > 0) {
      if (write(destFd, buf, n) != n) {
	 Msg_Append(MSGID(log.copyWriteFailed)
		    "Write error copying data to '%s': %s\n",
		    fileName, Msg_ErrString());
	 return FALSE;
      }
   }
   return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * Log_DisableThrottling --
 *
 *      Disables log throttling.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Log_DisableThrottling(void)
{
   LogState *log = &logState;

   log->throttleInfo.throttleBPS = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * LogIsThrottled --
 *
 *      Calculates the log data rate.  If this rate is greater
 *      than throttleBPS bytes per second for LOG_THROTTLE_PERIOD seconds,
 *      then logging will be suppressed until the rate drops below
 *      throttleBPS for LOG_UNTHROTTLE_PERIOD seconds.
 *
 *      To allow for for the the burst of data on starting a VM,
 *      throttling isn't enabled until throttleThreshold bytes
 *      have been logged.
 *
 * Results:
 *      TRUE: logging isn't allowed
 *      FALSE: logging is allowed
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static INLINE Bool
LogIsThrottled(LogThrottleInfo *info, const char **msg)
{
   VmTimeType sampleInterval, curTime;
   Bool throttle = info->throttled;

   if (info->throttleBPS == 0) {
      /* throttling isn't enabled */
      return FALSE;
   }

   info->bytesLogged += strlen(*msg);

   if ((info->bytesLogged < info->throttleThreshold)) {
      return FALSE;
   }

   sampleInterval = info->throttled ?
		    LOG_UNTHROTTLE_PERIOD : LOG_THROTTLE_PERIOD;
   Hostinfo_GetTimeOfDay(&curTime);

   if (curTime < info->lastSampleTime) {
      /*
       * Time went backwards / user changed their clock.  Reset
       * our lastSampleTime, and retain the current throtting
       * state (any calculations we make now would be bogus).
       */
      info->lastSampleTime = curTime;
      info->lastBytesSample = info->bytesLogged;
      return throttle;
   }

   if (curTime - info->lastSampleTime > sampleInterval) {
      uint32 bps = (info->bytesLogged - info->lastBytesSample) /
                   ((curTime - info->lastSampleTime) / (1000 * 1000));

      if (bps > info->throttleBPS) {
         info->throttled = TRUE;
         if (info == &guestLogThrottleInfo) {
            *msg = "\n<<< Guest Log Throttled >>>\n";
         } else {
            *msg = "\n<<< Log Throttled >>>\n";
         }
      } else if (info->throttled) {
         info->throttled = throttle = FALSE;
      }
      info->lastSampleTime = curTime;
      info->lastBytesSample = info->bytesLogged;
   }

   return throttle;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Log_GetQuietWarning --
 *
 *      Get the quiet-warning flag
 *
 * Results:
 *      logState.quietWarning
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Log_GetQuietWarning()
{
   return logState.quietWarning;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Log_SetQuietWarning --
 *
 *      Set the quiet-warning flag
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      No other.
 *
 *-----------------------------------------------------------------------------
 */

void
Log_SetQuietWarning(Bool quiet)
{
   logState.quietWarning = quiet;
}

static void
LogWriteLogFile(LogState *log, const char *msg, Bool isGuestLog)
{
   char tag[64];
   size_t tagLen = 0;

   LogLock(log, TRUE);

   if (! log->enable) {
      goto out;
   }
   if (log->fd < 0) {
      goto out;
   }

   /*
    * Call both throttling functions if it's a guest log message, since we
    * want both states to be updated.
    */
   if ((isGuestLog && LogIsThrottled(&guestLogThrottleInfo, &msg)) |
       LogIsThrottled(&log->throttleInfo, &msg)) {
      goto out;
   }

   /*
    * Preformat time stamp and/or thread name
    */

   if (log->timeStamp) {
      tagLen = LogMakeTimeString(log->millisec, tag, ARRAYSIZE(tag) - 5);
   }
   if (log->threadName) {
      const char *name = VThread_CurName();
      size_t nameLen = strlen(name);
      size_t maxNameLen = sizeof tag / sizeof *tag - tagLen - 2;

      if (nameLen > maxNameLen) {
	 nameLen = maxNameLen;
      }
      memcpy(tag + tagLen, name, nameLen);
      tagLen += nameLen;
      tag[tagLen++] = '|';
      tag[tagLen++] = ' ';
   }

   for (;;) {
      size_t len;
      const char *p;

      if ((p = strchr(msg, '\n')) != NULL) {
	 len = p - msg + 1;
      } else {
	 len = strlen(msg);
      }
      if (len == 0) {
	 break;
      }

#if defined(_WIN32)
      if (!log->notBOL && tagLen > 0) {
	 write(log->fd, tag, tagLen);
         log->logSize += tagLen;
      }
      write(log->fd, msg, len);
      log->logSize += len;

#else
      if (log->notBOL || tagLen <= 0) {
	 write(log->fd, msg, len);
         log->logSize += len;
      } else {
	 struct iovec iov[2];
	 iov[0].iov_base = tag;
	 iov[0].iov_len = tagLen;
	 iov[1].iov_base = (void *) msg;
	 iov[1].iov_len = len;
	 writev(log->fd, iov, 2);
         log->logSize += tagLen + len;
      }
#endif

      FLUSH_LOGFD(log->fd);

      log->notBOL = msg[len - 1] != '\n';
      msg += len;
   }

   if (log->rotateSize && log->logSize > log->rotateSize) {
      LogRotateFile(log);
   }

out:
   LogLock(log, FALSE);
}


/*
 *----------------------------------------------------------------------
 *
 * Log_WriteLogFile --
 *
 *      Write a message to the log file.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Log file written.
 *
 *----------------------------------------------------------------------
 */

INLINE void 
Log_WriteLogFile(const char *msg)
{
   LogWriteLogFile(&logState, msg, FALSE);
}

/*
 *----------------------------------------------------------------------
 *
 * LogMakeTimeString --
 *
 *      Construct a printable representation the current time.
 *
 * Results:
 *      Length of time string.
 *	Time string in supplied buffer.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

size_t
LogMakeTimeString(Bool millisec, char *buf, size_t max)
{
   time_t sec;
   int msec;
   size_t len;

   {
   #ifdef _WIN32
      struct _timeb tb;
      _ftime(&tb);
      sec = tb.time;
      msec = tb.millitm;
   #else
      struct timeval tv;
      gettimeofday(&tv, NULL);
      sec = tv.tv_sec;
      msec = tv.tv_usec / 1000;
   #endif
   }

   len = strftime(buf, max, "%b %d %H:%M:%S: ", localtime(&sec));
   if (millisec && len >= 2) {
      len -= 2;
      buf += len;
      max -= len;
      len += Str_Sprintf(buf, max, ".%03d: ", msec);
   }
   return len;
}


/*
 *----------------------------------------------------------------------
 *
 * Log_Flush --
 *
 *      Flush the in-memory log buffer, if any.
 *	This is useful before we die or fork.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Flush.
 *
 *----------------------------------------------------------------------
 */

void
Log_Flush(void)
{
   /*
    * Nothing, intended for logging through stdio.
    */
}


/*
 *----------------------------------------------------------------------
 *
 * Log_SetAlwaysKeep --
 *
 *      Set alwaysKeep flag
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Changes the alwaysKeep flag.
 *
 *----------------------------------------------------------------------
 */

void
Log_SetAlwaysKeep(Bool alwaysKeep)
{
   logState.alwaysKeep = alwaysKeep;
}


/*
 *----------------------------------------------------------------------
 *
 * Log_RemoveFile --
 *
 *      Remove log file if keep is false.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Log file may be removed.
 *
 *----------------------------------------------------------------------
 */

Bool
Log_RemoveFile(Bool alwaysRemove)
{
   LogState *log = &logState;
   Bool retval = TRUE;

   LogLock(log, TRUE);

   if (!log->initialized) {
      Log("LOG trying to remove log file when not initialized.\n");
      goto out;
   }

   if (!alwaysRemove) {
      if (log->alwaysKeep) {
	 goto out;
      }
      if (log->keep > 0) {
	 goto out;
      }
      if (log->keep < 0 && !log->isTemp) {
	 goto out;
      }
   }

   ASSERT(log->fileName != NULL);
   Log("LOG removing %s.\n", log->fileName);
   if (log->fd >= 0) {
      close(log->fd);
      log->fd = -1;
   }
   if (Posix_Unlink(log->fileName) < 0) {
#if 0 // XXX can't log any more
      Warning("LOG failed to remove %s: %s.\n",
	      log->fileName, Msg_ErrString());
#endif
      retval = FALSE;
   }
   free(log->fileName);
   log->fileName = NULL;

out:
   LogLock(log, FALSE);
   return retval;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Warning --
 *
 *      Print to stderr and log (if log is available).
 *
 * Results:
 *      void
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
Warning(const char *fmt, ...)
{
   va_list args;
   char buf[4096];

   va_start(args, fmt);

   if (logState.basicFuncWarning) {
      logState.basicFuncWarning(fmt, args);
      va_end(args);
      return;
   }

   Str_Vsnprintf(buf, sizeof (buf), fmt, args);
   va_end(args);

   Log_WriteLogFile(buf);
   Log_Flush();

   if (!logState.quietWarning) {
      fputs(buf, stderr);
#ifdef _WIN32
      Win32U_OutputDebugString(buf);
#endif
   }
}


void
LogWork(LogState *log, Bool isGuestLog, const char *fmt, va_list args)
{
   char buf[4096];

   if (log->basicFuncLog) {
      log->basicFuncLog(fmt, args);
      return;
   }

   if (!log->enable) {
      return;
   }

   Str_Vsnprintf(buf, sizeof (buf), fmt, args);

   LogWriteLogFile(log, buf, isGuestLog);
   Log_Flush();
}


/*
 *-----------------------------------------------------------------------------
 *
 * Log --
 *
 *      Write to the logfile the printf style string and arguments.
 *
 * Results:
 *      void
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
Log(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   LogWork(&logState, FALSE, fmt, args);
   va_end(args);
}

void
GuestLog_Log(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   LogWork(&logState, TRUE, fmt, args);
   va_end(args);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Log_RegisterBasicFunctions --
 *
 *      Registers alternate functions to be used when Log() and/or Warning() is
 *      called.
 *
 * Results:
 *      void
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
Log_RegisterBasicFunctions(LogBasicFunc *funcLog,      // IN
                           LogBasicFunc *funcWarning)  // IN
{
   logState.basicFuncLog = funcLog;
   logState.basicFuncWarning = funcWarning;
}
