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

#ifdef __linux__
#define _GNU_SOURCE /* dladdr */
#endif
#include <stdlib.h>
#include <string.h>
#if !_WIN32
#include <fcntl.h>
#include <unistd.h>
#else
#include <windows.h>
#include <winsock.h>
#include <wincrypt.h>
#include "win32util.h"
#include "win32auth.h"
#endif
#include <errno.h> 
#include <stdio.h>
#include <stddef.h>

#include "vmware.h"
#include "ssl.h"
#include "vm_version.h"
#include "file.h"
#include "posix.h"
#if defined(_WIN32)
#include "win32u.h"
#endif
#include "su.h"
#include "safetime.h"
#include "syncRecMutex.h"
#include "err.h"
#include "util.h"
#include "crypto.h"
#include "str.h"
#include "unicode.h"

#define LOGLEVEL_MODULE none
#include "loglevel_user.h"

#include "mallocTracker.h"

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

// #define USE_SSL_DEBUG

#include "sslFunctionList.h"

struct SSLSockStruct {
   SSL *sslCnx;
   int fd;
   Bool encrypted;
   Bool closeFdOnShutdown;
   Bool connectionFailed;
#ifdef VMX86_DEVEL
   int initialized;
#endif /*VMX86_DEVEL */
#ifdef __APPLE__
   Bool loggedKernelReadBug;
#endif

#ifdef __APPLE_READ_BUG_WORKAROUND__
   SSLLibHandleErrorHookFn *errorHook;
   void *errorHookContext;
#endif

   IOState ioState;
   int sslIOError;
   SyncRecMutex spinlock;
};

#ifndef _WIN32
#include <dlfcn.h>
#endif

/*
 * The SSL wrapper functions
 *
 *      This module loads all the SSL symbols we use at runtime from one of
 *      several possible locations. The symbols are stashed away in a set of
 *      function pointers, and exposed by static wrapper functions which call
 *      through these pointers and blow up semi-gracefully if the pointers are
 *      invalid.
 *
 *      We use various tricks to make our wrapper functions appear to be the
 *      canonical SSL functions vis-a-vis the rest of the codebase. This gives
 *      us the best of both worlds: most code can happily call SSL functions
 *      by their normal names, but get invisibly redirected through our
 *      wrappers.
 *
 *      Unfortunately the implementation is somewhat complicated due to the
 *      differing conventions for symbol resolution on Windows vs. Linux,
 *      limitations of the C preprocessor for code generation, etc.
 */

/*
 * Declare the global function pointers
 *
 *      Define and use a list operator to generate function pointers of the
 *      form SSL_newFn, BIO_readFn, etc.
 */
