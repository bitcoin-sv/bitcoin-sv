// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

#include "net/msg_parser_buffer.h"

#include "mod_n_byte_parser.h"

#include <cstdint>
#include <numeric>
#include <utility>

#include "net/block_parser.h"
#include "net/msg_parser_buffer.h"
#include "net/p2p_msg_lengths.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(msg_parser_buffer_tests)

struct always_0_parser
{
    std::pair<size_t, size_t> operator()(std::span<const uint8_t>)
    {
        return std::make_pair(0, 0);
    }

    size_t read(size_t read_pos, std::span<uint8_t>)
    {
        assert(false);
        return 0;
    }

    size_t readable_size() const
    {
        return 0;
    }

    size_t size() const
    {
        return 0;
    }

    void clear()
    {
        assert(false);
    }
};

BOOST_AUTO_TEST_CASE(mod_n_byte_parser_tests)
{
    mod_n_byte_parser<10, 20> parser;
    vector<uint8_t> v(20);

    auto p = parser(span{v.data(), 0});
    BOOST_CHECK_EQUAL(0U, p.first);
    BOOST_CHECK_EQUAL(10U, p.second);
    BOOST_CHECK_EQUAL(0U, parser.size());
    
    p = parser(span{v.data(), 1});
    BOOST_CHECK_EQUAL(0U, p.first);
    BOOST_CHECK_EQUAL(10U, p.second);
    BOOST_CHECK_EQUAL(0U, parser.size());

    p = parser(span{v.data(), 10});
    BOOST_CHECK_EQUAL(10U, p.first);
    BOOST_CHECK_EQUAL(10U, p.second);
    BOOST_CHECK_EQUAL(10U, parser.size());
    
    p = parser(span{v.data() + 10U, 5});
    BOOST_CHECK_EQUAL(0U, p.first);
    BOOST_CHECK_EQUAL(10U, p.second);
    BOOST_CHECK_EQUAL(10U, parser.size());
    
    p = parser(span{v.data() + 10U, 10});
    BOOST_CHECK_EQUAL(10U, p.first);
    BOOST_CHECK_EQUAL(0U, p.second);
    BOOST_CHECK_EQUAL(20U, parser.size());
}

BOOST_AUTO_TEST_CASE(buffer_empty_input_empty)
{
    msg_parser_buffer parser{make_unique<msg_parser>(mod_n_byte_parser<2, 2>{})};
    const vector<uint8_t> in;
    parser(span{in.data(), in.size()});
    BOOST_CHECK_EQUAL(0, parser.size());
    BOOST_CHECK_EQUAL(0, parser.readable_size());
    BOOST_CHECK_EQUAL(0, parser.buffer_size());
    BOOST_CHECK_EQUAL(2, parser.buffer_size_reqd());
    BOOST_CHECK(!parser.parser_full());
}

BOOST_AUTO_TEST_CASE(buffer_empty_bytes_read_bytes_reqd)
{
    msg_parser_buffer parser{make_unique<msg_parser>(mod_n_byte_parser<2, 4>{})};
    const vector<uint8_t> in{1, 2, 3};
    parser(span{in.data(), in.size()});
    BOOST_CHECK_EQUAL(3, parser.size());
    BOOST_CHECK_EQUAL(2, parser.readable_size());
    BOOST_CHECK_EQUAL(1, parser.buffer_size());
    BOOST_CHECK_EQUAL(2, parser.buffer_size_reqd());
    BOOST_CHECK(!parser.parser_full());
}

BOOST_AUTO_TEST_CASE(buffer_empty_bytes_read_no_bytes_reqd)
{
    msg_parser_buffer parser{make_unique<msg_parser>(mod_n_byte_parser<2, 2>{})};
    const vector<uint8_t> in{1, 2, 3};
    parser(span{in.data(), in.size()});
    BOOST_CHECK_EQUAL(3, parser.size());
    BOOST_CHECK_EQUAL(2, parser.readable_size());
    BOOST_CHECK_EQUAL(1, parser.buffer_size());      // overflow: extra data has 
    BOOST_CHECK_EQUAL(0, parser.buffer_size_reqd()); // been buffered. 
    BOOST_CHECK(parser.parser_full());
}

