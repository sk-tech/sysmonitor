"""Service discovery module for SysMonitor."""

from .mdns_service import MDNSService, MDNSDiscovery
from .consul_client import ConsulClient

__all__ = ['MDNSService', 'MDNSDiscovery', 'ConsulClient']
