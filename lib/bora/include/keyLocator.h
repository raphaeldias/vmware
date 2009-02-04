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
 * keyLocator.h --
 *
 *       Interface to the KeyLocator module.
 *
 * A KeyLocator is an object that identifies a cryptographic key, a
 * collection of other key locators, or a piece of data protected by
 * another key locator.
 *
 * KeyLocators come in many different types, which are organized into
 * three different classes, the classes being 'atomic', 'indirect', and
 * 'compound'.
 *
 * An atomic key locator is just a thin wrapper around an exported
 * CryptoKey object. Aside from the null locator (which represents no
 * key), there is one type of locator in the atomic class - type 'key'.
 * Since there is no additional security protecting the contents of an
 * atomic key locator (aside from the use of a hard-coded passphrase
 * used to obfuscate the exported key), an atomic key locator should be
 * considered senstive data.
 *
 * A CryptoKey can be extracted from an atomic key locator with
 * KeyLocator_Extract.
 *
 * An indirect key locator describes how to find another key
 * locator. There are several varied types of indirect key locators,
 * currently 'passphrase', 'ldap', 'script', and 'role'. Callers can
 * follow an indirect key locator to the locator that it points to with
 * KeyLocator_Follow or KeyLocator_FollowExtract.
 *
 * The caller must supply a callback function that can actually perform
 * whatever labor is necessary to follow the indirect locator (either to
 * KeyLocator_AllocState or to the follow functions directly). This can
 * mean making an LDAP connection, asking the user for a passphrase,
 * etc. The requirements of the callback function are described below.
 *
 * If a NULL callback function was given to KeyLocator_AllocState, and
 * subsequent calls to the follow functions also specify NULL callbacks,
 * then any attempt to follow an indirect locator will result in
 * failure.
 *
 * A compound key locator can either be of the 'list' or 'pair' types.
 *
 * A list locator is simply a list of other locators. There are
 * functions provided to manipulate the locators in a list locator. When
 * a locator is added to a list, the list takes ownership of
 * it. Ownership is released when the locator is removed. Destroying a
 * list locator destroys all locators in the list, and cloning a list
 * locator clones all locators in the list.
 *
 * A 'pair' locator consists of one locator (called the 'locker') of any
 * type plus a blob of encrypted data (called the 'locked data'). If
 * constructed correctly, the behavior is that the key producted by
 * following the locker is able to unlock the locked data.
 *
 * KeyLocator_CreatePair can be used to create a pair locator. The
 * locker locator and crypto key specified are duplicated and do not
 * become owned by the pair locator. KeyLocator_GetPair returns an
 * opaque pointer to an internal data structure of a pair locator that
 * allows the caller to access the locker locator and the locked
 * data. KeyLocator_UnlockPair can be used to decrypt the data protected
 * by the pair.
 *
 * Pair locators are provided to facilitate the construction of a data
 * structure called a KeySafe, which is a list of pairs each of which
 * contains the same piece of data locked by a different locker. The
 * KeySafe module uses (will use) key locators to implement KeySafe
 * objects.
 *
 * KeyLocator_Export exports a locator into a URL (as defined by the
 * relevant RFCs) of the form "vmware:key<etc>". This is done not so
 * much because there are any practical applications today for this, but
 * to take advantage of any future opportunities to leverage
 * applications or code that can manipulate URLs.
 *
 * The KeyLocator module can optionally use a cache to remember the
 * results of KeyLocator_FollowExtract. If used, then the next time a
 * cached locator is passed to this function, the callback is not
 * invoked. Instead, the final target atomic locator is pulled directly
 * from the cache. Only indirect locators can be cached.
 *
 * This caching is possible because cacheable locators are given a
 * unique ID when created. Since there is always the possibility of a
 * collision between unique IDs (however remote), callers should plan
 * for this. For example, if a caller attempts to unlock a pair with a
 * FollowExtracted'd key that should work, and the unlock fails, then
 * the caller should call FollowExtract again with the cache disabled on
 * the assumption that the wrong key was returned from the cache.
 *
 * Anywhere a KeyLocator API takes a KeyLocatorState pointer, NULL can
 * also be passed-in. This will disable use of the cache and cause all
 * follows of indirect locators to fail.
 *
 */


