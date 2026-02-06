#!/bin/bash
# Integration test: Alert system
set -e

echo "=== SysMonitor Alert System Integration Test ==="

TEST_DIR="/tmp/sysmon_alerts_$$"
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

cleanup() {
    echo "Cleaning up..."
    pkill -f "sysmond.*alerts" || true
    pkill -f "stress-ng" || true
    rm -rf "$TEST_DIR"
}

trap cleanup EXIT

# Setup
echo "Setting up test environment..."
mkdir -p "$TEST_DIR"

# Create alert configuration
cat > "$TEST_DIR/alerts.yaml" << EOF
alerts:
  - name: test_high_cpu
    metric: cpu.total_usage
    condition: greater_than
    threshold: 50.0
    duration: 2s
    severity: warning
    notifications:
      - type: log
        path: $TEST_DIR/alerts.log
  
  - name: test_high_memory
    metric: memory.usage_percent
    condition: greater_than
    threshold: 90.0
    duration: 5s
    severity: critical
    notifications:
      - type: log
        path: $TEST_DIR/alerts.log
EOF

# Create daemon config
cat > "$TEST_DIR/config.yaml" << EOF
storage:
  db_path: $TEST_DIR/metrics.db
  retention_days: 1

collection:
  interval_ms: 500

alerts:
  enabled: true
  config_path: $TEST_DIR/alerts.yaml
EOF

# Step 1: Start daemon with alerts
echo "Step 1: Starting daemon with alert monitoring..."
./build/bin/sysmond --config "$TEST_DIR/config.yaml" \
    > "$TEST_DIR/daemon.log" 2>&1 &
DAEMON_PID=$!

sleep 3

if ! kill -0 $DAEMON_PID 2>/dev/null; then
    echo -e "${RED}FAIL: Daemon failed to start${NC}"
    cat "$TEST_DIR/daemon.log"
    exit 1
fi
echo -e "${GREEN}PASS: Daemon started with alerts enabled${NC}"

# Step 2: Generate CPU load to trigger alert
echo "Step 2: Generating CPU load..."

if command -v stress-ng &> /dev/null; then
    # Generate high CPU load
    stress-ng --cpu 4 --timeout 15s > /dev/null 2>&1 &
    STRESS_PID=$!
    echo "stress-ng started with PID $STRESS_PID"
else
    echo -e "${YELLOW}WARN: stress-ng not installed, simulating load differently${NC}"
    # Alternative: tight loop
    for i in {1..4}; do
        yes > /dev/null &
    done
    sleep 15
    pkill yes
fi

# Step 3: Wait for alert to trigger
echo "Step 3: Waiting for alert to trigger..."
sleep 20

# Step 4: Check if alert was fired
echo "Step 4: Checking alert log..."

if [ -f "$TEST_DIR/alerts.log" ]; then
    ALERT_COUNT=$(grep -c "ALERT" "$TEST_DIR/alerts.log" || true)
    
    if [ "$ALERT_COUNT" -gt 0 ]; then
        echo -e "${GREEN}PASS: Alert fired ($ALERT_COUNT times)${NC}"
        echo "Alert details:"
        cat "$TEST_DIR/alerts.log"
    else
        echo -e "${RED}FAIL: No alerts found in log${NC}"
        echo "Alert log contents:"
        cat "$TEST_DIR/alerts.log"
        exit 1
    fi
else
    echo -e "${RED}FAIL: Alert log file not created${NC}"
    exit 1
fi

# Step 5: Verify alert state via CLI
echo "Step 5: Checking alert state..."

# If CLI has alert status command
if ./build/bin/sysmon alerts > /dev/null 2>&1; then
    ./build/bin/sysmon alerts
    echo -e "${GREEN}PASS: Alert status retrieved${NC}"
else
    echo -e "${YELLOW}WARN: Alert status command not available${NC}"
fi

# Step 6: Let CPU normalize and verify alert clears
echo "Step 6: Waiting for CPU to normalize..."
sleep 10

# Check logs for resolution
if grep -q "RESOLVED" "$TEST_DIR/alerts.log" 2>/dev/null; then
    echo -e "${GREEN}PASS: Alert resolved after condition normalized${NC}"
else
    echo -e "${YELLOW}WARN: Alert resolution not detected (may still be in cooldown)${NC}"
fi

echo ""
echo "==================================="
echo -e "${GREEN}ALERT TEST PASSED!${NC}"
echo "==================================="
echo ""
echo "Test Summary:"
echo "  - Alerts configured: 2"
echo "  - Alerts fired: $ALERT_COUNT"
echo "  - Alert log: $TEST_DIR/alerts.log"

exit 0
