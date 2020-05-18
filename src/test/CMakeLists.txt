# Copyright (c) 2018 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

project(bitcoin-test)

# Process json files.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/data")

# Python is required to build C++ tests (and to execute functional tests)
find_package(PythonInterp 3 REQUIRED)


function(gen_json_header NAME)
	set(HEADERS "")
	foreach(f ${ARGN})
		set(h "${CMAKE_CURRENT_BINARY_DIR}/${f}.h")

		# Get the proper name for the test variable.
		get_filename_component(TEST_NAME ${f} NAME_WE)
		add_custom_command(OUTPUT ${h}
			COMMAND ${PYTHON_EXECUTABLE}
			ARGS
				"${CMAKE_CURRENT_SOURCE_DIR}/data/generate_header.py"
				"${TEST_NAME}"
				"${CMAKE_CURRENT_SOURCE_DIR}/${f}" > ${h}
			MAIN_DEPENDENCY ${f}
			DEPENDS
				"data/generate_header.py"
			VERBATIM
		)
		list(APPEND HEADERS ${h})
	endforeach(f)
	set(${NAME} "${HEADERS}" PARENT_SCOPE)
endfunction()

gen_json_header(JSON_HEADERS
	data/script_tests.json
	data/base58_keys_valid.json
	data/base58_encode_decode.json
	data/base58_keys_invalid.json
	data/tx_invalid.json
	data/tx_valid.json
	data/sighash.json
)

include(TestSuite)
create_test_suite(bitcoin)
add_dependencies(check check-bitcoin)

add_test_to_suite(bitcoin test_bitcoin
	alertnotify_tests.cpp
	arith_uint256_tests.cpp
	addrman_tests.cpp
	amount_tests.cpp
	allocator_tests.cpp
	base32_tests.cpp
	base58_tests.cpp
	base64_tests.cpp
    big_int_tests.cpp
	bip32_tests.cpp
	blockcheck_tests.cpp
	blockencodings_tests.cpp
	block_info_tests.cpp
	blockmaxsize_tests.cpp
	blockfile_reading_tests.cpp
	blockstatus_tests.cpp
	blockvalidationstatus_tests.cpp
	bloom_tests.cpp
    bn_op_tests.cpp
	bswap_tests.cpp
	checkpoints_tests.cpp
	checkqueue_tests.cpp
	coins_tests.cpp
	compress_tests.cpp
	config_tests.cpp
	core_io_tests.cpp
	crypto_tests.cpp
	cuckoocache_tests.cpp
	dbwrapper_tests.cpp
	DoS_tests.cpp
	dstencode_tests.cpp
	enum_cast_tests.cpp
	excessiveblock_tests.cpp
	getarg_tests.cpp
	hash_tests.cpp
	inv_tests.cpp
    int_serialization_tests.cpp
	journal_tests.cpp
	json_tests.cpp
	jsonutil.cpp
	key_tests.cpp
	limitedmap_tests.cpp
	limitedstack_tests.cpp
	locked_ref_tests.cpp
	m_candidates_tests.cpp
	main_tests.cpp
	mempool_tests.cpp
	merkle_tests.cpp
	miner_tests.cpp
	object_stream_deserialization_tests.cpp
	opcode_tests.cpp
	multisig_tests.cpp
	net_tests.cpp
	netbase_tests.cpp
	pmt_tests.cpp
	pow_tests.cpp
	prevector_tests.cpp
	protocol_tests.cpp
	raii_event_tests.cpp
	random_tests.cpp
	reverselock_tests.cpp
	rpc_tests.cpp
	sanity_tests.cpp
	scheduler_tests.cpp
	script_P2SH_tests.cpp
	script_tests.cpp
	scriptflags.cpp
	scriptnum_tests.cpp
	serialize_tests.cpp
	sighash_tests.cpp
	sighashtype_tests.cpp
	sigopcount_tests.cpp
	sigutil.cpp
	skiplist_tests.cpp
	streams_tests.cpp
	stream_serialization_tests.cpp
	string_writer_tests.cpp
	taskcancellation_tests.cpp
	test_bitcoin.cpp
	test_bitcoin_main.cpp
	test_double_spend_detector.cpp
	test_orphantxns.cpp
	test_recent_rejects.cpp
	test_txnvalidation_utils.cpp
	test_txnvalidator.cpp
	testutil.cpp
    threadpool_tests.cpp
	timedata_tests.cpp
    time_locked_mempool_tests.cpp
	ttor_tests.cpp
	transaction_tests.cpp
	txvalidationcache_tests.cpp
	uint256_tests.cpp
	undo_tests.cpp
	univalue_tests.cpp
	util_tests.cpp
	validation_tests.cpp

	# Tests generated from JSON
	${JSON_HEADERS}
)

target_include_directories(test_bitcoin
	PUBLIC
		# To access the generated json headers.
		${CMAKE_CURRENT_BINARY_DIR}
	PRIVATE
		${OPENSSL_INCLUDE_DIR}
)

find_package(Boost 1.58 REQUIRED unit_test_framework)

target_link_libraries(
	test_bitcoin
	Boost::unit_test_framework
	rpcclient
	server
	$<$<PLATFORM_ID:Linux>:rt>) # use librt.so for aio on Linux

# We need to detect if the BOOST_TEST_DYN_LINK flag is required.
set(CMAKE_REQUIRED_LIBRARIES Boost::unit_test_framework)
check_cxx_source_compiles("
	#define BOOST_TEST_DYN_LINK
	#define BOOST_TEST_MAIN
	#include <boost/test/unit_test.hpp>
" BOOST_TEST_DYN_LINK)

if(BOOST_TEST_DYN_LINK)
	target_compile_definitions(test_bitcoin PRIVATE BOOST_TEST_DYN_LINK)
endif(BOOST_TEST_DYN_LINK)

if(BUILD_BITCOIN_WALLET)
	target_sources(test_bitcoin
		PRIVATE
			../wallet/test/wallet_test_fixture.cpp
			../wallet/test/accounting_tests.cpp
			../wallet/test/wallet_tests.cpp
			../wallet/test/walletdb_tests.cpp
			../wallet/test/crypto_tests.cpp
	)
endif()
