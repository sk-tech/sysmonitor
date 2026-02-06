#include "sysmon/alert_config.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace sysmon {

AlertCondition AlertConfig::ParseCondition(const std::string& str) {
    if (str == "above") return AlertCondition::ABOVE;
    if (str == "below") return AlertCondition::BELOW;
    if (str == "equals") return AlertCondition::EQUALS;
    return AlertCondition::ABOVE;
}

AlertSeverity AlertConfig::ParseSeverity(const std::string& str) {
    if (str == "info") return AlertSeverity::INFO;
    if (str == "warning") return AlertSeverity::WARNING;
    if (str == "critical") return AlertSeverity::CRITICAL;
    return AlertSeverity::INFO;
}

std::string AlertConfig::ConditionToString(AlertCondition condition) {
    switch (condition) {
        case AlertCondition::ABOVE: return "above";
        case AlertCondition::BELOW: return "below";
        case AlertCondition::EQUALS: return "equals";
    }
    return "above";
}

std::string AlertConfig::SeverityToString(AlertSeverity severity) {
    switch (severity) {
        case AlertSeverity::INFO: return "info";
        case AlertSeverity::WARNING: return "warning";
        case AlertSeverity::CRITICAL: return "critical";
    }
    return "info";
}

// Simple YAML parser for basic configuration
// TODO: Replace with yaml-cpp for production use
static std::string Trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

static std::pair<std::string, std::string> ParseKeyValue(const std::string& line) {
    size_t pos = line.find(':');
    if (pos == std::string::npos) return {"", ""};
    std::string key = Trim(line.substr(0, pos));
    std::string value = Trim(line.substr(pos + 1));
    return {key, value};
}

bool AlertConfig::LoadFromFile(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << config_path << std::endl;
        return false;
    }
    
    // Set defaults
    global_config_.check_interval = 5;
    global_config_.cooldown = 300;
    global_config_.enabled = true;
    
    std::string line;
    std::string current_section;
    AlertRule current_rule;
    bool in_rule = false;
    
    while (std::getline(file, line)) {
        std::string trimmed = Trim(line);
        
        // Skip comments and empty lines
        if (trimmed.empty() || trimmed[0] == '#') continue;
        
        // Count leading spaces for indent detection
        size_t indent = line.find_first_not_of(' ');
        if (indent == std::string::npos) indent = 0;
        
        // Check for section headers (no indent)
        if (indent == 0) {
            if (trimmed.find("global:") == 0) {
                current_section = "global";
                continue;
            }
            if (trimmed.find("alerts:") == 0) {
                current_section = "alerts";
                continue;
            }
            if (trimmed.find("notifications:") == 0) {
                current_section = "notifications";
                continue;
            }
            if (trimmed.find("process_alerts:") == 0) {
                current_section = "process_alerts";
                continue;
            }
        }
        
        // Check for new alert rule (starts with "- name:")
        if (trimmed.find("- name:") == 0) {
            if (in_rule && !current_rule.name.empty()) {
                if (current_section == "process_alerts") {
                    current_rule.is_process_alert = true;
                    process_alerts_.push_back(current_rule);
                } else {
                    system_alerts_.push_back(current_rule);
                }
            }
            current_rule = AlertRule();
            current_rule.is_process_alert = false;
            current_rule.condition = AlertCondition::ABOVE;
            current_rule.severity = AlertSeverity::INFO;
            current_rule.threshold = 0.0;
            current_rule.duration_seconds = 0;
            in_rule = true;
            
            auto [key, value] = ParseKeyValue(trimmed.substr(2)); // Skip "- "
            current_rule.name = value;
            continue;
        }
        
        // Parse key-value pairs
        if (trimmed.find(':') != std::string::npos) {
            auto [key, value] = ParseKeyValue(trimmed);
            if (key.empty()) continue;
            
            if (current_section == "global") {
                if (key == "check_interval") global_config_.check_interval = std::stoi(value);
                else if (key == "cooldown") global_config_.cooldown = std::stoi(value);
                else if (key == "enabled") global_config_.enabled = (value == "true");
            }
            else if (in_rule && (current_section == "alerts" || current_section == "process_alerts")) {
                if (key == "description") current_rule.description = value;
                else if (key == "metric") current_rule.metric = value;
                else if (key == "condition") current_rule.condition = ParseCondition(value);
                else if (key == "threshold") current_rule.threshold = std::stod(value);
                else if (key == "duration") current_rule.duration_seconds = std::stoi(value);
                else if (key == "severity") current_rule.severity = ParseSeverity(value);
                else if (key == "process_name") current_rule.process_name = value;
            }
        }
    }
    
    // Add last rule if exists
    if (in_rule && !current_rule.name.empty()) {
        if (current_rule.is_process_alert) {
            process_alerts_.push_back(current_rule);
        } else {
            system_alerts_.push_back(current_rule);
        }
    }
    
    std::cout << "Loaded " << system_alerts_.size() << " alert rules from " << config_path << std::endl;
    return true;
}

} // namespace sysmon
