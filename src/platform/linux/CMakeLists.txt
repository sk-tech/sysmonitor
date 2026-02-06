add_library(sysmon_platform_linux STATIC
    linux_process_monitor.cpp
    linux_system_metrics.cpp
)

target_include_directories(sysmon_platform_linux PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(sysmon_platform_linux PUBLIC
    Threads::Threads
)

target_compile_features(sysmon_platform_linux PUBLIC cxx_std_17)
