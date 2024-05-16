#!/usr/bin/env python3
# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

def tests(run_only_these):
    import importlib
    import inspect
    import glob
    from os.path import dirname, basename, isfile, join
    from chronicle_upgrade_tests.test_base import ChronicleHeightBasedSimpleTestsCase

    mods = glob.glob(join(dirname(__file__), "*.py"))
    filtered_mods = []
    if run_only_these:
        for run_only in run_only_these:
            for mod in mods:
                if mod.endswith(run_only):
                    filtered_mods.append(mod)
    else:
        filtered_mods = mods
    mod_names = [basename(f)[:-3] for f in filtered_mods if isfile(f) and not f.endswith('__init__.py')]

    test_list = []
    for mn in mod_names:
        mod = importlib.import_module(f"{__name__}.{mn}")

        def test_type(obj):
            if not inspect.isclass(obj):
                return False
            if obj in [ChronicleHeightBasedSimpleTestsCase]:
                return False
            ret = issubclass(obj, (ChronicleHeightBasedSimpleTestsCase))
            return ret

        test_list.extend([klass for name, klass in inspect.getmembers(mod, test_type)])
    return test_list
