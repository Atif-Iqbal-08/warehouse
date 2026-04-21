# Changelog

All notable changes to this project are documented in this file.
This changelog is based on repository history through 2026-02-17.
Release sections should match git tags in `vMAJOR.MINOR.PATCH` format.

## [Unreleased]

### Added
- Added `release.ps1` wrapper to bump version, roll `[Unreleased]` notes into a release entry, and build the installer in one command.

### Changed
- Updated installer script to rebuild latest application code before packaging by default.
- Added installer build options for `BuildConfig`, `CleanBuild`, and `SkipBuild`.
- Enforced admin installer mode via Inno Setup `PrivilegesRequired=admin` to trigger UAC.

### Documentation
- Added a dedicated `Version Details` section in `README.md`.
- Added a release tag strategy section in `README.md`.
- Added `CHANGELOG.md` and linked it from `README.md`.

## [1.0.0] - 2026-02-16

### Added
- Added database status UX improvements in the main window.
- Added Help menu dialog workflow.
- Added installer location prompts and related setup flow.
- Added technical documentation and user guide markdown files.

### Changed
- Updated `README.md` content and build/install guidance.
- Expanded main window behavior and declarations.

### Source
- Commit: `9c29a2b`

## [Pre-1.0.0] - 2026-02-10

### Added
- Initial Qt/C++ Warehouse SKU Generator application scaffold and repository structure.
- Initial SKU management, QR generation/history, export/print, role access, and backup foundations.
- Initial installer/build scripts, assets, and seed data.

### Source
- Commit: `93b92c8`
