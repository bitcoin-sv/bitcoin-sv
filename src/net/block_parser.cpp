// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include "block_parser.h"

#include <ios>

#include "parser_utils.h"

using namespace std;

std::pair<size_t, size_t> block_parser::operator()(
    const std::span<const uint8_t> s)
{
    const auto [hbytes_read, hbytes_reqd]{header_parser_(s)};
    if(hbytes_reqd)
        return make_pair(hbytes_read, hbytes_reqd);

    size_t total_bytes_read{hbytes_read};

    const auto [bytes_read, bytes_reqd]{txs_parser_(s.subspan(hbytes_read))};
    total_bytes_read += bytes_read;

    return make_pair(total_bytes_read, bytes_reqd);
}

size_t block_parser::read(size_t read_pos, std::span<uint8_t> s)
{
    if(read_pos >= readable_size())
        throw std::ios_base::failure("block_parser::read(): end of data");

    const size_t max_readable{min(s.size(), readable_size())};

    size_t total_bytes_read{};
    while(total_bytes_read < max_readable)
    {
        if(read_pos < header_parser_.size())
        {
            const size_t n{min(s.size(), 
                           header_parser_.size() - read_pos)};
            copy(header_parser_.cbegin() + read_pos,     //NOLINT(*-narrowing-conversions)
                 header_parser_.cbegin() + read_pos + n, //NOLINT(*-narrowing-conversions)
                 s.begin());
            read_pos += n;
            total_bytes_read += n;
            s = s.subspan(n);
        }
        else
        {
            const size_t bytes_read = ::read(txs_parser_, 
                                             read_pos - 
                                             header_parser_.size(), 
                                             s);
            read_pos += bytes_read;
            total_bytes_read += bytes_read;
            s = s.subspan(bytes_read);
        }
    }

    return total_bytes_read;
}

size_t block_parser::size() const 
{
    return header_parser_.size() + txs_parser_.size();
}

size_t block_parser::readable_size() const 
{
    return header_parser_.size() + txs_parser_.readable_size();
}

