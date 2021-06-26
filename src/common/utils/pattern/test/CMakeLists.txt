cmake_minimum_required(VERSION 2.8)

###################################################################################
#set(CMake_Debug_SetProjByDir true)
#set(CMake_Debug_BuildUnitTest true)
SetProjFromDir()

include(${PROJECT_ROOT_PATH}/cmake/configure.cmake)

file(GLOB TEST_SRCS
     ${CMAKE_CURRENT_LIST_DIR}/*_test.cpp)

#set(DEP_INCS ${PROJECT_ROOT_PATH}/src)
set(DEP_INCS "")
set(DEP_LIBS utils)

foreach(src_file ${TEST_SRCS})
  get_filename_component(src_name ${src_file} NAME)
  string(REPLACE ".cpp" "" exec_name ${src_name})
  BuildGTestUnitTest("${exec_name}" "${src_file}" "${DEP_INCS}" "${DEP_LIBS}")
endforeach()

#set(CMake_Debug_SetProjByDir false)
#set(CMake_Debug_BuildUnitTest false)
