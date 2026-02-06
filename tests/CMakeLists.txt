# Test suite for SysMonitor

include(GoogleTest)

# Common test libraries
set(TEST_LIBS
    gtest
    gtest_main
    gmock
    sysmon_core
    sysmon_storage
    sysmon_platform
    SQLite::SQLite3
    Threads::Threads
)

# Unit tests
add_executable(test_metrics_collector
    test_metrics_collector.cpp
)
target_link_libraries(test_metrics_collector PRIVATE ${TEST_LIBS})
gtest_discover_tests(test_metrics_collector)

add_executable(test_metrics_storage
    test_metrics_storage.cpp
)
target_link_libraries(test_metrics_storage PRIVATE ${TEST_LIBS})
gtest_discover_tests(test_metrics_storage)

add_executable(test_alert_manager
    test_alert_manager.cpp
)
target_link_libraries(test_alert_manager PRIVATE ${TEST_LIBS})
gtest_discover_tests(test_alert_manager)

add_executable(test_network_publisher
    test_network_publisher.cpp
)
target_link_libraries(test_network_publisher PRIVATE ${TEST_LIBS})
gtest_discover_tests(test_network_publisher)

add_executable(test_platform_metrics
    test_platform_metrics.cpp
)
target_link_libraries(test_platform_metrics PRIVATE ${TEST_LIBS})
gtest_discover_tests(test_platform_metrics)

# Add test data directory to build
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
