#include "sysmon/platform_interface.hpp"
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <cstring>

namespace sysmon {

class LinuxProcessMonitor : public IProcessMonitor {
public:
    LinuxProcessMonitor() = default;
    ~LinuxProcessMonitor() override = default;
    
    std::vector<ProcessInfo> GetProcessList() override {
        std::vector<ProcessInfo> processes;
        
        DIR* proc_dir = opendir("/proc");
        if (!proc_dir) {
            return processes;
        }
        
        struct dirent* entry;
        while ((entry = readdir(proc_dir)) != nullptr) {
            // Check if directory name is numeric (PID)
            if (entry->d_type != DT_DIR) {
                continue;
            }
            
            uint32_t pid = 0;
            try {
                pid = std::stoul(entry->d_name);
            } catch (...) {
                continue; // Not a PID directory
            }
            
            auto proc_info = GetProcessDetails(pid);
            if (proc_info) {
                processes.push_back(*proc_info);
            }
        }
        
        closedir(proc_dir);
        return processes;
    }
    
    std::unique_ptr<ProcessInfo> GetProcessDetails(uint32_t pid) override {
        auto info = std::make_unique<ProcessInfo>();
        info->pid = pid;
        
        // Read /proc/[pid]/stat for basic info
        std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
        std::ifstream stat_file(stat_path);
        if (!stat_file.is_open()) {
            return nullptr; // Process no longer exists
        }
        
        std::string line;
        std::getline(stat_file, line);
        
        // Parse stat file (format: pid (name) state ppid ...)
        size_t name_start = line.find('(');
        size_t name_end = line.rfind(')');
        if (name_start == std::string::npos || name_end == std::string::npos) {
            return nullptr;
        }
        
        info->name = line.substr(name_start + 1, name_end - name_start - 1);
        
        std::istringstream iss(line.substr(name_end + 2));
        char state_char;
        iss >> state_char >> info->ppid;
        
        switch (state_char) {
            case 'R': info->state = "Running"; break;
            case 'S': info->state = "Sleeping"; break;
            case 'D': info->state = "Disk Sleep"; break;
            case 'Z': info->state = "Zombie"; break;
            case 'T': info->state = "Stopped"; break;
            default: info->state = "Unknown"; break;
        }
        
        // Read /proc/[pid]/status for memory and threads
        std::string status_path = "/proc/" + std::to_string(pid) + "/status";
        std::ifstream status_file(status_path);
        if (status_file.is_open()) {
            while (std::getline(status_file, line)) {
                if (line.find("VmRSS:") == 0) {
                    std::istringstream line_ss(line.substr(7));
                    uint64_t mem_kb;
                    line_ss >> mem_kb;
                    info->memory_bytes = mem_kb * 1024;
                } else if (line.find("Threads:") == 0) {
                    std::istringstream line_ss(line.substr(8));
                    line_ss >> info->num_threads;
                }
            }
        }
        
        // Read /proc/[pid]/cmdline for executable
        std::string cmdline_path = "/proc/" + std::to_string(pid) + "/cmdline";
        std::ifstream cmdline_file(cmdline_path);
        if (cmdline_file.is_open()) {
            std::getline(cmdline_file, info->executable, '\0');
        }
        
        // TODO: Calculate CPU percentage (requires sampling over time)
        info->cpu_percent = 0.0;
        info->start_time = 0;
        
        return info;
    }
    
    bool ProcessExists(uint32_t pid) override {
        std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
        std::ifstream stat_file(stat_path);
        return stat_file.is_open();
    }
    
    bool KillProcess(uint32_t pid, int signal) override {
        return ::kill(pid, signal) == 0;
    }
};

// Factory function implementation
std::unique_ptr<IProcessMonitor> CreateLinuxProcessMonitor() {
    return std::make_unique<LinuxProcessMonitor>();
}

} // namespace sysmon
