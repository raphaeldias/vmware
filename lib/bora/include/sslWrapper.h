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
 * sslWrapper.h --
 *
 *      Define the wrapper function prefix name for Windows and Linux and
 *      generate wrapper bodies using the VMW_SSL_WRAPPER_BODY macro provided
 *      by our includer.
 */

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/aes.h>
#include <openssl/des.h>
#include <openssl/crypto.h>
#include <openssl/md5.h>
#include <openssl/md4.h>
#include <openssl/pkcs12.h>
#include <openssl/x509v3.h>

#include "sslFunctionList.h"

/*
 * Define the SSL wrapper functions prefix:
 *
 *      - On Linux, the SSL functions are declared as e.g. __wrap_SSL_new,
 *        __wrap_BIO_read etc. and at link time we use the --wrap argument to
 *        the GNU ld linker to indicate the presence of these wrappers. If we
 *        were to just provide the wrappers using the same names as the
 *        original functions, even the real SSL DLLs would start using our
 *        wrappers in any programs that are linked with --export-dynamic.
 *
 *        Unfortunately we can't use vmware_ as the prefix name because the
 *        --wrap argument hardcodes the __wrap_ prefix. --defsym doesn't halp
 *        us, because --defsym only allows you to redirect an unresolved
 *        symbol to a fully resolved one; it does *not* allow you to merely
 *        transform the name of an unresolved symbol.
 *
 *      - On Windows, the wrapper functions are prefixed with vmware_. Since
 *        the Windows linker doesn't provide the equivalent of --wrap, if you
 *        need to provide functions with the canonical names e.g. when
 *        compiling code that references SSL symbols directly, you can compile
 *        in lib/sslRemap to redirect the functions appropriately.
 */
#ifndef _WIN32
#define VMW_SSL_WRAPPER_NAME(_func) __wrap_##_func
#else
#define VMW_SSL_WRAPPER_NAME(_func) vmware_##_func
#endif

/*
 * Declare the SSL wrapper functions
 */
#define VMW_SSL_FUNC(_lib, _rettype, _func, _args, _argnames) \
   _rettype VMW_SSL_WRAPPER_NAME(_func) _args;
VMW_SSL_FUNCTIONS
#undef VMW_SSL_FUNC

/*
 * Define the SSL wrapper functions based on the body template provided by our
 * includer.
 */
#define VMW_SSL_FUNC(_lib, _rettype, _func, _args, _argnames) \
   VMW_SSL_WRAPPER_BODY(_rettype, return, _func, _args, _argnames)
VMW_SSL_RET_FUNCTIONS
#undef VMW_SSL_FUNC

#define VMW_SSL_FUNC(_lib, _rettype, _func, _args, _argnames) \
   VMW_SSL_WRAPPER_BODY(void, , _func, _args, _argnames)
VMW_SSL_VOID_FUNCTIONS
#undef VMW_SSL_FUNC