BOOST_AUTO_TEST_CASE(buffer_empty_no_bytes_read_bytes_reqd)
{
    msg_parser_buffer parser{make_unique<msg_parser>(mod_n_byte_parser<2, 2>{})};
    const vector<uint8_t> in{1};
    parser(span{in.data(), 0});
    BOOST_CHECK_EQUAL(0, parser.size());
    BOOST_CHECK_EQUAL(0, parser.readable_size());
    BOOST_CHECK_EQUAL(0, parser.buffer_size());
    BOOST_CHECK_EQUAL(2, parser.buffer_size_reqd());
    BOOST_CHECK(!parser.parser_full());

    parser(span{in.data(), in.size()});
    BOOST_CHECK_EQUAL(1, parser.size());
    BOOST_CHECK_EQUAL(0, parser.readable_size());
    BOOST_CHECK_EQUAL(1, parser.buffer_size());
    BOOST_CHECK_EQUAL(2, parser.buffer_size_reqd());
    BOOST_CHECK(!parser.parser_full());
}

BOOST_AUTO_TEST_CASE(buffer_empty_no_bytes_read_no_bytes_reqd)
{
    msg_parser_buffer parser{make_unique<msg_parser>(always_0_parser{})};
    const vector<uint8_t> in;
    parser(span{in.data(), in.size()});
    BOOST_CHECK_EQUAL(0, parser.size());
    BOOST_CHECK_EQUAL(0, parser.readable_size());
    BOOST_CHECK_EQUAL(0, parser.buffer_size());
    BOOST_CHECK_EQUAL(0, parser.buffer_size_reqd());
    BOOST_CHECK(parser.parser_full());
}

BOOST_AUTO_TEST_CASE(buffer_not_empty_input_empty)
{
    msg_parser_buffer parser{make_unique<msg_parser>(mod_n_byte_parser<2, 2>{})};
    const vector<uint8_t> in{1};
    parser(span{in.data(), in.size()});
    BOOST_CHECK_EQUAL(1, parser.size());
    BOOST_CHECK_EQUAL(0, parser.readable_size());
    BOOST_CHECK_EQUAL(1, parser.buffer_size());
    BOOST_CHECK_EQUAL(2, parser.buffer_size_reqd());
    BOOST_CHECK(!parser.parser_full());
    
    parser(span{in.data(), 0});
    BOOST_CHECK_EQUAL(1, parser.size());
    BOOST_CHECK_EQUAL(0, parser.readable_size());
    BOOST_CHECK_EQUAL(1, parser.buffer_size());
    BOOST_CHECK_EQUAL(2, parser.buffer_size_reqd());
    BOOST_CHECK(!parser.parser_full());
}

BOOST_AUTO_TEST_CASE(buffer_not_empty_bytes_read_bytes_reqd)
{
    msg_parser_buffer parser{make_unique<msg_parser>(mod_n_byte_parser<2, 4>{})};
    const vector<uint8_t> in{1, 2, 3};
    parser(span{in.data(), 1});
    BOOST_CHECK_EQUAL(1, parser.size());
    BOOST_CHECK_EQUAL(0, parser.readable_size());
    BOOST_CHECK_EQUAL(1, parser.buffer_size());
    BOOST_CHECK_EQUAL(2, parser.buffer_size_reqd());
    BOOST_CHECK(!parser.parser_full());
    
    parser(span{in.data() + 1, in.size() - 1});
    BOOST_CHECK_EQUAL(3, parser.size());
    BOOST_CHECK_EQUAL(2, parser.readable_size());
    BOOST_CHECK_EQUAL(1, parser.buffer_size());
    BOOST_CHECK_EQUAL(2, parser.buffer_size_reqd());
    BOOST_CHECK(!parser.parser_full());
}

