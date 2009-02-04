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
 * sigPosix.c --
 *
 *      Signal handling
 */

#if __linux__         // We need the REG_foo offsets in the gregset_t;
#  define _GNU_SOURCE // _GNU_SOURCE maps to __USE_GNU

/* And, the REG_foo definitions conflict with our own in x86.h */
#  if __x86_64__
#    define REG_RAX GNU_REG_RAX
#    define REG_RBX GNU_REG_RBX
#    define REG_RCX GNU_REG_RCX
#    define REG_RDX GNU_REG_RDX
#    define REG_RSI GNU_REG_RSI
#    define REG_RDI GNU_REG_RDI
#    define REG_RSP GNU_REG_RSP
#    define REG_RBP GNU_REG_RBP
#    define REG_RIP GNU_REG_RIP
#    define REG_R8  GNU_REG_R8
#    define REG_R9  GNU_REG_R9
#    define REG_R10 GNU_REG_R10
#    define REG_R11 GNU_REG_R11
#    define REG_R12 GNU_REG_R12
#    define REG_R13 GNU_REG_R13
#    define REG_R14 GNU_REG_R14
#    define REG_R15 GNU_REG_R15
#  else
#    if GLIBC_VERSION_22
#       define REG_EAX GNU_REG_EAX
#       define REG_EBX GNU_REG_EBX
#       define REG_ECX GNU_REG_ECX
#       define REG_EDX GNU_REG_EDX
#       define REG_ESI GNU_REG_ESI
#       define REG_EDI GNU_REG_EDI
#       define REG_ESP GNU_REG_ESP
#       define REG_EBP GNU_REG_EBP
#       define REG_EIP GNU_REG_EIP
#    else
#       define EAX GNU_REG_EAX
#       define EBX GNU_REG_EBX
#       define ECX GNU_REG_ECX
#       define EDX GNU_REG_EDX
#       define ESI GNU_REG_ESI
#       define EDI GNU_REG_EDI
#       define ESP GNU_REG_ESP
#       define EBP GNU_REG_EBP
#       define EIP GNU_REG_EIP
#    endif
#  endif
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <fcntl.h>
#if !defined(__APPLE__) && !defined(__FreeBSD__)
#   include <sys/syscall.h>
#else
#   include <pthread.h>
#   include <sys/sysctl.h>
#endif
#include <limits.h>
#include <sys/wait.h>
#include <sys/ucontext.h>
#if !defined VMX86_SERVER || !defined VMX86_VMX // { not ESX userworld
#   include <sys/utsname.h>
#   include <glob.h>
#endif
#include <sys/mman.h>
#if defined(__APPLE__) || defined(__FreeBSD__)
#define MAP_ANONYMOUS MAP_ANON
#endif

#if __linux__
#  if __x86_64__
#    undef REG_RAX
#    undef REG_RBX
#    undef REG_RCX
#    undef REG_RDX
#    undef REG_RSI
#    undef REG_RDI
#    undef REG_RSP
#    undef REG_RBP
#    undef REG_RIP
#    undef REG_R8
#    undef REG_R9
#    undef REG_R10
#    undef REG_R11
#    undef REG_R12
#    undef REG_R13
#    undef REG_R14
#    undef REG_R15
#  else
#    if GLIBC_VERSION_22
#       undef REG_EAX
#       undef REG_EBX
#       undef REG_ECX
#       undef REG_EDX
#       undef REG_ESI
#       undef REG_EDI
#       undef REG_ESP
#       undef REG_EBP
#       undef REG_EIP
#    else
#       undef EAX
#       undef EBX
#       undef ECX
#       undef EDX
#       undef ESI
#       undef EDI
#       undef ESP
#       undef EBP
#       undef EIP
#    endif
#  endif
#endif

#ifdef VMX86_VMX
#include "vmx.h"
#include "pollVMX.h"
#include "vmmon.h"
#include "vmxUStats.h"
#else
#include "vmware.h"
#include "poll.h"
#include "su.h"
#endif
#include "err.h"
#include "sig.h"
#include "vthread.h"
#include "util.h"
#include "log.h"
#include "panic.h"
#include "unicode.h"
#include "hashTable.h"
#include "config.h"
#include "sigPosix.h"
#include "posix.h"

#define LOGLEVEL_MODULE none
#include "loglevel_user.h"

#if defined(__APPLE__)
#define SC_EAX(uc) ((unsigned long) (uc)->uc_mcontext->ss.eax)
#define SC_EBX(uc) ((unsigned long) (uc)->uc_mcontext->ss.ebx)
#define SC_ECX(uc) ((unsigned long) (uc)->uc_mcontext->ss.ecx)
#define SC_EDX(uc) ((unsigned long) (uc)->uc_mcontext->ss.edx)
#define SC_EDI(uc) ((unsigned long) (uc)->uc_mcontext->ss.edi)
#define SC_ESI(uc) ((unsigned long) (uc)->uc_mcontext->ss.esi)
#define SC_EBP(uc) ((unsigned long) (uc)->uc_mcontext->ss.ebp)
#define SC_ESP(uc) ((unsigned long) (uc)->uc_mcontext->ss.esp)
#define SC_EIP(uc) ((unsigned long) (uc)->uc_mcontext->ss.eip)
#elif defined(__FreeBSD__)
#ifdef __x86_64__
#define SC_EAX(uc) ((unsigned long) (uc)->uc_mcontext.mc_rax)
#define SC_EBX(uc) ((unsigned long) (uc)->uc_mcontext.mc_rbx)
#define SC_ECX(uc) ((unsigned long) (uc)->uc_mcontext.mc_rcx)
#define SC_EDX(uc) ((unsigned long) (uc)->uc_mcontext.mc_rdx)
#define SC_EDI(uc) ((unsigned long) (uc)->uc_mcontext.mc_rdi)
#define SC_ESI(uc) ((unsigned long) (uc)->uc_mcontext.mc_rsi)
#define SC_EBP(uc) ((unsigned long) (uc)->uc_mcontext.mc_rbp)
#define SC_ESP(uc) ((unsigned long) (uc)->uc_mcontext.mc_rsp)
#define SC_EIP(uc) ((unsigned long) (uc)->uc_mcontext.mc_rip)
#define SC_R8(uc)  ((unsigned long) (uc)->uc_mcontext.mc_r8)
#define SC_R9(uc)  ((unsigned long) (uc)->uc_mcontext.mc_r9)
#define SC_R10(uc) ((unsigned long) (uc)->uc_mcontext.mc_r10)
#define SC_R11(uc) ((unsigned long) (uc)->uc_mcontext.mc_r11)
#define SC_R12(uc) ((unsigned long) (uc)->uc_mcontext.mc_r12)
#define SC_R13(uc) ((unsigned long) (uc)->uc_mcontext.mc_r13)
#define SC_R14(uc) ((unsigned long) (uc)->uc_mcontext.mc_r14)
#define SC_R15(uc) ((unsigned long) (uc)->uc_mcontext.mc_r15)
#else
#define SC_EAX(uc) ((unsigned long) (uc)->uc_mcontext.mc_eax)
#define SC_EBX(uc) ((unsigned long) (uc)->uc_mcontext.mc_ebx)
#define SC_ECX(uc) ((unsigned long) (uc)->uc_mcontext.mc_ecx)
#define SC_EDX(uc) ((unsigned long) (uc)->uc_mcontext.mc_edx)
#define SC_EDI(uc) ((unsigned long) (uc)->uc_mcontext.mc_edi)
#define SC_ESI(uc) ((unsigned long) (uc)->uc_mcontext.mc_esi)
#define SC_EBP(uc) ((unsigned long) (uc)->uc_mcontext.mc_ebp)
#define SC_ESP(uc) ((unsigned long) (uc)->uc_mcontext.mc_esp)
#define SC_EIP(uc) ((unsigned long) (uc)->uc_mcontext.mc_eip)
#endif
#else
#ifdef VM_X86_64
#define SC_EAX(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_RAX])
#define SC_EBX(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_RBX])
#define SC_ECX(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_RCX])
#define SC_EDX(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_RDX])
#define SC_EDI(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_RDI])
#define SC_ESI(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_RSI])
#define SC_EBP(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_RBP])
#define SC_ESP(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_RSP])
#define SC_EIP(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_RIP])
#define SC_R8(uc)  ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_R8])
#define SC_R9(uc)  ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_R9])
#define SC_R10(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_R10])
#define SC_R11(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_R11])
#define SC_R12(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_R12])
#define SC_R13(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_R13])
#define SC_R14(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_R14])
#define SC_R15(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_R15])
#else
#define SC_EAX(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_EAX])
#define SC_EBX(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_EBX])
#define SC_ECX(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_ECX])
#define SC_EDX(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_EDX])
#define SC_EDI(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_EDI])
#define SC_ESI(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_ESI])
#define SC_EBP(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_EBP])
#define SC_ESP(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_ESP])
#define SC_EIP(uc) ((unsigned long) (uc)->uc_mcontext.gregs[GNU_REG_EIP])
#endif
#endif

