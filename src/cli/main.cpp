#include "sysmon/platform_interface.hpp"
#include "sysmon/metrics_storage.hpp"
#include "sysmon/alert_manager.hpp"
#include "sysmon/agent_config.hpp"
#include "../utils/http_client.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <chrono>

// Simple JSON parser helper (for parsing aggregator responses)
std::string json_get_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();
    size_t end = json.find("\"", pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

int json_get_int(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.length();
    size_t end = json.find_first_of(",}", pos);
    if (end == std::string::npos) return 0;
    try {
        return std::stoi(json.substr(pos, end - pos));
    } catch (...) {
        return 0;
    }
}

double json_get_double(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0.0;
    pos += search.length();
    size_t end = json.find_first_of(",}", pos);
    if (end == std::string::npos) return 0.0;
    try {
        return std::stod(json.substr(pos, end - pos));
    } catch (...) {
        return 0.0;
    }
}

void print_system_info() {
    auto system_metrics = sysmon::CreateSystemMetrics();
    auto system_info = system_metrics->GetSystemInfo();
    
    std::cout << "System Information" << std::endl;
    std::cout << "==================" << std::endl;
    std::cout << "OS: " << system_info.os_name << " " << system_info.os_version << std::endl;
    std::cout << "Kernel: " << system_info.kernel_version << std::endl;
    std::cout << "Hostname: " << system_info.hostname << std::endl;
    std::cout << "Architecture: " << system_info.architecture << std::endl;
    std::cout << "Uptime: " << (system_info.uptime_seconds / 3600) << " hours" << std::endl;
}

void print_cpu_info() {
    auto system_metrics = sysmon::CreateSystemMetrics();
    auto cpu = system_metrics->GetCPUMetrics();
    
    std::cout << "\nCPU Metrics" << std::endl;
    std::cout << "===========" << std::endl;
    std::cout << "Cores: " << cpu.num_cores << std::endl;
    std::cout << "Usage: " << std::fixed << std::setprecision(2) << cpu.total_usage << "%" << std::endl;
    std::cout << "Load Average: " << cpu.load_average_1m << ", "
              << cpu.load_average_5m << ", " << cpu.load_average_15m << std::endl;
}

void print_memory_info() {
    auto system_metrics = sysmon::CreateSystemMetrics();
    auto mem = system_metrics->GetMemoryMetrics();
    
    std::cout << "\nMemory Metrics" << std::endl;
    std::cout << "==============" << std::endl;
    std::cout << "Total: " << (mem.total_bytes / 1024 / 1024) << " MB" << std::endl;
    std::cout << "Used: " << (mem.used_bytes / 1024 / 1024) << " MB" << std::endl;
    std::cout << "Free: " << (mem.free_bytes / 1024 / 1024) << " MB" << std::endl;
    std::cout << "Available: " << (mem.available_bytes / 1024 / 1024) << " MB" << std::endl;
    std::cout << "Usage: " << std::fixed << std::setprecision(2) << mem.usage_percent << "%" << std::endl;
}

void print_process_list() {
    auto process_monitor = sysmon::CreateProcessMonitor();
    auto processes = process_monitor->GetProcessList();
    
    std::cout << "\nTop Processes (by memory)" << std::endl;
    std::cout << "=========================" << std::endl;
    std::cout << std::left << std::setw(8) << "PID"
              << std::setw(30) << "Name"
              << std::setw(12) << "Memory (MB)"
              << std::setw(10) << "Threads"
              << std::setw(12) << "State" << std::endl;
    std::cout << std::string(72, '-') << std::endl;
    
    // Sort by memory usage
    std::sort(processes.begin(), processes.end(),
              [](const sysmon::ProcessInfo& a, const sysmon::ProcessInfo& b) {
                  return a.memory_bytes > b.memory_bytes;
              });
    
    // Print top 10
    int count = 0;
    for (const auto& proc : processes) {
        if (count++ >= 10) break;
        
        std::cout << std::left << std::setw(8) << proc.pid
                  << std::setw(30) << proc.name.substr(0, 29)
                  << std::setw(12) << (proc.memory_bytes / 1024 / 1024)
                  << std::setw(10) << proc.num_threads
                  << std::setw(12) << proc.state << std::endl;
    }
}

void print_history(const std::string& metric_type, const std::string& duration, int limit) {
    // Find database path
    std::string db_path;
    const char* home = std::getenv("HOME");
    if (home) {
        db_path = std::filesystem::path(home) / ".sysmon" / "data.db";
    } else {
        db_path = "sysmon_data.db";
    }
    
    // Check if database exists
    if (!std::filesystem::exists(db_path)) {
        std::cerr << "Error: Database not found at " << db_path << std::endl;
        std::cerr << "Run 'sysmond' first to collect data." << std::endl;
        return;
    }
    
    // Parse duration (e.g., "1h", "30m", "24h")
    int seconds = 3600; // Default 1 hour
    if (!duration.empty()) {
        char unit = duration.back();
        int value = std::stoi(duration.substr(0, duration.length() - 1));
        
        if (unit == 'h') {
            seconds = value * 3600;
        } else if (unit == 'm') {
            seconds = value * 60;
        } else if (unit == 'd') {
            seconds = value * 86400;
        }
    }
    
    // Calculate time range
    auto now = std::chrono::system_clock::now();
    int64_t end_ts = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()
    ).count();
    int64_t start_ts = end_ts - seconds;
    
    // Open database and query
    try {
        sysmon::StorageConfig config;
        config.db_path = db_path;
        sysmon::MetricsStorage storage(config);
        
        auto results = storage.QueryRange(metric_type, start_ts, end_ts, limit);
        
        if (results.empty()) {
            std::cout << "No data found for " << metric_type << std::endl;
            return;
        }
        
        std::cout << "\nMetric History: " << metric_type << std::endl;
        std::cout << "Time Range: Last " << duration << " (" << results.size() << " data points)" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << std::left << std::setw(20) << "Timestamp"
                  << std::setw(40) << "Tags"
                  << std::setw(15) << "Value" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        for (const auto& metric : results) {
            // Convert timestamp to readable format
            auto time_point = std::chrono::system_clock::from_time_t(metric.timestamp);
            auto time_t_val = std::chrono::system_clock::to_time_t(time_point);
            char time_str[20];
            std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t_val));
            
            std::cout << std::left << std::setw(20) << time_str
                      << std::setw(40) << (metric.tags.empty() ? "-" : metric.tags.substr(0, 39))
                      << std::fixed << std::setprecision(2) << std::setw(15) << metric.value
                      << std::endl;
        }
        
        // Calculate statistics
        double sum = 0, min_val = results[0].value, max_val = results[0].value;
        for (const auto& m : results) {
            sum += m.value;
            if (m.value < min_val) min_val = m.value;
            if (m.value > max_val) max_val = m.value;
        }
        double avg = sum / results.size();
        
        std::cout << std::string(80, '-') << std::endl;
        std::cout << "Statistics: Avg=" << std::fixed << std::setprecision(2) << avg
                  << ", Min=" << min_val << ", Max=" << max_val << std::endl;
                  
    } catch (const std::exception& e) {
        std::cerr << "Error querying database: " << e.what() << std::endl;
    }
}

