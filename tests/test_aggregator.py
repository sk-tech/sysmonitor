"""
Tests for aggregator API
"""

import pytest
import requests
import json
import time
from multiprocessing import Process
import sys
import os

# Add parent directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from sysmon.aggregator.server import AggregatorServer


class TestAggregatorAPI:
    """Test suite for aggregator API"""
    
    @pytest.fixture(autouse=True)
    def setup(self, temp_db, aggregator_port):
        """Start aggregator server for tests"""
        self.port = aggregator_port
        self.base_url = f"http://localhost:{self.port}"
        self.db_path = temp_db
        
        # Start server in background
        def run_server():
            server = AggregatorServer(port=self.port, db_path=self.db_path)
            server.start()
        
        self.server_process = Process(target=run_server)
        self.server_process.start()
        
        # Wait for server to start
        time.sleep(2)
        
        yield
        
        # Cleanup
        self.server_process.terminate()
        self.server_process.join(timeout=5)
    
    def test_health_endpoint(self):
        """Test /health endpoint"""
        response = requests.get(f"{self.base_url}/health")
        assert response.status_code == 200
        
        data = response.json()
        assert data["status"] == "healthy"
        assert "uptime" in data
    
    def test_metrics_endpoint_post(self, sample_cpu_metrics):
        """Test POST /api/metrics"""
        response = requests.post(
            f"{self.base_url}/api/metrics",
            json=[sample_cpu_metrics]
        )
        assert response.status_code == 200
        
        data = response.json()
        assert data["status"] == "success"
        assert data["received"] == 1
    
    def test_metrics_endpoint_batch(self):
        """Test batch metric ingestion"""
        metrics = []
        for i in range(10):
            metrics.append({
                "timestamp": 1234567890 + i,
                "metric_type": "cpu.total_usage",
                "host": "test-host",
                "value": 40.0 + i,
                "tags": {}
            })
        
        response = requests.post(
            f"{self.base_url}/api/metrics",
            json=metrics
        )
        assert response.status_code == 200
        
        data = response.json()
        assert data["received"] == 10
    
    def test_query_latest(self, sample_cpu_metrics):
        """Test /api/query/latest endpoint"""
        # First insert a metric
        requests.post(
            f"{self.base_url}/api/metrics",
            json=[sample_cpu_metrics]
        )
        
        time.sleep(0.5)  # Allow time for processing
        
        # Query it back
        response = requests.get(
            f"{self.base_url}/api/query/latest",
            params={"metric_type": "cpu.total_usage", "limit": 1}
        )
        assert response.status_code == 200
        
        data = response.json()
        assert len(data) >= 1
        assert data[0]["metric_type"] == "cpu.total_usage"
    
    def test_query_time_range(self, sample_cpu_metrics):
        """Test /api/query/range endpoint"""
        # Insert metric
        requests.post(
            f"{self.base_url}/api/metrics",
            json=[sample_cpu_metrics]
        )
        
        time.sleep(0.5)
        
        # Query time range
        now = int(time.time())
        response = requests.get(
            f"{self.base_url}/api/query/range",
            params={
                "metric_type": "cpu.total_usage",
                "start": now - 3600,
                "end": now + 3600
            }
        )
        assert response.status_code == 200
        
        data = response.json()
        assert isinstance(data, list)
    
    def test_hosts_endpoint(self, sample_cpu_metrics):
        """Test /api/hosts endpoint"""
        # Insert metrics from multiple hosts
        for host in ["host-1", "host-2", "host-3"]:
            metric = sample_cpu_metrics.copy()
            metric["host"] = host
            requests.post(f"{self.base_url}/api/metrics", json=[metric])
        
        time.sleep(0.5)
        
        response = requests.get(f"{self.base_url}/api/hosts")
        assert response.status_code == 200
        
        data = response.json()
        assert len(data) >= 3
    
    def test_metric_types_endpoint(self):
        """Test /api/metric-types endpoint"""
        # Insert different metric types
        metrics = [
            {
                "timestamp": int(time.time()),
                "metric_type": "cpu.total_usage",
                "host": "test",
                "value": 50.0,
                "tags": {}
            },
            {
                "timestamp": int(time.time()),
                "metric_type": "memory.used_bytes",
                "host": "test",
                "value": 8589934592,
                "tags": {}
            }
        ]
        requests.post(f"{self.base_url}/api/metrics", json=metrics)
        
        time.sleep(0.5)
        
        response = requests.get(f"{self.base_url}/api/metric-types")
        assert response.status_code == 200
        
        data = response.json()
        assert len(data) >= 2
    
    def test_invalid_metric(self):
        """Test posting invalid metric data"""
        invalid_metric = {
            "invalid_field": "value"
        }
        
        response = requests.post(
            f"{self.base_url}/api/metrics",
            json=[invalid_metric]
        )
        # Should handle gracefully
        assert response.status_code in [200, 400]
    
    def test_empty_batch(self):
        """Test posting empty batch"""
        response = requests.post(
            f"{self.base_url}/api/metrics",
            json=[]
        )
        assert response.status_code == 200
        
        data = response.json()
        assert data["received"] == 0
    
    def test_concurrent_requests(self, sample_cpu_metrics):
        """Test concurrent API requests"""
        import concurrent.futures
        
        def post_metric():
            return requests.post(
                f"{self.base_url}/api/metrics",
                json=[sample_cpu_metrics]
            )
        
        with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
            futures = [executor.submit(post_metric) for _ in range(20)]
            results = [f.result() for f in futures]
        
        # All should succeed
        assert all(r.status_code == 200 for r in results)


class TestAggregatorStats:
    """Test aggregator statistics endpoints"""
    
    def test_stats_endpoint(self):
        """Test /api/stats endpoint"""
        # This would require running aggregator
        # Placeholder for now
        pass
    
    def test_aggregations(self):
        """Test metric aggregations"""
        # Test average, min, max, sum aggregations
        pass


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
