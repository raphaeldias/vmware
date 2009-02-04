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
 * pollImpl.h --
 *
 *      Header file for poll implementations. Poll consumers should not
 *      include is file.
 */


#ifndef _POLLIMPL_H_
#define _POLLIMPL_H_

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#include "poll.h"

/*
 * PollImpl:
 *
 * A Poll implementation should provide a filled in PollImpl
 * to pass to Poll_Init.
 */

typedef struct PollImpl {
   void		(* Init)		(void);
   void		(* Exit)		(void);
   void		(* LoopTimeout)		(Bool loop, Bool *exit,
   					 PollClass c, int timeout);
   VMwareStatus	(* Callback)		(PollClassSet classSet, int flags,
					 PollerFunction f, void *clientData,
					 PollEventType type,
                                         PollDevHandle info,
					 struct DeviceLock *lock);
   Bool		(* CallbackRemove)	(PollClassSet classSet, int flags,
					 PollerFunction f, void *clientData,
					 PollEventType type);
} PollImpl;


void Poll_InitWithImpl(PollImpl *impl);


#endif /* _POLLIMPL_H_ */
