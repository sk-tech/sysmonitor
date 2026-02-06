#!/usr/bin/env python3
"""Wrapper to debug aggregator startup issues"""

import sys
import os
import traceback

# Set environment
os.environ['PYTHONPATH'] = '/home/shamkota/sysmonitor/python'
sys.path.insert(0, '/home/shamkota/sysmonitor/python')
os.environ['SYSMON_AGGREGATOR_TOKEN'] = 'test123'

# Log to file immediately
log_file = '/tmp/wrapper_debug.log'
with open(log_file, 'w') as f:
    f.write("=== Wrapper started ===\n")
    f.write(f"Python version: {sys.version}\n")
    f.write(f"sys.path: {sys.path}\n")
    f.write(f"PYTHONPATH: {os.environ.get('PYTHONPATH')}\n")
    f.write(f"Token: {os.environ.get('SYSMON_AGGREGATOR_TOKEN')}\n")
    f.flush()
    
    try:
        f.write("Attempting import...\n")
        f.flush()
        from sysmon.aggregator.server import run_aggregator_server
        f.write("Import successful!\n")
        f.flush()
        
        f.write("Starting server...\n")
        f.flush()
        run_aggregator_server(host='0.0.0.0', port=9000, db_path='/tmp/test.db')
    except Exception as e:
        f.write(f"ERROR: {e}\n")
        f.write(f"Traceback:\n{traceback.format_exc()}\n")
        f.flush()
        raise
