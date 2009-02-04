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
 * tunnelProxy.h --
 *
 *      Multi-channel socket proxy over HTTPS with control messages, lossless
 *      reconnect, heartbeats, etc.
 */

#ifndef __TUNNEL_PROXY_H__
#define __TUNNEL_PROXY_H__


#include "vm_basic_defs.h"
#include "vm_basic_types.h"
#include "asyncsocket.h"


/*
 * Tunnel URL paths.  Append to the server1/2 addresses returned in
 * the tunnel-connection XML API response.
 */

#define TP_CONNECT_URL_PATH "/ice/tunnel"
#define TP_RECONNECT_URL_PATH "/ice/reconnect"


/*
 * Known message types
 */

#define TP_MSG_ERROR          "error"
#define TP_MSG_INIT           "init"
#define TP_MSG_PLEASE_INIT    "please-init"
#define TP_MSG_START          "start"
#define TP_MSG_PLEASE_AUTH    "please-auth"
#define TP_MSG_AUTHENTICATED  "authenticated"
#define TP_MSG_AUTH_RQ        "auth_rq"
#define TP_MSG_AUTH_RP        "auth_rp"
#define TP_MSG_READY          "ready"
#define TP_MSG_AS_REQ         "as-req"
#define TP_MSG_AS_REP         "as-rep"
#define TP_MSG_TID_RQ         "tid-rq"
#define TP_MSG_TGS_REQ        "tgs-req"
#define TP_MSG_TGS_REP        "tgs-rep"
#define TP_MSG_TID_RP         "tid-rp"
#define TP_MSG_AUT_RQ         "aut-rq"
#define TP_MSG_AUT_RP         "aut-rp"
#define TP_MSG_ECHO_RQ        "echo-rq"
#define TP_MSG_ECHO_RP        "echo-rp"
#define TP_MSG_STOP           "stop"
#define TP_MSG_SYSMSG         "sysmsg"
#define TP_MSG_RSP            "rsp"
#define TP_MSG_LISTEN_RQ      "listen-rq"
#define TP_MSG_LISTEN_RP      "listen-rp"
#define TP_MSG_RAISE_RQ       "raise-rq"
#define TP_MSG_RAISE_RP       "raise-rp"
#define TP_MSG_LOWER          "lower"
#define TP_MSG_UNLISTEN_RQ    "unlisten-rq"
#define TP_MSG_UNLISTEN_RP    "unlisten-rp"
#define TP_MSG_OPENURL_RQ     "openurl-rq"
#define TP_MSG_OPENURL_RP     "openurl-rp"
#define TP_MSG_SECURITY_MSG   "security-msg"
#define TP_MSG_PAC_UPDATE     "pac-update"
#define TP_MSG_PAC_REPLY      "pac-reply"
#define TP_MSG_STID_RQ        "stid-rq"
#define TP_MSG_STID_RP        "stid-rp"
#define TP_MSG_CLIENT_ERROR   "client-error"

typedef enum {
   TP_ERR_OK,
   TP_ERR_NOT_CONNECTED,
   TP_ERR_CANT_CONNECT,
   TP_ERR_ALREADY_CONNECTED,
   TP_ERR_INVALID_RECONNECT,
   TP_ERR_INVALID_LISTENER,
   TP_ERR_INVALID_CHANNELID,
} TunnelProxyErr;

typedef struct TunnelProxy TunnelProxy;


typedef void (*TunnelProxySendNeededCb)(TunnelProxy *tp, void *userData);

typedef void (*TunnelProxyDisconnectCb)(TunnelProxy *tp,
                                        const char *reconnectSecret,
                                        const char *reason, void *userData);

typedef Bool (*TunnelProxyMsgHandlerCb)(TunnelProxy *tp, const char *msgId,
                                        const char *body, int len,
                                        void *userData);

typedef Bool (*TunnelProxyNewListenerCb)(TunnelProxy *tp, const char *portName,
                                         const char *bindAddr, int bindPort,
                                         void *userData);

typedef Bool (*TunnelProxyNewChannelCb)(TunnelProxy *tp, const char *portName,
                                        AsyncSocket *socket, void *userData);

typedef void (*TunnelProxyEndChannelCb)(TunnelProxy *tp, const char *portName,
                                        AsyncSocket *socket, void *userData);


TunnelProxy *TunnelProxy_Create(const char *connectionId,
                                TunnelProxyNewListenerCb listenerCb,
                                void *listenerCbData,
                                TunnelProxyNewChannelCb newChannelCb,
                                void *newChannelCbData,
                                TunnelProxyEndChannelCb endChannelCb,
                                void *endChannelCbData);

void TunnelProxy_Free(TunnelProxy *tp);

char *TunnelProxy_GetConnectUrl(TunnelProxy *tp, const char *serverUrl);

TunnelProxyErr TunnelProxy_Connect(TunnelProxy *tp,
                                   const char *hostIp, const char *hostAddr,
                                   TunnelProxySendNeededCb sendNeededCb,
                                   void *sendNeededCbData,
                                   TunnelProxyDisconnectCb disconnectCb,
                                   void *disconnectCbData);

TunnelProxyErr TunnelProxy_Disconnect(TunnelProxy *tp);

void TunnelProxy_AddMsgHandler(TunnelProxy *tp, const char *msgId,
                               TunnelProxyMsgHandlerCb cb,
                               void *userData);

void TunnelProxy_RemoveMsgHandler(TunnelProxy *tp, const char *msgId,
                                  TunnelProxyMsgHandlerCb cb,
                                  void *userData);

void TunnelProxy_SendMsg(TunnelProxy *tp, const char *msgId,
                         const char *body, int len);

TunnelProxyErr TunnelProxy_CloseChannel(TunnelProxy *tp,
                                        unsigned int channelId);

TunnelProxyErr TunnelProxy_CloseListener(TunnelProxy *tp, const char *portName);


/*
 * HTTP IO driver interface
 */

void TunnelProxy_HTTPRecv(TunnelProxy *tp, const char *buf, int bufSize,
                          Bool httpChunked);
void TunnelProxy_HTTPSend(TunnelProxy *tp, char *buf, int *bufSize,
                          Bool httpChunked);
Bool TunnelProxy_HTTPSendNeeded(TunnelProxy *tp);


/*
 * Message parsing utilities
 */

#define TP_TYPE_STRING "=S"
#define TP_TYPE_INT "=I"
#define TP_TYPE_LONG "=L"
#define TP_TYPE_BOOL "=B"
#define TP_TYPE_ERROR "=E"

/* Not yet supported */
/*
#define TP_TYPE_BINARY "=b"
#define TP_TYPE_STRINGLIST "=s"
#define TP_TYPE_EMPTY "=N"
*/

Bool TunnelProxy_ReadMsg(const char *body, int len,
                         const char* nameTypeKey, ...);

Bool TunnelProxy_FormatMsg(char **body, int *len,
                           const char* nameTypeKey, ...);


#endif // __TUNNEL_PROXY_H__
