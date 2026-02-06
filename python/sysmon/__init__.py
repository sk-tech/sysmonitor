"""
SysMonitor - Python Storage Module

Provides SQLAlchemy ORM models and query API for historical metrics data.
Uses SQLAlchemy for looser coupling (Week 2 consideration #2).
"""

__version__ = "0.1.0"

from .storage import database, query_api

__all__ = ['database', 'query_api']
