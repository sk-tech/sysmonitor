#!/bin/bash
# Demo script for distributed multi-host monitoring

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
AGGREGATOR_PORT="${SYSMON_AGGREGATOR_PORT:-9000}"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔═══════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  SysMonitor Distributed Demo - Week 5            ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════════════════╝${NC}"
echo

# Set auth token
export SYSMON_TOKEN="${SYSMON_TOKEN:-sysmon-demo-token-12345}"

# Arrays to hold PIDs
DAEMON_PIDS=()
AGENT_NAMES=("web-server-01" "db-server-01" "app-server-01")

# Cleanup function
cleanup() {
    echo
    echo -e "${YELLOW}Cleaning up...${NC}"
    
    # Kill all daemons
    for pid in "${DAEMON_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
            echo "  Stopped daemon PID: $pid"
        fi
    done
    
    # Kill aggregator
    if [ -f "/tmp/sysmon_aggregator.pid" ]; then
        AGG_PID=$(cat "/tmp/sysmon_aggregator.pid")
        if kill -0 "$AGG_PID" 2>/dev/null; then
            kill "$AGG_PID" 2>/dev/null
            echo "  Stopped aggregator PID: $AGG_PID"
        fi
        rm -f "/tmp/sysmon_aggregator.pid"
    fi
    
    echo -e "${GREEN}✓ Cleanup complete${NC}"
    exit 0
}

trap cleanup INT TERM

# Check if binaries exist
if [ ! -f "$PROJECT_ROOT/build/bin/sysmond" ]; then
    echo -e "${RED}✗ Daemon binary not found. Building project...${NC}"
    cd "$PROJECT_ROOT/build" && cmake --build . -j$(nproc)
fi

# Step 1: Start aggregator
echo -e "${CYAN}Step 1: Starting Aggregator Server${NC}"
echo "========================================"
bash "$SCRIPT_DIR/start-aggregator.sh"
sleep 3

# Check aggregator health
echo -e "\n${CYAN}Checking aggregator health...${NC}"
HEALTH_RESPONSE=$(curl -s "http://localhost:$AGGREGATOR_PORT/api/health" || echo '{"status":"error"}')
if echo "$HEALTH_RESPONSE" | grep -q '"status".*"healthy"'; then
    echo -e "${GREEN}✓ Aggregator is healthy${NC}"
else
    echo -e "${RED}✗ Aggregator health check failed${NC}"
    echo "Response: $HEALTH_RESPONSE"
    exit 1
fi

# Step 2: Start multiple agents
echo
echo -e "${CYAN}Step 2: Starting Multiple Agent Daemons${NC}"
echo "=========================================="

for i in "${!AGENT_NAMES[@]}"; do
    AGENT_NAME="${AGENT_NAMES[$i]}"
    DB_PATH="/tmp/sysmon_${AGENT_NAME}.db"
    CONFIG_PATH="/tmp/sysmon_${AGENT_NAME}.yaml"
    LOG_PATH="/tmp/sysmon_${AGENT_NAME}.log"
    
    # Create agent config
    cat > "$CONFIG_PATH" <<EOF
mode: agent
hostname: ${AGENT_NAME}
collection_interval_ms: 2000

agent:
  aggregator_url: http://localhost:${AGGREGATOR_PORT}
  auth_token: ${SYSMON_TOKEN}
  push_interval_seconds: 5
  tags:
    environment: demo
    datacenter: local
    role: $([ $i -eq 0 ] && echo "web" || ([ $i -eq 1 ] && echo "database" || echo "application"))
EOF
    
    echo
    echo -e "${GREEN}▶ Starting agent: ${AGENT_NAME}${NC}"
    echo "   Config: $CONFIG_PATH"
    echo "   Database: $DB_PATH"
    echo "   Logs: $LOG_PATH"
    
    "$PROJECT_ROOT/build/bin/sysmond" "$DB_PATH" "$CONFIG_PATH" > "$LOG_PATH" 2>&1 &
    DAEMON_PID=$!
    DAEMON_PIDS+=("$DAEMON_PID")
    echo "   PID: $DAEMON_PID"
    
    sleep 1
    
    # Check if daemon is running
    if ! kill -0 $DAEMON_PID 2>/dev/null; then
        echo -e "${RED}✗ Agent ${AGENT_NAME} failed to start${NC}"
        echo "   Check logs: tail -f $LOG_PATH"
    else
        echo -e "${GREEN}✓ Agent ${AGENT_NAME} is running${NC}"
    fi
