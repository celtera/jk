cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(jk LANGUAGES CXX)
enable_testing()
include(CTest)

find_package(Catch2 3 REQUIRED)
find_package(fmt REQUIRED)

find_path(SMALLFUN_INCLUDE_DIR
  NAMES smallfun.hpp
  HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rdparty/libossia/3rdparty/SmallFunction/smallfun/include"
  DOC "Directory containing the SmallFunction smallfun.hpp header")
if(NOT SMALLFUN_INCLUDE_DIR)
  message(FATAL_ERROR "SmallFunction was not found; set SMALLFUN_INCLUDE_DIR to its include directory")
endif()

add_library(jk
  include/jk/action_fun.hpp
  include/jk/action_handler.hpp
  include/jk/actions.hpp
  include/jk/config.hpp
  include/jk/generator.hpp
  include/jk/memory.hpp
  include/jk/parser.hpp
  include/jk/print.hpp
  include/jk/value.hpp
  src/parser.cpp)
target_compile_features(jk PUBLIC cxx_std_20)
target_include_directories(jk PUBLIC include "${SMALLFUN_INCLUDE_DIR}")

add_executable(execute tests/execute.cpp)
target_link_libraries(execute PRIVATE jk fmt::fmt)
add_executable(parse_test tests/parse.cpp tests/ownership.cpp)
target_link_libraries(parse_test PRIVATE jk Catch2::Catch2WithMain fmt::fmt)
add_test(NAME parse_test COMMAND parse_test)

# Conformance against jq itself: tests/cases.txt is run through the real jq by
# tests/gen-cases.sh, and the generated table is what these compare against.
add_executable(conformance tests/conformance.cpp)
target_link_libraries(conformance PRIVATE jk)
add_test(NAME conformance COMMAND conformance)

# The official jq suite (jq's own tests/jq.test). Programs jk does not
# implement are reported as unsupported; a mismatch is a real divergence.
add_executable(jq_suite tests/jq_suite.cpp)
target_link_libraries(jq_suite PRIVATE jk)

# Robustness and cost. jk runs on a realtime execution thread, so this asserts
# that nothing but values or a jk::error can reach the caller, and reports what
# a message costs.
add_executable(robustness tests/robustness.cpp)
target_link_libraries(robustness PRIVATE jk)
add_test(NAME robustness COMMAND robustness)
