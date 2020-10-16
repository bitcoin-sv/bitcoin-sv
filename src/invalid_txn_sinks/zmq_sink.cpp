// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "zmq_sink.h"

#include "validationinterface.h"

using namespace InvalidTxnPublisher;

#if ENABLE_ZMQ
void CInvalidTxnZmqSink::Publish(const InvalidTxnInfo& invalidTxInfo)
{
    auto messageSize = EstimateMessageSize(invalidTxInfo, true);
    CStringWriter tw;
    CJSONWriter jw(tw, false);
    invalidTxInfo.ToJson(jw, messageSize <= maxMessageSize);
    std::string jsonString = tw.MoveOutString();
    GetMainSignals().InvalidTxMessageZMQ(jsonString);
}
#endif