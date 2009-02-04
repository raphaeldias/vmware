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
 * http.c --
 */
// #define BASIC_HTTP_TRACE 1

#include "vmware.h"
#include "vm_version.h"
#include "vm_basic_types.h"
#include "vm_assert.h"
#include "hashTable.h"
#include "poll.h"
#include "util.h"
#include "str.h"
#include "basicHttp.h"
#include "requestQueue.h"
#include "sslPRNG.h"

#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>
#include <curl/multi.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <MSWSock.h>
#else
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif

#define DEFAULT_MAX_OUTSTANDING_REQUESTS ((size_t)-1)
#define BASIC_HTTP_TIMEOUT_DATA ((void *)1)

struct CurlSocketState;

struct BasicHttpCookieJar {
   CURLSH               *curlShare;     // Use CURLSH to maintain all the cookies.
   char                 *initialCookie; // Initial cookie for the jar.
};

struct BasicHttpRequest {
   const char           *url;
   BasicHttpMethod      httpMethod;
   BasicHttpCookieJar   *cookieJar;

   CURL                 *curl;
   struct curl_slist    *headerList;

   const char           *body;
   const char           *readPtr;
   size_t               sizeLeft;
   long                 redirectCount;

   DynBuf               receiveBuf;
   BasicHttpOnSentProc  *onSentProc;
   void                 *clientData;

   int                  authType;
   char                 *userNameAndPassword;

   CURLcode             result;
};

typedef struct CurlGlobalState {
   CURLM                   *curlMulti;
   struct CurlSocketState  *socketList;
   Bool                    useGlib;
   HashTable               *requests;
   Bool                    skipRemove;

   size_t                  maxOutstandingRequests;
   RequestQueue            *pending;
} CurlGlobalState;

typedef struct CurlSocketState {
   struct CurlSocketState  *next;

   curl_socket_t           socket;
   CURL                    *curl;
   int                     action;
} CurlSocketState;

static const char *userAgent = "VMware-client";

static CurlSocketState *BasicHttpFindSocket(curl_socket_t sock);

static CurlSocketState *BasicHttpAddSocket(curl_socket_t sock,
                                           CURL *curl,
                                           int action);

static void BasicHttpRemoveSocket(curl_socket_t sock);

static void BasicHttpSetSocketState(CurlSocketState *socketState,
                                    curl_socket_t sock,
                                    CURL *curl,
                                    int action);

static void BasicHttpPollAdd(CurlSocketState *socketState);

static void BasicHttpPollRemove(CurlSocketState *socketState);

static void BasicHttpSocketPollCallback(void *clientData);

static int BasicHttpSocketCurlCallback(CURL *curl,
                                       curl_socket_t sock,
                                       int action,
                                       void *clientData,
                                       void *socketp);  // private socket

static int BasicHttpTimerCurlCallback(CURLM *multi,
                                      long timeoutMS,
                                      void *clientData);

static Bool BasicHttpStartRequest(BasicHttpRequest *request);

static size_t BasicHttpReadCallback(void *buffer,
                                    size_t size,
                                    size_t nmemb,
                                    void *clientData);

static size_t BasicHttpWriteCallback(void *buffer,
                                     size_t size,
                                     size_t nmemb,
                                     void *clientData);

static CurlGlobalState *curlGlobalState = NULL;
static BasicHttpCookieJar *defaultCookieJar = NULL;

static PollCallbackProc *pollCallbackProc = NULL;
static PollCallbackRemoveProc *pollCallbackRemoveProc = NULL;

