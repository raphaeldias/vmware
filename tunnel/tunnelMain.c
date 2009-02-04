/*********************************************************
 * Copyright (C) 2008 VMware, Inc. All rights reserved.
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
 * tunnelMain.c --
 *
 *      Multi-channel socket proxy over HTTP with control messages, lossless
 *      reconnect, heartbeats, etc.
 */


#include <arpa/inet.h>  /* For inet_ntoa */
#include <sys/types.h>  /* For getsockname */
#include <sys/socket.h> /* For getsockname */
#include <netdb.h>      /* For getnameinfo */
#include <netinet/in.h> /* For getsockname */


#include "tunnelProxy.h"

#include "dynbuf.h"
#include "log.h"
#include "msg.h"
#include "preference.h"
#include "ssl.h"
#include "str.h"
#include "strutil.h"
#include "util.h"


#define APPNAME "vmware-view-tunnel"
#define TMPBUFSIZE 1024 * 16 /* arbitrary */
#define BLOCKING_TIMEOUT_MS 1000 * 3 /* 3 seconds, arbitrary */


static char *gServerArg = NULL;
static char *gConnectionIdArg = NULL;

static TunnelProxy *gTunnelProxy = NULL;
static AsyncSocket *gAsock = NULL;
static Bool gRecvHeaderDone = FALSE;
static char gRecvByte = 0;
static DynBuf gRecvBuf;


