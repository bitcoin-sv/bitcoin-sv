# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

cmake_minimum_required(VERSION 3.5)
project(BitcoinSV)

set(CMAKE_CXX_STANDARD 17)

# Default visibility is hidden on all targets.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

option(ENABLE_PROD_BUILD "Enable production build" OFF)
option(BUILD_BITCOIN_WALLET "Activate the wallet functionality" ON)
option(BUILD_BITCOIN_ZMQ "Activate the ZeroMQ functionalities" ON)

if (NOT MSVC)
  #bitcoin seeder can not be build with MSVC
	option(BUILD_BITCOIN_SEEDER "Build bitcoin-seeder" ON)
endif()

option(BUILD_BITCOIN_CLI "Build bitcoin-cli" ON)
option(BUILD_BITCOIN_TX "Build bitcoin-tx" ON)
option(BUILD_BITCOIN_MINER "Build bitcoin-miner" ON)
option(BUILD_BITCOIN_BENCH "Build benchmarks" ON)

# Ensure that WINDRES_PREPROC is enabled when using windres.
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	list(APPEND CMAKE_RC_FLAGS "-DWINDRES_PREPROC")
endif()

# Enable warning
include(AddCompilerFlags)

add_c_compiler_flag(-Wnested-externs -Wstrict-prototypes)
add_compiler_flag(
	-Wall
	-Wextra
	-Wformat
	-Wvla
	-Wformat-security
	-Wcast-align
	-Wunused-parameter
)

option(EXTRA_WARNINGS "Enable extra warnings" OFF)
if(EXTRA_WARNINGS)
	add_compiler_flag(-Wshadow)
	add_cxx_compiler_flag(-Wsuggest-override)
else()
	add_compiler_flag(-Wno-unused-parameter)
endif()

if(MSVC)
	# Do not define min/max macros - we use std::min/max
	add_compile_definitions(NOMINMAX)
	# Perform parallel builds:
	add_compile_options("/MP")

	# Fix Cmake's default compiler options - see also https://gitlab.kitware.com/cmake/community/wikis/FAQ#dynamic-replace
	# - remove default NDEBUG compile definition to solve #error "Bitcoin cannot be compiled without assertions"
	# - use static runtime instead of dynamic runtime to match the external libraries  
	foreach(flag_var
		CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
		CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO
		CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
		CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)
		
	if(${flag_var} MATCHES "/MD")
			string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")    
		endif()

		if(${flag_var} MATCHES "/DNDEBUG")
			string(REGEX REPLACE "/DNDEBUG" "" ${flag_var} "${${flag_var}}")
		endif()

	endforeach()

endif()

# openssl library
include(BrewHelper)
find_brew_prefix(OPENSSL_ROOT_DIR openssl)
find_package(OpenSSL REQUIRED)

# libtool style configure
add_subdirectory(config)

# libraries
add_subdirectory(crypto)
add_subdirectory(leveldb)
add_subdirectory(secp256k1)
add_subdirectory(univalue)

# Because the Bitcoin ABc source code is disorganised, we
# end up with a bunch of libraries without any aparent
# cohesive structure. This is inherited from Bitcoin Core
# and reflecting this.
# TODO: Improve the structure once cmake is rocking.

# Various completely unrelated features shared by all executables.
add_library(util
	chainparamsbase.cpp
	clientversion.cpp
	compat/glibc_sanity.cpp
	compat/glibcxx_sanity.cpp
	compat/strnlen.cpp
	fs.cpp
	logging.cpp
	random.cpp
	rpc/protocol.cpp
	support/cleanse.cpp
	support/lockedpool.cpp
	sync.cpp
	threadinterrupt.cpp
	threadpriority.cpp
	uint256.cpp
	util.cpp
	utilmoneystr.cpp
	utilstrencodings.cpp
	utiltime.cpp
)

if(ENABLE_PROD_BUILD)
    target_compile_definitions(util PUBLIC IS_PRODUCTION_BUILD)
endif()
target_compile_definitions(util PUBLIC HAVE_CONFIG_H)
target_include_directories(util
	PUBLIC
		.
		# To access the config.
		${CMAKE_CURRENT_BINARY_DIR}
	PRIVATE
		${OPENSSL_INCLUDE_DIR}
)

# Target specific configs
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(Boost_USE_STATIC_LIBS ON)
	set(Boost_THREADAPI win32)

	find_package(SHLWAPI REQUIRED)
	target_link_libraries(util ${SHLWAPI_LIBRARY})
	target_include_directories(util PUBLIC ${SHLWAPI_INCLUDE_DIR})

	find_library(WS2_32_LIBRARY NAMES ws2_32)
	target_link_libraries(util ${WS2_32_LIBRARY})
	
	target_compile_definitions(util PUBLIC BOOST_THREAD_USE_LIB)
endif()

# Boost packages
set(BOOST_PACKAGES_REQUIRED chrono filesystem program_options thread)

function(prepend var prefix)
	set(listVar "")
	foreach(f ${ARGN})
		list(APPEND listVar "${prefix}${f}")
	endforeach(f)
	set(${var} "${listVar}" PARENT_SCOPE)
endfunction(prepend)

prepend(BOOST_LIBRARIES "Boost::" ${BOOST_PACKAGES_REQUIRED})

find_package(Boost 1.59 REQUIRED ${BOOST_PACKAGES_REQUIRED})
target_link_libraries(util univalue crypto ${BOOST_LIBRARIES})

# Make sure boost uses std::atomic (it doesn't before 1.63)
target_compile_definitions(util PUBLIC BOOST_SP_USE_STD_ATOMIC BOOST_AC_USE_STD_ATOMIC)