#ifndef _KEYLOCATOR_H_
#define _KEYLOCATOR_H_


/*
 * KeyLocator includes
 */

#include "crypto.h"


/*
 * KeyLocator error codes
 */

typedef int KeyLocError;

#define KEYLOC_ERROR_SUCCESS              ((KeyLocError) 0)
#define KEYLOC_ERROR_NOMEM                ((KeyLocError) 1)
#define KEYLOC_ERROR_UNKNOWN_ERROR        ((KeyLocError) 2)
#define KEYLOC_ERROR_CACHE_NOT_ENABLED    ((KeyLocError) 3)
#define KEYLOC_ERROR_CACHE_DUPE_ENTRY     ((KeyLocError) 4)
#define KEYLOC_ERROR_BAD_EXPORTED_CACHE   ((KeyLocError) 5)
#define KEYLOC_ERROR_WRONG_LOCATOR_TYPE   ((KeyLocError) 6)
#define KEYLOC_ERROR_BAD_EXPORTED_KL      ((KeyLocError) 7)
#define KEYLOC_ERROR_BAD_LOCATOR_TYPE     ((KeyLocError) 8)
#define KEYLOC_ERROR_INVALID_URL_ELEM     ((KeyLocError) 9)
#define KEYLOC_ERROR_CRYPTO_ERROR         ((KeyLocError) 10)
#define KEYLOC_ERROR_NOT_INITED           ((KeyLocError) 11)
#define KEYLOC_ERROR_NULL_CALLBACK        ((KeyLocError) 12)

EXTERN const char *
KeyLocError_ToString(KeyLocError error);

static INLINE int
KeyLocError_ToInteger(KeyLocError error)
{
   return (int) error;
}

static INLINE KeyLocError
KeyLocError_FromInteger(int index)
{
   return (KeyLocError) index;
}

static INLINE Bool
KeyLocError_IsSuccess(KeyLocError error)
{
   return (KEYLOC_ERROR_SUCCESS == error);
}

static INLINE Bool
KeyLocError_IsFailure(KeyLocError error)
{
   return (KEYLOC_ERROR_SUCCESS != error);
}


/*
 * KeyLocator public constants
 */

/* identifiers for different classes of locators */
typedef enum KeyLocatorClass {
   KEYLOCATOR_CLASS_INVALID,
   KEYLOCATOR_CLASS_ATOMIC,
   KEYLOCATOR_CLASS_INDIRECT,
   KEYLOCATOR_CLASS_COMPOUND
} KeyLocatorClass;

/* identifiers for different types of locators */

typedef enum KeyLocatorType {
   KEYLOCATOR_TYPE_INVALID,     /* invalid type */

   // atomic
   KEYLOCATOR_TYPE_NULL,        /* the null key locator */
   KEYLOCATOR_TYPE_KEY,         /* encodes a key directly */

   // indirect
   KEYLOCATOR_TYPE_PASSPHRASE,  /* generates a key from a passphrase */
   KEYLOCATOR_TYPE_LDAP,        /* data in an LDAP server */
   KEYLOCATOR_TYPE_SCRIPT,      /* get key from external script */
   KEYLOCATOR_TYPE_ROLE,        /* data at a well known location */

   // compound
   KEYLOCATOR_TYPE_LIST,        /* list of KLs (KeySafe possibly) */
   KEYLOCATOR_TYPE_PAIR         /* A KL and associated encrypted data */
} KeyLocatorType;

/* identifiers for well-known keys managed by this module */
typedef enum KeyLocatorRole {
   KEYLOCATOR_ROLE_OBFUSCATION,
   KEYLOCATOR_ROLE_ADMIN_IDENT,
   KEYLOCATOR_ROLE_ADMIN_RECOVERY,
   KEYLOCATOR_ROLE_SERVER
} KeyLocatorRole;


/*
 * KeyLocator public types
 */

typedef struct KeyLocatorState KeyLocatorState;
typedef struct KeyLocator KeyLocator;

