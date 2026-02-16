# Warehouse SKU Generator (Qt)

Qt/C++ desktop app for SKU catalog management, QR generation, and print/export workflows, backed by SQLite.

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
- Runtime app log path: `<log/path>/app_run.log` (or `AppDataLocation/data/app_run.log` if `log/path` is not configured)

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

Manual qmake equivalent:

```ps1
New-Item -ItemType Directory -Path build-qmake -Force | Out-Null
Set-Location build-qmake
C:\Qt\6.9.3\mingw_64\bin\qmake.exe ..\warehouse_sku_generator.pro CONFIG+=release CONFIG-=debug CONFIG-=debug_and_release
C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe
```

## Windows Installer (Inno Setup)
Installer build script defaults to qmake output (`build-qmake\`).

Requirements:
- Qt (for `windeployqt.exe`, unless Qt runtime DLLs are already in build output)
- Inno Setup (`iscc.exe`)

Example:

```ps1
.\installer\build-installer.ps1 -BuildDir "build-qmake" -AppVersion "1.0.0"
```

Output:
- Staging files: `dist\staging\`
- Installer EXE: `dist\warehouse_installer.exe`

## Legacy Note
`CMakeLists.txt` remains in the repo for compatibility, but supported local builds use qmake.
