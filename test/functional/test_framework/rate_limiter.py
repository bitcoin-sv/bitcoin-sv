#!/usr/bin/env python3
# Copyright (c) 2026 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from math import ceil

import time


class RateLimiter:
    """ A class that can rate limit processing. Currently used to limit speed of reading and writing to socket."""

    MEASURING_PERIOD = 1.0  # seconds
    MAX_FRACTION_TO_PROCESS_IN_ONE_GO = 0.1

    def __init__(self, rate_limit):
        self.rate_limit = rate_limit  # in amount per second
        self.history = []
        self.processed_in_last_measuring_period = 0
        self.max_chunk = ceil(self.rate_limit * RateLimiter.MAX_FRACTION_TO_PROCESS_IN_ONE_GO * RateLimiter.MEASURING_PERIOD)
        self.amount_per_measuring_period = ceil(self.rate_limit * self.MEASURING_PERIOD)

    def calculate_next_chunk(self, time_now):

        # ignore history older than one measuring period
        prune_to_ndx = None
        for ndx, (time_processed, amount_processed) in enumerate(self.history):
            if time_now - time_processed > self.MEASURING_PERIOD:
                prune_to_ndx = ndx
                self.processed_in_last_measuring_period -= amount_processed
            else:
                break
        if prune_to_ndx is not None:
            del self.history[:prune_to_ndx + 1]

        if self.processed_in_last_measuring_period == 0:
            assert len(self.history) == 0, "History not empty when processed data is 0"
        else:
            assert len(self.history) != 0, "History empty when processed data is not 0"
        assert self.processed_in_last_measuring_period <= self.amount_per_measuring_period, "Too much of processed data"

        # max amount of data that can still be processed in this time period
        can_be_processed = self.amount_per_measuring_period - self.processed_in_last_measuring_period

        return min(can_be_processed, self.max_chunk)

    def update_amount_processed(self, time_processed, amount_processed, sleep_expected_fraction=0):
        if amount_processed:
            self.history.append((time_processed, amount_processed))
            self.processed_in_last_measuring_period += amount_processed
        if sleep_expected_fraction:
            expected_time_to_send = amount_processed / self.rate_limit
            time.sleep(sleep_expected_fraction * expected_time_to_send)
