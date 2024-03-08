Author: Chris Gibson

Copyright: 

    "Information in this document is subject to change without notice, and
    is furnished under a license agreement or nondisclosure agreement. The
    information may only be used or copied in accordance with the terms of
    those agreements. All rights reserved. No part of this publication may
    be reproduced, stored in a retrieval system, or transmitted in any form
    or any means electronic or mechanical, including photocopying and
    recording for any purpose other than the purchaser's personal use
    without the written permission of nChain Limited. The names of actual
    companies and products mentioned in this document may be trademarks of
    their respective owners. nChain Limited accepts no responsibility or
    liability for any errors or inaccuracies that may appear in this
    documentation."

Date: 01.07.2019

Organization: nChain

Title: BSV Script Big Numbers

Introduction
============

This document describes the reinstatement of big number arithmetic into
Bitcoin SV. Big Number arithmetic will remove the current limit
on the size of numbers in the Script language. Big number support will
enable cryptographic functionality in Script.

The new big number implementation must be 100% backward compatible. That
is, a Script number in an pre-existing transaction on the blockchain
when processed by the new implementation will produce the same result as
if it had been processed by the original implementation.

Pre-Genesis Functionality
=========================

In the current implementation of Bitcoin Script valid numbers are
limited to 4-bytes. The semantics are subtle, though: operands must be
in the range \[-2^31 +1...2^31 -1\], but results may overflow; overflows 
will not raise errors as long as the results are not used in a subsequent numeric operations.
CScriptNum enforces those semantics by storing results as an int64_t
and allowing out-of-range values to be returned as a vector of bytes but
throwing an exception if arithmetic is done or the result is interpreted
as an integer.

The CScriptNum class attempts to maintain an invariant over the range of
values assigned to the m_value member variable and report
over/underflow errors to calling code. For example:

    explicit CScriptNum(const std::vector<uint8_t> &vch, bool fRequireMinimal,
                        const size_t nMaxNumSize = MAXIMUM_ELEMENT_SIZE) {
        if (vch.size() > nMaxNumSize) {
            throw scriptnum_overflow_error("script number overflow");
        }
        if (fRequireMinimal && !IsMinimallyEncoded(vch, nMaxNumSize)) {
            throw scriptnum_minencode_error("non-minimally encoded script number");
       }
        m_value = set_vch(vch);
    }

However, in some assignments the invariant is only checked with an assertion statement;
and thus only enforced in a build that does not define the name NDEBUG.

    inline CScriptNum &operator+=(const int64_t &rhs) {
        assert(
            rhs == 0 ||
            (rhs > 0 && m_value <= std::numeric_limits<int64_t>::max() - rhs) ||
        (rhs < 0 && m_value >= std::numeric_limits<int64_t>::min() - rhs));
        m_value += rhs;
        return *this;
    }

Furthermore, the invariant is not checked every time that an assignment is made to the m_value member variable. 
For example:

    explicit CScriptNum(const int64_t &n) { m_value = n; }

    inline CScriptNum &operator=(const int64_t &rhs) {
        m_value = rhs;
        return *this;
    }

The blockchain serialized representation is little-endian, with the most
significant bit indicating the sign (0 if positive, 1 if negative). An
additional byte may be required to hold the sign. Little endianess of the
representations means that numbers are non-unique. 
E.g. Zero = {}, { 0x00 }, { 0x08 }, { 0x00, 0x08 }, ...

Positive Number Serialized Representation

    +--------+-----------------------------+
    |        |  serialized representation  |
    |        +---------+---------+---------+
    |        |   byte  |   byte  |   byte  |
    | dec    |   0     |   1     |   2     |
    +========+=========+=========+=========+
    |      0 |         |         |         |
    +--------+---------+---------+---------+
    |      1 |    0x1  |         |         |
    +--------+---------+---------+---------+
    |    ... |         |         |         |
    +--------+---------+---------+---------+
    |    127 |   0x7f  |         |         |
    +--------+---------+---------+---------+
    |    128 |   0x80  |   0x0   |         |
    +--------+---------+---------+---------+
    |    129 |   0x81  |   0x0   |         |
    +--------+---------+---------+---------+
    |    ... |         |         |         |
    +--------+---------+---------+---------+
    |    255 |   0xff  |   0x0   |         |
    +--------+---------+---------+---------+
    |    256 |   0x0   |   0x1   |         |
    +--------+---------+---------+---------+
    |    257 |   0x1   |   0x1   |         |
    +--------+---------+---------+---------+
    |    ... |         |         |         |
    +--------+---------+---------+---------+
    |  32767 |   0xff  |  0x7f   |         |
    +--------+---------+---------+---------+
    |  32768 |   0x0   |  0x80   |  0x0    |
    +--------+---------+---------+---------+
    |  32769 |   0x1   |  0x80   |  0x0    |
    +--------+---------+---------+---------+
    |    ... |         |         |         |
    +--------+---------+---------+---------+

