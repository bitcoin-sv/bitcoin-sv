from test_framework.cdefs import REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER, LEGACY_MAX_BLOCK_SIZE, ONE_GIGABYTE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (assert_equal, assert_raises_rpc_error)


class BSV_RPC_MaxBlockSize_Test (BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.tip = None
        self.setup_clean_chain = True
        self.extra_args = [['-whitelist=127.0.0.1']]

    def get_block_max_size(self):
        info = self.nodes[0].getinfo()
        bs = info['maxminedblocksize']
        return bs

    def test_blockmaxsize(self):
        # Check that we start with DEFAULT_MAX_GENERATED_BLOCK_SIZE
        assert_equal(self.get_block_max_size(), REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER)

        self.nodes[0].setblockmaxsize(10)
        bs = self.get_block_max_size()
        assert_equal(bs, 10)

        self.nodes[0].setblockmaxsize(ONE_GIGABYTE)
        bs = self.get_block_max_size()
        assert_equal(bs, ONE_GIGABYTE)

    def run_test(self):
        self.genesis_hash = int(self.nodes[0].getbestblockhash(), 16)
        self.test_blockmaxsize()


if __name__ == '__main__':
    BSV_RPC_MaxBlockSize_Test().main()
