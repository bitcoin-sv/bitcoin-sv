// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "aes.h"

#include "support/cleanse.h"

#include <array>
#include <cassert>
#include <cstring>

extern "C" {
#include "crypto/ctaes/ctaes.c" // NOLINT(bugprone-suspicious-include)
}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
AES128Encrypt::AES128Encrypt(const cspan key)
{
    AES128_init(&ctx, key.data());
}

AES128Encrypt::~AES128Encrypt()
{
    memory_cleanse(&ctx, sizeof(ctx));
}

void AES128Encrypt::Encrypt(uint8_t ciphertext[16], // NOLINT(cppcoreguidelines-avoid-c-arrays)
                            const uint8_t plaintext[16]) const // NOLINT(cppcoreguidelines-avoid-c-arrays)
{
    AES128_encrypt(&ctx, 1, ciphertext, plaintext);
}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
AES128Decrypt::AES128Decrypt(const cspan key)
{
    AES128_init(&ctx, key.data());
}

AES128Decrypt::~AES128Decrypt()
{
    memory_cleanse(&ctx, sizeof(ctx));
}

void AES128Decrypt::Decrypt(uint8_t plaintext[16], // NOLINT(cppcoreguidelines-avoid-c-arrays)
                            const uint8_t ciphertext[16]) const // NOLINT(cppcoreguidelines-avoid-c-arrays)
{
    AES128_decrypt(&ctx, 1, plaintext, ciphertext);
}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
AES256Encrypt::AES256Encrypt(const cspan key)
{
    AES256_init(&ctx, key.data());
}

AES256Encrypt::~AES256Encrypt()
{
    memory_cleanse(&ctx, sizeof(ctx));
}

void AES256Encrypt::Encrypt(uint8_t ciphertext[16], // NOLINT(cppcoreguidelines-avoid-c-arrays)
                            const uint8_t plaintext[16]) const // NOLINT(cppcoreguidelines-avoid-c-arrays)
{
    AES256_encrypt(&ctx, 1, ciphertext, plaintext);
}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
AES256Decrypt::AES256Decrypt(const cspan key)
{
    AES256_init(&ctx, key.data());
}

AES256Decrypt::~AES256Decrypt()
{
    memory_cleanse(&ctx, sizeof(ctx));
}

void AES256Decrypt::Decrypt(uint8_t plaintext[16], // NOLINT(cppcoreguidelines-avoid-c-arrays)
                            const uint8_t ciphertext[16]) const // NOLINT(cppcoreguidelines-avoid-c-arrays)
{
    AES256_decrypt(&ctx, 1, plaintext, ciphertext);
}

template<typename T>
static int CBCEncrypt(const T& enc,
                      const uint8_t iv[AES_BLOCKSIZE], // NOLINT(cppcoreguidelines-avoid-c-arrays)
                      const uint8_t* data,
                      int size,
                      bool pad,
                      uint8_t* out)
{
    int written = 0;
    int padsize = size % AES_BLOCKSIZE;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, AES_BLOCKSIZE> mixed;

    if (!data || !size || !out) return 0;

    if (!pad && padsize != 0) return 0;

    memcpy(mixed.data(), iv, AES_BLOCKSIZE);

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    // Write all but the last block
    while (written + AES_BLOCKSIZE <= size) {
        for (int i = 0; i != AES_BLOCKSIZE; i++)
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            mixed[i] ^= *data++;
        enc.Encrypt(out + written, mixed.data());
        memcpy(mixed.data(), out + written, AES_BLOCKSIZE);
        written += AES_BLOCKSIZE;
    }

    if(pad)
    {
        // For all that remains, pad each byte with the value of the remaining
        // space. If there is none, pad by a full block.
        for (int i = 0; i != padsize; i++)
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            mixed[i] ^= *data++;
        for (int i = padsize; i != AES_BLOCKSIZE; i++)
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            mixed[i] ^= AES_BLOCKSIZE - padsize;
        enc.Encrypt(out + written, mixed.data());
        written += AES_BLOCKSIZE;
    }
    return written;
}

