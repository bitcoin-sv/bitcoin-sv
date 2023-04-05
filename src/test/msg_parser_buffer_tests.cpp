// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include <boost/test/unit_test.hpp>

#include "net/msg_parser_buffer.h"

#include <numeric>
#include <utility>

using namespace std;
using namespace bsv;

BOOST_AUTO_TEST_SUITE(msg_parser_buffer_tests)

// reads n bytes at a time up to a max value;
template<size_t N>
class mod_n_byte_parser
{
    vector<uint8_t> v_;
    size_t max_size_{};

public:
    explicit mod_n_byte_parser(size_t max_size):
        max_size_{max_size}
    {}

    std::pair<size_t, size_t> operator()(bsv::span<const uint8_t> s)
    {
        if(v_.size() >= max_size_)
            return make_pair(0, 0);

        const auto old_size{v_.size()};
        while(s.size() >= N && (max_size_ - v_.size() >= N))
        {
            v_.insert(v_.end(), s.begin(), s.begin() + N);
            s = s.subspan(N);
        }
        
        const size_t remainder{s.size() % N};
        return make_pair(v_.size() - old_size,
                         remainder ? N : 0);
    }

    size_t read(size_t read_pos, bsv::span<uint8_t>)
    {
        assert(false);
        return 0;
    }

    size_t size() const { return v_.size(); }
    void clear() { v_.clear(); }
};

BOOST_AUTO_TEST_CASE(mod_n_byte_parser_tests)
{
    mod_n_byte_parser<10> parser{20};
    vector<uint8_t> v(11);

    size_t offset{};
    auto p = parser(span{v.data() + offset, 0});
    BOOST_CHECK_EQUAL(0, p.first);
    BOOST_CHECK_EQUAL(0, p.second);
    BOOST_CHECK_EQUAL(0, parser.size());
    
    p = parser(span{v.data() + offset, 1});
    BOOST_CHECK_EQUAL(0, p.first);
    BOOST_CHECK_EQUAL(10, p.second);
    BOOST_CHECK_EQUAL(0, parser.size());

    p = parser(span{v.data(), 2});
    BOOST_CHECK_EQUAL(0, p.first);
    BOOST_CHECK_EQUAL(10, p.second);
    BOOST_CHECK_EQUAL(0, parser.size());
    
    p = parser(span{v.data(), 10});
    BOOST_CHECK_EQUAL(10, p.first);
    BOOST_CHECK_EQUAL(0, p.second);
    BOOST_CHECK_EQUAL(10, parser.size());
    
    p = parser(span{v.data(), 11});
    BOOST_CHECK_EQUAL(10, p.first);
    BOOST_CHECK_EQUAL(10, p.second);
    BOOST_CHECK_EQUAL(20, parser.size());
    
    p = parser(span{v.data(), 11});
    BOOST_CHECK_EQUAL(0, p.first);
    BOOST_CHECK_EQUAL(0, p.second);
    BOOST_CHECK_EQUAL(20, parser.size());
}

BOOST_AUTO_TEST_CASE(buffer_unread_input)
{
    msg_parser_buffer buffer{make_unique<msg_parser>(mod_n_byte_parser<10>{10})};

    const vector<uint8_t> in{[]{
        vector<uint8_t> v(20);
        std::iota(v.begin(), v.end(), 0);
        return v;
    }()};

    buffer(span{in.data(), in.size()});
    BOOST_CHECK_EQUAL(10, buffer.buffer_size());
    BOOST_CHECK_EQUAL(10, buffer.buffer_size_reqd());

    buffer.clear();
    BOOST_CHECK_EQUAL(0, buffer.size());
    BOOST_CHECK_EQUAL(0, buffer.buffer_size());
    BOOST_CHECK_EQUAL(0, buffer.buffer_size_reqd());
}

BOOST_AUTO_TEST_CASE(buffer_unread_input_and_use_in_next_call)
{
    constexpr size_t N{10};
    msg_parser_buffer buffer{make_unique<msg_parser>(mod_n_byte_parser<N>{20})};

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
    BOOST_CHECK_EQUAL(2, buffer.buffer_size());
    BOOST_CHECK_EQUAL(N, buffer.buffer_size_reqd());
    BOOST_CHECK_EQUAL(3 * input_size, buffer.size());
    
    // previous input_size is buffered
    // nothing gets read
    // everything gets buffered
    offset += input_size;
    buffer(span{in.data() + offset, input_size});
    BOOST_CHECK_EQUAL(6, buffer.buffer_size());
    BOOST_CHECK_EQUAL(N, buffer.buffer_size_reqd());
    BOOST_CHECK_EQUAL(4 * input_size, buffer.size());
    
    // previous input_size is buffered
    // everything gets read
    // nothing gets buffered
    offset += input_size;
    buffer(span{in.data() + offset, input_size});
    BOOST_CHECK_EQUAL(0, buffer.buffer_size());
    BOOST_CHECK_EQUAL(0, buffer.buffer_size_reqd());
    BOOST_CHECK_EQUAL(5 * input_size, buffer.size());
    
    // nothing is buffered
    // nothing gets read
    // input gets buffered (overflow mode)
    offset += input_size;
    buffer(span{in.data() + offset, input_size});
    BOOST_CHECK_EQUAL(4, buffer.buffer_size());
    BOOST_CHECK_EQUAL(0, buffer.buffer_size_reqd());
}

