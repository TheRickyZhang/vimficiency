# 4.3 is the first release whose `import std` covers GCC; the gate UUID below
# changes per release and must be set before project().
cmake_minimum_required(VERSION 4.3)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.4)
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
else()
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
endif()

if(NOT CMAKE_GENERATOR MATCHES "Ninja")
  message(FATAL_ERROR "C++ modules need the Ninja generator: cmake -G Ninja -B build ...")
endif()

project(vimficiency VERSION 0.3.0 LANGUAGES CXX)

# Third-party targets inherit C++23 and would otherwise be dependency-scanned
# for modules they don't use; only project targets opt in.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
function(vimfy_enable_modules target)
  set_target_properties(${target} PROPERTIES
    CXX_SCAN_FOR_MODULES ON
    CXX_MODULE_STD ON
  )
endfunction()

# Default CMAKE_BUILD_TYPE to Debug for local iteration when none is specified.
# Single-config generators (Make, Ninja) leave `CMAKE_BUILD_TYPE` empty by
# default, which gives an undefined flag set — no `-O`, no `-DNDEBUG`. That's
# a footgun: it hides bugs where `assert(x)` bodies reference nonexistent
# symbols (NDEBUG would discard `x` before type-checking), and it makes local
# behavior depend on whatever build type happened to be cached last.
#
# Skipping for multi-config generators (Visual Studio, Xcode), where
# CMAKE_CONFIGURATION_TYPES drives the choice per-build instead.
#
# CI and scripts/install.sh pass `-DCMAKE_BUILD_TYPE=Release` explicitly, so
# this only affects bare `cmake -B build` invocations by developers.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "CMAKE_BUILD_TYPE not set, defaulting to Debug")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON) 
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Have debug enabled by default for development
option(VIMF_DEBUG "Enable debug output" ON)

# Off by default so plugin installs build only the shared library: the test
# tree fetches six third-party dependencies (see tests/CMakeLists.txt).
option(VIMF_BUILD_TESTS "Build test binaries and fetch their dependencies" OFF)

# Compile in explored-state tracking for the A* exploration tooling (off by
# default — enabling this adds per-state allocations on every popped node).
option(VIMF_TRACK_STATES "Enable explored-state recording for vimfy_explore" OFF)

# FuzzTest-backed property and safety tests are part of the normal test build.
# Coverage-guided campaigns are enabled separately with FUZZTEST_FUZZING_MODE
# (Clang only) — see dev/tests/fuzzing.md. The macro that sets FuzzTest's
# fuzzing flags (fuzztest_setup_fuzzing_flags) only exists after the fuzztest
# fetch in tests/, which runs *after* vimfy_core is defined below — so the code
# under test would not get coverage instrumentation. We therefore apply the
# flags here at root scope, before any target, so vimfy_core is instrumented
# too. Keep in sync with fuzztest's cmake/FuzzTestFlagSetup.cmake.
option(FUZZTEST_FUZZING_MODE "Build fuzz tests in coverage-guided fuzzing mode (Clang + ASan + sancov)" OFF)
if(FUZZTEST_FUZZING_MODE)
  set(_vimfy_fuzzing_flags "-g -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -UNDEBUG -fsanitize=address -fsanitize-coverage=inline-8bit-counters -fsanitize-coverage=trace-cmp -DADDRESS_SANITIZER")
  string(APPEND CMAKE_C_FLAGS " ${_vimfy_fuzzing_flags}")
  string(APPEND CMAKE_CXX_FLAGS " ${_vimfy_fuzzing_flags}")
  string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=address")
endif()

# Legacy Vim compatibility mode (uses Vim defaults instead of Neovim defaults)
# Key differences when enabled:
#   - startofline: ON (cursor goes to first non-blank on gg/G/dd/etc)
#   - joinspaces: ON (two spaces after .!? when joining lines)
#   - Y: yy behavior (yank line) instead of y$ (yank to EOL)
option(VIMF_LEGACY_VIM "Use legacy Vim defaults instead of Neovim defaults" OFF)

# Produces STATIC libvimfy_core.a.
# Note to self: Static library just produces bundled code for purposes of easier inclsusion into others

