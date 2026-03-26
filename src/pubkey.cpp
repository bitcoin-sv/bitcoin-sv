// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pubkey.h"
#include "ecc_guard.h"

#include <secp256k1.h>
#include <secp256k1_recovery.h>

namespace
{
    /* Global secp256k1_context object used for verification. */
    // NOLINTNEXTLINE(cert-err58-cpp)
    const ecc_guard secp256k1_context_verify{ecc_guard::operation::verify};

} // namespace

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)

/**
 * This function is taken from the libsecp256k1 distribution and implements DER
 * parsing for ECDSA signatures, while supporting an arbitrary subset of format
 * violations.
 *
 * Supported violations include negative integers, excessive padding, garbage at
 * the end, and overly long length descriptors. This is safe to use in Bitcoin
 * because since the activation of BIP66, signatures are verified to be strict
 * DER before being passed to this module, and we know it supports all
 * violations present in the blockchain before that point.
 */
static int ecdsa_signature_parse_der_lax(const secp256k1_context *ctx,
                                         secp256k1_ecdsa_signature *sig,
                                         const uint8_t *input,
                                         size_t inputlen)
{
    /* Hack to initialize sig with a correctly-parsed but invalid signature. */
    std::array<uint8_t, 64> tmpsig{};
    secp256k1_ecdsa_signature_parse_compact(ctx, sig, tmpsig.data());

    /* Sequence tag byte */
    size_t pos{};
    if (pos == inputlen || input[pos] != 0x30) {
        return 0;
    }
    pos++;

    /* Sequence length bytes */
    if (pos == inputlen) {
        return 0;
    }

    size_t lenbyte{input[pos++]};
    if (lenbyte & 0x80) {
        lenbyte -= 0x80;
        if (pos + lenbyte > inputlen) {
            return 0;
        }
        pos += lenbyte;
    }

    /* Integer tag byte for R */
    if (pos == inputlen || input[pos] != 0x02) {
        return 0;
    }
    pos++;

    /* Integer length for R */
    if (pos == inputlen) {
        return 0;
    }
    lenbyte = input[pos++];
    size_t rlen{};
    if (lenbyte & 0x80) {
        lenbyte -= 0x80;
        if (pos + lenbyte > inputlen) {
            return 0;
        }
        while (lenbyte > 0 && input[pos] == 0) {
            pos++;
            lenbyte--;
        }
        if (lenbyte >= sizeof(size_t)) {
            return 0;
        }
        rlen = 0;
        while (lenbyte > 0) {
            rlen = (rlen << 8) + input[pos];
            pos++;
            lenbyte--;
        }
    } else {
        rlen = lenbyte;
    }
    if (rlen > inputlen - pos) {
        return 0;
    }

    size_t rpos{pos};
    pos += rlen;

    /* Integer tag byte for S */
    if (pos == inputlen || input[pos] != 0x02) {
        return 0;
    }
    pos++;

    /* Integer length for S */
    if (pos == inputlen) {
        return 0;
    }
    lenbyte = input[pos++];
    size_t slen{};
    if (lenbyte & 0x80) {
        lenbyte -= 0x80;
        if (pos + lenbyte > inputlen) {
            return 0;
        }
        while (lenbyte > 0 && input[pos] == 0) {
            pos++;
            lenbyte--;
        }
        if (lenbyte >= sizeof(size_t)) {
            return 0;
        }
        slen = 0;
        while (lenbyte > 0) {
            slen = (slen << 8) + input[pos];
            pos++;
            lenbyte--;
        }
    } else {
        slen = lenbyte;
    }
    if (slen > inputlen - pos) {
        return 0;
    }

    /* Ignore leading zeroes in R */
    while (rlen > 0 && input[rpos] == 0) {
        rlen--;
        rpos++;
    }
    /* Copy R value */
    int overflow{};
    if (rlen > 32) {
        overflow = 1;
    } else {
        memcpy(tmpsig.data() + 32 - rlen, input + rpos, rlen);
    }

    /* Ignore leading zeroes in S */
    size_t spos{pos};
    while (slen > 0 && input[spos] == 0) {
        slen--;
        spos++;
    }
    /* Copy S value */
    if (slen > 32) {
        overflow = 1;
    } else {
        memcpy(tmpsig.data() + 64 - slen, input + spos, slen);
    }

    if (!overflow) {
        overflow = !secp256k1_ecdsa_signature_parse_compact(ctx, sig, tmpsig.data());
    }
    if (overflow) {
        /* Overwrite the result again with a correctly-parsed but invalid
           signature if parsing failed. */
        memset(tmpsig.data(), 0, 64);
        secp256k1_ecdsa_signature_parse_compact(ctx, sig, tmpsig.data());
    }
    return 1;
}

bool CPubKey::Verify(const uint256& hash,
                     const std::vector<uint8_t>& vchSig) const
{
    return Verify(hash, std::span<const uint8_t>{vchSig.data(), vchSig.size()});
}

bool CPubKey::Verify(const uint256& hash,
                     const std::span<const uint8_t> signature) const
{
    if(!IsValid())
        return false;
    secp256k1_pubkey pubkey;
    secp256k1_ecdsa_signature sig;
    if(!secp256k1_ec_pubkey_parse(
           secp256k1_context_verify.get(), &pubkey, &(*this)[0], size()))
    {
        return false;
    }
    if(signature.size() == 0)
    {
        return false;
    }
    if(!ecdsa_signature_parse_der_lax(secp256k1_context_verify.get(),
                                      &sig,
                                      &signature[0],
                                      signature.size()))
    {
        return false;
    }
    /**
     * libsecp256k1's ECDSA verification requires lower-S signatures, which have
     * not historically been enforced in Bitcoin, so normalize them first.
     */
    secp256k1_ecdsa_signature_normalize(
        secp256k1_context_verify.get(), &sig, &sig);
    return secp256k1_ecdsa_verify(
        secp256k1_context_verify.get(), &sig, hash.begin(), &pubkey);
}

