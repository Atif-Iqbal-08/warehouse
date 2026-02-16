# Warehouse SKU and QR Code Manager
Professional User and Admin Documentation

Document date: 2026-02-16  
Application: Warehouse SKU and QR Code Manager  
Organization: Skylark Drones Pvt. Ltd

## 1. Purpose and Scope
This document explains all major features, controls, buttons, and operational workflows in the Warehouse SKU and QR Code Manager desktop application.

It is written for:
- Data entry operators
- Warehouse supervisors
- Administrators who manage users, backups, and exports

## 2. System Overview
The application combines SKU catalog management, QR code lifecycle management, export workflows, and role-based security in one interface.

Main capabilities:
- SKU generation using category, sub-category, serial, and variation logic
- SKU master data create/read/update/delete workflows
- Product image upload and preview
- QR code generation with serial tracking
- PDF label export for generated QR codes
- QR history review, serial edits, and serial deletes
- CSV exports for SKU master and QR summary
- Database load/export/save workflows
- User login, role-based permissions, and custom access overrides
- Audit logging for sensitive actions
- Automated encrypted backup scheduling and retention pruning

## 3. Access and Security
### 3.1 First Run and Login
On first run (or with a new database path), the application:
- Prompts for a database folder
- Creates or opens `sku.db`
- Ensures schema and default roles
- Prompts to create a Master Admin account if no user exists
- Requires login before normal operations

If no saved database is available at startup, the prompt provides:
- `Create New DB`
- `Skip This Time` (opens UI without loading a database)
- `Exit`

### 3.2 Password Policy
User passwords must satisfy all rules:
- Minimum 10 characters
- At least one uppercase letter
- At least one lowercase letter
- At least one number
- At least one special character

### 3.3 Default Role Behavior
Base roles are:
- View Only (Print): read/search/export/print, no add/edit/delete
- Add Data (No Delete): add and edit allowed, delete and serial management restricted
- Full Access: all permissions enabled

Additional seeded roles exist in defaults:
- Master Admin
- Supervisor
- Operator
- Viewer

Custom roles can be created from the Security settings dialog, and per-user overrides can be applied.

### 3.4 Actions Requiring Admin Re-Authorization
The application prompts for admin credentials and reason text (minimum 20 characters) before sensitive actions:
- Delete SKU
- Delete serial numbers (QR rows)
- Edit existing serial history rows

## 4. Interface Structure
The UI has four main tabs, plus a Help menu in the menu bar.

### 4.1 Dashboard Tab
Purpose:
- High-level operational snapshot
- Quick backup trigger

Includes:
- Warehouse metrics cards (Total SKUs, Total Serial Numbers, This Quarter Incoming)
- Database group with backup button
- Latest SKU cards with key details and image previews

### 4.2 SKU Generation Tab
Purpose:
- Search and maintain SKU catalog records
- Prepare SKU data for QR operations

Areas:
- Search panel (SKU/Part Number/Part Name)
- Results table
- Image preview
- SKU form (all product fields)

### 4.3 QR Code Generation Tab
Purpose:
- Generate QR code batches for selected SKU
- Export generated labels to PDF

Areas:
- Generation form
- SKU detail panel
- Generated QR table and QR preview

### 4.4 QR Code Manager Tab
Purpose:
- Review historical/generated serial records
- Filter by SKU and serial
- Edit or delete selected serial rows

Areas:
- SKU selector and serial search
- History table
- SKU details panel
- QR details panel

### 4.5 Help Menu
Purpose:
- Provide in-app documentation access from the menu bar.

Areas:
- Menu path: `Help -> User and Technical Guides...`
- Popup dialog with:
  - User Guide viewer
  - Technical Guide viewer

## 5. Complete Button Reference
### 5.1 Dashboard
Button: `Backup DB`
- Function: Opens export backup flow for database copy.
- Use: Click and select output path.
- Permission: Backup/Restore permission required.

### 5.2 SKU Generation - Search Panel
Button: `Search`
- Function: Runs SKU search using SKU, part number, and part name filters.
- Use: Fill one or more search fields, then click.

Button: `Clear Search`
- Function: Clears search inputs, resets selected row/image, reloads records.
- Use: Click when starting a new lookup.

Button: `Fill From Search`
- Function: Loads the selected search result into the SKU form for editing/review.
- Use: Select a row first, then click.

### 5.3 SKU Generation - Form Panel
Button: `Browse`
- Function: Selects product image file and stores image bytes in DB-backed form state.
- Use: Click, pick image file, verify preview.

Button: `Clear Forms`
- Function: Clears all form fields and image, resets preview/status helpers.
- Use: Click before entering a new item or to cancel edits.

Button: `Generate SKU and Update List`
- Function: Validates required fields and creates new SKU catalog record.
- Use: Complete required fields, then click.
- Permission: Add permission required.

