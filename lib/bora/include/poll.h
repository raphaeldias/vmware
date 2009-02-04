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


#ifndef _POLL_H_
#define _POLL_H_

#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE
#include "includeCheck.h"

#include "vm_basic_types.h"
#include "vmware.h"

#ifdef _WIN32
#define HZ 100
#elif defined linux
#include <asm/param.h>
#elif __APPLE__
#include <sys/kernel.h>
#include <sys/poll.h>
#define HZ 100
#endif


/*
 * Poll event types: each type has a different reason for firing,
 * or condition that must be met before firing.
 */

typedef enum {
   /*
    * Actual Poll queue types against which you can register callbacks.
    */
   POLL_VIRTUALREALTIME = -1, /* Negative because it doesn't have its own Q */
   POLL_VTIME = 0,
   POLL_REALTIME,
   POLL_DEVICE,
   POLL_MAIN_LOOP,
   POLL_NUM_QUEUES
} PollEventType;


/*
 * Classes of events
 *
 * These are the predefined classes.  More can be declared
 * with Poll_AllocClass().
 */

typedef enum PollClass {
   POLL_CLASS_MAIN,
   POLL_CLASS_PAUSE,
   POLL_CLASS_IPC,
   POLL_CLASS_CPT,
   POLL_CLASS_MKS,
   POLL_FIXED_CLASSES
} PollClass;

#define POLL_MAX_CLASSES 31


/*
 * Each callback is registered in a set of classes
 */

typedef uint32 PollClassSet;

#define POLL_CS_BIT     (1 << 31) // avoid confusion with POLL_CLASS_*

#define POLL_CS_MAIN    ((1 << POLL_CLASS_MAIN)	| \
                         POLL_CS_BIT)
#define POLL_CS_PAUSE   ((1 << POLL_CLASS_PAUSE)| \
                         (1 << POLL_CLASS_MAIN) | \
                         POLL_CS_BIT)
#define POLL_CS_IPC     ((1 << POLL_CLASS_IPC)	| \
                         (1 << POLL_CLASS_PAUSE)| \
                         (1 << POLL_CLASS_MAIN) | \
                         POLL_CS_BIT)
#define POLL_CS_CPT     ((1 << POLL_CLASS_CPT)	| \
                         (1 << POLL_CLASS_PAUSE)| \
                         (1 << POLL_CLASS_MAIN) | \
                         POLL_CS_BIT)
#define POLL_CS_VMDB    POLL_CS_PAUSE /* POLL_CLASS_VMDB is retired */
#define POLL_CS_MKS	((1 << POLL_CLASS_MKS)	| \
                         POLL_CS_BIT)
/* 
 * DANGER.  You don't need POLL_CS_ALWAYS.  Really.  So don't use it.
 */
#define POLL_CS_ALWAYS  ((1 << POLL_CLASS_IPC)  | \
                         (1 << POLL_CLASS_CPT)  | \
                         (1 << POLL_CLASS_PAUSE)| \
                         (1 << POLL_CLASS_MAIN) | \
                         POLL_CS_BIT)

/*
 * Poll class-set taxonomy:
 * POLL_CS_MAIN
 *    - Unless you NEED another class, use POLL_CS_MAIN.
 * POLL_CS_PAUSE
 *    - For callbacks that must occur even if the guest is paused.
 *      Most VMDB or Foundry commands are in this category.
 * POLL_CS_CPT
 *    - Only for callbacks which can trigger intermediate Checkpoint 
 *      transitions.
 *      The ONLY such callbacks are CrossUserRPC and VMotion.
 * POLL_CS_IPC
 *    - Only for callbacks which can contain Msg_(Post|Hint|Question) 
 *      responses, and for signal handlers (why)?
 *      IPC, VMDB, and Foundry can contain Msg_* responses.
 * POLL_CS_MKS
      - Callback runs in MKS thread.
 * POLL_CS_ALWAYS
 *    - Only for events that must be processed immediately.
 *      The ONLY such callback is VThread watchdog.
 */


/*
 * Poll_Callback flags
 */

#define POLL_FLAG_PERIODIC		0x01    // keep after firing
#define POLL_FLAG_REMOVE_AT_POWEROFF	0x02  	// self-explanatory
#define POLL_FLAG_READ			0x04	// device is ready for reading
#define POLL_FLAG_WRITE			0x08	// device is ready for writing
#define POLL_FLAG_SOCKET      	        0x10	// device is a Windows socket
#define POLL_FLAG_NO_BULL               0x20    // callback does its own locking


/*
 * Advisory minimum time period.
 * Users that want the fastest running real-time poll
 * should use TICKS_TO_USECS(1).
 */

#define TICKS_TO_USECS(_x) ((_x) * (1000000 / HZ))
#define USECS_TO_TICKS(_x) ((_x) / (1000000 / HZ))


/*
 * Initialisers:
 *
 *      For the sake of convenience, we declare the initialisers
 *      for custom implmentations here, even though the actual
 *      implementations are distinct from the core poll code.
 */

EXTERN void Poll_InitDefault(void);
EXTERN void Poll_InitDefaultWithHighWin32EventLimit(void);
EXTERN void Poll_InitGtk(void); // On top of glib for Linux
EXTERN void Poll_InitCF(void);  // On top of CoreFoundation for OSX


/*
 * Functions
 */

typedef void (*PollerFunction)(void *clientData);

struct DeviceLock;

EXTERN void Poll_Loop(Bool loop, Bool *exit, PollClass c);
EXTERN void Poll_LoopTimeout(Bool loop, Bool *exit, PollClass c, int timeout);
EXTERN void Poll_Exit(void);


/*
 * Poll_Callback adds a callback regardless of whether an identical one exists.
 *
 * Likewise, Poll_CallbackRemove removes exactly one callback.
 */

EXTERN VMwareStatus Poll_Callback(PollClassSet classSet,
			          int flags,
			          PollerFunction f,
                                  void *clientData,
                                  PollEventType type,
                                  PollDevHandle info, // fd/microsec delay
			          struct DeviceLock *lck);
EXTERN Bool Poll_CallbackRemove(PollClassSet classSet,
				int flags,
				PollerFunction f,
				void *clientData,
				PollEventType type);


/*
 * Wrappers for Poll_Callback and Poll_CallbackRemove that present
 * simpler subsets of those interfaces.
 */

EXTERN VMwareStatus Poll_CB_Device(PollerFunction f,
			           void *clientData,
			           PollDevHandle device,
			           Bool periodic);

EXTERN Bool Poll_CB_DeviceRemove(PollerFunction f,
				 void *clientData,
				 Bool periodic);


EXTERN VMwareStatus Poll_CB_RTime(PollerFunction f,
			          void *clientData,
			          int delay,	// microseconds
			          Bool periodic,
				  struct DeviceLock *lock);

EXTERN Bool Poll_CB_RTimeRemove(PollerFunction f,
				void *clientData,
				Bool periodic);


#endif // _POLL_H_
