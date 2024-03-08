#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""Helpful routines for regression testing."""

from base64 import b64encode
from binascii import hexlify, unhexlify
import configparser
from decimal import Decimal, ROUND_DOWN
import glob
import hashlib
import json
import logging
import os
import random
import re
from subprocess import CalledProcessError
import time

from . import coverage
from .authproxy import AuthServiceProxy, JSONRPCException
from .streams import BlockPriorityStreamPolicy, DefaultStreamPolicy

logger = logging.getLogger("TestFramework.utils")

# Assert functions
##################


def assert_fee_amount(fee, tx_size, fee_per_kB):
    """Assert the fee was in range"""
    target_fee = tx_size * fee_per_kB / 1000
    if fee < target_fee:
        raise AssertionError(
            "Fee of %s BTC too low! (Should be %s BTC)" % (str(fee), str(target_fee)))
    # allow the wallet's estimation to be at most 2 bytes off
    if fee > (tx_size + 2) * fee_per_kB / 1000:
        raise AssertionError(
            "Fee of %s BTC too high! (Should be %s BTC)" % (str(fee), str(target_fee)))


def assert_equal(thing1, thing2, *args):
    if thing1 != thing2 or any(thing1 != arg for arg in args):
        raise AssertionError("not(%s)" % " == ".join(str(arg)
                                                     for arg in (thing1, thing2) + args))


def assert_greater_than(thing1, thing2):
    if thing1 <= thing2:
        raise AssertionError("%s <= %s" % (str(thing1), str(thing2)))


def assert_greater_than_or_equal(thing1, thing2):
    if thing1 < thing2:
        raise AssertionError("%s < %s" % (str(thing1), str(thing2)))


def assert_raises(exc, fun, *args, **kwds):
    assert_raises_message(exc, None, fun, *args, **kwds)


def assert_raises_message(exc, message, fun, *args, **kwds):
    try:
        fun(*args, **kwds)
    except JSONRPCException:
        raise AssertionError(
            "Use assert_raises_rpc_error() to test RPC failures")
    except exc as e:
        if message is not None and message not in e.error['message']:
            raise AssertionError(
                "Expected substring not found:" + e.error['message'])
    except Exception as e:
        raise AssertionError(
            "Unexpected exception raised: " + type(e).__name__)
    else:
        raise AssertionError("No exception raised")


def assert_raises_process_error(returncode, output, fun, *args, **kwds):
    """Execute a process and asserts the process return code and output.

    Calls function `fun` with arguments `args` and `kwds`. Catches a CalledProcessError
    and verifies that the return code and output are as expected. Throws AssertionError if
    no CalledProcessError was raised or if the return code and output are not as expected.

    Args:
        returncode (int): the process return code.
        output (string): [a substring of] the process output.
        fun (function): the function to call. This should execute a process.
        args*: positional arguments for the function.
        kwds**: named arguments for the function.
    """
    try:
        fun(*args, **kwds)
    except CalledProcessError as e:
        if returncode != e.returncode:
            raise AssertionError("Unexpected returncode %i" % e.returncode)
        if output not in e.output:
            raise AssertionError("Expected substring not found:" + e.output)
    else:
        raise AssertionError("No exception raised")


def assert_raises_rpc_error(code, message, fun, *args, **kwds):
    """Run an RPC and verify that a specific JSONRPC exception code and message is raised.

    Calls function `fun` with arguments `args` and `kwds`. Catches a JSONRPCException
    and verifies that the error code and message are as expected. Throws AssertionError if
    no JSONRPCException was raised or if the error code/message are not as expected.

    Args:
        code (int), optional: the error code returned by the RPC call (defined
            in src/rpc/protocol.h). Set to None if checking the error code is not required.
        message (string), optional: [a substring of] the error string returned by the
            RPC call. Set to None if checking the error string is not required.
        fun (function): the function to call. This should be the name of an RPC.
        args*: positional arguments for the function.
        kwds**: named arguments for the function.
    """
    assert try_rpc(code, message, fun, *args, **kwds), "No exception raised"


