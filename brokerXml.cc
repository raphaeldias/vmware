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
 * brokerXml.cc --
 *
 *    Broker XML API.
 */


#include <libxml/parser.h>


#include "brokerXml.hh"


#define BROKER_V1_HDR "<?xml version=\"1.0\"?><broker version=\"1.0\">"
#define BROKER_V2_HDR "<?xml version=\"1.0\"?><broker version=\"2.0\">"
#define BROKER_TAIL "</broker>"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::BrokerXml --
 *
 *      Constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

BrokerXml::BrokerXml(Util::string hostname, // IN
                     int port,              // IN
                     bool secure)           // IN
   : mHostname(hostname),
     mPort(port),
     mSecure(secure),
     mCookieJar(BasicHttp_CreateCookieJar()),
     mVersion(VERSION_2)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::~BrokerXml --
 *
 *      Destructor.  Cancel all active requests.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

BrokerXml::~BrokerXml()
{
   CancelRequests();
   BasicHttp_FreeCookieJar(mCookieJar);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetContent --
 *
 *       Get the text content from a named child node.
 *
 * Results:
 *       Content Util::string, possibly empty.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BrokerXml::GetContent(xmlNode *parentNode) // IN
{
   if (parentNode) {
      for (xmlNode *currentNode = parentNode->children; currentNode;
           currentNode = currentNode->next) {
         if (XML_TEXT_NODE == currentNode->type) {
            return (const char*) currentNode->content;
         }
      }
   }
   return "";
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetChild --
 *
 *       Find a child node with a given name.
 *
 *       From apps/lib/basicSOAP/basicSoapCommon.c.
 *
 * Results:
 *       xmlNode or NULL.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

xmlNode *
BrokerXml::GetChild(xmlNode *parentNode,    // IN
                    const char *targetName) // IN
{
   if (parentNode) {
      for (xmlNode *currentNode = parentNode->children; currentNode;
           currentNode = currentNode->next) {
         if (XML_ELEMENT_NODE == currentNode->type) {
            const char *currentName = (const char*) currentNode->name;
            /*
             * Be careful. XML is normally case-sensitive, but I am
             * being generous and allowing case differences.
             */
            if (currentName && (0 == Str_Strcasecmp(currentName, targetName))) {
               return currentNode;
            }
         }
      }
   }
   return NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetChildContent --
 *
 *       Get the text content from a named child node.
 *
 * Results:
 *       Content Util::string, possibly empty.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BrokerXml::GetChildContent(xmlNode *parentNode,    // IN
                           const char *targetName) // IN
{
   if (parentNode) {
      xmlNode *node = GetChild(parentNode, targetName);
      if (node) {
         return GetContent(node);
      }
   }
   return "";
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetChildContentInt --
 *
 *       Get the int content from a named child node.
 *
 * Results:
 *       Integer value or -1 if invalid content or empty.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

int
BrokerXml::GetChildContentInt(xmlNode *parentNode,    // IN
                              const char *targetName) // IN
{
   Util::string strval = GetChildContent(parentNode, targetName);
   if (strval.empty()) {
      return -1;
   }
   return strtol(strval.c_str(), NULL, 10);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetChildContentBool --
 *
 *       Get the bool content from a named child node.
 *
 * Results:
 *       true if XML value is "1", "TRUE", or "YES".  false, otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::GetChildContentBool(xmlNode *parentNode,    // IN
                               const char *targetName) // IN
{
   Util::string strval = GetChildContent(parentNode, targetName);
   if (strval == "1" || strval == "true" || strval == "TRUE" ||
       strval == "yes" || strval == "YES") {
      return true;
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Result::Parse --
 *
 *       Parse the common <result> success/fault element returned in all
 *       requests.
 *
 * Results:
 *       true if parsed success result, false if parse failed or a fault result
 *       was received and the onAbort handler was invoked.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::Result::Parse(xmlNode *parentNode,     // IN
                         Util::AbortSlot onAbort) // IN
{
   ASSERT(parentNode);

   result = GetChildContent(parentNode, "result");
   if (result.empty()) {
      onAbort(false, Util::exception(CDK_MSG(invalidResponseEmptyResult,
         "Invalid response from broker: Invalid \"result\" in XML.")));
      return false;
   }

   // Non-ok is not necessarily a failure
   if (result != "ok") {
      errorCode = GetChildContent(parentNode, "error-code");
      errorMessage = GetChildContent(parentNode, "error-message");
      userMessage = GetChildContent(parentNode, "user-message");
   }

   // Error code or message is always a failure
   if (!errorCode.empty() || !errorMessage.empty()) {
      if (!userMessage.empty()) {
         onAbort(false, Util::exception(userMessage, errorCode));
      } else if (!errorMessage.empty()) {
         onAbort(false, Util::exception(errorMessage, errorCode));
      } else {
         onAbort(false, Util::exception(
                    Util::Format(CDK_MSG(errorResponse.unknownError,
                                          "Unknown error: %s").c_str(),
                                 errorCode.c_str()),
                    errorCode));
      }
      return false;
   }

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Param::Parse --
 *
 *       Parse a <param> node parentNode containing a name element and
 *       possibly multiple value elements.
 *
 * Results:
 *       true if parsed successfully, false otherwise and the onAbort handler
 *       was invoked.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::Param::Parse(xmlNode *parentNode,     // IN
                        Util::AbortSlot onAbort) // IN
{
   name = GetChildContent(parentNode, "name");
   if (name.empty()) {
      onAbort(false, Util::exception(CDK_MSG(invalidResponseParamNoNameValue,
         "Invalid response from broker: Parameter with no name.")));
      return false;
   }

   readOnly = (GetChild(parentNode, "readonly") != NULL);

   xmlNode *valuesNode = GetChild(parentNode, "values");
   if (valuesNode) {
      for (xmlNode *valueNode = valuesNode->children; valueNode;
           valueNode = valueNode->next) {
         if (Str_Strcasecmp((const char *) valueNode->name, "value") == 0) {
            Util::string valueStr = GetContent(valueNode);
            if (!valueStr.empty()) {
               values.push_back(valueStr);
            }
         }
      }
   }

   if (values.size() == 0) {
      onAbort(false, Util::exception(Util::Format(
         CDK_MSG(invalidResponseParamNoValue,
                  "Invalid response from broker: Parameter \"%s\" has no "
                  "value.").c_str(),
         name.c_str())));
      return false;
   }

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthInfo::Parse --
 *
 *       Parse an <authentication> node and its <param> children values.
 *
 * Results:
 *       true if parsed successfully, false otherwise and the onAbort handler
 *       was invoked.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::AuthInfo::Parse(xmlNode *parentNode,     // IN
                           Util::AbortSlot onAbort) // IN
{
   ASSERT(parentNode);

   xmlNode *authNode = GetChild(parentNode, "authentication");
   if (!authNode) {
      onAbort(false, Util::exception(CDK_MSG(invalidResponseNoAuth,
         "Invalid response from broker: Invalid \"authentication\" in XML.")));
      return false;
   }

   xmlNode *screenNode = GetChild(authNode, "screen");
   if (!screenNode) {
      onAbort(false, Util::exception(CDK_MSG(invalidResponseNoScreen,
         "Invalid response from broker: Invalid \"screen\" in XML.")));
      return false;
   }

   name = GetChildContent(screenNode, "name");
   if (GetAuthType() == AUTH_NONE) {
      Log("Broker XML AuthInfo name unknown: \"%s\"\n", name.c_str());
      onAbort(false, Util::exception(CDK_MSG(badAuthType,
         "Invalid response from broker: Invalid \"name\" in XML.")));
      return false;
   }

   title = GetChildContent(screenNode, "title");
   text = GetChildContent(screenNode, "text");

   xmlNode *paramsNode = GetChild(screenNode, "params");
   if (paramsNode) {
      for (xmlNode *paramNode = paramsNode->children; paramNode;
           paramNode = paramNode->next) {
         if (Str_Strcasecmp((const char *) paramNode->name, "param") == 0) {
            Param param;
            if (!param.Parse(paramNode, onAbort)) {
               return false;
            }
            params.push_back(param);
         }
      }
   }

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthInfo::GetParam --
 *
 *       Accessor for the values associated with the named param, and optional
 *       read-only specifier.
 *
 * Results:
 *       List of param values. If readOnly is non-NULL, it is set to true if
 *       the param has been specified read-only, false otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

std::vector<Util::string>
BrokerXml::AuthInfo::GetParam(const Util::string name, // IN
                              bool *readOnly)          // OUT/OPT
   const
{
   for (std::vector<Param>::const_iterator i = params.begin();
        i != params.end(); i++) {
      if (i->name == name) {
         if (readOnly) {
            *readOnly = i->readOnly;
         }
         return i->values;
      }
   }
   return std::vector<Util::string>();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthInfo::GetAuthType --
 *
 *      Returns the current type of authentication: disclaimer, SecurID,
 *      or password.
 *
 * Results:
 *      The AuthType.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

BrokerXml::AuthType
BrokerXml::AuthInfo::GetAuthType()
   const
{
   if (name == "disclaimer") {
      return AUTH_DISCLAIMER;
   } else if (name == "securid-passcode") {
      return AUTH_SECURID_PASSCODE;
   } else if (name == "securid-nexttokencode") {
      return AUTH_SECURID_NEXTTOKENCODE;
   } else if (name == "securid-pinchange") {
      return AUTH_SECURID_PINCHANGE;
   } else if (name == "securid-wait") {
      return AUTH_SECURID_WAIT;
   } else if (name == "windows-password") {
      return AUTH_WINDOWS_PASSWORD;
   } else if (name == "windows-password-expired") {
      return AUTH_WINDOWS_PASSWORD_EXPIRED;
   } else {
      return AUTH_NONE;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthInfo::GetDisclaimer --
 *
 *       Accessor for the "text" param value in a "disclaimer" AuthInfo.
 *
 * Results:
 *       The value of the "text" param if it has exactly one value;
 *       empty string otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BrokerXml::AuthInfo::GetDisclaimer()
   const
{
   std::vector<Util::string> values = GetParam("text");
   return values.size() == 1 ? values[0] : "";
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthInfo::GetUsername --
 *
 *       Accessor for the "username" param value and its read-only status.
 *
 * Results:
 *       The value of the "username" param if it has exactly one value;
 *       empty string otherwise. readOnly, if given, is set to the param's
 *       read-only state.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BrokerXml::AuthInfo::GetUsername(bool *readOnly) // OUT/OPT
   const
{
   std::vector<Util::string> values = GetParam("username", readOnly);
   if (values.size() == 1) {
      return values[0];
   } else {
      return "";
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthInfo::GetError --
 *
 *       Accessor for the "error" param values, concatenated with a newline.
 *
 * Results:
 *       String of concatenated error values.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BrokerXml::AuthInfo::GetError()
   const
{
   std::vector<Util::string> values = GetParam("error");
   Util::string err;
   for (std::vector<Util::string>::const_iterator i = values.begin();
        i != values.end(); i++) {
      if (!err.empty()) {
         err += "\n";
      }
      err += *i;
   }
   return err;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Configuration::Parse --
 *
 *       Parse a <configuration> parentNode's children, currently consisting of
 *       optional authentication information.
 *
 * Results:
 *       Always true.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::Configuration::Parse(xmlNode *parentNode,     // IN
                                Util::AbortSlot onAbort) // IN
{
   ASSERT(parentNode);

   // Authentication info seems optional
   if (GetChild(parentNode, "authentication")) {
      return authInfo.Parse(parentNode, onAbort);
   } else {
      return true;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthResult::Parse --
 *
 *       Parse a <submit-authentication> parentNode's children, currently
 *       consisting of optional authentication information.
 *
 * Results:
 *       Always true.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::AuthResult::Parse(xmlNode *parentNode,     // IN
                             Util::AbortSlot onAbort) // IN
{
   ASSERT(parentNode);

   // Authentication info seems optional
   if (GetChild(parentNode, "authentication")) {
      return authInfo.Parse(parentNode, onAbort);
   } else {
      return true;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Tunnel::Parse --
 *
 *       Parse a <tunnel-connection> parentNode's children.
 *
 * Results:
 *       Always true.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::Tunnel::Parse(xmlNode *parentNode,     // IN
                         Util::AbortSlot onAbort) // IN
{
   connectionId = GetChildContent(parentNode, "connection-id");
   statusPort = GetChildContentInt(parentNode, "status-port");
   server1 = GetChildContent(parentNode, "server1");
   server2 = GetChildContent(parentNode, "server2");
   server1 = GetChildContent(parentNode, "server1");
   generation = GetChildContentInt(parentNode, "generation");
   bypassTunnel = GetChildContentBool(parentNode, "bypass-tunnel");
   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::UserPreferences::Parse --
 *
 *       Parse a <user-preferences> subelement of the parentNode, including
 *       loading individual preference key/value pairs.
 *
 * Results:
 *       Always true.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::UserPreferences::Parse(xmlNode *parentNode,     // IN
                                  Util::AbortSlot onAbort) // IN
{
   xmlNode *userPrefsNode = GetChild(parentNode, "user-preferences");
   if (userPrefsNode) {
      for (xmlNode *prefNode = userPrefsNode->children; prefNode;
           prefNode = prefNode->next) {
         if (Str_Strcasecmp((const char*) prefNode->name, "preference") == 0) {
            Preference pref;
            pref.first = (const char*) xmlGetProp(prefNode, (const xmlChar*) "name");
            pref.second = GetContent(prefNode);
            preferences.push_back(pref);
         }
      }
   }

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Desktop::Parse --
 *
 *       Parse a <desktop> parentNode's content.
 *
 * Results:
 *       Always true.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::Desktop::Parse(xmlNode *parentNode,     // IN
                          Util::AbortSlot onAbort) // IN
{
   id = GetChildContent(parentNode, "id");
   name = GetChildContent(parentNode, "name");
   type = GetChildContent(parentNode, "type");
   state = GetChildContent(parentNode, "state");
   sessionId = GetChildContent(parentNode, "session-id");
   resetAllowed = GetChildContentBool(parentNode, "reset-allowed");
   resetAllowedOnSession = GetChildContentBool(parentNode,
                                               "reset-allowed-on-session");
   return userPreferences.Parse(parentNode, onAbort);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::DesktopConnection::Parse --
 *
 *       Parse a <desktop-connection> parentNode's content.
 *
 * Results:
 *       Always true.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::DesktopConnection::Parse(xmlNode *parentNode,     // IN
                                    Util::AbortSlot onAbort) // IN
{
   id = GetChildContent(parentNode, "id");
   address = GetChildContent(parentNode, "address");
   port = GetChildContentInt(parentNode, "port");
   protocol = GetChildContent(parentNode, "protocol");
   username = GetChildContent(parentNode, "username");
   password = GetChildContent(parentNode, "password");
   domainName = GetChildContent(parentNode, "domain-name");
   enableUSB = GetChildContentBool(parentNode, "enable-usb");
   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::EntitledDesktops::Parse --
 *
 *       Parse a <desktops> parentNode's <desktop> children, and collect all the
 *       children.
 *
 * Results:
 *       Always true.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::EntitledDesktops::Parse(xmlNode *parentNode,     // IN
                                   Util::AbortSlot onAbort) // IN
{
   for (xmlNode *desktopNode = parentNode->children; desktopNode;
        desktopNode = desktopNode->next) {
      if (Str_Strcasecmp((const char*) desktopNode->name, "desktop") == 0) {
         Desktop desktop;
         if (!desktop.Parse(desktopNode, onAbort)) {
            return false;
         }
         desktops.push_back(desktop);
      }
   }

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Encode --
 *
 *      Encode an XML text string, escaping entity characters correctly using
 *      xmlEncodeSpecialChars.
 *
 * Results:
 *      XML-safe encoded Util::string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BrokerXml::Encode(const Util::string& val) // IN
{
   xmlChar *enc = xmlEncodeSpecialChars(NULL, (const xmlChar*)val.c_str());
   Util::string result = (const char*)enc;
   free(enc);
   return result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SendRequest --
 *
 *      Post an XML API request using basicHttp.
 *
 * Results:
 *      true if request was queued successfully.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::SendRequest(RequestState &req) // IN
{
   ASSERT(!req.requestOp.empty());
   ASSERT(!req.responseOp.empty());

   // NOTE: We get a 404 if we access "/broker/xml/"
   Util::string url = Util::Format("%s://%s:%d/broker/xml",
                                   mSecure ? "https" : "http",
                                   mHostname.c_str(), mPort);

   const char *hdr = mVersion == VERSION_1 ? BROKER_V1_HDR : BROKER_V2_HDR;
   Util::string body;

   if (req.args.empty()) {
      body = Util::Format("%s<%s/>" BROKER_TAIL, hdr, req.requestOp.c_str());
   } else {
      body = Util::Format("%s<%s>%s</%s>" BROKER_TAIL, hdr,
                          req.requestOp.c_str(), req.args.c_str(),
                          req.requestOp.c_str());
   }

   DEBUG_ONLY(Warning("BROKER REQUEST: %s\n", body.c_str()));

   req.request = BasicHttp_CreateRequest(url.c_str(), BASICHTTP_METHOD_POST,
                                         mCookieJar, NULL, body.c_str());
   ASSERT_MEM_ALLOC(req.request);

   bool success = BasicHttp_SendRequest(req.request, &BrokerXml::OnResponse,
                                        this);
   if (success) {
      mActiveRequests.push_back(req);
   }

   return success;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::OnResponse --
 *
 *      Parse an XML API response based on the response operation.  Invokes the
 *      onAbort/onDone handler passed to the initial request.
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
BrokerXml::OnResponse(BasicHttpRequest *request,   // IN
                      BasicHttpResponse *response, // IN
                      void *data)                  // IN
{
   BrokerXml *that = reinterpret_cast<BrokerXml*>(data);
   ASSERT(that);

   xmlDoc *doc = NULL;
   xmlNode *docNode;
   xmlNode *operationNode;
   Result result;

   RequestState state;
   for (std::list<RequestState>::iterator i = that->mActiveRequests.begin();
        i != that->mActiveRequests.end(); i++) {
      if ((*i).request == request) {
         state = *i;
         that->mActiveRequests.erase(i);
         break;
      }
   }
   ASSERT(state.request == request);

   if (response->errorCode != BASICHTTP_ERROR_NONE) {
      state.onAbort(false, Util::exception(CDK_MSG(cantConnect,
         "Could not connect to broker.")));
      goto exit;
   }

   DEBUG_ONLY(Warning("BROKER RESPONSE: %s\n", response->content));

   doc = xmlReadMemory(response->content, strlen(response->content),
                       "notused.xml", NULL, 0);
   if (!doc) {
      state.onAbort(false, Util::exception(CDK_MSG(invalidResponse,
         "Invalid response from broker: Malformed XML.")));
      goto exit;
   }

   docNode = xmlDocGetRootElement(doc);
   if (!docNode || Str_Strcasecmp((const char*)docNode->name, "broker") != 0) {
      state.onAbort(false, Util::exception(CDK_MSG(invalidResponseNoRoot,
         "Invalid response from broker: Malformed XML.")));
      goto exit;
   }

   // Protocol-level errors mean no operation node
   if (GetChildContent(docNode, "result") == "error") {
      Util::string errCode = GetChildContent(docNode, "error-code");
      Log("Broker XML general error: %s\n", errCode.c_str());
      if (result.Parse(docNode, state.onAbort)) {
         state.onAbort(false, 
            Util::exception(CDK_MSG(brokerXmlGeneralError,
               "Invalid response from broker: General error.")));
      }
      goto exit;
   }

   operationNode = GetChild(docNode, state.responseOp.c_str());
   if (!operationNode) {
      state.onAbort(false, Util::exception(Util::Format(
         CDK_MSG(invalidResponseNoOperation,
                  "Invalid response from broker: No \"%s\" element "
                  "in XML.").c_str(),
         state.responseOp.c_str())));
      goto exit;
   }

   if (!result.Parse(operationNode, state.onAbort)) {
      goto exit;
   }

   if (state.responseOp == "configuration") {
      Configuration config;
      if (config.Parse(operationNode, state.onAbort)) {
         state.onDone.configuration(result, config);
      }
   } else if (state.responseOp == "set-locale") {
      state.onDone.locale(result);
   } else if (state.responseOp == "submit-authentication") {
      AuthResult authResult;
      if (authResult.Parse(operationNode, state.onAbort)) {
         state.onDone.authentication(result, authResult);
      }
   } else if (state.responseOp == "tunnel-connection") {
      Tunnel tunnel;
      if (tunnel.Parse(operationNode, state.onAbort)) {
         state.onDone.tunnelConnection(result, tunnel);
      }
   } else if (state.responseOp == "desktops") {
      EntitledDesktops desktops;
      if (desktops.Parse(operationNode, state.onAbort)) {
         state.onDone.desktops(result, desktops);
      }
   } else if (state.responseOp == "user-global-preferences" ||
              state.responseOp == "set-user-global-preferences") {
      UserPreferences prefs;
      if (prefs.Parse(operationNode, state.onAbort)) {
         state.onDone.preferences(result, prefs);
      }
   } else if (state.responseOp == "set-user-desktop-preferences") {
      UserPreferences prefs;
      if (prefs.Parse(operationNode, state.onAbort)) {
         Util::string desktopId = GetChildContent(operationNode, "desktop-id");
         state.onDone.desktopPreferences(result, desktopId, prefs);
      }
   } else if (state.responseOp == "desktop-connection") {
      DesktopConnection conn;
      if (conn.Parse(operationNode, state.onAbort)) {
         state.onDone.desktopConnection(result, conn);
      }
   } else if (state.responseOp == "logout") {
      state.onDone.logout(result);
   } else if (state.responseOp == "kill-session") {
      state.onDone.killSession(result);
   } else if (state.responseOp == "reset-desktop") {
      state.onDone.reset(result);
   } else {
      NOT_REACHED();
   }

exit:
   BasicHttp_FreeRequest(request);
   BasicHttp_FreeResponse(response);
   xmlFreeDoc(doc);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetConfiguration --
 *
 *      Send a "get-configuration" request to the broker server.
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
BrokerXml::GetConfiguration(Util::AbortSlot onAbort,  // IN
                            ConfigurationSlot onDone) // IN
{
   RequestState req;
   req.requestOp = "get-configuration";
   req.responseOp = "configuration";
   req.onAbort = onAbort;
   req.onDone.configuration = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SetLocale --
 *
 *      Send a "set-locale" request to the broker server.
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
BrokerXml::SetLocale(Util::string locale,     // IN
                     Util::AbortSlot onAbort, // IN
                     LocaleSlot onDone)       // IN
{
   RequestState req;
   req.requestOp = "set-locale";
   req.responseOp = "set-locale";
   req.args = "<locale>" + Encode(locale) + "</locale>";
   req.onAbort = onAbort;
   req.onDone.locale = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SubmitAuthentication --
 *
 *      Send a "do-submit-authentication" request to the broker server.
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
BrokerXml::SubmitAuthentication(AuthInfo &auth,            // IN
                                Util::AbortSlot onAbort,   // IN
                                AuthenticationSlot onDone) // IN
{
   Util::string arg = "<screen>";

   if (!auth.name.empty()) {
      arg += "<name>" + Encode(auth.name) + "</name>";
   }
   if (!auth.title.empty()) {
      arg += "<title>" + Encode(auth.title) + "</title>";
   }
   if (!auth.title.empty()) {
      arg += "<text>" + Encode(auth.text) + "</text>";
   }

   arg += "<params>";
   for (std::vector<Param>::iterator i = auth.params.begin();
        i != auth.params.end(); i++) {
      arg += "<param>";
      arg += "<name>" + Encode((*i).name) + "</name>";

      arg += "<values>";
      for (std::vector<Util::string>::iterator j = (*i).values.begin();
           j != (*i).values.end(); j++) {
         arg += "<value>" + Encode((*j)) + "</value>";
      }
      arg += "</values>";

      if ((*i).readOnly) {
         arg += "<readonly/>";
      }
      arg += "</param>";
   }
   arg += "</params>";
   arg += "</screen>";

   RequestState req;
   req.requestOp = "do-submit-authentication";
   req.responseOp = "submit-authentication";
   req.args = arg;
   req.onAbort = onAbort;
   req.onDone.authentication = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::PasswordAuthentication --
 *
 *      Helper for SubmitAuthentication to send a "windows-password" auth info,
 *      containing username, password, and domain params.
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
BrokerXml::PasswordAuthentication(Util::string username,     // IN
                                  Util::string password,     // IN
                                  Util::string domain,       // IN
                                  Util::AbortSlot onAbort,   // IN
                                  AuthenticationSlot onDone) // IN
{
   AuthInfo authInfo;
   authInfo.name = "windows-password";

   Param usernameParam;
   usernameParam.name = "username";
   usernameParam.values.push_back(username);
   authInfo.params.push_back(usernameParam);

   Param passwdParam;
   passwdParam.name = "password";
   passwdParam.values.push_back(password);
   authInfo.params.push_back(passwdParam);

   Param domainParam;
   domainParam.name = "domain";
   domainParam.values.push_back(domain);
   authInfo.params.push_back(domainParam);

   SubmitAuthentication(authInfo, onAbort, onDone);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SecurIDUsernamePasscode --
 *
 *      Helper for SubmitAuthentication to send a "securid-passcode" auth info,
 *      containing username and passcode params.
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
BrokerXml::SecurIDUsernamePasscode(Util::string username,     // IN
                                   Util::string passcode,     // IN
                                   Util::AbortSlot onAbort,   // IN
                                   AuthenticationSlot onDone) // IN
{
   AuthInfo authInfo;
   authInfo.name = "securid-passcode";

   Param usernameParam;
   usernameParam.name = "username";
   usernameParam.values.push_back(username);
   authInfo.params.push_back(usernameParam);

   Param passcodeParam;
   passcodeParam.name = "passcode";
   passcodeParam.values.push_back(passcode);
   authInfo.params.push_back(passcodeParam);

   SubmitAuthentication(authInfo, onAbort, onDone);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SecurIDNextTokencode --
 *
 *      Helper for SubmitAuthentication to send a "securid-nexttokencode"
 *      auth info, containing a tokencode param.
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
BrokerXml::SecurIDNextTokencode(Util::string tokencode,    // IN
                                Util::AbortSlot onAbort,   // IN
                                AuthenticationSlot onDone) // IN
{
   AuthInfo authInfo;
   authInfo.name = "securid-nexttokencode";

   Param tokencodeParam;
   tokencodeParam.name = "tokencode";
   tokencodeParam.values.push_back(tokencode);
   authInfo.params.push_back(tokencodeParam);

   SubmitAuthentication(authInfo, onAbort, onDone);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SecurIDNextTokencode --
 *
 *      Helper for SubmitAuthentication to send a "securid-pinchange"
 *      auth info, containing two (hopefully matching) PIN values.
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
BrokerXml::SecurIDPins(Util::string pin1,         // IN
                       Util::string pin2,         // IN
                       Util::AbortSlot onAbort,   // IN
                       AuthenticationSlot onDone) // IN
{
   AuthInfo authInfo;
   authInfo.name = "securid-pinchange";

   Param pinParam1;
   pinParam1.name = "pin1";
   pinParam1.values.push_back(pin1);
   authInfo.params.push_back(pinParam1);

   Param pinParam2;
   pinParam2.name = "pin2";
   pinParam2.values.push_back(pin2);
   authInfo.params.push_back(pinParam2);

   SubmitAuthentication(authInfo, onAbort, onDone);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AcceptDisclaimer --
 *
 *      Helper for SubmitAuthentication to send a "disclaimer"
 *      AuthInfo with param "accept" = "true".
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
BrokerXml::AcceptDisclaimer(Util::AbortSlot onAbort,   // IN
                            AuthenticationSlot onDone) // IN
{
   AuthInfo authInfo;
   authInfo.name = "disclaimer";

   Param acceptParam;
   acceptParam.name = "accept";
   acceptParam.values.push_back("true");
   authInfo.params.push_back(acceptParam);

   SubmitAuthentication(authInfo, onAbort, onDone);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::ChangePassword --
 *
 *      Helper for SubmitAuthentication to send a "windows-password-expired"
 *      AuthInfo with params for old, new, and confirmed-new passwords.
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
BrokerXml::ChangePassword(Util::string oldPassword,  // IN
                          Util::string newPassword,  // IN
                          Util::string confirm,      // IN
                          Util::AbortSlot onAbort,   // IN
                          AuthenticationSlot onDone) // IN
{
   AuthInfo authInfo;
   authInfo.name = "windows-password-expired";

   Param oldParam;
   oldParam.name = "oldPassword";
   oldParam.values.push_back(oldPassword);
   authInfo.params.push_back(oldParam);

   Param newParam;
   newParam.name = "newPassword1";
   newParam.values.push_back(newPassword);
   authInfo.params.push_back(newParam);

   Param confirmParam;
   confirmParam.name = "newPassword2";
   confirmParam.values.push_back(confirm);
   authInfo.params.push_back(confirmParam);

   SubmitAuthentication(authInfo, onAbort, onDone);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetTunnelConnection --
 *
 *      Send a "get-tunnel-connection" request to the broker server.
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
BrokerXml::GetTunnelConnection(Util::AbortSlot onAbort,     // IN
                               TunnelConnectionSlot onDone) // IN
{
   RequestState req;
   req.requestOp = "get-tunnel-connection";
   req.responseOp = "tunnel-connection";
   req.onAbort = onAbort;
   req.onDone.tunnelConnection = onDone;
   SendRequest(req);
}



/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetDesktops --
 *
 *      Send a "get-desktops" request to the broker server.
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
BrokerXml::GetDesktops(Util::AbortSlot onAbort, // IN
                       DesktopsSlot onDone)     // IN
{
   RequestState req;
   req.requestOp = "get-desktops";
   req.responseOp = "desktops";
   req.onAbort = onAbort;
   req.onDone.desktops = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetUserGlobalPreferences --
 *
 *      Send a "get-user-global-preferences" request to the broker server.
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
BrokerXml::GetUserGlobalPreferences(Util::AbortSlot onAbort, // IN
                                    PreferencesSlot onDone)  // IN
{
   RequestState req;
   req.requestOp = "get-user-global-preferences";
   req.responseOp = "user-global-preferences";
   req.onAbort = onAbort;
   req.onDone.preferences = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SetUserGlobalPreferences --
 *
 *      Send a "set-user-global-preferences" request to the broker server.
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
BrokerXml::SetUserGlobalPreferences(UserPreferences &prefs,  // IN
                                    Util::AbortSlot onAbort, // IN
                                    PreferencesSlot onDone)  // IN
{
   Util::string arg = "<user-preferences>";
   for (std::vector<Preference>::iterator i = prefs.preferences.begin();
        i != prefs.preferences.end(); i++) {
      arg += Util::Format("<preferences name=\"%s\">%s</preference>",
                          Encode((*i).first).c_str(),
                          Encode((*i).second).c_str());
   }
   arg += "</user-preferences>";

   RequestState req;
   req.requestOp = "set-user-global-preferences";
   req.responseOp = "set-user-global-preferences";
   req.args = arg;
   req.onAbort = onAbort;
   req.onDone.preferences = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SetUserDesktopPreferences --
 *
 *      Send a "set-user-desktop-preferences" request to the broker server.
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
BrokerXml::SetUserDesktopPreferences(Util::string desktopId,        // IN
                                     UserPreferences &prefs,        // IN
                                     Util::AbortSlot onAbort,       // IN
                                     DesktopPreferencesSlot onDone) // IN
{
   ASSERT(!desktopId.empty());

   Util::string arg;
   arg = "<desktop-id>" + Encode(desktopId) + "</desktop-id>";
   arg += "<user-preferences>";
   for (std::vector<Preference>::iterator i = prefs.preferences.begin();
        i != prefs.preferences.end(); i++) {
      arg += Util::Format("<preference name=\"%s\">%s</preference>",
                          Encode((*i).first).c_str(),
                          Encode((*i).second).c_str());
   }
   arg += "</user-preferences>";

   RequestState req;
   req.requestOp = "set-user-desktop-preferences";
   req.responseOp = "set-user-desktop-preferences";
   req.args = arg;
   req.onAbort = onAbort;
   req.onDone.desktopPreferences = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetDesktopConnection --
 *
 *      Send a "get-desktop-connection" request to the broker server.
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
BrokerXml::GetDesktopConnection(Util::string desktopId,       // IN
                                Util::AbortSlot onAbort,      // IN
                                DesktopConnectionSlot onDone) // IN
{
   ASSERT(!desktopId.empty());

   RequestState req;
   req.requestOp = "get-desktop-connection";
   req.responseOp = "desktop-connection";
   req.args = "<desktop-id>" + Encode(desktopId) + "</desktop-id>";
   req.onAbort = onAbort;
   req.onDone.desktopConnection = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Logout --
 *
 *      Send a "do-logout" request to the broker server.
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
BrokerXml::Logout(Util::AbortSlot onAbort, // IN
                  LogoutSlot onDone)       // IN
{
   RequestState req;
   req.requestOp = "do-logout";
   req.responseOp = "logout";
   req.onAbort = onAbort;
   req.onDone.logout = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::KillSession --
 *
 *      Send a "kill-session" request (log out of remote desktop) to
 *      the broker server.
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
BrokerXml::KillSession(Util::string sessionId,  // IN
                       Util::AbortSlot onAbort, // IN
                       KillSessionSlot onDone)  // IN
{
   ASSERT(!sessionId.empty());

   RequestState req;
   req.requestOp = "kill-session";
   req.responseOp = "kill-session";
   req.args = "<session-id>" + Encode(sessionId) + "</session-id>";
   req.onAbort = onAbort;
   req.onDone.killSession = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::ResetDesktop --
 *
 *      Send a "reset-desktop" request (restart?) to the broker server.
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
BrokerXml::ResetDesktop(Util::string desktopId,  // IN
                        Util::AbortSlot onAbort, // IN
                        ResetDesktopSlot onDone) // IN
{
   ASSERT(!desktopId.empty());

   RequestState req;
   req.requestOp = "reset-desktop";
   req.responseOp = "reset-desktop";
   req.args = "<desktop-id>" + Encode(desktopId) + "</desktop-id>";
   req.onAbort = onAbort;
   req.onDone.reset = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::BadBrokerException --
 *
 *      Returns a standard exception for wacky broker responses.
 *
 * Results:
 *      The exception.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::exception
BrokerXml::BadBrokerException()
{
   return Util::exception(CDK_MSG(badBroker, "Invalid response from broker."));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::CancelRequests --
 *
 *      Cancel pending HTTP requests.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Request onAbort handlers are run with cancelled = true.
 *
 *-----------------------------------------------------------------------------
 */

void
BrokerXml::CancelRequests()
{
   std::list<Util::AbortSlot> slots;
   /*
    * It is extremely likely that an onAbort() handler will delete
    * this object, which will re-enter here and double-free things, so
    * clear the list, and then call the abort handlers.
    */
   for (std::list<RequestState>::iterator i = mActiveRequests.begin();
        i != mActiveRequests.end(); i++) {
      BasicHttp_FreeRequest(i->request);
      slots.push_back(i->onAbort);
   }
   mActiveRequests.clear();
   Log("Cancelling %d Broker XML requests.\n", slots.size());
   for (std::list<Util::AbortSlot>::iterator i = slots.begin();
        i != slots.end(); i++) {
      (*i)(true, Util::exception(CDK_MSG(requestCancelled,
                                         "Request cancelled by user.")));
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::ForgetCookies --
 *
 *      Forget all stored cookies.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Cookie jar is recreated.
 *
 *-----------------------------------------------------------------------------
 */

void
BrokerXml::ForgetCookies()
{
   BasicHttp_FreeCookieJar(mCookieJar);
   mCookieJar = BasicHttp_CreateCookieJar();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Tunnel::Tunnel --
 *
 *      Constrctor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

BrokerXml::Tunnel::Tunnel()
   : statusPort(-1),
     generation(-1),
     bypassTunnel(false)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Desktop::Desktop --
 *
 *      Constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

BrokerXml::Desktop::Desktop()
   : resetAllowed(false),
     resetAllowedOnSession(false)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::DesktopConnection::DesktopConnection --
 *
 *      Constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

BrokerXml::DesktopConnection::DesktopConnection()
   : port(-1),
     enableUSB(false)
{
}


} // namespace cdk
