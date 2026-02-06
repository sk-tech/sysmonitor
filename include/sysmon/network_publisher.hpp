#pragma once

#include "platform_interface.hpp"
#include "agent_config.hpp"
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

namespace sysmon {

/**
 * @brief Represents a metric to be published
 */
struct PublishableMetric {
    int64_t timestamp;
    std::string metric_type;
    double value;
    std::string tags;
    
    PublishableMetric(int64_t ts, const std::string& type, double val, const std::string& t = "")
        : timestamp(ts), metric_type(type), value(val), tags(t) {}
};

/**
 * @brief Network publisher for sending metrics to aggregator
 * 
 * Implements retry logic with exponential backoff, local queueing
 * when aggregator is unavailable, and batch publishing.
 */
class NetworkPublisher {
public:
    /**
     * @brief Initialize publisher with agent configuration
     * @param config Agent configuration
     */
    explicit NetworkPublisher(const AgentConfig& config);
    ~NetworkPublisher();
    
    // Non-copyable, non-movable
    NetworkPublisher(const NetworkPublisher&) = delete;
    NetworkPublisher& operator=(const NetworkPublisher&) = delete;
    
    /**
     * @brief Start background publishing thread
     */
    void Start();
    
    /**
     * @brief Stop background publishing thread
     */
    void Stop();
    
    /**
     * @brief Check if publisher is running
     */
    bool IsRunning() const { return running_.load(); }
    
    /**
     * @brief Queue a metric for publishing
     * @param metric Metric to publish
     * @return true if queued successfully, false if queue is full
     */
    bool QueueMetric(const PublishableMetric& metric);
    
    /**
     * @brief Queue CPU metrics for publishing
     */
    bool QueueCPUMetrics(const CPUMetrics& metrics);
    
    /**
     * @brief Queue memory metrics for publishing
     */
    bool QueueMemoryMetrics(const MemoryMetrics& metrics);
    
    /**
     * @brief Get queue size
     */
    size_t GetQueueSize() const;
    
    /**
     * @brief Get statistics
     */
    struct Stats {
        uint64_t metrics_queued = 0;
        uint64_t metrics_sent = 0;
        uint64_t metrics_failed = 0;
        uint64_t publish_attempts = 0;
        uint64_t publish_successes = 0;
        uint64_t publish_failures = 0;
        uint64_t queue_overflows = 0;
    };
    
    Stats GetStats() const;

private:
    void PublishLoop();
    bool PublishBatch(const std::vector<PublishableMetric>& batch);
    bool SendHTTPRequest(const std::string& json_body);
    std::string BuildJsonPayload(const std::vector<PublishableMetric>& metrics);
    int CalculateBackoffDelay(int attempt);
    
    AgentConfig config_;
    
    std::atomic<bool> running_{false};
    std::thread publish_thread_;
    
    mutable std::mutex queue_mutex_;
    std::queue<PublishableMetric> metric_queue_;
    
    Stats stats_;
    mutable std::mutex stats_mutex_;
    
    std::chrono::steady_clock::time_point last_publish_time_;
};

} // namespace sysmon
