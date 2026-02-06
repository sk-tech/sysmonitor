"""HTTP server for multi-host metrics aggregation"""

import json
import time
import socket
import ssl
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
from typing import Optional

from .storage import AggregatorStorage
from .auth import TokenAuthenticator


class AggregatorHandler(BaseHTTPRequestHandler):
    """HTTP request handler for aggregator server"""
    
    storage: AggregatorStorage = None
    authenticator: TokenAuthenticator = None
    
    def _send_json_response(self, status: int, data: dict):
        """Send JSON response"""
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(json.dumps(data, indent=2).encode())
    
    def _send_error_response(self, status: int, message: str):
        """Send error response"""
        self._send_json_response(status, {
            'error': message,
            'timestamp': int(time.time())
        })
    
    def _authenticate(self) -> bool:
        """Authenticate request using token"""
        token = self.authenticator.extract_from_header(self.headers)
        return self.authenticator.validate(token)
    
    def do_OPTIONS(self):
        """Handle CORS preflight"""
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type, X-SysMon-Token, Authorization')
        self.end_headers()
    
    def do_GET(self):
        """Handle GET requests"""
        parsed = urlparse(self.path)
        path = parsed.path
        params = parse_qs(parsed.query)
        
        # Health check (no auth required)
        if path == '/health' or path == '/api/health':
            self._send_json_response(200, {
                'status': 'healthy',
                'timestamp': int(time.time()),
                'version': '0.5.0'
            })
            return
        
        # All other endpoints require authentication
        if not self._authenticate():
            self._send_error_response(401, 'Unauthorized: Invalid or missing token')
            return
        
        # GET /api/hosts - List all registered hosts
        if path == '/api/hosts':
            include_inactive = params.get('include_inactive', ['false'])[0].lower() == 'true'
            hosts = self.storage.get_hosts(include_inactive=include_inactive)
            self._send_json_response(200, {
                'hosts': hosts,
                'count': len(hosts),
                'timestamp': int(time.time())
            })
            return
        
        # GET /api/metrics?host=X&metric_type=Y&start=T1&end=T2&limit=N
        if path == '/api/metrics':
            hostname = params.get('host', [None])[0]
            metric_type = params.get('metric_type', [None])[0]
            start_time = params.get('start', [None])[0]
            end_time = params.get('end', [None])[0]
            limit = int(params.get('limit', [1000])[0])
            
            if not hostname:
                self._send_error_response(400, 'Missing required parameter: host')
                return
            
            # Convert timestamps
            start_ts = int(start_time) if start_time else None
            end_ts = int(end_time) if end_time else None
            
            metrics = self.storage.get_host_metrics(
                hostname=hostname,
                metric_type=metric_type,
                start_time=start_ts,
                end_time=end_ts,
                limit=limit
            )
            
            self._send_json_response(200, {
                'host': hostname,
                'metrics': metrics,
                'count': len(metrics),
                'timestamp': int(time.time())
            })
            return
        
        # GET /api/latest?host=X - Latest metrics
        if path == '/api/latest':
            hostname = params.get('host', [None])[0]
            metrics = self.storage.get_latest_metrics(hostname=hostname)
            
            self._send_json_response(200, {
                'metrics': metrics,
                'count': len(metrics),
                'timestamp': int(time.time())
            })
            return
        
        # GET /api/fleet/summary - Fleet-wide statistics
        if path == '/api/fleet/summary':
            summary = self.storage.get_fleet_summary()
            self._send_json_response(200, summary)
            return
        
        # GET / or /dashboard - Serve multi-host dashboard
        if path == '/' or path == '/dashboard':
            self._serve_dashboard()
            return
        
        # Unknown endpoint
        self._send_error_response(404, f'Endpoint not found: {path}')
    
    def do_POST(self):
        """Handle POST requests"""
        parsed = urlparse(self.path)
        path = parsed.path
        
        # All POST endpoints require authentication
        if not self._authenticate():
            self._send_error_response(401, 'Unauthorized: Invalid or missing token')
            return
        
        # POST /api/metrics - Receive metrics from agent
        if path == '/api/metrics':
            try:
                print(f"DEBUG: Received POST to /api/metrics", flush=True)
                # Read request body
                content_length = int(self.headers.get('Content-Length', 0))
                print(f"DEBUG: Content length: {content_length}", flush=True)
                body = self.rfile.read(content_length)
                print(f"DEBUG: Read body", flush=True)
                data = json.loads(body.decode('utf-8'))
                print(f"DEBUG: Parsed JSON: hostname={data.get('hostname')}", flush=True)
                
                # Extract hostname and metrics
                hostname = data.get('hostname')
                if not hostname:
                    self._send_error_response(400, 'Missing required field: hostname')
                    return
                
                print(f"DEBUG: Registering host {hostname}", flush=True)
                # Register/update host
                self.storage.register_host(
                    hostname=hostname,
                    version=data.get('version'),
                    platform=data.get('platform'),
                    tags=data.get('tags', {})
                )
                print(f"DEBUG: Host registered", flush=True)
                
                # Write metrics
                metrics = data.get('metrics', [])
                if not metrics:
                    self._send_error_response(400, 'Missing required field: metrics')
                    return
                
                print(f"DEBUG: Writing {len(metrics)} metrics", flush=True)
                success, failed = self.storage.write_metrics(hostname, metrics)
                print(f"DEBUG: Metrics written: success={success}, failed={failed}", flush=True)
                
                print(f"DEBUG: Sending response", flush=True)
                self._send_json_response(200, {
                    'status': 'success',
                    'hostname': hostname,
                    'metrics_received': len(metrics),
                    'metrics_stored': success,
                    'metrics_failed': failed,
                    'timestamp': int(time.time())
                })
                
            except json.JSONDecodeError as e:
                self._send_error_response(400, f'Invalid JSON: {str(e)}')
            except Exception as e:
                self._send_error_response(500, f'Internal error: {str(e)}')
            return
        
        # POST /api/register - Register a new host
        if path == '/api/register':
            try:
                content_length = int(self.headers.get('Content-Length', 0))
                body = self.rfile.read(content_length)
                data = json.loads(body.decode('utf-8'))
                
                hostname = data.get('hostname')
                if not hostname:
                    self._send_error_response(400, 'Missing required field: hostname')
                    return
                
                success = self.storage.register_host(
                    hostname=hostname,
                    version=data.get('version'),
                    platform=data.get('platform'),
                    tags=data.get('tags', {})
                )
                
                if success:
                    self._send_json_response(200, {
                        'status': 'registered',
                        'hostname': hostname,
                        'timestamp': int(time.time())
                    })
                else:
                    self._send_error_response(500, 'Failed to register host')
                    
            except json.JSONDecodeError as e:
                self._send_error_response(400, f'Invalid JSON: {str(e)}')
            except Exception as e:
                self._send_error_response(500, f'Internal error: {str(e)}')
            return
        
        # Unknown endpoint
        self._send_error_response(404, f'Endpoint not found: {path}')
    
    def log_message(self, format, *args):
        """Override to customize logging"""
        print(f"{self.address_string()} - [{self.log_date_time_string()}] {format % args}")
    
    def _serve_dashboard(self):
        """Serve the multi-host dashboard HTML"""
        import os
        dashboard_path = os.path.join(
            os.path.dirname(__file__),
            '..',
            'api',
            'dashboard-multi.html'
        )
        
        try:
            with open(dashboard_path, 'r') as f:
                html_content = f.read()
            
            self.send_response(200)
            self.send_header('Content-Type', 'text/html')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(html_content.encode())
        except FileNotFoundError:
            self._send_error_response(404, 'Dashboard file not found')
        except Exception as e:
            self._send_error_response(500, f'Error serving dashboard: {str(e)}')


