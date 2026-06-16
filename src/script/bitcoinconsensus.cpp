// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bitcoinconsensus.h"

#include "config.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/interpreter.h"
#include "script/script_error.h"
#include "script/script_flags.h"
#include "taskcancellation.h"
#include "version.h"

// The public bitcoinconsensus_SCRIPT_* flag values are hand-written in the
// standalone C header (bitcoinconsensus.h is the only installed header and must
// stay C-includable, so it cannot pull in the internal C++ script_flags.h).
// These assertions bind the two enums together.
#define ASSERT_SAME_FLAG(pub, internal) \
    static_assert(static_cast<uint32_t>(pub) == static_cast<uint32_t>(internal))
ASSERT_SAME_FLAG(bitcoinconsensus_SCRIPT_FLAGS_VERIFY_P2SH, SCRIPT_VERIFY_P2SH);
ASSERT_SAME_FLAG(bitcoinconsensus_SCRIPT_FLAGS_VERIFY_DERSIG, SCRIPT_VERIFY_DERSIG);
ASSERT_SAME_FLAG(bitcoinconsensus_SCRIPT_FLAGS_VERIFY_NULLDUMMY, SCRIPT_VERIFY_NULLDUMMY);
ASSERT_SAME_FLAG(bitcoinconsensus_SCRIPT_FLAGS_VERIFY_CHECKLOCKTIMEVERIFY, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY);
ASSERT_SAME_FLAG(bitcoinconsensus_SCRIPT_FLAGS_VERIFY_CHECKSEQUENCEVERIFY, SCRIPT_VERIFY_CHECKSEQUENCEVERIFY);
ASSERT_SAME_FLAG(bitcoinconsensus_SCRIPT_ENABLE_SIGHASH_FORKID, SCRIPT_ENABLE_SIGHASH_FORKID);
ASSERT_SAME_FLAG(bitcoinconsensus_SCRIPT_GENESIS, SCRIPT_GENESIS);
ASSERT_SAME_FLAG(bitcoinconsensus_SCRIPT_UTXO_AFTER_GENESIS, SCRIPT_UTXO_AFTER_GENESIS);
ASSERT_SAME_FLAG(bitcoinconsensus_SCRIPT_CHRONICLE, SCRIPT_CHRONICLE);
ASSERT_SAME_FLAG(bitcoinconsensus_SCRIPT_UTXO_AFTER_CHRONICLE, SCRIPT_UTXO_AFTER_CHRONICLE);
#undef ASSERT_SAME_FLAG

namespace {

/** A class that deserializes a single CTransaction one time. */
class TxInputStream
{
public:
    //NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    TxInputStream(int nTypeIn, int nVersionIn, const uint8_t* txTo, size_t txToLen)
        : m_type(nTypeIn),
          m_version(nVersionIn),
          m_data(txTo),
          m_remaining(txToLen)
    {
    }

