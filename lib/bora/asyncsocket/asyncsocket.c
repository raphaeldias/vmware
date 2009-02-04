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
 * asyncsocket.c --
 *
 *      The AsyncSocket object is a fairly simple wrapper around a basic TCP
 *      socket. It's potentially asynchronous for both read and write
 *      operations. Reads are "requested" by registering a receive function
 *      that is called once the requested amount of data has been read from
 *      the socket. Similarly, writes are queued along with a send function
 *      that is called once the data has been written. Errors are reported via
 *      a separate callback.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

#include "str.h"

#ifdef _WIN32
/*
 * We redefine strcpy/strcat because the Windows SDK uses it for getaddrinfo().
 * When we upgrade SDKs, this redefinition can go away.
 */
#define strcpy(dst,src) Str_Strcpy((dst), (src), sizeof (dst))
#define strcat(dst,src) Str_Strcpy((dst), (src), sizeof (dst))
#include <winsock2.h>
#include <ws2tcpip.h>
#include <MSWSock.h>
#else
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "vmware.h"
#include "ssl.h"
#include "asyncsocket.h"
#include "poll.h"
#include "log.h"
#include "err.h"
#include "hostinfo.h"
#include "util.h"
#include "msg.h"
#include "posix.h"

#ifdef __APPLE_READ_BUG_WORKAROUND__
#include "config.h"
#endif

#define LOGLEVEL_MODULE asyncsocket
#include "loglevel_user.h"

#ifdef VMX86_SERVER
#include "uwvmkAPI.h"
#endif

#ifdef _WIN32
#define ASOCK_CLOSEFD(fd)       closesocket(fd)
#define ASOCK_LASTERROR()       WSAGetLastError()
#define ASOCK_ENOTCONN          WSAENOTCONN
#define ASOCK_ENOTSOCK          WSAENOTSOCK
#define ASOCK_EADDRINUSE        WSAEADDRINUSE
#define ASOCK_ECONNECTING       WSAEWOULDBLOCK
#define ASOCK_EWOULDBLOCK       WSAEWOULDBLOCK
#else
#define ASOCK_CLOSEFD(fd)       close(fd)
#define ASOCK_LASTERROR()       errno
#define ASOCK_ENOTCONN          ENOTCONN
#define ASOCK_ENOTSOCK          ENOTSOCK
#define ASOCK_EADDRINUSE        EADDRINUSE
#define ASOCK_ECONNECTING       EINPROGRESS
#define ASOCK_EWOULDBLOCK       EWOULDBLOCK
#endif

/*
 * The slots each have a "unique" ID, which is just an incrementing integer.
 */
static unsigned long nextid;

/*
 * Output buffer list data type, for the queue of outgoing buffers
 */
typedef struct SendBufList {
   struct SendBufList   *next;
   void                 *buf;
   int                   len;
   AsyncSocketSendFn     sendFn;
   void                 *clientData;
} SendBufList;

struct AsyncSocket {
   int id;
   AsyncSocketState state;
   int fd;
   SSLSock sslSock;
   int type;  /* SOCK_STREAM or SOCK_DGRAM */

   unsigned int refCount;
   AsyncSocketErrorFn errorFn;
   void *errorClientData;
   VmTimeType drainTimeoutUS;

   struct sockaddr remoteAddr;
   socklen_t remoteAddrLen;

   AsyncSocketConnectFn connectFn;
   AsyncSocketRecvFn recvFn;
   AsyncSocketRecvUDPFn recvUDPFn;
   void *clientData;		   /* shared by recvFn and connectFn */
   AsyncSocketPollParams pollParams;

   void *recvBuf;
   int recvPos;
   int recvLen;
   Bool recvCb;

#ifdef __APPLE_READ_BUG_WORKAROUND__
   Bool  readPausedForSocketBug;
   int   savedRecvPos;
   void  *savedRecvBuf;
   void  *savedRecvFunction;
   int   savedRecvLen;
#endif

   SendBufList *sendBufList;
   SendBufList **sendBufTail;
   int sendPos;
   Bool sendCb;
   Bool sendBufFull;

   Bool sslConnected;

   Bool inRecvLoop;
};


/*
 * Local Functions
 */
static int AsyncSocketMakeNonBlocking(int fd);
static void AsyncSocketHandleError(AsyncSocket *asock, int asockErr);
static void AsyncSocketAcceptCallback(void *clientData);
static void AsyncSocketConnectCallback(void *clientData);
static void AsyncSocketRecvCallback(void *clientData);
static void AsyncSocketRecvUDPCallback(void *clientData);
static void AsyncSocketSendCallback(void *clientData);
static int AsyncSocketAddRef(AsyncSocket *s);
static int AsyncSocketRelease(AsyncSocket *s);
static int AsyncSocketBlockingWork(AsyncSocket *asock, Bool read, void *buf, int len,
                                   int *completed, int timeoutMS);
static VMwareStatus AsyncSocketPollAdd(AsyncSocket *asock, Bool socket,
                                       int flags, PollerFunction callback,
				       ...);
static Bool AsyncSocketPollRemove(AsyncSocket *asock, Bool socket,
                                  int flags, PollerFunction callback);
static AsyncSocket *AsyncSocketInit(int socketType,
                                    AsyncSocketPollParams *pollParams,
                                    int *outError);
static Bool AsyncSocketBind(AsyncSocket *asock, unsigned int ip,
                            unsigned short port, int *outError);
static Bool AsyncSocketListen(AsyncSocket *asock, AsyncSocketConnectFn connectFn,
                              void *clientData, int *outError);
static AsyncSocket *AsyncSocketConnectIP(uint32 ip,
                                         unsigned short port,
                                         AsyncSocketConnectFn connectFn,
                                         void *clientData,
                                         AsyncSocketConnectFlags flags,
                                         AsyncSocketPollParams *pollParams,
                                         int *outError);
static int AsyncSocketResolveAddr(const char *hostname, unsigned short port,
                                  int type, struct sockaddr_in *addr);

