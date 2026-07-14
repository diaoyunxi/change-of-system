cmake_minimum_required(VERSION 3.16)

project(change-of-system
    VERSION 0.5.0
    DESCRIPTION "Cross-platform system change monitor"
    LANGUAGES CXX C
)

# 定义版本号宏，传递给编译器，确保 main.cpp 与 CMakeLists.txt 版本号一致
add_compile_definitions(COS_VERSION="${PROJECT_VERSION}")

# 尝试查找 nlohmann/json（可选，用于改进 updater.cpp 中的 JSON 解析）
# 如果系统上安装了 nlohmann/json，则定义 COS_HAS_NLOHMANN_JSON 宏
find_package(nlohmann_json 3.2.0 QUIET)
if(nlohmann_json_FOUND)
    add_compile_definitions(COS_HAS_NLOHMANN_JSON)
endif()

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
# 建议在 CI/CD 流水线中通过 -DCOS_WARNINGS_AS_ERRORS=ON 开启此选项，
# 以确保代码库中无编译器警告
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