BOOST_AUTO_TEST_CASE(buffer_not_empty_bytes_read_no_bytes_reqd)
{
    msg_parser_buffer parser{make_unique<msg_parser>(mod_n_byte_parser<2, 2>{})};
    const vector<uint8_t> in{ 1, 2, 3, 4 };

    parser(span{in.data(), 1});
    BOOST_CHECK_EQUAL(1, parser.buffer_size());
    BOOST_CHECK_EQUAL(0, parser.readable_size());
    BOOST_CHECK_EQUAL(1, parser.buffer_size());
    BOOST_CHECK_EQUAL(2, parser.buffer_size_reqd());
    BOOST_CHECK(!parser.parser_full());

    parser(span{in.data() + 1, in.size() - 1});
    BOOST_CHECK_EQUAL(4, parser.size());
    BOOST_CHECK_EQUAL(2, parser.readable_size());
    BOOST_CHECK_EQUAL(2, parser.buffer_size());
    BOOST_CHECK_EQUAL(0, parser.buffer_size_reqd());
    BOOST_CHECK(parser.parser_full());
}

struct always_req_more_parser
{
    std::pair<size_t, size_t> operator()(std::span<const uint8_t> s) 
    {
        return make_pair(0, s.size() + 1);
    }

    size_t read(size_t read_pos, std::span<uint8_t>)
    {
        assert(false);
        return 0;
    }
    
    size_t readable_size() const
    {
        return 0;
    }

    size_t size() const
    {
        return 0;
    }

    void clear()
    {
        assert(false);
    }
};

BOOST_AUTO_TEST_CASE(buffer_not_empty_no_bytes_read_bytes_reqd)
{
    msg_parser_buffer parser{make_unique<msg_parser>(always_req_more_parser{})};
    const vector<uint8_t> in{1, 2, 3};
    parser(span{in.data(), 1});
    BOOST_CHECK_EQUAL(1, parser.size());
    BOOST_CHECK_EQUAL(0, parser.readable_size());
    BOOST_CHECK_EQUAL(1, parser.buffer_size());
    BOOST_CHECK_EQUAL(2, parser.buffer_size_reqd());
    BOOST_CHECK(!parser.parser_full());
    
    parser(span{in.data() + 1, 1});
    BOOST_CHECK_EQUAL(2, parser.size());
    BOOST_CHECK_EQUAL(0, parser.readable_size());
    BOOST_CHECK_EQUAL(2, parser.buffer_size());
    BOOST_CHECK_EQUAL(3, parser.buffer_size_reqd());
    BOOST_CHECK(!parser.parser_full());
}

BOOST_AUTO_TEST_CASE(buffer_unread_input)
{
    msg_parser_buffer buffer{make_unique<msg_parser>(mod_n_byte_parser<10, 10>{})};

    const vector<uint8_t> in{[]{
        vector<uint8_t> v(20);
        std::iota(v.begin(), v.end(), 0);
        return v;
    }()};

    buffer(span{in.data(), in.size()});
    BOOST_CHECK_EQUAL(10U, buffer.buffer_size());
    BOOST_CHECK_EQUAL(0U, buffer.buffer_size_reqd());

    buffer.clear();
    BOOST_CHECK_EQUAL(0U, buffer.size());
    BOOST_CHECK_EQUAL(0U, buffer.buffer_size());
    BOOST_CHECK_EQUAL(0U, buffer.buffer_size_reqd());
}

