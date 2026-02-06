# System Architecture Design
## Cross-Platform System Monitor & Performance Analyzer

**Version:** 1.0  
**Date:** February 6, 2026  
**Author:** System Architect

---

## 1. Architecture Overview

### 1.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                          User Layer                              │
├──────────────┬──────────────┬──────────────┬────────────────────┤
│ Web Dashboard│  CLI Tool    │  REST API    │  WebSocket Client  │
└──────┬───────┴──────┬───────┴──────┬───────┴──────┬─────────────┘
       │              │              │              │
       └──────────────┴──────────────┴──────────────┘
                           │
       ┌───────────────────▼───────────────────────────┐
       │          Python Application Layer             │
       │  - FastAPI/Flask Web Server                   │
       │  - Analytics Engine (Anomaly Detection)       │
       │  - Alert Manager                              │
       │  - Data Export/Import                         │
       └───────────────────┬───────────────────────────┘
                           │ (pybind11)
       ┌───────────────────▼───────────────────────────┐
       │          C++ Core Monitoring Engine           │
       │  - Process Monitor                            │
       │  - CPU/Memory Collector                       │
       │  - Disk I/O Collector                         │
       │  - Network Collector                          │
       │  - Platform Abstraction Layer (PAL)           │
       └───────────────────┬───────────────────────────┘
                           │
       ┌───────────────────▼───────────────────────────┐
       │       Platform-Specific Implementations       │
       ├─────────────┬─────────────┬───────────────────┤
       │   Linux     │   Windows   │     macOS         │
       │  (/proc,    │  (WinAPI,   │  (sysctl,         │
       │   sysfs)    │   PDH)      │   libproc)        │
       └─────────────┴─────────────┴───────────────────┘
                           │
       ┌───────────────────▼───────────────────────────┐
       │           Data Persistence Layer              │
       │  - SQLite Time-Series Database                │
       │  - Configuration Files (YAML/JSON)            │
       │  - Log Files (Structured JSON logs)           │
       └───────────────────────────────────────────────┘
```

### 1.2 Component Interaction

```
┌──────────────┐    IPC/Shared Memory     ┌──────────────┐
│  Daemon      │◄────────────────────────►│  CLI Client  │
│  (sysmond)   │                          │  (sysmon)    │
└──────┬───────┘                          └──────────────┘
       │
       │ Metrics Pipeline
       ▼
┌──────────────────────────────────────────────────────┐
│  Metrics Collection Thread Pool                      │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐             │
│  │ Process │  │  CPU/   │  │ Network │             │
│  │ Monitor │  │  Memory │  │  I/O    │             │
│  └────┬────┘  └────┬────┘  └────┬────┘             │
└───────┼───────────┼────────────┼───────────────────┘
        │           │            │
        └───────────┴────────────┘
                    │
                    ▼
         ┌──────────────────────┐
         │  Metrics Aggregator  │
         │  (Ring Buffer)       │
         └──────────┬───────────┘
                    │
         ┌──────────▼───────────┐
         │   Storage Manager    │
         │   (Batch Writer)     │
         └──────────┬───────────┘
                    │
                    ▼
            ┌──────────────┐
            │ SQLite DB    │
            │ (WAL mode)   │
            └──────────────┘
