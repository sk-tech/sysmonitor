#include "sysmon/metrics_storage.hpp"
#include <sqlite3.h>
#include <sstream>
#include <cmath>

namespace sysmon {

/**
 * @brief Retention manager handles data rollup and deletion
 * 
 * Implements multi-tier retention strategy:
 * - 1-second resolution: 24 hours (raw data)
 * - 1-minute rollup: 30 days (averaged from 1s data)
 * - 1-hour rollup: 1 year (averaged from 1m data)
 */
class RetentionManager {
public:
    explicit RetentionManager(sqlite3* db) : db_(db) {}

    /**
     * @brief Rollup 1-second data to 1-minute averages
     * @param cutoff_ts Delete raw data older than this timestamp
     * @return Number of rows processed
     */
    int RollupToOneMinute(int64_t cutoff_ts) {
        const char* rollup_sql = R"(
            INSERT OR REPLACE INTO metrics_1m (timestamp, metric_type, host, tags, value)
            SELECT 
                (timestamp / 60) * 60 as minute_ts,
                metric_type,
                host,
                tags,
                AVG(value) as avg_value
            FROM metrics
            WHERE timestamp < ? AND timestamp >= ? - 86400
            GROUP BY minute_ts, metric_type, host, tags
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, rollup_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, cutoff_ts);
            sqlite3_bind_int64(stmt, 2, cutoff_ts);
            sqlite3_step(stmt);
            int changes = sqlite3_changes(db_);
            sqlite3_finalize(stmt);
            return changes;
        }
        return 0;
    }

    /**
     * @brief Rollup 1-minute data to 1-hour averages
     * @param cutoff_ts Delete 1m data older than this timestamp
     * @return Number of rows processed
     */
    int RollupToOneHour(int64_t cutoff_ts) {
        const char* rollup_sql = R"(
            INSERT OR REPLACE INTO metrics_1h (timestamp, metric_type, host, tags, value)
            SELECT 
                (timestamp / 3600) * 3600 as hour_ts,
                metric_type,
                host,
                tags,
                AVG(value) as avg_value
            FROM metrics_1m
            WHERE timestamp < ? AND timestamp >= ? - (30 * 86400)
            GROUP BY hour_ts, metric_type, host, tags
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, rollup_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, cutoff_ts);
            sqlite3_bind_int64(stmt, 2, cutoff_ts);
            sqlite3_step(stmt);
            int changes = sqlite3_changes(db_);
            sqlite3_finalize(stmt);
            return changes;
        }
        return 0;
    }

    /**
     * @brief Create rollup tables if they don't exist
     */
    void CreateRollupTables() {
        const char* create_1m = R"(
            CREATE TABLE IF NOT EXISTS metrics_1m (
                timestamp INTEGER NOT NULL,
                metric_type TEXT NOT NULL,
                host TEXT NOT NULL,
                tags TEXT,
                value REAL NOT NULL,
                PRIMARY KEY (timestamp, metric_type, host, tags)
            ) WITHOUT ROWID;
            CREATE INDEX IF NOT EXISTS idx_1m_metric_time ON metrics_1m(metric_type, timestamp);
        )";

        const char* create_1h = R"(
            CREATE TABLE IF NOT EXISTS metrics_1h (
                timestamp INTEGER NOT NULL,
                metric_type TEXT NOT NULL,
                host TEXT NOT NULL,
                tags TEXT,
                value REAL NOT NULL,
                PRIMARY KEY (timestamp, metric_type, host, tags)
            ) WITHOUT ROWID;
            CREATE INDEX IF NOT EXISTS idx_1h_metric_time ON metrics_1h(metric_type, timestamp);
        )";

        sqlite3_exec(db_, create_1m, nullptr, nullptr, nullptr);
        sqlite3_exec(db_, create_1h, nullptr, nullptr, nullptr);
    }

private:
    sqlite3* db_;
};

} // namespace sysmon
