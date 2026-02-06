# Week 6 Quick Start: Service Discovery & TLS

## Installation

### 1. Build C++ Components
```bash
cd sysmonitor
./build.sh
```

### 2. Install Python Dependencies (Optional)
```bash
# For mDNS discovery
pip3 install --user zeroconf

# Or if pip3 not available
sudo apt install python3-pip
pip3 install --user zeroconf
```

### 3. Install System Tools (Linux)
```bash
# For mDNS support
sudo apt install avahi-utils
```

## Usage Examples

### Example 1: mDNS Auto-Discovery

#### Start Aggregator with mDNS
```bash
# Start aggregator that advertises itself via mDNS
python3 -m sysmon.aggregator.server \
    --port 9000 \
    --mdns \
    --mdns-hostname "my-aggregator"
```

#### Configure Agent for Auto-Discovery
```yaml
# agent.yaml
mode: distributed
discovery_method: mdns
discovery_timeout_seconds: 5.0
auth_token: secret-token-123

# No aggregator_url needed! Agent will auto-discover.
```

#### Start Agent
```bash
# Agent will automatically find and connect to aggregator
./bin/sysmond --config agent.yaml
```

### Example 2: TLS/HTTPS Aggregator

#### Generate Certificates
```bash
./scripts/generate-certs.sh ~/.sysmon/certs 365
```

#### Start Aggregator with TLS
```bash
python3 -m sysmon.aggregator.server \
    --port 9443 \
    --tls \
    --cert ~/.sysmon/certs/server.crt \
    --key ~/.sysmon/certs/server.key
```

#### Configure Agent for HTTPS
```yaml
# agent.yaml
mode: distributed
aggregator_url: https://192.168.1.100:9443
auth_token: secret-token-123
tls_enabled: true
```

### Example 3: Consul Discovery

#### Register Aggregator in Consul
```python
from sysmon.discovery.consul_client import ConsulClient

client = ConsulClient("http://localhost:8500")
client.register_aggregator(
    port=9000,
    health_check_url="http://localhost:9000/api/health",
    tags=["production", "us-west"]
)
```

#### Configure Agent for Consul
```yaml
# agent.yaml
mode: distributed
discovery_method: consul
consul_addr: http://localhost:8500
consul_service_tag: production
auth_token: secret-token-123
```

### Example 4: Combined mDNS + TLS

#### Start Secure Aggregator with Auto-Discovery
```bash
# Generate certs first
./scripts/generate-certs.sh ~/.sysmon/certs

# Start with both TLS and mDNS
python3 -m sysmon.aggregator.server \
    --port 9443 \
    --tls \
    --cert ~/.sysmon/certs/server.crt \
    --key ~/.sysmon/certs/server.key \
    --mdns \
    --mdns-hostname "secure-aggregator"
```

#### Agent Config
```yaml
# agent.yaml
mode: distributed
discovery_method: mdns
discovery_timeout_seconds: 5.0
auth_token: secret-token-123
tls_enabled: true  # Will use HTTPS when discovered
```

## Running the Demo

### Full Service Discovery Demo
```bash
./scripts/demo-discovery.sh
```

This demo:
1. Starts aggregator with mDNS
2. Starts 3 agents with auto-discovery
3. Shows agents connecting automatically
4. Displays registered hosts via API

**Press Ctrl+C to stop**

## Testing mDNS Manually

### Check if mDNS Service is Advertised
```bash
# Browse for SysMonitor aggregators
avahi-browse -t -r _sysmon-aggregator._tcp

# You should see output like:
# +   eth0 IPv4 aggregator-01 _sysmon-aggregator._tcp local
```

### Python Discovery Test
```python
import sys
sys.path.insert(0, 'python')

from sysmon.discovery.mdns_service import MDNSDiscovery

discovery = MDNSDiscovery()
url = discovery.discover_first(timeout=5.0)
print(f"Discovered: {url}")  # http://192.168.1.100:9000
```

## Configuration Reference

### Discovery Methods

| Method | Description | Use Case |
|--------|-------------|----------|
| `none` | Static aggregator_url only | Testing, fixed infrastructure |
| `mdns` | Auto-discover via mDNS/Bonjour | Local network, zero-config |
| `consul` | Consul service registry | Enterprise, multi-datacenter |
| `static` | Explicit URL (same as none) | Fallback |

