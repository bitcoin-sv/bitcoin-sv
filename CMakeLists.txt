# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

cmake_minimum_required(VERSION 3.5)
project(BitcoinSV)
#Reduce warnings on MSVC, to closer match GCC compiler settings
if(POLICY CMP0092 AND MSVC)
   cmake_policy(SET CMP0092 NEW)
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W2")
endif()

# remove ndebug from flags for bitcoind release/relwithdebinfo to work
add_definitions(-UNDEBUG)
foreach (flags
    CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_RELWITHDEBINFO
    CMAKE_CXX_FLAGS_MINSIZEREL
    CMAKE_C_FLAGS_RELEASE
    CMAKE_C_FLAGS_RELWITHDEBINFO
    CMAKE_C_FLAGS_MINSIZEREL)
    string (REGEX REPLACE "(^| )[/-]D *NDEBUG($| )" " "
        "${flags}" "${${flags}}")
endforeach()

option(enable_debug "Enable debug (lockorder)" OFF)
if(enable_debug)
    add_definitions(-DDEBUG -DDEBUG_LOCKORDER -D_GLIBCXX_ASSERTIONS)
endif()

option(enable_metrics "Enable metrics" OFF)
if(enable_metrics)
  add_definitions(-DCOLLECT_METRICS)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR
   CMAKE_CXX_COMPILER_ID STREQUAL "GNU")

    # for more accurate stack trace
    set(sanitizer_common_flags "-fno-omit-frame-pointer")

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # gdb requires file/line print flag to be set explicitly
        set(sanitizer_common_flags "-ggdb ${sanitizer_common_flags}")
    endif()

    ###
    # sanitizer: address
    ###
    # NOTE: with gcc in case you get error:
    #   LeakSanitizer does not work under ptrace
    # disable leak sanitizer part of asan to overcome this:
    #   ASAN_OPTIONS=detect_leaks=0 <executable_name>
    option(enable_asan "Use clang/gcc address sanitizer" OFF)
    if(enable_asan)
        if(enable_tsan)
            message(FATAL_ERROR "enable_asan can not be"
                    " used with enable_tsan enabled!")
        endif()
        set(CMAKE_CXX_FLAGS
            "${CMAKE_CXX_FLAGS} ${sanitizer_common_flags} -fsanitize=address")
        set(CMAKE_LINKER_FLAGS
            "${CMAKE_LINKER_FLAGS} ${sanitizer_common_flags} -fsanitize=address")
    endif()

    ###
    # sanitizer: undefined
    ###
    # NOTE: with gcc stack trace must be enabled with environmet variable:
    #   UBSAN_OPTIONS="print_stacktrace=1" <executable_name>
    option(enable_ubsan "Use clang/gcc undefined behaviour sanitizer" OFF)
    if(enable_ubsan)
        if(enable_tsan)
            message(FATAL_ERROR "enable_ubsan can not be used"
                    " with enable_tsan enabled!")
        endif()
        set(CMAKE_CXX_FLAGS
            "${CMAKE_CXX_FLAGS} ${sanitizer_common_flags} -fsanitize=undefined")
        set(CMAKE_LINKER_FLAGS
            "${CMAKE_LINKER_FLAGS} ${sanitizer_common_flags} -fsanitize=undefined")
    endif()

    ###
    # sanitizer: thread
    ###
    # NOTE: on Linux this sanitizer is also checking recursive locking and
    #       can produce a lot of false positives.
    option(enable_tsan "Use clang thread sanitizer" OFF)
    if(enable_tsan)
        set(CMAKE_CXX_FLAGS
            "${CMAKE_CXX_FLAGS} ${sanitizer_common_flags} -fsanitize=thread")
        set(CMAKE_LINKER_FLAGS
            "${CMAKE_LINKER_FLAGS} ${sanitizer_common_flags} -fsanitize=thread")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        ###
        # static analyzer
        ###
        option(enable_static_analyzer "Use clang static analyzer" OFF)
        if(enable_static_analyzer)
            set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=-*,clang-analyzer-*")
        endif()
    endif()
endif()

# Add path for custom modules.
set(CMAKE_MODULE_PATH
	${CMAKE_MODULE_PATH}
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
