# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Utilities for BSV Node pretty-printers.
Provides common functionality shared across multiple printers.
"""

import gdb


def get_print_format():
    """
    Get the current print format option (e.g., 'x' for hex, 'a' for array).

    Returns:
        str: The format character ('x', 'a', etc.) or empty string for default
    """
    try:
        print_opts = gdb.print_options()
        return print_opts.get('format', '')
    except Exception:
        return ''
