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

#ifndef __ASYNC_SOCKET_H__
#define __ASYNC_SOCKET_H__

/*
 * asyncsocket.h --
 *
 *      The AsyncSocket object is a fairly simple wrapper around a basic TCP
 *      socket. It's potentially asynchronous for both read and write
 *      operations. Reads are "requested" by registering a receive function
 *      that is called once the requested amount of data has been read from
 *      the socket. Similarly, writes are queued along with a send function
 *      that is called once the data has been written. Errors are reported via
 *      a separate callback.
 */

#define INCLUDE_ALLOW_VMCORE
#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

/*
 * Error codes
 */
#define ASOCKERR_SUCCESS           0
#define ASOCKERR_GENERIC           1
#define ASOCKERR_TIMEOUT           2
#define ASOCKERR_NOTCONNECTED      3
#define ASOCKERR_REMOTE_DISCONNECT 4
#define ASOCKERR_INVAL             5
#define ASOCKERR_CONNECT           6
#define ASOCKERR_ACCEPT            7
#define ASOCKERR_POLL              8
#define ASOCKERR_CLOSED            9
#define ASOCKERR_BIND              10
#define ASOCKERR_BINDADDRINUSE     11
#define ASOCKERR_LISTEN            12

/*
 * Address types to use with AsyncSocket_SendTo().
 */
typedef enum {
   ASOCKADDR_HOSTNAME = 0,
   ASOCKADDR_IPADDRESS,
   ASOCKADDR_SOCKADDR
} AsyncSocketSendToType;

/*
 * Flags passed into AsyncSocket_Connect*().
 * Default value is '0'.
 * The first two flags allow explicitly selecting
 * an ESX network stack. The 3rd is for code
 * that uses inet_pton() to get an IP address.
 * inet_pton() returns address in network-byte-order,
 * instead of the expected host-byte-order.
 */
typedef enum {
   ASOCKCONN_USE_ESX_SHADOW_STACK       = 1<<0,
   ASOCKCONN_USE_ESX_NATIVE_STACK       = 1<<1,
   ASOCKCONN_ADDR_IN_NETWORK_BYTE_ORDER = 1<<2
} AsyncSocketConnectFlags;

/*
 * SSL opaque type declarations (so we don't have to include ssl.h)
 */
struct SSLSockStruct;
struct _SSLVerifyParam;

/*
 * sockaddr type declaration (so we don't have to include winsock2.h, etc)
 */
struct sockaddr_in;

/*
 * AsyncSocket type is opaque
 */
typedef struct AsyncSocket AsyncSocket;

/*
 * AsyncSocket registers poll callbacks, so give client the opportunity
 * to control how this is done.
 *
 * All the AsyncSocket constructors (Listen, Connect, Attach) take an
 * optional AsyncSocketPollParam* argument; if NULL the default behavior is
 * used (callback is registered in POLL_CS_MAIN and locked by the BULL).
 * Or the client can specify its favorite poll class and locking behavior.
 */
#include "poll.h"
typedef struct AsyncSocketPollParams {
   int pollClass;           /* Default is POLL_CS_MAIN */
   int flags;               /* Default 0, only POLL_FLAG_NO_BULL is valid */
   struct DeviceLock *lock; /* Default: none but BULL */
} AsyncSocketPollParams;

/*
 * Initialize platform libraries
 */
int AsyncSocket_Init(void);

/*
 * Check the current state of the socket
 */
typedef enum AsyncSocketState {
   AsyncSocketListening,
   AsyncSocketConnecting,
   AsyncSocketConnected,
   AsyncSocketClosed,
} AsyncSocketState;

AsyncSocketState AsyncSocket_GetState(AsyncSocket *sock);

const char * AsyncSocket_Err2String(int err);

const char * AsyncSocket_MsgError(int asyncSockErr);

/*
 * Return a "unique" ID
 */
int AsyncSocket_GetID(AsyncSocket *asock);

/*
 * Return the fd corresponding to the connection
 */
int AsyncSocket_GetFd(AsyncSocket *asock);

/*
 * Return the remote IP address associated with this socket if applicable
 */
int AsyncSocket_GetRemoteIPAddress(AsyncSocket *asock,
                                   unsigned int *ip,
                                   const char **ipStr);

/*
 * Recv callback fires once previously requested data has been received
 */
typedef void (*AsyncSocketRecvFn) (void *buf, int len, AsyncSocket *asock,
                                   void *clientData);
typedef void (*AsyncSocketRecvUDPFn) (void *buf, int len, AsyncSocket *asock,
                                      void *clientData, struct sockaddr_in *sin,
                                      int sin_len);

/*
 * Send callback fires once previously queued data has been sent
 */
typedef void (*AsyncSocketSendFn) (void *buf, int len, AsyncSocket *asock,
                                   void *clientData);

/*
 * Error callback fires on I/O errors during read/write operations
 */
typedef void (*AsyncSocketErrorFn) (int error, AsyncSocket *asock,
                                    void *clientData);

typedef void (*AsyncSocketConnectFn) (AsyncSocket *asock, void *clientData);

/*
 * Listen on port and fire callback with new asock
 */
AsyncSocket *AsyncSocket_Listen(unsigned short port,
                                AsyncSocketConnectFn connectFn,
                                void *clientData,
                                AsyncSocketPollParams *pollParams,
                                int *error);
