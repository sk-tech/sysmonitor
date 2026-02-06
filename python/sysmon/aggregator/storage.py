"""Multi-host storage backend for aggregator"""

import sqlite3
import json
import time
import os
from typing import List, Dict, Optional, Tuple
from contextlib import contextmanager


class AggregatorStorage:
    """SQLite storage for multi-host metrics"""

    def __init__(self, db_path: str):
        """
        Initialize aggregator storage
        
        Args:
            db_path: Path to SQLite database
        """
        self.db_path = os.path.expanduser(db_path)
        
        # Create directory if needed
        os.makedirs(os.path.dirname(self.db_path), exist_ok=True)
        
        # Initialize schema
        self._init_schema()
    
    @contextmanager
    def _get_connection(self):
        """Context manager for database connections"""
        conn = sqlite3.connect(self.db_path, timeout=10.0)
        conn.row_factory = sqlite3.Row
        try:
            yield conn
            conn.commit()
        except Exception:
            conn.rollback()
            raise
        finally:
            conn.close()
    
    def _init_schema(self):
        """Initialize database schema"""
        with self._get_connection() as conn:
            cursor = conn.cursor()
            
            # Hosts table
            cursor.execute('''
                CREATE TABLE IF NOT EXISTS hosts (
                    hostname TEXT PRIMARY KEY,
                    first_seen INTEGER NOT NULL,
                    last_seen INTEGER NOT NULL,
                    tags TEXT DEFAULT '{}',
                    version TEXT,
                    platform TEXT,
                    status TEXT DEFAULT 'active'
                )
            ''')
            
            # Metrics table (same schema as single-host, but with host column indexed)
            cursor.execute('''
                CREATE TABLE IF NOT EXISTS metrics (
                    timestamp INTEGER NOT NULL,
                    metric_type TEXT NOT NULL,
                    host TEXT NOT NULL,
                    tags TEXT DEFAULT '',
                    value REAL NOT NULL,
                    PRIMARY KEY (timestamp, metric_type, host, tags)
                ) WITHOUT ROWID
            ''')
            
            # Indexes for efficient queries
            cursor.execute('CREATE INDEX IF NOT EXISTS idx_host_time ON metrics(host, timestamp DESC)')
            cursor.execute('CREATE INDEX IF NOT EXISTS idx_metric_host ON metrics(metric_type, host, timestamp DESC)')
            cursor.execute('CREATE INDEX IF NOT EXISTS idx_timestamp ON metrics(timestamp DESC)')
            
            # Enable WAL mode for concurrent reads
            cursor.execute('PRAGMA journal_mode=WAL')
            cursor.execute('PRAGMA synchronous=NORMAL')
    
    def register_host(self, hostname: str, version: str = None, platform: str = None, 
                      tags: Dict[str, str] = None) -> bool:
        """
        Register a new host or update existing host
        
        Args:
            hostname: Hostname of the agent
            version: SysMonitor version
            platform: Platform (Linux/Windows/macOS)
            tags: Host tags (environment, datacenter, etc.)
            
        Returns:
            True if successful
        """
        now = int(time.time())
        tags_json = json.dumps(tags or {})
        
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                
                # Check if host exists
                cursor.execute('SELECT hostname FROM hosts WHERE hostname = ?', (hostname,))
                exists = cursor.fetchone() is not None
                
                if exists:
                    cursor.execute('''
                        UPDATE hosts 
                        SET last_seen = ?, tags = ?, version = ?, platform = ?, status = 'active'
                        WHERE hostname = ?
                    ''', (now, tags_json, version, platform, hostname))
                else:
                    cursor.execute('''
                        INSERT INTO hosts (hostname, first_seen, last_seen, tags, version, platform)
                        VALUES (?, ?, ?, ?, ?, ?)
                    ''', (hostname, now, now, tags_json, version, platform))
                
                return True
        except Exception as e:
            print(f"Error registering host {hostname}: {e}")
            return False
    
    def update_heartbeat(self, hostname: str) -> bool:
        """
        Update host's last_seen timestamp
        
        Args:
            hostname: Hostname to update
            
        Returns:
            True if successful
        """
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute(
                    'UPDATE hosts SET last_seen = ?, status = "active" WHERE hostname = ?',
                    (int(time.time()), hostname)
                )
                return cursor.rowcount > 0
        except Exception as e:
            print(f"Error updating heartbeat for {hostname}: {e}")
            return False
    
    def get_hosts(self, include_inactive: bool = False) -> List[Dict]:
        """
        Get list of all registered hosts
        
        Args:
            include_inactive: Include hosts not seen in 5 minutes
            
        Returns:
            List of host information dictionaries
        """
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                
                if include_inactive:
                    cursor.execute('SELECT * FROM hosts ORDER BY last_seen DESC')
                else:
                    # Only show hosts seen in last 5 minutes
                    cutoff = int(time.time()) - 300
                    cursor.execute(
                        'SELECT * FROM hosts WHERE last_seen > ? ORDER BY last_seen DESC',
                        (cutoff,)
                    )
                
                hosts = []
                for row in cursor.fetchall():
                    hosts.append({
                        'hostname': row['hostname'],
                        'first_seen': row['first_seen'],
                        'last_seen': row['last_seen'],
                        'tags': json.loads(row['tags']) if row['tags'] else {},
                        'version': row['version'],
                        'platform': row['platform'],
                        'status': row['status']
                    })
                
                return hosts
        except Exception as e:
            print(f"Error getting hosts: {e}")
            return []
    
    def write_metrics(self, hostname: str, metrics: List[Dict]) -> Tuple[int, int]:
        """
        Write batch of metrics from a host
        
        Args:
            hostname: Source hostname
            metrics: List of metric dictionaries with keys: timestamp, metric_type, value, tags
            
        Returns:
            Tuple of (successful_writes, failed_writes)
        """
        success = 0
        failed = 0
        
        print(f"DEBUG write_metrics: Starting write for {hostname}, {len(metrics)} metrics", flush=True)
        
        try:
            print(f"DEBUG write_metrics: Getting connection", flush=True)
            with self._get_connection() as conn:
                print(f"DEBUG write_metrics: Got connection", flush=True)
                cursor = conn.cursor()
                
                for i, metric in enumerate(metrics):
                    try:
                        print(f"DEBUG write_metrics: Writing metric {i+1}/{len(metrics)}", flush=True)
                        cursor.execute('''
                            INSERT OR REPLACE INTO metrics (timestamp, metric_type, host, tags, value)
                            VALUES (?, ?, ?, ?, ?)
                        ''', (
                            metric['timestamp'],
                            metric['metric_type'],
                            hostname,
                            metric.get('tags', ''),
                            metric['value']
                        ))
                        success += 1
                        print(f"DEBUG write_metrics: Metric {i+1} written successfully", flush=True)
                    except Exception as e:
                        print(f"Error writing metric: {e}", flush=True)
                        failed += 1
                
                print(f"DEBUG write_metrics: Updating heartbeat inline", flush=True)
                # Update heartbeat in same transaction to avoid nested connection
                cursor.execute(
                    'UPDATE hosts SET last_seen = ?, status = "active" WHERE hostname = ?',
                    (int(time.time()), hostname)
                )
                print(f"DEBUG write_metrics: Heartbeat updated inline", flush=True)
                
        except Exception as e:
            print(f"Error in batch write for {hostname}: {e}", flush=True)
            failed = len(metrics) - success
        
        print(f"DEBUG write_metrics: Returning success={success}, failed={failed}", flush=True)
        return (success, failed)
    
    def get_host_metrics(self, hostname: str, metric_type: str = None, 
                         start_time: int = None, end_time: int = None, 
                         limit: int = 1000) -> List[Dict]:
        """
        Get metrics for a specific host
        
        Args:
            hostname: Hostname to query
            metric_type: Specific metric type (None for all)
            start_time: Start timestamp (None for no limit)
            end_time: End timestamp (None for no limit)
            limit: Maximum number of results
            
        Returns:
            List of metric dictionaries
        """
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                
                query = 'SELECT * FROM metrics WHERE host = ?'
                params = [hostname]
                
                if metric_type:
                    query += ' AND metric_type = ?'
                    params.append(metric_type)
                
                if start_time:
                    query += ' AND timestamp >= ?'
                    params.append(start_time)
                
                if end_time:
                    query += ' AND timestamp <= ?'
                    params.append(end_time)
                
                query += ' ORDER BY timestamp DESC LIMIT ?'
                params.append(limit)
                
                cursor.execute(query, params)
                
                metrics = []
                for row in cursor.fetchall():
                    metrics.append({
                        'timestamp': row['timestamp'],
                        'metric_type': row['metric_type'],
                        'host': row['host'],
                        'tags': row['tags'],
                        'value': row['value']
                    })
                
                return metrics
        except Exception as e:
            print(f"Error getting metrics for {hostname}: {e}")
            return []
    
    def get_latest_metrics(self, hostname: str = None) -> List[Dict]:
        """
        Get latest metrics for all hosts or specific host
        
        Args:
            hostname: Specific hostname (None for all hosts)
            
        Returns:
            List of latest metrics per metric_type per host
        """
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                
                if hostname:
                    # Latest metrics for specific host
                    query = '''
                        SELECT m.* FROM metrics m
                        INNER JOIN (
                            SELECT metric_type, MAX(timestamp) as max_ts
                            FROM metrics
                            WHERE host = ?
                            GROUP BY metric_type
                        ) latest ON m.metric_type = latest.metric_type 
                                 AND m.timestamp = latest.max_ts
                                 AND m.host = ?
                    '''
                    cursor.execute(query, (hostname, hostname))
                else:
                    # Latest metrics for all hosts
                    query = '''
                        SELECT m.* FROM metrics m
                        INNER JOIN (
                            SELECT host, metric_type, MAX(timestamp) as max_ts
                            FROM metrics
                            GROUP BY host, metric_type
                        ) latest ON m.host = latest.host
                                 AND m.metric_type = latest.metric_type 
                                 AND m.timestamp = latest.max_ts
                        ORDER BY m.host, m.metric_type
                    '''
                    cursor.execute(query)
                
                metrics = []
                for row in cursor.fetchall():
                    metrics.append({
                        'timestamp': row['timestamp'],
                        'metric_type': row['metric_type'],
                        'host': row['host'],
                        'tags': row['tags'],
                        'value': row['value']
                    })
                
                return metrics
        except Exception as e:
            print(f"Error getting latest metrics: {e}")
            return []
    
    def mark_host_inactive(self, hostname: str):
        """Mark a host as inactive"""
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute(
                    'UPDATE hosts SET status = "inactive" WHERE hostname = ?',
                    (hostname,)
                )
        except Exception as e:
            print(f"Error marking host {hostname} inactive: {e}")
    
    def cleanup_old_metrics(self, days: int = 30) -> int:
        """
        Delete metrics older than specified days
        
        Args:
            days: Retention period in days
            
        Returns:
            Number of deleted rows
        """
        cutoff = int(time.time()) - (days * 86400)
        
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('DELETE FROM metrics WHERE timestamp < ?', (cutoff,))
                return cursor.rowcount
        except Exception as e:
            print(f"Error cleaning up old metrics: {e}")
            return 0
    
    def get_fleet_summary(self) -> Dict:
        """
        Get aggregated fleet summary statistics
        
        Returns:
            Dictionary with fleet-wide statistics
        """
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                
                # Total hosts
                cursor.execute('SELECT COUNT(*) FROM hosts')
                total_hosts = cursor.fetchone()[0]
                
                # Online hosts (seen in last 5 minutes)
                cutoff = int(time.time()) - 300
                cursor.execute('SELECT COUNT(*) FROM hosts WHERE last_seen > ?', (cutoff,))
                online_hosts = cursor.fetchone()[0]
                
                offline_hosts = total_hosts - online_hosts
                
                # Average CPU usage across all online hosts
                cursor.execute('''
                    SELECT AVG(value) FROM (
                        SELECT m.value FROM metrics m
                        INNER JOIN hosts h ON m.host = h.hostname
                        INNER JOIN (
                            SELECT host, MAX(timestamp) as max_ts
                            FROM metrics
                            WHERE metric_type = 'cpu.total_usage'
                            GROUP BY host
                        ) latest ON m.host = latest.host AND m.timestamp = latest.max_ts
                        WHERE h.last_seen > ? AND m.metric_type = 'cpu.total_usage'
                    )
                ''', (cutoff,))
                avg_cpu = cursor.fetchone()[0] or 0
                
                # Total memory used across all online hosts
                cursor.execute('''
                    SELECT SUM(value) FROM (
                        SELECT m.value FROM metrics m
                        INNER JOIN hosts h ON m.host = h.hostname
                        INNER JOIN (
                            SELECT host, MAX(timestamp) as max_ts
                            FROM metrics
                            WHERE metric_type = 'memory.used_bytes'
                            GROUP BY host
                        ) latest ON m.host = latest.host AND m.timestamp = latest.max_ts
                        WHERE h.last_seen > ? AND m.metric_type = 'memory.used_bytes'
                    )
                ''', (cutoff,))
                total_memory = cursor.fetchone()[0] or 0
                
                return {
                    'total_hosts': total_hosts,
                    'online_hosts': online_hosts,
                    'offline_hosts': offline_hosts,
                    'avg_cpu_usage': float(avg_cpu),
                    'total_memory_used': float(total_memory),
                    'timestamp': int(time.time())
                }
        except Exception as e:
            print(f"Error getting fleet summary: {e}")
            return {
                'total_hosts': 0,
                'online_hosts': 0,
                'offline_hosts': 0,
                'avg_cpu_usage': 0,
                'total_memory_used': 0,
                'timestamp': int(time.time())
            }