#ifdef __APPLE_READ_BUG_WORKAROUND__
static Bool AsyncSocket_HandleSSLError(SSLSock sslSock, void *context);
static void AsyncSocketRetryReadCallback(void *clientData);
#define REMOVE_FROM_POLL_PERIOD_IN_MILLISECS    500
#endif 


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_Init --
 *
 *      Initializes the host's socket library. NOP on Posix.
 *      On Windows, calls WSAStartup().
 *
 * Results:
 *      ASOCKERR_SUCCESS or ASOCKERR_GENERIC.
 *
 * Side effects:
 *      On Windows, loads winsock library.
 *
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_Init(void)
{
#ifdef _WIN32
   WSADATA wsaData;
   WORD versionRequested = MAKEWORD(2, 0);
   return WSAStartup(versionRequested, &wsaData) ?
             ASOCKERR_GENERIC : ASOCKERR_SUCCESS;
#endif
   return ASOCKERR_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_Err2String --
 *
 *      Returns the error string associated with error code. 
 *
 * Results:
 *      Error string.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

const char *
AsyncSocket_Err2String(int err)  // IN
{
   return Msg_StripMSGID(AsyncSocket_MsgError(err));
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_MsgError --
 *
 *      Returns the message associated with error code. 
 *
 * Results:
 *      Message string.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

const char *
AsyncSocket_MsgError(int asyncSockError)   // IN
{
   const char *result = NULL;
   switch (asyncSockError) {
   case ASOCKERR_SUCCESS:
      result = MSGID(asyncsocket.success) "Success";
      break;
   case ASOCKERR_GENERIC:
      result = MSGID(asyncsocket.generic) "Generic asyncsocket error";
      break;
   case ASOCKERR_INVAL:
      result = MSGID(asyncsocket.invalid) "Invalid parameters";
      break;
   case ASOCKERR_TIMEOUT:
      result = MSGID(asyncsocket.timeout) "Time-out error";
      break;
   case ASOCKERR_NOTCONNECTED:
      result = MSGID(asyncsocket.notconnected) "Local socket not connected";
      break;
   case ASOCKERR_REMOTE_DISCONNECT:
      result = MSGID(asyncsocket.remotedisconnect) "Remote disconnection";
      break;
   case ASOCKERR_CLOSED:
      result = MSGID(asyncsocket.closed) "Closed socket";
      break;
   case ASOCKERR_CONNECT:
      result = MSGID(asyncsocket.connect) "Connection error";
      break;
   case ASOCKERR_POLL:
      result = MSGID(asyncsocket.poll) "Poll registration error";
      break;
   case ASOCKERR_BIND:
      result = MSGID(asyncsocket.bind) "Socket bind error";
      break;
   case ASOCKERR_BINDADDRINUSE:
      result = MSGID(asyncsocket.bindaddrinuse) "Socket bind address already in use";
      break;
   case ASOCKERR_LISTEN:
      result = MSGID(asyncsocket.listen) "Socket listen error";
      break;
   }

   if (!result) {
      Warning("AsyncSocket_MsgError was passed bad code %d\n", asyncSockError);
      result = MSGID(asyncsocket.unknown) "Unknown error";
   }
   return result;  
}

/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_GetFd --
 *
 *      Returns the fd for this socket.
 *
 * Results:
 *      File descriptor.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_GetFd(AsyncSocket *s)
{
   return s->fd;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_GetRemoteIPAddress --
 *
 *      Given an AsyncSocket object, returns the remote IP address associated
 *      with it, or an error if the request is meaningless for the underlying
 *      connection.
 *
 * Results:
 *      ASOCKERR_SUCCESS or ASOCKERR_GENERIC.
 *
 * Side effects:
 *
 *
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_GetRemoteIPAddress(AsyncSocket *asock,      // IN
                               uint32 *ipRet,           // OUT
                               const char **ipRetStr)   // OUT
{
   uint32 ip;
   struct in_addr ipAddr;

   ASSERT(asock);
   ASSERT(ipRet != NULL || ipRetStr != NULL);

   if ((ipRet == NULL && ipRetStr == NULL) || asock == NULL ||
       asock->state != AsyncSocketConnected ||
       asock->remoteAddrLen != sizeof (struct sockaddr_in)) {
      return ASOCKERR_GENERIC;
   }

   ip = ntohl(((struct sockaddr_in *) &asock->remoteAddr)->sin_addr.s_addr);

   if (ipRet != NULL) {
      *ipRet = ip;
   }

   if (ipRetStr != NULL) {
      ipAddr.s_addr = htonl(ip);
      *ipRetStr = inet_ntoa(ipAddr);
   }

   return ASOCKERR_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_Listen --
 *
 *      Listens on the specified port and accepts new connections. Fires the
 *      connect callback with new AsyncSocket object for each connection.
 *
 * Results:
 *      New AsyncSocket in listening state or NULL on error.
 *
 * Side effects:
 *      Creates new socket, binds and listens.
 *
 *----------------------------------------------------------------------------
 */

AsyncSocket *
AsyncSocket_Listen(unsigned short port,
                   AsyncSocketConnectFn connectFn,
                   void *clientData,
                   AsyncSocketPollParams *pollParams,
                   int *outError)
{
   return AsyncSocket_ListenIP(INADDR_ANY, port, connectFn, clientData,
                               pollParams, outError);
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_ListenIPStr --
 *
 *      Listens on the specified ip, port and accepts new connections. Fires
 *      the connect callback with new AsyncSocket object for each connection.
 *
 * Results:
 *      New AsyncSocket in listening state or NULL on error.
 *
 * Side effects:
 *      Creates new socket, binds and listens.
 *
 *----------------------------------------------------------------------------
 */

AsyncSocket *
AsyncSocket_ListenIPStr(const char *ipStr,
                        unsigned short port,
                        AsyncSocketConnectFn connectFn,
                        void *clientData,
                        AsyncSocketPollParams *pollParams,
                        int *outError)
{
   struct in_addr ipAddr;

   /* 
    * Windows doesn't have inet_aton so using inet_addr insted. Alternative is
    * ifdef with WSAStringToAddress and inet_aton.
    */
   ipAddr.s_addr = inet_addr(ipStr);
   if (ipAddr.s_addr == INADDR_NONE) {
      if (outError) {
         *outError = INADDR_NONE;
      }
      return NULL;
   }

   return AsyncSocket_ListenIP(ntohl(ipAddr.s_addr), port, connectFn, clientData,
                               pollParams, outError);
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_ListenIP --
 *
 *      Listens on the specified ip, port and accepts new connections. Fires
 *      the connect callback with new AsyncSocket object for each connection.
 *
 * Results:
 *      New AsyncSocket in listening state or NULL on error.
 *
 * Side effects:
 *      Creates new socket, binds and listens.
 *
 *----------------------------------------------------------------------------
 */

AsyncSocket *
AsyncSocket_ListenIP(unsigned int ip,
                     unsigned short port,
                     AsyncSocketConnectFn connectFn,
                     void *clientData,
                     AsyncSocketPollParams *pollParams,
                     int *outError)
{
   AsyncSocket *asock = AsyncSocketInit(SOCK_STREAM, pollParams, outError);
   if (NULL != asock
       && AsyncSocketBind(asock, ip, port, outError)
       && AsyncSocketListen(asock, connectFn, clientData, outError)) {
      return asock;
   }

   return NULL;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_BindUDP --
 *
 *      Listens on the specified port and accepts new UDP connections.
 *
 * Results:
 *      New AsyncSocket in listening state or NULL on error.
 *
 * Side effects:
 *      Creates new socket, binds.
 *
 *----------------------------------------------------------------------------
 */

AsyncSocket *
AsyncSocket_BindUDP(unsigned short port,
                    void *clientData,
                    AsyncSocketPollParams *pollParams,
                    int *outError)
{
   AsyncSocket *asock = AsyncSocketInit(SOCK_DGRAM, pollParams, outError);

   if (NULL != asock && AsyncSocketBind(asock, INADDR_ANY, port, outError)) {
      asock->connectFn = NULL;
      asock->clientData = clientData;
      asock->state = AsyncSocketConnected;
      return asock;
   }

   return NULL;
}

/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketInit --
 *
 *      This is an internal routine that sets up a socket.
 *
 * Results:
 *      New AsyncSocket or NULL on error.
 *
 * Side effects:
 *      Creates new socket.
 * 
 *----------------------------------------------------------------------------
 */

AsyncSocket *
AsyncSocketInit(int socketType,                    // IN
                AsyncSocketPollParams *pollParams, // IN
                int *outError)                     // OUT
{
   AsyncSocket *asock = NULL;
   int error = ASOCKERR_GENERIC;
   int sysErr;
   int fd = -1;

   /*
    * Create a new TCP/IP socket
    */
   if ((fd = socket(AF_INET, socketType, 0)) == -1) {
      sysErr = ASOCK_LASTERROR();
      Warning(ASOCKPREFIX "could not create new socket, error %d: %s\n",
              sysErr, Err_Errno2String(sysErr));
      goto error;
   }

   /*
    * Wrap it with an asock object
    */
   if ((asock = AsyncSocket_AttachToFd(fd, pollParams, &error)) == NULL) {
      goto error;
   }
   asock->fd   = fd;
   asock->type = socketType;

   /*
    *  Enable broadcast for UDP.
    */
   if (SOCK_DGRAM == socketType) {
      int bcast = 1;
      if (setsockopt(asock->fd, SOL_SOCKET, SO_BROADCAST,
                     (const void *) &bcast, sizeof(bcast)) != 0) {
         sysErr = ASOCK_LASTERROR();
         Warning(ASOCKPREFIX "could not set SO_BROADCAST, error %d: %s\n",
                 sysErr, Err_Errno2String(sysErr));
      }

#ifdef _WIN32
      {
         /*
          * On Windows, sending a UDP packet to a host may result in
          * a "connection reset by peer" message to be sent back by
          * the remote machine.  If that happens, our UDP socket becomes
          * useless.  We can disable this with the SIO_UDP_CONNRESET
          * ioctl option.
          */
         DWORD dwBytesReturned = 0;
         BOOL bNewBehavior = FALSE;
         DWORD status;

         status = WSAIoctl(asock->fd, SIO_UDP_CONNRESET,
                           &bNewBehavior, sizeof(bNewBehavior),
                           NULL, 0, &dwBytesReturned,
                           NULL, NULL);

         if (SOCKET_ERROR == status) {
            DWORD dwErr = WSAGetLastError();
            ASOCKLOG(3, asock, ("WSAIoctl(SIO_UDP_CONNRESET) Error: %d\n",
                                                                       dwErr));
         }
      }
#endif
   }

   return asock;

error:
   free(asock);

   if (fd != -1) {
      ASOCK_CLOSEFD(fd);
   }
   if (outError) {
      *outError = error;
   }
   return NULL;
}

/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketBind --
 *
 *      This is an internal routine that binds a socket to a port.
 *
 * Results:
 *      Returns TRUE upon success, FALSE upon failure.
 *
 * Side effects:
 *      Socket is bound to a particular port.
 *
 *----------------------------------------------------------------------------
 */


Bool
AsyncSocketBind(AsyncSocket *asock,  // IN
                unsigned int ip,     // IN
                unsigned short port, // IN
                int *outError)       // OUT
{
   struct sockaddr_in local_addr = { 0 };
   int error = ASOCKERR_BIND;
   int sysErr;
#ifndef _WIN32
   int reuse = port != 0;
#endif

   Log(ASOCKPREFIX "creating new listening socket on port %d\n", port);
   ASSERT(NULL != asock);

#ifndef _WIN32
   /*
    * Don't ever use SO_REUSEADDR on Windows; it doesn't mean what you think
    * it means.
    */
   if (setsockopt(asock->fd, SOL_SOCKET, SO_REUSEADDR,
                  (const void *) &reuse, sizeof(reuse)) != 0) {
      sysErr = ASOCK_LASTERROR();
      Warning(ASOCKPREFIX "could not set SO_REUSEADDR, error %d: %s\n",
              sysErr, Err_Errno2String(sysErr));
   }
#endif

#ifdef _WIN32
   /*
    * Always set SO_EXCLUSIVEADDRUSE on Windows, to prevent other applications
    * from stealing this socket. (Yes, Windows is that stupid).
    */
   {
      int exclusive = 1;
      if (setsockopt(asock->fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                     (const void *) &exclusive, sizeof(exclusive)) != 0) {
         sysErr = ASOCK_LASTERROR();
         Warning(ASOCKPREFIX "could not set SO_REUSEADDR, error %d: %s\n",
                 sysErr, Err_Errno2String(sysErr));
      }
   }
#endif

   /*
    * Bind to a port
    */
   local_addr.sin_family = AF_INET;
   local_addr.sin_addr.s_addr = htonl(ip);
   local_addr.sin_port = htons(port);

   if (bind(asock->fd, (struct sockaddr *) &local_addr,
            sizeof(local_addr)) != 0) {
      sysErr = ASOCK_LASTERROR();
      if (sysErr == ASOCK_EADDRINUSE) {
         error = ASOCKERR_BINDADDRINUSE;
      }
      Warning("could not bind socket, error %d: %s\n", sysErr,
              Err_Errno2String(sysErr));
      goto error;
   }
   return TRUE;

error:
   if (asock && asock->fd != -1) {
      ASOCK_CLOSEFD(asock->fd);
   }
   free(asock);

   if (outError) {
      *outError = error;
   }
   return FALSE;
}

/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketListen --
 *
 *      This is an internal routine that calls listen() on a socket.
 *
 * Results:
 *      Returns TRUE upon success, FALSE upon failure.
 *
 * Side effects:
 *      Socket is in listening state.
 *
 *----------------------------------------------------------------------------
 */


Bool
AsyncSocketListen(AsyncSocket *asock,                // IN
                  AsyncSocketConnectFn connectFn,    // IN
                  void *clientData,                  // IN
                  int *outError)                     // OUT
{
   VMwareStatus pollStatus;
   int error;

   ASSERT(NULL != asock);
   ASSERT(SOCK_STREAM == asock->type);

   if (!connectFn) {
      Warning(ASOCKPREFIX "invalid arguments to listen!\n");
      error = ASOCKERR_INVAL;
      goto error;
   }

   /*
    * Listen on the socket
    */
   if (listen(asock->fd, 5) != 0) {
      int sysErr = ASOCK_LASTERROR();
      Warning(ASOCKPREFIX "could not listen on socket, error %d: %s\n", sysErr,
              Err_Errno2String(sysErr));
      error = ASOCKERR_LISTEN;
      goto error;
   }

   /*
    * Register a read callback to fire each time the socket
    * is ready for accept.
    */
   pollStatus = AsyncSocketPollAdd(asock, TRUE, POLL_FLAG_READ | POLL_FLAG_PERIODIC,
                                   AsyncSocketAcceptCallback);

   if (pollStatus != VMWARE_STATUS_SUCCESS) {
      ASOCKWARN(asock, ("could not register accept callback!\n"));
      error = ASOCKERR_POLL;
      goto error;
   }
   asock->state = AsyncSocketListening;

   asock->connectFn = connectFn;
   asock->clientData = clientData;

   return TRUE;

error:
   if (asock && asock->fd != -1) {
      ASOCK_CLOSEFD(asock->fd);
   }
   free(asock);

   if (outError) {
      *outError = error;
   }
   return FALSE;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_Connect --
 *
 *      AsyncSocket constructor.  This is just a wrapper for ConnectIP that
 *      does hostname -> IP address lookup.
 *
 *      NOTE: This function can block.
 *
 * Results:
 *      AsyncSocket * on success and NULL on failure. 
 *      On failure, error is returned in *outError.
 *
 * Side effects:
 *      Allocates an AsyncSocket, registers a poll callback.
 *
 *----------------------------------------------------------------------------
 */

AsyncSocket *
AsyncSocket_Connect(const char *hostname,
                    unsigned short port,
                    AsyncSocketConnectFn connectFn,
                    void *clientData,
                    AsyncSocketConnectFlags flags,
                    AsyncSocketPollParams *pollParams,
                    int *outError)
{
   struct sockaddr_in addr;
   uint32 ip;
   int getaddrinfoError;
   int error;
   AsyncSocket *asock;

   if (!connectFn || !hostname) {
      error = ASOCKERR_INVAL;
      Warning(ASOCKPREFIX "invalid arguments to connect!\n");
      goto error;
   }

   /*
    * Resolve the hostname.  Handles dotted decimal strings, too.
    */
   getaddrinfoError = AsyncSocketResolveAddr(hostname, port,
                                             SOCK_STREAM, &addr);

   if (0 != getaddrinfoError) {
      Log(ASOCKPREFIX "Failed to resolve address '%s' and port %u\n",
          hostname, port);
      error = ASOCKERR_CONNECT;
      goto error;
   }

   ip = ntohl(addr.sin_addr.s_addr);
   Log(ASOCKPREFIX "creating new socket, connecting to %u.%u.%u.%u:%u (%s)\n",
       (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
       port, hostname);

   asock = AsyncSocketConnectIP(ip, port,
                                connectFn, clientData, flags, pollParams,
                                &error);
   if (!asock) {
      Warning(ASOCKPREFIX "connection attempt failed\n");
      error = ASOCKERR_CONNECT;
      goto error;
   }

   /*
    * Store a copy of the sockaddr_in so we can look it up later. Note that
    * sizeof(struct sockaddr) == sizeof(struct sockaddr_in) so this is ok.
    */
   asock->remoteAddr = *((struct sockaddr *) &addr);
   asock->remoteAddrLen = sizeof addr;

   return asock;

error:
   if (outError) {
      *outError = error;
   }
   return NULL;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_ConnectIP --
 *
 *      AsyncSocket constructor. Connects to the specified address:port, and
 *      passes the caller a valid asock via the callback once the connection
 *      has been established.
 *
 * Results:
 *      ASOCKERR_SUCCESS or ASOCKERR_GENERIC.
 *
 * Side effects:
 *      Allocates an AsyncSocket, registers a poll callback.
 *
 *----------------------------------------------------------------------------
 */

AsyncSocket *
AsyncSocket_ConnectIP(uint32 ip,
                      unsigned short port,
                      AsyncSocketConnectFn connectFn,
                      void *clientData,
                      AsyncSocketConnectFlags flags,
                      AsyncSocketPollParams *pollParams,
                      int *outError)
{
   Log(ASOCKPREFIX "creating new socket, connecting to %u.%u.%u.%u:%u\n",
       (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
       port);
   return AsyncSocketConnectIP(ip, port, connectFn, clientData, flags,
                               pollParams, outError);
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketConnectIP --
 *
 *      Internal AsyncSocket constructor.
 *
 * Results:
 *      ASOCKERR_SUCCESS or ASOCKERR_GENERIC.
 *
 * Side effects:
 *      Allocates an AsyncSocket, registers a poll callback.
 *
 *----------------------------------------------------------------------------
 */

static AsyncSocket *
AsyncSocketConnectIP(uint32 ip,
                     unsigned short port,
                     AsyncSocketConnectFn connectFn,
                     void *clientData,
                     AsyncSocketConnectFlags flags,
                     AsyncSocketPollParams *pollParams,
                     int *outError)
{
   int fd = -1;
   struct sockaddr_in local_addr;
   VMwareStatus pollStatus;
   AsyncSocket *asock = NULL;
   int error = ASOCKERR_GENERIC;
   int sysErr;
   int socketFamily = PF_INET;

   if (!connectFn) {
      error = ASOCKERR_INVAL;
      Warning(ASOCKPREFIX "invalid arguments to connect!\n");
      goto error;
   }

   /* 
    * Allow selecting alternate network stack for ESX.
    */
#ifdef VMX86_SERVER
   if ((flags & (ASOCKCONN_USE_ESX_SHADOW_STACK | 
                 ASOCKCONN_USE_ESX_NATIVE_STACK)) ==
       (ASOCKCONN_USE_ESX_SHADOW_STACK | ASOCKCONN_USE_ESX_NATIVE_STACK)) {
      error = ASOCKERR_INVAL;
      Warning(ASOCKPREFIX "Can choose only one ESX stack for connect!\n");
      LOG(2, (ASOCKPREFIX "Tried BOTH ESX stacks?!\n"));
      goto error;
   }

   if (flags & ASOCKCONN_USE_ESX_SHADOW_STACK) {
      LOG(2, (ASOCKPREFIX "Selecting ESX SHADOW stack.\n"));
      socketFamily = PF_VMKINET_SHADOW;
   }
   if (flags & ASOCKCONN_USE_ESX_NATIVE_STACK) {
      LOG(2, (ASOCKPREFIX "Selecting ESX NATIVE stack.\n"));
      socketFamily = PF_VMKINET_NATIVE;
   }
#endif

   /*
    * Create a new IP socket
    */
   if ((fd = socket(socketFamily, SOCK_STREAM, 0)) == -1) {
      sysErr = ASOCK_LASTERROR();
      Warning(ASOCKPREFIX "failed to create socket, error %d: %s\n",
              sysErr, Err_Errno2String(sysErr));
      error = ASOCKERR_CONNECT;
      goto error;
   }

   /*
    * Wrap it with an asock
    */
   if ((asock = AsyncSocket_AttachToFd(fd, pollParams, &error)) == NULL) {
      goto error;
   }

   /*
    * Create a address structure to pass to connect
    */
   memset((char *)&local_addr, 0, sizeof(local_addr));
   local_addr.sin_family = AF_INET;
   local_addr.sin_port = htons(port);

   /*
    * Is address already in network-byte-order?
    */
   if ((flags & ASOCKCONN_ADDR_IN_NETWORK_BYTE_ORDER) !=
         ASOCKCONN_ADDR_IN_NETWORK_BYTE_ORDER) {
      ip = htonl(ip);
   }

   local_addr.sin_addr.s_addr = ip;

   /*
    * Call connect(), which can either succeed immediately or return an error
    * indicating that the connection is in progress. In the latter case, we
    * can poll the fd for write to find out when the connection attempt
    * has succeeded (or failed). In either case, we want to invoke the
    * caller's connect callback from Poll rather than directly, so if the
    * connection succeeds immediately, we just schedule the connect callback
    * as a one-time (RTime) callback instead.
    */
   if (connect(fd, (struct sockaddr *) &local_addr, sizeof(local_addr)) != 0) {
      if (ASOCK_LASTERROR() == ASOCK_ECONNECTING) {
	 ASOCKLOG(1, asock, ("registering write callback for socket connect\n"));
         pollStatus = AsyncSocketPollAdd(asock, TRUE, POLL_FLAG_WRITE,
                                         AsyncSocketConnectCallback);
      } else {
         sysErr = ASOCK_LASTERROR();
         Warning(ASOCKPREFIX "connect failed, error %d: %s\n",
                 sysErr, Err_Errno2String(sysErr));
         error = ASOCKERR_CONNECT;
         goto error;
      }
   } else {
      ASOCKLOG(2, asock,
	       ("socket connected, registering RTime callback for connect\n"));
      pollStatus = AsyncSocketPollAdd(asock, FALSE, 0, AsyncSocketConnectCallback, 0);
   }

   if (pollStatus != VMWARE_STATUS_SUCCESS) {
      ASOCKWARN(asock, ("failed to register callback in connect!\n"));
      error = ASOCKERR_POLL;
      goto error;
   }

   asock->state = AsyncSocketConnecting;
   asock->connectFn = connectFn;
   asock->clientData = clientData;
   asock->type = SOCK_STREAM;

   return asock;

error:
   if (asock) {
      free(asock);
   }
   if (fd != -1) {
      ASOCK_CLOSEFD(fd);
   }
   if (outError) {
      *outError = error;
   }
   return NULL;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_AttachToSSLSock --
 *
 *      AsyncSocket constructor. Wraps an existing SSLSock object with an
 *      AsyncSocket and returns the latter.
 *
 * Results:
 *      New AsyncSocket object or NULL on error.
 *
 * Side effects:
 *      Allocates memory, makes the underlying fd for the socket non-blocking.
 *
 *----------------------------------------------------------------------------
 */

AsyncSocket *
AsyncSocket_AttachToSSLSock(SSLSock sslSock,
                            AsyncSocketPollParams *pollParams,
                            int *outError)
{
   AsyncSocket *s;
   int fd;
   int error;

   ASSERT(sslSock);

   fd = SSL_GetFd(sslSock);

   if ((AsyncSocketMakeNonBlocking(fd)) != ASOCKERR_SUCCESS) {
      int sysErr = ASOCK_LASTERROR();
      Warning(ASOCKPREFIX "failed to make fd %d non-blocking!: %d, %s\n", 
              fd, sysErr, Err_Errno2String(sysErr));
      error = ASOCKERR_GENERIC;
      goto error;
   }

   s = Util_SafeCalloc(1, sizeof *s);
   s->id = ++nextid;
   s->sslSock = sslSock;
   s->fd = fd;
   s->state = AsyncSocketConnected;
   s->refCount = 1;
   s->inRecvLoop = FALSE;
   s->sendBufFull = FALSE;
   s->type = SOCK_STREAM;
   s->sendBufTail = &(s->sendBufList);

   if (pollParams) {
      s->pollParams = *pollParams;
   } else {
      s->pollParams.pollClass = POLL_CS_MAIN;
      s->pollParams.flags = 0;
      s->pollParams.lock = NULL;
   }

   /*
    * On Apple, attach the hook function that works around the read() error.
    */
#ifdef __APPLE_READ_BUG_WORKAROUND__
   if (Config_GetBool(TRUE, "asock.pauseReadOnFalseError")) {
      SSL_SetErrorHook(sslSock, AsyncSocket_HandleSSLError, s);
   }
#endif

   ASOCKLOG(1, s, ("new asock id %d attached to fd %d\n", s->id, s->fd));
   return s;

error:
   if (outError) {
      *outError = error;
   }
   return NULL;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_AttachToFd --
 *
 *      AsyncSocket constructor. Wraps a valid socket fd with an AsyncSocket
 *      object.
 *
 * Results:
 *      New AsyncSocket or NULL on error.
 *
 * Side effects:
 *      Enables SSL (via SSL_Connect) on the fd if useSSL is specified.
 *
 *----------------------------------------------------------------------------
 */

AsyncSocket *
AsyncSocket_AttachToFd(int fd,
                       AsyncSocketPollParams *pollParams,
                       int *outError)
{
   SSLSock sslSock;

   /*
    * Create a new SSL socket object with the current socket
    */
   if (!(sslSock = SSL_New(fd, FALSE))) {
      if (outError) {
         *outError = ENOMEM;
      }
      LOG(0, (ASOCKPREFIX "failed to create SSL socket object\n"));
      return NULL;
   }

   return AsyncSocket_AttachToSSLSock(sslSock, pollParams, outError);
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_UseNodelay --
 *
 *      Sets or unset TCP_NODELAY on the socket, which disables or
 *      enables Nagle's algorithm, respectively.
 *
 * Results:
 *      ASOCKERR_SUCCESS on success, ASOCKERR_GENERIC otherwise.
 *
 * Side Effects:
 *      Increased bandwidth usage for short messages on this socket
 *      due to TCP overhead, in exchange for lower latency.
 *
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_UseNodelay(AsyncSocket *asock, Bool nodelay)
{
   int flag = nodelay ? 1 : 0;

   if (setsockopt(asock->fd, IPPROTO_TCP, TCP_NODELAY,
		  (const void *) &flag, sizeof(flag)) != 0) {
      LOG(0, (ASOCKPREFIX "could not set TCP_NODELAY, error %d: %s\n",
	      Err_Errno(), Err_ErrString()));
      return ASOCKERR_GENERIC;
   } else {
      return ASOCKERR_SUCCESS;
   }
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_Recv --
 *
 *      Registers a callback that will fire once the specified amount of data
 *      has been received on the socket.  This handles both TCP and UDP.
 *
 *      Data that was not retrieved at the last call of SSL_read() could still
 *      be buffered inside the SSL layer and will be retrieved on the next
 *      call to SSL_read(). However poll/select might not mark the socket as rea
 *      for reading since there might not be any data in the underlying network
 *      socket layer. Hence in the read callback, we keep spinning until all
 *      all the data buffered inside the SSL layer is retrieved before
 *      returning to the poll loop (See AsyncSocketFillRecvBuffer()).
 *
 *      However, we might not have come out of Poll in the first place, e.g.
 *      if this is the first call to AsyncSocket_Recv() after creating a new
 *      connection. In this situation, if there is buffered SSL data pending,
 *      we have to schedule an RTTime callback to force retrieval of the data.
 *      This could also happen if the client calls AsyncSocket_RecvBlocking,
 *      some data is left in the SSL layer, and the client then calls
 *      AsyncSocket_Recv. We use the inRecvLoop variable to detect and handle
 *      this condition, i.e., if inRecvLoop is FALSE, we need to schedule the
 *      RTime callback.
 *
 *      This can be used with either TCP or UDP sockets. 
 *
 *      TCP usage:
 *      AsyncSocket_Recv(AsyncSocket *asock,
 *                       void *buf,
 *                       int len,
 *                       AsyncSocketRecvFn recvFn,
 *                       void *clientData)
 *
 *      UDP usage:
 *      AsyncSocket_Recv(AsyncSocket *asock,
 *                       void *buf,
 *                       int len,
 *                       AsyncSocketRecvUDPFn recvFn,
 *                       void *clientData)
 *
 * Results:
 *      ASOCKERR_*.
 *
 * Side effects:
 *      Could register poll callback.
 *
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_Recv(AsyncSocket *asock,
                 void *buf,
                 int len, ...)
{
   va_list ap;
   AsyncSocketRecvFn recvFn = NULL;
   AsyncSocketRecvUDPFn recvUDPFn = NULL;
   void *clientData = NULL;

   if (!asock) {
      Warning(ASOCKPREFIX "Recv called with invalid arguments!\n");
      return ASOCKERR_INVAL;
   }

   if (!asock->errorFn) {
      ASOCKWARN(asock, ("%s: no registered error handler!\n", __FUNCTION__));
      return ASOCKERR_INVAL;
   }

   va_start(ap, len);

   if (SOCK_STREAM == asock->type) {
      recvFn = va_arg(ap, AsyncSocketRecvFn);
      clientData = va_arg(ap, void *);
   } else if (SOCK_DGRAM == asock->type) {
      recvUDPFn = va_arg(ap, AsyncSocketRecvUDPFn);
      clientData = va_arg(ap, void *);
   } else {
      /*
       * If this isn't TCP or UDP, it isn't supported.
       */
      va_end(ap);
      return ASOCKERR_INVAL;
   }

   va_end(ap);

   /*
    * XXX We might want to allow passing NULL for the recvFn, to indicate that
    *     the client is no longer interested in reading from the socket. This
    *     would be useful e.g. for HTTP, where the client sends a request and
    *     then the client->server half of the connection is closed.
    */
   if (!buf || (!recvFn && !recvUDPFn) || len <= 0) {
      Warning(ASOCKPREFIX "Recv called with invalid arguments!\n");
      return ASOCKERR_INVAL;
   }

   if (asock->state != AsyncSocketConnected) {
      ASOCKWARN(asock, ("recv called but state is not connected!\n"));
      return ASOCKERR_NOTCONNECTED;
   }

   if (!asock->recvBuf && !asock->recvCb) {
      VMwareStatus pollStatus;

      /*
       * Register the Poll callback
       */
      ASOCKLOG(3, asock, ("installing recv poll callback\n"));

      pollStatus = AsyncSocketPollAdd(asock, TRUE, POLL_FLAG_READ | POLL_FLAG_PERIODIC,
                      (SOCK_STREAM == asock->type) ? AsyncSocketRecvCallback :
                                                   AsyncSocketRecvUDPCallback);

      if (pollStatus != VMWARE_STATUS_SUCCESS) {
         ASOCKWARN(asock, ("failed to install recv callback!\n"));
         return ASOCKERR_POLL;
      }
      asock->recvCb = TRUE;
   }

   if (SOCK_STREAM == asock->type && SSL_Pending(asock->sslSock)
                                                    && !asock->inRecvLoop) {
      ASOCKLOG(0, asock, ("installing recv RTime poll callback\n"));
      if (Poll_CB_RTime(AsyncSocketRecvCallback, asock, 0, FALSE, NULL) !=
          VMWARE_STATUS_SUCCESS) {
         return ASOCKERR_POLL;
      }
   }

   asock->recvBuf = buf;
   asock->recvFn = recvFn;
   asock->recvUDPFn = recvUDPFn;
   asock->recvLen = len;
   asock->recvPos = 0;
   asock->clientData = clientData;

   return ASOCKERR_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketPoll --
 *
 *      Blocks on the specified socket until there's data pending or a
 *      timeout occurs.
 *
 * Results:
 *      ASOCKERR_SUCCESS if it worked, ASOCKERR_GENERIC on system call failures, and
 *      ASOCKERR_TIMEOUT if we just didn't receive enough data.
 *
 * Side effects:
 *      None.
 *----------------------------------------------------------------------------
 */

static int
AsyncSocketPoll(AsyncSocket *s, Bool read, int timeoutMS)
{
#ifndef _WIN32
   struct pollfd p;
   int retval;
#else
   /*
    * We use select() to do this on Windows, since there ain't no poll().
    * Fortunately, select() doesn't have the 1024 fd value limit.
    */
   int retval;
   struct timeval tv;
   struct fd_set rwfds;
   struct fd_set exceptfds;
#endif

   if (read && SSL_Pending(s->sslSock)) {
      return ASOCKERR_SUCCESS;
   }

   while (1) {

#ifndef _WIN32
      p.fd = s->fd;
      p.events = read ? POLLIN : POLLOUT;

      retval = poll(&p, 1, timeoutMS);
#else
      tv.tv_sec = timeoutMS / 1000;
      tv.tv_usec = (timeoutMS % 1000) * 1000;

      FD_ZERO(&rwfds);
      FD_ZERO(&exceptfds);
      FD_SET(s->fd, &rwfds);
      FD_SET(s->fd, &exceptfds);

      retval = select(1, read ? &rwfds : NULL, read ? NULL : &rwfds, &exceptfds,
                      timeoutMS >= 0 ? &tv : NULL);
#endif
      switch (retval) {

      case 1:
#ifndef _WIN32
         if (p.revents & (POLLERR | POLLNVAL))
#else
            if (FD_ISSET(s->fd, &exceptfds))
#endif
            {
               int sockErr = 0, sysErr, sockErrLen = sizeof sockErr;

               ASOCKLG0(s, ("AsyncSocketPoll on fd %d failed\n", s->fd));

               if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR,
                              (void *) &sockErr, (void *) &sockErrLen) == 0) {
                  if (sockErr) {
                     ASOCKLG0(s, ("getsockopt error lookup returned %d: %s\n",
                                  sockErr, Err_Errno2String(sockErr)));
                  }
               } else {
                  sysErr = ASOCK_LASTERROR();
                  ASOCKLG0(s, ("getsockopt failed with error %d: %s\n", sysErr, 
                               Err_Errno2String(sysErr)));
               }

               return ASOCKERR_GENERIC;
            }

         /*
          * One socket was ready, and it wasn't in an exception state, so
          * everything is ok. The socket is ready for reading/writing.
          */
         return ASOCKERR_SUCCESS;

      case 0:
         /*
          * No sockets were ready within the specified time.
          */
         return ASOCKERR_TIMEOUT;

      case -1:
         if (ASOCK_LASTERROR() == EINTR) {
            /*
             * We were somehow interrupted by signal. Let's loop and retry.
             */
            continue;
         }
         return ASOCKERR_GENERIC;
      default:
         NOT_REACHED();
      }
   }
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_RecvBlocking --
 * AsyncSocket_SendBlocking --
 *
 *      Implement "blocking + timeout" operations on the socket. These are
 *      simple wrappers around the AsyncSocketBlockingWork function, which
 *      operates on the actual non-blocking socket, using poll to determine
 *      when it's ok to keep reading/writing. If we can't finish within the
 *      specified time, we give up and return the ASOCKERR_TIMEOUT error.
 *
 * Results:
 *      ASOCKERR_SUCCESS if we finished the operation, ASOCKERR_* error codes
 *      otherwise.
 *
 * Side effects:
 *      Reads/writes the socket.
 *
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_RecvBlocking(AsyncSocket *s,
                         void *buf,
                         int len,
                         int *received,
                         int timeoutMS)
{
   return AsyncSocketBlockingWork(s, TRUE, buf, len, received, timeoutMS);
}

int
AsyncSocket_SendBlocking(AsyncSocket *s,
                         void *buf,
                         int len,
                         int *sent,
                         int timeoutMS)
{
   return AsyncSocketBlockingWork(s, FALSE, buf, len, sent, timeoutMS);
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketBlockingWork --
 *
 *      Try to complete the specified read/write operation within the
 *      specified time.
 *
 * Results:
 *      ASOCKERR_*.
 *
 * Side effects:
 *      None.
 *----------------------------------------------------------------------------
 */

int
AsyncSocketBlockingWork(AsyncSocket *s,
                        Bool read,
                        void *buf,
                        int len,
                        int *completed,
                        int timeoutMS)
{
   VmTimeType now, done;
   int sysErr;

   if (s == NULL || buf == NULL || len <= 0) {
      Warning(ASOCKPREFIX "Recv called with invalid arguments!\n");
      return ASOCKERR_INVAL;
   }

   if (s->state != AsyncSocketConnected) {
      ASOCKWARN(s, ("recv called but state is not connected!\n"));
      return ASOCKERR_NOTCONNECTED;
   }

   if (completed) {
      *completed = 0;
   }
   now = Hostinfo_SystemTimerUS() / 1000;
   done = now + timeoutMS;
   do {
      int numBytes, error;

      if ((error = AsyncSocketPoll(s, read, done - now)) != ASOCKERR_SUCCESS) {
         return error;
      }

      if ((numBytes = read ? SSL_Read(s->sslSock, buf, len)
                           : SSL_Write(s->sslSock, buf, len)) > 0) {
         if (completed) {
            *completed += numBytes;
         }
         len -= numBytes;
         if (len == 0) {
            return ASOCKERR_SUCCESS;
         }
         buf = (uint8*)buf + numBytes;
      } else if (numBytes == 0) {
         ASOCKLG0(s, ("blocking %s detected peer closed connection\n",
                      read ? "recv" : "send"));
         return ASOCKERR_REMOTE_DISCONNECT;
      } else if ((sysErr = ASOCK_LASTERROR()) != ASOCK_EWOULDBLOCK) {
         ASOCKWARN(s, ("blocking %s error %d: %s\n",
                       read ? "recv" : "send",
                       sysErr, Err_Errno2String(sysErr)));
         return ASOCKERR_GENERIC;
      }

      now = Hostinfo_SystemTimerUS() / 1000;
   } while ((now < done && timeoutMS > 0) || (timeoutMS < 0));

   return ASOCKERR_TIMEOUT;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_Send --
 *
 *      Queues the provided data for sending on the socket. If a send callback
 *      is provided, the callback is fired after the data has been written to
 *      the socket. Note that this only guarantees that the data has been
 *      copied to the transmit buffer, we make no promises about whether it
 *      has actually been transmitted, or received by the client, when the
 *      callback is fired.
 *
 *      Send callbacks should also be able to deal with being called if none
 *      or only some of the queued buffer has been transmitted, since the send
 *      callbacks for any remaining buffers are fired by AsyncSocket_Close().
 *      This condition can be detected by checking the len parameter passed to
 *      the send callback.
 *
 * Results:
 *      ASOCKERR_*.
 *
 * Side effects:
 *      May register poll callback or perform I/O.
 *
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_Send(AsyncSocket *asock,
                 void *buf,
                 int len,
                 AsyncSocketSendFn sendFn,
                 void *clientData)
{
   SendBufList *newBuf;

   /*
    * Note: I think it should be fine to send with a length of zero and a
    * buffer of NULL or any other garbage value.  However the code
    * downstream of here is unprepared for it (silently misbehaves).  Hence
    * the <= zero check instead of just a < zero check.  --Jeremy.
    */
   if (!asock || !buf || len <= 0) {
      Warning(ASOCKPREFIX "Send called with invalid arguments! asynchSock: %p "
              "buffer: %p length: %d\n", asock, buf, len);
      return ASOCKERR_INVAL;
   }

   ASSERT(SOCK_STREAM == asock->type);

   if (asock->state != AsyncSocketConnected) {
      ASOCKWARN(asock, ("send called but state is not connected!\n"));
      return ASOCKERR_NOTCONNECTED;
   }

   /*
    * If the send buffer list is currently empty, we schedule a one-time
    * callback to "prime" the output. This is necessary to support the
    * FD_WRITE network event semantic for sockets on Windows (see
    * WSAEventSelect documentation). The event won't signal unless a previous
    * write() on the socket failed with WSAEWOULDBLOCK, so we have to perform
    * at least one partial write before we can start polling for write.
    */
   if (!asock->sendBufList && !asock->sendCb) {
      if (AsyncSocketPollAdd(asock, FALSE, 0, AsyncSocketSendCallback, 0)
          != VMWARE_STATUS_SUCCESS) {
         return ASOCKERR_POLL;
      }
      asock->sendCb = TRUE;
   }

   /*
    * Allocate and initialize new send buffer entry
    */
   newBuf = Util_SafeCalloc(1, sizeof(SendBufList));
   newBuf->buf = buf;
   newBuf->len = len;
   newBuf->sendFn = sendFn;
   newBuf->clientData = clientData;

   /*
    * Append new send buffer to the tail of list.
    */
   *asock->sendBufTail = newBuf;
   asock->sendBufTail = &(newBuf->next);

   return ASOCKERR_SUCCESS;
}

/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_SendTo --
 *
 *      Sends a UDP Packet.
 *
 *      Usage:
 *      AsyncSocket_SendTo(asock, b, sizeof(b), ASOCKADDR_HOSTNAME,
                                                         "localhost", 8081);
 *      AsyncSocket_SendTo(asock, b, sizeof(b), ASOCKADDR_IPADDRESS,
                                                         uint32Ip, 8081);
 *      AsyncSocket_SendTo(asock, b, sizeof(b), ASOCKADDR_SOCKADDR,
                                                     &sockaddrInStruct,
 *                                                   sizeof(sockaddrInStruct));
 *
 *      Note: When used with type==HOSTNAME, this function may block while
 *      performing DNS resolution.
 *
 * Results:
 *      ASOCKERR_SUCCESS or ASOCKERR_GENERIC.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_SendTo(AsyncSocket *asock, void *buf, int len,
                   AsyncSocketSendToType type, ... )
{
   va_list ap;
   char *hostname;
   uint32 ip;
   int port;
   struct sockaddr_in addr;
   struct sockaddr_in *addrPointer = NULL;
   int getaddrinfoError;
   int sendToRet;
   int ret = ASOCKERR_GENERIC;
   int sockaddrSize = sizeof(struct sockaddr_in);

   ASSERT(asock);
   ASSERT(buf);
   ASSERT(SOCK_DGRAM == asock->type);

   va_start(ap, type);

   switch (type) {
   case ASOCKADDR_HOSTNAME:
      hostname = va_arg(ap, char *);
      port     = va_arg(ap, int);
      ASSERT( 0 < port && port < 65536);

      /*
       * Resolve the hostname.  Handles dotted decimal strings, too.
       */
      getaddrinfoError = AsyncSocketResolveAddr(hostname, port,
                                                asock->type, &addr);
      if (0 != getaddrinfoError) {
         goto bye;
      }
      break;

   case ASOCKADDR_IPADDRESS:
      ip   = va_arg(ap, uint32);
      port = va_arg(ap, int);
      ASSERT( 0 < port && port < 65536);

      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);
      addr.sin_addr.s_addr = htonl(ip);
      break;

   case ASOCKADDR_SOCKADDR:
      addrPointer = va_arg(ap, struct sockaddr_in *);
      addr = *addrPointer;
      sockaddrSize = va_arg(ap, socklen_t);
      break;

   default: 
      /*
       * Unsupported send type.
       */
      NOT_REACHED();
   }

   /*
    * If the socket is in non-blocking mode and the kernel does not
    * have enough resources to store/send this packet, sendto() could return
    * EAGAIN,EWOULDBLOCK on linux or WSAEWOULDBLOCK on Win32.
    * But this is UDP, an unreliable transport.  If that happens, just
    * drop the packet and let the application layer handle the
    * congestion control.
    */
   sendToRet = sendto(asock->fd, buf, len, 0, (struct sockaddr *) &addr,
                                                                sockaddrSize);
   ret = (-1 == sendToRet) ? ASOCKERR_GENERIC : ASOCKERR_SUCCESS;
   if (ASOCKERR_GENERIC == ret) {
      int sysErr = ASOCK_LASTERROR();
      Warning(ASOCKPREFIX "sendto() failed on socket with error %d: %s\n", 
      	      sysErr, Err_Errno2String(sysErr));
   }

bye:
   va_end(ap);

   return ret;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketResolveAddr --
 *
 *      Resolves a hostname and port.
 *
 * Results:
 *      Zero upon success.  This returns whatever getaddrinfo() returns.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */
int
AsyncSocketResolveAddr(const char *hostname,
                       unsigned short port,
                       int type,
                       struct sockaddr_in *addr)
{
   struct addrinfo hints;
   struct addrinfo *aiTop = NULL;
   struct addrinfo *aiIterator = NULL;
   int getaddrinfoError = 0;
   char portString[6]; /* strlen("65535\0") == 6 */

   ASSERT(NULL != addr);
   Str_Sprintf(portString, sizeof(portString), "%d", port);
   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = type;

   /*
    * We use getaddrinfo() since it is thread-safe and IPv6 ready.
    * gethostbyname() is not thread-safe, and gethostbyname_r() is not
    * defined on Windows.
    */
   getaddrinfoError = Posix_GetAddrInfo(hostname, portString, &hints, &aiTop);
   if (0 != getaddrinfoError) {
      Log(ASOCKPREFIX "getaddrinfo failed for host %s: %s\n", hostname,
                      gai_strerror(getaddrinfoError));
      goto bye;
   }
   for (aiIterator = aiTop; NULL != aiIterator ; aiIterator =
                                                       aiIterator->ai_next) {
      if (aiIterator->ai_family != AF_INET) {
         continue;
      }
      *addr = *((struct sockaddr_in *) (aiIterator->ai_addr));
      break;
   }

bye:
   if (NULL != aiTop) {
      freeaddrinfo(aiTop);
   }

   return getaddrinfoError;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketFillRecvBuffer --
 *
 *      Called when an asock has data ready to be read via the poll callback.
 *
 * Results:
 *      ASOCKERR_SUCCESS if everything worked, 
 *      ASOCKERR_REMOTE_DISCONNECT if peer closed connection gracefully,
 *      ASOCKERR_CLOSED if trying to read from a closed socket.
 *      ASOCKERR_GENERIC for other errors.
 *
 * Side effects:
 *      Reads data, could fire recv completion or trigger socket destruction.
 *
 *----------------------------------------------------------------------------
 */

static int
AsyncSocketFillRecvBuffer(AsyncSocket *s)
{
   int recvd;
   int needed;
   int sysErr = 0;
   int result;
   int pending = 0;
   VmTimeType drainStartTime = 0;

   ASSERT(s->state == AsyncSocketConnected);

   /*
    * When a socket has received all its desired content and FillRecvBuffer is
    * called again for the same socket, just return ASOCKERR_SUCCESS. The reason
    * we need this hack is that if a client which registered a receive callback
    * asynchronously later changes its mind to do it synchronously, (e.g. aioMgr
    * wait function), then FillRecvBuffer can be potentially be called twice for the
    * same receive event.
    */
   needed = s->recvLen - s->recvPos;
   if (!s->recvBuf && needed == 0) {
      return ASOCKERR_SUCCESS;
   }

   ASSERT(needed > 0);

   AsyncSocketAddRef(s);

   /*
    * See comment in AsyncSocket_Recv
    */
   s->inRecvLoop = TRUE;

   if (s->drainTimeoutUS) {
      drainStartTime = Hostinfo_SystemTimerUS();
   }

   do {
      Bool isDraining;

      /*
       * Try to read the remaining bytes to complete the current recv request.
       */
      recvd = SSL_Read(s->sslSock, (uint8 *) s->recvBuf + s->recvPos, needed);
      ASOCKLOG(3, s, ("need\t%d\trecv\t%d\tremain\t%d\n", needed, recvd,
		      needed - recvd));

      if (recvd > 0) {
	 s->sslConnected = TRUE;
	 if ((s->recvPos += recvd) == s->recvLen) {
	    void *recvBuf = s->recvBuf;
	    ASOCKLOG(3, s, ("recv buffer full, calling recvFn\n"));

            /*
             * We do this dance in case the handler frees the buffer (so
             * that there's no possible window where there are dangling
             * references here.  Obviously if the handler frees the buffer,
             * but them fails to register a new one, we'll put back the
             * dangling reference in the automatic reset case below, but
             * there's currently a limit to how far we go to shield clients
             * who use our API in a broken way.
             */

	    s->recvBuf = NULL;
	    s->recvFn(recvBuf, s->recvLen, s, s->clientData);
	    if (s->state == AsyncSocketClosed) {
	       ASOCKLG0(s, ("owner closed connection in recv callback\n"));
	       result = ASOCKERR_CLOSED;
	       goto exit;
	    } else if (s->recvLen - s->recvPos == 0) {
               /* Automatically reset keeping the current handler */
               s->recvPos = 0;
               s->recvBuf = recvBuf;
            }
	 }
      } else if (recvd == 0) {
	 ASOCKLG0(s, ("recv detected client closed connection\n"));
         /*
          * We treat this as an error so that the owner can detect closing
          * of connection by peer (via the error handler callback).
          */
	 result = ASOCKERR_REMOTE_DISCONNECT;
	 goto exit;
      } else if ((sysErr = ASOCK_LASTERROR()) == ASOCK_EWOULDBLOCK) {
	 ASOCKLOG(4, s, ("recv would block\n"));
	 break;
      } else {
	 ASOCKLG0(s, ("recv error %d: %s\n", sysErr, Err_Errno2String(sysErr)));
	 result = ASOCKERR_GENERIC;
	 goto exit;
      }

      /*
       * At this point, s->recvFoo have been updated to point to the
       * next chained Recv buffer. By default we're done at this
       * point, but we may want to continue if either:
       *
       *  (1) The SSL socket has data buffered in userspace already
       *      (SSL_Pending), or
       *
       *  (2) the caller has requested that we keep draining the
       *      input socket until either the data runs out or we
       *      run out of time to process it.
       *      (See AsyncSocket_SetDrainTimeout)
       */

      needed = s->recvLen - s->recvPos;
      isDraining = FALSE;
      ASSERT(needed > 0);

      if (s->drainTimeoutUS) {
         VmTimeType elapsed = Hostinfo_SystemTimerUS() - drainStartTime;
         ASOCKLOG(2, s, ("recv spent %d us draining, limit is %d us\n",
                         (int)elapsed, (int)s->drainTimeoutUS));
         if (elapsed <= s->drainTimeoutUS) {
            isDraining = TRUE;
         }
      }

      if (!isDraining) {
         /*
          * Not draining- read only data that's already buffered by
          * SSL in userspace.
          */

         pending = SSL_Pending(s->sslSock);
         needed = MIN(needed, pending);
      }

   } while (needed);

   /*
    * Reach this point only when previous SSL_Pending returns 0 or 
    * error is ASOCK_EWOULDBLOCK
    */
   ASSERT(pending == 0 || sysErr == ASOCK_EWOULDBLOCK);

   /*
    * Both a spurious wakeup and receiving any data even if it wasn't enough
    * to fire the callback are both success.  We were ready and now
    * presumably we aren't ready anymore.
    */
   result = ASOCKERR_SUCCESS;

exit:
   s->inRecvLoop = FALSE;
   AsyncSocketRelease(s);
   return result;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketWriteBuffers --
 *
 *      The meat of AsyncSocket's sending functionality.  This function
 *      actually writes to the wire assuming there's space in the buffers
 *      for the socket.
 *
 * Results:
 *      ASOCKERR_SUCESS if everything worked, else ASOCKERR_GENERIC.
 *
 * Side effects:
 *      None.
 *----------------------------------------------------------------------------
 */

static int
AsyncSocketWriteBuffers(AsyncSocket *s)
{
   int result;

   ASSERT(s);

   if (s->sendBufList == NULL) {
      return ASOCKERR_SUCCESS;     /* Vacuously true */
   }

   if (s->state != AsyncSocketConnected) {
      ASOCKWARN(s, ("write buffers on a disconnected socket (%d)!\n",
                    s->state));
      return ASOCKERR_GENERIC;
   }

   AsyncSocketAddRef(s);

   while (s->sendBufList && s->state == AsyncSocketConnected) {
      SendBufList *head = s->sendBufList;
      int error = 0;
      int sent = 0;
      int left = head->len - s->sendPos;

      sent = SSL_Write(s->sslSock,
                       (uint8 *) head->buf + s->sendPos, left);
      ASOCKLOG(3, s, ("left\t%d\tsent\t%d\tremain\t%d\n",
                      left, sent, left - sent));
      if (sent > 0) {
         s->sendBufFull = FALSE;
	 s->sslConnected = TRUE;
         if ((s->sendPos += sent) == head->len) {
            /*
             * We're done with the current buffer, so pop it off and nuke it.
             * We do the list management *first*, so that the list is in a
             * consistent state.
             */
            SendBufList tmp = *head;

            s->sendBufList = head->next;
            if (s->sendBufList == NULL) {
               s->sendBufTail = &(s->sendBufList);
            }
            s->sendPos = 0;
            free(head);

            if (tmp.sendFn) {
               /*
                * XXX
                * Firing the send completion could trigger the socket's
                * destruction (since the callback could turn around and call
                * AsyncSocket_Close()). Since we're in the middle of a loop on
                * the asock's queue, we avoid a use-after-free by deferring
                * the actual freeing of the asock structure. This is shady but
                * it works. --rrdharan
                */
               tmp.sendFn(tmp.buf, tmp.len, s, tmp.clientData);
            }
         }
      } else if (sent == 0) {
         ASOCKLG0(s, ("socket write() should never return 0.\n"));
         NOT_REACHED();
      } else if ((error = ASOCK_LASTERROR()) != ASOCK_EWOULDBLOCK) {
         ASOCKLG0(s, ("send error %d: %s\n", error, Err_Errno2String(error)));
         result = ASOCKERR_GENERIC;
         goto exit;
      } else {
         /*
          * Ran out of space to send. This is actually successful completion
          * (our contract obligates us to send as much data as space allows
          * and we fulfilled that).
          *
          * Indicate send buffer is full.
          */
         s->sendBufFull = TRUE;
         break;
      }
   }

   result = ASOCKERR_SUCCESS;

exit:
   AsyncSocketRelease(s);
   return result;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketAcceptInternal --
 *
 *      The meat of 'accept'.  This function can be invoked either via a
 *      poll callback or blocking. We call accept to get the new socket fd,
 *      create a new asock, and call the newFn callback previously supplied
 *      by the call to AsyncSocket_Listen.
 *
 * Results:
 *      ASOCKERR_SUCCESS if everything works, else an error code.
 *      ASOCKERR_GENERIC is returned to hide accept() system call's nitty-gritty,
 *        it implies that we should try accept() again and not 
 *        report error to client.
 *      ASOCKERR_ACCEPT to report accept operation's error to client.
 *
 * Side effects:
 *      Accepts on listening fd, creates new asock.
 *
 *----------------------------------------------------------------------------
 */

static int
AsyncSocketAcceptInternal(AsyncSocket *s)
{
   AsyncSocket *newsock;
   int sysErr;
   int fd;
   struct sockaddr remoteAddr;
   socklen_t remoteAddrLen = sizeof remoteAddr;

   ASSERT(s->state == AsyncSocketListening);

   if ((fd = accept(s->fd, &remoteAddr, &remoteAddrLen)) == -1) {
      sysErr = ASOCK_LASTERROR();
      if (sysErr == ASOCK_EWOULDBLOCK) {
         ASOCKWARN(s, ("spurious accept notification\n"));
         return ASOCKERR_GENERIC;
#ifndef _WIN32
         /*
          * This sucks. Linux accept() can return ECONNABORTED for connections
          * that closed before we got to actually call accept(), but Windows
          * just ignores this case. So we have to special case for Linux here.
          * We return ASOCKERR_GENERIC here because we still want to continue
          * accepting new connections.
          */
      } else if (sysErr == ECONNABORTED) {
         ASOCKLG0(s, ("accept: new connection was aborted\n"));
         return ASOCKERR_GENERIC;
#endif
      } else {
         ASOCKWARN(s, ("accept failed on fd %d, error %d: %s\n",
                       s->fd, sysErr, Err_Errno2String(sysErr)));
         return ASOCKERR_ACCEPT;
      }
   }

   if (!(newsock = AsyncSocket_AttachToFd(fd, &s->pollParams, NULL))) {
      return ASOCKERR_ACCEPT;
   }

   newsock->remoteAddr = remoteAddr;
   newsock->remoteAddrLen = remoteAddrLen;
   newsock->state = AsyncSocketConnected;

   s->connectFn(newsock, s->clientData);

   return ASOCKERR_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketConnectInternal --
 *
 *      The meat of connect.  This function is invoked either via a poll
 *      callback or the blocking API and verifies that connect() succeeded
 *      or reports is failure.  On success we call the registered 'new
 *      connection' function.
 *
 * Results:
 *      ASOCKERR_SUCCESS if it all worked out or ASOCKERR_GENERIC.
 *
 * Side effects:
 *      Creates new asock, fires newFn callback.
 *
 *----------------------------------------------------------------------------
 */

static int
AsyncSocketConnectInternal(AsyncSocket *s)
{
   int optval = 0, optlen = sizeof optval, sysErr;

   ASSERT(s->state == AsyncSocketConnecting);

   if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR,
		  (void *) &optval, (void *)&optlen) != 0) {
      sysErr = ASOCK_LASTERROR();
      Warning(ASOCKPREFIX "getsockopt for connect on fd %d failed with " 
      	      "error %d : %s\n", s->fd, sysErr, Err_Errno2String(sysErr));
      return ASOCKERR_GENERIC;
   }

   if (optval != 0) {
      Warning(ASOCKPREFIX "SO_ERROR for connect on fd %d: %s\n",
              s->fd, Err_Errno2String(optval));
      return ASOCKERR_GENERIC;
   }

   s->state = AsyncSocketConnected;
   s->connectFn(s, s->clientData);
   return ASOCKERR_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_WaitForConnection --
 *
 *      Spins a socket currently listening or connecting until the
 *      connection completes or the allowed time elapses.
 *
 * Results:
 *      ASOCKERR_SUCCESS if it worked, ASOCKERR_GENERIC on failures, and
 *      ASOCKERR_TIMEOUT if nothing happened in the allotted time.
 *
 * Side effects:
 *      None.
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_WaitForConnection(AsyncSocket *s, int timeoutMS)
{
   Bool read;
   int error;
   VmTimeType now, done;

   if (s->state == AsyncSocketConnected) {
      return ASOCKERR_SUCCESS;
   }

   if (s->state != AsyncSocketListening && s->state != AsyncSocketConnecting) {
      return ASOCKERR_GENERIC;
   }

   read = s->state == AsyncSocketListening;

   now = Hostinfo_SystemTimerUS() / 1000;
   done = now + timeoutMS;

   do {
      if ((error = AsyncSocketPoll(s, read, done - now)) != ASOCKERR_SUCCESS) {
         ASOCKWARN(s, ("wait for connection failed\n"));
         return error;
      }

      now = Hostinfo_SystemTimerUS() / 1000;

      if (read) {
         if (AsyncSocketAcceptInternal(s) != ASOCKERR_SUCCESS) {
            ASOCKWARN(s, ("wait for connection: accept failed\n"));
            /*
             * Just fall through, we'll loop and try again as long as we still
             * have time remaining.
             */
         } else {
            return ASOCKERR_SUCCESS;
         }
      } else {
         /*
          * A nuisance.  ConnectCallback() is either registered as a device or
          * rtime callback depending on the prior return value of connect().
          * So we try to remove it from both.
          */
         Bool removed = FALSE;
         removed = AsyncSocketPollRemove(s, TRUE, POLL_FLAG_WRITE,
                                         AsyncSocketConnectCallback)
            || AsyncSocketPollRemove(s, FALSE, 0, AsyncSocketConnectCallback);
         ASSERT(removed);
         return AsyncSocketConnectInternal(s);
      }

   } while ((now < done && timeoutMS > 0) || (timeoutMS < 0));

   return ASOCKERR_TIMEOUT;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_DoOneMsg --
 *
 *      Spins a socket until the specified amount of time has elapsed or
 *      data has arrived / been sent.
 *
 * Results:
 *      ASOCKERR_SUCCESS if it worked, ASOCKERR_GENERIC on system call failures, and
 *      ASOCKERR_TIMEOUT if nothing happened in the allotted time.
 *
 * Side effects:
 *      None.
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_DoOneMsg(AsyncSocket *s, Bool read, int timeoutMS)
{
   int retVal;

   if (read) {
      VMwareStatus pollStatus;

      /*
       * Bug 158571: There could other threads polling on the same asyncsocket
       * If two threads land up polling  on the same socket at the same time,
       * the first thread to be scheduled reads the data from the socket,
       * while the second one blocks infinitely. This hangs the VM. To prevent
       * this, we temporarily remove the poll callback and then reinstate it
       * after reading the data.
       */
      Bool removed;

      ASSERT(s->state == AsyncSocketConnected);
      ASSERT(s->recvCb); /* We are supposed to call someone... */
      AsyncSocketAddRef(s);
      removed = AsyncSocketPollRemove(s, TRUE,
                                      POLL_FLAG_READ | POLL_FLAG_PERIODIC,
                                      (SOCK_STREAM == s->type) ?
                                      AsyncSocketRecvCallback :
                                      AsyncSocketRecvUDPCallback);
      ASSERT(removed);

      if ((retVal = AsyncSocketPoll(s, read, timeoutMS)) != ASOCKERR_SUCCESS) {
         if (retVal == ASOCKERR_GENERIC) {
            ASOCKWARN(s, ("DoOneMsg: failed to poll on the socket during read.\n"));
         }
      } else {
         retVal = AsyncSocketFillRecvBuffer(s);
      }

      /*
       * If socket got closed in AsyncSocketFillRecvBuffer, we cannot add poll
       * callback - AsyncSocket_Close() would remove it if we would not remove
       * it above.
       */
      if (s->state != AsyncSocketClosed) {
         ASSERT(s->refCount > 1); /* We should not be last user of socket */
         ASSERT(s->state == AsyncSocketConnected);
         ASSERT(s->recvCb); /* Still interested in callback. */
         pollStatus = AsyncSocketPollAdd(s, TRUE, POLL_FLAG_READ | POLL_FLAG_PERIODIC,
                                         (SOCK_STREAM == s->type) ? AsyncSocketRecvCallback :
                                         AsyncSocketRecvUDPCallback);

         if (pollStatus != VMWARE_STATUS_SUCCESS) {
            ASOCKWARN(s, ("failed to install recv callback!\n"));
            AsyncSocketRelease(s);
            return ASOCKERR_POLL;
         }
      }
      /* This may destroy socket s if it is in AsyncSocketClosed state now. */
      AsyncSocketRelease(s);
   } else {
      if ((retVal = AsyncSocketPoll(s, read, timeoutMS)) != ASOCKERR_SUCCESS) {
         if (retVal == ASOCKERR_GENERIC) {
            ASOCKWARN(s, ("DoOneMsg: failed to poll on the socket during write.\n"));
         }
      } else {
         retVal = AsyncSocketWriteBuffers(s);
      }
   }
   return retVal;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_Flush --
 *
 *      Try to send any pending out buffers until we run out of buffers, or
 *      the timeout expires.
 *
 * Results:
 *      ASOCKERR_SUCCESS if it worked, ASOCKERR_GENERIC on system call failures, and
 *      ASOCKERR_TIMEOUT if we couldn't send enough data before the timeout
 *      expired.
 *
 * Side effects:
 *      None.
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_Flush(AsyncSocket *s, int timeoutMS)
{
   VmTimeType now, done;

   if (s == NULL) {
      Warning(ASOCKPREFIX "Flush called with invalid arguments!\n");
      return ASOCKERR_INVAL;
   }

   if (s->state != AsyncSocketConnected) {
      ASOCKWARN(s, ("flush called but state is not connected!\n"));
      return ASOCKERR_INVAL;
   }

   now = Hostinfo_SystemTimerUS() / 1000;
   done = now + timeoutMS;

   while (s->sendBufList) {
      int error;

      if ((error = AsyncSocketPoll(s, FALSE, done - now)) != ASOCKERR_SUCCESS) {
         ASOCKWARN(s, ("flush failed\n"));
         return error;
      }

      if ((error = AsyncSocketWriteBuffers(s)) != ASOCKERR_SUCCESS) {
         return error;
      }
      ASSERT(s->state == AsyncSocketConnected);

      /* Setting timeoutMS to -1 means never timeout. */
      if (timeoutMS >= 0) {
         now = Hostinfo_SystemTimerUS() / 1000;

         /* Don't timeout if you've sent everything */
         if (now > done && s->sendBufList) {
            ASOCKWARN(s, ("flush timed out\n"));
            return ASOCKERR_TIMEOUT;
         }
      }
   }

   return ASOCKERR_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_SetErrorFn --
 *
 *      Sets the error handling function for the asock. The error function
 *      is invoked automatically on I/O errors. Passing NULL as the error
 *      function restores the default behavior, which is to just destroy the
 *      AsyncSocket on any errors.
 *
 * Results:
 *      ASOCKERR_SUCCESS or ASOCKERR_INVAL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_SetErrorFn(AsyncSocket *asock,           // IN/OUT
		       AsyncSocketErrorFn errorFn,   // IN
                       void *clientData)             // IN
{
   if (!asock) {
      Warning(ASOCKPREFIX "SetErrorFn called with invalid arguments!\n");
      return ASOCKERR_INVAL;
   }

   asock->errorFn = errorFn;
   asock->errorClientData = clientData;
   return ASOCKERR_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_SetDrainTimeout --
 *
 *      Sets the maximum number of microseconds for which we'll
 *      process chained AsyncSocket_Recv() requests on a single socket
 *      before returning to the main Poll loop. If the argument is
 *      zero, we never process chained Recv requests.
 *
 *      After receiving data from a socket with a nonzero drain timeout,
 *      we'll continue trying to receive data as long as:
 *
 *        1. More AsyncSocket_Recv requests are pending,
 *        2. Data is available on the socket,
 *        3. and the timeout has not yet been exhausted.
 *
 *      This function can dramatically increase performance when the
 *      caller is using many tiny chained Recv requests and/or the
 *      thread's Poll loop is full of CPU-hungry callbacks. The
 *      timeout is a safeguard against a single busy socket DoS'ing
 *      the whole thread.
 *
 * Results:
 *      ASOCKERR_SUCCESS or ASOCKERR_INVAL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_SetDrainTimeout(AsyncSocket *asock,    // IN/OUT
                            VmTimeType timeoutUS)  // IN
{
   if (!asock) {
      /*
       * This seems fairly useless, but we'll be consistent with
       * syncSocket_SetErrorFn for now...
       */
      Warning(ASOCKPREFIX "SetDrainTimeout called with invalid arguments!\n");
      return ASOCKERR_INVAL;
   }

   asock->drainTimeoutUS = timeoutUS;

   return ASOCKERR_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_Close --
 *
 *      AsyncSocket destructor. The destructor should be safe to call at any
 *      time.  It's invoked automatically for I/O errors on slots that have no
 *      error handler set, and should be called manually by the error handler
 *      as necessary. It could also be called as part of the normal program
 *      flow.
 *
 * Results:
 *      ASOCKERR_*.
 *
 * Side effects:
 *      Closes the socket fd, unregisters all Poll callbacks, and fires the
 *      send triggers for any remaining output buffers.
 *
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_Close(AsyncSocket *asock)
{
   Bool removed;
   AsyncSocketState oldState;

   if (!asock) {
      return ASOCKERR_INVAL;
   }

   if (asock->state == AsyncSocketClosed) {
      Warning("AsyncSocket_Close() called on already closed asock!\n");
      return ASOCKERR_CLOSED;
   }

   /*
    * Set the new state to closed, and then check the old state and do the
    * right thing accordingly
    */
   ASOCKLOG(1, asock, ("closing socket\n"));
   oldState = asock->state;
   asock->state = AsyncSocketClosed;

   switch(oldState) {
   case AsyncSocketListening:
      ASOCKLOG(1, asock, ("old state was listening, removing accept callback\n"));
      removed = AsyncSocketPollRemove(asock, TRUE, POLL_FLAG_READ | POLL_FLAG_PERIODIC,
                                      AsyncSocketAcceptCallback);
      ASSERT(removed);
      break;

   case AsyncSocketConnecting:
      ASOCKLOG(1, asock, ("old state was connecting, removing connect callback\n"));
      removed = AsyncSocketPollRemove(asock, TRUE, POLL_FLAG_WRITE,
                                      AsyncSocketConnectCallback);
      if (!removed) {
         ASOCKLOG(1, asock, ("connect callback is not present in the poll list.\n"));
      }
      break;

   case AsyncSocketConnected:
      ASOCKLOG(1, asock, ("old state was connected\n"));

      /*
       * Remove the read and write poll callbacks.
       *
       * We could fire the current recv completion callback here, but in
       * practice clients won't want to know about partial reads since it just
       * complicates the common case (i.e. every read callback would need to
       * check the len parameter).
       *
       * For writes, however, we *do* fire all of the callbacks. The argument
       * here is that the common case for writes is "fire and forget", e.g.
       * send this buffer and free it. Firing the triggers at close time
       * simplifies client code, since the clients aren't forced to keep track
       * of send buffers themselves. Clients can figure out how much data was
       * actually transmitted (if they care) by checking the len parameter
       * passed to the send callback.
       *
       * A modification suggested by Jeremy is to pass a list of unsent
       * buffers and their completion callbacks to the error handler if one is
       * registered, and only fire the callbacks here if there was no error
       * handler invoked.
       */
      ASSERT(!asock->recvBuf || asock->recvCb);
      if (asock->recvCb) {
         ASOCKLOG(1, asock, ("recvCb is non-NULL, removing recv callback\n"));
         removed = AsyncSocketPollRemove(asock, TRUE,
                                         POLL_FLAG_READ | POLL_FLAG_PERIODIC,
                                         (SOCK_STREAM == asock->type) ?
                                              AsyncSocketRecvCallback :
                                              AsyncSocketRecvUDPCallback);

         /*
          * Callback might be temporarily removed in AsyncSocket_DoOneMsg.
          */
         ASSERT_NOT_TESTED(removed);

         /*
          * We may still have the RTime callback, try to remove if it exists
          */
         removed = Poll_CB_RTimeRemove(AsyncSocketRecvCallback, asock, FALSE);
      }

      if (asock->sendCb) {
	 ASOCKLOG(1, asock, ("sendBufList is non-NULL, removing send callback\n"));

         /*
          * The send callback could be either a device or RTime callback, so
          * we check the latter if it wasn't the former.
          */
         removed = AsyncSocketPollRemove(asock, TRUE, POLL_FLAG_WRITE,
                                         AsyncSocketSendCallback)
            || AsyncSocketPollRemove(asock, FALSE, 0, AsyncSocketSendCallback);
         ASSERT(removed);
         asock->sendCb = FALSE;
      }

      while (asock->sendBufList) {
         /*
          * Pop each remaining buffer and fire its completion callback.
          */
         SendBufList *cur = asock->sendBufList;
         if (asock->sendBufList->sendFn) {
            asock->sendBufList->sendFn(asock->sendBufList->buf,
                                       asock->sendPos, asock,
                                       asock->sendBufList->clientData);
         }
         asock->sendBufList = asock->sendBufList->next;
         asock->sendPos = 0;
         free(cur);
      }

      break;

   default:
      NOT_REACHED();
   }

#ifdef __APPLE_READ_BUG_WORKAROUND__
   if (asock->readPausedForSocketBug) {
      removed = AsyncSocketPollRemove(asock, 
                                      FALSE, 
                                      0,
                                      AsyncSocketRetryReadCallback);
   }
#endif

   SSL_Shutdown(asock->sslSock);
   ASOCK_CLOSEFD(asock->fd);

   AsyncSocketRelease(asock);
   return ASOCKERR_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_GetState --
 *
 *      Returns the state of the provided asock or ASOCKERR_INVAL.
 *
 * Results:
 *      AsyncSocketState enum.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

AsyncSocketState
AsyncSocket_GetState(AsyncSocket *asock)
{
   return (asock ? asock->state : ASOCKERR_INVAL);
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_IsSendBufferFull --
 *
 *      Indicate if socket send buffer is full.
 *
 * Results:
 *      0: send space probably available,
 *      1: send has reached maximum,
 *      ASOCKERR_GENERIC: null socket.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_IsSendBufferFull(AsyncSocket *asock)
{
   return (asock ? asock->sendBufFull : ASOCKERR_GENERIC);
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocket_GetID --
 *
 *      Returns a unique identifier for the asock.
 *
 * Results:
 *      Integer id or ASOCKERR_INVAL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

int
AsyncSocket_GetID(AsyncSocket *asock)
{
   return (asock ? asock->id : ASOCKERR_INVAL);
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketMakeNonBlocking --
 *
 *      Make the specified socket non-blocking if it isn't already.
 *
 * Results:
 *      ASOCKERR_SUCCESS if the operation succeeded, ASOCKERR_GENERIC otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

static int
AsyncSocketMakeNonBlocking(int fd)
{
#ifdef _WIN32
   int retval;
   u_long argp = 1; /* non-zero => enable non-blocking mode */

   retval = ioctlsocket(fd, FIONBIO, &argp);

   if (retval != 0) {
      ASSERT(retval == SOCKET_ERROR);
      return ASOCKERR_GENERIC;
   }

#else
   int flags;

   if ((flags = fcntl(fd, F_GETFL)) < 0) {
      return ASOCKERR_GENERIC;
   }

   if (!(flags & O_NONBLOCK) && (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0))
   {
      return ASOCKERR_GENERIC;
   }
#endif
   return ASOCKERR_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketHandleError --
 *
 *      Internal error handling helper. Changes the socket's state to error,
 *      and calls the registered error handler or closes the socket.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Lots.
 *
 *----------------------------------------------------------------------------
 */

static void
AsyncSocketHandleError(AsyncSocket *asock, int asockErr)
{
   ASSERT(asock);
   if (asock->errorFn) {
      ASOCKLOG(3, asock, ("firing error callback\n"));
      asock->errorFn(asockErr, asock, asock->errorClientData);
   } else {
      ASOCKLOG(3, asock, ("no error callback, closing socket\n"));
      AsyncSocket_Close(asock);
   }
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketAcceptCallback --
 *
 *      Poll callback for listening fd waiting to complete an accept
 *      operation. We call accept to get the new socket fd, create a new
 *      asock, and call the newFn callback previously supplied by the call to
 *      AsyncSocket_Listen.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Accepts on listening fd, creates new asock.
 *
 *----------------------------------------------------------------------------
 */

static void
AsyncSocketAcceptCallback(void *clientData)
{
   AsyncSocket *asock = (AsyncSocket *) clientData;
   int retval;

   retval = AsyncSocketAcceptInternal(asock);
   /*
    * See comment for return value of AsyncSocketAcceptInternal().
    */
   if (retval == ASOCKERR_ACCEPT) {
      AsyncSocketHandleError(asock, retval);
   }
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketConnectCallback --
 *
 *      Poll callback for connecting fd. Calls through to
 *      AsyncSocketConnectInternal to do the real work.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates new asock, fires newFn callback.
 *
 *----------------------------------------------------------------------------
 */

static void
AsyncSocketConnectCallback(void *clientData)
{
   AsyncSocket *asock = (AsyncSocket *) clientData;
   int retval;

   ASSERT(asock);
   retval = AsyncSocketConnectInternal(asock);
   if (retval != ASOCKERR_SUCCESS) {
      ASSERT(retval == ASOCKERR_GENERIC); /* Only one we're expecting */
      AsyncSocketHandleError(asock, retval);
   }
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketRecvCallback --
 *
 *      Poll callback for input waiting on the socket. We try to pull off the
 *      remaining data requested by the current receive function.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Reads data, could fire recv completion or trigger socket destruction.
 *
 *----------------------------------------------------------------------------
 */

static void
AsyncSocketRecvCallback(void *clientData)
{
   AsyncSocket *asock = (AsyncSocket *) clientData;
   int error;

   ASSERT(asock);

   AsyncSocketAddRef(asock);

   error = AsyncSocketFillRecvBuffer(asock);
   if (error == ASOCKERR_GENERIC || error == ASOCKERR_REMOTE_DISCONNECT) {
      AsyncSocketHandleError(asock, error);
   }

   AsyncSocketRelease(asock);
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketRecvUDPCallback --
 *
 *      Retrieve the UDP packet and fire a callback with it.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Reads data, fires recv completion.
 *
 *----------------------------------------------------------------------------
 */

static void
AsyncSocketRecvUDPCallback(void *clientData)
{
   AsyncSocket *asock = (AsyncSocket *) clientData;
   struct sockaddr_in clientAddr;
   int clientAddrLen = sizeof(clientAddr);
   int actualPacketLength = 0;

   ASSERT(asock);

   /*
    * TODO: It would be useful if we also get the destination address
    * and pass that to the callback.  This way, if the socket is
    * bound to multiple interfaces, we know which interface the
    * packet came in from.  getsockname() doesn't appear to work here.
    * Instead, recvmsg() with the IP_PKTINFO socket option enabled
    * appears to be the right thing to do.  (Use WSARecvMsg on Win32)
    */

   AsyncSocketAddRef(asock);
   actualPacketLength = recvfrom(asock->fd, asock->recvBuf, asock->recvLen, 0,
                             (struct sockaddr *) &clientAddr, &clientAddrLen);

   if (-1 == actualPacketLength) {
      AsyncSocketHandleError(asock, ASOCKERR_GENERIC);
      goto exit;
   }

   asock->recvUDPFn(asock->recvBuf, actualPacketLength, asock,
                    asock->clientData, &clientAddr, clientAddrLen);

exit:
   AsyncSocketRelease(asock);
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketSendCallback --
 *
 *      Poll callback for output socket buffer space available (socket is
 *      writable). We iterate over all the remaining buffers in our queue,
 *      writing as much as we can until we fill the socket buffer again. If we
 *      don't finish, we register ourselves as a device write callback.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Writes data, could trigger write completion or socket destruction.
 *
 *----------------------------------------------------------------------------
 */

static void
AsyncSocketSendCallback(void *clientData)
{
   AsyncSocket *s = (AsyncSocket *) clientData;
   int retval;

   ASSERT(s);
   s->sendCb = FALSE; /* AsyncSocketSendCallback is never periodic */
   retval = AsyncSocketWriteBuffers(s);
   if (retval != ASOCKERR_SUCCESS) {
      AsyncSocketHandleError(s, retval);
   } else if (s->sendBufList && !s->sendCb) {
      VMwareStatus pollStatus;
      
      /*
       * We didn't finish, so we need to reschedule the Poll callback (the
       * write callback is *not* periodic).
       */

#ifdef _WIN32
      /*
       * If any data has been sent out or read in from the sslSock, 
       * SSL has finished the handshaking. Otherwise, 
       * we have to schedule a realtime callback for write. See bug 37147
       */
      if (!s->sslConnected) {
	 pollStatus = AsyncSocketPollAdd(s, FALSE, 0, 
					 AsyncSocketSendCallback, 100000);
	 ASSERT_NOT_IMPLEMENTED(pollStatus == VMWARE_STATUS_SUCCESS);
      } else 
#endif
      {
	 pollStatus = AsyncSocketPollAdd(s, TRUE, POLL_FLAG_WRITE,
		                         AsyncSocketSendCallback);
	 ASSERT_NOT_IMPLEMENTED(pollStatus == VMWARE_STATUS_SUCCESS);
      }
      s->sendCb = TRUE;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * AsyncSocketAddRef --
 *
 *    Increments reference count on AsyncSocket struct.
 *
 * Results:
 *    New reference count.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

int
AsyncSocketAddRef(AsyncSocket *s)
{
   ASSERT(s && s->refCount > 0);
   ASOCKLOG(1, s, ("AddRef (count now %d)\n", s->refCount + 1));
   return ++s->refCount;
}


/*
 *-----------------------------------------------------------------------------
 *
 * AsyncSocketRelease --
 *
 *    Decrements reference count on AsyncSocket struct, freeing it when it
 *    reaches 0.
 *
 * Results:
 *    New reference count; 0 if freed.
 *
 * Side effects:
 *    May free struct.
 *
 *-----------------------------------------------------------------------------
 */

int
AsyncSocketRelease(AsyncSocket *s)
{
   if (0 == --s->refCount) {
      ASOCKLOG(1, s, ("Final release; freeing asock struct\n"));
      free(s);
      return 0;
   }
   ASOCKLOG(1, s, ("Release (count now %d)\n", s->refCount));
   return s->refCount;
}


/*
 *-----------------------------------------------------------------------------
 *
 * AsyncSocketPollAdd --
 *
 *    Add a poll callback.  Wrapper for Poll_Callback since we always call
 *    it in one of two basic forms.
 *    
 *    If socket is FALSE, user has to pass in the timeout value
 *
 * Results:
 *    VMwareStatus result code from Poll_Callback
 *
 * Side effects:
 *    Only the obvious.
 *
 *-----------------------------------------------------------------------------
 */

VMwareStatus
AsyncSocketPollAdd(AsyncSocket *asock,
                   Bool socket,
                   int flags,
                   PollerFunction callback,
		   ...)
{
   int type, info;

   if (socket) {
      type = POLL_DEVICE;
      flags |= POLL_FLAG_SOCKET;
      info = asock->fd;
   } else {
      va_list marker;
      va_start(marker, callback);

      type = POLL_REALTIME;
      info = va_arg(marker, int);
      
      va_end(marker);
   }

   return Poll_Callback(asock->pollParams.pollClass,
                        flags | asock->pollParams.flags,
                        callback, asock, type, info,
                        asock->pollParams.lock);
}


/*
 *-----------------------------------------------------------------------------
 *
 * AsyncSocketPollRemove --
 *
 *    Remove a poll callback.  Wrapper for Poll_CallbackRemove since we
 *    always call it in one of two basic forms.
 *
 * Results:
 *    TRUE if removed, FALSE if not found.
 *
 * Side effects:
 *    Only the obvious.
 *
 *-----------------------------------------------------------------------------
 */

Bool
AsyncSocketPollRemove(AsyncSocket *asock,
                      Bool socket,
                      int flags,
                      PollerFunction callback)
{
   int type;

   if (socket) {
      type = POLL_DEVICE;
      flags |= POLL_FLAG_SOCKET;
   } else {
      type = POLL_REALTIME;
   }
      
   return Poll_CallbackRemove(asock->pollParams.pollClass,
                              flags | asock->pollParams.flags,
                              callback, asock, type);
}


/*
 *-----------------------------------------------------------------------------
 *
 * AsyncSocket_CancelRecv --
 *
 *    Call this function if you know what you are doing. This should be 
 *    called if you want to synchronously receive the outstanding data on 
 *    the socket. It removes the recv poll callback for both tcp/udp sockets.
 *    For tcp socket it also returns number of partially read bytes (if any). 
 *    A partially read response may exist as AsyncSocketRecvCallback calls 
 *    the recv callback only when all the data has been received.
 *
 * Results:
 *    ASOCKERR_SUCCESS or ASOCKERR_INVAL.
 *
 * Side effects:
 *    Subsequent client call to AsyncSocket_Recv can reinstate async behaviour.
 *
 *-----------------------------------------------------------------------------
 */

int
AsyncSocket_CancelRecv(AsyncSocket *asock,         // IN
                       int *partialRecvd,          // OUT
                       void **recvBuf,             // OUT
                       void **recvFn)              // OUT
{
   Bool isTcp;
   if (!asock) {
      Warning(ASOCKPREFIX "Invalid socket while cancelling recv request!\n");
      return ASOCKERR_INVAL;
   }

   if (asock->state != AsyncSocketConnected) {
      Warning(ASOCKPREFIX "Failed to cancel request on disconnected socket!\n");
      return ASOCKERR_INVAL;
   }

   isTcp = SOCK_STREAM == asock->type;

   if (isTcp && (asock->sendBufList || asock->sendCb)) {
      Warning(ASOCKPREFIX "Can't cancel request as socket has send operation "
      	      "pending.\n");
      return ASOCKERR_INVAL;
   }

   if (asock->recvCb) {
      Bool removed;
      ASOCKLOG(1, asock, ("Removing poll recv callback while cancelling recv.\n"));
      /*
       * TODO: Maybe factor out this conditional to select between AsyncSocketRecvCallback
       * and AsyncSocketRecvUDPCallback.
       */
      removed = AsyncSocketPollRemove(asock, TRUE,
                                      POLL_FLAG_READ | POLL_FLAG_PERIODIC,
                                      isTcp ? AsyncSocketRecvCallback :
                                      AsyncSocketRecvUDPCallback);
      ASSERT_NOT_IMPLEMENTED(removed);
      asock->recvCb = FALSE;
      if (isTcp && partialRecvd && asock->recvLen > 0) {
         ASOCKLOG(1, asock, ("Partially read %d bytes out of %d bytes while "
         	             "cancelling recv request.\n", asock->recvPos, asock->recvLen));
         *partialRecvd = asock->recvPos;
      }
      if (recvFn) {
      	 if (isTcp) {
            *recvFn = asock->recvFn;
	 } else {
            *recvFn = asock->recvUDPFn;
	 }
      }
      if (recvBuf) {
         *recvBuf = asock->recvBuf;
      }
      asock->recvBuf = NULL;
      if (isTcp) {
         asock->recvFn = NULL;
         asock->recvPos = 0;
      } else {
         asock->recvUDPFn = NULL;
      }
      asock->recvLen = 0;
   }
   return ASOCKERR_SUCCESS;
}


#ifdef __APPLE_READ_BUG_WORKAROUND__
/*
 *-----------------------------------------------------------------------------
 *
 * AsyncSocket_HandleSSLError --
 *
 *    There is a bug on Mac OS 10.4.x where read(2) can return zero
 *    even if the other end of the socket is not disconnected. When that
 *    happens, the SSL library will call this function.
 *
 *    One workaround is to let the caller function deal with this, such
 *    as to remove the socket from poll for a little while. This doesn't
 *    really fix the problem, but it removes the socket from Poll so we
 *    don't spin on it. The caller is responsible for adding the socket
 *    back to the Poll list in some time. That means we don't see any
 *    activity on the socket while it is off poll, so the timeout should 
 *    be short. During that timeout, we miss any event on this socket like
 *    new data or actually closing the socket.
 *
 *    Really, this is still just spinning on the socket, but it spins
 *    more slowly. So, we tradeoff some responsiveness (we don't see socket
 *    events promptly) for less performance impact on the overall system
 *    by processing fewer false events on the socket.
 *
 * Results:
 *    TRUE if SSL library should ignore the error.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

Bool
AsyncSocket_HandleSSLError(SSLSock sslSock,   // IN
                           void *context)     // IN
{
   AsyncSocket *asock = (AsyncSocket *) context;
   int cancelResult;
   VMwareStatus pollStatus;

   if (NULL == asock) {
      ASOCKLOG(0, asock, ("AsyncSocket_HandleSSLError passed a NULL socket\n"));
      return FALSE;
   }

   if (asock->state == AsyncSocketClosed) {
      ASOCKLOG(0, asock, ("AsyncSocket_HandleSSLError finds the socket is AsyncSocketClosed\n"));
      return FALSE;
   }

   /*
    * If we are already planning on retrying the read at a later time, then
    * do nothing. Return TRUE to tell the caller (the SSL code) to ignore the
    * current error.
    */
   if (asock->readPausedForSocketBug) {
      ASOCKLOG(0, asock, ("AsyncSocket_HandleSSLError passed a socket that is already paused\n"));
      return TRUE;
   }
   ASOCKLOG(3, asock, ("AsyncSocket_HandleSSLError receives an SSL error\n"));

   /*
    * One socket may have a lot of Poll callbacks. However, it seems to
    * be only read callbacks that incorrectly return when we hit this Apple
    * bug.
    *
    * For now, only remove the read callback. That makes restoring the
    * state simpler.
    */
   asock->savedRecvLen = asock->recvLen;
   cancelResult = AsyncSocket_CancelRecv(asock, 
                                         &(asock->savedRecvPos), 
                                         &(asock->savedRecvBuf), 
                                         &(asock->savedRecvFunction));
   if (ASOCKERR_SUCCESS != cancelResult) {
      ASOCKLOG(0, asock, ("AsyncSocket_HandleSSLError. AsyncSocket_CancelRecv failed\n"));
      return FALSE;
   }

   AsyncSocketAddRef(asock);
   pollStatus = AsyncSocketPollAdd(asock, 
                                   FALSE, 
                                   0, 
                                   AsyncSocketRetryReadCallback, 
                                   REMOVE_FROM_POLL_PERIOD_IN_MILLISECS * 1000);
   if (VMWARE_STATUS_SUCCESS != pollStatus) {
      ASOCKLOG(0, asock, ("AsyncSocket_HandleSSLError. AsyncSocketPollAdd failed\n"));
      AsyncSocketRelease(asock);
      return FALSE;
   }

   asock->readPausedForSocketBug = TRUE;

   return TRUE;
}


/*
 *----------------------------------------------------------------------------
 *
 * AsyncSocketRetryReadCallback --
 *
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Writes data, could trigger write completion or socket destruction.
 *
 *----------------------------------------------------------------------------
 */

static void
AsyncSocketRetryReadCallback(void *clientData)     // IN
{
   AsyncSocket *asock = (AsyncSocket *) clientData;
   VMwareStatus pollStatus;

   /*
    * Add the socket back to the poll list.
    */
   if ((asock->state != AsyncSocketClosed) 
         && (asock->readPausedForSocketBug)) {
      ASSERT(asock->refCount > 1); /* We should not be last user of socket */
      ASSERT(asock->state == AsyncSocketConnected);

      ASOCKLOG(3, asock, ("AsyncSocketRetryReadCallback registering for a new read\n"));

      pollStatus = AsyncSocketPollAdd(asock, 
                                      TRUE, 
                                      POLL_FLAG_READ | POLL_FLAG_PERIODIC,
                                      (SOCK_STREAM == asock->type) 
                                             ? AsyncSocketRecvCallback :
                                                AsyncSocketRecvUDPCallback);
      if (pollStatus != VMWARE_STATUS_SUCCESS) {
         ASOCKWARN(asock, ("failed to install recv callback!\n"));
      }

      /*
       * Restore the state.
       */
      asock->recvPos = asock->savedRecvPos;
      asock->recvBuf = asock->savedRecvBuf;
      if (SOCK_STREAM == asock->type) {
         asock->recvFn = asock->savedRecvFunction;
      } else {
         asock->recvUDPFn = asock->savedRecvFunction;
      }
      asock->recvCb = TRUE;
      asock->recvLen = asock->savedRecvLen;
      asock->readPausedForSocketBug = FALSE;
   }

   /*
    * Balance the AddRef in AsyncSocket_HandleSSLError.
    */
   AsyncSocketRelease(asock);
}
#endif // __APPLE_READ_BUG_WORKAROUND__


/*
 *-----------------------------------------------------------------------------
 *
 * AsyncSocket_ConnectSSL --
 *
 *    Initialize the socket's SSL object, by calling SSL_ConnectAndVerify.
 *    NOTE: This call is blocking.
 *
 * Results:
 *    TRUE if SSL_ConnectAndVerify succeeded, FALSE otherwise.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
AsyncSocket_ConnectSSL(AsyncSocket *asock,          // IN
                       SSLVerifyParam *verifyParam) // IN/OPT
{
   ASSERT(asock);
   return SSL_ConnectAndVerify(asock->sslSock, verifyParam);
}
