"""
Simple unit test to verify test infrastructure works
"""

import pytest


def test_simple_pass():
    """Basic passing test"""
    assert True


def test_simple_math():
    """Basic math test"""
    assert 2 + 2 == 4


def test_string_operations():
    """String operations test"""
    result = "sysmonitor".upper()
    assert result == "SYSMONITOR"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
