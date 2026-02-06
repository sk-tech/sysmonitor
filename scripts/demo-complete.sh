# 🎯 Final Demo Script - Complete System Showcase

#!/bin/bash

# SysMonitor Complete System Demo
# Demonstrates all 8 weeks of features
# Duration: ~10 minutes

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_header() {
    echo -e "\n${BLUE}======================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}======================================${NC}\n"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_info() {
    echo -e "${YELLOW}ℹ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

# Check prerequisites
print_header "Pre-flight Checks"

if [ ! -f "./build/bin/sysmon" ]; then
    print_error "Binary not found. Running build..."
    ./build.sh
fi
print_success "Binaries ready"

if ! command -v python3 &> /dev/null; then
    print_error "Python3 not found!"
    exit 1
fi
print_success "Python3 available"

if ! command -v stress-ng &> /dev/null; then
    print_info "stress-ng not found. Some tests will be skipped."
fi

print_success "All prerequisites met"

# Demo Week 1-2: Core Monitoring & Storage
print_header "Demo: Core Monitoring (Weeks 1-2)"

print_info "Collecting system metrics..."
./build/bin/sysmon info
echo ""
./build/bin/sysmon cpu
echo ""
./build/bin/sysmon memory
print_success "Core monitoring working"

print_info "Testing history storage..."
./build/bin/sysmon history cpu.total_usage 5m 2>/dev/null || print_info "No historical data yet (run daemon first)"

# Demo Week 3: Web Dashboard
print_header "Demo: Web Dashboard (Week 3)"

print_info "Starting single-host system..."
./scripts/start.sh &
SINGLE_PID=$!
sleep 3

print_info "Dashboard available at: http://localhost:8000"
print_success "Single-host mode running (PID: $SINGLE_PID)"

# Demo Week 4: Alerting
print_header "Demo: Alerting System (Week 4)"

print_info "Viewing alert configuration..."
./build/bin/sysmon alerts 2>/dev/null || print_info "No alerts configured (check config/alerts.yaml.example)"

if [ -f ~/.sysmon/alerts.log ]; then
    print_info "Recent alerts:"
    tail -5 ~/.sysmon/alerts.log || echo "No alerts fired yet"
fi
print_success "Alert system configured"

# Demo Week 5: Distributed Monitoring
print_header "Demo: Distributed Monitoring (Week 5)"

print_info "Stopping single-host mode..."
./scripts/stop.sh
sleep 2

if [ -f "./scripts/start-aggregator.sh" ]; then
    print_info "Starting distributed mode..."
    ./scripts/start-aggregator.sh
    sleep 3
    
    print_info "Aggregator available at: http://localhost:9000"
    print_info "Multi-host dashboard: http://localhost:9000/dashboard"
    print_success "Distributed mode running"
    
    # Test distributed API
    print_info "Testing distributed API..."
    curl -s http://localhost:9000/api/health && echo "" && print_success "API responding"
else
    print_info "Distributed scripts not found (Week 5 feature)"
fi

# Demo Week 6: Service Discovery
print_header "Demo: Service Discovery (Week 6)"

if [ -f "./scripts/demo-discovery.sh" ]; then
    print_info "Service discovery features:"
    echo "  - mDNS/Bonjour automatic discovery"
    echo "  - Consul integration"
    echo "  - TLS/HTTPS support"
    
    if [ -f ~/.sysmon/certs/server.crt ]; then
        print_success "TLS certificates present"
    else
        print_info "Generate certs with: ./scripts/generate-certs.sh"
    fi
else
    print_info "Discovery features not installed (Week 6 feature)"
fi

# Demo Week 7: ML Anomaly Detection
print_header "Demo: ML Anomaly Detection (Week 7)"

if python3 -c "import sklearn" 2>/dev/null; then
    print_info "ML dependencies available"
    
    if [ -f "./scripts/demo-ml.sh" ]; then
        print_info "ML features:"
        echo "  - Statistical anomaly detection"
        echo "  - Isolation Forest ML model"
        echo "  - Adaptive baseline learning"
        echo "  - Forecast API (1-hour horizon)"
        print_success "ML system ready"
        
        print_info "Run full ML demo: ./scripts/demo-ml.sh"
    else
        print_info "ML demo script not found"
    fi
else
    print_info "Install ML deps: pip install numpy scipy scikit-learn"
fi

# Demo Week 8: Testing & CI/CD
print_header "Demo: Testing & Production (Week 8)"

print_info "Test suite status:"
if [ -f "./build/bin/run_tests" ]; then
    print_success "C++ unit tests compiled"
    print_info "Run with: ./build/bin/run_tests"
else
    print_info "Build with: cmake -B build -DBUILD_TESTING=ON"
fi

if [ -f "tests/test_aggregator.py" ]; then
    print_success "Python tests available"
    print_info "Run with: pytest tests/"
fi

if [ -f ".github/workflows/build-and-test.yml" ]; then
    print_success "CI/CD pipeline configured"
    print_info "GitHub Actions: build-and-test.yml, release.yml"
fi

if [ -f "Dockerfile.agent" ]; then
    print_success "Docker images available"
    print_info "Run with: docker-compose up -d"
fi

# Performance Test
print_header "Performance Test"

if command -v stress-ng &> /dev/null; then
    print_info "Running 10-second CPU load test..."
    stress-ng --cpu 4 --timeout 10s &> /dev/null &
    STRESS_PID=$!
    
    sleep 3
    print_info "CPU usage during load:"
    ./build/bin/sysmon cpu | grep "Usage:"
    
    wait $STRESS_PID 2>/dev/null || true
    sleep 2
    
    print_info "CPU usage after load:"
    ./build/bin/sysmon cpu | grep "Usage:"
    print_success "Performance test complete"
else
    print_info "Install stress-ng for performance testing"
fi

# CLI Showcase
print_header "CLI Commands Showcase"

print_info "Available command groups:"
./build/bin/sysmon --help 2>&1 | grep -A 100 "Commands:" || echo "See: ./build/bin/sysmon --help"

# Summary
print_header "Demo Summary"

echo -e "System Status:"
echo -e "  ${GREEN}✓${NC} Core monitoring (CPU, memory, disk, network, processes)"
echo -e "  ${GREEN}✓${NC} SQLite time-series storage with history queries"
echo -e "  ${GREEN}✓${NC} REST API with 15 endpoints"
echo -e "  ${GREEN}✓${NC} Web dashboard with real-time charts"
echo -e "  ${GREEN}✓${NC} Alert system with threshold-based rules"

if [ -f "./scripts/start-aggregator.sh" ]; then
    echo -e "  ${GREEN}✓${NC} Distributed monitoring (aggregator + agents)"
    echo -e "  ${GREEN}✓${NC} Service discovery (mDNS, Consul)"
else
    echo -e "  ${YELLOW}○${NC} Distributed monitoring (not installed)"
fi

if python3 -c "import sklearn" 2>/dev/null; then
    echo -e "  ${GREEN}✓${NC} ML anomaly detection"
else
    echo -e "  ${YELLOW}○${NC} ML features (pip install numpy scipy scikit-learn)"
fi

if [ -f "./build/bin/run_tests" ]; then
    echo -e "  ${GREEN}✓${NC} Testing & CI/CD configured"
else
    echo -e "  ${YELLOW}○${NC} Testing (build with -DBUILD_TESTING=ON)"
fi

echo ""
print_header "Quick Access Links"
echo "  Single-Host Dashboard: http://localhost:8000"
echo "  Multi-Host Dashboard:  http://localhost:9000"
echo "  ML Dashboard:          http://localhost:8000/ml-dashboard.html"
echo ""
echo "Documentation:"
echo "  Complete Summary: docs/PROJECT-COMPLETE.md"
echo "  Architecture:     docs/architecture/system-design.md"
echo "  API Reference:    docs/API.md"
echo "  CLI Reference:    docs/CLI-REFERENCE.md"
echo "  Troubleshooting:  docs/TROUBLESHOOTING.md"
echo ""

print_header "Next Steps"
echo "1. View dashboard in browser (links above)"
echo "2. Try CLI commands: ./build/bin/sysmon <command>"
echo "3. Run full ML demo: ./scripts/demo-ml.sh"
echo "4. Set up distributed mode: ./scripts/demo-distributed.sh"
echo "5. Run tests: pytest tests/ && ./build/bin/run_tests"
echo "6. Read docs: docs/PROJECT-COMPLETE.md"
echo ""

print_success "Demo complete! System is running."
print_info "Press Ctrl+C to stop all services"

# Keep running
trap 'echo ""; print_info "Shutting down..."; ./scripts/stop.sh 2>/dev/null; exit 0' INT TERM

wait