def run_aggregator_server(host: str = '0.0.0.0', port: int = 9000, 
                          db_path: str = '~/.sysmon/aggregator.db',
                          token: str = None,
                          tls_enabled: bool = False,
                          tls_cert_path: str = None,
                          tls_key_path: str = None,
                          enable_mdns: bool = False,
                          mdns_hostname: str = None):
    """
    Run the aggregator HTTP server
    
    Args:
        host: Host to bind to
        port: Port to listen on
        db_path: Path to aggregator database
        token: Authentication token (if None, read from environment)
        tls_enabled: Enable HTTPS with TLS
        tls_cert_path: Path to TLS certificate file
        tls_key_path: Path to TLS private key file
        enable_mdns: Enable mDNS/Bonjour service advertisement
        mdns_hostname: mDNS hostname (default: system hostname)
    """
    # Initialize storage
    storage = AggregatorStorage(db_path)
    
    # Initialize authenticator
    try:
        authenticator = TokenAuthenticator(token)
    except ValueError as e:
        print(f"Error: {e}")
        return
    
    # Set up handler class variables
    AggregatorHandler.storage = storage
    AggregatorHandler.authenticator = authenticator
    
    # Create server
    server = HTTPServer((host, port), AggregatorHandler)
    
    # Configure TLS if enabled
    protocol = "http"
    if tls_enabled:
        if not tls_cert_path or not tls_key_path:
            print("Error: TLS enabled but certificate/key paths not provided")
            return
        
        try:
            import os
            tls_cert_path = os.path.expanduser(tls_cert_path)
            tls_key_path = os.path.expanduser(tls_key_path)
            
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(tls_cert_path, tls_key_path)
            server.socket = context.wrap_socket(server.socket, server_side=True)
            protocol = "https"
            print(f"✓ TLS enabled (cert: {tls_cert_path})")
        except Exception as e:
            print(f"Error enabling TLS: {e}")
            return
    
    # Start mDNS advertisement if enabled
    mdns_service = None
    if enable_mdns:
        try:
            from sysmon.discovery.mdns_service import MDNSService
            mdns_service = MDNSService(
                port=port,
                hostname=mdns_hostname,
                metadata={
                    'version': '0.6.0',
                    'protocol': protocol,
                }
            )
            mdns_service.start()
            print(f"✓ mDNS service advertised as {mdns_service.hostname}")
        except ImportError:
            print("Warning: zeroconf library not installed, mDNS disabled")
            print("Install: pip install zeroconf")
        except Exception as e:
            print(f"Warning: Failed to start mDNS: {e}")
    
    print(f"\nSysMonitor Aggregator Server")
    print(f"============================")
    print(f"Listening on {protocol}://{host}:{port}")
    print(f"Database: {storage.db_path}")
    print(f"Auth token: {'*' * len(authenticator.token)}")
    print(f"\nEndpoints:")
    print(f"  GET  /api/health          - Health check (no auth)")
    print(f"  GET  /api/hosts           - List registered hosts")
    print(f"  GET  /api/metrics?host=X  - Get host metrics")
    print(f"  GET  /api/latest?host=X   - Get latest metrics")
    print(f"  POST /api/metrics         - Receive metrics from agent")
    print(f"  POST /api/register        - Register new host")
    print(f"\nPress Ctrl+C to stop...")
    
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        if mdns_service:
            mdns_service.stop()
        server.shutdown()


# Note: __main__ handling moved to __main__.py
# This file can still be imported as a library

