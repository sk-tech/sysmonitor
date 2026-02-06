#!/bin/bash
# Integration test: Full pipeline
set -e

echo "=== SysMonitor Full Pipeline Integration Test ==="

# Configuration
TEST_DIR="/tmp/sysmon_test_$$"
DB_PATH="$TEST_DIR/metrics.db"
CONFIG_PATH="$TEST_DIR/config.yaml"
LOG_PATH="$TEST_DIR/daemon.log"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

cleanup() {
    echo "Cleaning up..."
    pkill -f sysmond || true
    pkill -f "python.*sysmon" || true
    rm -rf "$TEST_DIR"
}

trap cleanup EXIT

# Setup
echo "Setting up test environment..."
mkdir -p "$TEST_DIR"

# Create test config
cat > "$CONFIG_PATH" << EOF
storage:
  db_path: $DB_PATH
  retention_days: 1
  batch_size: 10

collection:
  interval_ms: 500

alerts:
  enabled: false
EOF

# Step 1: Build if needed
echo "Step 1: Checking build..."
if [ ! -f "./build/bin/sysmond" ]; then
    echo "Building project..."
    ./build.sh || exit 1
fi

# Step 2: Start daemon
echo "Step 2: Starting daemon..."
./build/bin/sysmond --config "$CONFIG_PATH" > "$LOG_PATH" 2>&1 &
DAEMON_PID=$!
echo "Daemon started with PID $DAEMON_PID"

# Wait for daemon to initialize
sleep 3

# Check if daemon is running
if ! kill -0 $DAEMON_PID 2>/dev/null; then
    echo -e "${RED}FAIL: Daemon failed to start${NC}"
    cat "$LOG_PATH"
    exit 1
fi
echo -e "${GREEN}PASS: Daemon started successfully${NC}"

# Step 3: Collect metrics
echo "Step 3: Collecting metrics..."
sleep 5  # Let it collect some metrics

# Step 4: Query CLI
echo "Step 4: Testing CLI commands..."

# Test info command
./build/bin/sysmon info > /dev/null
if [ $? -eq 0 ]; then
    echo -e "${GREEN}PASS: sysmon info${NC}"
else
    echo -e "${RED}FAIL: sysmon info${NC}"
    exit 1
fi

# Test CPU command
./build/bin/sysmon cpu > /dev/null
if [ $? -eq 0 ]; then
    echo -e "${GREEN}PASS: sysmon cpu${NC}"
else
    echo -e "${RED}FAIL: sysmon cpu${NC}"
    exit 1
fi

# Test memory command
./build/bin/sysmon memory > /dev/null
if [ $? -eq 0 ]; then
    echo -e "${GREEN}PASS: sysmon memory${NC}"
else
    echo -e "${RED}FAIL: sysmon memory${NC}"
    exit 1
fi

# Step 5: Verify database
echo "Step 5: Verifying database..."
if [ -f "$DB_PATH" ]; then
    # Check if database has data
    ROW_COUNT=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM metrics;")
    if [ "$ROW_COUNT" -gt 0 ]; then
        echo -e "${GREEN}PASS: Database has $ROW_COUNT metrics${NC}"
    else
        echo -e "${RED}FAIL: Database is empty${NC}"
        exit 1
    fi
else
    echo -e "${RED}FAIL: Database not created${NC}"
    exit 1
fi

# Step 6: Test history query
echo "Step 6: Testing history query..."
./build/bin/sysmon history cpu.total_usage 1m > /dev/null
if [ $? -eq 0 ]; then
    echo -e "${GREEN}PASS: sysmon history${NC}"
else
    echo -e "${RED}FAIL: sysmon history${NC}"
    exit 1
fi

# Step 7: Start Python API
echo "Step 7: Starting Python API..."
cd python
python3 -m sysmon.api.server --port 18888 --db "$DB_PATH" > /dev/null 2>&1 &
API_PID=$!
cd ..

sleep 3

# Check if API is running
if ! kill -0 $API_PID 2>/dev/null; then
    echo -e "${RED}FAIL: API failed to start${NC}"
    exit 1
fi

# Step 8: Test API endpoints
echo "Step 8: Testing API endpoints..."

# Health check
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:18888/health)
if [ "$HTTP_CODE" = "200" ]; then
    echo -e "${GREEN}PASS: API health check${NC}"
else
    echo -e "${RED}FAIL: API health check (HTTP $HTTP_CODE)${NC}"
    exit 1
fi

# Latest metrics
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:18888/api/latest?metric_type=cpu.total_usage")
if [ "$HTTP_CODE" = "200" ]; then
    echo -e "${GREEN}PASS: API latest metrics${NC}"
else
    echo -e "${RED}FAIL: API latest metrics (HTTP $HTTP_CODE)${NC}"
    exit 1
fi

# Metric types
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:18888/api/types")
if [ "$HTTP_CODE" = "200" ]; then
    echo -e "${GREEN}PASS: API metric types${NC}"
else
    echo -e "${RED}FAIL: API metric types (HTTP $HTTP_CODE)${NC}"
    exit 1
fi

echo ""
echo "==================================="
echo -e "${GREEN}ALL TESTS PASSED!${NC}"
echo "==================================="
echo ""
echo "Test Summary:"
echo "  - Daemon: Running"
echo "  - Database: $ROW_COUNT metrics collected"
echo "  - CLI: All commands working"
echo "  - API: All endpoints responding"

exit 0
