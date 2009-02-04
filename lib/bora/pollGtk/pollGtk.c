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
 * pollGtk.c -- a simple poll implementation built on top of GLib
 * For historical reasons, it is named pollGtk but does not
 * depend on gtk.
 * This is the actual Poll_* functions, and so it is different
 * than the Gtk IVmdbPoll implementation.
 *
 * This has to be at least slightly thread-safe. Specifically,
 * it has to allow any thread to schedule callbacks on the Poll
 * thread. For example, the asyncSocket library may schedule a
 * callback in a signal handler when a socket is suddenly 
 * disconnected. As a result, we need to wrap a lock around the
 * queue of events.
 */


#ifdef _WIN32
#pragma pack(push, 8)
#endif
#include <glib.h>
#ifdef _WIN32
#pragma pack(pop)
#endif

#include "pollImpl.h"


/*
 * This describes a single callback waiting for an event
 * or a timeout.
 */
typedef struct PollGtkEntry {
   int flags;
   PollerFunction f;
   void *clientData;
   PollClassSet classSet;
   PollEventType type;

   int event;	// POLL_DEVICE event source

   /* Handle of the registered GTK callback  */
   guint gtkInputId;
} PollGtkEntry;


/*
 * The global Poll state.
 */
typedef struct Poll {
   GStaticRecMutex lock;

   GHashTable *deviceTable;
   GHashTable *timerTable;
} Poll;

static Poll *pollState;


static VMwareStatus
PollGtkCallback(PollClassSet classSet,   // IN
                int flags,               // IN
                PollerFunction f,        // IN
                void *clientData,        // IN
                PollEventType type,      // IN
                PollDevHandle info,      // IN
                struct DeviceLock *lock); // IN


static gboolean PollGtkBasicCallback(gpointer data);

static gboolean PollGtkEventCallback(GIOChannel *source,
                                     GIOCondition condition,
                                     gpointer data);

static void PollGtkRemoveOneCallback(PollGtkEntry *eventEntry);


/*
 *----------------------------------------------------------------------
 *
 * PollGtkInit --
 *
 *      Module initialization.
 *
 * Results: 
 *       None
 *
 * Side effects: 
 *       Initializes the module-wide state and sets pollState.
 *
 *----------------------------------------------------------------------
 */

static void
PollGtkInit(void)
{
   ASSERT(pollState == NULL);
   pollState = g_new0(Poll, 1);

   g_static_rec_mutex_init(&pollState->lock);

   pollState->deviceTable = g_hash_table_new_full(g_direct_hash,
                                                  g_direct_equal,
                                                  NULL,
                                                  (GDestroyNotify)PollGtkRemoveOneCallback);
   ASSERT(pollState->deviceTable);

   pollState->timerTable = g_hash_table_new_full(g_direct_hash,
                                                 g_direct_equal,
                                                 NULL,
                                                 (GDestroyNotify)PollGtkRemoveOneCallback);
   ASSERT(pollState->timerTable);
}


/*
 *----------------------------------------------------------------------
 *
 * PollGtkExit --
 *
 *      Module exit.
 *
 * Results: 
 *       None
 *
 * Side effects: 
 *       Discards the module-wide state and clears pollState.
 *
 *----------------------------------------------------------------------
 */

