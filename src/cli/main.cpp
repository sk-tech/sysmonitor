#include "sysmon/platform_interface.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>

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

void print_usage() {
    std::cout << "SysMonitor CLI v0.1.0" << std::endl;
    std::cout << "\nUsage: sysmon <command>" << std::endl;
    std::cout << "\nCommands:" << std::endl;
    std::cout << "  info      Show system information" << std::endl;
    std::cout << "  cpu       Show CPU metrics" << std::endl;
    std::cout << "  memory    Show memory metrics" << std::endl;
    std::cout << "  top       Show top processes" << std::endl;
    std::cout << "  all       Show all metrics" << std::endl;
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
