#pragma once
/*
 *      Copyright (C) 2005-2010 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "StdString.h"
#include <map>
#include <stdio.h>
#include <string>
#include <iostream>
#include "ITransportLayer.h"
#include "IAnnouncer.h"
#include "json/json.h"
#include "JSONUtils.h"
#include "JSONServiceDescription.h"

namespace JSONRPC
{
  /*!
   \ingroup jsonrpc
   \brief JSON RPC handler

   Sets up and manages all needed information to process
   JSON RPC requests and answering with the appropriate
   JSON RPC response (actual response or error message).
   */
  class CJSONRPC : public CJSONUtils
  {
  public:
    /*!
     \brief Initializes the JSON RPC handler
     */
    static void Initialize();

    /*
     \brief Handles an incoming JSON RPC request
     \param inputString received JSON RPC request
     \param transport Transport protocol on which the request arrived
     \param client Client which sent the request
     \return JSON RPC response to be sent back to the client

     Parses the received input string for the called method and provided
     parameters. If the request does not conform to the JSON RPC 2.0
     specification an error is returned. Otherwise the parameters provided
     in the request are checked for validity and completeness. If the request
     is valid and the requested method exists it is called and executed.
     */
    static CStdString MethodCall(const CStdString &inputString, ITransportLayer *transport, IClient *client);

    static JSON_STATUS Introspect(const CStdString &method, ITransportLayer *transport, IClient *client, const Json::Value& parameterObject, Json::Value &result);
    static JSON_STATUS Version(const CStdString &method, ITransportLayer *transport, IClient *client, const Json::Value& parameterObject, Json::Value &result);
    static JSON_STATUS Permission(const CStdString &method, ITransportLayer *transport, IClient *client, const Json::Value& parameterObject, Json::Value &result);
    static JSON_STATUS Ping(const CStdString &method, ITransportLayer *transport, IClient *client, const Json::Value& parameterObject, Json::Value &result);
    static JSON_STATUS GetNotificationFlags(const CStdString &method, ITransportLayer *transport, IClient *client, const Json::Value& parameterObject, Json::Value &result);
    static JSON_STATUS SetNotificationFlags(const CStdString &method, ITransportLayer *transport, IClient *client, const Json::Value& parameterObject, Json::Value &result);
    static JSON_STATUS Notify(const CStdString &method, ITransportLayer *transport, IClient *client, const Json::Value& parameterObject, Json::Value &result);
  
  private:
    static void setup();
    static bool HandleMethodCall(Json::Value& request, Json::Value& response, ITransportLayer *transport, IClient *client);
    static inline bool IsProperJSONRPC(const Json::Value& inputroot);

    inline static void BuildResponse(const Json::Value& request, JSON_STATUS code, const Json::Value& result, Json::Value& response);

    static JsonRpcMethodMap m_methodMaps[];
    static bool m_initialized;
  };
}