void print_alert_status() {
    std::cout << "\nAlert System Status" << std::endl;
    std::cout << "===================" << std::endl;
    
    // Check if alerts.yaml exists
    std::filesystem::path config_path;
    const char* home = std::getenv("HOME");
    if (home) {
        config_path = std::filesystem::path(home) / ".sysmon" / "alerts.yaml";
    }
    
    if (!std::filesystem::exists(config_path)) {
        std::cout << "No alert configuration found at: " << config_path << std::endl;
        std::cout << "Copy config/alerts.yaml.example to " << config_path << " to enable alerts" << std::endl;
        return;
    }
    
    sysmon::AlertConfig alert_config;
    if (!alert_config.LoadFromFile(config_path.string())) {
        std::cerr << "Failed to load alert configuration" << std::endl;
        return;
    }
    
    auto global = alert_config.GetGlobalConfig();
    std::cout << "Configuration: " << config_path << std::endl;
    std::cout << "Status: " << (global.enabled ? "Enabled" : "Disabled") << std::endl;
    std::cout << "Check Interval: " << global.check_interval << " seconds" << std::endl;
    std::cout << "Cooldown: " << global.cooldown << " seconds" << std::endl;
    std::cout << "\nConfigured Alerts (" << alert_config.GetSystemAlerts().size() << "):" << std::endl;
    std::cout << std::string(80, '-') << std::endl;
    
    for (const auto& rule : alert_config.GetSystemAlerts()) {
        std::cout << "• " << rule.name << " [" 
                  << sysmon::AlertConfig::SeverityToString(rule.severity) << "]" << std::endl;
        std::cout << "  Metric: " << rule.metric << std::endl;
        std::cout << "  Condition: " << sysmon::AlertConfig::ConditionToString(rule.condition) 
                  << " " << rule.threshold << std::endl;
        std::cout << "  Duration: " << rule.duration_seconds << " seconds" << std::endl;
        std::cout << "  Description: " << rule.description << std::endl;
        std::cout << std::endl;
    }
    
    // Check alert log
    std::filesystem::path log_path;
    if (home) {
        log_path = std::filesystem::path(home) / ".sysmon" / "alerts.log";
        if (std::filesystem::exists(log_path)) {
            auto size = std::filesystem::file_size(log_path);
            std::cout << "Alert Log: " << log_path << " (" << (size / 1024) << " KB)" << std::endl;
        }
    }
}

