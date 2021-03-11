// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <cassert>
#include <secp256k1.h>

class ecc_guard
{
    secp256k1_context* ctx_{nullptr};

public:
    enum class operation
    {
        sign,
        verify
    };

    explicit ecc_guard(operation op)
        : ctx_{op == operation::sign
                   ? secp256k1_context_create(SECP256K1_CONTEXT_SIGN)
                   : secp256k1_context_create(SECP256K1_CONTEXT_VERIFY)}
    {
        assert(ctx_);
    }

    ecc_guard(const ecc_guard&) = delete;
    ecc_guard &operator=(const ecc_guard &) = delete;
    ecc_guard(ecc_guard &&) = delete;
    ecc_guard& operator=(ecc_guard&&) = delete;

    ~ecc_guard()
    {
        try
        {
            assert(ctx_);
            secp256k1_context_destroy(ctx_);
        }
        catch(...)
        {
        }
    }

    secp256k1_context* get() const { return ctx_; }
};
