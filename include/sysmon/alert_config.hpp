#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace sysmon {

enum class AlertCondition {
    ABOVE,
    BELOW,
    EQUALS
};

enum class AlertSeverity {
    INFO,
    WARNING,
    CRITICAL
};

struct NotificationChannel {
    std::string type;  // "email", "webhook", "log"
    bool enabled;
    std::map<std::string, std::string> config;
};

struct AlertRule {
    std::string name;
    std::string description;
    std::string metric;
    AlertCondition condition;
    double threshold;
    int duration_seconds;  // Must exceed threshold for this duration
    AlertSeverity severity;
    std::vector<std::string> notification_channels;
    
    // For process-specific alerts
    std::string process_name;  // Empty for system alerts, "*" for any process
    bool is_process_alert;
};

struct GlobalAlertConfig {
    int check_interval;
    int cooldown;
    bool enabled;
};

class AlertConfig {
public:
    AlertConfig() = default;
    
    // Load configuration from YAML file
    bool LoadFromFile(const std::string& config_path);
    
    // Get configuration
    const GlobalAlertConfig& GetGlobalConfig() const { return global_config_; }
    const std::vector<AlertRule>& GetSystemAlerts() const { return system_alerts_; }
    const std::vector<AlertRule>& GetProcessAlerts() const { return process_alerts_; }
    const std::map<std::string, NotificationChannel>& GetNotificationChannels() const {
        return notification_channels_;
    }
    
    // Helpers
    static AlertCondition ParseCondition(const std::string& str);
    static AlertSeverity ParseSeverity(const std::string& str);
    static std::string ConditionToString(AlertCondition condition);
    static std::string SeverityToString(AlertSeverity severity);

private:
    GlobalAlertConfig global_config_;
    std::vector<AlertRule> system_alerts_;
    std::vector<AlertRule> process_alerts_;
    std::map<std::string, NotificationChannel> notification_channels_;
};

} // namespace sysmon