BOOST_AUTO_TEST_CASE(parse_byte_by_byte)
{
    msg_parser_buffer buffer{make_unique<msg_parser>(mod_n_byte_parser<10>{110})};

    const vector<uint8_t> in{[]{
        vector<uint8_t> v(110);
        std::iota(v.begin(), v.end(), 0);
        return v;
    }()};
    
    for(size_t i{}; i < in.size(); ++i)
    {
        buffer(bsv::span{in.data() + i, 1});
        const auto remainder{(i + 1) % 10};
        BOOST_CHECK_EQUAL(remainder, buffer.buffer_size());
        BOOST_CHECK_EQUAL(remainder ? 10 : 0, buffer.buffer_size_reqd());
    }
}

BOOST_AUTO_TEST_CASE(parse_byte_by_n_bytes)
{
    msg_parser_buffer buffer{make_unique<msg_parser>(mod_n_byte_parser<10>{110})};

    const vector<uint8_t> in{[]{
        vector<uint8_t> v(110);
        std::iota(v.begin(), v.end(), 0);
        return v;
    }()};
   
    const size_t inc{11};
    for(size_t i{}; i < in.size(); i += inc)
    {
        buffer(bsv::span{in.data() + i, inc});
        const auto remainder{(i + 1) % 10};
        BOOST_CHECK_EQUAL(remainder, buffer.buffer_size());
        BOOST_CHECK_EQUAL(buffer.buffer_size_reqd() ? 10 : 0, buffer.buffer_size_reqd());
    }
}

BOOST_AUTO_TEST_CASE(parse_buffer_size)
{
    msg_parser_buffer parser{make_unique<msg_parser>(mod_n_byte_parser<10>{50})};

    const vector<uint8_t> in{[]{
        vector<uint8_t> v(42);
        std::iota(v.begin(), v.end(), 0);
        return v;
    }()};

    bsv::span s{in.data(), in.size()};
    constexpr size_t n{3};
    constexpr size_t m{14};
    constexpr size_t q{20};
    {
        parser(s.first(n));
        BOOST_CHECK_EQUAL(n, parser.buffer_size());
        BOOST_CHECK_EQUAL(10, parser.buffer_size_reqd());
        s = s.subspan(n);
    }
    {
        parser(s.first(m));
        BOOST_CHECK_EQUAL(n + m - 10, parser.buffer_size());
        BOOST_CHECK_EQUAL(10, parser.buffer_size_reqd());
        s = s.subspan(m);
    }
    {
        parser(s.first(q));
        BOOST_CHECK_EQUAL(n + m + q - 30, parser.buffer_size());
        BOOST_CHECK_EQUAL(10, parser.buffer_size_reqd());
    }
}

struct always_0_parser
{
    std::pair<size_t, size_t> operator()(bsv::span<const uint8_t>)
    {
        return std::make_pair(0, 0);
    }

    size_t read(size_t read_pos, bsv::span<uint8_t>)
    {
        assert(false);
        return 0;
    }

    size_t size() const
    {
        assert(false);
        return 0;
    }

    void clear()
    {
        assert(false);
    }
};

BOOST_AUTO_TEST_CASE(overflow_on_nothing_read_or_reqd)
{
    msg_parser_buffer buffer{make_unique<msg_parser>(always_0_parser{})};
    vector<uint8_t> v(42, 42);

    const bsv::span s{v.data(), v.size()};
    buffer(s);
    BOOST_CHECK_EQUAL(v.size(), buffer.buffer_size());
    BOOST_CHECK_EQUAL(0, buffer.buffer_size_reqd());
    
    buffer(s);
    BOOST_CHECK_EQUAL(2 * v.size(), buffer.buffer_size());
    BOOST_CHECK_EQUAL(0, buffer.buffer_size_reqd());
}

BOOST_AUTO_TEST_SUITE_END()

