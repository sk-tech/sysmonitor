# CLI tool with distributed monitoring support

add_executable(sysmon
    main.cpp
    ../utils/http_client.cpp
)

target_link_libraries(sysmon PRIVATE
    sysmon_core
    sysmon_storage
    Threads::Threads
)

target_include_directories(sysmon PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
