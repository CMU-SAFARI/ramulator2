cmake_minimum_required(VERSION 3.14)

project(
  Ramulator
  VERSION 2.1
  LANGUAGES CXX
)

#### Prompt the build type ####
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()
message("Configuring ${CMAKE_PROJECT_NAME} ${CMAKE_PROJECT_Version}...")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DRAMULATOR_DEBUG")
# set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
###############################

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

#### External libraries ####
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Skip updating the external dependencies after populating them for the first time")

message("Configuring yaml-cpp...")
option(YAML_CPP_BUILD_CONTRIB "Enable yaml-cpp contrib in library" OFF)
option(YAML_CPP_BUILD_TOOLS "Enable parse tools" OFF)
option(YAML_BUILD_SHARED_LIBS "Build yaml-cpp as a shared library" OFF)
FetchContent_Declare(
  yaml-cpp                             
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG        yaml-cpp-0.7.0                        
  SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/ext/yaml-cpp
)
FetchContent_MakeAvailable(yaml-cpp)
include_directories(${yaml-cpp_SOURCE_DIR}/include)
message("Done configuring yaml-cpp.")

message("Configuring fmt...")
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG        10.2.1
  SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/ext/fmt
)
FetchContent_MakeAvailable(fmt)
include_directories(${fmt_SOURCE_DIR}/include)
message("Done configuring fmt.")

option(RAMULATOR_PYTHON_BINDINGS "Build Python nanobind bindings" ON)
if(RAMULATOR_PYTHON_BINDINGS)
  find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)

  message("Configuring nanobind...")
  FetchContent_Declare(
    nanobind
    GIT_REPOSITORY https://github.com/wjakob/nanobind.git
    GIT_TAG        v2.4.0
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/ext/nanobind
  )
  FetchContent_MakeAvailable(nanobind)
  message("Done configuring nanobind.")
endif()
##################################

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(ramulator SHARED)
set_target_properties(ramulator PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY  ${PROJECT_SOURCE_DIR}
)
# PUBLIC so consumers of the ramulator target get the include path transitively
target_include_directories(ramulator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(
  ramulator
  PRIVATE fmt::fmt
  PRIVATE yaml-cpp
)

add_subdirectory(src/ramulator)

if(RAMULATOR_PYTHON_BINDINGS)
  # Run codegen before building — generates C++ headers from DRAM DSL
  # and Python wrappers from C++ source. Only writes changed files.
  # Post-codegen: run clang-format on generated C++ to match repo style.
  find_program(CLANG_FORMAT clang-format)
  add_custom_target(codegen
    COMMAND ${CMAKE_COMMAND} -E env
      "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python"
      ${Python_EXECUTABLE} -m ramulator codegen
      --src-dir ${CMAKE_CURRENT_SOURCE_DIR}/src/ramulator
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running ramulator codegen..."
  )
  if(CLANG_FORMAT)
    add_custom_target(codegen_format
      COMMAND find ${CMAKE_CURRENT_SOURCE_DIR}/src/ramulator/dram/impl -name "*.cpp" -exec ${CLANG_FORMAT} -i {} +
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Formatting generated C++ files..."
    )
    add_dependencies(codegen_format codegen)
    add_dependencies(ramulator codegen_format)
  else()
    message(STATUS "clang-format not found — generated C++ files will not be auto-formatted")
    add_dependencies(ramulator codegen)
  endif()

  nanobind_add_module(_ramulator src/ramulator/python/bindings.cpp)
  target_link_libraries(_ramulator PRIVATE ramulator)
  target_include_directories(_ramulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  set_target_properties(_ramulator PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/python/ramulator
  )
endif()