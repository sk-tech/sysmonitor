#!/usr/bin/env python3
"""
Test script for Python storage module

Demonstrates querying metrics from SQLite database using SQLAlchemy.
"""

import sys
import os
from datetime import datetime, timedelta

# Add python directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from sysmon.storage import query_api

def main():
    # Database path
    db_path = os.path.expanduser('~/.sysmon/data.db')
    
    if not os.path.exists(db_path):
        print(f"Database not found: {db_path}")
        print("Run 'sysmond' first to collect data")
        return 1
    
    print("SysMonitor Python Storage API Test")
    print("=" * 60)
    print(f"Database: {db_path}\n")
    
    # Get available metric types
    print("Available Metrics:")
    try:
        metric_types = query_api.get_metric_types(db_path)
        for metric_type in sorted(metric_types)[:10]:  # Show first 10
            print(f"  - {metric_type}")
        if len(metric_types) > 10:
            print(f"  ... and {len(metric_types) - 10} more")
        print()
    except Exception as e:
        print(f"Error getting metric types: {e}\n")
    
    # Get hosts
    print("Hosts:")
    try:
        hosts = query_api.get_hosts(db_path)
        for host in hosts:
            print(f"  - {host}")
        print()
    except Exception as e:
        print(f"Error getting hosts: {e}\n")
    
    # Query CPU usage for last hour
    print("CPU Usage (Last Hour):")
    print("-" * 60)
    try:
        end = datetime.now()
        start = end - timedelta(hours=1)
        
        df = query_api.query_range(
            db_path=db_path,
            metric_type='cpu.total_usage',
            start=start,
            end=end,
            limit=10
        )
        
        if not df.empty:
            print(df[['datetime', 'value']].to_string(index=False))
            print(f"\nStatistics:")
            print(f"  Count: {len(df)}")
            print(f"  Mean:  {df['value'].mean():.2f}%")
            print(f"  Min:   {df['value'].min():.2f}%")
            print(f"  Max:   {df['value'].max():.2f}%")
        else:
            print("No data found")
        print()
    except Exception as e:
        print(f"Error querying CPU: {e}\n")
    
    # Query latest memory usage
    print("Latest Memory Usage:")
    print("-" * 60)
    try:
        latest = query_api.query_latest(
            db_path=db_path,
            metric_type='memory.usage_percent'
        )
        
        if latest:
            ts = datetime.fromtimestamp(latest['timestamp'])
            print(f"  Timestamp: {ts}")
            print(f"  Value:     {latest['value']:.2f}%")
        else:
            print("No data found")
        print()
    except Exception as e:
        print(f"Error querying memory: {e}\n")
    
    # Aggregate metrics
    print("CPU Usage (5-minute averages):")
    print("-" * 60)
    try:
        end = datetime.now()
        start = end - timedelta(hours=1)
        
        df = query_api.aggregate_metrics(
            db_path=db_path,
            metric_type='cpu.total_usage',
            start=start,
            end=end,
            agg_func='avg',
            group_by_minutes=5
        )
        
        if not df.empty:
            print(df[['datetime', 'value']].to_string(index=False))
        else:
            print("No data found")
        print()
    except Exception as e:
        print(f"Error aggregating: {e}\n")
    
    print("=" * 60)
    print("Python storage API test complete!")
    return 0

if __name__ == '__main__':
    sys.exit(main())
