cmake_minimum_required(VERSION 3.14)

project(
  Ramulator2
  VERSION 2.0
  LANGUAGES CXX
)

# Configure build type ========================================================

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Dramulator2_DEBUG")
# set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# Project settings ============================================================

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# External libraries ==========================================================

include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
    "Skip update of external dependencies after first build."
)

message("Configuring yaml-cpp...")
set(YAML_CPP_BUILD_CONTRIB OFF)
set(YAML_CPP_BUILD_TOOLS OFF)
set(YAML_BUILD_SHARED_LIBS OFF)
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG        yaml-cpp-0.7.0
)
FetchContent_MakeAvailable(yaml-cpp)
message("Done configuring yaml-cpp.")

message("Configuring spdlog...")
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.11.0
)
FetchContent_MakeAvailable(spdlog)
message("Done configuring spdlog.")

message("Configuring argparse...")
FetchContent_Declare(
    argparse
    GIT_REPOSITORY https://github.com/p-ranav/argparse.git
    GIT_TAG        v3.0
)
FetchContent_MakeAvailable(argparse)
message("Done configuring argparse.")

# Import object targets =======================================================

add_subdirectory(src)

# Shared library target =======================================================

add_library(ramulator2 SHARED)
target_include_directories(ramulator2 PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(
  ramulator2
  PRIVATE
    spdlog
    yaml-cpp
    ramulator2_base
    ramulator2_test
    ramulator2_frontend
    ramulator2_translation
    ramulator2_memory_system
    ramulator2_addr_mapper
    ramulator2_dram
    ramulator2_controller
)

# Executable target ===========================================================

add_executable(ramulator2_main ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
target_link_libraries(
  ramulator2_main
  PRIVATE
    ramulator2
    argparse
    spdlog
    yaml-cpp
)
set_target_properties(ramulator2_main PROPERTIES
  OUTPUT_NAME ramulator2
)

# Install =====================================================================

install(TARGETS ramulator2
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(TARGETS ramulator2_main
    RUNTIME DESTINATION bin
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
