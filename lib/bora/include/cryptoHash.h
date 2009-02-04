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
 * cryptoHash.h --
 *
 *      Hashes and keyed hashes for cryptographic support library.
 *
 *      NOTE: This header file is within the FIPS crypto boundary and
 *      thus should not change in such a way as to break the interface
 *      to the vmcryptolib library. See bora/make/mk/README_FIPS for
 *      more information.
 */

#ifndef VMWARE_CRYPTOHASH_H
#define VMWARE_CRYPTOHASH_H 1

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#include "vmware.h"

#include "cryptoError.h"

/*
 * Hash algorithms.
 *
 * Choosing a hash algorithm is more difficult these days:
 *
 *      - SHA-1 is supported by all versions of the crypto library, but SHA-1
 *        is now considered broken by many cryptographers, based on an attack
 *        requiring 2**69 operations to find a collision.  The break in SHA-1
 *        doesn't mean that it is broken for all uses.  For example, it is fine
 *        for use as a keyed hash (see below).
 *
 *      - SHA-256 is new and not supported by older versions of the crypto
 *        library, but it is believed secure.  It is relatively slow.
 *        It is probably the best choice where speed and backward compatibility
 *        are not paramount.
 */
typedef struct CryptoHash CryptoHash;         /* A cryptographic hash. */

/* SHA-1. */
#define CryptoHash_SHA_1_BlockSize 64                   /* Block size. */
#define CryptoHash_SHA_1_OutputSize 20                  /* Output size. */
#define CryptoHashName_SHA_1 "SHA-1"

/* SHA-256. */
#define CryptoHash_SHA_256_BlockSize 64                 /* Block size. */
#define CryptoHash_SHA_256_OutputSize 32                /* Output size. */
#define CryptoHashName_SHA_256 "SHA-256"

EXTERN CryptoError
CryptoHash_FromString(const char *string,             // IN
                      CryptoHash **hash);             // OUT

EXTERN const char *
CryptoHash_ToString(CryptoHash *hash);                // IN

EXTERN size_t
CryptoHash_GetBlockSize(CryptoHash *hash);            // IN

EXTERN size_t
CryptoHash_GetOutputSize(CryptoHash *hash);           // IN

EXTERN CryptoError
CryptoHash_Compute(CryptoHash *hash,                  // IN
                   const uint8 *input,                // IN
                   size_t inputSize,                  // IN
                   uint8 *output,                     // OUT
                   size_t outputSize);                // IN

/*
 * Hashing in progress.
 */

typedef struct CryptoHashState CryptoHashState;

EXTERN CryptoHashState *
CryptoHashState_Create(CryptoHash *hash);             // IN

EXTERN void
CryptoHashState_Process(CryptoHashState *hashState,   // IN/OUT
                        const uint8 *input,           // IN
                        size_t inputSize);            // IN
EXTERN CryptoError
CryptoHashState_Finish(CryptoHashState *hashState,    // IN/OUT
                       uint8 *output,                 // OUT
                       size_t outputSize);            // IN

EXTERN CryptoHash *
CryptoHashState_GetHash(const CryptoHashState *hashState); // IN

/*
 * Keyed hash algorithms.
 *
 * Choosing a keyed hash may seem more difficult these days with all the recent
 * attacks on popular hash functions.  However, the usage of a secret key means
 * that the attacks on SHA-1 do not affect HMAC-SHA-1.  Thus, there is no
 * immediate need to transition.
 *
 * Here are some guidelines:
 *
 *      - HMAC-SHA-1: Supported by all versions of the crypto library.  No
 *        known attacks.  Recommended for general use.
 *
 *      - HMAC-SHA-1-128: This is HMAC-SHA-1 truncated to 128 bits.  Only
 *        recommended for use when a shorter hash is needed; for example, the
 *        encFile library uses it to achieve 16-byte alignment for all file
 *        structures.
 *
 *      - HMAC-SHA-256: Not supported by older versions of crypto library, but
 *        this may be the best future choice.  Relatively slow.  Recommended
 *        for uses where backward-compatibility and speed are not paramount.
 */
typedef struct CryptoKeyedHash CryptoKeyedHash;       /* A keyed hash. */

/* HMAC-SHA-1. */
#define CryptoKeyedHash_HMAC_SHA_1_OutputSize 20        /* Output size. */
#define CryptoKeyedHashName_HMAC_SHA_1 "HMAC-SHA-1"

/* HMAC-SHA-1-128. */
#define CryptoKeyedHash_HMAC_SHA_1_128_OutputSize 16    /* Output size. */
#define CryptoKeyedHashName_HMAC_SHA_1_128 "HMAC-SHA-1-128"

/* HMAC-SHA-256. */
#define CryptoKeyedHash_HMAC_SHA_256_OutputSize 32      /* Output size. */
#define CryptoKeyedHashName_HMAC_SHA_256 "HMAC-SHA-256"

EXTERN CryptoError
CryptoKeyedHash_FromString(const char *string,           // IN
                           CryptoKeyedHash **keyedHash); // OUT

EXTERN const char *
CryptoKeyedHash_ToString(CryptoKeyedHash *keyedHash); // IN

EXTERN size_t
CryptoKeyedHash_GetOutputSize(CryptoKeyedHash *keyedHash); // IN

EXTERN CryptoError
CryptoKeyedHash_Compute(CryptoKeyedHash *keyedHash,   // IN
                        const uint8 *key,             // IN
                        size_t keySize,               // IN
                        const uint8 *data,            // IN
                        size_t dataSize,              // IN
                        uint8 *keyedHashBytes,        // OUT
                        size_t keyedHashSize);        // IN

struct iovec;
EXTERN CryptoError
CryptoKeyedHash_ComputeIov(CryptoKeyedHash *keyedHash,   // IN
                           const uint8 *key,             // IN
                           size_t keySize,               // IN
                           const struct iovec *data,     // IN
                           size_t numData,               // IN
                           uint8 *keyedHashBytes,        // OUT
                           size_t keyedHashSize);        // IN

/*
 * Keyed hash in progress.
 */

typedef struct CryptoKeyedHashState CryptoKeyedHashState;

EXTERN CryptoKeyedHashState *
CryptoKeyedHashState_Create(CryptoKeyedHash *,                  // IN
                            const uint8 *key,                   // IN
                            size_t keySize);                    // IN

EXTERN void
CryptoKeyedHashState_Process(CryptoKeyedHashState *,            // IN/OUT
                             const uint8 *input,                // IN
                             size_t inputSize);                 // IN
EXTERN CryptoError
CryptoKeyedHashState_Finish(CryptoKeyedHashState *,             // IN/OUT
                            uint8 *output,                      // OUT
                            size_t outputSize);                 // IN

EXTERN CryptoKeyedHash *
CryptoKeyedHashState_GetKeyedHash(const CryptoKeyedHashState *hashState); // IN

#endif /* cryptoHash.h */
