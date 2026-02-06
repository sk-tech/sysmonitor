/**
 * @file test_alert_manager.cpp
 * @brief Unit tests for AlertManager
 */

#include <gtest/gtest.h>
#include "sysmon/alert_manager.hpp"
#include "sysmon/alert_config.hpp"
#include <fstream>
#include <thread>
#include <chrono>

using namespace sysmon;
using namespace std::chrono_literals;

// Mock notification handler for testing
class MockNotificationHandler : public NotificationHandler {
public:
    bool Send(const AlertEvent& event) override {
        sent_events_.push_back(event);
        return true;
    }
    
    std::string GetType() const override {
        return "mock";
    }
    
    const std::vector<AlertEvent>& GetSentEvents() const {
        return sent_events_;
    }
    
    void Clear() {
        sent_events_.clear();
    }
    
private:
    std::vector<AlertEvent> sent_events_;
};

class AlertManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_config_path_ = "/tmp/test_alerts_" + 
                           std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) +
                           ".yaml";
    }
    
    void TearDown() override {
        if (std::filesystem::exists(temp_config_path_)) {
            std::filesystem::remove(temp_config_path_);
        }
    }
    
    void CreateTestConfig(const std::string& content) {
        std::ofstream file(temp_config_path_);
        file << content;
        file.close();
    }
    
    std::string temp_config_path_;
};

TEST_F(AlertManagerTest, Construction) {
    AlertManager manager;
    EXPECT_FALSE(manager.IsRunning());
}

TEST_F(AlertManagerTest, LoadValidConfig) {
    std::string config = R"(
alerts:
  - name: high_cpu
    metric: cpu.total_usage
    condition: greater_than
    threshold: 80.0
    duration: 30s
    severity: warning
    notifications:
      - type: log
)";
    
    CreateTestConfig(config);
    
    AlertManager manager;
    EXPECT_TRUE(manager.LoadConfig(temp_config_path_));
}

TEST_F(AlertManagerTest, LoadInvalidConfig) {
    CreateTestConfig("invalid yaml: [[[");
    
    AlertManager manager;
    EXPECT_FALSE(manager.LoadConfig(temp_config_path_));
}

TEST_F(AlertManagerTest, LoadNonexistentConfig) {
    AlertManager manager;
    EXPECT_FALSE(manager.LoadConfig("/nonexistent/path.yaml"));
}

TEST_F(AlertManagerTest, StartStop) {
    AlertManager manager;
    
    EXPECT_FALSE(manager.IsRunning());
    
    manager.Start();
    EXPECT_TRUE(manager.IsRunning());
    
    manager.Stop();
    EXPECT_FALSE(manager.IsRunning());
}

TEST_F(AlertManagerTest, EvaluateMetricNoAlert) {
    std::string config = R"(
alerts:
  - name: high_cpu
    metric: cpu.total_usage
    condition: greater_than
    threshold: 80.0
    duration: 30s
    severity: warning
    notifications:
      - type: log
)";
    
    CreateTestConfig(config);
    
    AlertManager manager;
    manager.LoadConfig(temp_config_path_);
    manager.Start();
    
    // Evaluate with value below threshold
    manager.EvaluateMetric("cpu.total_usage", 50.0);
    
    std::this_thread::sleep_for(100ms);
    
    auto alerts = manager.GetActiveAlerts();
    EXPECT_EQ(alerts.size(), 0);
    
    manager.Stop();
}

TEST_F(AlertManagerTest, EvaluateMetricTriggersAlert) {
    std::string config = R"(
alerts:
  - name: high_cpu
    metric: cpu.total_usage
    condition: greater_than
    threshold: 80.0
    duration: 0s
    severity: warning
    notifications:
      - type: log
)";
    
    CreateTestConfig(config);
    
    AlertManager manager;
    auto mock_handler = std::make_unique<MockNotificationHandler>();
    auto* mock_ptr = mock_handler.get();
    
    manager.LoadConfig(temp_config_path_);
    manager.RegisterNotificationHandler(std::move(mock_handler));
    manager.Start();
    
    // Trigger alert
    manager.EvaluateMetric("cpu.total_usage", 90.0);
    
    std::this_thread::sleep_for(200ms);
    
    auto alerts = manager.GetActiveAlerts();
    EXPECT_GT(alerts.size(), 0);
    
    // Check notification was sent
    auto& events = mock_ptr->GetSentEvents();
    EXPECT_GT(events.size(), 0);
    
    manager.Stop();
}

