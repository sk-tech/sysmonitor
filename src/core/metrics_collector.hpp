#include "sysmon/platform_interface.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>

namespace sysmon {

/**
 * @brief Main metrics collection coordinator
 * 
 * Manages periodic collection of system metrics using multiple threads.
 * Provides non-blocking access to latest metrics.
 */
class MetricsCollector {
public:
    using MetricCallback = std::function<void(const CPUMetrics&, const MemoryMetrics&)>;
    
    MetricsCollector();
    ~MetricsCollector();
    
    // Non-copyable, non-movable
    MetricsCollector(const MetricsCollector&) = delete;
    MetricsCollector& operator=(const MetricsCollector&) = delete;
    
    /**
     * @brief Start background collection threads
     * @param interval_ms Collection interval in milliseconds
     */
    void Start(uint32_t interval_ms = 1000);
    
    /**
     * @brief Stop background collection threads
     */
    void Stop();
    
    /**
     * @brief Check if collector is running
     */
    bool IsRunning() const { return running_.load(); }
    
    /**
     * @brief Get latest CPU metrics (non-blocking)
     */
    CPUMetrics GetLatestCPU() const;
    
    /**
     * @brief Get latest memory metrics (non-blocking)
     */
    MemoryMetrics GetLatestMemory() const;
    
    /**
     * @brief Get process list (blocking, may take time)
     */
    std::vector<ProcessInfo> GetProcessList() const;
    
    /**
     * @brief Register callback for metric updates
     */
    void RegisterCallback(MetricCallback callback);
    
private:
    void CollectionLoop();
    void UpdateMetrics();
    
    std::unique_ptr<IProcessMonitor> process_monitor_;
    std::unique_ptr<ISystemMetrics> system_metrics_;
    
    std::atomic<bool> running_{false};
    std::thread collection_thread_;
    uint32_t interval_ms_;
    
    // Latest metrics (accessed atomically)
    mutable std::mutex metrics_mutex_;
    CPUMetrics latest_cpu_;
    MemoryMetrics latest_memory_;
    
    std::vector<MetricCallback> callbacks_;
};

} // namespace sysmon
