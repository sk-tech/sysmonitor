#include "sysmon/platform_interface.hpp"

#ifdef PLATFORM_MACOS
#include <libproc.h>
#include <sys/sysctl.h>
#include <sys/proc_info.h>
#include <signal.h>
#include <vector>
#include <string>

namespace sysmon {

class MacOSProcessMonitor : public IProcessMonitor {
public:
    MacOSProcessMonitor() = default;
    ~MacOSProcessMonitor() override = default;
    
    std::vector<ProcessInfo> GetProcessList() override {
        std::vector<ProcessInfo> processes;
        
        // Get process count
        int num_procs = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
        if (num_procs <= 0) {
            return processes;
        }
        
        // Allocate buffer for PIDs
        std::vector<pid_t> pids(num_procs);
        num_procs = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), pids.size() * sizeof(pid_t));
        
        if (num_procs <= 0) {
            return processes;
        }
        
        // Get info for each process
        for (int i = 0; i < num_procs; i++) {
            if (pids[i] == 0) {
                continue; // Skip invalid PIDs
            }
            
            auto proc_info = GetProcessDetails(pids[i]);
            if (proc_info) {
                processes.push_back(*proc_info);
            }
        }
        
        return processes;
    }
    
    std::unique_ptr<ProcessInfo> GetProcessDetails(uint32_t pid) override {
        auto info = std::make_unique<ProcessInfo>();
        info->pid = pid;
        
        // Get basic process info
        struct proc_bsdinfo proc_info;
        if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &proc_info, sizeof(proc_info)) <= 0) {
            return nullptr; // Process doesn't exist
        }
        
        info->name = proc_info.pbi_comm;
        info->ppid = proc_info.pbi_ppid;
        
        // Map process status
        switch (proc_info.pbi_status) {
            case SIDL: info->state = "Idle"; break;
            case SRUN: info->state = "Running"; break;
            case SSLEEP: info->state = "Sleeping"; break;
            case SSTOP: info->state = "Stopped"; break;
            case SZOMB: info->state = "Zombie"; break;
            default: info->state = "Unknown"; break;
        }
        
        // Get process path
        char path_buf[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_pidpath(pid, path_buf, sizeof(path_buf)) > 0) {
            info->executable = path_buf;
        }
        
        // Get task info for memory and threads
        struct proc_taskinfo task_info;
        if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &task_info, sizeof(task_info)) > 0) {
            info->memory_bytes = task_info.pti_resident_size;
            info->num_threads = task_info.pti_threadnum;
            
            // Calculate CPU percentage (simplified)
            info->cpu_percent = 0.0; // TODO: Requires sampling over time
        }
        
        // Get start time
        info->start_time = proc_info.pbi_start_tvsec;
        
        return info;
    }
    
    bool ProcessExists(uint32_t pid) override {
        struct proc_bsdinfo proc_info;
        return proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &proc_info, sizeof(proc_info)) > 0;
    }
    
    bool KillProcess(uint32_t pid, int signal) override {
        return ::kill(pid, signal) == 0;
    }
};

// Factory function implementation
std::unique_ptr<IProcessMonitor> CreateMacOSProcessMonitor() {
    return std::make_unique<MacOSProcessMonitor>();
}

} // namespace sysmon

#endif // PLATFORM_MACOS
