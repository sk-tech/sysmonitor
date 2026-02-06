/**
 * @file service_discovery.cpp
 * @brief Service discovery implementation
 */

#include "sysmon/service_discovery.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>

#ifdef PLATFORM_LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

namespace sysmon {

// ============================================================================
// MDNSDiscovery Implementation
// ============================================================================

class MDNSDiscovery::Impl {
public:
    Impl() = default;
    ~Impl() = default;
    
    std::vector<ServiceInfo> Discover(double timeout_seconds) {
#ifdef PLATFORM_LINUX
        // Use avahi-browse via system call
        // In production, would use Avahi C API directly
        std::string cmd = "avahi-browse -t -r -p _sysmon-aggregator._tcp 2>/dev/null | grep '^=' | head -n 5";
        
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            std::cerr << "mDNS discovery failed: avahi-browse not available" << std::endl;
            return {};
        }
        
        std::vector<ServiceInfo> services;
        char buffer[512];
        
        while (fgets(buffer, sizeof(buffer), pipe)) {
            // Parse avahi-browse output
            // Format: =;eth0;IPv4;hostname;_sysmon-aggregator._tcp;local;hostname.local;192.168.1.100;8080;...
            std::string line(buffer);
            if (line[0] != '=') continue;
            
            std::istringstream ss(line);
            std::string token;
            std::vector<std::string> parts;
            
            while (std::getline(ss, token, ';')) {
                parts.push_back(token);
            }
            
            if (parts.size() >= 9) {
                ServiceInfo info;
                info.name = parts[3];
                info.address = parts[7];
                info.port = static_cast<uint16_t>(std::stoi(parts[8]));
                info.protocol = "http";  // Default
                info.region = "local";
                
                services.push_back(info);
            }
        }
        
        pclose(pipe);
        
        if (!services.empty()) {
            std::cout << "mDNS: Discovered " << services.size() << " aggregator(s)" << std::endl;
        }
        
        return services;
#else
        std::cerr << "mDNS discovery not implemented for this platform" << std::endl;
        return {};
#endif
    }
    
    std::optional<ServiceInfo> DiscoverFirst(double timeout_seconds) {
        auto services = Discover(timeout_seconds);
        if (!services.empty()) {
            return services[0];
        }
        return std::nullopt;
    }
};

MDNSDiscovery::MDNSDiscovery() : impl_(std::make_unique<Impl>()) {}
MDNSDiscovery::~MDNSDiscovery() = default;

std::vector<ServiceInfo> MDNSDiscovery::Discover(double timeout_seconds) {
    return impl_->Discover(timeout_seconds);
}

std::optional<ServiceInfo> MDNSDiscovery::DiscoverFirst(double timeout_seconds) {
    return impl_->DiscoverFirst(timeout_seconds);
}

// ============================================================================
// ConsulDiscovery Implementation
// ============================================================================

ConsulDiscovery::ConsulDiscovery(const std::string& consul_addr,
                                 const std::string& service_tag)
    : consul_addr_(consul_addr), service_tag_(service_tag) {
}

ConsulDiscovery::~ConsulDiscovery() = default;

