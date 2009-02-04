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

#ifndef VMWARE_CRYPTO_H
#define VMWARE_CRYPTO_H 1

/*
 * crypto.h --
 *
 *      Cryptographic support library.
 *
 *      NOTE: All these header files are within the FIPS crypto
 *      boundary and thus should not change in such a way as to break
 *      the interface to the vmcryptolib library. See
 *      bora/make/mk/README_FIPS for more information.
 */

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#include "vmware.h"

#include "cryptoBuffer.h"
#include "cryptoCipher.h"
#include "cryptoDict.h"
#include "cryptoError.h"
#include "cryptoFips.h"
#include "cryptoHash.h"
#include "cryptoPassword.h"
#include "cryptoRandom.h"
#include "cryptoSector.h"

#endif /* crypto.h */
