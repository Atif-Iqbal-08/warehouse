# Warehouse SKU and QR Code Manager
Technical Documentation

Document date: 2026-02-16  
Codebase root: `d:\Wareehouse Bar Code generator`  
Primary implementation files: `src/mainwindow.cpp`, `src/mainwindow_history.cpp`, `src/printsettingsdialog.cpp`, `src/mainwindow.ui`

## 1. Purpose
This document describes the system behavior, runtime flow, major function groups, database behavior, and error paths of the Warehouse SKU and QR Code Manager desktop application.

This is a code-driven technical reference intended for:
- Developers maintaining the codebase
- QA and release engineers validating behavior
- Technical leads reviewing system logic and risks

## 2. Technology and Runtime Context
- Language: C++
- UI framework: Qt Widgets
- Database: SQLite (`QSQLITE`)
- Printing/export: Qt PrintSupport PDF output
- Security primitives:
  - Password hashing: SHA-256 with per-user salt
  - Backup encryption:
    - Windows: DPAPI (`CryptProtectData`) when available
    - Fallback: machine-derived XOR obfuscation

Entry point:
- `src/main.cpp`: creates `QApplication`, initializes app metadata, instantiates `MainWindow`, then enters event loop.

## 3. High-Level Architecture
The application uses a monolithic `MainWindow` controller with direct UI/data orchestration.

### 3.1 Main Components
1. UI Layer
- `.ui` layout file (`src/mainwindow.ui`) plus runtime wiring in `MainWindow::setupUi` (`src/mainwindow.cpp:2358`)
- Tabs:
  - Dashboard
  - SKU Generation
  - QR Code Generation
  - QR Code Manager (history)
- Help access:
  - Menu bar `Help` menu
  - Popup dialog containing User Guide and Technical Guide viewers

2. Domain Logic Layer (inside `MainWindow`)
- SKU computation and validation
- QR value generation and serial tracking
- Permission checks and authorization gates
- Backup scheduling and retention

3. Persistence Layer
- SQLite schema and migration management (`ensureSchema`)
- CRUD operations via `QSqlQuery`
- Soft-delete strategy for SKU and barcode rows

4. Security and Audit Layer
- User/role tables and permission resolution
- Login + admin re-authorization for destructive operations
- Centralized audit entries (`logAction`)

## 4. End-to-End Runtime Flow
### 4.1 Startup Sequence
Main path:
1. `MainWindow::MainWindow` (`src/mainwindow.cpp:181`)
2. `setupUi()` (`src/mainwindow.cpp:2358`)
3. `selectDatabaseOnStartup()` (`src/mainwindow.cpp:3022`)
4. If DB opens:
   - `initDb()` if needed
   - `importBootstrapAdminFromSettings()`
   - `ensureInitialAdmin()`
   - `promptLogin()`
5. After login:
   - `loadCategories()`
   - `onCategoryChanged()`
   - `searchRecords()`
   - `loadSkuList()`
   - `loadHistorySkuList()`
   - `updateDashboardMetrics()`
   - `scheduleAutomatedBackups()`

If startup DB setup is skipped:
- Main window remains open in no-database state.
- User can later load a DB via `File -> Load DB...`.
- Login is enforced immediately after DB load from that state.

Failure behavior:
- If DB selection/open/login/admin bootstrap fails, status is set and the window closes.

### 4.2 Database Selection/Open Flow
Function: `selectDatabaseOnStartup` (`src/mainwindow.cpp:3022`)
- Priority order:
  1. CLI override (`--db-path`)
  2. Saved path in `QSettings` (`db/path`)
  3. Interactive startup choice:
     - Create New DB
     - Skip This Time
     - Exit

Open behavior:
- `openDatabaseAt(path)` (`src/mainwindow.cpp:3097`) ensures directory, opens SQLite connection, ensures schema, roles, bootstrap users, initial admin, seed imports, and then reloads UI datasets.

