#include "sysmon/platform_interface.hpp"
#include "sysmon/metrics_storage.hpp"
#include "../core/metrics_collector.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <filesystem>
#include <cstdlib>

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
        // Configure storage path
        std::string db_path;
        if (argc > 1) {
            db_path = argv[1];
        } else {
            // Default: ~/.sysmon/data.db
            const char* home = std::getenv("HOME");
            if (home) {
                std::filesystem::path sysmon_dir = std::filesystem::path(home) / ".sysmon";
                std::filesystem::create_directories(sysmon_dir);
                db_path = (sysmon_dir / "data.db").string();
            } else {
                db_path = "sysmon_data.db";
            }
        }
        
        std::cout << "Storage: " << db_path << std::endl;
        
        // Configure storage
        sysmon::StorageConfig storage_config;
        storage_config.db_path = db_path;
        storage_config.retention_days = 30;
        storage_config.batch_size = 100;
        storage_config.flush_interval_ms = 5000;
        
        // Create metrics collector with storage
        sysmon::MetricsCollector collector(storage_config);
        
        // Register callback to print metrics
        collector.RegisterCallback([](const sysmon::CPUMetrics& cpu, const sysmon::MemoryMetrics& mem) {
            std::cout << "\n--- Metrics Update (stored) ---" << std::endl;
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