BOOST_AUTO_TEST_CASE(buffer_unread_input_and_use_in_next_call)
{
    constexpr size_t N{10};
    msg_parser_buffer buffer{make_unique<msg_parser>(mod_n_byte_parser<N, 20>{})};

    const vector<uint8_t> in{[]{
        vector<uint8_t> v(24);
        std::iota(v.begin(), v.end(), 0);
        return v;
    }()};

    // nothing is buffered
    // nothing gets read
    // everything gets buffered
    size_t offset{}; 
    constexpr size_t input_size{4};
    buffer(span{in.data() + offset, input_size});
    BOOST_CHECK_EQUAL(input_size, buffer.buffer_size());
    BOOST_CHECK_EQUAL(N, buffer.buffer_size_reqd());
    BOOST_CHECK_EQUAL(input_size, buffer.size());

    // previous input_size is buffered
    // nothing gets read
    // everything gets buffered
    offset += input_size;
    buffer(span{in.data() + offset, input_size});
    BOOST_CHECK_EQUAL(2 * input_size, buffer.buffer_size());
    BOOST_CHECK_EQUAL(N, buffer.buffer_size_reqd());
    BOOST_CHECK_EQUAL(2 * input_size, buffer.size());

    // previous inputs are buffered
    // buffer + half of input gets read
    // half of input gets buffered
    offset += input_size;
    buffer(span{in.data() + offset, input_size});
    BOOST_CHECK_EQUAL(2U, buffer.buffer_size());
    BOOST_CHECK_EQUAL(N, buffer.buffer_size_reqd());
    BOOST_CHECK_EQUAL(3 * input_size, buffer.size());
    
    // previous input_size is buffered
    // nothing gets read
    // everything gets buffered
    offset += input_size;
    buffer(span{in.data() + offset, input_size});
    BOOST_CHECK_EQUAL(6U, buffer.buffer_size());
    BOOST_CHECK_EQUAL(N, buffer.buffer_size_reqd());
    BOOST_CHECK_EQUAL(4 * input_size, buffer.size());
    
    // previous input_size is buffered
    // everything gets read
    // nothing gets buffered
    offset += input_size;
    buffer(span{in.data() + offset, input_size});
    BOOST_CHECK_EQUAL(0U, buffer.buffer_size());
    BOOST_CHECK_EQUAL(0U, buffer.buffer_size_reqd());
    BOOST_CHECK_EQUAL(5 * input_size, buffer.size());
    
    // nothing is buffered
    // nothing gets read
    // input gets buffered (overflow mode)
    offset += input_size;
    buffer(span{in.data() + offset, input_size});
    BOOST_CHECK_EQUAL(4U, buffer.buffer_size());
    BOOST_CHECK_EQUAL(0U, buffer.buffer_size_reqd());
}

BOOST_AUTO_TEST_CASE(parse_byte_by_byte)
{
    constexpr auto max_size{110};
    msg_parser_buffer buffer{make_unique<msg_parser>(mod_n_byte_parser<10, max_size>{})};

    const vector<uint8_t> in{[]{
        vector<uint8_t> v(110);
        std::iota(v.begin(), v.end(), 0);
        return v;
    }()};
    
    for(size_t i{}; i < in.size(); ++i)
    {
        buffer(std::span{in.data() + i, 1});
        const auto remainder{(i + 1) % 10};
        BOOST_CHECK_EQUAL(remainder, buffer.buffer_size());
        BOOST_CHECK_EQUAL(buffer.size() < max_size ? 10U : 0U, buffer.buffer_size_reqd());
    }
}

BOOST_AUTO_TEST_CASE(parse_byte_by_n_bytes)
{
    msg_parser_buffer buffer{make_unique<msg_parser>(mod_n_byte_parser<10, 110>{})};

    const vector<uint8_t> in{[]{
        vector<uint8_t> v(110);
        std::iota(v.begin(), v.end(), 0);
        return v;
    }()};
   
    const size_t inc{11};
    for(size_t i{}; i < in.size(); i += inc)
    {
        buffer(std::span{in.data() + i, inc});
        const auto remainder{(i + 1) % 10};
        BOOST_CHECK_EQUAL(remainder, buffer.buffer_size());
        BOOST_CHECK_EQUAL(buffer.buffer_size_reqd() ? 10U : 0U, buffer.buffer_size_reqd());
    }
}