```

---

## 2. Component Design

### 2.1 C++ Core Monitoring Engine

#### 2.1.1 Platform Abstraction Layer (PAL)

**Purpose**: Abstract OS-specific system calls behind unified interface

**Key Interfaces**:
```cpp
// src/core/platform_interface.hpp
namespace sysmon {

class IProcessMonitor {
public:
    virtual ~IProcessMonitor() = default;
    virtual std::vector<ProcessInfo> GetProcessList() = 0;
    virtual ProcessDetails GetProcessDetails(pid_t pid) = 0;
    virtual bool KillProcess(pid_t pid, int signal) = 0;
};

class ISystemMetrics {
public:
    virtual ~ISystemMetrics() = default;
    virtual CPUMetrics GetCPUMetrics() = 0;
    virtual MemoryMetrics GetMemoryMetrics() = 0;
    virtual std::vector<DiskMetrics> GetDiskMetrics() = 0;
    virtual std::vector<NetworkMetrics> GetNetworkMetrics() = 0;
};

// Factory pattern for platform selection
std::unique_ptr<IProcessMonitor> CreateProcessMonitor();
std::unique_ptr<ISystemMetrics> CreateSystemMetrics();

} // namespace sysmon
```

**Implementation Strategy**:
- Linux: Read `/proc/stat`, `/proc/meminfo`, `/proc/net/dev`
- Windows: Use `EnumProcesses()`, `GetSystemInfo()`, PDH API
- macOS: Use `sysctl()`, `proc_listpids()`, `host_statistics64()`

#### 2.1.2 Metrics Collector

**Threading Model**: Thread pool with lock-free ring buffer for metric passing

```cpp
// Pseudo-code architecture
class MetricsCollector {
private:
    ThreadPool worker_pool_;
    RingBuffer<Metric> metric_buffer_; // SPMC queue
    std::atomic<bool> running_;
    
public:
    void Start() {
        worker_pool_.Submit([this]() { CollectProcessMetrics(); });
        worker_pool_.Submit([this]() { CollectSystemMetrics(); });
        // ... other collectors
    }
    
    void CollectProcessMetrics() {
        while (running_) {
            auto processes = monitor_->GetProcessList();
            for (auto& proc : processes) {
                metric_buffer_.Push(Metric{proc});
            }
            std::this_thread::sleep_for(1s);
        }
    }
};
```

### 2.2 Python Application Layer

#### 2.2.1 Web API Server (FastAPI)

**Endpoints**:
```python
# python/api/server.py
from fastapi import FastAPI, WebSocket
from datetime import datetime, timedelta

app = FastAPI()

# REST API
@app.get("/api/v1/metrics/current")
async def get_current_metrics():
    """Real-time snapshot of all metrics"""
    return await collector.get_snapshot()

@app.get("/api/v1/metrics/history")
async def get_metric_history(
    metric: str,
    start: datetime,
    end: datetime,
    resolution: str = "1m"
):
    """Historical time-series data"""
    return await db.query_range(metric, start, end, resolution)

# WebSocket for streaming
@app.websocket("/ws/metrics")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    async for metric in collector.stream():
        await websocket.send_json(metric)
```

#### 2.2.2 Time-Series Database

**Schema**:
```sql
-- SQLite schema optimized for time-series
CREATE TABLE metrics (
    timestamp INTEGER NOT NULL,  -- Unix timestamp
    metric_type TEXT NOT NULL,   -- 'cpu', 'memory', 'disk', 'network'
    host TEXT NOT NULL,
    tags TEXT,                   -- JSON blob for flexible tagging
    value REAL NOT NULL,
    PRIMARY KEY (timestamp, metric_type, host)
) WITHOUT ROWID;

CREATE INDEX idx_metric_time ON metrics(metric_type, timestamp);
CREATE INDEX idx_host_time ON metrics(host, timestamp);

-- Retention policy: Partition by day, drop old partitions
CREATE TABLE metrics_2026_02_06 AS SELECT * FROM metrics WHERE 1=0;
```

**Data Retention**:
- 1-second resolution: 24 hours
- 1-minute rollup: 30 days
- 1-hour rollup: 1 year

---

## 3. Data Flow

### 3.1 Metric Collection Pipeline

```
OS Kernel
    ↓ (system calls)
Platform-Specific Collector (C)
    ↓ (struct copy)
Platform Abstraction Layer (C++)
    ↓ (method call)
Metrics Collector (C++)
    ↓ (ring buffer)
Aggregator Thread (C++)
    ↓ (batch write)
SQLite Database
    ↓ (pybind11 query)
Python Analytics Layer
    ↓ (WebSocket/REST)
Web Dashboard / CLI
```

### 3.2 Alert Flow

```
Metric Collection → Threshold Check → Alert Manager
                                           ↓
                    ┌──────────────────────┴──────────────────┐
                    ↓                      ↓                   ↓
              Email Sender          Webhook POST         Log to File
