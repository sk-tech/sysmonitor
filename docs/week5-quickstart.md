# Week 5 Multi-Host Dashboard - Quick Start Guide

## Overview

Week 5 adds a complete multi-host monitoring dashboard that works with the distributed aggregator architecture. This guide shows you how to use all new features.

## Quick Demo (Recommended First Step)

The fastest way to see everything working:

```bash
cd ~/sysmonitor
./scripts/demo-distributed.sh
```

This will:
1. Start the aggregator on port 9000
2. Launch 3 simulated agents (web-server-01, db-server-01, app-server-01)
3. Wait for metrics to flow
4. Open the multi-host dashboard in your browser

**Press Ctrl+C to stop everything**

## Dashboard Features

### Fleet Overview (Top Section)

Shows aggregated statistics across all hosts:
- **Total Hosts**: Number of registered hosts
- **Online**: Hosts seen in last 5 minutes (🟢)
- **Offline**: Hosts not seen recently (🔴)
- **Avg CPU Usage**: Average CPU % across all online hosts
- **Total Memory Used**: Sum of memory used by all hosts

### Host Selector

- **Dropdown**: Select any registered host to view its metrics
- **Status Icons**: 🟢 Online / 🔴 Offline
- **View Mode Buttons**:
  - **Single Host**: Detailed view of one host with charts
  - **Compare Hosts**: Side-by-side comparison of up to 3 hosts

### Single Host View

When viewing a single host:
- 4 metric cards: CPU, Memory %, Load Average, Memory Used
- Change indicators (▲/▼) showing metric changes
- Real-time timestamps
- 5-minute historical chart (CPU and Memory trends)

### Comparison View

When comparing hosts:
- Up to 3 hosts shown side-by-side
- Compact metric cards for quick scanning
- Easy visual comparison of resources across hosts

### Auto-Refresh

Dashboard updates every 5 seconds automatically. No need to refresh the page.

## Production Usage

### Step 1: Start Aggregator

On your central monitoring server:

```bash
cd ~/sysmonitor

# Set a secure token
export SYSMON_TOKEN="your-secure-token-here"

# Start aggregator
./scripts/start-aggregator.sh

# Or run in foreground:
./scripts/start-aggregator.sh -f
```

Access the dashboard at: **http://your-server:9000**

### Step 2: Configure Agents

On each host you want to monitor, create `~/.sysmon/agent.yaml`:

```yaml
mode: agent
hostname: web-server-01  # Unique name for this host
collection_interval_ms: 2000

agent:
  aggregator_url: http://monitoring-server:9000
  auth_token: your-secure-token-here
  push_interval_seconds: 10
  tags:
    environment: production
    datacenter: us-east-1
    role: web-server
```

### Step 3: Start Agents

On each monitored host:

```bash
cd ~/sysmonitor

# Set the same token
export SYSMON_TOKEN="your-secure-token-here"

# Start in distributed mode
export SYSMON_MODE=distributed
./scripts/start.sh
```

Or use the convenience script:

```bash
./scripts/start.sh distributed
```

### Step 4: Verify

Check the aggregator API:

```bash
# Health check
curl http://monitoring-server:9000/api/health

# List registered hosts
curl http://monitoring-server:9000/api/hosts

# Fleet summary
curl http://monitoring-server:9000/api/fleet/summary
```

## API Endpoints

All endpoints are on the aggregator server (default port 9000):

### Public Endpoints (No Auth Required)
- `GET /` or `/dashboard` - Multi-host dashboard HTML
- `GET /api/health` - Health check

### Authenticated Endpoints (Require Token)
- `GET /api/hosts` - List all registered hosts
- `GET /api/hosts?include_inactive=true` - Include offline hosts
- `GET /api/latest?host=HOSTNAME` - Latest metrics for specific host
- `GET /api/metrics?host=X&metric_type=Y&start=T1&end=T2` - Historical metrics
- `GET /api/fleet/summary` - Fleet-wide aggregated statistics
- `POST /api/metrics` - Agents push metrics here (auto-called)

## Configuration Options

### Environment Variables

```bash
# Aggregator port (default: 9000)
export SYSMON_AGGREGATOR_PORT=9000

# Auth token (required for production)
export SYSMON_TOKEN="your-token"

# Single-host API port (default: 8000)
export SYSMON_API_PORT=8000

# Operating mode
export SYSMON_MODE=distributed  # or 'single'
```

### Agent Config File (`~/.sysmon/agent.yaml`)

```yaml
mode: agent                     # Required: 'agent' for distributed mode
hostname: unique-name           # Unique identifier for this host
collection_interval_ms: 2000    # How often to collect metrics (ms)

agent:
  aggregator_url: http://host:9000  # Where to send metrics
  auth_token: ${SYSMON_TOKEN}       # Can use env var
  push_interval_seconds: 10         # How often to push to aggregator
  
  tags:                         # Optional: Custom tags
    environment: production
    datacenter: us-east-1
    role: web-server
    team: platform
```

## Stopping Services

### Stop Everything (Including Demo):

```bash
./scripts/stop.sh
```

This stops:
- Single-host daemon
- Single-host API server
- Aggregator server
- All demo agents

### Stop Aggregator Only:

```bash
kill $(cat /tmp/sysmon_aggregator.pid)
```

### Stop Specific Demo Agent:

