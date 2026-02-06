"""
Tests for storage module
"""

import pytest
import sqlite3
import time
import sys
import os

# Add parent directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from sysmon.storage.metrics_db import MetricsDatabase


class TestMetricsDatabase:
    """Test suite for MetricsDatabase"""
    
    def test_create_database(self, temp_db):
        """Test database creation"""
        db = MetricsDatabase(temp_db)
        assert os.path.exists(temp_db)
    
    def test_insert_metric(self, temp_db, sample_cpu_metrics):
        """Test inserting a single metric"""
        db = MetricsDatabase(temp_db)
        
        result = db.insert_metric(
            timestamp=sample_cpu_metrics["timestamp"],
            metric_type=sample_cpu_metrics["metric_type"],
            host=sample_cpu_metrics["host"],
            value=sample_cpu_metrics["value"],
            tags=sample_cpu_metrics["tags"]
        )
        
        assert result is True
    
    def test_insert_metrics_batch(self, temp_db):
        """Test batch insert"""
        db = MetricsDatabase(temp_db)
        
        metrics = []
        for i in range(100):
            metrics.append({
                "timestamp": 1234567890 + i,
                "metric_type": "cpu.total_usage",
                "host": "test-host",
                "value": 40.0 + i,
                "tags": {}
            })
        
        result = db.insert_metrics_batch(metrics)
        assert result is True
        
        # Verify count
        count = db.get_metric_count("cpu.total_usage")
        assert count == 100
    
    def test_query_latest(self, temp_db, sample_cpu_metrics):
        """Test querying latest metrics"""
        db = MetricsDatabase(temp_db)
        
        # Insert multiple metrics
        for i in range(5):
            metric = sample_cpu_metrics.copy()
            metric["timestamp"] = 1234567890 + i
            metric["value"] = 40.0 + i
            db.insert_metric(**metric)
        
        # Query latest
        results = db.query_latest("cpu.total_usage", limit=3)
        assert len(results) == 3
        assert results[0]["value"] == 44.0  # Latest value
    
    def test_query_time_range(self, temp_db):
        """Test querying time range"""
        db = MetricsDatabase(temp_db)
        
        # Insert metrics with different timestamps
        now = int(time.time())
        for i in range(10):
            db.insert_metric(
                timestamp=now - 600 + i * 60,  # Every minute
                metric_type="cpu.total_usage",
                host="test",
                value=50.0,
                tags={}
            )
        
        # Query last 5 minutes
        results = db.query_time_range(
            "cpu.total_usage",
            start=now - 300,
            end=now
        )
        
        assert len(results) >= 5
    
    def test_query_by_host(self, temp_db):
        """Test querying metrics by host"""
        db = MetricsDatabase(temp_db)
        
        # Insert metrics for different hosts
        for host in ["host-1", "host-2", "host-3"]:
            for i in range(5):
                db.insert_metric(
                    timestamp=1234567890 + i,
                    metric_type="cpu.total_usage",
                    host=host,
                    value=50.0,
                    tags={}
                )
        
        # Query specific host
        results = db.query_latest("cpu.total_usage", host="host-2", limit=10)
        assert len(results) == 5
        assert all(r["host"] == "host-2" for r in results)
    
    def test_get_hosts(self, temp_db):
        """Test getting list of hosts"""
        db = MetricsDatabase(temp_db)
        
        # Insert metrics from different hosts
        for host in ["host-a", "host-b", "host-c"]:
            db.insert_metric(
                timestamp=int(time.time()),
                metric_type="cpu.total_usage",
                host=host,
                value=50.0,
                tags={}
            )
        
        hosts = db.get_hosts()
        assert len(hosts) >= 3
        assert "host-a" in hosts
        assert "host-b" in hosts
        assert "host-c" in hosts
    
    def test_get_metric_types(self, temp_db):
        """Test getting list of metric types"""
        db = MetricsDatabase(temp_db)
        
        # Insert different metric types
        types = ["cpu.total_usage", "memory.used_bytes", "disk.read_bytes"]
        for metric_type in types:
            db.insert_metric(
                timestamp=int(time.time()),
                metric_type=metric_type,
                host="test",
                value=100.0,
                tags={}
            )
        
        result_types = db.get_metric_types()
        assert len(result_types) >= 3
        for t in types:
            assert t in result_types
    
    def test_delete_old_metrics(self, temp_db):
        """Test deleting old metrics"""
        db = MetricsDatabase(temp_db)
        
        # Insert old metrics
        old_time = int(time.time()) - 86400 * 30  # 30 days ago
        for i in range(10):
            db.insert_metric(
                timestamp=old_time + i,
                metric_type="cpu.total_usage",
                host="test",
                value=50.0,
                tags={}
            )
        
        # Delete metrics older than 7 days
        deleted = db.delete_old_metrics(retention_days=7)
        assert deleted == 10
    
    def test_get_database_size(self, temp_db):
        """Test getting database size"""
        db = MetricsDatabase(temp_db)
        
        # Insert some data
        for i in range(100):
            db.insert_metric(
                timestamp=int(time.time()) + i,
                metric_type="cpu.total_usage",
                host="test",
                value=50.0,
                tags={}
            )
        
        size = db.get_database_size()
        assert size > 0
    
    def test_aggregations(self, temp_db):
        """Test metric aggregations"""
        db = MetricsDatabase(temp_db)
        
        # Insert test data
        values = [10.0, 20.0, 30.0, 40.0, 50.0]
        now = int(time.time())
        for i, val in enumerate(values):
            db.insert_metric(
                timestamp=now + i,
                metric_type="test.metric",
                host="test",
                value=val,
                tags={}
            )
        
        # Test average
        avg = db.get_average("test.metric", start=now - 10, end=now + 10)
        assert avg == 30.0
        
        # Test min/max
        min_val = db.get_min("test.metric", start=now - 10, end=now + 10)
        max_val = db.get_max("test.metric", start=now - 10, end=now + 10)
        assert min_val == 10.0
        assert max_val == 50.0
    
    def test_concurrent_access(self, temp_db):
        """Test concurrent database access"""
        import threading
        
        db = MetricsDatabase(temp_db)
        errors = []
        
        def insert_metrics():
            try:
                for i in range(50):
                    db.insert_metric(
                        timestamp=int(time.time()) + i,
                        metric_type="cpu.total_usage",
                        host="test",
                        value=50.0,
                        tags={}
                    )
            except Exception as e:
                errors.append(e)
        
        threads = [threading.Thread(target=insert_metrics) for _ in range(5)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        
        assert len(errors) == 0
        
        # Verify total count
        count = db.get_metric_count("cpu.total_usage")
        assert count == 250


class TestMetricsQuery:
    """Test advanced query capabilities"""
    
    def test_query_with_tags(self, temp_db):
        """Test querying with tag filters"""
        db = MetricsDatabase(temp_db)
        
        # Insert metrics with tags
        db.insert_metric(
            timestamp=int(time.time()),
            metric_type="cpu.usage",
            host="test",
            value=50.0,
            tags={"core": "0", "type": "user"}
        )
        
        # Query with tag filter would be implemented
        pass
    
    def test_downsampling(self, temp_db):
        """Test metric downsampling"""
        # Test ability to downsample high-frequency metrics
        pass


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
