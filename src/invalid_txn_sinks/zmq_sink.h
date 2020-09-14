// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "invalid_txn_publisher.h"

namespace InvalidTxnPublisher
{
#if ENABLE_ZMQ
    class CInvalidTxnZmqSink : public CInvalidTxnSink
    {
        int64_t maxMessageSize;
    public:
        CInvalidTxnZmqSink(int64_t maxMsgSize)
            :maxMessageSize(maxMsgSize)
        {}

        void Publish(const InvalidTxnInfo& invalidTxInfo) override;
    };
#endif
}