Button: `Update Selected Record`
- Function: Updates selected existing SKU record with current form values.
- Use: Search, select row, fill form, then click.
- Permission: Edit permission required.

Button: `Delete SKU`
- Function: Soft-deletes SKU and related serial records.
- Use: Select existing row, click, confirm, then authorize as admin.
- Permission: Delete permission required plus admin re-authorization.

### 5.4 QR Code Generation Tab
Button: `Generate QR Codes`
- Function: Creates batch of QR records for selected SKU and quantity.
- Use: Choose SKU, prefix, quantity, then click.
- Permission: Add permission required.

Button: `Clear Fields`
- Function: Clears QR generation controls, preview, and temporary generated list.
- Use: Click when switching SKU/task context.

Button: `Export QR Codes (PDF)`
- Function: Exports last generated set (or current last code) into PDF label pages.
- Use: Generate first, then click and choose output file.
- Permission: Print permission required.

Button: `Delete Selected Serials`
- Function: Soft-deletes selected serial rows from QR table.
- Use: Select rows, click, confirm, and authorize.
- Permission: Serial delete permission required plus admin re-authorization.

### 5.5 QR Code Manager Tab
Button: `Load History`
- Function: Loads serial history for selected SKU.
- Use: Select SKU then click.

Button: `Edit Selected Serial`
- Function: Opens editor for serial/quarter/year/date and recomputed QR value.
- Use: Select a history row then click.
- Permission: Serial edit permission required plus admin re-authorization.

Button: `Delete Selected Serials`
- Function: Soft-deletes selected history rows.
- Use: Multi-select rows, click, confirm, authorize.
- Permission: Serial delete permission required plus admin re-authorization.

Button: `Clear Fields`
- Function: Clears history tab filters, selection, and detail panels.
- Use: Click to reset history workspace.

## 6. Menu Action Reference
### 6.1 File Menu
Action: `Load DB...`
- Opens an existing database file.
- Permission: Backup/Restore required.

Action: `Export DB...`
- Saves a copy of current database to user-selected path.
- Permission: Backup/Restore required.

Action: `Save DB`
- Creates backup using default backup path and timestamp.
- Permission: Backup/Restore required.

Action: `Export SKU Master (CSV)...`
- Exports active SKU catalog to CSV with timestamped filename.
- Permission: Export required.

Action: `Export QR Summary (CSV)...`
- Exports grouped QR counts by SKU/year/quarter.
- Permission: Export required.

Action: `Exit`
- Closes application.

### 6.2 View Menu
Action: `Full Screen`
- Toggles full-screen display mode.

Action Group: `Theme`
- `Dark`
- `Light`
- `System Default`

### 6.3 Options Menu
Action: `QR Code / Sticker Settings...`
- Opens print settings dialog.
- Current implementation keeps sticker width/height and QR size fixed; inner margin is adjustable.

### 6.4 Security Menu
Action: `User Access Control...`
- Opens role and user management dialog.
- Permission: Manage Users required.

Action: `Switch User...`
- Opens login dialog to switch active account.

## 7. Additional Interaction Features
### 7.1 Search Result Context Menu
Right-click in SKU results table to access:
- Copy Cell
- Copy Row
- Copy Column

### 7.2 Status Messaging
Bottom status label messages are color-coded:
- Green for successful operations
- Red for validation/operation errors

### 7.3 Auto Logging
UI button presses, action triggers, and tab switches are logged with current user/module context.

## 8. Data Rules and Generation Logic
### 8.1 SKU Format
SKU is computed as:
- `category_digit + subcategory_digit + item_serial(3-digit padded) + variation_code`

Variation code mapping:
- 1 to 9 -> `1` to `9`
- 10 to 35 -> `A` to `Z`

### 8.2 QR Code Format
QR value is computed as:
- `prefix + sku + quarter + year_2digits + serial_5digits`

Prefix options:
- `SD` (Skylark Drones branding)
- `SK` (Skykart branding)

Example pattern:
- `SK<SKU>QYYSSSSS` in concatenated numeric/text form used by the app.

### 8.3 Fiscal Quarter and Year Logic
Current fiscal mapping:
- Q1 = Apr-Jun
- Q2 = Jul-Sep
- Q3 = Oct-Dec
- Q4 = Jan-Mar

Fiscal year:
- If month is Apr-Dec: current calendar year
- If month is Jan-Mar: previous calendar year

Note:
- `Next Serial` tracker uses selected quarter/year controls.
- Generation logic uses selected quarter/year controls in the QR generation form.

### 8.4 Required Validation Before SKU Save/Update
Required:
- Part Name
- Product Weight (kg) as positive numeric value
- Valid category and sub-category selections

