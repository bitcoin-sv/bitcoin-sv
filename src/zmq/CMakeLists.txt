# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

project(zmq)

add_library(zmq
	zmqabstractnotifier.cpp
	zmqnotificationinterface.cpp
	zmqpublishnotifier.cpp
)

find_package(ZeroMQ REQUIRED)
target_include_directories(zmq PRIVATE ${ZMQ_INCLUDE_DIR})
target_link_libraries(zmq util ${ZMQ_LIBRARY})

if (MSVC)
  # Iphlpapi is required for if_nametoindex and other interface related helper functions
  target_link_libraries(zmq Iphlpapi)
endif()


if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	target_compile_definitions(zmq PUBLIC ZMQ_STATIC)
endif()