def try_rpc(code, message, fun, *args, **kwds):
    """Tries to run an rpc command.

    Test against error code and message if the rpc fails.
    Returns whether a JSONRPCException was raised."""
    try:
        fun(*args, **kwds)
    except JSONRPCException as e:
        # JSONRPCException was thrown as expected. Check the code and message values are correct.
        if (code is not None) and (code != e.error["code"]):
            raise AssertionError(
                "Unexpected JSONRPC error code %i" % e.error["code"])
        if (message is not None) and (message not in e.error['message']):
            raise AssertionError(
                "Expected substring not found:" + e.error['message'])
        return True
    except Exception as e:
        raise AssertionError(
            "Unexpected exception raised: " + type(e).__name__)
    else:
        return False


def assert_is_hex_string(string):
    try:
        int(string, 16)
    except Exception as e:
        raise AssertionError(
            "Couldn't interpret %r as hexadecimal; raised: %s" % (string, e))


def assert_is_hash_string(string, length=64):
    if not isinstance(string, str):
        raise AssertionError("Expected a string, got type %r" % type(string))
    elif length and len(string) != length:
        raise AssertionError(
            "String of length %d expected; got %d" % (length, len(string)))
    elif not re.match('[abcdef0-9]+$', string):
        raise AssertionError(
            "String %r contains invalid characters for a hash." % string)


def assert_array_result(object_array, to_match, expected, should_not_find=False):
    """
        Pass in array of JSON objects, a dictionary with key/value pairs
        to match against, and another dictionary with expected key/value
        pairs.
        If the should_not_find flag is true, to_match should not be found
        in object_array
        """
    if should_not_find:
        assert_equal(expected, {})
    num_matched = 0
    for item in object_array:
        all_match = True
        for key, value in to_match.items():
            if item[key] != value:
                all_match = False
        if not all_match:
            continue
        elif should_not_find:
            num_matched = num_matched + 1
        for key, value in expected.items():
            if item[key] != value:
                raise AssertionError("%s : expected %s=%s" %
                                     (str(item), str(key), str(value)))
            num_matched = num_matched + 1
    if num_matched == 0 and not should_not_find:
        raise AssertionError("No objects matched %s" % (str(to_match)))
    if num_matched > 0 and should_not_find:
        raise AssertionError("Objects were found %s" % (str(to_match)))

# Utility functions
###################


def check_json_precision():
    """Make sure json library being used does not lose precision converting BTC values"""
    n = Decimal("20000000.00000003")
    satoshis = int(json.loads(json.dumps(float(n))) * 1.0e8)
    if satoshis != 2000000000000003:
        raise RuntimeError("JSON encode/decode loses precision")


def count_bytes(hex_string):
    return len(bytearray.fromhex(hex_string))


def bytes_to_hex_str(byte_str):
    return hexlify(byte_str).decode('ascii')


def hash256(byte_str):
    sha256 = hashlib.sha256()
    sha256.update(byte_str)
    sha256d = hashlib.sha256()
    sha256d.update(sha256.digest())
    return sha256d.digest()[::-1]


def hex_str_to_bytes(hex_str):
    return unhexlify(hex_str.encode('ascii'))


def str_to_b64str(string):
    return b64encode(string.encode('utf-8')).decode('ascii')


def satoshi_round(amount):
    return Decimal(amount).quantize(Decimal('0.00000001'), rounding=ROUND_DOWN)


def wait_until(predicate, *, attempts=float('inf'), timeout=float('inf'), lock=None, check_interval=0.05, label="wait_until"):
    if attempts == float('inf') and timeout == float('inf'):
        timeout = 60
    attempt = 0
    timestamp = timeout + time.time()

    while attempt < attempts and time.time() < timestamp:
        if lock:
            with lock:
                if predicate():
                    return
        else:
            if predicate():
                return
        attempt += 1
        time.sleep(check_interval)

    # Print the cause of the timeout
    assert attempts > attempt, f"{label} : max attempts exceeeded (attempts={attempt})"
    assert timestamp >= time.time(), f"{label} : timeout exceeded {timeout}"
    raise RuntimeError('Unreachable')

# RPC/P2P connection constants and functions
############################################


# The maximum number of nodes a single test can spawn
MAX_NODES = 8
# Don't assign rpc, p2p or zmq ports lower than this
PORT_MIN = 11000
# The number of ports to "reserve" for p2p, rpc and zmq, each
PORT_RANGE = 5000


class PortSeed:
    # Must be initialized with a unique integer for each process
    n = None


