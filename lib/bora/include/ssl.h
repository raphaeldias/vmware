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
 * ssl.h --
 *
 *	SSL encryption.
 *
 *	glob together common tasks,  isolate rest of code from
 *	ugly openssl includes.
 */

#ifndef _SSL_H_
#define _SSL_H_

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#include "vm_basic_types.h"

/*
 * SSLVerifyParam --
 *
 *      Specifies in/out parameters used to verify the peer
 *      certificate. The hostName, if non-zero in length, specifies the name used in the
 *      connection, and should match the DNS name on the received certificate.  If
 *      the host name is zero in length, that means that the certificate will be 
 *      checked but that there is no matching done between the host name and the DNS
 *      name on the received certificate.  This is useful for the case of a client
 *      certificate, where the certificate, if supplied by the client, is expected to
 *      be signed properly, but the DNS information is irrelevant and information
 *      from the client certificate is passed to some higher layer for authentication
 *      purposes.
 *
 *      The errorText is an output parameter that stores the error text of the 
 *      verification error if applicable.
 *
 *      This parameter can be attached to an SSLSock by calling
 *      SSL_SetVerifyParam.
 */

#define SSL_HOSTNAME_SIZE 0x200
#define SSL_V_THUMBPRINT_SIZE 20
#define SSL_V_THUMBPRINT_STRING_SIZE (3*SSL_V_THUMBPRINT_SIZE + 1)

typedef struct _SSLVerifyParam {
   /*
    * IN param. In the case of CA-sigend certificates, this is used to
    * be compared with the subject name of the certificate.  
    */
   char hostName[SSL_HOSTNAME_SIZE];

   /*
    * IN/OUT param. The SHA1 digest of the certificate unparsed as a
    *     NULL-terminated string.
    *
    *     If an IN param, this is the expected digest of the
    *     certificate. The server is authenticated if this digest
    *     matches the actual certificate received from the server.
    *
    *     If no digest matching is to take place, then this should be
    *     an empty string
    *
    *     After cert verification completes, the OUT param stores the
    *     digest of the self-signed certificate actually received from
    *     the server.
    */
   char thumbprintString[SSL_V_THUMBPRINT_STRING_SIZE];
   
   /* OUT param. Whether there was a verification error */
   Bool hasError;

   /* OUT param. Whether the cert was celf-signed */
   Bool selfSigned;

   /*
    * OUT param. Human readable explanation of the errors encountered
    * with the certificate.  
    */
   char errorText[0x200];
} SSLVerifyParam;

/*
 * SSLVerifyType --
 *
 *      Specifies whether peer certificates should be verified:
 *
 *      SSL_VERIFY_CERTIFICATES_OFF - certificates are not verified
 *
 *      SSL_VERIFY_CERTIFICATES_ON - certificates are verified
 *
 *      SSL_VERIFY_CERTIFICATES_DEFAULT - the decision of whether
 *      certificates are verified or not is determined by system
 *      settings. On Windows these are under the following registry
 *      key:
 *
 *      HKLM\SOFTWARE\VMware, Inc.\<Product Name>\VerifySSLCertificates
 *
 *      If the above registry flag is not set, then the default state
 *      is to not verify peer SSL certificates
 */

typedef enum {
   SSL_VERIFY_CERTIFICATES_OFF = -1,
   SSL_VERIFY_CERTIFICATES_DEFAULT = 0,
   SSL_VERIFY_CERTIFICATES_ON = 1,
} SSLVerifyType;

typedef enum {
   IOSTATE_READY = 0,
   IOSTATE_READ_INPROGRESS = 1,
   IOSTATE_WRITE_INPROGRESS = 2,
} IOState;

typedef struct SSLSockStruct *SSLSock;
typedef char* (SSLLibFn)(const char*, const char*);

void SSL_Init(SSLLibFn *getLibFn, const char *defaultLib, const char *name);
void SSL_InitEx(SSLLibFn *getLibFn, const char *defaultLib, const char *name,
                Bool useSystem, Bool doVersionCheck, Bool disableLoading);