void test_alert_config(const std::string& config_file) {
    std::cout << "\nTesting Alert Configuration" << std::endl;
    std::cout << "============================" << std::endl;
    
    sysmon::AlertManager alert_manager;
    if (!alert_manager.LoadConfig(config_file)) {
        std::cerr << "Failed to load configuration from: " << config_file << std::endl;
        return;
    }
    
    std::cout << "✓ Configuration loaded successfully" << std::endl;
    
    // Get current metrics
    auto system_metrics = sysmon::CreateSystemMetrics();
    auto cpu = system_metrics->GetCPUMetrics();
    auto mem = system_metrics->GetMemoryMetrics();
    
    std::cout << "\nCurrent Metrics:" << std::endl;
    std::cout << "  CPU Usage: " << cpu.total_usage << "%" << std::endl;
    std::cout << "  Memory Usage: " << mem.usage_percent << "%" << std::endl;
    std::cout << "  Available Memory: " << (mem.available_bytes / 1024 / 1024) << " MB" << std::endl;
    
    // Test evaluation (dry run)
    alert_manager.EvaluateCPUMetrics(cpu);
    alert_manager.EvaluateMemoryMetrics(mem);
    
    std::cout << "\n✓ Alert evaluation test complete" << std::endl;
    std::cout << "Note: Alerts would fire after sustained threshold breaches" << std::endl;
}

// ===== DISTRIBUTED MONITORING COMMANDS =====

std::string get_config_path() {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / ".sysmon" / "agent.yaml";
    }
    return "agent.yaml";
}

std::string get_aggregator_url() {
    std::string config_path = get_config_path();
    if (!std::filesystem::exists(config_path)) {
        return "";
    }
    
    sysmon::AgentConfigParser parser;
    if (!parser.LoadFromFile(config_path)) {
        return "";
    }
    
    return parser.GetConfig().aggregator_url;
}

