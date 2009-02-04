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
 * pollDefault.c -- the default implementation of the Poll interface.
 */


#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#if _WIN32
#include <winsock2.h> // must include instead of windows.h (see winsock2.h)
#include <winuser.h>
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <errno.h>
#endif

#include "vmware.h"
#include "vthreadBase.h"
#include "pollImpl.h"
#include "hostinfo.h"
#include "err.h"
#include "util.h"

#define LOGLEVEL_MODULE poll
#include "loglevel_user.h"


#ifdef VMX86_SERVER
#define MAX_QUEUE_LENGTH 4096
#else
#if _WIN32 && MAXIMUM_WAIT_OBJECTS != 64
#error MAXIMUM_WAIT_OBJECTS is not 64.
#endif
#ifdef _WIN32
// MsgWaitForMultipleObjects() only takes MAXIMUM_WAIT_OBJECTS - 1 objects
#define MAX_QUEUE_LENGTH 63
#define POLL_MAX_SLAVE_THREADS 3
#define INVALID_EVENT_INDEX -1
#else
// arbitrarily large queue length but still limited similar to esx.
#define MAX_QUEUE_LENGTH 512
#endif
#endif


/*
 * When a periodic callback's period is an even multiple of the host
 * tick, there is a small chance PollExecuteTimeQueues() will miss
 * calling it by a few microseconds (see me or Edward for full explanation).
 *
 * To get around this problem, callbacks which are 'slop' microseconds
 * in the future are also fired.
 *
 * Most hosts have a tick period ranging from 10 to 15 milliseconds.
 * My own experiments on W2K host show the time variations between
 * time samples tend to be within 625 microseconds of a full tick.
 * (They tend to always be positive variations, which is good).
 *
 * A slop of 2 milliseconds should be large enough to handle the small
 * variations mentioned above.
 *
 * -- leb
 */

#define POLL_TIME_SLOP  2000 // 2 milliseconds


typedef struct PollEntry {
   struct PollEntry *next;
   int count;			// reference count
   PollClassSet classSet;
   int flags;
   Bool onQueue;		// this entry is on a poll queue
   PollerFunction f;
   void *clientData;
   VmTimeType time;		// valid for POLL_REALTIME

   union {
      uint32 delay;	 // The interval length between periodic callbacks
      PollDevHandle fd;  // fd/handle for POLL_DEVICE events
   } info;
} PollEntry;

#ifdef _WIN32
typedef struct {
   PollEntry *readPollEntry;
   PollEntry *writePollEntry;
   SOCKET     socket;
   HANDLE     event;
   int        refcount;
} ClassEventInfo;

static INLINE void PollClearClassEventInfo(ClassEventInfo *info) {
   info->refcount = 0;
   info->readPollEntry = NULL;
   info->writePollEntry = NULL;
   info->socket = INVALID_SOCKET;
   info->event = NULL;
}

typedef struct SlaveThreadInfo {
   HANDLE threadHandle;
   int tid;
   PollClass class;
   int numEvents;
   int eventIndex;
   ClassEventInfo info[MAX_QUEUE_LENGTH];
} SlaveThreadInfo;

typedef struct SlaveEvents {
   int numSlaves;
   SlaveThreadInfo slaveThreads[POLL_MAX_SLAVE_THREADS];
} SlaveEvents;

typedef struct Slaves {
   int reservedSlaveEvent;
   SlaveEvents slaveEvents[POLL_FIXED_CLASSES];
} Slaves;

static Slaves *slaveState;

typedef struct SocketToEvent {
   SOCKET socket;
   HANDLE eventHandle;
   long networkEvents;
   PollClassSet classSet;
   struct SocketToEvent *next;
} SocketToEvent;

typedef enum PollSlaveEvents {
   POLL_SLAVE_RESUME,
   POLL_SLAVE_UPDATE,
   POLL_SLAVE_EXIT,
   POLL_SLAVE_EVENTS
} PollSlaveEvents;
#else
typedef struct {
   PollEntry *readPollEntry;
   PollEntry *writePollEntry;
   int	      fd;
   int	      events;
} ClassEventInfo;
#endif

typedef struct Poll {
   PollEntry *queue[POLL_NUM_QUEUES];
   PollEntry *free;

   struct ClassEvents {
      int numEvents;
      ClassEventInfo info[MAX_QUEUE_LENGTH];  
   } classEvents[POLL_FIXED_CLASSES];
} Poll;

static Poll *pollState;


static void PollDefaultReset(void);
static Bool PollExecuteQueue(PollEventType typeQueue, PollClass class);
static void PollEntryFree(PollEntry *e, Poll *poll);
static void PollEntryDequeue(PollEventType type, PollEntry **ep);
static Bool PollFireQueue(PollEventType type, PollEntry **queue, int n);
static Bool PollFireAndDequeue(Poll *poll, PollEventType type,
                               PollEntry *e, PollEntry **ep);
static Bool PollExecuteDevice(uint32 timeout, PollClass class);
static VmTimeType PollGetNextTime(PollEventType type, PollClass class);
static Bool PollExecuteTimeQueues(VmTimeRealClock realTime, PollClass class);
static Bool PollIsDeviceDescriptorGood(PollEntry *e);
static void PollDumpDeviceQueue(Poll *poll, PollClass class);

#ifdef _WIN32
static INLINE Bool PollFireAndDequeueSocketEvent(Poll *poll,
                                                 ClassEventInfo *eventInfo);
static HANDLE PollMapSocketToEvent(SOCKET s, PollClassSet classSet,
                                   int pollFlags);
static HANDLE PollUnmapSocketToEvent(SOCKET s, PollClassSet classSet,
                                     int pollFlags);

// List of socket to event object mappings.
static SocketToEvent *socket2EventList;

static int PollStartSlave(PollClass class, int tid);
static void PollStopSlave(int class, int tid);
static DWORD WINAPI PollSlaveThread(LPVOID param);
#endif // _WIN32


static INLINE void
PollFire(PollEntry *e)
{
   (e->f)(e->clientData);
}


/*
 *----------------------------------------------------------------------
 *
 * PollEntryIncrement --
 *
 *      Increment a poll entry's reference count
 *
 *----------------------------------------------------------------------
 */

static INLINE void 
PollEntryIncrement(PollEntry *e) // IN/OUT
{
   ASSERT(e);
   e->count++;
}


/*
 *----------------------------------------------------------------------
 *
 * PollEntryDecrement --
 *
 *      Decrement a poll entry's reference count and destroy
 *      it if the count reaches zero.
 *
 * Side effects
 *
 *      If the entry is destroyed, the caller's pointer to it
 *      is set to NULL.
 *
 *----------------------------------------------------------------------
 */

static INLINE void
PollEntryDecrement(Poll *poll,     // IN 
                   PollEntry **ep) // IN/OUT
{
   PollEntry *e;

   ASSERT(ep);
   e = *ep;
   ASSERT(e);
   ASSERT(e->count);
   if (--e->count <= 0) {
      PollEntryFree(e, poll);
      *ep = NULL;
   }
}


#ifdef _WIN32
/*
 *----------------------------------------------------------------------
 *
 * IsHandleGood --
 *
 *      Compact wrapper for GetHandleInformation just to test a handle.
 *
 * Result: handle good/bad
 *
 * Side effects: none
 *
 *----------------------------------------------------------------------
 */

