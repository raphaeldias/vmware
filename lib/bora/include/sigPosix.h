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
 * sigPosix.h --
 *
 *      Signal handling for POSIX
 */

#ifndef _SIGPOSIX_H_
#define _SIGPOSIX_H_

#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE
#include "includeCheck.h"

#include "vthread.h"

#include <signal.h>
#include <setjmp.h>


/*
 * Types
 */

/*
 * When dispatching a signal for whom there is a registrant (someone who called
 * Sig_Callback), a callback function of this type will be called with a 
 * pointer to the siginfo_t if the signal handler was established using 
 * SA_SIGINFO (and NULL otherwise). At the moment this will occur for all 
 * signals on non-Linux platforms, and for all signals but SIGPROF on Linux.
 *
 * Arguably, the SIGPROF handler on Linux may want the struct sigcontext
 * that the older Linux-style handler can provide, but in practice none of
 * this function's registrants would care, and adding it would dirty this 
 * interface unnecessarily.
 */
typedef void (*SigCallbackFunc)(int s, siginfo_t *info, void *clientData);

enum {
   SIG_DEFAULT = 0,
   SIG_NOHANDLER,
   SIG_SAFE,
   SIG_MONACTION,
   SIG_IMMEDIATE,
   SIG_ALLOW,
   SIG_NUM_TYPES,
   SIG_TYPE = 0x0f,
   SIG_CHAIN = 0x10,
};

typedef sigset_t SigMask;

typedef struct SigCrashCatcherState {
   jmp_buf jmpBuf;
   Bool active;
} SigCrashCatcherState;

extern SigCrashCatcherState SigCrashCatcher[];


/*
 * Functions
 */

EXTERN void Sig_CoreDump(void);
EXTERN Bool Sig_Init(void);
EXTERN void Sig_InitThread(void);

EXTERN void Sig_Callback(int s, int type,
		         SigCallbackFunc func, void *clientData);
EXTERN void Sig_SetCoreDumpFunc(void (*func)(void *clientData),
                                void *clientData);
EXTERN void Sig_NoMainLoop(Bool on);
EXTERN void Sig_Continue(int s);
EXTERN void Sig_ForceSig(int s);
EXTERN void Sig_NullHandler(int s, siginfo_t *info, void *clientData);

EXTERN void Sig_BlockAll(SigMask *oldMask);
EXTERN void Sig_Block(int s, SigMask *oldMask);
EXTERN void Sig_Unblock(int s, SigMask *oldMask);
EXTERN void Sig_Restore(SigMask *mask);
EXTERN void Sig_RaiseSig(int s);

#if !defined VMX86_SERVER || !defined VMX86_VMX // not ESX userworld
extern void Sig_CoreDumpRegion(Bool add, Bool unmap, void *addr, size_t size);
#else
static INLINE void Sig_CoreDumpRegion(Bool add, Bool unmap,
                                      void *addr, size_t size) {
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * Sig_CrashCatcherBegin --
 *
 *      Set up a setjmp/longjmp region that catches crashes in the
 *      form of SIGSEGV, SIGBUS, SIGILL, and SIGABRT signals.
 *
 *      Usage:
 *        if (LIKELY(Sig_CrashCatcherBegin() == 0)) {
 *           // protected code
 *           Sig_CrashCatcherEnd();
 *        } else {
 *           // error handler code
 *        }
 *
 *      Since the setjmp() call itself must be guaranteed to run in a
 *      stack frame that will still exist when the protected code is
 *      executed, this function is actually implemented as a
 *      combination of a macro and an inline function.
 *
 * Results:
 *      0 on first call.
 *      The signal number when reentered via a signal.
 *
 * Side effects:
 *      Sets thread-local state which includes a jmp_buf which
 *      instructs the signal handler to jump back here after a
 *      crash has occurred.
 *
 *      This function should be very fast: it makes one standard
 *      library call, and no system calls. For especially performance
 *      critical call sites, you can even avoid calling
 *      VThread_CurID() and pass a constant thread ID.
 *
 *----------------------------------------------------------------------
 */

#define Sig_CrashCatcherBegin()  Sig_CrashCatcherBeginWithTID(VThread_CurID())


/*
 *----------------------------------------------------------------------
 *
 * Sig_CrashCatcherBeginWithTID --
 *
 *      Just like Sig_CrashCatcherBegin(), but the caller is responsible
 *      for providing the VThreadID of the current thread.
 *
 *      This can be used when the calling code is guaranteed to run on
 *      a particular thread with a constant ID. It will be slightly
 *      faster on release builds.
 *
 * Results:
 *      0 on first call.
 *      The signal number when reentered via a signal.
 *
 * Side effects:
 *      See Sig_CrashCatcherBegin().
 *
 *----------------------------------------------------------------------
 */

static inline int
SigCrashCatcherBeginWork(VThreadID tid, int caughtSignal)
{
   SigCrashCatcherState *state = &SigCrashCatcher[tid];

   ASSERT(tid == VThread_CurID());

   if (caughtSignal == 0) {
      ASSERT(!state->active);
      state->active = TRUE;
   } else {
      ASSERT(state->active);
      state->active = FALSE;
   }

   return caughtSignal;
}

#define Sig_CrashCatcherBeginWithTID(tid)                                     \
   ({                                                                         \
      VThreadID _tid = (tid);                                                 \
      ASSERT(_tid == VThread_CurID());                                        \
      SigCrashCatcherBeginWork(_tid, _setjmp(SigCrashCatcher[_tid].jmpBuf));  \
   })


/*
 *----------------------------------------------------------------------
 *
 * Sig_CrashCatcherEnd --
 *
 *      End a crash catcher region which was entered with Sig_CrashCatcherBegin.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sets thread-local state.
 *
 *----------------------------------------------------------------------
 */

#define Sig_CrashCatcherEnd()  Sig_CrashCatcherEndWithTID(VThread_CurID())


/*
 *----------------------------------------------------------------------
 *
 * Sig_CrashCatcherEndWithTID --
 *
 *      End a crash catcher region which was entered with
 *      Sig_CrashCatcherBeginWithTID.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sets thread-local state.
 *
 *----------------------------------------------------------------------
 */

static inline void
Sig_CrashCatcherEndWithTID(VThreadID tid)
{
   ASSERT(tid == VThread_CurID());
   ASSERT(SigCrashCatcher[tid].active == TRUE);
   SigCrashCatcher[tid].active = FALSE;
}


#endif // ifndef _SIGPOSIX_H_
