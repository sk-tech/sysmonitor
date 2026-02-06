#include "metrics_collector.hpp"
#include <iostream>

namespace sysmon {

MetricsCollector::MetricsCollector()
    : process_monitor_(CreateProcessMonitor()),
      system_metrics_(CreateSystemMetrics()),
      storage_(nullptr),
      interval_ms_(1000) {
}

MetricsCollector::MetricsCollector(const StorageConfig& storage_config)
    : process_monitor_(CreateProcessMonitor()),
      system_metrics_(CreateSystemMetrics()),
      storage_(std::make_unique<MetricsStorage>(storage_config)),
      interval_ms_(1000) {
}

MetricsCollector::~MetricsCollector() {
    Stop();
}

void MetricsCollector::Start(uint32_t interval_ms) {
    if (running_.load()) {
        return; // Already running
    }
    
    interval_ms_ = interval_ms;
    running_.store(true);
    
    collection_thread_ = std::thread(&MetricsCollector::CollectionLoop, this);
}

void MetricsCollector::Stop() {
    if (!running_.load()) {
        return; // Not running
    }
    
    running_.store(false);
    
    if (collection_thread_.joinable()) {
        collection_thread_.join();
    }
}

void MetricsCollector::CollectionLoop() {
    while (running_.load()) {
        auto start_time = std::chrono::steady_clock::now();
        
        try {
            UpdateMetrics();
        } catch (const std::exception& e) {
            std::cerr << "Error collecting metrics: " << e.what() << std::endl;
        }
        
        // Sleep for remaining interval time
        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time
        ).count();
        
        auto sleep_time = interval_ms_ - elapsed;
        if (sleep_time > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        }
    }
}

void MetricsCollector::UpdateMetrics() {
    // Collect metrics (potentially blocking operations)
    auto cpu_metrics = system_metrics_->GetCPUMetrics();
    auto memory_metrics = system_metrics_->GetMemoryMetrics();
    
    // Update stored metrics atomically
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        latest_cpu_ = cpu_metrics;
        latest_memory_ = memory_metrics;
    }
    
    // Write to storage if enabled (batched, non-blocking)
    if (storage_) {
        storage_->WriteCPUMetrics(cpu_metrics);
        storage_->WriteMemoryMetrics(memory_metrics);
        
        // Optionally collect and store disk/network metrics
        try {
            auto disk_metrics = system_metrics_->GetDiskMetrics();
            storage_->WriteDiskMetrics(disk_metrics);
            
            auto network_metrics = system_metrics_->GetNetworkMetrics();
            storage_->WriteNetworkMetrics(network_metrics);
        } catch (const std::exception& e) {
            std::cerr << "Error collecting disk/network metrics: " << e.what() << std::endl;
        }
    }
    
    // Notify callbacks
    for (auto& callback : callbacks_) {
        try {
            callback(cpu_metrics, memory_metrics);
        } catch (const std::exception& e) {
            std::cerr << "Error in metric callback: " << e.what() << std::endl;
        }
    }
}

CPUMetrics MetricsCollector::GetLatestCPU() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return latest_cpu_;
}

MemoryMetrics MetricsCollector::GetLatestMemory() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return latest_memory_;
}

std::vector<ProcessInfo> MetricsCollector::GetProcessList() const {
    return process_monitor_->GetProcessList();
}

void MetricsCollector::RegisterCallback(MetricCallback callback) {
    callbacks_.push_back(std::move(callback));
}

} // namespace sysmon
