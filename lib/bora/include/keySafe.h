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
 * keySafe.h --
 *
 *    A KeySafe is an object that protects a single piece of data with
 *    an arbitrary number of cryptographic keys. Each such key is
 *    represented by a KeyLocator from which it can be
 *    derived. Internally, a KeySafe consists of a list of KeyLocator
 *    pairs, each pair being a KeyLocator and an encrypted blob of data.
 *
 *    A KeySafeUserRing contains a list of key locators and, for each
 *    locator, the key that results from following the
 *    locator. Therefore, a user ring is sensitive data. As described
 *    above, a KeySafe contains just the locators themselves plus the
 *    encrypted data, and therefore is not sensitive data. (Atomic key
 *    locators in a KeySafe are stored as the null locator type.)
 *
 *    To unlock a KeySafe, the caller can either present or not present
 *    a user ring.
 *
 *    If the caller presents a user ring, an attempt is made to unlock
 *    the KeySafe using each key in the user ring. If this fails, no
 *    attempt is made to manually follow the locators in the KeySafe.
 *
 *    If the caller does not present a user ring, the locators in the
 *    KeySafe are manually followed to generate a key that can unlock
 *    the KeySafe.
 *
 *    To use KeySafe, callers must ensure that KeyLocator has been
 *    previously initialized via a call to KeyLocator_Init(). It is
 *    legal pass a NULL callback function address to KeyLocator_Init();
 *    in this case, any attempt to unlock a KeySafe without specifying a
 *    user ring will fail.
 *
 *    Anywhere a KeySafe API takes a KeyLocatorState pointer, NULL can
 *    also be passed-in. This will disable use of the KeyLocator cache
 *    and cause all follows of indirect locators to fail. It only makes
 *    sense to pass NULL for KeyLocatorState when you are passing
 *    non-NULL for the 'userRing' parameter, since if following is not
 *    possible then only directly-specified keys can be used.
 */

#ifndef VMWARE_KEYSAFE_H
#define VMWARE_KEYSAFE_H 1

#include "crypto.h"
#include "keyLocator.h"


/*
 * Error codes.
 */

typedef int KeySafeError;

#define KEYSAFE_ERROR_SUCCESS         ((KeySafeError) 0)
#define KEYSAFE_ERROR_NOMEM           ((KeySafeError) 1)
#define KEYSAFE_ERROR_CRYPTO_ERROR    ((KeySafeError) 2)
#define KEYSAFE_ERROR_NEED_KEY        ((KeySafeError) 3)
#define KEYSAFE_ERROR_BAD_FORMAT      ((KeySafeError) 4)
#define KEYSAFE_ERROR_UNKNOWN_ERROR   ((KeySafeError) 5)
#define KEYSAFE_ERROR_LOCKED          ((KeySafeError) 6)
#define KEYSAFE_ERROR_EMPTY           ((KeySafeError) 7)
#define KEYSAFE_ERROR_IO_ERROR        ((KeySafeError) 8)
#define KEYSAFE_ERROR_LOCATOR_ERROR   ((KeySafeError) 9)
#define KEYSAFE_ERROR_BAD_PARAMETER   ((KeySafeError) 10)

EXTERN const char *
KeySafeError_ToString(KeySafeError error);
EXTERN const char *
KeySafeError_ToMsgString(KeySafeError error);

static INLINE int
KeySafeError_ToInteger(KeySafeError error)
{
   return (int) error;
}

static INLINE KeySafeError
KeySafeError_FromInteger(int index)
{
   return (KeySafeError) index;
}

static INLINE Bool
KeySafeError_IsSuccess(KeySafeError error)
{
   return (KEYSAFE_ERROR_SUCCESS == error);
}

static INLINE Bool
KeySafeError_IsFailure(KeySafeError error)
{
   return (KEYSAFE_ERROR_SUCCESS != error);
}


/*
 * KeyLocator public types
 */

typedef struct KeySafeUserRing KeySafeUserRing;
typedef struct KeySafe KeySafe;


/*
 * KeyLocator public functions
 */

/* Creating and destroying. */

EXTERN KeySafeError
KeySafe_Create(const KeySafeUserRing *userRing,
               const uint8 *data,
               size_t dataSize,
               KeySafe **keySafe);

EXTERN KeySafeError
KeySafe_Clone(const KeySafe *oldKeySafe,
              KeySafe **newKeySafe);

