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
 * procHelper.cc --
 *
 *    Child process helper.
 */


#include <sys/types.h>
#include <sys/socket.h> /* For socketpair */
#include <sys/wait.h>   /* For waitpid */
#include <signal.h>     /* For kill */


#include "procHelper.hh"

extern "C" {
#include "vm_assert.h"
#include "err.h"
#include "log.h"
#include "poll.h"
#include "posix.h"
#include "util.h" /* For DIRSEPS */
}


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelper::ProcHelper --
 *
 *      Constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

ProcHelper::ProcHelper()
   : mPid(-1),
     mErrFd(-1)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelper::~ProcHelper --
 *
 *      Destructor.  Calls Kill.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

ProcHelper::~ProcHelper()
{
   Kill();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelper::Connect --
 *
 *      Fork and exec child process.  procName is the friendly name of the
 *      process, to be used in Log messages.  procPath is the path to the
 *      binary, passed to Posix_Execvp.
 *
 *      The stdIn string is written all at once to the process's stdin.
 *
 *      skipFd1 & skipFd2 are left open in the child process.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Creates two pipes for stdio, and forks a child.
 *
 *-----------------------------------------------------------------------------
 */

void
ProcHelper::Start(Util::string procName,          // IN
                  Util::string procPath,          // IN
                  std::vector<Util::string> args, // IN
                  Util::string stdIn,             // IN
                  int skipFd1,                    // IN
                  int skipFd2)                    // IN
{
   ASSERT(mPid == -1);
   ASSERT(!procPath.empty());
   ASSERT(!procName.empty());

   char const **argList = (char const**)
      Util_SafeMalloc(sizeof(char*) * (args.size() + 2));
   int argIdx = 0;

   argList[argIdx++] = procName.c_str();
   for (std::vector<Util::string>::iterator i = args.begin();
        i != args.end(); i++) {
      argList[argIdx++] = (*i).c_str();
   }
   argList[argIdx++] = NULL;

   int inFds[2];
   int errFds[2];

   if (pipe(inFds) < 0 || pipe(errFds) < 0) {
      Warning("Pipe call failed: %s\n", Err_ErrString());
      NOT_IMPLEMENTED();
   }

   pid_t pid = fork();
   switch (pid) {
   case -1:
      Warning("Fork call failed: %s\n", Err_ErrString());
      NOT_IMPLEMENTED();

      close(inFds[0]);
      close(inFds[1]);
      close(errFds[0]);
      close(errFds[1]);
      break;

   case 0: // child
      // Handle stdout as if it's stderr (that is, log it).
      ResetProcessState(inFds[0], errFds[1], errFds[1], skipFd1, skipFd2);

      // Search in $PATH
      Posix_Execvp(procPath.c_str(), (char* const*) argList);

      // An error occurred
      Log("Failed to spawn %s: %s\n", procName.c_str(), Err_ErrString());
      _exit(1);
      NOT_REACHED();

   default: // parent
      close(inFds[0]);  // don't want to read
      close(errFds[1]); // don't want to write

      if (!stdIn.empty()) {
         // Write and sync to child stdin
         write(inFds[1], stdIn.c_str(), stdIn.size());
         fsync(inFds[1]);
         close(inFds[1]);
      }

      mProcName = procName;
      mPid = pid;
      mErrFd = errFds[0];

      Poll_CB_Device(&ProcHelper::OnErr, this, mErrFd, false);
      break;
   }

   free(argList);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelper::Kill --
 *
 *      Kill the child process, if running.  If mPid is set, send it a SIGTERM.
 *      If mErrFd is set, close it and remove the poll callback.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Sends SIGTERM to child.  Emits onExit if the child has exited.
 *
 *-----------------------------------------------------------------------------
 */

void
ProcHelper::Kill()
{
   if (mErrFd > -1) {
      Poll_CB_DeviceRemove(&ProcHelper::OnErr, this, false);
      close(mErrFd);
      mErrFd = -1;
   }

   if (mPid < 0) {
      return;
   }

   if (kill(mPid, SIGTERM) && errno != ESRCH) {
      Log("Unable to kill %s(%d): %s\n", mProcName.c_str(), mPid,
          Err_ErrString());
   }
   int status;
   pid_t rv;
   do {
      rv = waitpid(mPid, &status, 0);
   } while (rv < 0 && EINTR == errno);

   if (rv < 0) {
      Log("Unable to waitpid on %s(%d): %s\n", mProcName.c_str(), mPid,
          Err_ErrString());
   } else if (rv == mPid) {
      if (WIFEXITED(status)) {
         if (WEXITSTATUS(status)) {
            Warning("%s(%d) exited with status: %d\n",
                    mProcName.c_str(), mPid, WEXITSTATUS(status));
         } else {
            Warning("%s(%d) exited normally.\n", mProcName.c_str(), mPid);
         }
      } else {
         Warning("%s(%d) exited due to signal %d.\n",
                 mProcName.c_str(), mPid, WTERMSIG(status));
      }
   } else {
      // It wasn't a normal exit, but what value should we use?
      status = 0xff00;
   }

   mPid = -1;
   onExit(status);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelper::ResetProcessState --
 *
 *      Called in a forked child to reset all signal handlers, remap std
 *      in/out/err, and close all fds.  An fd to remap for each std in/out/err
 *      file descriptor is taken as an argument, with -1 meaning to inherit
 *      from the parent.  The skip1 and skip2 fds are left open.
 *
 *      Taken from Hostinfo_ResetProcessState, to avoid added library
 *      dependencies.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
ProcHelper::ResetProcessState(int stdIn,   // IN
                              int stdOut,  // IN
                              int stdErr,  // IN
                              int skipFd1, // IN
                              int skipFd2) // IN
   const
{
   struct sigaction sa;
   int sig;
   for (sig = 1; sig <= NSIG; sig++) {
      sa.sa_handler = SIG_DFL;
      sigfillset(&sa.sa_mask);
      sa.sa_flags = SA_RESTART;
      sigaction(sig, &sa, NULL);
   }

   if (stdIn > -1 && dup2(stdIn, STDIN_FILENO) < 0) {
      close(STDIN_FILENO);
   }
   if (stdOut > -1 && dup2(stdOut, STDOUT_FILENO) < 0) {
      close(STDOUT_FILENO);
   }
   if (stdErr > -1 && dup2(stdErr, STDERR_FILENO) < 0) {
      close(STDERR_FILENO);
   }

   int fd;
   for (fd = (int) sysconf(_SC_OPEN_MAX) - 1; fd > STDERR_FILENO; fd--) {
      if (fd != skipFd1 && fd != skipFd2) {
         close(fd);
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelper::OnErr --
 *
 *      Stderr poll callback for the child process. Reads and logs the output,
 *      and passes the line to the onErr signal.  Keeps a line buffer for
 *      appending partial reads so that we only log/emit full lines.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Calls Poll_CB_Device to schedule more IO.
 *
 *-----------------------------------------------------------------------------
 */

void
ProcHelper::OnErr(void *data) // IN: this
{
   ProcHelper *that = reinterpret_cast<ProcHelper*>(data);
   ASSERT(that);

   if (that->mErrFd == -1) {
      return;
   }

   char buf[1024];
   int cnt = read(that->mErrFd, buf, sizeof(buf) - 1);
   if (cnt <= 0) {
      Warning("%s(%d) died.\n", that->mProcName.c_str(), that->mPid);
      that->Kill();
      return;
   }

   buf[cnt] = 0;
   char *line = buf;

   /*
    * Look for full/unterminated lines in the read buffer. For full lines,
    * replace \n with \0 and log/emit the line minus the terminator.  For a
    * trailing unterminated line, store it as the prefix for future reads.
    */
   while (*line) {
      char *newline = strchr(line, '\n');
      if (!newline) {
         that->mErrPartialLine += line;
         break;
      }

      *newline = 0; // Terminate line
      Util::string fullLine = that->mErrPartialLine + line;
      that->mErrPartialLine = "";
      line = &newline[1];

      Warning("%s(%d): %s\n", that->mProcName.c_str(), that->mPid,
              fullLine.c_str());

      that->onErr(fullLine);
   }

   Poll_CB_Device(&ProcHelper::OnErr, that, that->mErrFd, false);
}


} // namespace cdk