void print_hosts_list() {
    std::cout << "\nRegistered Hosts" << std::endl;
    std::cout << "================" << std::endl;
    
    std::string aggregator_url = get_aggregator_url();
    if (aggregator_url.empty()) {
        std::cerr << "Error: No aggregator configured" << std::endl;
        std::cerr << "Configure distributed mode with: sysmon config set mode distributed" << std::endl;
        return;
    }
    
    sysmon::HttpClient client;
    auto response = client.Get(aggregator_url + "/api/hosts");
    
    if (!response.success) {
        std::cerr << "Error: Failed to connect to aggregator at " << aggregator_url << std::endl;
        std::cerr << "Details: " << response.error << std::endl;
        std::cerr << "Make sure aggregator is running: ./scripts/start-aggregator.sh" << std::endl;
        return;
    }
    
    // Parse JSON response (simple parsing for host list)
    std::string json = response.body;
    
    // Count hosts
    size_t host_count = 0;
    size_t pos = 0;
    while ((pos = json.find("\"hostname\":", pos)) != std::string::npos) {
        host_count++;
        pos++;
    }
    
    if (host_count == 0) {
        std::cout << "No hosts registered yet" << std::endl;
        std::cout << "Start an agent to register a host" << std::endl;
        return;
    }
    
    std::cout << "Total hosts: " << host_count << std::endl;
    std::cout << std::string(80, '-') << std::endl;
    std::cout << std::left << std::setw(25) << "Hostname"
              << std::setw(15) << "Platform"
              << std::setw(15) << "Version"
              << std::setw(15) << "Status" << std::endl;
    std::cout << std::string(80, '-') << std::endl;
    
    // Parse each host entry
    pos = 0;
    while ((pos = json.find("\"hostname\":\"", pos)) != std::string::npos) {
        size_t start = pos;
        size_t end = json.find("}", start);
        if (end == std::string::npos) break;
        
        std::string host_json = json.substr(start, end - start + 1);
        
        std::string hostname = json_get_string(host_json, "hostname");
        std::string platform = json_get_string(host_json, "platform");
        std::string version = json_get_string(host_json, "version");
        int last_seen = json_get_int(host_json, "last_seen_seconds_ago");
        
        std::string status = (last_seen < 30) ? "✓ Online" : "✗ Offline";
        
        std::cout << std::left << std::setw(25) << hostname.substr(0, 24)
                  << std::setw(15) << platform
                  << std::setw(15) << version
                  << std::setw(15) << status;
        
        if (last_seen >= 30) {
            std::cout << " (last seen " << last_seen << "s ago)";
        }
        std::cout << std::endl;
        
        pos = end;
    }
    
    std::cout << std::string(80, '-') << std::endl;
}

void print_host_details(const std::string& hostname) {
    std::cout << "\nHost Details: " << hostname << std::endl;
    std::cout << "=============" << std::string(hostname.length(), '=') << std::endl;
    
    std::string aggregator_url = get_aggregator_url();
    if (aggregator_url.empty()) {
        std::cerr << "Error: No aggregator configured" << std::endl;
        return;
    }
    
    sysmon::HttpClient client;
    auto response = client.Get(aggregator_url + "/api/hosts/" + hostname);
    
    if (!response.success) {
        std::cerr << "Error: Host not found or aggregator unreachable" << std::endl;
        return;
    }
    
    std::string json = response.body;
    
    std::cout << "\nGeneral Information:" << std::endl;
    std::cout << "  Hostname: " << json_get_string(json, "hostname") << std::endl;
    std::cout << "  Platform: " << json_get_string(json, "platform") << std::endl;
    std::cout << "  Version: " << json_get_string(json, "version") << std::endl;
    
    int last_seen = json_get_int(json, "last_seen_seconds_ago");
    std::cout << "  Last Seen: " << last_seen << " seconds ago";
    if (last_seen < 30) {
        std::cout << " (Online)";
    } else {
        std::cout << " (Offline)";
    }
    std::cout << std::endl;
    
    // Show tags if present
    size_t tags_start = json.find("\"tags\":{");
    if (tags_start != std::string::npos) {
        std::cout << "\nTags:" << std::endl;
        size_t tags_end = json.find("}", tags_start);
        std::string tags_section = json.substr(tags_start, tags_end - tags_start);
        
        size_t pos = 0;
        while ((pos = tags_section.find("\"", pos + 1)) != std::string::npos) {
            size_t key_end = tags_section.find("\"", pos + 1);
            if (key_end == std::string::npos) break;
            std::string key = tags_section.substr(pos + 1, key_end - pos - 1);
            
            size_t val_start = tags_section.find("\"", key_end + 1);
            if (val_start == std::string::npos) break;
            size_t val_end = tags_section.find("\"", val_start + 1);
            if (val_end == std::string::npos) break;
            std::string value = tags_section.substr(val_start + 1, val_end - val_start - 1);
            
            std::cout << "  " << key << ": " << value << std::endl;
            pos = val_end;
        }
    }
    
    // Get latest metrics
    auto metrics_resp = client.Get(aggregator_url + "/api/hosts/" + hostname + "/metrics/latest");
    if (metrics_resp.success) {
        std::cout << "\nLatest Metrics:" << std::endl;
        
        double cpu_usage = json_get_double(metrics_resp.body, "cpu_usage");
        double mem_usage = json_get_double(metrics_resp.body, "memory_usage");
        double load_1m = json_get_double(metrics_resp.body, "load_average_1m");
        
        if (cpu_usage > 0) {
            std::cout << "  CPU Usage: " << std::fixed << std::setprecision(2) << cpu_usage << "%" << std::endl;
        }
        if (mem_usage > 0) {
            std::cout << "  Memory Usage: " << std::fixed << std::setprecision(2) << mem_usage << "%" << std::endl;
        }
        if (load_1m > 0) {
            std::cout << "  Load Average (1m): " << std::fixed << std::setprecision(2) << load_1m << std::endl;
        }
    }
    
    std::cout << std::endl;
}

