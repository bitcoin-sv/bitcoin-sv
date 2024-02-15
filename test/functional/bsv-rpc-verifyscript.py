#!/usr/bin/env python3
# Copyright (c) 2021  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test RPC function verifyscript
"""

from test_framework.blocktools import create_block, create_coinbase, create_transaction, create_tx
from test_framework.key import CECKey
from test_framework.mininode import CTransaction, CTxIn, COutPoint, CTxOut, ToHex, COIN
from test_framework.script import CScript, hash160, SignatureHashForkId, OP_CHECKSIG, OP_DROP, OP_DUP, OP_EQUALVERIFY, OP_FALSE, OP_HASH160, OP_MUL, OP_TRUE, SIGHASH_ALL, SIGHASH_FORKID
from test_framework.util import assert_equal, assert_raises_rpc_error, bytes_to_hex_str
from test_framework.test_framework import BitcoinTestFramework


# Create and submit block with anyone-can-spend coinbase transaction.
# Returns coinbase transaction.
def make_coinbase(conn_rpc):
    tip = conn_rpc.getblock(conn_rpc.getbestblockhash())
    coinbase_tx = create_coinbase(tip["height"] + 1)
    block = create_block(int(tip["hash"], 16), coinbase_tx, tip["time"] + 1)
    block.solve()
    conn_rpc.submitblock(ToHex(block))
    return coinbase_tx


class BSV_RPC_verifyscript (BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        # genesis height is specified so that we can check if script verification flags are properly set
        self.genesisactivationheight = 103
        self.extra_args = [["-genesisactivationheight=%d" % self.genesisactivationheight]]

        # Private key used in scripts with CHECKSIG
        self.prvkey = CECKey()
        self.prvkey.set_secretbytes(b"horsebattery")
        self.pubkey = self.prvkey.get_pubkey()

    # Return a transaction that spends given anyone-can-spend coinbase and provides
    # several outputs that can be used in test to verify scripts
    def create_test_tx(self, coinbase_tx):
        assert_equal(coinbase_tx.vout[0].nValue, 50*COIN) # we expect 50 coins

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(coinbase_tx.sha256, 0), b"", 0xffffffff))

        # Simple anyone-can-spend output
        tx.vout.append(CTxOut(int(1*COIN), CScript([OP_TRUE])))

        # Output using standard P2PKH script
        tx.vout.append(CTxOut(int(1*COIN), CScript([OP_DUP, OP_HASH160, hash160(self.pubkey), OP_EQUALVERIFY, OP_CHECKSIG])))

        # Another simple anyone-can-spend output
        tx.vout.append(CTxOut(int(1*COIN), CScript([OP_TRUE])))

        # Final output provides remaining coins and is not needed by test
        tx.vout.append(CTxOut(int(47*COIN), CScript([OP_FALSE])))

        tx.rehash()
        return tx

    # Sign input 0 in tx spending n-th output from spend_tx using self.prvkey
    def sign_tx(self, tx, spend_tx, n):
        sighash = SignatureHashForkId(spend_tx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spend_tx.vout[n].nValue)
        tx.vin[0].scriptSig = CScript([self.prvkey.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID])), self.pubkey])

    def verifyscript_check(self, node, expected_result, scripts, *args):
        N = len(scripts)
        assert_equal(len(expected_result), N)
        result = node.verifyscript(scripts, *args)
        assert_equal(len(result), N)
        for i in range(N):
            if result[i]["result"] != expected_result[i]:
                raise AssertionError("Unexpected script verification result "+str(i)+"! Expected '"+expected_result[i]+"' got " +str(result[i]))
        return result

    def verifyscript_check_ok(self, node, scripts, *args):
        return self.verifyscript_check(node, len(scripts)*["ok"], scripts, *args)

    def verifyscript_check_error(self, node, scripts, *args):
        return self.verifyscript_check(node, len(scripts)*["error"], scripts, *args)

    def run_test(self):
        node = self.nodes[0]

        # Create spendable coinbase transaction
        coinbase_tx = make_coinbase(node)
        node.generate(99)

        # Create, send and mine test transaction
        tx_test = self.create_test_tx(coinbase_tx)
        node.sendrawtransaction(ToHex(tx_test), False, True) # disable fee check
        assert_equal(node.getrawmempool(), [tx_test.hash])
        node.generate(1)
        assert_equal(node.getrawmempool(), [])

        tip_hash = node.getbestblockhash()

        #
        # Check parameter parsing
        #
        tx0 = create_tx(tx_test, 0, 1*COIN)
        assert_raises_rpc_error(-8, "Missing required argument", node.verifyscript) # 1st parameter scripts is required and must be JSON array of objects
        assert_raises_rpc_error(-1, None, node.verifyscript, "abc")
        assert_raises_rpc_error(-1, None, node.verifyscript, 123)
        assert_raises_rpc_error(-1, None, node.verifyscript, True)
        assert_raises_rpc_error(-1, None, node.verifyscript, {})
        assert_raises_rpc_error(-1, None, node.verifyscript, ["abc"])
        assert_raises_rpc_error(-1, None, node.verifyscript, [123])
        assert_raises_rpc_error(-1, None, node.verifyscript, [True])

        assert_raises_rpc_error(-1, None, node.verifyscript, [{"tx": ToHex(tx0), "n": 0}], "abc") # 2nd parameter stopOnFirstInvalid is boolean
        assert_raises_rpc_error(-1, None, node.verifyscript, [{"tx": ToHex(tx0), "n": 0}], 0)
        assert_raises_rpc_error(-1, None, node.verifyscript, [{"tx": ToHex(tx0), "n": 0}], [])
        assert_raises_rpc_error(-1, None, node.verifyscript, [{"tx": ToHex(tx0), "n": 0}], {})

        assert_raises_rpc_error(-8, "Invalid value for totalTimeout", node.verifyscript, [{"tx": ToHex(tx0), "n": 0}], True, -1) # 3rd parameter totalTimeout is non-negative integer
        assert_raises_rpc_error(-1, None, node.verifyscript, [{"tx": ToHex(tx0), "n": 0}], True, "abc")
        assert_raises_rpc_error(-1, None, node.verifyscript, [{"tx": ToHex(tx0), "n": 0}], True, True)
        assert_raises_rpc_error(-1, None, node.verifyscript, [{"tx": ToHex(tx0), "n": 0}], True, [])
        assert_raises_rpc_error(-1, None, node.verifyscript, [{"tx": ToHex(tx0), "n": 0}], True, {})

        assert_raises_rpc_error(-8, "Too many arguments", node.verifyscript, [{"tx": ToHex(tx0), "n": 0}], True, 100, "abc") # max 3 arguments

        #
        # Check scripts parameter parsing
        #
        assert_raises_rpc_error(-8, "Missing", node.verifyscript, [{}]) # tx and n fields are required
        assert_raises_rpc_error(-8, "Missing scripts[0].n", node.verifyscript, [{"tx": ToHex(tx0)}])
        assert_raises_rpc_error(-8, "Missing scripts[0].tx", node.verifyscript, [{"n": 0}])
        assert_raises_rpc_error(-8, "Missing scripts[1].n", node.verifyscript, [{"tx": ToHex(tx0), "n": 0}, {"tx": ToHex(tx0)}])
        assert_raises_rpc_error(-8, "Missing scripts[1].tx", node.verifyscript, [{"tx": ToHex(tx0), "n": 0}, {"n": 0}])

        assert_raises_rpc_error(-22, "TX decode failed for scripts[0].tx", node.verifyscript, [{"tx": "", "n": 0}]) # tx must be a hex string of a transaction
        assert_raises_rpc_error(-22, "TX decode failed for scripts[0].tx", node.verifyscript, [{"tx": "01abc", "n": 0}])
        assert_raises_rpc_error(-22, "TX decode failed for scripts[0].tx", node.verifyscript, [{"tx": "00", "n": 0}])
        assert_raises_rpc_error(-8, "Invalid value for n in scripts[0]", node.verifyscript, [{"tx": ToHex(tx0), "n": -1}]) # n must be non-negative integer
        assert_raises_rpc_error(-8, "Both flags and prevblockhash specified in scripts[0]", node.verifyscript, [{"tx": ToHex(tx0), "n": 0, "flags": 0, "prevblockhash": tip_hash}]) # both flags and prevblockhash are not allowed
        assert_raises_rpc_error(-8, "Unknown block", node.verifyscript, [{"tx": ToHex(tx0), "n": 0, "prevblockhash": "0000000000000000000000000000000000000000000000000000000000000000"}]) # invalid block hash

        assert_raises_rpc_error(-3, None, node.verifyscript, [{"tx": ToHex(tx0), "n": 0, "txo": 0}]) # txo must be JSON object with three fields
        assert_raises_rpc_error(-3, None, node.verifyscript, [{"tx": ToHex(tx0), "n": 0, "txo": "abc"}])
        assert_raises_rpc_error(-3, None, node.verifyscript, [{"tx": ToHex(tx0), "n": 0, "txo": True}])
        assert_raises_rpc_error(-8, "Missing scripts[0].txo.lock", node.verifyscript, [{"tx": ToHex(tx0), "n": 0, "txo": {"value": 1, "height": 0}}])
        assert_raises_rpc_error(-8, "Missing scripts[0].txo.value", node.verifyscript, [{"tx": ToHex(tx0), "n": 0, "txo": {"lock": "00", "height": 0}}])
        assert_raises_rpc_error(-8, "Missing scripts[0].txo.height", node.verifyscript, [{"tx": ToHex(tx0), "n": 0, "txo": {"lock": "00", "value": 1}}])
        assert_raises_rpc_error(-8, "must be hexadecimal string", node.verifyscript, [{"tx": ToHex(tx0), "n": 0, "txo": {"lock": "01abc", "value": 1, "height": 0}}]) # lock must be hexstring
        self.verifyscript_check_ok(node, [{"tx": ToHex(create_transaction(tx_test, 0, CScript([OP_TRUE]), 1*COIN)), "n": 0, "txo": {"lock": "", "value": 1*COIN, "height": 0}}]) # empty lock script is valid
        assert_raises_rpc_error(-8, "Invalid value for scripts[0].txo.value", node.verifyscript, [{"tx": ToHex(tx0), "n": 0, "txo": {"lock": "00", "value": -1, "height": 0}}]) # value must be non-negative integer
        assert_raises_rpc_error(-8, "Invalid value for scripts[0].txo.height", node.verifyscript, [{"tx": ToHex(tx0), "n": 0, "txo": {"lock": "00", "value": 1, "height": -2}}]) # height must be non-negative integer or -1

        assert_raises_rpc_error(-8, "Unable to find TXO spent by transaction scripts[0].tx", node.verifyscript, [{"tx": ToHex(create_tx(tx0, 0, 1*COIN)), "n": 0}]) # Check that non-existent coin is detected

        #
        # Check verification of a valid P2PKH script
        #
        tx1 = create_tx(tx_test, 1, 1*COIN)
        self.sign_tx(tx1, tx_test, 1)
        expected_flags = 81931 # this is the expected value for automatically determined script verification flags
        res = self.verifyscript_check_ok(node, [
            # Automatically find TXO and block
            {
                "tx": ToHex(tx1),
                "n": 0,
                "reportflags": True # report actual flags used by script verification
            },
            # Explicitly provide TXO and block
            {
                "tx": ToHex(tx1),
                "n": 0,
                "reportflags": True,
                "prevblockhash": tip_hash,
                "txo": {
                    "lock": bytes_to_hex_str(tx_test.vout[0].scriptPubKey),
                    "value": tx_test.vout[0].nValue,
                    "height": node.getblockcount()
                }
            },
            # Explicitly provide script verification flags
            {
                "tx": ToHex(tx1),
                "n": 0,
                "flags": expected_flags,
                "reportflags": True,
                "txo": {
                    "lock": bytes_to_hex_str(tx_test.vout[0].scriptPubKey),
                    "value": tx_test.vout[0].nValue
                }
            },
            # Explicitly provide script verification flags and automatically determine TXO flags
            {
                "tx": ToHex(tx1),
                "n": 0,
                "flags": expected_flags ^ (1 << 19), # mess up value of SCRIPT_UTXO_AFTER_GENESIS flag that is always set from TXO
                "reportflags": True,
                "txo": {
                    "lock": bytes_to_hex_str(tx_test.vout[0].scriptPubKey),
                    "value": tx_test.vout[0].nValue,
                    "height": node.getblockcount()
                }
            },
            # Once more without reporting flags
            {
                "tx": ToHex(tx1),
                "n": 0
            }
        ])

        # Check that automatically determined script flags are as expected
        assert_equal(res[0]["flags"], expected_flags)
        assert_equal(res[1]["flags"], expected_flags)
        assert_equal(res[2]["flags"], expected_flags)
        assert_equal(res[3]["flags"], expected_flags)
        assert("flags" not in res[4])

        # Changing the output value must make the script invalid
        tx2 = create_tx(tx_test, 1, 1*COIN)
        self.sign_tx(tx2, tx_test, 1)
        tx2.vout[0].nValue = int(0.9*COIN)
        self.verifyscript_check_error(node, [
            {
                "tx": ToHex(tx2),
                "n": 0
            }
        ])

        #
        # Check working of stopOnFirstInvalid
        #
        self.verifyscript_check(node, ["error", "ok"], [{"tx": ToHex(tx2), "n": 0}, {"tx": ToHex(tx1), "n": 0}])
        self.verifyscript_check(node, ["error", "ok"], [{"tx": ToHex(tx2), "n": 0}, {"tx": ToHex(tx1), "n": 0}], False)  # default for stopOnFirstInvalid is False
        self.verifyscript_check(node, ["error", "skipped"], [{"tx": ToHex(tx2), "n": 0}, {"tx": ToHex(tx1), "n": 0}], True)

        #
        # Check that TXO is also found in mempool
        #
        tx3 = create_tx(tx_test, 0, 1*COIN)
        node.sendrawtransaction(ToHex(tx3), False, True)
        assert_equal(node.getrawmempool(), [tx3.hash])
        tx4 = create_tx(tx3, 0, 1*COIN)
        self.verifyscript_check_ok(node, [{"tx": ToHex(tx4), "n": 0}])

        #
        # Check that genesis related script flags are selected after some height
        #

        # Generating one more block should place us one block below genesis activation
        # but mempool should be already be at genesis height.
        node.generate(1)
        assert_equal(node.getblockcount(), self.genesisactivationheight-1)

        # Flags should now also include SCRIPT_GENESIS and SCRIPT_VERIFY_SIGPUSHONLY
        # but not SCRIPT_UTXO_AFTER_GENESIS, because TXO is still before genesis.
        res = self.verifyscript_check_ok(node, [{"tx": ToHex(tx4), "n": 0, "reportflags": True}])
        assert_equal(res[0]["flags"], expected_flags + 262144 + 32)

        # Send this transaction so that we have a spendable coin created after genesis
        node.sendrawtransaction(ToHex(tx4), False, True)
        assert_equal(node.getrawmempool(), [tx4.hash])
        node.generate(1)
        assert_equal(node.getrawmempool(), [])
        assert_equal(node.getblockcount(), self.genesisactivationheight) # tip should now be at genesis height

        # Transaction spending coin that was created after genesis
        tx5 = create_tx(tx4, 0, 1*COIN)

        # Now flags should (besides SCRIPT_GENESIS and SCRIPT_VERIFY_SIGPUSHONLY) also
        # include SCRIPT_UTXO_AFTER_GENESIS, because TXO is also after genesis.
        res = self.verifyscript_check_ok(node, [{"tx": ToHex(tx5), "n": 0, "reportflags": True}])
        assert_equal(res[0]["flags"], expected_flags + 524288 + 262144 + 32)

        #
        # Check timeout detection
        #
        self.verifyscript_check(node, ["skipped", "skipped"], [{"tx": ToHex(tx1), "n": 0}, {"tx": ToHex(tx1), "n": 0}], True, 0)  # everything must be skipped if timeout is 0
        self.verifyscript_check(node, ["skipped", "skipped"], [{"tx": ToHex(tx1), "n": 0}, {"tx": ToHex(tx1), "n": 0}], False, 0)

        # Restart the node to allow unlimited script size, large numbers and larger than default but still too low value for maxstdtxvalidationduration needed to completely verify a complex script.
        self.restart_node(0, self.extra_args[0] + ["-maxstdtxvalidationduration=100", "-maxnonstdtxvalidationduration=101", "-maxscriptsizepolicy=0", "-maxscriptnumlengthpolicy=250000"])

        # Create, send and mine transaction with large anyone-can-spend lock script
        tx6 = create_tx(tx_test, 2, 1*COIN)
        tx6.vout[0] = CTxOut(int(1*COIN), CScript([bytearray([42] * 250000), bytearray([42] * 200 * 1000), OP_MUL, OP_DROP, OP_TRUE]))
        tx6.rehash()
        node.sendrawtransaction(ToHex(tx6), False, True)
        assert_equal(node.getrawmempool(), [tx6.hash])
        node.generate(1)
        assert_equal(node.getrawmempool(), [])

        # This transaction should take more than 100ms and less than 2000ms to verify
        # NOTE: If verification takes more or less time than this, some of the checks below will fail.
        #       This can, for example, happen on a very fast, very slow or busy machine.
        tx7 = create_tx(tx6, 0, 1*COIN)

        # First tx is small and should be successfully verified.
        # Second tx is big and its verification should timeout.
        # Verification of third tx should be skipped even if stopOnFirstInvalid is false because maximum allowed total verification time was already exceeded.
        self.verifyscript_check(node, ["ok", "timeout", "skipped"], [{"tx": ToHex(tx1), "n": 0}, {"tx": ToHex(tx7), "n": 0}, {"tx": ToHex(tx1), "n": 0}], False, 100)

        # If we allow enough time, verification of second tx should still timeout because of maxstdtxvalidationduration.
        self.verifyscript_check(node, ["ok", "timeout", "ok"], [{"tx": ToHex(tx1), "n": 0}, {"tx": ToHex(tx7), "n": 0}, {"tx": ToHex(tx1), "n": 0}], False, 2000)

        # Restart the node with larger value for maxstdtxvalidationduration so that its
        # value does not limit maximum execution time of single script.
        self.restart_node(0, self.extra_args[0] + ["-maxstdtxvalidationduration=2000", "-maxnonstdtxvalidationduration=2001", "-maxscriptsizepolicy=0", "-maxscriptnumlengthpolicy=250000"])

        # Verification of all three scripts should now succeed if total timeout is large enough ...
        self.verifyscript_check(node, ["ok", "ok", "ok"], [{"tx": ToHex(tx1), "n": 0}, {"tx": ToHex(tx7), "n": 0}, {"tx": ToHex(tx1), "n": 0}], False, 2000)

        # ... and timeout as before if it is not
        self.verifyscript_check(node, ["ok", "timeout", "skipped"], [{"tx": ToHex(tx1), "n": 0}, {"tx": ToHex(tx7), "n": 0}, {"tx": ToHex(tx1), "n": 0}], False, 100)


if __name__ == '__main__':
    BSV_RPC_verifyscript().main()
