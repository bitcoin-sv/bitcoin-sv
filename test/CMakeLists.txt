# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

###
# Create config.ini file for tests
###
set(abs_top_srcdir ${CMAKE_SOURCE_DIR})
set(abs_top_builddir ${CMAKE_BINARY_DIR})
if(CMAKE_SYSTEM_NAME MATCHES "Windows")
	set(EXEEXT ".exe")
endif()

if(NOT BUILD_BITCOIN_WALLET)
	set(ENABLE_WALLET_TRUE "#")
endif()

if(NOT BUILD_BITCOIN_TX OR NOT BUILD_BITCOIN_TX)
	set(BUILD_BITCOIN_UTILS_TRUE "#")
endif()
if(NOT BUILD_BITCOIN_ZMQ)
	set(ENABLE_ZMQ_TRUE "#")
endif()

# Create build ini file
configure_file(config.ini.in config.ini @ONLY)


###
# Setup symlinks for testing
### 
macro(make_link src dest)
  if (MSVC)  
    # Although recent versions of Windows 10 enables creation of symbolic for non-admin users, this 
    # required developer mode to be turned on. To play it safe, we just copy the files 
    # instead of creating links. Be careful when editing those files in build directory. You might loose changes.
    add_custom_command(
      OUTPUT "${dest}"
      COMMAND ${CMAKE_COMMAND} -E copy "${src}" "${dest}"
      COMMENT "copy (fake link) ${src} -> ${dest}"
      MAIN_DEPENDENCY "${src}"
    )


  else()
    add_custom_command(
      OUTPUT "${dest}"
      COMMAND ${CMAKE_COMMAND} -E create_symlink "${src}" "${dest}"
      COMMENT "make_link ${src} -> ${dest}"
      MAIN_DEPENDENCY "${src}"
    )
  endif()
endmacro()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/functional/)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/util/)

make_link(
	${CMAKE_CURRENT_SOURCE_DIR}/functional/test_runner.py
	${CMAKE_CURRENT_BINARY_DIR}/functional/test_runner.py
)
make_link(
	${CMAKE_CURRENT_SOURCE_DIR}/functional/create_cache.py
	${CMAKE_CURRENT_BINARY_DIR}/functional/create_cache.py
)
make_link(
	${CMAKE_CURRENT_SOURCE_DIR}/util/bitcoin-util-test.py
	${CMAKE_CURRENT_BINARY_DIR}/util/bitcoin-util-test.py
)

IF (MSVC)
	# MSVC build requires configuration name to find buildbitcoind.exe
	SET(extra_runner_prams --buildconfig  $<CONFIG>)
endif()

add_custom_target(check-functional
	COMMAND
		./functional/test_runner.py  ${extra_runner_prams}
	DEPENDS
		../src/bitcoind
		${CMAKE_CURRENT_BINARY_DIR}/functional/test_runner.py
		${CMAKE_CURRENT_BINARY_DIR}/functional/create_cache.py
		${CMAKE_CURRENT_BINARY_DIR}/util/bitcoin-util-test.py
)

add_dependencies(check-all check-functional)
