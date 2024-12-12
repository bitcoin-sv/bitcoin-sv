# Try to find the tcmalloc libraries
# TCMALLOC_FOUND - system has tcmalloc lib
# TCMALLOC_LIBRARY - Libraries needed to use tcmalloc

if(TCMALLOC_LIBRARY)
	# Already in cache, be silent
	set(TCMALLOC_FIND_QUIETLY TRUE)
endif()

find_library(TCMALLOC_LIBRARY NAMES tcmalloc_minimal libtcmalloc_minimal)

message(STATUS "TCMalloc lib: " ${TCMALLOC_LIBRARY})

mark_as_advanced(TCMALLOC_LIBRARY)