def get_rpc_proxy(url, node_number, timeout=None, coveragedir=None):
    """
    Args:
        url (str): URL of the RPC server to call
        node_number (int): the node number (or id) that this calls to

    Kwargs:
        timeout (int): HTTP timeout in seconds

    Returns:
        AuthServiceProxy. convenience object for making RPC calls.

    """
    proxy_kwargs = {}
    if timeout is not None:
        proxy_kwargs['timeout'] = timeout

    proxy = AuthServiceProxy(url, **proxy_kwargs)
    proxy.url = url  # store URL on proxy for info

    coverage_logfile = coverage.get_filename(
        coveragedir, node_number) if coveragedir else None

    return coverage.AuthServiceProxyWrapper(proxy, coverage_logfile)


def p2p_port(n):
    assert(n <= MAX_NODES)
    return PORT_MIN + n + (MAX_NODES * PortSeed.n) % (PORT_RANGE - 1 - MAX_NODES)


def rpc_port(n):
    return PORT_MIN + PORT_RANGE + n + (MAX_NODES * PortSeed.n) % (PORT_RANGE - 1 - MAX_NODES)


def zmq_port(n):
    assert(n <= MAX_NODES)
    return PORT_MIN + 2*PORT_RANGE + n + (MAX_NODES * PortSeed.n) % (PORT_RANGE - 1 - MAX_NODES)


def rpc_url(datadir, i, rpchost=None):
    rpc_u, rpc_p = get_auth_cookie(datadir)
    host = '127.0.0.1'
    port = rpc_port(i)
    if rpchost:
        parts = rpchost.split(':')
        if len(parts) == 2:
            host, port = parts
        else:
            host = rpchost
    return "http://%s:%s@%s:%d" % (rpc_u, rpc_p, host, int(port))

# Node functions
################


def initialize_datadir(dirname, n):
    datadir = os.path.join(dirname, "node" + str(n))
    if not os.path.isdir(datadir):
        os.makedirs(datadir)
    with open(os.path.join(datadir, "bitcoin.conf"), 'w', encoding='utf8') as f:
        f.write("regtest=1\n")
        f.write("port=" + str(p2p_port(n)) + "\n")
        f.write("rpcport=" + str(rpc_port(n)) + "\n")
        f.write("shrinkdebugfile=0\n")
    return datadir


def get_datadir_path(dirname, n):
    return os.path.join(dirname, "node" + str(n))


def get_auth_cookie(datadir):
    user = None
    password = None
    if os.path.isfile(os.path.join(datadir, "bitcoin.conf")):
        with open(os.path.join(datadir, "bitcoin.conf"), 'r', encoding='utf8') as f:
            for line in f:
                if line.startswith("rpcuser="):
                    assert user is None  # Ensure that there is only one rpcuser line
                    user = line.split("=")[1].strip("\n")
                if line.startswith("rpcpassword="):
                    assert password is None  # Ensure that there is only one rpcpassword line
                    password = line.split("=")[1].strip("\n")
    if os.path.isfile(os.path.join(datadir, "regtest", ".cookie")):
        try:
            with open(os.path.join(datadir, "regtest", ".cookie"), 'r') as f:
                userpass = f.read()
                split_userpass = userpass.split(':')
                user = split_userpass[0]
                password = split_userpass[1]
        except: pass # any failures while reading the cookie file are treated as if the file was not there
    if user is None or password is None:
        raise ValueError("No RPC credentials")
    return user, password


def log_filename(dirname, n_node, logname):
    return os.path.join(dirname, "node" + str(n_node), "regtest", logname)


def set_node_times(nodes, t):
    for node in nodes:
        node.setmocktime(t)


# Disconnects only the outbound connection "from_connection -> node_num"
# If nodes were connected with connect_nodes_bi (default setup_network) use disconnect_nodes_bi to completely split the nodes if needed
def disconnect_nodes(from_connection, node_num):
    subver = "testnode%d" % node_num
    for peer_id in [peer['id'] for peer in from_connection.getpeerinfo() if subver in peer['subver'] and not peer['inbound']]:
        from_connection.disconnectnode(nodeid=peer_id)

    for _ in range(50):
        if [peer['id'] for peer in from_connection.getpeerinfo() if subver in peer['subver'] and not peer['inbound']] == []:
            break
        time.sleep(0.1)
    else:
        raise AssertionError("timed out waiting for disconnect")