Runtime log path:
- Run log writer checks `QSettings` key `log/path` first.
- If set, app writes `app_run.log` under that folder.
- If unset, fallback is `<AppDataLocation>/data/app_run.log`.

### 4.3 Authentication and Authorization Flow
Login:
- `promptLogin` (`src/mainwindow.cpp:812`) validates credentials using `verifyUserCredentials`.
- On success: loads user metadata, resolves effective permissions, updates title/status.

Permission gate:
- `requireAccess` (`src/mainwindow.cpp:354`) is the standard guard.
- Denials show warning dialog and audit `ACCESS_DENIED`.

Privileged confirmation:
- `promptAdminAuthorization` (`src/mainwindow.cpp:1668`) requires:
  - valid admin credentials
  - reason comment minimum length 20 when required

## 5. Functional Subsystems
## 5.1 Access Control and User Management
Core functions:
- `applyAccessControl` (`src/mainwindow.cpp:293`)
- `rolePolicyFor`, `applyRolePolicy`, `applyUserOverrides`
- `showSettingsDialog` (`src/mainwindow.cpp:960`)

Behavior:
- UI buttons/actions are enabled/disabled per effective access policy.
- Role defaults come from `app_roles`, optionally overridden per user (`app_users` permission columns).

### 5.2 Schema, Migration, and Seed Data
Core functions:
- `ensureSchema` (`src/mainwindow.cpp:3923`)
- `loadRulesIfEmpty` (`src/mainwindow.cpp:4263`)
- `loadCatalogIfEmpty` (`src/mainwindow.cpp:4377`)
- `migrateLegacyDataIfNeeded` (`src/mainwindow.cpp:6675`)

Tables created/managed:
- `sku_rules`
- `sku_catalog`
- `barcode_log`
- `barcode_serials`
- `app_users`
- `app_roles`
- `app_audit_log`
- `app_meta`
- `app_backup_history`

Soft-delete model:
- Active views:
  - `sku_catalog_active`
  - `barcode_log_active`
- Delete operations set flags and snapshot metadata instead of hard delete.

### 5.3 SKU Generation and Catalog CRUD
Key functions:
- Search and list: `searchRecords` (`src/mainwindow.cpp:5173`)
- Fill form from row: `fillFormFromSearch` (`src/mainwindow.cpp:5298`)
- Create: `saveForm` (`src/mainwindow.cpp:5369`)
- Update: `updateSelected` (`src/mainwindow.cpp:6080`)
- Delete: `deleteSelectedSku` (`src/mainwindow.cpp:6264`)

SKU logic:
- `computeSku` (`src/mainwindow.cpp:5063`)
- `getVariationCode` (`src/mainwindow.cpp:5075`)
- Formula: `category_digit + subcategory_digit + serial(3) + variation_code`

Validation rules:
- Part name required
- Weight required and > 0
- Category/sub-category required
- Duplicate SKU checks on create/update

### 5.4 QR Generation and Print Export
Key functions:
- Generate batch: `generateBarcodes` (`src/mainwindow.cpp:5525`)
- Delete selected serials: `deleteSelectedBarcodes` (`src/mainwindow.cpp:5641`)
- Export PDF labels: `printBarcodes` (`src/mainwindow.cpp:5829`)
- Next serial resolver: `updateNextBarcodeSerial` (`src/mainwindow.cpp:4605`)

QR value logic:
- `buildBarcodeValue` (`src/mainwindow.cpp:4639`)
- Pattern: `prefix + SKU + quarter + year_2digit + serial_5digit`

Prefix-dependent branding:
- Website and sticker logo selected by prefix via:
  - `stickerWebsiteForPrefix` (`src/mainwindow.cpp:4649`)
  - `stickerLogoForPrefix` (`src/mainwindow.cpp:4653`)

Print behavior:
- Uses `QPrinter` PDF mode.
- Sticker dimensions from `PrintSettings`.
- For each barcode, queries SKU context and lays out text + logo + QR.

### 5.5 QR History Management
Implementation: `src/mainwindow_history.cpp`