# More completely unrelated features shared by all executables.
# Because nothing says this is different from util than "common"
add_library(common
	amount.cpp
	base58.cpp
	chainparams.cpp
	config.cpp
	consensus/merkle.cpp
	coins.cpp
	compressor.cpp
	dstencode.cpp
	core_read.cpp
	core_write.cpp
	key.cpp
	keystore.cpp
	netaddress.cpp
	netbase.cpp
	primitives/block.cpp
	protocol.cpp
	rpc/jsonwriter.cpp
	scheduler.cpp
	script/sign.cpp
	script/standard.cpp
	warnings.cpp
)

target_link_libraries(common util secp256k1)

# libbitcoinconsensus
add_library(bitcoinconsensus
	arith_uint256.cpp
    big_int.cpp
	hash.cpp
	primitives/transaction.cpp
	pubkey.cpp
	script/bitcoinconsensus.cpp
	script/interpreter.cpp
	script/limitedstack.cpp
	script/script.cpp
	script/script_num.cpp
	script/script_error.cpp
	uint256.cpp
	utilstrencodings.cpp
)

target_link_libraries(bitcoinconsensus common)
target_include_directories(bitcoinconsensus
	PRIVATE
		${OPENSSL_INCLUDE_DIR}
)

# Bitcoin server facilities
add_library(server
	addrman.cpp
	addrdb.cpp
	bloom.cpp
	blockencodings.cpp
	blockfileinfostore.cpp
	chain.cpp
	checkpoints.cpp
	config.cpp
	httprpc.cpp
	httpserver.cpp
	init.cpp
	dbwrapper.cpp
	merkleblock.cpp
	mining/assembler.cpp
	mining/candidates.cpp
	mining/factory.cpp
	mining/journal.cpp
	mining/journal_builder.cpp
	mining/journal_change_set.cpp
	mining/journaling_block_assembler.cpp
    mining/legacy.cpp
	net.cpp
	net_processing.cpp
	noui.cpp
	orphan_txns.cpp
	policy/fees.cpp
	policy/policy.cpp
	pow.cpp
	rest.cpp
	rpc/abc.cpp
	rpc/blockchain.cpp
	rpc/mining.cpp
	rpc/mining-fbb.cpp
	rpc/misc.cpp
	rpc/net.cpp
	rpc/rawtransaction.cpp
	rpc/server.cpp
	script/scriptcache.cpp
	script/sigcache.cpp
	script/ismine.cpp
	timedata.cpp
    time_locked_mempool.cpp
	torcontrol.cpp
	txdb.cpp
	txmempool.cpp
    tx_mempool_info.cpp
	txn_double_spend_detector.cpp
	txn_propagator.cpp
	txn_validation_data.cpp
	txn_validator.cpp
	txn_recent_rejects.cpp
	ui_interface.cpp
	validation.cpp
	validationinterface.cpp
    vmtouch.cpp
)

# This require libevent
find_package(Event REQUIRED)

target_include_directories(server
	PRIVATE
		leveldb/helpers/memenv
		${EVENT_INCLUDE_DIR}
)

target_link_libraries(server
	${EVENT_LIBRARY}
	bitcoinconsensus
	leveldb
	memenv
)

if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	target_link_libraries(server ${EVENT_PTHREAD_LIBRARY})
else()
  # New mining API uses boost uuid library which requires bcrypt.lib on Windows
  find_library(BCRYPT_LIBRARY NAMES bcrypt)
  target_link_libraries(server ${BCRYPT_LIBRARY})
endif()

# Test suite.
add_subdirectory(test)

# Benchmark suite
if(BUILD_BITCOIN_BENCH)
	add_subdirectory(bench)
endif()

# Wallet
if(BUILD_BITCOIN_WALLET)
	add_subdirectory(wallet)
	target_link_libraries(server wallet)
endif()

# ZeroMQ
if(BUILD_BITCOIN_ZMQ)
	add_subdirectory(zmq)
	target_link_libraries(server zmq)
endif()

# RPC client support
add_library(rpcclient rpc/client.cpp)
target_link_libraries(rpcclient univalue util)

# bitcoin-seeder
if(BUILD_BITCOIN_SEEDER)
	add_subdirectory(seeder)
endif()

# bitcoin-cli
if(BUILD_BITCOIN_CLI)
	add_executable(bitcoin-cli bitcoin-cli.cpp)
	if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
		target_sources(bitcoin-cli PRIVATE bitcoin-cli-res.rc)
	endif()

	target_include_directories(bitcoin-cli PRIVATE ${EVENT_INCLUDE_DIR})
	target_link_libraries(bitcoin-cli common rpcclient ${EVENT_LIBRARY})
endif()

# bitcoin-tx
if(BUILD_BITCOIN_TX)
	add_executable(bitcoin-tx bitcoin-tx.cpp)
	if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
		target_sources(bitcoin-tx PRIVATE bitcoin-tx-res.rc)
	endif()

	target_link_libraries(bitcoin-tx bitcoinconsensus)
endif()

# bitcoin-miner
if(BUILD_BITCOIN_MINER)
	add_executable(bitcoin-miner bitcoin-miner.cpp)
	target_include_directories(bitcoin-miner PRIVATE ${EVENT_INCLUDE_DIR})
	target_link_libraries(bitcoin-miner common rpcclient bitcoinconsensus ${EVENT_LIBRARY})
endif()

# bitcoind
add_executable(bitcoind bitcoind.cpp)
target_link_libraries(
	bitcoind
	server
	$<$<PLATFORM_ID:Linux>:rt>) # use librt.so for aio on Linux
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	target_sources(bitcoind PRIVATE bitcoind-res.rc)
endif()