/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_Init --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttp_Init(PollCallbackProc *pollCbProc,             // IN
               PollCallbackRemoveProc *pollCbRemoveProc) // IN
{
   return BasicHttp_InitEx(pollCbProc, pollCbRemoveProc,
                           DEFAULT_MAX_OUTSTANDING_REQUESTS);
} // BasicHttp_Init


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_InitEx --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttp_InitEx(PollCallbackProc *pollCbProc,              // IN
                 PollCallbackRemoveProc *pollCbRemoveProc,  // IN
                 size_t maxOutstandingRequests)             // IN
{
   Bool success = TRUE;
   CURLcode code = CURLE_OK;

   ASSERT(pollCbProc);
   ASSERT(pollCbRemoveProc);

   if (NULL != curlGlobalState) {
      NOT_IMPLEMENTED();
   }

#ifdef _WIN32
   SSLPRNG_Install();
   code = curl_global_init(CURL_GLOBAL_WIN32);
#else
   code = curl_global_init(CURL_GLOBAL_ALL);
#endif

   if (CURLE_OK != code) {
      success = FALSE;
      goto abort;
   }

   curlGlobalState = (CurlGlobalState *) Util_SafeCalloc(1, sizeof *curlGlobalState);
   curlGlobalState->curlMulti = curl_multi_init();
   curlGlobalState->useGlib = FALSE;
   if (NULL == curlGlobalState->curlMulti) {
      success = FALSE;
      goto abort;
   }

   curl_multi_setopt(curlGlobalState->curlMulti,
                     CURLMOPT_SOCKETFUNCTION,
                     BasicHttpSocketCurlCallback);
   curl_multi_setopt(curlGlobalState->curlMulti,
                     CURLMOPT_SOCKETDATA,
                     NULL);
   curl_multi_setopt(curlGlobalState->curlMulti,
                     CURLMOPT_TIMERFUNCTION,
                     BasicHttpTimerCurlCallback);
   curl_multi_setopt(curlGlobalState->curlMulti,
                     CURLMOPT_TIMERDATA,
                     NULL);

   curlGlobalState->requests = HashTable_Alloc(16, HASH_INT_KEY, NULL);
   curlGlobalState->skipRemove = FALSE;
   curlGlobalState->maxOutstandingRequests = maxOutstandingRequests;
   curlGlobalState->pending = RequestQueue_New();

   pollCallbackProc = pollCbProc;
   pollCallbackRemoveProc = pollCbRemoveProc;

abort:
   if (!success) {
      free(curlGlobalState);
      curlGlobalState = NULL;
   }

   return success;
} // BasicHttp_InitEx


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpRemoveFreeRequest --
 *
 *      Remove the connection for an outstanding request and then free
 *      the request.
 *
 * Results:
 *      Always 0.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

