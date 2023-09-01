cmake_minimum_required(VERSION 3.14)

project(
  Ramulator
  VERSION 2.0
  LANGUAGES CXX
)

#### Run conan setup ####
include(conan.cmake)
conan_cmake_run(CONANFILE conanfile.py  # or relative build/conanfile.txt
                BASIC_SETUP CMAKE_TARGETS
                BUILD missing
                BUILD_TYPE Release
                )
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

set(CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR} ${CMAKE_MODULE_PATH})
set(CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR} ${CMAKE_PREFIX_PATH})

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
find_package(yaml-cpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(argparse REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/src)

add_library(ramulator SHARED)
set_target_properties(ramulator PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY  ${PROJECT_SOURCE_DIR}
)
target_link_libraries(
  ramulator 
  PUBLIC yaml-cpp
  PUBLIC spdlog
  PRIVATE fmt::fmt
)

add_executable(ramulator-exe)
target_include_directories(ramulator-exe PUBLIC include)
target_link_libraries(
  ramulator-exe 
  # PRIVATE -Wl,--whole-archive ramulator -Wl,--no-whole-archive 
  PRIVATE ramulator
  PRIVATE argparse::argparse
)

set_target_properties(
  ramulator-exe
  PROPERTIES 
  OUTPUT_NAME ramulator2
)

add_subdirectory(src)

if (DEFINED CMAKE_INSTALL_INCLUDEDIR)
  install(TARGETS ramulator TYPE LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  install(DIRECTORY include/ramulator DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

