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
 * sig.h --
 *
 *      Signal handling
 */

#ifndef _SIG_H_
#define _SIG_H_

#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE
#include "includeCheck.h"


EXTERN Bool Sig_Init(void);
EXTERN void Sig_Exit(void);

#ifdef _WIN32

#define Sig_NoMainLoop(on)
static INLINE void Sig_CoreDumpRegion(Bool add, Bool unmap,
                                      void *addr, size_t size) {
}

#else // not WIN32

#include "sigPosix.h"

#endif // _WIN32 vs Posix

#endif // ifndef _SIG_H_