static int
BasicHttpRemoveFreeRequest(BasicHttpRequest *request, // IN
                           void *value,               // IN: Unused
                           void *clientData)          // IN: Unused
{
   BasicHttp_FreeRequest(request);

   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_Shutdown --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_Shutdown(void)
{
   if (NULL != curlGlobalState) {
      curlGlobalState->skipRemove = TRUE;
      HashTable_ForEach(curlGlobalState->requests,
                        (HashTableForEachCallback)BasicHttpRemoveFreeRequest,
                        NULL);
      HashTable_Free(curlGlobalState->requests);
      RequestQueue_Free(curlGlobalState->pending);
   }

   if (NULL != defaultCookieJar) {
      BasicHttp_FreeCookieJar(defaultCookieJar);
      defaultCookieJar = NULL;
   }

   if (NULL != curlGlobalState) {
      curl_multi_cleanup(curlGlobalState->curlMulti);
      curl_global_cleanup();
      free(curlGlobalState);
      curlGlobalState = NULL;
   }

#ifdef _WIN32
   SSLPRNG_Restore();
#endif
} // BasicHttp_Shutdown


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_CreateCookieJar --
 *
 *
 * Results:
 *       BasicHttpCookieJar.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

BasicHttpCookieJar *
BasicHttp_CreateCookieJar(void)
{
   BasicHttpCookieJar *cookieJar;

   ASSERT(NULL != curlGlobalState);

   cookieJar = (BasicHttpCookieJar *) Util_SafeMalloc(sizeof *cookieJar);
   cookieJar->curlShare = curl_share_init();
   curl_share_setopt(cookieJar->curlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
   cookieJar->initialCookie = NULL;

   return cookieJar;
} // BasicHttp_CreateCookieJar


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_SetInitialCookie --
 *
 *       Set the initial cookie for a cookie jar. This should only be called
 *       after the cookie Jar is created, and really should only be called
 *       before any requests have been made - the results will be confusing
 *       otherwise.
 *
 *       The cookie should be in either the "Set-Cookie:" format returned
 *       by an http server or netscape/mozilla cookie file format.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_SetInitialCookie(BasicHttpCookieJar *cookieJar, // IN
                           const char *cookie)            // IN
{
   ASSERT(NULL == cookieJar->initialCookie);

   cookieJar->initialCookie = Util_SafeStrdup(cookie);
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_FreeCookieJar --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_FreeCookieJar(BasicHttpCookieJar *cookieJar)      // IN
{
   if (NULL == cookieJar) {
      return;
   }

   curl_share_setopt(cookieJar->curlShare, CURLSHOPT_UNSHARE, CURL_LOCK_DATA_COOKIE);
   curl_share_cleanup (cookieJar->curlShare);
   free(cookieJar->initialCookie);
   free(cookieJar);
} // BasicHttp_FreeCookieJar


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpSocketCurlCallback --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

int
BasicHttpSocketCurlCallback(CURL *curl,                  // IN
                            curl_socket_t sock,          // IN
                            int action,                  // IN
                            void *clientData,            // IN
                            void *socketp)               // IN
{
   CurlSocketState *socketState;

   ASSERT(NULL != curlGlobalState);

   if (CURL_POLL_REMOVE == action) {
      BasicHttpRemoveSocket(sock);
   } else if (CURL_POLL_NONE != action) {
      socketState = BasicHttpFindSocket(sock);

      if (NULL == socketState) {
         BasicHttpAddSocket(sock, curl, action);
      } else {
         BasicHttpSetSocketState(socketState, sock, curl, action);
      }
   }

   return 0;
} // BasicHttpSocketCurlCallback


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpTimerCurlCallback --
 *
 *      Callback function that libcurl calls when it wants us to adjust the
 *      timeout callback we're running on the poll loop. Curl uses this
 *      mechanism to implement timeouts on its http connections.
 *
 * Results:
 *      Always 0.
 *
 * Side effects:
 *      Old timer callback is always cleared and a new one might be registered.
 *
 *-----------------------------------------------------------------------------
 */

int
BasicHttpTimerCurlCallback(CURLM *multi,     // IN:
                           long timeoutMS,   // IN:
                           void *clientData) // IN:
{
   pollCallbackRemoveProc(POLL_CS_MAIN,
                          0,
                          BasicHttpSocketPollCallback,
                          BASIC_HTTP_TIMEOUT_DATA,
                          POLL_REALTIME);

   if (timeoutMS >= 0) {
      VMwareStatus pollResult;
      pollResult = pollCallbackProc(POLL_CS_MAIN,
                                    0,
                                    BasicHttpSocketPollCallback,
                                    BASIC_HTTP_TIMEOUT_DATA,
                                    POLL_REALTIME,
                                    timeoutMS * 1000, /* Convert to microsec */
                                    NULL /* deviceLock */);
      if (VMWARE_STATUS_SUCCESS != pollResult) {
         ASSERT(0);
      }
   }
   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpFindSocket --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

CurlSocketState *
BasicHttpFindSocket(curl_socket_t sock)                  // IN
{
   CurlSocketState *socketState = NULL;

   ASSERT(NULL != curlGlobalState);

   socketState = curlGlobalState->socketList;
   while (NULL != socketState) {
      if (sock == socketState->socket)
         break;

      socketState = socketState->next;
   }

   return socketState;
} // BasicHttpFindSocket


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpAddSocket --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

CurlSocketState *
BasicHttpAddSocket(curl_socket_t sock,                   // IN
                   CURL *curl,                           // IN
                   int action)                           // IN
{
   CurlSocketState *socketState = NULL;

   ASSERT(NULL != curlGlobalState);
   ASSERT(NULL == BasicHttpFindSocket(sock));

   socketState = (CurlSocketState *) Util_SafeCalloc(1, sizeof *socketState);
   socketState->socket = sock;
   socketState->curl = curl;
   socketState->action = action;

   BasicHttpPollAdd(socketState);

   socketState->next = curlGlobalState->socketList;
   curlGlobalState->socketList = socketState;

   return socketState;
} // BasicHttpAddSocket


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpRemoveSocket --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpRemoveSocket(curl_socket_t sock)                   // IN
{
   CurlSocketState **socketState;
   CurlSocketState *socketStateToRemove = NULL;

   ASSERT(NULL != curlGlobalState);

   socketState = &(curlGlobalState->socketList);
   while (NULL != *socketState) {
      if (sock != (*socketState)->socket) {
         socketState = &((*socketState)->next);
         continue;
      }

      socketStateToRemove = *socketState;
      *socketState = (*socketState)->next;

      BasicHttpPollRemove(socketStateToRemove);
      free(socketStateToRemove);
   }
} // BasicHttpRemoveSocket


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpSetSocketState --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpSetSocketState(CurlSocketState *socketState,    // IN
                        curl_socket_t sock,              // IN
                        CURL *curl,                      // IN
                        int action)                      // IN
{
   ASSERT(NULL != socketState);

   if ((socketState->socket != sock)
         || (socketState->curl != curl)
         || (socketState->action != action)) {
      BasicHttpPollRemove(socketState);
      socketState->socket = sock;
      socketState->curl = curl;
      socketState->action = action;
      BasicHttpPollAdd(socketState);
   }
} // BasicHttpSetSocketState


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpPollAdd --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpPollAdd(CurlSocketState *socketState)           // IN
{
   VMwareStatus pollResult;

   ASSERT(NULL != socketState);

   if (CURL_POLL_IN & socketState->action) {
      pollResult = pollCallbackProc(POLL_CS_MAIN,
                                    POLL_FLAG_READ |
                                    POLL_FLAG_PERIODIC |
                                    POLL_FLAG_SOCKET,
                                    BasicHttpSocketPollCallback,
                                    socketState,
                                    POLL_DEVICE,
                                    socketState->socket,
                                    NULL /* deviceLock */);
      if (VMWARE_STATUS_SUCCESS != pollResult) {
         ASSERT(0);
      }
   }
   if (CURL_POLL_OUT & socketState->action) {
      pollResult = pollCallbackProc(POLL_CS_MAIN,
                                    POLL_FLAG_WRITE |
                                    POLL_FLAG_PERIODIC |
                                    POLL_FLAG_SOCKET,
                                    BasicHttpSocketPollCallback,
                                    socketState,
                                    POLL_DEVICE,
                                    socketState->socket,
                                    NULL /* deviceLock */);
      if (VMWARE_STATUS_SUCCESS != pollResult) {
         ASSERT(0);
      }
   }
} // BasicHttpPollAdd


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpPollRemove --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpPollRemove(CurlSocketState *socketState)        // IN
{
   ASSERT(NULL != socketState);

   if (CURL_POLL_IN & socketState->action) {
      pollCallbackRemoveProc(POLL_CS_MAIN,
                             POLL_FLAG_READ |
                             POLL_FLAG_PERIODIC |
                             POLL_FLAG_SOCKET,
                             BasicHttpSocketPollCallback,
                             socketState,
                             POLL_DEVICE);
   }
   if (CURL_POLL_OUT & socketState->action) {
      pollCallbackRemoveProc(POLL_CS_MAIN,
                             POLL_FLAG_WRITE |
                             POLL_FLAG_PERIODIC |
                             POLL_FLAG_SOCKET,
                             BasicHttpSocketPollCallback,
                             socketState,
                             POLL_DEVICE);
   }
} // BasicHttpPollRemove


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpCompleteRequestCallback --
 *
 *
 * Results:
 *       Always FALSE.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

static void
BasicHttpCompleteRequestCallback(void *clientData)          // IN
{
   BasicHttpRequest *request;
   BasicHttpResponse *response;
   BasicHttpErrorCode errorCode;
   size_t contentLength;

   ASSERT(NULL != clientData);

   request = (BasicHttpRequest *) clientData;
   response = (BasicHttpResponse *) Util_SafeCalloc(1, sizeof *response);
   curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &response->responseCode);

   /* Map error codes. */
   switch (request->result) {
   /* 1:1 mappings. */
   case CURLE_OK:
      errorCode = BASICHTTP_ERROR_NONE;
      break;
   case CURLE_UNSUPPORTED_PROTOCOL:
      errorCode = BASICHTTP_ERROR_UNSUPPORTED_PROTOCOL;
      break;
   case CURLE_URL_MALFORMAT:
      errorCode = BASICHTTP_ERROR_URL_MALFORMAT;
      break;
   case CURLE_COULDNT_RESOLVE_PROXY:
      errorCode = BASICHTTP_ERROR_COULDNT_RESOLVE_PROXY;
      break;
   case CURLE_COULDNT_RESOLVE_HOST:
      errorCode = BASICHTTP_ERROR_COULDNT_RESOLVE_HOST;
      break;
   case CURLE_COULDNT_CONNECT:
      errorCode = BASICHTTP_ERROR_COULDNT_CONNECT;
      break;
   case CURLE_HTTP_RETURNED_ERROR:
      errorCode = BASICHTTP_ERROR_HTTP_RETURNED_ERROR;
      break;
   case CURLE_OPERATION_TIMEDOUT:
      errorCode = BASICHTTP_ERROR_OPERATION_TIMEDOUT;
      break;
   case CURLE_SSL_CONNECT_ERROR:
      errorCode = BASICHTTP_ERROR_SSL_CONNECT_ERROR;
      break;
   case CURLE_TOO_MANY_REDIRECTS:
      errorCode = BASICHTTP_ERROR_TOO_MANY_REDIRECTS;
      break;
   /* n:1 mappings */
   case CURLE_WRITE_ERROR:
   case CURLE_READ_ERROR:
   case CURLE_SEND_ERROR:
   case CURLE_RECV_ERROR:
      errorCode = BASICHTTP_ERROR_TRANSFER;
      break;
   case CURLE_SSL_ENGINE_NOTFOUND:
   case CURLE_SSL_ENGINE_SETFAILED:
   case CURLE_SSL_CERTPROBLEM:
   case CURLE_SSL_CIPHER:
   case CURLE_SSL_CACERT:
   case CURLE_SSL_ENGINE_INITFAILED:
   case CURLE_SSL_CACERT_BADFILE:
   case CURLE_SSL_SHUTDOWN_FAILED:
      errorCode = BASICHTTP_ERROR_SSL_SECURITY;
      break;
   default:
      errorCode = BASICHTTP_ERROR_GENERIC;
      break;
   }
   response->errorCode = errorCode;

   contentLength = DynBuf_GetSize(&request->receiveBuf);
   response->content = (char *) Util_SafeMalloc(contentLength + 1);
   memcpy(response->content,
          DynBuf_Get(&request->receiveBuf),
          contentLength);
   response->content[contentLength] = '\0';

#ifdef BASIC_HTTP_TRACE
   fprintf(stderr, "RECEIVED RECEIVED RECEIVED RECEIVED RECEIVED RECEIVED\n");
   fprintf(stderr, "%s\n\n\n", response->content); 
#endif

   (request->onSentProc)(request, response, request->clientData);

   /*
    * Don't use request after this point. Let's assume request has
    * been deleted by the callback.
    */
} // BasicHttpCompleteRequestCallback


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpProcessCURLMulti --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       Completition notifications are queued up to run asynchronously.
 *
 *-----------------------------------------------------------------------------
 */

static void
BasicHttpProcessCURLMulti(void)
{
   while (TRUE) {
      CURLMsg *msg;
      int msgsLeft;

      msg = curl_multi_info_read(curlGlobalState->curlMulti, &msgsLeft);
      if (NULL == msg) {
         break;
      }

      if (CURLMSG_DONE == msg->msg) {
         CURL *curl;
         CURLcode curlCode;
         BasicHttpRequest *request = NULL;

         /*
          * Save state as msg is unavailable after _multi_remove_handle.
          */
         curl = msg->easy_handle;
         curlCode = msg->data.result;
         curl_multi_remove_handle(curlGlobalState->curlMulti, curl);

         curl_easy_getinfo(curl, CURLINFO_PRIVATE, &request);
         if (NULL != request) {
            ASSERT(curl == request->curl);

            if (NULL != request->cookieJar) {
               curl_easy_setopt(request->curl, CURLOPT_SHARE, NULL);
            }

            /* Store easy error code to handle later. */
            request->result = curlCode;

            /*
            * We are done. Invoke the callback function.
            */
            if (NULL != request->onSentProc) {
               VMwareStatus pollResult;
               pollResult = pollCallbackProc(POLL_CS_MAIN,
                                             0,
                                             BasicHttpCompleteRequestCallback,
                                             request,
                                             POLL_REALTIME,
                                             0, /* Convert to microsec */
                                             NULL /* deviceLock */);
               if (VMWARE_STATUS_SUCCESS != pollResult) {
                  ASSERT(0);
               }
            }
         }
      }
   }
} // BasicHttpProcessCURLMulti


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpSocketPollCallback --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpSocketPollCallback(void *clientData)         // IN
{
   CurlSocketState *socketState;
   CURLMcode curlMErr;
   curl_socket_t socket = 0;
   Bool isTimeout;

   isTimeout = (clientData == BASIC_HTTP_TIMEOUT_DATA);
   if (isTimeout) {
      clientData = NULL;
   }

   socketState = (CurlSocketState *) clientData;
   if (socketState) {
      socket = socketState->socket;
   }

   ASSERT(NULL != curlGlobalState);
   while (TRUE) {
      int runningHandles;

      if (isTimeout) {
         curlMErr = curl_multi_socket(curlGlobalState->curlMulti,
                                      CURL_SOCKET_TIMEOUT,
                                      &runningHandles);
      } else if (socketState) {
         curlMErr = curl_multi_socket(curlGlobalState->curlMulti,
                                      socket,
                                      &runningHandles);
      } else {
         /*
          * Before calling curl_multi_socket_all, we need to process all
          * the pending curl multi results. Otherwise, one curl connection
          * could be assigned to more than one curl easy handles.
          *
          * There's a bug(?) in cUrl implementation up to 7.16.0 in that
          * the connection is returned to pool as soon as the request
          * becomes COMPLETED. However, it's not removed from easy multi
          * handle until curl_multi_remove_handle is called. If curl_multi
          * _socket_all is called when this happens, the same connection
          * could be	assigned to 2 curl easy handles which would cause mess
          * later on.
          */
         BasicHttpProcessCURLMulti();
         curlMErr = curl_multi_socket_all(curlGlobalState->curlMulti,
                                          &runningHandles);
      }

      if (CURLM_CALL_MULTI_PERFORM != curlMErr) {
         /* 
          * A CURL internal bug cause returning CURLM_BAD_SOCKET before 
          * a curl handle is able to transit to the final complete state.
            
          * It is timing related and the chance is exactly 1%. When this
          * happens, we need to redrive the curl handle using the 
          * curl_multi_socket_all API. Hence we set socketState to NULL

          * Note redrive using curl_multi_socket will not work as it could
          * not find the removed socket in hash and returns CURLM_BAD_SOCKET
          * before get a chance to finish the final state transition.
          */
         if (CURLM_BAD_SOCKET == curlMErr) {
            socketState = NULL;
            continue;
         }

         ASSERT( CURLM_OK == curlMErr );

         break;
      }
   }

   BasicHttpProcessCURLMulti();

   while (curlGlobalState->pending->size > 0 &&
          HashTable_GetNumElements(curlGlobalState->requests) <
                                   curlGlobalState->maxOutstandingRequests) {
      BasicHttpRequest *request = RequestQueue_PopHead(curlGlobalState->pending);
      BasicHttpStartRequest(request);
   }
} // BasicHttpSocketPollCallback


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_CreateRequest --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

BasicHttpRequest *
BasicHttp_CreateRequest(const char *url,                 // IN
                        BasicHttpMethod httpMethod,      // IN
                        BasicHttpCookieJar *cookieJar,   // IN
                        const char *header,              // IN
                        const char *body)                // IN
{
   BasicHttpRequest *request = NULL;

   if ((NULL == url)
      || (httpMethod < BASICHTTP_METHOD_GET)
      || (httpMethod > BASICHTTP_METHOD_HEAD)) {
      goto abort;
   }

   if (BASICHTTP_DEFAULT_COOKIEJAR == cookieJar) {
      if (NULL == defaultCookieJar) {
         defaultCookieJar = BasicHttp_CreateCookieJar();
      }
      cookieJar = defaultCookieJar;
   }

   request = (BasicHttpRequest *) Util_SafeCalloc(1, sizeof *request);
   request->url = Util_SafeStrdup(url);
   request->httpMethod = httpMethod;
   request->cookieJar = cookieJar;
   BasicHttp_AppendRequestHeader(request, header);
   request->body = Util_SafeStrdup(body);
   request->readPtr = request->body;
   request->sizeLeft = strlen(request->body);
   request->redirectCount = 0;
   DynBuf_Init(&request->receiveBuf);
   request->authType = BASICHTTP_AUTHENTICATION_NONE;
   request->userNameAndPassword = NULL;

abort:
   return request;
} // BasicHttp_CreateRequest


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_AppendRequestHeader --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_AppendRequestHeader(BasicHttpRequest *request,    // IN
                              const char *header)           // IN
{
   if (NULL != header) {
      request->headerList = curl_slist_append(request->headerList, header);
   }
} // BasicHttp_AppendRequestHeader


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_SetRequestNameAndPassword --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_SetRequestNameAndPassword(BasicHttpRequest *request,     // IN
                                    int authenticationType,        // IN
                                    const char *userName,          // IN
                                    const char *userPassword)      // IN
{
   if ((NULL == request)
         || (authenticationType < BASICHTTP_AUTHENTICATION_NONE)) {
      ASSERT(0);
      return;
   }

   request->authType = authenticationType;

   free(request->userNameAndPassword);
   request->userNameAndPassword = NULL;
   if ((NULL != userName) && (NULL != userPassword)) {
      size_t strLength = strlen(userName) + strlen(userPassword) + 2;
      request->userNameAndPassword = (char *) Util_SafeCalloc(1, strLength);
      snprintf(request->userNameAndPassword, 
                  strLength, 
                  "%s:%s", 
                  userName, 
                  userPassword);                                                  
   }
} // BasicHttp_SetRequestNameAndPassword


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpStartRequest --
 *
 *
 * Results:
 *       A Boolean indicating whether the request has been successfully started
 *       or not.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

static Bool
BasicHttpStartRequest(BasicHttpRequest *request)
{
   Bool success = TRUE;
   CURLMcode curlMErr;

   request->curl = curl_easy_init();
   if (NULL == request->curl) {
      success = FALSE;
      goto abort;
   }

   ASSERT(NULL != request->url);
   curl_easy_setopt(request->curl, CURLOPT_URL, request->url);
   curl_easy_setopt(request->curl, CURLOPT_USERAGENT, userAgent);
   curl_easy_setopt(request->curl, CURLOPT_SSL_VERIFYPEER, (long) 0);
   curl_easy_setopt(request->curl, CURLOPT_SSL_VERIFYHOST, (long) 0);
   curl_easy_setopt(request->curl, CURLOPT_COOKIEFILE, ""); // Activate cookie engine.
   curl_easy_setopt(request->curl, CURLOPT_FOLLOWLOCATION, (long) 1);
   curl_easy_setopt(request->curl, CURLOPT_POST301, (long) 1);
   curl_easy_setopt(request->curl, CURLOPT_NOSIGNAL, (long) 1);
#ifdef _WIN32
   curl_easy_setopt(request->curl, CURLOPT_CONNECTTIMEOUT, (long) 60);
   /*
    * Set a dummy random file, this is pretty much a no-op in libcurl
    * however, it triggers the libcurl to check if the random seed has enough
    * entrophy and skips a lengthy rand_screen() if that is the case.
    */
   curl_easy_setopt(request->curl, CURLOPT_RANDOM_FILE, "");
#else
   curl_easy_setopt(request->curl, CURLOPT_CONNECTTIMEOUT, (long) 5);
#endif

   if ((BASICHTTP_AUTHENTICATION_NONE != request->authType) 
         && (NULL != request->userNameAndPassword)) {
      curl_easy_setopt(request->curl, CURLOPT_USERPWD, request->userNameAndPassword);
      switch(request->authType) {
      case BASICHTTP_AUTHENTICATION_BASIC:
         curl_easy_setopt(request->curl, CURLOPT_HTTPAUTH, (long) CURLAUTH_BASIC); 
         break;
      case BASICHTTP_AUTHENTICATION_DIGEST:
         curl_easy_setopt(request->curl, CURLOPT_HTTPAUTH, (long) CURLAUTH_DIGEST); 
         break;
      case BASICHTTP_AUTHENTICATION_NTLM:
         curl_easy_setopt(request->curl, CURLOPT_PROXYAUTH, (long) CURLAUTH_NTLM); 
         break;
      case BASICHTTP_AUTHENTICATION_ANY:
      default:
         curl_easy_setopt(request->curl, CURLOPT_PROXYAUTH, (long) CURLAUTH_ANY); 
         break;
      }
   } // Set the username/password.

   if (NULL != request->cookieJar) {
      curl_easy_setopt(request->curl, CURLOPT_SHARE, request->cookieJar->curlShare);
      /*
       * Curl can be so insane sometimes. You can share a cookie jar but you can't put
       * anything into it until you have an actual easy handle. So we have to store the
       * initial cookie until the first handle comes along, and then set it then.
       */
      if (NULL != request->cookieJar->initialCookie) {
         curl_easy_setopt(request->curl, CURLOPT_COOKIELIST, request->cookieJar->initialCookie);
         free(request->cookieJar->initialCookie);
         request->cookieJar->initialCookie = NULL;
      }
   }

#ifdef BASIC_HTTP_TRACE
   curl_easy_setopt(request->curl, CURLOPT_VERBOSE, (long) 1);
#endif

   switch (request->httpMethod) {
      case BASICHTTP_METHOD_GET:
         curl_easy_setopt(request->curl, CURLOPT_HTTPGET, (long) 1);
         break;

      case BASICHTTP_METHOD_POST:
         curl_easy_setopt(request->curl, CURLOPT_POST, (long) 1);
         curl_easy_setopt(request->curl, CURLOPT_POSTFIELDSIZE, (long) request->sizeLeft);
         break;

      case BASICHTTP_METHOD_HEAD:
      default:
         // TODO: add later
         success = FALSE;
         goto abort;
   }

   if (NULL != request->headerList) {
      curl_easy_setopt(request->curl, CURLOPT_HTTPHEADER, request->headerList);
   }

   curl_easy_setopt(request->curl, CURLOPT_READFUNCTION, BasicHttpReadCallback);
   curl_easy_setopt(request->curl, CURLOPT_READDATA, request);

   curl_easy_setopt(request->curl, CURLOPT_WRITEFUNCTION, BasicHttpWriteCallback);
   curl_easy_setopt(request->curl, CURLOPT_WRITEDATA, request);

   curl_easy_setopt(request->curl, CURLOPT_PRIVATE, request);

   HashTable_Insert(curlGlobalState->requests, (void *)request, NULL);
   curlMErr = curl_multi_add_handle(curlGlobalState->curlMulti, request->curl);
   if (CURLM_OK != curlMErr) {
      success = FALSE;
      goto abort;
   }


#ifdef BASIC_HTTP_TRACE
   fprintf(stderr, "SENDING SENDING SENDING SENDING SENDING SENDING\n");
   if (request->url)
      fprintf(stderr, "%s\n", request->url);
   if (request->body)
      fprintf(stderr, "%s\n\n\n", request->body);
#endif

   BasicHttpSocketPollCallback(NULL);

 abort:
   return success;
} // BasicHttpStartRequest


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_SendRequest --
 *
 *       The callback function onSentProc will be responsible for
 *       deleteing request and response.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttp_SendRequest(BasicHttpRequest *request,         // IN
                      BasicHttpOnSentProc *onSentProc,   // IN
                      void *clientData)                  // IN
{
   Bool success = TRUE;

   if ((NULL == request) || (NULL == onSentProc)) {
      success = FALSE;
      goto abort;
   }

   ASSERT(NULL != curlGlobalState);
   ASSERT(NULL == request->curl);

   request->onSentProc = onSentProc;
   request->clientData = clientData;

   if (HashTable_GetNumElements(curlGlobalState->requests) >=
       curlGlobalState->maxOutstandingRequests) {
      // Queue up request.
      RequestQueue_PushTail(curlGlobalState->pending, request);
   } else {
      success = BasicHttpStartRequest(request);
   }

abort:
   return success;
} // BasicHttp_SendRequest


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_CancelRequest --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_CancelRequest(BasicHttpRequest *request)       // IN
{
   if (NULL == request) {
      return;
   }

   ASSERT(NULL != curlGlobalState);

   if (NULL != request->curl) {
      curl_multi_remove_handle(curlGlobalState->curlMulti, request->curl);
   }
} // BasicHttp_CancelRequest


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpReadCallback --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

size_t
BasicHttpReadCallback(void *buffer,                      // IN, OUT
                      size_t size,                       // IN
                      size_t nmemb,                      // IN
                      void *clientData)                  // IN
{
   BasicHttpRequest *request;
   long redirectCount;
   size_t bufferSize;
   size_t readSize = 0;

   request = (BasicHttpRequest *) clientData;
   ASSERT(NULL != request);

   bufferSize = size * nmemb;
   if (bufferSize < 1) {
      readSize = 0;
      goto abort;
   }

   ASSERT(NULL != request->curl);
   if (curl_easy_getinfo(request->curl, CURLINFO_REDIRECT_COUNT,
                         &redirectCount, NULL) == CURLE_OK) {
      if (redirectCount > request->redirectCount) {
         /*
          * We've been redirected since the last read. Reset
          * the read state.
          */
         request->readPtr = request->body;
         request->sizeLeft = strlen(request->body);
         request->redirectCount = redirectCount;
      }
   }

   if (request->sizeLeft > 0) {
      if (request->sizeLeft < bufferSize) {
         bufferSize = request->sizeLeft;
      }
      memcpy(buffer, request->readPtr, bufferSize);
      request->readPtr += bufferSize;
      request->sizeLeft -= bufferSize;

      readSize = bufferSize;
      goto abort;
   }
   else { // reset since curl may need to retry if the connection is broken.
      request->readPtr = request->body;
      request->sizeLeft = strlen(request->body);
   }

abort:
   return readSize;
} // BasicHttpReadCallback


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpWriteCallback --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

size_t
BasicHttpWriteCallback(void *buffer,                     // IN
                       size_t size,                      // IN
                       size_t nmemb,                     // IN
                       void *clientData)                 // IN
{
   BasicHttpRequest *request;
   size_t bufferSize;

   request = (BasicHttpRequest *) clientData;
   ASSERT(NULL != request);

   bufferSize = size * nmemb;
   if (bufferSize > 0) {
      if (!DynBuf_Append(&request->receiveBuf, buffer, bufferSize)) {
         /*
          * If Append() fails, then this function should fail as well.
          * By returning a value less than (size * nmemb), curl will abort
          * the transfer and return an error (CURLE_WRITE_ERROR).
          */
         return 0;
      }
   }

   return bufferSize;
} // BasicHttpWriteCallback


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_FreeRequest --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_FreeRequest(BasicHttpRequest *request)            // IN
{
   if (NULL == request) {
      return;
   }

   BasicHttp_CancelRequest(request);

   free((void *) request->url);
   curl_slist_free_all(request->headerList);
   free((void *) request->body);
   DynBuf_Destroy(&request->receiveBuf);
   free(request->userNameAndPassword);
   if (NULL != request->curl) {
      curl_easy_cleanup(request->curl);
   }
   if (!curlGlobalState->skipRemove) {
      HashTable_Delete(curlGlobalState->requests, (void *)request);
   }
   free(request);
} // BasicHttp_FreeRequest


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_FreeResponse --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_FreeResponse(BasicHttpResponse *response)         // IN
{
   if (NULL == response) {
      return;
   }

   free(response->content);
   free(response);
} // BasicHttp_FreeResponse
