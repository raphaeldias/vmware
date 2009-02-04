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
 * cryptoCipher.h --
 *
 *      Ciphers and keys for cryptographic support library.
 *
 *      NOTE: This header file is within the FIPS crypto boundary and
 *      thus should not change in such a way as to break the interface
 *      to the vmcryptolib library. See bora/make/mk/README_FIPS for
 *      more information.
 */

#ifndef VMWARE_CRYPTOCIPHER_H
#define VMWARE_CRYPTOCIPHER_H 1

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#include "vmware.h"

#include "cryptoError.h"
#include "cryptoHash.h"

/*
 * Cipher algorithms.
 */
typedef struct CryptoCipher CryptoCipher;     /* A cipher. */

/* Symmetric cipher: 128-bit AES. */
#define CryptoCipher_AES_128_KeySize 16         /* Key size in bytes. */
#define CryptoCipher_AES_128_IVSize 16          /* IV size in bytes. */ 
#define CryptoCipherName_AES_128 "AES-128"      /* Name. */             

/* Symmetric cipher: 192-bit AES. */
#define CryptoCipher_AES_192_KeySize 24         /* Key size in bytes. */
#define CryptoCipher_AES_192_IVSize 16          /* IV size in bytes. */ 
#define CryptoCipherName_AES_192 "AES-192"      /* Name. */             

/* Symmetric cipher: 256-bit AES. */
#define CryptoCipher_AES_256_KeySize 32         /* Key size in bytes. */
#define CryptoCipher_AES_256_IVSize 16          /* IV size in bytes. */ 
#define CryptoCipherName_AES_256 "AES-256"      /* Name. */             

/* Predefined RSA public-key ciphers. */
#define CryptoCipherName_RSA_512 "RSA-512"      /* RSA-512 over AES-128. */
#define CryptoCipherName_RSA_1024 "RSA-1024"    /* RSA-1024 over AES-128. */
#define CryptoCipherName_RSA_2048 "RSA-2048"    /* RSA-2048 over AES-128. */
#define CryptoCipherName_RSA_4096 "RSA-4096"    /* RSA-4096 over AES-128. */

/*
 * Cipher types.
 */
typedef enum CryptoCipher_Type {
   CRYPTO_CIPHER_INVALID,               /* Invalid/unknown. */
   CRYPTO_CIPHER_SYMMETRIC,             /* Symmetric ciphers. */
   CRYPTO_CIPHER_PUBLIC,                /* Public-key ciphers. */
} CryptoCipher_Type;

EXTERN CryptoError
CryptoCipher_FromString(const char *string,           // IN
                        CryptoCipher **cipher);       // OUT

EXTERN const char *
CryptoCipher_ToString(CryptoCipher *cipher);          // IN

EXTERN CryptoCipher_Type
CryptoCipher_GetType(CryptoCipher *cipher);           // IN

EXTERN size_t
CryptoCipher_GetIVSize(CryptoCipher *cipher);         // IN

/*
 * A cipher configured with a key.
 */
typedef struct CryptoKey CryptoKey;           /* A cipher with a key. */

EXTERN CryptoError
CryptoKey_Create(CryptoCipher *cipher,                // IN
                 const uint8 *keyData,                // IN
                 size_t keyDataSize,                  // IN
                 CryptoKey **key);                    // OUT

EXTERN CryptoKey *
CryptoKey_Clone(const CryptoKey *old);                // IN

EXTERN CryptoError
CryptoKey_Generate(CryptoCipher *cipher,              // IN
                   CryptoKey **key);                  // OUT

EXTERN CryptoError
CryptoKey_Export(const CryptoKey *key,                // IN
                 const char *password,                // IN
                 char **output,                       // OUT
                 size_t *outputSize);                 // OUT

EXTERN CryptoError
CryptoKey_Import(const char *input,                   // IN
                 size_t inputSize,                    // IN
                 const char *password,                // IN
                 CryptoKey **key);                    // OUT

EXTERN void
CryptoKey_Free(CryptoKey *key);                       // IN/OUT

EXTERN CryptoCipher *
CryptoKey_GetCipher(const CryptoKey *key);            // IN