    void read(char *pch, size_t nSize) {
        if (nSize > m_remaining)
            throw std::ios_base::failure(std::string(__func__) +
                                         ": end of data");

        if (pch == nullptr)
            throw std::ios_base::failure(std::string(__func__) +
                                         ": bad destination buffer");

        if (m_data == nullptr)
            throw std::ios_base::failure(std::string(__func__) +
                                         ": bad source buffer");

        memcpy(pch, m_data, nSize);
        m_remaining -= nSize;
        m_data += nSize; //NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }

    template <typename T> TxInputStream &operator>>(T &obj) {
        ::Unserialize(*this, obj);
        return *this;
    }

    int GetVersion() const { return m_version; }
    int GetType() const { return m_type; }

private:
    const int m_type;    //NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    const int m_version; //NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    const uint8_t* m_data;
    size_t m_remaining;
};

inline int set_error(bitcoinconsensus_error *ret,
                     bitcoinconsensus_error serror) {
    if (ret) *ret = serror;
    return 0;
}

} // namespace

/** Check that all specified flags are part of the libconsensus interface. */
static bool verify_flags(unsigned int flags) {
    if ((flags & ~(bitcoinconsensus_SCRIPT_FLAGS_VERIFY_ALL)) != 0) {
        return false;
    }
    // Mirror VerifyScript's valid_flags() precondition: a UTXO cannot be after
    // Chronicle without also being after Genesis. Reject it here so the caller
    // gets a clean bitcoinconsensus_ERR_INVALID_FLAGS rather than a bare verdict
    // of 0 with *err left at ERR_OK.
    if (IsUtxoAfterChronicle(flags) && !IsUtxoAfterGenesis(flags)) {
        return false;
    }
    return true;
}

static int verify_script(const Config& config,
                         const uint8_t* scriptPubKey,
                         unsigned int scriptPubKeyLen,
                         Amount amount,
                         const uint8_t* txTo,
                         //NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                         unsigned int txToLen,
                         unsigned int nIn,
                         unsigned int flags,
                         bitcoinconsensus_error* err)
{
    if (!verify_flags(flags)) {
        return bitcoinconsensus_ERR_INVALID_FLAGS;
    }
    try {
        TxInputStream stream(SER_NETWORK, PROTOCOL_VERSION, txTo, txToLen);
        CTransaction tx(deserialize, stream);
        if (nIn >= tx.vin.size())
            return set_error(err, bitcoinconsensus_ERR_TX_INDEX);
        if (GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) != txToLen)
            return set_error(err, bitcoinconsensus_ERR_TX_SIZE_MISMATCH);

        // Regardless of the verification result, the tx did not error.
        set_error(err, bitcoinconsensus_ERR_OK);

        PrecomputedTransactionData txdata(tx);
        auto source = task::CCancellationSource::Make();

        constexpr bool consensus{true};
        const auto params{make_verify_script_params(config.GetConfigScriptPolicy(), flags, consensus)};
        const auto
            res = VerifyScript(params,
                               source->GetToken(),
                               tx.vin[nIn].scriptSig,
                               //NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                               CScript(scriptPubKey, scriptPubKey + scriptPubKeyLen),
                               flags,
                               TransactionSignatureChecker(&tx, nIn, amount, txdata));

        return (res == SCRIPT_ERR_OK);
    }
    catch (const std::exception &) {
        // Error deserializing
        return set_error(err, bitcoinconsensus_ERR_TX_DESERIALIZE);
    }
}

int bitcoinconsensus_verify_script_with_amount(const Config& config,
                                               const uint8_t* scriptPubKey,
                                               //NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                                               unsigned int scriptPubKeyLen,
                                               int64_t amount,
                                               const uint8_t* txTo,
                                               unsigned int txToLen,
                                               unsigned int nIn,
                                               unsigned int flags,
                                               bitcoinconsensus_error* err)
{
    Amount am(amount);
    return ::verify_script(config, scriptPubKey, scriptPubKeyLen, am, txTo, txToLen,
                           nIn, flags, err);
}

int bitcoinconsensus_verify_script(const Config& config,
                                   const uint8_t* scriptPubKey,
                                   unsigned int scriptPubKeyLen,
                                   const uint8_t* txTo,
                                   unsigned int txToLen,
                                   unsigned int nIn,
                                   unsigned int flags,
                                   bitcoinconsensus_error* err)
{
    // Check flag validity before the amount guard so that ERR_INVALID_FLAGS
    // takes precedence over ERR_AMOUNT_REQUIRED when both conditions hold.
    if (!verify_flags(flags)) {
        return bitcoinconsensus_ERR_INVALID_FLAGS;
    }
    if (flags & bitcoinconsensus_SCRIPT_ENABLE_SIGHASH_FORKID) {
        return set_error(err, bitcoinconsensus_ERR_AMOUNT_REQUIRED);
    }

    Amount am(0);
    return ::verify_script(config, scriptPubKey, scriptPubKeyLen, am, txTo, txToLen,
                           nIn, flags, err);
}

unsigned int bitcoinconsensus_version() {
    // Just use the API version for now
    return BITCOINCONSENSUS_API_VER;
}
