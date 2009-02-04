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
 * cryptoSector.h --
 *
 *      Abstract cryptographic functions that facilitate disk sector
 *      encryption.
 *
 *      NOTE: This header file is within the FIPS crypto boundary and
 *      thus should not change in such a way as to break the interface
 *      to the vmcryptolib library. See bora/make/mk/README_FIPS for
 *      more information.
 */

#ifndef VMWARE_CRYPTOSECTOR_H
#define VMWARE_CRYPTOSECTOR_H 1

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#include "vmware.h"
#include "cryptoCipher.h"
#include "cryptoError.h"
#include "iovector.h"

/*
 * Should be kept in-sync with DISKLIB_SECTOR_SIZE.
 */

#define CRYPTO_SECTOR_SIZE 512

/*
 * A cipher, a key, and an IV-generation algorithm taken
 * together; used to decrypt and encrypt disk sectors.
 */

typedef struct CryptoSectorCipherCtx CryptoSectorCipherCtx;

/*
 * Exported functions.
 */

EXTERN CryptoSectorCipherCtx *
CryptoSector_CipherCtxCreate(const CryptoKey *key, // IN
                             const uint8 *iv,      // IN
                             size_t ivSize);       // IN

EXTERN CryptoError
CryptoSector_CipherCtxGenerate(CryptoCipher *cipher,               // IN
                               CryptoSectorCipherCtx **cipherCtx); // OUT

EXTERN CryptoSectorCipherCtx *
CryptoSector_CipherCtxGrab(CryptoSectorCipherCtx *cipherCtx); // IN

EXTERN void
CryptoSector_CipherCtxRelease(CryptoSectorCipherCtx *cipherCtx); // IN

EXTERN const CryptoKey *
CryptoSector_CipherCtxGetKey(CryptoSectorCipherCtx *cipherCtx); // IN

EXTERN const uint8 *
CryptoSector_CipherCtxGetIV(CryptoSectorCipherCtx *cipherCtx); // IN

EXTERN size_t
CryptoSector_CipherCtxGetIVSize(CryptoSectorCipherCtx *cipherCtx); // IN

EXTERN CryptoCipher *
CryptoSector_CipherCtxGetCipher(CryptoSectorCipherCtx *cipherCtx); // IN

EXTERN size_t
CryptoSector_CipherCtxExpansion(CryptoSectorCipherCtx *cipherCtx, // IN
                                size_t plainTextSize);            // IN

EXTERN size_t
CryptoSector_CipherCtxMaxExpansion(CryptoSectorCipherCtx *cipherCtx); // IN

EXTERN CryptoError
CryptoSector_Crypt(Bool encrypt,                      // IN
                   CryptoSectorCipherCtx *cipherCtx,  // IN
                   SectorType logicalSector,          // IN
                   const uint8 *src,                  // IN
                   uint8 *dst);                       // OUT

EXTERN CryptoError
CryptoSector_HMACEncrypt(CryptoSectorCipherCtx *cipherCtx,  // IN
                         SectorType logicalStart,           // IN
                         uint8 *text,                       // IN/OUT
                         size_t textSize,                   // IN
                         size_t textExpansion,              // IN
                         const void *extraAuthText,         // IN
                         size_t extraAuthTextSize);         // IN

EXTERN CryptoError
CryptoSector_HMACDecrypt(CryptoSectorCipherCtx *cipherCtx,  // IN
                         SectorType logicalStart,           // IN
                         uint8 *text,                       // IN/OUT
                         size_t textSize,                   // IN
                         size_t textShrinkage,              // IN
                         const void *extraAuthText,         // IN
                         size_t extraAuthTextSize);         // IN

#endif /* cryptoSector.h */
