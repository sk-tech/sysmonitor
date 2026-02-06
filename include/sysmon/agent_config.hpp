#pragma once

#include <string>
#include <map>
#include <vector>

namespace sysmon {

enum class AgentMode {
    LOCAL,        // Store locally only
    DISTRIBUTED,  // Push to aggregator
    HYBRID        // Both local and remote
};

enum class DiscoveryMethod {
    NONE,         // No discovery (use static config)
    MDNS,         // mDNS/Bonjour
    CONSUL,       // Consul service discovery
    STATIC        // Static configuration
};

struct AgentConfig {
    AgentMode mode = AgentMode::LOCAL;
    
    // Service discovery settings
    DiscoveryMethod discovery_method = DiscoveryMethod::NONE;
    std::string consul_addr = "http://localhost:8500";
    std::string consul_service_tag;
    double discovery_timeout_seconds = 5.0;
    
    // Aggregator settings (for distributed mode)
    std::string aggregator_url;
    std::string auth_token;
    int push_interval_ms = 5000;  // How often to push to aggregator
    int max_queue_size = 1000;     // Max metrics to queue if aggregator is down
    int retry_max_attempts = 3;
    int retry_base_delay_ms = 1000;  // Base delay for exponential backoff
    
    // Host identification
    std::string hostname;  // Auto-detected if empty
    std::map<std::string, std::string> host_tags;
    
    // TLS settings
    bool tls_enabled = false;
    bool tls_verify_peer = true;
    std::string tls_ca_cert;
    
    // Timeouts
    int http_timeout_ms = 10000;
    int connection_timeout_ms = 5000;
};

class AgentConfigParser {
public:
    AgentConfigParser() = default;
    
    /**
     * @brief Load agent configuration from YAML file
     * @param config_path Path to agent.yaml
     * @return true if loaded successfully
     */
    bool LoadFromFile(const std::string& config_path);
    
    /**
     * @brief Get parsed configuration
     */
    const AgentConfig& GetConfig() const { return config_; }
    
    /**
     * @brief Validate configuration
     * @return true if config is valid
     */
    bool Validate() const;
    
    /**
     * @brief Get validation errors
     */
    const std::vector<std::string>& GetErrors() const { return errors_; }
    
    // Helper methods
    static AgentMode ParseMode(const std::string& mode_str);
    static DiscoveryMethod ParseDiscoveryMethod(const std::string& method_str);
    static std::string ModeToString(AgentMode mode);
    static std::string DiscoveryMethodToString(DiscoveryMethod method);
    static std::string GetHostname();

private:
    AgentConfig config_;
    std::vector<std::string> errors_;
    
    bool ParseYaml(const std::string& content);
    void SetDefaults();
};

} // namespace sysmon
