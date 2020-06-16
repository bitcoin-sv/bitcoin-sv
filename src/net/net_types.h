// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <map>
#include <string>

/** Some common type alises used throughout the networking code */

// A node ID
using NodeId = int64_t;

// Command, total bytes
using mapMsgCmdSize = std::map<std::string, uint64_t>;

// Average bandwidth, number of items average is calculated over
using AverageBandwidth = std::pair<uint64_t, size_t>;

/** Some constants */

// Peer average bandwidth measurement interval
static const unsigned PEER_AVG_BANDWIDTH_CALC_FREQUENCY_SECS = 5;

// Reject codes for stream errors
static const uint8_t REJECT_STREAM_SETUP = 0x60;

// Maximum length for a serialised StreamPolicy name
static const size_t MAX_STREAM_POLICY_NAME_LENGTH = 64;
