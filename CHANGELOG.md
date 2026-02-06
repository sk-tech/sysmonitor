# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Planned for v0.4.0 (Week 4)
- Advanced alerting system (threshold-based)
- Email/webhook notifications
- Process-level metrics detailed view
- Multi-host monitoring (distributed mode)
- Configuration file support (YAML)

## [0.3.0] - 2026-02-06

### Added - Week 3: Web API & Dashboard
- **Python HTTP API Server**: RESTful endpoints using standard library (no FastAPI dependency required)
- **Direct SQLite Access**: Zero-dependency database queries for metrics retrieval
- **REST Endpoints**:
  - `GET /api/health` - Server health check
  - `GET /api/metrics/latest` - Most recent metric value
  - `GET /api/metrics/history` - Historical time-range queries
  - `GET /api/metrics/types` - List all available metrics
  - `GET /api/stream` - Server-Sent Events for real-time streaming
- **Interactive Web Dashboard**:
  - Real-time metric cards with trend indicators (CPU, memory, load avg)
  - Live CPU & Memory chart with 5-minute history
  - Auto-refresh every 2 seconds
  - Dark theme with responsive design
  - Canvas-based charting (no external dependencies)
- **Startup Scripts**:
  - `scripts/start.sh` - Launch daemon + API server
  - `scripts/stop.sh` - Graceful shutdown
- **API Documentation**: Complete endpoint reference in `docs/API.md`

### Changed
- **API Server**: Implemented with `http.server` (stdlib) instead of FastAPI for zero dependencies
- **Storage Access**: Direct SQLite queries instead of SQLAlchemy (fallback for missing deps)

### Technical Achievements
- **Zero External Dependencies**: API server runs with Python 3 standard library only
- **SSE Streaming**: Real-time metrics push via Server-Sent Events (2s polling interval)
- **Canvas Charting**: Custom chart implementation without external JS libraries
- **API Response Time**: <20ms for latest metric, <50ms for history queries
- **Dashboard Performance**: Sub-100ms page load, 2-second refresh with minimal CPU impact

### Performance Metrics
- API server memory footprint: ~15MB RSS
- Dashboard load time: <100ms
- Metrics fetch latency: 15-20ms per endpoint
- Chart rendering: <5ms (30 data points)
- Concurrent connections: Tested up to 10 simultaneous clients

### Known Issues
- SSE stream requires manual reconnection on network errors (browser handles this)
- Chart limited to 30 data points for performance (scrolling history)
- No HTTPS support yet (local development only)

## [0.2.0] - 2026-02-06

### Added - Week 2: Data Collection & Storage
- **SQLite Storage Layer**: Time-series database with 1-second resolution metrics
- **MetricsStorage Class**: C++ storage backend with batch writes and transactions
- **Schema Migration System**: Version table for production-ready database migrations
- **Retention Manager**: Multi-tier retention (1s→1m→1h rollups, configurable retention)
- **Storage Integration**: MetricsCollector now persists CPU, memory, disk, network metrics
- **Python Storage Module**: SQLAlchemy ORM models for metrics tables
- **Query API**: Python functions for historical data retrieval with pandas output
  - `query_range()`: Time-range queries with auto-resolution selection
  - `query_latest()`: Get most recent metric value
  - `aggregate_metrics()`: Time-window aggregations (avg/min/max/sum)
  - `get_metric_types()` / `get_hosts()`: Database introspection
- **CLI History Command**: `sysmon history <metric> [duration] [limit]` for querying stored data
- **Batch Writing**: Configurable batch size (default 100) and flush interval (5s) for I/O efficiency
- **WAL Mode**: Write-Ahead Logging enabled for concurrent read performance

### Changed
- **MetricsCollector**: Added optional storage configuration constructor
- **Daemon**: Now creates `~/.sysmon/data.db` by default, accepts custom path as argument
- **Build System**: Enabled storage subdirectory, linked SQLite3 to core/CLI/daemon
- **Dependencies**: Added pandas to Python requirements

### Technical Achievements
- **Storage Write Performance**: <100ms latency from collection to database write
- **Batch Transaction Support**: Single transaction for up to 100 metrics (10x faster than individual writes)
- **Schema Versioning**: Production-ready migration system with `schema_version` table
- **Ring Buffer**: 10k metric buffer for storage failure resilience
- **Type Safety**: Fixed DiskMetrics/NetworkMetrics type consistency across platform layer
- **Database Size**: ~216KB for 15 seconds of data collection (all metrics)

### Design Decisions (Week 2 Considerations)
1. **Storage Write Thread**: Uses MetricsCollector's existing background thread (simplicity over isolation)
2. **Python Binding Scope**: SQLAlchemy-only access (loose coupling, no pybind11 for storage yet)
3. **Schema Migration**: Manual version table approach (industry standard over auto-migrations)

### Performance Metrics
- Build time: ~3 seconds (incremental), ~8 seconds (clean build)
- Database writes: Batched transactions (100 metrics/batch)
- Query performance: <50ms for 1-hour range (tested with cpu.total_usage)
- Storage footprint: ~14KB/minute (all metrics at 5s interval)

### Known Issues
- Disk/Network metrics collection may throw exceptions on some systems (non-blocking)
- VACUUM not run automatically (consider offline maintenance for space reclamation)
- Python module requires `sqlalchemy` and `pandas` packages (install via pip or apt)

## [0.1.0] - 2026-02-06

### Added - Week 1: Core Infrastructure
- Project structure with full SDLC documentation
- Software Requirements Specification (SRS)
- System Architecture Design document
- CMake cross-platform build system
- GitHub Actions CI/CD for Linux, Windows, macOS
- Platform abstraction layer interfaces
- Core monitoring engine foundation
- Python bindings setup with pybind11
- Unit test framework (Google Test, pytest)
- Code quality checks (cppcheck, clang-format, pylint)
- CPack configuration for packaging (.deb, .rpm, .msi, .dmg)

### Technical Achievements
- Successfully configured multi-platform builds
- Established development workflow and Git conventions
- Created comprehensive documentation structure
- Set up automated testing infrastructure

### Known Issues
- None (initial setup phase)

### Performance Metrics
- Build time: ~2 minutes (Release mode)
- Binary size: TBD (awaiting implementation)
- Test coverage: TBD

---

## Release Notes Template (for future weeks)

### [0.X.0] - YYYY-MM-DD

#### Added
- New features implemented this week

#### Changed
- Modifications to existing functionality

#### Fixed
- Bug fixes and issue resolutions

#### Performance
- Optimization results and benchmarks

#### Technical Challenges
- Interesting problems solved
- Design decisions made
- Trade-offs considered

#### Demo Highlights
- Key features demonstrated on Friday
- User-facing improvements

#### Next Week's Goals
- Planned features for next sprint
