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

#ifndef _BASIC_HTTP_H_
#define _BASIC_HTTP_H_

#ifdef __cplusplus
extern "C"{
#endif

#include "vmware.h"
#include "dynbuf.h"
#include "poll.h"


typedef int BasicHttpMethod;
enum {
   BASICHTTP_METHOD_GET          = 0,
   BASICHTTP_METHOD_POST         = 1,
   BASICHTTP_METHOD_HEAD         = 2,
   BASICHTTP_METHOD_UNKNOWN      = 3,
};

typedef unsigned int BasicHttpErrorCode;
enum {
   /* 1:1 mapped curl errors. */
   BASICHTTP_ERROR_NONE                  = 0,
   BASICHTTP_ERROR_UNSUPPORTED_PROTOCOL  = 1,
   BASICHTTP_ERROR_URL_MALFORMAT         = 3,
   BASICHTTP_ERROR_COULDNT_RESOLVE_PROXY = 5,
   BASICHTTP_ERROR_COULDNT_RESOLVE_HOST  = 6,
   BASICHTTP_ERROR_COULDNT_CONNECT       = 7,
   BASICHTTP_ERROR_HTTP_RETURNED_ERROR   = 22,
   BASICHTTP_ERROR_OPERATION_TIMEDOUT    = 28,
   BASICHTTP_ERROR_SSL_CONNECT_ERROR     = 35,
   BASICHTTP_ERROR_TOO_MANY_REDIRECTS    = 47,

   /* n:1 mapped curl errors. */
   BASICHTTP_ERROR_TRANSFER,
   BASICHTTP_ERROR_SSL_SECURITY,

   /* generic error. */
   BASICHTTP_ERROR_GENERIC,
};

typedef long BasicHttpResponseCode;
enum {
   /* Informational */
   BASICHTTP_RESPONSE_CONTINUE                     = 100,
   BASICHTTP_RESPONSE_SWITCHINGPROTOCOLS           = 101,

   /* Success */
   BASICHTTP_RESPONSE_OK                           = 200,
   BASICHTTP_RESPONSE_CREATED                      = 201,
   BASICHTTP_RESPONSE_ACCEPTED                     = 202,
   BASICHTTP_RESPONSE_NONAUTHORITATIVEINFORMATION  = 203,
   BASICHTTP_RESPONSE_NOCONTENT                    = 204,
   BASICHTTP_RESPONSE_RESETCONTENT                 = 205,
   BASICHTTP_RESPONSE_PARTIALCONTENT               = 206,

   /* Redirection */
   BASICHTTP_RESPONSE_MULTIPLECHOICES              = 300,
   BASICHTTP_RESPONSE_MOVEDPERMANENTLY             = 301,
   BASICHTTP_RESPONSE_FOUND                        = 302,
   BASICHTTP_RESPONSE_SEEOTHER                     = 303,
   BASICHTTP_RESPONSE_NOTMODIFIED                  = 304,
   BASICHTTP_RESPONSE_USEPROXY                     = 305,
   BASICHTTP_RESPONSE_TEMPORARYREDIRECT            = 307,

   /* Client Error */
   BASICHTTP_RESPONSE_BADREQUEST                   = 400,
   BASICHTTP_RESPONSE_UNAUTHORIZED                 = 401,
   BASICHTTP_RESPONSE_PAYMENTREQUIRED              = 402,
   BASICHTTP_RESPONSE_FORBIDDEN                    = 403,
   BASICHTTP_RESPONSE_NOTFOUND                     = 404,
   BASICHTTP_RESPONSE_METHODNOTALLOWED             = 405,
   BASICHTTP_RESPONSE_NOTACCEPTABLE                = 406,
   BASICHTTP_RESPONSE_PROXYAUTHORIZATIONREQUIRED   = 407,
   BASICHTTP_RESPONSE_REQUESTTIMEOUT               = 408,
   BASICHTTP_RESPONSE_CONFLICT                     = 409,
   BASICHTTP_RESPONSE_GONE                         = 410,
   BASICHTTP_RESPONSE_LENGTHREQUIRED               = 411,
   BASICHTTP_RESPONSE_PRECONDITIONFAILED           = 412,
   BASICHTTP_RESPONSE_REQUESTENTITYTOOLARGE        = 413,
   BASICHTTP_RESPONSE_REQUESTURITOOLONG            = 414,
   BASICHTTP_RESPONSE_UNSUPPORTEDMEDIATYPE         = 415,
   BASICHTTP_RESPONSE_REQUESTEDRANGENOTSATISFIABLE = 416,
   BASICHTTP_RESPONSE_EXPECTATIONFAILED            = 417,

   /* Server Error */
   BASICHTTP_RESPONSE_INTERNALSERVERERROR          = 500,
   BASICHTTP_RESPONSE_NOTIMPLEMENTED               = 501,
   BASICHTTP_RESPONSE_BADGATEWAY                   = 502,
   BASICHTTP_RESPONSE_SERVICEUNAVAILABLE           = 503,
   BASICHTTP_RESPONSE_GATEWAYTIMEOUT               = 504,
   BASICHTTP_RESPONSE_HTTPVERSIONNOTSUPPORTED      = 505,
};

typedef int BasicHttpAuthenticationType;
enum {
   BASICHTTP_AUTHENTICATION_NONE = 0,
   BASICHTTP_AUTHENTICATION_BASIC,
   BASICHTTP_AUTHENTICATION_DIGEST,
   BASICHTTP_AUTHENTICATION_NTLM,
   BASICHTTP_AUTHENTICATION_ANY,
   // To be done, Digest, NTLM, etc.
};

typedef struct BasicHttpRequest BasicHttpRequest;
typedef struct BasicHttpResponse BasicHttpResponse;
typedef struct BasicHttpCookieJar BasicHttpCookieJar;

#define BASICHTTP_DEFAULT_COOKIEJAR     ((BasicHttpCookieJar *) 1)

typedef void BasicHttpOnSentProc(BasicHttpRequest *request,
                                 BasicHttpResponse *response,
                                 void *clientData);

typedef void BasicHttpOnReceiveProc(BasicHttpRequest *request,
                                    BasicHttpResponse *response,
                                    void *clientData);

struct BasicHttpResponse {
   BasicHttpErrorCode      errorCode;
   BasicHttpResponseCode   responseCode;
   char                    *content;
};

typedef VMwareStatus PollCallbackProc(PollClassSet classSet, int flags,
                                      PollerFunction f, void *clientData,
                                      PollEventType type, PollDevHandle info,
                                      struct DeviceLock *lock);

typedef Bool PollCallbackRemoveProc(PollClassSet classSet, int flags,
                                    PollerFunction f, void *clientData,
                                    PollEventType type);

Bool BasicHttp_Init(PollCallbackProc *pollCallbackProc,
                    PollCallbackRemoveProc *pollCallbackRemoveProc);

Bool BasicHttp_InitEx(PollCallbackProc *pollCallbackProc,
                      PollCallbackRemoveProc *pollCallbackRemoveProc,
                      size_t maxOutstandingRequests);

void BasicHttp_Shutdown(void);

BasicHttpCookieJar *BasicHttp_CreateCookieJar(void);

void BasicHttp_SetInitialCookie(BasicHttpCookieJar *cookieJar, const char *cookie);

void BasicHttp_FreeCookieJar(BasicHttpCookieJar *cookieJar);

BasicHttpRequest *BasicHttp_CreateRequest(const char *url,
                                          BasicHttpMethod httpMethod,
                                          BasicHttpCookieJar *cookieJar,
                                          const char *header,
                                          const char *body);

void BasicHttp_AppendRequestHeader(BasicHttpRequest *request,
                                   const char *header);

void BasicHttp_SetRequestNameAndPassword(BasicHttpRequest *request,
                                         int authenticationType,
                                         const char *userName,
                                         const char *userPassword);

Bool BasicHttp_SendRequest(BasicHttpRequest *request,
                           BasicHttpOnSentProc *onSentProc,
                           void *clientData);

void BasicHttp_CancelRequest(BasicHttpRequest *request);

void BasicHttp_FreeRequest(BasicHttpRequest *request);

void BasicHttp_FreeResponse(BasicHttpResponse *response);


#ifdef __cplusplus
} // extern "C" {
#endif 


#endif // _BASIC_HTTP_H_
