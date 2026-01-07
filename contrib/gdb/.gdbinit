# GDB initialization file for BSV Node
#
# This file auto-loads BSV pretty printers for all binaries
# (bitcoind, test_bitcoin, bench_bitcoin, bitcoin-cli, etc.)
#
# It automatically searches upward from the current directory to find
# the project root containing contrib/gdb/, so it works from any
# subdirectory (src/, test/, etc.)
#
# To enable auto-loading of local .gdbinit files, add to ~/.gdbinit:
#   set auto-load local-gdbinit on
#   set auto-load safe-path /
#
# Or start GDB with: gdb -iex "set auto-load local-gdbinit on"

python
import os
import sys

def find_printers_dir():
    """Search upward from current location for contrib/gdb/ directory."""
    # Start from the directory containing this .gdbinit file
    if '__file__' in dir():
        current_dir = os.path.dirname(os.path.abspath(__file__))
    else:
        current_dir = os.getcwd()

    # Search upward for contrib/gdb/
    max_depth = 10  # Prevent infinite loops
    for _ in range(max_depth):
        printers_dir = os.path.join(current_dir, 'contrib', 'gdb')
        if os.path.exists(os.path.join(printers_dir, 'load_printers.py')):
            return printers_dir, current_dir

        # Move up one directory
        parent_dir = os.path.dirname(current_dir)
        if parent_dir == current_dir:  # Reached filesystem root
            break
        current_dir = parent_dir

    return None, None

printers_dir, project_root = find_printers_dir()

if printers_dir:
    if printers_dir not in sys.path:
        sys.path.insert(0, printers_dir)

    # Track loaded objfiles by their filename to detect reloads
    _bsv_loaded_objfiles = set()

    # Load pretty-printers for an objfile
    def load_printers_for_objfile(objfile):
        global _bsv_loaded_objfiles

        # Only register for the main executable, not system libraries
        # Check if this looks like a BSV Node binary
        if not objfile.filename:
            return

        # Skip system libraries - only load for binaries in the project directory
        filename = objfile.filename
        if not any(name in filename for name in ['bitcoind', 'test_bitcoin', 'bench_bitcoin', 'bitcoin-cli', 'bitcoin-tx', 'bitcoin-wallet']) and project_root not in filename:
            return

        # Check if this is a reload (same filename seen before)
        is_reload = filename in _bsv_loaded_objfiles

        if is_reload:
            # Reload modules to pick up any changes
            try:
                import importlib
                # Reload the main loader module
                if 'load_printers' in sys.modules:
                    importlib.reload(sys.modules['load_printers'])
                # Reload all printer modules
                for mod_name in list(sys.modules.keys()):
                    if mod_name.startswith('printers.'):
                        importlib.reload(sys.modules[mod_name])
            except Exception as e:
                print(f"Warning: Could not reload printer modules: {e}")

        try:
            from load_printers import register_all_printers
            register_all_printers(objfile)
            print(f"BSV pretty printers {'reloaded' if is_reload else 'loaded'} from: {printers_dir}")
            _bsv_loaded_objfiles.add(filename)
        except Exception as e:
            print(f"Error loading BSV pretty printers: {e}")
            import traceback
            traceback.print_exc()

    # Handler for new objfile events (for files loaded after .gdbinit)
    def load_printers_on_objfile(event):
        load_printers_for_objfile(event.new_objfile)

    # Connect event handler for future objfile loads
    try:
        gdb.events.new_objfile.connect(load_printers_on_objfile)
    except Exception as e:
        print(f"Error setting up BSV pretty printer loading: {e}")

    # Load printers for any already-loaded objfiles (when file passed as argument)
    try:
        for objfile in gdb.objfiles():
            if objfile.filename:
                load_printers_for_objfile(objfile)
                break  # Only load once per GDB startup
    except:
        pass  # gdb.objfiles() may not be available yet
else:
    print("Warning: BSV pretty printers not found (contrib/gdb/ not in parent directories)")
end
