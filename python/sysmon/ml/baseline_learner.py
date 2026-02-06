"""
Baseline learning for adaptive anomaly detection.
Learns normal behavior patterns and calculates dynamic thresholds.
"""

import sqlite3
import numpy as np
from typing import Dict, Optional, Tuple, List
from dataclasses import dataclass
from datetime import datetime, timedelta
import json
import os


@dataclass
class Baseline:
    """Learned baseline for a metric"""
    metric_type: str
    host: str
    mean: float
    stddev: float
    min_value: float
    max_value: float
    sample_count: int
    last_updated: int  # Unix timestamp
    percentile_95: float
    percentile_99: float
    
    def to_dict(self) -> Dict:
        """Convert to dictionary for JSON serialization"""
        return {
            'metric_type': self.metric_type,
            'host': self.host,
            'mean': self.mean,
            'stddev': self.stddev,
            'min_value': self.min_value,
            'max_value': self.max_value,
            'sample_count': self.sample_count,
            'last_updated': self.last_updated,
            'percentile_95': self.percentile_95,
            'percentile_99': self.percentile_99,
        }
    
    def get_threshold(self, sigma: float = 3.0) -> Tuple[float, float]:
        """
        Get dynamic threshold based on baseline.
        
        Args:
            sigma: Number of standard deviations for threshold
            
        Returns:
            Tuple of (lower_threshold, upper_threshold)
        """
        lower = self.mean - sigma * self.stddev
        upper = self.mean + sigma * self.stddev
        return (lower, upper)