```

---

## 4. Deployment Architecture

### 4.1 Standalone Mode (Single Host)

```
┌────────────────────────────────────────┐
│          Target Machine                 │
│                                         │
│  ┌──────────────────────────────────┐  │
│  │  sysmond (daemon/service)        │  │
│  │  - Listening on 127.0.0.1:8080   │  │
│  │  - SQLite DB: ~/.sysmon/data.db  │  │
│  └──────────────────────────────────┘  │
│            ↑                            │
│            │ HTTP                       │
│  ┌─────────┴──────────┐                │
│  │  Web Browser       │                │
│  │  http://localhost  │                │
│  └────────────────────┘                │
└────────────────────────────────────────┘
```

### 4.2 Distributed Mode (Multi-Host)

```
┌─────────────────────────────────────────────────────────┐
│                  Central Server                          │
│  ┌────────────────────────────────────────────────────┐ │
│  │  sysmon-server                                     │ │
│  │  - Aggregates metrics from agents                 │ │
│  │  - Web dashboard on port 8080                     │ │
│  │  - PostgreSQL/TimescaleDB for storage             │ │
│  └────────────────────────────────────────────────────┘ │
└───────────────────────────┬─────────────────────────────┘
                            │ (TCP + Protocol Buffers)
          ┌─────────────────┼─────────────────┐
          ↓                 ↓                 ↓
┌─────────────────┐  ┌─────────────┐  ┌─────────────┐
│  Agent Host 1   │  │ Agent Host 2│  │ Agent Host N│
│  sysmon-agent   │  │ sysmon-agent│  │ sysmon-agent│
└─────────────────┘  └─────────────┘  └─────────────┘
```

---

## 5. Technology Stack

### 5.1 Core Components

| Layer | Technology | Rationale |
|-------|------------|-----------|
| System API | C (Linux syscalls, WinAPI) | Direct OS interface, maximum performance |
| Core Engine | C++17 | RAII, templates, STL containers, cross-platform |
| Bindings | pybind11 | Seamless C++/Python interop |
| Application | Python 3.8+ | Rapid development, rich ecosystem |
| Web Framework | FastAPI | Async support, auto-generated OpenAPI docs |
| Database | SQLite (standalone), PostgreSQL (distributed) | Embedded DB, ACID guarantees |
| Frontend | React + Chart.js | Responsive UI, real-time charts |
| Build System | CMake 3.15+ | Cross-platform, industry standard |
| Testing | Google Test (C++), pytest (Python) | Comprehensive test frameworks |
| CI/CD | GitHub Actions | Free for open source, multi-OS runners |

### 5.2 Key Libraries

**C++ Dependencies**:
- `boost::asio` (optional, for async I/O)
- `spdlog` (structured logging)
- `nlohmann/json` (JSON parsing)
- `CLI11` (command-line parsing)

**Python Dependencies**:
```python
# requirements.txt
fastapi>=0.100.0
uvicorn[standard]>=0.23.0
sqlalchemy>=2.0.0
pybind11>=2.11.0
pyyaml>=6.0
aiofiles>=23.0.0
websockets>=11.0
pytest>=7.4.0
httpx>=0.24.0  # for testing
```

---

## 6. Security Considerations

### 6.1 Threat Model

| Threat | Mitigation |
|--------|------------|
| Unauthorized metric access | JWT authentication on API, localhost-only by default |
| Privilege escalation | Run as non-root, request sudo only for specific operations |
| Code injection via config | Strict YAML validation, no eval() |
| DoS via API flooding | Rate limiting (100 req/min per IP) |
| Data leakage | HTTPS enforcement, no sensitive data in logs |

### 6.2 Secure Defaults

- Web API disabled by default (enable via config)
- Admin operations require explicit authentication
- Config files readable only by owner (chmod 600)
- TLS certificate validation for distributed mode

---

## 7. Performance Optimization

### 7.1 Target Metrics

| Metric | Target | Measurement |
|--------|--------|-------------|
| Collection overhead | <2% CPU | Compared to idle system |
| Memory footprint | <50MB RSS | Measured by `ps` |
| Metric latency | <100ms | From collection to storage |
| Dashboard load time | <2s | Time to interactive |
| Concurrent clients | 100+ | WebSocket connections |

### 7.2 Optimization Strategies

1. **Lock-Free Data Structures**: Use `std::atomic` and ring buffers for inter-thread communication
2. **Batch Processing**: Aggregate metrics before DB writes (reduce I/O)
3. **Lazy Evaluation**: Compute expensive metrics only when requested
4. **Connection Pooling**: Reuse HTTP connections for distributed agents
5. **Incremental Updates**: Delta encoding for WebSocket messages

---

## 8. Error Handling & Reliability

### 8.1 Failure Modes

| Scenario | Behavior | Recovery |
|----------|----------|----------|
| Permission denied reading /proc | Log warning, continue with available data | Prompt for elevated privileges |
| Database write failure | Buffer metrics in memory (max 10k entries) | Retry writes, drop oldest on overflow |
| Network timeout (distributed) | Mark agent as offline, retain local cache | Reconnect with exponential backoff |
| Python exception in analytics | Log traceback, disable affected feature | Continue core monitoring |
| Out of memory | Reduce collection frequency, trigger GC | Alert user, attempt graceful degradation |

### 8.2 Health Monitoring

```python
# Internal health checks
@app.get("/health")
async def health_check():
    return {
        "status": "healthy",
        "uptime_seconds": get_uptime(),
        "collector_status": collector.is_running(),
        "db_connection": db.check(),
        "memory_mb": get_process_memory(),
        "metrics_collected": collector.total_count(),
    }
