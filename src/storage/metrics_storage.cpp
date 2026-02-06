#include "sysmon/metrics_storage.hpp"
#include <sqlite3.h>
#include <stdexcept>
#include <sstream>
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <sys/utsname.h>

namespace sysmon {

namespace {

// SQL schema with versioning support
constexpr const char* SCHEMA_VERSION_1 = R"(
-- Schema version tracking
CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER PRIMARY KEY,
    applied_at INTEGER NOT NULL
);

-- Main metrics table (time-series data)
CREATE TABLE IF NOT EXISTS metrics (
    timestamp INTEGER NOT NULL,
    metric_type TEXT NOT NULL,
    host TEXT NOT NULL,
    tags TEXT,
    value REAL NOT NULL,
    PRIMARY KEY (timestamp, metric_type, host, tags)
) WITHOUT ROWID;

-- Indexes for common query patterns
CREATE INDEX IF NOT EXISTS idx_metric_time ON metrics(metric_type, timestamp);
CREATE INDEX IF NOT EXISTS idx_host_time ON metrics(host, timestamp);
CREATE INDEX IF NOT EXISTS idx_timestamp ON metrics(timestamp);

-- Insert schema version
INSERT OR IGNORE INTO schema_version (version, applied_at) VALUES (1, strftime('%s', 'now'));
)";

// Get current Unix timestamp in seconds
int64_t GetCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// Get current time in milliseconds
int64_t GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// Get hostname
std::string GetHostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        hostname[sizeof(hostname) - 1] = '\0';
        return std::string(hostname);
    }
    return "unknown";
}

} // anonymous namespace

MetricsStorage::MetricsStorage(const StorageConfig& config)
    : config_(config), hostname_(GetHostname()) {
    
    // Open database with appropriate flags
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
    int rc = sqlite3_open_v2(config_.db_path.c_str(), &db_, flags, nullptr);
    
    if (rc != SQLITE_OK) {
        std::string error = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw std::runtime_error("Failed to open database: " + error);
    }

    // Enable WAL mode for better concurrent read performance
    if (config_.enable_wal) {
        ExecuteSQL("PRAGMA journal_mode=WAL;");
    }

    // Performance tuning
    ExecuteSQL("PRAGMA synchronous=NORMAL;");  // Faster writes, still safe with WAL
    ExecuteSQL("PRAGMA cache_size=-64000;");   // 64MB cache
    ExecuteSQL("PRAGMA temp_store=MEMORY;");   // Use memory for temp tables

    // Initialize schema
    InitializeSchema();

    // Prepare statements for batch inserts
    PrepareStatements();

    // Reserve batch space
    batch_.reserve(config_.batch_size);
    last_flush_time_ms_ = GetCurrentTimeMs();
}

MetricsStorage::~MetricsStorage() {
    // Flush any pending writes
    Flush();

    // Clean up prepared statements
    FinalizeStatements();

    // Close database
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void MetricsStorage::InitializeSchema() {
    // Check current schema version
    int current_version = GetSchemaVersion();

    // Apply migrations
    if (current_version < 1) {
        ExecuteSQL(SCHEMA_VERSION_1);
    }

    // Future migrations would go here:
    // if (current_version < 2) { ExecuteSQL(SCHEMA_VERSION_2); }
}

void MetricsStorage::ExecuteSQL(const std::string& sql) {
    char* error_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_msg);
    
    if (rc != SQLITE_OK) {
        std::string error = error_msg ? error_msg : "Unknown error";
        sqlite3_free(error_msg);
        throw std::runtime_error("SQL error: " + error);
    }
}

void MetricsStorage::PrepareStatements() {
    // Prepare insert statement for batch operations
    const char* insert_sql = R"(
        INSERT OR REPLACE INTO metrics (timestamp, metric_type, host, tags, value)
        VALUES (?, ?, ?, ?, ?)
    )";

    int rc = sqlite3_prepare_v2(db_, insert_sql, -1, &insert_stmt_, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare insert statement: " +
                                 std::string(sqlite3_errmsg(db_)));
    }
}

void MetricsStorage::FinalizeStatements() {
    if (insert_stmt_) {
        sqlite3_finalize(insert_stmt_);
        insert_stmt_ = nullptr;
    }
}

bool MetricsStorage::WriteCPUMetrics(const CPUMetrics& metrics) {
    int64_t ts = GetCurrentTimestamp();
    
    // Store aggregate CPU metrics
    bool success = true;
    success &= AddToBatch({ts, "cpu.total_usage", hostname_, "", metrics.total_usage});
    success &= AddToBatch({ts, "cpu.num_cores", hostname_, "", static_cast<double>(metrics.num_cores)});
    success &= AddToBatch({ts, "cpu.load_avg_1m", hostname_, "", metrics.load_average_1m});
    success &= AddToBatch({ts, "cpu.load_avg_5m", hostname_, "", metrics.load_average_5m});
    success &= AddToBatch({ts, "cpu.load_avg_15m", hostname_, "", metrics.load_average_15m});
    success &= AddToBatch({ts, "cpu.context_switches", hostname_, "", static_cast<double>(metrics.context_switches)});
    success &= AddToBatch({ts, "cpu.interrupts", hostname_, "", static_cast<double>(metrics.interrupts)});

    // Store per-core usage (with core number in tags)
    for (size_t i = 0; i < metrics.per_core_usage.size(); ++i) {
        std::string tags = "{\"core\":" + std::to_string(i) + "}";
        success &= AddToBatch({ts, "cpu.core_usage", hostname_, tags, metrics.per_core_usage[i]});
    }

    return success;
}

