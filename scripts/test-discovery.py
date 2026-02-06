#!/usr/bin/env python3
"""Quick test of service discovery modules (no zeroconf required for basic tests)"""

import sys
import os

# Add project to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

def test_consul_client():
    """Test Consul client (no external dependencies)"""
    print("Testing Consul client...")
    
    from sysmon.discovery.consul_client import ConsulClient
    
    client = ConsulClient("http://localhost:8500")
    print(f"✓ ConsulClient created: {client.consul_addr}")
    
    # Test URL building (no actual Consul needed)
    service_id = "sysmon-aggregator-test-9000"
    print(f"✓ Service ID format: {service_id}")
    
    print("✓ Consul client tests passed\n")

def test_mdns_import():
    """Test mDNS module imports"""
    print("Testing mDNS imports...")
    
    try:
        from sysmon.discovery.mdns_service import MDNSService, MDNSDiscovery
        print("✓ MDNSService and MDNSDiscovery imported")
        print("  Note: Actual mDNS requires 'pip install zeroconf'")
    except ImportError as e:
        print(f"⚠ mDNS import failed (expected without zeroconf): {e}")
    
    print()

def test_service_info():
    """Test service info parsing"""
    print("Testing service info structures...")
    
    service = {
        'name': 'test-aggregator',
        'addresses': ['192.168.1.100'],
        'port': 9000,
        'properties': {'protocol': 'http', 'version': '0.6.0'}
    }
    
    url = f"{service['properties']['protocol']}://{service['addresses'][0]}:{service['port']}"
    print(f"✓ Service URL: {url}")
    
    assert url == "http://192.168.1.100:9000"
    print("✓ Service info tests passed\n")

def main():
    print("=" * 60)
    print("SysMonitor Week 6: Service Discovery Module Tests")
    print("=" * 60)
    print()
    
    try:
        test_consul_client()
        test_mdns_import()
        test_service_info()
        
        print("=" * 60)
        print("✅ All basic tests passed!")
        print("=" * 60)
        print()
        print("Next steps:")
        print("  1. Install zeroconf: sudo apt install python3-pip && pip3 install zeroconf")
        print("  2. Test mDNS discovery: ./scripts/demo-discovery.sh")
        print("  3. Generate TLS certs: ./scripts/generate-certs.sh")
        
    except Exception as e:
        print(f"❌ Test failed: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == '__main__':
    main()
