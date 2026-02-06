#include "sysmon/platform_interface.hpp"

#ifdef PLATFORM_MACOS
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <vector>
#include <string>
#include <cstring>

namespace sysmon {

class MacOSSystemMetrics : public ISystemMetrics {
public:
    MacOSSystemMetrics() {
        int mib[2] = {CTL_HW, HW_NCPU};
        size_t len = sizeof(num_cpus_);
        sysctl(mib, 2, &num_cpus_, &len, nullptr, 0);
    }
    
    ~MacOSSystemMetrics() override = default;
    
    CPUMetrics GetCPUMetrics() override {
        CPUMetrics metrics;
        metrics.num_cores = num_cpus_;
        
        // Get CPU load info using Mach API
        host_cpu_load_info_data_t cpu_info;
        mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
        
        if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                           (host_info_t)&cpu_info, &count) == KERN_SUCCESS) {
            uint64_t total_ticks = 0;
            for (int i = 0; i < CPU_STATE_MAX; i++) {
                total_ticks += cpu_info.cpu_ticks[i];
            }
            
            uint64_t idle_ticks = cpu_info.cpu_ticks[CPU_STATE_IDLE];
            uint64_t busy_ticks = total_ticks - idle_ticks;
            
            metrics.total_usage = (total_ticks > 0)
                ? (100.0 * busy_ticks / total_ticks)
                : 0.0;
        }
        
        // Per-core usage would require processor_info API
        metrics.per_core_usage.resize(num_cpus_, metrics.total_usage / num_cpus_);
        
        // Get load average
        struct loadavg load_info;
        int mib[2] = {CTL_VM, VM_LOADAVG};
        size_t len = sizeof(load_info);
        if (sysctl(mib, 2, &load_info, &len, nullptr, 0) == 0) {
            metrics.load_average_1m = load_info.ldavg[0] / static_cast<double>(load_info.fscale);
            metrics.load_average_5m = load_info.ldavg[1] / static_cast<double>(load_info.fscale);
            metrics.load_average_15m = load_info.ldavg[2] / static_cast<double>(load_info.fscale);
        }
        
        // Context switches and interrupts (not easily available on macOS)
        metrics.context_switches = 0;
        metrics.interrupts = 0;
        
