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
 * tunnelProxy.c --
 *
 *      Multi-channel socket proxy over HTTP with control messages, lossless
 *      reconnect, heartbeats, etc.
 */


#include <sys/time.h>   /* For gettimeofday */
#include <time.h>       /* For gettimeofday */
#include <sys/types.h>  /* For getsockname */
#include <sys/socket.h> /* For getsockname */
#include <netinet/in.h> /* For getsockname */


#include "tunnelProxy.h"
#include "base64.h"
#include "circList.h"
#include "dynbuf.h"
#include "msg.h"
#include "poll.h"
#include "str.h"
#include "strutil.h"
#include "util.h"


#if 0
#define DEBUG_DATA(x) DEBUG_ONLY(Warning x)
#else
#define DEBUG_DATA(x)
#endif
#define DEBUG_MSG(x) DEBUG_ONLY(Warning x)


typedef enum {
   TP_CHUNK_TYPE_ACK     = 65, // 'A'
   TP_CHUNK_TYPE_DATA    = 68, // 'D'
   TP_CHUNK_TYPE_MESSAGE = 77, // 'M'
} TPChunkType;


#define TP_MSGID_MAXLEN 24
#define TP_PORTNAME_MAXLEN 24
#define TP_BUF_MAXLEN 1024 * 10 // Tunnel reads/writes limited to 10K due to
                                // buffer pooling in tunnel server.
#define TP_MAX_UNACKNOWLEDGED 4
#define TP_MAX_START_FLOW_CONTROL 4 * TP_MAX_UNACKNOWLEDGED
#define TP_MIN_END_FLOW_CONTROL TP_MAX_UNACKNOWLEDGED


typedef struct {
   ListItem list;
   char type;
   unsigned int ackId;
   unsigned int chunkId;
   unsigned int channelId;
   char msgId[TP_MSGID_MAXLEN];
   char *body;
   int len;
} TPChunk;


typedef struct {
   ListItem list;
   char msgId[TP_MSGID_MAXLEN];
   TunnelProxyMsgHandlerCb cb;
   void *userData;
} TPMsgHandler;


typedef struct {
   ListItem list;
   TunnelProxy *tp;
   char portName[TP_PORTNAME_MAXLEN];
   unsigned int port;
   AsyncSocket *listenSock;
   Bool singleUse;
} TPListener;


typedef struct {
   ListItem list;
   TunnelProxy *tp;
   unsigned int channelId;
   char portName[TP_PORTNAME_MAXLEN];
   AsyncSocket *socket;
   char recvByte;
} TPChannel;


struct TunnelProxy
{
   char *capID;
   char *hostIp;   // From TunnelProxy_Connect
   char *hostAddr; // From TunnelProxy_Connect
   char *reconnectSecret;     // From TP_MSG_AUTHENTICATED
   int64 lostContactTimeout;  // From TP_MSG_AUTHENTICATED
   int64 disconnectedTimeout; // From TP_MSG_AUTHENTICATED
   int64 sessionTimeout;      // From TP_MSG_AUTHENTICATED

   struct timeval lastConnect;

   TunnelProxyNewListenerCb listenerCb;
   void *listenerCbData;
   TunnelProxyNewChannelCb newChannelCb;
   void *newChannelCbData;
   TunnelProxyEndChannelCb endChannelCb;
   void *endChannelCbData;
   TunnelProxySendNeededCb sendNeededCb;
   void *sendNeededCbData;
   TunnelProxyDisconnectCb disconnectCb;
   void *disconnectCbData;

   unsigned int maxChannelId;
   Bool flowStopped;

   unsigned int lastChunkIdSeen;
   unsigned int lastChunkAckSeen;
   unsigned int lastChunkIdSent;
   unsigned int lastChunkAckSent;

   // Outgoing fifo
   ListItem *queueOut;
   ListItem *queueOutNeedAck;

   ListItem *listeners;
   ListItem *channels;
   ListItem *msgHandlers;

   DynBuf readBuf;
   DynBuf writeBuf;
};


/* Helpers */

static TunnelProxyErr TunnelProxyDisconnect(TunnelProxy *tp, const char *reason,
                                            Bool closeSockets, Bool notify);
static void TunnelProxyFireSendNeeded(TunnelProxy *tp);
static void TunnelProxySendChunk(TunnelProxy *tp, TPChunkType type,
                                 unsigned int channelId, const char *msgId,
                                 char *body, int bodyLen);
static void TunnelProxyFreeChunk(TPChunk *chunk, ListItem **list);
static void TunnelProxyFreeMsgHandler(TPMsgHandler *handler, ListItem **list);
static void TunnelProxyResetTimeouts(TunnelProxy *tp, Bool requeue);


/* Default Msg handler callbacks */

static Bool TunnelProxyEchoRequestCb(TunnelProxy *tp, const char *msgId,
                                     const char *body, int len, void *userData);
static Bool TunnelProxyEchoReplyCb(TunnelProxy *tp, const char *msgId,
                                   const char *body, int len, void *userData);
static Bool TunnelProxyStopCb(TunnelProxy *tp, const char *msgId,
                              const char *body, int len, void *userData);
static Bool TunnelProxyAuthenticatedCb(TunnelProxy *tp, const char *msgId,
                                       const char *body, int len, void *userData);
static Bool TunnelProxyReadyCb(TunnelProxy *tp, const char *msgId,
                               const char *body, int len, void *userData);
static Bool TunnelProxySysMsgCb(TunnelProxy *tp, const char *msgId,
                                const char *body, int len, void *userData);
static Bool TunnelProxyErrorCb(TunnelProxy *tp, const char *msgId,
                               const char *body, int len, void *userData);
static Bool TunnelProxyPleaseInitCb(TunnelProxy *tp, const char *msgId,
                                    const char *body, int len, void *userData);
static Bool TunnelProxyRaiseReplyCb(TunnelProxy *tp, const char *msgId,
                                    const char *body, int len, void *userData);
static Bool TunnelProxyListenRequestCb(TunnelProxy *tp, const char *msgId,
                                       const char *body, int len, void *userData);
static Bool TunnelProxyUnlistenRequestCb(TunnelProxy *tp, const char *msgId,
                                         const char *body, int len, void *userData);
static Bool TunnelProxyLowerCb(TunnelProxy *tp, const char *msgId,
                               const char *body, int len, void *userData);

/* Timer callbacks */

static void TunnelProxyEchoTimeoutCb(void *userData);
static void TunnelProxyLostContactTimeoutCb(void *userData);


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_Create --
 *
 *       Create a TunnelProxy object, and add all the default message handlers.
 *
 * Results:
 *       Newly allocated TunnelProxy.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

