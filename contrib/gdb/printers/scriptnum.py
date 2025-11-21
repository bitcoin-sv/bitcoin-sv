# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Pretty-printer for CScriptNum type.

CScriptNum is a std::variant<long, bsv::bint> that can hold either
a native long or a big integer. The printer detects which variant
is active and formats it accordingly.

Supports three display formats:
- Default (p): Decimal representation
- Hex (p/x): Hexadecimal representation
- Array (p/a): Serialized byte array
"""

import gdb
from .common import get_print_format
from .bint import BigIntPP


class ScriptNumPP:
    """Pretty-printer for CScriptNum type (std::variant<long, bsv::bint>)."""

    def __init__(self, val):
        self._val = val

    def to_string(self):
        try:
            if not self._val.address:
                return "<no address>"

            format_opt = get_print_format()
            variant = self._val['m_value']
            index = int(variant['_M_index'])

            if index == 0:
                # It's a long (int64_t) - access directly from variant storage
                # std::variant stores first alternative in _M_u._M_first._M_storage
                value = int(variant['_M_u']['_M_first']['_M_storage'])

                # Format based on detected format
                if format_opt == 'a':
                    result = self._format_long_raw(value)
                elif format_opt == 'x':
                    # Format as hex with proper prefix
                    if value < 0:
                        result = f"-0x{-value:x}"
                    else:
                        result = f"0x{value:x}"
                else:
                    result = str(value)

                return f"<long> {result}"
            elif index == 1:
                # It's a bsv::bint - delegate to BigIntPP
                bint_ptr = variant['_M_u']['_M_rest']['_M_first']['_M_storage'].address
                bint_val = gdb.parse_and_eval(f"*(bsv::bint*){bint_ptr}")

                # Delegate to BigIntPP and always add type prefix
                bint_printer = BigIntPP(bint_val)
                return f"<bint> {bint_printer.to_string()}"
            else:
                return f"<invalid index: {index}>"
        except Exception as e:
            return f"<error: {str(e)}>"

    def _format_long_raw(self, value):
        """Format long as serialized bytes."""
        # Convert long to bytes (little-endian, signed)
        bytes_list = []

        if value != 0:
            # Simple serialization for display
            abs_val = abs(value)
            is_negative = value < 0

            while abs_val > 0:
                bytes_list.append(abs_val & 0xff)
                abs_val >>= 8

            # Handle sign bit
            if bytes_list[-1] & 0x80:
                bytes_list.append(0x80 if is_negative else 0x00)
            elif is_negative:
                bytes_list[-1] |= 0x80

        hex_str = " ".join(f"{b:02x}" for b in bytes_list)
        return f"[{hex_str}] [lsb -> msb]"

    def display_hint(self):
        return None


# Export printers for auto-registration
# Format: (name, type_regex, printer_class)
PRINTERS = [
    ('ScriptNumPP', '^CScriptNum$', ScriptNumPP),
]
