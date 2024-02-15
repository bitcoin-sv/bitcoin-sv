# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019-2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

cmake_minimum_required(VERSION 3.9)
project(BitcoinSV)

set(CMAKE_CXX_STANDARD 20)

# Default visibility is hidden on all targets.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

option(ENABLE_PROD_BUILD "Enable production build" OFF)
option(BUILD_BITCOIN_WALLET "Activate the wallet functionality" ON)
option(BUILD_BITCOIN_ZMQ "Activate the ZeroMQ functionalities" ON)
option(BUILD_BITCOIN_UPNP "Activate the UPnP functionality" OFF)

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

# Enable hardening
option(USE_HARDENING "Enable extra hardening" ON)
if(CMAKE_CXX_COMPILER_ID MATCHES GNU|Clang)
    if(USE_HARDENING)
        add_compile_options(
            -Wstack-protector
            -fstack-protector-all
            -fwrapv
        )
        add_link_options(
            "LINKER:-z,relro"
            "LINKER:-z,now"
            "LINKER:-z,noexecstack"
            "LINKER:-z,nodlopen"
            "LINKER:-pie"
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
	amount.h
	chainparamsbase.cpp
	chainparamsbase.h
	clientversion.cpp
	clientversion.h
	compat.h
	compat/byteswap.h
	compat/endian.h
	compat/glibc_sanity.cpp
	compat/glibcxx_sanity.cpp
	compat/strnlen.cpp
	compiler_warnings.h
	enum_cast.h
	fs.cpp
	fs.h
	logging.cpp
	logging.h
	prevector.h
	random.cpp
	random.h
	rpc/protocol.cpp
	rpc/protocol.h
	rpc/text_writer.cpp
	rpc/text_writer.h
	serialize.h
	support/cleanse.cpp
	support/cleanse.h
	support/lockedpool.cpp
	support/lockedpool.h
	sync.cpp
	sync.h
	threadinterrupt.cpp
	threadinterrupt.h
	threadpriority.cpp
	threadpriority.h
	threadsafety.h
	tinyformat.h
	uint256.h
	util.cpp
	util.h
	utilmoneystr.cpp
	utilmoneystr.h
	utilstrencodings.cpp
	utilstrencodings.h
	utiltime.cpp
	utiltime.h
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

find_package(Boost 1.74 REQUIRED ${BOOST_PACKAGES_REQUIRED})
target_link_libraries(util univalue crypto ${BOOST_LIBRARIES})

# Make sure boost uses std::atomic (it doesn't before 1.63)
target_compile_definitions(util PUBLIC BOOST_SP_USE_STD_ATOMIC BOOST_AC_USE_STD_ATOMIC)

# More completely unrelated features shared by all executables.
# Because nothing says this is different from util than "common"
add_library(common
	addrdb.h
	addrman.h
	amount.cpp
	arith_uint256.h
	base58.cpp
	base58.h
	big_int.h
	block_file_info.h
	block_index.h
	blockstreams.h
	blockvalidation.h
	bloom.h
	cfile_util.cpp
	cfile_util.h
	chain.h
	chainparams.cpp
	chainparams.h
	chainparamsseeds.h
	coins.cpp
	coins.h
	compressor.cpp
	compressor.h
	config.cpp
	config.h
	consensus/consensus.h
	consensus/merkle.cpp
	consensus/merkle.h
	consensus/params.h
	consensus/validation.h
	core_io.h
	core_memusage.h
	core_read.cpp
	core_write.cpp
	dirty_block_index_store.h
	disk_block_pos.h
	dstencode.cpp
	dstencode.h
	ecc_guard.h
	hash.h
	indirectmap.h
	invalid_txn_publisher.h
	key.cpp
	key.h
	keystore.cpp
	keystore.h
	limitedmap.h
	memusage.h
	merkleproof.h
	merkletree.h
	mining/assembler.h
	mining/candidates.h
	mining/factory.h
	mining/group_builder.h
	mining/journal.h
	mining/journal_builder.h
	mining/journal_change_set.h
	mining/journal_entry.h
	net/association.h
	net/association_id.h
	net/authconn.h
    net/block_parser.cpp
    net/block_parser.h
    net/blocktxn_parser.cpp
    net/blocktxn_parser.h
    net/cmpct_size.h
    net/cmpct_size.cpp
    net/cmpctblock_parser.cpp
    net/cmpctblock_parser.h
    net/fixed_len_multi_parser.h
    net/fixed_len_multi_parser.cpp
    net/fixed_len_parser.h
    net/fixed_len_parser.cpp
    net/msg_buffer.h
    net/msg_buffer.cpp
    net/msg_parser.h
    net/msg_parser_buffer.h
    net/msg_parser_buffer.cpp
    net/parser_utils.h
    net/single_seg_parser.h
    net/single_seg_parser.cpp
	net/net.h
	net/net_message.h
	net/net_types.h
	net/netaddress.cpp
	net/netaddress.h
	net/netbase.cpp
	net/netbase.h
	net/node_stats.h
    net/prefilled_tx_parser.h
    net/prefilled_tx_parser.cpp
    net/p2p_msg_lengths.h
    net/array_parser.h
	net/send_queue_bytes.h
	net/stream.h
	net/stream_policy.h
	net/stream_policy_factory.h
    net/tx_parser.h
    net/tx_parser.cpp
	orphan_txns.h
	policy/policy.h
	pow.h
	primitives/block.cpp
	primitives/block.h
	primitives/transaction.h
	protocol.cpp
	protocol.h
	pubkey.h
	reverselock.h
	rpc/jsonwriter.cpp
	rpc/jsonwriter.h
	scheduler.cpp
	scheduler.h
	script/int_serialization.h
	script/interpreter.h
	script/limitedstack.h
	script/opcodes.h
	script/script.h
	script/script_error.h
	script/script_flags.h
	script/script_num.h
	script/sighashtype.h
	script/sign.cpp
	script/sign.h
	script/standard.cpp
	script/standard.h
	script_config.h
	streams.h
	support/allocators/secure.h
	support/allocators/zeroafterfree.h
	task.h
	task_helpers.h
	taskcancellation.cpp
	taskcancellation.h
	thread_safe_queue.h
	threadpool.h
	threadpoolT.h
	time_locked_mempool.h
	timedata.h
	transaction_specific_config.cpp
	transaction_specific_config.h
	tx_mempool_info.h
	txhasher.cpp
	txhasher.h
	txmempool.h
	txn_double_spend_detector.h
    txn_grouper.h
	txn_sending_details.h
	txn_util.h
	txn_validation_config.h
	txn_validation_data.h
	txn_validation_result.h
    unique_array.h
    unique_array.cpp
	validation.h
	version.h
	versionbits.h
	warnings.cpp
	warnings.h
	write_preferring_upgradable_mutex.cpp
	write_preferring_upgradable_mutex.h
)

target_include_directories(common PRIVATE secp256k1/include)
target_link_libraries(common util secp256k1)

# libbitcoinconsensus
add_library(bitcoinconsensus
	arith_uint256.cpp
	big_int.cpp
	hash.cpp
	primitives/transaction.cpp
	pubkey.cpp
	script/bitcoinconsensus.cpp
	script/bitcoinconsensus.h
	script/instruction.h
	script/instruction_iterator.h
	script/interpreter.cpp
	script/limitedstack.cpp
	script/opcodes.cpp
	script/script.cpp
	script/script_error.cpp
	script/script_num.cpp
    taskcancellation.cpp
    taskcancellation.h
)

target_link_libraries(bitcoinconsensus common)
target_include_directories(bitcoinconsensus
	PRIVATE
		${OPENSSL_INCLUDE_DIR}
        secp256k1/include
)

# Bitcoin server facilities
add_library(server
	abort_node.cpp
	abort_node.h
	addrdb.cpp
	addrman.cpp
	async_file_reader.h
	block_file_access.cpp
	block_file_access.h
	block_file_info.cpp
	block_hasher.h
	block_index.cpp
	block_index_store.h
	block_index_store_loader.cpp
	block_index_store_loader.h
	blockencodings.cpp
	blockencodings.h
	blockfileinfostore.cpp
	blockfileinfostore.h
	blockindex_with_descendants.h
	bloom.cpp
	chain.cpp
	checkpoints.cpp
	checkpoints.h
	checkqueue.h
	checkqueuepool.h
	compat/sanity.h
	cuckoocache.h
	dbwrapper.cpp
	dbwrapper.h
	disk_block_index.h
	disk_tx_pos.h
	double_spend/dsattempt_handler.cpp
	double_spend/dsattempt_handler.h
	double_spend/dscallback_msg.cpp
	double_spend/dscallback_msg.h
	double_spend/dsdetected_defaults.h
	double_spend/dsdetected_message.cpp
	double_spend/dsdetected_message.h
	double_spend/dstxn_serialiser.cpp
	double_spend/dstxn_serialiser.h
	double_spend/time_limited_blacklist.h
	frozentxo.cpp
	frozentxo.h
	frozentxo_db.cpp
	frozentxo_db.h
	frozentxo_logging.cpp
	frozentxo_logging.h
	httprpc.cpp
	httprpc.h
	httpserver.cpp
	httpserver.h
	init.cpp
	init.h
	invalid_txn_publisher.cpp
	invalid_txn_sinks/file_sink.cpp
	invalid_txn_sinks/file_sink.h
	invalid_txn_sinks/zmq_sink.cpp
	invalid_txn_sinks/zmq_sink.h
	leaky_bucket.h
	locked_ref.h
	mempooltxdb.cpp
	mempooltxdb.h
	merkleblock.cpp
	merkleblock.h
	merkleproof.cpp
	merkletree.cpp
	merkletreedb.cpp
	merkletreedb.h
	merkletreestore.cpp
	merkletreestore.h
	metrics.h
	miner_id/coinbase_doc.cpp
	miner_id/coinbase_doc.h
    miner_id/dataref_index.cpp
    miner_id/dataref_index.h
    miner_id/dataref_index_detail.h
    miner_id/datareftx.cpp
    miner_id/datareftx.h
	miner_id/miner_id.cpp
	miner_id/miner_id.h
	miner_id/miner_info_tracker.cpp
	miner_id/miner_info_tracker.h
    miner_id/miner_id_db.cpp
    miner_id/miner_id_db.h
    miner_id/miner_id_db_defaults.h
	miner_id/miner_info.cpp
	miner_id/miner_info.h
    miner_id/miner_info_doc.cpp
    miner_id/miner_info_doc.h
    miner_id/miner_info_error.cpp
    miner_id/miner_info_error.h
    miner_id/miner_info_ref.cpp
    miner_id/miner_info_ref.h
    miner_id/revokemid.cpp
    miner_id/revokemid.h
	mining/assembler.cpp
	mining/candidates.cpp
	mining/factory.cpp
	mining/group_builder.cpp
	mining/journal.cpp
	mining/journal_builder.cpp
	mining/journal_change_set.cpp
	mining/journal_entry.cpp
	mining/journaling_block_assembler.cpp
	mining/journaling_block_assembler.h
	net/association.cpp
	net/association_id.cpp
	net/block_download_tracker.cpp
	net/block_download_tracker.h
	net/net.cpp
	net/net_message.cpp
	net/net_processing.cpp
	net/net_processing.h
	net/node_state.cpp
	net/node_state.h
	net/stream.cpp
	net/stream_policy.cpp
	net/stream_policy_factory.cpp
    net/validation_scheduler.cpp
    net/validation_scheduler.h
	netmessagemaker.h
	noui.cpp
	noui.h
	orphan_txns.cpp
	policy/fees.cpp
	policy/fees.h
	policy/policy.cpp
	pow.cpp
	processing_block_index.cpp
	processing_block_index.h
	rawtxvalidator.cpp
	rawtxvalidator.h
	rest.cpp
	rpc/abc.cpp
	rpc/blockchain.cpp
	rpc/blockchain.h
	rpc/frozentxo_rpc.cpp
	rpc/http_protocol.h
	rpc/mining-fbb.cpp
	rpc/mining.cpp
	rpc/mining.h
	rpc/miner_info.cpp
	rpc/misc.cpp
	rpc/misc.h
	rpc/net.cpp
	rpc/rawtransaction.cpp
	rpc/register.h
	rpc/safe_mode.cpp
	rpc/server.cpp
	rpc/server.h
	rpc/tojson.h
    rpc/webhook_client.cpp
    rpc/webhook_client.h
    rpc/webhook_client_defaults.h
	safe_mode.cpp
	safe_mode.h
	script/ismine.cpp
	script/ismine.h
	script/scriptcache.cpp
	script/scriptcache.h
	script/sigcache.cpp
	script/sigcache.h
	time_locked_mempool.cpp
	timedata.cpp
	tx_mempool_info.cpp
	txdb.cpp
	txdb.h
	txmempool.cpp
	txmempoolevictioncandidates.cpp
	txmempoolevictioncandidates.h
	txn_double_spend_detector.cpp
    txn_grouper.cpp
    txn_grouper.h
	txn_handlers.h
	txn_propagator.cpp
	txn_propagator.h
	txn_recent_rejects.cpp
	txn_recent_rejects.h
	txn_validation_data.cpp
	txn_validator.cpp
	txn_validator.h
	ui_interface.cpp
	ui_interface.h
	undo.h
	validation.cpp
	validationinterface.cpp
	validationinterface.h
	vmtouch.cpp
	vmtouch.h
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

# UPnP
if(BUILD_BITCOIN_UPNP)
    find_package(MiniUPnP REQUIRED)
    target_include_directories(server PRIVATE ${UPNP_INCLUDE_DIR})
    target_link_libraries(server ${UPNP_LIBRARY})
    add_compile_definitions(USE_UPNP=0)
endif()

# RPC client support
add_library(rpcclient
	cfile_util.cpp
	cfile_util.h
    net/netaddress.cpp
    net/netaddress.h
    net/netbase.cpp
    net/netbase.h
	rpc/client.cpp
	rpc/client.h
	rpc/client_config.cpp
	rpc/client_config.h
	rpc/client_utils.cpp
	rpc/client_utils.h
	rpc/http_request.cpp
	rpc/http_request.h
	rpc/http_response.cpp
	rpc/http_response.h
	support/events.h
)
target_link_libraries(rpcclient univalue util)

# bitcoin-cli
if(BUILD_BITCOIN_CLI)
	add_executable(bitcoin-cli bitcoin-cli.cpp
)
	if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
		target_sources(bitcoin-cli PRIVATE bitcoin-cli-res.rc)
	endif()

	target_include_directories(bitcoin-cli PRIVATE ${EVENT_INCLUDE_DIR})
	target_link_libraries(bitcoin-cli common rpcclient ${EVENT_LIBRARY})
endif()

# bitcoin-tx
if(BUILD_BITCOIN_TX)
	add_executable(bitcoin-tx bitcoin-tx.cpp
)
	if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
		target_sources(bitcoin-tx PRIVATE bitcoin-tx-res.rc)
	endif()

	target_link_libraries(bitcoin-tx bitcoinconsensus)
endif()

# bitcoin-miner
if(BUILD_BITCOIN_MINER)
	add_executable(bitcoin-miner bitcoin-miner.cpp
)
	target_include_directories(bitcoin-miner PRIVATE ${EVENT_INCLUDE_DIR})
	target_link_libraries(bitcoin-miner common rpcclient bitcoinconsensus ${EVENT_LIBRARY})
endif()

# bitcoind
add_executable(bitcoind bitcoind.cpp
)
target_link_libraries(
	bitcoind
	server
    rpcclient
	$<$<PLATFORM_ID:Linux>:rt>) # use librt.so for aio on Linux
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	target_sources(bitcoind PRIVATE bitcoind-res.rc)
endif()

include(ClangTidy)
AddClangTidy(bitcoind)
if(BUILD_BITCOIN_CLI)
    AddClangTidy(bitcoin-cli)
endif()

if (MSVC)
	# prevents default build from running unit tests automaticaly
	set_target_properties(check-bitcoin PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD TRUE)
	set_target_properties(check-secp256k1 PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD TRUE)
	set_target_properties(check-univalue PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD TRUE)
	set_target_properties(check-all PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD TRUE)
endif()