Key functions:
- Load SKU list and filter: `loadHistorySkuList` (line 28)
- Load barcode history rows: `loadBarcodeHistory` (line 100)
- Edit row: `editSelectedHistoryBarcode` (line 255)
- Delete rows: `deleteSelectedHistoryBarcodes` (line 433)

Edit flow:
- Reads selected row -> opens dialog -> recomputes QR -> validates uniqueness -> admin authorization -> updates row -> refreshes serial tracker.

### 5.6 Backup and Retention
Core functions:
- `onExportDbTriggered` (`src/mainwindow.cpp:3656`)
- `onSaveDbTriggered` (`src/mainwindow.cpp:3824`)
- `backupDatabaseTo` (`src/mainwindow.cpp:3211`)
- `runAutomatedBackup` (`src/mainwindow.cpp:3429`)
- `pruneBackupRetention` (`src/mainwindow.cpp:3323`)

Automated backup behavior:
- Backup file naming by classification/day window.
- Retention policy:
  - keep all <= 30 days
  - keep weekly/monthly representatives for older backups

## 6. UI Event Wiring Model
`setupUi` performs all `connect(...)` bindings for:
- Buttons
- Menu actions
- Combobox edits and selection changes
- Table selection events
- Tab changes

Reference region:
- `src/mainwindow.cpp:2755` to `src/mainwindow.cpp:2889`

Notable behavior:
- Tab switch hook `onTabChanged` clears QR tab fields whenever QR tab is selected (`src/mainwindow.cpp:5819`).

## 7. Error Handling Model
### 7.1 Common Mechanisms
1. `setStatus(message, ok)` (`src/mainwindow.cpp:5086`)
- User-facing message and color coding.

2. `requireAccess(...)` (`src/mainwindow.cpp:354`)
- Standard permission denial path.

3. `logAction(...)` (`src/mainwindow.cpp:1733`)
- Unified audit insert; includes action metadata and error details.

4. SQL failure branch patterns
- Most operations check `exec()` and branch to rollback/status/audit.

### 7.2 Error Categories and Cases
Database errors:
- DB open failure
- Schema ensure failure
- Seed CSV missing (`sku_rules.csv`)
- Backup copy failure

Validation errors:
- Missing required SKU fields
- Invalid quantity/weight
- Duplicate SKU or duplicate barcode collision

Permission errors:
- Role denies operation
- Admin re-authorization rejected/canceled

I/O errors:
- Image file unreadable/unsupported
- CSV/PDF output path not writable

Data-state errors:
- No selected rows for update/delete/edit actions
- No QR data available to export

## 8. Logical Flow Review Findings (Code-Level)
This section lists observed flow inconsistencies and risks from the current code.

### Finding 1 (Resolved): QR generation period source
Evidence:
- Generation now uses selected period controls in `generateBarcodes` (`selectedBarcodeYear()` and `selectedBarcodeQuarter()`).
- UI period selectors and next-serial tracker are aligned.

Impact:
- Generated rows match operator-selected quarter/year in the QR form.

Recommendation:
- Keep this behavior and add regression tests around period selection.

### Finding 2 (Resolved): Export DB permission consistency
Evidence:
- Action enablement and runtime check both require backup/restore permission.

Impact:
- Avoids false-positive enabled actions for export-only users.

Recommendation:
- Keep checks synchronized whenever permission model evolves.

### Finding 3 (Medium): Switching to QR tab clears current QR workspace state
Evidence:
- `onTabChanged` calls `clearBarcodeFields` when `barcodeTab` is selected (`src/mainwindow.cpp:5819`).

Impact:
- Returning to QR tab resets current context unexpectedly for some workflows.

Recommendation:
- Clear only on explicit user action (`Clear Fields`) or only on first entry/new session.

### Finding 4 (Low): Print settings UI text conflicts with fixed constants
Evidence:
- `PrintSettings::kQrWidthMm` is 20.0 (`src/printsettingsdialog.h`), while tooltip says fixed 19 mm (`src/printsettingsdialog.cpp`).

