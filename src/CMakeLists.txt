set(COS_CORE_SOURCES
    core/event.cpp
    core/monitor.cpp
    core/monitor_engine.cpp
    platform/platform_detection.cpp
    config/config_store.cpp
    storage/storage.cpp
    reporting/reporter.cpp
    monitor/filesystem/filesystem_monitor.cpp
    monitor/process/process_monitor.cpp
    monitor/network/network_monitor.cpp
    monitor/system_config/system_config_monitor.cpp
    alert/alert_manager.cpp
    filter/event_filter.cpp
    stats/statistics.cpp
)

add_library(cos_core STATIC ${COS_CORE_SOURCES})
target_include_directories(cos_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
set_target_properties(cos_core PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(cos_core PUBLIC pthread)
endif()

add_executable(change-of-system main.cpp)
target_link_libraries(change-of-system PRIVATE cos_core)

set_target_properties(change-of-system PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(COS_BUILD_GUI)
    find_package(Qt5 COMPONENTS Widgets Core Gui QUIET)
    if(Qt5_FOUND)
        message(STATUS "Qt5 found - building GUI")
        add_executable(change-of-system-gui
            gui/main_window.cpp
            gui/main_gui.cpp
        )
        target_link_libraries(change-of-system-gui PRIVATE cos_core Qt5::Widgets Qt5::Core Qt5::Gui)
        set_target_properties(change-of-system-gui PROPERTIES
            CXX_STANDARD 17
            AUTOMOC ON
            AUTOUIC ON
            AUTORCC ON
        )
    else()
        message(STATUS "Qt5 not found - GUI build skipped")
    endif()
endif()
