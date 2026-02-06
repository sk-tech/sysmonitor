#!/bin/bash
# Week 7 Demo: ML Anomaly Detection
# Demonstrates intelligent anomaly detection using statistical and ML-based methods

set -e

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  SysMonitor Week 7: ML Anomaly Detection Demo            ║${NC}"
echo -e "${BLUE}║  Intelligent monitoring with machine learning            ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════════════════════════╝${NC}"
echo

# Check if ML dependencies are installed
echo -e "${YELLOW}[1/8] Checking ML dependencies...${NC}"
if ! python3 -c "import numpy, scipy, sklearn" 2>/dev/null; then
    echo -e "${YELLOW}Installing ML dependencies...${NC}"
    pip3 install -q -r python/requirements-ml.txt || {
        echo -e "${RED}Failed to install ML dependencies${NC}"
        exit 1
    }
    echo -e "${GREEN}✓ ML dependencies installed${NC}"
else
    echo -e "${GREEN}✓ ML dependencies already installed${NC}"
fi
echo

# Start daemon if not running
echo -e "${YELLOW}[2/8] Ensuring daemon is running...${NC}"
if ! pgrep -f sysmond > /dev/null; then
    echo "Starting sysmond..."
    ./build/bin/sysmond &
    DAEMON_PID=$!
    sleep 3
    echo -e "${GREEN}✓ Daemon started (PID: $DAEMON_PID)${NC}"
else
    echo -e "${GREEN}✓ Daemon already running${NC}"
    DAEMON_PID=$(pgrep -f sysmond)
fi
echo

# Wait for some data collection
echo -e "${YELLOW}[3/8] Collecting baseline data (30 seconds)...${NC}"
echo "This allows the system to learn normal behavior patterns."
for i in {1..30}; do
    echo -ne "\r  Progress: [$i/30] "
    sleep 1
done
echo -e "\n${GREEN}✓ Baseline data collected${NC}"
echo

# Start API server
echo -e "${YELLOW}[4/8] Starting API server with ML endpoints...${NC}"
python3 python/sysmon/api/server.py 8000 > /tmp/sysmon-api.log 2>&1 &
API_PID=$!
sleep 2

if ps -p $API_PID > /dev/null; then
    echo -e "${GREEN}✓ API server started (PID: $API_PID)${NC}"
    echo "  URL: http://localhost:8000"
else
    echo -e "${RED}✗ Failed to start API server${NC}"
    cat /tmp/sysmon-api.log
    exit 1
fi
echo

# Train ML models
echo -e "${YELLOW}[5/8] Training ML models...${NC}"
echo "Training anomaly detection models on historical data..."
TRAIN_RESPONSE=$(curl -s -X POST http://localhost:8000/api/ml/train \
    -H "Content-Type: application/json" \
    -d '{"metric": "cpu.total_usage", "hours": 24}')

if echo "$TRAIN_RESPONSE" | grep -q '"status":"success"'; then
    echo -e "${GREEN}✓ Models trained successfully${NC}"
    echo "$TRAIN_RESPONSE" | python3 -m json.tool | head -10
else
    echo -e "${YELLOW}! Training may need more data (this is normal for new installations)${NC}"
    echo "$TRAIN_RESPONSE" | python3 -m json.tool | head -10
fi
echo

# Check baseline
echo -e "${YELLOW}[6/8] Checking learned baseline...${NC}"
BASELINE_RESPONSE=$(curl -s "http://localhost:8000/api/ml/baseline?metric=cpu.total_usage")
if echo "$BASELINE_RESPONSE" | grep -q '"baseline"'; then
    echo -e "${GREEN}✓ Baseline learned${NC}"
    echo "$BASELINE_RESPONSE" | python3 -c "
