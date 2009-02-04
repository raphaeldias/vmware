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
 * cryptoBuffer.h --
 *
 *      APIs for zeroing buffers, freeing buffers allocated by
 *      other crypto API functions.
 *
 *      NOTE: This header file is within the FIPS crypto boundary and
 *      thus should not change in such a way as to break the interface
 *      to the vmcryptolib library. See bora/make/mk/README_FIPS for
 *      more information.
 */

#ifndef VMWARE_CRYPTOBUFFER_H
#define VMWARE_CRYPTOBUFFER_H 1

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#include "vmware.h"

/*
 * Crypto_Alloc allocates a buffer that can be deallocated with
 * Crypto_Free(String). Has the same semantics as malloc().
 */

EXTERN void *
Crypto_Alloc(size_t size);  // IN

/*
 * Crypto_Free ONLY frees allocated buffers returned by other crypto
 * API functions, such as Crypto_Alloc. It will zero the first
 * 'bytesToZero' bytes before freeing. Otherwise, has the same
 * semantics as free().
 *
 * Crypto_FreeString is the same as Crypto_Free, except it assumes its
 * input is a NULL-terminated string and thus relieves the caller of
 * the burden of calling strlen.
 *
 * NULL is a legal input to both functions.
 *
 * If you just want to zero memory without freeing it, call Util_Zero or
 * Util_ZeroString. If you want to zero and free memory that didn't come
 * from the crypto library, use Util_ZeroFree or Util_ZeroFreeString.
 */

EXTERN void
Crypto_Free(void *buf,            // OUT
            size_t bytesToZero);  // IN

EXTERN void
Crypto_FreeString(char *str); // IN;

#endif /* cryptoBuffer.h */
