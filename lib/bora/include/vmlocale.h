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
 * vmlocale.h --
 *
 *	Host locale interface
 */

#ifndef _VMLOCALE_H_
#define _VMLOCALE_H_

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"


/*
 * Global functions
 */

char *Locale_GetUserLanguage(void);
char *Locale_GetUserNumericLocale(void);


#endif // ifndef _VMLOCALE_H_