AsyncSocket *AsyncSocket_ListenIP(unsigned int ip,
                                  unsigned short port,
                                  AsyncSocketConnectFn connectFn,
                                  void *clientData,
                                  AsyncSocketPollParams *pollParams,
                                  int *error);
AsyncSocket *AsyncSocket_ListenIPStr(const char *ipStr,
                                     unsigned short port,
                                     AsyncSocketConnectFn connectFn,
                                     void *clientData,
                                     AsyncSocketPollParams *pollParams,
                                     int *error);

AsyncSocket *AsyncSocket_BindUDP(unsigned short port,
                                 void *clientData,
                                 AsyncSocketPollParams *pollParams,
                                 int *error);

/*
 * Connect to address:port and fire callback with new asock
 */
AsyncSocket *AsyncSocket_Connect(const char *hostname, unsigned short port,
                                 AsyncSocketConnectFn connectFn,
                                 void *clientData,
                                 AsyncSocketConnectFlags flags,
                                 AsyncSocketPollParams *pollParams,
                                 int *error);
AsyncSocket *AsyncSocket_ConnectIP(unsigned int ip, unsigned short port,
                                   AsyncSocketConnectFn connectFn,
                                   void *clientData,
                                   AsyncSocketConnectFlags flags,
                                   AsyncSocketPollParams *pollParams,
                                   int *error);

/*
 * Initiate SSL connection on existing asock, with optional cert verification
 */
Bool AsyncSocket_ConnectSSL(AsyncSocket *asock,
                            struct _SSLVerifyParam *verifyParam);

/*
 * Create a new AsyncSocket from an existing socket
 */
AsyncSocket *AsyncSocket_AttachToFd(int fd, AsyncSocketPollParams *pollParams,
                                    int *error);
AsyncSocket *AsyncSocket_AttachToSSLSock(struct SSLSockStruct *sslSock,
                                         AsyncSocketPollParams *pollParams,
                                         int *error);

/*
 * Enable or disable TCP_NODELAY on this AsyncSocket.
 */
int AsyncSocket_UseNodelay(AsyncSocket *asock, Bool nodelay);

/*
 * Waits until at least one packet is received or times out.
 */
int AsyncSocket_DoOneMsg(AsyncSocket *s, Bool read, int timeoutMS);

/*
 * Waits until at least one connect() is accept()ed or times out.
 */
int AsyncSocket_WaitForConnection(AsyncSocket *s, int timeoutMS);

/*
 * Send all pending packets onto the wire or give up after timeoutMS msecs.
 */
int AsyncSocket_Flush(AsyncSocket *asock, int timeoutMS);

/*
 * Specify the amount of data to receive and the receive function to call
 */
int AsyncSocket_Recv(AsyncSocket *asock, void *buf, int len, ...);

/*
 * Specify the amount of data to send/receive and how long to wait before giving
 * up.
 */
int AsyncSocket_RecvBlocking(AsyncSocket *asock,
                             void *buf, int len, int *received,
                             int timeoutMS);

int AsyncSocket_SendBlocking(AsyncSocket *asock,
                             void *buf, int len, int *sent,
                             int timeoutMS);

/*
 * Specify the amount of data to send and the send function to call
 */
int AsyncSocket_Send(AsyncSocket *asock, void *buf, int len,
                      AsyncSocketSendFn sendFn, void *clientData);

int AsyncSocket_SendTo(AsyncSocket *asock, void *buf, int len,
                       AsyncSocketSendToType type, ... );

int AsyncSocket_IsSendBufferFull(AsyncSocket *asock);
int AsyncSocket_CancelRecv(AsyncSocket *asock, int *partialRecvd, void **recvBuf,
                           void **recvFn); 

/*
 * Set the error handler to invoke on I/O errors (default is to close the
 * socket)
 */
int AsyncSocket_SetErrorFn(AsyncSocket *asock, AsyncSocketErrorFn errorFn,
                           void *clientData);

/*
 * Request that we continue processing chained AsyncSocket_Recv
 * requests until all available data has been drained, or until a
 * supplied timeout is exhausted.
 */
int AsyncSocket_SetDrainTimeout(AsyncSocket *asock, VmTimeType timeoutUS);

/*
 * Close the connection and destroy the asock.
 */
int AsyncSocket_Close(AsyncSocket *asock);

/*
 * Some logging macros for convenience
 */
#define ASOCKPREFIX "SOCKET "

#define ASOCKWARN(_asock, _warnargs)                                 \
   do {                                                              \
      Warning(ASOCKPREFIX "%d (%d) ",                                \
              AsyncSocket_GetID(_asock), AsyncSocket_GetFd(_asock)); \
      Warning _warnargs;                                             \
   } while (0)

#define ASOCKLG0(_asock, _logargs)                               \
   do {                                                          \
      Log(ASOCKPREFIX "%d (%d) ",                                \
          AsyncSocket_GetID(_asock), AsyncSocket_GetFd(_asock)); \
      Log _logargs;                                              \
   } while (0)

#define ASOCKLOG(_level, _asock, _logargs)                            \
   do {                                                               \
      if (((_level) == 0) || DOLOG_BYNAME(asyncsocket, (_level))) {   \
         Log(ASOCKPREFIX "%d (%d) ",                                  \
             AsyncSocket_GetID((_asock)), AsyncSocket_GetFd(_asock)); \
         Log _logargs;                                                \
      }                                                               \
   } while(0)

#endif // __ASYNC_SOCKET_H__

