"""
Pytest configuration and fixtures
"""

import pytest
import tempfile
import shutil
import os
from pathlib import Path

@pytest.fixture
def temp_db():
    """Create a temporary database for testing"""
    temp_dir = tempfile.mkdtemp()
    db_path = os.path.join(temp_dir, "test.db")
    yield db_path
    shutil.rmtree(temp_dir)

@pytest.fixture
def temp_config():
    """Create a temporary config file"""
    temp_dir = tempfile.mkdtemp()
    config_path = os.path.join(temp_dir, "config.yaml")
    yield config_path
    shutil.rmtree(temp_dir)

@pytest.fixture
def sample_cpu_metrics():
    """Sample CPU metrics for testing"""
    return {
        "timestamp": 1234567890,
        "metric_type": "cpu.total_usage",
        "host": "test-host",
        "value": 45.5,
        "tags": {}
    }

@pytest.fixture
def sample_memory_metrics():
    """Sample memory metrics for testing"""
    return {
        "timestamp": 1234567890,
        "metric_type": "memory.used_bytes",
        "host": "test-host",
        "value": 8589934592,  # 8GB
        "tags": {}
    }

@pytest.fixture
def aggregator_port():
    """Get available port for aggregator testing"""
    return 19999  # Use high port for testing