void print_hosts_compare(const std::string& host1, const std::string& host2) {
    std::cout << "\nComparing Hosts" << std::endl;
    std::cout << "===============" << std::endl;
    
    std::string aggregator_url = get_aggregator_url();
    if (aggregator_url.empty()) {
        std::cerr << "Error: No aggregator configured" << std::endl;
        return;
    }
    
    sysmon::HttpClient client;
    
    // Fetch metrics for both hosts
    auto resp1 = client.Get(aggregator_url + "/api/hosts/" + host1 + "/metrics/latest");
    auto resp2 = client.Get(aggregator_url + "/api/hosts/" + host2 + "/metrics/latest");
    
    if (!resp1.success || !resp2.success) {
        std::cerr << "Error: Failed to fetch metrics for one or both hosts" << std::endl;
        return;
    }
    
    std::cout << "\n" << std::left << std::setw(20) << "Metric"
              << std::setw(20) << host1
              << std::setw(20) << host2
              << std::setw(15) << "Difference" << std::endl;
    std::cout << std::string(75, '-') << std::endl;
    
    // Compare CPU usage
    double cpu1 = json_get_double(resp1.body, "cpu_usage");
    double cpu2 = json_get_double(resp2.body, "cpu_usage");
    if (cpu1 > 0 && cpu2 > 0) {
        std::cout << std::left << std::setw(20) << "CPU Usage (%)"
                  << std::setw(20) << std::fixed << std::setprecision(2) << cpu1
                  << std::setw(20) << cpu2
                  << std::setw(15) << (cpu1 - cpu2) << std::endl;
    }
    
    // Compare Memory usage
    double mem1 = json_get_double(resp1.body, "memory_usage");
    double mem2 = json_get_double(resp2.body, "memory_usage");
    if (mem1 > 0 && mem2 > 0) {
        std::cout << std::left << std::setw(20) << "Memory Usage (%)"
                  << std::setw(20) << std::fixed << std::setprecision(2) << mem1
                  << std::setw(20) << mem2
                  << std::setw(15) << (mem1 - mem2) << std::endl;
    }
    
    // Compare Load average
    double load1 = json_get_double(resp1.body, "load_average_1m");
    double load2 = json_get_double(resp2.body, "load_average_1m");
    if (load1 > 0 && load2 > 0) {
        std::cout << std::left << std::setw(20) << "Load Avg (1m)"
                  << std::setw(20) << std::fixed << std::setprecision(2) << load1
                  << std::setw(20) << load2
                  << std::setw(15) << (load1 - load2) << std::endl;
    }
    
    std::cout << std::string(75, '-') << std::endl;
}

void print_config_show() {
    std::cout << "\nCurrent Configuration" << std::endl;
    std::cout << "=====================" << std::endl;
    
    std::string config_path = get_config_path();
    
    if (!std::filesystem::exists(config_path)) {
        std::cout << "No configuration file found at: " << config_path << std::endl;
        std::cout << "Using default local mode" << std::endl;
        std::cout << "\nTo enable distributed monitoring:" << std::endl;
        std::cout << "  1. Copy config/agent.yaml.example to ~/.sysmon/agent.yaml" << std::endl;
        std::cout << "  2. Edit the file to set aggregator_url" << std::endl;
        std::cout << "  3. Run: sysmon config set mode distributed" << std::endl;
        return;
    }
    
    sysmon::AgentConfigParser parser;
    if (!parser.LoadFromFile(config_path)) {
        std::cerr << "Error: Failed to parse configuration file" << std::endl;
        for (const auto& error : parser.GetErrors()) {
            std::cerr << "  - " << error << std::endl;
        }
        return;
    }
    
    const auto& config = parser.GetConfig();
    
    std::cout << "Config File: " << config_path << std::endl;
    std::cout << "\nMode: " << sysmon::AgentConfigParser::ModeToString(config.mode) << std::endl;
    std::cout << "Hostname: " << config.hostname << std::endl;
    
    if (config.mode == sysmon::AgentMode::DISTRIBUTED || config.mode == sysmon::AgentMode::HYBRID) {
        std::cout << "\nAggregator Settings:" << std::endl;
        std::cout << "  URL: " << config.aggregator_url << std::endl;
        std::cout << "  Push Interval: " << config.push_interval_ms << " ms" << std::endl;
        std::cout << "  Max Queue Size: " << config.max_queue_size << std::endl;
        std::cout << "  HTTP Timeout: " << config.http_timeout_ms << " ms" << std::endl;
    }
    
    if (!config.host_tags.empty()) {
        std::cout << "\nHost Tags:" << std::endl;
        for (const auto& tag : config.host_tags) {
            std::cout << "  " << tag.first << ": " << tag.second << std::endl;
        }
    }
    
    std::cout << std::endl;
}

