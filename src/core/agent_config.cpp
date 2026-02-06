#include "sysmon/agent_config.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unistd.h>
#include <limits.h>

namespace sysmon {

// Simple YAML parser (handles basic key: value pairs)
// For production, consider using yaml-cpp library
static std::map<std::string, std::string> ParseSimpleYaml(const std::string& content) {
    std::map<std::string, std::string> result;
    std::istringstream stream(content);
    std::string line;
    
    while (std::getline(stream, line)) {
        // Skip comments and empty lines
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        if (line.empty()) {
            continue;
        }
        
        // Parse key: value
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            
            // Trim key and value
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // Remove quotes from value
            if (value.size() >= 2 && 
                ((value.front() == '"' && value.back() == '"') ||
                 (value.front() == '\'' && value.back() == '\''))) {
                value = value.substr(1, value.size() - 2);
            }
            
            result[key] = value;
        }
    }
    
    return result;
}

bool AgentConfigParser::LoadFromFile(const std::string& config_path) {
    errors_.clear();
    
    // Read file
    std::ifstream file(config_path);
    if (!file.is_open()) {
        errors_.push_back("Failed to open config file: " + config_path);
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    return ParseYaml(content);
}

bool AgentConfigParser::ParseYaml(const std::string& content) {
    auto kv_map = ParseSimpleYaml(content);
    
    // Set defaults first
    SetDefaults();
    
    // Parse mode
    if (kv_map.count("mode")) {
        config_.mode = ParseMode(kv_map["mode"]);
    }
    
    // Parse discovery settings
    if (kv_map.count("discovery_method")) {
        config_.discovery_method = ParseDiscoveryMethod(kv_map["discovery_method"]);
    }
    
    if (kv_map.count("consul_addr")) {
        config_.consul_addr = kv_map["consul_addr"];
    }
    
    if (kv_map.count("consul_service_tag")) {
        config_.consul_service_tag = kv_map["consul_service_tag"];
    }
    
    if (kv_map.count("discovery_timeout_seconds")) {
        try {
            config_.discovery_timeout_seconds = std::stod(kv_map["discovery_timeout_seconds"]);
        } catch (...) {
            errors_.push_back("Invalid discovery_timeout_seconds value");
        }
    }
    
    // Parse aggregator settings
    if (kv_map.count("aggregator_url")) {
        config_.aggregator_url = kv_map["aggregator_url"];
    }
    
    if (kv_map.count("auth_token")) {
        config_.auth_token = kv_map["auth_token"];
    }
    
    if (kv_map.count("push_interval_ms")) {
        try {
            config_.push_interval_ms = std::stoi(kv_map["push_interval_ms"]);
        } catch (...) {
            errors_.push_back("Invalid push_interval_ms value");
        }
    }
    
    if (kv_map.count("max_queue_size")) {
        try {
            config_.max_queue_size = std::stoi(kv_map["max_queue_size"]);
        } catch (...) {
            errors_.push_back("Invalid max_queue_size value");
        }
    }
    
    if (kv_map.count("retry_max_attempts")) {
        try {
            config_.retry_max_attempts = std::stoi(kv_map["retry_max_attempts"]);
        } catch (...) {
            errors_.push_back("Invalid retry_max_attempts value");
        }
    }
    
    if (kv_map.count("http_timeout_ms")) {
        try {
            config_.http_timeout_ms = std::stoi(kv_map["http_timeout_ms"]);
        } catch (...) {
            errors_.push_back("Invalid http_timeout_ms value");
        }
    }
    
    // Parse TLS settings
    if (kv_map.count("tls_enabled")) {
        std::string tls_str = kv_map["tls_enabled"];
        std::transform(tls_str.begin(), tls_str.end(), tls_str.begin(), ::tolower);
        config_.tls_enabled = (tls_str == "true" || tls_str == "yes" || tls_str == "1");
    }
    
    // Parse hostname
    if (kv_map.count("hostname")) {
        config_.hostname = kv_map["hostname"];
    } else {
        config_.hostname = GetHostname();
    }
    
    // Parse tags (simple format: tag1=value1, tag2=value2)
    if (kv_map.count("tags")) {
        std::string tags_str = kv_map["tags"];
        std::istringstream tags_stream(tags_str);
        std::string tag_pair;
        
        while (std::getline(tags_stream, tag_pair, ',')) {
            size_t eq_pos = tag_pair.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = tag_pair.substr(0, eq_pos);
                std::string value = tag_pair.substr(eq_pos + 1);
                
                // Trim
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                
                config_.host_tags[key] = value;
            }
        }
    }
    
    return Validate();
}

