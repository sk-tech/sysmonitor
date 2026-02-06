# Platform-specific implementations

if(PLATFORM_NAME STREQUAL "linux")
    add_subdirectory(linux)
    target_link_libraries(sysmon_core PUBLIC sysmon_platform_linux)
elseif(PLATFORM_NAME STREQUAL "windows")
    add_subdirectory(windows)
    target_link_libraries(sysmon_core PUBLIC sysmon_platform_windows)
elseif(PLATFORM_NAME STREQUAL "macos")
    add_subdirectory(macos)
    target_link_libraries(sysmon_core PUBLIC sysmon_platform_macos)
endif()
