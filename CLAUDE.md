# SV Node Repository

## Overview
This is the SV Node implementation for the BSV Blockchain, focused on scaling and enterprise features. The codebase is a fork of Bitcoin Core with significant enhancements specific to the BSV Blockchain vision.

## Repository Structure

### Core Directories
- `/src` - Main source code (C++)
- `/src/test/` - Unit tests (C++)
- `/test/functional/` - Functional tests (Python)
- `/doc` - Documentation
- `/contrib` - Contributed scripts and tools
- `/depends` - Dependencies management
- `/cmake` - CMake build configuration
- `/share` - Shared resources

### Key Components in `/src`
- `consensus/` - Consensus rules and validation
- `crypto/` - Cryptographic primitives
- `net/` - P2P networking
- `rpc/` - RPC server implementation
- `mining/` - Mining functionality
- `wallet/` - Wallet implementation
- `script/` - Bitcoin script interpreter
- `miner_id/` - Miner identification features
- `double_spend/` - Double spend detection system

## Programming Languages
- **C++** (.cpp, .h) - Primary implementation language
- **C** (.c) - Used in crypto and leveldb components
- **Python** (.py) - Test scripts and utilities
- **Shell** (.sh) - Build and deployment scripts
- **CMake** - Build system

## Build Systems
- Autotools (primary, officially supported for releases) - `configure.ac`, `Makefile.am`
- CMake (alternative) - `CMakeLists.txt`

## Notable BSV Blockchain Features
- Large block support
- Parallel block validation (PBV)
- Parallel transaction validation (both free transactions and during block validation)
- Miner ID implementation
- Double spend detection and reporting
- Frozen TXO functionality
- ZMQ support for real-time notifications
- Enhanced transaction validation

## External Dependencies
- LevelDB - Embedded at `/src/leveldb/`
- Secp256k1 - Bitcoin's elliptic curve library at `/src/secp256k1/`
- UniValue - JSON parsing library at `/src/univalue/`

## Testing
- Unit tests in `/src/test/` (C++)
- Functional tests in `/test/functional/` (Python)
- Over 328 Python test files for comprehensive coverage

## Development Commands
When working on this codebase, common commands include:
- Build commands (check README.md for specific instructions)
- Test execution commands
- Linting and type checking (specific commands should be verified with the user)
- GitHub CLI (`gh`) for accessing private repository PRs and issues

## Quality and Safety
The codebase emphasizes code quality and safety through:
- Extensive test coverage (unit and functional tests)
- Safe string function usage
- Comprehensive null pointer checks

## CI/CD
- Uses GitHub Actions (workflows in `.github/workflows/`)

## License
Open BSV License (see LICENSE file)