// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key.h"

#include "arith_uint256.h"
#include "crypto/common.h"
#include "crypto/hmac_sha512.h"
#include "pubkey.h"
#include "random.h"

#include <cstdint>
#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include "ecc_guard.h"

namespace
{
    //NOLINTNEXTLINE(cert-err58-cpp)
    const ecc_guard secp256k1_context_sign{ecc_guard::operation::sign};

    //NOLINTNEXTLINE(cert-err58-cpp)
    const bool secp256k1_seeded = [] {
        // Pass in a random blinding seed to the secp256k1 context.
        std::vector<uint8_t, secure_allocator<uint8_t>> vseed(32);
        GetRandBytes(vseed.data(), 32);
        bool ret = secp256k1_context_randomize(secp256k1_context_sign.get(),
                                               vseed.data());
        assert(ret);
        return true;
    }();
}

/** These functions are taken from the libsecp256k1 distribution and are very
 * ugly. */
//NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
static int ec_privkey_import_der(const secp256k1_context *ctx, uint8_t *out32,
                                 const uint8_t *privkey, size_t privkeylen) {
    const uint8_t *end = privkey + privkeylen;
    int lenb = 0;
    int len = 0;
    memset(out32, 0, 32);
    /* sequence header */
    if (end < privkey + 1 || *privkey != 0x30) {
        return 0;
    }
    privkey++;
    /* sequence length constructor */
    if (end < privkey + 1 || !(*privkey & 0x80)) {
        return 0;
    }
    lenb = *privkey & ~0x80;
    privkey++;
    if (lenb < 1 || lenb > 2) {
        return 0;
    }
    if (end < privkey + lenb) {
        return 0;
    }
    /* sequence length */
    len = privkey[lenb - 1] | (lenb > 1 ? privkey[lenb - 2] << 8 : 0);
    privkey += lenb;
    if (end < privkey + len) {
        return 0;
    }
    /* sequence element 0: version number (=1) */
    if (end < privkey + 3 || privkey[0] != 0x02 || privkey[1] != 0x01 ||
        privkey[2] != 0x01) {
        return 0;
    }
    privkey += 3;
    /* sequence element 1: octet string, up to 32 bytes */
    if (end < privkey + 2 || privkey[0] != 0x04 || privkey[1] > 0x20 ||
        end < privkey + 2 + privkey[1]) {
        return 0;
    }
    memcpy(out32 + 32 - privkey[1], privkey + 2, privkey[1]);
    if (!secp256k1_ec_seckey_verify(ctx, out32)) {
        memset(out32, 0, 32);
        return 0;
    }
    return 1;
}