/*
 * High-level encryption and decryption interface.
 *
 * These functions provide both confidentiality and integrity, and they are
 * designed to be difficult to misuse, so they are preferred over the
 * lower-level routines.
 *
 * These functions support both symmetric and public-key cryptography.  For
 * public keys, they generate a random symmetric key, encrypt it with the
 * public key, then encrypt the data with that symmetric key.
 *
 * There are two reasons not to use these routines:
 *
 *      - Data expansion: With symmetric keys, the ciphertext is 37 to 52 bytes
 *        longer than the plaintext with a cipher that has 128-bit blocks
 *        (e.g. AES-128, AES-192, AES-256) and a 160-bit keyed hash
 *        (e.g. HMAC-SHA-1).  Data expansion cannot be tolerated in some
 *        situations.
 *
 *      - Inflexibility: The input must be in a single contiguous buffer.
 *        The output is also placed into a single contiguous buffer
 *        allocated by the callee.  This is not normally a serious problem;
 *        if it becomes one then providing a more flexible interface is
 *        probably a better idea than resorting to lower-level routines.
 *
 * Note: the recommended keyedHash for use with these functions is still
 * CryptoKeyedHash_HMAC_SHA_1.  The usage of a secret key means that the
 * attacks on SHA-1 do not affect HMAC-SHA-1.  We should probably transition to
 * HMAC-SHA-256 someday, but in the meantime there is no need to break backward
 * compatibility.
 */

EXTERN CryptoError
CryptoKey_EncryptWithMAC(const CryptoKey *key,                  // IN
                         CryptoKeyedHash *keyedHash,            // IN
                         const uint8 *plainText,                // IN
                         size_t plainTextSize,                  // IN
                         uint8 **cipherText,                    // OUT
                         size_t *cipherTextSize);               // OUT

EXTERN CryptoError                  
CryptoKey_DecryptWithMAC(const CryptoKey *key,                  // IN
                         CryptoKeyedHash *keyedHash,            // IN
                         const uint8 *cipherText,               // IN
                         size_t cipherTextSize,                 // IN
                         uint8 **plainText,                     // OUT
                         size_t *plainTextSize);                // OUT

/*
 * Mid-level encryption and decryption interface.
 *
 * When used properly, these functions provide confidentiality.  They do not
 * directly provide any form of integrity checking.  Use them if the high-level
 * interface is not suitable, but be aware of the pitfalls:
 *
 *      - In CBC mode, the initialization vector should be randomly selected.
 *        Reusing an IV may allow an adversary to determine whether two
 *        ciphertexts correspond to the same plaintext.  Using sequential IVs
 *        can also lower security (instead, use ECB-mode encryption to convert
 *        sequential IVs into random ones; see below).
 *
 *      - CBC mode *should* be combined with integrity checking.  If it is not,
 *        then an adversary can mount a "splicing" attack in which complete
 *        blocks of ciphertext are moved around.  This produces two corrupted
 *        blocks of plaintext, one at each end of the splice, but plaintext
 *        in the middle is successfully copied verbatim.  Only integrity
 *        checking (e.g. with a keyed hash) can defeat this attack.
 *
 *      - In CTR mode, byteOffset values should not be reused.  Writing data
 *        at a given byteOffset and then rewriting it is a big mistake: if the
 *        adversary can guess one plaintext then he can trivially discover the
 *        other with a simple XOR operation.
 *
 *      - CTR mode *must* be combined with integrity checking.  If an adversary
 *        can guess plaintext, then he can modify the ciphertext so that it
 *        decrypts to whatever he wants, again trivially with a simple XOR.
 *        Only integrity checking (e.g. with a keyed hash) can defeat this
 *        attack.
 */

EXTERN CryptoError
CryptoKey_CBCEncrypt(const CryptoKey *key,            // IN
                     uint8 *iv,                       // IN/OUT
                     size_t ivSize,                   // IN
                     const uint8 *plainText,          // IN
                     uint8 *cipherText,               // OUT
                     size_t textSize);                // IN

EXTERN CryptoError
CryptoKey_CBCDecrypt(const CryptoKey *key,            // IN
                     uint8 *iv,                       // IN/OUT
                     size_t ivSize,                   // IN
                     const uint8 *cipherText,         // IN
                     uint8 *plainText,                // OUT
                     size_t textSize);                // IN

EXTERN CryptoError
CryptoKey_CTREncrypt(const CryptoKey *key,            // IN
                     uint64 byteOffset,               // IN
                     const uint8 *plainText,          // IN
                     uint8 *cipherText,               // OUT
                     size_t textSize);                // IN

EXTERN CryptoError
CryptoKey_CTRDecrypt(const CryptoKey *key,            // IN
                     uint64 byteOffset,               // IN
                     const uint8 *cipherText,         // IN
                     uint8 *plainText,                // OUT
                     size_t textSize);                // IN

