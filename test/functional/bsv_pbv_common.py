#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Helper functions for bsv-pbv-*.py tests
"""

from test_framework.util import wait_until


def wait_for_waiting_blocks(hashes, node, log):
    oldArray = []

    def should_wait():
        nonlocal oldArray
        blocks = node.getwaitingblocks()
        if oldArray != blocks:
            log.info("currently waiting blocks: " + str(blocks))
        oldArray = blocks
        return hashes.issubset(blocks)
    wait_until(should_wait)


def wait_for_validating_blocks(hashes, node, log):
    oldArray = []

    def should_wait():
        nonlocal oldArray
        blocks = node.getcurrentlyvalidatingblocks()
        if oldArray != blocks:
            log.info("currently validating blocks: " + str(blocks))
        oldArray = blocks
        return hashes.issubset(blocks)
    wait_until(should_wait)


def wait_for_not_validating_blocks(hashes, node, log):
    oldArray = []

    def should_wait():
        nonlocal oldArray
        blocks = node.getcurrentlyvalidatingblocks()
        if oldArray != blocks:
            log.info("currently validating blocks: " + str(blocks))
        oldArray = blocks
        return hashes.isdisjoint(blocks)
    wait_until(should_wait)
