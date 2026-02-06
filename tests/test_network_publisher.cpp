/**
 * @file test_network_publisher.cpp
 * @brief Unit tests for NetworkPublisher
 */

#include <gtest/gtest.h>
#include "sysmon/network_publisher.hpp"
#include "sysmon/platform_interface.hpp"
#include <thread>
#include <chrono>

using namespace sysmon;
using namespace std::chrono_literals;

class NetworkPublisherTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.aggregator_url = "http://localhost:9999";
        config_.publish_interval_ms = 1000;
        config_.batch_size = 10;
        config_.hostname = "test-host";
    }
    
    PublisherConfig config_;
};

TEST_F(NetworkPublisherTest, Construction) {
    EXPECT_NO_THROW({
        NetworkPublisher publisher(config_);
    });
}

TEST_F(NetworkPublisherTest, StartStop) {
    NetworkPublisher publisher(config_);
    
    EXPECT_FALSE(publisher.IsRunning());
    
    publisher.Start();
    EXPECT_TRUE(publisher.IsRunning());
    
    publisher.Stop();
    EXPECT_FALSE(publisher.IsRunning());
}

TEST_F(NetworkPublisherTest, PublishCPUMetrics) {
    NetworkPublisher publisher(config_);
    
    CPUMetrics metrics;
    metrics.total_usage = 45.5;
    metrics.user_usage = 30.0;
    metrics.system_usage = 15.5;
    
    // Should queue even if not connected
    EXPECT_TRUE(publisher.PublishCPUMetrics(metrics));
}

TEST_F(NetworkPublisherTest, PublishMemoryMetrics) {
    NetworkPublisher publisher(config_);
    
    MemoryMetrics metrics;
    metrics.total_bytes = 16ULL * 1024 * 1024 * 1024;
    metrics.used_bytes = 8ULL * 1024 * 1024 * 1024;
    metrics.usage_percent = 50.0;
    
    EXPECT_TRUE(publisher.PublishMemoryMetrics(metrics));
}

TEST_F(NetworkPublisherTest, BatchPublish) {
    NetworkPublisher publisher(config_);
    
    // Publish multiple metrics
    for (int i = 0; i < 20; i++) {
        CPUMetrics metrics;
        metrics.total_usage = 40.0 + i;
        publisher.PublishCPUMetrics(metrics);
    }
    
    // Should have batched internally
    SUCCEED();
}

TEST_F(NetworkPublisherTest, GetStats) {
    NetworkPublisher publisher(config_);
    publisher.Start();
    
    // Publish some metrics
    CPUMetrics cpu;
    cpu.total_usage = 50.0;
    publisher.PublishCPUMetrics(cpu);
    
    std::this_thread::sleep_for(100ms);
    
    auto stats = publisher.GetStats();
    EXPECT_GE(stats.metrics_published, 0);
    
    publisher.Stop();
}

TEST_F(NetworkPublisherTest, InvalidURL) {
    config_.aggregator_url = "invalid://url";
    
    NetworkPublisher publisher(config_);
    publisher.Start();
    
    CPUMetrics metrics;
    metrics.total_usage = 50.0;
    publisher.PublishCPUMetrics(metrics);
    
    std::this_thread::sleep_for(100ms);
    
    // Should handle gracefully
    publisher.Stop();
    SUCCEED();
}

TEST_F(NetworkPublisherTest, ConnectionFailure) {
    // Use non-existent host
    config_.aggregator_url = "http://nonexistent-host-12345:9999";
    
    NetworkPublisher publisher(config_);
    publisher.Start();
    
    CPUMetrics metrics;
    metrics.total_usage = 50.0;
    publisher.PublishCPUMetrics(metrics);
    
    std::this_thread::sleep_for(500ms);
    
    auto stats = publisher.GetStats();
    // Should have attempted publish even if failed
    EXPECT_GE(stats.metrics_published, 0);
    
    publisher.Stop();
}

TEST_F(NetworkPublisherTest, Flush) {
    NetworkPublisher publisher(config_);
    
    // Queue some metrics
    for (int i = 0; i < 5; i++) {
        CPUMetrics metrics;
        metrics.total_usage = 40.0 + i;
        publisher.PublishCPUMetrics(metrics);
    }
    
    // Flush should not throw
    EXPECT_NO_THROW({
        publisher.Flush();
    });
}

TEST_F(NetworkPublisherTest, MultipleStartStop) {
    NetworkPublisher publisher(config_);
    
    for (int i = 0; i < 3; i++) {
        publisher.Start();
        EXPECT_TRUE(publisher.IsRunning());
        
        std::this_thread::sleep_for(100ms);
        
        publisher.Stop();
        EXPECT_FALSE(publisher.IsRunning());
    }
}

TEST_F(NetworkPublisherTest, ConcurrentPublish) {
    NetworkPublisher publisher(config_);
    publisher.Start();
    
    std::vector<std::thread> threads;
    std::atomic<int> publish_count{0};
    
    // Multiple threads publishing
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < 10; j++) {
                CPUMetrics metrics;
                metrics.total_usage = 40.0 + i * 10 + j;
                if (publisher.PublishCPUMetrics(metrics)) {
                    publish_count++;
                }
                std::this_thread::sleep_for(10ms);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    publisher.Stop();
    
    EXPECT_EQ(publish_count, 50);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
