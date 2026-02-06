# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Planned for v0.2.0 (Week 2)
- Time-series database implementation (SQLite backend)
- Metrics aggregation and batch writing
- Data retention policies
- Historical data query API
- Python storage layer with SQLAlchemy

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
