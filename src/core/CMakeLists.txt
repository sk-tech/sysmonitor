# CMakeLists for core monitoring engine

add_library(sysmon_core STATIC
    metrics_collector.cpp
    platform_factory.cpp
    alert_config.cpp
    alert_manager.cpp
)

target_include_directories(sysmon_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(sysmon_core PUBLIC
    Threads::Threads
)

# Platform-specific implementations will link against this
target_compile_features(sysmon_core PUBLIC cxx_std_17)
