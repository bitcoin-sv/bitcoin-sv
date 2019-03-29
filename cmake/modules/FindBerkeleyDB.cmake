# Try to find the BerkeleyDB librairies
# BDB_FOUND - system has Berkeley DB lib
# BDBXX_INCLUDE_DIR - the Berkeley DB include directory for C++
# BDBXX_LIBRARY - Library needed to use Berkeley DB C++ API

include(BrewHelper)
find_brew_prefix(BREW_HINT berkeley-db)

find_path(BDBXX_INCLUDE_DIR
	NAMES
		db_cxx.h
	HINTS ${BREW_HINT}
)
if (MSVC)
	# MSVC and VCPKG package manager use versioned library name
  find_library(BDBXX_LIBRARY 	NAMES libdb48)  
else()
	find_library(BDBXX_LIBRARY
		NAMES
			db_cxx libdb_cxx
		HINTS ${BREW_HINT}
)
endif()

MESSAGE(STATUS "BerkeleyDB libs: "  ${BDBXX_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BerkeleyDB DEFAULT_MSG   BDBXX_INCLUDE_DIR BDBXX_LIBRARY)

mark_as_advanced(BDBXX_INCLUDE_DIR BDBXX_LIBRARY)

set(BerkeleyDB_LIBRARIES ${BDBXX_LIBRARY})
set(BerkeleyDB_INCLUDE_DIRS ${BDBXX_INCLUDE_DIR})
