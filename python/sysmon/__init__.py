"""
SysMonitor - Python Module

Provides storage, ML, and API capabilities for system monitoring.
- storage: SQLAlchemy ORM for metrics (optional)
- ml: Anomaly detection and baseline learning (Week 7)
- api: REST API server
"""

__version__ = "0.7.0"

# Lazy imports to avoid hard dependencies
__all__ = ['database', 'query_api']

def __getattr__(name):
    """Lazy import to avoid requiring all dependencies"""
    if name == 'database':
        from .storage import database
        return database
    elif name == 'query_api':
        from .storage import query_api
        return query_api
    raise AttributeError(f"module '{__name__}' has no attribute '{name}'")
