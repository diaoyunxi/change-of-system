cmake_minimum_required(VERSION 3.16)

project(change-of-system-tests
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/../src)

add_executable(cos_tests
    test_event.cpp
    test_monitor_engine.cpp
)

enable_testing()
add_test(NAME cos_tests COMMAND cos_tests)