static void
PollGtkExit(void)
{
   Poll *poll = pollState;

   ASSERT(poll != NULL);

   g_static_rec_mutex_lock(&poll->lock);
   g_hash_table_destroy(poll->deviceTable);
   g_hash_table_destroy(poll->timerTable);
   g_static_rec_mutex_unlock(&poll->lock);

   g_static_rec_mutex_free(&poll->lock);

   g_free(poll);
   pollState = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * PollGtkLoopTimeout --
 *
 *       The poll loop.
 *       This is defined here to allow libraries like Foundry to link.
 *       When run with the Gtk Poll implementation, however, this routine
 *       should never be called. The Gtk framework will pump events.
 *
 * Result:
 *       Void.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static void
PollGtkLoopTimeout(Bool loop,          // IN: loop forever if TRUE, else do one pass.
                   Bool *exit,         // IN: NULL or set to TRUE to end loop.
                   PollClass class,    // IN: class of events (POLL_CLASS_*)
                   int timeout)        // IN: maximum time to sleep
{
   ASSERT_NOT_IMPLEMENTED(0);
}


/*
 *----------------------------------------------------------------------
 *
 * PollGtkFindPredicate --
 *
 *      Predicate usable by GHashTable iteration functions to find
 *      or remove specific elements.
 *
 * Results:
 *      TRUE if the value matches our search criteria, FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static gboolean
PollGtkFindPredicate(gpointer key,   // IN
                     gpointer value, // IN
                     gpointer data)  // IN
{
   PollGtkEntry *current = (PollGtkEntry *)value;
   PollGtkEntry *search = (PollGtkEntry *)data;

   ASSERT(current->type == search->type);
   return current->classSet == search->classSet &&
          current->f == search->f &&
          current->clientData == search->clientData;
}


/*
 *----------------------------------------------------------------------
 *
 * PollGtkCallbackRemove --
 *
 *      Remove a callback.
 *
 * Results:
 *      TRUE if entry found and removed, FALSE otherwise
 *
 * Side effects:
 *      A callback may be modified instead of completely removed.
 *
 *----------------------------------------------------------------------
 */

static Bool
PollGtkCallbackRemove(PollClassSet classSet,   // IN
                      int flags,               // IN
                      PollerFunction f,        // IN
                      void *clientData,        // IN
                      PollEventType type)      // IN
{
   Poll *poll = pollState;
   GHashTable *searchTable;
   PollGtkEntry searchEntry;
   PollGtkEntry *foundEntry;
   gboolean modify = FALSE;
   int finalFlags = 0;
   gint key;

   ASSERT(poll);
   ASSERT(type >= 0 && type < POLL_NUM_QUEUES);

   searchEntry.classSet = classSet;
   searchEntry.flags = flags;
   searchEntry.f = f;
   searchEntry.clientData = clientData;
   searchEntry.type = type;

   switch (type) {
   case POLL_REALTIME:
   case POLL_MAIN_LOOP:
      searchTable = poll->timerTable;
      break;
   case POLL_DEVICE:
      searchTable = poll->deviceTable;
      /*
       * When neither flag is passed, default to READ.
       */
      if ((flags & (POLL_FLAG_READ|POLL_FLAG_WRITE)) == 0) {
	 flags |= POLL_FLAG_READ;
      }
      break;
   case POLL_VIRTUALREALTIME:
   case POLL_VTIME:
   default:
      NOT_IMPLEMENTED();
   }

   g_static_rec_mutex_lock(&poll->lock);

   foundEntry = g_hash_table_find(searchTable,
                                  PollGtkFindPredicate,
                                  &searchEntry);
   if (foundEntry) {
      if (type == POLL_DEVICE) {
         key = foundEntry->event;
         if (foundEntry->flags != flags) {
            finalFlags = foundEntry->flags;
            if (flags & POLL_FLAG_READ) {
               finalFlags &= ~POLL_FLAG_READ;
            }
            if (flags & POLL_FLAG_WRITE) {
               finalFlags &= ~POLL_FLAG_WRITE;
            }
            modify = TRUE;
         }
      } else {
         key = foundEntry->gtkInputId;
      }
      g_hash_table_remove(searchTable, (gpointer)(intptr_t)key);

      if (modify) {
         PollGtkCallback(classSet, finalFlags, f, clientData, type, key, NULL);
      }
   }

   g_static_rec_mutex_unlock(&poll->lock);
   return foundEntry != NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * PollGtkRemoveOneCallback --
 *
 * Results:
 *      TRUE if entry found and removed, FALSE otherwise
 *
 * Side effects:
 *      queues modified
 *
 *----------------------------------------------------------------------
 */

static void
PollGtkRemoveOneCallback(PollGtkEntry *eventEntry) // IN
{
   switch(eventEntry->type) {
   case POLL_REALTIME:
   case POLL_MAIN_LOOP:
   case POLL_DEVICE:
      g_source_remove(eventEntry->gtkInputId);
      break;
   case POLL_VIRTUALREALTIME:
   case POLL_VTIME:
   default:
      NOT_IMPLEMENTED();
   }

   g_free(eventEntry);
}


/*
 *----------------------------------------------------------------------
 *
 * PollGtkCallback --
 *
 *      For the POLL_REALTIME or POLL_DEVICE queues, entries can be
 *      inserted for good, to fire on a periodic basis (by setting the
 *      POLL_FLAG_PERIODIC flag).
 *
 *      Otherwise, the callback fires only once.
 *
 *      For periodic POLL_REALTIME callbacks, "info" is the time in
 *      microseconds between execution of the callback.  For
 *      POLL_DEVICE callbacks, info is a file descriptor.
 *
 *----------------------------------------------------------------------
 */

static VMwareStatus
PollGtkCallback(PollClassSet classSet,   // IN
                int flags,               // IN
                PollerFunction f,        // IN
                void *clientData,        // IN
                PollEventType type,      // IN
                PollDevHandle info,      // IN
                struct DeviceLock *lock) // IN
{
   VMwareStatus result;
   Poll *poll = pollState;
   GHashTable *insertTable;
   PollGtkEntry *newEntry;
   GIOChannel *channel;
   int conditionFlags;
   gint key;

   g_static_rec_mutex_lock(&poll->lock);

   if (type == POLL_DEVICE) {
      PollGtkEntry searchEntry;
      PollGtkEntry *foundEntry;

      searchEntry.classSet = classSet;
      searchEntry.flags = flags;
      searchEntry.f = f;
      searchEntry.clientData = clientData;
      searchEntry.type = type;

      foundEntry = g_hash_table_find(poll->deviceTable,
                                     PollGtkFindPredicate,
                                     &searchEntry);
      if (foundEntry) {
         /*
          * final flags are the union of old and new flags.
          */
         flags |= foundEntry->flags;
         g_hash_table_remove(poll->deviceTable, (gpointer)(intptr_t)info);
         return PollGtkCallback(classSet, flags, f, clientData, type, info, lock);
      }
   }

   ASSERT(f);
   ASSERT(lock == NULL);

   ASSERT_BUG(5315, poll != NULL);

   /*
    * Make sure caller passed POLL_CS instead of POLL_CLASS. 
    */
   ASSERT(classSet & POLL_CS_BIT);

   /* 
    * Every callback must be in POLL_CLASS_MAIN (plus possibly others) 
    */
   ASSERT((classSet & 1 << POLL_CLASS_MAIN) != 0);

   newEntry = g_new0(PollGtkEntry, 1);

   newEntry->f = f;
   newEntry->clientData = clientData;
   newEntry->classSet = classSet;
   newEntry->flags = flags;
   newEntry->type = type;

   ASSERT(type >= 0 && type < POLL_NUM_QUEUES);
   switch(type) {
   case POLL_MAIN_LOOP:
      ASSERT(info == 0);
      /* Fall-through */
   case POLL_REALTIME:
      ASSERT(info == (uint32) info);
      ASSERT_BUG(2430, info >= 0);

      newEntry->event = 0; /* unused field */

      /*
       * info is the delay in microseconds, but we need to pass in
       * a delay in milliseconds.
       */
      newEntry->gtkInputId = g_timeout_add(info / 1000,
                                           PollGtkBasicCallback,
                                           newEntry);
      insertTable = poll->timerTable;
      key = newEntry->gtkInputId;
      break;

   case POLL_DEVICE:
      conditionFlags = 0;

      if (POLL_FLAG_READ & flags) {
         conditionFlags |= G_IO_IN | G_IO_PRI;
      }
      if (POLL_FLAG_WRITE & flags) {
         conditionFlags |= G_IO_OUT;
      }
      conditionFlags |= G_IO_ERR | G_IO_HUP | G_IO_NVAL;

      /*
       * When neither flag is passed, default to READ.
       */
      if ((flags & (POLL_FLAG_READ | POLL_FLAG_WRITE)) == 0) {
         conditionFlags |= G_IO_IN | G_IO_PRI;
      }

      /*
       * info is a file descriptor/socket/handle
       */
      newEntry->event = info;

      /*
       * XXX Looking at the GTK/GLIB source code, it seems that a returned value
       *     of 0 indicates failure (and I should check for it), but that is not
       *     clear 
       */
#ifdef _WIN32
      if (flags & POLL_FLAG_SOCKET) {
         channel = g_io_channel_win32_new_socket(info);
      } else {
         channel = g_io_channel_win32_new_messages(info);
      }
#else
      channel = g_io_channel_unix_new(info);
#endif
      newEntry->gtkInputId = g_io_add_watch(channel,
                                            conditionFlags,
                                            PollGtkEventCallback,
                                            newEntry);
      g_io_channel_unref(channel);
      key = info;
      insertTable = poll->deviceTable;

      break;
      
   case POLL_VIRTUALREALTIME:
   case POLL_VTIME:
   default:
      NOT_IMPLEMENTED();
   }

   g_hash_table_insert(insertTable, (gpointer)(intptr_t)key, newEntry);

   result = VMWARE_STATUS_SUCCESS;

   g_static_rec_mutex_unlock(&poll->lock);
   return result;
} // Poll_Callback


/*
 *-----------------------------------------------------------------------------
 *
 * PollGtkBasicCallback --
 *
 *       This is the basic callback marshaller. It is invoked directly by gtk
 *       in the case of timer callbacks and indirectly through a wrapper for
 *       event callbacks. It calls the real callback and either cleans up
 *       the event or (if it's PERIODIC) leaves it registered to fire again.
 *
 *       This is called by Gtk, so it does not hold the Poll lock.
 *       This is important, because the poll lock is a leaf lock, so
 *       it cannot deadlock with some other lock.
 *
 * Results:
 *    Bool whether to leave the callback enabled.
 *
 * Side effects:
 *    Depends on the invoked callback
 *
 *-----------------------------------------------------------------------------
 */

static gboolean 
PollGtkBasicCallback(gpointer data) // IN: The eventEntry
{
   PollGtkEntry *eventEntry;
   PollerFunction cbFunc;
   void *clientData;
   gboolean ret;

   eventEntry = (PollGtkEntry *)data;
   ASSERT(eventEntry);

   /*
    * Cache the bits we need to fire the callback in case the
    * callback is discarded for being non-periodic.
    */
   cbFunc = eventEntry->f;
   clientData = eventEntry->clientData;

   ret = eventEntry->flags & POLL_FLAG_PERIODIC;

   if (!ret) {
      g_static_rec_mutex_lock(&pollState->lock);
      g_hash_table_remove(pollState->timerTable, (gpointer)(intptr_t)eventEntry->gtkInputId);
      g_static_rec_mutex_unlock(&pollState->lock);
   }

   /*
    * Fire the callback.
    *
    * The callback must fire after unregistering non-periodic callbacks
    * in case the callback function explicitly calls Poll_CallbackRemove.
    * Poll_CallbackRemove is safe when the callback is already gone, but
    * the code above is not safe under those conditions.
    */
   (*cbFunc)(clientData);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * PollGtkEventCallback --
 *
 *       This is called by Gtk when a condition event fires. It is simply
 *       an adapter that immediately calls PollGtkBasicCallback.
 *
 * Results:
 *    TRUE if the event source should remain registered. FALSE otherwise.
 *
 * Side effects:
 *    Depends on the invoked callback
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
PollGtkEventCallback(GIOChannel *source,     // IN: Unix file descriptor the
                                             //     GTK callback is associated to
                     GIOCondition condition, // IN: State(s) of the file
                                             //   descriptor that triggered
                                             //   the GTK callback
                     gpointer data)          // IN: A callback converter
{
   return PollGtkBasicCallback(data);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Poll_InitGtk --
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
Poll_InitGtk(void)
{
   static PollImpl gtkImpl =
   {
      PollGtkInit,
      PollGtkExit,
      PollGtkLoopTimeout,
      PollGtkCallback,
      PollGtkCallbackRemove,
   };

   Poll_InitWithImpl(&gtkImpl);
}
