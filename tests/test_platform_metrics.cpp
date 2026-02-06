/**
 * @file test_platform_metrics.cpp
 * @brief Unit tests for platform-specific metric gathering
 */

#include <gtest/gtest.h>
#include "sysmon/platform_interface.hpp"
#include <memory>
#include <thread>
#include <chrono>

using namespace sysmon;
using namespace std::chrono_literals;

// External factory functions
extern std::unique_ptr<ISystemMetrics> CreateSystemMetrics();
extern std::unique_ptr<IProcessMonitor> CreateProcessMonitor();

class PlatformMetricsTest : public ::testing::Test {
protected:
    void SetUp() override {
        system_metrics_ = CreateSystemMetrics();
        process_monitor_ = CreateProcessMonitor();
        
        ASSERT_NE(system_metrics_, nullptr);
        ASSERT_NE(process_monitor_, nullptr);
    }
    
    std::unique_ptr<ISystemMetrics> system_metrics_;
    std::unique_ptr<IProcessMonitor> process_monitor_;
};

TEST_F(PlatformMetricsTest, GetCPUMetrics) {
    auto metrics = system_metrics_->GetCPUMetrics();
    
    // Validate CPU metrics
    EXPECT_GE(metrics.total_usage, 0.0);
    EXPECT_LE(metrics.total_usage, 100.0);
    EXPECT_GE(metrics.user_usage, 0.0);
    EXPECT_LE(metrics.user_usage, 100.0);
    EXPECT_GE(metrics.system_usage, 0.0);
    EXPECT_LE(metrics.system_usage, 100.0);
    EXPECT_GE(metrics.idle_usage, 0.0);
    EXPECT_LE(metrics.idle_usage, 100.0);
    EXPECT_GT(metrics.num_cores, 0);
}

TEST_F(PlatformMetricsTest, GetMemoryMetrics) {
    auto metrics = system_metrics_->GetMemoryMetrics();
    
    // Validate memory metrics
    EXPECT_GT(metrics.total_bytes, 0);
    EXPECT_GE(metrics.used_bytes, 0);
    EXPECT_LE(metrics.used_bytes, metrics.total_bytes);
    EXPECT_GE(metrics.available_bytes, 0);
    EXPECT_LE(metrics.available_bytes, metrics.total_bytes);
    EXPECT_GE(metrics.usage_percent, 0.0);
    EXPECT_LE(metrics.usage_percent, 100.0);
}

TEST_F(PlatformMetricsTest, GetDiskMetrics) {
    auto metrics = system_metrics_->GetDiskMetrics();
    
    // Should have at least one disk
    EXPECT_GT(metrics.total_bytes, 0);
    EXPECT_GE(metrics.used_bytes, 0);
    EXPECT_GE(metrics.available_bytes, 0);
}

TEST_F(PlatformMetricsTest, GetNetworkMetrics) {
    auto metrics = system_metrics_->GetNetworkMetrics();
    
    // Network metrics should be non-negative
    EXPECT_GE(metrics.bytes_sent, 0);
    EXPECT_GE(metrics.bytes_received, 0);
    EXPECT_GE(metrics.packets_sent, 0);
    EXPECT_GE(metrics.packets_received, 0);
}

TEST_F(PlatformMetricsTest, GetProcessList) {
    auto processes = process_monitor_->GetProcessList();
    
    // Should have at least one process
    EXPECT_GT(processes.size(), 0);
    
    // Validate process data
    bool found_valid = false;
    for (const auto& proc : processes) {
        if (proc.pid > 0 && !proc.name.empty()) {
            EXPECT_GE(proc.cpu_usage, 0.0);
            EXPECT_LE(proc.cpu_usage, 100.0 * proc.num_threads); // Can exceed 100% on multi-core
            EXPECT_GE(proc.memory_bytes, 0);
            EXPECT_GT(proc.num_threads, 0);
            found_valid = true;
        }
    }
    EXPECT_TRUE(found_valid);
}

