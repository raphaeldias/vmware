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
 * poll.c -- management of the event callback queues, selects, ...
 */


#include "vmware.h"
#include "pollImpl.h"

/*
 * Maximum time (us.) to sleep when there is nothing else to do
 * before this time elapses. It has an impact on how often the
 * POLL_MAIN_LOOP events are fired.  --hpreg
 */

#define MAX_SLEEP_TIME (1 * 1000 * 1000) /* 1 s. */


static PollImpl *pollImpl = NULL;


/*
 *----------------------------------------------------------------------
 *
 * Poll_InitWithImpl --
 *
 *      Module initialization. An implementation of Poll should call
 *      this is initialize the function table and then start Poll.
 *
 * Results: void
 *
 * Side effects: poll is alive
 *
 *----------------------------------------------------------------------
 */

void
Poll_InitWithImpl(PollImpl *impl)
{
   ASSERT(pollImpl == NULL);

   pollImpl = impl;

   pollImpl->Init();
}

/*
 *----------------------------------------------------------------------
 *
 * Poll_Exit --
 *
 *      module de-initalization
 *
 * Warning:
 *
 *      This function is intended to be called from vmxScsiLib or
 *      nbdScsiLib only. It has *not* been used, nor tested, in the
 *      context of the VMX product.
 *
 *----------------------------------------------------------------------
 */
void
Poll_Exit(void)
{
   pollImpl->Exit();

   pollImpl = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Poll_LoopTimeout --
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

void
Poll_LoopTimeout(Bool loop,          // IN: loop forever if TRUE, else do one pass.
                 Bool *exit,         // IN: NULL or set to TRUE to end loop.
                 PollClass class,    // IN: class of events (POLL_CLASS_*)
                 int timeout)        // IN: maximum time to sleep
{
   pollImpl->LoopTimeout(loop, exit, class, timeout);
}



/*
 *----------------------------------------------------------------------
 *
 * Poll_Loop --
 *
 *      Run Poll_LoopTimeout with the default timeout of
 *      MAX_SLEEP_TIME (1 second)
 *
 *----------------------------------------------------------------------
 */

void
Poll_Loop(Bool loop,	      // IN: loop forever if TRUE, else do one pass
          Bool *exit,         // IN: NULL or set to TRUE to end loop.
	  PollClass class)    // IN: class of events (POLL_CLASS_*)
{
   Poll_LoopTimeout(loop, exit, class, MAX_SLEEP_TIME);
}


/*
 *----------------------------------------------------------------------
 *
 * Poll_CallbackRemove --
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

Bool
Poll_CallbackRemove(PollClassSet classSet,
		    int flags,
		    PollerFunction f,
		    void *clientData,
		    PollEventType type)
{
   return pollImpl->CallbackRemove(classSet, flags, f, clientData, type);
}


/*
 *----------------------------------------------------------------------
 *
 * Poll_Callback --
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

VMwareStatus
Poll_Callback(PollClassSet classSet,
	      int flags,
	      PollerFunction f,
              void *clientData,
              PollEventType type,
              PollDevHandle info,
	      struct DeviceLock *lock)
{
   return pollImpl->Callback(classSet, flags, f, clientData, type, info, lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Wrappers for Poll_Callback and Poll_CallbackRemove -- special cases
 * with fewer arguments.
 *
 *----------------------------------------------------------------------
 */

VMwareStatus
Poll_CB_Device(PollerFunction f,
	       void *clientData,
	       PollDevHandle info,
	       Bool periodic)
{
   return
   Poll_Callback(POLL_CS_MAIN,
		 POLL_FLAG_READ |
		 POLL_FLAG_REMOVE_AT_POWEROFF |
		 (periodic ? POLL_FLAG_PERIODIC : 0),
		 f,
		 clientData,
		 POLL_DEVICE,
		 info, NULL);
}


Bool
Poll_CB_DeviceRemove(PollerFunction f,
		     void *clientData,
		     Bool periodic)
{
   return
      Poll_CallbackRemove(POLL_CS_MAIN,
			  POLL_FLAG_REMOVE_AT_POWEROFF |
			  (periodic ? POLL_FLAG_PERIODIC : 0),
			  f,
			  clientData,
			  POLL_DEVICE);
}


VMwareStatus
Poll_CB_RTime(PollerFunction f,
	      void *clientData,
	      int info, //microsecs
	      Bool periodic,
	      struct DeviceLock *lock)
{
   return
   Poll_Callback(POLL_CS_MAIN,
		 POLL_FLAG_REMOVE_AT_POWEROFF |
		 (periodic ? POLL_FLAG_PERIODIC : 0),
		 f,
		 clientData,
		 POLL_REALTIME,
		 info, lock);
}


Bool
Poll_CB_RTimeRemove(PollerFunction f,
		    void *clientData,
		    Bool periodic)
{
   return
      Poll_CallbackRemove(POLL_CS_MAIN,
			  POLL_FLAG_REMOVE_AT_POWEROFF |
			  (periodic ? POLL_FLAG_PERIODIC : 0),
			  f,
			  clientData,
			  POLL_REALTIME);
}
