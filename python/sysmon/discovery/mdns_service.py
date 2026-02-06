"""mDNS/Bonjour service discovery for SysMonitor aggregator.

Uses zeroconf library for cross-platform mDNS support.
Service name: _sysmon-aggregator._tcp.local.
"""

import logging
import socket
import time
from typing import Optional, List, Dict
from zeroconf import Zeroconf, ServiceInfo, ServiceBrowser, ServiceListener

logger = logging.getLogger(__name__)

SERVICE_TYPE = "_sysmon-aggregator._tcp.local."
DEFAULT_TIMEOUT = 5.0  # seconds


class MDNSService:
    """Advertise SysMonitor aggregator via mDNS.
    
    Example:
        service = MDNSService(port=8080, hostname="aggregator-01")
        service.start()
        # ... run aggregator ...
        service.stop()
    """
    
    def __init__(self, port: int, hostname: Optional[str] = None, 
                 metadata: Optional[Dict[str, str]] = None):
        """Initialize mDNS service advertisement.
        
        Args:
            port: Aggregator HTTP(S) port
            hostname: Service hostname (default: machine hostname)
            metadata: Additional service metadata (version, region, etc.)
        """
        self.port = port
        self.hostname = hostname or socket.gethostname()
        self.metadata = metadata or {}
        
        self.zeroconf: Optional[Zeroconf] = None
        self.service_info: Optional[ServiceInfo] = None
        
    def start(self):
        """Start advertising the aggregator service."""
        try:
            self.zeroconf = Zeroconf()
            
            # Build service properties
            properties = {
                'version': self.metadata.get('version', '0.6.0'),
                'protocol': self.metadata.get('protocol', 'http'),
                'region': self.metadata.get('region', 'default'),
            }
            
            # Register service
            service_name = f"{self.hostname}.{SERVICE_TYPE}"
            self.service_info = ServiceInfo(
                SERVICE_TYPE,
                service_name,
                addresses=[socket.inet_aton(self._get_local_ip())],
                port=self.port,
                properties=properties,
                server=f"{self.hostname}.local.",
            )
            
            self.zeroconf.register_service(self.service_info)
            logger.info(f"mDNS service registered: {service_name} on port {self.port}")
            
        except Exception as e:
            logger.error(f"Failed to start mDNS service: {e}")
            raise
            
    def stop(self):
        """Stop advertising the service."""
        if self.zeroconf and self.service_info:
            try:
                self.zeroconf.unregister_service(self.service_info)
                self.zeroconf.close()
                logger.info("mDNS service unregistered")
            except Exception as e:
                logger.error(f"Error stopping mDNS service: {e}")
                
    def _get_local_ip(self) -> str:
        """Get local IP address for service advertisement."""
        try:
            # Connect to external address to get local IP
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except Exception:
            return "127.0.0.1"
            
    def __enter__(self):
        self.start()
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()


class AggregatorListener(ServiceListener):
    """Listener for mDNS aggregator services."""
    
    def __init__(self):
        self.services: List[Dict[str, any]] = []
        self.discovered_event = None
        
    def add_service(self, zc: Zeroconf, type_: str, name: str):
        """Called when a service is discovered."""
        info = zc.get_service_info(type_, name)
        if info:
            service_data = {
                'name': name,
                'addresses': [socket.inet_ntoa(addr) for addr in info.addresses],
                'port': info.port,
                'properties': {k.decode(): v.decode() for k, v in info.properties.items()},
                'server': info.server,
            }
            self.services.append(service_data)
            logger.info(f"Discovered aggregator: {service_data['addresses'][0]}:{service_data['port']}")
            
    def remove_service(self, zc: Zeroconf, type_: str, name: str):
        """Called when a service is removed."""
        self.services = [s for s in self.services if s['name'] != name]
        logger.info(f"Aggregator removed: {name}")
        
    def update_service(self, zc: Zeroconf, type_: str, name: str):
        """Called when a service is updated."""
        # Re-add with updated info
        self.remove_service(zc, type_, name)
        self.add_service(zc, type_, name)


class MDNSDiscovery:
    """Discover SysMonitor aggregators via mDNS.
    
    Example:
        discovery = MDNSDiscovery()
        aggregators = discovery.discover(timeout=5.0)
        if aggregators:
            url = f"http://{aggregators[0]['addresses'][0]}:{aggregators[0]['port']}"
    """
    
    def __init__(self):
        self.zeroconf: Optional[Zeroconf] = None
        self.browser: Optional[ServiceBrowser] = None
        
    def discover(self, timeout: float = DEFAULT_TIMEOUT) -> List[Dict[str, any]]:
        """Discover aggregators on the local network.
        
        Args:
            timeout: Discovery timeout in seconds
            
        Returns:
            List of discovered aggregator services
        """
        listener = AggregatorListener()
        
        try:
            self.zeroconf = Zeroconf()
            self.browser = ServiceBrowser(self.zeroconf, SERVICE_TYPE, listener)
            
            # Wait for discovery
            logger.info(f"Discovering aggregators for {timeout}s...")
            time.sleep(timeout)
            
            self.browser.cancel()
            self.zeroconf.close()
            
            logger.info(f"Discovery complete: found {len(listener.services)} aggregator(s)")
            return listener.services
            
        except Exception as e:
            logger.error(f"Discovery error: {e}")
            return []
            
    def discover_first(self, timeout: float = DEFAULT_TIMEOUT) -> Optional[str]:
        """Discover first available aggregator and return URL.
        
        Args:
            timeout: Discovery timeout in seconds
            
        Returns:
            Aggregator URL (e.g., "http://192.168.1.100:8080") or None
        """
        services = self.discover(timeout)
        if services:
            service = services[0]
            protocol = service['properties'].get('protocol', 'http')
            address = service['addresses'][0]
            port = service['port']
            return f"{protocol}://{address}:{port}"
        return None
