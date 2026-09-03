cmake_minimum_required(VERSION 3.22)
project(siglus_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(SIGLUS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

if(NOT EXISTS "${SIGLUS_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR "找不到工程根目录: ${SIGLUS_ROOT}")
endif()

add_subdirectory(${SIGLUS_ROOT} ${CMAKE_BINARY_DIR}/siglus_core_build)
