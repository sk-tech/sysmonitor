#include "sysmon/alert_manager.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <thread>
#include <unistd.h>

namespace sysmon {

// Helper to get hostname
static std::string GetHostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "unknown";
}

// Helper to format timestamp
static std::string FormatTimestamp(const std::chrono::system_clock::time_point& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// AlertManager implementation
AlertManager::AlertManager() : running_(false) {}

AlertManager::~AlertManager() {
    Stop();
}

bool AlertManager::LoadConfig(const std::string& config_path) {
    return config_.LoadFromFile(config_path);
}

void AlertManager::Start() {
    if (running_) return;
    
    if (!config_.GetGlobalConfig().enabled) {
        std::cout << "Alert system is disabled in configuration" << std::endl;
        return;
    }
    
    running_ = true;
    evaluation_thread_ = std::make_unique<std::thread>(&AlertManager::EvaluationLoop, this);
    std::cout << "Alert manager started" << std::endl;
}

void AlertManager::Stop() {
    if (!running_) return;
    
    running_ = false;
    if (evaluation_thread_ && evaluation_thread_->joinable()) {
        evaluation_thread_->join();
    }
    std::cout << "Alert manager stopped" << std::endl;
}

bool AlertManager::IsRunning() const {
    return running_;
}

void AlertManager::EvaluateMetric(const std::string& metric_name, double value) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    latest_metrics_[metric_name] = value;
}

void AlertManager::EvaluateCPUMetrics(const CPUMetrics& metrics) {
    EvaluateMetric("cpu.total_usage", metrics.total_usage);
    // Note: user_usage and system_usage not in current CPUMetrics struct
}

void AlertManager::EvaluateMemoryMetrics(const MemoryMetrics& metrics) {
    EvaluateMetric("memory.total_bytes", static_cast<double>(metrics.total_bytes));
    EvaluateMetric("memory.available_bytes", static_cast<double>(metrics.available_bytes));
    EvaluateMetric("memory.used_bytes", static_cast<double>(metrics.used_bytes));
    
    double percent_used = (static_cast<double>(metrics.used_bytes) / metrics.total_bytes) * 100.0;
    EvaluateMetric("memory.percent_used", percent_used);
}

void AlertManager::RegisterNotificationHandler(std::unique_ptr<NotificationHandler> handler) {
    std::string type = handler->GetType();
    notification_handlers_[type] = std::move(handler);
    std::cout << "Registered notification handler: " << type << std::endl;
}

std::vector<AlertEvent> AlertManager::GetActiveAlerts() const {
    std::vector<AlertEvent> active;
    // TODO: Maintain list of active alerts
    return active;
}

std::map<std::string, AlertState> AlertManager::GetAlertStates() const {
    std::map<std::string, AlertState> states;
    for (const auto& [name, instance] : alert_states_) {
        states[name] = instance.state;
    }
    return states;
}

void AlertManager::EvaluationLoop() {
    const auto check_interval = std::chrono::seconds(config_.GetGlobalConfig().check_interval);
    
    while (running_) {
        auto start = std::chrono::steady_clock::now();
        
        // Get current metrics snapshot
        std::map<std::string, double> metrics_snapshot;
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            metrics_snapshot = latest_metrics_;
        }
        
        // Check all system alerts
        for (const auto& rule : config_.GetSystemAlerts()) {
            auto it = metrics_snapshot.find(rule.metric);
            if (it != metrics_snapshot.end()) {
                CheckAlert(rule, it->second);
            }
        }
        
        // Sleep for remaining time
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto sleep_time = check_interval - elapsed;
        if (sleep_time.count() > 0) {
            std::this_thread::sleep_for(sleep_time);
        }
    }
}

void AlertManager::CheckAlert(const AlertRule& rule, double current_value) {
    auto now = std::chrono::system_clock::now();
    
    // Get or create alert instance
    auto& instance = alert_states_[rule.name];
    instance.current_value = current_value;
    
    // Check if in cooldown
    if (IsInCooldown(instance)) {
        return;
    }
    
    // Check threshold condition
    bool threshold_breached = false;
    switch (rule.condition) {
        case AlertCondition::ABOVE:
            threshold_breached = (current_value > rule.threshold);
            break;
        case AlertCondition::BELOW:
            threshold_breached = (current_value < rule.threshold);
            break;
        case AlertCondition::EQUALS:
            threshold_breached = (std::abs(current_value - rule.threshold) < 0.001);
            break;
    }
    
    if (threshold_breached) {
        if (instance.state == AlertState::NORMAL) {
            // First breach - start tracking
            instance.state = AlertState::BREACHED;
            instance.breach_start = now;
        }
        else if (instance.state == AlertState::BREACHED) {
            // Check if duration threshold met
            auto breach_duration = std::chrono::duration_cast<std::chrono::seconds>(
                now - instance.breach_start).count();
            
            if (breach_duration >= rule.duration_seconds) {
                // Fire the alert
                FireAlert(rule, current_value);
                instance.state = AlertState::FIRING;
                instance.last_fired = now;
            }
        }
        // If already firing, keep firing (could add logic to re-notify)
    }
    else {
        // Threshold not breached - reset to normal
        if (instance.state != AlertState::NORMAL) {
            instance.state = AlertState::NORMAL;
        }
    }
}

bool AlertManager::ShouldFireAlert(const AlertRule& rule, const AlertInstance& instance,
                                  double current_value) const {
    // Already covered in CheckAlert logic
    return true;
}

