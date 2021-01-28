# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019-2020 Bitcoin Association
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

option(BUILD_BITCOIN_CLI "Build bitcoin-cli" ON)
option(BUILD_BITCOIN_TX "Build bitcoin-tx" ON)
option(BUILD_BITCOIN_MINER "Build bitcoin-miner" ON)
option(BUILD_BITCOIN_BENCH "Build benchmarks" ON)

# Ensure that WINDRES_PREPROC is enabled when using windres.
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	list(APPEND CMAKE_RC_FLAGS "-DWINDRES_PREPROC")
endif()

# Enable warning
option(EXTRA_WARNINGS "Enable extra warnings" OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES GNU|Clang)
	add_compile_options(
		-Wall
		-Wextra
		-Wformat
		-Wvla
		-Wformat-security
		-Wcast-align
		-Wunused-parameter
		$<$<COMPILE_LANGUAGE:C>:-Wnested-externs>
		$<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>
	)

	if(EXTRA_WARNINGS)
		add_compile_options(
			-Wshadow
			$<$<COMPILE_LANGUAGE:CXX>:-Wsuggest-override>
		)
	else()
		add_compile_options(
			-Wno-unused-parameter
		)
	endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES MSVC)
    if(NOT EXTRA_WARNINGS)
		add_compile_options(
			/wd4146
			/wd4244 # conversion from double to int
			/D_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING # deprecated usage of std::iterator class template
		)
	endif()
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
	amount.h                       # utilmoneystr.h
	chainparamsbase.cpp
	chainparamsbase.h              # chainparamsbase.cpp
	clientversion.cpp
	clientversion.h                # clientversion.cpp
	compat.h                       # util.h
	compat/byteswap.h              # compat/endian.h
	compat/endian.h                # serialize.h
	compat/glibc_sanity.cpp
	compat/glibcxx_sanity.cpp
	compat/strnlen.cpp
	compiler_warnings.h            # compat/glibcxx_sanity.cpp
	enum_cast.h                    # threadpriority.h
	fs.cpp
	fs.h                           # fs.cpp
	logging.cpp
	logging.h                      # logging.cpp
	prevector.h                    # serialize.h
	random.cpp
	random.h                       # random.cpp
	rpc/protocol.cpp
	rpc/protocol.h                 # rpc/protocol.cpp
	rpc/text_writer.h              # utilstrencodings.h
	serialize.h                    # util.cpp
	support/cleanse.cpp
	support/cleanse.h              # support/cleanse.cpp
	support/lockedpool.cpp
	support/lockedpool.h           # support/lockedpool.cpp
	sync.cpp
	sync.h                         # sync.cpp
	threadinterrupt.cpp
	threadinterrupt.h              # threadinterrupt.cpp
	threadpriority.cpp
	threadpriority.h               # threadpriority.cpp
	threadsafety.h                 # sync.h
	tinyformat.h                   # chainparamsbase.cpp
	uint256.h                      # random.h
	util.cpp
	util.h                         # util.cpp
	utilmoneystr.cpp
	utilmoneystr.h                 # utilmoneystr.cpp
	utilstrencodings.cpp
	utilstrencodings.h             # utilstrencodings.cpp
	utiltime.cpp
	utiltime.h                     # utiltime.cpp
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
	addrdb.h                       # net/net.h
	addrman.h                      # net/net.h
	amount.cpp
	arith_uint256.h                # key.cpp
	base58.cpp
	base58.h                       # base58.cpp
	big_int.h                      # script/script_num.h
	block_file_info.h              # chain.h
	blockstreams.h                 # validation.h
	blockvalidation.h              # validation.h
	bloom.h                        # net/net.h
	cfile_util.h                   # streams.h
	chain.h                        # validation.h
	chainparams.cpp
	chainparams.h                  # chainparams.cpp
	chainparamsseeds.h             # chainparams.cpp
	coins.cpp
	coins.h                        # coins.cpp
	compressor.cpp
	compressor.h                   # compressor.cpp
	config.cpp
	config.h                       # config.cpp
	consensus/consensus.h          # coins.cpp
	consensus/merkle.cpp
	consensus/merkle.h             # consensus/merkle.cpp
	consensus/params.h             # chainparams.h
	consensus/validation.h         # txn_validation_result.h
	core_io.h                      # core_read.cpp
	core_memusage.h                # coins.h
	core_read.cpp
	core_write.cpp
	disk_block_pos.h               # chain.h
	dstencode.cpp
	dstencode.h                    # dstencode.cpp
	hash.h                         # base58.cpp
	indirectmap.h                  # memusage.h
	invalid_txn_publisher.h        # config.h
	key.cpp
	key.h                          # key.cpp
	keystore.cpp
	keystore.h                     # keystore.cpp
	limitedmap.h                   # net/net.h
	memusage.h                     # coins.cpp
	merkletree.h                   # config.cpp
	mining/assembler.h             # mining/factory.h
	mining/candidates.h            # mining/factory.h
	mining/factory.h               # config.h
	mining/journal.h               # mining/journal_change_set.h
	mining/journal_builder.h       # txmempool.h
	mining/journal_change_set.h    # validation.h
	mining/journal_entry.h         # txmempool.h
	net/association.h              # net/net.h
	net/association_id.h           # net/association.h
	net/net.h                      # config.h
	net/net_message.h              # net/net.h
	net/net_types.h                # protocol.h
	net/netaddress.cpp
	net/netaddress.h               # net/netaddress.cpp
	net/netbase.cpp
	net/netbase.h                  # net/netbase.cpp
	net/node_stats.h               # net/net.h
	net/send_queue_bytes.h         # net/stream.h
	net/stream.h                   # net/association.h
	net/stream_policy.h            # net/stream_policy_factory.h
	net/stream_policy_factory.h    # net/net.h
	orphan_txns.h                  # config.h
	policy/policy.h                # chainparams.cpp
	pow.h                          # chain.h
	primitives/block.cpp
	primitives/block.h             # primitives/block.cpp
	primitives/transaction.h       # core_read.cpp
	protocol.cpp
	protocol.h                     # protocol.cpp
	pubkey.h                       # compressor.cpp
	reverselock.h                  # scheduler.cpp
	rpc/jsonwriter.cpp
	rpc/jsonwriter.h               # rpc/jsonwriter.cpp
	scheduler.cpp
	scheduler.h                    # scheduler.cpp
	script/int_serialization.h     # script/standard.cpp
	script/interpreter.h           # script/standard.h
	script/limitedstack.h          # script/interpreter.h
	script/opcodes.h               # script/script.h
	script/script.h                # core_read.cpp
	script/script_error.h          # validation.h
	script/script_flags.h          # script/interpreter.h
	script/script_num.h            # chainparams.cpp
	script/sighashtype.h           # script/sign.h
	script/sign.cpp
	script/sign.h                  # script/sign.cpp
	script/standard.cpp
	script/standard.h              # script/standard.cpp
	script_config.h                # config.h
	span.h                         # script/int_serialization.h
	streams.h                      # core_read.cpp
	support/allocators/secure.h    # key.h
	support/allocators/zeroafterfree.h # base58.h
	task.h                         # validation.h
	task_helpers.h                 # net/net.h
	taskcancellation.h             # script/sign.cpp
	thread_safe_queue.h            # invalid_txn_publisher.h
	threadpool.h                   # task_helpers.h
	threadpoolT.h                  # threadpool.h
	time_locked_mempool.h          # txmempool.h
	timedata.h                     # addrman.h
	tx_mempool_info.h              # txmempool.h
	txhasher.cpp
	txhasher.h                     # txhasher.cpp
	txmempool.h                    # txn_validation_result.h
	txn_double_spend_detector.h    # validation.h
	txn_sending_details.h          # net/net.h
	txn_util.h                     # txn_validation_data.h
	txn_validation_config.h        # config.h
	txn_validation_data.h          # txn_validation_result.h
	txn_validation_result.h        # validation.h
	validation.h                   # config.cpp
	version.h                      # core_read.cpp
	versionbits.h                  # validation.h
	warnings.cpp
	warnings.h                     # warnings.cpp
	write_preferring_upgradable_mutex.cpp
	write_preferring_upgradable_mutex.h # write_preferring_upgradable_mutex.cpp
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
	script/bitcoinconsensus.h      # script/bitcoinconsensus.cpp
	script/instruction.h           # script/instruction_iterator.h
	script/instruction_iterator.h  # script/script.cpp
	script/interpreter.cpp
	script/limitedstack.cpp
	script/opcodes.cpp
	script/script.cpp
	script/script_error.cpp
	script/script_num.cpp
)

