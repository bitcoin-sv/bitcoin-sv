# Try to find the ZeroMQ librairies
# ZMQ_FOUND - system has ZeroMQ lib
# ZMQ_INCLUDE_DIR - the ZeroMQ include directory
# ZMQ_LIBRARY - Libraries needed to use ZeroMQ

if(ZMQ_INCLUDE_DIR AND ZMQ_LIBRARY)
	# Already in cache, be silent
	set(ZMQ_FIND_QUIETLY TRUE)
endif()

find_path(ZMQ_INCLUDE_DIR NAMES zmq.h)

if (MSVC)
  find_library(ZMQ_LIBRARY NAMES libzmq-mt-s-4_3_2 libzmq-mt-s-4_3_3)
else()
  find_library(ZMQ_LIBRARY NAMES zmq libzmq)
endif()

message(STATUS "ZeroMQ lib: " ${ZMQ_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZeroMQ DEFAULT_MSG ZMQ_INCLUDE_DIR ZMQ_LIBRARY)

mark_as_advanced(ZMQ_INCLUDE_DIR ZMQ_LIBRARY)

set(ZeroMQ_LIBRARIES ${ZMQ_LIBRARY})
set(ZeroMQ_INCLUDE_DIRS ${ZMQ_INCLUDE_DIR})