bool MetricsStorage::WriteMemoryMetrics(const MemoryMetrics& metrics) {
    int64_t ts = GetCurrentTimestamp();
    
    bool success = true;
    success &= AddToBatch({ts, "memory.total_bytes", hostname_, "", static_cast<double>(metrics.total_bytes)});
    success &= AddToBatch({ts, "memory.available_bytes", hostname_, "", static_cast<double>(metrics.available_bytes)});
    success &= AddToBatch({ts, "memory.used_bytes", hostname_, "", static_cast<double>(metrics.used_bytes)});
    success &= AddToBatch({ts, "memory.free_bytes", hostname_, "", static_cast<double>(metrics.free_bytes)});
    success &= AddToBatch({ts, "memory.cached_bytes", hostname_, "", static_cast<double>(metrics.cached_bytes)});
    success &= AddToBatch({ts, "memory.buffers_bytes", hostname_, "", static_cast<double>(metrics.buffers_bytes)});
    success &= AddToBatch({ts, "memory.usage_percent", hostname_, "", metrics.usage_percent});
    success &= AddToBatch({ts, "memory.swap_total_bytes", hostname_, "", static_cast<double>(metrics.swap_total_bytes)});
    success &= AddToBatch({ts, "memory.swap_used_bytes", hostname_, "", static_cast<double>(metrics.swap_used_bytes)});

    return success;
}

bool MetricsStorage::WriteProcessMetrics(const std::vector<ProcessInfo>& processes) {
    int64_t ts = GetCurrentTimestamp();
    
    // Store top processes by CPU and memory (limit to top 20 to reduce storage)
    // In production, you might want to store all or use sampling
    bool success = true;
    int count = 0;
    const int MAX_PROCESSES = 20;

    for (const auto& proc : processes) {
        if (count++ >= MAX_PROCESSES) break;

        std::string tags = "{\"pid\":" + std::to_string(proc.pid) + 
                          ",\"name\":\"" + proc.name + "\"}";
        
        success &= AddToBatch({ts, "process.cpu_percent", hostname_, tags, proc.cpu_percent});
        success &= AddToBatch({ts, "process.memory_bytes", hostname_, tags, static_cast<double>(proc.memory_bytes)});
        success &= AddToBatch({ts, "process.num_threads", hostname_, tags, static_cast<double>(proc.num_threads)});
    }

    // Store process count
    success &= AddToBatch({ts, "process.count", hostname_, "", static_cast<double>(processes.size())});

    return success;
}

bool MetricsStorage::WriteDiskMetrics(const std::vector<DiskMetrics>& disks) {
    int64_t ts = GetCurrentTimestamp();
    
    bool success = true;
    for (const auto& disk : disks) {
        std::string tags = "{\"device\":\"" + disk.device_name + 
                          "\",\"mount\":\"" + disk.mount_point + "\"}";
        
        success &= AddToBatch({ts, "disk.total_bytes", hostname_, tags, static_cast<double>(disk.total_bytes)});
        success &= AddToBatch({ts, "disk.used_bytes", hostname_, tags, static_cast<double>(disk.used_bytes)});
        success &= AddToBatch({ts, "disk.free_bytes", hostname_, tags, static_cast<double>(disk.free_bytes)});
        success &= AddToBatch({ts, "disk.usage_percent", hostname_, tags, disk.usage_percent});
        success &= AddToBatch({ts, "disk.read_bytes", hostname_, tags, static_cast<double>(disk.read_bytes)});
        success &= AddToBatch({ts, "disk.write_bytes", hostname_, tags, static_cast<double>(disk.write_bytes)});
    }

    return success;
}

bool MetricsStorage::WriteNetworkMetrics(const std::vector<NetworkMetrics>& interfaces) {
    int64_t ts = GetCurrentTimestamp();
    
    bool success = true;
    for (const auto& iface : interfaces) {
        std::string tags = "{\"interface\":\"" + iface.interface_name + "\"}";
        
        success &= AddToBatch({ts, "network.bytes_sent", hostname_, tags, static_cast<double>(iface.bytes_sent)});
        success &= AddToBatch({ts, "network.bytes_recv", hostname_, tags, static_cast<double>(iface.bytes_recv)});
        success &= AddToBatch({ts, "network.packets_sent", hostname_, tags, static_cast<double>(iface.packets_sent)});
        success &= AddToBatch({ts, "network.packets_recv", hostname_, tags, static_cast<double>(iface.packets_recv)});
        success &= AddToBatch({ts, "network.errors_in", hostname_, tags, static_cast<double>(iface.errors_in)});
        success &= AddToBatch({ts, "network.errors_out", hostname_, tags, static_cast<double>(iface.errors_out)});
        success &= AddToBatch({ts, "network.drops_in", hostname_, tags, static_cast<double>(iface.drops_in)});
        success &= AddToBatch({ts, "network.drops_out", hostname_, tags, static_cast<double>(iface.drops_out)});
    }

    return success;
}