class BaselineLearner:
    """
    Learns and maintains baselines for metrics.
    Stores baselines in SQLite for persistence.
    """
    
    def __init__(self, db_path: str = None):
        """
        Initialize baseline learner.
        
        Args:
            db_path: Path to metrics database (defaults to ~/.sysmon/data.db)
        """
        if db_path is None:
            db_path = os.path.expanduser("~/.sysmon/data.db")
        
        self.db_path = db_path
        self.baselines: Dict[str, Baseline] = {}
        self._init_database()
        self._load_baselines()
    
    def _init_database(self) -> None:
        """Initialize baselines table in database"""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        # Create baselines table
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS baselines (
                metric_type TEXT NOT NULL,
                host TEXT NOT NULL,
                mean REAL NOT NULL,
                stddev REAL NOT NULL,
                min_value REAL NOT NULL,
                max_value REAL NOT NULL,
                sample_count INTEGER NOT NULL,
                last_updated INTEGER NOT NULL,
                percentile_95 REAL NOT NULL,
                percentile_99 REAL NOT NULL,
                PRIMARY KEY (metric_type, host)
            ) WITHOUT ROWID
        """)
        
        # Create index for efficient lookups
        cursor.execute("""
            CREATE INDEX IF NOT EXISTS idx_baselines_updated 
            ON baselines(last_updated)
        """)
        
        conn.commit()
        conn.close()
    
    def _load_baselines(self) -> None:
        """Load baselines from database"""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        cursor.execute("SELECT * FROM baselines")
        rows = cursor.fetchall()
        
        for row in rows:
            baseline = Baseline(
                metric_type=row[0],
                host=row[1],
                mean=row[2],
                stddev=row[3],
                min_value=row[4],
                max_value=row[5],
                sample_count=row[6],
                last_updated=row[7],
                percentile_95=row[8],
                percentile_99=row[9]
            )
            key = f"{row[1]}:{row[0]}"  # host:metric_type
            self.baselines[key] = baseline
        
        conn.close()
    
    def learn_baseline(
        self, 
        metric_type: str, 
        host: str = "localhost",
        hours: int = 24
    ) -> Optional[Baseline]:
        """
        Learn baseline from historical data.
        
        Args:
            metric_type: Type of metric (e.g., 'cpu.total_usage')
            host: Hostname
            hours: Number of hours of history to use
            
        Returns:
            Learned Baseline object or None if insufficient data
        """
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        # Get historical data
        cutoff_time = int((datetime.now() - timedelta(hours=hours)).timestamp())
        
        cursor.execute("""
            SELECT timestamp, value
            FROM metrics
            WHERE metric_type = ? AND host = ? AND timestamp >= ?
            ORDER BY timestamp ASC
        """, (metric_type, host, cutoff_time))
        
        rows = cursor.fetchall()
        conn.close()
        
        if len(rows) < 10:  # Need minimum samples
            return None
        
        # Extract values
        values = np.array([row[1] for row in rows])
        
        # Calculate statistics
        baseline = Baseline(
            metric_type=metric_type,
            host=host,
            mean=float(np.mean(values)),
            stddev=float(np.std(values)),
            min_value=float(np.min(values)),
            max_value=float(np.max(values)),
            sample_count=len(values),
            last_updated=int(datetime.now().timestamp()),
            percentile_95=float(np.percentile(values, 95)),
            percentile_99=float(np.percentile(values, 99))
        )
        
        # Store baseline
        self._save_baseline(baseline)
        
        # Cache in memory
        key = f"{host}:{metric_type}"
        self.baselines[key] = baseline
        
        return baseline
    
    def _save_baseline(self, baseline: Baseline) -> None:
        """Save baseline to database"""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        cursor.execute("""
            INSERT OR REPLACE INTO baselines 
            (metric_type, host, mean, stddev, min_value, max_value, 
             sample_count, last_updated, percentile_95, percentile_99)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            baseline.metric_type,
            baseline.host,
            baseline.mean,
            baseline.stddev,
            baseline.min_value,
            baseline.max_value,
            baseline.sample_count,
            baseline.last_updated,
            baseline.percentile_95,
            baseline.percentile_99
        ))
        
        conn.commit()
        conn.close()
    
    def get_baseline(
        self, 
        metric_type: str, 
        host: str = "localhost",
        max_age_hours: int = 24
    ) -> Optional[Baseline]:
        """
        Get baseline for a metric.
        
        Args:
            metric_type: Type of metric
            host: Hostname
            max_age_hours: Maximum age of baseline in hours (relearn if older)
            
        Returns:
            Baseline object or None if not available
        """
        key = f"{host}:{metric_type}"
        baseline = self.baselines.get(key)
        
        if baseline is None:
            # Try to learn baseline
            return self.learn_baseline(metric_type, host)
        
        # Check if baseline is stale
        age_seconds = datetime.now().timestamp() - baseline.last_updated
        if age_seconds > max_age_hours * 3600:
            # Relearn baseline
            return self.learn_baseline(metric_type, host)
        
        return baseline
    
    def update_all_baselines(self, hours: int = 24) -> Dict[str, Baseline]:
        """
        Update all baselines from recent data.
        
        Args:
            hours: Number of hours of history to use
            
        Returns:
            Dictionary of updated baselines
        """
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        # Get all unique metric/host combinations
        cursor.execute("""
            SELECT DISTINCT metric_type, host
            FROM metrics
        """)
        
        combinations = cursor.fetchall()
        conn.close()
        
        updated = {}
        for metric_type, host in combinations:
            baseline = self.learn_baseline(metric_type, host, hours)
            if baseline:
                key = f"{host}:{metric_type}"
                updated[key] = baseline
        
        return updated
    
    def is_anomalous(
        self,
        metric_type: str,
        value: float,
        host: str = "localhost",
        sigma: float = 3.0
    ) -> Tuple[bool, Optional[Baseline]]:
        """
        Check if a value is anomalous compared to baseline.
        
        Args:
            metric_type: Type of metric
            value: Current value
            host: Hostname
            sigma: Number of standard deviations for threshold
            
        Returns:
            Tuple of (is_anomalous, baseline)
        """
        baseline = self.get_baseline(metric_type, host)
        
        if baseline is None:
            return (False, None)
        
        lower, upper = baseline.get_threshold(sigma)
        is_anomalous = value < lower or value > upper
        
        return (is_anomalous, baseline)
    
    def get_all_baselines(self) -> Dict[str, Baseline]:
        """Get all cached baselines"""
        return self.baselines.copy()
