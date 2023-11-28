// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#include <boost/uuid/uuid.hpp>

/**
 * Base class for association ID types. Currently only a UUID based ID is
 * supported, but this may change in future to include things like key based
 * IDs.
 *
 * The format of an AssociationID is as follows:
 * [Type of ID (1 byte)][ID (1 to 128 bytes)]
 */
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class AssociationID
{
  public:

    // Maximum length for a serialised association ID; long enough for a byte
    // to identify the type + 128 bytes of data
    static constexpr size_t MAX_ASSOCIATION_ID_LENGTH {129};

    // String used to denote a null (not-set) association ID
    static constexpr const char* NULL_ID_STR { "Not-Set" };

    // Supported association ID types
    enum class IDType : uint8_t { UUID = 0 };

    virtual ~AssociationID() = default;

    // Equality operator
    bool operator==(const AssociationID& that) const { return IsEqual(that); }

    // Factory method to reconstruct an association ID from received bytes
    static std::unique_ptr<AssociationID> Make(const std::vector<uint8_t>& bytes);

    // String converter (for debugging and logging)
    virtual std::string ToString() const = 0;

    // Get as an array of bytes for sending over the network.
    // The returned data includes the type byte.
    virtual std::vector<uint8_t> GetBytes() const = 0;

  protected:

    // Equality method
    virtual bool IsEqual(const AssociationID& that) const = 0;

};
using AssociationIDPtr = std::shared_ptr<AssociationID>;

/**
 * Intermediate association ID class that uses the Curiously Recurring
 * Template Pattern to implement equality comparison. 
 *
 * Concrete implementations of association IDs should derive from this
 * rather than the AssociationID base class.
 */
template<class Derived>
class AssociationID_ : public AssociationID
{
  protected:

    // Equality method
    bool IsEqual(const AssociationID& that) const final
    {
        // First check types are equal
        if(typeid(*this) != typeid(that))
        {
            return false;
        }

        // Call derived class equality comparison
        const Derived& thisDerived { static_cast<const Derived&>(*this) };
        const Derived& thatDerived { static_cast<const Derived&>(that) };
        return thisDerived == thatDerived;
    }

  private:

    // Force derived classes to implement operator==
    bool operator==(const AssociationID_&) = delete;
};

/**
 * A UUID based association ID.
 */
class UUIDAssociationID : public AssociationID_<UUIDAssociationID>
{
  public:

    UUIDAssociationID();
    UUIDAssociationID(const std::vector<uint8_t>& bytes);

    // Equality operator
    bool operator==(const UUIDAssociationID& that) const;

    // String converter
    std::string ToString() const override;

    // Get as an array of bytes for sending over the network
    std::vector<uint8_t> GetBytes() const override;

  private:

    boost::uuids::uuid mID {};
};
