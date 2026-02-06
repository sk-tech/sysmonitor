#include "sysmon/platform_interface.hpp"

#ifdef PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace sysmon {

class WindowsSystemMetrics : public ISystemMetrics {
public:
    WindowsSystemMetrics() {
        SYSTEM_INFO sysinfo;
        GetSystemInfo(&sysinfo);
        num_cpus_ = sysinfo.dwNumberOfProcessors;
    }
    
    ~WindowsSystemMetrics() override = default;
    
    CPUMetrics GetCPUMetrics() override {
        CPUMetrics metrics;
        metrics.num_cores = num_cpus_;
        
        // Get CPU usage via PDH (Performance Data Helper)
        // This is simplified - production code would maintain PDH query handles
        
        // For now, return placeholder values
        metrics.total_usage = 0.0;
        metrics.per_core_usage.resize(num_cpus_, 0.0);
        
        // Windows doesn't have load average like Unix
        metrics.load_average_1m = 0.0;
        metrics.load_average_5m = 0.0;
        metrics.load_average_15m = 0.0;
        
        metrics.context_switches = 0;
        metrics.interrupts = 0;
        
        return metrics;
    }
    
    MemoryMetrics GetMemoryMetrics() override {
        MemoryMetrics metrics = {};
        
        MEMORYSTATUSEX mem_status;
        mem_status.dwLength = sizeof(mem_status);
        
        if (GlobalMemoryStatusEx(&mem_status)) {
            metrics.total_bytes = mem_status.ullTotalPhys;
            metrics.available_bytes = mem_status.ullAvailPhys;
            metrics.used_bytes = metrics.total_bytes - metrics.available_bytes;
            metrics.free_bytes = mem_status.ullAvailPhys;
            metrics.swap_total_bytes = mem_status.ullTotalPageFile - mem_status.ullTotalPhys;
            metrics.swap_used_bytes = (mem_status.ullTotalPageFile - mem_status.ullAvailPageFile) - 
                                      (mem_status.ullTotalPhys - mem_status.ullAvailPhys);
            metrics.usage_percent = static_cast<double>(mem_status.dwMemoryLoad);
            
            // Windows doesn't separate buffers/cached like Linux
            metrics.cached_bytes = 0;
            metrics.buffers_bytes = 0;
        }
        
        return metrics;
    }
    
    std::vector<DiskMetrics> GetDiskMetrics() override {
        std::vector<DiskMetrics> disks;
        
        // Enumerate all logical drives
        DWORD drives = GetLogicalDrives();
        char drive_letter = 'A';
        
        for (int i = 0; i < 26; i++) {
            if (drives & (1 << i)) {
                std::string drive_path = std::string(1, drive_letter + i) + ":\\";
                
                UINT drive_type = GetDriveTypeA(drive_path.c_str());
                // Only include fixed drives
                if (drive_type != DRIVE_FIXED) {
                    continue;
                }
                
                ULARGE_INTEGER free_bytes, total_bytes, free_bytes_available;
                if (GetDiskFreeSpaceExA(
                    drive_path.c_str(),
                    &free_bytes_available,
                    &total_bytes,
                    &free_bytes
                )) {
                    DiskMetrics disk;
                    disk.device_name = drive_path;
                    disk.mount_point = drive_path;
                    disk.total_bytes = total_bytes.QuadPart;
                    disk.free_bytes = free_bytes.QuadPart;
                    disk.used_bytes = disk.total_bytes - disk.free_bytes;
                    disk.usage_percent = (disk.total_bytes > 0)
                        ? (100.0 * disk.used_bytes / disk.total_bytes)
                        : 0.0;
                    
                    // TODO: Get I/O stats from Performance Counters
                    disk.read_bytes = 0;
                    disk.write_bytes = 0;
                    disk.read_ops = 0;
                    disk.write_ops = 0;
                    disk.io_utilization = 0.0;
                    
                    disks.push_back(disk);
                }
            }
        }
        
        return disks;
    }
    
    std::vector<NetworkMetrics> GetNetworkMetrics() override {
        std::vector<NetworkMetrics> interfaces;
        
        // Get adapter info
        ULONG buffer_size = 0;
        GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &buffer_size);
        
