// Copyright (c) 2023 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "taskcancellation.h"

namespace task
{   

bool CCancellationToken::IsCanceled() const
{
    return
        std::any_of(
            mSource.begin(),
            mSource.end(),
            [](auto source){ return source->IsCanceled(); });
}

}
