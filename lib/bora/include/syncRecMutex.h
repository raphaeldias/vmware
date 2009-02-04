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
 * syncRecMutex.h --
 *
 *      Implements a platform independent recursive mutex
 */

#ifndef _SYNC_RECMUTEX_H_
#define _SYNC_RECMUTEX_H_

#include "syncMutex.h"
#include "vm_atomic.h"
#include "util.h"

/*
 * SyncRecMutex --
 */

typedef struct SyncRecMutex {
   SyncMutex      mutex;
   Util_ThreadID  ownerId;  // ID of the thread that currently owns this mutex, or -1
   uint32         refCount;
} SyncRecMutex;

Bool SyncRecMutex_Init(SyncRecMutex *that, char const *path);
void SyncRecMutex_Destroy(SyncRecMutex *that);
Bool SyncRecMutex_Lock(SyncRecMutex *that);
Bool SyncRecMutex_Trylock(SyncRecMutex *that);
Bool SyncRecMutex_Unlock(SyncRecMutex *that);

#endif // #infdef _SYNC_RECMUTEX_H_
