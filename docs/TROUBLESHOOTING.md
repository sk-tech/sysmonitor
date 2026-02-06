# SysMonitor Troubleshooting Guide

This guide covers common issues and their solutions.

## Table of Contents

- [Installation Issues](#installation-issues)
- [Build Problems](#build-problems)
- [Runtime Issues](#runtime-issues)
- [Performance Problems](#performance-problems)
- [Network Issues](#network-issues)
- [Database Issues](#database-issues)
- [Alert Issues](#alert-issues)
- [Debugging Tools](#debugging-tools)

## Installation Issues

### Build fails with "CMake not found"

**Problem:** CMake is not installed or not in PATH.

**Solution:**
```bash
# Ubuntu/Debian
sudo apt-get install cmake

# macOS
brew install cmake

# Windows
# Download from https://cmake.org/download/
```

### SQLite3 not found during build

**Problem:** SQLite3 development headers missing.

**Solution:**
```bash
# Ubuntu/Debian
sudo apt-get install libsqlite3-dev

# macOS
brew install sqlite3

# Or use bundled version
cmake -B build -DUSE_BUNDLED_SQLITE=ON
```

### Python bindings fail to build

**Problem:** Python development headers missing or pybind11 not found.

**Solution:**
```bash
# Ubuntu/Debian
sudo apt-get install python3-dev

# macOS
brew install python3

# Disable Python bindings if not needed
cmake -B build -DBUILD_PYTHON_BINDINGS=OFF
```

## Build Problems

### Compilation errors in platform code

**Problem:** Platform-specific code doesn't compile.

**Symptoms:**
- Errors in `src/platform/linux/`, `src/platform/windows/`, etc.
- Undefined symbols like `GetSystemInfo`, `sysctl`, etc.

**Solution:**
```bash
# Verify platform detection
cmake -B build -DCMAKE_VERBOSE_MAKEFILE=ON

# Check CMake output for PLATFORM_LINUX, PLATFORM_WINDOWS, etc.

# Clean and rebuild
rm -rf build
./build.sh
```

### Linker errors

**Problem:** Undefined references during linking.

**Common causes:**
- Missing library
- Wrong linking order
- Static/dynamic linking mismatch

**Solution:**
```bash
# Check what libraries are linked
ldd build/bin/sysmond

# Force static linking
cmake -B build -DSTATIC_LINK_RUNTIME=ON

# Or use dynamic linking
cmake -B build -DSTATIC_LINK_RUNTIME=OFF -DBUILD_SHARED_LIBS=ON
```

## Runtime Issues

### Daemon won't start

**Problem:** `sysmond` exits immediately after start.

**Diagnosis:**
```bash
# Run in foreground to see errors
./build/bin/sysmond --config /etc/sysmon/config.yaml --foreground

# Check logs
journalctl -u sysmond -n 50

# Verify config
./build/bin/sysmond --validate-config /etc/sysmon/config.yaml
```

**Common causes:**
1. **Invalid configuration**
   ```bash
   # Fix YAML syntax
   python3 -c "import yaml; yaml.safe_load(open('/etc/sysmon/config.yaml'))"
   ```

2. **Permission denied on database**
   ```bash
   # Fix permissions
   sudo chown sysmon:sysmon /var/lib/sysmon
   sudo chmod 755 /var/lib/sysmon
   ```

3. **Port already in use** (for API)
   ```bash
   # Find what's using the port
   sudo lsof -i :9000
   
   # Use different port
   # Edit config.yaml or use environment variable
   export SYSMON_PORT=9001
   ```

### No metrics being collected

**Problem:** Daemon running but database empty or not growing.

**Diagnosis:**
```bash
# Check database
sqlite3 /var/lib/sysmon/metrics.db "SELECT COUNT(*) FROM metrics;"

# Check recent metrics
sqlite3 /var/lib/sysmon/metrics.db "SELECT * FROM metrics ORDER BY timestamp DESC LIMIT 5;"

# Check collection interval
grep interval_ms /etc/sysmon/config.yaml
```

**Solutions:**
1. **Collection disabled**
   - Check if daemon is in correct mode
   - Verify collection_interval_ms > 0

2. **Storage writes failing**
   ```bash
   # Check disk space
   df -h /var/lib/sysmon
   
   # Check file permissions
   ls -la /var/lib/sysmon/metrics.db
   
   # Check for database locks
   sqlite3 /var/lib/sysmon/metrics.db "PRAGMA wal_checkpoint;"
   ```

### High CPU usage

**Problem:** `sysmond` using excessive CPU (>10%).

**Diagnosis:**
```bash
# Check what it's doing
strace -p $(pgrep sysmond)

# Profile with perf (Linux)
sudo perf top -p $(pgrep sysmond)

# Check collection frequency
grep interval_ms /etc/sysmon/config.yaml
```

**Solutions:**
1. **Collection too frequent**
   ```yaml
   collection:
     interval_ms: 1000  # Increase to 2000 or 5000
   ```

2. **Too many processes being monitored**
   - Reduce process monitoring frequency
   - Filter processes by name/user

3. **Disk I/O bottleneck**
   ```bash
   # Check I/O wait
   iostat -x 1
   
   # Increase batch size
   # In config.yaml:
   storage:
     batch_size: 200  # Increase from default
   ```

### Memory leak

**Problem:** Memory usage grows over time.

**Diagnosis:**
```bash
# Monitor memory usage
watch -n 1 'ps aux | grep sysmond'

# Use valgrind
valgrind --leak-check=full --show-leak-kinds=all ./build/bin/sysmond
```

**Solutions:**
1. **Ring buffer overflow**
   - Check if storage writes are failing
   - Metrics accumulating in memory

2. **SQLite not releasing memory**
   ```bash
   # Run VACUUM periodically
   sqlite3 /var/lib/sysmon/metrics.db "VACUUM;"
   ```

3. **Update to latest version**
   - Memory leaks fixed in newer versions

## Performance Problems

### Slow queries

**Problem:** API queries taking >5 seconds.

**Diagnosis:**
```bash
# Check query plan
sqlite3 /var/lib/sysmon/metrics.db "EXPLAIN QUERY PLAN SELECT * FROM metrics WHERE metric_type='cpu.total_usage';"

# Check database size
du -h /var/lib/sysmon/metrics.db

# Check index usage
sqlite3 /var/lib/sysmon/metrics.db ".indexes"
```

**Solutions:**
1. **Missing indexes**
   ```sql
   -- Add indexes (should be automatic, but verify)
   CREATE INDEX IF NOT EXISTS idx_timestamp ON metrics(timestamp);
   CREATE INDEX IF NOT EXISTS idx_metric_type ON metrics(metric_type);
   CREATE INDEX IF NOT EXISTS idx_host ON metrics(host);
   ```

2. **Database too large**
   ```bash
   # Clean old data
   ./build/bin/sysmon cleanup --older-than 30d
   
   # Or adjust retention
   # In config.yaml:
   storage:
     retention_days: 30  # Reduce if needed
   ```

3. **Optimize database**
   ```bash
   sqlite3 /var/lib/sysmon/metrics.db "ANALYZE; VACUUM;"
   ```

### Dashboard slow to load

**Problem:** Web dashboard takes >10 seconds to load.

**Solutions:**
1. **Reduce time range**
   - Query last hour instead of last day
   - Implement pagination

2. **Add caching**
   - Cache recent metrics
   - Use SSE for real-time updates only

3. **Database on slow disk**
   - Move database to SSD
   - Use RAM disk for temporary storage

## Network Issues

### Agent can't reach aggregator

**Problem:** Agent running but metrics not appearing in aggregator.

**Diagnosis:**
```bash
# Test connectivity
curl http://aggregator.example.com:9000/health

# Check from agent host
telnet aggregator.example.com 9000

# Check agent logs
journalctl -u sysmond -f | grep -i "publish\|error"
```

**Solutions:**
1. **Firewall blocking**
   ```bash
   # Check firewall (Ubuntu/Debian)
   sudo ufw status
   sudo ufw allow 9000/tcp
   
   # Check firewall (RHEL/CentOS)
   sudo firewall-cmd --list-all
   sudo firewall-cmd --add-port=9000/tcp --permanent
   sudo firewall-cmd --reload
   ```

2. **Wrong URL in config**
   ```yaml
   publisher:
     aggregator_url: http://aggregator.example.com:9000  # Verify this
   ```

3. **SSL/TLS issues**
   ```bash
   # Test SSL
   openssl s_client -connect aggregator.example.com:9000
   
   # Check certificate validity
   echo | openssl s_client -connect aggregator.example.com:9000 2>/dev/null | openssl x509 -noout -dates
   ```

### High network latency

**Problem:** Metrics taking >1 second to publish.

**Diagnosis:**
```bash
# Test latency
ping aggregator.example.com

# Test bandwidth
iperf3 -c aggregator.example.com
```

**Solutions:**
1. **Increase batch size**
   ```yaml
   publisher:
     batch_size: 100  # Send more metrics per request
   ```

2. **Reduce publish frequency**
   ```yaml
   publisher:
     publish_interval_ms: 10000  # Publish every 10s instead of 5s
   ```

## Database Issues

### Database locked

**Problem:** "database is locked" errors.

**Solutions:**
```bash
# Enable WAL mode (should be automatic)
sqlite3 /var/lib/sysmon/metrics.db "PRAGMA journal_mode=WAL;"

# Check for stale locks
fuser /var/lib/sysmon/metrics.db

# If needed, force checkpoint
sqlite3 /var/lib/sysmon/metrics.db "PRAGMA wal_checkpoint(TRUNCATE);"
```

### Database corruption

**Problem:** "database disk image is malformed".

**Diagnosis:**
```bash
# Check integrity
sqlite3 /var/lib/sysmon/metrics.db "PRAGMA integrity_check;"
```

**Solutions:**
1. **Try to recover**
   ```bash
   # Dump and restore
   sqlite3 /var/lib/sysmon/metrics.db ".dump" | sqlite3 /tmp/recovered.db
   
   # Backup old database
   mv /var/lib/sysmon/metrics.db /var/lib/sysmon/metrics.db.corrupt
   
   # Use recovered
   mv /tmp/recovered.db /var/lib/sysmon/metrics.db
   ```

2. **Restore from backup**
   ```bash
   # Stop daemon
   systemctl stop sysmond
   
   # Restore backup
   cp /backups/metrics-20240101.db /var/lib/sysmon/metrics.db
   
   # Start daemon
   systemctl start sysmond
   ```

## Alert Issues

### Alerts not firing

**Problem:** Thresholds breached but no alerts.

**Diagnosis:**
```bash
# Check alert configuration
cat /etc/sysmon/alerts.yaml

# Validate YAML
python3 -c "import yaml; yaml.safe_load(open('/etc/sysmon/alerts.yaml'))"

# Check if alerts enabled
grep "alerts:" /etc/sysmon/config.yaml

# Check alert logs
grep -i alert /var/log/sysmon/daemon.log
```

**Solutions:**
1. **Alerts disabled**
   ```yaml
   alerts:
     enabled: true  # Ensure this is set
   ```

2. **Duration not met**
   - Check `duration` field in alert rules
   - Condition must be true for full duration

3. **Wrong metric name**
   ```bash
   # List available metrics
   ./build/bin/sysmon types
   
   # Check spelling in alerts.yaml
   ```

### Too many alerts (alert fatigue)

**Problem:** Getting flooded with alerts.

**Solutions:**
1. **Adjust thresholds**
   ```yaml
   - name: high_cpu
     threshold: 90.0  # Increase from 80.0
   ```

2. **Increase duration**
   ```yaml
   - name: high_cpu
     duration: 5m  # Was 30s
   ```

3. **Add cooldown**
   ```yaml
   - name: high_cpu
     cooldown: 1h  # Don't re-alert for 1 hour
   ```

## Debugging Tools

### Enable debug logging

```yaml
# In config.yaml
logging:
  level: debug
  file: /var/log/sysmon/daemon.log
```

### Use CLI for diagnostics

```bash
# System info
./build/bin/sysmon info

# Current metrics
./build/bin/sysmon cpu
./build/bin/sysmon memory

# Historical data
./build/bin/sysmon history cpu.total_usage 1h

# Database stats
./build/bin/sysmon stats
```

### Trace system calls

```bash
# Linux
strace -f -e trace=open,read,write ./build/bin/sysmond

# macOS
dtruss ./build/bin/sysmond
```

### Network debugging

```bash
# Monitor HTTP traffic
tcpdump -i any -A 'tcp port 9000'

# Or use Wireshark for GUI

# Check connection states
netstat -an | grep 9000
ss -an | grep 9000
```

### Memory profiling

```bash
# Valgrind
valgrind --tool=massif ./build/bin/sysmond
ms_print massif.out.*

# Heaptrack (Linux)
heaptrack ./build/bin/sysmond
heaptrack_gui heaptrack.sysmond.*
```

## Getting Help

### Collect diagnostic information

```bash
#!/bin/bash
# diagnostic.sh - Collect debug info

echo "=== System Info ==="
uname -a
cat /etc/os-release

echo "=== SysMonitor Version ==="
./build/bin/sysmond --version

echo "=== Configuration ==="
cat /etc/sysmon/config.yaml

echo "=== Recent Logs ==="
journalctl -u sysmond -n 100

echo "=== Database Info ==="
sqlite3 /var/lib/sysmon/metrics.db "PRAGMA page_count; PRAGMA page_size; SELECT COUNT(*) FROM metrics;"

echo "=== Process Info ==="
ps aux | grep sysmond
lsof -p $(pgrep sysmond)

echo "=== Network ==="
netstat -tlnp | grep sysmond
```

### Report a bug

Include:
1. SysMonitor version
2. Operating system and version
3. Configuration files (sanitized)
4. Logs showing the error
5. Steps to reproduce
6. Expected vs actual behavior

### Community resources

- GitHub Issues: https://github.com/yourusername/sysmonitor/issues
- Documentation: https://github.com/yourusername/sysmonitor/docs
- Discussions: https://github.com/yourusername/sysmonitor/discussions

---

**Last Updated:** 2024-02-06  
**Version:** 1.0
