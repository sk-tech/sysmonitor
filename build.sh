#!/bin/bash
# Quick build and test script for SysMonitor

set -e  # Exit on error

echo "======================================"
echo "SysMonitor Build & Test Script"
echo "======================================"

# Check if build directory exists
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir build
fi

cd build

# Configure
echo ""
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DBUILD_TESTING=OFF \
         -DBUILD_PYTHON_BINDINGS=OFF

# Build
echo ""
echo "Building..."
cmake --build . -j$(nproc)

# Test
echo ""
echo "======================================"
echo "Testing Binaries"
echo "======================================"

echo ""
echo "1. System Information:"
./bin/sysmon info

echo ""
echo "2. CPU Metrics:"
./bin/sysmon cpu

echo ""
echo "3. Memory Metrics:"
./bin/sysmon memory

echo ""
echo "======================================"
echo "Build and test completed successfully!"
echo "======================================"
echo ""
echo "Run './bin/sysmon --help' for usage"
echo "Run './bin/sysmond' to start the daemon"
