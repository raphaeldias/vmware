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
 * dictType.c --
 *
 *	Template for generic dictionary routines,
 *	with an interface similar to Config_Get<type>.
 *
 *	Before including define these macros:
 *
 *	TYPE
 *	RETURN_TYPE (optionally, if different from TYPE)
 *	TYPE_TAG
 *	DICT_SET
 *	DICT_GET
 *	COPIER (optionally, if not the identity)
 */

#ifndef RETURN_TYPE
#define RETURN_TYPE TYPE
#endif

#ifndef COPIER
#define COPIER
#endif

#ifdef DICT_GET
RETURN_TYPE
DICT_GET(Dictionary *dict,
	 TYPE defaultValue,
	 const char *fmt,
	 ...)
{
   char name[BIG_NAME_SIZE];

   va_list args;
   va_start(args, fmt);
   Str_Vsnprintf(name, BIG_NAME_SIZE, fmt, args);
   va_end(args);
   
   return COPIER(*(RETURN_TYPE *) (Dictionary_Get(dict, &defaultValue,
						  TYPE_TAG, name)));
}
#endif


void
DICT_SET(Dictionary *dict,
	 TYPE value,
	 const char *fmt,
	 ...)
{
   char name[BIG_NAME_SIZE];

   va_list args;
   va_start(args, fmt);
   Str_Vsnprintf(name, BIG_NAME_SIZE, fmt, args);
   va_end(args);

   Dictionary_Set(dict, &value, TYPE_TAG, name);
}


#undef TYPE
#undef RETURN_TYPE
#undef TYPE_TAG
#undef DICT_SET
#undef DICT_GET
#undef COPIER