# Disconnects both outbound and inbound connections between nodes[node_a_index] and nodes[node_b_index]
# Inbound connection on one node is implicitly closed as a result of closing the outbound connection on the other node
def disconnect_nodes_bi(nodes, node_a_index, node_b_index):
    disconnect_nodes(nodes[node_a_index], node_b_index)
    disconnect_nodes(nodes[node_b_index], node_a_index)


# Returns False if node has -multistreams=0 set
def is_multistreams_enabled(node):
    for extra_arg in node.extra_args:
        if '-multistreams' in extra_arg:
            checkValueAt = extra_arg.find("=") + 1
            return int(extra_arg[checkValueAt:])
    return True


# Returns the number of additional stream policies used by connecting nodes.
def number_of_additional_streams(from_node, to_node):
    streampolicies_from_node = from_node.getnetworkinfo()["streampolicies"].split(",")
    streampolicies_to_node = to_node.getnetworkinfo()["streampolicies"].split(",")
    # Find the first policy both nodes have in common and return the number of additional streams used by the matched policy
    for policy_from_node in streampolicies_from_node:
        for policy_to_node in streampolicies_to_node:
            if policy_from_node == policy_to_node:
                additional_streams = []
                if policy_from_node == "BlockPriority":
                    additional_streams = BlockPriorityStreamPolicy().additional_streams
                elif policy_from_node == "Default":
                    additional_streams = DefaultStreamPolicy().additional_streams
                else:
                    raise AssertionError("Connecting test nodes are using an unexpected stream policy %s" % (policy_from_node))
                return len(additional_streams)
    return 0


# Connects two nodes and waits for connection to be established
# Set wait_multistreams to False to skip waiting for multistreams connection establishment
def connect_nodes(nodes, from_node_num, to_node_num, wait_multistreams=True):
    ip_port = "127.0.0.1:" + str(p2p_port(to_node_num))
    nodes[from_node_num].addnode(ip_port, "onetry")

    # check if both nodes use multistreams (True by default)
    multiStreamsEnabled = wait_multistreams and is_multistreams_enabled(nodes[from_node_num]) and is_multistreams_enabled(nodes[to_node_num])

    # Poll until version handshake complete to avoid race conditions with transaction relaying
    # Wait for fully established associations if multistreams is set on both nodes:
    # - only connections with 'testnode<to_node_num>' 'subver' are waited here
    # - 'associd' must not be 'Not-Set'
    # - 'streams' must contain predefined number of streams, depending on -multistreampolicies configuration parameter
    subver = "testnode%d" % to_node_num
    # Number of streams between nodes that will be established
    number_of_streams = number_of_additional_streams(nodes[from_node_num], nodes[to_node_num]) + 1
    wait_until(lambda: all(peer['version'] != 0 and (not multiStreamsEnabled or subver not in peer['subver'] or (peer['associd'] != 'Not-Set' and len(peer['streams']) == number_of_streams)) for peer in nodes[from_node_num].getpeerinfo()))


def connect_nodes_bi(nodes, a, b):
    connect_nodes(nodes, a, b)
    connect_nodes(nodes, b, a)


def connect_nodes_mesh(nodes, bi=False):
    for i in range(len(nodes)):
        for j in range(i + 1, len(nodes)):
            if bi:
                connect_nodes_bi(nodes, i, j)
            else:
                connect_nodes(nodes, i, j)


def sync_blocks(rpc_connections, *, wait=1, timeout=60):
    """
    Wait until everybody has the same tip.

    sync_blocks needs to be called with an rpc_connections set that has least
    one node already synced to the latest, stable tip, otherwise there's a
    chance it might return before all nodes are stably synced.
    """
    # Use getblockcount() instead of waitforblockheight() to determine the
    # initial max height because the two RPCs look at different internal global
    # variables (chainActive vs latestBlock) and the former gets updated
    # earlier.
    maxheight = max(x.getblockcount() for x in rpc_connections)
    start_time = cur_time = time.time()
    while cur_time <= start_time + timeout:
        tips = [r.waitforblockheight(maxheight, int(wait * 1000))
                for r in rpc_connections]
        if all(t["height"] == maxheight for t in tips):
            if all(t["hash"] == tips[0]["hash"] for t in tips):
                return
            raise AssertionError("Block sync failed, mismatched block hashes:{}".format(
                                 "".join("\n  {!r}".format(tip) for tip in tips)))
        cur_time = time.time()
    raise AssertionError("Block sync to height {} timed out:{}".format(
                         maxheight, "".join("\n  {!r}".format(tip) for tip in tips)))


