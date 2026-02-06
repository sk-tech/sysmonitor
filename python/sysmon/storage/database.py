"""SQLAlchemy ORM models for metrics storage"""

from sqlalchemy import Column, Integer, String, Float, Text, Index
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker
from sqlalchemy import create_engine
import json

Base = declarative_base()


class Metric(Base):
    """Main metrics table - time-series data with 1-second resolution"""
    __tablename__ = 'metrics'

    timestamp = Column(Integer, primary_key=True, nullable=False)
    metric_type = Column(String, primary_key=True, nullable=False)
    host = Column(String, primary_key=True, nullable=False)
    tags = Column(Text, primary_key=True, nullable=False, default='')
    value = Column(Float, nullable=False)

    __table_args__ = (
        Index('idx_metric_time', 'metric_type', 'timestamp'),
        Index('idx_host_time', 'host', 'timestamp'),
        Index('idx_timestamp', 'timestamp'),
        {'sqlite_autoincrement': False}
    )

    def __repr__(self):
        return f"<Metric(timestamp={self.timestamp}, type='{self.metric_type}', value={self.value})>"

    @property
    def tags_dict(self):
        """Parse JSON tags into dictionary"""
        if self.tags:
            try:
                return json.loads(self.tags)
            except json.JSONDecodeError:
                return {}
        return {}


class Metric1m(Base):
    """1-minute rollup table (30-day retention)"""
    __tablename__ = 'metrics_1m'

    timestamp = Column(Integer, primary_key=True, nullable=False)
    metric_type = Column(String, primary_key=True, nullable=False)
    host = Column(String, primary_key=True, nullable=False)
    tags = Column(Text, primary_key=True, nullable=False, default='')
    value = Column(Float, nullable=False)

    __table_args__ = (
        Index('idx_1m_metric_time', 'metric_type', 'timestamp'),
        {'sqlite_autoincrement': False}
    )


class Metric1h(Base):
    """1-hour rollup table (1-year retention)"""
    __tablename__ = 'metrics_1h'

    timestamp = Column(Integer, primary_key=True, nullable=False)
    metric_type = Column(String, primary_key=True, nullable=False)
    host = Column(String, primary_key=True, nullable=False)
    tags = Column(Text, primary_key=True, nullable=False, default='')
    value = Column(Float, nullable=False)

    __table_args__ = (
        Index('idx_1h_metric_time', 'metric_type', 'timestamp'),
        {'sqlite_autoincrement': False}
    )


class SchemaVersion(Base):
    """Schema version tracking for migrations"""
    __tablename__ = 'schema_version'

    version = Column(Integer, primary_key=True)
    applied_at = Column(Integer, nullable=False)


class MetricsDatabase:
    """Database connection manager"""

    def __init__(self, db_path: str):
        """
        Initialize database connection

        Args:
            db_path: Path to SQLite database file
        """
        self.engine = create_engine(f'sqlite:///{db_path}', echo=False)
        self.Session = sessionmaker(bind=self.engine)

    def get_session(self):
        """Get a new database session"""
        return self.Session()

    def close(self):
        """Close database connection"""
        self.engine.dispose()

    def get_schema_version(self):
        """Get current schema version"""
        session = self.get_session()
        try:
            version = session.query(SchemaVersion).order_by(
                SchemaVersion.version.desc()
            ).first()
            return version.version if version else 0
        finally:
            session.close()
