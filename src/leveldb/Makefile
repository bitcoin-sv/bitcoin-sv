# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

# Temporary target to ease CI transition.
# Once the CI system has been updated this entire file can be removed.
check:
	-mkdir -p build
	-cd build; cmake -D LEVELDB_BUILD_TESTS=ON ..
	-cd build; make; make test
