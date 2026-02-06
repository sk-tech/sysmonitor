# SysMonitor CLI Reference - Week 5 Distributed Monitoring

## Overview

Week 5 adds distributed monitoring capabilities to the SysMonitor CLI, enabling management of multi-host deployments from a central location.

## New Commands

### Hosts Management

#### `sysmon hosts list`
Lists all hosts registered with the central aggregator.

**Example:**
```bash
$ sysmon hosts list

Registered Hosts
================
Total hosts: 3
--------------------------------------------------------------------------------
Hostname                 Platform       Version        Status         
--------------------------------------------------------------------------------
web-server-01            Linux          0.5.0          ✓ Online       
web-server-02            Linux          0.5.0          ✓ Online       
db-server-01             Linux          0.5.0          ✗ Offline      (last seen 142s ago)
--------------------------------------------------------------------------------
```

**Status Indicators:**
- `✓ Online` - Host seen within last 30 seconds
- `✗ Offline` - Host not seen for more than 30 seconds

**Error Handling:**
- If no aggregator is configured: Shows setup instructions
- If aggregator is unreachable: Displays connection error with aggregator URL

---

#### `sysmon hosts show <hostname>`
Shows detailed information about a specific host.

**Example:**
```bash
$ sysmon hosts show web-server-01

Host Details: web-server-01
============================

General Information:
  Hostname: web-server-01
  Platform: Linux
  Version: 0.5.0
  Last Seen: 5 seconds ago (Online)

Tags:
  environment: production
  role: webserver
  datacenter: us-east-1

Latest Metrics:
  CPU Usage: 15.32%
  Memory Usage: 42.18%
  Load Average (1m): 0.85
```

**Features:**
- Shows host metadata (platform, version, uptime status)
- Displays custom tags for host classification
- Shows latest metric snapshot (CPU, memory, load)

---

#### `sysmon hosts compare <host1> <host2>`
Compares current metrics between two hosts side-by-side.

**Example:**
```bash
$ sysmon hosts compare web-server-01 web-server-02

Comparing Hosts
===============

Metric              web-server-01       web-server-02       Difference     
---------------------------------------------------------------------------
CPU Usage (%)       15.32               22.45               -7.13
Memory Usage (%)    42.18               38.95               3.23
Load Avg (1m)       0.85                1.12                -0.27
---------------------------------------------------------------------------
```

**Use Cases:**
- Identify load imbalances across servers
- Verify similar configurations have similar metrics
- Troubleshoot performance differences

---

### Configuration Management

#### `sysmon config show`
Displays current agent configuration.

**Example:**
```bash
$ sysmon config show

Current Configuration
=====================
Config File: /home/user/.sysmon/agent.yaml

Mode: distributed
Hostname: web-server-01

Aggregator Settings:
  URL: http://aggregator.example.com:9000
  Push Interval: 5000 ms
  Max Queue Size: 1000
  HTTP Timeout: 10000 ms

Host Tags:
  environment: production
  datacenter: us-east-1
```

**Modes:**
- `local` - Store metrics locally only (no network)
- `distributed` - Push to aggregator (no local storage)
- `hybrid` - Both local storage and push to aggregator

---

#### `sysmon config set mode <mode>`
Switches the agent operating mode.

**Syntax:**
```bash
sysmon config set mode <local|distributed|hybrid>
```

**Example:**
```bash
$ sysmon config set mode distributed
✓ Configuration updated
Mode set to: distributed

Restart sysmond for changes to take effect:
  ./scripts/stop.sh && ./scripts/start.sh
```

**Requirements:**
- Config file must exist at `~/.sysmon/agent.yaml`
- For `distributed` mode: `aggregator_url` and `auth_token` required
- For `hybrid` mode: Same as distributed

**Error Handling:**
- Invalid mode: Shows valid options
- Missing config file: Shows setup instructions
- Missing required fields: Lists missing fields

---

## Setup Guide

### 1. Enable Distributed Monitoring

**Step 1: Create agent configuration**
```bash
cp config/agent.yaml.example ~/.sysmon/agent.yaml
```

**Step 2: Edit configuration**
```yaml
mode: distributed
aggregator_url: http://your-aggregator-host:9000
auth_token: "your-secret-token"
hostname: ""  # Auto-detected

# Optional: Add host tags
# tags:
#   environment: production
#   role: webserver
```

**Step 3: Switch to distributed mode**
```bash
sysmon config set mode distributed
```

**Step 4: Restart daemon**
```bash
./scripts/stop.sh
./scripts/start.sh
```

### 2. Start Central Aggregator

On your aggregation server:
```bash
./scripts/start-aggregator.sh
```

This starts:
- Aggregator service on port 9000
- Web dashboard on http://localhost:8000

### 3. Verify Setup

```bash
# Check configuration
sysmon config show

# List registered hosts
sysmon hosts list

# View specific host
sysmon hosts show $(hostname)
```