BOOST_AUTO_TEST_CASE(parse_buffer_size)
{
    msg_parser_buffer parser{make_unique<msg_parser>(mod_n_byte_parser<10, 50>{})};

    const vector<uint8_t> in{[]{
        vector<uint8_t> v(42);
        std::iota(v.begin(), v.end(), 0);
        return v;
    }()};

    std::span s{in.data(), in.size()};
    constexpr size_t n{3};
    constexpr size_t m{14};
    constexpr size_t q{20};
    {
        parser(s.first(n));
        BOOST_CHECK_EQUAL(n, parser.buffer_size());
        BOOST_CHECK_EQUAL(10U, parser.buffer_size_reqd());
        s = s.subspan(n);
    }
    {
        parser(s.first(m));
        BOOST_CHECK_EQUAL(n + m - 10, parser.buffer_size());
        BOOST_CHECK_EQUAL(10U, parser.buffer_size_reqd());
        s = s.subspan(m);
    }
    {
        parser(s.first(q));
        BOOST_CHECK_EQUAL(n + m + q - 30, parser.buffer_size());
        BOOST_CHECK_EQUAL(10U, parser.buffer_size_reqd());
    }
}

BOOST_AUTO_TEST_CASE(overflow_on_nothing_read_or_reqd)
{
    msg_parser_buffer buffer{make_unique<msg_parser>(always_0_parser{})};
    vector<uint8_t> v(42, 42);

    const std::span s{v.data(), v.size()};
    buffer(s);
    BOOST_CHECK_EQUAL(v.size(), buffer.buffer_size());
    BOOST_CHECK_EQUAL(0U, buffer.buffer_size_reqd());
    
    buffer(s);
    BOOST_CHECK_EQUAL(2 * v.size(), buffer.buffer_size());
    BOOST_CHECK_EQUAL(0U, buffer.buffer_size_reqd());
}

BOOST_AUTO_TEST_CASE(handle_parser_not_reading_reqd_bytes)
{
    msg_parser_buffer buffer{make_unique<msg_parser>(block_parser{})};
    vector<uint8_t> v(bsv::block_header_len, 0);   // header
    v.push_back(0x1);           // 1 tx

    v.insert(v.cend(), bsv::version_len, 2);
    v.push_back(1);  // n inputs
    v.insert(v.cend(), bsv::outpoint_len, 3);
    v.push_back(0);  // script len
    v.insert(v.cend(), bsv::seq_len, 4);
    v.push_back(1);  // n outputs
    v.insert(v.cend(), bsv::value_len, 4);
    v.push_back(0xfd);
    v.push_back(1);
    v.push_back(2);

    constexpr uint8_t len{bsv::block_header_len
                          + bsv::var_int_len_1
                          + bsv::version_len
                          + bsv::var_int_len_1
                          + bsv::outpoint_len
                          + bsv::var_int_len_1
                          + bsv::seq_len
                          + bsv::var_int_len_1}; 

    const std::span s2{v.data(), len + 1};
    buffer(s2);
    BOOST_CHECK_EQUAL(1, buffer.buffer_size());
    BOOST_CHECK_EQUAL(bsv::value_len + bsv::var_int_len_1, buffer.buffer_size_reqd());
    BOOST_CHECK_EQUAL(len + 1, buffer.size());
    
    const std::span s3{v.data() + len + 1, 9};
    buffer(s3);
    BOOST_CHECK_EQUAL(1 + bsv::value_len + bsv::var_int_len_1, buffer.buffer_size());
    BOOST_CHECK_EQUAL(bsv::value_len + bsv::var_int_len_3, buffer.buffer_size_reqd());
    BOOST_CHECK_EQUAL(len + 10, buffer.size());
}

BOOST_AUTO_TEST_SUITE_END()