void config_set_mode(const std::string& mode_str) {
    std::string config_path = get_config_path();
    
    // Validate mode
    if (mode_str != "local" && mode_str != "distributed" && mode_str != "hybrid") {
        std::cerr << "Error: Invalid mode. Must be: local, distributed, or hybrid" << std::endl;
        return;
    }
    
    // Check if config file exists
    if (!std::filesystem::exists(config_path)) {
        std::cerr << "Error: Config file not found at: " << config_path << std::endl;
        std::cerr << "Create it first by copying config/agent.yaml.example" << std::endl;
        return;
    }
    
    // Read current config file
    std::ifstream infile(config_path);
    if (!infile) {
        std::cerr << "Error: Cannot read config file" << std::endl;
        return;
    }
    
    std::string content((std::istreambuf_iterator<char>(infile)),
                        std::istreambuf_iterator<char>());
    infile.close();
    
    // Update mode line
    size_t mode_pos = content.find("mode:");
    if (mode_pos != std::string::npos) {
        size_t line_end = content.find('\n', mode_pos);
        if (line_end != std::string::npos) {
            content.replace(mode_pos, line_end - mode_pos, "mode: " + mode_str);
        }
    } else {
        // Add mode line at the beginning
        content = "mode: " + mode_str + "\n" + content;
    }
    
    // Write back
    std::ofstream outfile(config_path);
    if (!outfile) {
        std::cerr << "Error: Cannot write config file" << std::endl;
        return;
    }
    
    outfile << content;
    outfile.close();
    
    std::cout << "✓ Configuration updated" << std::endl;
    std::cout << "Mode set to: " << mode_str << std::endl;
    std::cout << "\nRestart sysmond for changes to take effect:" << std::endl;
    std::cout << "  ./scripts/stop.sh && ./scripts/start.sh" << std::endl;
}