TunnelProxy *
TunnelProxy_Create(const char *connectionId,             // IN
                   TunnelProxyNewListenerCb listenerCb,  // IN/OPT
                   void *listenerCbData,                 // IN/OPT
                   TunnelProxyNewChannelCb newChannelCb, // IN/OPT
                   void *newChannelCbData,               // IN/OPT
                   TunnelProxyEndChannelCb endChannelCb, // IN/OPT
                   void *endChannelCbData)               // IN/OPT
{
   TunnelProxy *tp = Util_SafeCalloc(1, sizeof(TunnelProxy));

   tp->capID = Util_SafeStrdup(connectionId);

   tp->listenerCb = listenerCb;
   tp->listenerCbData = listenerCbData;
   tp->newChannelCb = newChannelCb;
   tp->newChannelCbData = newChannelCbData;
   tp->endChannelCb = endChannelCb;
   tp->endChannelCbData = endChannelCbData;

#define TP_AMH(_msg, _cb) TunnelProxy_AddMsgHandler(tp, _msg, _cb, NULL)
   TP_AMH(TP_MSG_AUTHENTICATED, TunnelProxyAuthenticatedCb);
   TP_AMH(TP_MSG_ECHO_RQ,       TunnelProxyEchoRequestCb);
   TP_AMH(TP_MSG_ECHO_RP,       TunnelProxyEchoReplyCb);
   TP_AMH(TP_MSG_ERROR,         TunnelProxyErrorCb);
   TP_AMH(TP_MSG_LISTEN_RQ,     TunnelProxyListenRequestCb);
   TP_AMH(TP_MSG_LOWER,         TunnelProxyLowerCb);
   TP_AMH(TP_MSG_PLEASE_INIT,   TunnelProxyPleaseInitCb);
   TP_AMH(TP_MSG_RAISE_RP,      TunnelProxyRaiseReplyCb);
   TP_AMH(TP_MSG_READY,         TunnelProxyReadyCb);
   TP_AMH(TP_MSG_STOP,          TunnelProxyStopCb);
   TP_AMH(TP_MSG_SYSMSG,        TunnelProxySysMsgCb);
   TP_AMH(TP_MSG_UNLISTEN_RQ,   TunnelProxyUnlistenRequestCb);
#undef TP_AMH

   return tp;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyFireSendNeeded --
 *
 *       Utility to call the TunnelProxy's sendNeededCb if there are chunks
 *       that can be sent.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxyFireSendNeeded(TunnelProxy *tp) // IN
{
   ASSERT(tp);
   if (tp->sendNeededCb && TunnelProxy_HTTPSendNeeded(tp)) {
      tp->sendNeededCb(tp, tp->sendNeededCbData);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxySendChunk --
 *
 *       Create and queue a new outgoing TPChunk object, specifying all the
 *       content.  The chunk will be appended to the TunnelProxy's outgoing
 *       queue, and the sendNeededCb passed to TunnelProxy_Connect invoked.
 *       Body content is always duplicated.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxySendChunk(TunnelProxy *tp,        // IN
                     TPChunkType type,       // IN
                     unsigned int channelId, // IN/OPT
                     const char *msgId,      // IN/OPT
                     char *body,             // IN/OPT
                     int bodyLen)            // IN/OPT
{
   TPChunk *newChunk;

   ASSERT(tp);

   newChunk = Util_SafeCalloc(1, sizeof(TPChunk));
   newChunk->type = type;

   newChunk->channelId = channelId;
   if (msgId) {
      Str_Strcpy(newChunk->msgId, msgId, TP_MSGID_MAXLEN);
   }
   if (body) {
      newChunk->len = bodyLen;
      newChunk->body = Util_SafeMalloc(bodyLen + 1);
      newChunk->body[bodyLen] = 0;
      memcpy(newChunk->body, body, bodyLen);
   }

   LIST_QUEUE(&newChunk->list, &tp->queueOut);

   TunnelProxyFireSendNeeded(tp);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_Free --
 *
 *       Free a TunnelProxy object.  Calls TunnelProxyDisconnect to close all
 *       sockets, frees all pending in and out chunks.
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
TunnelProxy_Free(TunnelProxy *tp) // IN
{
   ListItem *li;
   ListItem *liNext;

   ASSERT(tp);

   TunnelProxyDisconnect(tp, NULL, TRUE, FALSE);

   LIST_SCAN_SAFE(li, liNext, tp->queueOut) {
      TunnelProxyFreeChunk(LIST_CONTAINER(li, TPChunk, list), &tp->queueOut);
   }
   LIST_SCAN_SAFE(li, liNext, tp->queueOutNeedAck) {
      TunnelProxyFreeChunk(LIST_CONTAINER(li, TPChunk, list),
                           &tp->queueOutNeedAck);
   }

   LIST_SCAN_SAFE(li, liNext, tp->msgHandlers) {
      TunnelProxyFreeMsgHandler(LIST_CONTAINER(li, TPMsgHandler, list),
                                &tp->msgHandlers);
   }

   free(tp->capID);
   free(tp->hostIp);
   free(tp->hostAddr);
   free(tp->reconnectSecret);
   free(tp);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyFreeChunk --
 *
 *       Free a TPChunk object, and remove it from the queue passed.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxyFreeChunk(TPChunk *chunk,  // IN
                     ListItem **list) // IN/OUT
{
   ASSERT(chunk);
   ASSERT(list);
   LIST_DEL(&chunk->list, list);
   free(chunk->body);
   free(chunk);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyFreeMsgHandler --
 *
 *       Free a TPMsgHandler object, and remove it from the queue passed.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxyFreeMsgHandler(TPMsgHandler *handler, // IN
                          ListItem **list)       // IN/OUT
{
   ASSERT(handler);
   LIST_DEL(&handler->list, list);
   free(handler);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_AddMsgHandler --
 *
 *       Allocate a new msg handler object which handles messages of the given
 *       msgId, and add it to the TunnelProxy handler queue.
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
TunnelProxy_AddMsgHandler(TunnelProxy *tp,            // IN
                          const char *msgId,          // IN
                          TunnelProxyMsgHandlerCb cb, // IN
                          void *userData)             // IN/OPT
{
   TPMsgHandler *msgHandler;

   ASSERT(tp);
   ASSERT(msgId);

   msgHandler = Util_SafeCalloc(1, sizeof(TPMsgHandler));
   Str_Strcpy(msgHandler->msgId, msgId, TP_MSGID_MAXLEN);
   msgHandler->userData = userData;
   msgHandler->cb = cb;

   LIST_QUEUE(&msgHandler->list, &tp->msgHandlers);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_RemoveMsgHandler --
 *
 *       Free an existing msg handler which handles messages of the given
 *       msgId, and remove it from the TunnelProxy handler queue.
 *
 *       Handlers are matched based on msgId and cb/userData.  If no
 *       cb/userData is passed, all message handlers for the given msgId will
 *       be removed.
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
TunnelProxy_RemoveMsgHandler(TunnelProxy *tp,            // IN
                             const char *msgId,          // IN
                             TunnelProxyMsgHandlerCb cb, // IN
                             void *userData)             // IN/OPT
{
   ListItem *li;
   ListItem *liNext;

   ASSERT(tp);
   ASSERT(msgId);

   LIST_SCAN_SAFE(li, liNext, tp->msgHandlers) {
      TPMsgHandler *handler = LIST_CONTAINER(li, TPMsgHandler, list);
      if (Str_Strcmp(handler->msgId, msgId) == 0 &&
          (!cb || (handler->cb == cb && handler->userData == userData))) {
         TunnelProxyFreeMsgHandler(handler, &tp->msgHandlers);
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_SendMsg --
 *
 *       Append a message of msgId with the given body to the outgoing message
 *       queue.  The chunk will be assigned the next serial chunk id.  The
 *       body buffer will be copied.
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
TunnelProxy_SendMsg(TunnelProxy *tp,   // IN
                    const char *msgId, // IN
                    const char *body,  // IN
                    int len)           // IN
{
   ASSERT(tp);
   ASSERT(msgId && strlen(msgId) < TP_MSGID_MAXLEN);

   TunnelProxySendChunk(tp, TP_CHUNK_TYPE_MESSAGE, 0, msgId, (char*) body, len);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_ReadMsg --
 *
 *       Parse a formatted message using a key=type:value markup syntax, with
 *       the value destination pointer passed in to the next argument, similar
 *       to sscanf.  Final argument must be NULL.  Supported types are:
 *
 *          S - a base64 encoded utf8 string
 *          E - a base64 encoded utf8 error string
 *          I - integer
 *          L - 64-bit integer
 *          B - boolean, 1, "true", and "yes" are all considered TRUE
 *
 *       e.g. TunnelProxy_ReadMsg(body, len, "reason=S", &reasonStr, NULL);
 *
 *       Returned strings must be freed by the caller.  Non-null string args
 *       must be freed regardless of return value, in case of partial success.
 *
 * Results:
 *       TRUE if all key=type:value pairs parsed correctly, FALSE otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
TunnelProxy_ReadMsg(const char *body,        // IN
                    int len,                 // IN
                    const char* nameTypeKey, // IN
                    ...)                     // OUT
{
   va_list args;
   va_start(args, nameTypeKey);
   Bool success = TRUE;
   char *valueStr = NULL;

   while (nameTypeKey) {
      int nameLen = strlen(nameTypeKey);
      char *prefix = strstr(body, nameTypeKey);
      int idx;
      int32 *I;
      int64 *L;
      Bool *B;

      if (!prefix || prefix[nameLen] != ':' ||
          (prefix != body && prefix[-1] != '|')) {
         success = FALSE;
         break;
      }

      /*
       * Find value string before '|' which starts the next element.  Skip the
       * colon ':' after nameTypeKey, separating the type-id char from the value
       * string.
       */
      idx = nameLen + 1;
      valueStr = StrUtil_GetNextToken(&idx, prefix, "|");

      switch (prefix[nameLen-1]) {
      case 'S':
      case 'E': {
         char **S = va_arg(args, char**);
         size_t decodeLen;
         uint8 *decodeBuf = NULL;

         decodeLen = Base64_DecodedLength(valueStr, strlen(valueStr));
         decodeBuf = Util_SafeMalloc(decodeLen + 1);

         success = Base64_Decode(valueStr, decodeBuf, decodeLen, &decodeLen);

         if (success) {
            decodeBuf[decodeLen] = '\0';
            *S = (char*) decodeBuf;
         } else {
            free(decodeBuf);
            *S = NULL;
            goto exit;
         }
         break;
      }
      case 'I':
         I = va_arg(args, int32*);
         StrUtil_StrToInt(I, valueStr);
         break;
      case 'L':
         L = va_arg(args, int64*);
         StrUtil_StrToInt64(L, valueStr);
         break;
      case 'B':
         B = va_arg(args, Bool*);
         *B = FALSE;
         if (Str_Strcmp(valueStr, "1") == 0 ||
             Str_Strcasecmp(valueStr, "true") == 0 ||
             Str_Strcasecmp(valueStr, "yes") == 0) {
            *B = TRUE;
         }
         break;
      default:
         NOT_IMPLEMENTED();
      }

      free(valueStr);
      valueStr = NULL;

      nameTypeKey = va_arg(args, char*);
   }

exit:
   va_end(args);
   free(valueStr);

   return success;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_FormatMsg --
 *
 *       Compose a formatted message using a key=type:value markup syntax,
 *       where the value is taken from the next argument.  Final argument
 *       must be NULL.  See TunnelProxy_ReadMsg for supported types.
 *
 *       e.g. TunnelProxy_FormatMsg(&val, &len, "portName=S", portName, NULL);
 *
 *       Returned body must be freed by the caller.
 *
 * Results:
 *       TRUE if all name=type:value pairs parsed correctly, FALSE otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
TunnelProxy_FormatMsg(char **body,             // OUT
                      int *len,                // OUT
                      const char* nameTypeKey, // IN
                      ...)                     // IN
{
   va_list args;
   DynBuf builder;
   Bool success = TRUE;

   *body = NULL;
   *len = -1;

   DynBuf_Init(&builder);
   va_start(args, nameTypeKey);

   while (nameTypeKey) {
      int nameLen = strlen(nameTypeKey);
      char numStr[128];
      const char *S;
      char *SEncoded;
      int32 I;
      int64 L;
      Bool B;

      DynBuf_Append(&builder, nameTypeKey, nameLen);
      DynBuf_Append(&builder, ":", 1);

      switch (nameTypeKey[nameLen-1]) {
      case 'S':
      case 'E':
         // Strings are always Base64 encoded
         S = va_arg(args, const char*);
         ASSERT(S);
         success = Base64_EasyEncode(S, strlen(S), &SEncoded);
         if (!success) {
            Log("Failed to base64-encode \"%s\"\n", S);
            goto exit;
         }
         DynBuf_Append(&builder, SEncoded, strlen(SEncoded));
         free(SEncoded);
         break;
      case 'I':
         I = va_arg(args, int32);
         Str_Sprintf(numStr, sizeof(numStr), "%d", I);
         DynBuf_Append(&builder, numStr, strlen(numStr));
         break;
      case 'L':
         L = va_arg(args, int64);
         Str_Sprintf(numStr, sizeof(numStr), "%"FMT64"d", L);
         DynBuf_Append(&builder, numStr, strlen(numStr));
         break;
      case 'B':
         B = va_arg(args, int);
         S = B ? "true" : "false";
         DynBuf_Append(&builder, S, strlen(S));
         break;
      default:
         NOT_IMPLEMENTED();
      }

      DynBuf_Append(&builder, "|", 1);

      nameTypeKey = va_arg(args, char*);
   }

exit:
   va_end(args);

   if (success) {
      /* Null terminate for safety */
      DynBuf_Append(&builder, "\0", 1);

      *len = DynBuf_GetSize(&builder) - 1;
      *body = (char*) DynBuf_Detach(&builder);
   }
   DynBuf_Destroy(&builder);

   return success;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_GetConnectUrl --
 *
 *       Create a URL to use when POSTing, based on a server URL retreived
 *       during broker tunnel initialization.  If the TunnelProxy has been
 *       connected before and there is a valid reconnectSecret, the URL will be
 *       different from an initial connection.
 *
 * Results:
 *       The URL of the tunnel connect/reconnect.  Caller must free it.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

char *
TunnelProxy_GetConnectUrl(TunnelProxy *tp,       // IN
                          const char *serverUrl) // IN
{
   size_t len = 0;
   if (tp->capID) {
      if (tp->reconnectSecret) {
         return Str_Asprintf(&len, "%s"TP_RECONNECT_URL_PATH"?%s&%s", serverUrl,
                             tp->capID, tp->reconnectSecret);
      } else {
         return Str_Asprintf(&len, "%s"TP_CONNECT_URL_PATH"?%s", serverUrl,
                             tp->capID);
      }
   } else {
      return Str_Asprintf(&len, "%s"TP_CONNECT_URL_PATH, serverUrl);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_Connect --
 *
 *       Connect or reconnect a TunnelProxy.  Queues an INIT msg.
 *
 * Results:
 *       TunnelProxyErr.
 *
 * Side effects:
 *       Reinitializes tunnel read/write buffers.
 *
 *-----------------------------------------------------------------------------
 */

TunnelProxyErr
TunnelProxy_Connect(TunnelProxy *tp,                      // IN
                    const char *hostIp,                   // IN/OPT
                    const char *hostAddr,                 // IN/OPT
                    TunnelProxySendNeededCb sendNeededCb, // IN/OPT
                    void *sendNeededCbData,               // IN/OPT
                    TunnelProxyDisconnectCb disconnectCb, // IN/OPT
                    void *disconnectCbData)               // IN/OPT
{
   Bool isReconnect;

   ASSERT(tp);

   isReconnect = (tp->lastConnect.tv_sec > 0);
   if (isReconnect && !tp->reconnectSecret) {
      return TP_ERR_INVALID_RECONNECT;
   }

   gettimeofday(&tp->lastConnect, NULL);

   free(tp->hostIp);
   free(tp->hostAddr);
   tp->hostIp = strdup(hostIp ? hostIp : "127.0.0.1");
   tp->hostAddr = strdup(hostAddr ? hostAddr : "localhost");

   tp->sendNeededCb = sendNeededCb;
   tp->sendNeededCbData = sendNeededCbData;
   tp->disconnectCb = disconnectCb;
   tp->disconnectCbData = disconnectCbData;

   DynBuf_Destroy(&tp->readBuf);
   DynBuf_Init(&tp->readBuf);
   DynBuf_Destroy(&tp->writeBuf);
   DynBuf_Init(&tp->writeBuf);

   if (isReconnect) {
      TunnelProxyResetTimeouts(tp, TRUE);

      tp->queueOut = LIST_SPLICE(tp->queueOutNeedAck, tp->queueOut);
      tp->queueOutNeedAck = NULL;
      /* Want to ACK the last chunk ID we saw */
      tp->lastChunkAckSent = 0;

      TunnelProxyFireSendNeeded(tp);
   } else {
      char *initBody = NULL;
      int initLen = 0;
      /* XXX: Need our own type, and version. */
      TunnelProxy_FormatMsg(&initBody, &initLen,
                            "type=S", "C", /* "simple" C client */
                            "v1=I", 3, "v2=I", 1, "v3=I", 4,
                            "cid=S", "1234",
                            NULL);
      TunnelProxy_SendMsg(tp, TP_MSG_INIT, initBody, initLen);
      free(initBody);
   }

   return TP_ERR_OK;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyDisconnect --
 *
 *       Disconnect a TunnelProxy.  If closeSockets is true, all sockets and
 *       channels are shutdown, and corresponding UNLISTEN_RP messages are
 *       sent.  If notify is true, the disconnect callback is invoked with
 *       with reason argument to allow reconnection.
 *
 * Results:
 *       TunnelProxyErr.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

TunnelProxyErr
TunnelProxyDisconnect(TunnelProxy *tp,    // IN
                      const char *reason, // IN
                      Bool closeSockets,  // IN
                      Bool notify)        // IN
{
   TunnelProxyErr err;

   if (tp->lastConnect.tv_sec == 0) {
      return TP_ERR_NOT_CONNECTED;
   }

   /* Cancel any existing timeouts */
   TunnelProxyResetTimeouts(tp, FALSE);

   if (closeSockets) {
      ListItem *li;
      ListItem *liNext;

      LIST_SCAN_SAFE(li, liNext, tp->listeners) {
         TPListener *listener = LIST_CONTAINER(li, TPListener, list);
         /* This will close all the channels as well */
         err = TunnelProxy_CloseListener(tp, listener->portName);
         ASSERT(TP_ERR_OK == err);
      }
   }

   if (notify && tp->disconnectCb) {
      ASSERT(reason);
      tp->disconnectCb(tp, tp->reconnectSecret, reason, tp->disconnectCbData);
   }

   return TP_ERR_OK;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_Disconnect --
 *
 *       Disconnect a TunnelProxy.  All sockets and channels are shutdown, and
 *       corresponding UNLISTEN_RP messages are sent.  The disconnect callback
 *       passed to TunnelProxy_Connect is not invoked.
 *
 * Results:
 *       TunnelProxyErr.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

TunnelProxyErr
TunnelProxy_Disconnect(TunnelProxy *tp) // IN
{
   return TunnelProxyDisconnect(tp, NULL, TRUE, FALSE);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_CloseListener --
 *
 *       Close a listening socket identified by portName.  All socket channels
 *       are closed, and an UNLISTEN_RP msg is sent to the tunnel server.
 *
 * Results:
 *       TunnelProxyErr.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

TunnelProxyErr
TunnelProxy_CloseListener(TunnelProxy *tp,      // IN
                          const char *portName) // IN
{
   TPListener *listener = NULL;
   ListItem *li;
   ListItem *liNext;
   char *unlisten = NULL;
   int unlistenLen = 0;
   TunnelProxyErr err;

   ASSERT(tp);
   ASSERT(portName);

   LIST_SCAN(li, tp->listeners) {
      TPListener *listenerIter = LIST_CONTAINER(li, TPListener, list);
      if (Str_Strcmp(listenerIter->portName, portName) == 0) {
         listener = listenerIter;
         break;
      }
   }
   if (!listener) {
      return TP_ERR_INVALID_LISTENER;
   }

   AsyncSocket_Close(listener->listenSock);
   LIST_DEL(&listener->list, &tp->listeners);
   free(listener);

   /*
    * Send an UNLISTEN_RQ in any case of closing.  It might not be an actual
    * reply if closing due to max connections being hit.
    */
   TunnelProxy_FormatMsg(&unlisten, &unlistenLen, "portName=S", portName, NULL);
   TunnelProxy_SendMsg(tp, TP_MSG_UNLISTEN_RP, unlisten, unlistenLen);
   free(unlisten);

   /* Close all the channels */
   LIST_SCAN_SAFE(li, liNext, tp->channels) {
      TPChannel *channel = LIST_CONTAINER(li, TPChannel, list);
      if (Str_Strcmp(channel->portName, portName) == 0) {
         err = TunnelProxy_CloseChannel(tp, channel->channelId);
         ASSERT(TP_ERR_OK == err);
      }
   }

   return TP_ERR_OK;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_CloseChannel --
 *
 *       Close an individual socket channel identified by its channelId.  If
 *       the channel's listner is single-use, TunnelProxy_CloseListener is
 *       invoked.  Otherwise, a LOWER message is sent to the tunnel server.
 *
 * Results:
 *       TunnelProxyErr.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

TunnelProxyErr
TunnelProxy_CloseChannel(TunnelProxy *tp,        // IN
                         unsigned int channelId) // IN
{
   TPChannel *channel = NULL;
   ListItem *li;
   TunnelProxyErr err;

   ASSERT(tp);

   LIST_SCAN(li, tp->channels) {
      TPChannel *chanIter = LIST_CONTAINER(li, TPChannel, list);
      if (chanIter->channelId == channelId) {
         channel = chanIter;
         break;
      }
   }
   if (!channel) {
      return TP_ERR_INVALID_CHANNELID;
   }

   LIST_SCAN(li, tp->listeners) {
      TPListener *listener = LIST_CONTAINER(li, TPListener, list);

      if (listener->singleUse &&
          Str_Strcmp(listener->portName, channel->portName) == 0) {
         Log("Closing single-use listener \"%s\" after channel \"%d\" "
             "disconnect.\n", channel->portName, channelId);

         err = TunnelProxy_CloseListener(tp, channel->portName);
         ASSERT(TP_ERR_OK == err);

         /* Channel is no more */
         channel = NULL;
         break;
      }
   }

   if (channel) {
      char *lower = NULL;
      int lowerLen = 0;

      if (channel->socket) {
         AsyncSocket_Close(channel->socket);
      }

      TunnelProxy_FormatMsg(&lower, &lowerLen, "chanID=I", channelId, NULL);
      TunnelProxy_SendMsg(tp, TP_MSG_LOWER, lower, lowerLen);
      free(lower);

      LIST_DEL(&channel->list, &tp->channels);
      free(channel);
   }

   return TP_ERR_OK;
}


/*
 * Tunnel channel connect and IO handlers
 */


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxySocketRecvCb --
 *
 *       Read IO callback handler for a given socket channel.  An attempt is
 *       made to read all data available on the socket in a non-blocking
 *       fashion.  If an error occurs while reading, TunnelProxy_CloseChannel
 *       is called.
 *
 *       If any data is read, a new outgoing data chunk is queued with all the
 *       data.  Max data size for one chunk is 10K (see
 *       wswc_tunnel/conveyor.cpp).
 *
 *       AsyncSocket_Recv for 1 byte is issued to cause this callback to be
 *       invoked the next time there is at least one byte of data to read.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxySocketRecvCb(void *buf,          // IN/OPT: read data
                        int len,            // IN/OPT: buf length
                        AsyncSocket *asock, // IN
                        void *clientData)   // IN: TPChannel
{
   TPChannel *channel = clientData;
   TunnelProxy *tp;
   int throttle = 3;

   ASSERT(channel);
   ASSERT(channel->socket == asock);

   tp = channel->tp;
   ASSERT(tp);

   while (throttle--) {
      int asockErr = ASOCKERR_SUCCESS;
      TunnelProxyErr err;
      char recvBuf[TP_BUF_MAXLEN];
      int recvLen = 0;

      /*
       * Non-blocking read with 0 timeout to drain queued data.  Offset into
       * readBuf by the size of the buf argument.
       */
      asockErr = AsyncSocket_RecvBlocking(asock, &recvBuf[len],
                                          sizeof(recvBuf) - len, &recvLen,
                                          0);

      /* Prepend the buf argument the first time through this loop */
      if (len) {
         ASSERT(buf);
         memcpy(recvBuf, buf, len);
         recvLen += len;
         len = 0;
      }

      if (recvLen != 0) {
         /* Send the data we have, regardless of recv success */
         TunnelProxySendChunk(tp, TP_CHUNK_TYPE_DATA, channel->channelId,
                              NULL, recvBuf, recvLen);
      }

      if (asockErr != ASOCKERR_SUCCESS && asockErr != ASOCKERR_TIMEOUT) {
         Log("Error reading from channel \"%d\": %s\n", channel->channelId,
             AsyncSocket_Err2String(asockErr));

         err = TunnelProxy_CloseChannel(tp, channel->channelId);
         ASSERT(TP_ERR_OK == err);
         return;
      }

      if (recvLen == 0) {
         break;
      }
   }

   /* Recv at least 1-byte before calling this callback again */
   AsyncSocket_Recv(asock, &channel->recvByte, 1,
                    TunnelProxySocketRecvCb, channel);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxySocketErrorCb --
 *
 *       Error handler for a socket channel.  Calls TunnelProxy_CloseChannel
 *       to notify the server the local side of the channel has closed.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       Calls the TunnelProxy's endChannelCb to notify of channel death.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxySocketErrorCb(int error,          // IN
                         AsyncSocket *asock, // IN
                         void *clientData)   // IN
{
   TPChannel *channel = clientData;
   TunnelProxy *tp;
   TunnelProxyErr err;

   ASSERT(channel);
   ASSERT(channel->socket == asock);

   tp = channel->tp;
   ASSERT(tp);

   if (tp->endChannelCb) {
      tp->endChannelCb(tp, channel->portName, asock, tp->endChannelCbData);
   }

   Log("Closing channel \"%d\" socket for listener \"%s\": %s.\n",
       channel->channelId, channel->portName, AsyncSocket_Err2String(error));

   err = TunnelProxy_CloseChannel(tp, channel->channelId);
   ASSERT(TP_ERR_OK == err);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxySocketConnectCb --
 *
 *       Connection handler callback to notify of a new local socket
 *       connection for a given TPListener.  Creates a new channel and adds it
 *       to the TunnelProxy's channel queue.
 *
 *       Sends a RAISE_RQ to the tunnel server to notify it of the new channel
 *       connection.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       Calls the TunnelProxy's newChannelCb to notify of channel create.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxySocketConnectCb(AsyncSocket *asock, // IN
                           void *clientData)   // IN
{
   TPListener *listener = clientData;
   TunnelProxy *tp;
   char *raiseBody;
   int raiseLen;
   TPChannel *newChannel;
   int newChannelId;

   ASSERT(listener);

   tp = listener->tp;
   ASSERT(tp);

   if (tp->newChannelCb && !tp->newChannelCb(tp, listener->portName, asock,
                                             tp->newChannelCbData)) {
      Log("Rejecting new channel connection to listener \"%s\"",
          listener->portName);
      AsyncSocket_Close(asock);
      return;
   }

   newChannelId = ++tp->maxChannelId;

   Log("Creating new channel \"%d\" to listener \"%s\".\n",
       newChannelId, listener->portName);

   newChannel = Util_SafeCalloc(1, sizeof(TPChannel));
   newChannel->channelId = newChannelId;
   Str_Strcpy(newChannel->portName, listener->portName, TP_PORTNAME_MAXLEN);
   newChannel->socket = asock;
   newChannel->tp = tp;

   LIST_QUEUE(&newChannel->list, &tp->channels);

   AsyncSocket_SetErrorFn(asock, TunnelProxySocketErrorCb, newChannel);
   AsyncSocket_UseNodelay(asock, TRUE);

   TunnelProxy_FormatMsg(&raiseBody, &raiseLen,
                         "chanID=I", newChannel->channelId,
                         "portName=S", newChannel->portName, NULL);
   TunnelProxy_SendMsg(tp, TP_MSG_RAISE_RQ, raiseBody, raiseLen);
   free(raiseBody);
}


/*
 * HTTP IO driver interface
 */


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyReadHex --
 *
 *       Inline stream parsing helper.  Attempts to read a hex-encoded integer
 *       string followed by a trailing char, and returns it in the out param.
 *       Advances the idx param past the trailing char.
 *
 * Results:
 *       TRUE if a terminated hex number was read, FALSE otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static inline Bool
TunnelProxyReadHex(char *buf,  // IN
                   int len,    // IN
                   char trail, // IN
                   int *idx,   // IN/OUT
                   int *out)   // OUT
{
   int numDigits = 0;
   long value = 0;

   while (len > *idx + numDigits) {
      char digit = buf[*idx + numDigits];

      if (digit == trail) {
         *idx += numDigits + 1;
         *out = value;
         return TRUE;
      }

      numDigits++;
      value = value << 4; /* Shift four places (multiply by 16) */

      /* Add in digit value */
      if (digit >= '0' && digit <= '9') {
         value += digit - '0';      /* Number range 0..9 */
      } else if (digit >= 'A' && digit <= 'F') {
         value += digit - 'A' + 10; /* Character range 10..15 */
      } else if (digit >= 'a' && digit <= 'f') {
         value += digit - 'a' + 10; /* Character range 10..15 */
      } else {
         Log("TunnelProxyReadHex: Invalid number character: %u\n", digit);
         return FALSE;
      }
   }

   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyReadStr --
 *
 *       Inline stream parsing helper.  Given a string length, attempts to
 *       verify that the entire string is available and is terminated by a ';'.
 *       Advances the idx param past the ';'.  Sets the start param to the
 *       beginning of the string.
 *
 * Results:
 *       TRUE if a ';' terminated string number was read, FALSE otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static inline Bool
TunnelProxyReadStr(char *buf,    // IN
                   int len,      // IN
                   int *idx,     // IN/OUT: current index
                   char **start, // OUT: start of string
                   int strLen)   // IN: expecting this size
{
   if (len > *idx + strLen + 1 && buf[*idx + strLen] == ';') {
      *start = &buf[*idx];
      *idx += strLen + 1;
      return TRUE;
   }
   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyReadChunk --
 *
 *       Attempts to read a single well-formatted Ack, Data or Message chunk
 *       from the buf param.  Fills the chunk arg with the read data, without
 *       copying.
 *
 *       If httpChunked is true, the buf is assumed to be HTTP-chunked encoded,
 *       with '%x\r\n.....\r\n' surrounding each chunk.
 *
 * Results:
 *       Size of data read from buf, 0 if no chunk was read.
 *
 * Side effects:
 *       If successful, sets readLen to the amount of data read from the
 *       beginning of buf to construct the chunk.
 *
 *-----------------------------------------------------------------------------
 */

static unsigned int
TunnelProxyReadChunk(char *buf,        // IN
                     int len,          // IN
                     Bool httpChunked, // IN
                     TPChunk *chunk)   // IN/OUT
{
   int minLen = httpChunked ? 10 : 3;
   int readLen = 0;

#define READHEX(_val) \
   if (!TunnelProxyReadHex(buf, len, ';', &readLen, _val)) { return 0; }
#define READSTR(_out, _len) \
   if (!TunnelProxyReadStr(buf, len, &readLen, _out, _len)) { return 0; }

   ASSERT(chunk);

   if (len < minLen) {
      return 0;
   }

   if (httpChunked) {
      /* Chunked header looks like %x\r\n.....\r\n */
      int chunkLen = 0;
      if (!TunnelProxyReadHex(buf, len, '\r', &readLen, &chunkLen) ||
          readLen + 1 + chunkLen + 2 > len) {
         return FALSE;
      }

      ASSERT(buf[readLen] == '\n');
      readLen++;
   }

   {
      char *typeStr = NULL;
      READSTR(&typeStr, 1)
      chunk->type = *typeStr;
   }

   switch (chunk->type) {
   case TP_CHUNK_TYPE_ACK:
      READHEX(&chunk->ackId)
      DEBUG_DATA(("RECV-ACK(ackId=%d)\n", chunk->ackId));
      break;
   case TP_CHUNK_TYPE_MESSAGE: {
      char *hdr = NULL;
      int hdrLen = 0;
      char *msgId = NULL;

      READHEX(&chunk->chunkId)
      READHEX(&chunk->ackId)

      READHEX(&hdrLen)
      READSTR(&hdr, hdrLen)

      READHEX(&chunk->len)
      READSTR(&chunk->body, chunk->len)

      if (!TunnelProxy_ReadMsg(hdr, hdrLen, "messageType=S", &msgId, NULL)) {
         Log("Invalid messageType in tunnel message header!\n");
         return FALSE;
      }

      Str_Strcpy(chunk->msgId, msgId, TP_MSGID_MAXLEN);
      free(msgId);

      DEBUG_DATA(("RECV-MSG(id=%d, ack=%d, msgid=%s, length=%d): %.*s\n",
                  chunk->chunkId, chunk->ackId,
                  chunk->msgId,
                  chunk->len, chunk->len, chunk->body));
      break;
   }
   case TP_CHUNK_TYPE_DATA:
      READHEX(&chunk->chunkId)
      READHEX(&chunk->ackId)
      READHEX(&chunk->channelId)

      READHEX(&chunk->len)
      READSTR(&chunk->body, chunk->len)

      DEBUG_DATA(("RECV-DATA(id=%d, ack=%d, channel=%d, length=%d)\n",
                  chunk->chunkId, chunk->ackId, chunk->channelId, chunk->len));
      break;
   default:
      Log("Invalid tunnel message type identifier \"%c\" (%d).\n",
          chunk->type, chunk->type);
      NOT_IMPLEMENTED();
   }

   if (httpChunked) {
      ASSERT(buf[readLen] == '\r');
      ASSERT(buf[readLen + 1] == '\n');

      /* Move past trailing \r\n */
      readLen += 2;
   }

   return readLen;

#undef READHEX
#undef READSTR
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyHandleInChunk --
 *
 *       Processes the next chunk in the incoming chunk queue.  If the chunk
 *       is an Ack, a message in the outgoing needs-ACK queue with the
 *       corresponding chunkId is found and freed.  If a Message chunk, a
 *       handler for the chunk's msgId header is found and invoked with the
 *       chunk data.  If a data chunk, the socket channel with the
 *       corresponding channelId is located and the chunk data written to the
 *       socket using AsyncSocket_Send.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxyHandleInChunk(TunnelProxy *tp, // IN
                         TPChunk *chunk)  // IN
{
   ListItem *li;
   ListItem *liNext;

   ASSERT(tp);
   ASSERT(chunk);

   if (chunk->chunkId > 0) {
      if (chunk->chunkId <= tp->lastChunkIdSeen) {
         /* This chunk has been replayed... skip it. */
         Log("Skipping replayed chunk ID '%d'.\n", chunk->chunkId);
         return;
      }
      tp->lastChunkIdSeen = chunk->chunkId;
   }

   if (chunk->ackId > 0) {
      if (chunk->ackId > tp->lastChunkIdSent) {
         Log("Unknown ACK ID '%d' in received tunnel message.\n", chunk->ackId);
      }

      LIST_SCAN_SAFE(li, liNext, tp->queueOutNeedAck) {
         TPChunk *outChunk = LIST_CONTAINER(li, TPChunk, list);
         ASSERT(outChunk);

         /* queueOutNeedAck is sorted in ascending chunk ID order. */
         if (chunk->ackId >= outChunk->chunkId) {
            TunnelProxyFreeChunk(outChunk, &tp->queueOutNeedAck);
         } else {
            break;
         }
      }

      tp->lastChunkAckSeen = chunk->ackId;
   }

   switch (chunk->type) {
   case TP_CHUNK_TYPE_MESSAGE: {
      Bool found = FALSE;

      ASSERT(chunk->msgId);
      LIST_SCAN_SAFE(li, liNext, tp->msgHandlers) {
         TPMsgHandler *handler = LIST_CONTAINER(li, TPMsgHandler, list);

         if (Str_Strcasecmp(handler->msgId, chunk->msgId) == 0) {
            ASSERT(handler->cb);
            found = TRUE;
            if (handler->cb(tp, chunk->msgId, chunk->body, chunk->len,
                            handler->userData)) {
               /* True means the handler handled the message, so stop here. */
               break;
            }
         }
      }

      if (!found) {
         DEBUG_MSG(("Unhandled message type '%s' received.\n", chunk->msgId));
      }
      break;
   }
   case TP_CHUNK_TYPE_DATA: {
      Bool found = FALSE;

      LIST_SCAN(li, tp->channels) {
         TPChannel *channel = LIST_CONTAINER(li, TPChannel, list);

         if (channel->channelId == chunk->channelId) {
            char *body = Util_SafeMalloc(chunk->len);
            memcpy(body, chunk->body, chunk->len);

            /* Send the copied body, to be freed on completion */
            AsyncSocket_Send(channel->socket, body, chunk->len,
                             (AsyncSocketSendFn) free, NULL);

            found = TRUE;
            break;
         }
      }

      if (!found) {
         DEBUG_MSG(("Data received for unknown channel id '%d'.\n",
                    chunk->channelId));
      }
      break;
   }
   case TP_CHUNK_TYPE_ACK:
      /* Let the common ACK handling happen */
      break;
   default:
      NOT_IMPLEMENTED();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_HTTPRecv --
 *
 *       Process incoming tunnel data read from an unknown HTTP source.  The
 *       buffer contents are assumed *not* to be in HTTP chunked encoding.
 *
 *       Appends the buffer data to the TunnelProxy's readBuf, and attempts to
 *       construct and queue incoming chunks from the data by calling
 *       TunnelProxyReadChunk.  The data read from the readBuf and used
 *       to construct full chunks is removed from the front of the readBuf.
 *
 *       TunnelProxyHandleInChunk is called repeatedly to process any
 *       existing or newly queued incoming chunks.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       Many.
 *
 *-----------------------------------------------------------------------------
 */

void
TunnelProxy_HTTPRecv(TunnelProxy *tp,  // IN
                     const char *buf,  // IN
                     int bufSize,      // IN
                     Bool httpChunked) // IN
{
   int totalReadLen = 0;

   ASSERT(tp);
   ASSERT(buf && bufSize > 0);

   DynBuf_Append(&tp->readBuf, buf, bufSize);

   while (TRUE) {
      unsigned int readLen;
      TPChunk chunk;

      memset(&chunk, 0, sizeof(chunk));
      readLen = TunnelProxyReadChunk(&tp->readBuf.data[totalReadLen],
                                     tp->readBuf.size - totalReadLen,
                                     httpChunked, &chunk);
      if (!readLen) {
         break;
      }

      TunnelProxyHandleInChunk(tp, &chunk);
      totalReadLen += readLen;
   }

   if (!totalReadLen) {
      return;
   }

   /* Shrink the front of the read buffer */
   memcpy(tp->readBuf.data, &tp->readBuf.data[totalReadLen],
          tp->readBuf.size - totalReadLen);
   tp->readBuf.size -= totalReadLen;

   /* Reset timeouts after successfully reading a chunk. */
   TunnelProxyResetTimeouts(tp, TRUE);

   /* Toggle flow control if needed */
   {
      unsigned int unackCnt = tp->lastChunkIdSent - tp->lastChunkAckSeen;

      if ((unackCnt > TP_MAX_START_FLOW_CONTROL) && !tp->flowStopped) {
         DEBUG_MSG(("Starting flow control (%d unacknowledged chunks)\n",
                    unackCnt));
         tp->flowStopped = TRUE;
      } else if ((unackCnt < TP_MIN_END_FLOW_CONTROL) && tp->flowStopped) {
         DEBUG_MSG(("Ending flow control\n"));
         tp->flowStopped = FALSE;
         TunnelProxyFireSendNeeded(tp);
      }
   }

   /* Queue new ACK if we haven't sent one in a while */
   if (tp->lastChunkIdSeen - tp->lastChunkAckSent >= TP_MAX_UNACKNOWLEDGED) {
      DEBUG_MSG(("Recv'd %d unacknowledged chunks.  Sending ACK chunk.\n",
                 TP_MAX_UNACKNOWLEDGED));
      TunnelProxySendChunk(tp, TP_CHUNK_TYPE_ACK, 0, NULL, NULL, 0);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyWriteNextOutChunk --
 *
 *       Serialize the next chunk in the outgoing chunk queue into the
 *       TunnelProxy's writeBuf.  If the chunk to be serialized is a Data
 *       chunk, its body is first base64 encoded.
 *
 *       If the outgoing chunk doesn't have an ackId field set, the next chunk
 *       on the incoming to-be-ACK's queue is used, and is freed.
 *
 *       Once processed the chunk is moved to the outgoing needs-ACK queue.
 *
 * Results:
 *       TRUE if a chunk was written successfully, FALSE otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelProxyWriteNextOutChunk(TunnelProxy *tp,  // IN
                             Bool httpChunked) // IN
{
   ListItem *li;
   TPChunk *chunk = NULL;
   char *msg = NULL;
   size_t msgLen = 0;

   ASSERT(tp);

   LIST_SCAN(li, tp->queueOut) {
      TPChunk *chunkIter = LIST_CONTAINER(li, TPChunk, list);
      if (!tp->flowStopped || chunkIter->type != TP_CHUNK_TYPE_DATA) {
         chunk = chunkIter;
         break;
      }
   }
   if (!chunk) {
      return FALSE;
   }

   /*
    * Assign the next chunk ID if this is not an ACK or a resent chunk
    * following a reconnect.
    */
   if (chunk->chunkId == 0 && chunk->type != TP_CHUNK_TYPE_ACK) {
      chunk->chunkId = ++tp->lastChunkIdSent;
   }
   if (tp->lastChunkAckSent < tp->lastChunkIdSeen) {
      chunk->ackId = tp->lastChunkIdSeen;
      tp->lastChunkAckSent = chunk->ackId;
   }

   switch (chunk->type) {
   case TP_CHUNK_TYPE_MESSAGE: {
      char *hdr;
      int hdrLen = 0;
      if (!TunnelProxy_FormatMsg(&hdr, &hdrLen, "messageType=S", chunk->msgId,
                                 NULL)) {
         Log("Failed to create tunnel msg header chunkId=%d.\n",
             chunk->chunkId);
         return FALSE;
      }
      msg = Str_Asprintf(&msgLen, "M;%X;%.0X;%X;%.*s;%X;%.*s;", chunk->chunkId,
                         chunk->ackId, hdrLen, hdrLen, hdr, chunk->len,
                         chunk->len, chunk->body);
      free(hdr);

      DEBUG_DATA(("SEND-MSG(id=%d, ack=%d, msgid=%s, length=%d): %.*s\n",
                  chunk->chunkId, chunk->ackId,
                  chunk->msgId,
                  chunk->len, chunk->len, chunk->body));
      break;
   }
   case TP_CHUNK_TYPE_DATA: {
      msg = Str_Asprintf(&msgLen, "D;%X;%.0X;%X;%X;", chunk->chunkId,
                         chunk->ackId, chunk->channelId, chunk->len);

      msg = Util_SafeRealloc(msg, msgLen + chunk->len + 1);
      memcpy(&msg[msgLen], chunk->body, chunk->len);
      msg[msgLen + chunk->len] = ';';
      msgLen += chunk->len + 1;

      DEBUG_DATA(("SEND-DATA(id=%d, ack=%d, channel=%d, length=%d)\n",
                  chunk->chunkId, chunk->ackId, chunk->channelId, chunk->len));
      break;
   }
   case TP_CHUNK_TYPE_ACK:
      ASSERT(chunk->ackId > 0);
      msg = Str_Asprintf(&msgLen, "A;%X;", chunk->ackId);

      DEBUG_DATA(("SEND-ACK(ackId=%d)\n", chunk->ackId));
      break;
   default:
      NOT_REACHED();
   }

   ASSERT(msg);

   if (httpChunked) {
      char chunkHdr[32];
      Str_Sprintf(chunkHdr, sizeof(chunkHdr), "%X\r\n", (int)msgLen);
      DynBuf_Append(&tp->writeBuf, chunkHdr, strlen(chunkHdr));
      DynBuf_Append(&tp->writeBuf, msg, msgLen);
      DynBuf_Append(&tp->writeBuf, "\r\n", 2);
   } else {
      DynBuf_Append(&tp->writeBuf, msg, msgLen);
   }

   /*
    * Move outgoing Data/Message chunk to need-ACK outgoing list.
    * TunnelProxyHandleInChunk assumes queueOutNeedAck is sorted by
    * ascending chunk ID, so queue at the end.
    */
   LIST_DEL(&chunk->list, &tp->queueOut);
   LIST_QUEUE(&chunk->list, &tp->queueOutNeedAck);

   free(msg);
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_HTTPSend --
 *
 *       Write outgoing chunk data to the buffer supplied.  The buffer is
 *       intended to be written to the tunnel server over HTTP.  The buffer
 *       contents are *not* separated by HTTP chunked encoding.
 *
 *       The data written from the writeBuf is removed from the front of the
 *       writeBuf and the amount of data written returned in the bufSize
 *       param.
 *
 *       TunnelProxyWriteNextOutChunk is called repeatedly to serialize any
 *       existing or newly queued outgoing chunks.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       Many.
 *
 *-----------------------------------------------------------------------------
 */

void
TunnelProxy_HTTPSend(TunnelProxy *tp,  // IN
                     char *buf,        // IN/OUT
                     int *bufSize,     // IN/OUT
                     Bool httpChunked) // IN
{
   ASSERT(tp);
   ASSERT(buf && *bufSize > 0);

   /*
    * If we don't do the HTTP chunked encoding ourselves, the caller has to,
    * so only serialize a single message at a time so the caller can chunk
    * encode it.
    */
   while (TunnelProxyWriteNextOutChunk(tp, httpChunked) && httpChunked) {
      /* Do nothing. */
   }

   *bufSize = MIN(tp->writeBuf.size, *bufSize);
   memcpy(buf, tp->writeBuf.data, *bufSize);

   /* Shrink the front of the write buffer */
   memcpy(tp->writeBuf.data, &tp->writeBuf.data[*bufSize],
          tp->writeBuf.size - *bufSize);
   tp->writeBuf.size -= *bufSize;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_HTTPSendNeeded --
 *
 *       Determine if TunnelProxy_HTTPSend should be called in order to
 *       serialize outgoing tunnel chunks, so as to be written over HTTP.
 *
 * Results:
 *       TRUE if there is chunk data to send, FALSE if there's nothing to send.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
TunnelProxy_HTTPSendNeeded(TunnelProxy *tp) // IN
{
   ListItem *li;
   TPChunk *chunk = NULL;

   ASSERT(tp);

   LIST_SCAN(li, tp->queueOut) {
      chunk = LIST_CONTAINER(li, TPChunk, list);
      if (!tp->flowStopped || chunk->type != TP_CHUNK_TYPE_DATA) {
         return TRUE;
      }
   }
   return FALSE;
}


/*
 * Default Msg handler impls
 */

/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyEchoRequestCb --
 *
 *       ECHO_RQ tunnel msg handler.  Sends an ECHO_RP msg.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelProxyEchoRequestCb(TunnelProxy *tp,   // IN
                         const char *msgId, // IN
                         const char *body,  // IN
                         int len,           // IN
                         void *userData)    // IN: not used
{
   TunnelProxy_SendMsg(tp, TP_MSG_ECHO_RP, NULL, 0);
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyEchoReplyCb --
 *
 *       ECHO_RP tunnel msg handler.  Does nothing other than avoid "Unhandled
 *       message" in logs.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelProxyEchoReplyCb(TunnelProxy *tp,   // IN
                       const char *msgId, // IN
                       const char *body,  // IN
                       int len,           // IN
                       void *userData)    // IN: not used
{
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyStopCb --
 *
 *       STOP tunnel msg handler.  Disconnects the TunnelProxy.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelProxyStopCb(TunnelProxy *tp,   // IN
                  const char *msgId, // IN
                  const char *body,  // IN
                  int len,           // IN
                  void *userData)    // IN: not used
{
   TunnelProxyErr err;
   char *reason;

   TunnelProxy_ReadMsg(body, len, "reason=S", &reason, NULL);
   Warning("TUNNEL STOPPED: %s\n", reason);

   /* Reconnect secret isn't valid after a STOP */
   free(tp->reconnectSecret);
   tp->reconnectSecret = NULL;

   err = TunnelProxyDisconnect(tp, reason, TRUE, TRUE);
   ASSERT(TP_ERR_OK == err);

   free(reason);
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyAuthenticatedCb --
 *
 *       AUTHENTICATED tunnel msg handler.  Stores reconnection and timeout
 *       information in the TunnelProxy object.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelProxyAuthenticatedCb(TunnelProxy *tp,   // IN
                           const char *msgId, // IN
                           const char *body,  // IN
                           int len,           // IN
                           void *userData)    // IN: not used
{
   char *capID = NULL;
   Bool allowAutoReconnection = FALSE;

   /* Ignored body contents:
    *    "sessionTimeout" long, time until the session will die
    */
   if (!TunnelProxy_ReadMsg(body, len,
                            "allowAutoReconnection=B", &allowAutoReconnection,
                            "capID=S", &capID,
                            "lostContactTimeout=L", &tp->lostContactTimeout,
                            "disconnectedTimeout=L", &tp->disconnectedTimeout,
                            NULL)) {
      NOT_IMPLEMENTED();
   }

   if (tp->capID && Str_Strcmp(capID, tp->capID) != 0) {
      Warning("Tunnel authenticated capID \"%s\" does not match expected "
              "value \"%s\".\n", capID, tp->capID);
   } else {
      tp->capID = capID;
      capID = NULL;
   }

   free(tp->reconnectSecret);
   tp->reconnectSecret = NULL;

   if (allowAutoReconnection &&
       !TunnelProxy_ReadMsg(body, len,
                            "reconnectSecret=S", &tp->reconnectSecret,
                            NULL)) {
      Warning("Tunnel automatic reconnect disabled: no reconnect secret in "
              "auth_rp.\n");
   }

   /* Kick off echo & disconnect timeouts */
   TunnelProxyResetTimeouts(tp, TRUE);

   free(capID);
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyReadyCb --
 *
 *       READY tunnel msg handler.  Just prints a message.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelProxyReadyCb(TunnelProxy *tp,   // IN: not used
                   const char *msgId, // IN: not used
                   const char *body,  // IN: not used
                   int len,           // IN: not used
                   void *userData)    // IN: not used
{
   Warning("TUNNEL READY\n");
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxySysMsgCb --
 *
 *       SYSMSG tunnel msg handler.  Prints the system message to stdout.
 *
 *       XXX: Should do something better here, like notify the user.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelProxySysMsgCb(TunnelProxy *tp,   // IN
                    const char *msgId, // IN
                    const char *body,  // IN
                    int len,           // IN
                    void *userData)    // IN: not used
{
   char *msg;

   TunnelProxy_ReadMsg(body, len, "msg=S", &msg, NULL);
   Warning("TUNNEL SYSTEM MESSAGE: %s\n", msg ? msg : "<Invalid Message>");
   free(msg);

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyErrorCb --
 *
 *       ERROR tunnel msg handler.  Prints the error and panics.
 *
 *       XXX: Should do something better here, like notify the user.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelProxyErrorCb(TunnelProxy *tp,   // IN
                   const char *msgId, // IN
                   const char *body,  // IN
                   int len,           // IN
                   void *userData)    // IN: not used
{
   char *msg;

   TunnelProxy_ReadMsg(body, len, "msg=S", &msg, NULL);
   Warning("TUNNEL ERROR: %s\n", msg ? msg : "<Invalid Error>");
   free(msg);

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyPleaseInitCb --
 *
 *       PLEASE_INIT tunnel msg handler.  Sends a START message in response
 *       containing the host's ip address, hostname, and time.
 *
 *       XXX: Need to figure out the host's IP address and hostname, or at
 *            least make a decent guess.
 *
 * Results:
 *       TRUE if the correlation-id matches that sent in INIT msg.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelProxyPleaseInitCb(TunnelProxy *tp,   // IN
                        const char *msgId, // IN
                        const char *body,  // IN
                        int len,           // IN
                        void *userData)    // IN: not used
{
   /* Ignored body contents:
    *    "plugins" string array
    *    "criticalities" ?? string array
    */
   char *startBody = NULL;
   int startLen = 0;
   int64 t1 = 0;

   ASSERT(tp->hostIp && tp->hostAddr);

   {
      char *cid = NULL;
      TunnelProxy_ReadMsg(body, len, "cid=S", &cid, NULL);
      if (!cid || Str_Strcmp(cid, "1234") != 0) {
         Warning("Incorrect correlation-id in tunnel PLEASEINIT: %s.\n", cid);
         return FALSE;
      }
      free(cid);
   }

   {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      t1 = tv.tv_sec * 1000;
      t1 += tv.tv_usec / 1000;
   }

   TunnelProxy_FormatMsg(&startBody, &startLen,
                         "ipaddress=S", tp->hostIp,
                         "hostaddress=S", tp->hostAddr,
                         "capID=S", tp->capID ? tp->capID : "",
                         "type=S", "C", // "simple" C client
                         "t1=L", t1, NULL);
   TunnelProxy_SendMsg(tp, TP_MSG_START, startBody, startLen);
   free(startBody);

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyRaiseReplyCb --
 *
 *       RAISE_RP tunnel msg handler.  If the message does not contain an
 *       error, we start up socket channel IO for the channel id referred to
 *       by chanId in the message by calling TunnelProxySocketRecvCb,
 *       otherwise calls TunnelProxy_CloseChannel to teardown the
 *       server-disallowed socket.
 *
 * Results:
 *       TRUE if IO was started or the channel was closed, FALSE if the
 *       channel is unknown.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelProxyRaiseReplyCb(TunnelProxy *tp,   // IN
                        const char *msgId, // IN
                        const char *body,  // IN
                        int len,           // IN
                        void *userData)    // IN: not used
{
   char *problem = NULL;
   int chanId = 0;
   TPChannel *channel = NULL;
   ListItem *li;
   TunnelProxyErr err;

   if (!TunnelProxy_ReadMsg(body, len, "chanID=I", &chanId, NULL)) {
      NOT_IMPLEMENTED();
   }

   LIST_SCAN(li, tp->channels) {
      TPChannel *chanIter = LIST_CONTAINER(li, TPChannel, list);
      if (chanIter->channelId == chanId) {
         channel = chanIter;
         break;
      }
   }
   if (!channel) {
      Log("Invalid channel \"%d\" in raise reply.\n", chanId);
      return FALSE;
   }

   TunnelProxy_ReadMsg(body, len, "problem=E", &problem, NULL);

   if (problem) {
      Log("Error raising channel \"%d\": %s\n", chanId, problem);

      err = TunnelProxy_CloseChannel(tp, channel->channelId);
      ASSERT(TP_ERR_OK == err);
   } else {
      /* Kick off channel reading */
      TunnelProxySocketRecvCb(NULL, 0, channel->socket, channel);
   }

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyListenRequestCb --
 *
 *       LISTEN_RQ tunnel msg handler.  Creates a local listener socket, and a
 *       TPListener object to manage it.  Sends LISTEN_RP message in reply if
 *       we were able to listen successfully.  Calls the TunnelProxy's
 *       listenerCb to notify of a new listener creation.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelProxyListenRequestCb(TunnelProxy *tp,   // IN
                           const char *msgId, // IN
                           const char *body,  // IN
                           int len,           // IN
                           void *userData)    // IN: not used
{
   int bindPort = -1;
   char *serverHost = NULL;
   int serverPort = 0;
   char *portName;
   int maxConns;
   int cid;
   char *bindAddr = NULL;
   int listenErr = ASOCKERR_SUCCESS;
   char *reply = NULL;
   int replyLen = 0;
   AsyncSocket *asock = NULL;
   TPListener *newListener = NULL;
   char *problem = NULL;

   if (!TunnelProxy_ReadMsg(body, len,
                            "clientPort=I", &bindPort,
                            "serverHost=S", &serverHost,
                            "serverPort=I", &serverPort,
                            "portName=S", &portName,
                            "maxConnections=I", &maxConns,
                            "cid=I", &cid, NULL)) {
      NOT_IMPLEMENTED();
   }

   if (bindPort == -1) {
      bindPort = INADDR_ANY;
   }

   /* clientHost is often null, so parse it optionally */
   TunnelProxy_ReadMsg(body, len, "clientHost=S", &bindAddr);
   if (!bindAddr) {
      bindAddr = strdup("127.0.0.1");
   }

   /* Create the listener early, so it can be the ConnectCb user data */
   newListener = Util_SafeCalloc(1, sizeof(TPListener));

   asock = AsyncSocket_ListenIPStr(bindAddr, bindPort,
                                   TunnelProxySocketConnectCb, newListener,
                                   NULL, &listenErr);
   if (!asock || ASOCKERR_SUCCESS != listenErr) {
      Log("Error creating new listener \"%s\" on %s:%d to server %s:%d: %s\n",
          portName, bindAddr, bindPort, serverHost, serverPort,
          AsyncSocket_Err2String(listenErr));

      problem = strdup(AsyncSocket_Err2String(listenErr));
      goto error;
   }

   AsyncSocket_UseNodelay(asock, TRUE);

   if (bindPort == 0) {
      /* Find the local port we've bound. */
      int fd = AsyncSocket_GetFd(asock);
      struct sockaddr addr = { 0 };
      socklen_t addrLen = sizeof(addr);

      if (getsockname(fd, &addr, &addrLen) < 0) {
         NOT_IMPLEMENTED();
      }

      bindPort = ntohs(((struct sockaddr_in*) &addr)->sin_port);
   }
   ASSERT(bindPort > 0);

   if (tp->listenerCb && !tp->listenerCb(tp, portName, bindAddr,
                                         bindPort, tp->listenerCbData)) {
      AsyncSocket_Close(asock);

      Log("Rejecting new listener \"%s\" on %s:%d to server %s:%d.\n",
          portName, bindAddr, bindPort, serverHost, serverPort);

      problem = strdup("User Rejected");
      goto error;
   }

   Log("Creating new listener \"%s\" on %s:%d to server %s:%d.\n",
       portName, bindAddr, bindPort, serverHost, serverPort);

   Str_Strcpy(newListener->portName, portName, TP_PORTNAME_MAXLEN);
   newListener->port = bindPort;
   newListener->listenSock = asock;
   newListener->singleUse = maxConns == 1;
   newListener->tp = tp;

   LIST_QUEUE(&newListener->list, &tp->listeners);

   TunnelProxy_FormatMsg(&reply, &replyLen,
                         "cid=I", cid,
                         "portName=S", portName,
                         "clientHost=S", bindAddr,
                         "clientPort=I", bindPort, NULL);

exit:
   TunnelProxy_SendMsg(tp, TP_MSG_LISTEN_RP, reply, replyLen);

   free(bindAddr);
   free(serverHost);
   free(portName);
   free(reply);

   return TRUE;

error:
   ASSERT(problem && !reply);
   TunnelProxy_FormatMsg(&reply, &replyLen, "cid=I", cid, "problem=E", problem,
                         NULL);
   free(problem);
   free(newListener);
   goto exit;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyUnlistenRequestCb --
 *
 *       UNLISTEN_RQ tunnel msg handler.  Looks up the portName provided in
 *       the message and calls TunnelProxy_CloseListener to close the listener
 *       and all its socket channels.  Sends an UNLISTEN_RP to verify the
 *       close completed successfully.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelProxyUnlistenRequestCb(TunnelProxy *tp,   // IN
                             const char *msgId, // IN
                             const char *body,  // IN
                             int len,           // IN
                             void *userData)    // IN: not used
{
   char *portName = NULL;
   char *reply = NULL;
   int replyLen = 0;

   if (!TunnelProxy_ReadMsg(body, len, "portName=S", &portName, NULL)) {
      NOT_IMPLEMENTED();
   }

   if (!portName || TP_ERR_OK != TunnelProxy_CloseListener(tp, portName)) {
      TunnelProxy_FormatMsg(&reply, &replyLen, "problem=E", "Invalid portName",
                            NULL);
   }

   TunnelProxy_SendMsg(tp, TP_MSG_UNLISTEN_RP, reply, replyLen);

   free(portName);
   free(reply);
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyLowerCb --
 *
 *       LOWER tunnel msg handler.  Looks up the channel for the channel ID
 *       provided in the message and calls TunnelProxy_CloseChannel to close
 *       the channel and its socket.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
TunnelProxyLowerCb(TunnelProxy *tp,   // IN
                   const char *msgId, // IN
                   const char *body,  // IN
                   int len,           // IN
                   void *userData)    // IN: not used
{
   int chanId = 0;
   TunnelProxyErr err;

   if (!TunnelProxy_ReadMsg(body, len, "chanID=I", &chanId, NULL)) {
      NOT_IMPLEMENTED();
   }

   Warning("Tunnel requested socket channel close (chanID: %d)\n", chanId);
   err = TunnelProxy_CloseChannel(tp, chanId);
   if (err != TP_ERR_OK) {
       Warning("Error closing socket channel %d: %d\n", chanId, err);
   }

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyResetTimeouts --
 *
 *       Cancel pending echo and lost contact timeouts and requeues them if
 *       the TunnelProxy has a lostContactTimeout as received in the
 *       AUTHENTICATED msg.
 *
 *       The echo timeout is 1/3 the time of the lost contact timeout, to
 *       mimic wswc_tunnel.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       Poll timeouts removed/added.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxyResetTimeouts(TunnelProxy *tp, // IN
                         Bool requeue)    // IN: restart timeouts
{
   ASSERT(tp);

   Poll_CB_RTimeRemove(TunnelProxyLostContactTimeoutCb, tp, FALSE);
   Poll_CB_RTimeRemove(TunnelProxyEchoTimeoutCb, tp, TRUE);

   if (requeue && tp->lostContactTimeout > 0) {
      Poll_CB_RTime(TunnelProxyLostContactTimeoutCb,
                    tp, tp->lostContactTimeout * 1000, FALSE, NULL);
      Poll_CB_RTime(TunnelProxyEchoTimeoutCb,
                    tp, tp->lostContactTimeout * 1000 / 3, TRUE, NULL);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyEchoTimeoutCb --
 *
 *       Echo poll timeout callback.  Sends an ECHO_RQ with a "now" field
 *       containing the current time in millis.
 *
 *       NOTE: There is no ECHO_RP message handler currently, as the handler
 *       in wswc_tunnel does nothing.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       Sends a ECHO_RQ tunnel msg.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxyEchoTimeoutCb(void *userData) // IN: TunnelProxy
{
   TunnelProxy *tp = userData;
   char *req = NULL;
   int reqLen = 0;
   int64 now = 0;

   ASSERT(tp);

   {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      now = tv.tv_sec * 1000;
      now += tv.tv_usec / 1000;
   }

   DEBUG_MSG(("Sending echo_rq (now=%"FMT64"d)\n", now));

   TunnelProxy_FormatMsg(&req, &reqLen, "now=L", now, NULL);
   TunnelProxy_SendMsg(tp, TP_MSG_ECHO_RQ, req, reqLen);
   free(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyLostContactTimeoutCb --
 *
 *       Lost contact timeout callback.  Calls TunnelProxyDisconnect to notify
 *       the client of the disconnect, and allows reconnection without
 *       destroying our listening ports.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxyLostContactTimeoutCb(void *userData) // IN: TunnelProxy
{
   TunnelProxy *tp = userData;
   char *msg;

   ASSERT(tp);

   msg = Msg_GetString(MSGID("cdk.linuxTunnel.lostContact")
                       "Client disconnected following no activity.");
   TunnelProxyDisconnect(tp, msg, FALSE, TRUE);
   free(msg);
}