# Generate a single header carrying every build-time flag. Source code reads
# these via `#if` against a name that CMake *actually* emits, eliminating the
# class of bug where `-DFOO=ON` on the command line silently fails to drive a
# `#ifdef FOO` in source (CMake accepts unknown `-D` cache variables).
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BuildConfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/BuildConfig.h
    @ONLY
)

# Hash Api.def at configure time and embed it into the .so so the Lua loader
# can detect a stale binary against newer FFI bindings. Editing Api.def must
# trigger a reconfigure for the hash to refresh, hence CMAKE_CONFIGURE_DEPENDS.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    src/LuaExports/Api.def
)
file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/src/LuaExports/Api.def VF_API_DEF_HASH)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AbiHash.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/AbiHash.h
    @ONLY
)

# Planner-only WebAssembly module for the docs site's interactive explainer
# (dev/web/how-it-works-site.md). Needs the Emscripten toolchain:
#   emcmake cmake -G Ninja -B build-wasm -DVIMF_WASM=ON -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-wasm --target vimfy_planner_wasm
# Explicit source list rather than the glob: the module must stay the planner
# closure (no Lua exports, CLI, fast_float, or tests), and growing it should be
# a visible decision. The list is the transitive import closure of
# PlannerExports.cpp; every module interface needs its implementation units too.
option(VIMF_WASM "Build the planner-only WebAssembly module (requires emcmake)" OFF)
if(VIMF_WASM)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "VIMF_WASM needs the Emscripten toolchain: emcmake cmake -G Ninja -B build-wasm -DVIMF_WASM=ON")
  endif()
  # Directory-wide rather than per target: the std module CMake builds for
  # `import std` must match the importers' -fPIC setting.
  set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
  # emcc does not answer `-print-file-name=libc++.modules.json`, and the
  # description Emscripten ships omits the directory holding the `std/*.inc`
  # files its std.cppm includes, so generate a complete one.
  set(_vimfy_libcxx_modules ${EMSCRIPTEN_ROOT_PATH}/system/lib/libcxx/modules)
  set(CMAKE_CXX_STDLIB_MODULES_JSON ${CMAKE_CURRENT_BINARY_DIR}/libc++.modules.json)
  file(WRITE ${CMAKE_CXX_STDLIB_MODULES_JSON} "{
  \"version\": 1,
  \"revision\": 1,
  \"modules\": [
    {
      \"logical-name\": \"std\",
      \"source-path\": \"${_vimfy_libcxx_modules}/prebuilt/share/libc++/v1/std.cppm\",
      \"is-std-library\": true,
      \"local-arguments\": {
        \"system-include-directories\": [
          \"${_vimfy_libcxx_modules}/prebuilt/share/libc++/v1\",
          \"${_vimfy_libcxx_modules}\"
        ]
      }
    },
    {
      \"logical-name\": \"std.compat\",
      \"source-path\": \"${_vimfy_libcxx_modules}/prebuilt/share/libc++/v1/std.compat.cppm\",
      \"is-std-library\": true,
      \"local-arguments\": {
        \"system-include-directories\": [
          \"${_vimfy_libcxx_modules}/prebuilt/share/libc++/v1\",
          \"${_vimfy_libcxx_modules}\"
        ]
      }
    }
  ]
}
")
  add_executable(vimfy_planner_wasm
    src/Wasm/PlannerExports.cpp
    src/Boundary/BoundaryContext.cpp
    src/Boundary/TransformBoundary.cpp
    src/Effort/RunningEffort.cpp
    src/Interpreter/SequenceParser.cpp
    src/Interpreter/SequenceStream.cpp
    src/Keyboard/Config.cpp
    src/Keyboard/PhysicalKeys.cpp
    src/Keyboard/ToKeys/CharToKeys.cpp
    src/Keyboard/ToKeys/CountToKeys.cpp
    src/Keyboard/ToKeys/MovementToKeys.cpp
    src/Keyboard/ToKeys/SequenceToKeys.cpp
    src/Optimizer/DiffPlanner/DiffState.cpp
    src/Optimizer/DiffPlanner/MyersDiff.cpp
    src/Optimizer/DiffPlanner/PlannerCosts.cpp
    src/Optimizer/DiffPlanner/SealMatchedRuns.cpp
    src/Optimizer/DiffPlanner/VimDiff.cpp
    src/Types/CharInterval.cpp
    src/Utils/Debug.cpp
  )
  target_sources(vimfy_planner_wasm PRIVATE
    FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src FILES
    src/Boundary/BoundaryContext.cppm
    src/Boundary/TransformBoundary.cppm
    src/Effort/RunningEffort.cppm
    src/Interpreter/ParserChar.cppm
    src/Interpreter/SequenceParser.cppm
    src/Keyboard/Config.cppm
    src/Keyboard/Finger.cppm
    src/Keyboard/Hand.cppm
    src/Keyboard/Key.cppm
    src/Keyboard/KeyedSequence.cppm
    src/Keyboard/PhysicalKeys.cppm
    src/Keyboard/ToKeys/CharToKeys.cppm
    src/Keyboard/ToKeys/CommandToKeys.cppm
    src/Keyboard/ToKeys/CountToKeys.cppm
    src/Keyboard/ToKeys/MovementToKeys.cppm
    src/Keyboard/ToKeys/SequenceToKeys.cppm
    src/Optimizer/CountPenalty.cppm
    src/Optimizer/DiffPlanner/DiffState.cppm
    src/Optimizer/DiffPlanner/MyersDiff.cppm
    src/Optimizer/DiffPlanner/PlannerCosts.cppm
    src/Optimizer/DiffPlanner/SealMatchedRuns.cppm
    src/Optimizer/DiffPlanner/VimDiff.cppm
    src/Optimizer/GlobalRuntimeOptions.cppm
    src/Types/BracketFlags.cppm
    src/Types/CharInterval.cppm
    src/Types/CharLineRange.cppm
    src/Types/CharRange.cppm
    src/Types/CountPrefixLimits.cppm
    src/Types/CursorPos.cppm
    src/Types/KeyNotation.cppm
    src/Types/LineCharRange.cppm
    src/Types/Lines.cppm
    src/Types/NoChar.cppm
    src/Types/Pos.cppm
    src/Types/QuoteFlags.cppm
    src/Types/Sequence.cppm
    src/Types/Token.cppm
    src/Utils/Debug.cppm
    src/Utils/PrettyText.cppm
    src/VimCore/CharMask.cppm
  )
  vimfy_enable_modules(vimfy_planner_wasm)
  target_include_directories(vimfy_planner_wasm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
  )
  target_compile_definitions(vimfy_planner_wasm PRIVATE
    $<$<BOOL:${VIMF_LEGACY_VIM}>:VIMF_LEGACY_VIM>
  )
  set_target_properties(vimfy_planner_wasm PROPERTIES
    OUTPUT_NAME vimfy_planner
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/wasm
  )
  # No exception catching: the wrapper sanitizes input so the one throwing
  # call in the closure (CHAR_TO_KEYS.at) cannot fire.
  target_link_options(vimfy_planner_wasm PRIVATE
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sENVIRONMENT=web
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=33554432
    -sASSERTIONS=0
    --no-entry
    -sEXPORTED_RUNTIME_METHODS=cwrap,UTF8ToString,stringToNewUTF8
    -sEXPORTED_FUNCTIONS=_malloc,_free,_vf_plan,_vf_grid,_vf_tiling,_vf_seal
  )
  return()
