#!/usr/bin/env python3
"""Push test data to aggregator to demonstrate dashboard"""

import requests
import time
import random
from datetime import datetime

AGGREGATOR_URL = "http://localhost:9000"
AUTH_TOKEN = "sysmon-demo-token-12345"

hosts = ["web-server-01", "db-server-01", "app-server-01"]

def push_metrics(host, cpu_usage, memory_used, memory_total):
    """Push metrics for a host"""
    timestamp = int(time.time())
    data = {
        "hostname": host,
        "version": "0.5.0",
        "platform": "Linux",
        "tags": {
            "environment": "demo",
            "datacenter": "local"
        },
        "metrics": [
            {"timestamp": timestamp, "metric_type": "cpu.total_usage", "value": cpu_usage},
            {"timestamp": timestamp, "metric_type": "memory.used_bytes", "value": memory_used * 1024 * 1024},
            {"timestamp": timestamp, "metric_type": "memory.total_bytes", "value": memory_total * 1024 * 1024},
            {"timestamp": timestamp, "metric_type": "memory.percent", "value": (memory_used / memory_total) * 100}
        ]
    }
    
    headers = {
        "Authorization": f"Bearer {AUTH_TOKEN}",
        "Content-Type": "application/json"
    }
    
    try:
        response = requests.post(
            f"{AGGREGATOR_URL}/api/metrics",
            json=data,
            headers=headers,
            timeout=5
        )
        if response.status_code == 200:
            print(f"✓ Pushed metrics for {host}")
            return True
        else:
            print(f"✗ Failed to push for {host}: {response.status_code} - {response.text}")
            return False
    except Exception as e:
        print(f"✗ Error pushing for {host}: {e}")
        return False

def main():
    print("Pushing test metrics to aggregator...")
    print(f"Aggregator: {AGGREGATOR_URL}")
    print(f"Hosts: {', '.join(hosts)}\n")
    
    # Base values for each host
    base_cpu = {
        "web-server-01": 45.0,
        "db-server-01": 75.0,
        "app-server-01": 30.0
    }
    
    base_memory = {
        "web-server-01": 2048,
        "db-server-01": 4096,
        "app-server-01": 1536
    }
    
    memory_total = 8192  # 8GB
    
    for i in range(20):
        print(f"\n=== Push {i+1}/20 ===")
        for host in hosts:
            # Vary metrics slightly
            cpu = base_cpu[host] + random.uniform(-5, 5)
            memory = base_memory[host] + random.randint(-200, 200)
            
            push_metrics(host, cpu, memory, memory_total)
        
        time.sleep(2)
    
    print("\n✓ Test data push complete!")
    print(f"\nView dashboard at: {AGGREGATOR_URL}")

if __name__ == "__main__":
    main()