/* external representation of the contents of an indirect locator */
typedef struct KeyLocatorIndirect {
   KeyLocatorType type;
   char *uniqueId;

   union {
      // type KEYLOCATOR_TYPE_PASSPHRASE:
      struct KeyLocatorPassphraseParams {
         char *keyGenData;  // data to help generate key from passphrase
         size_t keyGenDataSize;
      } phrase;

      // type KEYLOCATOR_TYPE_LDAP:
      struct KeyLocatorLDAPParams {
         char *server;
         char *domain;
         uint32 port;
         Bool useSSL;
         char *path;
      } ldap;

      // type KEYLOCATOR_TYPE_SCRIPT:
      struct KeyLocatorScriptParams {
         char *relPath;    // relative to some arbitrary location
         char *signature;  // script signature, signed with admin key
         size_t signatureSize;
      } script;

      // type KEYLOCATOR_TYPE_ROLE:
      KeyLocatorRole role;
   } u;
} KeyLocatorIndirect;

/* external representation of the contents of a pair locator */
typedef struct KeyLocatorPair {
   KeyLocator *locker;          // identifies key that locks the data
   CryptoKeyedHash *cryptoMAC;  // MAC used during the encryption
   uint8 *lockedData;           // the encrypted/locked data
   size_t lockedDataSize;
} KeyLocatorPair;

/*
 * Function type for indirect locator callback. The data returned by this
 * callback depends on the type of locator:
 *
 * KEYLOCATOR_TYPE_PASSPHRASE - data is a passphrase
 * KEYLOCATOR_TYPE_LDAP - data is an exported KeyLocator
 * KEYLOCATOR_TYPE_SCRIPT - data is an exported KeyLocator
 * KEYLOCATOR_TYPE_ROLE - data is an exported KeyLocator
 *
 */

typedef Bool (*KeyLocatorIndirectCallback)(
   KeyLocatorState *klState,       // IN (OPT): state
   void *context,                  // IN: callback context
   const KeyLocatorIndirect *kli,  // IN: describes how to follow
   uint8 **data,                   // OUT: result of the follow
   size_t *size);                  // OUT: size of data


/*
 * KeyLocator public functions
 */

/*
 * KeyLocator_AllocState/FreeState: State alloc/destroy functionality.
 */

EXTERN Bool
KeyLocator_AllocState(KeyLocatorIndirectCallback callback,
                      void *callbackContext,
                      KeyLocatorState **klState);

EXTERN void
KeyLocator_FreeState(KeyLocatorState *klState);

/*
 * KeyLocator_Enable/DisableKeyCache: Enables/disables use of the internal
 * KeyLocator key cache.
 */

EXTERN void
KeyLocator_EnableKeyCache(KeyLocatorState *klState);

EXTERN void
KeyLocator_DisableKeyCache(KeyLocatorState *klState);

EXTERN void
KeyLocator_ClearKeyCache(KeyLocatorState *klState);

/*
 * KeyLocator_Import/ExportKeyCache: Imports/Exports the internal KeyLocator
 * key cache.
 */

EXTERN KeyLocError
KeyLocator_ExportKeyCache(KeyLocatorState *klState,
                          char **cache,
                          size_t *size);

EXTERN KeyLocError
KeyLocator_ImportKeyCache(KeyLocatorState *klState,
                          const char *cache,
                          size_t size);

/*
 * KeyLocator_Import, KeyLocator_Export: Create from and serialize to string
 * representation in the form of URL.
 */

EXTERN KeyLocError
KeyLocator_Export(const KeyLocator *kl, char **s);

EXTERN KeyLocError
KeyLocator_Import(const char *s, KeyLocator **kl);

/*
 * KeyLocator_Clone: Clones a locator.
 */

EXTERN KeyLocError
KeyLocator_Clone(const KeyLocator *kl, KeyLocator **clone);

/*
 * KeyLocator_Destroy: Frees a locator.
 */

EXTERN void
KeyLocator_Destroy(KeyLocator *kl);

/*
 * KeyLocator_GetClass, KeyLocator_GetType, KeyLocator_GetIndirect: For
 * getting information about a KeyLocator.
 */

EXTERN KeyLocatorClass
KeyLocator_GetClass(const KeyLocator *kl);

EXTERN KeyLocatorType
KeyLocator_GetType(const KeyLocator *kl);

EXTERN const KeyLocatorIndirect *
KeyLocator_GetIndirect(const KeyLocator *kl);