def sync_chain(rpc_connections, *, wait=1, timeout=60):
    """
    Wait until everybody has the same best block
    """
    while timeout > 0:
        best_hash = [x.getbestblockhash() for x in rpc_connections]
        if best_hash == [best_hash[0]] * len(best_hash):
            return
        time.sleep(wait)
        timeout -= wait
    raise AssertionError("Chain sync failed: Best block hashes don't match")


def sync_mempools(rpc_connections, *, wait=1, timeout=60):
    """
    Wait until everybody has the same transactions in their memory
    pools
    """
    while timeout > 0:
        pool = set(rpc_connections[0].getrawmempool())
        non_final_pool = set(rpc_connections[0].getrawnonfinalmempool())
        num_match = 1
        for i in range(1, len(rpc_connections)):
            pool_match = set(rpc_connections[i].getrawmempool()) == pool
            non_final_pool_match = set(rpc_connections[i].getrawnonfinalmempool()) == non_final_pool
            if pool_match and non_final_pool_match:
                num_match = num_match + 1
        if num_match == len(rpc_connections):
            return
        time.sleep(wait)
        timeout -= wait
    raise AssertionError("Mempool sync failed")


def check_mempool_equals(rpc, should_be_in_mempool, timeout=20, check_interval=0.1):
    try:
        wait_until(lambda: set(rpc.getrawmempool()) == {t.hash for t in should_be_in_mempool},
                   timeout=timeout, check_interval=check_interval)
    except:
        mempool = set(rpc.getrawmempool())
        expected = {t.hash for t in should_be_in_mempool}
        missing = expected - mempool
        unexpected = mempool - expected
        if missing:
            rpc.log.info("Transactions missing from the mempool: " + str(list(missing)))
        if unexpected:
            rpc.log.info("Transactions that should not be in the mempool: " + str(list(unexpected)))
        raise


# The function checks if transaction/block was rejected
# The actual reject reason is checked if specified
def wait_for_reject_message(conn, reject_reason=None, timeout=5):
    wait_until(lambda: ('reject' in list(conn.cb.last_message.keys())
                        and (reject_reason == None or conn.cb.last_message['reject'].reason == reject_reason)),
               timeout=timeout)
    if conn.cb.last_message['reject'].message == b'tx':
        conn.rpc.log.info('Transaction rejected with ' + (conn.cb.last_message['reject'].reason).decode('utf8') + ' -- OK')
    else:
        conn.rpc.log.info('Block rejected with ' + (conn.cb.last_message['reject'].reason).decode('utf8') + ' -- OK')

    conn.cb.last_message.pop('reject', None)


# The function checks that transaction/block was not rejected
def ensure_no_rejection(conn):
    # wait 2 seconds for transaction/block before checking for reject message
    time.sleep(2)
    wait_until(lambda: not ('reject' in list(conn.cb.last_message.keys())) or conn.cb.last_message[
        'reject'].reason == None, timeout=5)
    conn.rpc.log.info('Not rejected -- OK')


# Transaction/Block functions
#############################


def find_output(node, txid, amount):
    """
    Return index to output of txid with value amount
    Raises exception if there is none.
    """
    txdata = node.getrawtransaction(txid, 1)
    for i in range(len(txdata["vout"])):
        if txdata["vout"][i]["value"] == amount:
            return i
    raise RuntimeError("find_output txid %s : %s not found" %
                       (txid, str(amount)))


def gather_inputs(from_node, amount_needed, confirmations_required=1):
    """
    Return a random set of unspent txouts that are enough to pay amount_needed
    """
    assert(confirmations_required >= 0)
    utxo = from_node.listunspent(confirmations_required)
    random.shuffle(utxo)
    inputs = []
    total_in = Decimal("0.00000000")
    while total_in < amount_needed and len(utxo) > 0:
        t = utxo.pop()
        total_in += t["amount"]
        inputs.append(
            {"txid": t["txid"], "vout": t["vout"], "address": t["address"]})
    if total_in < amount_needed:
        raise RuntimeError("Insufficient funds: need %d, have %d" %
                           (amount_needed, total_in))
    return (total_in, inputs)


