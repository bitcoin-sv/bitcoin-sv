// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sigcache.h"

#include "cuckoocache.h"
#include "memusage.h"
#include "pubkey.h"
#include "random.h"
#include "uint256.h"
#include "util.h"

#include <boost/thread.hpp>

namespace {

/**
 * Valid signature cache, to avoid doing expensive ECDSA signature checking
 * twice for every transaction (once when accepted into memory pool, and
 * again when accepted into the block chain). 
 * Invalid signature cache, to avoid doing expensive ECDSA signature checking
 * in case of an attack (invalid signature is cached and does not need to be
 * calculated again).
 */
class CSignatureCache {
private:
    //! Entries are SHA256(nonce || signature hash || public key || signature):
    uint256 nonce;
    typedef CuckooCache::cache<uint256, SignatureCacheHasher> map_type;
    map_type setValid;
    map_type setInvalid;
    boost::shared_mutex cs_sigcache;

public:
    CSignatureCache() { GetRandBytes(nonce.begin(), 32); }

    void ComputeEntry(uint256 &entry, const uint256 &hash,
                      const std::vector<uint8_t> &vchSig,
                      const CPubKey &pubkey) {
        CSHA256()
            .Write(nonce.begin(), 32)
            .Write(hash.begin(), 32)
            .Write(&pubkey[0], pubkey.size())
            .Write(&vchSig[0], vchSig.size())
            .Finalize(entry.begin());
    }

    bool Get(const uint256 &entry, const bool erase) {
        boost::shared_lock<boost::shared_mutex> lock(cs_sigcache);
        return setValid.contains(entry, erase);
    }

    bool GetInvalid(const uint256 &entry, const bool erase) {
        boost::shared_lock lock(cs_sigcache);
        return setInvalid.contains(entry, erase);
    }

    void Set(uint256 &entry) {
        boost::unique_lock<boost::shared_mutex> lock(cs_sigcache);
        setValid.insert(entry);
    }

    void SetInvalid(uint256 &entry) {
        std::scoped_lock lock(cs_sigcache);
        setInvalid.insert(entry);
    }

    uint32_t setup_bytes(size_t n) { return setValid.setup_bytes(n); }

    uint32_t setup_bytes_invalid(size_t n) { return setInvalid.setup_bytes(n); }
};

/**
 * In previous versions of this code, signatureCache was a local static variable
 * in CachingTransactionSignatureChecker::VerifySignature. We initialize
 * signatureCache outside of VerifySignature to avoid the atomic operation per
 * call overhead associated with local static variables even though
 * signatureCache could be made local to VerifySignature.
 */
static CSignatureCache signatureCache;
} // namespace

// To be called once in AppInit2/TestingSetup to initialize the signatureCache

void InitSignatureCache() {
    // nMaxCacheSize is unsigned. If -maxsigcachesize is set to zero,
    // setup_bytes creates the minimum possible cache (2 elements).
    auto initCache = [](std::string argName, unsigned int defaultSize, std::string_view type, auto& classInstance, auto callback){
      size_t nMaxCacheSize = std::min(std::max(int64_t(0), gArgs.GetArg(argName, defaultSize)), MAX_MAX_SIG_CACHE_SIZE) *
      (size_t(1) << 20);
      auto nElems = (classInstance.*callback)(nMaxCacheSize);
      LogPrintf("Using %zu MiB out of %zu requested for %ssignature cache, able to "
            "store %zu elements\n", (nElems * sizeof(uint256)) >> 20, nMaxCacheSize >> 20, type, nElems);
    };

    initCache("-maxsigcachesize", DEFAULT_MAX_SIG_CACHE_SIZE, "", signatureCache, &CSignatureCache::setup_bytes);
    initCache("-maxinvalidsigcachesize", DEFAULT_INVALID_MAX_SIG_CACHE_SIZE, "invalid ", signatureCache, &CSignatureCache::setup_bytes_invalid);
}


bool CachingTransactionSignatureChecker::VerifySignature(
    const std::vector<uint8_t> &vchSig, const CPubKey &pubkey,
    const uint256 &sighash) const {
    uint256 entry;
    signatureCache.ComputeEntry(entry, sighash, vchSig, pubkey);
    if (signatureCache.Get(entry, !store)) {
        return true;
    }
    if (signatureCache.GetInvalid(entry, !store)) {
        return false;
    }

    if (!TransactionSignatureChecker::VerifySignature(vchSig, pubkey,
                                                      sighash)) {
       
        signatureCache.SetInvalid(entry);
        return false;
    }
    if (store) {
        signatureCache.Set(entry);
    }
    return true;
}
