#!/bin/bash
# SysMonitor stop script

GREEN='\033[0;32m'
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

# Fallback: kill by name
pkill -f "sysmond" 2>/dev/null && echo -e "${GREEN}✓${NC} Stopped sysmond"
pkill -f "sysmon/api/server.py" 2>/dev/null && echo -e "${GREEN}✓${NC} Stopped API server"

echo "All services stopped."