static INLINE Bool
IsHandleGood(HANDLE h)
{
   DWORD info;
   return GetHandleInformation(h, &info);
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * PollDefaultInit --
 *
 *      Module initialization.
 *
 * Results: void
 *
 * Side effects: poll is alive
 *
 *----------------------------------------------------------------------
 */

static void
PollDefaultInit(void)
{
   ASSERT(pollState == NULL);
   pollState = Util_SafeCalloc(1, sizeof *pollState);

   PollDefaultReset();
}


/*
 *----------------------------------------------------------------------
 *
 * PollDefaultExit --
 *
 *      module de-initalization
 *
 *----------------------------------------------------------------------
 */
static void

PollDefaultExit(void)
{
   Poll *poll = pollState;
   WIN32_ONLY(int s;)
   WIN32_ONLY(int i;)

#ifdef _WIN32
   if (slaveState != NULL) {
      for (i = 0; i < POLL_FIXED_CLASSES; i++) {
         for (s = 0; s < slaveState->slaveEvents[i].numSlaves; s++) {
            if (slaveState->slaveEvents[i].slaveThreads[s].numEvents > 0) {
               PollStopSlave(i, s);
            } 
         }
      }
   }
   free(slaveState);
   slaveState = NULL;
#endif

   PollDefaultReset();                           // destroy queue entries
   WIN32_ONLY(ASSERT(socket2EventList == NULL);) // not used
   free(poll);                                   // free main structure
   pollState = NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * PollDefaultReset --
 *
 *      Clear all entries from all queues.
 *	Useful when forking without execing.
 *
 * Results:
 *      No.
 *
 * Side effects:
 *	No more callbacks until new ones are registered.
 *
 *-----------------------------------------------------------------------------
 */

void
PollDefaultReset(void)
{
   Poll *poll = pollState;
   PollEntry *pe;
   PollEntry *next;
   int i;

   ASSERT(poll != NULL);

   for (i = 0; i < POLL_NUM_QUEUES; i++) {
      for (pe = poll->queue[i]; pe != NULL; pe = next) {
	 next = pe->next;
         ASSERT(pe->count > 0);
	 free(pe);
      }
      poll->queue[i] = NULL;
   }

   for (pe = poll->free; pe != NULL; pe = next) {
      next = pe->next;
      ASSERT(pe->count == 0);
      free(pe);
   }
   
   for (i = 0; i < POLL_FIXED_CLASSES; i++) {
#if !_WIN32
      poll->classEvents[i].numEvents = 0;
#else
      int j;
      poll->classEvents[i].numEvents = 0;   
      for (j = 0; j < MAX_QUEUE_LENGTH; j++) {
         PollClearClassEventInfo(&poll->classEvents[i].info[j]);
      }
#endif
   }

   poll->free = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * PollDefaultLoopTimeout --
 *
 *	The poll loop.
 *	This is supposed to be the main loop for most programs.
 *
 * Result:
 *	Void.
 *
 * Side effects:
 *      Fiat lux!
 *
 *----------------------------------------------------------------------
 */

#define CHECK_EXIT() \
   if (exit && *exit == TRUE) { return; }

static void
PollDefaultLoopTimeout(Bool loop,          // IN: loop forever if TRUE, else do one pass.
                       Bool *exit,         // IN: NULL or set to TRUE to end loop.
                       PollClass class,    // IN: class of events (POLL_CLASS_*)
                       int timeout)        // IN: maximum time to sleep
{
   ASSERT(timeout >= 0);
   ASSERT((class & POLL_CS_BIT) == 0); // Prevent confusion.

   if (exit && *exit == TRUE) {
      Warning("Poll: Asked to return before even starting!\n");
      ASSERT_DEVEL(FALSE);	/* Don't make this an ASSERT_BUG!  -J. */
      return;
   }
      
   do {
      /* Service requested class */
      VmTimeType nextTimeEvent;
      VmTimeType now;
       
      PollExecuteQueue(POLL_MAIN_LOOP, class);
      CHECK_EXIT();

      now = Hostinfo_SystemTimerUS();
      PollExecuteTimeQueues(now, class);
      CHECK_EXIT();

      if (timeout == 0) {
         nextTimeEvent = 0;
      } else if ((nextTimeEvent = PollGetNextTime(POLL_REALTIME, class)) == 0) {
         /* Cannot just use -1 since main-loop callbacks still want to fire. */
         nextTimeEvent = timeout;
      } else {
         nextTimeEvent = MAX(0, nextTimeEvent - now);
         nextTimeEvent = MIN(nextTimeEvent, timeout);
      }
      ASSERT(nextTimeEvent >= 0);
      ASSERT(nextTimeEvent <= 0xffffffff);
      
      PollExecuteDevice((uint32) nextTimeEvent, class);
      CHECK_EXIT();
   } while (loop);
}

#undef CHECK_EXIT


/*
 *----------------------------------------------------------------------
 *
 * PollInsert --
 *
 *      inserted into ordered queue of events.
 *
 *----------------------------------------------------------------------
 */

static void
PollInsert(PollEntry **queue,   // IN-OUT
           PollEntry *e)        // IN
{
   ASSERT(e->count > 0);
   ASSERT(!e->onQueue);
   e->onQueue = TRUE;

   if (*queue && (*queue)->time < e->time) {
      PollEntry *tmp = *queue;
      while (tmp->next && tmp->next->time < e->time) {
         tmp = tmp->next;
      }
      ASSERT(e != tmp->next);
      e->next = tmp->next;
      tmp->next = e;
   } else {
      ASSERT(e != *queue);
      e->next = *queue;
      *queue = e;
   }
}


/*
 *----------------------------------------------------------------------
 *
 * PollEntryFree --
 *
 *      put poll entry on the polling device's free list
 *
 *----------------------------------------------------------------------
 */

static void
PollEntryFree(PollEntry *e, Poll *poll)
{
   ASSERT(e->count == 0);
   ASSERT(!e->onQueue);
   e->next = poll->free;
   poll->free = e;
}


/*
 *----------------------------------------------------------------------
 *
 * PollGetNextTime --
 *
 *	Returns the time the next callback in the given queue wants to
 *	fire.  It's used by monitorEvent.c while the monitor is halted to
 *	determine how much time it can sleep before it needs to process the
 *	time queues.  Note that the function is only meaningful for the two
 *	actual time based queues so it's fatal to call it for other queues.
 *
 * Results:
 *	Head of the requested queue (roughly in the case of real time).
 *
 * Side Effects:
 *	None.
 *----------------------------------------------------------------------
 */

VmTimeType
PollGetNextTime(PollEventType type,	// IN: Head of which queue
                PollClass class)        // IN: Class mask
{
   Poll *poll = pollState;
   PollEntry *queue;
   PollClassSet classSet = 1 << class;

   ASSERT(type == POLL_REALTIME || type == POLL_VTIME);
   queue = poll->queue[type];

   while (queue) {
      if (queue->classSet & classSet) {
         return queue->time;
      }
      queue = queue->next;
   }

   return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * PollDefault_CallbackRemove --
 *
 *      remove a callback from the real-time queue, the virtual time
 *      queue, the file descriptor select set, or the main loop queue.
 *
 * Results:
 *      TRUE if entry found and removed, FALSE otherwise
 *
 * Side effects:
 *      queues modified
 *
 *----------------------------------------------------------------------
 */

static Bool
PollDefaultCallbackRemove(PollClassSet classSet,
	                  int flags,
		          PollerFunction f,
		          void *clientData,
		          PollEventType type)
{
   Poll *poll = pollState;
   PollEntry *e, **ep;

   ASSERT(poll);
   ASSERT(type >= 0 && type < POLL_NUM_QUEUES);

   if (type == POLL_DEVICE) {
      /*
       * When neither flag is passed, default to READ.
       */
      if ((flags & (POLL_FLAG_READ|POLL_FLAG_WRITE)) == 0) {
	 flags |= POLL_FLAG_READ;
      }
   }

   for (ep = &poll->queue[type]; (e = *ep) != NULL; ep = &e->next) {
      if (e->f == f && e->clientData == clientData &&
          e->classSet == classSet && e->flags == flags) {
	 PollEntryDequeue(type, ep);
	 return TRUE;
      }
   }
   return FALSE;
}

/*
 *-----------------------------------------------------------------------------
 *
 * PollEntryDequeue --
 *
 *      Remove an entry from a poll queue.
 *
 * Results:
 *      No
 *
 * Side effects:
 *	Alter the queue, free the entry,
 *	and possibly change the poll table for the POLL_DEVICE queue.
 *
 *-----------------------------------------------------------------------------
 */

void
PollEntryDequeue(PollEventType type,
		 PollEntry **ep)
{
   Poll *poll = pollState;
   PollEntry *e = *ep;
   int i;
   
   *ep = e->next;
   e->next = NULL;
   ASSERT(e->onQueue);
   e->onQueue = FALSE;
   if (type == POLL_DEVICE) {

#ifdef _WIN32
      HANDLE eventHandle;
      eventHandle = (e->flags & POLL_FLAG_SOCKET) ?
         PollUnmapSocketToEvent((SOCKET)e->info.fd, e->classSet, e->flags) 
         : (HANDLE)e->info.fd;
#endif

      for (i = 0; i < POLL_FIXED_CLASSES; i++) {
         if (e->classSet & (1 << i)) {
#if !_WIN32
            struct ClassEvents *classEvents = &poll->classEvents[i];
	    ClassEventInfo *eventInfo;
            int k;

            ASSERT(classEvents->numEvents);

            for (k = 0; k < classEvents->numEvents &&
                        classEvents->info[k].fd != e->info.fd; k++);

            ASSERT(k < classEvents->numEvents); // better find it

	    eventInfo = &classEvents->info[k];

            ASSERT(eventInfo->readPollEntry == e || eventInfo->writePollEntry == e );
            ASSERT(eventInfo->events);

	    if (eventInfo->readPollEntry == e) {
	       eventInfo->events &= ~POLLIN;
	       eventInfo->readPollEntry = NULL;
	    }
	    if (eventInfo->writePollEntry == e) {
	       eventInfo->events &= ~POLLOUT;
	       eventInfo->writePollEntry = NULL;
	    }
	    if (eventInfo->events == 0) {
	       classEvents->numEvents--;
	       if (k < classEvents->numEvents) {
		  *eventInfo = classEvents->info[classEvents->numEvents];
	       }
	    }
#else
            struct ClassEvents *classEvents = &poll->classEvents[i];
            ClassEventInfo *eventInfo = NULL;
            int k, j;
            Bool slaveEvent = FALSE;
            SlaveThreadInfo *slaveThread;

            ASSERT(classEvents->numEvents);

            /* Search through the parent poll thread event list for the 
               matching event info */
            for (k = 0; k < classEvents->numEvents &&
                        classEvents->info[k].event != eventHandle; k++);

            ASSERT(k < classEvents->numEvents || slaveState != NULL);

            if (k < classEvents->numEvents) {

               /* Grab the event info from the parent poll thread's list */
               eventInfo = &classEvents->info[k];
               ASSERT(eventInfo != NULL);
            } else if (slaveState != NULL) {
               int s;

               /* Event info is in one of our slaves' lists */
               for (s = 0; s < slaveState->slaveEvents[i].numSlaves; s++) {
                  slaveThread = &slaveState->slaveEvents[i].slaveThreads[s];
                  slaveEvent = TRUE;

                  /* search the slave's list */
                  for (k = POLL_SLAVE_EVENTS; k < slaveThread->numEvents &&
                           slaveThread->info[k].event != eventHandle; k++);
                  if (k < slaveThread->numEvents) {

                     /* found it */
                     eventInfo = &slaveThread->info[k];
                     ASSERT(eventInfo != NULL);
                     break;
                  }
               }
            }

            ASSERT(eventInfo != NULL); // better find it

            ASSERT(eventInfo->readPollEntry == e || eventInfo->writePollEntry == e );
            ASSERT(eventInfo->refcount);

            if (eventInfo->readPollEntry == e) {
               PollEntryDecrement(poll, &e);
               ASSERT(e); // at least one reference to it remains
               eventInfo->readPollEntry = NULL;
               eventInfo->refcount --;
            }
            if (eventInfo->writePollEntry == e) {
               PollEntryDecrement(poll, &e);
               ASSERT(e); // at least one reference to it remains
               eventInfo->writePollEntry = NULL;
               eventInfo->refcount --;
            }

            // If no more poll entries are registered, delete this classEvent entry
            if (eventInfo->refcount == 0) {
               if (slaveEvent == TRUE) {
                  if (k == slaveThread->eventIndex) {
                     slaveThread->eventIndex = INVALID_EVENT_INDEX;
                  } else if (slaveThread->eventIndex  > k) { 
                     slaveThread->eventIndex--;
                  }
                  for (j = k; j < slaveThread->numEvents - 1; j++) {
                     slaveThread->info[j] = slaveThread->info[j + 1];
                  }  
                  PollClearClassEventInfo(&slaveThread->info[j]);
                  slaveThread->numEvents--;
               } else {
                  for (j = k; j < classEvents->numEvents - 1; j++) {
                     classEvents->info[j] = classEvents->info[j + 1];
                  } 
                  PollClearClassEventInfo(&classEvents->info[j]);
                  classEvents->numEvents--;
               }
            }
#endif            
         }
      }
   }
   PollEntryDecrement(poll, &e); // might be the last reference
}


/*
 *----------------------------------------------------------------------
 *
 * PollDefault_Callback --
 *
 *      Insert a callback into one of the queues (e.g., the real-time
 *      queue, the virtual time queue, the file descriptor select
 *      set, or the main loop queue).
 *
 *      For the POLL_REALTIME or POLL_DEVICE queues, entries can be
 *      inserted for good, to fire on a periodic basis (by setting the
 *      POLL_FLAG_PERIODIC flag).
 *
 *      Otherwise, the callback fires only once.
 *
 *	For periodic POLL_REALTIME callbacks, "info" is the time in
 *	microseconds between execution of the callback.  For
 *	POLL_DEVICE callbacks, info is a file descriptor.
 *
 *
 *----------------------------------------------------------------------
 */

static VMwareStatus
PollDefaultCallback(PollClassSet classSet,
	            int flags,
	            PollerFunction f,
                    void *clientData,
                    PollEventType type,
                    PollDevHandle info,
	            struct DeviceLock *lock)
{
   VMwareStatus result;
   Poll *poll = pollState;
   PollEventType typeQueue = type;
   PollEntry *e;
   int i;
   
   ASSERT(f);
   ASSERT(lock == NULL);

   ASSERT_BUG(5315, poll != NULL);

   LOG(3, ("POLL: inserting callback %p(%p), type 0x%x, %s = %"FMTH"d\n",
	   f, clientData, type, (typeQueue == POLL_DEVICE) ? "fd": "delay",
           info));

   ASSERT_NOT_IMPLEMENTED(typeQueue != POLL_VIRTUALREALTIME);
   ASSERT_NOT_IMPLEMENTED(typeQueue != POLL_VTIME);

   /*
    * On Linux, POLL_FLAG_READ/WRITE makes sense only with POLL_DEVICE.
    * On Win32, POLL_FLAG_READ/WRITE makes sense only with POLL_DEVICE and
    *           POLL_FLAG_SOCKET.
    *
    * A lot of common code (Poll_CB_Device(), ...) uses
    * POLL_FLAG_READ/WRITE with POLL_DEVICE but without POLL_FLAG_SOCKET on
    * Win32. We should fix all call sites, but in the meantime, I'm just making
    * the Win32 assert more lenient (i.e. I only enforce one direction of the
    * equivalence) --hpreg
    */
#if !_WIN32
   ASSERT(   (type == POLL_DEVICE)
          == ((flags & (POLL_FLAG_READ | POLL_FLAG_WRITE)) != 0));
#else
   //ASSERT(   (type == POLL_DEVICE && flags & POLL_FLAG_SOCKET)
   //       == ((flags & (POLL_FLAG_READ | POLL_FLAG_WRITE)) != 0));
   ASSERT(   !(type == POLL_DEVICE && flags & POLL_FLAG_SOCKET)
          || (flags & (POLL_FLAG_READ | POLL_FLAG_WRITE)) != 0);
#endif

   /*
    * POLL_FLAG_READ and POLL_FLAG_WRITE are mutually exclusive. Nobody really
    * knows why. It could be that in the past, the data structures did not
    * support it.
    */
   ASSERT((flags&(POLL_FLAG_READ|POLL_FLAG_WRITE))
          != (POLL_FLAG_READ|POLL_FLAG_WRITE));
   
   /* Make sure caller passed POLL_CS instead of POLL_CLASS. */
   ASSERT(classSet & POLL_CS_BIT);

   /* For now, only allow POLL_CS_MAIN for time events. */
   ASSERT_NOT_IMPLEMENTED(classSet == POLL_CS_MAIN || type != POLL_REALTIME);

   /* Every callback must be in POLL_CLASS_MAIN (plus possibly others) */
   ASSERT((classSet & 1 << POLL_CLASS_MAIN) != 0);

   if (poll->free) {
      e = poll->free;
      poll->free = e->next;
      e->next = NULL;
   } else {
      e = (PollEntry *)calloc(1, sizeof(PollEntry));
      ASSERT_MEM_ALLOC(e);
      e->next = NULL;
   }
   ASSERT(e->count == 0);
   PollEntryIncrement(e);

   e->f          = f;
   e->clientData = clientData;
   e->classSet	 = classSet;
   e->flags	 = flags;

   ASSERT(typeQueue >= 0 && typeQueue < POLL_NUM_QUEUES);
   switch(typeQueue) {

   case POLL_REALTIME:
      e->info.delay = (flags & POLL_FLAG_PERIODIC) ? info : 0;
      e->time = info + Hostinfo_SystemTimerUS();
      ASSERT_BUG(2430, info >= 0);
      ASSERT_BUG(1319, e->time > 0);
      break;

   case POLL_DEVICE: {
      WIN32_ONLY( HANDLE eventHandle; )

      /*
       * When neither flag is passed, default to READ.
       */
      if ((flags & (POLL_FLAG_READ | POLL_FLAG_WRITE)) == 0) {
	 e->flags |= POLL_FLAG_READ;
      }
      /* info is a file descriptor/handle */
      e->info.fd = info;
      e->time = 0;		//unused field
#ifdef _WIN32
      if (flags & POLL_FLAG_SOCKET) {
         eventHandle = PollMapSocketToEvent((SOCKET)info,
					    e->classSet, e->flags);
      } else {
         ASSERT(!(flags & POLL_FLAG_WRITE));
	 eventHandle = (HANDLE)info;
      }
#endif

      ASSERT(PollIsDeviceDescriptorGood(e));

      for (i = 0; i < POLL_FIXED_CLASSES; i++) {
         if (classSet & (1 << i)) {
#if !_WIN32
            int num;
	    ClassEventInfo *eventInfo;
            Bool newInThisClass;

            // search the class list for an entry with matching event object
            for (num = 0; num < poll->classEvents[i].numEvents &&
               poll->classEvents[i].info[num].fd != info; num++);

            eventInfo = &poll->classEvents[i].info[num];
            newInThisClass = num == poll->classEvents[i].numEvents;

            if (newInThisClass) {
	       if (num >= MAX_QUEUE_LENGTH) {
                  Warning("POLL: reached limit of %d events in class %d\n", num, i); 
		  result = VMWARE_STATUS_INSUFFICIENT_RESOURCES;
		  goto out;
	       }
	       eventInfo->fd = info;
	       eventInfo->events = 0;
	       eventInfo->readPollEntry = NULL;
	       eventInfo->writePollEntry = NULL;
	       poll->classEvents[i].numEvents++;
            }
            if (e->flags & POLL_FLAG_WRITE) {
               /* 
                * Assert that a maximum of one callback may be registered
                * for a given network event on a given socket.
                */
               ASSERT(!(eventInfo->events & POLLOUT));
	       ASSERT(!eventInfo->writePollEntry);
	       eventInfo->events |= POLLOUT;
	       eventInfo->writePollEntry = e;
            }
            if (e->flags & POLL_FLAG_READ) {
               ASSERT(!(eventInfo->events & POLLIN));
	       ASSERT(!eventInfo->readPollEntry);
	       eventInfo->events |= POLLIN;
	       eventInfo->readPollEntry = e;
            }

#else

            int num, s;
            ClassEventInfo *eventInfo = NULL;

            // search the class list for an entry with matching event object
            for (num = 0; num < poll->classEvents[i].numEvents &&
               poll->classEvents[i].info[num].event != eventHandle; num++);
            
            if (num == poll->classEvents[i].numEvents) {
               if (slaveState != NULL) {
                  for (s = 0; s < slaveState->slaveEvents[i].numSlaves; s++) {
                     SlaveThreadInfo *slaveThread = 
                        &slaveState->slaveEvents[i].slaveThreads[s];
                     int n;
                     for (n = POLL_SLAVE_EVENTS; 
                          n < slaveThread->numEvents &&
                             slaveThread->info[n].event != eventHandle;
                          n++);
                     if (n < slaveThread->numEvents) {
                        eventInfo = &slaveThread->info[n];
                        break;
                     }
                  }
               }  
            } else {
               eventInfo = &poll->classEvents[i].info[num];
            }

            if (eventInfo == NULL) {
               Bool foundSpace = FALSE;

	       if (slaveState != NULL && 
                     num >= slaveState->reservedSlaveEvent) {
                  SlaveThreadInfo *slaveThread;
                  for (s = 0; s < slaveState->slaveEvents[i].numSlaves &&
                          eventInfo == NULL; s++) {
                     slaveThread = &slaveState->slaveEvents[i].slaveThreads[s];
                     if (slaveThread->numEvents < MAX_QUEUE_LENGTH) {
                        if ( slaveThread->numEvents == 0) {
                           int ret = PollStartSlave(i,s);
                           if (ret != 0) {
                              result = ret;
                              goto out;
                           }
                        }
                        eventInfo = &slaveThread->info[slaveThread->numEvents];
                        eventInfo->event = eventHandle;
                        slaveThread->numEvents++;
                        if( SetEvent(slaveThread->info[POLL_SLAVE_UPDATE].event) == FALSE) {
                           Log("POLL slave thread bad return value from SetEvent, error %d (%s) event %d\n", 
                               GetLastError(), Err_ErrString(),
                               slaveThread->info[POLL_SLAVE_UPDATE].event);
                           ASSERT(FALSE);
                        }
                     }
                  }
               } else if (num < MAX_QUEUE_LENGTH) {
                  eventInfo = &poll->classEvents[i].info[num];
                  poll->classEvents[i].info[num].event = eventHandle;
                  poll->classEvents[i].numEvents++;   
               }
               
               if (eventInfo == NULL) {
                  if (e->flags & POLL_FLAG_SOCKET) {
                     PollUnmapSocketToEvent((SOCKET)e->info.fd,
					    e->classSet, e->flags);
                  }
                  memset(e, 0, sizeof(*e));
		  PollEntryFree(e, poll);
                  Warning("%s:%d reached array limit\n", __FILE__, __LINE__); 
		  result = VMWARE_STATUS_INSUFFICIENT_RESOURCES;
                  goto out;
	       }

               // Verify the entry is properly initialized
               ASSERT_BUG_DEBUGONLY(9916, eventInfo->socket == INVALID_SOCKET &&
                                          eventInfo->readPollEntry == NULL &&
                                          eventInfo->writePollEntry == NULL &&
                                          eventInfo->refcount == 0);
               
               if (flags & POLL_FLAG_SOCKET) {
                  eventInfo->socket = (SOCKET)info;
               }
            } else {
               ASSERT(eventInfo->event == eventHandle);
               ASSERT(eventInfo->refcount == 1);
            }

            if (e->flags & POLL_FLAG_READ) {
               ASSERT(eventInfo->readPollEntry == NULL);
               PollEntryIncrement(e);
	       eventInfo->readPollEntry = e;
               eventInfo->refcount ++;
            }
            if (e->flags & POLL_FLAG_WRITE) {
               // write-ready events only supported on sockets
               ASSERT(eventInfo->socket != INVALID_SOCKET);
               ASSERT(eventInfo->writePollEntry == NULL);
               PollEntryIncrement(e);
	       eventInfo->writePollEntry = e;
               eventInfo->refcount ++;
            }
            ASSERT(eventInfo->refcount >= 1 && eventInfo->refcount <= 2);
#endif
         }
      }

      break;
   }
      
   case POLL_MAIN_LOOP:
      ASSERT(info == 0);
      e->info.fd = (PollDevHandle) VMW_INVALID_HANDLE;   /* unused field */
      e->time = 0;                                       /* unused field */
      break;

   default:
      NOT_REACHED();
   }

   if (typeQueue == POLL_REALTIME) {
      /* The queue is ordered, insert in the right place */
      PollInsert(&poll->queue[typeQueue], e);
   } else {
      /* The other queues are unordered, so just insert in front */
      e->next = poll->queue[typeQueue];
      poll->queue[typeQueue] = e;
      e->onQueue = TRUE;
   }

   result = VMWARE_STATUS_SUCCESS;

out:
   return result;
}


/*
 *----------------------------------------------------------------------
 *
 * PollExecuteQueue --
 *
 *	Fire all the events in a queue and
 *	dequeue the one-time entries.
 *
 * Result:
 *      TRUE if a callback was fired, else FALSE.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
PollExecuteQueue(PollEventType typeQueue,
                 PollClass class)
{
   Poll *poll = pollState;
   PollClassSet classSet = 1 << class;
   PollEntry *queue[MAX_QUEUE_LENGTH];
   PollEntry *e;
   int n;

   ASSERT(class >= 0);
   ASSERT(class < POLL_FIXED_CLASSES);
   ASSERT(typeQueue >= 0);
   ASSERT(typeQueue < POLL_NUM_QUEUES);

   n = 0;
   for (e = poll->queue[typeQueue]; e != NULL; e = e->next) {
      if ((e->classSet & classSet) != 0) {
         /*
          * Add to temporary queue; increment will be balanced by
          * decrement in PollFireQueue
          */
	 ASSERT(n < MAX_QUEUE_LENGTH);
	 queue[n++] = e;
	 PollEntryIncrement(e);
      }
   }

   return n > 0 && PollFireQueue(typeQueue, queue, n);
}


/*
 *----------------------------------------------------------------------
 *
 * PollFireRealtimeCallback --
 *
 *	Dequeue and fire a specified entry on the real time queue.
 *      A periodic entry is re-inserted into the queue.
 *
 * Result:
 *      TRUE if a callback was fired, else FALSE.
 *
 * Side effects:
 *      Many. In particular, the callback may lead to a recursive
 *      call into this function.
 *
 *----------------------------------------------------------------------
 */

static INLINE Bool
PollFireRealtimeCallback(Poll *poll,            // IN: Poll struct
                         PollEntry *e,          // IN: entry to fire
               	         PollEntry *previous,   // IN: previous entry in queue
			 VmTimeType realTime)   // IN: current real time
{
   Bool fired = FALSE;

   ASSERT(e->count > 0);
   ASSERT(e->onQueue);

   /* First, dequeue the entry */
   e->onQueue = FALSE;
   if (previous) {
      previous->next = e->next;
   } else {
      ASSERT(e == poll->queue[POLL_REALTIME]);
      poll->queue[POLL_REALTIME] = e->next;
   }

   if ((e->flags & POLL_FLAG_PERIODIC) != 0) {
       fired = TRUE;
       ASSERT(e->info.delay > 0);
       e->time = realTime + e->info.delay;

       PollInsert(&poll->queue[POLL_REALTIME], e);
       e->count++;
       PollFire(e);
       if (--e->count <= 0) {
          PollEntryFree(e, poll);
       }

   } else {
      LOG(3, ("POLL: executing realtime callback %p(%p)\n",
	  e->f, e->clientData));
      fired = TRUE;
      PollFire(e);
      if (--e->count <= 0) {
	 PollEntryFree(e, poll);
      }
   }

   return fired;
}


/*
 *----------------------------------------------------------------------
 *
 * PollExecuteTimeQueues --
 *
 *	Walks the Virtual and Real time queues and fires all the
 *	of the registered callbacks that need to fire at or by the current
 *	(passed in) virtual or real time.  It then re-registers any
 *	periodic callbacks so that they will fire again in the future.
 *
 *	Note: Callbacks only fire once per call to this function, even if
 *	sufficient time has elapsed for them to rightfully have fired more
 *	than once.
 *
 * Result:
 *      TRUE if a callback was fired, else FALSE.
 *
 * Side effects:
 *      Time-based callbacks can be fired.
 *
 *----------------------------------------------------------------------
 */

static Bool
PollExecuteTimeQueues(VmTimeRealClock realTime,
                      PollClass class)
{
   Poll *poll = pollState;
   Bool fired = FALSE;
   Bool found;
   PollClassSet classSet = 1 << class;

   ASSERT(poll->queue[POLL_VTIME] == NULL);

   /*
    * Fire all the realtime callbacks.
    *
    * Periodic callbacks that are within POLL_TIME_SLOP microseconds in the
    * future are also eligible for firing.
    * This code is tricky in that it can be recursively invoked by the callback,
    * i.e., the queue can change as a side effect of firing.
    *
    * To handle this, two nested loops are used. The inner searches the time
    * queue for the first eligible callback. If found, the callback is fired,
    * we break out of the inner loop, and let the outer loop initiate a new
    * search beginning from the head.
    * If none is found, the outer loop terminates.
    */

   do {
      PollEntry *e        = poll->queue[POLL_REALTIME]; 
      PollEntry *previous = NULL;
      found               = FALSE;

      while (e != NULL && e->time <= realTime + POLL_TIME_SLOP) {
         if ((e->time <= realTime || (e->flags & POLL_FLAG_PERIODIC) != 0) &&
	     (e->classSet & classSet)) {
            found = TRUE;
            if (PollFireRealtimeCallback(poll, e, previous, realTime)) {
               fired = TRUE;
            }
            break;
         } else {
	    previous = e;
            e = e->next;
         }
      }
   } while (found);

   return fired;
}


/*
 *-----------------------------------------------------------------------------
 *
 * PollFireQueue --
 *
 *      Fire events of type TYPE on array QUEUE with length N.
 *	Remove non-periodic entries from queue before firing.
 *
 * Results:
 *      TRUE if at least one callback fired. FALSE otherwise.
 *      In practice, should always return TRUE because the first
 *      entry in the list is on the queue at firing time.
 *
 * Side effects:
 *	The callbacks may do anything, including calling
 *	Poll_Loop or PollExecuteDevice reentrantly.
 *
 *-----------------------------------------------------------------------------
 */

Bool
PollFireQueue(PollEventType type,
	      PollEntry **queue,
	      int n)
{
   Bool fired = FALSE;
   Poll *poll = pollState;
   int i;
   PollEntry *e;

   for (i = 0; i < n; i++) {
      e = queue[i];
      ASSERT(e->count > 0);
      if (PollFireAndDequeue(poll, type, e, NULL)) {
         fired = TRUE;
      }
      /*
       * Balance the increment from PollExecuteQueue or PollExecuteDevice
       */
      PollEntryDecrement(poll, &e);
   }
   return fired;
}


/*
 *----------------------------------------------------------------------
 *
 * PollFireAndDequeue --
 *
 *   If a poll entry is on a poll queue, fire it. Before firing, the
 *   entry is removed from the queue if it is non-periodic.
 *
 * Result:
 *
 *   TRUE if the entry was on the queue and was fired. FALSE otherwise
 *
 * Side effects:
 *
 *   The entry may be dequeued and destroyed.
 *
 *----------------------------------------------------------------------
 */

static Bool
PollFireAndDequeue(Poll *poll,           // IN
                   PollEventType type,   // IN
                   PollEntry *e,         // IN
                   PollEntry **ep)       // IN (ok to pass NULL)
{
   Bool fired = FALSE;

   ASSERT(e);
   ASSERT(e->count > 0);

   if (!e->onQueue) {
      goto out;
   }

   /*
    * Maintain a reference on e while firing; the callback might remove it
    * and we don't want it to totally disappear till we're ready.
    */
   PollEntryIncrement(e);

   if ((e->flags & POLL_FLAG_PERIODIC) == 0) {
      /* 
       * Dequeue the entry before firing. If the caller didn't provide
       * a pointer to the previous entry's 'next' field, compute it now.
       */
      if (ep == NULL) {
         for (ep = &poll->queue[type];
              *ep != NULL && *ep != e; 
              ep = &(*ep)->next);

         ASSERT(*ep == e); // must find
      }
      ASSERT(*ep == e);
      PollEntryDequeue(type, ep);
   }
   
   PollFire(e);
   fired = TRUE;

   if ((e->flags & POLL_FLAG_PERIODIC) == 0) {
      ASSERT(!e->next);
   }

   /*
    * Balance the increment above.  Note that this will destroy e if it's
    * already been dequeued.
    */
   if (e->count == 1) {
      ASSERT(!e->onQueue);
   }
   PollEntryDecrement(poll, &e);

 out:
   return fired;
}


/*
 *----------------------------------------------------------------------
 *
 * PollExecuteDevice --
 *
 *      "poll()" on the relevant file descriptors and fire
 *      the appropriate callbacks.
 *
 * Result: whether we fired anything
 *
 * Side effects: everything.
 *
 *----------------------------------------------------------------------
 */

#if !_WIN32
#define POLLINREADY	(POLLIN|POLLHUP|POLLERR)
#define POLLOUTREADY	(POLLOUT|POLLHUP|POLLERR)

Bool
PollExecuteDevice(uint32 timeout, // IN microsecs
                  PollClass class)
{
   Poll *polltab = pollState;
   int nfds;
   struct pollfd pollFds[MAX_QUEUE_LENGTH];
   ClassEventInfo *ents;
   int retval;
   int idx;
   Bool fired = FALSE;

   ASSERT(0 <= class && class < POLL_FIXED_CLASSES);

   /*
    * Make a copy of the fds to poll on -- the code already did this; now
    * it's extra necessary since other threads are allowed to be in Poll code
    * while we're sleeping in poll().  -- MattG
    *
    * Cut out early if nothing to poll on and not sleepy.
    */

   if ((nfds = polltab->classEvents[class].numEvents) == 0 && timeout == 0) {
      goto out;
   }
   ASSERT(nfds <= MAX_QUEUE_LENGTH);
   ents = polltab->classEvents[class].info;
   for (idx = 0; idx < nfds; idx++) {
      pollFds[idx].fd = ents[idx].fd;
      pollFds[idx].events = ents[idx].events;
   }
  
   retval = poll(pollFds, nfds, CEILING(timeout, 1000));

   /*
    * Handle devices that are ready.
    *
    * EBADF means some internal screwup that we will likely loop on.
    * We otherwise don't care about errors.
    */

   if (retval <= 0) {
      if (retval < 0 && errno == EBADF) {
	 int i;
	 for (i = 0; i < nfds; i++) {
	    Log("POLL fds %d: %c%c\n", pollFds[i].fd,
		(pollFds[i].events & POLLINREADY) ? 'r' : '-',
		(pollFds[i].events & POLLOUTREADY) ? 'w' : '-');
	 }
         PollDumpDeviceQueue(polltab, class);
	 NOT_REACHED_BUG(5543);
      }

   } else if (retval == 1) {
      int i;

      for (i = 0; i < nfds; i++) {
	 if ((pollFds[i].revents & POLLINREADY) && ents[i].readPollEntry) {
	    if ((pollFds[i].revents & POLLOUTREADY) && ents[i].writePollEntry) {
	       if (ents[i].writePollEntry != ents[i].readPollEntry) {
		  PollEntry *queue[2];

		  queue[0] = ents[i].readPollEntry;
		  queue[1] = ents[i].writePollEntry;
		  PollEntryIncrement(queue[0]);
		  PollEntryIncrement(queue[1]);
      		  fired = PollFireQueue(POLL_DEVICE, queue, 2);
	       } else {
		  PollFireAndDequeue(polltab, POLL_DEVICE, ents[i].readPollEntry, NULL);
	          fired = TRUE;
	       }
	    } else {
	       PollFireAndDequeue(polltab, POLL_DEVICE, ents[i].readPollEntry, NULL);
	       fired = TRUE;
	    }
	    break;
	 } else if ((pollFds[i].revents & POLLOUTREADY) && ents[i].writePollEntry) {
	    PollFireAndDequeue(polltab, POLL_DEVICE, ents[i].writePollEntry, NULL);
	    fired = TRUE;
	    break;
	 } else {
	    ASSERT(!pollFds[i].revents);
	 }
      }
      ASSERT(fired);
   } else {
      PollEntry *queue[MAX_QUEUE_LENGTH];
      int n, i;

      n = 0;
      for (i = 0; i < nfds; i++) {
	 if ((pollFds[i].revents & POLLINREADY) && ents[i].readPollEntry) {
	    ASSERT_BUG(12400, n < MAX_QUEUE_LENGTH);
	    queue[n++] = ents[i].readPollEntry;
	    PollEntryIncrement(ents[i].readPollEntry);
	    if ((pollFds[i].revents & POLLOUTREADY) && ents[i].writePollEntry) {
	       if (ents[i].writePollEntry != ents[i].readPollEntry) {
	    	  ASSERT_BUG(12400, n < MAX_QUEUE_LENGTH);
		  queue[n++] = ents[i].writePollEntry;
		  PollEntryIncrement(ents[i].writePollEntry);
	       }
	    }
	    /*
	     * Do not take shortcut path for debug build
	     */
#ifdef VMX86_DEBUG
	    --retval;
#else
	    if (!--retval) {
	       break;
	    }
#endif
	 } else if ((pollFds[i].revents & POLLOUTREADY) && ents[i].writePollEntry) {
	    ASSERT_BUG(12400, n < MAX_QUEUE_LENGTH);
	    queue[n++] = ents[i].writePollEntry;
	    PollEntryIncrement(ents[i].writePollEntry);
	    /*
	     * Do not take shortcut path for debug build
	     */
#ifdef VMX86_DEBUG
	    --retval;
#else
	    if (!--retval) {
	       break;
	    }
#endif
	 } else {
	    ASSERT(!pollFds[i].revents);
	 }
      }

      ASSERT(!retval);
      ASSERT(n);
      fired = PollFireQueue(POLL_DEVICE, queue, n);
      ASSERT(fired); // because at least one entry (the first one) was on the queue
   }

out:
   return fired;
}

#else // --------- Win32 specific code start -----------

Bool
PollExecuteDevice(uint32 timeout,       // IN microsecs
                  PollClass class)      // IN
{
   Poll *poll = pollState;
   int i, s, retval, startSlave;
   int totalEvents;
   Bool fired = FALSE;
   struct ClassEvents *classEvents = &poll->classEvents[class];
   struct SlaveEvents *slaveEvents = 
      slaveState == NULL ? NULL : &slaveState->slaveEvents[class];
   void* events[MAX_QUEUE_LENGTH];
   MSG msg;
   DWORD msTimeout; /* Number of ms to wait */
   int reservedSlaveEvent = slaveState == NULL ? -1 : 
                               slaveState->reservedSlaveEvent;
   
   /*
    * Cut out early if nothing to wait on and not sleepy.
    */

   if (classEvents->numEvents == 0 && timeout == 0) {
      return FALSE;
   }

   ASSERT(classEvents->numEvents > 0 || class == POLL_CLASS_MAIN);

   /*
    * Make a copy of the events to wait on since other threads are allowed
    * to be in Poll code while we're sleeping in MsgWaitFor...().  -- MattG
    */
   
   totalEvents = classEvents->numEvents;
   for (i = 0; i < totalEvents; i++) {
      events[i] = classEvents->info[i].event;
   }

   /* Add in the special events that monitor the slave threads. */
   startSlave = i;
   if (slaveState != NULL) {
      for (s = 0; s < slaveEvents->numSlaves && 
             slaveEvents->slaveThreads[s].numEvents > 0; s++) {
         events[i] = classEvents->info[reservedSlaveEvent + s].event;
         i++;
         totalEvents++;
      } 
   }

   ASSERT(totalEvents < MAX_QUEUE_LENGTH);

   /*
    * Calculate delay in ms before timeout:
    *
    * We use CEILING() instead of a simple division in order to effectively
    * block when timeout is in [1; 999], even if we block for a little too
    * long (1 ms.). This prevents us from busily looping in
    * Poll_LoopTimeout() --hpreg
    */

   msTimeout = CEILING(timeout, 1000);
   
   /*
    * Now we do some form of MsgWaitForMultipleObjects on the active events.
    * May or may not also check for Windows messages, depending on pump, but:
    *
    * Our threads like to block on poll.  However, all good windows
    * citizens need to periodically pump their message queue, or bad
    * things start happening.  Specifically, some messages when sent
    * require a rendezvous with the receiver, which happens when the
    * receiver checks its message queue, and the sender blocks till
    * this happens.  So the UI/MKS need to be alert to messages when
    * they arrive, even if it's not a good time to process them.
    *
    * So, our M.O. is as follows: always call MsgWaitForMultipleObjects.
    * In threads that receive Windows messages (UI and MKS), call
    * PeekMessage(PM_NOREMOVE) to notice the message but leave it
    * in the queue to be processed later.  In other threads, assume
    * they shouldn't be getting messages anyway so just drop
    * any that they do get.
    *
    * After calling MsgWaitForMultiple, if it returns WAIT_IO_COMPLETION,
    * don't update any data structures because an asynchronous I/O completion
    * routine may have fired, potentially modifying poll state.
    * See the comment for WAIT_IO_COMPLETION below.
    */

   retval = MsgWaitForMultipleObjectsEx(totalEvents, events, 
					msTimeout, QS_ALLINPUT,
					MWMO_ALERTABLE);

   /*
    * Figure out which event fired:
    */

   if ((retval >= WAIT_OBJECT_0) && (retval < WAIT_OBJECT_0 + totalEvents)) {
      /*
       * Event fired
       */
      int iEvent = retval - WAIT_OBJECT_0;
      SlaveThreadInfo *slaveThread;
      ClassEventInfo *eventInfo = &classEvents->info[iEvent];

      ASSERT(iEvent < totalEvents);

      if (iEvent >= startSlave) {
         int slave = iEvent - startSlave;
         slaveThread = &slaveEvents->slaveThreads[slave];
         eventInfo = slaveThread->eventIndex == INVALID_EVENT_INDEX ? NULL :
            &slaveThread->info[slaveThread->eventIndex];
      }

      /* 
       * Slave thread's event might have already been invalidated
       * by PollDefaultCallbackRemove
       */
      if (eventInfo == NULL) {
         ASSERT(iEvent >= startSlave);
      } else {
         if (eventInfo->socket != INVALID_SOCKET) {
            fired = PollFireAndDequeueSocketEvent(poll, eventInfo);
         } else {
	    ASSERT(eventInfo->writePollEntry == NULL);
	    fired = PollFireAndDequeue(poll, POLL_DEVICE,
                                       eventInfo->readPollEntry, NULL);
         }
      } 

      if (iEvent >= startSlave) {
         slaveThread->eventIndex = INVALID_EVENT_INDEX;
         if( SetEvent(slaveThread->info[POLL_SLAVE_RESUME].event) == FALSE) {
            Log("POLL slave thread bad return value from SetEvent, " 
                "error %d (%s) event %d\n", GetLastError(), Err_ErrString(),
               slaveThread->info[POLL_SLAVE_RESUME].event);
            ASSERT(FALSE);
         }
      }

   } else if (retval == WAIT_OBJECT_0 + totalEvents) {
      /*
       * Event fired: Windows message.
       */

      /*
       * Currently only the UI and MKS threads *process* windows messages.
       * But they might not be the only threads to *receive* such messages:
       * MsgWaitForMultipleObjects() sometimes returns spuriously,
       * indicating that a message is pending when in fact there is none.
       * Handle this case by ignoring Windows messages for threads
       * that shouldn't get any.
       */
      switch (VThread_CurID()) {
      case VTHREAD_UI_ID:
      case VTHREAD_MKS_ID:
	 PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE);
	 break;
      default:
	 if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
	    Warning("Ignoring windows message posted to non-UI thread."
		    " hwnd %d msg %d wp %08x lp %08x\n",
		    msg.hwnd, msg.message, msg.wParam, msg.lParam);
	 }
      }
   } else if (retval == WAIT_TIMEOUT) {
      // nothing signaled and nothing to do
   } else if (retval == WAIT_IO_COMPLETION) {
      /*
       * An asynchronous I/O completion routine fired. Poll may have been
       * re-entered and the poll state modified underneath us,
       * so don't touch any data structures and return.
       */
   } else {
      /*
       * The spiritual cousin of bug 5543: waited on a bad handle.
       */
      int i;
      Log("POLL bad return value %d from WaitForMultiple, error %d (%s)\n",
	  retval, GetLastError(), Err_ErrString());
      Log("POLL timeout %d totalEvents %d\n", timeout, totalEvents);
      Log("POLL waited on events:\n");
      for (i = 0; i < totalEvents; i++) {
         Log("POLL   handle %d\n", events[i]);
      }
      PollDumpDeviceQueue(poll, class);
      NOT_REACHED_BUG(4934);
   }

   return fired;
}


