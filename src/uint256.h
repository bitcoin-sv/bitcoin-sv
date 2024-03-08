// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UINT256_H
#define BITCOIN_UINT256_H

#include "crypto/common.h"
#include "utilstrencodings.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "boost/functional/hash.hpp"

/** Template base class for fixed-sized opaque blobs. */
template <unsigned int BITS> class base_blob {
protected:
    enum { WIDTH = BITS / 8 };
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    uint8_t data[WIDTH];

public:
    // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
    base_blob() { memset(data, 0, sizeof(data)); }
    
    template<typename T>
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    base_blob(T first, T last)
    {
        assert(std::distance(first, last) == sizeof(data));
        std::copy(first, last, &data[0]);
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    explicit base_blob(const std::vector<uint8_t> &vch) {
        assert(vch.size() == sizeof(data));
        // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
        memcpy(data, &vch[0], sizeof(data));
    }

    bool IsNull() const {
        for (int i = 0; i < WIDTH; i++)
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            if (data[i] != 0) return false;
        return true;
    }

    // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
    void SetNull() { memset(data, 0, sizeof(data)); }

    inline int Compare(const base_blob &other) const {
        // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
        return memcmp(data, other.data, sizeof(data));
    }

    friend inline bool operator==(const base_blob &a, const base_blob &b) {
        return a.Compare(b) == 0;
    }
    friend inline bool operator!=(const base_blob &a, const base_blob &b) {
        return a.Compare(b) != 0;
    }
    friend inline bool operator<(const base_blob &a, const base_blob &b) {
        return a.Compare(b) < 0;
    }

    std::string GetHex() const {
        std::string hex(WIDTH * 2, 0);
        for(unsigned int i = 0; i < WIDTH; ++i) {
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
            uint8_t c = data[WIDTH - i - 1];
            // NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result)
            hex[i * 2] = hexmap[c >> 4];
            hex[i * 2 + 1] = hexmap[c & 15];
            // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
        }
        return hex;
    }

    void SetHex(const char *psz) {
        // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
        memset(data, 0, sizeof(data));
        // skip leading spaces
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        while (isspace(*psz))
            ++psz;
        
        // skip 0x
        if (psz[0] == '0' && tolower(psz[1]) == 'x')
            psz += 2;

        // hex string to uint
        const char *pbegin = psz;
        while (::HexDigit(*psz) != -1)
            ++psz;

        --psz;
        // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
        uint8_t *p1 = data;
        uint8_t *pend = p1 + WIDTH;
        while (psz >= pbegin && p1 < pend) {
            *p1 = ::HexDigit(*psz);
            --psz;
            if (psz >= pbegin) {
                *p1 |= uint8_t(::HexDigit(*psz) << 4);
                --psz;
                ++p1;
            }
        }
        // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
    void SetHex(const std::string &str) { SetHex(str.c_str()); };

    std::string ToString() const { return GetHex(); };

    uint8_t *begin() { return &data[0]; }
    uint8_t *end() { return &data[WIDTH]; }

    const uint8_t *begin() const { return &data[0]; }
    const uint8_t *end() const { return &data[WIDTH]; }

    unsigned int size() const { return sizeof(data); }

    uint64_t GetUint64(int pos) const {
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const uint8_t *ptr = data + pos * 8;
        return ((uint64_t)ptr[0]) | ((uint64_t)ptr[1]) << 8 |
               ((uint64_t)ptr[2]) << 16 | ((uint64_t)ptr[3]) << 24 |
               ((uint64_t)ptr[4]) << 32 | ((uint64_t)ptr[5]) << 40 |
               ((uint64_t)ptr[6]) << 48 | ((uint64_t)ptr[7]) << 56;
        // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }

    template <typename Stream> void Serialize(Stream &s) const {
        s.write((char *)data, sizeof(data));
    }

    template <typename Stream> void Unserialize(Stream &s) {
        s.read((char *)data, sizeof(data));
    }
};

/**
 * 160-bit opaque blob.
 * @note This type is called uint160 for historical reasons only. It is an
 * opaque blob of 160 bits and has no integer operations.
 */
class uint160 : public base_blob<160> {
public:
    uint160() {}
    uint160(const base_blob<160> &b) : base_blob<160>(b) {}
    explicit uint160(const std::vector<uint8_t> &vch) : base_blob<160>(vch) {}
};

/**
 * 256-bit opaque blob.
 * @note This type is called uint256 for historical reasons only. It is an
 * opaque blob of 256 bits and has no integer operations. Use arith_uint256 if
 * those are required.
 */
class uint256 : public base_blob<256> {
public:
    uint256() {}
    uint256(const base_blob& b) : base_blob(b) {}
    explicit uint256(const std::vector<uint8_t>& vch)
        : uint256(vch.begin(), vch.end()) 
    {}

    template<typename T>
    uint256(T first, T last):base_blob{first, last}
    {}

    /**
     * A cheap hash function that just returns 64 bits from the result, it can
     * be used when the contents are considered uniformly random. It is not
     * appropriate when the value can easily be influenced from outside as e.g.
     * a network adversary could provide values to trigger worst-case behavior.
     */
    // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
    uint64_t GetCheapHash() const { return ReadLE64(data); }
};

inline std::ostream& operator<<(std::ostream& os, const uint256& i)
{
    os << i.ToString();
    return os;
}

/**
 * Specialise std::hash for uint256.
 */
namespace std
{
    template<>
    class hash<uint256> {
      public:
        size_t operator()(const uint256& u) const
        {
            return static_cast<size_t>(u.GetCheapHash());
        }
    };
}

/**
 * uint256 from const char *.
 * This is a separate function because the constructor uint256(const char*) can
 * result in dangerously catching uint256(0).
 */
inline uint256 uint256S(const char *str) {
    uint256 rv;
    rv.SetHex(str);
    return rv;
}

/**
 * uint256 from std::string.
 * This is a separate function because the constructor uint256(const std::string
 * &str) can result in dangerously catching uint256(0) via std::string(const
 * char*).
 */
inline uint256 uint256S(const std::string &str) {
    uint256 rv;
    rv.SetHex(str);
    return rv;
}

inline std::istream& operator>>(std::istream& stream, uint256& hash)
{
    std::string s;
    stream >> s;
    hash = uint256S(s);
    return stream;
}

inline uint160 uint160S(const char *str) {
    uint160 rv;
    rv.SetHex(str);
    return rv;
}
inline uint160 uint160S(const std::string &str) {
    uint160 rv;
    rv.SetHex(str);
    return rv;
}

inline std::size_t hash_value(const uint256& i)
{
    return boost::hash_range(i.begin(), i.end());
}

#endif // BITCOIN_UINT256_H
