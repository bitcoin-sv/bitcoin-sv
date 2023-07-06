// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include "blocktxn_parser.h"

#include "parser_utils.h"
#include <ios>

using namespace std;
using namespace bsv;

std::pair<size_t, size_t> blocktxn_parser::operator()(
    const span<const uint8_t> s)
{
    const auto [hbytes_read, hbytes_reqd]{header_parser_(s)};
    if(hbytes_reqd)
        return make_pair(hbytes_read, hbytes_reqd);
    
    size_t total_bytes_read{hbytes_read};

    const auto [bytes_read, bytes_reqd]{txs_parser_(s.subspan(hbytes_read))};
    total_bytes_read += bytes_read;

    return make_pair(total_bytes_read, bytes_reqd);
}

size_t blocktxn_parser::read(size_t read_pos, span<uint8_t> s)
{
    const size_t total_parser_size{header_parser_.size() +
                                   txs_parser_.size()};

    if(read_pos >= total_parser_size)
        throw std::ios_base::failure("blocktxn_parser::read(): end of data");

    const size_t max_readable{min(s.size(), total_parser_size)};
    
    size_t total_bytes_read{};
    while(total_bytes_read < max_readable)
    {
        if(read_pos < header_parser_.size())
        {
            const size_t n{min(s.size(), 
                           header_parser_.size() - read_pos)};
            copy(header_parser_.cbegin() + read_pos, 
                 header_parser_.cbegin() + read_pos + n,
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

size_t blocktxn_parser::size() const 
{
    return header_parser_.size() + txs_parser_.size();
}

void blocktxn_parser::clear()
{
    header_parser_.clear();
    txs_parser_.clear();
}