/*
 * Local data
 *
 * There seems to be no standard typedef for catchers,
 * so we declare our own, in an unconventional way.  -- edward
 */

// (1) Unix sa_handler prototype:
typedef void SigOldUnix(int s);
#if __linux__
// (2) Linux sa_handler extended with struct sigcontext:
typedef void SigOldLinux(int s, struct sigcontext context);
#endif
// (3) Posix sa_sigaction prototype:
typedef void SigPosix(int s, siginfo_t *info, void *ucontext);

#define SIG_IGN_POSIX ((SigPosix *) SIG_IGN)
#define SIG_DFL_POSIX ((SigPosix *) SIG_DFL)

typedef struct SigCallback {
   int type;
   Bool handling;
   Bool chain;
   Bool oldCatcherIsSigPosix;
   SigCallbackFunc func;
   void *clientData;
   SigPosix *oldCatcher;
} SigCallback;

typedef struct Sig {
   Bool initialized;
   SigCallback callbacks[NSIG];
   void (*coreDumpFunc)(void *clientData);
   void *coreDumpClientData;
   sigset_t allSignalMask;
   int noMainLoop;
   ucontext_t ucontext;
   int loopCount;
   int pipeFds[2];
#if !defined VMX86_SERVER || !defined VMX86_VMX // not ESX userworld
   HashTable *coreDumpRegions;
#endif
} Sig;

static Sig sig;

/* This is not static, since inline functions in sigPosix.h need access to it. */
SigCrashCatcherState SigCrashCatcher[VTHREAD_MAX_THREADS];


/*
 * Local functions
 */

#if __linux__ && defined VMX86_STATS && defined VMX86_PROFILE
static SigOldLinux SigCatcherLinux;
#endif
static SigPosix SigCatcherPosix;
static void SigCatcherCommon(int s, struct sigcontext *context,
                             siginfo_t *info, ucontext_t *ucontext);
static void SigCallChain(int s, struct sigcontext *context,
                         siginfo_t *info, ucontext_t *ucontext);
static void SigNoHandler(int s, siginfo_t *info, ucontext_t *cp);
static void SigQueue(int s, siginfo_t *info);
static void SigDispatch(void *clientData);
#if !defined VMX86_SERVER || !defined VMX86_VMX // not ESX userworld
static pid_t SigCoreDumpViaChild(void);
static int SigCoreDumpUnmap(const char *keyStr, void *value, void *clientData);
#endif

#define SIGACTION(s, newSA, oldSA) do { \
      if (sigaction(s, newSA, oldSA) < 0) { \
	 Warning("SIG sigaction failed on signal %d: %s\n", \
		 s, Err_ErrString()); \
	 NOT_IMPLEMENTED(); \
      } \
   } while (FALSE)

#if __linux__
/*
 * We verified that the pthread_sigmask() code from glibc's libpthread has been
 * doing the right thing on Linux since glibc 2.0.5. However we cannot use it
 * here yet, because it would create a dependency on libpthread for some
 * applications. See bug 124552 for details.
 */
#   define pthread_sigmask(a, newMask, oldMask) \
   (sigprocmask(a, newMask, oldMask) < 0 ? errno : 0)
#endif

#define PTHREAD_SIGMASK(a, newMask, oldMask) do { \
      int result; \
                  \
      result = pthread_sigmask(a, newMask, oldMask); \
      if (result) { \
         Warning("SIG pthread_sigmask failed: %s\n", \
                 Err_Errno2String(result)); \
	 NOT_IMPLEMENTED(); \
      } \
   } while (FALSE)


/*
 *----------------------------------------------------------------------
 *
 * Sig_Init --
 *
 *      One-time initialization for signals
 *
 * Results:
 *      FALSE on failure, TRUE on success
 *
 * Side effects:
 *	Signals are caught once and for all
 *
 *----------------------------------------------------------------------
 */

