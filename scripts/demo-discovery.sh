#!/bin/bash
# Demo: Week 6 Service Discovery with mDNS
# =========================================
# Demonstrates automatic aggregator discovery using mDNS/Bonjour

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "========================================="
echo "SysMonitor Week 6: Service Discovery Demo"
echo "========================================="
echo ""

# Check dependencies
echo "Checking dependencies..."
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 not found"
    exit 1
fi

if ! python3 -c "import zeroconf" 2>/dev/null; then
    echo "Installing zeroconf library..."
    pip3 install --user zeroconf
fi

# Check if avahi-browse is available (Linux)
if command -v avahi-browse &> /dev/null; then
    echo "✓ avahi-browse found (mDNS support available)"
else
    echo "⚠ avahi-browse not found - install avahi-utils for mDNS support"
    echo "  Ubuntu/Debian: sudo apt install avahi-utils"
fi

# Set token
export SYSMON_TOKEN="demo-token-week6"

# Create temp config directories
AGENT1_DIR="/tmp/sysmon-agent1"
AGENT2_DIR="/tmp/sysmon-agent2"
AGENT3_DIR="/tmp/sysmon-agent3"
AGGREGATOR_DIR="$HOME/.sysmon"

mkdir -p "$AGENT1_DIR" "$AGENT2_DIR" "$AGENT3_DIR" "$AGGREGATOR_DIR"

# Create agent configs with mDNS discovery (NO aggregator URL!)
cat > "$AGENT1_DIR/agent.yaml" <<EOF
mode: distributed
discovery_method: mdns
discovery_timeout_seconds: 5.0
auth_token: $SYSMON_TOKEN
push_interval_ms: 3000
hostname: agent-01
tags: role=web,region=us-west
EOF

cat > "$AGENT2_DIR/agent.yaml" <<EOF
mode: distributed
discovery_method: mdns
discovery_timeout_seconds: 5.0
auth_token: $SYSMON_TOKEN
push_interval_ms: 3000
hostname: agent-02
tags: role=database,region=us-east
EOF

cat > "$AGENT3_DIR/agent.yaml" <<EOF
mode: distributed
discovery_method: mdns
discovery_timeout_seconds: 5.0
auth_token: $SYSMON_TOKEN
push_interval_ms: 3000
hostname: agent-03
tags: role=cache,region=eu-west
EOF

echo ""
echo "Step 1: Starting aggregator with mDNS advertisement..."
echo "----------------------------------------------------"
cd "$PROJECT_ROOT"

# Start aggregator with mDNS
python3 -m sysmon.aggregator.server \
    --port 9000 \
    --db "$AGGREGATOR_DIR/aggregator.db" \
    --mdns \
    --mdns-hostname "aggregator-main" &
AGGREGATOR_PID=$!

echo "Aggregator PID: $AGGREGATOR_PID"
sleep 3

# Check if aggregator is running
if ! kill -0 $AGGREGATOR_PID 2>/dev/null; then
    echo "Error: Aggregator failed to start"
    exit 1
fi

echo "✓ Aggregator started with mDNS advertisement"
echo ""

# Show mDNS service (if avahi-browse available)
if command -v avahi-browse &> /dev/null; then
    echo "Checking mDNS service registration..."
    timeout 2 avahi-browse -t _sysmon-aggregator._tcp --resolve 2>/dev/null | head -n 10 || true
    echo ""
fi

echo "Step 2: Starting agents with auto-discovery..."
echo "----------------------------------------------------"
echo "Notice: Agents do NOT have aggregator_url configured!"
echo "They will auto-discover via mDNS..."
echo ""

# Note: For this demo, we'll simulate agents by using the Python discovery client
# In production, the C++ daemon would use the service_discovery.hpp interface

# Start agent 1
(
    echo "Agent 1: Discovering aggregator via mDNS..."
    cd "$PROJECT_ROOT"
    python3 <<'PYEOF'
import sys
import time
sys.path.insert(0, 'python')

from sysmon.discovery.mdns_service import MDNSDiscovery

# Discover aggregator
discovery = MDNSDiscovery()
url = discovery.discover_first(timeout=5.0)

if url:
    print(f"Agent 1: ✓ Discovered aggregator at {url}")
    print("Agent 1: Now sending metrics...")
    time.sleep(30)  # Simulate running for 30s
else:
    print("Agent 1: ✗ No aggregator found")
    sys.exit(1)
PYEOF
) &
AGENT1_PID=$!

sleep 2

# Start agent 2
(
    echo "Agent 2: Discovering aggregator via mDNS..."
    cd "$PROJECT_ROOT"
    python3 <<'PYEOF'
import sys
import time
sys.path.insert(0, 'python')

from sysmon.discovery.mdns_service import MDNSDiscovery

discovery = MDNSDiscovery()
url = discovery.discover_first(timeout=5.0)

if url:
    print(f"Agent 2: ✓ Discovered aggregator at {url}")
    print("Agent 2: Now sending metrics...")
    time.sleep(30)
else:
    print("Agent 2: ✗ No aggregator found")
    sys.exit(1)
PYEOF
) &
AGENT2_PID=$!

sleep 2

# Start agent 3
(
    echo "Agent 3: Discovering aggregator via mDNS..."
    cd "$PROJECT_ROOT"
    python3 <<'PYEOF'
import sys
import time
sys.path.insert(0, 'python')

from sysmon.discovery.mdns_service import MDNSDiscovery

discovery = MDNSDiscovery()
url = discovery.discover_first(timeout=5.0)

if url:
    print(f"Agent 3: ✓ Discovered aggregator at {url}")
    print("Agent 3: Now sending metrics...")
    time.sleep(30)
else:
    print("Agent 3: ✗ No aggregator found")
    sys.exit(1)
PYEOF
) &
AGENT3_PID=$!

echo ""
echo "Step 3: Monitoring system..."
echo "----------------------------------------------------"
echo "Press Ctrl+C to stop all services"
echo ""
echo "Open in browser: http://localhost:9000/api/hosts"
echo ""

# Wait for user interrupt
trap "echo ''; echo 'Stopping all services...'; kill $AGGREGATOR_PID $AGENT1_PID $AGENT2_PID $AGENT3_PID 2>/dev/null; exit 0" SIGINT SIGTERM

# Show status every 5 seconds
for i in {1..6}; do
    sleep 5
    echo "[$i/6] System running... (Ctrl+C to stop)"
    
    # Query aggregator
    if command -v curl &> /dev/null; then
        echo "  Registered hosts:"
        curl -s http://localhost:9000/api/hosts 2>/dev/null | python3 -m json.tool 2>/dev/null | grep hostname || true
    fi
done

echo ""
echo "Demo complete! Cleaning up..."
kill $AGGREGATOR_PID $AGENT1_PID $AGENT2_PID $AGENT3_PID 2>/dev/null || true
sleep 1

echo ""
echo "========================================="
echo "Key Features Demonstrated:"
echo "========================================="
echo "✓ mDNS/Bonjour service advertisement"
echo "✓ Automatic aggregator discovery"
echo "✓ Zero-configuration agents"
echo "✓ Service name: _sysmon-aggregator._tcp.local."
echo ""
echo "Next steps:"
echo "  - Test Consul integration"
echo "  - Enable TLS (./scripts/generate-certs.sh)"
echo "  - Deploy to production network"
