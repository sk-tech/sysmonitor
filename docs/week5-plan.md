# Week 5 Plan: Distributed Multi-Host Monitoring

**Duration:** Week 5 of 8  
**Status:** 🚧 In Progress  
**Sprint Goal:** Enable monitoring multiple hosts from a centralized aggregator

## 🎯 Objectives

### 1. Network Protocol Design
- **Metric Push Protocol:** Simple HTTP/JSON for agents to push metrics to aggregator
- **Discovery:** Manual host registration (auto-discovery planned for Week 6)
- **Authentication:** Basic token-based auth (header: `X-SysMon-Token`)
- **Compression:** Optional gzip for metric payloads

### 2. Aggregator Server
- **Component:** New Python service (`sysmon-aggregator`)
- **Functions:**
  - Accept metrics from multiple agents
  - Store in centralized SQLite (with host tagging)
  - Provide unified API for all hosts
- **Schema:** Extend metrics table with `host` column (already exists!)
- **API:** Same endpoints as single-host, filtered by host parameter

### 3. Agent Configuration
- **Config File:** `~/.sysmon/agent.yaml`
  ```yaml
  agent:
    mode: distributed        # local | distributed
    aggregator_url: http://central-server:9000
    auth_token: secret123
    push_interval: 10s       # Push metrics every 10s
    host_tags:
      datacenter: us-east-1
      role: web-server
  ```

### 4. Multi-Host Dashboard
- **View Modes:**
  - Overview: All hosts summary
  - Host Detail: Individual host metrics
  - Compare: Side-by-side host comparison
- **Features:**
  - Host selector dropdown
  - Real-time status indicators (online/offline)
  - Aggregated statistics (avg CPU across fleet)

### 5. CLI Enhancements
```bash
sysmon hosts list                    # Show registered hosts
sysmon hosts show <hostname>         # Host details
sysmon hosts compare <h1> <h2>       # Compare metrics
sysmon config set mode distributed   # Switch to distributed mode
```

## 📐 Architecture

```
┌──────────────────────────────────────────────────────────┐
│                 Aggregator Server                        │
│  ┌────────────────────────────────────────────────┐     │
│  │  HTTP API (Port 9000)                          │     │
│  │  - POST /api/metrics (accept from agents)      │     │
│  │  - GET /api/hosts (list all hosts)             │     │
│  │  - GET /api/metrics?host=X (filtered metrics)  │     │
│  └────────────┬───────────────────────────────────┘     │
│               │                                          │
│  ┌────────────▼───────────────────────────────────┐     │
│  │  Aggregated SQLite Database                    │     │
│  │  metrics(timestamp, type, host, tags, value)   │     │
│  │  hosts(hostname, last_seen, tags, version)     │     │
│  └────────────────────────────────────────────────┘     │
└──────────────────────────────────────────────────────────┘
                       ▲
                       │ HTTP POST (metrics)
         ┌─────────────┼─────────────┐
         │             │             │
    ┌────┴────┐   ┌────┴────┐   ┌───┴─────┐
    │ Agent 1 │   │ Agent 2 │   │ Agent 3 │
    │ (web01) │   │ (db01)  │   │ (cache) │
    └─────────┘   └─────────┘   └─────────┘
```

## 🔧 Implementation Steps

### Phase 1: Aggregator Server (Days 1-2)
1. Create `python/sysmon/aggregator/server.py`
2. Implement `/api/metrics` POST endpoint
3. Add `hosts` table schema
4. Implement host registration and heartbeat
5. Add token authentication middleware

### Phase 2: Agent Configuration (Days 2-3)
1. Create `agent.yaml` schema and parser
2. Add `NetworkPublisher` class for pushing metrics
3. Integrate with `MetricsCollector` (callback-based)
4. Add retry logic with exponential backoff
5. Implement graceful degradation (queue metrics locally if aggregator down)

### Phase 3: Multi-Host Dashboard (Days 3-4)
1. Create `dashboard-multi.html`
2. Add host selector UI component
3. Implement fleet overview cards
4. Add host comparison charts
5. WebSocket for real-time status updates

### Phase 4: CLI & Testing (Days 4-5)
1. Add `sysmon hosts` command group
2. Write integration test script
3. Create demo with 3 virtual hosts
4. Update documentation

## 📊 Success Metrics

- **Performance:**
  - Aggregator handles 10+ agents @ 10s intervals
  - <100ms latency for metric POST
  - <1MB memory per active host
- **Reliability:**
  - Agent queues metrics for 5 minutes during aggregator downtime
  - Auto-reconnect on network failure
- **Usability:**
  - Zero-config for single-host mode (backward compatible)
  - 5-line config to enable distributed mode

## 🔒 Security Considerations

- **Week 5 (Basic):**
  - Shared secret token (environment variable)
  - HTTP only (TLS in Week 6)
  - No authentication on aggregator read API
- **Future (Week 6+):**
  - TLS/HTTPS
  - Per-host API keys
  - Role-based access control

## 📝 Files to Create

```
python/sysmon/aggregator/
  __init__.py
  server.py              # Aggregator HTTP server
  storage.py             # Multi-host storage layer
  auth.py                # Token authentication

src/core/
  network_publisher.cpp  # Push metrics to aggregator

include/sysmon/
  agent_config.hpp       # Agent configuration parser
  network_publisher.hpp

config/
  agent.yaml.example

python/sysmon/api/
  dashboard-multi.html   # Multi-host dashboard

docs/
  week5-summary.md       # Retrospective
  architecture/
    distributed-design.md
```

## 🎓 Interview Topics Demonstrated

- **Distributed Systems:**
  - Agent-aggregator architecture
  - Network protocols (HTTP/JSON)
  - Fault tolerance (queuing, retries)
  - Data aggregation patterns
- **Scalability:**
  - Multi-host data handling
  - Connection pooling
  - Batch processing
- **System Design:**
  - Push vs. pull tradeoffs
  - Centralized vs. decentralized monitoring
  - Schema design for multi-tenancy

---

## Next: Week 6 Preview

- **Advanced Features:**
  - Service discovery (mDNS/Consul)
  - TLS encryption
  - Metric federation (aggregator → aggregator)
  - Advanced anomaly detection (ML-based)
