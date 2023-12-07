#!/usr/bin/env python3
# Copyright (c) 2020  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.associations import AssociationCB
from test_framework.mininode import msg_ping
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, wait_until
from test_framework.streams import StreamType, BlockPriorityStreamPolicy, DefaultStreamPolicy

# Test functionality within the test framework for simply creating P2P associations to nodes


# Subclass association callback type to get notifications we are interested in
class MyAssociationCB(AssociationCB):
    def __init__(self):
        super().__init__()
        self.pong_count = 0
        self.pong_stream = StreamType.UNKNOWN

    # Count number of received pongs
    def on_pong(self, stream, message):
        super().on_pong(stream, message)
        self.pong_count += 1
        self.pong_stream = stream.stream_type


class P2PSimpleAssociation(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1

    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):
        # Setup 2 associations; one with BlockPriority policy and one with Default policy
        associations_stream_policies = [BlockPriorityStreamPolicy(), DefaultStreamPolicy()]
        with self.run_node_with_associations("Simple Association Setup", 0, [], associations_stream_policies, cb_class=MyAssociationCB) as associations:

            # Check we got an association ID and some known stream policies back
            for association in associations:
                assert(association.association_id)
                assert(association.recvd_stream_policy_names)

            # Check nodes peer info looks correct
            peerinfo = self.nodes[0].getpeerinfo()
            assert_equal(len(peerinfo), len(associations))
            for i in range(len(associations_stream_policies)):
                assert_equal(len(peerinfo[i]["streams"]), len(associations_stream_policies[i].additional_streams) + 1)

            # Check sending and receiving messages works
            for association in associations:
                # Check initial pong message count is 0
                assert_equal(association.callbacks.pong_count, 0)
                # Send ping
                association.send_message(msg_ping())
                # Wait for callback to indicate we got the pong on the association
                wait_until(lambda: association.callbacks.pong_count > 0)
                # Check type of stream pong came over is correct for the stream policy in use
                if type(association.stream_policy) is BlockPriorityStreamPolicy:
                    assert_equal(association.callbacks.pong_stream, StreamType.DATA1)
                elif type(association.stream_policy) is DefaultStreamPolicy:
                    assert_equal(association.callbacks.pong_stream, StreamType.GENERAL)


if __name__ == '__main__':
    P2PSimpleAssociation().main()
