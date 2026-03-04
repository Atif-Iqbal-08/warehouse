#ifndef AppName
#define AppName "Warehouse SKU Generator"
#endif

#ifndef AppVersion
#define AppVersion "1.0.0"
#endif

#ifndef Publisher
#define Publisher "Skylark Drones Pvt. Ltd"
#endif

#ifndef SourceDir
#define SourceDir "dist\\staging"
#endif

#ifndef OutputDir
#define OutputDir "dist"
#endif

#define AppNameSafe StringChange(AppName, " ", "_")

[Setup]
AppId={{9D4637A9-9F4B-4631-8A53-9483C0909F8E}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#Publisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir={#OutputDir}
OutputBaseFilename=warehouse_installer
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\warehouse_sku_generator.exe
PrivilegesRequired=admin
UsePreviousPrivileges=no
DisableDirPage=no
UsePreviousAppDir=no

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop icon"; GroupDescription: "Additional icons:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\warehouse_sku_generator.exe"; IconFilename: "{app}\Assets\app_icon.ico"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\warehouse_sku_generator.exe"; Tasks: desktopicon; IconFilename: "{app}\Assets\app_icon.ico"

[Registry]
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\db"; ValueType: string; ValueName: "path"; ValueData: "{code:GetDbPath}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\backup"; ValueType: string; ValueName: "path"; ValueData: "{code:GetBackupPath}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\bootstrap\\master_admin"; Flags: deletekey
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\bootstrap\\developer_break_glass"; Flags: deletekey

[Run]
Filename: "{app}\warehouse_sku_generator.exe"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[Code]
var
  DbDirPage: TInputDirWizardPage;
  BackupDirPage: TInputDirWizardPage;
  LogConsentPage: TInputOptionWizardPage;

procedure InitializeWizard();
begin
  DbDirPage := CreateInputDirPage(
    wpSelectDir,
    'Database Location',
    'Choose where to store the database',
    'Select a folder where the application database (sku.db) will be stored.',
    False,
    '');
  DbDirPage.Add('Database folder:');
  DbDirPage.Values[0] := ExpandConstant('{userdocs}\\Warehouse SKU Generator');

  BackupDirPage := CreateInputDirPage(
    DbDirPage.ID,
    'Backup Location',
    'Provide required backup location',
    'Select a secure folder for automated encrypted backups. This field is required.',
    False,
    '');
  BackupDirPage.Add('Backup folder:');
  BackupDirPage.Values[0] := '';

  LogConsentPage := CreateInputOptionPage(
    BackupDirPage.ID,
    'Runtime Log Permission',
    'Allow runtime log storage',
    'The application stores runtime logs for diagnostics and support troubleshooting.',
    False,
    False);
  LogConsentPage.Add('Allow storing logs at: ' + ExpandConstant('{localappdata}\\Warehouse SKU Logs'));
  LogConsentPage.Values[0] := True;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Path: string;
begin
  Result := True;
  if CurPageID = DbDirPage.ID then
  begin
    Path := Trim(DbDirPage.Values[0]);
    if Path = '' then
    begin
      MsgBox('Please choose a folder for the database.', mbError, MB_OK);
      Result := False;
      exit;
    end;
    if not DirExists(Path) then
    begin
      if not CreateDir(Path) then
      begin
        MsgBox('Unable to create the selected folder. Please choose another location.', mbError, MB_OK);
        Result := False;
        exit;
      end;
    end;
  end;

  if CurPageID = BackupDirPage.ID then
  begin
    Path := Trim(BackupDirPage.Values[0]);
    if Path = '' then
    begin
      MsgBox('Please choose a folder for backups.', mbError, MB_OK);
      Result := False;
      exit;
    end;
    if not DirExists(Path) then
    begin
      if not CreateDir(Path) then
      begin
        MsgBox('Unable to create the selected backup folder.', mbError, MB_OK);
        Result := False;
        exit;
      end;
    end;
  end;

  if CurPageID = LogConsentPage.ID then
  begin
    if not LogConsentPage.Values[0] then
    begin
      MsgBox('Runtime log storage permission is required to continue installation. Logs are stored per Windows user under Local AppData.', mbError, MB_OK);
      Result := False;
      exit;
    end;
  end;
end;

function GetDbPath(Param: string): string;
begin
  Result := AddBackslash(Trim(DbDirPage.Values[0])) + 'sku.db';
end;

function GetBackupPath(Param: string): string;
begin
  Result := AddBackslash(Trim(BackupDirPage.Values[0]));
end;