        return metrics;
    }
    
    MemoryMetrics GetMemoryMetrics() override {
        MemoryMetrics metrics = {};
        
        // Get physical memory
        int mib[2] = {CTL_HW, HW_MEMSIZE};
        size_t len = sizeof(metrics.total_bytes);
        sysctl(mib, 2, &metrics.total_bytes, &len, nullptr, 0);
        
        // Get VM statistics
        vm_statistics64_data_t vm_stats;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                             (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
            vm_size_t page_size;
            host_page_size(mach_host_self(), &page_size);
            
            metrics.free_bytes = vm_stats.free_count * page_size;
            metrics.used_bytes = (vm_stats.active_count + vm_stats.wire_count) * page_size;
            metrics.cached_bytes = vm_stats.external_page_count * page_size;
            metrics.available_bytes = metrics.free_bytes + vm_stats.inactive_count * page_size;
            
            metrics.usage_percent = (metrics.total_bytes > 0)
                ? (100.0 * metrics.used_bytes / metrics.total_bytes)
                : 0.0;
        }
        
        // Get swap info
        struct xsw_usage swap_info;
        len = sizeof(swap_info);
        int swap_mib[2] = {CTL_VM, VM_SWAPUSAGE};
        if (sysctl(swap_mib, 2, &swap_info, &len, nullptr, 0) == 0) {
            metrics.swap_total_bytes = swap_info.xsu_total;
            metrics.swap_used_bytes = swap_info.xsu_used;
        }
        
        metrics.buffers_bytes = 0; // Not separately tracked on macOS
        
        return metrics;
    }
    
    std::vector<DiskMetrics> GetDiskMetrics() override {
        std::vector<DiskMetrics> disks;
        
        // Get mounted filesystems
        struct statfs *mounts;
        int num_mounts = getmntinfo(&mounts, MNT_NOWAIT);
        
        for (int i = 0; i < num_mounts; i++) {
            // Skip non-local filesystems
            if (!(mounts[i].f_flags & MNT_LOCAL)) {
                continue;
            }
            
            // Skip special filesystems
            std::string fs_type = mounts[i].f_fstypename;
            if (fs_type == "devfs" || fs_type == "autofs") {
                continue;
            }
            
            DiskMetrics disk;
            disk.device_name = mounts[i].f_mntfromname;
            disk.mount_point = mounts[i].f_mntonname;
            disk.total_bytes = mounts[i].f_blocks * mounts[i].f_bsize;
            disk.free_bytes = mounts[i].f_bfree * mounts[i].f_bsize;
            disk.used_bytes = disk.total_bytes - disk.free_bytes;
            disk.usage_percent = (disk.total_bytes > 0)
                ? (100.0 * disk.used_bytes / disk.total_bytes)
                : 0.0;
            
            // I/O stats would require IOKit framework (more complex)
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
        
        struct ifaddrs *ifap, *ifa;
        if (getifaddrs(&ifap) != 0) {
            return interfaces;
        }
        
        for (ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_LINK) {
                continue;
            }
            
            NetworkMetrics net;
            net.interface_name = ifa->ifa_name;
            
            // Get interface data using sysctl
            int mib[6];
            mib[0] = CTL_NET;
            mib[1] = PF_ROUTE;
            mib[2] = 0;
            mib[3] = 0;
            mib[4] = NET_RT_IFLIST2;
            mib[5] = if_nametoindex(ifa->ifa_name);
            
            size_t len;
            if (sysctl(mib, 6, nullptr, &len, nullptr, 0) == 0) {
                std::vector<char> buf(len);
                if (sysctl(mib, 6, buf.data(), &len, nullptr, 0) == 0) {
                    struct if_msghdr2 *ifm = (struct if_msghdr2 *)buf.data();
                    
                    net.bytes_recv = ifm->ifm_data.ifi_ibytes;
                    net.bytes_sent = ifm->ifm_data.ifi_obytes;
                    net.packets_recv = ifm->ifm_data.ifi_ipackets;
                    net.packets_sent = ifm->ifm_data.ifi_opackets;
                    net.errors_in = ifm->ifm_data.ifi_ierrors;
                    net.errors_out = ifm->ifm_data.ifi_oerrors;
                    net.drops_in = ifm->ifm_data.ifi_iqdrops;
                    net.drops_out = 0; // Not available
                    net.speed_mbps = ifm->ifm_data.ifi_baudrate / 1000000;
                    net.is_up = (ifm->ifm_flags & IFF_UP) != 0;
                }
            }
            
            interfaces.push_back(net);
        }
        
        freeifaddrs(ifap);
        return interfaces;
    }
    
    SystemInfo GetSystemInfo() override {
        SystemInfo info;
        
        // Get macOS version
        info.os_name = "macOS";
        
        char os_version[256];
        size_t len = sizeof(os_version);
        if (sysctlbyname("kern.osproductversion", os_version, &len, nullptr, 0) == 0) {
            info.os_version = os_version;
        }
        
        // Get kernel version
        char kernel_version[256];
        len = sizeof(kernel_version);
        if (sysctlbyname("kern.osrelease", kernel_version, &len, nullptr, 0) == 0) {
            info.kernel_version = kernel_version;
        }
        
        // Get hostname
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            info.hostname = hostname;
        }
        
        // Get architecture
        char machine[256];
        len = sizeof(machine);
        if (sysctlbyname("hw.machine", machine, &len, nullptr, 0) == 0) {
            info.architecture = machine;
        }
        
        // Get boot time
        struct timeval boot_time;
        len = sizeof(boot_time);
        int mib[2] = {CTL_KERN, KERN_BOOTTIME};
        if (sysctl(mib, 2, &boot_time, &len, nullptr, 0) == 0) {
            info.boot_time = boot_time.tv_sec;
            auto now = std::time(nullptr);
            info.uptime_seconds = now - info.boot_time;
        }
        
        return info;
    }
    
private:
    int num_cpus_;
};

// Factory function implementation
std::unique_ptr<ISystemMetrics> CreateMacOSSystemMetrics() {
    return std::make_unique<MacOSSystemMetrics>();
}

} // namespace sysmon

#endif // PLATFORM_MACOS