def make_change(from_node, amount_in, amount_out, fee):
    """
    Create change output(s), return them
    """
    outputs = {}
    amount = amount_out + fee
    change = amount_in - amount
    if change > amount * 2:
        # Create an extra change output to break up big inputs
        change_address = from_node.getnewaddress()
        # Split change in two, being careful of rounding:
        outputs[change_address] = Decimal(
            change / 2).quantize(Decimal('0.00000001'), rounding=ROUND_DOWN)
        change = amount_in - amount - outputs[change_address]
    if change > 0:
        outputs[from_node.getnewaddress()] = change
    return outputs


def send_zeropri_transaction(from_node, to_node, amount, fee):
    """
    Create&broadcast a zero-priority transaction.
    Returns (txid, hex-encoded-txdata)
    Ensures transaction is zero-priority by first creating a send-to-self,
    then using its output
    """

    # Create a send-to-self with confirmed inputs:
    self_address = from_node.getnewaddress()
    (total_in, inputs) = gather_inputs(from_node, amount + fee * 2)
    outputs = make_change(from_node, total_in, amount + fee, fee)
    outputs[self_address] = float(amount + fee)

    self_rawtx = from_node.createrawtransaction(inputs, outputs)
    self_signresult = from_node.signrawtransaction(self_rawtx)
    self_txid = from_node.sendrawtransaction(self_signresult["hex"], True)

    vout = find_output(from_node, self_txid, amount + fee)
    # Now immediately spend the output to create a 1-input, 1-output
    # zero-priority transaction:
    inputs = [{"txid": self_txid, "vout": vout}]
    outputs = {to_node.getnewaddress(): float(amount)}

    rawtx = from_node.createrawtransaction(inputs, outputs)
    signresult = from_node.signrawtransaction(rawtx)
    txid = from_node.sendrawtransaction(signresult["hex"], True)

    return (txid, signresult["hex"])


def random_zeropri_transaction(nodes, amount, min_fee, fee_increment, fee_variants):
    """
    Create a random zero-priority transaction.
    Returns (txid, hex-encoded-transaction-data, fee)
    """
    from_node = random.choice(nodes)
    to_node = random.choice(nodes)
    fee = min_fee + fee_increment * random.randint(0, fee_variants)
    (txid, txhex) = send_zeropri_transaction(from_node, to_node, amount, fee)
    return (txid, txhex, fee)


def random_transaction(nodes, amount, min_fee, fee_increment, fee_variants):
    """
    Create a random transaction.
    Returns (txid, hex-encoded-transaction-data, fee)
    """
    from_node = random.choice(nodes)
    to_node = random.choice(nodes)
    fee = min_fee + fee_increment * random.randint(0, fee_variants)

    (total_in, inputs) = gather_inputs(from_node, amount + fee)
    outputs = make_change(from_node, total_in, amount, fee)
    outputs[to_node.getnewaddress()] = float(amount)

    rawtx = from_node.createrawtransaction(inputs, outputs)
    signresult = from_node.signrawtransaction(rawtx)
    txid = from_node.sendrawtransaction(signresult["hex"], True)

    return (txid, signresult["hex"], fee)


# Helper to create at least "count" utxos
# Pass in a fee that is sufficient for relay and mining new transactions.
def create_confirmed_utxos(fee, node, count, age=101, nodes=None):

    to_generate = int(0.5 * count) + age
    while to_generate > 0:
        node.generate(min(25, to_generate))
        to_generate -= 25

    if nodes is not None:
        sync_blocks(nodes)

    utxos = node.listunspent()
    iterations = count - len(utxos)
    addr1 = node.getnewaddress()
    addr2 = node.getnewaddress()
    if iterations <= 0:
        return utxos
    for i in range(iterations):
        t = utxos.pop()
        inputs = []
        inputs.append({"txid": t["txid"], "vout": t["vout"]})
        outputs = {}
        send_value = t['amount'] - fee
        outputs[addr1] = satoshi_round(send_value / 2)
        outputs[addr2] = satoshi_round(send_value / 2)
        raw_tx = node.createrawtransaction(inputs, outputs)
        signed_tx = node.signrawtransaction(raw_tx)["hex"]
        node.sendrawtransaction(signed_tx)

    while (node.getmempoolinfo()['size'] > 0):
        node.generate(1)

    # If running multiple nodes they have to be synced regularly to prevent simultaneous syncing of blocks and txs
    if nodes is not None:
        sync_blocks(nodes)

    utxos = node.listunspent()
    assert(len(utxos) >= count)
    return utxos