### Full Agent Config (Week 6)
```yaml
# Operating mode
mode: distributed  # local, distributed, hybrid

# Service Discovery
discovery_method: mdns  # none, mdns, consul, static
consul_addr: http://localhost:8500
consul_service_tag: production
discovery_timeout_seconds: 5.0

# Aggregator (static or fallback)
aggregator_url: http://192.168.1.100:9000
auth_token: secret-token-here

# Push settings
push_interval_ms: 5000
max_queue_size: 1000
retry_max_attempts: 3

# TLS
tls_enabled: false
tls_verify_peer: true

# Timeouts
http_timeout_ms: 10000
```

## Troubleshooting

### mDNS Not Working

**Problem:** Agent can't discover aggregator

**Solutions:**
```bash
# Check if Avahi daemon is running
systemctl status avahi-daemon

# Test manual discovery
avahi-browse -t _sysmon-aggregator._tcp

# Check firewall (allow UDP 5353)
sudo ufw allow 5353/udp

# Install missing packages
sudo apt install avahi-daemon avahi-utils
```

### TLS Certificate Errors

**Problem:** "SSL: CERTIFICATE_VERIFY_FAILED"

**Solutions:**
```bash
# Regenerate certificates
./scripts/generate-certs.sh ~/.sysmon/certs

# Check permissions
ls -l ~/.sysmon/certs/
# server.key should be 600 (rw-------)
# server.crt should be 644 (rw-r--r--)

# Fix permissions if needed
chmod 600 ~/.sysmon/certs/server.key
chmod 644 ~/.sysmon/certs/server.crt
```

### Python zeroconf Import Error

**Problem:** "ModuleNotFoundError: No module named 'zeroconf'"

**Solutions:**
```bash
# Option 1: pip3
pip3 install --user zeroconf

# Option 2: system package
sudo apt install python3-pip
pip3 install zeroconf

# Option 3: venv
python3 -m venv venv
source venv/bin/activate
pip install zeroconf
```

## Performance Notes

- **Discovery Overhead:** +0.5-1.0s at agent startup (one-time)
- **mDNS Network Traffic:** ~1KB multicast per query
- **TLS Handshake:** +50-100ms per connection
- **Certificate Validation:** +10-20ms per request

## Security Considerations

### Production Deployment

1. **Use Real Certificates**
   ```bash
   # Don't use self-signed certs in production!
   # Use Let's Encrypt or internal CA
   certbot certonly --standalone -d aggregator.example.com
   ```

2. **Enable Certificate Validation**
   ```yaml
   tls_enabled: true
   tls_verify_peer: true  # Always true in production
   tls_ca_cert: /etc/ssl/certs/ca-certificates.crt
   ```

3. **Secure Token Management**
   ```bash
   # Don't hardcode tokens in config
   export SYSMON_TOKEN=$(cat /run/secrets/sysmon_token)
   ```

4. **Network Segmentation**
   - mDNS only on trusted management network
   - Use Consul for cross-datacenter
   - Firewall rules: only allow aggregator ports

## C++ API Example (Future)

```cpp
// Coming in Week 7: Full C++ integration
#include "sysmon/service_discovery.hpp"
#include "sysmon/network_publisher.hpp"

// Auto-discover aggregator
auto discovery = CreateServiceDiscovery(DiscoveryMethod::MDNS);
auto service = discovery->DiscoverFirst(5.0);

if (service) {
    // Update agent config with discovered URL
    config.aggregator_url = service->GetURL();
    
    // Start publisher
    NetworkPublisher publisher(config);
    publisher.Start();
}
```

## Next Steps

1. **Test Discovery:** `./scripts/demo-discovery.sh`
2. **Generate Certs:** `./scripts/generate-certs.sh`
3. **Read Full Docs:** `docs/week6-summary.md`
4. **Week 7 Plan:** Multi-aggregator load balancing

## Resources

- [Week 6 Summary](docs/week6-summary.md)
- [Implementation Complete](docs/WEEK6-IMPLEMENTATION-COMPLETE.md)
- [Agent Config Example](config/agent.yaml.example)
- [mDNS RFC 6762](https://tools.ietf.org/html/rfc6762)
- [Consul Service Discovery](https://www.consul.io/docs/discovery/services)
