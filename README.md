# Warehouse SKU Generator (Qt)

This project is a Qt C++ desktop application for SKU generation, search, and catalog management. It mirrors the Google Sheets logic you provided and stores all SKU data in a separate SQLite database file.

## Features
- Auto-generate SKU based on category + sub-category + item serial + variation code
- Search by SKU / Part Number / Part Name (partial match, case-insensitive)
- Store product metadata (dimensions, weight, storage, family, min stock, comments)
- Save and preview package images in a local image library
- SQLite database file separate from the application binaries

## Data Files
- Rules source: `Assets/SKU Rules.xlsx`
- Rules CSV used for seeding: `Assets/sku_rules.csv`
- Database file (auto-created on first run): AppDataLocation `data/sku.db`
- Stored images: AppDataLocation `data/images/`

If you update the Excel rules, regenerate `Assets/sku_rules.csv` before launching the app so the new rules are imported on first run.

## Build (CMake)
Requirements:
- Qt 5 or Qt 6 with Widgets + SQL modules
- CMake 3.16+
- A compiler (MSVC, MinGW, or clang)

Example (Windows PowerShell):

```ps1
cmake -S . -B build
cmake --build build --config Release
```

Run the app from the build output folder.

## Windows Installer (Inno Setup)
This repo includes a simple installer script that bundles the Qt runtime with `windeployqt` and builds a setup EXE using Inno Setup.

Requirements:
- Qt (for `windeployqt.exe`)
- Inno Setup (for `iscc.exe`)

Example (PowerShell):
```ps1
.\installer\build-installer.ps1 -BuildDir "build-qmake\release" -AppVersion "1.0.0"
```

Output:
- Staging files: `dist\staging\`
- Installer: `dist\Warehouse_SKU_Generator_Setup_1.0.0.exe` (name varies by version)

## Usage Notes
- Select Category and Sub Category to auto-generate a new SKU.
- `Generate SKU and Update List` saves the entry to the database.
- `Fill From Search` loads the first search result into the form.
- Images are copied into `data/images/` to keep them inside the system.
