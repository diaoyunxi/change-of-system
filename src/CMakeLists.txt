set(COS_CORE_SOURCES
    core/event.cpp
    core/monitor.cpp
    core/monitor_engine.cpp
    platform/platform_detection.cpp
    config/config_store.cpp
    config/config_watcher.cpp
    storage/storage.cpp
    reporting/reporter.cpp
    monitor/filesystem/filesystem_monitor.cpp
    monitor/process/process_monitor.cpp
    monitor/network/network_monitor.cpp
    monitor/system_config/system_config_monitor.cpp
    monitor/user_activity/user_activity_monitor.cpp
    monitor/service/service_monitor.cpp
    monitor/file_integrity/file_integrity_monitor.cpp
    monitor/usb_device/usb_device_monitor.cpp
    monitor/disk_space/disk_space_monitor.cpp
    monitor/system_load/system_load_monitor.cpp
    monitor/log/log_monitor.cpp
    monitor/port/port_monitor.cpp
    monitor/package/package_monitor.cpp
    monitor/environment/environment_monitor.cpp
    alert/alert_manager.cpp
    filter/event_filter.cpp
    stats/statistics.cpp
    security/security_auditor.cpp
    webhook/webhook_notifier.cpp
    updater/updater.cpp
    export/event_exporter.cpp
    report/report_generator.cpp
    utils/daemon.cpp
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
    find_package(OpenSSL REQUIRED)
    target_link_libraries(cos_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

# CURL detection: enables the update checker and remote HTTP reporting
# (webhook module). Gated by both CURL availability and the
# COS_USE_REMOTE_REPORTING option (ON by default).
find_package(CURL QUIET)
if(CURL_FOUND AND COS_USE_REMOTE_REPORTING)
    target_link_libraries(cos_core PUBLIC CURL::libcurl)
    target_compile_definitions(cos_core PUBLIC COS_USE_REMOTE_REPORTING)
    message(STATUS "CURL found - update checker and remote reporting enabled")
else()
    message(STATUS "CURL not found or remote reporting disabled - update checker disabled")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(cos_core PUBLIC "-framework Security")
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