---

## Architecture

### Data Flow

```
┌─────────────┐         ┌─────────────┐         ┌─────────────┐
│  Agent 1    │────────▶│ Aggregator  │◀────────│  Agent 2    │
│  (Host A)   │  HTTP   │  (Central)  │  HTTP   │  (Host B)   │
└─────────────┘         └─────────────┘         └─────────────┘
                               │
                               ▼
                        ┌─────────────┐
                        │   SQLite    │
                        │   Storage   │
                        └─────────────┘
                               │
                               ▼
                        ┌─────────────┐
                        │ CLI Queries │
                        └─────────────┘
```

### HTTP Client Implementation

- **Location:** `src/utils/http_client.cpp`
- **Features:** 
  - Simple GET/POST methods
  - Timeout support (default 5s)
  - Platform-agnostic (Linux/Windows/macOS)
  - No external dependencies (uses POSIX sockets)

### API Endpoints Used

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/hosts` | GET | List all registered hosts |
| `/api/hosts/{hostname}` | GET | Get host details |
| `/api/hosts/{hostname}/metrics/latest` | GET | Get latest metrics for host |

---

## Troubleshooting

### "No aggregator configured"
**Cause:** Config file doesn't exist or mode is `local`

**Solution:**
```bash
cp config/agent.yaml.example ~/.sysmon/agent.yaml
sysmon config set mode distributed
```

### "Failed to connect to aggregator"
**Cause:** Aggregator not running or wrong URL

**Solution:**
1. Check aggregator is running:
   ```bash
   curl http://localhost:9000/api/health
   ```
2. Verify URL in config:
   ```bash
   sysmon config show
   ```
3. Start aggregator if needed:
   ```bash
   ./scripts/start-aggregator.sh
   ```

### "Host not found"
**Cause:** Host hasn't reported metrics yet or used wrong hostname

**Solution:**
1. Check host list:
   ```bash
   sysmon hosts list
   ```
2. Verify agent is running on target host:
   ```bash
   ps aux | grep sysmond
   ```

---

## Implementation Details

### JSON Parsing

The CLI uses simple string-based JSON parsing (no external libraries):

```cpp
std::string json_get_string(const std::string& json, const std::string& key);
int json_get_int(const std::string& json, const std::string& key);
double json_get_double(const std::string& json, const std::string& key);
```

**Rationale:** Keep CLI dependencies minimal (no jsoncpp/rapidjson)

### Configuration Parsing

Uses existing `AgentConfigParser` class:
- Reads YAML from `~/.sysmon/agent.yaml`
- Validates required fields based on mode
- Provides friendly error messages

### HTTP Client

Custom implementation in `src/utils/http_client.cpp`:
- Zero dependencies (stdlib only)
- Cross-platform socket API
- Simple request/response model
- Timeout support via setsockopt

**Alternative:** Could use libcurl for production, but avoided to minimize dependencies.

---

## Future Enhancements

### Planned (Week 6+)
- `sysmon hosts group <group-name>` - Create host groups
- `sysmon hosts filter tag=value` - Filter by tags
- `sysmon alerts list --host <hostname>` - Per-host alerts
- `sysmon metrics query <hostname> <metric>` - Historical queries
- TLS support for secure communication
- Authentication token management

### Performance
- Connection pooling for multiple requests
- Response caching (with TTL)
- Parallel queries for compare command

---

## Examples

### Scenario: Load Balancer Health Check
```bash
# Compare all web servers
for host in web-01 web-02 web-03; do
  echo "=== $host ==="
  sysmon hosts show $host | grep "CPU Usage:"
done

# Or use compare for two hosts
sysmon hosts compare web-01 web-02
```

### Scenario: Emergency Response
```bash
# Check all hosts quickly
sysmon hosts list

# Investigate offline host
sysmon hosts show db-server-01

# Compare to working host
sysmon hosts compare db-server-01 db-server-02
```

### Scenario: Deployment Verification
```bash
# Before deployment
sysmon hosts show app-server-01 > before.txt

# Deploy new version...

# After deployment
sysmon hosts show app-server-01 > after.txt
diff before.txt after.txt
```

---

## Testing

### Manual Testing
```bash
# Build
./build.sh

# Test help
./build/bin/sysmon

# Test config commands
./build/bin/sysmon config show
./build/bin/sysmon config set mode distributed

# Test hosts commands (requires running aggregator)
./build/bin/sysmon hosts list
./build/bin/sysmon hosts show $(hostname)
```

### Integration Testing
See `scripts/demo-distributed.sh` for full distributed demo.

---

## Related Documentation

- [Week 5 Summary](week5-summary.md) - Implementation overview
- [API Documentation](API.md) - Aggregator API reference
- [Architecture](architecture/system-design.md) - System design
- [Agent Config](../config/agent.yaml.example) - Configuration template
