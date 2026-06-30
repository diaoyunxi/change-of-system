cmake_minimum_required(VERSION 3.16)

project(change-of-system
    VERSION 0.4.0
    DESCRIPTION "Cross-platform system change monitor"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(COS_BUILD_TESTS "Build unit tests" OFF)
option(COS_BUILD_GUI "Build Qt GUI frontend" ON)
option(COS_USE_REMOTE_REPORTING "Enable remote HTTP reporting module" ON)
option(COS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(COS_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
elseif(MSVC)
    add_compile_options(/W4)
    if(COS_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(DetectPlatform)

set(COS_SOURCES
    src/main.cpp
)

add_subdirectory(src)

if(COS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS change-of-system
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

message(STATUS "")
message(STATUS "=== change-of-system ${PROJECT_VERSION} ===")
message(STATUS "  Platform          : ${COS_PLATFORM}")
message(STATUS "  Build type        : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build GUI         : ${COS_BUILD_GUI}")
message(STATUS "  Remote reporting  : ${COS_USE_REMOTE_REPORTING}")
message(STATUS "  Tests             : ${COS_BUILD_TESTS}")
message(STATUS "")
