#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace sysmon {

// ============================================
// Data Structures
// ============================================

struct ProcessInfo {
    uint32_t pid;
    uint32_t ppid;              // Parent process ID
    std::string name;
    std::string executable;
    double cpu_percent;
    uint64_t memory_bytes;
    uint32_t num_threads;
    int64_t start_time;         // Unix timestamp
    std::string state;          // Running, Sleeping, Zombie, etc.
    std::string username;       // Process owner
    uint64_t read_bytes;        // Disk I/O
    uint64_t write_bytes;
    uint32_t open_files;        // Number of open file descriptors
};

struct CPUMetrics {
    uint32_t num_cores;
    std::vector<double> per_core_usage;  // Percentage per core
    double total_usage;                  // Overall percentage
    double load_average_1m;
    double load_average_5m;
    double load_average_15m;
    uint64_t context_switches;
    uint64_t interrupts;
};

struct MemoryMetrics {
    uint64_t total_bytes;
    uint64_t available_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t cached_bytes;
    uint64_t buffers_bytes;
    uint64_t swap_total_bytes;
    uint64_t swap_used_bytes;
    double usage_percent;
};

struct DiskMetrics {
    std::string device_name;
    std::string mount_point;
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    double usage_percent;
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t read_ops;
    uint64_t write_ops;
    double io_utilization;
};

struct NetworkMetrics {
    std::string interface_name;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    uint64_t packets_sent;
    uint64_t packets_recv;
    uint64_t errors_in;
    uint64_t errors_out;
    uint64_t drops_in;
    uint64_t drops_out;
    bool is_up;
    uint64_t speed_mbps;
};

struct SystemInfo {
    std::string os_name;
    std::string os_version;
    std::string kernel_version;
    std::string hostname;
    std::string architecture;
    uint64_t uptime_seconds;
    uint64_t boot_time;
};

// ============================================
// Platform Abstraction Layer Interfaces
// ============================================

/**
 * @brief Interface for process monitoring operations
 * 
 * Platform-specific implementations must provide:
 * - Linux: /proc filesystem parsing
 * - Windows: EnumProcesses, OpenProcess, GetProcessMemoryInfo
 * - macOS: proc_listpids, proc_pidinfo
 */
class IProcessMonitor {
public:
    virtual ~IProcessMonitor() = default;
    
    /**
     * @brief Get list of all running processes
     * @return Vector of ProcessInfo structs
     */
    virtual std::vector<ProcessInfo> GetProcessList() = 0;
    
    /**
     * @brief Get detailed information about a specific process
     * @param pid Process ID
     * @return ProcessInfo struct, or null if process doesn't exist
     */
    virtual std::unique_ptr<ProcessInfo> GetProcessDetails(uint32_t pid) = 0;
    
    /**
     * @brief Check if a process exists
     * @param pid Process ID
     * @return true if process exists, false otherwise
     */
    virtual bool ProcessExists(uint32_t pid) = 0;
    
    /**
     * @brief Send signal to process (kill on Windows)
     * @param pid Process ID
     * @param signal Signal number (SIGTERM, SIGKILL, etc.)
     * @return true if signal sent successfully
     */
    virtual bool KillProcess(uint32_t pid, int signal) = 0;
};

/**
 * @brief Interface for system-wide metrics collection
 */
class ISystemMetrics {
public:
    virtual ~ISystemMetrics() = default;
    
    /**
     * @brief Get CPU usage metrics
     * @return CPUMetrics struct with current CPU statistics
     */
    virtual CPUMetrics GetCPUMetrics() = 0;
    
    /**
     * @brief Get memory usage metrics
     * @return MemoryMetrics struct with current memory statistics
     */
    virtual MemoryMetrics GetMemoryMetrics() = 0;
    
    /**
     * @brief Get disk I/O metrics for all mounted devices
     * @return Vector of DiskMetrics structs
     */
    virtual std::vector<DiskMetrics> GetDiskMetrics() = 0;
    
    /**
     * @brief Get network interface metrics
     * @return Vector of NetworkMetrics structs
     */
    virtual std::vector<NetworkMetrics> GetNetworkMetrics() = 0;
    
    /**
     * @brief Get system information
     * @return SystemInfo struct with OS details
     */
    virtual SystemInfo GetSystemInfo() = 0;
};

// ============================================
// Factory Functions
// ============================================

/**
 * @brief Create platform-specific process monitor
 * @return Unique pointer to IProcessMonitor implementation
 */
std::unique_ptr<IProcessMonitor> CreateProcessMonitor();

/**
 * @brief Create platform-specific system metrics collector
 * @return Unique pointer to ISystemMetrics implementation
 */
std::unique_ptr<ISystemMetrics> CreateSystemMetrics();

} // namespace sysmon