Negative Number Serialized Representation

    +--------+-----------------------------+
    |        |  serialized representation  |
    |        +---------+---------+---------+
    |        |   byte  |   byte  |   byte  |
    | dec    |   0     |   1     |   2     |
    +========+=========+=========+=========+
    |      0 |         |         |         |
    +--------+---------+---------+---------+
    |     -1 |   0x81  |         |         |
    +--------+---------+---------+---------+
    |    ... |         |         |         |
    +--------+---------+---------+---------+
    |   -127 |   0xff  |         |         |
    +--------+---------+---------+---------+
    |   -128 |   0x80  |  0x80   |         |
    +--------+---------+---------+---------+
    |   -129 |   0x81  |  0x80   |         |
    +--------+---------+---------+---------+
    |    ... |         |         |         |
    +--------+---------+---------+---------+
    |   -255 |   0xff  |  0x80   |         |
    +--------+---------+---------+---------+
    |   -256 |   0x0   |  0x81   |         |
    +--------+---------+---------+---------+
    |   -257 |   0x1   |  0x81   |         |
    +--------+---------+---------+---------+
    |    ... |         |         |         |
    +--------+---------+---------+---------+
    | -32767 |   0xff  |  0xff   |         |
    +--------+---------+---------+---------+
    | -32768 |   0x0   |  0x80   |  0x80   |
    +--------+---------+---------+---------+
    | -32769 |   0x1   |  0x80   |  0x80   |
    +--------+---------+---------+---------+
    |    ... |         |         |         |
    +--------+---------+---------+---------+


The script engine can demand that a number is represented using the
minimum number of bytes possible (see BIP62, Rule 4) if the
SCRIPT_VERFIY_MINIMALDATA flag is passed.

Post-Genesis Implementation
=====================

Design Considerations
---------------------

A hard fork in February 2020 will be used to move from the existing
implementation to the new big number implementation.

The hard fork makes it *possible* to adopt a new serialized blockchain
format for Script numbers and maintain backward compatibility. However,
this would mean that client applications such as wallets and browsers
would need to be upgraded to process the new format. For this reason the
new serialized format for Script numbers will be the same as that for
Script numbers less than or equal to four bytes.

Any transaction in the UTXO set that originated before the hard fork
could contain a locking script that relies on the overflow behaviour of
a Script number (unlikely, but possible). Therefore, to ensure
spendability script processing of any such transaction should maintain the 4-byte number
semantics. Any Script number in a transaction created after the hard
fork should have the semantics of a big number. Thus, to determine
whether a UTXO riginated before the hard fork the block
height/validation time must be tracked.

If a transaction is submitted and validated just before the hard fork it
could be added to a block after the hard fork. To avoid any semantic
issues we propose that no scripts relying on overflow behaviour be
submitted for some period, say 48 hrs, before the hard fork. We believe
that the natural instances of such scripts should be very low.

What happens when a script is composed of a locking script with 4-byte
number semantics and an unlocking script with big number semantics? The
locking script expects 4-byte semantics, therefore the unlocking script
(data) must be downgraded to 4-byte semantics. If any number in the unlocking
script is over 4-bytes it should be converted to an overflowed 4-byte
number.

Implementation Strategy
-----------------------

The CScriptNum class is responsible for the implementation of numbers in
script. Currently, the class definition has a single non-static member
of type int64_t. The CScriptNum class enforces the semantics described
in 'Existing Functionality'.

The proposal is to replace the CScript::m_value member with a different
type. One which models the concept of Integer, as int64_t does, but is
not restricted to a 64 bit representation. The new type 'big_int' will
be implemented in terms of an existing Big Number library such as
[OpenSSL](http://www.openssl.org/docs/man1.0.2/man3/bn.html) or
[GMPlib](https://gmplib.org/). We propose to use OpenSSL in the first
instance to be consistent with the SDK.

Chris Gibson 