# Create large OP_RETURN txouts that can be appended to a transaction
# to make it large (helper for constructing large transactions).
def gen_return_txouts():
    # Some pre-processing to create a bunch of OP_RETURN txouts to insert into transactions we create
    # So we have big transactions (and therefore can't fit very many into each block)
    # create one script_pubkey
    script_pubkey = "6a4d0200"  # OP_RETURN OP_PUSH2 512 bytes
    for i in range(512):
        script_pubkey = script_pubkey + "01"
    # concatenate 128 txouts of above script_pubkey which we'll insert before
    # the txout for change
    txouts = "81"
    for k in range(128):
        # add txout value
        txouts = txouts + "0000000000000000"
        # add length of script_pubkey
        txouts = txouts + "fd0402"
        # add script_pubkey
        txouts = txouts + script_pubkey
    return txouts


def create_tx(node, coinbase, to_address, amount):
    inputs = [{"txid": coinbase, "vout": 0}]
    outputs = {to_address: amount}
    rawtx = node.createrawtransaction(inputs, outputs)
    signresult = node.signrawtransaction(rawtx)
    assert_equal(signresult["complete"], True)
    return signresult["hex"]


# Create a spend of each passed-in utxo, splicing in "txouts" to each raw
# transaction to make it large.  See gen_return_txouts() above.
def create_lots_of_big_transactions(node, txouts, utxos, num, fee):
    addr = node.getnewaddress()
    txids = []
    for _ in range(num):
        t = utxos.pop()
        inputs = [{"txid": t["txid"], "vout": t["vout"]}]
        outputs = {}
        change = t['amount'] - fee
        outputs[addr] = satoshi_round(change)
        rawtx = node.createrawtransaction(inputs, outputs)
        newtx = rawtx[0:92]
        newtx = newtx + txouts
        newtx = newtx + rawtx[94:]
        signresult = node.signrawtransaction(newtx, None, None, "NONE|FORKID")
        txid = node.sendrawtransaction(signresult["hex"], True)
        txids.append(txid)
    return txids


def mine_large_block(node, utxos=None):
    # generate a 66k transaction,
    # and 14 of them is close to the 1MB block limit
    num = 14
    txouts = gen_return_txouts()
    utxos = utxos if utxos is not None else []
    if len(utxos) < num:
        utxos.clear()
        utxos.extend(node.listunspent())

    # we pay a fee so we are not rejected by mempool rejection fee
    # Before we used the relayfee which does not exist any more
    # fee = 200 * node.getnetworkinfo()["relayfee"]
    fee = 200 * Decimal("0.00000250")
    create_lots_of_big_transactions(node, txouts, utxos, num, fee=fee)
    node.generate(1)


def get_srcdir(calling_script=None):
    """
    Try to find out the base folder containing the 'src' folder.
    If SRCDIR is set it does a sanity check and returns that.
    Otherwise it goes on a search and rescue mission.

    Returns None if it cannot find a suitable folder.

    TODO: This is only used for cdefs, consider moving that there.
    """
    def contains_src(path_to_check):
        if not path_to_check:
            return False
        else:
            cand_path = os.path.join(path_to_check, 'src')
            return os.path.exists(cand_path) and os.path.isdir(cand_path)

    srcdir = os.environ.get('SRCDIR', '')
    if contains_src(srcdir):
        return srcdir

    # If we have a caller, try to guess from its location where the
    # top level might be.
    if calling_script:
        caller_basedir = os.path.dirname(
            os.path.dirname(os.path.dirname(calling_script)))
        if caller_basedir != '' and contains_src(os.path.abspath(caller_basedir)):
            return os.path.abspath(caller_basedir)

    # Try to work it based out on main module
    # We might expect the caller to be rpc-tests.py or a test script
    # itself.
    import sys
    mainmod = sys.modules['__main__']
    mainmod_path = getattr(mainmod, '__file__', '')
    if mainmod_path and mainmod_path.endswith('.py'):
        maybe_top = os.path.dirname(
            os.path.dirname(os.path.dirname(mainmod_path)))
        if contains_src(os.path.abspath(maybe_top)):
            return os.path.abspath(maybe_top)

    # No luck, give up.
    return None


