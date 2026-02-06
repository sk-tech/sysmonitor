#include "sysmon/platform_interface.hpp"
#include "../core/metrics_collector.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running.store(false);
}

int main(int argc, char* argv[]) {
    std::cout << "SysMonitor Daemon v0.1.0" << std::endl;
    std::cout << "=========================" << std::endl;
    
    // Install signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        // Create metrics collector
        sysmon::MetricsCollector collector;
        
        // Register callback to print metrics
        collector.RegisterCallback([](const sysmon::CPUMetrics& cpu, const sysmon::MemoryMetrics& mem) {
            std::cout << "\n--- Metrics Update ---" << std::endl;
            std::cout << "CPU Usage: " << cpu.total_usage << "%" << std::endl;
            std::cout << "Load Average: " << cpu.load_average_1m << ", "
                      << cpu.load_average_5m << ", " << cpu.load_average_15m << std::endl;
            std::cout << "Memory: " << (mem.used_bytes / 1024 / 1024) << " MB / "
                      << (mem.total_bytes / 1024 / 1024) << " MB ("
                      << mem.usage_percent << "%)" << std::endl;
        });
        
        // Start collection
        std::cout << "\nStarting metric collection (Ctrl+C to stop)..." << std::endl;
        collector.Start(5000); // 5 second interval
        
        // Main loop
        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // Stop collection
        std::cout << "\nStopping metric collection..." << std::endl;
        collector.Stop();
        
        std::cout << "Daemon shutdown complete." << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