static int ec_privkey_export_der(const secp256k1_context* ctx,
                                 uint8_t* privkey,
                                 size_t* privkeylen,
                                 const uint8_t* key32,
                                 int compressed)
{
    secp256k1_pubkey pubkey;
    size_t pubkeylen = 0;
    if (!secp256k1_ec_pubkey_create(ctx, &pubkey, key32)) {
        *privkeylen = 0;
        return 0;
    }

    if (compressed) {
        static const std::array<uint8_t, 8> begin{0x30, 0x81, 0xD3, 0x02,
                                                  0x01, 0x01, 0x04, 0x20};
        static const std::array<uint8_t, 141> middle{
            0xA0, 0x81, 0x85, 0x30, 0x81, 0x82, 0x02, 0x01, 0x01, 0x30, 0x2C,
            0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x01, 0x01, 0x02, 0x21,
            0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFC, 0x2F,
            0x30, 0x06, 0x04, 0x01, 0x00, 0x04, 0x01, 0x07, 0x04, 0x21, 0x02,
            0x79, 0xBE, 0x66, 0x7E, 0xF9, 0xDC, 0xBB, 0xAC, 0x55, 0xA0, 0x62,
            0x95, 0xCE, 0x87, 0x0B, 0x07, 0x02, 0x9B, 0xFC, 0xDB, 0x2D, 0xCE,
            0x28, 0xD9, 0x59, 0xF2, 0x81, 0x5B, 0x16, 0xF8, 0x17, 0x98, 0x02,
            0x21, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6,
            0xAF, 0x48, 0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41,
            0x41, 0x02, 0x01, 0x01, 0xA1, 0x24, 0x03, 0x22, 0x00};
        uint8_t *ptr = privkey;
        memcpy(ptr, begin.data(), sizeof(begin));
        ptr += sizeof(begin);
        memcpy(ptr, key32, 32);
        ptr += 32;
        memcpy(ptr, middle.data(), sizeof(middle));
        ptr += sizeof(middle);
        pubkeylen = 33;
        secp256k1_ec_pubkey_serialize(ctx, ptr, &pubkeylen, &pubkey,
                                      SECP256K1_EC_COMPRESSED);
        ptr += pubkeylen;
        *privkeylen = ptr - privkey;
    } else {
        static const std::array<uint8_t, 9> begin {0x30, 0x82, 0x01, 0x13, 0x02,
                                                   0x01, 0x01, 0x04, 0x20};
        static const std::array<uint8_t, 173> middle{
            0xA0, 0x81, 0xA5, 0x30, 0x81, 0xA2, 0x02, 0x01, 0x01, 0x30, 0x2C,
            0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x01, 0x01, 0x02, 0x21,
            0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFC, 0x2F,
            0x30, 0x06, 0x04, 0x01, 0x00, 0x04, 0x01, 0x07, 0x04, 0x41, 0x04,
            0x79, 0xBE, 0x66, 0x7E, 0xF9, 0xDC, 0xBB, 0xAC, 0x55, 0xA0, 0x62,
            0x95, 0xCE, 0x87, 0x0B, 0x07, 0x02, 0x9B, 0xFC, 0xDB, 0x2D, 0xCE,
            0x28, 0xD9, 0x59, 0xF2, 0x81, 0x5B, 0x16, 0xF8, 0x17, 0x98, 0x48,
            0x3A, 0xDA, 0x77, 0x26, 0xA3, 0xC4, 0x65, 0x5D, 0xA4, 0xFB, 0xFC,
            0x0E, 0x11, 0x08, 0xA8, 0xFD, 0x17, 0xB4, 0x48, 0xA6, 0x85, 0x54,
            0x19, 0x9C, 0x47, 0xD0, 0x8F, 0xFB, 0x10, 0xD4, 0xB8, 0x02, 0x21,
            0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6, 0xAF,
            0x48, 0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41,
            0x02, 0x01, 0x01, 0xA1, 0x44, 0x03, 0x42, 0x00};
        uint8_t *ptr = privkey;
        memcpy(ptr, begin.data(), sizeof(begin));
        ptr += sizeof(begin);
        memcpy(ptr, key32, 32);
        ptr += 32;
        memcpy(ptr, middle.data(), sizeof(middle));
        ptr += sizeof(middle);
        pubkeylen = 65;
        secp256k1_ec_pubkey_serialize(ctx, ptr, &pubkeylen, &pubkey,
                                      SECP256K1_EC_UNCOMPRESSED);
        ptr += pubkeylen;
        *privkeylen = ptr - privkey;
    }
    return 1;
}
//NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

bool CKey::Check(const uint8_t *vch) {
    return secp256k1_ec_seckey_verify(secp256k1_context_sign.get(), vch);
}

void CKey::MakeNewKey(bool fCompressedIn) {
    do
    {
        //NOLINTNEXTLINE(*-narrowing-conversions)
        GetStrongRandBytes(keydata.data(), keydata.size());
    } while (!Check(keydata.data()));
    fValid = true;
    fCompressed = fCompressedIn;
}

CPrivKey CKey::GetPrivKey() const
{
    assert(fValid);
    CPrivKey privkey;
    size_t privkeylen{279};
    privkey.resize(privkeylen);
    const int ret = ec_privkey_export_der(secp256k1_context_sign.get(),
                                          (uint8_t*)&privkey[0],
                                          &privkeylen,
                                          begin(),
                                          fCompressed ? SECP256K1_EC_COMPRESSED
                                                      : SECP256K1_EC_UNCOMPRESSED);
    assert(ret);
    privkey.resize(privkeylen);
    return privkey;
}

