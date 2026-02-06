#include "sysmon/platform_interface.hpp"
#include "sysmon/metrics_storage.hpp"
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
