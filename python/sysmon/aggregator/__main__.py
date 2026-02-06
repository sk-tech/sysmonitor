"""Entry point for running aggregator as a module: python -m sysmon.aggregator.server"""

import sys
import argparse

# Write to stderr immediately to debug
sys.stderr.write("=== AGGREGATOR __main__.py LOADED ===\n")
sys.stderr.flush()

from .server import run_aggregator_server


def main():
    import os
    sys.stderr.write(f"DEBUG: Starting aggregator __main__\n")
    sys.stderr.write(f"DEBUG: SYSMON_AGGREGATOR_TOKEN={'SET' if os.environ.get('SYSMON_AGGREGATOR_TOKEN') else 'NOT SET'}\n")
    sys.stderr.write(f"DEBUG: sys.argv = {sys.argv}\n")
    sys.stderr.flush()
    
    print(f"DEBUG: Starting aggregator __main__", flush=True)
    print(f"DEBUG: SYSMON_AGGREGATOR_TOKEN={'SET' if os.environ.get('SYSMON_AGGREGATOR_TOKEN') else 'NOT SET'}", flush=True)
    print(f"DEBUG: sys.argv = {sys.argv}", flush=True)
    
    # Support positional args for backward compatibility: port db_path
    parser = argparse.ArgumentParser(description='SysMonitor Aggregator Server')
    parser.add_argument('port', type=int, nargs='?', default=9000, help='Port to listen on')
    parser.add_argument('db', nargs='?', default='~/.sysmon/aggregator.db', help='Database path')
    parser.add_argument('--host', default='0.0.0.0', help='Host to bind to')
    parser.add_argument('--tls', action='store_true', help='Enable TLS/HTTPS')
    parser.add_argument('--cert', help='TLS certificate path')
    parser.add_argument('--key', help='TLS private key path')
    parser.add_argument('--mdns', action='store_true', help='Enable mDNS advertisement')
    parser.add_argument('--mdns-hostname', help='mDNS hostname')
    
    args = parser.parse_args()
    print(f"DEBUG: Parsed args - port={args.port}, db={args.db}", flush=True)
    
    print(f"DEBUG: Calling run_aggregator_server()", flush=True)
    run_aggregator_server(
        host=args.host,
        port=args.port,
        db_path=args.db,
        tls_enabled=args.tls,
        tls_cert_path=args.cert,
        tls_key_path=args.key,
        enable_mdns=args.mdns,
        mdns_hostname=args.mdns_hostname
    )
    print(f"DEBUG: run_aggregator_server() returned", flush=True)


if __name__ == '__main__':
    main()