/*
 * Low-level encryption and decryption interface.
 *
 * This interface uses the cipher directly.  It is difficult to use to
 * provide confidentiality and does not provide any integrity.  In general,
 * don't use this interface.
 *
 * There is only one relatively common situation where this interface is
 * appropriate: for generating good IVs from a counter.  A sequential counter
 * should not generally be used as an IV; e.g. see RFC 2405:
 *      
 *      "The problem arises if you use a counter as an IV, or some other
 *      source with a low Hamming distance between successive IVs, for
 *      encryption in CBC mode.  In CBC mode, the "effective plaintext" for
 *      an encryption is the XOR of the actual plaintext and the ciphertext
 *      of the preceeding block.  Normally, that's a random value, which
 *      means that the effective plaintext is quite random.  That's good,
 *      because many blocks of actual plaintext don't change very much from
 *      packet to packet, either.
 *
 *      For the first block of plaintext, though, the IV takes the place of
 *      the previous block of ciphertext.  If the IV doesn't differ much from
 *      the previous IV, and the actual plaintext block doesn't differ much
 *      from the previous packet's, then the effective plaintext won't differ
 *      much, either.  This means that you have pairs of ciphertext blocks
 *      combined with plaintext blocks that differ in just a few bit
 *      positions.  This can be a wedge for assorted cryptanalytic attacks."
 *
 * A simple way to fix the problem of a counter being a low Hamming distance
 * source is to pass it through an ECB encryption using these functions.
 *
 * See GenerateIV in lib/disklib/encryption.c for an example of usage.
 */

EXTERN CryptoError
CryptoKey_ECBEncrypt(const CryptoKey *key,            // IN
                     const uint8 *plainText,          // IN
                     uint8 *cipherText,               // OUT
                     size_t textSize);                // IN

EXTERN CryptoError
CryptoKey_ECBDecrypt(const CryptoKey *key,            // IN
                     const uint8 *cipherText,         // IN
                     uint8 *plainText,                // OUT
                     size_t textSize);                // IN

/*
 * CryptoKey_PK(En/De)crypt are specialized functions that directly
 * encrypt or decrypt the specific data with the appropriate half of
 * an asymmetric key. (Encrypt uses the public half, Decrypt the
 * private half.) For most purposes, CryptoKey(En/De)cryptWithMAC or
 * CryptoKey_(Sign/Verify) are more appropriate.
 */

EXTERN CryptoError
CryptoKey_PKEncrypt(const CryptoKey *key,          // IN
                    const uint8 *plainText,        // IN
                    size_t plainTextSize,          // IN
                    uint8 **cipherText,            // OUT
                    size_t *cipherTextSize);       // OUT

EXTERN CryptoError
CryptoKey_PKDecrypt(const CryptoKey *key,          // IN
                    const uint8 *cipherText,       // IN
                    size_t *cipherTextSize,        // IN/OUT
                    uint8 **plainText,             // OUT
                    size_t *plainTextSize);        // OUT

EXTERN void
CryptoKey_GetKeyData(const CryptoKey *key,            // IN
                     const uint8 **keyData,           // OUT
                     size_t *keyDataSize);            // OUT

EXTERN Bool
CryptoKey_HasPrivateKey(const CryptoKey *key);        // IN

EXTERN CryptoError
CryptoKey_GetPublicKey(const CryptoKey *privateKey,   // IN
                       CryptoKey **publicKey);        // OUT

/*
 * Public-key signature generation.
 */

typedef struct CryptoSignState CryptoSignState;

EXTERN CryptoError
CryptoKey_SignStart(CryptoHash *hash,                   // IN
                    CryptoSignState **signState);       // OUT

EXTERN void
CryptoKey_SignProcess(CryptoSignState *signState,       // IN/OUT
                      const uint8 *input,               // IN
                      size_t inputSize);                // IN

EXTERN CryptoError
CryptoKey_SignFinish(CryptoSignState *signState,        // IN/OUT
                     const CryptoKey *privateKey,       // IN
                     uint8 **signature,                 // OUT
                     size_t *signatureSize);            // OUT

/* Convenient wrapper for above three functions. */
EXTERN CryptoError
CryptoKey_Sign(const CryptoKey *privateKey,             // IN
               const uint8 *data,                       // IN
               size_t dataSize,                         // IN
               CryptoHash *,                            // IN
               uint8 **signature,                       // OUT
               size_t *signatureSize);                  // OUT

/*
 * Public-key signature verification.
 */

typedef struct CryptoVerifyState CryptoVerifyState;

EXTERN CryptoError
CryptoKey_VerifyStart(CryptoHash *hash,                 // IN
                      CryptoVerifyState **verifyState); // OUT

EXTERN void
CryptoKey_VerifyProcess(CryptoVerifyState *verifyState, // IN/OUT
                        const uint8 *input,             // IN
                        size_t inputSize);              // IN

EXTERN CryptoError
CryptoKey_VerifyFinish(CryptoVerifyState *verifyState,  // IN/OUT
                       const CryptoKey *publicKey,      // IN
                       const uint8 *signature,          // IN
                       size_t signatureSize);           // IN

/* Convenient wrapper for above three functions. */
EXTERN CryptoError
CryptoKey_Verify(const CryptoKey *publicKey,            // IN
                 const uint8 *data,                     // IN
                 size_t dataSize,                       // IN
                 CryptoHash *,                          // IN
                 const uint8 *signature,                // IN
                 size_t signatureSize);                 // IN


#endif /* cryptoCipher.h */
