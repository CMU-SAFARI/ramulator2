add_library(ramulator-translation OBJECT)

target_sources(
  ramulator-translation PRIVATE
  i_translation.h

  impl/no_translation.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-translation
)
