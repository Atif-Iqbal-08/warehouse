# Warehouse SKU Generator (Qt)

Qt/C++ desktop app for SKU catalog management, QR generation, and print/export workflows, backed by SQLite.

## Version Details
- Current application version: `1.0.0`
- Version source: `installer/warehouse_sku_generator.iss` (`AppVersion`) and `installer/build-installer.ps1` (`-AppVersion` default)
- Latest release baseline in repo history: `1.0.0` on `2026-02-16`
- Changelog: [`CHANGELOG.md`](CHANGELOG.md)

## Tag Strategy
- Tag format: `vMAJOR.MINOR.PATCH` (example: `v1.0.0`)
- Use annotated tags for every release build:
  - `git tag -a v1.0.1 -m "Release v1.0.1"`
  - `git push origin v1.0.1`
- Bump rules:
  - `PATCH` (`1.0.0 -> 1.0.1`): bug fixes, small safe improvements
  - `MINOR` (`1.0.0 -> 1.1.0`): backward-compatible features
  - `MAJOR` (`1.0.0 -> 2.0.0`): breaking changes
- Pre-release tags (optional): `v1.1.0-rc.1`, `v1.1.0-beta.1`
- Release flow:
  1. Run release wrapper (bumps version, updates changelog, builds installer):
     `.\release.ps1 -Bump patch`
  2. Commit release changes.
  3. Create and push the release tag.

## Current Features
- SKU auto-generation from category + sub-category + item serial + variation code
- SKU catalog CRUD with search by SKU / part number / part name
- Product metadata fields: part info, dimensions, weight, storage, rack/bin, family, comments
- Product image upload, preview, and storage in DB/file fallback
- QR code generation per SKU with prefix, quantity, quarter/year, and serial tracking
- QR history view with SKU/serial filtering, edit serial details, and delete selected history rows
- QR label export to PDF with configurable print settings (label size, margins, QR placement preview)
- CSV export:
  - SKU master export
  - QR summary export
- Dashboard cards (total SKUs, total QRs, current quarter QRs, latest SKUs)
- Role-based access/login:
  - View Only
  - Add Data
  - Full Access
- User management UI for creating users and assigning roles
- Audit logging for sensitive actions
- Database operations:
  - Load DB
  - Save/Export DB copy
  - Backup DB
- Theme switching (Dark/Light/System), fullscreen toggle
- First-run seed/migration support from asset CSV and legacy data paths

## Data Files
- Rules source: `Assets/SKU Rules.xlsx`
- Rules seed CSV: `Assets/sku_rules.csv`
- Catalog seed CSV (optional): `Assets/EXISTING sku DATA.csv`
- Runtime DB path: AppDataLocation `data/sku.db`
- Runtime image path: AppDataLocation `data/images/`
- Runtime app log path (automatic): `%LOCALAPPDATA%\\Warehouse SKU Logs\\app_run_YYYY-MM.log`

If you update the Excel rules, regenerate `Assets/sku_rules.csv` before first launch on a fresh DB.

## Official Build Platform
Use **qmake (Qt environment)** as the canonical build system.

Requirements:
- Qt 5 or Qt 6 with `Widgets`, `Sql`, and `PrintSupport`
- `qmake` (from the Qt kit you want to use)
- A matching make tool (`mingw32-make`, `jom`, or `nmake`)
- A C++ compiler toolchain (MSVC/MinGW/clang)

Windows PowerShell:

```ps1
.\build.ps1
```

This builds `Release` to `build-qmake\` by default.
If script execution is blocked, run:

```ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

Common options:

```ps1
.\build.ps1 -Config Debug
.\build.ps1 -Clean
.\build.ps1 -QtBinDir "C:\Qt\6.9.3\mingw_64\bin"
.\build.ps1 -MakeTool "C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe"
```

One-command app + installer build:

```ps1
.\build-all.ps1 -Clean -QtBinDir "C:\Qt\6.9.3\mingw_64\bin"
```

Manual qmake equivalent:

```ps1
New-Item -ItemType Directory -Path build-qmake -Force | Out-Null
Set-Location build-qmake
C:\Qt\6.9.3\mingw_64\bin\qmake.exe ..\warehouse_sku_generator.pro CONFIG+=release CONFIG-=debug CONFIG-=debug_and_release
C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe
```

## Windows Installer (Inno Setup)
Installer build script defaults to qmake output (`build-qmake\`).

The installer now writes machine-wide bootstrap defaults to `ProgramData\Warehouse SKU Generator\installer_bootstrap.ini`.
On first launch after install or reinstall, the app imports those DB/backup defaults into the current user's settings, which keeps admin installs and per-user settings aligned without installer HKCU writes.

Requirements:
- Qt (for `windeployqt.exe`, unless Qt runtime DLLs are already in build output)
- Inno Setup (`iscc.exe`)

Example:

```ps1
.\installer\build-installer.ps1 -BuildDir "build-qmake" -BuildConfig Release -CleanBuild -AppVersion "1.0.0"
```

Output:
- Staging files: `dist\staging\`
- Installer EXE: `dist\warehouse_installer.exe`
- Installed docs (placed by installer): `docs\Warehouse_SKU_QR_Manager_User_Guide.md` and `docs\Warehouse_SKU_QR_Manager_Technical_Documentation.md`

### Feature-to-Installer Process
Use this process whenever new features are added:

1. Merge/commit new feature code.
2. Run installer build script (it now rebuilds the app first by default):
   `.\installer\build-installer.ps1 -BuildDir "build-qmake" -BuildConfig Release -CleanBuild -AppVersion "<new-version>"`
3. Verify installer output exists:
   `dist\warehouse_installer.exe`
4. Install and smoke-test key new feature flows.

Notes:
- Use `-SkipBuild` only if you intentionally want to package an existing build directory.
- Keep `AppVersion` aligned with `CHANGELOG.md` and tags.

### One-Command Release
Use the wrapper script for release packaging and version/changelog updates:

```ps1
.\release.ps1 -Bump patch
.\release.ps1 -Version 1.1.0 -BuildDir "build-qmake-release" -OutputDir "dist-release"
```

## Legacy Note
`CMakeLists.txt` remains in the repo for compatibility, but supported local builds use qmake.
