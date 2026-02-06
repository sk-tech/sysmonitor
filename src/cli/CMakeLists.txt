# Minimal CLI stub (will be expanded later)

add_executable(sysmon
    main.cpp
)

target_link_libraries(sysmon PRIVATE
    sysmon_core
    sysmon_storage
    Threads::Threads
)

target_include_directories(sysmon PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
