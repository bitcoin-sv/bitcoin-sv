// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <cstdio>
#include <memory>

// helper function for use with std::unique_ptr to enable RAII file closing
struct CloseFileDeleter
{
    void operator()(FILE* file) { ::fclose(file); }
};

using UniqueCFile = std::unique_ptr<FILE, CloseFileDeleter>;
