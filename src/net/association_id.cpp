// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <net/association_id.h>
#include <sstream>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

std::unique_ptr<AssociationID> AssociationID::Make(const std::vector<uint8_t>& bytes)
{
    if(bytes.size() > 1)
    {
        // Get ID type
        uint8_t rawIdType { bytes[0] };
        AssociationID::IDType idType { static_cast<AssociationID::IDType>(rawIdType) };
        if(idType == AssociationID::IDType::UUID)
        {
            std::vector<uint8_t> uuidBytes { bytes.begin() + 1, bytes.end() };
            return std::make_unique<UUIDAssociationID>(uuidBytes);
        }
        else
        {
            std::stringstream err {};
            err << "Unsupported association ID type " << rawIdType;
            throw std::runtime_error(err.str());
        }
    }

    return nullptr;
}

UUIDAssociationID::UUIDAssociationID()
{
    // Generate a new random UUID
    boost::uuids::basic_random_generator<boost::mt19937> gen {};
    mID = gen();
}

// Construct from a list of raw bytes (the type ID byte has already been
// removed from the vector)
UUIDAssociationID::UUIDAssociationID(const std::vector<uint8_t>& bytes)
{
    // Reconstruct UUID from serialised bytes
    if(bytes.size() != mID.size())
    {
        std::stringstream err {};
        err << "Wrong number of bytes in UUID (" << bytes.size() << " != " << mID.size() << ")";
        throw std::runtime_error(err.str());
    }
    std::copy(bytes.begin(), bytes.end(), mID.begin());
}

std::string UUIDAssociationID::ToString() const
{
    return boost::uuids::to_string(mID);
}

std::vector<uint8_t> UUIDAssociationID::GetBytes() const
{
    std::vector<uint8_t> bytes(mID.size() + 1);

    // Set type
    bytes[0] = static_cast<uint8_t>(IDType::UUID);

    // Copy UUID bytes
    std::copy(mID.begin(), mID.end(), bytes.begin() + 1);
    return bytes;
}

bool UUIDAssociationID::operator==(const UUIDAssociationID& that) const
{
    return mID == that.mID;
}

