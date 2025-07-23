# Bitcoin SV Development Guidelines

## Architecture Overview

Bitcoin SV is a C++20 implementation of the Bitcoin protocol.

## Build System

Use CMake or autotools:
```bash
# CMake build
mkdir build && cd build
cmake -G Ninja -DCMAKE_UNITY_BUILD=ON
cmake --build
cmake --build --target test_bitcoin

# Autotools build
./autogen.sh
./configure
make -j$(nproc)
```

## Testing Strategy
- **Unit Tests**: `src/test/` using Boost.Test framework - run with `src/test/test_bitcoin`
- **Functional Tests**: `test/functional/` using Python - run with `test/functional/test_runner.py --extended --jobs=5 --output_type=0 --failfast`
- **Test Pattern**: File naming convention `<source_filename>_tests.cpp` with `BOOST_AUTO_TEST_SUITE`

## Code Style (C++20)
- Prefer list-initialization
- **Formatting**: 4-space indentation, braces on new lines for classes/functions and control flow
- **Modern C++**: Use modern C++ features wherever possible (e.g., `std::optional`, `std::variant`, concepts)

## Key Configuration
- Sanitizers available: `-Denable_asan=ON`, `-Denable_ubsan=ON`, `-Denable_tsan=ON`
- Debug mode: `-Denable_debug=ON`
