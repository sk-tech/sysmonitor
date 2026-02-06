#include "sysmon/platform_interface.hpp"
#include <fstream>
#include <sstream>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <vector>

namespace sysmon {

class LinuxSystemMetrics : public ISystemMetrics {
public:
    LinuxSystemMetrics() : num_cpus_(sysconf(_SC_NPROCESSORS_ONLN)) {}
    ~LinuxSystemMetrics() override = default;
    
    CPUMetrics GetCPUMetrics() override {
        CPUMetrics metrics;
        metrics.num_cores = num_cpus_;
        
        // Read /proc/stat for CPU times
        std::ifstream stat_file("/proc/stat");
        if (!stat_file.is_open()) {
            return metrics;
        }
        
        std::string line;
        std::getline(stat_file, line); // First line is aggregate
        
        std::istringstream iss(line);
        std::string cpu_label;
        uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
        iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        
        uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
        uint64_t busy = total - idle - iowait;
        
        // Calculate percentage (requires previous sample for accurate delta)
        // For now, use a simple estimation
        metrics.total_usage = (total > 0) ? (100.0 * busy / total) : 0.0;
        
        // Read per-core usage (simplified - just report total for each core)
        metrics.per_core_usage.resize(num_cpus_, metrics.total_usage / num_cpus_);
        
        // Read load average
        struct sysinfo si;
        if (sysinfo(&si) == 0) {
            metrics.load_average_1m = si.loads[0] / 65536.0;
            metrics.load_average_5m = si.loads[1] / 65536.0;
            metrics.load_average_15m = si.loads[2] / 65536.0;
        }
        
        // Context switches and interrupts (from /proc/stat)
        stat_file.clear();
        stat_file.seekg(0);
        while (std::getline(stat_file, line)) {
            if (line.find("ctxt") == 0) {
                std::istringstream iss(line);
                std::string label;
                iss >> label >> metrics.context_switches;
            } else if (line.find("intr") == 0) {
                std::istringstream iss(line);
                std::string label;
                iss >> label >> metrics.interrupts;
            }
        }
        
        return metrics;
    }
    
    MemoryMetrics GetMemoryMetrics() override {
        MemoryMetrics metrics = {};
        
        std::ifstream meminfo("/proc/meminfo");
        if (!meminfo.is_open()) {
            return metrics;
        }
        
        std::string line;
        while (std::getline(meminfo, line)) {
            std::istringstream iss(line);
            std::string key;
            uint64_t value;
            std::string unit;
            
            iss >> key >> value >> unit;
            
            // Convert kB to bytes
            value *= 1024;
            
            if (key == "MemTotal:") {
                metrics.total_bytes = value;
            } else if (key == "MemFree:") {
                metrics.free_bytes = value;
            } else if (key == "MemAvailable:") {
                metrics.available_bytes = value;
            } else if (key == "Cached:") {
                metrics.cached_bytes = value;
            } else if (key == "Buffers:") {
                metrics.buffers_bytes = value;
            } else if (key == "SwapTotal:") {
                metrics.swap_total_bytes = value;
            } else if (key == "SwapFree:") {
                uint64_t swap_free = value;
                metrics.swap_used_bytes = metrics.swap_total_bytes - swap_free;
            }
        }
        
        metrics.used_bytes = metrics.total_bytes - metrics.available_bytes;
        metrics.usage_percent = (metrics.total_bytes > 0)
            ? (100.0 * metrics.used_bytes / metrics.total_bytes)
            : 0.0;
        
        return metrics;
    }
    