CPubKey CKey::GetPubKey() const {
    assert(fValid);
    secp256k1_pubkey pubkey;
    size_t clen = 65;
    CPubKey result;
    int ret = secp256k1_ec_pubkey_create(
        secp256k1_context_sign.get(), &pubkey, begin());
    assert(ret);
    secp256k1_ec_pubkey_serialize(secp256k1_context_sign.get(),
                                  result.data(),
                                  &clen,
                                  &pubkey,
                                  fCompressed ? SECP256K1_EC_COMPRESSED
                                              : SECP256K1_EC_UNCOMPRESSED);
    assert(result.size() == clen);
    assert(result.IsValid());
    return result;
}

bool CKey::Sign(const uint256& hash,
                std::vector<uint8_t>& vchSig,
                uint32_t test_case) const
{
    if (!fValid) return false;
    vchSig.resize(72);
    size_t nSigLen = 72;
    std::array<uint8_t, 32> extra_entropy = {0};
    WriteLE32(extra_entropy.data(), test_case);
    secp256k1_ecdsa_signature sig;
    int ret = secp256k1_ecdsa_sign(secp256k1_context_sign.get(),
                                   &sig,
                                   hash.begin(),
                                   begin(),
                                   secp256k1_nonce_function_rfc6979,
                                   test_case ? extra_entropy.data() : nullptr);
    assert(ret);
    secp256k1_ecdsa_signature_serialize_der(
        secp256k1_context_sign.get(), (uint8_t*)&vchSig[0], &nSigLen, &sig);
    vchSig.resize(nSigLen);
    return true;
}

bool CKey::VerifyPubKey(const CPubKey& pubkey) const
{
    if (pubkey.IsCompressed() != fCompressed) {
        return false;
    }

    std::array<uint8_t, 8> rnd{};
    std::string str = "Bitcoin key verification\n";
    GetRandBytes(rnd.data(), sizeof(rnd));
    uint256 hash;
    CHash256()
        //NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
        .Write((uint8_t*)str.data(), str.size())
        .Write(rnd.data(), sizeof(rnd))
        .Finalize(CHash256::span{hash.begin(), CHash256::OUTPUT_SIZE});
    std::vector<uint8_t> vchSig;
    Sign(hash, vchSig);
    return pubkey.Verify(hash, vchSig);
}

bool CKey::SignCompact(const uint256 &hash,
                       std::vector<uint8_t> &vchSig) const {
    if (!fValid) return false;
    vchSig.resize(65);
    int rec = -1;
    secp256k1_ecdsa_recoverable_signature sig;
    int ret = secp256k1_ecdsa_sign_recoverable(secp256k1_context_sign.get(),
                                               &sig,
                                               hash.begin(),
                                               begin(),
                                               secp256k1_nonce_function_rfc6979,
                                               nullptr);
    assert(ret);
    secp256k1_ecdsa_recoverable_signature_serialize_compact(
        secp256k1_context_sign.get(), (uint8_t*)&vchSig[1], &rec, &sig);
    assert(ret);
    assert(rec != -1);
    vchSig[0] = 27 + rec + (fCompressed ? 4 : 0);
    return true;
}