```

---

## 9. Testing Strategy

### 9.1 Test Pyramid

```
             ┌─────────────────┐
             │  System Tests   │  ← E2E, multi-platform
             │  (10%)          │
             └─────────────────┘
            ┌───────────────────┐
            │ Integration Tests │  ← API, database, IPC
            │     (30%)         │
            └───────────────────┘
       ┌────────────────────────────┐
       │      Unit Tests            │  ← Individual functions
       │        (60%)               │
       └────────────────────────────┘
```

### 9.2 Test Coverage Requirements

- **Unit Tests**: 80%+ line coverage (measured by gcov/lcov)
- **Integration Tests**: All API endpoints, database operations
- **System Tests**: Install, start, stop, upgrade scenarios
- **Platform Tests**: Verify on Ubuntu, Windows, macOS

---

## 10. Build & Deployment

### 10.1 Build Matrix

| Platform | Architecture | Compiler | Package Format |
|----------|--------------|----------|----------------|
| Ubuntu 22.04 | x86_64 | GCC 11 | .deb, .tar.gz |
| Ubuntu 22.04 | ARM64 | GCC 11 | .deb, .tar.gz |
| RHEL 8 | x86_64 | GCC 8 | .rpm, .tar.gz |
| Windows 11 | x86_64 | MSVC 2022 | .msi, .zip |
| macOS 13 | x86_64 | Clang 14 | .dmg, .tar.gz |
| macOS 13 | ARM64 | Clang 14 | .dmg, .tar.gz |

### 10.2 Static Linking Strategy

```cmake
# CMakeLists.txt - Static linking for portability
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)

# Link statically against C++ runtime
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()
```

---

## 11. Future Architecture Enhancements

### Phase 2 (Post v1.0)
- Kubernetes operator for pod monitoring
- Time-series compression (Gorilla algorithm)
- Machine learning anomaly detection (isolation forest)
- gRPC for inter-service communication

### Phase 3
- Distributed tracing integration (OpenTelemetry)
- Cloud-native deployment (Helm charts)
- Multi-tenancy support
- GPU monitoring (NVML API)

---

**Document Control:**
- **Last Updated**: 2026-02-06
- **Review Cycle**: Every sprint
- **Next Review**: 2026-02-13