void *SSL_DefaultContext(void);
void SSL_SetCertChain(char **certChain, int numCerts);
void SSL_SetVerifySSLCertificates(SSLVerifyType verify);
SSLVerifyType SSL_GetVerifySSLCertificates(void);
void SSL_SetCerts(const char* certFile, const char* keyFile);
void SSL_SetCiphers(const char* ciphers);
void SSL_Exit(void);
SSLSock SSL_New(int fd,Bool closeFdOnShutdown);
Bool SSL_Connect(SSLSock sSock);
Bool SSL_ConnectAndVerify(SSLSock sSock, SSLVerifyParam *verifyParam);
Bool SSL_ConnectAndVerifyWithContext(SSLSock sSock, SSLVerifyParam *verifyParam,
                                     void *ctx);
Bool SSL_VerifyX509(SSLVerifyParam *verifyParam, void *x509Cert);
Bool SSL_Accept(SSLSock sSock);
Bool SSL_AcceptWithContext(SSLSock sSock, void *ctx);
Bool SSL_CheckCert(SSLSock sSock, char *host, Bool allowSelfSigned);
ssize_t SSL_Read(SSLSock ssl, char *buf, size_t num);
ssize_t SSL_Write(SSLSock ssl,const char  *buf, size_t num);
int SSL_Shutdown(SSLSock ssl);
int SSL_GetFd(SSLSock sSock);
int SSL_Pending(SSLSock ssl);
long SSL_SetMode(SSLSock ssl, long mode);
int SSL_Want(const SSLSock ssl);
int SSL_WantRead(const SSLSock ssl);
int SSL_WantWrite(const SSLSock ssl);
ssize_t SSL_SafeRead(SSLSock ssl, void *buf, size_t num);
ssize_t SSL_SafeWrite(SSLSock ssl, void *buf, size_t num);
Bool SSL_CompleteAccept(SSLSock ssl);
void SSL_SetRequireCerts(Bool required);
void SSL_SetDHParamFiles(const char *dh512File, const char *dh1024File);
void SSL_SetLoadCertificatesFromFile(Bool value);

#define CONFIG_VMWARESSLDIR  "libdir"

#ifdef _WIN32
#define DEFAULT_SSLLIBDIR NULL
#define SSLGeneric_read(sock,buf,num) recv(sock,buf,num,0)
#define SSLGeneric_write(sock,buf,num) send(sock,buf,num,0)
#define SSLGeneric_close(sock) closesocket(sock)
#else
#define DEFAULT_SSLLIBDIR DEFAULT_LIBDIRECTORY
#define SSLGeneric_read(sock,buf,num) read(sock,buf,num)
#define SSLGeneric_write(sock,buf,num) write(sock,buf,num)
#define SSLGeneric_close(sock) close(sock)
#endif

#ifdef _WIN32
Bool SSLVerifyCertAgainstSystemStore(const unsigned char *certBytes,  // IN
                                     int certLen,  // IN
                                     SSLVerifyParam *verifyParam);
#endif

void *SSL_BIO_new_file(const char *filename, const char *mode);

/* XXX Coding convention violation
 */
#define SSL_WrapRead(connection,buf,num) SSL_Read(connection->ssl,buf,num)
#define SSL_WrapWrite(connection,buf,num) SSL_Write(connection->ssl,buf,num)
#define SSL_WrapShutdown(connection) SSL_Shutdown(connection->ssl)

/*
 * This is a hook function that can handle read errors on the socket.
 * This allows higher levels of code to provide a workaround for the Apple bug 5202831.
 */
#ifdef __APPLE__
#define __APPLE_READ_BUG_WORKAROUND__ 1
#endif

#ifdef __APPLE_READ_BUG_WORKAROUND__
typedef Bool SSLLibHandleErrorHookFn(SSLSock sSock, void *context);
void SSL_SetErrorHook(SSLSock ssl, SSLLibHandleErrorHookFn *hookProc, void *context);
#endif

#endif // ifndef _SSL_H_
