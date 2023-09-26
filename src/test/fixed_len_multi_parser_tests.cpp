// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE
#include <boost/test/unit_test.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>

#include "net/fixed_len_multi_parser.h"
#include "net/msg_parser.h"
#include "net/msg_parser_buffer.h"
#include "net/parser_utils.h"
#include "net/p2p_msg_lengths.h"

using namespace std;
using namespace bsv;

constexpr size_t sid_len{6};
constexpr size_t sids_per_seg{100};

BOOST_AUTO_TEST_SUITE(fixed_len_multi_parser_tests)

BOOST_AUTO_TEST_CASE(parse_empty_input)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    vector<uint8_t> ip;
    const std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(0, bytes_read);
    BOOST_CHECK_EQUAL(1, bytes_reqd);
    BOOST_CHECK_EQUAL(0, parser.size());
    BOOST_CHECK_EQUAL(0, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_zero_count)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    vector<uint8_t> ip;
    ip.push_back(0);
    const std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(1, bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(1, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_count_only)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    vector<uint8_t> ip;
    ip.push_back(2);
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(1, bytes_read);
    BOOST_CHECK_EQUAL(2 * sid_len, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(1, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_max_shortids_count_only)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    vector<uint8_t> ip(9, 0xff); // cmpctsize::max
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(9, bytes_read);
    const auto expected_bytes_reqd{(numeric_limits<uint64_t>::max() / sid_len) * sid_len};
    BOOST_CHECK_EQUAL(expected_bytes_reqd, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(1, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_max_shortids_count_and_partial_fixed_len)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    vector<uint8_t> ip(9, 0xff); // cmpctsize::max
    ip.push_back(42);            // partial fixed_len
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(9, bytes_read);
    const auto expected_bytes_reqd{(numeric_limits<uint64_t>::max() / sid_len) * sid_len};
    BOOST_CHECK_EQUAL(expected_bytes_reqd, bytes_reqd);
    BOOST_CHECK_EQUAL(9, parser.size());
    BOOST_CHECK_EQUAL(1, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_max_shortids_count_and_short_id)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    vector<uint8_t> ip(9, 0xff); // cmpctsize::max
    ip.insert(ip.cend(), 6, 42); // 1 fixed_len
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(15, bytes_read);
    const auto expected_bytes_reqd{(numeric_limits<uint64_t>::max() / sid_len) * sid_len};
    BOOST_CHECK_EQUAL(expected_bytes_reqd, bytes_reqd);
    BOOST_CHECK_EQUAL(15, parser.size());
    BOOST_CHECK_EQUAL(1, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_max_shortids_count_short_id_and_partial_short_id)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    vector<uint8_t> ip(9, 0xff); // cmpctsize::max
    ip.insert(ip.cend(), 6, 42); // 1 fixed_len
    ip.push_back(101);           // partial fixed_len
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(15, bytes_read);
    const auto expected_bytes_reqd{(numeric_limits<uint64_t>::max() / sid_len) * sid_len};
    BOOST_CHECK_EQUAL(expected_bytes_reqd, bytes_reqd);
    BOOST_CHECK_EQUAL(15, parser.size());
    BOOST_CHECK_EQUAL(1, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(overflow_with_shortids_and_partial_short_id)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};

    vector<uint8_t> ip{0xff, 1, 0, 0, 0, 0, 0, 0, 0x80};
    ip.insert(ip.cend(), sid_len * sid_len + 1, 42);
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(45, bytes_read);
    const auto expected_bytes_reqd{(numeric_limits<uint64_t>::max() / sid_len) * sid_len};
    BOOST_CHECK_EQUAL(expected_bytes_reqd, bytes_reqd);
    BOOST_CHECK_EQUAL(45, parser.size());
    BOOST_CHECK_EQUAL(1, parser.segment_count());
}

static std::vector<uint8_t> make_msg(const size_t n_sids)
{
    constexpr size_t sid_len{6};
    vector<uint8_t> v;
    v.push_back(n_sids); 
    for(size_t i{}; i < n_sids; ++i)
        v.insert(v.end(), sid_len, i);
    return v;
}

BOOST_AUTO_TEST_CASE(parse_sid_1_seg_1)
{
    const vector<uint8_t> ip{make_msg(1)};
    const size_t sids_per_seg{1};
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    const std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(2, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_sid_2_seg_1)
{
    const vector<uint8_t> ip{make_msg(2)};
    const size_t sids_per_seg{1};
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    const std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(3, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_sid_2_seg_2)
{
    const vector<uint8_t> ip{make_msg(2)};
    const size_t sids_per_seg{2};
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    const std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(2, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_sid_3_seg_2)
{
    const vector<uint8_t> ip{make_msg(3)};
    const size_t sids_per_seg{2};
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    const std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(3, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_sid_200_seg_100)
{
    const vector<uint8_t> ip{make_msg(200)};
    const size_t sids_per_seg{100};
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    const std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(3, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_sid_70117_seg_100_1_pass)
{
    constexpr size_t n_sids{70117};
    constexpr size_t sid_len{6};
    vector<uint8_t> ip;
    ip.push_back(0xfe);
    ip.push_back(0xe5);
    ip.push_back(0x11);
    ip.push_back(0x1);
    ip.push_back(0x0);
    for(size_t i{}; i < n_sids; ++i)
        ip.insert(ip.end(), sid_len, i);

    const size_t sids_per_seg{100};
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    const std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(703, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_sid_70117_seg_100)
{
    constexpr size_t n_sids{70117};
    constexpr size_t sid_len{6};
    vector<uint8_t> ip;
    ip.push_back(0xfe);
    ip.push_back(0xe5);
    ip.push_back(0x11);
    ip.push_back(0x1);
    ip.push_back(0x0);
    for(size_t i{}; i < n_sids; ++i)
        ip.insert(ip.end(), sid_len, i);

    const size_t sids_per_seg{100};
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    
    size_t total_bytes_read{};
    constexpr size_t split{65423};

    const std::span s{ip.data(), split};
    const auto [bytes_read, bytes_reqd] = parser(s);
    total_bytes_read += bytes_read;
    BOOST_CHECK_EQUAL(split, bytes_read);
    BOOST_CHECK_EQUAL(ip.size() - split, bytes_reqd);
    BOOST_CHECK_EQUAL(split, parser.size());
    BOOST_CHECK_EQUAL(110, parser.segment_count());
    BOOST_CHECK_EQUAL(split, total_bytes_read);
    
    const std::span s2{ip.data() + split, ip.size() - split};
    const auto [bytes_read_2, bytes_reqd_2] = parser(s2);
    total_bytes_read += bytes_read_2;
    BOOST_CHECK_EQUAL(ip.size()-split, bytes_read_2);
    BOOST_CHECK_EQUAL(0, bytes_reqd_2);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(703, parser.segment_count());
    BOOST_CHECK_EQUAL(ip.size(), total_bytes_read);
}

BOOST_AUTO_TEST_CASE(parse_only_counted_bytes)
{
    std::vector<uint8_t> ip{make_msg(1)};
    ip.insert(ip.cend(), 6, 42); // shouldn't be parsed

    const size_t sids_per_seg{1};
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size()-6, bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size()-6, parser.size());
    BOOST_CHECK_EQUAL(2, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_half_a_sid)
{
    const std::vector<uint8_t> ip{make_msg(1)};
    const size_t sids_per_seg{1};
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    const span s{ip.data(), ip.size()};
    const span s1{s.first(var_int_len_1 + (sid_len/2))};
    const auto [bytes_read, bytes_reqd] = parser(s1);
    BOOST_CHECK_EQUAL(1, bytes_read);
    BOOST_CHECK_EQUAL(sid_len, bytes_reqd);
    BOOST_CHECK_EQUAL(1, parser.size());
    BOOST_CHECK_EQUAL(1, parser.segment_count());
    
    const span s2{s.subspan(bytes_read)};
    const auto [bytes_read_2, bytes_reqd_2] = parser(s2);
    BOOST_CHECK_EQUAL(6, bytes_read_2);
    BOOST_CHECK_EQUAL(0, bytes_reqd_2);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(2, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_part_msg)
{
    const vector<uint8_t> ip{make_msg(4)};
    const size_t sids_per_seg{2};
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    std::span s{ip.data(), ip.size() - 1};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size() - sid_len, bytes_read);
    BOOST_CHECK_EQUAL(sid_len, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size() - sid_len, parser.size());
}

const std::vector<uint8_t> mcci_msg{[]
{
    vector<uint8_t> v;
    constexpr int n{200};
    v.push_back(n); 
    for(int i{}; i < n; ++i)
        v.insert(v.end(), sid_len, i);
    return v;
}()};

BOOST_AUTO_TEST_CASE(parse_all)
{
    {
        // size(mcci_msg) < mcci_msg.size()
        fixed_len_multi_parser parser{sid_len, sids_per_seg};
        std::span s{mcci_msg.data(), mcci_msg.size() - 1};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(mcci_msg.size() - sid_len, bytes_read);
        BOOST_CHECK_EQUAL(sid_len, bytes_reqd);
        BOOST_CHECK_EQUAL(mcci_msg.size() - sid_len, parser.size());
    }

    {
        // size(mcci_msg) == mcci_msg.size()
        fixed_len_multi_parser parser{sid_len, sids_per_seg};
        std::span s{mcci_msg.data(), mcci_msg.size()};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(mcci_msg.size(), bytes_read);
        BOOST_CHECK_EQUAL(0, bytes_reqd);
        BOOST_CHECK_EQUAL(mcci_msg.size(), parser.size());
    }

    {
        // size(mcci_msg) > mcci_msg.size()
        fixed_len_multi_parser parser{sid_len, sids_per_seg};
        vector<uint8_t> ip{mcci_msg.cbegin(), mcci_msg.cend()};
        ip.insert(ip.cend(), 6, 42); // extra shortid that shouldn't get parsed
        std::span s{ip.data(), ip.size()};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(ip.size() - 6, bytes_read);
        BOOST_CHECK_EQUAL(0, bytes_reqd);
        BOOST_CHECK_EQUAL(ip.size() - 6, parser.size());

        const auto p = parser(s.subspan(bytes_read));
        BOOST_CHECK_EQUAL(0, p.first);
        BOOST_CHECK_EQUAL(0, p.second);
    }
}

BOOST_AUTO_TEST_CASE(parse_byte_by_byte)
{
    msg_parser_buffer parser{make_unique<msg_parser>(fixed_len_multi_parser{sid_len, sids_per_seg})};

    for(size_t i{}; i < mcci_msg.size(); ++i)
    {
        std::span s{mcci_msg.data() + i, 1};
        parser(s);
    }

    BOOST_CHECK_EQUAL(mcci_msg.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(parse_as_reqd)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    size_t total_bytes_read{};
    size_t offset{};
    size_t n{1};
    size_t passes{};
    while(total_bytes_read < mcci_msg.size())
    {
        span s{mcci_msg.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        ++passes;
        if(bytes_read)
        {
            total_bytes_read += bytes_read;
            offset += bytes_read;
            if(bytes_reqd)
                n += bytes_reqd - bytes_read;
        }
        else
        {
            n = bytes_reqd; 
        }
    }
    BOOST_CHECK_EQUAL(mcci_msg.size(), total_bytes_read);
    BOOST_CHECK_EQUAL(2, passes);
    BOOST_CHECK_EQUAL(mcci_msg.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(seg_offset)
{
    const vector<uint8_t> ip{make_msg(2)};
    const size_t sids_per_seg{1};
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    const std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(3, parser.segment_count());
    
    const auto [seg0, byte0] = parser.seg_offset(0);
    BOOST_CHECK_EQUAL(0, seg0);
    BOOST_CHECK_EQUAL(0, byte0);
    
    const auto [seg1, byte1] = parser.seg_offset(1);
    BOOST_CHECK_EQUAL(1, seg1);
    BOOST_CHECK_EQUAL(0, byte1);
    
    const auto [seg2, byte2] = parser.seg_offset(6);
    BOOST_CHECK_EQUAL(1, seg2);
    BOOST_CHECK_EQUAL(5, byte2);
    
    const auto [seg3, byte3] = parser.seg_offset(7);
    BOOST_CHECK_EQUAL(2, seg3);
    BOOST_CHECK_EQUAL(0, byte3);
    
    const auto [seg4, byte4] = parser.seg_offset(12);
    BOOST_CHECK_EQUAL(2, seg4);
    BOOST_CHECK_EQUAL(5, byte4);
}

BOOST_AUTO_TEST_CASE(read_all)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    std::span s{mcci_msg.data(), mcci_msg.size()};
    parser(s);
    BOOST_CHECK_EQUAL(mcci_msg.size(), size(parser));

    vector<uint8_t> out(mcci_msg.size());
    const auto bytes_read = read(parser, 0, std::span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(mcci_msg.size(), bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(mcci_msg.cbegin(), mcci_msg.cend(),
                                  out.cbegin(), out.cend());
    BOOST_CHECK_EQUAL(mcci_msg.size(), size(parser));
}

BOOST_AUTO_TEST_CASE(read_empty_span)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    std::span s{mcci_msg.data(), mcci_msg.size()};
    parser(s);

    vector<uint8_t> out(mcci_msg.size());
    const auto bytes_read = read(parser, 0, std::span{out.data(), 0});
    BOOST_CHECK_EQUAL(0, bytes_read);
}

BOOST_AUTO_TEST_CASE(read_empty_parser)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    vector<uint8_t> out(mcci_msg.size());
    const auto bytes_read = read(parser, 0, std::span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(0, bytes_read);
}

BOOST_AUTO_TEST_CASE(read_byte_by_byte)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    std::span s{mcci_msg.data(), mcci_msg.size()};
    parser(s);

    vector<uint8_t> out(mcci_msg.size());
    size_t bytes_read{};
    for(size_t i{}; i < mcci_msg.size(); ++i)
    {
        bytes_read += read(parser, i, span{out.data() + i, 1});
    }
    BOOST_CHECK_EQUAL(mcci_msg.size(), bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(mcci_msg.cbegin(), mcci_msg.cend(),
                                  out.cbegin(), out.cend());
}

BOOST_AUTO_TEST_CASE(read_beyond_parser_size)
{
    fixed_len_multi_parser parser{sid_len, sids_per_seg};
    std::span s{mcci_msg.data(), mcci_msg.size()};
    parser(s);
    BOOST_CHECK_EQUAL(mcci_msg.size(), size(parser));

    vector<uint8_t> out(mcci_msg.size() + 1);
    const auto bytes_read = read(parser, 0, std::span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(mcci_msg.size(), bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(mcci_msg.cbegin(), mcci_msg.cend(),
                                  out.cbegin(), out.cend() - 1);
}

BOOST_AUTO_TEST_CASE(read_reset_check)
{
    constexpr size_t seg_size{25};
    fixed_len_multi_parser parser{sid_len, seg_size};
    std::span s{mcci_msg.data(), mcci_msg.size()};
    parser(s);
    BOOST_CHECK_EQUAL(mcci_msg.size(), size(parser));

    vector<uint8_t> out(mcci_msg.size());
    const auto bytes_read = read(parser, 0, std::span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(mcci_msg.size(), bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(mcci_msg.cbegin(), mcci_msg.cend(),
                                  out.cbegin(), out.cend());
    BOOST_CHECK_EQUAL(mcci_msg.size(), size(parser));
}

BOOST_AUTO_TEST_SUITE_END()
