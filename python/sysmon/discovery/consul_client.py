"""Consul service discovery client for SysMonitor.

Optional integration for enterprise environments with Consul clusters.
Provides service registration, health checks, and discovery.
"""

import logging
import json
import socket
from typing import Optional, List, Dict
from urllib.request import Request, urlopen
from urllib.error import URLError

logger = logging.getLogger(__name__)

DEFAULT_CONSUL_ADDR = "http://localhost:8500"
SERVICE_NAME = "sysmon-aggregator"


class ConsulClient:
    """Consul client for service registration and discovery.
    
    Example:
        # Aggregator side
        client = ConsulClient()
        client.register_aggregator(port=8080, health_check_url="http://localhost:8080/api/health")
        
        # Agent side
        aggregators = client.discover_aggregators()
    """
    
    def __init__(self, consul_addr: str = DEFAULT_CONSUL_ADDR):
        """Initialize Consul client.
        
        Args:
            consul_addr: Consul agent address (e.g., "http://localhost:8500")
        """
        self.consul_addr = consul_addr.rstrip('/')
        
    def register_aggregator(self, port: int, 
                           health_check_url: Optional[str] = None,
                           tags: Optional[List[str]] = None,
                           metadata: Optional[Dict[str, str]] = None) -> bool:
        """Register aggregator service in Consul.
        
        Args:
            port: Aggregator port
            health_check_url: URL for health checks
            tags: Service tags (e.g., ['production', 'us-west'])
            metadata: Service metadata
            
        Returns:
            True if registration successful
        """
        hostname = socket.gethostname()
        service_id = f"{SERVICE_NAME}-{hostname}-{port}"
        
        registration = {
            "ID": service_id,
            "Name": SERVICE_NAME,
            "Port": port,
            "Address": self._get_local_ip(),
            "Tags": tags or ["sysmonitor", "aggregator"],
            "Meta": metadata or {"version": "0.6.0"},
        }
        
        # Add health check if provided
        if health_check_url:
            registration["Check"] = {
                "HTTP": health_check_url,
                "Interval": "10s",
                "Timeout": "5s",
                "DeregisterCriticalServiceAfter": "60s",
            }
            
        try:
            url = f"{self.consul_addr}/v1/agent/service/register"
            req = Request(url, 
                         data=json.dumps(registration).encode('utf-8'),
                         headers={'Content-Type': 'application/json'},
                         method='PUT')
            
            with urlopen(req, timeout=5) as response:
                if response.status in (200, 204):
                    logger.info(f"Registered aggregator in Consul: {service_id}")
                    return True
                else:
                    logger.error(f"Consul registration failed: HTTP {response.status}")
                    return False
                    
        except URLError as e:
            logger.error(f"Failed to register with Consul: {e}")
            return False
            
    def deregister_aggregator(self, port: int) -> bool:
        """Deregister aggregator service from Consul.
        
        Args:
            port: Aggregator port
            
        Returns:
            True if deregistration successful
        """
        hostname = socket.gethostname()
        service_id = f"{SERVICE_NAME}-{hostname}-{port}"
        
        try:
            url = f"{self.consul_addr}/v1/agent/service/deregister/{service_id}"
            req = Request(url, method='PUT')
            
            with urlopen(req, timeout=5) as response:
                if response.status in (200, 204):
                    logger.info(f"Deregistered aggregator from Consul: {service_id}")
                    return True
                else:
                    logger.error(f"Consul deregistration failed: HTTP {response.status}")
                    return False
                    
        except URLError as e:
            logger.error(f"Failed to deregister from Consul: {e}")
            return False
            
    def discover_aggregators(self, tag: Optional[str] = None) -> List[Dict[str, any]]:
        """Discover aggregator services from Consul.
        
        Args:
            tag: Optional tag filter
            
        Returns:
            List of aggregator services
        """
        try:
            url = f"{self.consul_addr}/v1/health/service/{SERVICE_NAME}"
            if tag:
                url += f"?tag={tag}"
            url += "&passing=true"  # Only healthy services
            
            req = Request(url)
            with urlopen(req, timeout=5) as response:
                if response.status == 200:
                    data = json.loads(response.read().decode('utf-8'))
                    
                    services = []
                    for entry in data:
                        service = entry['Service']
                        services.append({
                            'id': service['ID'],
                            'address': service['Address'],
                            'port': service['Port'],
                            'tags': service.get('Tags', []),
                            'meta': service.get('Meta', {}),
                        })
                    
                    logger.info(f"Discovered {len(services)} aggregator(s) from Consul")
                    return services
                else:
                    logger.error(f"Consul discovery failed: HTTP {response.status}")
                    return []
                    
        except URLError as e:
            logger.error(f"Failed to discover from Consul: {e}")
            return []
            
    def discover_first(self, tag: Optional[str] = None) -> Optional[str]:
        """Discover first available aggregator and return URL.
        
        Args:
            tag: Optional tag filter
            
        Returns:
            Aggregator URL or None
        """
        services = self.discover_aggregators(tag)
        if services:
            service = services[0]
            protocol = service['meta'].get('protocol', 'http')
            return f"{protocol}://{service['address']}:{service['port']}"
        return None
        
    def _get_local_ip(self) -> str:
        """Get local IP address."""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except Exception:
            return "127.0.0.1"
