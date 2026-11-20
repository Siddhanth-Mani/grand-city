cmake_minimum_required(VERSION 3.20)
project(GrandCity6 C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Renderer: d3d12 | vulkan | null. Default: d3d12 on Windows, null elsewhere.
if(WIN32)
  set(GC6_RENDERER "d3d12" CACHE STRING "Renderer backend: d3d12, vulkan or null")
else()
  set(GC6_RENDERER "null" CACHE STRING "Renderer backend: d3d12, vulkan or null")
endif()
option(GC6_BUILD_TOOLS "Build helper tools" ON)
option(GC6_USE_LUA "Enable Lua mission scripting" ON)
option(GC6_USE_AUDIO "Enable miniaudio 3D audio (always built; GC6_NO_AUDIO=1 disables at runtime)" ON)

include(FetchContent)
FetchContent_Declare(glm URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz)
FetchContent_MakeAvailable(glm)

if(GC6_USE_LUA)
  FetchContent_Declare(lua URL https://github.com/lua/lua/archive/refs/tags/v5.4.6.tar.gz)
  FetchContent_MakeAvailable(lua)
  add_library(lua STATIC
    ${lua_SOURCE_DIR}/lapi.c ${lua_SOURCE_DIR}/lauxlib.c ${lua_SOURCE_DIR}/lbaselib.c
    ${lua_SOURCE_DIR}/lcode.c ${lua_SOURCE_DIR}/lcorolib.c ${lua_SOURCE_DIR}/lctype.c
    ${lua_SOURCE_DIR}/ldblib.c ${lua_SOURCE_DIR}/ldebug.c ${lua_SOURCE_DIR}/ldo.c
    ${lua_SOURCE_DIR}/ldump.c ${lua_SOURCE_DIR}/lfunc.c ${lua_SOURCE_DIR}/lgc.c
    ${lua_SOURCE_DIR}/linit.c ${lua_SOURCE_DIR}/liolib.c ${lua_SOURCE_DIR}/llex.c
    ${lua_SOURCE_DIR}/lmathlib.c ${lua_SOURCE_DIR}/lmem.c ${lua_SOURCE_DIR}/loadlib.c
    ${lua_SOURCE_DIR}/lobject.c ${lua_SOURCE_DIR}/lopcodes.c ${lua_SOURCE_DIR}/loslib.c
    ${lua_SOURCE_DIR}/lparser.c ${lua_SOURCE_DIR}/lstate.c ${lua_SOURCE_DIR}/lstring.c
    ${lua_SOURCE_DIR}/lstrlib.c ${lua_SOURCE_DIR}/ltable.c ${lua_SOURCE_DIR}/ltablib.c
    ${lua_SOURCE_DIR}/ltm.c ${lua_SOURCE_DIR}/lundump.c ${lua_SOURCE_DIR}/lutf8lib.c
    ${lua_SOURCE_DIR}/lvm.c ${lua_SOURCE_DIR}/lzio.c)
  target_include_directories(lua PUBLIC ${lua_SOURCE_DIR})
  if(UNIX)
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
    target_link_libraries(lua PUBLIC m dl)
  endif()
  add_compile_definitions(GC6_USE_LUA=1)
endif()

# ---- engine library (single static lib, platform sources conditional) ----
file(GLOB_RECURSE GC6_SOURCES CONFIGURE_DEPENDS
  src/core/*.cpp src/engine/*.cpp src/physics/*.cpp src/game/*.cpp
  src/scripting/*.cpp src/renderer/null_renderer.cpp src/renderer/renderer.cpp
  src/renderer/material.cpp src/renderer/post.cpp src/renderer/shader_watcher.cpp
  src/audio/*.cpp src/net/*.cpp src/renderdoc/*.cpp)
# Audio is always built (miniaudio is vendored, no external dependency);
# set GC6_NO_AUDIO=1 at runtime to run without a sound device.
# Platform backends are added explicitly below; keep them out of the glob so
# they are never listed twice in the target.
list(REMOVE_ITEM GC6_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/win32_window.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/d3d12/d3d12_renderer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/vulkan/vulkan_renderer.cpp)
if(WIN32)
  list(APPEND GC6_SOURCES src/engine/win32_window.cpp
    src/renderer/d3d12/d3d12_renderer.cpp)
  find_package(Vulkan QUIET)
  if(Vulkan_FOUND)
    list(APPEND GC6_SOURCES src/renderer/vulkan/vulkan_renderer.cpp)
  elseif(GC6_RENDERER STREQUAL "vulkan")
    message(WARNING "Vulkan SDK not found; Vulkan backend will fail to link. Install the Vulkan SDK.")
  endif()
endif()
if(APPLE)
  list(APPEND GC6_SOURCES src/renderer/metal/metal_renderer.mm)
endif()

add_library(gc6 ${GC6_SOURCES})
target_include_directories(gc6 PUBLIC src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gc6 PUBLIC glm::glm)
if(UNIX)
  # miniaudio + job system need threads, dlopen and libm.
  find_package(Threads REQUIRED)
  target_link_libraries(gc6 PUBLIC Threads::Threads dl m)
endif()
if(NOT MSVC)
  # Game code is warning-clean: -Werror on the engine target.
  target_compile_options(gc6 PRIVATE -Wall -Wextra -Werror)
endif()
if(GC6_USE_LUA)
  target_link_libraries(gc6 PUBLIC lua)
endif()
if(WIN32)
  # Lean Windows headers (so winsock2.h does not clash with winsock.h), wide-char Win32 API.
  target_compile_definitions(gc6 PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_link_libraries(gc6 PUBLIC d3d12 dxgi d3dcompiler user32 gdi32 ws2_32)
  if(Vulkan_FOUND)
    target_link_libraries(gc6 PUBLIC Vulkan::Vulkan)
    target_compile_definitions(gc6 PUBLIC GC6_HAS_VULKAN=1)
  endif()
endif()
target_compile_definitions(gc6 PUBLIC GC6_RENDERER_NAME="${GC6_RENDERER}")

add_executable(grand-city-6 src/main.cpp)
target_link_libraries(grand-city-6 PRIVATE gc6)

enable_testing()
add_executable(gc6_tests tests/test_main.cpp)
target_link_libraries(gc6_tests PRIVATE gc6)
add_test(NAME gc6_tests COMMAND gc6_tests WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
