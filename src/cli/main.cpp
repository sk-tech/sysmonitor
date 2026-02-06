#include "sysmon/platform_interface.hpp"
#include "sysmon/metrics_storage.hpp"
#include "sysmon/alert_manager.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>

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

void print_usage() {
    std::cout << "SysMonitor CLI v0.1.0" << std::endl;
    std::cout << "\nUsage: sysmon <command> [options]" << std::endl;
    std::cout << "\nCommands:" << std::endl;
    std::cout << "  info      Show system information" << std::endl;
    std::cout << "  cpu       Show CPU metrics" << std::endl;
    std::cout << "  memory    Show memory metrics" << std::endl;
    std::cout << "  top       Show top processes" << std::endl;
    std::cout << "  all       Show all metrics" << std::endl;
    std::cout << "  history <metric> [duration] [limit]" << std::endl;
    std::cout << "            Query historical metrics" << std::endl;
    std::cout << "            Examples:" << std::endl;
    std::cout << "              sysmon history cpu.total_usage 1h 20" << std::endl;
    std::cout << "              sysmon history memory.usage_percent 24h" << std::endl;
    std::cout << "            Duration: 1h, 30m, 24h, 7d (default: 1h)" << std::endl;
    std::cout << std::endl;
    std::cout << "  alerts" << std::endl;
    std::cout << "            Show alert status and configuration" << std::endl;
    std::cout << "  test-alert <config_file>" << std::endl;
    std::cout << "            Test alert configuration with current metrics" << std::endl;
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
