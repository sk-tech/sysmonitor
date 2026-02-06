"""Query API for historical metrics data"""

from datetime import datetime, timedelta
from typing import List, Dict, Optional, Union
import pandas as pd
from sqlalchemy import and_, func

from .database import MetricsDatabase, Metric, Metric1m, Metric1h


def query_range(
    db_path: str,
    metric_type: str,
    start: Union[datetime, int],
    end: Union[datetime, int],
    resolution: str = "auto",
    host: Optional[str] = None,
    tags: Optional[Dict[str, str]] = None,
    limit: int = 0
) -> pd.DataFrame:
    """
    Query metrics within a time range

    Args:
        db_path: Path to SQLite database
        metric_type: Type of metric (e.g., "cpu.usage", "memory.used_bytes")
        start: Start time (datetime or Unix timestamp)
        end: End time (datetime or Unix timestamp)
        resolution: Resolution to use ("1s", "1m", "1h", or "auto")
        host: Filter by hostname (None = all hosts)
        tags: Filter by tags (JSON matching)
        limit: Maximum number of results (0 = no limit)

    Returns:
        DataFrame with columns: timestamp, metric_type, host, tags, value
    """
    # Convert datetimes to Unix timestamps
    if isinstance(start, datetime):
        start = int(start.timestamp())
    if isinstance(end, datetime):
        end = int(end.timestamp())

    # Auto-select resolution based on time range
    if resolution == "auto":
        time_range = end - start
        if time_range <= 86400:  # <= 24 hours
            resolution = "1s"
        elif time_range <= 2592000:  # <= 30 days
            resolution = "1m"
        else:
            resolution = "1h"

    # Select appropriate table
    if resolution == "1m":
        table_class = Metric1m
    elif resolution == "1h":
        table_class = Metric1h
    else:
        table_class = Metric

    # Build query
    db = MetricsDatabase(db_path)
    session = db.get_session()

    try:
        query = session.query(table_class).filter(
            and_(
                table_class.metric_type == metric_type,
                table_class.timestamp >= start,
                table_class.timestamp <= end
            )
        )

        # Add optional filters
        if host:
            query = query.filter(table_class.host == host)

        if tags:
            # Note: This is a simple contains check, not exact JSON matching
            for key, value in tags.items():
                tag_pattern = f'%"{key}":"{value}"%'
                query = query.filter(table_class.tags.like(tag_pattern))

        # Order and limit
        query = query.order_by(table_class.timestamp.desc())
        if limit > 0:
            query = query.limit(limit)

        # Execute and convert to DataFrame
        results = query.all()
        
        if not results:
            return pd.DataFrame(columns=['timestamp', 'metric_type', 'host', 'tags', 'value'])

        data = [{
            'timestamp': r.timestamp,
            'metric_type': r.metric_type,
            'host': r.host,
            'tags': r.tags,
            'value': r.value
        } for r in results]

        df = pd.DataFrame(data)
        
        # Convert timestamp to datetime for convenience
        df['datetime'] = pd.to_datetime(df['timestamp'], unit='s')
        
        return df

    finally:
        session.close()
        db.close()


def query_latest(
    db_path: str,
    metric_type: str,
    host: Optional[str] = None
) -> Optional[Dict]:
    """
    Query the latest value for a metric

    Args:
        db_path: Path to SQLite database
        metric_type: Type of metric
        host: Filter by hostname

    Returns:
        Dictionary with latest metric data, or None if not found
    """
    db = MetricsDatabase(db_path)
    session = db.get_session()

    try:
        query = session.query(Metric).filter(Metric.metric_type == metric_type)
        
        if host:
            query = query.filter(Metric.host == host)
        
        result = query.order_by(Metric.timestamp.desc()).first()
        
        if result:
            return {
                'timestamp': result.timestamp,
                'metric_type': result.metric_type,
                'host': result.host,
                'tags': result.tags,
                'value': result.value
            }
        
        return None

    finally:
        session.close()
        db.close()


def aggregate_metrics(
    db_path: str,
    metric_type: str,
    start: Union[datetime, int],
    end: Union[datetime, int],
    agg_func: str = "avg",
    group_by_minutes: int = 1
) -> pd.DataFrame:
    """
    Aggregate metrics over time windows

    Args:
        db_path: Path to SQLite database
        metric_type: Type of metric
        start: Start time
        end: End time
        agg_func: Aggregation function ("avg", "min", "max", "sum")
        group_by_minutes: Group by this many minutes

    Returns:
        DataFrame with aggregated data
    """
    # Convert datetimes to Unix timestamps
    if isinstance(start, datetime):
        start = int(start.timestamp())
    if isinstance(end, datetime):
        end = int(end.timestamp())

    db = MetricsDatabase(db_path)
    session = db.get_session()

    try:
        # Select aggregation function
        agg_funcs = {
            'avg': func.avg,
            'min': func.min,
            'max': func.max,
            'sum': func.sum
        }
        
        if agg_func not in agg_funcs:
            raise ValueError(f"Invalid aggregation function: {agg_func}")

        agg_col = agg_funcs[agg_func](Metric.value).label('value')
        
        # Group by time windows (convert to minutes, then back)
        time_window = (Metric.timestamp / (group_by_minutes * 60)).cast(Integer) * (group_by_minutes * 60)
        
        query = session.query(
            time_window.label('timestamp'),
            Metric.metric_type,
            Metric.host,
            agg_col
        ).filter(
            and_(
                Metric.metric_type == metric_type,
                Metric.timestamp >= start,
                Metric.timestamp <= end
            )
        ).group_by(
            time_window,
            Metric.metric_type,
            Metric.host
        ).order_by(time_window.desc())

        results = query.all()
        
        if not results:
            return pd.DataFrame(columns=['timestamp', 'metric_type', 'host', 'value'])

        data = [{
            'timestamp': r.timestamp,
            'metric_type': r.metric_type,
            'host': r.host,
            'value': r.value
        } for r in results]

        df = pd.DataFrame(data)
        df['datetime'] = pd.to_datetime(df['timestamp'], unit='s')
        
        return df

    finally:
        session.close()
        db.close()


def get_metric_types(db_path: str) -> List[str]:
    """
    Get list of all metric types in database

    Args:
        db_path: Path to SQLite database

    Returns:
        List of metric type names
    """
    db = MetricsDatabase(db_path)
    session = db.get_session()

    try:
        results = session.query(Metric.metric_type).distinct().all()
        return [r[0] for r in results]

    finally:
        session.close()
        db.close()


def get_hosts(db_path: str) -> List[str]:
    """
    Get list of all hosts in database

    Args:
        db_path: Path to SQLite database

    Returns:
        List of hostnames
    """
    db = MetricsDatabase(db_path)
    session = db.get_session()

    try:
        results = session.query(Metric.host).distinct().all()
        return [r[0] for r in results]

    finally:
        session.close()
        db.close()