bool CKey::Load(CPrivKey& privkey,
                CPubKey& vchPubKey,
                bool fSkipCheck)
{
    if(!ec_privkey_import_der(secp256k1_context_sign.get(),
                              //NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
                              (uint8_t*)begin(),
                              &privkey[0],
                              privkey.size()))
        return false;
    fCompressed = vchPubKey.IsCompressed();
    fValid = true;

    if (fSkipCheck) return true;

    return VerifyPubKey(vchPubKey);
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
bool CKey::Derive(CKey& keyChild,
                  ChainCode& ccChild,
                  unsigned int nChild,
                  const ChainCode& cc) const
{
    assert(IsValid());
    assert(IsCompressed());
    std::vector<uint8_t, secure_allocator<uint8_t>> vout(64);
    if((nChild >> 31) == 0)
    {
        CPubKey pubkey = GetPubKey();
        assert(pubkey.begin() + 33 == pubkey.end());
        BIP32Hash(cc,
                  nChild,
                  *pubkey.begin(),
                  std::span<const uint8_t, 32>{pubkey.begin() + 1, 32},
                  std::span<uint8_t, 64>{vout});
    }
    else
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        assert(begin() + 32 == end());
        BIP32Hash(cc,
                  nChild,
                  0,
                  std::span<const uint8_t, 32>{begin(), 32},
                  std::span<uint8_t, 64>{vout});
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    memcpy(ccChild.begin(), vout.data() + 32, 32);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    memcpy((uint8_t*)keyChild.begin(), begin(), 32);
    const bool ret = secp256k1_ec_seckey_tweak_add(secp256k1_context_sign.get(),
                                                   // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
                                                   (uint8_t*)keyChild.begin(),
                                                   vout.data());
    keyChild.fCompressed = true;
    keyChild.fValid = ret;
    return ret;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

bool CExtKey::Derive(CExtKey& out, unsigned int _nChild) const
{
    out.nDepth = nDepth + 1;
    CKeyID id = key.GetPubKey().GetID();
    memcpy(out.vchFingerprint.data(), &id, vchFingerprint.size());
    out.nChild = _nChild;
    return key.Derive(out.key, out.chaincode, _nChild, chaincode);
}

void CExtKey::SetMaster(const uint8_t *seed, unsigned int nSeedLen)
{
    static const std::array<uint8_t, 12> hashkey{'B', 'i', 't', 'c', 'o', 'i',
                                      'n', ' ', 's', 'e', 'e', 'd'};
    std::vector<uint8_t, secure_allocator<uint8_t>> vout(64);
    CHMAC_SHA512(hashkey.data(),
                 sizeof(hashkey))
                 .Write(seed, nSeedLen)
                 .Finalize(CHMAC_SHA512::span{vout.begin(), CHMAC_SHA512::OUTPUT_SIZE});
    key.Set(&vout[0], &vout[32], true);
    memcpy(chaincode.begin(), &vout[32], 32);
    nDepth = 0;
    nChild = 0;
    memset(vchFingerprint.data(), 0, vchFingerprint.size());
}

CExtPubKey CExtKey::Neuter() const
{
    CExtPubKey ret;
    ret.nDepth = nDepth;
    memcpy(ret.vchFingerprint.data(),
           vchFingerprint.data(),
           vchFingerprint.size());
    ret.nChild = nChild;
    ret.pubkey = key.GetPubKey();
    ret.chaincode = chaincode;
    return ret;
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
void CExtKey::Encode(const std::span<uint8_t, BIP32_EXTKEY_SIZE> code) const
{
    code[0] = nDepth;
    memcpy(code.data() + 1,
           vchFingerprint.data(),
           vchFingerprint.size());
    code[5] = (nChild >> 24) & 0xFF;
    code[6] = (nChild >> 16) & 0xFF;
    code[7] = (nChild >> 8) & 0xFF;
    code[8] = (nChild >> 0) & 0xFF;
    memcpy(code.data() + 9, chaincode.begin(), 32);
    code[41] = 0;
    assert(key.size() == 32);
    memcpy(code.data() + 42, key.begin(), 32);
}

void CExtKey::Decode(const std::span<const uint8_t, BIP32_EXTKEY_SIZE> code)
{
    nDepth = code[0];
    memcpy(vchFingerprint.data(),
           code.data() + 1,
           vchFingerprint.size());
    nChild = (code[5] << 24) | (code[6] << 16) | (code[7] << 8) | code[8];
    memcpy(chaincode.begin(), code.data() + 9, 32);
    key.Set(code.data() + 42, code.data() + BIP32_EXTKEY_SIZE, true);
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

bool ECC_InitSanityCheck() {
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    return key.VerifyPubKey(pubkey);
}
