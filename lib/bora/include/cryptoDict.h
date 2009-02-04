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
 * cryptoDict.h --
 *
 *      Secure dictionary. (That is, it zeroes out everything when it
 *      is freed.)
 *
 *      NOTE: This header file is within the FIPS crypto boundary and
 *      thus should not change in such a way as to break the interface
 *      to the vmcryptolib library. See bora/make/mk/README_FIPS for
 *      more information.
 */

#ifndef VMWARE_CRYPTODICT_H
#define VMWARE_CRYPTODICT_H 1

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#include "vmware.h"

#include "cryptoError.h"

typedef struct CryptoDict CryptoDict;

EXTERN char CryptoDict_NotFound[];

EXTERN CryptoError
CryptoDict_Create(CryptoDict **dict);                 // OUT

EXTERN CryptoError
CryptoDict_CreateAndImport(const char *buffer,        // IN
                           size_t size,               // IN
                           CryptoDict **dict);        // OUT

EXTERN void
CryptoDict_Free(CryptoDict *dict);                    // IN/OUT

EXTERN char *
CryptoDict_Get(const CryptoDict *dict,                // IN
               const char *name);                     // IN

EXTERN CryptoError
CryptoDict_GetBase64(const CryptoDict *dict,          // IN
                     const char *name,                // IN
                     uint8 **data,                    // OUT
                     size_t *size);                   // OUT

EXTERN CryptoError
CryptoDict_GetUint32(const CryptoDict *dict,          // IN
                     const char *name,                // IN
                     uint32 *value);                  // OUT

EXTERN CryptoError
CryptoDict_Set(CryptoDict *dict,                      // IN/OUT
               const char *name,                      // IN
               const char *value);                    // IN

EXTERN CryptoError
CryptoDict_SetBase64(CryptoDict *dict,                // IN/OUT
                     const char *name,                // IN
                     const uint8 *data,               // IN
                     size_t size);                    // IN

EXTERN CryptoError
CryptoDict_SetUint32(CryptoDict *dict,                // IN/OUT
                     const char *name,                // IN
                     uint32 value);                   // IN        

EXTERN Bool
CryptoDict_HadSetError(const CryptoDict *dict);       // IN

EXTERN CryptoError
CryptoDict_Import(CryptoDict *dict,                   // IN/OUT
                  const char *buffer,                 // IN
                  size_t size);                       // IN

EXTERN CryptoError
CryptoDict_Export(const CryptoDict *dict,             // IN/OUT
                  Bool multiline,                     // IN
                  char **buffer,                      // OUT
                  size_t *size);                      // OUT

/*
 * Secure dictionary iterator.
 */
typedef struct CryptoDictIterator CryptoDictIterator;

EXTERN CryptoDictIterator *
CryptoDictIterator_Create(const CryptoDict *dict);    // IN

EXTERN Bool
CryptoDictIterator_HasNext(CryptoDictIterator *iter); // IN

EXTERN Bool
CryptoDictIterator_Next(CryptoDictIterator *iter,     // IN
                        const char **name,            // OUT
                        const char **value);          // OUT

EXTERN void
CryptoDictIterator_Destroy(CryptoDictIterator *iter); // IN/OUT

#endif /* cryptoDict.h */
