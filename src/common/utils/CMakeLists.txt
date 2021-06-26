cmake_minimum_required(VERSION 2.8)

###################################################################################
set(PROJ utils)
project(${PROJ})

#set(CMAKE_BUILD_TYPE Release)
set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_VERBOSE_MAKEFILE true)

include(${PROJECT_ROOT_PATH}/cmake/configure.cmake)


file(GLOB src
        lang/cmdline.cpp
        lang/debug.cpp
        lang/error.cpp
        lang/lang.cpp
        lang/string.cpp
        lang/terminal.cpp
        lang/timer.cpp
        lang/thread_group.cpp
	sys/DynamicLibrary.cpp
        file_util.cpp
        log.cpp
        util.cpp
	)


file(GLOB inc
        )

add_library(${PROJ} SHARED ${src})
set_target_properties(${PROJ} PROPERTIES OUTPUT_NAME ${PROJ})
target_link_libraries(${PROJ} ${LINK_ARGS})