/*
 * For resolving Atomic KeyLocators:
 *
 * KeyLocator_Extract: Resolve a Leaf locator to a key.
 */

EXTERN KeyLocError
KeyLocator_Extract(const KeyLocator *kl, CryptoKey **key);


/*
 * Expose a pair.
 */

EXTERN const KeyLocatorPair *
KeyLocator_GetPair(const KeyLocator *kl);


/*
 * KeyLocator_Create*: Ways to create key locators.
 */

EXTERN KeyLocError
KeyLocator_CreateNull(KeyLocator **kl);

EXTERN KeyLocError
KeyLocator_CreateLeaf(const CryptoKey *key, KeyLocator **kl);

EXTERN KeyLocError
KeyLocator_CreateLinkToPassphrase(KeyLocator **kl);

EXTERN KeyLocError
KeyLocator_CreateLinkToLDAP(const char *server,
                            const char *domain,
                            uint32 port,
                            Bool useSSL,
                            const char *path,
                            KeyLocator **kl);

EXTERN KeyLocError
KeyLocator_CreateLinkToScript(const char *relPath,
                              const char *signature,
                              size_t signatureSize,
                              KeyLocator **kl);

EXTERN KeyLocError
KeyLocator_CreateLinkToRole(KeyLocatorRole role, KeyLocator **kl);

EXTERN KeyLocError
KeyLocator_CreatePair(const KeyLocator *kl,
                      const CryptoKey *key,
                      const uint8 *data,
                      size_t sizeData,
                      KeyLocator **klPair);

EXTERN KeyLocError
KeyLocator_CreateList(KeyLocator **klList);

/*
 * KeyLocator_*List*: List manipulation functions. When a locator is added to
 * a list locator, it becomes owned by the list locator until removed. If a
 * list locator is destroyed, all locators in the list go also.
 *
 * List is not re-ordered after an element is removed. That is, there is
 * no need to rewind to the head after calling _ListRemove, one can just
 * proceed to the next element.
 */

EXTERN Bool
KeyLocator_ListIsEmpty(const KeyLocator *klList);

EXTERN KeyLocator *
KeyLocator_ListFirst(const KeyLocator *klList);

EXTERN KeyLocator *
KeyLocator_ListLast(const KeyLocator *klList);

EXTERN KeyLocator *
KeyLocator_ListNext(const KeyLocator *klList, const KeyLocator *klElem);

EXTERN KeyLocator *
KeyLocator_ListPrev(const KeyLocator *klList, const KeyLocator *klElem);

EXTERN Bool
KeyLocator_ListRemove(KeyLocator *klList, KeyLocator *klElem);

EXTERN Bool
KeyLocator_ListAddAfter(KeyLocator *klList,
                        KeyLocator *klElem,
                        KeyLocator *klNewElem);

EXTERN Bool
KeyLocator_ListAddBefore(KeyLocator *klList,
                         KeyLocator *klElem,
                         KeyLocator *klNewElem);

EXTERN Bool
KeyLocator_ListAddFirst(KeyLocator *klList, KeyLocator *klNewElem);

EXTERN Bool
KeyLocator_ListAddLast(KeyLocator *klList, KeyLocator *klNewElem);


/*
 * KeyLocator_Follow: Follows an indirect key locator.
 * KeyLocator_FollowExtract: Does Follow until atomic, then Extract.
 */

EXTERN KeyLocError
KeyLocator_Follow(KeyLocatorState *klState,
                  const KeyLocator *kl,
                  KeyLocatorIndirectCallback callback,
                  void *callbackContext,
                  KeyLocator **klTarget);

EXTERN KeyLocError
KeyLocator_FollowExtract(KeyLocatorState *klState,
                         const KeyLocator *kl,
                         Bool useCache,
                         KeyLocatorIndirectCallback callback,
                         void *callbackContext,
                         CryptoKey **key);


/*
 * KeyLocator_UnlockPair: Decrypts the data locked by the pair.
 */

EXTERN KeyLocError
KeyLocator_UnlockPair(KeyLocatorState *klState,
                      const KeyLocator *klPair,
                      const CryptoKey *key,
                      Bool useCache,
                      uint8 **data,
                      size_t *size);

#endif // _KEYLOCATOR_H_