Bool
Sig_Init(void)
{
   int s;
   struct sigaction sa, osa;
   SigMask mask;

   if (sig.initialized) { 
      Log("Sig_Init already initialized \n");
      return TRUE;
   }
   sig.initialized = TRUE;

   /*
    * Set up system signal catcher.
    *
    * We ignore SIGTTIN and SIGTTOU so we can always do input and output
    * to the controlling TTY, because we can't use a safe handler in these
    * cases.
    * SIGPIPE is ignored, too, because AIO is too messed up to deal
    * with a dead slave process.  It might help with a dead X server also.
    * It also helps if the UI dies while we are in VMXVmdb_Init().
    *
    * We used to ignore SIGCHLD, but that prevents us from using wait()
    * and waitpid() in a normal way (according to Single Unix Specification,
    * etc.) so we just leave it alone.  (See bug 30099.)
    *
    * We catch signals that have to do with program errors (e.g., SIGSEGV)
    * and signals we want to process (e.g., SIGINT).
    */

   sigfillset(&sig.allSignalMask);

   Sig_BlockAll(&mask);

   /*
    * A bit of unpleasantness with SIGPROF and Linux pthread:
    * libpthread wraps sigaction() but seems unaware of SIGPROF
    * catching by the profiler, which uses __sigaction() to do
    * its business.  So we try to replicate the SIGPROF state
    * with libpthread's sigaction().
    *
    * We only need to do this when running with both profiling
    * and stats, when both the profiler and ustats need to process
    * SIGPROF (with the SIG_CHAIN feature).
    *
    * -- edward
    */

#if __linux__ && defined VMX86_PROFILE && defined VMX86_STATS
   extern int __sigaction(int sig, struct sigaction *sa, struct sigaction *osa);
   if (__sigaction(SIGPROF, NULL, &osa) < 0) {
      NOT_IMPLEMENTED();
   }
   SIGACTION(SIGPROF, &osa, NULL);
#endif

   for (s = 1; s < NSIG; s++) {
      sa.sa_flags = 0;
      switch (s) {
      case SIGTTIN:
      case SIGTTOU:
      case SIGPIPE:
	 sa.sa_handler = SIG_IGN;
	 break;
      case SIGCHLD:
	 sig.callbacks[s].type = SIG_ALLOW;
         sa.sa_flags |= SA_SIGINFO;
	 sa.sa_sigaction = SigCatcherPosix;
	 break;
#ifdef VMX86_STATS
      case SIGPROF:
#if __linux__ && defined VMX86_PROFILE
         /*
          * On Linux, we MUST catch SIGPROF with an old-style handler
          * so we can chain to glibc's old-style handler.
          */
         sig.callbacks[s].type = SIG_NOHANDLER;
         sa.sa_handler = (SigOldUnix *) SigCatcherLinux;
         break;
#endif // linux && profiling
	/*
	 * On platforms other than Linux, fall through; we treat
	 * SIGPROF like all the other unhandled signals.
	 */
#endif // stats
      case SIGHUP:
      case SIGINT:
      case SIGQUIT:
      case SIGILL:
      case SIGABRT:
#if SIGIOT != SIGABRT
      case SIGIOT:
#endif
      case SIGBUS:
      case SIGFPE:
      case SIGUSR1:
      case SIGSEGV:
      case SIGUSR2:
      case SIGALRM:
      case SIGTERM:
#ifdef SIGSTKFLT
      case SIGSTKFLT:
#endif
      case SIGTSTP:
      case SIGURG:
      case SIGXCPU:
      case SIGXFSZ:
      case SIGVTALRM:
      case SIGIO:
#if defined(SIGPOLL) && SIGPOLL != SIGIO
      case SIGPOLL:
#endif
	 sig.callbacks[s].type = SIG_NOHANDLER;
	 sa.sa_flags |= SA_SIGINFO;
	 sa.sa_sigaction = SigCatcherPosix;
	 break;
      default:
	 continue;
      }
      sigfillset(&sa.sa_mask);
      sa.sa_flags |= SA_RESTART;
      /*
       * Handle all signals with SIGBUS, SIGSEGV, and SIGILL unmasked
       * so we can catch faults in the signal handler.  And this means
       * that these signals also need to use SA_NODEFER.
       */
      if (s == SIGBUS || s == SIGSEGV || s == SIGILL) {
         sa.sa_flags |= SA_NODEFER;
      }
      sigdelset(&sa.sa_mask, SIGBUS);
      sigdelset(&sa.sa_mask, SIGSEGV);
      sigdelset(&sa.sa_mask, SIGILL);
      SIGACTION(s, &sa, &osa);
      sig.callbacks[s].oldCatcherIsSigPosix = (osa.sa_flags & SA_SIGINFO) != 0;
      sig.callbacks[s].oldCatcher = sig.callbacks[s].oldCatcherIsSigPosix ?
         osa.sa_sigaction : (SigPosix *) osa.sa_handler;
   }

#if defined VMX86_STATS && defined VMX86_PROFILE
   sig.callbacks[SIGPROF].chain = TRUE;
#if __linux__
   ASSERT_NOT_IMPLEMENTED(!sig.callbacks[SIGPROF].oldCatcherIsSigPosix);
#endif // linux
#endif // stats


   /* 
    * Use a pipe for signal delivery. When we catch a signal, we'll shove
    * the siginfo_t down the pipe. This'll set off the poll callback which will
    * dispatch the right signal handler after reading the siginfo_t from the 
    * pipe. 
    * 
    * According to pipe(7), all writes sized under PIPE_BUF are atomic. 
    * Luckily for us, a siginfo_t is 128 bytes, which is under PIPE_BUF (512 
    * bytes in POSIX.1-2001, 4096 bytes in Linux). This means that we need
    * no locking or signal blocking when putting the signal in the pipe.
    *
    * We need non-blocking IO for this otherwise we may block in the catcher
    * or in the dispatcher, which would be unfortunate.
    * 
    * XXX: Also according to pipe(7), the capacity of a pipe since Linux 2.6.11
    * is 64k, but prior to 2.6.11, the capacity was only 4k (page size). Since
    * a siginfo_t is 128 bytes, 2.6.11+ kernels can queue up to 512 pending
    * signals, while older kernels can only queue up to 32. If this ever 
    * becomes a problem, we will need to use one pipe per signum.
    */
   if (pipe(sig.pipeFds) == -1 ||
       fcntl(sig.pipeFds[0], F_SETFL, O_RDONLY | O_NONBLOCK) < 0 ||
       fcntl(sig.pipeFds[1], F_SETFL, O_WRONLY | O_NONBLOCK) < 0) {
      NOT_IMPLEMENTED();
   }

   Sig_Restore(&mask);
   Poll_Callback(POLL_CS_IPC, POLL_FLAG_READ | POLL_FLAG_PERIODIC,
		 SigDispatch, NULL, POLL_DEVICE, sig.pipeFds[0], NULL);

#if !defined VMX86_SERVER || !defined VMX86_VMX // not ESX userworld
   /*
    * Allocate hash table for core dump unmap regions,
    * sized to accommodate MainMem (say, 64GB in 1MB mapping
    * chunks is 64k entries).  The MainMem cache is currently
    * much smaller than that.
    *
    * Also, we use the data pointer to store a size_t value,
    * so check the size.
    */

   if (!Config_GetBool(FALSE, "signal.dontUnmap")) {
      ASSERT_ON_COMPILE(sizeof (size_t) <= sizeof (void *));
      sig.coreDumpRegions = HashTable_Alloc(4096, HASH_INT_KEY, NULL);
   }
#endif

   return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * Sig_InitThread --
 *
 *      Initialization for a child thread
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Yes.
 *
 *----------------------------------------------------------------------
 */

void
Sig_InitThread()
{
   sigset_t mask;

   /*
    * Don't initialize thread if the main process was
    * never initialized (so not using our signal mechanism).
    */

   if (!sig.initialized) {
      return;
   }

   /*
    * Block all the signals only the main thread should see.
    * Posix threads share signal state except the signal mask.
    */

   sigemptyset(&mask);
   sigaddset(&mask, SIGHUP);
   sigaddset(&mask, SIGINT);
   sigaddset(&mask, SIGQUIT);
   sigaddset(&mask, SIGUSR1);
   sigaddset(&mask, SIGUSR2);
   sigaddset(&mask, SIGALRM);
   sigaddset(&mask, SIGTSTP);
   sigaddset(&mask, SIGURG);
   sigaddset(&mask, SIGVTALRM);
   sigaddset(&mask, SIGIO);
#if defined(SIGPOLL) && SIGPOLL != SIGIO
   sigaddset(&mask, SIGPOLL);
#endif
   sigaddset(&mask, SIGTERM);
   PTHREAD_SIGMASK(SIG_BLOCK, &mask, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Sig_Exit --
 *
 *      One-time cleanup for signals
 *
 * Results:
 *      none
 *
 * Side effects:
 *	none
 *
 *----------------------------------------------------------------------
 */

void
Sig_Exit()
{
   Poll_CallbackRemove(POLL_CS_IPC, POLL_FLAG_READ | POLL_FLAG_PERIODIC,
                       SigDispatch, NULL, POLL_DEVICE);
   close(sig.pipeFds[0]);
   close(sig.pipeFds[1]);
#if !defined VMX86_SERVER || !defined VMX86_VMX // not ESX userworld
   if (sig.coreDumpRegions != NULL) {
      HashTable_Free(sig.coreDumpRegions);
      sig.coreDumpRegions = NULL;
   }
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Sig_Callback --
 *
 *      Register a signal callback
 *
 * Results:
 *      none
 *
 * Side effects:
 *	Callback is registers.
 *
 *----------------------------------------------------------------------
 */

void
Sig_Callback(int s, int type,
	     SigCallbackFunc func,
	     void *clientData)
{
   SigCallback *c = sig.callbacks + s;
   SigMask mask;
   int flags;
   Bool newCatcherIsSigPosix;

   ASSERT(s > 0);
   ASSERT(s < NSIG);

   flags = type & ~SIG_TYPE;
   type &= SIG_TYPE;

#if __linux__ && defined VMX86_PROFILE
   newCatcherIsSigPosix = (s != SIGPROF);
#else
   newCatcherIsSigPosix = TRUE;
#endif
   if ((flags & SIG_CHAIN)
       && c->oldCatcher != SIG_DFL_POSIX && c->oldCatcher != SIG_IGN_POSIX) {
      ASSERT_NOT_IMPLEMENTED(newCatcherIsSigPosix == c->oldCatcherIsSigPosix);
   }

   if (type == SIG_NOHANDLER) {

      /*
       * It's hard to deal with an uncalled safe handler here,
       * so we just drop it.  It's not the coolest thing,
       * so user beware.  -- edward
       *
       * The actual dropping occurs when SigDispatch pulls the
       * signal out of the pipe.
       */

      switch (c->type) {
      case SIG_IMMEDIATE:
      case SIG_SAFE:
         break;
      default:
	 NOT_REACHED();
      }
      func = NULL;
      clientData = NULL;

   } else {
      ASSERT(type > SIG_NOHANDLER && type < SIG_NUM_TYPES);
      ASSERT_BUG(5516, c->type == SIG_NOHANDLER || c->type == SIG_ALLOW);
      ASSERT_NOT_IMPLEMENTED(type != SIG_MONACTION);

      /* 
       * For now we impose the restriction that if the new catcher isn't
       * going to be POSIX-style, the handler must be SIG_IMMEDIATE. This is
       * because our SIG_SAFE dispatcher will be expecting a siginfo_t,
       * which non POSIX-style handlers won't be giving us.
       *
       * This seems reasonable since the only non POSIX-style handler we use
       * is for SIGPROF, which is registered as SIG_IMMEDIATE anyway.
       */
      ASSERT(newCatcherIsSigPosix || type == SIG_IMMEDIATE);
   }

   Sig_BlockAll(&mask);
   ASSERT(!c->handling);
   c->type = type;
   c->chain = (flags & SIG_CHAIN) != 0;
   c->func = func;
   c->clientData = clientData;
   Sig_Restore(&mask);
}


/*
 *----------------------------------------------------------------------
 *
 * Sig_SetCoreDumpFunc --
 *
 *      Set the clean-up hook for the not-handled signals.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	No-handler hook is set.
 *
 *----------------------------------------------------------------------
 */

void
Sig_SetCoreDumpFunc(void (*func)(void *clientData),
		    void *clientData)
{
   SigMask mask;

   Sig_BlockAll(&mask);

   if (func == NULL) {
      ASSERT(sig.coreDumpFunc != NULL);
      clientData = NULL;
   } else {
      ASSERT(sig.coreDumpFunc == NULL);
   }

   sig.coreDumpFunc = func;
   sig.coreDumpClientData = clientData;

   Sig_Restore(&mask);
}


#if __linux__ && defined VMX86_STATS && defined VMX86_PROFILE
/*
 *----------------------------------------------------------------------
 *
 * SigCatcherLinux --
 *
 *      Linux non-SA_SIGINFO wrapper for SigCatcherCommon.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Signal is processed.
 *
 *----------------------------------------------------------------------
 */

void
SigCatcherLinux(int s,                     // IN
                struct sigcontext context) // IN
{
   SigCatcherCommon(s, &context, NULL, NULL);
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * SigCatcherPosix --
 *
 *      Posix SA_SIGINFO wrapper for SigCatcherCommon.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Signal is processed.
 *
 *----------------------------------------------------------------------
 */

void
SigCatcherPosix(int s,           // IN
                siginfo_t *info, // IN
                void *uap)       // IN
{
   ucontext_t *context = (ucontext_t *) uap;
   SigCatcherCommon(s, NULL, info, context);
}


/*
 *----------------------------------------------------------------------
 *
 * SigCatcherCommon --
 *
 *      The signal catcher.  Dispatch or register for later dispatch
 *	the user catcher.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Signal is processed.
 *
 *----------------------------------------------------------------------
 */

void
SigCatcherCommon(int s,                      // IN
                 struct sigcontext *context, // IN
                 siginfo_t *info,            // IN
                 ucontext_t *ucontext)       // IN
{
   SigCallback *c = sig.callbacks + s;
   VThreadID tid;
   int savedErrno;

   /* Save errno so we can restore it when we finish handling the signal. */
   savedErrno = errno;
   tid = VThread_CurID();

   ASSERT(s > 0);
   ASSERT(s < NSIG);

   /*
    * If this is one of the signals handled by Sig_CrashCatcherBegin/End
    * and we're in an active crash catcher region, longjmp back to the
    * handler.
    *
    * We don't use sigsetjmp/siglongjmp for the crash catcher, because
    * they're too slow. (The performance is okay on Linux, but on Mac
    * OS X they're cripplingly slow due to the implicit sigprocmask()
    * call in setjmp() coupled with Mac OS's horrible syscall
    * performance.)
    *
    * Instead, we'll manually restore the signal mask that was saved
    * in ucontext. This requires that we were registered with
    * SA_SIGINFO. This is the case for all signals that the crash
    * catcher traps.
    */

   if (s == SIGSEGV || s == SIGBUS || s == SIGILL || s == SIGABRT) {
      SigCrashCatcherState *crashCatcher = &SigCrashCatcher[tid];

      if (crashCatcher->active) {
         ASSERT(info && ucontext);

         /* Sanity check: Make sure the signal that just occurred was unmasked. */
         ASSERT(!sigismember(&ucontext->uc_sigmask, s));

         Sig_Restore(&ucontext->uc_sigmask);
         _longjmp(crashCatcher->jmpBuf, s);
         NOT_REACHED();
      }
   }

   // XXX c->oldCatcher and c->func, if invoked, must return to this handler...
   VThread_SetIsInSignal(tid, TRUE);

   switch (c->type) {
   case SIG_NOHANDLER:
      if (c->chain) {
         SigCallChain(s, context, info, ucontext);
      } else {
         /*
          * SigNoHandler only knows the SA_SIGINFO way.  So we'd better
          * have been registered with SA_SIGINFO; the other case is only
          * for chaining.
          */
         ASSERT(info && ucontext);
         SigNoHandler(s, info, ucontext);
      }
      break;

   case SIG_SAFE:

      /* 
       * If we've made it to SIG_SAFE, our handler was registered with 
       * SA_SIGINFO.
       */
      ASSERT(info && ucontext);

      /*
       * XXX hack to call handler immediately when nobody
       * XXX is calling SigDispatch
       */

      if (sig.noMainLoop > 0) {
	 if (c->handling) {
	    Warning("Serial signal %d delayed.\n", s);
	 } else {
	    c->handling = TRUE;
	    c->func(s, info, c->clientData);
	 }
      } else {
         SigQueue(s, info);
      }

      break;

   case SIG_MONACTION:
      NOT_REACHED();
      break;

   case SIG_IMMEDIATE:
      if (c->chain) {
         SigCallChain(s, context, info, ucontext);
      }
      c->func(s, info, c->clientData);
      break;

   case SIG_ALLOW:
      break;

   default:
      NOT_REACHED();
      break;
   }

   VThread_SetIsInSignal(tid, FALSE);
   errno = savedErrno;
}


/*
 *-----------------------------------------------------------------------------
 *
 * SigCallChain --
 *
 *      Chain to the oldCatcher for a signal.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Calls handler.
 *
 *-----------------------------------------------------------------------------
 */

void
SigCallChain(int s,                      // IN
             struct sigcontext *context, // IN
             siginfo_t *info,            // IN
             ucontext_t *ucontext)       // IN
{
   SigCallback *c = sig.callbacks + s;

   if (c->oldCatcher != SIG_IGN_POSIX && c->oldCatcher != SIG_DFL_POSIX) {
      if (c->oldCatcherIsSigPosix) {
         ASSERT(info && ucontext);
         c->oldCatcher(s, info, ucontext);
      } else {
#if __linux__
         (*(SigOldLinux *) c->oldCatcher)(s, *context);
#else
         /* On platforms other than Linux, everything must use SA_SIGINFO. */
         NOT_IMPLEMENTED();
#endif
      }
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SigNoHandler --
 *
 *      The default signal handler.
 *
 * Results:
 *      Never returns.
 *
 * Side effects:
 *	Hook functions are called.
 *	The signal is resent, or the process aborts, loops, or panics.
 *
 *----------------------------------------------------------------------
 */

void
SigNoHandler(int s,           // IN
             siginfo_t *info, // IN
             ucontext_t *cp)  // IN
{
   sigset_t mask;
   Bool su;

   su = IsSuperUser();
   SuperUser(FALSE);

   switch (sig.loopCount++) {
   case 0:
      break;
   case 1:
      Panic("Loop on signal %d -- tid %lu at 0x%08lx.\n",
	    s, (unsigned long)Util_GetCurrentThreadId(), SC_EIP(cp));
   default:
      VThread_ExitThread(FALSE);
   }

   /*
    * Save signal context for the debugger and print EIP,
    * in this order (and here and not sooner) so if we segfault
    * again (for example) it's already saved and not clobbered
    * by the second signal.
    */

   sig.ucontext = *cp;

   if (s == SIGHUP || s == SIGINT || s == SIGTERM || s == SIGTSTP) {
      Warning("Caught signal %d -- tid %lu (eip 0x%08lx)\n",
	      s, (unsigned long)Util_GetCurrentThreadId(), SC_EIP(cp));
   } else {
      int i;

      /*
       * Make sure signal backtrace gets logged
       */
      Log_DisableThrottling();

      Warning("Caught signal %d -- tid %lu\n", s, (unsigned long)Util_GetCurrentThreadId());
      Log("SIGNAL: eip 0x%lx esp 0x%lx ebp 0x%lx\n",
	  SC_EIP(cp), SC_ESP(cp), SC_EBP(cp));
      Log("SIGNAL: eax 0x%lx ebx 0x%lx ecx 0x%lx edx 0x%lx esi 0x%lx "
          "edi 0x%lx\n",
          SC_EAX(cp), SC_EBX(cp), SC_ECX(cp), SC_EDX(cp), SC_ESI(cp),
          SC_EDI(cp));
#ifdef VM_X86_64
      Log("        r8 0x%lx r9 0x%lx r10 0x%lx r11 0x%lx r12 0x%lx "
          "r13 0x%lx r14 0x%lx r15 0x%lx\n",
          SC_R8(cp), SC_R9(cp), SC_R10(cp), SC_R11(cp), SC_R12(cp),
          SC_R13(cp), SC_R14(cp), SC_R15(cp));
#endif
      for (i = 0; i < 8; i++) {
         uint32 *x = (uint32 *)(SC_ESP(cp) + i*4*4);
         Log("SIGNAL: stack %p : 0x%08x 0x%08x 0x%08x 0x%08x\n",
             x, x[0],x[1],x[2],x[3]);
      }
      Util_Backtrace(0);
   }

   /*
    * Some signals we just send to ourselves again
    * The return is there because the signal isn't necessarily fatal.
    */

   switch (s) {
   case SIGHUP:
   case SIGINT:
   case SIGTERM:
   case SIGTSTP:
      Sig_ForceSig(s);
      SuperUser(su);
      sig.loopCount = 0;
      return;
   }

   /*
    * Unblock signals before panicking, except SIGPROF, which we get
    * all the time (if at all).
    *
    * The fact that we need to keep blocking SIGPROF suggests that
    * we maybe shouldn't unblock any signal, but it seems nicer
    * to be receiving signals while we do all the panic stuff.
    *
    * -- edward
    */

   sigemptyset(&mask);
   sigaddset(&mask, SIGPROF);
   pthread_sigmask(SIG_SETMASK, &mask, NULL);

   Panic("Unexpected signal: %d.\n", s);
}


/*
 *----------------------------------------------------------------------
 *
 * Sig_NoMainLoop --
 *
 *      Use Xt to handle signal callbacks
 *
 * Results:
 *      none
 *
 * Side effects:
 *	sig.noMainLoop may be changed.
 *
 *----------------------------------------------------------------------
 */

void
Sig_NoMainLoop(Bool on)
{
   if (on) {
      sig.noMainLoop++;
   } else {
      sig.noMainLoop--;
      ASSERT(sig.noMainLoop >= 0);
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SigQueue --
 *
 *      Queue a SIG_SAFE signal for dispatching later. The pipe holding
 *      pending signals must be open for business.
 *
 * Results:
 *      none
 *
 * Side effects:
 *	Pipe may be written to.
 *
 *----------------------------------------------------------------------
 */

void
SigQueue(int s, siginfo_t *info)
{
   ssize_t nbytes = write(sig.pipeFds[1], info, sizeof *info);

   /* No partial writes since sizeof *info <= PIPE_BUF. */
   ASSERT(nbytes == -1 || nbytes == sizeof *info);
   
   if (nbytes == -1) {
      if (errno == EAGAIN) {
         /* 
          * Pipe is full. It's not safe to dispatch it immediately, so
          * for now we'll ASSERT, and if QA discovers that this is a problem,
          * we'll use one pipe per signal, which should help.
          */
         Warning("Too many signals queued, this shouldn't happen\n");
         ASSERT(FALSE);
      } else {
         Warning("Could not queue signal %d (error %d)\n", s, errno);
      }
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SigDispatch --
 *
 *      Dispatch signal handlers, if any. Called from the poll context,
 *      so there should be no concurrency issues w.r.t Sig_Callback.
 *
 * Results:
 *      none
 *
 * Side effects:
 *	Signal handlers may be called.
 *
 *----------------------------------------------------------------------
 */

void
SigDispatch(void *clientData)
{
   siginfo_t info;
   ssize_t nbytes;

#ifdef VMX86_VMX
   POLL_VECTOR(U_Poll_SigDispatch);
#endif

   /* 
    * No need to block signals here. We're calling the signal handlers
    * synchronously, and if we're interrupted by another signal, it'll just
    * get stashed in the pipe.
    */
   nbytes = read(sig.pipeFds[0], &info, sizeof info);
   if (nbytes == sizeof info) {
      ASSERT(info.si_signo > 0 && info.si_signo < NSIG);
      SigCallback *c = sig.callbacks + info.si_signo;

      /* 
       * It's possible that by the time we got this signal, the signal
       * handler had been unregistered. Drop the signal.
       */
      if (c->type == SIG_NOHANDLER) {
         Warning("Dropping unhandled signal %d.\n", info.si_signo);
         return;
      }

      if (c->handling) {
         /* 
          * This is annoying. We're already handling a signal of this type, so
          * shove the siginfo back in the pipe to be handled later.
          *
          * XXX: The write will re-order the siginfos. Is it avoidable? We 
          * can't call fcntl(sig.pipeFds[0], F_SETFL, O_APPEND) or lseek().
          */
         SigQueue(info.si_signo, &info);
      } else {
         c->handling = TRUE;
         c->func(info.si_signo, &info, c->clientData);
      }
      return;
   } else if (nbytes == -1) {
      /* 
       * It's possible that SigDispatch was called spuriously, in which case
       * read(2) would fail with EAGAIN. Let's allow for that.
       */
      if (errno == EAGAIN) {
         return;
      }
   }

   /* 
    * Short read (which shouldn't ever happen) or some other error in reading 
    * from the pipe. We can't cope with these situations.
    */
   Warning("Could not read siginfo: %d\n", errno);
   NOT_IMPLEMENTED();
}


/*
 *----------------------------------------------------------------------
 *
 * Sig_Continue --
 *
 *      Allow handlers to be dispatched again for signal s.
 *
 * Results:
 *      none
 *
 * Side effects:
 *	The state for signal s is changed.
 *
 *----------------------------------------------------------------------
 */

void
Sig_Continue(int s)
{
   SigCallback *c = sig.callbacks + s;

   ASSERT(s > 0);
   ASSERT(s < NSIG);
   ASSERT(c->handling);

   c->handling = FALSE;
}


/*
 *----------------------------------------------------------------------
 *
 * Sig_BlockAll --
 *
 *      Block all signals
 *
 * Results:
 *      If oldMask is not NULL, the old signal mask is returned.
 *
 * Side effects:
 *	All blockable signals are blocks.
 *
 *----------------------------------------------------------------------
 */

void
Sig_BlockAll(SigMask *oldMask)
{
   PTHREAD_SIGMASK(SIG_BLOCK, &sig.allSignalMask, oldMask);
}


/*
 *----------------------------------------------------------------------
 *
 * Sig_Block --
 *
 *      Block a signal
 *
 * Results:
 *      If oldMask is not NULL, the old signal mask is returned.
 *
 * Side effects:
 *	The specified signal is blocked.
 *
 *----------------------------------------------------------------------
 */

void
Sig_Block(int s, SigMask *oldMask)
{
   sigset_t mask;

   sigemptyset(&mask);
   sigaddset(&mask, s);
   PTHREAD_SIGMASK(SIG_BLOCK, &mask, oldMask);
}


/*
 *----------------------------------------------------------------------
 *
 * Sig_Unblock --
 *
 *      Unblock a signal
 *
 * Results:
 *      If oldMask is not NULL, the old signal mask is returned.
 *
 * Side effects:
 *	The specified signal is unblocked.
 *
 *----------------------------------------------------------------------
 */

void
Sig_Unblock(int s, SigMask *oldMask)
{
   sigset_t mask;

   sigemptyset(&mask);
   sigaddset(&mask, s);
   PTHREAD_SIGMASK(SIG_UNBLOCK, &mask, oldMask);
}


/*
 *----------------------------------------------------------------------
 *
 * Sig_Restore --
 *
 *      Restore signal mask
 *
 * Results:
 *      none
 *
 * Side effects:
 *	The signal mask is changed.
 *
 *----------------------------------------------------------------------
 */

void
Sig_Restore(SigMask *mask)
{
   PTHREAD_SIGMASK(SIG_SETMASK, mask, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Sig_ForceSig --
 *
 *      Send a signal to ourselves and make sure we get it
 *
 * Results:
 *      none
 *
 * Side effects:
 *	A signal is received
 *
 *----------------------------------------------------------------------
 */

void
Sig_ForceSig(int s) // IN
{
   struct sigaction sa, oldSA;
   SigMask mask;

   memset(&sa, 0, sizeof sa);
   sa.sa_handler = SIG_DFL;
   SIGACTION(s, &sa, &oldSA);
   Sig_RaiseSig(s);
   Sig_Unblock(s, &mask);
   Sig_Restore(&mask);
   SIGACTION(s, &oldSA, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Sig_NullHandler --
 *
 *	Handle and drop a signal.
 *	Use this function as the generic ignore-signal handler.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *----------------------------------------------------------------------
 */

void
Sig_NullHandler(int s, siginfo_t *info, void *clientData)
{
   if (clientData != NULL) {
      Warning("Ignored signal %d -- tid %lu (%s).\n",
	      s, (unsigned long)Util_GetCurrentThreadId(), (char *) clientData);
   } else {
      Warning("Ignored signal %d -- tid %lu.\n", s, (unsigned long)Util_GetCurrentThreadId());
   }
   Sig_Continue(s);
}


/*
 *----------------------------------------------------------------------
 *
 * __vmware_fork --
 *
 *      Fork a process on OS level, without taking any glibc mutexes
 *	and without invoking callbacks registered through pthread_atfork.
 *
 * Results:
 *      Like normal fork: 
 *	0	for child
 *	>0	for parent (child pid)
 *	-1	for error, errno set
 *
 * Side effects:
 *	Child process started.
 *
 *----------------------------------------------------------------------
 */

static INLINE pid_t
__vmware_fork(void)
{
#if defined(__APPLE__) || defined(__FreeBSD__)
   /*
    * If we ever change __vmware_fork to do something other than fork() on Mac
    * OS then we also need to update the code in Sig_ForceSig that depends on
    * the fact that pthread_kill works correctly on Mac OS.
    */

   Bool su;
   pid_t result;

   /*
    * Give the child thread the ability to acquire super user privileges if the
    * parent thread has that ability.
    */

   su = IsSuperUser();
   SuperUser(TRUE);

   /*
    * XXXMACOS The Linux trick does not work, so for now do a regular fork(2),
    *          it is better than nothing.
    */
   result = fork();

   SuperUser(su);

   return result;
#else
   return syscall(SYS_fork);
#endif
}


#if !defined VMX86_SERVER || !defined VMX86_VMX // { not ESX userworld

#if !defined(__APPLE__) && !defined(__FreeBSD__)

/*
 *----------------------------------------------------------------------
 *
 * SigGetSysctlString --
 *
 *      Retrieve specified sysctl value.
 *
 * Results:
 *      TRUE on success, FALSE on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static Bool
SigGetSysctlString(const char *sysctlPath,   // IN: Buffer for file name
                   char *sysctlValue,        // OUT: Sysctl's value
                   size_t sysctlValueSize)   // IN: Maximum core name length
{
   int fd;
   Bool empty;

   fd = Posix_Open(sysctlPath, O_RDONLY);
   if (fd == -1) {
      return FALSE;
   }
   empty = TRUE;
   while (sysctlValueSize > 0) {
      ssize_t rd = read(fd, sysctlValue, sysctlValueSize);
      if (rd == -1) {
         close(fd);
         return FALSE;
      }
      if (rd == 0) {
         close(fd);
         /* Strip trailing \n.  Should be always present. */
         if (!empty && sysctlValue[-1] == '\n') {
            sysctlValue--;
         }
         *sysctlValue = 0;
         return TRUE;
      }
      ASSERT((size_t)rd <= sysctlValueSize);
      sysctlValue += rd;
      sysctlValueSize -= rd;
      empty = FALSE;
   }
   close(fd);
   return FALSE;
}


/*
 *----------------------------------------------------------------------
 *
 * SigGetSysctlInt --
 *
 *      Retrieve specified sysctl value.
 *
 * Results:
 *      TRUE on success, FALSE on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static Bool
SigGetSysctlInt(const char *sysctlPath,   // IN: Buffer for file name
                int *sysctlValue)         // OUT: Sysctl's value
{
   char value[32];

   if (SigGetSysctlString(sysctlPath, value, sizeof value)) {
      char* end;

      *sysctlValue = strtoul(value, &end, 10);
      /*
       * strtoul must have eat string to end, and there must have been
       * at least one digit.
       */
      if (end != value && *end == 0) {
         return TRUE;
      }
   }
   return FALSE;
}

#endif /* ndef __APPLE__ */


/*
 *----------------------------------------------------------------------
 *
 * SigGetSysctlCorePattern --
 *
 *      Retrieve core pattern via sysctl
 *
 * Results:
 *      Always succeeds; may guess a value if underlying sysctl fails.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
SigGetSysctlCorePattern(char *pattern,   // IN/OUT: Buffer for core pattern
                        size_t len)      // IN: Size of buffer
{
#if defined(__APPLE__)
   /*
    * Ideally, this would be sysctlbyname("kern.corefile"...), but
    * that call does not work on Mac OS (as of 10.4.7).
    */
   int mib[2] = {CTL_KERN, KERN_COREFILE};
   if (!sysctl(mib, ARRAYSIZE(mib), pattern, &len, NULL, 0)) {
      return;
   }
#elif defined(linux)
   if (SigGetSysctlString("/proc/sys/kernel/core_pattern", pattern, len)) {
      return;
   }
#endif

   strncpy(pattern, "core", len); // Sysctls failed - use the default value
}

/*
 *----------------------------------------------------------------------
 *
 * SigAppendString --
 *
 *      Append string to the buffer.
 *
 * Results:
 *      TRUE on success.
 *      FALSE if value did not fit to the buffer.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static Bool
SigAppendString(char       **ptr,     // IN/OUT: Buffer for value
                char       *end,      // IN: End of buffer
                const char *value)    // IN: Value
{
   char *pos = *ptr;
   int len;
   size_t size = end - pos;

   len = snprintf(pos, size, "%s", value);
   if (len >= size) {
      return FALSE;
   }
   *ptr = pos + len;
   return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * SigAppendInt --
 *
 *      Append integer value to the buffer.
 *
 * Results:
 *      TRUE on success.
 *      FALSE if value did not fit to the buffer.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static Bool
SigAppendInt(char **ptr,      // IN/OUT: Buffer for value
             char  *end,      // IN: End of buffer
             int    value)    // IN: Value
{
   char *pos = *ptr;
   int len;
   size_t size = end - pos;

   len = snprintf(pos, size, "%u", value);
   if (len >= size) {
      return FALSE;
   }
   *ptr = pos + len;
   return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * SigGetCoreFileName --
 *
 *      Compute core file name.  Duplicates kernel functionality.
 *
 *      CAUTION! This routine is called during panic and it is rather
 *      sensitive. Don't use things like the Str_* routines or routines
 *      which may use Msg_Append in here.
 *
 * Results:
 *      NULL    Unicode conversion problems.
 *      !NULL   Core file name, truncated according to the kernel truncation
 *              rules, in UTF8. Caller is resposible to free the result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
SigGetCoreFileName(Unicode *result,  // OUT: Buffer for file name
                   pid_t corePid,    // IN: Core pid
                   int coreSig)      // IN: Core signal
{
   char coreFileName[PATH_MAX + 100];
/* Kernel limits for core name length */
#define MAX_CORE_PATTERN 64
#define MAX_CORE_NAME_LENGTH 64
   char corePattern[MAX_CORE_PATTERN + 2]; /* kernel limit, plus LF + terminator */
   char *corePtr;
   char *coreEnd;
   char *coreFilePtr;
   Bool pidEmitted = FALSE;
   Bool hasWildcard = FALSE;
   Bool coreUsesPid = FALSE; // TRUE forces append of PID if no %p

#if defined(linux)
   {
      int val = 0;
      if (!SigGetSysctlInt("/proc/sys/kernel/core_uses_pid", &val)) {
         coreUsesPid = FALSE;
      } else {
         coreUsesPid = (val != 0);
      }
   }
#endif
   SigGetSysctlCorePattern(corePattern, sizeof corePattern);

   /* 
    * On kernels >= 2.6.19, a '|' at the beginning means send
    * core dump to process after pipe via that process' stdin.
    * See PR149633.
    */
   if (corePattern[0] ==  '|') {
      /* 
       * GUESS that the core is where Ubuntu might put it, for now.
       * We have no way of knowing where, since the script gets the
       * core via stdin and can write it anywhere (or not write it).
       */
      Log("Core dump pipes to process %s, core file unreliable\n", 
          corePattern + 1);
      strncpy(corePattern, "core", sizeof corePattern);
   }
   
   coreFilePtr = coreFileName;
   coreEnd = coreFilePtr + sizeof(coreFileName) - 1; /* last byte for zero termination */
   /* Prepend current directory if core path is not absolute */
   if (corePattern[0] != DIRSEPC) {
      if (getcwd(coreFilePtr, sizeof(coreFileName))) {
         coreFilePtr += strlen(coreFilePtr);
         /*
          * If current directory name was too long, revert to reporting relative
          * path.  Otherwise if current directory was not empty and it does not
          * end with slash, append one.  If path ends with slash (on posix system
          * it should occur only if current directory is root of filesystem) or
          * if it is empty (which should never happen), do nothing.
          */
         if (coreFilePtr >= coreEnd) {
            coreFilePtr = coreFileName;
         } else if (coreFilePtr > coreFileName && coreFilePtr[-1] != DIRSEPC) {
            *coreFilePtr++ = DIRSEPC;
         }
      }
   }
   /* Maximum core name length enforced by kernel. */
   if (coreFilePtr + MAX_CORE_NAME_LENGTH < coreEnd) {
      coreEnd = coreFilePtr + MAX_CORE_NAME_LENGTH;
   }
   for (corePtr = corePattern; *corePtr; corePtr++) {
      if (*corePtr == '%') {
         switch (*++corePtr) {
         case 0:
            goto exit;
         case '%':
            if (coreFilePtr >= coreEnd) {
               goto exit;
            }
            *coreFilePtr++ = '%';
            break;
#if defined(__APPLE__)
    /* 
     * Apple/Darwin obeys POSIX flags:
     * %P = pid
     * %U = user id (uid)
     * %N = name of process
     */
         case 'P':
            pidEmitted = TRUE;
            if (!SigAppendInt(&coreFilePtr, coreEnd, corePid)) {
               goto exit;
            }
            break;
         case 'U':
            if (!SigAppendInt(&coreFilePtr, coreEnd, getuid())) {
               goto exit;
            }
            break;
         case 'N':
            {
               /* 
		* Note: use getpid() instead of corePid to look up the process
		* name.  corePid is the dieing child, which does not have
		* a resolvable name via this BSD-style interface.
		*/
               struct kinfo_proc info;
               size_t length = sizeof info;
	       int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
	       if (sysctl(mib, ARRAYSIZE(mib), &info, &length, NULL, 0)) {
                  goto exit;
               };
               if (!SigAppendString(&coreFilePtr, coreEnd, 
                                    info.kp_proc.p_comm)) {
                  goto exit;
               }
            }
            break;
#else  /* Linux flags */
         case 'p':
            pidEmitted = TRUE;
            if (!SigAppendInt(&coreFilePtr, coreEnd, corePid)) {
               goto exit;
            }
            break;
         case 'u':
            if (!SigAppendInt(&coreFilePtr, coreEnd, getuid())) {
               goto exit;
            }
            break;
         case 'g':
            if (!SigAppendInt(&coreFilePtr, coreEnd, getgid())) {
               goto exit;
            }
            break;
         case 's':
            if (!SigAppendInt(&coreFilePtr, coreEnd, coreSig)) {
               goto exit;
            }
            break;
         case 't':
            /* Time changes every second... */
            hasWildcard = TRUE;
            if (!SigAppendString(&coreFilePtr, coreEnd, "*")) {
               goto exit;
            }
            break;
         case 'h':
            {
               struct utsname uts;

               if (uname(&uts) != 0) {
                  hasWildcard = TRUE;
                  strncpy(uts.nodename, "*", sizeof uts.nodename);
               }
               if (!SigAppendString(&coreFilePtr, coreEnd, uts.nodename)) {
                  goto exit;
               }
            }
            break;
         case 'e':
            /*
             * Image name is not easily available. Would have to parse 
             * /proc/pid/status and unescape value found.
             */
            hasWildcard = TRUE;
            if (!SigAppendString(&coreFilePtr, coreEnd, "*")) {
               goto exit;
            }
            break;
#endif
         default:
            break;
         }
      } else {
         if (coreFilePtr >= coreEnd) {
            goto exit;
         }
         *coreFilePtr++ = *corePtr;
      }
   }
   if (coreUsesPid && !pidEmitted) {
      if (coreFilePtr >= coreEnd) {
         goto exit;
      }
      *coreFilePtr++ = '.';
      if (!SigAppendInt(&coreFilePtr, coreEnd, corePid)) {
         goto exit;
      }
   }
exit:
   *coreFilePtr = '\0';

   /*
    * If core pattern contains %t or %e, we just put wildcard to the pattern.
    * Let's try whether such pattern matches only one file - and if it does,
    * use file that matched.  It will not work if user uses pattern
    * /cores/user%u/core.%e.%t to put all cores into /cores, but for normal
    * configurations where process ID is part of pattern it should work
    * reasonable.
    *
    * If you want it precise, then implement %e correctly and use alphabetically
    * latest one file found (as %t will use 10 digit stamps for next 280
    * years.
    */
   if (hasWildcard) {
      glob_t globbuf;

      if (glob(coreFileName, GLOB_NOCHECK, NULL, &globbuf) == 0) {
         if (globbuf.gl_pathc == 1) {
            *result = Unicode_Alloc(globbuf.gl_pathv[0],
                                    STRING_ENCODING_DEFAULT);
         }
         globfree(&globbuf);
         return;
      }
   }

   *result = Unicode_Alloc(coreFileName, STRING_ENCODING_DEFAULT);
}

#endif // }


/*
 *----------------------------------------------------------------------
 *
 * Sig_CoreDump --
 *
 *      Core dump.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	We leave a core file, hopefully without actually dying.
 *
 *----------------------------------------------------------------------
 */

void
Sig_CoreDump(void)
{
#if defined VMX86_SERVER && defined VMX86_VMX // ESX userworld
   int rc;
   char coreFileName[PATH_MAX + 100];

   if (sig.coreDumpFunc != NULL) {
      sig.coreDumpFunc(sig.coreDumpClientData);
   }
   rc = VMMon_LiveCoreDump(coreFileName, sizeof coreFileName);
   if (rc == 0) {
      Panic_SetCoreDumpFileName(coreFileName);
   }
#else
   pid_t corePid = SigCoreDumpViaChild();

   if (corePid != -1) {
      Unicode coreFileName = NULL;

      SigGetCoreFileName(&coreFileName, corePid, SIGABRT);
      if (coreFileName == NULL) {
         Log("%s: SigGetCoreFileName failure", __FUNCTION__);
      }
      Panic_SetCoreDumpFileName(coreFileName);
      Unicode_Free(coreFileName);
   }
#endif
}


#if !defined VMX86_SERVER || !defined VMX86_VMX // { not ESX userworld

/*
 *-----------------------------------------------------------------------------
 *
 * SigCoreDumpViaChild --
 *
 *    Fork and have the child dump core.
 *
 * Results:
 *    Child pid.
 *
 * Side effects:
 *    Ideally none, but might kill us.
 *
 *-----------------------------------------------------------------------------
 */

pid_t
SigCoreDumpViaChild()
{
   struct rlimit rlim;
   pid_t child;

   /* 
    * Check core dump limit and record it.
    * Do this here to minimize what we do in the child.
    */

   if (getrlimit(RLIMIT_CORE, &rlim) < 0) {
      Warning("Unable to get core dump limit: %s.\n", Err_ErrString());
   } else if (rlim.rlim_cur != RLIM_INFINITY) {
      Warning("Core dump limit is %lu KB.\n",
	      (unsigned long) (rlim.rlim_cur / 1024));
   }

   /*
    * Fork and let the child core dump.  Then just return.
    *
    * This is good because
    *   - Windows does the same thing
    *   - sig.coreDumpFunc may mess up the state of other
    *     (possibly still running) threads
    *   - core dump doesn't seem to happen in a pthread thread
    */

   child = __vmware_fork();
   switch (child) {
   case -1:
      Warning("Fork failed: %s\n", Err_ErrString());
      return -1;

   case 0:
      break; /* continue on our path towards destruction */

   default: {
      /*
       * We got someone else to do our dirty work for us,
       * but be nice and wait for it (but not forever).
       *
       * The advantages of waiting are
       *   - the child has a copy of our state and may act like us,
       *     so don't try to run at the same time
       *   - we can tell from the exit status whether a core was
       *     actually generated
       *
       * The disadvantage is
       *   - can't wait forever, so the code gets complicated
       *
       * -- edward
       */

      int i;
      pid_t retval = -1;

      for (i = 0;;) {
	 int status;
	 pid_t pid = waitpid(child, &status, WNOHANG);
	 if (pid == child) {
	    if (!WCOREDUMP(status)) {
	       Warning("Child process %d failed to dump core (status %#x).\n",
		       child, status);
	    } else {
	       Warning("Core dumped.\n");
	       retval = child;
	    }
	    break;
	 }
	 if (pid < 0) {
	    Warning("Failed to wait for child process %d: %s.\n",
		    child, Err_ErrString());
	    break;
	 }
	 if (pid > 0) {
	    Warning("Wait for child process %d returned %d.\n", child, pid);
	    break;
	 }
	 if (++i > 120) {
	    Warning("Timed out waiting for child process %d.\n", child);
	    break;
	 }
	 if (i > 10) {
	    Warning("Waiting for child process %d to dump core...\n", child);
	 }
	 sleep(1);
      }
      return retval;
      }
   }

   /*
    * Call the predump function.
    */

   if (sig.coreDumpFunc != NULL) {
      sig.coreDumpFunc(sig.coreDumpClientData);
   }

   /*
    * Unmap regions to exclude from core.
    */

   if (sig.coreDumpRegions != NULL) {
      HashTable_ForEach(sig.coreDumpRegions, SigCoreDumpUnmap, NULL);
   }

#if __APPLE__
   /*
    * On Mac OS, writing a core file requires being super-user or being a
    * member of the 'admin' group (because of the permissions on '/cores'), and
    * like on other Unices, the process must not be setuid or setgid.
    *
    * Unfortunately, when our per-thread unprivileged identity layer is
    * present, it hides all the groups the unprivileged user belongs to (except
    * for the current group).
    *
    * So we must first discard our per-thread unprivileged identity layer to
    * expose a potential membership of the unprivileged user to the 'admin'
    * group, then we must permanently change our per-process identity (which is
    * safe to do now that we are in our own separate child process with only
    * one thread) to that of the unprivileged user. Was bug 229195.
    */

   SuperUser(TRUE);
   setuid(getuid());
#else
   /*
    * We may still be able to dump core when VMMon_AllowCoreDump() fails
    * (normal process without setuid, for example).
    */

   SuperUser(FALSE);
#   ifdef VMX86_VMX
   VMMon_AllowCoreDump();
#   endif
#endif

   Sig_ForceSig(SIGABRT);
   _exit(1);
   NOT_REACHED();
}


/*
 *-----------------------------------------------------------------------------
 *
 * SigCoreDumpUnmap --
 *
 *    Unmap a region.
 *
 * Results:
 *    Always 0.
 *
 * Side effects:
 *    Ideally none, but might kill us.
 *
 *-----------------------------------------------------------------------------
 */

int
SigCoreDumpUnmap(const char *keyStr, // IN
                 void *value,        // IN
                 void *clientData)   // IN: Unused
{
   // low bit of size means remap
   if (((size_t) value & 1) == 0) {
      /*
       * We blindly call munmap() on memory of unknown origin,
       * some of which may have been mapped by shmat().
       * However, this should work and is at worse a no-op.
       */

      munmap((void *) keyStr, (size_t) value);

   } else {
      void *start = (void *) keyStr;
      void *end = start + ((size_t) value & ~1);
      void *p;

      /*
       * Fix up each preserved region by remapping each page
       * with anonymous memory and copying the contents.
       */

      for (p = start; p < end; p += PAGE_SIZE) {
	 static uint8 buf[PAGE_SIZE]; // static to keep stack small, 
	 memcpy(buf, p, PAGE_SIZE);
	 if (mmap(p, PAGE_SIZE, PROT_READ|PROT_WRITE,
		  MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0) == MAP_FAILED) {
	    // fail silently, nothing we can do
	    continue;
	 }
	 memcpy(p, buf, PAGE_SIZE);
      }
   }
   return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Sig_CoreDumpRegion --
 *
 *      Define or undefine a region to unmap or remap (as MAP_PRIVATE)
 *	before dumping core.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Sig_CoreDumpRegion(Bool add,	// IN: add or delete
                   Bool unmap,	// IN: unmap or remap
                   void *addr,	// IN: start address of region
                   size_t size)	// IN: size of region
{
   ASSERT(addr != NULL);
   //ASSERT(PAGE_OFFSET(addr) == 0);
   ASSERT(size != 0);

   if (sig.coreDumpRegions == NULL) {
   } else if (add) {
      Bool success;
      // low bit of size means remap
      size = ROUNDUPBITS(size, PAGE_SHIFT) | !unmap;
      success = HashTable_Insert(sig.coreDumpRegions, addr, (void *) size);
      ASSERT(success);
   } else {
      Bool success;
      success = HashTable_Delete(sig.coreDumpRegions, addr);
      ASSERT(success);
   }
}

#endif // }


/*
 *----------------------------------------------------------------------
 *
 * Sig_RaiseSig --
 *
 *      Send a signal to the current thread but do not adjust any signal 
 *      handlers that may be set up.
 *
 * Results:
 *      none
 *
 * Side effects:
 *      A signal is raised on the current thread.
 *
 *----------------------------------------------------------------------
 */

void
Sig_RaiseSig(int s)
{
   Util_ThreadID tid;

   /*
    * We may end up calling this function from a child process that we've
    * created just for the purpose of core dumping. If we've created this
    * process by calling directly into the kernel and bypassing the callbacks
    * registered via pthread_atfork then pthread_kill may get confused and
    * start killing the wrong thread when we invoke:
    *           pthread_kill((pthread_t)tid, s);
    * On Mac OS, we do not use __vmware_fork() so we can safely use
    * pthread_kill here.
    */
   tid = Util_GetCurrentThreadId();
#if defined(__APPLE__) || defined(__FreeBSD__)
   pthread_kill(tid, s);
#else
   kill(tid, s);
#endif
}
