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
 * url.h --
 *
 *	Useful URL's.
 */

#ifndef __URL_H__
#   define __URL_H__

#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE
#include "includeCheck.h"
#include "vm_basic_types.h"

typedef enum {
#define URLAPPENDENTRY(codename, name) URLAPPEND_ ## codename,
#   include "urlAppend.h"
#undef URLAPPENDENTRY
} UrlAppend;


typedef enum {
#define URLTABLEENTRY(codename, id, append, name) URLINDEX_ ## codename = id,
#   include "urlTable.h"
#undef URLTABLEENTRY
} UrlId;


extern void URL_SetAppend(UrlAppend index, char const *value);
extern char *URL_Get(UrlId id, Bool append);
extern void URL_Destroy(void);
extern void URL_EncodeURL(const char *in, // IN
                          char *out,      // OUT
                          int outlength); // IN

#endif /* __URL_H__ */
