# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Auto-discovery and registration of BSV Node pretty-printers.

This module automatically discovers all printer modules in the printers/
directory and registers them with GDB. Each printer module should export
a PRINTERS list containing tuples of (name, type_regex, printer_class).

Usage:
    from load_printers import register_all_printers
    register_all_printers(gdb.current_objfile())
"""

import gdb
import gdb.printing
import importlib
from pathlib import Path


def discover_printer_modules():
    """
    Discover all printer modules in the printers/ directory.

    Returns:
        list: List of module names that can be imported
    """
    printers_dir = Path(__file__).parent / 'printers'

    modules = []
    for item in printers_dir.glob('*.py'):
        if item.name.startswith('_'):
            # Skip __init__.py and other private modules
            continue
        module_name = item.stem
        modules.append(f'printers.{module_name}')

    return modules


def load_printers_from_module(module_name):
    """
    Load printer definitions from a module.

    Args:
        module_name: The module name to import (e.g., 'printers.bint')

    Returns:
        list: List of printer tuples (name, type_regex, printer_class)
    """
    try:
        module = importlib.import_module(module_name)
        if hasattr(module, 'PRINTERS'):
            return module.PRINTERS
        else:
            # Silently skip utility modules without PRINTERS (e.g., common.py)
            return []
    except ImportError as e:
        print(f"Warning: Could not import {module_name}: {e}")
        return []
    except Exception as e:
        print(f"Error loading printers from {module_name}: {e}")
        return []


def build_pretty_printers(obj_file):
    """
    Build a collection of all discovered pretty-printers.

    Args:
        obj_file: The objfile for registration (usually from gdb.current_objfile())

    Returns:
        RegexpCollectionPrettyPrinter: Collection of all printers
    """
    pp = gdb.printing.RegexpCollectionPrettyPrinter("bsv")

    # Discover all printer modules
    printer_modules = discover_printer_modules()

    total_printers = 0
    for module_name in sorted(printer_modules):
        printers = load_printers_from_module(module_name)
        for name, type_regex, printer_class in printers:
            pp.add_printer(name, type_regex, printer_class)
            total_printers += 1

    if total_printers > 0:
        print(f"Registered {total_printers} BSV pretty-printer(s)")

    return pp


def register_all_printers(obj_file):
    """
    Register all discovered pretty-printers with GDB.

    This is the main entry point called from .gdbinit.

    Args:
        obj_file: The objfile for registration (usually from gdb.current_objfile())
    """
    try:
        pp = build_pretty_printers(obj_file)
        gdb.printing.register_pretty_printer(obj_file, pp)
    except Exception as e:
        print(f"Error registering BSV pretty-printers: {e}")
        import traceback
        traceback.print_exc()
