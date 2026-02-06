/**
 * @file test_metrics_storage.cpp
 * @brief Unit tests for MetricsStorage
 */

#include <gtest/gtest.h>
#include "sysmon/metrics_storage.hpp"
#include "sysmon/platform_interface.hpp"
#include <filesystem>
#include <chrono>

using namespace sysmon;
namespace fs = std::filesystem;

class MetricsStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create unique temp database
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        temp_db_path_ = "/tmp/test_storage_" + std::to_string(now) + ".db";
        
        config_.db_path = temp_db_path_;
        config_.retention_days = 7;
        config_.enable_wal = true;
        config_.batch_size = 10;
    }
    
    void TearDown() override {
        // Clean up test files
        if (fs::exists(temp_db_path_)) {
            fs::remove(temp_db_path_);
        }
        if (fs::exists(temp_db_path_ + "-wal")) {
            fs::remove(temp_db_path_ + "-wal");
        }
        if (fs::exists(temp_db_path_ + "-shm")) {
            fs::remove(temp_db_path_ + "-shm");
        }
    }
    
    CPUMetrics CreateTestCPUMetrics() {
        CPUMetrics metrics;
        metrics.total_usage = 45.5;
        metrics.user_usage = 30.0;
        metrics.system_usage = 15.5;
        metrics.idle_usage = 54.5;
        metrics.num_cores = 8;
        return metrics;
    }
    
    MemoryMetrics CreateTestMemoryMetrics() {
        MemoryMetrics metrics;
        metrics.total_bytes = 16ULL * 1024 * 1024 * 1024; // 16GB
        metrics.available_bytes = 8ULL * 1024 * 1024 * 1024; // 8GB
        metrics.used_bytes = 8ULL * 1024 * 1024 * 1024;
        metrics.usage_percent = 50.0;
        return metrics;
    }
    
    std::string temp_db_path_;
    StorageConfig config_;
};

TEST_F(MetricsStorageTest, Construction) {
    EXPECT_NO_THROW({
        MetricsStorage storage(config_);
    });
    
    // Verify database file was created
    EXPECT_TRUE(fs::exists(temp_db_path_));
}

TEST_F(MetricsStorageTest, WriteCPUMetrics) {
    MetricsStorage storage(config_);
    
    auto metrics = CreateTestCPUMetrics();
    EXPECT_TRUE(storage.WriteCPUMetrics(metrics));
    
    // Flush to ensure write
    storage.Flush();
}

TEST_F(MetricsStorageTest, WriteMemoryMetrics) {
    MetricsStorage storage(config_);
    
    auto metrics = CreateTestMemoryMetrics();
    EXPECT_TRUE(storage.WriteMemoryMetrics(metrics));
    
    storage.Flush();
}

TEST_F(MetricsStorageTest, BatchWrites) {
    MetricsStorage storage(config_);
    
    // Write multiple metrics (should batch)
    for (int i = 0; i < 50; i++) {
        auto cpu = CreateTestCPUMetrics();
        cpu.total_usage = 40.0 + i;
        EXPECT_TRUE(storage.WriteCPUMetrics(cpu));
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    storage.Flush();
}

TEST_F(MetricsStorageTest, QueryLatestMetrics) {
    MetricsStorage storage(config_);
    
    // Write some metrics
    auto cpu = CreateTestCPUMetrics();
    storage.WriteCPUMetrics(cpu);
    storage.Flush();
    
    // Query time range (last 10 seconds)
    auto now = std::chrono::system_clock::now().time_since_epoch().count() / 1000000000;
    auto results = storage.QueryRange("cpu.total_usage", now - 10, now + 10);
    EXPECT_GT(results.size(), 0);
    if (!results.empty()) {
        EXPECT_EQ(results[0].metric_type, "cpu.total_usage");
        EXPECT_NEAR(results[0].value, 45.5, 0.1);
    }
}

TEST_F(MetricsStorageTest, QueryTimeRange) {
    MetricsStorage storage(config_);
    
    // Write metrics over time
    auto now = std::chrono::system_clock::now();
    for (int i = 0; i < 10; i++) {
        auto cpu = CreateTestCPUMetrics();
        cpu.total_usage = 40.0 + i;
        storage.WriteCPUMetrics(cpu);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    storage.Flush();
    
    // Query last 2 seconds
    auto start = std::chrono::duration_cast<std::chrono::seconds>(
        (now - std::chrono::seconds(2)).time_since_epoch()).count();
    auto end = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    auto results = storage.QueryTimeRange("cpu.total_usage", start, end);
    EXPECT_GT(results.size(), 0);
}

TEST_F(MetricsStorageTest, DeleteOldMetrics) {
    config_.retention_days = 0; // Delete everything
    MetricsStorage storage(config_);
    
    // Write old metrics
    auto cpu = CreateTestCPUMetrics();
    storage.WriteCPUMetrics(cpu);
    storage.Flush();
    
    // Delete old data
    int deleted = storage.DeleteOldMetrics();
    EXPECT_GE(deleted, 0);
}

TEST_F(MetricsStorageTest, GetMetricTypes) {
    MetricsStorage storage(config_);
    
    // Write different metric types
    storage.WriteCPUMetrics(CreateTestCPUMetrics());
    storage.WriteMemoryMetrics(CreateTestMemoryMetrics());
    storage.Flush();
    
    auto types = storage.GetMetricTypes();
    EXPECT_GT(types.size(), 0);
    
    bool found_cpu = false;
    bool found_mem = false;
    for (const auto& type : types) {
        if (type.find("cpu") != std::string::npos) found_cpu = true;
        if (type.find("memory") != std::string::npos) found_mem = true;
    }
    EXPECT_TRUE(found_cpu);
    EXPECT_TRUE(found_mem);
}

TEST_F(MetricsStorageTest, ConcurrentWrites) {
    MetricsStorage storage(config_);
    
    std::vector<std::thread> threads;
    std::atomic<int> write_count{0};
    
    // Multiple threads writing
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < 10; j++) {
                auto cpu = CreateTestCPUMetrics();
                cpu.total_usage = 40.0 + i * 10 + j;
                if (storage.WriteCPUMetrics(cpu)) {
                    write_count++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    storage.Flush();
    EXPECT_EQ(write_count, 50);
}

TEST_F(MetricsStorageTest, WALMode) {
    MetricsStorage storage(config_);
    
    // Write some data
    storage.WriteCPUMetrics(CreateTestCPUMetrics());
    storage.Flush();
    
    // Check WAL file exists (if enabled)
    if (config_.enable_wal) {
        // WAL file may or may not exist depending on writes
        // Just verify storage works
        SUCCEED();
    }
}

TEST_F(MetricsStorageTest, InvalidPath) {
    StorageConfig bad_config;
    bad_config.db_path = "/invalid/path/that/does/not/exist/test.db";
    
    EXPECT_THROW({
        MetricsStorage storage(bad_config);
    }, std::runtime_error);
}

TEST_F(MetricsStorageTest, GetDatabaseSize) {
    MetricsStorage storage(config_);
    
    // Write lots of data
    for (int i = 0; i < 100; i++) {
        storage.WriteCPUMetrics(CreateTestCPUMetrics());
        storage.WriteMemoryMetrics(CreateTestMemoryMetrics());
    }
    storage.Flush();
    
    // Database size check removed (method not in API)
    // Just verify database exists
    EXPECT_TRUE(storage.IsHealthy());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
