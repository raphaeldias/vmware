/*********************************************************
 * Copyright (C) 2008 VMware, Inc. All rights reserved.
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
 * stubs.c --
 *
 *      Provides bora/lib stubs and basic implementation for functions
 *      needed (currently) by the bora/libs the vmware-view-client code
 *      actually uses.
 */


#include "vm_assert.h"
#include "cryptoError.h"
#include "keySafe.h"
#include "syncMutex.h"
#include "syncRecMutex.h"


#if !defined(__linux__) && !defined(__APPLE__)
#error "These stubs only work on Linux (and maybe Mac OS)"
#endif


const char *
CryptoError_ToString(CryptoError error) // IN
{
   NOT_IMPLEMENTED();
   return NULL;
}


Bool
CryptoFips_FipsModeEnabled(void)
{
   return FALSE;
}


void
Crypto_Free(void *buf,          // IN
            size_t bytesToZero) // IN
{
   NOT_IMPLEMENTED();
}


CryptoKey *
CryptoKey_Clone(const CryptoKey *old) // IN
{
   NOT_IMPLEMENTED();
   return NULL;
}


CryptoError
CryptoKey_DecryptWithMAC(const CryptoKey *key,       // IN
                         CryptoKeyedHash *keyedHash, // IN
                         const uint8 *cipherText,    // IN
                         size_t cipherTextSize,      // IN
                         uint8 **plainText,          // OUT
                         size_t *plainTextSize)      // OUT
{
   NOT_IMPLEMENTED();
   return CRYPTO_ERROR_NO_CRYPTO;
}


CryptoError
CryptoKeyedHash_FromString(const char *string,     // IN
                           CryptoKeyedHash **hash) // OUT
{
   NOT_IMPLEMENTED();
   return CRYPTO_ERROR_NO_CRYPTO;
}


CryptoError
CryptoKey_EncryptWithMAC(const CryptoKey *key,       // IN
                         CryptoKeyedHash *keyedHash, // IN
                         const uint8 *plainText,     // IN
                         size_t plainTextSize,       // IN
                         uint8 **cipherText,         // OUT
                         size_t *cipherTextSize)     // OUT
{
   NOT_IMPLEMENTED();
   return CRYPTO_ERROR_NO_CRYPTO;
}


void
CryptoKey_Free(CryptoKey *key) // IN
{
   ASSERT(key == NULL);
}


KeySafeError
KeySafe_Clone(const KeySafe *oldKeySafe, // IN
              KeySafe **newKeySafe)      // OUT
{
   NOT_IMPLEMENTED();
   return KEYSAFE_ERROR_CRYPTO_ERROR;
}


void
KeySafe_Destroy(KeySafe *keySafe) // IN
{
   ASSERT(keySafe == NULL);
}


const char *
KeySafeError_ToString(KeySafeError error) // IN
{
   NOT_IMPLEMENTED();
   return NULL;
}


KeySafeError
KeySafe_Export(const KeySafe *keySafe, // IN
               char **output,          // OUT
               size_t *outputSize)     // OUT
{
   NOT_IMPLEMENTED();
   return KEYSAFE_ERROR_CRYPTO_ERROR;
}


KeySafeError
KeySafe_Unseal(KeyLocatorState *klState,        // IN
               const char *input,               // IN
               size_t inputSize,                // IN
               const KeySafeUserRing *userRing, // IN
               KeySafe **keySafe,               // OUT
               CryptoKey **key)                 // OUT
{
   NOT_IMPLEMENTED();
   return KEYSAFE_ERROR_CRYPTO_ERROR;
}


KeySafeError
KeySafe_Seal(const KeySafeUserRing *userRing, // IN
             CryptoKey **key,                 // IN/OUT
             KeySafe **keySafe,               // OUT
             char **exportedKeySafe,          // OUT
             size_t *exportSize)              // OUT
{
   NOT_IMPLEMENTED();
   return KEYSAFE_ERROR_CRYPTO_ERROR;
}


Bool
SyncRecMutex_Trylock(SyncRecMutex *that) // IN
{
   return TRUE;
}


Bool
SyncRecMutex_Unlock(SyncRecMutex *that) // IN
{
   return TRUE;
}


Bool
SyncRecMutex_Lock(SyncRecMutex *that) // IN
{
   return TRUE;
}


void
SyncRecMutex_Destroy(SyncRecMutex *that) // IN
{
   // Do nothing.
}


Bool
SyncRecMutex_Init(SyncRecMutex *that,   // IN
                  char const *path)     // IN
{
   return TRUE;
}


Bool
SyncMutex_Init(SyncMutex *that,  // IN
               char const *path) // IN
{
   return TRUE;
}


void
SyncMutex_Destroy(SyncMutex *that) // IN
{
   // Do nothing.
}


Bool
SyncMutex_Unlock(SyncMutex *that) // IN
{
   return TRUE;
}


Bool
SyncMutex_Lock(SyncMutex *that) // IN
{
   return TRUE;
}


SyncMutex *
SyncMutex_CreateSingleton(Atomic_Ptr *lckStorage) // IN
{
   return NULL;
}

#if (__APPLE__)
char *
UUID_GetHostUUID(void)
{
    return NULL;
}
#endif
