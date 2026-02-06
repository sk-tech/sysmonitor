# Docker Deployment Guide

This guide covers running SysMonitor in Docker containers.

## Quick Start

### Using Docker Compose (Recommended)

```bash
# Clone repository
git clone https://github.com/yourusername/sysmonitor
cd sysmonitor

# Start full stack
docker-compose up -d

# View logs
docker-compose logs -f

# Stop
docker-compose down
```

## Docker Images

### Agent Image

Build the agent image:

```bash
docker build -f Dockerfile.agent -t sysmonitor-agent:latest .
```

Run standalone agent:

```bash
docker run -d \
  --name sysmon-agent \
  --hostname $(hostname) \
  -v /proc:/host/proc:ro \
  -v /sys:/host/sys:ro \
  -v sysmon-data:/var/lib/sysmon \
  --privileged \
  sysmonitor-agent:latest
```

### Aggregator Image

Build the aggregator image:

```bash
docker build -f Dockerfile.aggregator -t sysmonitor-aggregator:latest .
```

Run aggregator:

```bash
docker run -d \
  --name sysmon-aggregator \
  -p 9000:9000 \
  -v sysmon-aggregator-data:/var/lib/sysmon \
  sysmonitor-aggregator:latest
```

## Docker Compose Configuration

Complete `docker-compose.yml`:

```yaml
version: '3.8'

services:
  # Aggregator - Central collection point
  aggregator:
    image: sysmonitor-aggregator:latest
    build:
      context: .
      dockerfile: Dockerfile.aggregator
    ports:
      - "9000:9000"
    volumes:
      - aggregator-data:/var/lib/sysmon
      - ./config/aggregator.yaml:/etc/sysmon/config.yaml:ro
    environment:
      - SYSMON_PORT=9000
      - SYSMON_LOG_LEVEL=info
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:9000/health"]
      interval: 30s
      timeout: 10s
      retries: 3

  # Agent - Metrics collector
  agent:
    image: sysmonitor-agent:latest
    build:
      context: .
      dockerfile: Dockerfile.agent
    volumes:
      - agent-data:/var/lib/sysmon
      - /proc:/host/proc:ro
      - /sys:/host/sys:ro
      - ./config/agent.yaml:/etc/sysmon/config.yaml:ro
    environment:
      - SYSMON_AGGREGATOR_URL=http://aggregator:9000
      - SYSMON_HOSTNAME=${HOSTNAME:-docker-agent}
    depends_on:
      - aggregator
    privileged: true
    restart: unless-stopped

  # Dashboard (optional)
  dashboard:
    image: nginx:alpine
    ports:
      - "8080:80"
    volumes:
      - ./python/sysmon/aggregator/static:/usr/share/nginx/html:ro
    depends_on:
      - aggregator
    restart: unless-stopped

volumes:
  aggregator-data:
  agent-data:

networks:
  default:
    driver: bridge
```

## Multi-Host Deployment

### Aggregator Host

```yaml
# docker-compose-aggregator.yml
version: '3.8'

services:
  aggregator:
    image: sysmonitor-aggregator:latest
    ports:
      - "9000:9000"
    volumes:
      - /opt/sysmon/data:/var/lib/sysmon
      - /opt/sysmon/config/aggregator.yaml:/etc/sysmon/config.yaml:ro
    restart: unless-stopped
    logging:
      driver: "json-file"
      options:
        max-size: "10m"
        max-file: "3"
```

### Agent Hosts

```yaml
# docker-compose-agent.yml
version: '3.8'

services:
  agent:
    image: sysmonitor-agent:latest
    volumes:
      - /var/lib/sysmon:/var/lib/sysmon
      - /proc:/host/proc:ro
      - /sys:/host/sys:ro
      - /etc/sysmon/agent.yaml:/etc/sysmon/config.yaml:ro
    environment:
      - SYSMON_AGGREGATOR_URL=http://aggregator.example.com:9000
      - SYSMON_HOSTNAME=${HOSTNAME}
    privileged: true
    restart: unless-stopped
```

Deploy agents:

```bash
# On each agent host
export HOSTNAME=$(hostname)
docker-compose -f docker-compose-agent.yml up -d
```

## Configuration Files

### Agent Configuration (`config/agent.yaml`)

```yaml
agent:
  hostname: ${SYSMON_HOSTNAME}
  tags:
    environment: production
    datacenter: us-east-1

publisher:
  aggregator_url: ${SYSMON_AGGREGATOR_URL}
  publish_interval_ms: 5000
  batch_size: 50

storage:
  db_path: /var/lib/sysmon/metrics.db
  retention_days: 7

collection:
  interval_ms: 1000
```

### Aggregator Configuration (`config/aggregator.yaml`)

```yaml
server:
  port: 9000
  host: 0.0.0.0

storage:
  db_path: /var/lib/sysmon/aggregator.db
  retention_days: 90

alerts:
  enabled: true
  config_path: /etc/sysmon/alerts.yaml
```

## Networking

### Expose Aggregator

Using nginx reverse proxy:

```nginx
server {
    listen 80;
    server_name sysmon.example.com;

    location / {
        proxy_pass http://localhost:9000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    }
}
```

### SSL/TLS

```yaml
aggregator:
  # ... other config ...
  volumes:
    - ./certs:/etc/sysmon/certs:ro
  environment:
    - SYSMON_TLS_CERT=/etc/sysmon/certs/server.crt
    - SYSMON_TLS_KEY=/etc/sysmon/certs/server.key
```

## Monitoring Containers

### Health Checks

```bash
# Check container health
docker ps
docker inspect sysmon-aggregator | grep Health

# View logs
docker logs -f sysmon-aggregator
docker logs -f sysmon-agent
```

### Resource Limits

```yaml
services:
  agent:
    # ... other config ...
    deploy:
      resources:
        limits:
          cpus: '0.5'
          memory: 512M
        reservations:
          cpus: '0.25'
          memory: 256M
```

## Backup and Restore

### Backup

```bash
# Backup aggregator database
docker exec sysmon-aggregator sqlite3 /var/lib/sysmon/aggregator.db ".backup /var/lib/sysmon/backup.db"
docker cp sysmon-aggregator:/var/lib/sysmon/backup.db ./backup-$(date +%Y%m%d).db
```

### Restore

```bash
# Restore database
docker cp ./backup.db sysmon-aggregator:/var/lib/sysmon/restore.db
docker exec sysmon-aggregator sh -c "mv /var/lib/sysmon/restore.db /var/lib/sysmon/aggregator.db"
docker restart sysmon-aggregator
```

## Upgrading

```bash
# Pull latest images
docker-compose pull

# Restart with new images
docker-compose up -d

# Clean old images
docker image prune -a
```

## Troubleshooting

### Container won't start

```bash
# Check logs
docker logs sysmon-agent

# Check configuration
docker exec sysmon-agent cat /etc/sysmon/config.yaml

# Test connectivity
docker exec sysmon-agent ping aggregator
```

### Permission issues

```bash
# Check volumes
docker volume inspect sysmon-data

# Fix permissions
docker exec -u root sysmon-agent chown -R sysmon:sysmon /var/lib/sysmon
```

### Network connectivity

```bash
# Check network
docker network inspect sysmonitor_default

# Test from agent to aggregator
docker exec sysmon-agent curl http://aggregator:9000/health
```
