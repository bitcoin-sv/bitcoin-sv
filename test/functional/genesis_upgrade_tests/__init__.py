#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

def tests():
    import importlib
    import inspect
    import glob
    from os.path import dirname, basename, isfile, join
    from genesis_upgrade_tests.test_base import GenesisHeightTestsCaseBase, GenesisHeightBasedSimpleTestsCase

    mods = glob.glob(join(dirname(__file__), "*.py"))
    mod_names = [basename(f)[:-3] for f in mods if isfile(f) and not f.endswith('__init__.py')]

    test_list = []
    for mn in mod_names:
        mod = importlib.import_module(f"{__name__}.{mn}")

        def tt (obj):
            if not inspect.isclass(obj):
                return False
            if obj in [GenesisHeightTestsCaseBase, GenesisHeightBasedSimpleTestsCase]:
                return False
            ret = issubclass(obj, (GenesisHeightTestsCaseBase, GenesisHeightBasedSimpleTestsCase))
            return ret

        test_list.extend([klass for name, klass in inspect.getmembers(mod, tt)])
    return test_list
