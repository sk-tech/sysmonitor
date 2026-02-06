#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <mutex>
#include "platform_interface.hpp"

// Forward declare SQLite types to avoid header dependency
struct sqlite3;
struct sqlite3_stmt;

namespace sysmon {

/**
 * @brief Represents a stored metric data point
 */
struct StoredMetric {
    int64_t timestamp;        // Unix timestamp (seconds)
    std::string metric_type;  // e.g., "cpu.usage", "memory.used_bytes"
    std::string host;
    std::string tags;         // JSON string for flexible tagging
    double value;
};

/**
 * @brief Configuration for metrics storage
 */
struct StorageConfig {
    std::string db_path;
    int retention_days = 30;
    bool enable_wal = true;           // Write-Ahead Logging mode
    int batch_size = 100;             // Number of metrics to batch before write
    int flush_interval_ms = 5000;     // Max time to wait before flushing batch
};

/**
 * @brief Time-series storage backend using SQLite
 * 
 * Implements batch writes with transactions, WAL mode for concurrent reads,
 * and automatic schema migrations. Thread-safe for concurrent reads/writes.
 * 
 * Design decisions (from Week 2 considerations):
 * - Writes happen in MetricsCollector's background thread (single-threaded)
 * - Ring buffer for metrics during storage failures
 * - Schema versioning for production-ready migrations
 */
class MetricsStorage {
public:
    /**
     * @brief Initialize storage with given configuration
     * @param config Storage configuration
     * @throws std::runtime_error if database cannot be opened or initialized
     */
    explicit MetricsStorage(const StorageConfig& config);
    ~MetricsStorage();

    // Delete copy/move to prevent SQLite handle issues
    MetricsStorage(const MetricsStorage&) = delete;
    MetricsStorage& operator=(const MetricsStorage&) = delete;

    /**
     * @brief Write CPU metrics to storage (batched)
     * @param metrics CPU metrics to store
     * @return true if successfully queued, false if buffer full
     */
    bool WriteCPUMetrics(const CPUMetrics& metrics);

    /**
     * @brief Write memory metrics to storage (batched)
     * @param metrics Memory metrics to store
     * @return true if successfully queued, false if buffer full
     */
    bool WriteMemoryMetrics(const MemoryMetrics& metrics);

    /**
     * @brief Write process metrics to storage (batched)
     * @param processes List of process metrics
     * @return true if successfully queued, false if buffer full
     */
    bool WriteProcessMetrics(const std::vector<ProcessInfo>& processes);

    /**
     * @brief Write disk metrics to storage (batched)
     * @param disks List of disk metrics
     * @return true if successfully queued, false if buffer full
     */
    bool WriteDiskMetrics(const std::vector<DiskMetrics>& disks);

    /**
     * @brief Write network metrics to storage (batched)
     * @param interfaces List of network interface metrics
     * @return true if successfully queued, false if buffer full
     */
    bool WriteNetworkMetrics(const std::vector<NetworkMetrics>& interfaces);

    /**
     * @brief Flush pending batch writes immediately
     * @return true if flush successful, false on error
     */
    bool Flush();

    /**
     * @brief Query metrics within a time range
     * @param metric_type Type of metric (e.g., "cpu.usage")
     * @param start_ts Start timestamp (Unix seconds)
     * @param end_ts End timestamp (Unix seconds)
     * @param limit Maximum number of results (0 = no limit)
     * @return Vector of matching metrics
     */
    std::vector<StoredMetric> QueryRange(const std::string& metric_type,
                                         int64_t start_ts,
                                         int64_t end_ts,
                                         int limit = 0);

    /**
     * @brief Apply retention policy - delete old data
     * @param retention_days Keep data newer than this many days
     * @return Number of rows deleted
     */
    int ApplyRetention(int retention_days);

    /**
     * @brief Get current schema version
     * @return Schema version number
     */
    int GetSchemaVersion();

    /**
     * @brief Check if database is healthy and writable
     * @return true if database is accessible
     */
    bool IsHealthy();

private:
    /**
     * @brief Initialize database schema and apply migrations
     */
    void InitializeSchema();

    /**
     * @brief Execute a SQL statement without results
     * @param sql SQL statement to execute
     * @throws std::runtime_error on SQL error
     */
    void ExecuteSQL(const std::string& sql);

    /**
     * @brief Add a metric to the pending batch
     * @param metric Metric to add
     * @return true if added, false if buffer full
     */
    bool AddToBatch(const StoredMetric& metric);

    /**
     * @brief Write batched metrics to database with transaction
     * @return true if write successful, false on error
     */
    bool FlushBatch();

    /**
     * @brief Prepare SQL statements for batch inserts
     */
    void PrepareStatements();

    /**
     * @brief Clean up prepared statements
     */
    void FinalizeStatements();

    // Configuration
    StorageConfig config_;
    std::string hostname_;

    // SQLite handles
    sqlite3* db_ = nullptr;
    sqlite3_stmt* insert_stmt_ = nullptr;

    // Batch write buffer
    std::vector<StoredMetric> batch_;
    std::mutex batch_mutex_;
    int64_t last_flush_time_ms_ = 0;

    // Ring buffer for failures (up to 10k metrics)
    static constexpr size_t MAX_BUFFER_SIZE = 10000;
};

} // namespace sysmon