TEST_F(AlertManagerTest, DurationThreshold) {
    std::string config = R"(
alerts:
  - name: high_cpu
    metric: cpu.total_usage
    condition: greater_than
    threshold: 80.0
    duration: 2s
    severity: warning
    notifications:
      - type: log
)";
    
    CreateTestConfig(config);
    
    AlertManager manager;
    manager.LoadConfig(temp_config_path_);
    manager.Start();
    
    // Breach threshold
    manager.EvaluateMetric("cpu.total_usage", 90.0);
    std::this_thread::sleep_for(500ms);
    
    // Should not fire yet (duration not met)
    auto alerts = manager.GetActiveAlerts();
    EXPECT_EQ(alerts.size(), 0);
    
    // Keep breaching
    manager.EvaluateMetric("cpu.total_usage", 90.0);
    std::this_thread::sleep_for(2s);
    
    // Should fire now
    alerts = manager.GetActiveAlerts();
    EXPECT_GT(alerts.size(), 0);
    
    manager.Stop();
}

TEST_F(AlertManagerTest, MultipleAlerts) {
    std::string config = R"(
alerts:
  - name: high_cpu
    metric: cpu.total_usage
    condition: greater_than
    threshold: 80.0
    duration: 0s
    severity: warning
    notifications:
      - type: log
  - name: high_memory
    metric: memory.usage_percent
    condition: greater_than
    threshold: 90.0
    duration: 0s
    severity: critical
    notifications:
      - type: log
)";
    
    CreateTestConfig(config);
    
    AlertManager manager;
    manager.LoadConfig(temp_config_path_);
    manager.Start();
    
    // Trigger both alerts
    manager.EvaluateMetric("cpu.total_usage", 85.0);
    manager.EvaluateMetric("memory.usage_percent", 95.0);
    
    std::this_thread::sleep_for(200ms);
    
    auto alerts = manager.GetActiveAlerts();
    EXPECT_EQ(alerts.size(), 2);
    
    manager.Stop();
}

TEST_F(AlertManagerTest, EvaluateCPUMetrics) {
    std::string config = R"(
alerts:
  - name: high_cpu
    metric: cpu.total_usage
    condition: greater_than
    threshold: 80.0
    duration: 0s
    severity: warning
    notifications:
      - type: log
)";
    
    CreateTestConfig(config);
    
    AlertManager manager;
    manager.LoadConfig(temp_config_path_);
    manager.Start();
    
    CPUMetrics metrics;
    metrics.total_usage = 90.0;
    metrics.user_usage = 60.0;
    metrics.system_usage = 30.0;
    
    manager.EvaluateCPUMetrics(metrics);
    
    std::this_thread::sleep_for(200ms);
    
    auto alerts = manager.GetActiveAlerts();
    EXPECT_GT(alerts.size(), 0);
    
    manager.Stop();
}

TEST_F(AlertManagerTest, EvaluateMemoryMetrics) {
    std::string config = R"(
alerts:
  - name: high_memory
    metric: memory.usage_percent
    condition: greater_than
    threshold: 85.0
    duration: 0s
    severity: critical
    notifications:
      - type: log
)";
    
    CreateTestConfig(config);
    
    AlertManager manager;
    manager.LoadConfig(temp_config_path_);
    manager.Start();
    
    MemoryMetrics metrics;
    metrics.total_bytes = 16ULL * 1024 * 1024 * 1024;
    metrics.used_bytes = 14ULL * 1024 * 1024 * 1024;
    metrics.usage_percent = 87.5;
    
    manager.EvaluateMemoryMetrics(metrics);
    
    std::this_thread::sleep_for(200ms);
    
    auto alerts = manager.GetActiveAlerts();
    EXPECT_GT(alerts.size(), 0);
    
    manager.Stop();
}

TEST_F(AlertManagerTest, AlertStates) {
    std::string config = R"(
alerts:
  - name: test_alert
    metric: test.metric
    condition: greater_than
    threshold: 50.0
    duration: 0s
    severity: warning
    notifications:
      - type: log
)";
    
    CreateTestConfig(config);
    
    AlertManager manager;
    manager.LoadConfig(temp_config_path_);
    manager.Start();
    
    // Get initial states
    auto states = manager.GetAlertStates();
    EXPECT_GT(states.size(), 0);
    
    manager.Stop();
}

TEST_F(AlertManagerTest, CustomNotificationHandler) {
    std::string config = R"(
alerts:
  - name: test_alert
    metric: test.metric
    condition: greater_than
    threshold: 50.0
    duration: 0s
    severity: warning
    notifications:
      - type: mock
)";
    
    CreateTestConfig(config);
    
    AlertManager manager;
    auto mock_handler = std::make_unique<MockNotificationHandler>();
    auto* mock_ptr = mock_handler.get();
    
    manager.RegisterNotificationHandler(std::move(mock_handler));
    manager.LoadConfig(temp_config_path_);
    manager.Start();
    
    manager.EvaluateMetric("test.metric", 75.0);
    std::this_thread::sleep_for(200ms);
    
    auto& events = mock_ptr->GetSentEvents();
    EXPECT_GT(events.size(), 0);
    
    if (events.size() > 0) {
        EXPECT_EQ(events[0].alert_name, "test_alert");
        EXPECT_EQ(events[0].metric, "test.metric");
        EXPECT_DOUBLE_EQ(events[0].current_value, 75.0);
    }
    
    manager.Stop();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
