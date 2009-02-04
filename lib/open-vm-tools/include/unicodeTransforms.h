/*********************************************************
 * Copyright (C) 2007 VMware, Inc. All rights reserved.
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
 * unicodeTransforms.h --
 *
 *      Operations that transform all the characters in a string.
 *
 *      Transform operations like uppercase and lowercase are
 *      locale-sensitive (depending on the user's country and language
 *      preferences).
 */

#ifndef _UNICODE_TRANSFORMS_H_
#define _UNICODE_TRANSFORMS_H_

#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMNIXMOD
#include "includeCheck.h"

#include "unicodeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Standardizes the case of the string by doing a locale-agnostic
 * uppercase operation, then a locale-agnostic lowercase operation.
 */
Unicode Unicode_FoldCase(ConstUnicode str);

/*
 * Trims whitespace from either side of the string.
 */
Unicode Unicode_Trim(ConstUnicode str);
Unicode Unicode_TrimLeft(ConstUnicode str);
Unicode Unicode_TrimRight(ConstUnicode str);

#ifdef __cplusplus
}
#endif

#endif // _UNICODE_TRANSFORMS_H_