bool CPubKey::RecoverCompact(const uint256 &hash,
                             const std::vector<uint8_t> &vchSig) {
    if (vchSig.size() != 65) return false;
    int recid = (vchSig[0] - 27) & 3;
    bool fComp = ((vchSig[0] - 27) & 4) != 0;
    secp256k1_pubkey pubkey;
    secp256k1_ecdsa_recoverable_signature sig;
    if(!secp256k1_ecdsa_recoverable_signature_parse_compact(
           secp256k1_context_verify.get(), &sig, &vchSig[1], recid))
    {
        return false;
    }
    if(!secp256k1_ecdsa_recover(
           secp256k1_context_verify.get(), &pubkey, &sig, hash.begin()))
    {
        return false;
    }
   
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, 65> pub;
    size_t publen = 65;
    secp256k1_ec_pubkey_serialize(secp256k1_context_verify.get(),
                                  pub.data(),
                                  &publen,
                                  &pubkey,
                                  fComp ? SECP256K1_EC_COMPRESSED
                                        : SECP256K1_EC_UNCOMPRESSED);
    Set(pub.data(), pub.data() + publen);
    return true;
}

bool CPubKey::IsFullyValid() const {
    if (!IsValid()) return false;
    secp256k1_pubkey pubkey;
    return secp256k1_ec_pubkey_parse(
        secp256k1_context_verify.get(), &pubkey, &(*this)[0], size());
}

bool CPubKey::Decompress() {
    if (!IsValid()) return false;
    secp256k1_pubkey pubkey;
    if(!secp256k1_ec_pubkey_parse(
           secp256k1_context_verify.get(), &pubkey, &(*this)[0], size()))
    {
        return false;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, 65> pub;
    size_t publen = 65;
    secp256k1_ec_pubkey_serialize(secp256k1_context_verify.get(),
                                  pub.data(),
                                  &publen,
                                  &pubkey,
                                  SECP256K1_EC_UNCOMPRESSED);
    Set(pub.data(), pub.data() + publen);
    return true;
}

bool CPubKey::Derive(CPubKey &pubkeyChild, ChainCode &ccChild,
                     unsigned int nChild, const ChainCode &cc) const {
    assert(IsValid());
    assert((nChild >> 31) == 0);
    assert(begin() + 33 == end());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, 64> out;
    BIP32Hash(cc,
              nChild,
              *begin(),
              std::span<const uint8_t, 32>{begin() + 1, end()},
              out);
    memcpy(ccChild.begin(), out.data() + 32, 32);
    secp256k1_pubkey pubkey;
    if(!secp256k1_ec_pubkey_parse(secp256k1_context_verify.get(),
                                  &pubkey,
                                  &(*this)[0],
                                  size()))
    {
        return false;
    }

    if(!secp256k1_ec_pubkey_tweak_add(secp256k1_context_verify.get(),
                                      &pubkey,
                                      out.data()))
    {
        return false;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, 33> pub;
    size_t publen = 33;
    secp256k1_ec_pubkey_serialize(secp256k1_context_verify.get(),
                                  pub.data(),
                                  &publen,
                                  &pubkey,
                                  SECP256K1_EC_COMPRESSED);
    pubkeyChild.Set(pub.data(), pub.data() + publen);
    return true;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

void CExtPubKey::Encode(const std::span<uint8_t, BIP32_EXTKEY_SIZE> code) const
{
    code[0] = nDepth;
    //NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    memcpy(code.data() + 1, vchFingerprint.data(), 4);
    code[5] = (nChild >> 24) & 0xFF;
    code[6] = (nChild >> 16) & 0xFF;
    code[7] = (nChild >> 8) & 0xFF;
    code[8] = (nChild >> 0) & 0xFF;
    memcpy(code.data() + 9, chaincode.begin(), 32);
    assert(pubkey.size() == 33);
    memcpy(code.data() + 41, pubkey.data(), 33);
    //NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

void CExtPubKey::Decode(const std::span<const uint8_t, BIP32_EXTKEY_SIZE> code)
{
    nDepth = code[0];
    //NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    memcpy(vchFingerprint.data(), code.data() + 1, 4);
    nChild = (code[5] << 24) | (code[6] << 16) | (code[7] << 8) | code[8];
    memcpy(chaincode.begin(), code.data() + 9, 32);
    pubkey.Set(code.data() + 41,
               code.data() + BIP32_EXTKEY_SIZE);
    //NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

bool CExtPubKey::Derive(CExtPubKey &out, unsigned int _nChild) const {
    out.nDepth = nDepth + 1;
    CKeyID id = pubkey.GetID();
    memcpy(out.vchFingerprint.data(), &id, 4);
    out.nChild = _nChild;
    return pubkey.Derive(out.pubkey, out.chaincode, _nChild, chaincode);
}

bool CPubKey::CheckLowS(const std::span<const uint8_t> s)
{
    secp256k1_ecdsa_signature sig;
    if(!ecdsa_signature_parse_der_lax(
           secp256k1_context_verify.get(), &sig, s.data(), s.size()))
    {
        return false;
    }
    return (!secp256k1_ecdsa_signature_normalize(
        secp256k1_context_verify.get(), nullptr, &sig));
}

