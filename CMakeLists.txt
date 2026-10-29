# OpenBattlefield standalone build.
#
# Builds the engine-agnostic simulation core, its tests and the benchmark harness WITHOUT Unreal
# Engine. The core sources live inside the Unreal plugin module directory
# (Source/OpenBattlefieldCore) and are compiled from there, so there is exactly one copy of every
# algorithm. See Docs/shared-source.md.
cmake_minimum_required(VERSION 3.20)

project(OpenBattlefield
  VERSION 0.1.0
  DESCRIPTION "Open-source battlefield simulation framework core"
  LANGUAGES CXX)

option(OB_BUILD_TESTS "Build the core test suite" ON)
option(OB_BUILD_BENCHMARKS "Build the benchmark harness" ON)
option(OB_BUILD_TOOLS "Build developer tools (ob_snapshot)" ON)
option(OB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(OB_ENABLE_SANITIZERS "Enable Address/Undefined sanitizers (GCC/Clang)" OFF)
set(OB_CXX_STANDARD "17" CACHE STRING "C++ standard used to compile the core (17 or 20)")

set(CMAKE_CXX_STANDARD ${OB_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Record the commit that was built so benchmark output can reference it.
find_package(Git QUIET)
set(OB_GIT_COMMIT "unknown")
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE OB_GIT_COMMIT_OUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE OB_GIT_RESULT)
  if(OB_GIT_RESULT EQUAL 0)
    set(OB_GIT_COMMIT "${OB_GIT_COMMIT_OUT}")
  endif()
endif()

# ---------------------------------------------------------------------------------------------------
# Warning configuration shared by every target.
# ---------------------------------------------------------------------------------------------------
add_library(ob_warnings INTERFACE)
if(MSVC)
  target_compile_options(ob_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus /utf-8)
  target_compile_definitions(ob_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
  if(OB_WARNINGS_AS_ERRORS)
    target_compile_options(ob_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(ob_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
    -Wnull-dereference -Wdouble-promotion -Wformat=2)
  if(OB_WARNINGS_AS_ERRORS)
    target_compile_options(ob_warnings INTERFACE -Werror)
  endif()
endif()

if(OB_ENABLE_SANITIZERS AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------------------------------
# OpenBattlefieldCore: the simulation core, compiled from the Unreal module directory.
# ---------------------------------------------------------------------------------------------------
set(OB_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Source/OpenBattlefieldCore)

set(OB_CORE_PUBLIC_HEADERS
  ${OB_CORE_DIR}/Public/OpenBattlefieldCore/ObApi.h
  ${OB_CORE_DIR}/Public/OpenBattlefieldCore/ObChunk.h
  ${OB_CORE_DIR}/Public/OpenBattlefieldCore/ObChunkMesher.h
  ${OB_CORE_DIR}/Public/OpenBattlefieldCore/ObCoordinates.h
  ${OB_CORE_DIR}/Public/OpenBattlefieldCore/ObHash.h
  ${OB_CORE_DIR}/Public/OpenBattlefieldCore/ObInstrumentation.h
  ${OB_CORE_DIR}/Public/OpenBattlefieldCore/ObProceduralGenerator.h
  ${OB_CORE_DIR}/Public/OpenBattlefieldCore/ObSerialization.h
  ${OB_CORE_DIR}/Public/OpenBattlefieldCore/ObTerrainOperation.h
  ${OB_CORE_DIR}/Public/OpenBattlefieldCore/ObTerrainWorld.h
  ${OB_CORE_DIR}/Public/OpenBattlefieldCore/ObTypes.h
  ${OB_CORE_DIR}/Public/OpenBattlefieldCore/ObWorldConfig.h
  ${OB_CORE_DIR}/Public/OpenBattlefieldCore/OpenBattlefieldCore.h)

set(OB_CORE_SOURCES
  ${OB_CORE_DIR}/Private/ObChunk.cpp
  ${OB_CORE_DIR}/Private/ObChunkMesher.cpp
  ${OB_CORE_DIR}/Private/ObCoordinates.cpp
  ${OB_CORE_DIR}/Private/ObHash.cpp
  ${OB_CORE_DIR}/Private/ObProceduralGenerator.cpp
  ${OB_CORE_DIR}/Private/ObSerialization.cpp
  ${OB_CORE_DIR}/Private/ObTerrainOperation.cpp
  ${OB_CORE_DIR}/Private/ObTerrainWorld.cpp
  ${OB_CORE_DIR}/Private/ObTypes.cpp
  ${OB_CORE_DIR}/Private/ObWorldConfig.cpp
  ${OB_CORE_DIR}/Private/OpenBattlefieldCoreModule.cpp)

add_library(OpenBattlefieldCore STATIC ${OB_CORE_SOURCES} ${OB_CORE_PUBLIC_HEADERS})
add_library(OpenBattlefield::Core ALIAS OpenBattlefieldCore)
target_include_directories(OpenBattlefieldCore PUBLIC ${OB_CORE_DIR}/Public)
target_link_libraries(OpenBattlefieldCore PRIVATE ob_warnings)
# Mirror Unreal's build environment: no exceptions, no RTTI. The core must compile either way.
if(MSVC)
  target_compile_options(OpenBattlefieldCore PRIVATE /GR- /EHs-c-)
  target_compile_definitions(OpenBattlefieldCore PRIVATE _HAS_EXCEPTIONS=0)
else()
  target_compile_options(OpenBattlefieldCore PRIVATE -fno-exceptions -fno-rtti)
endif()

# ---------------------------------------------------------------------------------------------------
# Tests and benchmarks.
# ---------------------------------------------------------------------------------------------------
if(OB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(Tests/Core)
endif()

if(OB_BUILD_BENCHMARKS)
  add_subdirectory(Benchmarks/Core)
endif()

if(OB_BUILD_TOOLS)
  add_subdirectory(Tools/Snapshot)
endif()
