#!/bin/bash
# SysMonitor startup script - runs both daemon and web server
# Supports single-host and distributed modes

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
DB_PATH="${HOME}/.sysmon/data.db"
API_PORT="${SYSMON_API_PORT:-8000}"
MODE="${SYSMON_MODE:-single}"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔═══════════════════════════════════════╗${NC}"
echo -e "${BLUE}║     SysMonitor Startup Script         ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════╝${NC}"
echo

# Check mode
if [ "$MODE" = "distributed" ] || [ "$1" = "distributed" ]; then
    echo -e "${BLUE}Mode: Distributed (agent + aggregator)${NC}"
    MODE="distributed"
else
    echo -e "${BLUE}Mode: Single-host${NC}"
    MODE="single"
fi
echo

# Check if binaries exist
if [ ! -f "$PROJECT_ROOT/build/bin/sysmond" ]; then
    echo -e "${YELLOW}⚠️  Daemon binary not found. Building project...${NC}"
    cd "$PROJECT_ROOT/build" && cmake --build . -j$(nproc)
fi

if [ "$MODE" = "distributed" ]; then
    # Distributed mode: Start aggregator instead of single-host API
    echo -e "${GREEN}▶ Starting in distributed mode...${NC}"
    echo
    
    # Start aggregator
    bash "$SCRIPT_DIR/start-aggregator.sh"
    
    # Start daemon in agent mode
    CONFIG_PATH="${HOME}/.sysmon/agent.yaml"
    if [ ! -f "$CONFIG_PATH" ]; then
        echo -e "${YELLOW}⚠️  Creating default agent config: $CONFIG_PATH${NC}"
        mkdir -p "$(dirname "$CONFIG_PATH")"
        cat > "$CONFIG_PATH" <<EOF
mode: agent
hostname: $(hostname)
collection_interval_ms: 2000

agent:
  aggregator_url: http://localhost:9000
  auth_token: ${SYSMON_TOKEN:-sysmon-default-token}
  push_interval_seconds: 10
  tags:
    environment: production
EOF
    fi
    
    echo -e "${GREEN}▶ Starting metrics collector daemon (agent mode)...${NC}"
    echo "   Config: $CONFIG_PATH"
    echo "   Database: $DB_PATH"
    "$PROJECT_ROOT/build/bin/sysmond" "$DB_PATH" "$CONFIG_PATH" > /tmp/sysmond.log 2>&1 &
    DAEMON_PID=$!
    echo "   PID: $DAEMON_PID"
    sleep 2
    
    if ! kill -0 $DAEMON_PID 2>/dev/null; then
        echo -e "${YELLOW}⚠️  Daemon failed to start. Check /tmp/sysmond.log${NC}"
        exit 1
    fi
    
    echo
    echo -e "${GREEN}✓ SysMonitor is running in distributed mode!${NC}"
    echo
    echo "Services:"
    echo "  Aggregator: http://localhost:9000"
    echo "  Agent PID:  $DAEMON_PID (logs: /tmp/sysmond.log)"
    echo
    echo "Access:"
    echo "  Dashboard:   http://localhost:9000"
    echo "  API Health:  http://localhost:9000/api/health"
    echo
    echo "To stop:"
    echo "  $SCRIPT_DIR/stop.sh"
    echo
    
    # Save PID
    echo "$DAEMON_PID" > /tmp/sysmon_daemon.pid
    
else
    # Single-host mode (original behavior)
    
    # Start daemon in background
    echo -e "${GREEN}▶ Starting metrics collector daemon...${NC}"
    echo "   Database: $DB_PATH"
    "$PROJECT_ROOT/build/bin/sysmond" "$DB_PATH" > /tmp/sysmond.log 2>&1 &
    DAEMON_PID=$!
    echo "   PID: $DAEMON_PID"
    sleep 2

    # Check if daemon is running
    if ! kill -0 $DAEMON_PID 2>/dev/null; then
        echo -e "${YELLOW}⚠️  Daemon failed to start. Check /tmp/sysmond.log${NC}"
        exit 1
    fi

    # Start API server
    echo -e "${GREEN}▶ Starting web API server...${NC}"
    echo "   Port: $API_PORT"
    echo "   Dashboard: http://localhost:$API_PORT"
    python3 "$PROJECT_ROOT/python/sysmon/api/server.py" $API_PORT > /tmp/sysmon_api.log 2>&1 &
    API_PID=$!
    echo "   PID: $API_PID"
    sleep 2

    # Check if API is running
    if ! kill -0 $API_PID 2>/dev/null; then
        echo -e "${YELLOW}⚠️  API server failed to start. Check /tmp/sysmon_api.log${NC}"
        kill $DAEMON_PID 2>/dev/null
        exit 1
    fi

    echo
    echo -e "${GREEN}✓ SysMonitor is running!${NC}"
    echo
    echo "Services:"
    echo "  Daemon PID:  $DAEMON_PID (logs: /tmp/sysmond.log)"
    echo "  API PID:     $API_PID (logs: /tmp/sysmon_api.log)"
    echo
    echo "Access:"
    echo "  Dashboard:   http://localhost:$API_PORT"
    echo "  API Docs:    http://localhost:$API_PORT/api/metrics/types"
    echo "  CLI:         $PROJECT_ROOT/build/bin/sysmon history cpu.total_usage 1h"
    echo
    echo "To stop:"
    echo "  kill $DAEMON_PID $API_PID"
    echo "  or run: $SCRIPT_DIR/stop.sh"
    echo

    # Save PIDs for stop script
    echo "$DAEMON_PID" > /tmp/sysmon_daemon.pid
    echo "$API_PID" > /tmp/sysmon_api.pid
fi

# Wait for interrupt
echo "Press Ctrl+C to stop all services"
trap "kill $DAEMON_PID $API_PID 2>/dev/null; echo; echo 'Stopped'; exit" INT TERM
wait