bool AlertManager::IsInCooldown(const AlertInstance& instance) const {
    if (instance.state != AlertState::FIRING && instance.state != AlertState::COOLDOWN) {
        return false;
    }
    
    auto now = std::chrono::system_clock::now();
    auto time_since_fire = std::chrono::duration_cast<std::chrono::seconds>(
        now - instance.last_fired).count();
    
    return time_since_fire < config_.GetGlobalConfig().cooldown;
}

void AlertManager::FireAlert(const AlertRule& rule, double current_value) {
    AlertEvent event;
    event.alert_name = rule.name;
    event.metric = rule.metric;
    event.current_value = current_value;
    event.threshold = rule.threshold;
    event.condition = rule.condition;
    event.severity = rule.severity;
    event.timestamp = std::chrono::system_clock::now();
    event.hostname = GetHostname();
    
    // Build message
    std::stringstream ss;
    ss << "[" << AlertConfig::SeverityToString(rule.severity) << "] ";
    ss << rule.name << ": " << rule.description << " - ";
    ss << "Current value: " << current_value << ", ";
    ss << "Threshold: " << AlertConfig::ConditionToString(rule.condition) << " " << rule.threshold;
    event.message = ss.str();
    
    std::cout << "ALERT FIRED: " << event.message << std::endl;
    
    // Send notifications
    SendNotifications(event, rule);
}

void AlertManager::SendNotifications(const AlertEvent& event, const AlertRule& rule) {
    for (const auto& channel_name : rule.notification_channels) {
        auto it = notification_handlers_.find(channel_name);
        if (it != notification_handlers_.end()) {
            bool success = it->second->Send(event);
            if (!success) {
                std::cerr << "Failed to send notification via " << channel_name << std::endl;
            }
        }
    }
}

// LogNotificationHandler implementation
LogNotificationHandler::LogNotificationHandler(const std::string& log_path, size_t max_size_mb)
    : log_path_(log_path), max_size_bytes_(max_size_mb * 1024 * 1024) {
}

bool LogNotificationHandler::Send(const AlertEvent& event) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    std::ofstream log(log_path_, std::ios::app);
    if (!log.is_open()) {
        std::cerr << "Failed to open alert log: " << log_path_ << std::endl;
        return false;
    }
    
    log << FormatTimestamp(event.timestamp) << " "
        << "[" << AlertConfig::SeverityToString(event.severity) << "] "
        << event.hostname << " - "
        << event.message << std::endl;
    
    return true;
}

// WebhookNotificationHandler implementation
WebhookNotificationHandler::WebhookNotificationHandler(
    const std::string& url,
    const std::map<std::string, std::string>& headers,
    int timeout_seconds)
    : url_(url), headers_(headers), timeout_seconds_(timeout_seconds) {
}

bool WebhookNotificationHandler::Send(const AlertEvent& event) {
    // Build JSON payload
    std::stringstream json;
    json << "{"
         << "\"alert_name\":\"" << event.alert_name << "\","
         << "\"metric\":\"" << event.metric << "\","
         << "\"current_value\":" << event.current_value << ","
         << "\"threshold\":" << event.threshold << ","
         << "\"severity\":\"" << AlertConfig::SeverityToString(event.severity) << "\","
         << "\"hostname\":\"" << event.hostname << "\","
         << "\"timestamp\":\"" << FormatTimestamp(event.timestamp) << "\","
         << "\"message\":\"" << event.message << "\""
         << "}";
    
    // Use curl command for simplicity (production should use libcurl)
    std::stringstream cmd;
    cmd << "curl -X POST -H 'Content-Type: application/json'";
    for (const auto& [key, value] : headers_) {
        cmd << " -H '" << key << ": " << value << "'";
    }
    cmd << " -d '" << json.str() << "'";
    cmd << " --max-time " << timeout_seconds_;
    cmd << " '" << url_ << "' >/dev/null 2>&1";
    
    int result = system(cmd.str().c_str());
    return (result == 0);
}

// EmailNotificationHandler implementation
EmailNotificationHandler::EmailNotificationHandler(
    const std::string& smtp_host, int smtp_port,
    const std::string& username, const std::string& password,
    const std::string& from, const std::vector<std::string>& to)
    : smtp_host_(smtp_host), smtp_port_(smtp_port),
      username_(username), password_(password),
      from_(from), to_(to) {
}

bool EmailNotificationHandler::Send(const AlertEvent& event) {
    // Build email body
    std::stringstream body;
    body << "Subject: [SysMonitor Alert] " << event.alert_name << "\n\n";
    body << "Alert: " << event.alert_name << "\n";
    body << "Severity: " << AlertConfig::SeverityToString(event.severity) << "\n";
    body << "Hostname: " << event.hostname << "\n";
    body << "Timestamp: " << FormatTimestamp(event.timestamp) << "\n";
    body << "Metric: " << event.metric << "\n";
    body << "Current Value: " << event.current_value << "\n";
    body << "Threshold: " << event.threshold << "\n\n";
    body << "Message: " << event.message << "\n";
    
    // For production, use libcurl with SMTP or a proper email library
    // For now, just log that email would be sent
    std::cout << "Would send email to: ";
    for (const auto& recipient : to_) {
        std::cout << recipient << " ";
    }
    std::cout << std::endl;
    
    return true;
}

} // namespace sysmon
