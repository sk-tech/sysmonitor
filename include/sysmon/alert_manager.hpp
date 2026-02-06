#pragma once

#include "sysmon/alert_config.hpp"
#include "sysmon/platform_interface.hpp"
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <map>
#include <functional>

namespace sysmon {

struct AlertEvent {
    std::string alert_name;
    std::string metric;
    double current_value;
    double threshold;
    AlertCondition condition;
    AlertSeverity severity;
    std::chrono::system_clock::time_point timestamp;
    std::string message;
    std::string hostname;
    
    // For process-specific alerts
    std::string process_name;
    int process_id;
};

enum class AlertState {
    NORMAL,       // Below threshold
    BREACHED,     // Threshold breached, but not for long enough
    FIRING,       // Alert is actively firing
    COOLDOWN      // Alert fired recently, in cooldown period
};

struct AlertInstance {
    AlertState state;
    std::chrono::system_clock::time_point breach_start;
    std::chrono::system_clock::time_point last_fired;
    double current_value;
};

class NotificationHandler {
public:
    virtual ~NotificationHandler() = default;
    virtual bool Send(const AlertEvent& event) = 0;
    virtual std::string GetType() const = 0;
};

class AlertManager {
public:
    AlertManager();
    ~AlertManager();
    
    // Load configuration
    bool LoadConfig(const std::string& config_path);
    
    // Start/stop alert evaluation
    void Start();
    void Stop();
    bool IsRunning() const;
    
    // Check metrics against alert rules
    void EvaluateMetric(const std::string& metric_name, double value);
    void EvaluateCPUMetrics(const CPUMetrics& metrics);
    void EvaluateMemoryMetrics(const MemoryMetrics& metrics);
    
    // Register custom notification handlers
    void RegisterNotificationHandler(std::unique_ptr<NotificationHandler> handler);
    
    // Get active alerts
    std::vector<AlertEvent> GetActiveAlerts() const;
    std::map<std::string, AlertState> GetAlertStates() const;

private:
    void EvaluationLoop();
    void CheckAlert(const AlertRule& rule, double current_value);
    void FireAlert(const AlertRule& rule, double current_value);
    void SendNotifications(const AlertEvent& event, const AlertRule& rule);
    
    bool ShouldFireAlert(const AlertRule& rule, const AlertInstance& instance, 
                        double current_value) const;
    bool IsInCooldown(const AlertInstance& instance) const;
    
    AlertConfig config_;
    std::map<std::string, AlertInstance> alert_states_;
    std::map<std::string, std::unique_ptr<NotificationHandler>> notification_handlers_;
    
    // Latest metric values
    std::map<std::string, double> latest_metrics_;
    mutable std::mutex metrics_mutex_;
    
    // Control flags
    bool running_;
    std::unique_ptr<std::thread> evaluation_thread_;
};

// Built-in notification handlers
class LogNotificationHandler : public NotificationHandler {
public:
    explicit LogNotificationHandler(const std::string& log_path, size_t max_size_mb = 10);
    bool Send(const AlertEvent& event) override;
    std::string GetType() const override { return "log"; }

private:
    std::string log_path_;
    size_t max_size_bytes_;
    mutable std::mutex log_mutex_;
};

class WebhookNotificationHandler : public NotificationHandler {
public:
    WebhookNotificationHandler(const std::string& url, 
                               const std::map<std::string, std::string>& headers,
                               int timeout_seconds = 5);
    bool Send(const AlertEvent& event) override;
    std::string GetType() const override { return "webhook"; }

private:
    std::string url_;
    std::map<std::string, std::string> headers_;
    int timeout_seconds_;
};

class EmailNotificationHandler : public NotificationHandler {
public:
    EmailNotificationHandler(const std::string& smtp_host, int smtp_port,
                            const std::string& username, const std::string& password,
                            const std::string& from, const std::vector<std::string>& to);
    bool Send(const AlertEvent& event) override;
    std::string GetType() const override { return "email"; }

private:
    std::string smtp_host_;
    int smtp_port_;
    std::string username_;
    std::string password_;
    std::string from_;
    std::vector<std::string> to_;
};

} // namespace sysmon
