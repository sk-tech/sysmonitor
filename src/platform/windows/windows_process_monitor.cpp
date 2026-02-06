#include "sysmon/platform_interface.hpp"

#ifdef PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>

namespace sysmon {

class WindowsProcessMonitor : public IProcessMonitor {
public:
    WindowsProcessMonitor() = default;
    ~WindowsProcessMonitor() override = default;
    
    std::vector<ProcessInfo> GetProcessList() override {
        std::vector<ProcessInfo> processes;
        
        // Create snapshot of all processes
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return processes;
        }
        
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        
        if (Process32FirstW(snapshot, &pe32)) {
            do {
                auto proc_info = GetProcessDetails(pe32.th32ProcessID);
                if (proc_info) {
                    processes.push_back(*proc_info);
                }
            } while (Process32NextW(snapshot, &pe32));
        }
        
        CloseHandle(snapshot);
        return processes;
    }
    
    std::unique_ptr<ProcessInfo> GetProcessDetails(uint32_t pid) override {
        auto info = std::make_unique<ProcessInfo>();
        info->pid = pid;
        
        // Open process handle
        HANDLE hProcess = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
            FALSE,
            pid
        );
        
        if (!hProcess) {
            return nullptr; // Cannot access process
        }
        
        // Get process name
        wchar_t name_buf[MAX_PATH];
        DWORD name_len = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, name_buf, &name_len)) {
            // Convert wide string to UTF-8
            int size_needed = WideCharToMultiByte(
                CP_UTF8, 0, name_buf, -1, nullptr, 0, nullptr, nullptr
            );
            std::string utf8_name(size_needed, 0);
            WideCharToMultiByte(
                CP_UTF8, 0, name_buf, -1, &utf8_name[0], size_needed, nullptr, nullptr
            );
            info->executable = utf8_name;
            
            // Extract just filename
            size_t last_slash = info->executable.find_last_of("\\/");
            if (last_slash != std::string::npos) {
                info->name = info->executable.substr(last_slash + 1);
            } else {
                info->name = info->executable;
            }
        }
        
        // Get memory info
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            info->memory_bytes = pmc.WorkingSetSize;
        }
        
        // Get thread count
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te32;
            te32.dwSize = sizeof(THREADENTRY32);
            
            uint32_t thread_count = 0;
            if (Thread32First(snapshot, &te32)) {
                do {
                    if (te32.th32OwnerProcessID == pid) {
                        thread_count++;
                    }
                } while (Thread32Next(snapshot, &te32));
            }
            info->num_threads = thread_count;
            CloseHandle(snapshot);
        }
        
        // Get process times for CPU calculation
        FILETIME create_time, exit_time, kernel_time, user_time;
        if (GetProcessTimes(hProcess, &create_time, &exit_time, &kernel_time, &user_time)) {
            ULARGE_INTEGER create_time_int;
            create_time_int.LowPart = create_time.dwLowDateTime;
            create_time_int.HighPart = create_time.dwHighDateTime;
            // Convert to Unix timestamp (approximate)
            info->start_time = create_time_int.QuadPart / 10000000ULL - 11644473600ULL;
        }
        
        // TODO: Calculate CPU percentage
        info->cpu_percent = 0.0;
        info->state = "Running"; // Windows doesn't have same state model as Linux
        info->ppid = 0; // Would need to get from snapshot
        
        CloseHandle(hProcess);
        return info;
    }
    
    bool ProcessExists(uint32_t pid) override {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProcess) {
            CloseHandle(hProcess);
            return true;
        }
        return false;
    }
    
    bool KillProcess(uint32_t pid, int signal) override {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!hProcess) {
            return false;
        }
        
        BOOL result = TerminateProcess(hProcess, 1);
        CloseHandle(hProcess);
        return result != 0;
    }
};

// Factory function implementation
std::unique_ptr<IProcessMonitor> CreateWindowsProcessMonitor() {
    return std::make_unique<WindowsProcessMonitor>();
}

} // namespace sysmon

#endif // PLATFORM_WINDOWS