void print_usage() {
    std::cout << "SysMonitor CLI v0.5.0" << std::endl;
    std::cout << "\nUsage: sysmon <command> [options]" << std::endl;
    
    std::cout << "\n=== Local Monitoring ===" << std::endl;
    std::cout << "  info      Show system information" << std::endl;
    std::cout << "  cpu       Show CPU metrics" << std::endl;
    std::cout << "  memory    Show memory metrics" << std::endl;
    std::cout << "  top       Show top processes" << std::endl;
    std::cout << "  all       Show all metrics" << std::endl;
    
    std::cout << "\n=== Historical Data ===" << std::endl;
    std::cout << "  history <metric> [duration] [limit]" << std::endl;
    std::cout << "            Query historical metrics" << std::endl;
    std::cout << "            Examples:" << std::endl;
    std::cout << "              sysmon history cpu.total_usage 1h 20" << std::endl;
    std::cout << "              sysmon history memory.usage_percent 24h" << std::endl;
    std::cout << "            Duration: 1h, 30m, 24h, 7d (default: 1h)" << std::endl;
    
    std::cout << "\n=== Alerting ===" << std::endl;
    std::cout << "  alerts" << std::endl;
    std::cout << "            Show alert status and configuration" << std::endl;
    std::cout << "  test-alert <config_file>" << std::endl;
    std::cout << "            Test alert configuration with current metrics" << std::endl;
    
    std::cout << "\n=== Distributed Monitoring (Week 5) ===" << std::endl;
    std::cout << "  hosts list" << std::endl;
    std::cout << "            List all registered hosts" << std::endl;
    std::cout << "  hosts show <hostname>" << std::endl;
    std::cout << "            Show detailed host information" << std::endl;
    std::cout << "  hosts compare <host1> <host2>" << std::endl;
    std::cout << "            Compare metrics between two hosts" << std::endl;
    std::cout << "  config show" << std::endl;
    std::cout << "            Display current agent configuration" << std::endl;
    std::cout << "  config set mode <local|distributed|hybrid>" << std::endl;
    std::cout << "            Switch monitoring mode" << std::endl;
    
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  sysmon hosts list" << std::endl;
    std::cout << "  sysmon hosts show server-01" << std::endl;
    std::cout << "  sysmon hosts compare web-01 web-02" << std::endl;
    std::cout << "  sysmon config show" << std::endl;
    std::cout << "  sysmon config set mode distributed" << std::endl;
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    std::string command = argv[1];
    
    try {
        if (command == "info") {
            print_system_info();
        } else if (command == "cpu") {
            print_cpu_info();
        } else if (command == "memory") {
            print_memory_info();
        } else if (command == "top") {
            print_process_list();
        } else if (command == "all") {
            print_system_info();
            print_cpu_info();
            print_memory_info();
            print_process_list();
        } else if (command == "history") {
            if (argc < 3) {
                std::cerr << "Error: Metric type required" << std::endl;
                std::cerr << "Usage: sysmon history <metric> [duration] [limit]" << std::endl;
                return 1;
            }
            std::string metric_type = argv[2];
            std::string duration = (argc > 3) ? argv[3] : "1h";
            int limit = (argc > 4) ? std::stoi(argv[4]) : 50;
            print_history(metric_type, duration, limit);
        } else if (command == "alerts") {
            print_alert_status();
        } else if (command == "test-alert") {
            if (argc < 3) {
                std::cerr << "Error: Config file required" << std::endl;
                std::cerr << "Usage: sysmon test-alert <config_file>" << std::endl;
                return 1;
            }
            test_alert_config(argv[2]);
        } else if (command == "hosts") {
            if (argc < 3) {
                std::cerr << "Error: Subcommand required" << std::endl;
                std::cerr << "Usage: sysmon hosts <list|show|compare>" << std::endl;
                return 1;
            }
            std::string subcmd = argv[2];
            if (subcmd == "list") {
                print_hosts_list();
            } else if (subcmd == "show") {
                if (argc < 4) {
                    std::cerr << "Error: Hostname required" << std::endl;
                    std::cerr << "Usage: sysmon hosts show <hostname>" << std::endl;
                    return 1;
                }
                print_host_details(argv[3]);
            } else if (subcmd == "compare") {
                if (argc < 5) {
                    std::cerr << "Error: Two hostnames required" << std::endl;
                    std::cerr << "Usage: sysmon hosts compare <host1> <host2>" << std::endl;
                    return 1;
                }
                print_hosts_compare(argv[3], argv[4]);
            } else {
                std::cerr << "Unknown hosts subcommand: " << subcmd << std::endl;
                std::cerr << "Available: list, show, compare" << std::endl;
                return 1;
            }
        } else if (command == "config") {
            if (argc < 3) {
                std::cerr << "Error: Subcommand required" << std::endl;
                std::cerr << "Usage: sysmon config <show|set>" << std::endl;
                return 1;
            }
            std::string subcmd = argv[2];
            if (subcmd == "show") {
                print_config_show();
            } else if (subcmd == "set") {
                if (argc < 5) {
                    std::cerr << "Error: Invalid syntax" << std::endl;
                    std::cerr << "Usage: sysmon config set mode <local|distributed|hybrid>" << std::endl;
                    return 1;
                }
                std::string setting = argv[3];
                std::string value = argv[4];
                if (setting == "mode") {
                    config_set_mode(value);
                } else {
                    std::cerr << "Unknown setting: " << setting << std::endl;
                    std::cerr << "Available: mode" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Unknown config subcommand: " << subcmd << std::endl;
                std::cerr << "Available: show, set" << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Unknown command: " << command << std::endl;
            print_usage();
            return 1;
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}