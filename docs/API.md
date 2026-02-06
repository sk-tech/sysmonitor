# SysMonitor API Documentation

## Overview
The SysMonitor API provides RESTful endpoints for accessing system metrics and a real-time web dashboard.

**Base URL:** `http://localhost:8000`

## Endpoints

### Health Check
```
GET /api/health
```
Returns server health status and storage availability.

**Response:**
```json
{
  "status": "ok",
  "storage": true
}
```

---

### Get Latest Metric
```
GET /api/metrics/latest?metric=<metric_type>&host=<hostname>
```
Retrieve the most recent value for a specific metric.

**Parameters:**
- `metric` (required): Metric type (e.g., `cpu.total_usage`)
- `host` (optional): Filter by hostname

**Example:**
```bash
curl "http://localhost:8000/api/metrics/latest?metric=cpu.total_usage"
```

**Response:**
```json
{
  "timestamp": 1770400316,
  "metric_type": "cpu.total_usage",
  "host": "LIN51013423",
  "tags": "",
  "value": 4.93,
  "datetime": "2026-02-06T17:51:56"
}
```

---

### Get Metric History
```
GET /api/metrics/history?metric=<metric_type>&duration=<duration>&limit=<max_results>
```
Query historical data for a metric over a time range.

**Parameters:**
- `metric` (required): Metric type
- `duration` (optional): Time range, e.g., `1h`, `30m`, `24h`, `7d` (default: `1h`)
- `limit` (optional): Maximum results to return (default: `100`)
- `host` (optional): Filter by hostname

**Example:**
```bash
curl "http://localhost:8000/api/metrics/history?metric=memory.usage_percent&duration=1h&limit=10"
```

**Response:**
```json
{
  "data": [
    {
      "timestamp": 1770400316,
      "metric_type": "memory.usage_percent",
      "host": "LIN51013423",
      "tags": "",
      "value": 15.71,
      "datetime": "2026-02-06T17:51:56"
    },
    ...
  ]
}
```

---

### List Available Metrics
```
GET /api/metrics/types
```
Get all available metric types in the database.

**Example:**
```bash
curl "http://localhost:8000/api/metrics/types"
```

**Response:**
```json
{
  "metrics": [
    "cpu.context_switches",
    "cpu.core_usage",
    "cpu.interrupts",
    "cpu.load_avg_15m",
    "cpu.load_avg_1m",
    "cpu.load_avg_5m",
    "cpu.num_cores",
    "cpu.total_usage",
    "disk.free_bytes",
    "memory.usage_percent",
    ...
  ]
}
```

---

### Real-Time Streaming (SSE)
```
GET /api/stream
```
Server-Sent Events stream for real-time metrics updates. Pushes key metrics every 2 seconds.

**Example (JavaScript):**
```javascript
const eventSource = new EventSource('/api/stream');
eventSource.onmessage = (event) => {
  const metrics = JSON.parse(event.data);
  console.log('CPU:', metrics['cpu.total_usage'].value);
};
```

---

## Available Metrics

### CPU Metrics
- `cpu.total_usage` - Overall CPU utilization (%)
- `cpu.core_usage` - Per-core utilization (with `core` tag)
- `cpu.num_cores` - Number of CPU cores
- `cpu.load_avg_1m` / `cpu.load_avg_5m` / `cpu.load_avg_15m` - Load averages
- `cpu.context_switches` - Total context switches
- `cpu.interrupts` - Total interrupts

### Memory Metrics
- `memory.usage_percent` - Memory utilization (%)
- `memory.total_bytes` - Total RAM
- `memory.used_bytes` - Used RAM
- `memory.free_bytes` - Free RAM
- `memory.available_bytes` - Available RAM
- `memory.cached_bytes` - Cached memory
- `memory.buffers_bytes` - Buffer memory
- `memory.swap_total_bytes` / `memory.swap_used_bytes` - Swap usage

### Disk Metrics
- `disk.usage_percent` - Disk utilization (with `device` and `mount` tags)
- `disk.total_bytes` / `disk.used_bytes` / `disk.free_bytes` - Disk space
- `disk.read_bytes` / `disk.write_bytes` - I/O bytes

### Network Metrics
- `network.bytes_sent` / `network.bytes_recv` - Network traffic (with `interface` tag)
- `network.packets_sent` / `network.packets_recv` - Packet counts
- `network.errors_in` / `network.errors_out` - Error counts
- `network.drops_in` / `network.drops_out` - Dropped packets

### Process Metrics
- `process.cpu_percent` - Per-process CPU (with `pid` and `name` tags)
- `process.memory_bytes` - Per-process memory
- `process.num_threads` - Thread count
- `process.count` - Total process count

---

## Dashboard

Access the interactive web dashboard at:
```
http://localhost:8000/
```

Features:
- Real-time metric cards with trend indicators
- Live CPU & Memory chart (last 5 minutes)
- Auto-refresh every 2 seconds
- Responsive design

---

## Error Responses

All endpoints return JSON error responses:

```json
{
  "error": "Error description"
}
```

HTTP status codes:
- `200` - Success
- `400` - Bad request (missing parameters)
- `404` - Resource not found
- `500` - Server error

---

## Running the Server

```bash
# Start both daemon and API server
./scripts/start.sh

# Or manually:
./build/bin/sysmond ~/.sysmon/data.db &
python3 python/sysmon/api/server.py 8000

# Stop all services
./scripts/stop.sh
```

---

## Configuration

Environment variables:
- `SYSMON_API_PORT` - API server port (default: `8000`)
- Database path: `~/.sysmon/data.db` (configurable via daemon argument)