Duplicate protection:
- Save blocks duplicate SKU.
- Update blocks collisions with other record IDs.

## 9. Standard Operating Procedures
### 9.1 Create New SKU
1. Open `SKU Generation` tab.
2. Fill product fields.
3. Confirm generated SKU preview.
4. Add image (optional) using `Browse`.
5. Click `Generate SKU and Update List`.
6. Verify record appears in search and SKU lists.

### 9.2 Update Existing SKU
1. Search record in search panel.
2. Select result row.
3. Click `Fill From Search`.
4. Edit fields.
5. Click `Update Selected Record`.

### 9.3 Delete SKU
1. Search and select target SKU.
2. Click `Delete SKU`.
3. Confirm delete prompt.
4. Provide admin authorization and reason.

### 9.4 Generate QR Codes
1. Open `QR Code Generation`.
2. Select SKU.
3. Select prefix and quantity.
4. Click `Generate QR Codes`.
5. Verify rows, QR preview, and next serial.

### 9.5 Export QR Labels PDF
1. Generate QR codes (or ensure current code exists in field).
2. Click `Export QR Codes (PDF)`.
3. Choose filename/path.
4. Verify output PDF.

### 9.6 Edit or Delete Existing Serials
1. Open `QR Code Manager`.
2. Select SKU and optionally apply serial search filter.
3. Click `Load History`.
4. Select row(s).
5. Use `Edit Selected Serial` or `Delete Selected Serials`.
6. Complete admin authorization when prompted.

### 9.7 Export CSV Reports
1. Use `File` menu.
2. Select `Export SKU Master (CSV)...` or `Export QR Summary (CSV)...`.
3. Choose output path.

### 9.8 Database Backup / Export / Load
Backup and export:
1. Use `Backup DB` or `File -> Export DB...`.
2. Select destination.

Load:
1. Use `File -> Load DB...`.
2. Select database file.
3. Re-login if prompted.

### 9.9 Manage Users and Roles
1. Open `Security -> User Access Control...`.
2. In `Roles`, add or update role permissions.
3. In `Users`, create accounts and assign role.
4. Optionally apply per-user permission override.

## 10. Backup and Retention Behavior
### 10.1 Manual Backup
Manual backup/export operations copy database to chosen `.db` path.

### 10.2 Automated Backup
Automated backup writes encrypted `.encdb` files under configured backup root.

Backup timing behavior:
- Uses evening window around 17:30 to 19:30 local time
- Randomized offset within the window
- Fallback run if missed

Retention behavior:
- Keep all backups from last 30 days
- For older backups, keep representative weekly and monthly snapshots

## 11. Paths, Files, and Imports
### 11.1 Runtime Paths
App data root:
- `QStandardPaths::AppDataLocation` (platform-resolved)

Data folder:
- `<AppDataLocation>/data`

Database file:
- `<selected_or_saved_path>/sku.db`

Images folder:
- `<AppDataLocation>/data/images`

Runtime log file:
- `<configured_log_path>/app_run.log`
- If not configured, defaults to `<AppDataLocation>/data/app_run.log`

Default backup root:
- `<AppDataLocation>/backups` (unless overridden by settings)

### 11.2 Seed/Input Files
Rules seed CSV:
- `Assets/sku_rules.csv`

Optional existing catalog CSV:
- `Assets/EXISTING sku DATA.csv`

### 11.3 Import Conditions
- SKU rules are loaded when `sku_rules` table is empty.
- Existing catalog CSV is imported once for initial setup and tracked via metadata/flag.

## 12. Known Operational Notes
- Deletions are soft deletes (`is_deleted` flags), with audit metadata.
- History and generation serial trackers refresh after edits/deletes.
- QR print settings dialog currently allows practical adjustment of inner margin only.
- Selecting the QR generation tab clears QR fields by design.

## 13. Troubleshooting
Issue: Cannot open/create DB in selected folder.
- Cause: Permission restrictions.
- Action: Choose a writable folder or relaunch elevated when prompted.

Issue: Save/Update blocked with validation errors.
- Cause: Missing Part Name, invalid weight, or invalid category/sub-category.
- Action: Complete required fields and retry.

Issue: QR generation blocked.
- Cause: Invalid SKU selection or invalid quantity.
- Action: Select valid SKU and positive integer quantity.

Issue: Sensitive action denied.
- Cause: Missing permission or failed admin authorization.
- Action: Use authorized account with required permissions.

Issue: Export unavailable.
- Cause: Role does not include export or print capability.
- Action: Review role/override settings in User Access Control.

## 14. Recommended Operating Practices
- Keep role assignments minimal by responsibility.
- Use custom permission overrides only when required.
- Record clear reasons for admin-authorized destructive operations.
- Run periodic manual export backups in addition to automated backups.
- Validate CSV exports after major batch operations.
