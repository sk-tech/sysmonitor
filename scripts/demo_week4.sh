#!/bin/bash
# Week 4 Demo Script - Alerting System

set -e

echo "=========================================="
echo "SysMonitor Week 4: Alerting Demo"
echo "=========================================="
echo ""

# Setup
echo "1. Setting up alert configuration..."
cp config/alerts.yaml.example ~/.sysmon/alerts.yaml
echo "✓ Copied example config to ~/.sysmon/alerts.yaml"
echo ""

# View configured alerts
echo "2. Displaying configured alerts..."
./build/bin/sysmon alerts
echo ""

# Test alert configuration
echo "3. Testing alert configuration with current metrics..."
./build/bin/sysmon test-alert ~/.sysmon/alerts.yaml
echo ""

# Start daemon with alerts (background)
echo "4. Starting daemon with alert manager..."
echo "   (Will run for 30 seconds)"
timeout 30 ./build/bin/sysmond &
DAEMON_PID=$!

# Wait for startup
sleep 3

# Show current metrics
echo ""
echo "5. Current system metrics:"
./build/bin/sysmon all
echo ""

# Monitor for a bit
echo "6. Monitoring for alerts (waiting 25 seconds)..."
echo "   Check ~/.sysmon/alerts.log for any fired alerts"
sleep 25

# Check if any alerts were logged
echo ""
echo "7. Alert log contents:"
if [ -f ~/.sysmon/alerts.log ]; then
    echo "----------------------------------------"
    tail -20 ~/.sysmon/alerts.log || echo "No alerts fired (system is healthy)"
    echo "----------------------------------------"
else
    echo "No alerts fired (system is healthy)"
fi

# Cleanup
wait $DAEMON_PID 2>/dev/null || true

echo ""
echo "=========================================="
echo "Demo Complete!"
echo "=========================================="
echo ""
echo "Key Achievements:"
echo "  ✓ Alert configuration loaded successfully"
echo "  ✓ Alert manager running in daemon"
echo "  ✓ Real-time threshold evaluation active"
echo "  ✓ Notification handlers registered"
echo ""
echo "Try these commands:"
echo "  ./build/bin/sysmon alerts           # View alert status"
echo "  tail -f ~/.sysmon/alerts.log        # Monitor alerts"
echo "  stress-ng --cpu 8 --timeout 60s     # Generate test alert"
echo ""
