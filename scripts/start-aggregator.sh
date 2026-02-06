#!/bin/bash
# Start the SysMonitor aggregator server

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
AGGREGATOR_PORT="${SYSMON_AGGREGATOR_PORT:-9000}"
AGGREGATOR_DB="${HOME}/.sysmon/aggregator.db"
AGGREGATOR_LOG="${HOME}/.sysmon/aggregator.log"
PID_FILE="/tmp/sysmon_aggregator.pid"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔═══════════════════════════════════════╗${NC}"
echo -e "${BLUE}║   SysMonitor Aggregator Server        ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════╝${NC}"
echo

# Check if already running
if [ -f "$PID_FILE" ]; then
    OLD_PID=$(cat "$PID_FILE")
    if kill -0 "$OLD_PID" 2>/dev/null; then
        echo -e "${YELLOW}⚠️  Aggregator is already running (PID: $OLD_PID)${NC}"
        echo "   To stop it, run: kill $OLD_PID"
        echo "   Or use: $SCRIPT_DIR/stop.sh"
        exit 1
    else
        echo -e "${YELLOW}⚠️  Removing stale PID file${NC}"
        rm -f "$PID_FILE"
    fi
fi

# Create .sysmon directory if it doesn't exist
mkdir -p "$(dirname "$AGGREGATOR_DB")"

# Generate auth token if not set
if [ -z "$SYSMON_TOKEN" ]; then
    export SYSMON_TOKEN="sysmon-default-token-$(date +%s)"
    echo -e "${YELLOW}⚠️  No SYSMON_TOKEN set, using: $SYSMON_TOKEN${NC}"
    echo "   Set SYSMON_TOKEN environment variable for custom token"
fi

# Auth module expects SYSMON_AGGREGATOR_TOKEN
export SYSMON_AGGREGATOR_TOKEN="$SYSMON_TOKEN"

# Start aggregator
echo -e "${GREEN}▶ Starting aggregator server...${NC}"
echo "   Port: $AGGREGATOR_PORT"
echo "   Database: $AGGREGATOR_DB"
echo "   Dashboard: http://localhost:$AGGREGATOR_PORT"
echo "   Auth Token: ${SYSMON_TOKEN:0:10}..."

# Change to project root to enable module imports
cd "$PROJECT_ROOT"

# Run using Python -c with inline code (module execution has background issues)
python3 -c "
import sys
import os
sys.path.insert(0, '$PROJECT_ROOT/python')
os.environ['SYSMON_AGGREGATOR_TOKEN'] = '$SYSMON_TOKEN'
from sysmon.aggregator.server import run_aggregator_server
run_aggregator_server(host='0.0.0.0', port=$AGGREGATOR_PORT, db_path='$AGGREGATOR_DB')
" >> "$AGGREGATOR_LOG" 2>&1 &

AGGREGATOR_PID=$!
echo "$AGGREGATOR_PID" > "$PID_FILE"
echo "   PID: $AGGREGATOR_PID"
echo "   Logs: $AGGREGATOR_LOG"

# Wait a moment and check if it's still running
sleep 2

if ! kill -0 $AGGREGATOR_PID 2>/dev/null; then
    echo -e "${RED}✗ Aggregator failed to start. Check logs:${NC}"
    echo "   tail -f $AGGREGATOR_LOG"
    rm -f "$PID_FILE"
    exit 1
fi

echo
echo -e "${GREEN}✓ Aggregator server is running!${NC}"
echo
echo "Services:"
echo "  Aggregator PID: $AGGREGATOR_PID"
echo
echo "Access:"
echo "  Dashboard:  http://localhost:$AGGREGATOR_PORT"
echo "  API Health: http://localhost:$AGGREGATOR_PORT/api/health"
echo "  API Hosts:  http://localhost:$AGGREGATOR_PORT/api/hosts"
echo
echo "To stop:"
echo "  kill $AGGREGATOR_PID"
echo "  or run: $SCRIPT_DIR/stop.sh"
echo
echo "Logs:"
echo "  tail -f $AGGREGATOR_LOG"
echo

# If running in foreground mode
if [ "$1" = "-f" ] || [ "$1" = "--foreground" ]; then
    echo "Running in foreground mode (Ctrl+C to stop)..."
    trap "kill $AGGREGATOR_PID 2>/dev/null; echo; echo 'Stopped'; exit" INT TERM
    wait $AGGREGATOR_PID
fi