std::vector<ServiceInfo> ConsulDiscovery::Discover(double timeout_seconds) {
    // Simple HTTP GET to Consul API
    // In production, would use libcurl or dedicated HTTP client
    
    std::string url = consul_addr_ + "/v1/health/service/sysmon-aggregator?passing=true";
    if (!service_tag_.empty()) {
        url += "&tag=" + service_tag_;
    }
    
    std::string cmd = "curl -s --max-time " + std::to_string(static_cast<int>(timeout_seconds)) +
                     " '" + url + "' 2>/dev/null";
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "Consul discovery failed: curl not available" << std::endl;
        return {};
    }
    
    std::ostringstream response;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        response << buffer;
    }
    pclose(pipe);
    
    // Basic JSON parsing (in production, use json library)
    std::vector<ServiceInfo> services;
    std::string json = response.str();
    
    // Very simple parser - look for Address and Port fields
    size_t pos = 0;
    while ((pos = json.find("\"Service\":", pos)) != std::string::npos) {
        size_t addr_pos = json.find("\"Address\":\"", pos);
        size_t port_pos = json.find("\"Port\":", pos);
        
        if (addr_pos != std::string::npos && port_pos != std::string::npos) {
            addr_pos += 11;  // Skip "Address":"
            size_t addr_end = json.find("\"", addr_pos);
            
            port_pos += 7;  // Skip "Port":
            size_t port_end = json.find_first_of(",}", port_pos);
            
            if (addr_end != std::string::npos && port_end != std::string::npos) {
                ServiceInfo info;
                info.address = json.substr(addr_pos, addr_end - addr_pos);
                info.port = static_cast<uint16_t>(std::stoi(json.substr(port_pos, port_end - port_pos)));
                info.protocol = "http";
                info.name = "consul-discovered";
                info.region = service_tag_;
                
                services.push_back(info);
            }
        }
        
        pos++;
    }
    
    if (!services.empty()) {
        std::cout << "Consul: Discovered " << services.size() << " aggregator(s)" << std::endl;
    }
    
    return services;
}

std::optional<ServiceInfo> ConsulDiscovery::DiscoverFirst(double timeout_seconds) {
    auto services = Discover(timeout_seconds);
    if (!services.empty()) {
        return services[0];
    }
    return std::nullopt;
}

// ============================================================================
// StaticDiscovery Implementation
// ============================================================================

StaticDiscovery::StaticDiscovery(const std::string& aggregator_url) {
    // Parse URL: http://192.168.1.100:8080
    size_t proto_end = aggregator_url.find("://");
    if (proto_end == std::string::npos) {
        throw std::invalid_argument("Invalid URL: " + aggregator_url);
    }
    
    service_.protocol = aggregator_url.substr(0, proto_end);
    
    size_t host_start = proto_end + 3;
    size_t port_start = aggregator_url.find(":", host_start);
    
    if (port_start == std::string::npos) {
        service_.address = aggregator_url.substr(host_start);
        service_.port = (service_.protocol == "https") ? 443 : 80;
    } else {
        service_.address = aggregator_url.substr(host_start, port_start - host_start);
        
        size_t path_start = aggregator_url.find("/", port_start);
        std::string port_str;
        if (path_start == std::string::npos) {
            port_str = aggregator_url.substr(port_start + 1);
        } else {
            port_str = aggregator_url.substr(port_start + 1, path_start - port_start - 1);
        }
        service_.port = static_cast<uint16_t>(std::stoi(port_str));
    }
    
    service_.name = "static";
    service_.region = "configured";
}

StaticDiscovery::~StaticDiscovery() = default;

std::vector<ServiceInfo> StaticDiscovery::Discover(double timeout_seconds) {
    return {service_};
}

std::optional<ServiceInfo> StaticDiscovery::DiscoverFirst(double timeout_seconds) {
    return service_;
}

// ============================================================================
// Factory Functions
// ============================================================================

std::unique_ptr<IServiceDiscovery> CreateServiceDiscovery(
    DiscoveryMethod method,
    const std::string& config_value
) {
    switch (method) {
        case DiscoveryMethod::MDNS:
            return std::make_unique<MDNSDiscovery>();
            
        case DiscoveryMethod::Consul:
            if (config_value.empty()) {
                return std::make_unique<ConsulDiscovery>();
            } else {
                return std::make_unique<ConsulDiscovery>(config_value);
            }
            
        case DiscoveryMethod::Static:
            if (config_value.empty()) {
                std::cerr << "Static discovery requires aggregator URL" << std::endl;
                return nullptr;
            }
            return std::make_unique<StaticDiscovery>(config_value);
            
        case DiscoveryMethod::None:
        default:
            return nullptr;
    }
}

DiscoveryMethod ParseDiscoveryMethod(const std::string& method_str) {
    std::string lower = method_str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "mdns" || lower == "bonjour") {
        return DiscoveryMethod::MDNS;
    } else if (lower == "consul") {
        return DiscoveryMethod::Consul;
    } else if (lower == "static") {
        return DiscoveryMethod::Static;
    } else {
        return DiscoveryMethod::None;
    }
}

} // namespace sysmon
