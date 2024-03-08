// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <rpc/client_config.h>

namespace rpc::client
{

class HTTPRequest;
class HTTPResponse;

/**
 * Class to perform HTTP RPC/REST requests.
 */
class RPCClient
{
  public:
    explicit RPCClient(const RPCClientConfig& config) : mConfig{config} {}

    // Submit a request and wait for a response
    void SubmitRequest(HTTPRequest& request, HTTPResponse* response) const;

  private:

    // Config to describe the required connection type
    RPCClientConfig mConfig {};
};

}