Impact:
- Documentation/UI mismatch.

Recommendation:
- Align tooltip text with constants or constants with intended label specification.

## 9. Error Case Matrix (Operational)
### 9.1 SKU Create/Update
Triggers:
- Empty part name
- Non-positive or invalid weight
- Missing category/sub-category
- SKU duplication

System response:
- Operation aborted
- Status message set to error
- Audit record written for failure branches

### 9.2 QR Generate
Triggers:
- Missing SKU selection
- SKU not present in catalog
- Invalid quantity
- Insert failure mid-batch

System response:
- On validation failure: abort with status
- On insert failure: transaction rollback + failure audit

### 9.3 Serial Delete/Edit
Triggers:
- No selected rows
- Authorization denied
- SQL update failure

System response:
- Abort with message
- For SQL failures: rollback and audit
- For auth failure/cancel: `AUTHORIZATION_DENIED` audit

### 9.4 Exports
Triggers:
- Unauthorized action
- User cancels dialog
- Output file not writable
- No source QR values to print

System response:
- Access warning or status error
- No destructive side effects

## 10. Function Group Index
### 10.1 Startup and Environment
- `setupUi` (`src/mainwindow.cpp:2358`)
- `createMenusAndToolbars` (`src/mainwindow.cpp:2837`)
- `selectDatabaseOnStartup` (`src/mainwindow.cpp:3022`)
- `openDatabaseAt` (`src/mainwindow.cpp:3097`)

### 10.2 Security
- `promptLogin` (`src/mainwindow.cpp:812`)
- `showSettingsDialog` (`src/mainwindow.cpp:960`)
- `promptAdminAuthorization` (`src/mainwindow.cpp:1668`)
- `logAction` (`src/mainwindow.cpp:1733`)

### 10.3 Data Layer
- `ensureSchema` (`src/mainwindow.cpp:3923`)
- `loadRulesIfEmpty` (`src/mainwindow.cpp:4263`)
- `loadCatalogIfEmpty` (`src/mainwindow.cpp:4377`)

### 10.4 SKU
- `searchRecords` (`src/mainwindow.cpp:5173`)
- `saveForm` (`src/mainwindow.cpp:5369`)
- `updateSelected` (`src/mainwindow.cpp:6080`)
- `deleteSelectedSku` (`src/mainwindow.cpp:6264`)

### 10.5 QR
- `generateBarcodes` (`src/mainwindow.cpp:5525`)
- `printBarcodes` (`src/mainwindow.cpp:5829`)
- `deleteSelectedBarcodes` (`src/mainwindow.cpp:5641`)

### 10.6 History
- `loadBarcodeHistory` (`src/mainwindow_history.cpp:100`)
- `editSelectedHistoryBarcode` (`src/mainwindow_history.cpp:255`)
- `deleteSelectedHistoryBarcodes` (`src/mainwindow_history.cpp:433`)

### 10.7 Backup
- `backupDatabaseTo` (`src/mainwindow.cpp:3211`)
- `runAutomatedBackup` (`src/mainwindow.cpp:3429`)
- `pruneBackupRetention` (`src/mainwindow.cpp:3323`)

## 11. Suggested Refactor Directions
1. Split `MainWindow` into service classes:
- AuthService
- SkuService
- QrService
- BackupService
- AuditService

2. Replace manual SQL repetition with repository helpers.

3. Formalize transaction wrappers for create/update/delete workflows.

4. Add deterministic unit tests for:
- SKU/QR value composition
- permission matrix evaluation
- serial tracker updates

5. Add integration tests for:
- startup DB bootstrap
- soft-delete and active-view correctness
- backup scheduling and retention behavior

## 12. Conclusion
The system is functionally complete and operationally rich (SKU + QR + security + backup + audit).  
Primary logic risks are in permission consistency and period-selection handling rather than core CRUD robustness.  
Addressing the high/medium findings in Section 8 will improve predictability and reduce operator confusion.
