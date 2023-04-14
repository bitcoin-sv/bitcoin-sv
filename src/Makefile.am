# Copyright (c) 2013-2016 The Bitcoin Core developers
# Copyright (c) 2018-2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

DIST_SUBDIRS = secp256k1 univalue

AM_LDFLAGS = $(PTHREAD_CFLAGS) $(LIBTOOL_LDFLAGS) $(HARDENED_LDFLAGS)
AM_CXXFLAGS = $(HARDENED_CXXFLAGS) $(ERROR_CXXFLAGS)
AM_CPPFLAGS = $(HARDENED_CPPFLAGS)
EXTRA_LIBRARIES =

if EMBEDDED_UNIVALUE
LIBUNIVALUE = univalue/libunivalue.la

$(LIBUNIVALUE): $(wildcard univalue/lib/*) $(wildcard univalue/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)
else
LIBUNIVALUE = $(UNIVALUE_LIBS)
endif

BITCOIN_INCLUDES=-I$(builddir) -I$(builddir)/obj $(BDB_CPPFLAGS) $(BOOST_CPPFLAGS) $(LEVELDB_CPPFLAGS) $(CRYPTO_CFLAGS) $(SSL_CFLAGS)

BITCOIN_INCLUDES += -I$(srcdir)/secp256k1/include
BITCOIN_INCLUDES += $(UNIVALUE_CFLAGS)

LIBBITCOIN_SERVER=libbitcoin_server.a
LIBBITCOIN_COMMON=libbitcoin_common.a
LIBBITCOIN_CONSENSUS=libbitcoin_consensus.a
LIBBITCOIN_CLI=libbitcoin_cli.a
LIBBITCOIN_UTIL=libbitcoin_util.a
LIBBITCOIN_CRYPTO=crypto/libbitcoin_crypto.a
LIBSECP256K1=secp256k1/libsecp256k1.la

if ENABLE_ZMQ
LIBBITCOIN_ZMQ=libbitcoin_zmq.a
endif
if BUILD_BITCOIN_LIBS
LIBBITCOINCONSENSUS=libbitcoinconsensus.la
endif
if ENABLE_WALLET
LIBBITCOIN_WALLET=libbitcoin_wallet.a
endif

$(LIBSECP256K1): $(wildcard secp256k1/src/*) $(wildcard secp256k1/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

# Make is not made aware of per-object dependencies to avoid limiting building parallelization
# But to build the less dependent modules first, we manually select their order here:
EXTRA_LIBRARIES += \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_CONSENSUS) \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_CLI) \
  $(LIBBITCOIN_WALLET) \
  $(LIBBITCOIN_ZMQ)

lib_LTLIBRARIES = $(LIBBITCOINCONSENSUS)

bin_PROGRAMS =
noinst_PROGRAMS =
TESTS =
BENCHMARKS =

if BUILD_BITCOIND
  bin_PROGRAMS += bitcoind
endif

if BUILD_BITCOIN_UTILS
  bin_PROGRAMS += bitcoin-cli bitcoin-tx bitcoin-miner
endif

.PHONY: FORCE check-symbols check-security
# bitcoin core #
BITCOIN_CORE_H = \
  abort_node.h \
  addrdb.h \
  addrman.h \
  async_file_reader.h \
  base58.h \
  bloom.h \
  block_hasher.h \
  block_index.h \
  block_index_store.h \
  block_index_store_loader.h \
  dirty_block_index_store.h \
  blockencodings.h \
  blockfileinfostore.h \
  block_file_access.h \
  block_file_info.h \
  blockindex_with_descendants.h \
  blockstreams.h \
  blockvalidation.h \
  cfile_util.h \
  chain.h \
  chainparams.h \
  chainparamsbase.h \
  chainparamsseeds.h \
  checkpoints.h \
  checkqueue.h \
  checkqueuepool.h \
  clientversion.h \
  coins.h \
  miner_id/coinbase_doc.h \
  compat.h \
  compat/byteswap.h \
  compat/endian.h \
  compat/sanity.h \
  compiler_warnings.h \
  compressor.h \
  config.h \
  consensus/consensus.h \
  core_io.h \
  core_memusage.h \
  cuckoocache.h \
  disk_block_index.h \
  disk_block_pos.h \
  disk_tx_pos.h \
  double_spend/dsattempt_handler.h \
  double_spend/dscallback_msg.h \
  double_spend/dsdetected_defaults.h \
  double_spend/dsdetected_message.h \
  double_spend/dstxn_serialiser.h \
  double_spend/time_limited_blacklist.h \
  dstencode.h \
  enum_cast.h \
  frozentxo.h \
  frozentxo_db.h \
  frozentxo_logging.h \
  fs.h \
  httprpc.h \
  httpserver.h \
  indirectmap.h \
  init.h \
  invalid_txn_publisher.h \
  invalid_txn_sinks/file_sink.h \
  invalid_txn_sinks/zmq_sink.h \
  key.h \
  keystore.h \
  dbwrapper.h \
  leaky_bucket.h \
  limitedmap.h \
  limited_cache.h \
  locked_ref.h \
  logging.h \
  memusage.h \
  mempooltxdb.h \
  merkleblock.h \
  merkleproof.h \
  merkletree.h \
  merkletreedb.h \
  merkletreestore.h \
  metrics.h \
  miner_id/dataref_index.h \
  miner_id/dataref_index_detail.h \
  miner_id/datareftx.h \
  miner_id/miner_id.h \
  miner_id/miner_id_db.h \
  miner_id/miner_id_db_defaults.h \
  miner_id/miner_info.h \
  miner_id/miner_info_doc.h \
  miner_id/miner_info_error.h \
  miner_id/miner_info_ref.h \
  miner_id/revokemid.h \
  miner_id/miner_info_tracker.h \
  mining/assembler.h \
  mining/candidates.h \
  mining/factory.h \
  mining/group_builder.h \
  mining/journal.h \
  mining/journal_builder.h \
  mining/journal_change_set.h \
  mining/journal_entry.h \
  mining/journaling_block_assembler.h \
  net/array_parser.h \
  net/association.h \
  net/association_id.h \
  net/authconn.h \
  net/block_download_tracker.h \
  net/block_parser.h \
  net/blocktxn_parser.h \
  net/cmpct_size.h \
  net/cmpctblock_parser.h \
  net/fixed_len_multi_parser.h \
  net/fixed_len_parser.h \
  net/msg_buffer.h \
  net/msg_parser.h \
  net/msg_parser_buffer.h \
  net/net.h \
  net/netaddress.h \
  net/netbase.h \
  net/net_message.h \
  net/net_processing.h \
  net/net_types.h \
  net/node_state.h \
  net/node_stats.h \
  net/parser_utils.h \
  net/prefilled_tx_parser.h \
  net/p2p_msg_lengths.h \
  net/send_queue_bytes.h \
  net/single_seg_parser.h \
  net/stream.h \
  net/stream_policy.h \
  net/stream_policy_factory.h \
  net/tx_parser.h \
  net/validation_scheduler.h \
  netmessagemaker.h \
  noui.h \
  orphan_txns.h \
  policy/fees.h \
  policy/policy.h \
  pow.h \
  processing_block_index.h \
  protocol.h \
  random.h \
  rawtxvalidator.h \
  reverselock.h \
  rpc/blockchain.h \
  rpc/client.h \
  rpc/client_config.h \
  rpc/client_utils.h \
  rpc/http_protocol.h \
  rpc/http_request.h \
  rpc/http_response.h \
  rpc/jsonwriter.h \
  rpc/mining.h \
  rpc/misc.h \
  rpc/protocol.h \
  rpc/server.h \
  rpc/tojson.h \
  rpc/register.h \
  rpc/text_writer.h \
  rpc/webhook_client.h \
  rpc/webhook_client_defaults.h \
  safe_mode.h \
  scheduler.h \
  script_config.h \
  script/scriptcache.h \
  script/sigcache.h \
  script/sign.h \
  script/standard.h \
  script/ismine.h \
  streams.h \
  support/allocators/secure.h \
  support/allocators/zeroafterfree.h \
  support/cleanse.h \
  support/events.h \
  support/lockedpool.h \
  sync.h \
  task.h \
  taskcancellation.h \
  task_helpers.h \
  thread_safe_queue.h \
  threadpool.h \
  threadpoolT.h \
  threadpriority.h \
  threadsafety.h \
  threadinterrupt.h \
  timedata.h \
  time_locked_mempool.h \
  transaction_specific_config.h \
  txdb.h \
  txhasher.h \
  txmempool.h \
  txmempoolevictioncandidates.h \
  tx_mempool_info.h \
  txn_double_spend_detector.h \
  txn_grouper.h \
  txn_handlers.h \
  txn_propagator.h \
  txn_recent_rejects.h \
  txn_sending_details.h \
  txn_util.h \
  txn_validation_config.h \
  txn_validation_data.h \
  txn_validation_result.h \
  txn_validator.h \
  ui_interface.h \
  undo.h \
  unique_array.h \
  util.h \
  utilmoneystr.h \
  utiltime.h \
  validation.h \
  validationinterface.h \
  versionbits.h \
  vmtouch.h \
  wallet/coincontrol.h \
  wallet/crypter.h \
  wallet/db.h \
  wallet/finaltx.h \
  wallet/rpcdump.h \
  wallet/rpcwallet.h \
  wallet/wallet.h \
  wallet/walletdb.h \
  warnings.h \
  write_preferring_upgradable_mutex.h \
  zmq/zmq_publisher.h \
  zmq/zmqabstractnotifier.h \
  zmq/zmqconfig.h\
  zmq/zmqnotificationinterface.h \
  zmq/zmqpublishnotifier.h


obj/build.h: FORCE
	@$(MKDIR_P) "$(builddir)/obj"
	@$(top_srcdir)/share/genbuild.sh "$(abs_top_builddir)/src/obj/build.h" \
	  "$(abs_top_srcdir)"
libbitcoin_util_a-clientversion.$(OBJEXT): obj/build.h

# server:
libbitcoin_server_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(MINIUPNPC_CPPFLAGS) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
libbitcoin_server_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_server_a_SOURCES = \
  abort_node.cpp \
  addrman.cpp \
  addrdb.cpp \
  bloom.cpp \
  block_index.cpp \
  blockencodings.cpp \
  block_file_info.cpp \
  blockfileinfostore.cpp \
  block_file_access.cpp \
  block_index_store_loader.cpp \
  chain.cpp \
  checkpoints.cpp \
  miner_id/coinbase_doc.cpp \
  frozentxo.cpp \
  frozentxo_db.cpp \
  frozentxo_logging.cpp \
  httprpc.cpp \
  httpserver.cpp \
  init.cpp \
  invalid_txn_publisher.cpp \
  invalid_txn_sinks/file_sink.cpp \
  invalid_txn_sinks/zmq_sink.cpp \
  dbwrapper.cpp \
  double_spend/dsattempt_handler.cpp \
  double_spend/dscallback_msg.cpp \
  double_spend/dsdetected_message.cpp \
  double_spend/dstxn_serialiser.cpp \
  mempooltxdb.cpp \
  merkleblock.cpp \
  merkleproof.cpp \
  merkletree.cpp \
  merkletreedb.cpp \
  merkletreestore.cpp \
  miner_id/dataref_index.cpp \
  miner_id/datareftx.cpp \
  miner_id/miner_id.cpp \
  miner_id/miner_id_db.cpp \
  miner_id/miner_info.cpp \
  miner_id/miner_info_doc.cpp \
  miner_id/miner_info_error.cpp \
  miner_id/miner_info_ref.cpp \
  miner_id/revokemid.cpp \
  miner_id/miner_info_tracker.cpp \
  mining/assembler.cpp \
  mining/candidates.cpp \
  mining/factory.cpp \
  mining/group_builder.cpp \
  mining/journal.cpp \
  mining/journal_builder.cpp \
  mining/journal_change_set.cpp \
  mining/journal_entry.cpp \
  mining/journaling_block_assembler.cpp \
  net/association.cpp \
  net/association_id.cpp \
  net/block_download_tracker.cpp \
  net/block_parser.cpp \
  net/blocktxn_parser.cpp \
  net/cmpct_size.cpp \
  net/cmpctblock_parser.cpp \
  net/fixed_len_multi_parser.cpp \
  net/fixed_len_parser.cpp \
  net/msg_buffer.cpp \
  net/msg_parser_buffer.cpp \
  net/net.cpp \
  net/net_message.cpp \
  net/net_processing.cpp \
  net/node_state.cpp \
  net/prefilled_tx_parser.cpp \
  net/single_seg_parser.cpp \
  net/stream.cpp \
  net/stream_policy.cpp \
  net/stream_policy_factory.cpp \
  net/tx_parser.cpp \
  net/validation_scheduler.cpp \
  noui.cpp \
  orphan_txns.cpp \
  policy/fees.cpp \
  policy/policy.cpp \
  pow.cpp \
  processing_block_index.cpp \
  rawtxvalidator.cpp \
  rest.cpp \
  rpc/abc.cpp \
  rpc/blockchain.cpp \
  rpc/frozentxo_rpc.cpp \
  rpc/mining.cpp \
  rpc/miner_info.cpp \
  rpc/mining-fbb.cpp \
  rpc/misc.cpp \
  rpc/net.cpp \
  rpc/rawtransaction.cpp \
  rpc/safe_mode.cpp \
  rpc/server.cpp \
  rpc/webhook_client.cpp \
  safe_mode.cpp \
  script/scriptcache.cpp \
  script/sigcache.cpp \
  script/ismine.cpp \
  taskcancellation.cpp \
  timedata.cpp \
  time_locked_mempool.cpp \
  txdb.cpp \
  txmempool.cpp \
  txmempoolevictioncandidates.cpp \
  tx_mempool_info.cpp \
  txn_double_spend_detector.cpp \
  txn_grouper.cpp \
  txn_propagator.cpp \
  txn_validation_data.cpp \
  txn_recent_rejects.cpp \
  txn_validator.cpp \
  ui_interface.cpp \
  unique_array.cpp \
  validation.cpp \
  validationinterface.cpp \
  vmtouch.cpp \
  $(BITCOIN_CORE_H)

if ENABLE_ZMQ
libbitcoin_zmq_a_CPPFLAGS = $(BITCOIN_INCLUDES) $(ZMQ_CFLAGS)
libbitcoin_zmq_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_zmq_a_SOURCES = \
  zmq/zmq_publisher.cpp \
  zmq/zmqabstractnotifier.cpp \
  zmq/zmqnotificationinterface.cpp \
  zmq/zmqpublishnotifier.cpp
endif


# wallet: only linked when wallet enabled
libbitcoin_wallet_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_wallet_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_wallet_a_SOURCES = \
  wallet/crypter.cpp \
  wallet/db.cpp \
  wallet/finaltx.cpp \
  wallet/rpcdump.cpp \
  wallet/rpcwallet.cpp \
  wallet/wallet.cpp \
  wallet/walletdb.cpp \
  $(BITCOIN_CORE_H)

# crypto primitives library
crypto_libbitcoin_crypto_a_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libbitcoin_crypto_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libbitcoin_crypto_a_SOURCES = \
  crypto/aes.cpp \
  crypto/aes.h \
  crypto/chacha20.h \
  crypto/chacha20.cpp \
  crypto/common.h \
  crypto/hmac_sha256.cpp \
  crypto/hmac_sha256.h \
  crypto/hmac_sha512.cpp \
  crypto/hmac_sha512.h \
  crypto/ripemd160.cpp \
  crypto/ripemd160.h \
  crypto/sha1.cpp \
  crypto/sha1.h \
  crypto/sha256.cpp \
  crypto/sha256.h \
  crypto/sha512.cpp \
  crypto/sha512.h

if USE_ASM
crypto_libbitcoin_crypto_a_SOURCES += crypto/sha256_sse4.cpp
endif

# consensus: shared between all executables that validate any consensus rules.
libbitcoin_consensus_a_CPPFLAGS = $(AM_CPPFLAGS) $(OPENSSL_INCLUDES) $(OPENSSL_LDFLAGS) $(OPENSSL_LIBS) $(BITCOIN_INCLUDES)
libbitcoin_consensus_a_CXXFLAGS = $(AM_CXXFLAGS) $(OPENSSL_INCLUDES) $(OPENSSL_LDFLAGS) $(OPENSSL_LIBS) $(PIE_FLAGS)
libbitcoin_consensus_a_SOURCES = \
  amount.h \
  arith_uint256.cpp \
  arith_uint256.h \
  big_int.cpp \
  big_int.h \
  cfile_util.cpp \
  cfile_util.h \
  consensus/merkle.cpp \
  consensus/merkle.h \
  consensus/params.h \
  consensus/validation.h \
  ecc_guard.h \
  hash.cpp \
  hash.h \
  prevector.h \
  primitives/block.cpp \
  primitives/block.h \
  primitives/transaction.cpp \
  primitives/transaction.h \
  pubkey.cpp \
  pubkey.h \
  script/bitcoinconsensus.cpp \
  script/sighashtype.h \
  script/instruction.h \
  script/instruction_iterator.h \
  script/interpreter.cpp \
  script/interpreter.h \
  script/int_serialization.h \
  script/limitedstack.cpp \
  script/limitedstack.h \
  script/opcodes.cpp \
  script/opcodes.h \
  script/script_flags.h \
  script/script.cpp \
  script/script.h \
  script/script_num.cpp \
  script/script_num.h \
  script/script_error.cpp \
  script/script_error.h \
  serialize.h \
  taskcancellation.h \
  taskcancellation.cpp \
  tinyformat.h \
  uint256.h \
  utilstrencodings.cpp \
  utilstrencodings.h \
  version.h

# common: shared between bitcoind and non-server tools
libbitcoin_common_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_common_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_common_a_SOURCES = \
  amount.cpp \
  base58.cpp \
  chainparams.cpp \
  config.cpp \
  coins.cpp \
  compressor.cpp \
  dstencode.cpp \
  core_read.cpp \
  core_write.cpp \
  key.cpp \
  keystore.cpp \
  net/netaddress.cpp \
  net/netbase.cpp \
  protocol.cpp \
  rpc/jsonwriter.cpp \
  scheduler.cpp \
  script/sign.cpp \
  script/standard.cpp \
  transaction_specific_config.cpp \
  txhasher.cpp \
  warnings.cpp \
  write_preferring_upgradable_mutex.cpp \
  $(BITCOIN_CORE_H)

# util: shared between all executables.
# This library *must* be included to make sure that the glibc
# backward-compatibility objects and their sanity checks are linked.
libbitcoin_util_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_util_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_util_a_SOURCES = \
  support/lockedpool.cpp \
  chainparamsbase.cpp \
  clientversion.cpp \
  compat/glibc_sanity.cpp \
  compat/glibcxx_sanity.cpp \
  compat/strnlen.cpp \
  fs.cpp \
  logging.cpp \
  random.cpp \
  rpc/protocol.cpp \
  rpc/text_writer.cpp \
  support/cleanse.cpp \
  sync.cpp \
  threadinterrupt.cpp \
  threadpriority.cpp \
  uint256.h \
  util.cpp \
  utilmoneystr.cpp \
  utilstrencodings.cpp \
  utiltime.cpp \
  $(BITCOIN_CORE_H)

if GLIBC_BACK_COMPAT
libbitcoin_util_a_SOURCES += compat/glibc_compat.cpp
AM_LDFLAGS += $(COMPAT_LDFLAGS)
endif

# cli:
libbitcoin_cli_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_cli_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_cli_a_SOURCES = \
  cfile_util.cpp \
  net/netaddress.cpp \
  net/netbase.cpp \
  rpc/client.cpp \
  rpc/client_config.cpp \
  rpc/client_utils.cpp \
  rpc/http_request.cpp \
  rpc/http_response.cpp \
  $(BITCOIN_CORE_H)

# bitcoind binary #
bitcoind_SOURCES = bitcoind.cpp
bitcoind_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
bitcoind_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
bitcoind_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
bitcoind_SOURCES += bitcoind-res.rc
endif

bitcoind_LDADD = \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_CLI) \
  $(LIBBITCOIN_COMMON) \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_WALLET) \
  $(LIBBITCOIN_ZMQ) \
  $(LIBBITCOIN_CONSENSUS) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBLEVELDB) \
  $(LIBCRC32C) \
  $(LIBMEMENV) \
  $(LIBSECP256K1)

bitcoind_LDADD += \
  $(BOOST_LIBS) \
  $(BDB_LIBS) \
  $(OPENSSL_LIBS) \
  $(MINIUPNPC_LIBS) \
  $(EVENT_PTHREADS_LIBS) \
  $(EVENT_LIBS) \
  $(ZMQ_LIBS) \
  $(AIO_LIBS) \
  $(TBB_LIBS)

# bitcoin-cli binary #
bitcoin_cli_SOURCES = bitcoin-cli.cpp
bitcoin_cli_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(EVENT_CFLAGS)
bitcoin_cli_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
bitcoin_cli_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
bitcoin_cli_SOURCES += bitcoin-cli-res.rc
endif

bitcoin_cli_LDADD = \
  $(LIBBITCOIN_CLI) \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_CRYPTO)

bitcoin_cli_LDADD += $(BOOST_LIBS) $(OPENSSL_LIBS) $(EVENT_LIBS)
#

# bitcoin-tx binary #
bitcoin_tx_SOURCES = bitcoin-tx.cpp
bitcoin_tx_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
bitcoin_tx_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
bitcoin_tx_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
bitcoin_tx_SOURCES += bitcoin-tx-res.rc
endif

bitcoin_tx_LDADD = \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_CONSENSUS) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBSECP256K1)

bitcoin_tx_LDADD += $(BOOST_LIBS) $(OPENSSL_LIBS)
#

# bitcoin-miner binary #
bitcoin_miner_SOURCES = bitcoin-miner.cpp \
arith_uint256.cpp \
arith_uint256.h \
uint256.h \
crypto/sha512.cpp \
crypto/sha512.h \
crypto/chacha20.cpp \
crypto/chacha20.h
 
bitcoin_miner_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(EVENT_CFLAGS)
bitcoin_miner_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
bitcoin_miner_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)
 
bitcoin_miner_LDADD = \
  $(LIBBITCOIN_CLI) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_UTIL)
 
bitcoin_miner_LDADD += $(BOOST_LIBS) $(OPENSSL_LIBS) $(EVENT_LIBS)
#

# bitcoinconsensus library #
if BUILD_BITCOIN_LIBS
include_HEADERS = script/bitcoinconsensus.h
libbitcoinconsensus_la_SOURCES = $(crypto_libbitcoin_crypto_a_SOURCES) $(libbitcoin_consensus_a_SOURCES)

if GLIBC_BACK_COMPAT
  libbitcoinconsensus_la_SOURCES += compat/glibc_compat.cpp
endif

libbitcoinconsensus_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
libbitcoinconsensus_la_LIBADD = $(LIBSECP256K1)
libbitcoinconsensus_la_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(OPENSSL_INCLUDES) $(OPENSSL_LDFLAGS) $(OPENSSL_LIBS) -I$(builddir)/obj -I$(srcdir)/secp256k1/include -DBUILD_BITCOIN_INTERNAL
libbitcoinconsensus_la_CXXFLAGS = $(AM_CXXFLAGS) $(BITCOIN_INCLUDES) $(OPENSSL_INCLUDES) $(OPENSSL_LDFLAGS) $(OPENSSL_LIBS) $(PIE_FLAGS)

endif
#

CTAES_DIST =  crypto/ctaes/bench.c
CTAES_DIST += crypto/ctaes/ctaes.c
CTAES_DIST += crypto/ctaes/ctaes.h
CTAES_DIST += crypto/ctaes/README.md
CTAES_DIST += crypto/ctaes/test.c

CLEANFILES = $(EXTRA_LIBRARIES)

CLEANFILES += *.gcda *.gcno
CLEANFILES += compat/*.gcda compat/*.gcno
CLEANFILES += consensus/*.gcda consensus/*.gcno
CLEANFILES += crypto/*.gcda crypto/*.gcno
CLEANFILES += policy/*.gcda policy/*.gcno
CLEANFILES += primitives/*.gcda primitives/*.gcno
CLEANFILES += rpc/*.gcda rpc/*.gcno
CLEANFILES += script/*.gcda script/*.gcno
CLEANFILES += support/*.gcda support/*.gcno
CLEANFILES += univalue/*.gcda univalue/*.gcno
CLEANFILES += wallet/*.gcda wallet/*.gcno
CLEANFILES += wallet/test/*.gcda wallet/test/*.gcno
CLEANFILES += zmq/*.gcda zmq/*.gcno

DISTCLEANFILES = obj/build.h

EXTRA_DIST = $(CTAES_DIST)

clean-local:
	-$(MAKE) -C secp256k1 clean
	-$(MAKE) -C univalue clean
	-rm -f leveldb/*/*.gcda leveldb/*/*.gcno leveldb/helpers/memenv/*.gcda leveldb/helpers/memenv/*.gcno
	-rm -rf test/__pycache__

.rc.o:
	@test -f $(WINDRES)
	## FIXME: How to get the appropriate modulename_CPPFLAGS in here?
	$(AM_V_GEN) $(WINDRES) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(CPPFLAGS) -DWINDRES_PREPROC -i $< -o $@

.mm.o:
	$(AM_V_CXX) $(OBJCXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(AM_CXXFLAGS) $(PIE_FLAGS) $(CXXFLAGS) -c -o $@ $<

check-symbols: $(bin_PROGRAMS)
if GLIBC_BACK_COMPAT
	@echo "Checking glibc back compat..."
	$(AM_V_at) READELF=$(READELF) CPPFILT=$(CPPFILT) $(top_srcdir)/contrib/devtools/symbol-check.py $(bin_PROGRAMS)
endif

check-security: $(bin_PROGRAMS)
if HARDEN
	@echo "Checking binary security..."
	$(AM_V_at) READELF=$(READELF) OBJDUMP=$(OBJDUMP) $(top_srcdir)/contrib/devtools/security-check.py $(bin_PROGRAMS)
endif

%.pb.cc %.pb.h: %.proto
	@test -f $(PROTOC)
	$(AM_V_GEN) $(PROTOC) --cpp_out=$(@D) --proto_path=$(<D) $<

if EMBEDDED_LEVELDB
include Makefile.crc32c.include
include Makefile.leveldb.include
endif

if ENABLE_TESTS
include Makefile.test.include
endif

if ENABLE_BENCH
include Makefile.bench.include
endif