template<typename T>
static int CBCDecrypt(const T& dec,
                      const uint8_t iv[AES_BLOCKSIZE], // NOLINT(cppcoreguidelines-avoid-c-arrays)
                      const uint8_t* data,
                      int size,
                      bool pad,
                      uint8_t* out)
{
    uint8_t padsize = 0;
    int written = 0;
    bool fail = false;
    const uint8_t *prev = iv;

    if (!data || !size || !out) return 0;

    if (size % AES_BLOCKSIZE != 0) return 0;

    // Decrypt all data. Padding will be checked in the output.
    while (written != size) {
        dec.Decrypt(out, data + written);
        for (int i = 0; i != AES_BLOCKSIZE; i++)
            *out++ ^= prev[i];
        prev = data + written;
        written += AES_BLOCKSIZE;
    }

    // When decrypting padding, attempt to run in constant-time
    if (pad) {
        // If used, padding size is the value of the last decrypted byte. For
        // it to be valid, It must be between 1 and AES_BLOCKSIZE.
        padsize = *--out;
        fail = !padsize | (padsize > AES_BLOCKSIZE);

        // If not well-formed, treat it as though there's no padding.
        padsize *= !fail;

        // All padding must equal the last byte otherwise it's not well-formed
        for (int i = AES_BLOCKSIZE; i != 0; i--)
            fail |= ((i > AES_BLOCKSIZE - padsize) & (*out-- != padsize));

        written -= padsize;
    }
    return written * !fail;
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
AES256CBCEncrypt::AES256CBCEncrypt(const key_span key,
                                   const block_span ivIn,
                                   const bool padIn)
    : enc{key},
      pad{padIn}
{
    memcpy(iv.data(), ivIn.data(), AES_BLOCKSIZE);
}

int AES256CBCEncrypt::Encrypt(const uint8_t* data, int size, uint8_t* out) const
{
    return CBCEncrypt(enc, iv.data(), data, size, pad, out);
}

AES256CBCEncrypt::~AES256CBCEncrypt()
{
    memory_cleanse(iv.data(), sizeof(iv));
}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
AES256CBCDecrypt::AES256CBCDecrypt(const key_span key, const block_span ivIn, bool padIn)
    : dec{key},
      pad{padIn}
{
    memcpy(iv.data(), ivIn.data(), AES_BLOCKSIZE);
}

int AES256CBCDecrypt::Decrypt(const uint8_t* data, int size, uint8_t* out) const
{
    return CBCDecrypt(dec, iv.data(), data, size, pad, out);
}

AES256CBCDecrypt::~AES256CBCDecrypt()
{
    memory_cleanse(iv.data(), sizeof(iv));
}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
AES128CBCEncrypt::AES128CBCEncrypt(const key_span key, const block_span ivIn, bool padIn)
    : enc{key},
      pad{padIn}
{
    memcpy(iv.data(), ivIn.data(), AES_BLOCKSIZE);
}

AES128CBCEncrypt::~AES128CBCEncrypt()
{
    memory_cleanse(iv.data(), sizeof(iv));
}

int AES128CBCEncrypt::Encrypt(const uint8_t* data, int size, uint8_t* out) const
{
    return CBCEncrypt(enc, iv.data(), data, size, pad, out);
}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
AES128CBCDecrypt::AES128CBCDecrypt(const key_span key, const block_span ivIn, bool padIn)
    : dec{key},
      pad{padIn}
{
    memcpy(iv.data(), ivIn.data(), AES_BLOCKSIZE);
}

AES128CBCDecrypt::~AES128CBCDecrypt()
{
    memory_cleanse(iv.data(), sizeof(iv));
}

int AES128CBCDecrypt::Decrypt(const uint8_t* data, int size, uint8_t* out) const
{
    return CBCDecrypt(dec, iv.data(), data, size, pad, out);
}

