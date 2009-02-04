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
 * cryptoRandom.h --
 *
 *      Random number generation for cryptographic support library.
 *
 *      NOTE: This header file is within the FIPS crypto boundary and
 *      thus should not change in such a way as to break the interface
 *      to the vmcryptolib library. See bora/make/mk/README_FIPS for
 *      more information.
 */

#ifndef VMWARE_CRYPTORANDOM_H
#define VMWARE_CRYPTORANDOM_H 1

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#include "vmware.h"

#include "cryptoError.h"

/*
 * Random number generation. Seeding is not required; if not done
 * manually, random bytes will be taken from the host. Pass NULL to
 * CryptoRandom_Seed to force seeding from host random data.
 */

EXTERN CryptoError
CryptoRandom_GetBytes(uint8 *buf,      // OUT
                      size_t bufSize); // IN

EXTERN size_t
CryptoRandom_GetSeedSize(void);

EXTERN CryptoError
CryptoRandom_Seed(const uint8 *seed,  // IN (OPT)
                  size_t seedSize);   // IN

#endif /* cryptoRandom.h */