```bash
kill $(cat /tmp/sysmon_web-server-01.pid)
```

## Troubleshooting

### Dashboard Not Loading

**Symptom**: Browser shows "Connection refused" or 404

**Solution**:
```bash
# Check if aggregator is running
ps aux | grep aggregator

# Check logs
tail -f ~/.sysmon/aggregator.log

# Restart aggregator
./scripts/stop.sh
./scripts/start-aggregator.sh
```

### No Hosts Showing in Dashboard

**Symptom**: Dashboard shows "Total Hosts: 0"

**Solution**:
```bash
# Check if agents are running
ps aux | grep sysmond

# Check agent logs
tail -f /tmp/sysmon_*.log

# Verify agent config has correct aggregator URL
cat ~/.sysmon/agent.yaml

# Test aggregator API
curl http://localhost:9000/api/hosts
```

### Metrics Not Updating

**Symptom**: Dashboard shows old or no data

**Solution**:
```bash
# Check aggregator database
ls -lh ~/.sysmon/aggregator.db

# Query hosts table directly
sqlite3 ~/.sysmon/aggregator.db "SELECT * FROM hosts"

# Query recent metrics
sqlite3 ~/.sysmon/aggregator.db "SELECT * FROM metrics ORDER BY timestamp DESC LIMIT 10"

# Check agent is pushing
tail -f /tmp/sysmon_*.log | grep "Pushed"
```

### Authentication Errors

**Symptom**: Agent logs show "401 Unauthorized"

**Solution**:
```bash
# Ensure token matches on both sides
echo $SYSMON_TOKEN

# Check aggregator startup message (shows token mask)
tail ~/.sysmon/aggregator.log | grep "Auth token"

# Update agent config with correct token
nano ~/.sysmon/agent.yaml
```

### Port Already in Use

**Symptom**: "Address already in use" error

**Solution**:
```bash
# Find what's using the port
sudo lsof -i :9000

# Stop existing process
./scripts/stop.sh

# Or kill specific PID
kill <PID>

# Use different port
export SYSMON_AGGREGATOR_PORT=9001
./scripts/start-aggregator.sh
```

## Logs

### Aggregator Logs:
```bash
tail -f ~/.sysmon/aggregator.log
```

### Agent Logs (Single Host):
```bash
tail -f /tmp/sysmond.log
```

### Agent Logs (Demo):
```bash
tail -f /tmp/sysmon_web-server-01.log
tail -f /tmp/sysmon_db-server-01.log
tail -f /tmp/sysmon_app-server-01.log
```

### API Server Logs (Single-Host Mode):
```bash
tail -f /tmp/sysmon_api.log
```

## Performance Tips

### For 10-50 Hosts:
- Keep default settings
- Aggregator can handle this easily on any modern server

### For 50+ Hosts:
- Increase `push_interval_seconds` to 15-30
- Increase `collection_interval_ms` to 5000
- Consider database on SSD
- Monitor aggregator CPU/memory with `top`

### For 100+ Hosts:
- Consider sharding (multiple aggregators)
- Implement metrics batching (modify agent code)
- Use dedicated database server
- Enable database connection pooling

## Security Recommendations

### Production Deployment:

1. **Use Strong Tokens**:
   ```bash
   # Generate secure token
   export SYSMON_TOKEN=$(openssl rand -hex 32)
   ```

2. **Restrict Network Access**:
   - Use firewall to allow only agent IPs
   - Consider VPN or private network
   - Use HTTPS reverse proxy (nginx/caddy)

3. **Separate Credentials**:
   - Don't commit tokens to git
   - Use environment variables
   - Rotate tokens regularly

4. **Monitor Access**:
   - Check aggregator logs for suspicious activity
   - Alert on failed auth attempts

## Next Steps

After getting comfortable with the dashboard:

1. **Customize Agent Tags** - Add meaningful tags for filtering
2. **Monitor More Hosts** - Add agents to more servers
3. **Set Up Alerts** - Configure alert rules (Week 4 feature)
4. **Automate Deployment** - Create systemd services for production
5. **Integrate with Tools** - Use API for custom integrations

## Support

For issues:
1. Check logs (see Logs section above)
2. Review troubleshooting section
3. Check [docs/week5-summary.md](docs/week5-summary.md) for technical details
4. Review [docs/PROJECT_SUMMARY.md](docs/PROJECT_SUMMARY.md) for architecture

## Examples

### Example 1: Monitor 3 Web Servers

**Aggregator (monitoring.example.com)**:
```bash
export SYSMON_TOKEN=$(openssl rand -hex 32)
./scripts/start-aggregator.sh
```

**Each Web Server** (web-01.example.com, web-02.example.com, web-03.example.com):
```yaml
# ~/.sysmon/agent.yaml
mode: agent
hostname: web-01
agent:
  aggregator_url: http://monitoring.example.com:9000
  auth_token: <token-from-above>
  push_interval_seconds: 10
  tags:
    role: web-server
    environment: production
```

```bash
export SYSMON_TOKEN="<token-from-above>"
./scripts/start.sh distributed
```

Access: http://monitoring.example.com:9000

### Example 2: Local Development

```bash
# Single command - runs everything locally
./scripts/demo-distributed.sh
```

Access: http://localhost:9000

---

**Last Updated**: February 6, 2026  
**Version**: 0.5.0 (Week 5)
