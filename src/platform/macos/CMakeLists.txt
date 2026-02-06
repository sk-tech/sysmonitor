add_library(sysmon_platform_macos STATIC
    macos_process_monitor.cpp
    macos_system_metrics.cpp
)

target_include_directories(sysmon_platform_macos PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# macOS frameworks
find_library(COREFOUNDATION_LIBRARY CoreFoundation)
find_library(IOKIT_LIBRARY IOKit)

target_link_libraries(sysmon_platform_macos PUBLIC
    Threads::Threads
    ${COREFOUNDATION_LIBRARY}
    ${IOKIT_LIBRARY}
)

target_compile_features(sysmon_platform_macos PUBLIC cxx_std_17)
