add_library(ramulator2_translation OBJECT)

target_include_directories(ramulator2_translation PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_sources(ramulator2_translation
  PUBLIC
    translation.h

  PRIVATE
    impl/no_translation.cpp
    impl/random_translation.cpp
)

target_link_libraries(ramulator2_translation PRIVATE
  spdlog
  yaml-cpp
)
