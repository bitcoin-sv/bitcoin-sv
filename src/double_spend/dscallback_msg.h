// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <serialize.h>

#include <array>
#include <string>
#include <vector>

class CScript;

/**
 * Encapsulate a double-spend callback message as embedded in an OP_RETURN
 * output.
 *
 * All fields are in network byte order.
 */
class DSCallbackMsg
{
  private:

    // Constants for bit masks
    static constexpr uint8_t IP_VERSION_MASK { 0x80 };
    static constexpr uint8_t PROTOCOL_VERSION_MASK { 0x1F };

  public:

    // IP address type
    using IPAddr = std::vector<uint8_t>;

    DSCallbackMsg() = default;
    DSCallbackMsg(uint8_t version, const std::vector<std::string>& addrs,
                  const std::vector<uint32_t>& inputs);
    DSCallbackMsg(const CScript& script);

    // Accessors
    [[nodiscard]] uint8_t GetVersionByte() const { return mVersion; }
    [[nodiscard]] unsigned GetProtocolVersion() const { return mVersion & PROTOCOL_VERSION_MASK; }
    [[nodiscard]] const std::vector<IPAddr>& GetAddresses() const { return mIPAddrs; }
    [[nodiscard]] const std::vector<uint32_t>& GetInputs() const { return mInputs; }

    // Helper to convert an IPAddr to a string
    [[nodiscard]] static std::string IPAddrToString(const IPAddr& addr);

    // Serialisation
    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        // Version and flags
        READWRITE(mVersion);

        // IP address list
        if(ser_action.ForRead())
        {
            uint64_t numAddrs {0};
            READWRITE(VARINT(numAddrs));
            // 0 addresses is an error
            if(numAddrs == 0)
            {
                throw std::runtime_error("DSCallbackMsg has IP address count 0");
            }

            mIPAddrs.clear();
            for(uint64_t i = 0; i < numAddrs; ++i)
            {
                if(mVersion & IP_VERSION_MASK)
                {
                    // IPv6
                    std::array<uint8_t, 16> bytes {};
                    READWRITE(FLATDATA(bytes));
                    mIPAddrs.emplace_back(bytes.begin(), bytes.end());
                }
                else
                {
                    // IPv4
                    std::array<uint8_t, 4> bytes {};
                    READWRITE(FLATDATA(bytes));
                    mIPAddrs.emplace_back(bytes.begin(), bytes.end());
                }
            }
        }
        else
        {
            READWRITE(VARINT(mIPAddrs.size()));
            for(const auto& addr : mIPAddrs)
            {
                if(mVersion & IP_VERSION_MASK)
                {
                    // IPv6
                    std::array<uint8_t, 16> bytes {};
                    memcpy(bytes.data(), addr.data(), 16);
                    READWRITE(FLATDATA(bytes));
                }
                else
                {
                    // IPv4
                    std::array<uint8_t, 4> bytes {};
                    memcpy(bytes.data(), addr.data(), 4);
                    READWRITE(FLATDATA(bytes));
                }
            }
        }

        // Inputs list
        if(ser_action.ForRead())
        {
            uint64_t numInputs {0};
            READWRITE(VARINT(numInputs));
            mInputs.clear();
            for(uint64_t i = 0; i < numInputs; ++i)
            {
                uint32_t input {0};
                READWRITE(VARINT(input));
                mInputs.push_back(input);
            }
        }
        else
        {
            READWRITE(VARINT(mInputs.size()));
            for(const auto& input : mInputs)
            {
                READWRITE(VARINT(input));
            }
        }

        // Strict encoding check; ensure there are no redundant trailing bytes left
        // unprocessed after reading.
        if(ser_action.ForRead())
        {
            bool ok {true};
            try
            {
                // There's no uniform way to check whether a stream has been drained, so
                // try reading a byte and see if the underlying stream complains.
                uint8_t dummy; // NOLINT(cppcoreguidelines-init-variables)
                READWRITE(dummy);
                ok = false;
            }
            catch(...) {} // NOLINT(bugprone-empty-catch)

            if(!ok)
            {
                throw std::runtime_error("DSCallbackMsg has trailing bytes");
            }
        }
    }

  private:

    // Version identifier
    uint8_t mVersion {0};

    // IP addresses
    std::vector<IPAddr> mIPAddrs {};

    // Inputs
    std::vector<uint32_t> mInputs {};

};