endif()

# fast_float: portable, header-only replacement for std::from_chars<double>.
# Apple's libc++ ships from_chars<double> as = delete (P0067 unimplemented as
# of Xcode 16), so std::from_chars<double> fails to compile on macOS.
# fast_float is faster than std::from_chars even where the latter works.
include(FetchContent)
FetchContent_Declare(
    fast_float
    GIT_REPOSITORY https://github.com/fastfloat/fast_float.git
    GIT_TAG        v8.0.0
)
FetchContent_MakeAvailable(fast_float)

# Source globs are intentional for fast-moving local development.
file(GLOB_RECURSE VIMFY_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
list(FILTER VIMFY_CORE_SOURCES EXCLUDE REGEX [[/src/(LuaExports/|Journal/|Stats/|Wasm/|lua_exports\.cpp$|main\.cpp$)]])
file(GLOB_RECURSE VIMFY_CORE_MODULES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm
)
list(FILTER VIMFY_CORE_MODULES EXCLUDE REGEX [[/src/(LuaExports/|Journal/|Stats/|Wasm/)]])

add_library(vimfy_core STATIC ${VIMFY_CORE_SOURCES})
target_sources(vimfy_core PUBLIC
    FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src FILES ${VIMFY_CORE_MODULES}
)
vimfy_enable_modules(vimfy_core)
target_link_libraries(vimfy_core PUBLIC fast_float)

# We can reference everything under src/ absolutely. The generated/ directory
# carries BuildConfig.h; source includes it as `#include "BuildConfig.h"`.
target_include_directories(vimfy_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# VIMF_TRACK_STATES is consumed via the generated BuildConfig.h (see above).
# VIMF_DEBUG and VIMF_LEGACY_VIM are still surfaced as raw macros here because
# their source-side readers currently use mismatched names (tracked as
# cleanup items in dev/architecture/todo.md).
target_compile_definitions(vimfy_core PUBLIC
    $<$<BOOL:${VIMF_DEBUG}>:VIMF_DEBUG>
    $<$<BOOL:${VIMF_LEGACY_VIM}>:VIMF_LEGACY_VIM>
)

# Protobuf carries the FFI requests and results declared in src/LuaExports/proto/
# (dev/lua/ffi-payloads.md). Generated C++ only compiles against the exact
# runtime version its protoc belongs to, so both are pinned here. protoc is the
# official prebuilt: building it from source is ~80% of the dependency's
# compile time.
set(_vimfy_protobuf_version 36.1)
set(_vimfy_protoc_host "${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}")
if(_vimfy_protoc_host STREQUAL "Linux-x86_64")
  set(_vimfy_protoc_asset linux-x86_64)
  set(_vimfy_protoc_sha256 c4bc672d9d49214dc8cafdceadf4df92182d6ca8e3ec65a56b2d7de5602669b4)
elseif(_vimfy_protoc_host STREQUAL "Linux-aarch64")
  set(_vimfy_protoc_asset linux-aarch_64)
  set(_vimfy_protoc_sha256 237a68856edf1bd28b6204bddd0596c1cf46d298bc29c620012540b2e44c73e7)
elseif(_vimfy_protoc_host STREQUAL "Darwin-arm64")
  set(_vimfy_protoc_asset osx-aarch_64)
  set(_vimfy_protoc_sha256 de56d57afe30c5d191b11d24ff93dd4025728d7fb43b773886b2d3613e0bdbb2)
elseif(_vimfy_protoc_host STREQUAL "Darwin-x86_64")
  set(_vimfy_protoc_asset osx-x86_64)
  set(_vimfy_protoc_sha256 ee2c5496e4af0aa6a224894bc0f7025145260e004d890487d510725ce8b473eb)
else()
  message(FATAL_ERROR "No prebuilt protoc for host ${_vimfy_protoc_host}")
endif()
FetchContent_Declare(
    protoc_prebuilt
    URL https://github.com/protocolbuffers/protobuf/releases/download/v${_vimfy_protobuf_version}/protoc-${_vimfy_protobuf_version}-${_vimfy_protoc_asset}.zip
    URL_HASH SHA256=${_vimfy_protoc_sha256}
)
FetchContent_MakeAvailable(protoc_prebuilt)
set(protobuf_PROTOC_EXE ${protoc_prebuilt_SOURCE_DIR}/bin/protoc)

# Abseil is fetched here, under FuzzTest's name and version, so protobuf and the
# test tree's FuzzTest share one copy; each would otherwise add its own and the
# targets collide. Keep the tag equal to absl_TAG in FuzzTest's
# cmake/BuildDependencies.cmake.
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    abseil-cpp
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG        20260107.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(abseil-cpp)
if(APPLE)
  # cctz sets _XOPEN_SOURCE, which makes the macOS SDK hide quick_exit while
  # libstdc++'s <cstdlib> still expects it.
  target_compile_definitions(time_zone PRIVATE _DARWIN_C_SOURCE)
endif()

set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_LIBUPB OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG        v${_vimfy_protobuf_version}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(protobuf)
include(${protobuf_SOURCE_DIR}/cmake/protobuf-generate.cmake)

set(_vimfy_proto_dir ${CMAKE_CURRENT_SOURCE_DIR}/src/LuaExports/proto)
# protoc output is compiler-independent, so the configure-only clangd tree
# points this at the real build's copy instead of generating its own.
set(VIMF_PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated CACHE PATH
    "Directory holding the generated proto/ headers")
set(_vimfy_proto_out ${VIMF_PROTO_GEN_DIR}/proto)
file(MAKE_DIRECTORY ${_vimfy_proto_out})
file(GLOB VIMFY_PROTO_FILES CONFIGURE_DEPENDS ${_vimfy_proto_dir}/*.proto)

# The schema Lua decodes with, embedded so it always matches the library
# (vf_proto_descriptor).
add_custom_command(
    OUTPUT ${_vimfy_proto_out}/descriptor.cpp
    COMMAND ${protobuf_PROTOC_EXE}
        --descriptor_set_out=${_vimfy_proto_out}/descriptor.pb
        -I ${_vimfy_proto_dir} ${VIMFY_PROTO_FILES}
    COMMAND ${CMAKE_COMMAND}
        -DINPUT=${_vimfy_proto_out}/descriptor.pb
        -DOUTPUT=${_vimfy_proto_out}/descriptor.cpp
        -DSYMBOL=VF_PROTO_DESCRIPTOR
        -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed-binary.cmake
    DEPENDS ${VIMFY_PROTO_FILES} scripts/embed-binary.cmake
)

add_library(vimfy_proto STATIC ${VIMFY_PROTO_FILES} ${_vimfy_proto_out}/descriptor.cpp)
protobuf_generate(
    TARGET vimfy_proto
    LANGUAGE cpp
    IMPORT_DIRS ${_vimfy_proto_dir}
    PROTOC_OUT_DIR ${_vimfy_proto_out}
    PROTOC_EXE ${protobuf_PROTOC_EXE}
)
target_include_directories(vimfy_proto PUBLIC ${VIMF_PROTO_GEN_DIR})
# protobuf's headers include utf8_validity.h but link the target privately.
target_link_libraries(vimfy_proto PUBLIC protobuf::libprotobuf-lite utf8_validity)

# The LuaExports module interfaces plus the payload/logic code that tests
# exercise without the FFI entry points. A separate target because a module
# interface can only be built once: tests import these instead of recompiling
# them into each binary.
file(GLOB VIMFY_LUA_COMMON_MODULES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LuaExports/*.cppm
)
add_library(vimfy_lua_common STATIC
    src/LuaExports/ConfigState.cpp
    src/LuaExports/Envelope.cpp
    src/LuaExports/Logic.cpp
)
target_sources(vimfy_lua_common PUBLIC
    FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src FILES ${VIMFY_LUA_COMMON_MODULES}
)
vimfy_enable_modules(vimfy_lua_common)
target_link_libraries(vimfy_lua_common PUBLIC vimfy_core vimfy_proto)

# `:Vimfy stats` (src/Stats/): the session log and the reduction of it and the
# journal samples. Outside vimfy_core because it reads protobuf files.
file(GLOB VIMFY_STATS_MODULES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Stats/*.cppm
)
file(GLOB VIMFY_STATS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Stats/*.cpp
)
add_library(vimfy_stats_lib STATIC ${VIMFY_STATS_SOURCES})
target_sources(vimfy_stats_lib PUBLIC
    FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src FILES ${VIMFY_STATS_MODULES}
)
vimfy_enable_modules(vimfy_stats_lib)
target_link_libraries(vimfy_stats_lib PUBLIC vimfy_lua_common)

# SHARED library for Neovim/Lua direct usage
file(GLOB VIMFY_LUA_EXPORT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LuaExports/*Exports.cpp
)

add_library(vimficiency SHARED
    src/lua_exports.cpp
    ${VIMFY_LUA_EXPORT_SOURCES}
)
vimfy_enable_modules(vimficiency)
target_link_libraries(vimficiency PRIVATE vimfy_lua_common vimfy_stats_lib vimfy_journal_lib)

# Lua has no protobuf decoder, so starwing/lua-protobuf's C module ships inside
# the shared library and ffi/core.lua opens it with package.loadlib. Only the
# LuaJIT headers are needed: its Lua C API calls resolve against the Neovim
# process at load time. SOURCE_SUBDIR points nowhere so neither checkout is
# add_subdirectory()'d.
enable_language(C)
FetchContent_Declare(
    lua_protobuf
    GIT_REPOSITORY https://github.com/starwing/lua-protobuf.git
    GIT_TAG        0.6.0
    SOURCE_SUBDIR  _none
)
FetchContent_Declare(
    luajit_headers
    URL https://github.com/LuaJIT/LuaJIT/archive/63c8f1a9b479ba5518cf6a170ff7729178bc3cfd.tar.gz
    URL_HASH SHA256=8cad76f444b76001b7123dc626c5d0bfc24f1f790333737443345f7e6b41bb5f
    SOURCE_SUBDIR  _none
)
FetchContent_MakeAvailable(lua_protobuf luajit_headers)
add_library(vimfy_pb OBJECT ${lua_protobuf_SOURCE_DIR}/pb.c)
target_include_directories(vimfy_pb PRIVATE ${luajit_headers_SOURCE_DIR}/src)
target_sources(vimficiency PRIVATE $<TARGET_OBJECTS:vimfy_pb>)
if(APPLE)
  target_link_options(vimficiency PRIVATE LINKER:-undefined,dynamic_lookup)
endif()

# Journal sampling (src/Journal/): replays capture journals and scores windows
# of them. Outside vimfy_core because it reads the protobuf journal and applies
# the editor's config. `:Vimfy stats` runs the executable, so scripts/install.sh
# builds it alongside the shared library, which links the library to cut a
# journal run again (`vf_stats_run`).
file(GLOB VIMFY_JOURNAL_MODULES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Journal/*.cppm
)
file(GLOB VIMFY_JOURNAL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Journal/*.cpp
)
list(FILTER VIMFY_JOURNAL_SOURCES EXCLUDE REGEX [[/main\.cpp$]])
add_library(vimfy_journal_lib STATIC ${VIMFY_JOURNAL_SOURCES})
target_sources(vimfy_journal_lib PUBLIC
    FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src FILES ${VIMFY_JOURNAL_MODULES}
)
vimfy_enable_modules(vimfy_journal_lib)
target_link_libraries(vimfy_journal_lib PUBLIC vimfy_lua_common vimfy_stats_lib)

add_executable(vimfy_journal src/Journal/main.cpp)
vimfy_enable_modules(vimfy_journal)
target_link_libraries(vimfy_journal PRIVATE vimfy_journal_lib)

# CLI for independent testing
add_executable(vimficiency_cli src/main.cpp)
vimfy_enable_modules(vimficiency_cli)
target_link_libraries(vimficiency_cli PRIVATE vimfy_core)

if(VIMF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
  # Tooling (e.g. exploration data collector; depends on test_utils)
  add_subdirectory(tools)
endif()

# The clangd tree (.clangd) is configure-only, so nothing reruns its globs when a
# source is added. Refresh it whenever another tree reconfigures.
set(_vimfy_clangd_dir ${CMAKE_CURRENT_SOURCE_DIR}/build-clangd)
if(PROJECT_IS_TOP_LEVEL AND EXISTS ${_vimfy_clangd_dir}/CMakeCache.txt
   AND NOT CMAKE_CURRENT_BINARY_DIR STREQUAL _vimfy_clangd_dir)
  execute_process(
      COMMAND ${CMAKE_COMMAND} ${_vimfy_clangd_dir}
      RESULT_VARIABLE _vimfy_clangd_result
      OUTPUT_QUIET ERROR_QUIET
  )
  if(NOT _vimfy_clangd_result EQUAL 0)
    message(WARNING "Refreshing ${_vimfy_clangd_dir} failed; clangd may be stale")
  endif()
endif()