def loghash(inhash=None):
    if inhash:
        if len(inhash) > 12:
            return "{" + inhash[:6] + "...." + inhash[-6:] + "}"
        else:
            return inhash
    else:
        return inhash


def check_for_log_msg(rpc, log_msg, node_dir=None):
    """
    Checks for occurrence of the log_msg in the bitcoind.log
    rpc can be any object which has .log member (logger)
    If node_dir is None, the rpc must be an TestNode instance and the logfile to search will be the one associated with this TestNode instance.
    """
    assert hasattr(rpc, "log")

    if node_dir is None:
        assert hasattr(rpc, "datadir")
        logfile_path = os.path.join(rpc.datadir, "regtest", "bitcoind.log")
    else:
        logfile_path = glob.glob(rpc.options.tmpdir + node_dir + "/regtest/bitcoind.log")[0]

    with open(logfile_path) as f:
        for line in f:
            if log_msg in line:
                rpc.log.info("Found line: %s", line.strip())
                return True
    return False


def count_log_msg(rpc, log_msg, node_dir=None):
    """
    Checks for number of occurrences of the log_msg in the bitcoind.log
    rpc can be any object which has .log member (logger)
    If node_dir is None, the rpc must be an TestNode instance and the logfile to search will be the one associated with this TestNode instance.
    """
    assert hasattr(rpc, "log")

    if node_dir is None:
        assert hasattr(rpc, "datadir")
        logfile_path = os.path.join(rpc.datadir, "regtest", "bitcoind.log")
    else:
        logfile_path = glob.glob(rpc.options.tmpdir + node_dir + "/regtest/bitcoind.log")[0]

    count = 0
    with open(logfile_path) as f:
        for line in f:
            if log_msg in line:
                count += 1
    rpc.log.info(f'String "{log_msg}"" found in {count} lines')
    return count


def hashToHex(hash):
    return format(hash, '064x')


def check_zmq_test_requirements(configfile, skip_test_exception):
    # Check that bitcoin has been built with ZMQ enabled
    config = configparser.ConfigParser()
    if not configfile:
        if os.path.exists(os.path.dirname(__file__) + "/../../../build/test/config.ini"):
            configfile = os.path.dirname(__file__) + "/../../../build/test/config.ini"
        elif os.path.exists(os.path.dirname(__file__) + "/../../config.ini"):
            configfile = os.path.dirname(__file__) + "/../../config.ini"
        else:
            raise Exception("config.ini not found please provide path with --configfile <path to>/config.ini")
    config.read_file(open(configfile))

    if not config["components"].getboolean("ENABLE_ZMQ"):
        raise skip_test_exception

    # if we built bitcoind with ZMQ enabled, then we need zmq package to test its functionality
    try:
        import zmq
    except ImportError:
        raise Exception("pyzmq module not available.")


def wait_for_ptv_completion(conn, exp_mempool_size, check_interval=0.1, timeout=60):
    """
    The invocation of this function waits until the following conditions are met:
    a) there is an expected amount of transactions in the mempool
    b) there are no transactions in the ptv's queues, including: pending, being processed, detected orphans
    Both conditions ensure that the call won't finish too early, because of race conditions such as:
    - not sending all txs to the node through the connection
    """
    wait_until(lambda: conn.rpc.getmempoolinfo()['size'] >= exp_mempool_size,
               check_interval=check_interval, timeout=timeout)
    conn.rpc.waitforptvcompletion()


def wait_for_txn_propagator(node):
    # Wait for this node's transactions propagator to finish relaying transactions to other nodes.
    # Can be used to make sure current transactions are not relayed to nodes being reconnected later.
    wait_until(lambda: (node.getnetworkinfo()['txnpropagationqlen'] == 0))
