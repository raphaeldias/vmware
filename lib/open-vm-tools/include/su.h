/*********************************************************
 * Copyright (C) 1998 VMware, Inc. All rights reserved.
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
 * su.h --
 *
 *   Manage super-user priviledges
 *
 */

#ifndef USER_SU_H
#define USER_SU_H

#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE
#include "includeCheck.h"
#include "vm_basic_types.h"

#if defined(__APPLE__)

#include <sys/types.h>
#include <unistd.h>

void Id_SetSuperUser(Bool yes);
int Id_SetGid(gid_t egid);
int Id_SetREUid(uid_t ruid, uid_t euid);
int Id_SetRESUid(uid_t ruid, uid_t euid, uid_t suid);

#define IsSuperUser() (0 == geteuid())
#define SuperUser(yes) Id_SetSuperUser(yes)
#define Id_GetEUid() geteuid()

void *Id_AuthGetLocal();
void *Id_AuthGetExternal(size_t *size);
Bool Id_AuthSet(void const *buf, size_t size);
Bool Id_AuthCheck(char const *right,
                  char const *localizedDescription);

#elif (defined(__linux__) || defined(sun) || defined(__FreeBSD__)) && !defined(N_PLAT_NLM)

#include <sys/types.h>
#include <unistd.h>

/* Our set of set*id functions which affect current thread only */
int Id_SetUid(uid_t euid);
int Id_SetGid(gid_t egid);
int Id_SetREUid(uid_t ruid, uid_t euid);
int Id_SetREGid(gid_t rgid, gid_t egid);
int Id_SetRESUid(uid_t ruid, uid_t euid, uid_t suid);
int Id_SetRESGid(gid_t rgid, gid_t egid, gid_t sgid);

/* For symmetry */
#define Id_GetEUid() geteuid()

/*
 *----------------------------------------------------------------------------
 *
 * Id_SetEUid --
 *
 *      Set specified effective uid for current thread.  Does not affect
 *      real uid or saved uid.
 *
 * Results:
 *	0 on success, -1 on failure, errno set
 *
 * Side effects:
 *      errno may be modified on success
 *
 *----------------------------------------------------------------------------
 */

static INLINE int
Id_SetEUid(uid_t euid)
{
   return Id_SetRESUid((uid_t)-1, euid, (uid_t)-1);
}


/*
 *----------------------------------------------------------------------------
 *
 * Id_SetEGid --
 *
 *      Set specified effective gid for current thread.  Does not affect
 *      real gid or saved gid.
 *
 * Results:
 *      0 on success, -1 on failure, errno set
 *
 * Side effects:
 *      errno may be modified on success
 *
 *----------------------------------------------------------------------------
 */

static INLINE int
Id_SetEGid(gid_t egid)
{
   return Id_SetRESGid((gid_t)-1, egid, (gid_t)-1);
}

#define IsSuperUser()	(0 == geteuid())
#define SuperUser(yes)	((void) ((yes) ? Id_SetEUid(0) : Id_SetEUid(getuid())))

#else /* linux */

// XXX does Windows need this?
#define IsSuperUser()	TRUE
#define SuperUser(yes)	

#endif /* linux */

#endif /* USER_SU_H */

