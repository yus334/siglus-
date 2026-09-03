# 依赖获取：SDL2（必需）+ stb（可选，PNG/OGG/TTF）
#
# 安卓端推荐做法（离线、可控）：
#   git clone --depth 1 -b release-2.30.x https://github.com/libsdl-org/SDL.git thirdparty/SDL
#   然后 configure 时传 -DSIGLUS_SDL2_ROOT=<abs>/thirdparty/SDL
# PC 端若无本地 SDL2，会把 SDL2 通过 FetchContent 拉下来（需要网络）。

if(DEFINED ENV{SIGLUS_SDL2_ROOT} AND NOT SIGLUS_SDL2_ROOT)
  set(SIGLUS_SDL2_ROOT "$ENV{SIGLUS_SDL2_ROOT}")
endif()

if(NOT TARGET SDL2::SDL2)
  if(SIGLUS_SDL2_ROOT)
    if(NOT EXISTS "${SIGLUS_SDL2_ROOT}/CMakeLists.txt")
      message(FATAL_ERROR "SIGLUS_SDL2_ROOT 下没有 CMakeLists.txt: ${SIGLUS_SDL2_ROOT}")
    endif()
    set(SDL_SHARED ON  CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST   OFF CACHE BOOL "" FORCE)
    add_subdirectory(${SIGLUS_SDL2_ROOT} ${CMAKE_BINARY_DIR}/_sdl2 EXCLUDE_FROM_ALL)
  else()
    find_package(SDL2 QUIET)
  endif()
endif()

if(NOT TARGET SDL2::SDL2 AND SIGLUS_FETCH_DEPS AND NOT ANDROID)
  message(STATUS "[deps] 未找到本地 SDL2，使用 FetchContent 获取 …")
  include(FetchContent)
  set(SDL_SHARED ON  CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST   OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.9
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
  FetchContent_MakeAvailable(SDL2)
endif()

if(NOT TARGET SDL2::SDL2)
  message(FATAL_ERROR
    "找不到 SDL2。请安装 SDL2 开发包，或设置 -DSIGLUS_SDL2_ROOT=<SDL 源码目录>。")
endif()

# ---- stb 单头文件（可选）----
set(SIGLUS_STB_INCLUDE_DIR "" CACHE PATH "stb 头文件目录（含 stb_image.h / stb_truetype.h / stb_vorbis.h）")

function(siglus_find_stb out_var)
  set(dir "")
  # 只有目录里确实有 stb_image.h 才启用，避免 -D 传入不存在的路径导致编译失败
  if(SIGLUS_STB_INCLUDE_DIR AND EXISTS "${SIGLUS_STB_INCLUDE_DIR}/stb_image.h")
    set(dir "${SIGLUS_STB_INCLUDE_DIR}")
  elseif(DEFINED ENV{STB_INCLUDE_DIR} AND EXISTS "$ENV{STB_INCLUDE_DIR}/stb_image.h")
    set(dir "$ENV{STB_INCLUDE_DIR}")
  elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../thirdparty/stb/stb_image.h")
    get_filename_component(dir "${CMAKE_CURRENT_LIST_DIR}/../thirdparty/stb" ABSOLUTE)
  endif()
  set(${out_var} "${dir}" PARENT_SCOPE)
endfunction()

siglus_find_stb(SIGLUS_STB_DIR)
set(SIGLUS_HAVE_STB 0)
if(SIGLUS_STB AND SIGLUS_STB_DIR)
  set(SIGLUS_HAVE_STB 1)
  message(STATUS "[deps] stb 头文件目录: ${SIGLUS_STB_DIR}")
endif()

function(siglus_apply_feature_flags target)
  target_compile_definitions(${target} PUBLIC SIGLUS_HAVE_STB=$<BOOL:${SIGLUS_HAVE_STB}>)
  if(SIGLUS_HAVE_STB)
    target_include_directories(${target} PUBLIC ${SIGLUS_STB_DIR})
    target_compile_definitions(${target} PUBLIC
      SIGLUS_HAVE_STB_IMAGE=$<BOOL:${SIGLUS_HAVE_STB}>
      SIGLUS_HAVE_STB_TRUETYPE=$<BOOL:${SIGLUS_HAVE_STB}>
      SIGLUS_HAVE_STB_VORBIS=$<BOOL:${SIGLUS_HAVE_STB}>)
  endif()
  if(ANDROID)
    target_compile_definitions(${target} PUBLIC SIGLUS_ANDROID=1)
  endif()
endfunction()
