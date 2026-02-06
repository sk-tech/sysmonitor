# Systemd Deployment Guide

This guide covers deploying SysMonitor as a systemd service on Linux.

## Prerequisites

- Linux system with systemd
- Root or sudo access
- SysMonitor binaries built and installed

## Installation

### 1. Install Binaries

```bash
# Build from source
./build.sh

# Install binaries
sudo cp build/bin/sysmond /usr/local/bin/
sudo cp build/bin/sysmon /usr/local/bin/
sudo chmod +x /usr/local/bin/sysmond /usr/local/bin/sysmon
```

### 2. Create System User

```bash
sudo useradd --system --no-create-home --shell /usr/sbin/nologin sysmon
```

### 3. Create Configuration Directory

```bash
sudo mkdir -p /etc/sysmon
sudo mkdir -p /var/lib/sysmon
sudo chown sysmon:sysmon /var/lib/sysmon
```

### 4. Create Configuration File

Create `/etc/sysmon/config.yaml`:

```yaml
storage:
  db_path: /var/lib/sysmon/metrics.db
  retention_days: 30
  enable_wal: true
  batch_size: 100

collection:
  interval_ms: 1000

alerts:
  enabled: true
  config_path: /etc/sysmon/alerts.yaml

# Optional: For distributed monitoring
publisher:
  enabled: true
  aggregator_url: http://aggregator.example.com:9000
  publish_interval_ms: 5000
  batch_size: 50
```

## Systemd Service

### Agent Service

Create `/etc/systemd/system/sysmond.service`:

```ini
[Unit]
Description=SysMonitor Daemon - System Metrics Collection
Documentation=https://github.com/yourusername/sysmonitor
After=network.target

[Service]
Type=simple
User=sysmon
Group=sysmon
ExecStart=/usr/local/bin/sysmond --config /etc/sysmon/config.yaml
Restart=always
RestartSec=10
StandardOutput=journal
StandardError=journal

# Security hardening
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/sysmon
CapabilityBoundingSet=CAP_NET_RAW CAP_DAC_READ_SEARCH
SystemCallFilter=@system-service
SystemCallErrorNumber=EPERM

# Resource limits
LimitNOFILE=65536
MemoryMax=512M
CPUQuota=50%

[Install]
WantedBy=multi-user.target
```

### Aggregator Service

Create `/etc/systemd/system/sysmon-aggregator.service`:

```ini
[Unit]
Description=SysMonitor Aggregator - Centralized Metrics Server
Documentation=https://github.com/yourusername/sysmonitor
After=network.target

[Service]
Type=simple
User=sysmon
Group=sysmon
WorkingDirectory=/opt/sysmon
ExecStart=/usr/bin/python3 -m sysmon.aggregator.server --port 9000 --db /var/lib/sysmon/aggregator.db
Restart=always
RestartSec=10
StandardOutput=journal
StandardError=journal

# Environment
Environment="PYTHONPATH=/opt/sysmon"

# Security hardening
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/sysmon

# Resource limits
LimitNOFILE=65536
MemoryMax=2G

[Install]
WantedBy=multi-user.target
```

## Service Management

### Enable and Start Services

```bash
# Agent
sudo systemctl daemon-reload
sudo systemctl enable sysmond
sudo systemctl start sysmond
sudo systemctl status sysmond

# Aggregator (if needed)
sudo systemctl enable sysmon-aggregator
sudo systemctl start sysmon-aggregator
sudo systemctl status sysmon-aggregator
```

### View Logs

```bash
# Agent logs
sudo journalctl -u sysmond -f

# Aggregator logs
sudo journalctl -u sysmon-aggregator -f

# Last 100 lines
sudo journalctl -u sysmond -n 100
```

### Restart Services

```bash
sudo systemctl restart sysmond
sudo systemctl restart sysmon-aggregator
```

### Stop Services

```bash
sudo systemctl stop sysmond
sudo systemctl stop sysmon-aggregator
```

## Log Rotation

Create `/etc/logrotate.d/sysmon`:

```
/var/log/sysmon/*.log {
    daily
    rotate 14
    compress
    delaycompress
    notifempty
    create 0640 sysmon sysmon
    sharedscripts
    postrotate
        systemctl reload sysmond > /dev/null 2>&1 || true
    endscript
}
```

## Monitoring the Monitor

### Health Checks

Create a simple health check script `/usr/local/bin/sysmon-healthcheck`:

```bash
#!/bin/bash
# Check if daemon is running
if ! systemctl is-active --quiet sysmond; then
    echo "ERROR: sysmond is not running"
    exit 1
fi

# Check if metrics are being collected
LAST_METRIC=$(sqlite3 /var/lib/sysmon/metrics.db "SELECT MAX(timestamp) FROM metrics;")
NOW=$(date +%s)
AGE=$((NOW - LAST_METRIC))

if [ $AGE -gt 120 ]; then
    echo "ERROR: No metrics collected in last 2 minutes"
    exit 1
fi

echo "OK: sysmond healthy"
exit 0
```

Add to crontab:

```bash
*/5 * * * * /usr/local/bin/sysmon-healthcheck || systemctl restart sysmond
```

## Uninstallation

```bash
# Stop and disable services
sudo systemctl stop sysmond sysmon-aggregator
sudo systemctl disable sysmond sysmon-aggregator

# Remove files
sudo rm /etc/systemd/system/sysmond.service
sudo rm /etc/systemd/system/sysmon-aggregator.service
sudo rm -rf /etc/sysmon
sudo rm -rf /var/lib/sysmon
sudo rm /usr/local/bin/sysmond
sudo rm /usr/local/bin/sysmon

# Remove user
sudo userdel sysmon

# Reload systemd
sudo systemctl daemon-reload
```

## Troubleshooting

### Service won't start

```bash
# Check logs
sudo journalctl -u sysmond -xe

# Check configuration
sysmond --config /etc/sysmon/config.yaml --validate

# Check permissions
ls -la /var/lib/sysmon
```

### High CPU usage

```bash
# Check collection interval
grep interval_ms /etc/sysmon/config.yaml

# Monitor resource usage
systemctl status sysmond
```

### Database growing too large

```bash
# Check retention settings
grep retention_days /etc/sysmon/config.yaml

# Manually clean old data
sysmon cleanup --older-than 30d
```