#define VMW_SSL_FUNC(_lib, _rettype, _func, _args, _argnames) \
   _rettype (* _func##Fn) _args;
VMW_SSL_FUNCTIONS
#undef VMW_SSL_FUNC

/*
 * Template for the wrapper function body. Just checks if the pointer is
 * valid; if not, Panic(), otherwise, call through.
 */
#define VMW_SSL_WRAPPER_BODY(_rettype, _retstmt, _func, _args, _argnames) \
   _rettype VMW_SSL_WRAPPER_NAME(_func) _args { \
      if (UNLIKELY(_func##Fn == NULL)) { \
          Panic("SSL wrapper: invoked uninitialized function " #_func "!\n"); \
      } \
      _retstmt _func##Fn _argnames; \
   }

/*
 * Bring in sslWrapper.h to generate the wrapper bodies
 */
#include "sslWrapper.h"

static char *SSLCertFile = NULL;
static char *SSLKeyFile = NULL;
static Bool SSLModuleInitialized = FALSE;
static int SSLVerifyParamIx = -1;
static SSLVerifyType SSLVerifySSLCertificates = SSL_VERIFY_CERTIFICATES_DEFAULT;
static char *SSLDHParamsFiles[2] = { NULL, NULL };

#define SERVER_CERT_FILE "rui.crt"
#define SERVER_KEY_FILE  "rui.key"
#define SSL_DH512_FILE   "dh512.pem"
#define SSL_DH1024_FILE  "dh1024.pem"

#ifdef USE_SSL_DEBUG
#define SSL_LOG(x) LOG_DEVEL(x)
#else
#define SSL_LOG(x)
#endif /*USE_SSL_DEBUG*/

/* How long (ms) to wait before retrying a SSL_connect operation */
#define SSL_WAIT_TIME 100

/* How long (sec) to wait for data to be available on a socket during connect */
#define SSL_CONNECT_WAIT_TIMEOUT  120 

static SSL_CTX *ssl_ctx = NULL;
static SyncMutex *ssl_locks = NULL;
#ifndef __APPLE__
static void *libsslHandle;
static void *libcryptoHandle;
#endif

/* XXX: Cleanup SSL library configuration at some point. */

/* 
 * Global config flag to control whether SSL_Accept fails if certificate loading
 * fails. This is useful in cases where authentication is not required, but
 * privacy is.
 */
static Bool requireCertificates = TRUE;

/* Used only for Windows. Don't care on other platforms. */
static Bool loadCertificatesFromFile = FALSE;

/*
 * OpenSSL cipher lists are colon, comma, or space delimited lists.
 * To get a full list of ciphers:
 * openssl ciphers | sed 's#:#\n#g'
 */
#define SSL_CIPHER_LIST "AES256-SHA,AES128-SHA"


#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif

enum {
   SSL_SOCK_WANT_RETRY,
   SSL_SOCK_LOST_CONNECTION,
};

// XXX temporary hack for Win32 SCONS build
#ifdef _WIN32
#ifndef FIPS_SIGS_NAME
#define FIPS_SIGS_NAME "fipsSigs.dat"
#endif
#endif

#ifdef __APPLE__
   /*
    * Mac OS provides a crypto lib, so use the SDK's version
    * unless explicitly overriden.
    */
#   include <openssl/opensslv.h>
#   define LIBCRYPTO_SO_QUOTED "libcrypto." SHLIB_VERSION_NUMBER ".dylib"
#   define LIBSSL_SO_QUOTED    "libssl."    SHLIB_VERSION_NUMBER ".dylib"

#else /* __APPLE__ */
   /*
    * Ensure that the filenames for SSL libs are provided
    * for Windows or Linux, and make sure they are quoted correctly.
    */
#   ifndef LIBCRYPTO_SO
#      error Must provide LIBCRYPTO_SO filename
#   endif
#   ifndef LIBSSL_SO
#      error Must provide LIBSSL_SO filename
#   endif
#   define LIBCRYPTO_SO_QUOTED XSTR(LIBCRYPTO_SO)
#   define LIBSSL_SO_QUOTED XSTR(LIBSSL_SO)
#endif /* __APPLE__ */

/*
 * On Linux we have either libXXX.so.0.9.8, or libXXX.so.6.  0.9.8
 * is hidden somewhere in Makefiles and arrives here as 
 * LIBCRYPTO_SO on compiler command line (which is, BTW, completely
 * broken as whole purpose of this file is to make sure nobody else
 * has to know!)
 */
#ifdef __linux__
#   define LIBCRYPTO_SO_ALT "libcrypto.so.6"
#   define LIBCRYPTO_SO_ALT_2 "libcrypto.so"
#   define LIBSSL_SO_ALT "libssl.so.6"
#   define LIBSSL_SO_ALT_2 "libssl.so"
#else
#   define LIBCRYPTO_SO_ALT NULL
#   define LIBCRYPTO_SO_ALT_2 NULL
#   define LIBSSL_SO_ALT NULL
#   define LIBSSL_SO_ALT_2 NULL
#endif

static SSL_CTX *SSLNewDefaultContext(void);


/*
 *----------------------------------------------------------------------
 *
 * SSLPrintErrors
 *
 *    Print out all the errors in the SSL error stack.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    Clears out the SSL error stack
 *
 *----------------------------------------------------------------------
 */

static void
SSLPrintErrors(void)
{
   int errNum;
   while ((errNum = ERR_get_error())) {
      Warning("SSL Error: %s\n", ERR_error_string(errNum, NULL));
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SSLPrintCipher
 *
 *    Prints out the cipher that is currently being used by
 *    the ssl connection.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *----------------------------------------------------------------------
 */

static void
SSLPrintCipher(SSL *ssl)
{
   int bits = 0;
   SSL_CIPHER *cipher;
   const char* cipher_name;

   cipher = SSL_get_current_cipher(ssl);
   SSL_CIPHER_get_bits(cipher, &bits);

   /*
    * if cipher is null, OpenSSL dies!
    */
   if (cipher != NULL) {
      cipher_name = SSL_CIPHER_get_name(cipher);
   } else {
      cipher_name = "undefined";
   }
   
   SSL_LOG(("Using cipher %s with %u bits\n", cipher_name, bits));
}


/*
 *----------------------------------------------------------------------
 *
 * SSLSetSystemError
 *
 *    Maps the ssl error state into an appropriate errno / WSA error. 
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *----------------------------------------------------------------------
 */

static void
SSLSetSystemError(int err)
{
   switch (err) {
      case SSL_SOCK_WANT_RETRY:
#ifdef _WIN32
         WSASetLastError(WSAEWOULDBLOCK);
#else
         errno = EAGAIN;
#endif
         break;
      case SSL_SOCK_LOST_CONNECTION:
         /*
          * no good way to know what the real error was (could have been
          * a failure to load certificates in an accept), so return
          * something generic.
          */
#ifdef _WIN32
         WSASetLastError(WSAEACCES);
#else
         errno = EPERM;
#endif
         break;
      default:
         NOT_REACHED();
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SSLSetErrorState
 *
 *    Each ssl read / write could result in several reads and writes on
 *    the underlying socket.  In this case the actual value for errno 
 *    will not be correct.  Manually setup the error value so that
 *    clients will do the right thing.
 *
 *    XXX: Mapping the SSL_ERROR_WANT_<something> errors to a single error code
 *    is not good. Applications using non-blocking IO would not know whether
 *    they should put the FD in a read wait or a write wait. Note that SSL_read
 *    can return SSL_ERROR_WANT_WRITE and SSL_write may return
 *    SSL_ERROR_WANT_READ.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    errno / windows error might be set.
 *
 *----------------------------------------------------------------------
 */

static int
SSLSetErrorState(SSL *ssl,
                 int result)
{
   int sslError = SSL_get_error(ssl, result);
   switch(sslError) {
      case SSL_ERROR_NONE:
         SSL_LOG(("SSL: action success, %d bytes\n", result));
         break;
      case SSL_ERROR_ZERO_RETURN:
         SSL_LOG(("SSL: Zero return\n"));
         break;
      case SSL_ERROR_WANT_READ:
         SSL_LOG(("SSL: Want read\n"));
         SSLSetSystemError(SSL_SOCK_WANT_RETRY);
         break;
      case SSL_ERROR_WANT_WRITE:
         SSL_LOG(("SSL: Want write\n"));
         SSLSetSystemError(SSL_SOCK_WANT_RETRY);
         break;
      case SSL_ERROR_WANT_X509_LOOKUP:
         SSL_LOG(("SSL: want x509 lookup\n"));
         break;
      case SSL_ERROR_SYSCALL:
         SSL_LOG(("SSL: syscall error\n"));
         break;
      case SSL_ERROR_SSL:
         Warning("SSL: Unknown SSL Error\n");
         break;
   }
   return sslError;
}

/*
 * Generic DLOPEN, DLSYM, and DLCLOSE macroes.
 */

#if defined(_WIN32)
#define DLOPEN(x) (void *) Win32U_LoadLibrary(x)
#define DLCLOSE(x) FreeLibrary(x)
#define DLSYM(name, dllHandle)                                          \
do {                                                                    \
   (FARPROC) name ## Fn = GetProcAddress((HMODULE)dllHandle, #name);    \
   if (name ## Fn == NULL) {                                            \
      SSL_LOG(("GetProcAddress: Failed to resolve %s: %d\n", #name, GetLastError())); \
   }                                                                    \
} while (0)
#else
#define DLOPEN(x) Posix_Dlopen(x, RTLD_LAZY | RTLD_GLOBAL)
#define DLCLOSE(x) dlclose(x)
#define DLSYM(name, dllHandle)                                          \
do {                                                                    \
   char *error;                                                         \
   name ## Fn = dlsym(dllHandle, #name);                                \
   if ((error = dlerror()) != NULL) {                                   \
      SSL_LOG(("DLSYM: Failed to resolve %s: %s\n", #name, error));     \
   }                                                                    \
} while (0)
#endif


#ifndef __APPLE__
/*
 *----------------------------------------------------------------------
 *
 * SSLGetModulePath --
 *
 *	Try and deduce the full path to the executable that is calling
 *	this function.  Find it via /proc/ (2.2.x+ kernels).  This call
 *	fails otherwise.
 *
 *      **NOTE: XXX This function is copied from bora/lib/user/hostinfo*.c
 *        in the main branch.  This is the only function needed from 
 *        lib/user so instead of linking that we copied the function 
 *        -wchien 03/13/03
 *   
 * Results:
 *      The full path or NULL on failure.
 *
 * Side effects:
 *	Memory is allocated.
 *
 *----------------------------------------------------------------------
 */

static char *
SSLGetModulePath(void)
{
   char *path;

#if defined(_WIN32)
   path = Win32U_GetModuleFileName(NULL);

   if (path == NULL) {
      Warning("%s: GetModuleFileName failed: %d\n", __FUNCTION__,
              GetLastError());
   }
#else
   Bool su = IsSuperUser();

   SuperUser(TRUE);
   path = Posix_ReadLink("/proc/self/exe");
   SuperUser(su);

   if (path == NULL) {
      Warning("%s: readlink failed: %s\n", __FUNCTION__, strerror(errno));
   }
#endif

   return path;
}


/*
 *----------------------------------------------------------------------
 *
 * SSLGetLibraryPath --
 *
 *	Try and deduce the full path to the library in which we are.
 *      Implemented only on Linux, as it is only platform where we
 *      need this.
 *
 * Results:
 *      The full path or NULL on failure.
 *
 * Side effects:
 *	Memory is allocated.
 *
 *----------------------------------------------------------------------
 */

static Unicode
SSLGetLibraryPath(void)
{
#ifdef __linux__
   Dl_info info;

   if (dladdr(SSLGetLibraryPath, &info)) {
      return Unicode_Alloc(info.dli_fname, STRING_ENCODING_DEFAULT);
   }
   return NULL;
#else
   return NULL;
#endif
}


/* 
 *----------------------------------------------------------------------
 *
 *  SSLOpenSystemLibrary --
 *
 *     Tries to open system library - either libcrypto or libssl.
 *
 * Results:
 *     The library handle on success, NULL on failure.
 *
 * Side effects:
 *     Library loaded & verified.
 *
 *----------------------------------------------------------------------
 */

static void *
SSLOpenSystemLibrary(const char *libname,   // IN: library name
                     Bool doVersionCheck)   // IN: is libcrypto and wants new version
{
   void *libHandle;

   /*
    * This doesn't actually load the system OpenSSL libraries on
    * Windows since for LoadLibrary Windows always looks in the
    * loading application's directory first.
    */

   libHandle = DLOPEN(libname);
   if (libHandle) {
      long (*SSLeayFn)(void);

      if (!doVersionCheck) {
         return libHandle;
      }

      DLSYM(SSLeay, libHandle);
      if (SSLeayFn) {
         long ver;

         /*
          * We require that the version is strictly equal to or greater than the
          * one we compiled against - including the 'letter' version. The OpenSSL
          * devs have a habit of breaking binary compatibility between minor releases.
          * eg: BIO_clear_flags changed from a macro to a function at some point 
          * between 0.9.8b and 0.9.8g.
          */

         ver = SSLeayFn();
         if (ver >= OPENSSL_VERSION_NUMBER) {
            Log("Using system libcrypto, version %lX\n", ver);
            return libHandle;
         }
         Log("System %s library is older than our library (%lX < %lX)\n",
             libname, ver, OPENSSL_VERSION_NUMBER);
      }
      DLCLOSE(libHandle);
   }
   return NULL;
}


/* 
 *----------------------------------------------------------------------
 *
 *  SSLOpenLibraryWithPath --
 *
 *     Tries to open libcrypto/libssl library in directory where argument
 *     lives.
 *
 * Results:
 *     The library handle on success, NULL on failure.
 *
 * Side effects:
 *     Library loaded.
 *
 *----------------------------------------------------------------------
 */

static void *
SSLOpenLibraryWithPath(const char *fullPath,  // IN: path
                       const char *libname)   // IN: library name
{
   char *libLocation;
   void *libHandle;
   char *pathEnd;
   unsigned int sublen;

   /* Broken on Windows... */
   pathEnd = strrchr(fullPath, DIRSEPC);
   if (!pathEnd) {
      /*
       * No slash in path means that we will be loading
       * system library.  If you want that, do not pass
       * no system library flag to SSLLoadSharedLIbrary....
       */
      return NULL;
   }
   sublen = pathEnd - fullPath;

   libLocation = Str_Asprintf(NULL, "%*.*s%c%s", sublen, sublen, fullPath,
                              DIRSEPC, libname);

   ASSERT_MEM_ALLOC(libLocation);

   libHandle = DLOPEN(libLocation);

   free(libLocation);

   if (libHandle) {
      return libHandle;
   }

   libLocation = Str_Asprintf(NULL, "%*.*s%clibdir%clib%c%s%c%s", sublen,
                              sublen, fullPath, DIRSEPC, DIRSEPC, DIRSEPC,
                              libname, DIRSEPC, libname);

   ASSERT_MEM_ALLOC(libLocation);

   libHandle = DLOPEN(libLocation);

   free(libLocation);

   if (libHandle) {
      return libHandle;
   }
   return NULL;
}


/* 
 *----------------------------------------------------------------------
 *
 *  SSLOpenLibrary --
 *
 *     Gets the directory and name of a library and returns a handle to 
 *     it.  Panics if it can't be found.
 *
 *     First try system library, then library in our libdir, then library
 *     in the app's directory, and finally (for devel builds only) bora-root.
 * 
 * Results:
 *     The library handle on success.
 *     Panic() on failure.
 *
 * Side effects:
 *     Library loaded & verified.
 *
 *----------------------------------------------------------------------
 */

static void *
SSLOpenLibrary(const char *libdir,      // IN: directory with our libs
               const char *libname,     // IN: library name
               const char *altLibName,  // IN: alternative library name
               const char *altLibName2, // IN: second alternative library name
               Bool isLibCrypto,        // IN: libname is libcrypto
               Bool *system,            // IN/OUT: TRUE => try system library
                                        //         FALSE => system library unusable
               Bool doVersionCheck)     // IN: Whether to version check the system library
{
   char *libLocation;
   char *fullpath;
   void *libHandle;

   if (*system) {
      /*
       * This doesn't actually load the system OpenSSL libraries on
       * Windows since for LoadLibrary Windows always looks in the
       * loading application's directory first.
       */

      libHandle = SSLOpenSystemLibrary(libname, isLibCrypto && doVersionCheck);
      if (libHandle) {
         return libHandle;
      }
      if (altLibName) {
         libHandle = SSLOpenSystemLibrary(altLibName, isLibCrypto && doVersionCheck);
         if (libHandle) {
            return libHandle;
         }
      }
      if (altLibName2) {
         libHandle = SSLOpenSystemLibrary(altLibName2, isLibCrypto && doVersionCheck);
         if (libHandle) {
            return libHandle;
         }
      }
      /* System's libcrypto failed - do not attempt to use system's libssl */
      *system = FALSE;
   }

#ifdef __APPLE__
   /*
    * These fallback paths are not implemented on Mac OS because Apple
    * guarantees that crypto libs are present (they are in the SDK).
    * XXX It would be better to create a fallback in libdir
    * XXX for crypto libs we ship.  But for now stop here, because
    * XXX SSLGetModulePath isn't implemented for __APPLE__
    */
   NOT_IMPLEMENTED();
#endif

   /*
    * try libdir/lib/libname-i386/libname or libdir/lib/libname-x86-64/libname
    * and then libdir/lib/libname/libname then libdir/lib/libname 
    */
   if (libdir != NULL) {
      libLocation = Str_SafeAsprintf(NULL,
#ifdef VM_X86_64
                   "%s%clib%c%s-x86-64%c%s",
#else
                   "%s%clib%c%s-i386%c%s",
#endif
                   libdir, DIRSEPC, /* "lib" */ DIRSEPC, libname,
                   DIRSEPC, libname);
      libHandle = DLOPEN(libLocation);
      free(libLocation);
      if (libHandle) {
         return libHandle;
      }

      libLocation = Str_SafeAsprintf(NULL, "%s%clib%c%s%c%s", libdir, DIRSEPC,
                                     /* "lib" */ DIRSEPC, libname, DIRSEPC,
                                     libname);
      libHandle = DLOPEN(libLocation);
      free(libLocation);
      if (libHandle) {
         return libHandle;
      }
      
      libLocation = Str_SafeAsprintf(NULL, "%s%clib%c%s", libdir, DIRSEPC,
                                     /* "lib" */ DIRSEPC, libname);
      libHandle = DLOPEN(libLocation);
      free(libLocation);
      if (libHandle) {
        return libHandle;
      }

      libLocation = Str_SafeAsprintf(NULL, "%s%c%s", libdir, DIRSEPC, libname);
      libHandle = DLOPEN(libLocation);
      free(libLocation);
      if (libHandle) {
         return libHandle;
      }
   }

   fullpath = SSLGetLibraryPath();
   if (fullpath) {
      libHandle = SSLOpenLibraryWithPath(fullpath, libname);
      free(fullpath);
      if (libHandle) {
         return libHandle;
      }
   }

   fullpath = SSLGetModulePath();
   if (fullpath) {
      libHandle = SSLOpenLibraryWithPath(fullpath, libname);
      free(fullpath);
      if (libHandle) {
         return libHandle;
      }
   }

#if defined(VMX86_DEVEL) && !defined(__APPLE__)
   libLocation = Str_SafeAsprintf(NULL, "%s%s%s",
				  SSL_SRC_DIR, DIRSEPS, libname);
   libHandle = DLOPEN(libLocation);
   free(libLocation);
   if (libHandle) {
      return libHandle;
   }
#endif

#ifdef _WIN32
   Panic("SSLLoadSharedLibrary: Failed to load library %s:%d\n", 
         libname, GetLastError());
#else
   Panic("SSLLoadSharedLibrary: Failed to load library %s:%s\n", 
         libname, dlerror());
#endif
   return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * SSLLoadSharedLibrary--
 *
 *   Links in functions from the SSL library.
 *
 *----------------------------------------------------------------------
 */

static void
SSLLoadSharedLibrary(const char *libdir, 
                     Bool useSystem, 
                     Bool doVersionCheck)
{
   Bool system = useSystem;

   libcryptoHandle = SSLOpenLibrary(libdir, LIBCRYPTO_SO_QUOTED, LIBCRYPTO_SO_ALT,
                                    LIBCRYPTO_SO_ALT_2, TRUE, &system, doVersionCheck);
   libsslHandle = SSLOpenLibrary(libdir, LIBSSL_SO_QUOTED, LIBSSL_SO_ALT,
                                 LIBSSL_SO_ALT_2, FALSE, &system, doVersionCheck);

   /*
    * Define a list operator to load each function we need and stash it in the
    * appropriate function pointer. We Panic() if we can't load a symbol we
    * want.
    */
   #define VMW_SSL_FUNC(_lib, _rettype, _func, _args, _argnames) \
      DLSYM(_func, lib##_lib##Handle);
   VMW_SSL_FUNCTIONS
   #undef VMW_SSL_FUNC
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * SSLAddLockCb --
 *
 *      Callback for adding two numbers atomically
 *
 *      We need this so that SSL_new(ssl_ctx) can be
 *      re-entrant. Without this callback we would be unsafely
 *      incrementing the ref-count to the global ssl_ctx
 *
 *      (see bug 105949 for more details)
 *
 * Results:
 *
 *      The new value of of *num.
 *
 *---------------------------------------------------------------------- 
 */

static int 
SSLAddLockCb(int *num,            // IN/OUT: number to be added
             int amount,          // IN: addend
             int type,            // IN: type of lock (ignored)
             const char *file,    // IN: file which called this method (for debugging)
             int line)            // IN: line in the file (for debugging)
{
   // XXX: This might not be 64-bit safe
   return Atomic_FetchAndAddUnfenced(Atomic_VolatileToAtomic(num), amount) +
          amount;
}


/*
 *----------------------------------------------------------------------
 *
 * SSLVerifyCb --
 *
 *      Callback invoked by SSL during a handshake in order to verify
 *      the peer certificate. 
 *
 *      Normally, the master SSL context would be given a certificate
 *      store with trusted certificates against which it would verify
 *      the peer certificate. Each time the peer certificate is
 *      checked, the callback will be invoked and the preverifyOk flag
 *      would be set to TRUE if the certificate was successfully
 *      verified. Otherwise the preverfyOk flag would be set to 0
 *      and the callback would be expected to do its own verification
 *      and decide whether to trust this certificate (by returning
 *      1) or not.
 *
 *      We employ two strategies to validate a certificate: 1)
 *      Thumbprint matching and 2) Signature checking.
 *
 *      Thumbprint matching involves comparing the certificate's
 *      thumbprint to a client-specified thumbprint. If the two match,
 *      the certificate is validated, because the client already
 *      expected to see this certificate (this is the SSH model). If
 *      the thumbprints don't match we proceed to the second approach
 *      of checking the signer's signature.
 *
 *      Signature checking is only implemented on Windows at the
 *      moment.
 *
 *      On Win32, rather than providing OpenSSL with the trusted
 *      certificates, we simply handle this callback and check the
 *      peer certificate against the default Win32 certificate store
 *      of trusted certifactes.
 *
 *      The Posix implementation is at present omitted, but in the
 *      future it may or may not follow the same strategy as the Win32
 *      implementation.
 *
 *---------------------------------------------------------------------- 
 */

static int
SSLVerifyCb(int preverifyOk, X509_STORE_CTX *storeCtx)
{
   SSL *ssl;
   SSLVerifyParam *verifyParam;
   X509 *cert;

   /*
    * Obtain the parameters we need in verification.
    *
    * Note that the current certificate that is being checked will
    * always be the peer certificate, since we did not provide OpenSSL
    * with a trusted certificate list
    */

   ssl = (SSL *) X509_STORE_CTX_get_ex_data(
      storeCtx, SSL_get_ex_data_X509_STORE_CTX_idx());

   verifyParam = (SSLVerifyParam *) SSL_get_ex_data(ssl, SSLVerifyParamIx);
   cert = X509_STORE_CTX_get_current_cert(storeCtx);

   return SSL_VerifyX509(verifyParam, cert) ? 1 : 0;
}



/*
 *----------------------------------------------------------------------
 *
 * SSLLockingCb --
 *
 *      Callback for locking for use by OpenSSL.
 *
 *      OpenSSL requires setting of a locking callback function in
 *      multi-threaded applications.
 *      The SyncMutex_XXX calls can reset error numbers, so save
 *      them and restore back
 *
 *---------------------------------------------------------------------- 
 */

static void 
SSLLockingCb(int mode,           // indicates locking or unlocking
             int n,              // lock index 
             const char *file,   // file name
             int line)           // line number
{
#ifdef WIN32
   int savederr = GetLastError();
#else
   int savederr = errno;
#endif

if (!SSLModuleInitialized) { 
	return;
}

   if (CRYPTO_LOCK & mode) {
      SyncMutex_Lock(&ssl_locks[n]);
   } else if (CRYPTO_UNLOCK & mode) {
      SyncMutex_Unlock(&ssl_locks[n]);
   }

#ifdef WIN32
   SetLastError(savederr);
#else
   errno = savederr;
#endif
}


/*
 *---------------------------------------------------------------------- 
 * 
 * SSLTmpDHCallback --
 *
 *    Callback for DH key exchange. Initializes the DH parameters
 *    from the configuration files, if not already initialized.
 *
 * Results:
 *    DH parameter for use by OpenSSL. NULL on failure.
 *
 * Side Effects:
 *    None.
 *
 *---------------------------------------------------------------------- 
 */

static DH *
SSLTmpDHCallback(SSL *ssl,       // IN: unused
                 int is_export,  // IN: Export restricted?
                 int keylength)  // IN: Length of key to be returned.
{
   static DH *dh512 = NULL;
   static DH *dh1024 = NULL;

   if (!dh512 || !dh1024) {
      BIO *bio = SSL_BIO_new_file(SSLDHParamsFiles[0], "r");
      if (bio) {
         dh512 = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
         if (!dh512) {
            Warning("Error reading DH parameter file");
         }
         BIO_free(bio);
      } else {
         Warning("Error opening DH parameter file");
      }

      bio = SSL_BIO_new_file(SSLDHParamsFiles[1], "r");
      if (bio) {
         dh1024 = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
         if (!dh1024) {
            Warning("Error reading DH parameter file");
         }
         BIO_free(bio);
      } else {
         Warning("Error opening DH parameter file");
      }
   }

   return (keylength == 512) ? dh512 : dh1024;
}


/*
 *----------------------------------------------------------------------
 *
 * SSLThreadIdCb --
 *
 *      Callback for current thread id for use by OpenSSL.
 *
 *      OpenSSL requires setting of a thread id callback function in
 *      multi-threaded applications.
 *
 *---------------------------------------------------------------------- 
 */

static unsigned long
SSLThreadIdCb(void)
{
   return (unsigned long)Util_GetCurrentThreadId();
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_InitEx --
 *
 * 	Initializes the SSL library and prepares the session context.
 * 	getLibFn is a function which is able to return the location of
 * 	the SSL libraries.  default and name are arguments to getLibFn.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Lots.

 *----------------------------------------------------------------------
 */

void
SSL_InitEx(SSLLibFn *getLibFn, 
           const char *defaultLib, 
           const char *name,
           Bool useSystem,
           Bool doVersionCheck,
           Bool disableLoading)

{
   /*Silently ignore any attempts to initialize module more than once*/
   if (!SSLModuleInitialized) {
      int numSslLocks, i;
#ifdef __APPLE__
      /*
       * On the Mac, we directly link the ssl libraries, so there's no
       * need to dlopen and load the symbols.
       */
      BEGIN_NO_STACK_MALLOC_TRACKER;
      BEGIN_NO_MALLOC_TRACKER;
#else
      char *libdir;
      BEGIN_NO_STACK_MALLOC_TRACKER;

      BEGIN_NO_MALLOC_TRACKER;
      if (disableLoading) {
         /* 
          * If SSL libraries are already loaded, then setup the function
          * pointers.
          */
#ifdef WIN32
         libcryptoHandle = GetModuleHandle(LIBCRYPTO_SO_QUOTED);
         ASSERT(libcryptoHandle);
         libsslHandle = GetModuleHandle(LIBSSL_SO_QUOTED);
         ASSERT(libsslHandle);
#else 
         void *procHandle = dlopen(NULL, RTLD_LAZY | RTLD_GLOBAL);
         void *thislibHandle = dlopen("libVICFbase.so", RTLD_LAZY | RTLD_GLOBAL);
         void *thisFn = dlsym(thislibHandle, "SSL_library_init");
         void *globalFn = dlsym(procHandle, "SSL_library_init");

         ASSERT(thislibHandle);
         ASSERT(thisFn);
         ASSERT(globalFn);

         if (thisFn == globalFn) {
            globalFn = dlsym(RTLD_NEXT, "SSL_library_init");
            libsslHandle = RTLD_NEXT;
         }
         if (thisFn == globalFn || globalFn == NULL) {
            SSL_LOG(("Failed to locate libssl symbols.\n"));
            Panic("Failed to locate libssl symbols.\n");
         }

         thisFn = dlsym(thislibHandle, "CRYPTO_num_locks");
         globalFn = dlsym(procHandle, "CRYPTO_num_locks");
         if (thisFn == globalFn) {
            thisFn = dlsym(RTLD_NEXT, "CRYPTO_num_locks");
            libcryptoHandle = RTLD_NEXT;
         }
         if (thisFn == globalFn || globalFn == NULL) {
            SSL_LOG(("Failed to locate libcrypto symbols.\n"));
            Panic("Failed to locate libcrypto symbols.\n");
         }

         dlclose(thislibHandle);
#endif
         /*
          * Define a list operator to load each function we need and stash it in the
          * appropriate function pointer. We Panic() if we can't load a symbol we
          * want.
          */
         #define VMW_SSL_FUNC(_lib, _rettype, _func, _args, _argnames) \
            DLSYM(_func, lib##_lib##Handle);
         VMW_SSL_FUNCTIONS
         #undef VMW_SSL_FUNC
      } else {
         if (getLibFn) {
            libdir = (getLibFn)(defaultLib, name);      
         } else {
            libdir = defaultLib ? strdup(defaultLib) : NULL;
         }
         // We check if libdir is valid in SSLLoadSharedLibrary
         SSLLoadSharedLibrary(libdir,
                              CryptoFips_FipsModeEnabled() ? FALSE : useSystem,
                              doVersionCheck);
         free(libdir);
      }
#endif
      SSL_library_init();
      SSL_load_error_strings();
      END_NO_MALLOC_TRACKER;

      /*
       * Force the PRNG to be initialized early, as opposed to at the
       * time when the SSL connection is made. A call to RAND_status
       * forces this initialization to happen. Initializing the PRNG
       * as early as possible in the process makes it take much less
       * time (e.g. 1sec. vs. sometimes 20sec.) compared to
       * initializing it later in the process, as may be the case on
       * the first SSL_accept() or SSL_connect(). That's because the
       * PRNG initialization walks the process heap and the total heap
       * is smaller at startup.
       *
       * If SSL_InitEx could not be called early enough in the
       * process, then the caller could just call RAND_status() by
       * itself. Only the first call to RAND_status will have the side
       * effect of initializing the PRNG, so calling it subsequently
       * would be a NOOP. 
       */
      RAND_status();


      numSslLocks = CRYPTO_num_locks();
      ssl_locks = (SyncMutex *)malloc(sizeof(struct SyncMutex) * numSslLocks);
      for (i = 0;i < numSslLocks; ++i) {
         SyncMutex_Init(&ssl_locks[i], NULL);
      }

      CRYPTO_set_locking_callback(SSLLockingCb);
      CRYPTO_set_id_callback(SSLThreadIdCb); 


      /*
       * Only peform additional initialization tasks if not compiled
       * inside vmcryptolib, because vmcryptolib itself does not need
       * to set-up networking.
       */

      if (!CryptoFips_InVmcryptolib()) {
         CRYPTO_set_add_lock_callback(SSLAddLockCb);
      }

      /*
       * Force the initialization of ssl_ctx, in case anyone is using it without
       * going through one of our functions.
       */

      ssl_ctx = SSLNewDefaultContext();

      SSL_LOG(("SSL: default ctx created\n"));
      SSL_LOG(("Initializing default ssl context: %p\n", ctx));

#ifdef _WIN32
      SSLCertFile = W32Util_GetInstalledFilePath("\\ssl\\" SERVER_CERT_FILE);
      SSLKeyFile = W32Util_GetInstalledFilePath("\\ssl\\" SERVER_KEY_FILE);
      SSLDHParamsFiles[0] = W32Util_GetInstalledFilePath("\\ssl\\"
                                                         SSL_DH512_FILE);
      SSLDHParamsFiles[1] = W32Util_GetInstalledFilePath("\\ssl\\"
                                                         SSL_DH1024_FILE);
#else
      SSLCertFile = strdup(VMWARE_HOST_DIRECTORY "/ssl/" SERVER_CERT_FILE);
      SSLKeyFile = strdup(VMWARE_HOST_DIRECTORY "/ssl/" SERVER_KEY_FILE);
      SSLDHParamsFiles[0] = strdup(VMWARE_HOST_DIRECTORY "/ssl/"
                                   SSL_DH512_FILE);
      SSLDHParamsFiles[1] = strdup(VMWARE_HOST_DIRECTORY "/ssl/"
                                   SSL_DH1024_FILE);
#endif //
      ASSERT_MEM_ALLOC(SSLCertFile);
      ASSERT_MEM_ALLOC(SSLKeyFile);
      ASSERT_MEM_ALLOC(SSLDHParamsFiles[0]);
      ASSERT_MEM_ALLOC(SSLDHParamsFiles[1]);

      SSLModuleInitialized = TRUE;

      END_NO_STACK_MALLOC_TRACKER;
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_Init --
 *
 * 	Initializes the SSL library and prepares the session context.
 * 	getLibFn is a function which is able to return the location of
 * 	the SSL libraries.  default and name are arguments to getLibFn.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Lots.
 *
 *----------------------------------------------------------------------
 */

void
SSL_Init(SSLLibFn *getLibFn, const char *defaultLib, const char *name)
{
   SSL_InitEx(getLibFn, defaultLib, name, TRUE, TRUE, FALSE);
}


/*
 *----------------------------------------------------------------------
 * SSLNewDefaultContext --
 *
 *      Return an SSL context initialized with reasonable defaults.
 *----------------------------------------------------------------------
 */

static SSL_CTX *
SSLNewDefaultContext(void)
{
   SSL_CTX *ctx;

   ctx = SSL_CTX_new(CryptoFips_FipsModeEnabled() ?
                     TLSv1_method() : SSLv23_method());

   if (!ctx) {
      SSLPrintErrors();
      Panic("Error Starting Up Default SSL context\n");
      NOT_REACHED();
   }

   /* 
    * Disable SSLv2 and enable all known bug workarounds.
    */
   SSL_CTX_set_options(ctx,
                       SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_SINGLE_DH_USE);

   /*
    * Automatically retry an operation that failed with
    * SSL_WANT_{READ|WRITE} if blocking sockets are being used
    *
    * This flag is ineffective for non-blocking sockets and the
    * application must retry the SSL IO operation that needs to be retried
    * until it succeeds, before it can perform any other IO on the
    * SSL.
    */
   SSL_CTX_ctrl(ctx, SSL_CTRL_MODE, SSL_MODE_AUTO_RETRY, NULL);

   /* Don't cache sessions (client not smart enough to use them */
   SSL_CTX_ctrl(ctx, SSL_CTRL_SET_SESS_CACHE_MODE,
                SSL_SESS_CACHE_OFF, NULL);
   /* 
    * disable the bidirectional shutdown sequence.  This is really
    * only useful when we want to use SSL session caching.  
    * (a session will only be cached if it was shutdown properly,
    * using the full bidirectional method).
    * */
   SSL_CTX_set_quiet_shutdown(ctx, 1);

   /*
    * Set the cipher for the context.  All sessions initiated from this
    * context will use the same cipher.  Use the SSL_set_cipher_list to
    * change the cipher on a per session basis.
    */
   SSL_CTX_set_cipher_list(ctx, SSL_CIPHER_LIST);

   /* 
    * Initialize the callback for use with cipher suites using Diffie-Hellman.
    */
   SSL_CTX_set_tmp_dh_callback(ctx, SSLTmpDHCallback);

   /*
    * Create a new slot (index) where we can store a pointer to the
    * SSLVerifyParam data structure
    */
   SSLVerifyParamIx = SSL_get_ex_new_index(0, NULL, NULL, NULL, NULL);
   
   return ctx;
}


/*
 *----------------------------------------------------------------------
 * SSL_DefaultContext --
 *
 *      Returns the global default SSL context. SSL_Init[Ex] must have been
 *      called before calling this.
 *----------------------------------------------------------------------
 */

void *
SSL_DefaultContext(void)
{
   ASSERT(SSLModuleInitialized);
   return (void *)ssl_ctx;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_SetVerifySSLCertificates --
 *
 *      Sets a global option to verify or not verify peeer SSL
 *      certificates.
 *
 *---------------------------------------------------------------------- 
 */

void
SSL_SetVerifySSLCertificates(SSLVerifyType verify)
{
   SSLVerifySSLCertificates = verify;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_GetVerifySSLCertificates --
 *
 *      Returns the global option that determines whether peer SSL
 *      certificates should be verified
 *
 *---------------------------------------------------------------------- 
 */

SSLVerifyType
SSL_GetVerifySSLCertificates(void)
{
   return SSLVerifySSLCertificates;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_VerifyX509Cert --
 *
 *      Stock implementation for most of the SSLVerifyCb that is
 *      passed to SSL_set_verify.
 *
 *      This function assumes that the second argument is of type
 *      X509, however since the ssl.h header currently does not
 *      include any OpenSSL headers, we cannot specify this type
 *      safely.
 *
 * Result:
 *      TRUE - if the verification succeeded, and FALSE otherwise. The
 *      verification error is stored in the SSLVerifyParam argument
 *
 *---------------------------------------------------------------------- 
 */

Bool
SSL_VerifyX509(SSLVerifyParam *verifyParam, void *x509Cert)
{
   X509 *cert = (X509 *) x509Cert;
   unsigned char md[EVP_MAX_MD_SIZE];
   unsigned int mdLen = 0; /* initialize per bug 286415 */
   char thumbprintString[SSL_V_THUMBPRINT_STRING_SIZE] = {0};
   int ii;
   char *pp = thumbprintString;

   verifyParam->selfSigned = 
      !X509_NAME_cmp(X509_get_subject_name(cert), X509_get_issuer_name(cert));
   verifyParam->hasError = FALSE;

   /*
    * Compute the thumbprint of the cert and convert it to a string.
    * Yes, we also have to check mdLen on "success", see 286415;
    * X509_digest has a bug whereby it doesn't check the return value
    * of functions it calls that are supposed to fill in mdLen, so it
    * could return TRUE without having touched mdLen.
    */
   if (!X509_digest(cert, EVP_sha1(), md, &mdLen) || (0 == mdLen)) {
      return FALSE;
   }

   for (ii = 0; ii < (int) mdLen; ii++) {
      if (pp - thumbprintString < SSL_V_THUMBPRINT_STRING_SIZE-3) {
         snprintf(pp, 4, (ii == 0) ? "%02X" : ":%02X", md[ii]);
         pp += (ii == 0) ? 2 : 3;
      }
   }

   /*
    * If there was a certificate thumbprint provided, we try to match
    * it with the thumbprint for cert. Otherwise we fall back to the
    * conventional way of verifying the certificate. In any case, we
    * store its thumbprint in verifyParam->thumbprintString 
    */
   if (strncmp(thumbprintString, 
               verifyParam->thumbprintString, 
               SSL_V_THUMBPRINT_STRING_SIZE) == 0) {
      /* 
       * The request certificate signature matched the actual
       * certificate signature. The user already knew about this
       * certificate and was OK with it.
       */
      return TRUE;
   }

   /*
    * Copy the actual thumbprint into the SSL verify param and proceed
    * with regular certificate verification 
    */
   strncpy(verifyParam->thumbprintString, thumbprintString, SSL_V_THUMBPRINT_STRING_SIZE);

#ifdef WIN32
   {
      unsigned char *certBytes = NULL;
      unsigned char *pp;
      int certLen;
   

      /*
       * Convert the OpenSSL certificate in 'currentCert' into a Win32
       * ceritificate context so we can use it with the Win32 CertXxx
       * APIs. We do this by first encoding the OpenSSL certificate into
       * a buffer of bytes and then we use that buffer to create a Win32
       * certificate context 
       */

      certLen = i2d_X509(cert, NULL);
      certBytes = pp = (unsigned char *) malloc(certLen);
      ASSERT_MEM_ALLOC(certBytes);
      i2d_X509(cert, &pp);
   
      verifyParam->hasError = 
         !SSLVerifyCertAgainstSystemStore(certBytes, certLen, verifyParam);
      if (certBytes != NULL) {
         free(certBytes);
      }

      return !verifyParam->hasError;
   }
#else // #ifdef WIN32
   if (SSL_GetVerifySSLCertificates() == SSL_VERIFY_CERTIFICATES_ON) {
      /*
       * On Linux, we have not implemented yet a way to verify certificate
       * signatures against a set of trusted root CA certificates. However,
       * we could do certificate verification based on the certificate's
       * thumbprint.
       *
       * Therefore if SSL cert verification is turned on, we are using the
       * thumbprint matching approach to verify certs. Furthermore, if we got
       * this far, the thumbprints did not match, so we indicate that the
       * verification has failed
       */

      char peerCN[0x200];

      X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName,
                                peerCN, sizeof peerCN);             

      Warning("SSL_VerifyX509: Thumbprint mismatch for certificate "
              "with subject name: %s, %s\n", peerCN, thumbprintString);

      return FALSE;                                                                       
   } else {
      /*
       * The default verification action on Linux is to not verify SSL
       * certificates, so we just return TRUE here.
       */

      return TRUE;
   }

#endif // #ifdef WIN32
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_SetCiphers --
 *
 *    Sets the ciphers to use for the default context. Overrides the
 *    default cipher information.  This function should be called after
 *    SSL_Init.
 *
 *----------------------------------------------------------------------
 */

void
SSL_SetCiphers(const char *ciphers) // IN: Cipher suite list
{
   ASSERT(ciphers != NULL);
   ASSERT(SSLModuleInitialized);

   SSL_CTX_set_cipher_list(SSL_DefaultContext(), ciphers);
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_SetCerts --
 *
 *    Sets the certificates and private key to use for the default
 *    context. Overrides the default cert and key information. This
 *    function should be called after SSL_Init.
 *
 *----------------------------------------------------------------------
 */

void
SSL_SetCerts(const char *certFile, // IN: Certificate file
             const char *keyFile)  // IN: Private Key File
{
   ASSERT(certFile != NULL || keyFile != NULL);
   ASSERT(SSLModuleInitialized);

   if (certFile != NULL) {
      free(SSLCertFile);
      SSLCertFile = strdup(certFile);
      ASSERT_MEM_ALLOC(SSLCertFile);
   }

   if (keyFile != NULL) {
      free(SSLKeyFile);
      SSLKeyFile = strdup(keyFile);
      ASSERT_MEM_ALLOC(SSLKeyFile);
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_Exit --
 *
 * 	Destroys the default SSL session context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 * 	Stuff.
 *
 *----------------------------------------------------------------------
 */

void
SSL_Exit(void)
{
   if (SSLModuleInitialized) {
      int numSslLocks = CRYPTO_num_locks(), i;

      BEGIN_NO_STACK_MALLOC_TRACKER;

      CRYPTO_set_locking_callback(NULL);
      SSL_CTX_free(ssl_ctx);
      ssl_ctx = NULL;

      /*
       * Disable callbacks, or bad surprise happens when threads exit
       * after SSL gets shut down, even if these threads have nothing
       * common with SSL.
       */
      CRYPTO_set_add_lock_callback(NULL);
      CRYPTO_set_locking_callback(NULL);

      for (i = 0; i < numSslLocks; ++i) {
         SyncMutex_Destroy(&ssl_locks[i]);
      }
      free(ssl_locks);
      ssl_locks = NULL;

      SSLModuleInitialized = FALSE;
      free(SSLCertFile);
      free(SSLKeyFile);
      free(SSLDHParamsFiles[0]);
      free(SSLDHParamsFiles[1]);
      SSLCertFile = NULL;
      SSLKeyFile = NULL;
      SSLDHParamsFiles[0] = NULL;
      SSLDHParamsFiles[1] = NULL;
      END_NO_STACK_MALLOC_TRACKER;
   }
}


#ifdef _WIN32
/*
 *----------------------------------------------------------------------
 *
 * SSLGetFileContents()
 *
 *    Given a file handle, allocate a buffer the size of the file and 
 *    reads the entire file contents into it. 
 *
 *    On success, caller is responsible for deallocating the allocated 
 *    buffer. 
 *
 * Results:
 *    TRUE if successful, FALSE otherwise.
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */

static Bool
SSLGetFileContents(HANDLE hFile,       // IN
                   char **contents,    // OUT
                   int *size)          // OUT
{
   int bytesRead, fileSize;
   char *buf = NULL;

   fileSize = GetFileSize(hFile, NULL);
   if (fileSize == INVALID_FILE_SIZE) {
      goto error;
   }

   buf = (char *)malloc(fileSize);
   if (buf == NULL) {
      Warning("Unable to allocate buffer.\n");
      goto error;
   }

   if (!ReadFile(hFile, buf, fileSize, &bytesRead, NULL)) {
      goto error;
   }

   ASSERT(bytesRead == fileSize);
   *size = fileSize;
   *contents = buf;
   return TRUE;

error:
   free(buf);
   return FALSE;
}  


/*
 *----------------------------------------------------------------------
 *
 * SSLCreateMemoryBIOFromFile --
 *
 *    Given a file handle, creates and memory BIO and populate it with
 *    contents of the file.
 *
 *    On success, caller is responsible for deallocating the create 
 *    BIO object.
 *
 * Results:
 *    TRUE if successful, FALSE otherwise.
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */

static BIO *
SSLCreateMemoryBIOFromFile(HANDLE hFile) 
{
   BIO *bio = NULL;
   char *buf = NULL;
   int size;

   bio = BIO_new(BIO_s_mem());
   if (bio == NULL) {
      Warning("Create BIO failed.\n");
      return NULL;
   }

   if (!SSLGetFileContents(hFile, &buf, &size)) {
      Warning("Unable to read file.\n");
      BIO_free(bio);
      return NULL;
   } else {
      if (BIO_write(bio, buf, size) <= 0) { //VCV|C
         Warning("Unable to write to BIO.\n");
         free(buf);
         BIO_free(bio);
         return NULL;
      }
   }

   Warning("Create Memory BIO succeeded.\n");
   free(buf);
   return bio;
}

#endif // WIN32


/*
 *----------------------------------------------------------------------
 *
 * SSLCreateMemoryBIOFromBuffer()
 *
 *    Given a buffer, creates and memory BIO and populate it with
 *    contents of the buffer.
 *
 *    On success, caller is responsible for deallocating the create 
 *    BIO object.
 *
 * Results:
 *    TRUE if successful, FALSE otherwise.
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */

static BIO *
SSLCreateMemoryBIOFromBuffer(char *buffer,
                             int size) 
{
   BIO *bio = NULL;

   bio = BIO_new(BIO_s_mem());
   if (bio == NULL) {
      Warning("Create BIO failed.\n");
      return NULL;
   }

   if (buffer) {
      if (BIO_write(bio, buffer, size) <= 0) {
         Warning("Unable to write to BIO.\n");
         BIO_free(bio);
         return NULL;
      }
   }

//   SSL_LOG(("SSL: Create Memory BIO succeeded for buffer: %s\n", buffer));

   return bio;
}


/*
 *----------------------------------------------------------------------
 *
 * SSLLoadCertificatesFromFile()
 *
 *    Loads the server's certificate & private key from disk, setting
 *    them up for use by the default context.
 *    The ssl directory should be readable only by a privileged user,
 *    so must become root / __vmware_user__. 
 *    
 * Results:
 *    TRUE on succes, FALSE on failure.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */


static Bool
SSLLoadCertificatesFromFile(void)
{
   Bool success = FALSE;
   char *certFile = SSLCertFile;
   char *keyFile = SSLKeyFile;
   Bool su = IsSuperUser();
   SSL_CTX *ctx = SSL_DefaultContext();
   SuperUser(TRUE);

   /* Loads certificate */
   SSL_LOG(("SSL: Loading certificate: '%s' ...\n", certFile));
   if (!SSL_CTX_use_certificate_file(ctx, certFile, SSL_FILETYPE_PEM)) {
      SSLPrintErrors();
      Warning("Error loading server certificate\n");
      goto cleanup;
   }
   SSL_LOG(("SSL: server certificate read\n"));

   /* Loads private key */
   SSL_LOG(("SSL: Loading private key: '%s' ...\n", keyFile));
   if (!SSL_CTX_use_PrivateKey_file(ctx, keyFile, SSL_FILETYPE_PEM)) {
      SSLPrintErrors();
      Warning("Error loading server certificate\n");
      goto cleanup;
   }
   SSL_LOG(("SSL: server private key read\n"));

   if (!SSL_CTX_check_private_key(ctx)) {
      SSLPrintErrors();
      Warning("Error verifying server certificate\n");
      goto cleanup;
   }
   SSL_LOG(("SSL: server certificate verified\n"));

   success = TRUE;

cleanup:
   SuperUser(su);
   return success;
}


/*
 *----------------------------------------------------------------------
 *
 * SSLLoadCertificatesFromStore()
 *
 *    Loads the server's certificate & private key from system certificate
 *    store on Windows, setting them up for use by the default context.
 *
 * Results:
 *    TRUE on succes, FALSE on failure.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

#if _WIN32

static Bool
SSLLoadCertificatesFromStore(void)
{
   Bool success = FALSE;
   char *certFile = SSLCertFile;
   char *keyFile = SSLKeyFile;
   HANDLE hFile;
   BIO *bio;
   X509 *x509 = NULL;
   EVP_PKEY *pkey = NULL;
   SSL_CTX *ctx = SSL_DefaultContext();

   /* Loads certificate */
   SSL_LOG(("SSL: Loading certificate: '%s' ...\n", certFile));
   /*
    * Windows 2003 Server introduces the "Impersonate a client after 
    * authentication right", which we don't have by default. So we go back
    * and ask authd to open the certificate and private key files for us.
    */
   hFile = W32Auth_OpenSecurable(W32AuthSecurableFile, certFile, 
				 GENERIC_READ | GENERIC_WRITE, 0,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_READONLY);
   if (hFile == INVALID_HANDLE_VALUE) {
      Warning("Error opening server certificate\n");
      goto cleanup;
   }

   bio = SSLCreateMemoryBIOFromFile(hFile);

   CloseHandle(hFile);
   hFile = INVALID_HANDLE_VALUE;

   if (bio == NULL) {
      Warning("Error reading server certificate data\n");
      goto cleanup;
   }

   x509 = PEM_read_bio_X509(bio, NULL, NULL, NULL);
   if (x509 == NULL) {
      Warning("Error reading server certificate from BIO\n");
      BIO_free(bio);
      goto cleanup;
   }

   BIO_free(bio);
   bio = NULL;

   if (!SSL_CTX_use_certificate(ctx, x509)) {
      X509_free(x509);
      SSLPrintErrors();
      Warning("Error loading server certificate\n");
      goto cleanup;
   }
   X509_free(x509);
   SSL_LOG(("SSL: server certificate read\n"));

   /* Loads private key */
   SSL_LOG(("SSL: Loading private key: '%s' ...\n", keyFile));
   hFile = W32Auth_OpenSecurable(W32AuthSecurableFile, keyFile, 
				 GENERIC_READ | GENERIC_WRITE, 0,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_READONLY);
   if (hFile == INVALID_HANDLE_VALUE) {
      Warning("Error opening server private key\n");
      success = FALSE;
      goto cleanup;
   }

   bio = SSLCreateMemoryBIOFromFile(hFile);
   CloseHandle(hFile);
   hFile = INVALID_HANDLE_VALUE;
   if (bio == NULL) {
      Warning("Error reading server private key data\n");
      goto cleanup;
   }

   pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
   if (pkey == NULL) {
      Warning("Error reading server private key from BIO\n");
      BIO_free(bio);
      goto cleanup;
   }

   BIO_free(bio);

   if (!SSL_CTX_use_PrivateKey(ctx, pkey)) {
      EVP_PKEY_free(pkey);
      SSLPrintErrors();
      Warning("Error reading server private key\n");
      goto cleanup;
   }
   EVP_PKEY_free(pkey);
   SSL_LOG(("SSL: server private key read\n"));

   if (!SSL_CTX_check_private_key(ctx)) {
      SSLPrintErrors();
      Warning("Error verifying server certificate\n");
      goto cleanup;
   }
   SSL_LOG(("SSL: server certificate verified\n"));

   success = TRUE;

cleanup:
   return success;
}


#endif


/*
 *----------------------------------------------------------------------
 *
 * SSLLoadCertificates()
 *    Loads the server SSL certificate and private key.
 *    
 * Results:
 *    TRUE on success, FALSE otherwise
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static Bool
SSLLoadCertificates(Bool fromFile)
{
#if _WIN32
   if (!fromFile) {
      return SSLLoadCertificatesFromStore();
   }
#endif
   return SSLLoadCertificatesFromFile();
   
}

/*
 *----------------------------------------------------------------------
 *
 * SSL_New()
 *
 *    
 * Results:
 *    Returns a freshly allocated SSLSock structure.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

 SSLSock 
SSL_New(int fd,
        Bool closeFdOnShutdown)
{
   SSLSock sslConnection;
   Bool ret;

   sslConnection = (SSLSock)calloc(1, sizeof(struct SSLSockStruct));
   ASSERT_MEM_ALLOC(sslConnection);
   sslConnection->fd = fd;
   sslConnection->closeFdOnShutdown = closeFdOnShutdown;
#ifdef VMX86_DEVEL
   sslConnection->initialized = 12345;
#endif /*VMX86_DEVEL*/
   ret = SyncRecMutex_Init(&sslConnection->spinlock, NULL);
   ASSERT_NOT_IMPLEMENTED(ret == TRUE);
   return sslConnection;
}

 
/*
 *----------------------------------------------------------------------
 *
 * SSL_Connect()
 *
 *    Initiates an SSL connection using current SSL context.
 *
 *    XXX should modify callers of this to code to check for
 *    failure.  
 *
 *    Callers of this function don't expect an error, so cache failures
 *    for later reporting during an SSL{Read|Write}.
 *
 * Results:
 *    Returns TRUE on success, FALSE on failure.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

Bool
SSL_Connect(SSLSock sSock) // IN: SSL socket
{
   Warning("SSL_Connect: SECURITY WARNING: Should use SSL_ConnectAndVerify instead\n");
   return SSL_ConnectAndVerify(sSock, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_ConnectAndVerify()
 *
 *      Similar to SSL_Connect, but allows for verification of
 *      peer-certificate. Verification is turned on if the verifyParam
 *      is non-NULL, in which case the verifyParam data structure
 *      stores input and output parameters for the verification. Uses
 *      the default context.
 *
 *---------------------------------------------------------------------- 
 */

Bool
SSL_ConnectAndVerify(SSLSock sSock,               // IN: SSL socket
                     SSLVerifyParam *verifyParam) // IN: Certificate validation parameters
{
   return SSL_ConnectAndVerifyWithContext(sSock, verifyParam, SSL_DefaultContext());
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_ConnectAndVerifyWithContext()
 *
 *      Similar to SSL_Connect, but allows for verification of
 *      peer-certificate. Verification is turned on if the verifyParam
 *      is non-NULL, in which case the verifyParam data structure
 *      stores input and output parameters for the verification.
 *
 *---------------------------------------------------------------------- 
 */

Bool SSL_ConnectAndVerifyWithContext(SSLSock sSock,               // IN: SSL socket
                                     SSLVerifyParam *verifyParam, // IN: Certification validation parameters
                                     void *ctx)                   // IN: OpenSSL context (SSL_CTX *)
{
   int retVal;
   Bool ret = TRUE;
   time_t startTime;
   ASSERT_BUG(37562, SSLModuleInitialized);
   ASSERT(sSock);
   ASSERT(ctx);
   ASSERT_DEVEL(sSock->initialized == 12345);

   BEGIN_NO_STACK_MALLOC_TRACKER;

   sSock->sslCnx = SSL_new(ctx);
   if (!sSock->sslCnx) {
      SSLPrintErrors();
      Warning("Error creating sslCnx from ctx\n");
      sSock->connectionFailed = TRUE;
      ret = FALSE;
      goto end;
   }
   SSL_set_connect_state(sSock->sslCnx);

   if (verifyParam != NULL) {
      // Verify server-side certificates:
      SSL_set_ex_data(sSock->sslCnx, SSLVerifyParamIx, verifyParam);
      SSL_set_verify(sSock->sslCnx, SSL_VERIFY_PEER, SSLVerifyCb);
   }

   SSL_LOG(("SSL: connect, ssl created %d\n", sSock->fd));
   if (!SSL_set_fd(sSock->sslCnx, sSock->fd)) {
      SSLPrintErrors();
      Warning("Error setting fd for SSL connection\n");
      sSock->connectionFailed = TRUE;
      ret = FALSE;
      goto end;
   }
   SSL_LOG(("SSL: connect fd set done\n"));

   /* XXX Because we use non blocking sockets, it could be that
    * SSL_connect will return without finishing (because either
    * a read or write on the socket couldn't complete).  In this
    * case we wait a little bit and try again.   In practive this
    * seems to only happen on windows (an we loop only a couple of times).
    */
   retVal = SSL_connect(sSock->sslCnx);   
   sSock->sslIOError = SSL_get_error(sSock->sslCnx, retVal);
   startTime = time(NULL);            
   while((sSock->sslIOError == SSL_ERROR_WANT_WRITE || 
          sSock->sslIOError == SSL_ERROR_WANT_READ) &&
         time(NULL) - startTime < SSL_CONNECT_WAIT_TIMEOUT) {
      SSL_LOG(("SSL: connect busy waiting loop\n"));
      usleep(SSL_WAIT_TIME * 1000);
      retVal = SSL_connect(sSock->sslCnx);   
      sSock->sslIOError = SSLSetErrorState(sSock->sslCnx, retVal);
   }
   
   if (sSock->sslIOError != SSL_ERROR_NONE) {
      SSLPrintErrors();
      Warning("SSL: connect failed\n");
      sSock->connectionFailed = TRUE;
      ret = FALSE;
      goto end;
   }
   SSL_LOG(("SSL: connect done\n"));

   SSLPrintCipher(sSock->sslCnx);
    
   sSock->encrypted = TRUE;

  end:
   if (sSock->sslCnx != NULL) {
      SSL_set_ex_data(sSock->sslCnx, SSLVerifyParamIx, NULL);
   }

   END_NO_STACK_MALLOC_TRACKER;

   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_SetCertChain --
 *
 *    Adds the cert chain to the default SSL context.  Will add the
 *    first cert as a certificate, and the following certs as extra
 *    chain certs.
 *
 *
 * Results:
 *    None
 *
 *
 * Side effects:
 *    New certs in SSL context
 *
 *
 *----------------------------------------------------------------------
 */

void
SSL_SetCertChain(char **certChain,        // IN
                 int numCerts)            // IN
{
   int index;
   int ret;
   BIO *bioCert = NULL;
   X509 *cert = NULL;
   SSL_CTX *ctx = SSL_DefaultContext();

   SSL_LOG(("SSL: Adding %d certs as a chain\n", numCerts));

   if (numCerts == 0) {
      goto end;
   }

   ASSERT(certChain);
   ASSERT(certChain[0]);

   SSL_LOG(("SSL: Adding leaf cert\n%s\n", certChain[0]));

   /* The first cert is a certificate */
   bioCert = SSLCreateMemoryBIOFromBuffer(certChain[0],
                                          (int)(strlen(certChain[0]) + 1));
   if (bioCert == NULL) {
      Warning("SSL: Failed to create BIO");
      goto end;
   }

   cert = PEM_read_bio_X509(bioCert, NULL, 0, NULL);
   BIO_free(bioCert);
   if (cert == NULL) {
      Warning("SSL: Invalid certificate in chain (0):\n%s\n", certChain[0]);
      SSLPrintErrors();
      goto end;
   }

   ret = SSL_CTX_use_certificate(ctx, cert);
   X509_free(cert);
   if (!ret) {
      Warning("SSL: Failed to use certificate (0):\n%s\n", certChain[0]);
      SSLPrintErrors();
      goto end;
   }
   
   /*
    * Add the rest of the certificates as part of the chain
    */

   for (index = 1; index < numCerts; index++) {

      SSL_LOG(("SSL: Adding chain cert\n%s\n", certChain[index]));

      bioCert = SSLCreateMemoryBIOFromBuffer(certChain[index],
                                             (int)(strlen(certChain[index]) + 1));
      if (bioCert == NULL) {
         Warning("SSL: Failed to create BIO");
         goto end;
      }

      cert = PEM_read_bio_X509(bioCert, NULL, 0, NULL);
      BIO_free(bioCert);

      if (cert == NULL) {
         Warning("SSL: Invalid certificate in chain (%d):\n%s",
                 index, certChain[index]);
         SSLPrintErrors();
         goto end;
      }

      /* Call to SSL_CTX_add_extra_chain_cert */
      ret = SSL_CTX_ctrl(ctx, SSL_CTRL_EXTRA_CHAIN_CERT, 0, (char *)cert);
      X509_free(cert);
      if (!ret) {
         Warning("SSL: Failed to use certificate (%d): %s",
                 index, certChain[index]);
         SSLPrintErrors();
         goto end;
      }
   } /* Loop over cert chain */

   SSL_LOG(("SSL: Done adding chain certs\n"));

end:

   return;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_CheckCert --
 *
 *    Checks that the common name of the peer cert matches
 *    the hostname
 *
 *
 * Results:
 *    Returns TRUE on success, FALSE on failure.
 *
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

Bool
SSL_CheckCert(SSLSock sSock,         // IN
              char *host,            // IN
              Bool allowSelfSigned)  // IN
{
   X509 *peerCert = NULL;
   char peerCN[256];
   Bool ret = FALSE;
   int rslt = 0;

   ASSERT(sSock);
   ASSERT(sSock->sslCnx);

   SSL_LOG(("SSL: Peer Cert Check start\n"));
   rslt = SSL_get_verify_result(sSock->sslCnx);

   if (rslt != X509_V_OK) {
      if (allowSelfSigned &&
          ((rslt == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT) ||
           (rslt == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN))) {
         
         Warning("SSL: Self signed certificate in chain\n");
      } else {
         Warning("SSL: Peer certificate does not verify (%d)\n", rslt);
         goto end;
      }
   }

   peerCert = SSL_get_peer_certificate(sSock->sslCnx);
   if (peerCert == NULL) {
      Warning("SSL: Could not get the peer certificate\n");
      goto end;
   }

   X509_NAME_get_text_by_NID(X509_get_subject_name(peerCert),
                               NID_commonName,
                               peerCN,
                               256);

   if (strcasecmp(peerCN, host)) {
      Warning("SSL: Peer common name does not match host (%s != %s)!\n",
              peerCN, host);
      goto end;
   }

   /* All checks passed */
   ret = TRUE;

end:
   SSL_LOG(("SSL: Peer Cert Check end\n"));

   X509_free(peerCert);
   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_Accept()
 *
 *    Accepts an SSL connection using default SSL context.
 *    
 *    XXX should modify callers of this to code to check for
 *    failure.
 *
 *    Callers of this function don't expect an error, so cache failures
 *    for later reporting during an SSL{Read|Write}.
 *
 *    XXX: This is broken. SSL_accept must be retried until it succeeds, once it
 *    returns SSL_ERROR_WANT_<something>. OpenSSL API requires that no other IO
 *    operations be performed on the SSL until the IO operation that returned 
 *    the retry error on the SSL completes successfully. For blocking sockets,
 *    simply setting the auto-retry mode flag takes care of this transparently
 *    to the application. However for non-blocking sockets, the application must
 *    expect and handle transient errors as required by OpenSSL.
 *
 * Results:
 *    Returns TRUE on success, FALSE on failure.  
 *
 * Side effects:
 *    The server's certificate & private key may be loaded from disk.
 *
 *----------------------------------------------------------------------
 */

Bool
SSL_Accept(SSLSock sSock) // IN: SSL socket
{
   return SSL_AcceptWithContext(sSock, SSL_DefaultContext());
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_AcceptWithContext()
 *
 *    Accepts an SSL connection using the passed SSL context.
 *    
 *    XXX should modify callers of this to code to check for
 *    failure.
 *
 *    Callers of this function don't expect an error, so cache failures
 *    for later reporting during an SSL{Read|Write}.
 *
 *    XXX: This is broken. SSL_accept must be retried until it succeeds, once it
 *    returns SSL_ERROR_WANT_<something>. OpenSSL API requires that no other IO
 *    operations be performed on the SSL until the IO operation that returned 
 *    the retry error on the SSL completes successfully. For blocking sockets,
 *    simply setting the auto-retry mode flag takes care of this transparently
 *    to the application. However for non-blocking sockets, the application must
 *    expect and handle transient errors as required by OpenSSL.
 *
 * Results:
 *    Returns TRUE on success, FALSE on failure.  
 *
 * Side effects:
 *    The server's certificate & private key may be loaded from disk.
 *
 *----------------------------------------------------------------------
 */

Bool SSL_AcceptWithContext(SSLSock sSock, // IN: SSL socket
                           void *ctx)     // IN: OpenSSL context (SSL_CTX *)
{
   static Bool acceptInitialized = FALSE; 
   Bool ret = TRUE;
   int sslRet;

   ASSERT(SSLModuleInitialized);
   ASSERT(sSock);
   ASSERT_DEVEL(sSock->initialized == 12345);
   ASSERT(ctx);

   BEGIN_NO_STACK_MALLOC_TRACKER;

   if (!acceptInitialized) {
      if (!SSLLoadCertificates(loadCertificatesFromFile) && requireCertificates) {
         sSock->connectionFailed = TRUE;
         ret = FALSE;
         goto end;
      }
      acceptInitialized = TRUE;
   }

   sSock->sslCnx = SSL_new(ctx);
   if (!sSock->sslCnx) {
      SSLPrintErrors();
      Warning("Error Creating SSL connection structure\n");
      sSock->connectionFailed = TRUE;
      ret = FALSE;
      goto end;
   }
   SSL_set_accept_state(sSock->sslCnx);

   SSL_LOG(("SSL: ssl created\n"));
   if (!SSL_set_fd(sSock->sslCnx, sSock->fd)) {
      SSLPrintErrors();
      Warning("Error setting fd for SSL connection\n");
      sSock->connectionFailed = TRUE;
      ret = FALSE;
      goto end;
   }
   SSL_LOG(("SSL: fd set done\n"));

  /*
   * Because we use non-blocking sockets, this might not actually finish We
   * could wait for SSL_accept to finish, but that causes problems with a VM
   * trying to suspend itself using perlAPI.  (SSL_Accept would then effectively
   * block -- which causes the VM to block, and deadlock ensues.
   */

   /*
    * XXX: There needs to be a better solution for the above problem, that does
    * not violate OpenSSL API requirements as described in the function header.
    * Until this is properly fixed, applications may use SSL_CompleteAccept to
    * loop around until success.
    */

   sslRet = SSL_accept(sSock->sslCnx);
   sSock->sslIOError = SSL_get_error(sSock->sslCnx, sslRet);
   sSock->encrypted = TRUE;

  end:
   END_NO_STACK_MALLOC_TRACKER;

   return ret;
}

/*
 *----------------------------------------------------------------------
 *
 * SSL_Read --
 *
 *    Functional equivalent of the read() syscall. 
 *    XXX should detect for packets that fail
 *    the MAC verification  & warn user that someone is being tricky.
 *
 * Results:
 *    Returns the number of bytes read, or -1 on error.  The
 *    data read will be placed in buf.
 *
 * Side effects:
 *
 *
 *----------------------------------------------------------------------
 */

ssize_t 
SSL_Read(SSLSock ssl,
         char *buf,
         size_t num)
{
   int ret;
   ASSERT(ssl);
   ASSERT_DEVEL(ssl->initialized == 12345);

   BEGIN_NO_STACK_MALLOC_TRACKER;

   if (ssl->connectionFailed) {
      SSLSetSystemError(SSL_SOCK_LOST_CONNECTION);
      ret = SOCKET_ERROR;
      goto end;
   }

   if (ssl->encrypted) {
      int result = SSL_read(ssl->sslCnx, buf, (int)num);

      ssl->sslIOError = SSLSetErrorState(ssl->sslCnx, result);
      if (ssl->sslIOError != SSL_ERROR_NONE) {
         SSL_LOG(("SSL: Read(%d, %p, %d): %d\n",
                  ssl->fd, buf, num, result));
         result = SOCKET_ERROR;
      }
      ret = result;
   } else {
#ifdef __APPLE__
blocking:
#endif
      ret = SSLGeneric_read(ssl->fd, buf, (int)num);

#ifdef __APPLE__
      /*
       * There is a bug on Mac OS 10.4.x where read(2) can return zero
       * even if the other end of the socket is not disconnected.
       * We verify this by calling write(ssl->fd, "", 0) and
       * see if it returns -1 with errno==EPIPE.  If that doesn't
       * happen, the socket is okay.  This has a side-effect where
       * a SIGPIPE signal will be sent to this process and
       * all apps that use this will need to handle that correctly.
       */
      if (ret == 0) {
         ssize_t writeRet;
         Bool ignoreError = FALSE;

#ifdef VMX86_DEBUG
         struct stat statBuffer;

         /*
          * Make sure we're using a socket.
          */
         ASSERT((fstat(ssl->fd, &statBuffer) == 0) &&
                ((statBuffer.st_mode & S_IFSOCK) == S_IFSOCK));
         
#endif
         writeRet = write(ssl->fd, "", 0);
         if (writeRet == 0) {
            char const *workaroundEnv;

            /*
             * The socket is still good.  read(2) should not have
             * returned zero.
             */
            if (! ssl->loggedKernelReadBug) {
               Log("Error: Encountered Apple bug #5202831.  Disconnecting.\n");
               ssl->loggedKernelReadBug = TRUE;
            }

            /*
             * One workaround is to let the caller function deal with this, such
             * as to remove the socket from poll for a little while. This doesn't
             * really fix the problem, but it removes the socket from Poll so we
             * don't spin on it. The caller is responsible for adding the socket
             * back to the Poll list in some time. That means we don't see any
             * activity on the socket while it is off poll, so the timeout should 
             * be short. During that timeout, we miss any event on this socket like
             * new data or actually closing the socket.
             *
             * Really, this is still just spinning on the socket, but it spins
             * more slowly. So, we tradeoff some responsiveness (we don't see socket
             * events promptly) for less performance impact on the overall system
             * by processing fewer false events on the socket.
             */
#if __APPLE_READ_BUG_WORKAROUND__
            if (ssl->errorHook) {
               ignoreError = (ssl->errorHook)(ssl, ssl->errorHookContext);
            }
#endif

            /*
             * Another "workaround" keeps things running, but hogs the
             * CPU. So only enable it when a particular environment
             * variable is set (for QA or a customer with a specific
             * need).
             */
            if (!ignoreError) {
               workaroundEnv = Posix_Getenv("VMWARE_SOCKET_WORKAROUND");
               if (workaroundEnv != NULL &&
                   Str_Strcasecmp(workaroundEnv, "YES") == 0) {
                  ignoreError = TRUE;
               }
            }

            if (ignoreError) {
               int fcntlFlags;

               fcntlFlags = fcntl(ssl->fd, F_GETFL, 0);
               if ((fcntlFlags & O_NONBLOCK) == O_NONBLOCK) {
                  /*
                   * The socket is not blocking, so let's pretend this
                   * is EAGAIN to make everyone happy.
                   */
                  ret = -1;
                  errno = EAGAIN;
               } else {
                  /*
                   * The socket is blocking.  Spin until we get
                   * real data or an end-of-stream where write(2) fails.
                   */
                  goto blocking;
               }
            }
         }
      }
#endif
   }

  end:
   END_NO_STACK_MALLOC_TRACKER;
   return ret;
}

 
/*
 *----------------------------------------------------------------------
 *
 * SSL_Write()
 *
 *    Functional equivalent of the write() syscall. 
 *    
 *
 * Results:
 *    Returns the number of bytes written, or -1 on error.  
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

ssize_t 
SSL_Write(SSLSock ssl,
          const char *buf,
          size_t num)
{
   int ret;
   ASSERT(ssl);
   ASSERT_DEVEL(ssl->initialized == 12345);

   BEGIN_NO_STACK_MALLOC_TRACKER;

   if (ssl->connectionFailed) {
      SSLSetSystemError(SSL_SOCK_LOST_CONNECTION);
      ret = SOCKET_ERROR;
      goto end;
   }
   if (ssl->encrypted) {
      int result = SSL_write(ssl->sslCnx, buf, (int)num);

      ssl->sslIOError = SSLSetErrorState(ssl->sslCnx, result);
      if (ssl->sslIOError != SSL_ERROR_NONE) {
         SSL_LOG(("SSL: Write(%d)\n", ssl->fd));
         result = SOCKET_ERROR;
      }
      ret = result;
   } else {
      ret = SSLGeneric_write(ssl->fd, buf, (int)num);
   }

  end:
   END_NO_STACK_MALLOC_TRACKER;
   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_Pending()
 *	
 *	Functional equivalent of select when SSL is enabled
 *      
 * Results:
 * 	Obtain number of readable bytes buffered in an SSL object if SSL
 *	is enabled, otherwise, return 0
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

int 
SSL_Pending(SSLSock ssl) // IN
{
   int ret;
   ASSERT(ssl);
   ASSERT_DEVEL(ssl->initialized == 12345);

   BEGIN_NO_STACK_MALLOC_TRACKER;

   if (ssl->encrypted) {
      ret = SSL_pending(ssl->sslCnx);
   } else {
      ret = 0;
   }

   END_NO_STACK_MALLOC_TRACKER;
   return ret;
}

 
/*
 *----------------------------------------------------------------------
 *
 * SSL_Shutdown()
 *
 *    Functional equivalent of the close() syscall.  Does
 *    not close the actual fd used for the connection.
 *    
 *
 * Results:
 *    0 on success, -1 on failure.
 *
 * Side effects:
 *    closes the SSL connection, freeing up the memory associated
 *    with the passed in ssl object
 *
 *----------------------------------------------------------------------
 */

int 
SSL_Shutdown(SSLSock ssl)
{
   int retVal = 0;
   ASSERT(ssl);
   ASSERT_DEVEL(ssl->initialized == 12345);
#ifdef VMX86_DEVEL
   ssl->initialized = 0;
#endif 

   BEGIN_NO_STACK_MALLOC_TRACKER;

   SSL_LOG(("SSL: Starting shutdown for %d\n", ssl->fd));
   if (ssl->encrypted) {
      /* since quiet_shutdown is set, SSL_shutdown always succeeds */
      SSL_shutdown(ssl->sslCnx);
      SSL_free(ssl->sslCnx);
   }

   if (ssl->closeFdOnShutdown) {
      SSL_LOG(("SSL: Trying to close %d\n", ssl->fd));
      retVal = SSLGeneric_close(ssl->fd);
   }

   END_NO_STACK_MALLOC_TRACKER;

   SyncRecMutex_Destroy(&ssl->spinlock);

   free(ssl);
   SSL_LOG(("SSL: shutdown done\n"));
   return retVal;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_GetFd()
 *
 *    Returns an SSL socket's file descriptor or handle.    
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

int 
SSL_GetFd(SSLSock ssl) // IN
{
   ASSERT(ssl);
   ASSERT_DEVEL(ssl->initialized == 12345);

   return ssl->fd;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_SetMode()
 *   
 *    Wrapper around SSL_set_mode.
 *
 * Results:
 *    Return value of SSL_set_mode.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

long
SSL_SetMode(SSLSock ssl, long mode)
{
   ASSERT(ssl);
   ASSERT_DEVEL(ssl->initialized == 12345);
   ASSERT(ssl->sslCnx);

   return SSL_set_mode(ssl->sslCnx, mode);
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_Want()
 *   
 *    Wrapper around SSL_want.
 *
 * Results:
 *    That of SSL_want.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

int
SSL_Want(const SSLSock ssl)
{
   ASSERT(ssl);
   ASSERT_DEVEL(ssl->initialized == 12345);
   ASSERT(ssl->sslCnx);

   return SSL_want(ssl->sslCnx);
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_WantWrite()
 *   
 *    Wrapper around SSL_want_write.
 *
 * Results:
 *    That of SSL_want_write.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

int
SSL_WantWrite(const SSLSock ssl)
{
   ASSERT(ssl);
   ASSERT_DEVEL(ssl->initialized == 12345);
   ASSERT(ssl->sslCnx);

   return SSL_want_write(ssl->sslCnx);
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_WantRead()
 *   
 *    Wrapper around SSL_want_read.
 *
 * Results:
 *    That of SSL_want_read.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

int
SSL_WantRead(const SSLSock ssl)
{
   ASSERT(ssl);
   ASSERT_DEVEL(ssl->initialized == 12345);
   ASSERT(ssl->sslCnx);

   return SSL_want_read(ssl->sslCnx);
}


/*
 *----------------------------------------------------------------------
 *
 * SSLSafeIO()
 *
 *    Perform an SSL IO operation and store error information in the SSLSock
 *    structure, for later use by subsequest SSLSafeIO calls.
 *
 *    Best practices:
 *    - Once a retry condition is returned from an IO, then no other IO must be
 *      performed on the SSL until the call that generated the retry condition
 *      succeeds. We enforce this using the ioState member in SSLSock.
 *
 *    - Applications can reduce the likelihood of hitting retry errors by
 *      ensuring that the underlying socket can actually perform the IO being
 *      requested without blocking(or returning EWOULDBLOCK) before re/trying IO.
 *
 *    - Further, in the case of a read operation, SSL_pending may be used to
 *      acertain if the SSL has any decrypted data in its buffer.
 *
 *    - SSLSafeIO should be used only for non-blocking IO. Blocking IO must use
 *      SSL_Read and SSL_Write.
 *
 *
 * Results:
 *    Returns < 0 on irrecoverable error.
 *    Returns > 0 on success.
 *    Returns 0 when the operation must be retried.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static ssize_t
SSLSafeIO(SSLSock ssl,
          void *buf,
          size_t num,
          Bool sslread)
{
   IOState thisInprogress, otherInprogress;
   IOState ioState;
   Bool safe = FALSE;
   ssize_t ret = 0;

   ASSERT(ssl);
   ASSERT(ssl->sslCnx);
   ASSERT_DEVEL(ssl->initialized == 12345);
   ASSERT(ssl->encrypted);

   BEGIN_NO_STACK_MALLOC_TRACKER;

   if (ssl->connectionFailed) {
      SSLSetSystemError(SSL_SOCK_LOST_CONNECTION);
      ret = SOCKET_ERROR;
      goto end;
   }

   if (sslread) {
      thisInprogress = IOSTATE_READ_INPROGRESS;
      otherInprogress = IOSTATE_WRITE_INPROGRESS;
   } else {
      thisInprogress = IOSTATE_WRITE_INPROGRESS;
      otherInprogress = IOSTATE_READ_INPROGRESS;
   }
   
   if (!SyncRecMutex_Trylock(&ssl->spinlock)) {
      /* Another thread has locked this SSLSock. Caller should retry. */
      goto end;
   }

   if (ssl->ioState != otherInprogress) {
      safe = TRUE;
   }
   ioState = ssl->ioState;

   if (safe) {
      /* Safe to proceed with IO operation. */

      ret = (ssize_t)(sslread ? 
		      SSL_read(ssl->sslCnx, buf, (int)num) : 
		      SSL_write(ssl->sslCnx, buf, (int)num));
      ssl->sslIOError = SSLSetErrorState(ssl->sslCnx, (int)ret);

      switch (ssl->sslIOError) {
         case SSL_ERROR_NONE:
            if (ioState != IOSTATE_READY) {
               /* Clear the SSLSock state so that other IO can proceed. */
               ssl->ioState = IOSTATE_READY;
            }
            break;
         case SSL_ERROR_WANT_READ:
         case SSL_ERROR_WANT_WRITE:
            /* Retry. Block other IO operations. */
            if (ioState == IOSTATE_READY) {
               ssl->ioState = thisInprogress;
            }
            ret = 0;
            break;
         default:
            /* Irrecoverable error. */
            /* XXX: SSL_ERROR_WANT_X509_LOOKUP is not handled yet. */
            SSLSetSystemError(SSL_SOCK_LOST_CONNECTION);
            ret = SOCKET_ERROR;
            ssl->connectionFailed = TRUE;
            break;
      }
   }

   SyncRecMutex_Unlock(&ssl->spinlock);

end:
   END_NO_STACK_MALLOC_TRACKER;
   return ret;
}

/*
 *----------------------------------------------------------------------
 *
 * SSL_SafeRead()
 *
 *    Perform a single-shot non-blocking read. 
 *    If 0 is returned, then SSL_SafeRead must be retried before another IO
 *    (SSL_SafeWrite) can proceed on this SSL.
 *
 * Results:
 *    Returns number of bytes read into the given buffer, which may be 0.
 *    Returns < 0 on irrecoverable error.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

ssize_t
SSL_SafeRead(SSLSock ssl,
             void *buf,
             size_t num)
{
   return SSLSafeIO(ssl, buf, num, TRUE);
}


/* 
 *----------------------------------------------------------------------
 *
 * SSL_SafeWrite()
 *
 *    Perform a single shot, non-blocking write.
 *    If 0 is returned, then SSL_SafeWrite must be retried before another IO
 *    (SSL_SafeRead) can proceed on this SSL.
 *
 * Results:
 *    Return the number of bytes written, which may be 0, if none were written. 
 *    On irrecoverable error, returns < 0.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

ssize_t
SSL_SafeWrite(SSLSock ssl,
              void *buf,
              size_t num)
{
   return SSLSafeIO(ssl, buf, num, FALSE);
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_CompleteAccept()
 *
 *    Sleep and loop until the SSL_accept succeeds. SSL_Accept must 
 *    have been called before this so that sslIOError reflects the 
 *    outcome of the last SSL_accept.
 *
 *    XXX: SSL_Accept is broken - OpenSSL requires that the IO operation
 *    (read/write/connect/accept), that needs to be retried, be retried until it
 *    succeeds, and no other IO operation be attempted on the same SSL in the
 *    meantime. So we must retry SSL_accept until it succeeds, and we must not 
 *    call any other IO operation on the SSL until the SSL_accept completes.
 *
 *    XXX: The better way to fix this API is to rename SSL_Accept to 
 *    SSL_AcceptInit and move the call to SSL_accept from there to a 
 *    new wrapper function called SSL_Accept. Applications can then call 
 *    SSL_AcceptInit once, and loop around SSL_Accept until it succeeds. 
 *    Leaving this for another day, since all the call sites would need
 *    to change.
 *
 * Results:
 *    TRUE if the SSL_accept completed successfully. FALSE otherwise.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

Bool
SSL_CompleteAccept(SSLSock ssl)
{
   ASSERT(ssl);
   ASSERT(ssl->sslCnx);

   if (ssl->connectionFailed) {
      SSLSetSystemError(SSL_SOCK_LOST_CONNECTION);
      return FALSE;
   }

   while (1) {
      int sslRet;

      switch (ssl->sslIOError) {
         case SSL_ERROR_NONE:
            return TRUE;
         case SSL_ERROR_WANT_READ:
         case SSL_ERROR_WANT_WRITE:
            usleep(SSL_WAIT_TIME * 1000);
            break;
         default:
            ssl->connectionFailed = TRUE;
            return FALSE;
      }
      sslRet = SSL_accept(ssl->sslCnx);
      ssl->sslIOError = SSL_get_error(ssl->sslCnx, sslRet);
   }

   return FALSE;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_SetRequireCerts()
 *    
 *    Sets the global flag to the input parameter. Must be called after
 *    SSL_InitEx. By default, certificates are required. So the client must
 *    explicitly choose to disable enforcement.
 *
 * Results:
 *    Nothing.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

void
SSL_SetRequireCerts(Bool required) // IN
{
   ASSERT(SSLModuleInitialized);
   requireCertificates = required;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_SetLoadCertificatesFromFile()
 *    
 *    This is useful only for Windows. Sets the global flag to the input
 *    parameter. Must be called after SSL_InitEx. By default, certificate and
 *    private key are loaded from system certificate store. This flag makes
 *    SSLLoadCertificates load them from files instead.
 *
 * Results:
 *    Nothing.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

void
SSL_SetLoadCertificatesFromFile(Bool value)
{
   ASSERT(SSLModuleInitialized);
   loadCertificatesFromFile = value;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_SetDHParamFiles()
 *    
 *    Sets the DH parameter file paths.
 *
 * Results:
 *    Nothing.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

void
SSL_SetDHParamFiles(const char *dh512File,   // IN
                    const char *dh1024File)  // IN
{
   ASSERT(dh512File || dh1024File);

   if (dh512File) {
      free(SSLDHParamsFiles[0]);
      SSLDHParamsFiles[0] = strdup(dh512File);
      ASSERT_MEM_ALLOC(SSLDHParamsFiles[0]);
   }
   if (dh1024File) {
      free(SSLDHParamsFiles[1]);
      SSLDHParamsFiles[1] = strdup(dh1024File);
      ASSERT_MEM_ALLOC(SSLDHParamsFiles[1]);
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_BIO_new_file
 *    
 *    Wrapper around BIO_new_file that converts "filename" from UTF-8
 *    to the local encoding.
 *
 * Results:
 *    New BIO object. Return value is "void *" because we can't
 *    include bio.h in ssl.h, or define BIO there, reliably.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

void *
SSL_BIO_new_file(const char *filename, // IN
                 const char *mode)     // IN
{
   BIO *res = NULL;
   char *path2 = NULL;

   ASSERT(filename);

   if (!CodeSet_Utf8ToCurrent(filename, strlen(filename), &path2, NULL)) {
      goto exit;
   }

   res = BIO_new_file(path2, mode);

  exit:
   free(path2);
   return res;
}


#ifdef __APPLE_READ_BUG_WORKAROUND__
/*
 *----------------------------------------------------------------------
 *
 * SSL_SetErrorHook
 *
 * Register a hook function that can handle read errors on the socket.
 * This allows higher levels of code to provide a workaround for the Apple bug 5202831.
 *
 *---------------------------------------------------------------------- 
 */

void
SSL_SetErrorHook(SSLSock ssl,                         // IN
                 SSLLibHandleErrorHookFn *hookProc,   // IN
                 void *context)                       // IN
{
   ASSERT(ssl);
   ASSERT_DEVEL(ssl->errorHook == NULL);

   ssl->errorHook = hookProc;
   ssl->errorHookContext = context;
}
#endif // __APPLE_READ_BUG_WORKAROUND__

