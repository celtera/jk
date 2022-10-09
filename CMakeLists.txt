cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(jk LANGUAGES CXX)
enable_testing()
include(CTest)

find_package(Catch2 3 REQUIRED)
find_package(Boost REQUIRED)

add_library(jk
  include/jk/action_handler.hpp
  include/jk/actions.hpp
  include/jk/config.hpp
  include/jk/generator.hpp
  include/jk/parser.hpp
  include/jk/print.hpp
  include/jk/value.hpp
  src/parser.cpp)
target_compile_features(jk PUBLIC cxx_std_20)
target_include_directories(jk PUBLIC include ${Boost_INCLUDE_DIRS})

add_executable(execute tests/execute.cpp)
target_link_libraries(execute PRIVATE jk fmt)
add_executable(parse_test tests/parse.cpp)
target_link_libraries(parse_test PRIVATE jk Catch2::Catch2WithMain fmt)
