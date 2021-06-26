cmake_minimum_required(VERSION 2.8)

###################################################################################
set(PROJ pps_device)
project(${PROJ})

#set(CMAKE_BUILD_TYPE Release)
set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_VERBOSE_MAKEFILE true)

include(${PROJECT_ROOT_PATH}/cmake/configure.cmake)

file(GLOB src
	cmdbuilder.cpp
	cmdprocessor.cpp
	memory.cpp
	device_time.cpp
	device_tools.cpp
	device_topology.cpp
	memory_manager.cpp
	device_events.cpp
	asic_type.cpp
	api/memory.cpp
	ppu/ppu_cmdbuilder.cpp
	ppu/ppu_cmdprocessor.cpp
	)
#device_memory.cpp
add_library(${PROJ} SHARED ${src})
set_target_properties(${PROJ} PROPERTIES OUTPUT_NAME ${PROJ})
target_link_libraries(${PROJ} ${LINK_ARGS})