/*
 *----------------------------------------------------------------------
 *
 * PollStartSlave --
 *    Starts a thread whose sole purpose is to wait for the spillover events
 *    and then thunk them back to the main thread.  Also creates the special
 *    events for that thread.
 *
 * Result: 0 on success, VMWARE_STATUS on error
 *
 * Side effects: Thread and events are created.
 *
 *----------------------------------------------------------------------
 */

int
PollStartSlave(PollClass class, 
               int tid)
{
   Poll *poll = pollState;
   SlaveThreadInfo *slaveThread = 
      &slaveState->slaveEvents[class].slaveThreads[tid];
   int i;
   int reservedSlaveEvent = slaveState->reservedSlaveEvent;
   
   for (i = 0; i <  POLL_SLAVE_EVENTS; i++) {
      slaveThread->info[i].event = CreateEvent(NULL, FALSE, FALSE, NULL);
      if (slaveThread->info[i].event == NULL) {
         Log("POLL Could not create event %d for slave thread %d,"
             "class %d, error %d(%s)\n",
             i, tid, class , GetLastError(), Err_ErrString());
         return VMWARE_STATUS_ERROR;
      }
   }

   poll->classEvents[class].info[reservedSlaveEvent + tid].event =
      CreateEvent(NULL, FALSE, FALSE, NULL);
   if (poll->classEvents[class].info[reservedSlaveEvent + tid].event == NULL) {
      Log("POLL Could not create main thread event for slave thread %d,"
          "class %d, error %d(%s)\n",
          tid, class, GetLastError(), Err_ErrString());
      return VMWARE_STATUS_ERROR;
   }

   /* This makes the thread valid. */
   slaveThread->numEvents += POLL_SLAVE_EVENTS;
   slaveThread->tid = tid;
   
   slaveThread->threadHandle = 
      CreateThread(NULL, 0, PollSlaveThread, (LPVOID)slaveThread, 0, NULL);
   if (slaveThread->threadHandle == NULL) {
      Log("POLL Could not create poll slave thread %d, class %d," 
          "error %d(%s)\n",tid, class, GetLastError(), Err_ErrString());
      return VMWARE_STATUS_INSUFFICIENT_RESOURCES;
   }

   return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * PollStopSlave --
 *    Stops the slave thread.  Cleans up the special events used
 *    by the thread.
 *
 * Result: 
 *    Thread and its events are gone.
 *
 * Side effects: 
 *
 *----------------------------------------------------------------------
 */

void
PollStopSlave(int class, 
              int tid)
{
  int i;
  SlaveThreadInfo *slaveThread = 
     &slaveState->slaveEvents[class].slaveThreads[tid];
  
  /* Invalidate the thread */ 
  slaveThread->numEvents = 0; 

  if (SetEvent(slaveThread->info[POLL_SLAVE_EXIT].event) == FALSE) {
     Log("POLL slave thread bad return value from SetEvent, error %d (%s) event %d\n", 
        GetLastError(), Err_ErrString(),
        slaveThread->info[POLL_SLAVE_EXIT].event);
     ASSERT(FALSE);
  }

  if (WaitForSingleObject(slaveThread->threadHandle, 15000) != WAIT_OBJECT_0) {
     TerminateThread(slaveThread->threadHandle, 0);
  }

  for (i = 0; i < POLL_SLAVE_EVENTS; i++) {
     CloseHandle(slaveThread->info[i].event);
  }
}


/*
 *----------------------------------------------------------------------
 *
 * PollSlaveThread --

 *        Main loop of the slave thread.  Wait for some poll event to
 *        be signaled, and pass it on to the main poll thread.
 *
 * Result: 
 *        Thread exits upon request from the main poll thread.
 *
 * Side effects: 
 *  
 *----------------------------------------------------------------------
 */

DWORD WINAPI
PollSlaveThread(LPVOID param) 
{
   Poll *poll = pollState;
   SlaveThreadInfo *slaveThread = (SlaveThreadInfo *)param;
   int mytid = slaveThread->tid;
   PollClass class = slaveThread->class;
   int i, retval;
   void *events[MAX_QUEUE_LENGTH];
   int totalEvents;
   int reservedSlaveEvent = slaveState->reservedSlaveEvent;
   Bool suspended = FALSE;
   
   while (TRUE) {

      /*
       * Make a copy of the events to wait on since other threads are allowed
       * to be in Poll code while we're sleeping in WaitFor...().  -- MattG
       */

      if (suspended) {
         totalEvents = POLL_SLAVE_EVENTS;
      } else {
          totalEvents = slaveThread->numEvents;
      }

      for (i = 0; i < totalEvents; i++) {
         events[i] = slaveThread->info[i].event;
      }
      
      retval = WaitForMultipleObjectsEx(totalEvents, events,
                                           FALSE, INFINITE, 
                                           MWMO_ALERTABLE);
      if (retval >= WAIT_OBJECT_0 && 
          retval < WAIT_OBJECT_0 + POLL_SLAVE_EVENTS ) {
         int iEvent = retval - WAIT_OBJECT_0;

         switch(iEvent) {
         case POLL_SLAVE_RESUME:
            ASSERT(suspended);
            ASSERT(slaveThread->eventIndex == INVALID_EVENT_INDEX);
            suspended = FALSE;
            break;
         case POLL_SLAVE_UPDATE:
            /* Refresh our event list */
            break;
         case POLL_SLAVE_EXIT:
            ExitThread(0);
            break;
         default:
            NOT_REACHED();
         }

      } else if ((retval >= WAIT_OBJECT_0 + POLL_SLAVE_EVENTS) && 
          (retval < WAIT_OBJECT_0 + totalEvents)) {
         int iEvent = retval - WAIT_OBJECT_0;
         struct ClassEvents *classEvents = &poll->classEvents[class];
         ASSERT(iEvent < totalEvents);
         ASSERT(suspended == FALSE);
         
         slaveThread->eventIndex = iEvent;
         
         if (SetEvent(classEvents->info[reservedSlaveEvent + mytid].event) 
             == FALSE) {
            Log("POLL slave thread bad return value from SetEvent, "
                "error %d (%s) event %d\n", GetLastError(), Err_ErrString(),
                classEvents->info[reservedSlaveEvent + mytid].event);
            ASSERT(FALSE);
         }
        
         suspended = TRUE;
       
      } else if (retval == WAIT_IO_COMPLETION) {
         /*
          * An asynchronous I/O completion routine fired.  Nothing to do
          * here... 
          */
      } else if (retval == WAIT_FAILED && 
                 GetLastError() == ERROR_INVALID_HANDLE) {
         /* 
          * Invalid handle can happen due to a race with
          * PollEntryDequeue, so no assert.  Going back to 
          * through the loop should remove this transient error.
          */
         NOT_TESTED();
         Log("POLL slave thread invalid handle  from" 
             "WaitForMultiple, error \n");
         Log("POLL  totalEvents %d\n", totalEvents);
         Log("POLL waited on events:\n");
         for (i = 0; i < totalEvents; i++) {
            Log("POLL   handle %d\n", events[i]);
         }
      } else {
         Log("POLL slave thread bad return value %d from" 
             "WaitForMultiple, error %d (%s)\n",
             retval, GetLastError(), Err_ErrString());
         Log("POLL  totalEvents %d\n", totalEvents);
         Log("POLL waited on events:\n");
         for (i = 0; i < totalEvents; i++) {
            Log("POLL   handle %d\n", events[i]);
         }
         PollDumpDeviceQueue(poll, class);
         NOT_REACHED_BUG(4934);           
      }
   }
   return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * PollFireAndDequeueSocketEvent
 *
 *       Given a ClassEventInfo said to be ready for I/O (according to
 *       WaitForMultipleObjects) and associated with a socket, call the
 *       reader and writer callbacks for activity on that socket.
 *
 * Result: whether any callbacks were called
 *
 * Side effects: PollFireAndDequeue
 *
 *----------------------------------------------------------------------
 */

static INLINE Bool
PollFireAndDequeueSocketEvent(Poll *poll, ClassEventInfo *eventInfo)
{
   /* 
    * The signaled event is associated with a socket. Determine which 
    * network events (read or write or both) caused the signaling, and
    * fire the appropriate callbacks.
    */
   WSANETWORKEVENTS wsaNetworkEvents;
   int err;
   Bool fired = FALSE;
   Bool callWrite = FALSE;
   Bool callRead = FALSE;
   PollEntry *reader, *writer;
   
   /* 
    * Cache the read and write poll entries now because eventInfo
    * may no longer be valid after firing the reader callback,
    * since dequeuing has the side effect of changing classEvents->info[].
    *
    * Be careful not to use eventInfo after firing even the first callback,
    * because it could cease to exist/be valid afterward.
    */
   reader = eventInfo->readPollEntry;
   writer = eventInfo->writePollEntry;

   /*
    * Event is manual-reset, which is what we want because multiple threads
    * could wait on the same one at the same time.  We have to explicitly
    * reset it, which isn't race-proof, so we might call the client
    * callbacks multiple times and they have to be able to deal with that.
    * Basically, select() wake-all semantics.
    */
   err = ResetEvent(eventInfo->event);
   ASSERT_NOT_IMPLEMENTED(err != 0);

   err = WSAEnumNetworkEvents(eventInfo->socket, NULL, &wsaNetworkEvents);
   ASSERT_NOT_IMPLEMENTED(err == 0);

   /* 
    * The writer entry is referenced after firing the reader callback.
    * Bump its reference count now to prevent it from being destroyed
    * during the invocation of the reader callback.
    */
   if (writer) {
      PollEntryIncrement(writer);
   }

   /*
    * Fire callbacks as applicable.  If we get a FD_CLOSE, call the reader AND
    * the writer if they exist.  This behavior is forced by
    * PollMapSocketToEvent().  See bug 83030 for the corresponding problem in
    * pollVMX.c.
    */

   if (wsaNetworkEvents.lNetworkEvents & (FD_READ | FD_ACCEPT)) {
      ASSERT(reader != NULL);
      callRead = TRUE;
   }
   if (wsaNetworkEvents.lNetworkEvents & (FD_WRITE | FD_CONNECT)) {
      ASSERT(writer != NULL);
      callWrite = TRUE;
   }
   if (wsaNetworkEvents.lNetworkEvents & FD_CLOSE) {
      ASSERT(reader != NULL || writer != NULL);
      callRead |= (reader != NULL);
      callWrite |= (writer != NULL);
   }

   if (callRead) {
      fired |= PollFireAndDequeue(poll, POLL_DEVICE, reader, NULL);
   }
   if (callWrite) {
      if (writer != reader || !callRead) {
         fired |= PollFireAndDequeue(poll, POLL_DEVICE, writer, NULL);
      }
   }

   /*
    * Balance our increment above.
    */
   if (writer) {
      PollEntryDecrement(poll, &writer);
   }

   return fired;
}


/*
 *----------------------------------------------------------------------
 *
 * PollMapSocketToEvent
 *
 *   Bind the socket to an event object using WSAEventSelect().
 *   When the socket becomes ready for reading, the event is
 *   signaled. When thread(s) waiting on the event is (are) released,
 *   the event must be manually reset to the non-signaled state.
 *
 *   After a recv() from the socket, the event is
 *   signaled again if data is still available for reading, 
 *   according to the documentation for WSAEventSelect().
 *   Good. This is the behavior we want. -leb
 *
 * Result:
 *
 *   The event handle associated with the socket
 *
 * Side effects:
 *
 *   Socket is associated with WSA.  Socket2eventlist is modified.
 *
 *----------------------------------------------------------------------
 */

HANDLE
PollMapSocketToEvent(SOCKET s,               // IN: socket
                     PollClassSet classSet,  // IN: poll class set
                     int pollFlags)          // IN: events of interest
{
   HANDLE event;
   SocketToEvent *socket2event;
   long newNetworkEvents = FD_CLOSE;
   int err;

   if (pollFlags & POLL_FLAG_READ) {
      newNetworkEvents |= FD_READ | FD_ACCEPT;
      ASSERT_NOT_TESTED(!(pollFlags & POLL_FLAG_WRITE));
   }
   if (pollFlags & POLL_FLAG_WRITE) {
      newNetworkEvents |= FD_WRITE | FD_CONNECT;
      ASSERT_NOT_TESTED(!(pollFlags & POLL_FLAG_READ));
   }

   // Search the list
   for (socket2event = socket2EventList;
        socket2event != NULL && socket2event->socket != s;
	socket2event = socket2event->next);

   if (socket2event == NULL) {
      // Not found. Create new event.
      event = WSACreateEvent();
      ASSERT_NOT_IMPLEMENTED(event != NULL);

      socket2event = calloc(1, sizeof(SocketToEvent));
      ASSERT_MEM_ALLOC(socket2event != NULL);

      socket2event->next = socket2EventList;
      socket2EventList = socket2event;
      socket2event->eventHandle = event;
      socket2event->socket = s;
      socket2event->networkEvents = 0;
      socket2event->classSet = classSet;
   } else {
      // Reuse old event.
      event = socket2event->eventHandle;
   }

   ASSERT(socket2event->socket == s);
   ASSERT(socket2event->classSet == classSet);

   // Assert that new network events are being registered 
   ASSERT((socket2event->networkEvents | newNetworkEvents) !=
      socket2event->networkEvents);
   socket2event->networkEvents |= newNetworkEvents;

   err = WSAEventSelect(s, event, socket2event->networkEvents);
   ASSERT_NOT_IMPLEMENTED(err == 0);

   return event;
}


/*
 *----------------------------------------------------------------------
 *
 * PollLookupSocketEvent
 *
 *   Finds the event handle associated with a socket.
 *
 * Return value
 *
 *   The event handle associated with the socket
 *
 *----------------------------------------------------------------------
 */

static HANDLE
PollLookupSocketEvent(SOCKET s)
{
   // Search the list
   SocketToEvent *socket2event;
   for (socket2event = socket2EventList;
        socket2event != NULL && socket2event->socket != s;
	socket2event = socket2event->next);
   return socket2event ? socket2event->eventHandle : NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * PollUnmapSocketToEvent
 *
 *   Unregister a set of network events associated with a socket.
 *   If the socket has no associated network events left, the 
 *   event object it is currently mapped to is destroyed.
 *
 * Return value
 *
 *   The event handle previously associated with the socket
 *   (but which is no longer valid and can't be used as such)
 *
 *----------------------------------------------------------------------
 */

HANDLE
PollUnmapSocketToEvent(SOCKET s,               // IN: socket
                       PollClassSet classSet,  // IN: poll class set
                       int pollFlags)          // IN: events to unregister
{
   HANDLE event;
   SocketToEvent *socket2event, **socket2eventPtr;
   long networkEventsToRemove = 0;
   int err;

   if (pollFlags & POLL_FLAG_READ) {
      networkEventsToRemove |= FD_READ | FD_ACCEPT;
      ASSERT_NOT_TESTED(!(pollFlags & POLL_FLAG_WRITE));
   }
   if (pollFlags & POLL_FLAG_WRITE) {
      networkEventsToRemove |= FD_WRITE | FD_CONNECT;
      ASSERT_NOT_TESTED(!(pollFlags & POLL_FLAG_READ));
   }

   // Search the list
   for (socket2eventPtr = &socket2EventList;
        (socket2event = *socket2eventPtr) != NULL && socket2event->socket != s;
	socket2eventPtr = &socket2event->next);

   ASSERT(socket2event != NULL); // must find
   ASSERT(socket2event->classSet == classSet);
   event = socket2event->eventHandle;

   // Assert we're actually unregistering at least one network event  
   ASSERT((socket2event->networkEvents & ~networkEventsToRemove) !=
          socket2event->networkEvents);

   socket2event->networkEvents &= ~networkEventsToRemove; 

   /* If the only network event remaining is FD_CLOSE, it means no more poll
    * entries are interested in this socket, so delete this mapping.
    */
   if (socket2event->networkEvents == FD_CLOSE) {

      *socket2eventPtr = socket2event->next; // Unlink from list 

      err = WSAEventSelect(s, NULL, 0); // destroy socket<->event association
      /* 
       * If the socket is deleted before Poll cleans up, the 
       * WSAEventSelect could run on a non-existent socket.
       * So Log WSAENOTSOCK, and error out in all other cases.
       */
      if (err) {
         int wsaerr = WSAGetLastError();
         if (wsaerr == WSAENOTSOCK) {
            Log("WSAEventSelect() on closed socket, ignoring.\n");
         } else {
            Warning("WSAEventSelect() error: %s\n",
                    Err_Errno2String(wsaerr));
            NOT_IMPLEMENTED_BUG(224859);
         }
      }

      err = CloseHandle(event);         // destroy event object
      ASSERT_NOT_IMPLEMENTED(err);

      free(socket2event);               // destroy data structure
   } else {
      /* The set of network events has changed but remains non-null.
       * Re-register the events of interest.
       */
      err = WSAEventSelect(s, event, socket2event->networkEvents);
      /* 
       * If the socket is deleted before Poll cleans up, the 
       * WSAEventSelect could run on a non-existent socket.
       * So Log WSAENOTSOCK, and error out in all other cases.
       */
      if (err) {
         int wsaerr = WSAGetLastError();
         if (wsaerr == WSAENOTSOCK) {
            Log("WSAEventSelect() on closed socket, ignoring.\n");
         } else {
            Warning("WSAEventSelect() error: %s\n",
                    Err_Errno2String(wsaerr));
            NOT_IMPLEMENTED_BUG(224859);
         }
      }
   }
   return event;
}

#endif // --------- Win32 specific code end -----------


/*
 *----------------------------------------------------------------------
 *
 * PollIsDeviceDescriptorGood --
 *
 *      Debugging helper to test whether a given fd/handle is good.
 *
 * Result: Bool, is e->info.fd valid
 *
 * Side effects: none.
 *
 *----------------------------------------------------------------------
 */

static Bool
PollIsDeviceDescriptorGood(PollEntry *e)
{
#ifdef _WIN32
      HANDLE eventHandle;
      eventHandle = (e->flags & POLL_FLAG_SOCKET) ?
         PollLookupSocketEvent((SOCKET)e->info.fd) : (HANDLE)e->info.fd;
      return IsHandleGood(eventHandle);
#else
      return fcntl(e->info.fd, F_GETFD) >= 0;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * PollDumpDeviceQueue --
 *
 *      Debugging helper to dump the state of registered device callbacks
 *      and their associated fds/handles, if the poll/WaitFor fails.
 *      See bugs 5543 and 4934.
 *
 * Result: void
 *
 * Side effects: logging.
 *
 *----------------------------------------------------------------------
 */

static void
PollDumpDeviceQueue(Poll *poll,
                    PollClass class)
{
   PollEntry *e;

   Log("POLL class %d cs %#x\n", class, 1 << class);
   for (e = poll->queue[POLL_DEVICE]; e != NULL; e = e->next) {
      Log("POLL func %p(%p) cs %#x flags %#x count %d onQueue %d "
          "handle %"FMTH"d %s\n",
          e->f, e->clientData, e->classSet, e->flags, e->count, e->onQueue,
          e->info.fd, PollIsDeviceDescriptorGood(e) ? "good" : "bad");
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Poll_InitDefault --
 *
 *      Public init function for this Poll implementation. Poll loop will be
 *      up and running after this is called.
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
Poll_InitDefault(void)
{
   static PollImpl defaultImpl =
   {
      PollDefaultInit,
      PollDefaultExit,
      PollDefaultLoopTimeout,
      PollDefaultCallback,
      PollDefaultCallbackRemove,
   };

   Poll_InitWithImpl(&defaultImpl);
}


/*
 *----------------------------------------------------------------------
 *
 * Poll_InitDefaultWithHighWin32EventLimit --
 *
 *      Allow more than 64 events to be handled by poll 
 *      on Windows.  The limitation comes from Windows' 
 *      WaitForMultipleObjects.  The current workaround
 *      has a relatively performance cost, as it requires
 *      two thread context switches for each event over the
 *      64-event limit.
 *
 *      Also initializes the implementation.
 *      
 * Results: void
 *
 * Side effects: Enables slave threads to be started when the number of
                 events goes above ~60.
 *
 *----------------------------------------------------------------------
 */

void
Poll_InitDefaultWithHighWin32EventLimit(void)
{
#ifdef _WIN32
   int i,s;
   Poll *poll;
   SlaveThreadInfo *slaveThread;
   
   Poll_InitDefault();
   poll = pollState;
   
   ASSERT(slaveState == NULL);
   slaveState = Util_SafeCalloc(1, sizeof *slaveState);
   
   for (i = 0; i < POLL_FIXED_CLASSES; i++) {
      slaveState->slaveEvents[i].numSlaves = POLL_MAX_SLAVE_THREADS;
      slaveState->reservedSlaveEvent = 
         MAX_QUEUE_LENGTH - POLL_MAX_SLAVE_THREADS;
      for (s = 0; s < POLL_MAX_SLAVE_THREADS; s++) {
         int j;
         slaveThread = &slaveState->slaveEvents[i].slaveThreads[s];
         slaveThread->numEvents = 0;   
         slaveThread->eventIndex = INVALID_EVENT_INDEX;
         for (j = 0; j < MAX_QUEUE_LENGTH; j++) {
            PollClearClassEventInfo(&slaveThread->info[j]);
         }
      }
   }
#else
   Poll_InitDefault();
#endif
}