EXTERN void
KeySafe_Destroy(KeySafe *keySafe);

/* Serializing and unserializing. */

EXTERN KeySafeError
KeySafe_Import(const char *input,
               size_t inputSize,
               KeySafe **keySafe);

EXTERN KeySafeError
KeySafe_Export(const KeySafe *keySafe,
               char **output,
               size_t *outputSize);

/* Convenience functions for serializing and unserializing. */

EXTERN KeySafeError
KeySafe_Unseal(KeyLocatorState *klState,
               const char *input,
               size_t inputSize,
               const KeySafeUserRing *userRing,
               KeySafe **keySafe,
               CryptoKey **key);

EXTERN KeySafeError
KeySafe_Seal(const KeySafeUserRing *userRing,
             CryptoKey **key,
             KeySafe **keySafe,
             char **exportedKeySafe,
             size_t *exportSize);

/* Unlocking and locking. */

EXTERN KeySafeError
KeySafe_Unlock(KeyLocatorState *klState,
               KeySafe *keySafe,
               const KeySafeUserRing *userRing);

EXTERN void
KeySafe_Lock(KeySafe *keySafe);

EXTERN KeySafeError
KeySafe_GetData(const KeySafe *keySafe,
                uint8 **data,
                size_t *dataSize);

/*
 * Managing locators. _GetLocator returns a pointer to the locator list that
 * contains all of the locator pairs. The caller can manipulate this list as
 * he wishes, but he can't add locators directly. To add locators, he must use
 * _AddLocators, which requires a user ring.
 */

EXTERN KeyLocator *
KeySafe_GetLocators(const KeySafe *keySafe);

EXTERN KeySafeError
KeySafe_AddLocators(KeySafe *keySafe,
                    const KeySafeUserRing *userRing);

EXTERN KeySafeError
KeySafe_RegenerateUserRing(KeySafe *keySafe,
                           KeyLocatorState *klState,
                           KeySafeUserRing **userRing);

/* user ring functions */

EXTERN KeySafeError
KeySafeUserRing_Create(KeySafeUserRing **userRing);

EXTERN KeySafeError
KeySafeUserRing_Export(const KeySafeUserRing *userRing,
                       Bool includePassphrases,
                       char **data,
                       size_t *dataSize);

EXTERN KeySafeError
KeySafeUserRing_Import(const char *data,
                       size_t dataSize,
                       KeySafeUserRing **userRing);

EXTERN KeySafeError
KeySafeUserRing_AddLocator(KeySafeUserRing *userRing,
                           const KeyLocator *locator,
                           const CryptoKey *key);
/*
 * _AddPassphrase/_AddKey: Wrappers around _AddLocator that relieve the caller
 * of the burden of having to manually create a locator and follow/extract the
 * crypto key.
 */

EXTERN KeySafeError
KeySafeUserRing_AddPassphrase(KeySafeUserRing *userRing,
                              const char *passphrase);

EXTERN KeySafeError
KeySafeUserRing_AddPassphraseWithCaching(
   KeySafeUserRing *userRing,
   const char *passphrase,
   KeyLocatorState *klState);

EXTERN KeySafeError
KeySafeUserRing_AddServerKeyWithCaching(KeySafeUserRing *userRing,
                                        CryptoKey *key,
                                        KeyLocatorState *klState);

EXTERN KeySafeError
KeySafeUserRing_AddKey(KeySafeUserRing *userRing,
                       CryptoKey *key);

EXTERN KeySafeError
KeySafeUserRing_AddRing(KeySafeUserRing *userRing,
                        const KeySafeUserRing *ringToAdd);

EXTERN Bool
KeySafeUserRing_IsEmpty(const KeySafeUserRing *userRing);

EXTERN KeySafeError
KeySafeUserRing_Clone(const KeySafeUserRing *userRing,
                      KeySafeUserRing **newUserRing);

EXTERN void
KeySafeUserRing_Destroy(KeySafeUserRing *userRing);

EXTERN uint32
KeySafeUserRing_GetNumKeys(const KeySafeUserRing *userRing);

EXTERN KeySafeError
KeySafeUserRing_GetKey(const KeySafeUserRing *userRing,
                       int index,
                       CryptoKey **key);

EXTERN KeySafeError
KeySafeUserRing_GetOnePassphrase(const KeySafeUserRing *userRing,
                                 char **passphrase);

#endif /* keySafe.h */
