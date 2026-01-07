# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Pretty-printer for bsv::bint (big integer) type.

Displays bint values in three formats:
- Default (p): Decimal representation
- Hex (p/x): Hexadecimal representation
- Array (p/a): Serialized byte array (LSB to MSB)
"""

import gdb
from .common import get_print_format


class BigIntPP:
    """Pretty-printer for bsv::bint type."""

    def __init__(self, val):
        self._val = val

    def to_string(self):
        try:
            if not self._val.address:
                return "<no address>"

            format_opt = get_print_format()

            # Handle 'a' format to show serialized bytes (p/a)
            # Note: p/r bypasses pretty printers entirely, so we use p/a instead
            if format_opt == 'a':
                return self._format_raw()

            # Choose function based on format
            if format_opt == 'x':
                eval_string = f"bsv::to_hex(*(bsv::bint*){self._val.address})"
            else:
                eval_string = f"bsv::to_dec(*(bsv::bint*){self._val.address})"

            c_str_result = gdb.parse_and_eval(f"({eval_string}).c_str()")
            result = c_str_result.string()

            # Convert hex to lowercase and add 0x prefix
            if format_opt == 'x':
                result = result.lower()
                # Add 0x prefix if not present
                if not result.startswith('0x') and not result.startswith('-0x'):
                    if result.startswith('-'):
                        result = '-0x' + result[1:]
                    else:
                        result = '0x' + result

            return result
        except Exception as e:
            return f"<error: {str(e)}>"

    def _format_raw(self):
        """Format serialized byte representation of bint."""
        try:
            # Call serialize() method and get the vector
            serialized = gdb.parse_and_eval(f"((bsv::bint*){self._val.address})->serialize()")

            # Get vector size and data
            size = int(serialized['_M_impl']['_M_finish'] - serialized['_M_impl']['_M_start'])
            data_ptr = serialized['_M_impl']['_M_start']

            if size == 0:
                return "[] [lsb -> msb]"

            # Read bytes
            bytes_list = []
            for i in range(size):
                byte_val = int(data_ptr[i])
                bytes_list.append(f"{byte_val:02x}")

            # Format as hex array
            hex_str = " ".join(bytes_list)
            return f"[{hex_str}] [lsb -> msb]"
        except Exception as e:
            return f"bint: <raw format error: {str(e)}>"

    def display_hint(self):
        return None


# Export printers for auto-registration
# Format: (name, type_regex, printer_class)
PRINTERS = [
    ('BigIntPP', '^bsv::bint$', BigIntPP),
]
