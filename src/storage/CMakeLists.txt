# Storage layer CMake configuration

add_library(sysmon_storage STATIC
    metrics_storage.cpp
    retention_manager.cpp
)

target_include_directories(sysmon_storage PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(sysmon_storage PUBLIC
    sysmon_core
    SQLite::SQLite3
)

# Link against threading library
find_package(Threads REQUIRED)
target_link_libraries(sysmon_storage PRIVATE Threads::Threads)

# Enable C++17
target_compile_features(sysmon_storage PUBLIC cxx_std_17)

# Platform-specific definitions
if(UNIX AND NOT APPLE)
    target_compile_definitions(sysmon_storage PRIVATE PLATFORM_LINUX)
elseif(WIN32)
    target_compile_definitions(sysmon_storage PRIVATE PLATFORM_WINDOWS)
elseif(APPLE)
    target_compile_definitions(sysmon_storage PRIVATE PLATFORM_MACOS)
endif()
