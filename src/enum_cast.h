// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

/*
 * A general purpose mechanism for casting between enums and strings.
 *
 * Given an enumeration, we also provide a function enumTable() that returns
 * an enumTableT specifying a mapping between the enumeration and the
 * castable string values. With that in place we can perform casting with
 * enum_cast<Enum>(string) or enum_cast<string>(Enum).
 *
 * Eg:
 *
 * enum class MyTypes { UNKNOWN, Type1, Type2 };
 *
 * const enumTableT<MyTypes, std::string>& enumTable(MyTypes, std::string)
 * {
 *   static enumTableT<MyTypes, std::string> table
 *   {
 *      {MyTypes::UNKNOWN, "Unknown"}, {MyTypes::Type1, "Type 1"}, {MyTypes::Type2, "Type 2"}
 *   };
 *   return table;
 * }
 *
 * std::string str { enum_cast<std::string>(MyTypes::Type1) };
 * MyTypes mytype { enum_cast<MyTypes>(str) };
 */

#pragma once

#include <string>
#include <unordered_map>

// The type returned by all enum_table() functions.
template <typename From>
class enumTableT
{
  public:

    // Constructor - requires table.size() > 0
    enumTableT(std::initializer_list<std::pair<const From, std::string>> table)
    : mLookupTable{table}, mDefaultValue{*table.begin()}
    {
        // Populate reverse lookup table
        for(const auto& item : mLookupTable)
        {
            mReverseLookupTable[item.second] = item.first;
        }
    }

    // Cast from enum to string
    template <typename CastType>
    const std::string& castToString(const CastType& from) const
    {
        auto it { mLookupTable.find(from) };
        if(it != mLookupTable.end())
        {
            return it->second;
        }
        return mDefaultValue.second;
    }

    // Cast from string to enum
    template <typename CastType>
    const From& castToEnum(const CastType& to) const
    {
        auto it { mReverseLookupTable.find(to) };
        if(it != mReverseLookupTable.end())
        {
            return it->second;
        }
        return mDefaultValue.first;
    }

  private:

    using LookupTable = std::unordered_map<From, std::string>;
    using ReverseLookupTable = std::unordered_map<std::string, From>;

    LookupTable mLookupTable {};
    ReverseLookupTable mReverseLookupTable {};
    typename LookupTable::value_type mDefaultValue {};

};

// Cast to string
template<typename ToType = std::string, typename FromType>
std::string enum_cast(const FromType& value)
{
    return enumTable(FromType{}).castToString(value);
}

// Cast from string
template<typename ToType>
ToType enum_cast(const std::string& value)
{
    return enumTable(ToType{}).castToEnum(value);
}

// Cast from convertable to string
template<typename ToType>
ToType enum_cast(const char* value)
{
    return enumTable(ToType{}).castToEnum(value);
}

