// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <cstdint>
#include <iosfwd>

struct malleability_status
{
    enum Enum : uint8_t
    {
        non_malleable = 0x0,
        unclean_stack = 0x1,
        non_minimal_encoding = 0x2,
        high_s = 0x4,
        non_push_data = 0x8,
    };

    constexpr malleability_status() = default;
    constexpr malleability_status(uint8_t e):
        value_{static_cast<Enum>(0xf & e)}
    {}
    
    constexpr malleability_status(Enum value): value_{value}
    {}

    constexpr Enum value() const { return value_; }

    constexpr bool operator==(const malleability_status&) const = default;

private:
    Enum value_{non_malleable};
};

std::ostream& operator<<(std::ostream&, const malleability_status&);

constexpr bool is_malleable(malleability_status s)
{
    return s != malleability_status::non_malleable;
}

constexpr bool is_unclean_stack(malleability_status s)
{
    return s.value() & malleability_status::unclean_stack;
}

constexpr bool is_non_minimal_encoding(malleability_status s)
{
    return s.value() & malleability_status::non_minimal_encoding;
}

constexpr bool is_high_s(malleability_status s)
{ 
    return s.value() & malleability_status::high_s;
}

constexpr bool has_non_push_data(malleability_status s)
{
    return s.value() & malleability_status::non_push_data;
}

