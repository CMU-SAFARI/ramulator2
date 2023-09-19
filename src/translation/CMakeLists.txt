add_library(ramulator-translation OBJECT)

target_sources(
  ramulator-translation PRIVATE
  translation.h

  impl/no_translation.cpp
  impl/random_translation.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-translation
)