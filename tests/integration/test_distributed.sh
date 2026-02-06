#!/bin/bash
# Integration test: Distributed monitoring
set -e

echo "=== SysMonitor Distributed Monitoring Integration Test ==="

# Configuration
TEST_DIR="/tmp/sysmon_distributed_$$"
AGG_PORT=19000
AGENT1_PORT=19001
AGENT2_PORT=19002

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

cleanup() {
    echo "Cleaning up..."
    pkill -f "sysmond" || true
    pkill -f "python.*aggregator" || true
    pkill -f "python.*agent" || true
    rm -rf "$TEST_DIR"
}

trap cleanup EXIT

# Setup
echo "Setting up test environment..."
mkdir -p "$TEST_DIR/aggregator"
mkdir -p "$TEST_DIR/agent1"
mkdir -p "$TEST_DIR/agent2"

# Step 1: Start aggregator
echo "Step 1: Starting aggregator..."
cd python
python3 -m sysmon.aggregator.server \
    --port $AGG_PORT \
    --db "$TEST_DIR/aggregator/metrics.db" \
    > "$TEST_DIR/aggregator.log" 2>&1 &
AGG_PID=$!
cd ..

sleep 3

if ! kill -0 $AGG_PID 2>/dev/null; then
    echo -e "${RED}FAIL: Aggregator failed to start${NC}"
    cat "$TEST_DIR/aggregator.log"
    exit 1
fi
echo -e "${GREEN}PASS: Aggregator started on port $AGG_PORT${NC}"

# Step 2: Create agent configs
echo "Step 2: Creating agent configurations..."

cat > "$TEST_DIR/agent1/config.yaml" << EOF
agent:
  hostname: test-agent-1
  tags:
    environment: test
    datacenter: dc1

publisher:
  aggregator_url: http://localhost:$AGG_PORT
  publish_interval_ms: 1000
  batch_size: 10

storage:
  db_path: $TEST_DIR/agent1/metrics.db
  retention_days: 1

collection:
  interval_ms: 500
EOF

cat > "$TEST_DIR/agent2/config.yaml" << EOF
agent:
  hostname: test-agent-2
  tags:
    environment: test
    datacenter: dc2

publisher:
  aggregator_url: http://localhost:$AGG_PORT
  publish_interval_ms: 1000
  batch_size: 10

storage:
  db_path: $TEST_DIR/agent2/metrics.db
  retention_days: 1

collection:
  interval_ms: 500
EOF

# Step 3: Start agents
echo "Step 3: Starting agents..."

./build/bin/sysmond --config "$TEST_DIR/agent1/config.yaml" \
    > "$TEST_DIR/agent1.log" 2>&1 &
AGENT1_PID=$!

./build/bin/sysmond --config "$TEST_DIR/agent2/config.yaml" \
    > "$TEST_DIR/agent2.log" 2>&1 &
AGENT2_PID=$!

sleep 5

# Check agents
if ! kill -0 $AGENT1_PID 2>/dev/null; then
    echo -e "${RED}FAIL: Agent 1 failed to start${NC}"
    cat "$TEST_DIR/agent1.log"
    exit 1
fi

if ! kill -0 $AGENT2_PID 2>/dev/null; then
    echo -e "${RED}FAIL: Agent 2 failed to start${NC}"
    cat "$TEST_DIR/agent2.log"
    exit 1
fi

echo -e "${GREEN}PASS: Both agents started${NC}"

# Step 4: Wait for metric collection
echo "Step 4: Collecting metrics..."
sleep 10

# Step 5: Verify aggregator has metrics
echo "Step 5: Verifying aggregator received metrics..."

# Check hosts endpoint
HOSTS_JSON=$(curl -s "http://localhost:$AGG_PORT/api/hosts")
HOST_COUNT=$(echo "$HOSTS_JSON" | python3 -c "import sys, json; print(len(json.load(sys.stdin)))")

if [ "$HOST_COUNT" -ge 2 ]; then
    echo -e "${GREEN}PASS: Aggregator has metrics from $HOST_COUNT hosts${NC}"
else
    echo -e "${RED}FAIL: Aggregator only has metrics from $HOST_COUNT hosts${NC}"
    echo "Hosts JSON: $HOSTS_JSON"
    exit 1
fi

# Check metric count
METRICS_JSON=$(curl -s "http://localhost:$AGG_PORT/api/query/latest?metric_type=cpu.total_usage&limit=100")
METRIC_COUNT=$(echo "$METRICS_JSON" | python3 -c "import sys, json; print(len(json.load(sys.stdin)))")

if [ "$METRIC_COUNT" -gt 0 ]; then
    echo -e "${GREEN}PASS: Aggregator has $METRIC_COUNT metrics${NC}"
else
    echo -e "${RED}FAIL: No metrics in aggregator${NC}"
    exit 1
fi

# Step 6: Verify metrics from both hosts
echo "Step 6: Verifying metrics from both hosts..."

AGENT1_METRICS=$(curl -s "http://localhost:$AGG_PORT/api/query/latest?host=test-agent-1&limit=1")
AGENT2_METRICS=$(curl -s "http://localhost:$AGG_PORT/api/query/latest?host=test-agent-2&limit=1")

AGENT1_COUNT=$(echo "$AGENT1_METRICS" | python3 -c "import sys, json; print(len(json.load(sys.stdin)))")
AGENT2_COUNT=$(echo "$AGENT2_METRICS" | python3 -c "import sys, json; print(len(json.load(sys.stdin)))")

if [ "$AGENT1_COUNT" -gt 0 ]; then
    echo -e "${GREEN}PASS: Metrics from agent 1${NC}"
else
    echo -e "${RED}FAIL: No metrics from agent 1${NC}"
    exit 1
fi

if [ "$AGENT2_COUNT" -gt 0 ]; then
    echo -e "${GREEN}PASS: Metrics from agent 2${NC}"
else
    echo -e "${RED}FAIL: No metrics from agent 2${NC}"
    exit 1
fi

# Step 7: Test aggregator dashboard
echo "Step 7: Testing aggregator dashboard..."

HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:$AGG_PORT/")
if [ "$HTTP_CODE" = "200" ]; then
    echo -e "${GREEN}PASS: Aggregator dashboard accessible${NC}"
else
    echo -e "${YELLOW}WARN: Dashboard returned HTTP $HTTP_CODE${NC}"
fi

echo ""
echo "==================================="
echo -e "${GREEN}DISTRIBUTED TEST PASSED!${NC}"
echo "==================================="
echo ""
echo "Test Summary:"
echo "  - Aggregator: Running on port $AGG_PORT"
echo "  - Agent 1: Running and sending metrics"
echo "  - Agent 2: Running and sending metrics"
echo "  - Total metrics: $METRIC_COUNT"
echo "  - Hosts tracked: $HOST_COUNT"

exit 0
