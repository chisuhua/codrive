cmake_minimum_required(VERSION 2.8)

###################################################################################
set(PROJ pps_platform)
project(${PROJ})

#set(CMAKE_BUILD_TYPE Release)
set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_VERBOSE_MAKEFILE true)

include(${PROJECT_ROOT_PATH}/cmake/configure.cmake)

include_directories(${PROJECT_ROOT_PATH}/src)

#add_compile_options(-DNDEBUG)
file(GLOB src
	util/lnx/os_linux.cpp
	util/small_heap.cpp
	util/timer.cpp
	topology.cpp
	pps.cpp
	pps_ext.cpp
	isa.cpp
	platform.cpp
	host_queue.cpp
	signal.cpp
	default_signal.cpp
	interrupt_signal.cpp
	queue.cpp
	cpu_agent.cpp
	ppu_agent.cpp
        blit_cpdma.cpp
	memory_region.cpp
	shared.cpp
	)
#	core/runtime/runtime.cpp
#	tools/intercept/aql_processor.cpp
#	core/runtime/host_queue.cpp
#	core/runtime/default_signal.cpp
#	core/runtime/amd_cpu_agent.cpp
#	core/runtime/amd_gpu_agent.cpp
#	)

#	tools/intercept/profiler.cpp
#	core/runtime/amd_gpu_agent.cpp

#file(GLOB inc
#	core/runtime/host_queue.h
#	core/runtime/agent.h
#	core/runtime/checked.h
#	core/runtime/isa.h
#	core/runtime/memory_region.h
#	core/runtime/queue.h
#	core/runtime/runtime.h
#	core/runtime/signal.h
#	tools/intercept/aql_processor.h
#	tools/intercept/aql_proxy_queue.h
#  )

add_library(${PROJ} SHARED ${src})
#add_library(${PROJ} STATIC ${src})
set_target_properties(${PROJ} PROPERTIES OUTPUT_NAME ${PROJ})
target_link_libraries(${PROJ} ${LINK_ARGS})
