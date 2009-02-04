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

#ifndef VMWARE_CRYPTOFIPS_H
#define VMWARE_CRYPTOFIPS_H 1

#include "unicodeTypes.h"

/*
 * cryptoFips.h --
 *
 *      Functions relating to FIPS-compliance.
 *
 *      NOTE: This header file is within the FIPS crypto boundary and
 *      thus should not change in such a way as to break the interface
 *      to the vmcryptolib library. See bora/make/mk/README_FIPS for
 *      more information.
 */

/*
 * Library integrity utility functions. Not for general use.
 */

EXTERN CryptoError
CryptoFips_SignVMwareFile(ConstUnicode fileToSign,    // IN
                          const char *fileSigs,       // IN
                          const char *filePrivKey);   // IN

EXTERN CryptoError
CryptoFips_VerifyVMwareFile(ConstUnicode fileToVerify,  // IN
                            const char *fileSigs);      // IN

/*
 * Check if FIPS-mode is enabled, or enabled it.
 */

EXTERN Bool
CryptoFips_FipsModeEnabled(void);

EXTERN Bool
CryptoFips_EnableFipsMode(void);

/*
 * The following macros and functions allow callers to instruct the
 * vmcryptolib shared library to use the caller's own Log, Warning,
 * and Panic functions. Otherwise, vmcryptolib will use minimal
 * implementations of its own.
 */

typedef void (*CryptolibCBType)(const char *fmt, ...);

EXTERN void
CryptoFips_SetVmcryptolibCallbacksEx(CryptolibCBType logFn,      // IN
                                     CryptolibCBType warningFn,  // IN
                                     CryptolibCBType panicFn);   // IN

#define CryptoFips_SetVmcryptolibCallbacks() \
   CryptoFips_SetVmcryptolibCallbacksEx(Log, Warning, Panic);

#define CryptoFips_UnsetVmcryptolibCallbacks() \
   CryptoFips_SetVmcryptolibCallbacksEx(NULL, NULL, NULL);


/*
 *----------------------------------------------------------------------
 *
 * CryptoFips_InVmcryptolib()
 *
 *    Return TRUE if the caller is inside the vmcryptolib shared
 *    library, FALSE otherwise.
 *
 * Results:
 *    See above.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static INLINE Bool
CryptoFips_InVmcryptolib(void)
{
#ifdef VMCRYPTOLIB_BUILD
   return TRUE;
#else
   return FALSE;
#endif
}

#endif /* cryptoFips.h */
