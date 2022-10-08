cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(jk LANGUAGES CXX)
enable_testing()
include(CTest)

set(CATCH_BUILD_STATIC_LIBRARY 1 FORCE)
set(CATCH_BUILD_STATIC_LIBRARY 1 CACHE "" INTERNAL FORCE)
add_subdirectory(/home/jcelerier/ossia/score/3rdparty/libossia/3rdparty/Catch2 catch2)

find_package(Catch2 REQUIRED)
find_package(Boost REQUIRED)

add_library(jk src/parser.cpp)
target_compile_features(jk PUBLIC cxx_std_20)
target_include_directories(jk PUBLIC include ${Boost_INCLUDE_DIRS})

add_executable(execute tests/execute.cpp)
target_link_libraries(execute PRIVATE jk fmt)
add_executable(parse_test tests/parse.cpp)
target_link_libraries(parse_test PRIVATE jk Catch2::Catch2WithMain fmt)