target_link_libraries(bitcoinconsensus common)
target_include_directories(bitcoinconsensus
	PRIVATE
		${OPENSSL_INCLUDE_DIR}
)

# Bitcoin server facilities
add_library(server
	addrdb.cpp
	addrman.cpp
	async_file_reader.h            # validation.cpp
	block_file_access.cpp
	block_file_access.h            # block_file_access.cpp
	block_file_info.cpp
	blockencodings.cpp
	blockencodings.h               # blockencodings.cpp
	blockfileinfostore.cpp
	blockfileinfostore.h           # blockfileinfostore.cpp
	bloom.cpp
	chain.cpp
	checkpoints.cpp
	checkpoints.h                  # checkpoints.cpp
	checkqueue.h                   # checkqueuepool.h
	checkqueuepool.h               # validation.cpp
	compat/sanity.h                # init.cpp
	cuckoocache.h                  # script/scriptcache.cpp
	dbwrapper.cpp
	dbwrapper.h                    # dbwrapper.cpp
	disk_tx_pos.h                  # block_file_access.cpp
	httprpc.cpp
	httprpc.h                      # httprpc.cpp
	httpserver.cpp
	httpserver.h                   # httpserver.cpp
	init.cpp
	init.h                         # init.cpp
	invalid_txn_publisher.cpp
	invalid_txn_sinks/file_sink.cpp
	invalid_txn_sinks/file_sink.h  # invalid_txn_sinks/file_sink.cpp
	invalid_txn_sinks/zmq_sink.cpp
	invalid_txn_sinks/zmq_sink.h   # invalid_txn_sinks/zmq_sink.cpp
	locked_ref.h                   # net/net_processing.cpp
	mempooltxdb.cpp
	mempooltxdb.h                  # mempooltxdb.cpp
	merkleblock.cpp
	merkleblock.h                  # merkleblock.cpp
	merkletree.cpp
	merkletreedb.cpp
	merkletreedb.h                 # merkletreedb.cpp
	merkletreestore.cpp
	merkletreestore.h              # merkletreestore.cpp
	mining/assembler.cpp
	mining/candidates.cpp
	mining/factory.cpp
	mining/journal.cpp
	mining/journal_builder.cpp
	mining/journal_change_set.cpp
	mining/journal_entry.cpp
	mining/journaling_block_assembler.cpp
	mining/journaling_block_assembler.h # mining/journaling_block_assembler.cpp
	net/association.cpp
	net/association_id.cpp
	net/block_download_tracker.cpp
	net/block_download_tracker.h   # net/block_download_tracker.cpp
	net/net.cpp
	net/net_message.cpp
	net/net_processing.cpp
	net/net_processing.h           # net/net_processing.cpp
	net/node_state.cpp
	net/node_state.h               # net/node_state.cpp
	net/stream.cpp
	net/stream_policy.cpp
	net/stream_policy_factory.cpp
	netmessagemaker.h              # net/block_download_tracker.cpp
	noui.cpp
	noui.h                         # noui.cpp
	orphan_txns.cpp
	policy/fees.cpp
	policy/fees.h                  # policy/fees.cpp
	policy/policy.cpp
	pow.cpp
	rest.cpp
	rpc/abc.cpp
	rpc/blockchain.cpp
	rpc/blockchain.h               # rpc/blockchain.cpp
	rpc/http_protocol.h            # httprpc.cpp
	rpc/mining-fbb.cpp
	rpc/mining.cpp
	rpc/mining.h                   # rpc/mining.cpp
	rpc/misc.cpp
	rpc/misc.h                     # rpc/misc.cpp
	rpc/net.cpp
	rpc/rawtransaction.cpp
	rpc/register.h                 # init.cpp
	rpc/server.cpp
	rpc/server.h                   # rpc/server.cpp
	rpc/tojson.h                   # rest.cpp
	script/ismine.cpp
	script/ismine.h                # script/ismine.cpp
	script/scriptcache.cpp
	script/scriptcache.h           # script/scriptcache.cpp
	script/sigcache.cpp
	script/sigcache.h              # script/sigcache.cpp
	time_locked_mempool.cpp
	timedata.cpp
	tx_mempool_info.cpp
	txdb.cpp
	txdb.h                         # txdb.cpp
	txmempool.cpp
	txmempoolevictioncandidates.cpp
	txmempoolevictioncandidates.h  # txmempoolevictioncandidates.cpp
	txn_double_spend_detector.cpp
	txn_handlers.h                 # txn_validator.h
	txn_propagator.cpp
	txn_propagator.h               # txn_propagator.cpp
	txn_recent_rejects.cpp
	txn_recent_rejects.h           # txn_recent_rejects.cpp
	txn_validation_data.cpp
	txn_validator.cpp
	txn_validator.h                # txn_validator.cpp
	ui_interface.cpp
	ui_interface.h                 # ui_interface.cpp
	undo.h                         # block_file_access.cpp
	validation.cpp
	validationinterface.cpp
	validationinterface.h          # validationinterface.cpp
	vmtouch.cpp
	vmtouch.h                      # vmtouch.cpp
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
add_library(rpcclient
	rpc/client.cpp
	rpc/client.h                   # rpc/client.cpp
	rpc/client_config.cpp
	rpc/client_config.h            # rpc/client_config.cpp
	rpc/client_utils.cpp
	rpc/client_utils.h             # rpc/client_utils.cpp
	rpc/http_request.cpp
	rpc/http_request.h             # rpc/http_request.cpp
	rpc/http_response.cpp
	rpc/http_response.h            # rpc/http_response.cpp
	support/events.h               # rpc/client.cpp
)
target_link_libraries(rpcclient univalue util)

# bitcoin-cli
if(BUILD_BITCOIN_CLI)
	add_executable(bitcoin-cli
	bitcoin-cli.cpp
)
	if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
		target_sources(bitcoin-cli PRIVATE bitcoin-cli-res.rc)
	endif()

	target_include_directories(bitcoin-cli PRIVATE ${EVENT_INCLUDE_DIR})
	target_link_libraries(bitcoin-cli common rpcclient ${EVENT_LIBRARY})
endif()

# bitcoin-tx
if(BUILD_BITCOIN_TX)
	add_executable(bitcoin-tx
	bitcoin-tx.cpp
)
	if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
		target_sources(bitcoin-tx PRIVATE bitcoin-tx-res.rc)
	endif()

	target_link_libraries(bitcoin-tx bitcoinconsensus)
endif()

# bitcoin-miner
if(BUILD_BITCOIN_MINER)
	add_executable(bitcoin-miner
	bitcoin-miner.cpp
)
	target_include_directories(bitcoin-miner PRIVATE ${EVENT_INCLUDE_DIR})
	target_link_libraries(bitcoin-miner common rpcclient bitcoinconsensus ${EVENT_LIBRARY})
endif()

# bitcoind
add_executable(bitcoind
	bitcoind.cpp
)
target_link_libraries(
	bitcoind
	server
	$<$<PLATFORM_ID:Linux>:rt>) # use librt.so for aio on Linux
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	target_sources(bitcoind PRIVATE bitcoind-res.rc)
endif()

if (MSVC)
	# prevents default build from running unit tests automaticaly
	set_target_properties(check-bitcoin PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD TRUE)
	set_target_properties(check-leveldb PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD TRUE)
	set_target_properties(check-secp256k1 PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD TRUE)
	set_target_properties(check-univalue PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD TRUE)
	set_target_properties(check-all PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD TRUE)
endif()
