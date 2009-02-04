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
 * requestQueue.h --
 *
 *      A simple queue for pending requests.
 */

#ifndef _REQUEST_QUEUE_H_
#define _REQUEST_QUEUE_H_


#include "basicHttp.h"
#include "util.h"
#include "vm_assert.h"


typedef struct _RequestQueueEntry RequestQueueEntry;
typedef struct _RequestQueue RequestQueue;


struct _RequestQueueEntry
{
   RequestQueueEntry *next;
   BasicHttpRequest *request;
};


struct _RequestQueue
{
   RequestQueueEntry *head;
   RequestQueueEntry *tail;
   size_t size;
};


/*
 *-----------------------------------------------------------------------------
 *
 * RequestQueue_New --
 *
 *      Instantiate a new RequestQueue.
 *
 * Results:
 *      The new RequestQueue.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

INLINE RequestQueue *
RequestQueue_New(void)
{
   return Util_SafeCalloc(1, sizeof (RequestQueue));
}


/*
 *-----------------------------------------------------------------------------
 *
 * RequestQueue_Free --
 *
 *      Free a RequestQueue. This will also free the contained Request objects
 *      as well.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

INLINE void
RequestQueue_Free(RequestQueue *queue) // IN:
{
   RequestQueueEntry *entry;

   ASSERT(queue);

   entry = queue->head;
   while(entry) {
      RequestQueueEntry *nextEntry = entry->next;
      
      BasicHttp_FreeRequest(entry->request);
      free(entry);
      entry = nextEntry;
   }
   free(queue);
}


/*
 *-----------------------------------------------------------------------------
 *
 * RequestQueue_PushTail --
 *
 *      Add a new Request to the queue.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

INLINE void
RequestQueue_PushTail(RequestQueue *queue,       // IN
                      BasicHttpRequest *request) // IN
{
   RequestQueueEntry *newEntry;

   ASSERT(queue);
   ASSERT(request);

   newEntry = Util_SafeCalloc(1, sizeof *newEntry);
   newEntry->request = request;

   if (!queue->head) {
      queue->head = newEntry;
   }
   if (queue->tail) {
      /*
       * This also fixes up the head when adding the second element to
       * a single element queue.
       */
      queue->tail->next = newEntry;
   }
   queue->tail = newEntry;
   (queue->size)++;
}


/*
 *-----------------------------------------------------------------------------
 *
 * RequestQueue_PopHead --
 *
 *      Remove a Request from the queue. The removed request is returned to
 *      the caller who is responsible for eventually freeing it.
 *
 * Results:
 *      The removed request.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

INLINE BasicHttpRequest *
RequestQueue_PopHead(RequestQueue *queue) // IN
{
   RequestQueueEntry *oldEntry;
   BasicHttpRequest *request;

   ASSERT(queue);
   ASSERT(queue->size);

   oldEntry = queue->head;
   queue->head = oldEntry->next;
   if (queue->head == NULL) {
      queue->tail = NULL;
   }
   (queue->size)--;

   request = oldEntry->request;
   free(oldEntry);

   return request;
}


#endif // _REQUEST_QUEUE_H_