void AgentConfigParser::SetDefaults() {
    config_.mode = AgentMode::LOCAL;
    config_.discovery_method = DiscoveryMethod::NONE;
    config_.consul_addr = "http://localhost:8500";
    config_.discovery_timeout_seconds = 5.0;
    config_.push_interval_ms = 5000;
    config_.max_queue_size = 1000;
    config_.retry_max_attempts = 3;
    config_.retry_base_delay_ms = 1000;
    config_.http_timeout_ms = 10000;
    config_.connection_timeout_ms = 5000;
    config_.hostname = GetHostname();
    config_.tls_enabled = false;
    config_.tls_verify_peer = true;
}

bool AgentConfigParser::Validate() const {
    if (config_.mode == AgentMode::DISTRIBUTED || config_.mode == AgentMode::HYBRID) {
        // If using discovery, aggregator_url is optional
        if (config_.discovery_method == DiscoveryMethod::NONE && config_.aggregator_url.empty()) {
            const_cast<AgentConfigParser*>(this)->errors_.push_back(
                "aggregator_url is required for distributed mode without discovery");
            return false;
        }
        
        if (config_.auth_token.empty()) {
            const_cast<AgentConfigParser*>(this)->errors_.push_back(
                "auth_token is required for distributed mode");
            return false;
        }
    }
    
    if (config_.push_interval_ms < 100) {
        const_cast<AgentConfigParser*>(this)->errors_.push_back(
            "push_interval_ms must be at least 100ms");
        return false;
    }
    
    return true;
}

AgentMode AgentConfigParser::ParseMode(const std::string& mode_str) {
    std::string lower = mode_str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "local") {
        return AgentMode::LOCAL;
    } else if (lower == "distributed") {
        return AgentMode::DISTRIBUTED;
    } else if (lower == "hybrid") {
        return AgentMode::HYBRID;
    }
    
    return AgentMode::LOCAL;
}

DiscoveryMethod AgentConfigParser::ParseDiscoveryMethod(const std::string& method_str) {
    std::string lower = method_str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "mdns" || lower == "bonjour") {
        return DiscoveryMethod::MDNS;
    } else if (lower == "consul") {
        return DiscoveryMethod::CONSUL;
    } else if (lower == "static") {
        return DiscoveryMethod::STATIC;
    } else if (lower == "none") {
        return DiscoveryMethod::NONE;
    }
    
    return DiscoveryMethod::NONE;
}

std::string AgentConfigParser::ModeToString(AgentMode mode) {
    switch (mode) {
        case AgentMode::LOCAL: return "local";
        case AgentMode::DISTRIBUTED: return "distributed";
        case AgentMode::HYBRID: return "hybrid";
        default: return "unknown";
    }
}

std::string AgentConfigParser::DiscoveryMethodToString(DiscoveryMethod method) {
    switch (method) {
        case DiscoveryMethod::NONE: return "none";
        case DiscoveryMethod::MDNS: return "mdns";
        case DiscoveryMethod::CONSUL: return "consul";
        case DiscoveryMethod::STATIC: return "static";
        default: return "unknown";
    }
}

std::string AgentConfigParser::GetHostname() {
    char hostname[HOST_NAME_MAX + 1];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "unknown-host";
}

} // namespace sysmon
