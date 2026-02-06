/**
 * @file service_discovery.hpp
 * @brief Service discovery interface for SysMonitor agents
 * 
 * Provides automatic discovery of aggregator services using:
 * - mDNS/Bonjour (local network)
 * - Consul (enterprise)
 * - Static configuration (fallback)
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace sysmon {

/**
 * Discovered service information
 */
struct ServiceInfo {
    std::string address;    ///< IP address
    uint16_t port;          ///< Port number
    std::string protocol;   ///< "http" or "https"
    std::string name;       ///< Service name
    std::string region;     ///< Region/zone (optional)
    
    /**
     * Get full service URL
     * @return URL (e.g., "http://192.168.1.100:8080")
     */
    std::string GetURL() const {
        return protocol + "://" + address + ":" + std::to_string(port);
    }
};

/**
 * Discovery method enumeration
 */
enum class DiscoveryMethod {
    None,       ///< No discovery (use static config)
    MDNS,       ///< mDNS/Bonjour
    Consul,     ///< Consul service discovery
    Static      ///< Static configuration
};

/**
 * Service discovery interface
 * 
 * Example usage:
 * @code
 * auto discovery = CreateServiceDiscovery(DiscoveryMethod::MDNS);
 * auto services = discovery->Discover(5.0);  // 5s timeout
 * if (!services.empty()) {
 *     std::string url = services[0].GetURL();
 *     // Connect to aggregator...
 * }
 * @endcode
 */
class IServiceDiscovery {
public:
    virtual ~IServiceDiscovery() = default;
    
    /**
     * Discover available aggregator services
     * @param timeout_seconds Discovery timeout
     * @return List of discovered services
     */
    virtual std::vector<ServiceInfo> Discover(double timeout_seconds = 5.0) = 0;
    
    /**
     * Discover first available aggregator
     * @param timeout_seconds Discovery timeout
     * @return First service or nullopt if none found
     */
    virtual std::optional<ServiceInfo> DiscoverFirst(double timeout_seconds = 5.0) = 0;
};

/**
 * mDNS/Bonjour service discovery
 * 
 * Uses system DNS-SD on macOS/iOS, Avahi on Linux.
 * Service type: _sysmon-aggregator._tcp.local.
 */
class MDNSDiscovery : public IServiceDiscovery {
public:
    MDNSDiscovery();
    ~MDNSDiscovery() override;
    
    std::vector<ServiceInfo> Discover(double timeout_seconds = 5.0) override;
    std::optional<ServiceInfo> DiscoverFirst(double timeout_seconds = 5.0) override;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Consul service discovery
 * 
 * Queries Consul HTTP API for healthy aggregator services.
 */
class ConsulDiscovery : public IServiceDiscovery {
public:
    /**
     * Create Consul discovery client
     * @param consul_addr Consul agent address (default: "http://localhost:8500")
     * @param service_tag Optional tag filter
     */
    explicit ConsulDiscovery(const std::string& consul_addr = "http://localhost:8500",
                            const std::string& service_tag = "");
    ~ConsulDiscovery() override;
    
    std::vector<ServiceInfo> Discover(double timeout_seconds = 5.0) override;
    std::optional<ServiceInfo> DiscoverFirst(double timeout_seconds = 5.0) override;
    
private:
    std::string consul_addr_;
    std::string service_tag_;
};

/**
 * Static configuration discovery (fallback)
 * 
 * Returns pre-configured aggregator URL from config file.
 */
class StaticDiscovery : public IServiceDiscovery {
public:
    /**
     * Create static discovery with single aggregator URL
     * @param aggregator_url Full URL (e.g., "http://192.168.1.100:8080")
     */
    explicit StaticDiscovery(const std::string& aggregator_url);
    ~StaticDiscovery() override;
    
    std::vector<ServiceInfo> Discover(double timeout_seconds = 5.0) override;
    std::optional<ServiceInfo> DiscoverFirst(double timeout_seconds = 5.0) override;
    
private:
    ServiceInfo service_;
};

/**
 * Factory function for creating service discovery instances
 * @param method Discovery method
 * @param config_value Configuration value (URL for static, Consul addr for Consul)
 * @return Discovery instance or nullptr on error
 */
std::unique_ptr<IServiceDiscovery> CreateServiceDiscovery(
    DiscoveryMethod method,
    const std::string& config_value = ""
);

/**
 * Parse discovery method from string
 * @param method_str Method name ("none", "mdns", "consul", "static")
 * @return Discovery method or None if invalid
 */
DiscoveryMethod ParseDiscoveryMethod(const std::string& method_str);

} // namespace sysmon
