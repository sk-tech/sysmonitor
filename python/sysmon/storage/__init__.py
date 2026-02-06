"""Storage module initialization"""

from .database import MetricsDatabase, Metric, Metric1m, Metric1h, SchemaVersion
from .query_api import (
    query_range,
    query_latest,
    aggregate_metrics,
    get_metric_types,
    get_hosts
)

__all__ = [
    'MetricsDatabase',
    'Metric',
    'Metric1m',
    'Metric1h',
    'SchemaVersion',
    'query_range',
    'query_latest',
    'aggregate_metrics',
    'get_metric_types',
    'get_hosts'
]