TEST_F(PlatformMetricsTest, GetProcessInfo) {
    // Get our own process
    int current_pid = getpid();
    
    auto proc_info = process_monitor_->GetProcessInfo(current_pid);
    
    EXPECT_EQ(proc_info.pid, current_pid);
    EXPECT_FALSE(proc_info.name.empty());
    EXPECT_GT(proc_info.memory_bytes, 0);
    EXPECT_GT(proc_info.num_threads, 0);
}

TEST_F(PlatformMetricsTest, CPUMetricsConsistency) {
    // Get CPU metrics twice with small delay
    auto metrics1 = system_metrics_->GetCPUMetrics();
    std::this_thread::sleep_for(100ms);
    auto metrics2 = system_metrics_->GetCPUMetrics();
    
    // Number of cores should be same
    EXPECT_EQ(metrics1.num_cores, metrics2.num_cores);
    
    // Usage values should be in valid range
    EXPECT_GE(metrics2.total_usage, 0.0);
    EXPECT_LE(metrics2.total_usage, 100.0);
}

TEST_F(PlatformMetricsTest, MemoryMetricsConsistency) {
    auto metrics1 = system_metrics_->GetMemoryMetrics();
    std::this_thread::sleep_for(100ms);
    auto metrics2 = system_metrics_->GetMemoryMetrics();
    
    // Total memory should not change
    EXPECT_EQ(metrics1.total_bytes, metrics2.total_bytes);
    
    // Used + available should approximately equal total
    uint64_t sum = metrics2.used_bytes + metrics2.available_bytes;
    EXPECT_NEAR(sum, metrics2.total_bytes, metrics2.total_bytes * 0.1); // Within 10%
}

TEST_F(PlatformMetricsTest, ProcessListStability) {
    auto processes1 = process_monitor_->GetProcessList();
    std::this_thread::sleep_for(50ms);
    auto processes2 = process_monitor_->GetProcessList();
    
    // Process count should be relatively stable (within 20%)
    EXPECT_NEAR(processes2.size(), processes1.size(), processes1.size() * 0.2);
}

TEST_F(PlatformMetricsTest, InvalidPID) {
    // Try to get info for invalid PID
    auto proc_info = process_monitor_->GetProcessInfo(-1);
    
    // Should return empty/invalid process info
    EXPECT_EQ(proc_info.pid, -1);
}

TEST_F(PlatformMetricsTest, PerCoreMetrics) {
    auto metrics = system_metrics_->GetCPUMetrics();
    
    if (!metrics.per_core_usage.empty()) {
        EXPECT_EQ(metrics.per_core_usage.size(), static_cast<size_t>(metrics.num_cores));
        
        for (double usage : metrics.per_core_usage) {
            EXPECT_GE(usage, 0.0);
            EXPECT_LE(usage, 100.0);
        }
    }
}

TEST_F(PlatformMetricsTest, ConcurrentAccess) {
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    // Multiple threads reading metrics concurrently
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&]() {
            try {
                auto cpu = system_metrics_->GetCPUMetrics();
                auto mem = system_metrics_->GetMemoryMetrics();
                auto procs = process_monitor_->GetProcessList();
                success_count++;
            } catch (...) {
                // Should not throw
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(success_count, 10);
}

TEST_F(PlatformMetricsTest, PerformanceBenchmark) {
    using namespace std::chrono;
    
    // Measure CPU metrics collection time
    auto start = high_resolution_clock::now();
    for (int i = 0; i < 100; i++) {
        system_metrics_->GetCPUMetrics();
    }
    auto end = high_resolution_clock::now();
    auto cpu_duration = duration_cast<milliseconds>(end - start).count();
    
    // Should be reasonably fast (< 10ms per call on average)
    EXPECT_LT(cpu_duration / 100.0, 10.0);
    
    // Measure memory metrics collection time
    start = high_resolution_clock::now();
    for (int i = 0; i < 100; i++) {
        system_metrics_->GetMemoryMetrics();
    }
    end = high_resolution_clock::now();
    auto mem_duration = duration_cast<milliseconds>(end - start).count();
    
    EXPECT_LT(mem_duration / 100.0, 10.0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