    std::vector<DiskMetrics> GetDiskMetrics() override {
        std::vector<DiskMetrics> disks;
        
        // Read /proc/mounts to find mounted filesystems
        std::ifstream mounts("/proc/mounts");
        if (!mounts.is_open()) {
            return disks;
        }
        
        std::string line;
        while (std::getline(mounts, line)) {
            std::istringstream iss(line);
            std::string device, mount_point, fs_type;
            iss >> device >> mount_point >> fs_type;
            
            // Skip virtual filesystems
            if (fs_type == "proc" || fs_type == "sysfs" || fs_type == "tmpfs" ||
                fs_type == "devtmpfs" || fs_type == "cgroup" || fs_type == "cgroup2") {
                continue;
            }
            
            struct statvfs vfs;
            if (statvfs(mount_point.c_str(), &vfs) != 0) {
                continue;
            }
            
            DiskMetrics disk;
            disk.device_name = device;
            disk.mount_point = mount_point;
            disk.total_bytes = vfs.f_blocks * vfs.f_frsize;
            disk.free_bytes = vfs.f_bfree * vfs.f_frsize;
            disk.used_bytes = disk.total_bytes - disk.free_bytes;
            disk.usage_percent = (disk.total_bytes > 0)
                ? (100.0 * disk.used_bytes / disk.total_bytes)
                : 0.0;
            
            // TODO: Read I/O stats from /proc/diskstats
            disk.read_bytes = 0;
            disk.write_bytes = 0;
            disk.read_ops = 0;
            disk.write_ops = 0;
            disk.io_utilization = 0.0;
            
            disks.push_back(disk);
        }
        
        return disks;
    }
    
    std::vector<NetworkMetrics> GetNetworkMetrics() override {
        std::vector<NetworkMetrics> interfaces;
        
        std::ifstream netdev("/proc/net/dev");
        if (!netdev.is_open()) {
            return interfaces;
        }
        
        std::string line;
        // Skip header lines
        std::getline(netdev, line);
        std::getline(netdev, line);
        
        while (std::getline(netdev, line)) {
            std::istringstream iss(line);
            std::string iface_name;
            std::getline(iss, iface_name, ':');
            
            // Trim whitespace
            size_t start = iface_name.find_first_not_of(" \t");
            if (start != std::string::npos) {
                iface_name = iface_name.substr(start);
            }
            
            NetworkMetrics net;
            net.interface_name = iface_name;
            
            iss >> net.bytes_recv >> net.packets_recv >> net.errors_in >> net.drops_in;
            iss.ignore(256, ' '); // Skip some fields
            iss >> net.bytes_sent >> net.packets_sent >> net.errors_out >> net.drops_out;
            
            // TODO: Check if interface is up, get speed
            net.is_up = true;
            net.speed_mbps = 0;
            
            interfaces.push_back(net);
        }
        
        return interfaces;
    }
    
    SystemInfo GetSystemInfo() override {
        SystemInfo info;
        
        // Read /etc/os-release for OS name and version
        std::ifstream os_release("/etc/os-release");
        if (os_release.is_open()) {
            std::string line;
            while (std::getline(os_release, line)) {
                if (line.find("PRETTY_NAME=") == 0) {
                    size_t start = line.find('"') + 1;
                    size_t end = line.rfind('"');
                    info.os_name = line.substr(start, end - start);
                } else if (line.find("VERSION_ID=") == 0) {
                    size_t start = line.find('"') + 1;
                    size_t end = line.rfind('"');
                    info.os_version = line.substr(start, end - start);
                }
            }
        }
        
        // Get kernel version
        std::ifstream version("/proc/version");
        if (version.is_open()) {
            std::getline(version, info.kernel_version);
            // Extract just the version number
            size_t start = info.kernel_version.find("version ");
            if (start != std::string::npos) {
                start += 8;
                size_t end = info.kernel_version.find(' ', start);
                info.kernel_version = info.kernel_version.substr(start, end - start);
            }
        }
        
        // Get hostname
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            info.hostname = hostname;
        }
        
        // Get architecture
        #if defined(__x86_64__) || defined(_M_X64)
        info.architecture = "x86_64";
        #elif defined(__aarch64__) || defined(_M_ARM64)
        info.architecture = "arm64";
        #elif defined(__i386__) || defined(_M_IX86)
        info.architecture = "i386";
        #else
        info.architecture = "unknown";
        #endif
        
        // Get uptime
        std::ifstream uptime_file("/proc/uptime");
        if (uptime_file.is_open()) {
            double uptime_seconds;
            uptime_file >> uptime_seconds;
            info.uptime_seconds = static_cast<uint64_t>(uptime_seconds);
        }
        
        // Calculate boot time
        auto now = std::time(nullptr);
        info.boot_time = now - info.uptime_seconds;
        
        return info;
    }
    
private:
    int num_cpus_;
};

// Factory function implementation
std::unique_ptr<ISystemMetrics> CreateLinuxSystemMetrics() {
    return std::make_unique<LinuxSystemMetrics>();
}

} // namespace sysmon
