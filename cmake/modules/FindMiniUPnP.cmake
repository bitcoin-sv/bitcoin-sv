# Try to find the miniupnpc libraries
# UPNP_FOUND - system has UPnP lib
# UPNP_INCLUDE_DIR - the UPnP include directory
# UPNP_LIBRARY - Libraries needed to use UPnP

if(UPNP_INCLUDE_DIR AND UPNP_LIBRARY)
	# Already in cache, be silent
	set(UPNP_FIND_QUIETLY TRUE)
endif()

find_path(UPNP_INCLUDE_DIR NAMES miniupnpc.h PATH_SUFFIXES miniupnpc)
find_library(UPNP_LIBRARY NAMES miniupnpc libminiupnpc)

message(STATUS "UPnP lib: " ${UPNP_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MiniUPnP DEFAULT_MSG UPNP_INCLUDE_DIR UPNP_LIBRARY)

mark_as_advanced(UPNP_INCLUDE_DIR UPNP_LIBRARY)

set(MiniUPnP_LIBRARIES ${UPNP_LIBRARY})
set(MiniUPnP_INCLUDE_DIRS ${UPNP_INCLUDE_DIR})
