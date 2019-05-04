# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 The Bitcoin SV developers
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

cmake_minimum_required(VERSION 3.5)
project(BitcoinSV)

# Add path for custom modules.
set(CMAKE_MODULE_PATH
	${CMAKE_MODULE_PATH}
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules
)

# If ccache is available, then use it.
find_program(CCACHE ccache)
if(CCACHE)
	set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${CCACHE})
	set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ${CCACHE})
endif(CCACHE)

# Add the magic target check and check-all.
add_custom_target(check-all)
add_custom_target(check)

add_subdirectory(src)
add_subdirectory(test)
