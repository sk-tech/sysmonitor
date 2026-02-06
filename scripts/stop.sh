#!/bin/bash
# SysMonitor stop script - stops daemon, API server, and aggregator

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "Stopping SysMonitor services..."

# Stop daemon
if [ -f /tmp/sysmon_daemon.pid ]; then
    PID=$(cat /tmp/sysmon_daemon.pid)
    if kill -0 $PID 2>/dev/null; then
        kill $PID
        echo -e "${GREEN}✓${NC} Stopped daemon (PID: $PID)"
    fi
    rm -f /tmp/sysmon_daemon.pid
fi

# Stop API server
if [ -f /tmp/sysmon_api.pid ]; then
    PID=$(cat /tmp/sysmon_api.pid)
    if kill -0 $PID 2>/dev/null; then
        kill $PID
        echo -e "${GREEN}✓${NC} Stopped API server (PID: $PID)"
    fi
    rm -f /tmp/sysmon_api.pid
fi

# Stop aggregator
if [ -f /tmp/sysmon_aggregator.pid ]; then
    PID=$(cat /tmp/sysmon_aggregator.pid)
    if kill -0 $PID 2>/dev/null; then
        kill $PID
        echo -e "${GREEN}✓${NC} Stopped aggregator (PID: $PID)"
    fi
    rm -f /tmp/sysmon_aggregator.pid
fi

# Stop any demo agents
for pidfile in /tmp/sysmon_*-server-*.pid; do
    if [ -f "$pidfile" ]; then
        PID=$(cat "$pidfile")
        if kill -0 $PID 2>/dev/null; then
            kill $PID
            echo -e "${GREEN}✓${NC} Stopped agent (PID: $PID)"
        fi
        rm -f "$pidfile"
    fi
done

# Fallback: kill by name
pkill -f "sysmond" 2>/dev/null && echo -e "${GREEN}✓${NC} Stopped remaining sysmond processes"
pkill -f "sysmon/api/server.py" 2>/dev/null && echo -e "${GREEN}✓${NC} Stopped remaining API processes"
pkill -f "sysmon/aggregator/server.py" 2>/dev/null && echo -e "${GREEN}✓${NC} Stopped remaining aggregator processes"

echo "All services stopped."
