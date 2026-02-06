#include "sysmon/platform_interface.hpp"
#include <stdexcept>

namespace sysmon {

// Forward declarations of platform-specific implementations
#ifdef PLATFORM_LINUX
std::unique_ptr<IProcessMonitor> CreateLinuxProcessMonitor();
std::unique_ptr<ISystemMetrics> CreateLinuxSystemMetrics();
#elif defined(PLATFORM_WINDOWS)
std::unique_ptr<IProcessMonitor> CreateWindowsProcessMonitor();
std::unique_ptr<ISystemMetrics> CreateWindowsSystemMetrics();
#elif defined(PLATFORM_MACOS)
std::unique_ptr<IProcessMonitor> CreateMacOSProcessMonitor();
std::unique_ptr<ISystemMetrics> CreateMacOSSystemMetrics();
#endif

// Factory implementations
std::unique_ptr<IProcessMonitor> CreateProcessMonitor() {
#ifdef PLATFORM_LINUX
    return CreateLinuxProcessMonitor();
#elif defined(PLATFORM_WINDOWS)
    return CreateWindowsProcessMonitor();
#elif defined(PLATFORM_MACOS)
    return CreateMacOSProcessMonitor();
#else
    throw std::runtime_error("Unsupported platform");
#endif
}

std::unique_ptr<ISystemMetrics> CreateSystemMetrics() {
#ifdef PLATFORM_LINUX
    return CreateLinuxSystemMetrics();
#elif defined(PLATFORM_WINDOWS)
    return CreateWindowsSystemMetrics();
#elif defined(PLATFORM_MACOS)
    return CreateMacOSSystemMetrics();
#else
    throw std::runtime_error("Unsupported platform");
#endif
}

} // namespace sysmon
