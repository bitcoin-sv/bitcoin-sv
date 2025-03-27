// Copyright (c) 2015-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// C++ wrapper around ctaes, a constant-time AES implementation

#ifndef BITCOIN_CRYPTO_AES_H
#define BITCOIN_CRYPTO_AES_H

extern "C" {
#include "crypto/ctaes/ctaes.h"
}

#include <span>

static const int AES_BLOCKSIZE = 16;
static const int AES128_KEYSIZE = 16;
static const int AES256_KEYSIZE = 32;

/** An encryption class for AES-128. */
class AES128Encrypt
{
    AES128_ctx ctx;

public:
    using cspan = std::span<const uint8_t, 16>;

    AES128Encrypt(cspan);
    AES128Encrypt(const AES128Encrypt&) = default;
    AES128Encrypt& operator=(const AES128Encrypt&) = default;
    AES128Encrypt(AES128Encrypt&&) = delete;
    AES128Encrypt& operator=(AES128Encrypt&&) = delete;
    ~AES128Encrypt();

    void Encrypt(uint8_t ciphertext[16], const uint8_t plaintext[16]) const;
};

/** A decryption class for AES-128. */
class AES128Decrypt {
private:
    AES128_ctx ctx;

public:
    AES128Decrypt(const uint8_t key[16]);
    ~AES128Decrypt();
    void Decrypt(uint8_t plaintext[16], const uint8_t ciphertext[16]) const;
};

/** An encryption class for AES-256. */
class AES256Encrypt {
private:
    AES256_ctx ctx;

public:
    AES256Encrypt(const uint8_t key[32]);
    ~AES256Encrypt();
    void Encrypt(uint8_t ciphertext[16], const uint8_t plaintext[16]) const;
};

/** A decryption class for AES-256. */
class AES256Decrypt {
private:
    AES256_ctx ctx;

public:
    AES256Decrypt(const uint8_t key[32]);
    ~AES256Decrypt();
    void Decrypt(uint8_t plaintext[16], const uint8_t ciphertext[16]) const;
};

class AES256CBCEncrypt {
public:
    AES256CBCEncrypt(const uint8_t key[AES256_KEYSIZE],
                     const uint8_t ivIn[AES_BLOCKSIZE], bool padIn);
    ~AES256CBCEncrypt();
    int Encrypt(const uint8_t *data, int size, uint8_t *out) const;

private:
    const AES256Encrypt enc;
    const bool pad;
    uint8_t iv[AES_BLOCKSIZE];
};

class AES256CBCDecrypt {
public:
    AES256CBCDecrypt(const uint8_t key[AES256_KEYSIZE],
                     const uint8_t ivIn[AES_BLOCKSIZE], bool padIn);
    ~AES256CBCDecrypt();
    int Decrypt(const uint8_t *data, int size, uint8_t *out) const;

private:
    const AES256Decrypt dec;
    const bool pad;
    uint8_t iv[AES_BLOCKSIZE];
};

class AES128CBCEncrypt
{
public:
    using key_span = std::span<const uint8_t, AES128_KEYSIZE>;
    using block_span = std::span<const uint8_t, AES_BLOCKSIZE>;

    AES128CBCEncrypt(key_span key,
                     block_span iv,
                     bool padIn);
    AES128CBCEncrypt(const AES128CBCEncrypt&) = default;
    AES128CBCEncrypt& operator=(const AES128CBCEncrypt&) = default;
    AES128CBCEncrypt(AES128CBCEncrypt&&) = delete;
    AES128CBCEncrypt& operator=(AES128CBCEncrypt&&) = delete;
    ~AES128CBCEncrypt();

    int Encrypt(const uint8_t* data, int size, uint8_t* out) const;

private:
    AES128Encrypt enc;
    bool pad;
    std::array<uint8_t, AES_BLOCKSIZE> iv;
};

class AES128CBCDecrypt {
public:
    AES128CBCDecrypt(const uint8_t key[AES128_KEYSIZE],
                     const uint8_t ivIn[AES_BLOCKSIZE], bool padIn);
    ~AES128CBCDecrypt();
    int Decrypt(const uint8_t *data, int size, uint8_t *out) const;

private:
    const AES128Decrypt dec;
    const bool pad;
    uint8_t iv[AES_BLOCKSIZE];
};

#endif // BITCOIN_CRYPTO_AES_H
