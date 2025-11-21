# GDB Pretty Printers for BSV Node

This directory contains GDB pretty printers for BSV-specific types to make debugging easier and more intuitive.

## Supported Types

- **`bsv::bint`** - Big integer type
- **`CScriptNum`** - Script number type (std::variant<long, bsv::bint>)

## Installation

### Automatic Setup (Recommended)

The pretty printers are **automatically configured** when you run CMake!

When you configure your build with CMake:
- Symlinks to `.gdbinit` are created at: project root, `src/`, and `src/test/`
- These symlinks point to `contrib/gdb/.gdbinit`
- Works for **all binaries** (bitcoind, test_bitcoin, bench_bitcoin, etc.)
- Pretty-printers load automatically when you debug any program

**One-time global GDB configuration:**

Enable local .gdbinit auto-loading in your `~/.gdbinit`:

```bash
echo "set auto-load local-gdbinit on" >> ~/.gdbinit
echo "set auto-load safe-path /" >> ~/.gdbinit
```

That's it! The pretty-printers will:
- Load automatically when GDB successfully opens a program
- Stay silent if the program file doesn't exist (so errors remain visible)
- Work from any directory in the worktree

### Manual Setup (Advanced)

If not using CMake, create a symlink manually:

```bash
# From your worktree root
ln -s contrib/gdb/.gdbinit .gdbinit

# Or from common working directories
cd src && ln -s ../contrib/gdb/.gdbinit .gdbinit
cd src/test && ln -s ../../contrib/gdb/.gdbinit .gdbinit
```

## Requirements

**Standard Library:** These pretty-printers require **libstdc++** (GNU C++
Standard Library). They access implementation-specific internal structures of
standard library types (std::vector, std::variant) and may not work with other
standard library implementations.

## Usage

Once loaded, the pretty printers automatically format BSV types when you use `print` or display variables.

### Format Specifiers

The pretty printers support multiple format specifiers to view values in different representations:

#### Decimal (default)
```gdb
(gdb) p my_bint
$1 = "123456789"
```

#### Hexadecimal (`/x`)
```gdb
(gdb) p/x my_bint
$2 = "0x75bcd15"
```

#### Serialized Bytes (`/a`)
Shows the value as it would be serialized in a Bitcoin script (little-endian, with sign encoding):
```gdb
(gdb) p/a my_bint
$3 = [15 cd 5b 07] [lsb -> msb]
```

#### Raw Structure (`/r`)
Bypasses pretty printers to show the raw C++ structure:
```gdb
(gdb) p/r my_bint
$4 = {value_ = {<std::unique_ptr<...>> = {...}}}
```

### Examples

```gdb
# scriptnum with long variant
(gdb) p my_scriptnum
$1 = "42"

(gdb) p/x my_scriptnum
$2 = "0x2a"

(gdb) p/a my_scriptnum
$3 = scriptnum: <long> [2a] [lsb -> msb]

# scriptnum with bint variant
(gdb) p large_scriptnum
$4 = "99999999999999999999"

(gdb) p/a large_scriptnum
$5 = scriptnum: <bint> [ff ff 63 a7 b3 b6 e0 0d 56] [lsb -> msb]
```

## Disabling Pretty Printers

### Temporarily Disable All Pretty Printers
```gdb
(gdb) set print pretty-printers off
(gdb) p my_bint
# ... shows raw structure ...
(gdb) set print pretty-printers on
```

### Disable Specific Pretty Printer
```gdb
(gdb) disable pretty-printer global bsv;BigIntPP
(gdb) disable pretty-printer global bsv;ScriptNumPP
```

### Re-enable Specific Pretty Printer
```gdb
(gdb) enable pretty-printer global bsv;BigIntPP
(gdb) enable pretty-printer global bsv;ScriptNumPP
```

### List All Pretty Printers
```gdb
(gdb) info pretty-printer
```

## Architecture

The pretty-printers use a **modular auto-discovery system**:

### Files

- `.gdbinit` - GDB initialization script (symlinked to multiple locations)
- `load_printers.py` - Auto-discovery and registration system
- `printers/` - Modular pretty-printer directory
  - `__init__.py` - Package marker
  - `common.py` - Shared utilities (`get_print_format()`)
  - `bint.py` - Pretty-printer for `bsv::bint`
  - `scriptnum.py` - Pretty-printer for `CScriptNum`

### Auto-Discovery

New printers are automatically discovered and registered! Just:
1. Create a file in `printers/` (e.g., `transaction.py`)
2. Define a `PRINTERS` list with your printer
3. Done - it's automatically found and loaded

## Troubleshooting

### Pretty Printers Not Loading

1. Check that Python support is enabled in GDB:
   ```gdb
   (gdb) python print("GDB Python support is working")
   ```

2. Verify the path in your source command is correct

3. Check for error messages when sourcing the file

### Pretty Printers Not Working

1. Ensure debug symbols are available (`-g` flag during compilation)
2. Check that you're using GDB 13.1+ for format specifier support
3. Try manually registering:
   ```gdb
   (gdb) python import sys; sys.path.insert(0, '/path/to/contrib/gdb')
   (gdb) python from load_printers import register_all_printers; register_all_printers(None)
   ```

### Format Specifiers Not Working

The `/x` and `/a` format specifiers require GDB 13.1 or later. Check your version:
```bash
gdb --version
```

## Contributing

When adding support for new types:

1. Create a new file in `printers/` directory (e.g., `transaction.py`)
2. Define your pretty-printer class(es)
3. Export them in a `PRINTERS` list at the end of the file:
   ```python
   PRINTERS = [
       ('MyTypePP', '^MyType$', MyTypePrettyPrinter),
   ]
   ```
4. The printer will be automatically discovered and loaded
5. Update this README with examples
6. Test with various format specifiers

## See Also

- [GDB Pretty Printing Documentation](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Pretty-Printing.html)
- [Python GDB API](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Python-API.html)