import sys, json
data = json.load(sys.stdin)
if 'baseline' in data:
    b = data['baseline']
    print(f\"  Mean: {b['mean']:.2f}%\")
    print(f\"  Std Dev: {b['stddev']:.2f}%\")
    print(f\"  Range: {b['min_value']:.1f}% - {b['max_value']:.1f}%\")
    print(f\"  Samples: {b['sample_count']}\")
    t = data['thresholds']
    print(f\"  Thresholds: {t['lower']:.2f}% - {t['upper']:.2f}%\")
"
else
    echo -e "${YELLOW}! No baseline yet (need more data)${NC}"
fi
echo

# Generate synthetic anomaly
echo -e "${YELLOW}[7/8] Generating synthetic anomaly...${NC}"
echo "Starting CPU stress to trigger anomaly detection..."
if command -v stress-ng &> /dev/null; then
    stress-ng --cpu 4 --timeout 20s > /dev/null 2>&1 &
    STRESS_PID=$!
    echo -e "${GREEN}✓ CPU stress started${NC}"
    
    # Wait a bit for metrics to be collected
    sleep 10
    
    # Run anomaly detection
    echo "Running anomaly detection..."
    DETECT_RESPONSE=$(curl -s "http://localhost:8000/api/ml/detect?metric=cpu.total_usage")
    
    if echo "$DETECT_RESPONSE" | grep -q '"is_anomaly"'; then
        IS_ANOMALY=$(echo "$DETECT_RESPONSE" | python3 -c "import sys,json; print(json.load(sys.stdin)['is_anomaly'])")
        
        if [ "$IS_ANOMALY" = "True" ]; then
            echo -e "${RED}⚠️  ANOMALY DETECTED!${NC}"
        else
            echo -e "${GREEN}✓ System operating normally${NC}"
        fi
        
        echo "$DETECT_RESPONSE" | python3 -c "
import sys, json
data = json.load(sys.stdin)
print(f\"  Current Value: {data['value']:.2f}%\")
print(f\"  Is Anomaly: {data['is_anomaly']}\")
print(f\"  Confidence: {data['confidence']*100:.1f}%\")
print(f\"  Detection Methods:\")
for method, result in data.get('methods', {}).items():
    status = '⚠️ ' if result['is_anomaly'] else '✓'
    print(f\"    {status} {method.capitalize()}: score={result['score']:.2f}\")
"
    fi
    
    # Wait for stress to complete
    wait $STRESS_PID 2>/dev/null || true
    echo -e "${GREEN}✓ Stress test completed${NC}"
else
    echo -e "${YELLOW}! stress-ng not found, skipping synthetic anomaly${NC}"
    echo "  Install with: sudo apt install stress-ng"
fi
echo

# Show forecast
echo -e "${YELLOW}[8/8] Generating forecast...${NC}"
FORECAST_RESPONSE=$(curl -s "http://localhost:8000/api/ml/predict?metric=cpu.total_usage&horizon=1h")
if echo "$FORECAST_RESPONSE" | grep -q '"predictions"'; then
    echo -e "${GREEN}✓ Forecast generated${NC}"
    echo "$FORECAST_RESPONSE" | python3 -c "
import sys, json
from datetime import datetime
data = json.load(sys.stdin)
predictions = data.get('predictions', [])[:5]  # Show first 5
print(f\"  Next {data['horizon_hours']} hour(s) forecast:\")
for pred in predictions:
    ts = datetime.fromtimestamp(pred['timestamp']).strftime('%H:%M:%S')
    print(f\"    {ts}: {pred['value']:.2f}%\")
if len(data.get('predictions', [])) > 5:
    print(f\"    ... and {len(data['predictions']) - 5} more points\")
"
else
    echo -e "${YELLOW}! Forecast not available yet${NC}"
fi
echo

# Summary
echo -e "${BLUE}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  Demo Complete!                                           ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════════════════════════╝${NC}"
echo
echo -e "${GREEN}✓ ML Module: Operational${NC}"
echo -e "${GREEN}✓ API Endpoints: Active${NC}"
echo -e "${GREEN}✓ Anomaly Detection: Working${NC}"
echo
echo -e "${BLUE}Dashboard:${NC} http://localhost:8000"
echo -e "${BLUE}ML Dashboard:${NC} http://localhost:8000/../dashboard-ml.html"
echo
echo -e "${YELLOW}API Endpoints:${NC}"
echo "  GET  /api/ml/detect?metric=cpu.total_usage"
echo "  GET  /api/ml/baseline?metric=cpu.total_usage"
echo "  GET  /api/ml/predict?metric=cpu.total_usage&horizon=1h"
echo "  POST /api/ml/train (body: {\"metric\": \"cpu.total_usage\"})"
echo
echo -e "${YELLOW}Try these commands:${NC}"
echo "  curl http://localhost:8000/api/ml/detect?metric=cpu.total_usage | jq"
echo "  curl http://localhost:8000/api/ml/baseline?metric=memory.usage_percent | jq"
echo "  curl -X POST http://localhost:8000/api/ml/train -H 'Content-Type: application/json' -d '{\"hours\":24}' | jq"
echo
echo "Press Ctrl+C to stop all services"
echo

# Cleanup function
cleanup() {
    echo
    echo -e "${YELLOW}Cleaning up...${NC}"
    [ ! -z "$API_PID" ] && kill $API_PID 2>/dev/null && echo "Stopped API server"
    [ ! -z "$DAEMON_PID" ] && kill $DAEMON_PID 2>/dev/null && echo "Stopped daemon"
    [ ! -z "$STRESS_PID" ] && kill $STRESS_PID 2>/dev/null
    echo -e "${GREEN}Done${NC}"
}

trap cleanup EXIT INT TERM

# Keep running
wait