bool MetricsStorage::AddToBatch(const StoredMetric& metric) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    
    // Check if buffer is full
    if (batch_.size() >= MAX_BUFFER_SIZE) {
        return false;
    }

    batch_.push_back(metric);

    // Auto-flush if batch size reached or time interval exceeded
    bool should_flush = (batch_.size() >= static_cast<size_t>(config_.batch_size)) ||
                       ((GetCurrentTimeMs() - last_flush_time_ms_) >= config_.flush_interval_ms);

    if (should_flush) {
        return FlushBatch();
    }

    return true;
}

bool MetricsStorage::Flush() {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return FlushBatch();
}

bool MetricsStorage::FlushBatch() {
    // Note: batch_mutex_ should already be locked by caller
    
    if (batch_.empty()) {
        return true;
    }

    // Begin transaction for batch insert
    char* error_msg = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &error_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(error_msg);
        return false;
    }

    // Insert all metrics in batch
    bool success = true;
    for (const auto& metric : batch_) {
        sqlite3_reset(insert_stmt_);
        sqlite3_bind_int64(insert_stmt_, 1, metric.timestamp);
        sqlite3_bind_text(insert_stmt_, 2, metric.metric_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt_, 3, metric.host.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt_, 4, metric.tags.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(insert_stmt_, 5, metric.value);

        rc = sqlite3_step(insert_stmt_);
        if (rc != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    // Commit or rollback transaction
    if (success) {
        rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &error_msg);
        if (rc != SQLITE_OK) {
            sqlite3_free(error_msg);
            success = false;
        }
    } else {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }

    // Clear batch on success
    if (success) {
        batch_.clear();
        last_flush_time_ms_ = GetCurrentTimeMs();
    }

    return success;
}

std::vector<StoredMetric> MetricsStorage::QueryRange(const std::string& metric_type,
                                                      int64_t start_ts,
                                                      int64_t end_ts,
                                                      int limit) {
    std::vector<StoredMetric> results;

    // Build query
    std::ostringstream query;
    query << "SELECT timestamp, metric_type, host, tags, value FROM metrics "
          << "WHERE metric_type = ? AND timestamp >= ? AND timestamp <= ? "
          << "ORDER BY timestamp DESC";
    
    if (limit > 0) {
        query << " LIMIT " << limit;
    }

    // Prepare statement
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, query.str().c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return results;
    }

    // Bind parameters
    sqlite3_bind_text(stmt, 1, metric_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, start_ts);
    sqlite3_bind_int64(stmt, 3, end_ts);

    // Execute and fetch results
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        StoredMetric metric;
        metric.timestamp = sqlite3_column_int64(stmt, 0);
        metric.metric_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        metric.host = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* tags_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        metric.tags = tags_text ? tags_text : "";
        
        metric.value = sqlite3_column_double(stmt, 4);
        
        results.push_back(metric);
    }

    sqlite3_finalize(stmt);
    return results;
}

int MetricsStorage::ApplyRetention(int retention_days) {
    int64_t cutoff_ts = GetCurrentTimestamp() - (retention_days * 86400);

    // Count rows to be deleted
    sqlite3_stmt* count_stmt = nullptr;
    const char* count_sql = "SELECT COUNT(*) FROM metrics WHERE timestamp < ?";
    
    int deleted_count = 0;
    if (sqlite3_prepare_v2(db_, count_sql, -1, &count_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(count_stmt, 1, cutoff_ts);
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            deleted_count = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }

    // Delete old data
    sqlite3_stmt* delete_stmt = nullptr;
    const char* delete_sql = "DELETE FROM metrics WHERE timestamp < ?";
    
    if (sqlite3_prepare_v2(db_, delete_sql, -1, &delete_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(delete_stmt, 1, cutoff_ts);
        sqlite3_step(delete_stmt);
        sqlite3_finalize(delete_stmt);
    }

    // Run VACUUM to reclaim space (can be slow, consider running offline)
    // ExecuteSQL("VACUUM");

    return deleted_count;
}

int MetricsStorage::GetSchemaVersion() {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT version FROM schema_version ORDER BY version DESC LIMIT 1";
    
    int version = 0;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            version = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return version;
}

bool MetricsStorage::IsHealthy() {
    // Simple health check - try to query schema version
    try {
        GetSchemaVersion();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace sysmon
