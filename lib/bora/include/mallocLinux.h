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

#ifndef _MALLOCLINUX_H_
#define _MALLOCLINUX_H_

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#ifdef LINUX_MALLOC_TRACKER
#define BEGIN_NO_STACK_MALLOC_TRACKER  \
  do { MallocLinux_DisableStack(TRUE); } while (0)
#define END_NO_STACK_MALLOC_TRACKER \
  do { MallocLinux_DisableStack(FALSE); } while (0)
#define BEGIN_NO_MALLOC_TRACKER  \
  do { MallocLinux_Disable(TRUE); } while (0)
#define END_NO_MALLOC_TRACKER \
  do { MallocLinux_Disable(FALSE); } while (0)

EXTERN void MallocLinux_Disable(int disable);
EXTERN void MallocLinux_DisableStack(int disable);
EXTERN void MallocLinux_SetMemTrackerLevel(int level);
EXTERN void MallocLinux_Init(const char *levelvar, const char *filename,
                             int defaultLevel);
EXTERN void MallocLinux_Exit(void);
EXTERN void MallocLinux_InitThread(void);
EXTERN void MallocLinux_ExitThread(void);

#else // ifdef LINUX_MALLOC_TRACKER

#define BEGIN_NO_STACK_MALLOC_TRACKER
#define END_NO_STACK_MALLOC_TRACKER
#define BEGIN_NO_MALLOC_TRACKER
#define END_NO_MALLOC_TRACKER

#endif // ifdef LINUX_MALLOC_TRACKER

#endif // ifndef _MALLOCLINUX_H_
