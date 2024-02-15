// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_RPCCLIENT_H
#define BITCOIN_RPCCLIENT_H

#include <univalue.h>
#include <functional>

/** Convert positional arguments to command-specific RPC representation */
UniValue RPCConvertValues(const std::string &strMethod,
                          const std::vector<std::string> &strParams);

/** Convert named arguments to command-specific RPC representation */
UniValue RPCConvertNamedValues(const std::string &strMethod,
                               const std::vector<std::string> &strParams);

/** Non-RFC4627 JSON parser, accepts internal values (such as numbers, true,
 * false, null)
 * as well as objects and arrays.
 */
UniValue ParseNonRFCJSONValue(const std::string &strVal);

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
int AppInitRPC(int argc, char *argv[], const std::string& usage, const std::function<std::string(void)>& help); 

// Exit codes are EXIT_SUCCESS, EXIT_FAILURE, CONTINUE_EXECUTION 
static const int CONTINUE_EXECUTION = -1;  

UniValue CallRPC(const std::string &strMethod, const UniValue &params); 

//
// Exception thrown on connection error.  This error is used to determine when
// to wait if -rpcwait is given.
//
class CConnectionFailed : public std::runtime_error {
public:
    explicit CConnectionFailed(const std::string& msg) : std::runtime_error{msg} {}
};

// Exception thrown if comm's times out
class CConnectionTimeout : public CConnectionFailed {
public:
    explicit CConnectionTimeout(const std::string& msg) : CConnectionFailed{msg} {}
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
static const char DEFAULT_RPCCONNECT[] = "127.0.0.1";
static const int DEFAULT_HTTP_CLIENT_TIMEOUT = 900;
static const bool DEFAULT_NAMED = false;

#endif // BITCOIN_RPCCLIENT_H
