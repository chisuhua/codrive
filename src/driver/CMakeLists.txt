cmake_minimum_required(VERSION 2.8)

###################################################################################
set(PROJ pps_queue)
project(${PROJ})

#set(CMAKE_BUILD_TYPE Release)
set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_VERBOSE_MAKEFILE true)

include(${PROJECT_ROOT_PATH}/cmake/configure.cmake)


file(GLOB src
	csq_context.cpp
	csq_queue.cpp
	csq_device.cpp
	csq_cmd.cpp
	csq_pointer.cpp
	csq_accelerator.cpp
	csq.cpp
	unpinned_copy_engine.cpp
	)


file(GLOB inc
	inc/csq.h
	inc/csq_context.h
	inc/csq_queue.h
	inc/csq_device.h
	inc/csq_cmd.h
        )

add_library(${PROJ} SHARED ${src})
set_target_properties(${PROJ} PROPERTIES OUTPUT_NAME ${PROJ})
target_link_libraries(${PROJ} ${LINK_ARGS})
