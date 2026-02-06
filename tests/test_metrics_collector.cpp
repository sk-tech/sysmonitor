/**
 * @file test_metrics_collector.cpp
 * @brief Unit tests for MetricsCollector
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "metrics_collector.hpp"
#include "sysmon/platform_interface.hpp"
#include <thread>
#include <chrono>

using namespace sysmon;
using namespace std::chrono_literals;

// Mock platform interface for testing
class MockSystemMetrics : public ISystemMetrics {
public:
    MOCK_METHOD(CPUMetrics, GetCPUMetrics, (), (override));
    MOCK_METHOD(MemoryMetrics, GetMemoryMetrics, (), (override));
    MOCK_METHOD(DiskMetrics, GetDiskMetrics, (), (override));
    MOCK_METHOD(NetworkMetrics, GetNetworkMetrics, (), (override));
};

class MockProcessMonitor : public IProcessMonitor {
public:
    MOCK_METHOD(std::vector<ProcessInfo>, GetProcessList, (), (override));
    MOCK_METHOD(ProcessInfo, GetProcessInfo, (int pid), (override));
};

class MetricsCollectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary database for testing
        temp_db_path_ = "/tmp/test_metrics_" + 
                       std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + 
                       ".db";
        
        storage_config_.db_path = temp_db_path_;
        storage_config_.retention_days = 1;
        storage_config_.batch_size = 10;
    }
    
    void TearDown() override {
        // Clean up test database
        std::remove(temp_db_path_.c_str());
    }
    
    std::string temp_db_path_;
    StorageConfig storage_config_;
};

TEST_F(MetricsCollectorTest, ConstructWithoutStorage) {
    MetricsCollector collector;
    EXPECT_FALSE(collector.IsRunning());
}

TEST_F(MetricsCollectorTest, ConstructWithStorage) {
    MetricsCollector collector(storage_config_);
    EXPECT_FALSE(collector.IsRunning());
}

TEST_F(MetricsCollectorTest, StartStopLifecycle) {
    MetricsCollector collector;
    
    EXPECT_FALSE(collector.IsRunning());
    
    collector.Start(100); // 100ms interval
    EXPECT_TRUE(collector.IsRunning());
    
    std::this_thread::sleep_for(250ms); // Let it collect a few times
    
    collector.Stop();
    EXPECT_FALSE(collector.IsRunning());
}

TEST_F(MetricsCollectorTest, GetLatestMetrics) {
    MetricsCollector collector;
    collector.Start(100);
    
    std::this_thread::sleep_for(250ms); // Wait for collection
    
    auto cpu = collector.GetLatestCPU();
    auto mem = collector.GetLatestMemory();
    
    // Verify we got valid metrics
    EXPECT_GE(cpu.total_usage, 0.0);
    EXPECT_LE(cpu.total_usage, 100.0);
    EXPECT_GT(mem.total_bytes, 0);
    
    collector.Stop();
}

TEST_F(MetricsCollectorTest, MetricCallback) {
    MetricsCollector collector;
    
    int callback_count = 0;
    CPUMetrics last_cpu;
    MemoryMetrics last_mem;
    
    collector.RegisterCallback([&](const CPUMetrics& cpu, const MemoryMetrics& mem) {
        callback_count++;
        last_cpu = cpu;
        last_mem = mem;
    });
    
    collector.Start(100);
    std::this_thread::sleep_for(350ms); // Should trigger ~3 callbacks
    collector.Stop();
    
    EXPECT_GE(callback_count, 2); // At least 2 callbacks
    EXPECT_GE(last_cpu.total_usage, 0.0);
}

TEST_F(MetricsCollectorTest, MultipleStartStopCycles) {
    MetricsCollector collector;
    
    for (int i = 0; i < 3; i++) {
        collector.Start(100);
        EXPECT_TRUE(collector.IsRunning());
        
        std::this_thread::sleep_for(150ms);
        
        collector.Stop();
        EXPECT_FALSE(collector.IsRunning());
    }
}

TEST_F(MetricsCollectorTest, GetProcessList) {
    MetricsCollector collector;
    
    auto processes = collector.GetProcessList();
    
    // Should have at least one process (ourselves)
    EXPECT_GT(processes.size(), 0);
    
    // Verify process data structure
    bool found_valid = false;
    for (const auto& proc : processes) {
        if (proc.pid > 0 && !proc.name.empty()) {
            found_valid = true;
            break;
        }
    }
    EXPECT_TRUE(found_valid);
}

TEST_F(MetricsCollectorTest, ConcurrentAccess) {
    MetricsCollector collector;
    collector.Start(50);
    
    std::atomic<int> read_count{0};
    std::vector<std::thread> threads;
    
    // Multiple threads reading metrics concurrently
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 10; j++) {
                auto cpu = collector.GetLatestCPU();
                auto mem = collector.GetLatestMemory();
                read_count++;
                std::this_thread::sleep_for(10ms);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    collector.Stop();
    
    EXPECT_EQ(read_count, 50); // All reads completed
}

TEST_F(MetricsCollectorTest, AlertManagerIntegration) {
    auto alert_manager = std::make_shared<AlertManager>();
    
    MetricsCollector collector;
    collector.SetAlertManager(alert_manager);
    
    collector.Start(100);
    std::this_thread::sleep_for(250ms);
    collector.Stop();
    
    // Just verify it doesn't crash with alert manager set
    SUCCEED();
}

// Test storage integration
TEST_F(MetricsCollectorTest, StorageIntegration) {
    MetricsCollector collector(storage_config_);
    
    collector.Start(100);
    std::this_thread::sleep_for(500ms); // Collect some metrics
    collector.Stop();
    
    // Verify database was created
    std::ifstream db_file(temp_db_path_);
    EXPECT_TRUE(db_file.good());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
