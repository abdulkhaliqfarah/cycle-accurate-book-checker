cmake_minimum_required(VERSION 3.20)
project(cycle_accurate_book_checker CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# The production engine under check, vendored from
# github.com/abdulkhaliqfarah/limit-order-book (same author) so this repo
# builds standalone.
add_library(lob third_party/lob/src/order_book.cpp)
target_include_directories(lob PUBLIC third_party/lob/include)

add_library(chk src/scoreboard.cpp)
target_include_directories(chk PUBLIC include)
target_link_libraries(chk PUBLIC lob)

add_executable(run_check tools/run_check.cpp)
target_link_libraries(run_check PRIVATE chk)

add_executable(sweep tools/sweep.cpp)
target_link_libraries(sweep PRIVATE chk)

enable_testing()
add_executable(test_checker tests/test_checker.cpp)
target_link_libraries(test_checker PRIVATE chk)
add_test(NAME checker_scenarios COMMAND test_checker)