static void TunnelConnect(void);
static void TunnelSocketConnectCb(AsyncSocket *asock, void *userData);


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelParseUrl --
 *
 *      Split a URL [<protocol>://]<host>[:<port>][<path>] into components.
 *
 *      XXX: Copied from bora/lib/http/httpUtil.c:Http_ParseUrl.  Should be
 *      moved someplace central and removed from here.
 *
 * Results:
 *      TRUE on success: 'proto', 'host', 'port' and 'path' are set if they are
 *                       not NULL. 'path' is guaranteed to start with a '/'.
 *      FALSE on failure: invalid URL
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelParseUrl(char const *url,      // IN
               char **proto,         // OUT
               char **host,          // OUT
               unsigned short *port, // OUT
               char **path,          // OUT
               Bool *secure)         // OUT
{
   char const *endProto;
   char *myProto;
   char *myHost;
   char *myPath;
   unsigned int index;
   unsigned int myPort;

   ASSERT(url);

   endProto = strstr(url, "://");
   if (endProto) {
      /* Explicit protocol */
      myProto = (char*) Util_SafeMalloc(endProto - url + 1 /* NUL */);
      memcpy(myProto, url, endProto - url);
      myProto[endProto - url] = '\0';
      index = endProto - url + 3;
   } else {
      /* Implicit protocol */
      myProto = Util_SafeStrdup("http");
      index = 0;
   }

   myHost = NULL;
   myPath = NULL;
   myHost = StrUtil_GetNextToken(&index, url, ":/");
   if (myHost == NULL) {
      goto error;
   }

   if (url[index] == ':') {
      /* Explicit port */
      index += 1;
      if (!StrUtil_GetNextUintToken(&myPort, &index, url, "/")) {
         goto error;
      }
      if (myPort > 0xffff) {
         goto error;
      }
   } else {
      /* Implicit port */
      if (Str_Strcmp(myProto, "http") == 0) {
         myPort = 80;
      } else if (Str_Strcmp(myProto, "https") == 0) {
         myPort = 443;
      } else {
         /* Not implemented */
         goto error;
      }
   }

   if (url[index] == '/') {
      /* Explicit path */
      myPath = strdup(&url[index]);
   } else {
      /* Implicit path */
      ASSERT(url[index] == '\0');
      myPath = strdup("/");
   }
   ASSERT_MEM_ALLOC(myPath);

   if (secure) {
      *secure = Str_Strcmp(myProto, "https") == 0;
   }
   if (proto) {
      *proto = myProto;
   } else {
      free(myProto);
   }
   if (host) {
      *host = myHost;
   } else {
      free(myHost);
   }
   if (port) {
      *port = myPort;
   }
   if (path) {
      *path = myPath;
   } else {
      free(myPath);
   }
   return TRUE;

error:
   free(myProto);
   free(myHost);
   free(myPath);
   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelDisconnectCb --
 *
 *      TunnelProxy disconnected callback.  If there is a reconnect secret,
 *      calls TunnelConnect attempt reconnect, otherwise exits with error.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      May call TunnelConnect.
 *
 *-----------------------------------------------------------------------------
 */

void
TunnelDisconnectCb(TunnelProxy *tp,             // IN
                   const char *reconnectSecret, // IN
                   const char *reason,          // IN
                   void *userData)              // IN: not used
{
   AsyncSocket_Close(gAsock);
   gAsock = NULL;
   gRecvHeaderDone = FALSE;
   if (reconnectSecret) {
      Warning("TUNNEL RESET: %s\n", reason ? reason : "Unknown reason");
      TunnelConnect();
   } else if (reason) {
      Warning("TUNNEL DISCONNECT: %s\n", reason);
      exit(1);
   } else {
      Warning("TUNNEL EXIT\n");
      exit(0);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketRead --
 *
 *      Utility to read all pending data from an async socket non-blocking,
 *      first prepending buf, and append it all to a DynBuf.
 *
 * Results:
 *      Byte count of buf + newly read bytes that were appended to recvBuf, 
 *      or -1 if read failed.
 *
 * Side effects:
 *      Calls AsyncSocket_RecvBlocking with 0 timeout, TunnelDisconnectCb on 
 *      read error.
 *
 *-----------------------------------------------------------------------------
 */

int
TunnelSocketRead(void *buf,          // IN: prepend buffer
                 int len,            // IN: buf length
                 AsyncSocket *asock, // IN: source asock
                 DynBuf *recvBuf)    // IN: dest buf
{
   int asockErr = ASOCKERR_SUCCESS;
   int totalRecvLen = 0;

   if (buf) {
      DynBuf_Append(recvBuf, buf, len);
      totalRecvLen += len;
   }

   /* Append all available data non-blocking (specify 0 timeout) */
   while (asockErr == ASOCKERR_SUCCESS) {
      char tmpBuf[TMPBUFSIZE];
      int recvLen = 0;

      asockErr = AsyncSocket_RecvBlocking(gAsock, tmpBuf, sizeof(tmpBuf),
                                          &recvLen, 0);

      if (asockErr != ASOCKERR_SUCCESS && asockErr != ASOCKERR_TIMEOUT) {
         char *msg;
         char *reason;
         size_t reasonLen = 0;

         msg = Msg_GetString(MSGID("cdk.linuxTunnel.errorReading")
                             "Error reading from tunnel HTTP socket: %s\n");
         reason = Str_Asprintf(&reasonLen, msg,
                               AsyncSocket_Err2String(asockErr));

         TunnelDisconnectCb(gTunnelProxy, NULL, reason, NULL);

         free(reason);
         free(msg);
         return -1;
      }

      DynBuf_Append(recvBuf, tmpBuf, recvLen);
      totalRecvLen += recvLen;
   }

   return totalRecvLen;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketParseHeader --
 *
 *      Simple HTTP header parsing.  Just looks in the DynBuf for the "\r\n\r\n"
 *      that terminates an HTTP header from the body. If found, the header is
 *      removed from the front of the DynBuf.
 *
 *      XXX: Parse response header, instead of assuming server/proxy will kill
 *      connection on failure.
 *
 * Results:
 *      True if headers have been received, false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Bool
TunnelSocketParseHeader(DynBuf *recvBuf) // IN
{
   char *dataStart;
   int dataSize;

   if (DynBuf_GetSize(recvBuf) == 0) {
      return FALSE;
   }

   /* Look for end of header */
   dataStart = Str_Strnstr((char*) DynBuf_Get(recvBuf), "\r\n\r\n",
                           DynBuf_GetSize(recvBuf));
   if (!dataStart) {
      return FALSE;
   }

   /* Remove header from beginning of recvBuf */
   dataStart += 4;
   dataSize = DynBuf_GetSize(recvBuf);
   dataSize -= dataStart - ((char*) DynBuf_Get(recvBuf));
   memmove(DynBuf_Get(recvBuf), dataStart, dataSize);
   DynBuf_SetSize(recvBuf, dataSize);

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketRecvCb --
 *
 *      AsyncSocket data received callback.  Reads available data from the
 *      tunnel socket, and pushes into the TunnelProxy using
 *      TunnelProxy_HTTPRecv.  Ignores response headers.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
TunnelSocketRecvCb(void *buf,          // IN: gRecvByte
                   int len,            // IN: always 1
                   AsyncSocket *asock, // IN
                   void *userData)     // IN: not used
{
   if (TunnelSocketRead(buf, len, asock, &gRecvBuf) < 0) {
      NOT_REACHED();
   }

   if (!gRecvHeaderDone) {
      gRecvHeaderDone = TunnelSocketParseHeader(&gRecvBuf);
   }

   if (gRecvHeaderDone && DynBuf_GetSize(&gRecvBuf) > 0) {
      TunnelProxy_HTTPRecv(gTunnelProxy, (char*) DynBuf_Get(&gRecvBuf),
                           DynBuf_GetSize(&gRecvBuf), TRUE);

      /* Reset recvBuf for next read */
      DynBuf_SetSize(&gRecvBuf, 0);
   }

   /* Recv at least 1-byte before calling this callback again */
   AsyncSocket_Recv(gAsock, &gRecvByte, 1, TunnelSocketRecvCb, NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketProxyRecvCb --
 *
 *      AsyncSocket data received callback, used during initial proxy server
 *      CONNECT setup.  Reads available data from the tunnel socket, and looks
 *      for the end of the HTTP header.  If found, starts tunnel endpoint POST
 *      request, otherwise, calls AsyncSocket_Recv to reinvoke this callback.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
TunnelSocketProxyRecvCb(void *buf,          // IN: gRecvByte
                        int len,            // IN: always 1
                        AsyncSocket *asock, // IN
                        void *userData)     // IN: not used
{
   if (TunnelSocketRead(buf, len, asock, &gRecvBuf) < 0) {
      NOT_REACHED();
   }

   if (TunnelSocketParseHeader(&gRecvBuf)) {
      /* Proxy portion of connect is done.  Connect using normal path. */
      DynBuf_Destroy(&gRecvBuf);
      TunnelSocketConnectCb(gAsock, NULL);
   } else {
      /* Recv at least 1-byte before calling this callback again */
      AsyncSocket_Recv(gAsock, &gRecvByte, 1, TunnelSocketProxyRecvCb, NULL);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSendNeededCb --
 *
 *      TunnelProxy send needed callback.  Fetches the available HTTP chunk data
 *      and performs a blocking sends over the AsyncSocket.
 *
 *      XXX: Use async send, and heap-allocated send buffer?
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
TunnelSendNeededCb(TunnelProxy *tp, // IN
                   void *userData)  // IN: not used
{
   while (TRUE) {
      char *sendBuf = Util_SafeMalloc(TMPBUFSIZE);
      int sendSize = TMPBUFSIZE;

      TunnelProxy_HTTPSend(gTunnelProxy, sendBuf, &sendSize, TRUE);
      if (sendSize == 0) {
         free(sendBuf);
         break;
      }

      AsyncSocket_Send(gAsock, sendBuf, sendSize, (AsyncSocketSendFn) free,
                       NULL);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketErrorCb --
 *
 *      AsyncSocket error callback.  Calls TunnelDisconnectCb with the
 *      AsyncSocket error string as the disconnect reason.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
TunnelSocketErrorCb(int error,          // IN
                    AsyncSocket *asock, // IN
                    void *userData)     // IN: not used
{
   TunnelDisconnectCb(gTunnelProxy, NULL, AsyncSocket_Err2String(error), NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketConnectCb --
 *
 *      AsyncSocket connection callback.  Converts the socket to SSL, if the URL
 *      passed on the command line is HTTPS, posts a simple HTTP1.1 request
 *      header, sets up socket read IO handler, and tells the TunnelProxy it is
 *      now connected.
 *
 *      XXX: Support connecting through a proxy.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelSocketConnectCb(AsyncSocket *asock, // IN
                      void *userData)     // IN: not used
{
   TunnelProxyErr err = TP_ERR_OK;
   char *request;
   size_t requestSize = 0;
   int sendSize = 0;
   int asockErr = ASOCKERR_SUCCESS;
   char *serverUrl;
   char *host = NULL;
   unsigned short port = 0;
   char *path = NULL;
   Bool secure = FALSE;
   const char *hostIp = NULL;
   char hostName[1024];

   serverUrl = TunnelProxy_GetConnectUrl(gTunnelProxy, gServerArg);
   if (!TunnelParseUrl(serverUrl, NULL, &host, &port, &path, &secure)) {
      NOT_IMPLEMENTED();
   }

   /* Establish SSL, but don't enforce the cert */
   if (secure && !AsyncSocket_ConnectSSL(asock, NULL)) {
      NOT_IMPLEMENTED();
   }

   request = Str_Asprintf(&requestSize,
      "POST %s HTTP/1.1\r\n"
      "Host: %s:%d\r\n"
      "Accept: text/*, application/octet-stream\r\n"
      "User-agent: Mozilla/4.0 (compatible; MSIE 6.0)\r\n"
      "Pragma: no-cache\r\n"
      "Connection: Keep-Alive\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Content-Type: application/octet-stream\r\n"
      "Cache-Control: no-cache, no-store, must-revalidate\r\n"
      "\r\n", path, host, port);

   /* Send initial request header */
   asockErr = AsyncSocket_SendBlocking(gAsock, request, requestSize,
                                       &sendSize, BLOCKING_TIMEOUT_MS);
   if (asockErr != ASOCKERR_SUCCESS) {
      Panic("TunnelSocketConnectCb: initial write failed: %s\n",
            AsyncSocket_Err2String(asockErr));
   }
   ASSERT(sendSize == requestSize);

   /* Kick off channel reading */
   DynBuf_Init(&gRecvBuf);
   TunnelSocketRecvCb(NULL, 0, NULL, NULL);

   {
      /* Find the local address */
      struct sockaddr addr = { 0 };
      socklen_t addrLen = sizeof(addr);
      int gaiErr;

      if (getsockname(AsyncSocket_GetFd(gAsock), &addr, &addrLen) < 0) {
         NOT_IMPLEMENTED();
      }

      hostIp = inet_ntoa(((struct sockaddr_in*) &addr)->sin_addr);
      if (!hostIp) {
         NOT_IMPLEMENTED();
      }

      gaiErr = getnameinfo(&addr, addrLen, hostName, sizeof(hostName), NULL, 0,
                           0);
      if (gaiErr < 0) {
         Warning("Unable to lookup name for localhost address '%s': %s.\n",
                 hostIp, gai_strerror(gaiErr));
         strcpy(hostName, hostIp);
      }
   }

   err = TunnelProxy_Connect(gTunnelProxy, hostIp, hostName,
                             TunnelSendNeededCb, NULL,
                             TunnelDisconnectCb, NULL);
   ASSERT(err == TP_ERR_OK);

   free(serverUrl);
   free(host);
   free(path);
   free(request);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketProxyConnectCb --
 *
 *      AsyncSocket connection callback for the proxy server.  Sends a simple
 *      HTTP1.1 CONNECT request header, calls TunnelSocketProxyRecvCb to read
 *      the proxy response header.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelSocketProxyConnectCb(AsyncSocket *asock, // IN
                           void *userData)     // IN: not used
{
   char *request;
   size_t requestSize = 0;
   int sendSize = 0;
   int asockErr = ASOCKERR_SUCCESS;
   char *serverUrl;
   char *host = NULL;
   unsigned short port = 0;

   serverUrl = TunnelProxy_GetConnectUrl(gTunnelProxy, gServerArg);
   if (!TunnelParseUrl(serverUrl, NULL, &host, &port, NULL, NULL)) {
      NOT_IMPLEMENTED();
   }

   request = Str_Asprintf(&requestSize,
      "CONNECT %s:%d HTTP/1.1\r\n"
      "Host: %s:%d\r\n"
      "User-agent: Mozilla/4.0 (compatible; MSIE 6.0)\r\n"
      "Proxy-Connection: Keep-Alive\r\n"
      "Content-Length: 0\r\n"
      "\r\n", host, port, host, port);

   /* Send initial request header */
   asockErr = AsyncSocket_SendBlocking(gAsock, request, requestSize,
                                       &sendSize, BLOCKING_TIMEOUT_MS);
   if (asockErr != ASOCKERR_SUCCESS) {
      Panic("TunnelSocketConnectCb: initial write failed: %s\n",
            AsyncSocket_Err2String(asockErr));
   }
   ASSERT(sendSize == requestSize);

   /* Kick off channel reading */
   DynBuf_Init(&gRecvBuf);
   TunnelSocketProxyRecvCb(NULL, 0, NULL, NULL);

   free(serverUrl);
   free(host);
   free(request);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelConnect --
 *
 *      Create an AsyncSocket and start the connection process for the server
 *      URL passed on the command line.  Will connect to the environment's
 *      http_proxy or https_proxy if set (depending on the protocol of the
 *      server URL).
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Created socket is stored in the gAsock global.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelConnect(void)
{
   const char *http_proxy = NULL;
   const char *http_proxy_env = NULL;
   const char *host;
   unsigned short port;
   int asockErr = ASOCKERR_SUCCESS;
   AsyncSocketConnectFn connectFn;
   char *serverUrl = NULL;
   char *serverProto = NULL;
   char *serverHost = NULL;
   unsigned short serverPort = 0;
   Bool serverSecure = FALSE;
   char *proxyHost = NULL;
   unsigned short proxyPort = 0;

   ASSERT(!gAsock);
   ASSERT(!gRecvHeaderDone);

   serverUrl = TunnelProxy_GetConnectUrl(gTunnelProxy, gServerArg);
   if (!TunnelParseUrl(serverUrl, &serverProto, &serverHost, &serverPort, NULL,
                       &serverSecure)) {
      Panic("Invalid <server-url> argument: %s\n", serverUrl);
   }

   if (Str_Strcmp(serverProto, "http") == 0) {
      http_proxy_env = "http_proxy";
      http_proxy = getenv(http_proxy_env);
   } else if (Str_Strcmp(serverProto, "https") == 0) {
      http_proxy_env = "https_proxy";
      http_proxy = getenv(http_proxy_env);
      if (!http_proxy || !*http_proxy) {
         http_proxy_env = "HTTPS_PROXY";
         http_proxy = getenv(http_proxy_env);
      }
   } else {
      Panic("Unknown <server-url> protocol '%s'.\n", serverProto);
   }

   if (http_proxy) {
      if (!*http_proxy) {
         /* Ignore empty proxy env var */
         http_proxy = NULL;
      } else if (!TunnelParseUrl(http_proxy, NULL, &proxyHost, &proxyPort,
                                 NULL, NULL)) {
         Warning("Invalid %s URL '%s'.  Attempting direct connection.\n",
                 http_proxy_env, http_proxy);
         http_proxy = NULL;
      }
   }

   if (http_proxy) {
      Log("Connecting to tunnel server '%s:%d' over %s, via %s server '%s:%d'.\n",
          serverHost, serverPort, serverSecure ? "HTTPS" : "HTTP",
          http_proxy_env, proxyHost, proxyPort);
      host = proxyHost;
      port = proxyPort;
      connectFn = TunnelSocketProxyConnectCb;
   } else {
      Log("Connecting to tunnel server '%s:%d' over %s.\n", serverHost,
          serverPort, serverSecure ? "HTTPS" : "HTTP");
      host = serverHost;
      port = serverPort;
      connectFn = TunnelSocketConnectCb;
   }
   ASSERT(host && port > 0 && connectFn);

   gAsock = AsyncSocket_Connect(host, port, connectFn, NULL, 0, NULL,
                                &asockErr);
   if (ASOCKERR_SUCCESS != asockErr) {
      Panic("Connection failed: %s (%d)\n", AsyncSocket_Err2String(asockErr),
            asockErr);
   }
   ASSERT(gAsock);

   AsyncSocket_SetErrorFn(gAsock, TunnelSocketErrorCb, NULL);
   AsyncSocket_UseNodelay(gAsock, TRUE);

   free(serverUrl);
   free(serverProto);
   free(serverHost);
   free(proxyHost);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelPrintUsage --
 *
 *      Print binary usage information, in the UNIX tradition.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelPrintUsage(const char *binName) // IN
{
   Warning("Usage: %s <server-url> <connection-id>\n", binName);
   exit(1);
}


/*
 *-----------------------------------------------------------------------------
 *
 * main --
 *
 *      Main tunnel entrypoint.  Create a TunnelProxy object, start the async
 *      connect process and start the main poll loop.
 *
 * Results:
 *      0.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

int
main(int argc,    // IN
     char **argv) // IN
{
   if (argc < 3) {
      TunnelPrintUsage(argv[0]);
   }
   gServerArg = argv[1];
   gConnectionIdArg = argv[2];

   if (!*gServerArg || !*gConnectionIdArg) {
      TunnelPrintUsage(argv[0]);
   }

   Poll_InitDefault();
   Preference_Init();
   Log_Init(NULL, APPNAME".log.filename", APPNAME);

   /* Use the system library, but don't do a version check */
   SSL_InitEx(NULL, NULL, NULL, TRUE, FALSE, FALSE);

   AsyncSocket_Init();

   gTunnelProxy = TunnelProxy_Create(gConnectionIdArg, NULL, NULL, NULL, NULL,
                                     NULL, NULL);
   ASSERT(gTunnelProxy);

   TunnelConnect();

   /* Enter the main loop */
   Poll_Loop(TRUE, NULL, POLL_CLASS_MAIN);

   return 0;
}
