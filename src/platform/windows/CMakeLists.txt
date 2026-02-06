add_library(sysmon_platform_windows STATIC
    windows_process_monitor.cpp
    windows_system_metrics.cpp
)

target_include_directories(sysmon_platform_windows PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(sysmon_platform_windows PUBLIC
    Threads::Threads
    pdh
    psapi
)

target_compile_features(sysmon_platform_windows PUBLIC cxx_std_17)
