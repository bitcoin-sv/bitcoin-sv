// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <double_spend/dscallback_msg.h>
#include <logging.h>
#include <net/netbase.h>
#include <script/script.h>
#include <streams.h>

namespace
{
    // Check we only have a single address type in the list
    void CheckAddressesAreSameType(Network& addrsType, Network thisAddrType)
    {
        if(addrsType == NET_UNROUTABLE)
        {
            addrsType = thisAddrType;
        }
        else if(addrsType != thisAddrType)
        {
            throw std::runtime_error("Endpoint addresses must all be of the same type");
        }
    }
}

DSCallbackMsg::DSCallbackMsg(uint8_t version, const std::vector<std::string>& addrs,
    const std::vector<uint32_t>& inputs)
: mVersion{version}, mInputs{inputs}
{
    // 0 IP addresses are not allowed
    if(addrs.empty())
    {
        throw std::runtime_error("DSCallbackMsg provided 0 IP addresses");
    }

    Network addrsType { NET_UNROUTABLE };

    // Convert string addresses to bytes in network byte order
    for(const std::string& addrStr : addrs)
    {
        CNetAddr addr {};
        if(LookupHost(addrStr.c_str(), addr, false))
        {
            if(addr.IsIPv4())
            {
                // Check all addresses are IPv4
                CheckAddressesAreSameType(addrsType, NET_IPV4);

                struct in_addr ip4addr {};
                if(addr.GetInAddr(&ip4addr))
                {
                    const uint8_t* bytes { reinterpret_cast<uint8_t*>(&ip4addr) };
                    std::vector<uint8_t> addrBin { bytes, bytes + 4 };
                    mIPAddrs.push_back(addrBin);
                }
                else
                {
                    throw std::runtime_error("Error converting string IPv4 to binary: " + addrStr);
                }
            }
            else if(addr.IsIPv6())
            {
                // Check all addresses are IPv6
                CheckAddressesAreSameType(addrsType, NET_IPV6);

                struct in6_addr ip6addr {};
                if(addr.GetIn6Addr(&ip6addr))
                {
                    const uint8_t* bytes { reinterpret_cast<uint8_t*>(&ip6addr) };
                    std::vector<uint8_t> addrBin { bytes, bytes + 16 };
                    mIPAddrs.push_back(addrBin);
                }
                else
                {
                    throw std::runtime_error("Error converting string IPv6 to binary: " + addrStr);
                }
            }
            else
            {
                throw std::runtime_error(addrStr + " is neither IPv4 or IPv6");
            }
        }
        else
        {
            LogPrint(BCLog::DOUBLESPEND, "DSCallbackMsg failed to lookup address %s\n", addrStr);
        }
    }
}

DSCallbackMsg::DSCallbackMsg(const CScript& script)
{
    // Check script is of correct type
    if(!IsDSNotification(script))
    {
        throw std::runtime_error("Script is not a double-spend enabled OP_RETURN");
    }

    std::vector<uint8_t> msgBytes {};
    opcodetype opcodeRet {};
    // Callback message starts 7 bytes in from the start of the script
    CScript::const_iterator pc { script.begin() + 7 };
    if(!script.GetOp(pc, opcodeRet, msgBytes))
    {
        throw std::runtime_error("Failed to extract callback message from script");
    }

    // Deserialise callback message bytes to ourselves
    CDataStream stream { msgBytes, SER_NETWORK, 0 };
    stream >> *this;
}

// Helper to convert an IPAddr to a string
std::string DSCallbackMsg::IPAddrToString(const IPAddr& addr)
{
    // Sanity check
    if(addr.size() != 4 && addr.size() != 16)
    {
        throw std::runtime_error("Bad size for IPAddr");
    }

    // Convert using CNetAddr
    CNetAddr netAddr {};
    Network net { addr.size() == 4 ? NET_IPV4 : NET_IPV6 };
    netAddr.SetRaw(net, addr.data());
    return netAddr.ToStringIP();
}

