# Minimal daemon stub (will be expanded in Week 3)

add_executable(sysmond
    main.cpp
)

target_link_libraries(sysmond PRIVATE
    sysmon_core
    Threads::Threads
)

target_include_directories(sysmond PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