        std::vector<BYTE> buffer(buffer_size);
        PIP_ADAPTER_ADDRESSES adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
        
        if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapters, &buffer_size) == NO_ERROR) {
            for (PIP_ADAPTER_ADDRESSES adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
                // Skip loopback and down interfaces
                if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
                    adapter->OperStatus != IfOperStatusUp) {
                    continue;
                }
                
                NetworkMetrics net;
                
                // Convert adapter name to UTF-8
                int size_needed = WideCharToMultiByte(
                    CP_UTF8, 0, adapter->FriendlyName, -1, nullptr, 0, nullptr, nullptr
                );
                std::string utf8_name(size_needed, 0);
                WideCharToMultiByte(
                    CP_UTF8, 0, adapter->FriendlyName, -1, &utf8_name[0], size_needed, nullptr, nullptr
                );
                net.interface_name = utf8_name;
                
                // Get statistics
                MIB_IF_ROW2 if_row;
                ZeroMemory(&if_row, sizeof(if_row));
                if_row.InterfaceIndex = adapter->IfIndex;
                
                if (GetIfEntry2(&if_row) == NO_ERROR) {
                    net.bytes_sent = if_row.OutOctets;
                    net.bytes_recv = if_row.InOctets;
                    net.packets_sent = if_row.OutUcastPkts;
                    net.packets_recv = if_row.InUcastPkts;
                    net.errors_in = if_row.InErrors;
                    net.errors_out = if_row.OutErrors;
                    net.drops_in = if_row.InDiscards;
                    net.drops_out = if_row.OutDiscards;
                    net.speed_mbps = if_row.TransmitLinkSpeed / 1000000;
                }
                
                net.is_up = (adapter->OperStatus == IfOperStatusUp);
                
                interfaces.push_back(net);
            }
        }
        
        return interfaces;
    }
    
    SystemInfo GetSystemInfo() override {
        SystemInfo info;
        
        // Get Windows version
        info.os_name = "Windows";
        
        // Get version info (requires version helper API)
        OSVERSIONINFOEXW osvi;
        ZeroMemory(&osvi, sizeof(OSVERSIONINFOEXW));
        osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);
        
        // Note: GetVersionEx is deprecated, should use VerifyVersionInfo or registry
        info.os_version = "10+";
        info.kernel_version = "NT";
        
        // Get computer name
        wchar_t computer_name[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = sizeof(computer_name) / sizeof(computer_name[0]);
        if (GetComputerNameW(computer_name, &size)) {
            int utf8_size = WideCharToMultiByte(
                CP_UTF8, 0, computer_name, -1, nullptr, 0, nullptr, nullptr
            );
            std::string utf8_name(utf8_size, 0);
            WideCharToMultiByte(
                CP_UTF8, 0, computer_name, -1, &utf8_name[0], utf8_size, nullptr, nullptr
            );
            info.hostname = utf8_name;
        }
        
        // Get architecture
        SYSTEM_INFO sysinfo;
        GetNativeSystemInfo(&sysinfo);
        switch (sysinfo.wProcessorArchitecture) {
            case PROCESSOR_ARCHITECTURE_AMD64:
                info.architecture = "x86_64";
                break;
            case PROCESSOR_ARCHITECTURE_ARM64:
                info.architecture = "arm64";
                break;
            case PROCESSOR_ARCHITECTURE_INTEL:
                info.architecture = "x86";
                break;
            default:
                info.architecture = "unknown";
                break;
        }
        
        // Get uptime
        info.uptime_seconds = GetTickCount64() / 1000;
        
        // Calculate boot time
        auto now = std::time(nullptr);
        info.boot_time = now - info.uptime_seconds;
        
        return info;
    }
    
private:
    uint32_t num_cpus_;
};

// Factory function implementation
std::unique_ptr<ISystemMetrics> CreateWindowsSystemMetrics() {
    return std::make_unique<WindowsSystemMetrics>();
}

} // namespace sysmon

#endif // PLATFORM_WINDOWS
