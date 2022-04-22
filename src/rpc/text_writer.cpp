// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "rpc/text_writer.h"

void CStringWriter::ReserveAdditional(size_t size)
{
    if(size > 0)
    {
        strBuffer.reserve(strBuffer.size() + size);
    }
}