done

# Step 3: Wait for metrics to flow
echo
echo -e "${CYAN}Step 3: Waiting for Metrics${NC}"
echo "==============================="
echo "Waiting 10 seconds for agents to push metrics..."
for i in {10..1}; do
    echo -n "  $i... "
    sleep 1
done
echo "Done!"

# Step 4: Check registered hosts
echo
echo -e "${CYAN}Step 4: Checking Registered Hosts${NC}"
echo "===================================="
HOSTS_RESPONSE=$(curl -s "http://localhost:$AGGREGATOR_PORT/api/hosts")
if echo "$HOSTS_RESPONSE" | grep -q '"hosts"'; then
    HOST_COUNT=$(echo "$HOSTS_RESPONSE" | grep -o '"hostname"' | wc -l)
    echo -e "${GREEN}✓ Found ${HOST_COUNT} registered hosts:${NC}"
    echo "$HOSTS_RESPONSE" | python3 -m json.tool 2>/dev/null || echo "$HOSTS_RESPONSE"
else
    echo -e "${YELLOW}⚠️  No hosts registered yet or API error${NC}"
    echo "Response: $HOSTS_RESPONSE"
fi

# Step 5: Check fleet summary
echo
echo -e "${CYAN}Step 5: Fleet Summary${NC}"
echo "======================="
SUMMARY_RESPONSE=$(curl -s "http://localhost:$AGGREGATOR_PORT/api/fleet/summary")
echo "$SUMMARY_RESPONSE" | python3 -m json.tool 2>/dev/null || echo "$SUMMARY_RESPONSE"

# Step 6: Open dashboard
echo
echo -e "${CYAN}Step 6: Opening Multi-Host Dashboard${NC}"
echo "======================================"
DASHBOARD_URL="http://localhost:$AGGREGATOR_PORT"
echo "Dashboard URL: $DASHBOARD_URL"

# Try to open browser
if command -v xdg-open &> /dev/null; then
    xdg-open "$DASHBOARD_URL" 2>/dev/null &
    echo -e "${GREEN}✓ Opened dashboard in browser${NC}"
elif command -v open &> /dev/null; then
    open "$DASHBOARD_URL" 2>/dev/null &
    echo -e "${GREEN}✓ Opened dashboard in browser${NC}"
else
    echo -e "${YELLOW}⚠️  Could not auto-open browser. Please visit:${NC}"
    echo "   $DASHBOARD_URL"
fi

# Summary
echo
echo -e "${BLUE}╔═══════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║           Demo is Running!                        ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════════════════╝${NC}"
echo
echo -e "${GREEN}Services:${NC}"
echo "  Aggregator: PID $(cat /tmp/sysmon_aggregator.pid 2>/dev/null || echo 'N/A')"
for i in "${!AGENT_NAMES[@]}"; do
    echo "  Agent ${AGENT_NAMES[$i]}: PID ${DAEMON_PIDS[$i]}"
done
echo
echo -e "${GREEN}Access:${NC}"
echo "  Multi-Host Dashboard: $DASHBOARD_URL"
echo "  API Hosts:           $DASHBOARD_URL/api/hosts"
echo "  API Fleet Summary:   $DASHBOARD_URL/api/fleet/summary"
echo
echo -e "${GREEN}Logs:${NC}"
echo "  Aggregator:          tail -f ~/.sysmon/aggregator.log"
for i in "${!AGENT_NAMES[@]}"; do
    echo "  Agent ${AGENT_NAMES[$i]}: tail -f /tmp/sysmon_${AGENT_NAMES[$i]}.log"
done
echo
echo -e "${YELLOW}Press Ctrl+C to stop all services${NC}"
echo

# Keep running
while true; do
    sleep 5
    
    # Check if all daemons are still running
    for i in "${!DAEMON_PIDS[@]}"; do
        if ! kill -0 "${DAEMON_PIDS[$i]}" 2>/dev/null; then
            echo -e "${RED}⚠️  Agent ${AGENT_NAMES[$i]} stopped unexpectedly${NC}"
        fi
    done
done
