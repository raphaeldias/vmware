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
 * cryptoPassword.h --
 *
 *      Password utility routines for cryptographic support library.
 *
 *      NOTE: This header file is within the FIPS crypto boundary and
 *      thus should not change in such a way as to break the interface
 *      to the vmcryptolib library. See bora/make/mk/README_FIPS for
 *      more information.
 */

#ifndef VMWARE_CRYPTOPASSWORD_H
#define VMWARE_CRYPTOPASSWORD_H 1

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#include "vmware.h"

#include "cryptoCipher.h"
#include "cryptoError.h"

/*
 * Algorithms for converting passwords to keys.
 */
typedef struct CryptoPass2Key CryptoPass2Key;   /* Password-to-key algorithm. */

/* PKCS#5 PBKDF2 using HMAC-SHA-1. */
#define CryptoPass2KeyName_PBKDF2_HMAC_SHA_1 "PBKDF2-HMAC-SHA-1"

EXTERN CryptoError
CryptoPass2Key_FromString(const char *string,         // IN
                          CryptoPass2Key **pass2key); // OUT

EXTERN const char *
CryptoPass2Key_ToString(CryptoPass2Key *pass2key);    // IN

EXTERN CryptoError
CryptoPass2Key_Compute(CryptoPass2Key *pass2key,      // IN
                       CryptoCipher *cipher,          // IN
                       int rounds,                    // IN
                       const uint8 *password,         // IN
                       size_t passwordLen,            // IN
                       uint8 **salt,                  // IN/OUT
                       size_t *saltSize,              // IN/OUT
                       uint8 **key,                   // OUT
                       size_t *keySize);              // OUT

EXTERN CryptoError
CryptoPass2Key_MakeKey(CryptoPass2Key *pass2key,      // IN
                       CryptoCipher *cipher,          // IN
                       int rounds,                    // IN
                       const uint8 *password,         // IN
                       size_t passwordLen,            // IN
                       uint8 **salt,                  // IN/OUT
                       size_t *saltSize,              // IN/OUT
                       CryptoKey **key);              // OUT

/*
 * Wrapping and unwrapping data using a password.
 */

EXTERN CryptoError
Crypto_PasswordWrapData(const char *password,         // IN
                        size_t passwordLength,        // IN
                        const uint8 *input,           // IN
                        size_t inputSize,             // IN
                        char **output,                // OUT
                        size_t *outputSize);          // OUT

EXTERN CryptoError
Crypto_PasswordUnwrapData(const char *password,       // IN
                          size_t passwordLength,      // IN
                          const char *input,          // IN
                          size_t inputSize,           // IN
                          uint8 **output,             // OUT
                          size_t *outputSize);        // OUT

/*
 * Miscellaneous.
 */

EXTERN char *
Crypto_GetPassword(const char *prompt,                // IN
                   Bool verify);                      // IN

EXTERN CryptoError
Crypto_ManglePassphrase(const char *passphrase,     // IN
                        size_t passphraseSize,      // IN
                        char **mangledPassphrase);  // OUT

/*
 * To avoid leaving password as plain text in memory, encrypt it before saving.
 */

typedef struct {
   uint8 *password;      /* encrypted password      */
   size_t  passwordLen;  /* length after encryption */
   CryptoKey *key;       /* encryption key          */
} EncryptedPassword;

EXTERN Bool
Crypto_EncryptPassword(const void *plainPwd,        // IN
                       const size_t size,           // IN
                       EncryptedPassword *encpwd);  // IN/OUT

EXTERN void *
Crypto_DecryptPassword(const EncryptedPassword *encpwd, size_t *size);

EXTERN void
Crypto_ClearEncryptedPassword(EncryptedPassword *encpwd); // IN/OUT

EXTERN void
Crypto_InitializeEncryptedPassword(EncryptedPassword *encpwd); // IN/OUT

#endif /* cryptoPassword.h */
