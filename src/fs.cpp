#include "fs.h"

#include <boost/filesystem.hpp>

namespace fsbridge {

FILE *fopen(const fs::path &p, const char *mode)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return ::fopen(p.string().c_str(), mode);
}

FILE *freopen(const fs::path &p, const char *mode, FILE *stream)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return ::freopen(p.string().c_str(), mode, stream);
}

} // fsbridge
