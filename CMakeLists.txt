cmake_minimum_required(VERSION 3.20)
project(siglus_port LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "build type" FORCE)
endif()

option(SIGLUS_BUILD_TOOLS "构建 siglus_probe 资源探测/反汇编工具" ON)
option(SIGLUS_FETCH_DEPS  "本地找不到 SDL2 时自动联网获取" ON)
option(SIGLUS_STB         "启用 thirdparty/stb 提供的 PNG/OGG/TTF 解码" ON)

if(ANDROID)
  set(SIGLUS_BUILD_TOOLS OFF)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/Deps.cmake)

set(SIGLUS_SOURCES
  src/core/FileSystem.cpp
  src/archive/Archive.cpp
  src/media/Decoder.cpp
  src/script/Script.cpp
  src/gfx/Renderer.cpp
  src/text/Text.cpp
  src/audio/Audio.cpp
  src/input/Input.cpp
  src/app/Engine.cpp
)

if(ANDROID)
  list(APPEND SIGLUS_SOURCES src/platform/VideoPlayer_android.cpp)
else()
  list(APPEND SIGLUS_SOURCES src/platform/VideoPlayer_desktop.cpp)
endif()

add_library(siglus_core STATIC ${SIGLUS_SOURCES})
target_include_directories(siglus_core PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)
target_link_libraries(siglus_core PUBLIC SDL2::SDL2)
siglus_apply_feature_flags(siglus_core)

# Windows 下 SDL_main 提供 WinMain 入口
if(TARGET SDL2::SDL2main AND NOT ANDROID)
  target_link_libraries(siglus_core PUBLIC SDL2::SDL2main)
endif()

if(ANDROID)
  add_library(siglus SHARED src/app/main.cpp)
  target_link_libraries(siglus PRIVATE siglus_core)
else()
  add_executable(siglus_pc WIN32 src/app/main.cpp)
  target_link_libraries(siglus_pc PRIVATE siglus_core)

  if(SIGLUS_BUILD_TOOLS)
    add_executable(siglus_probe tools/probe.cpp)
    target_link_libraries(siglus_probe PRIVATE siglus_core)
  endif()
endif()

# 把 assets/ 复制到构建产物目录，方便 PC 端直接跑
if(NOT ANDROID)
  add_custom_target(siglus_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_LIST_DIR}/assets ${CMAKE_BINARY_DIR}/assets
    COMMENT "拷贝 assets/ 到构建目录")
endif()
