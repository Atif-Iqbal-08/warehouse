#ifndef AppName
#define AppName "Warehouse SKU Generator"
#endif

#ifndef AppVersion
#define AppVersion "1.1.0"
#endif

#ifndef Publisher
#define Publisher "Skylark Drones Pvt. Ltd"
#endif

#ifndef BootstrapFileName
#define BootstrapFileName "installer_bootstrap.ini"
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
; In-place upgrade support: when the same AppId is already installed the
; directory/start-menu pages are hidden (auto), the previous location, tasks
; and settings are reused, and a running app instance is closed via the
; Windows Restart Manager instead of forcing a manual uninstall.
DisableDirPage=auto
DisableProgramGroupPage=auto
UsePreviousAppDir=yes
UsePreviousGroup=yes
UsePreviousTasks=yes
CloseApplications=yes
RestartApplications=no
SetupMutex=WarehouseSKUGeneratorSetupMutex
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} Setup

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop icon"; GroupDescription: "Additional icons:"

[Dirs]
Name: "{commonappdata}\{#AppName}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs

[INI]
Filename: "{commonappdata}\{#AppName}\{#BootstrapFileName}"; Section: "bootstrap"; Key: "db_path"; String: "{code:GetDbPath}"
Filename: "{commonappdata}\{#AppName}\{#BootstrapFileName}"; Section: "bootstrap"; Key: "backup_path"; String: "{code:GetBackupPath}"
Filename: "{commonappdata}\{#AppName}\{#BootstrapFileName}"; Section: "bootstrap"; Key: "log_consent"; String: "{code:GetLogConsentValue}"

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\warehouse_sku_generator.exe"; IconFilename: "{app}\Assets\app_icon.ico"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\warehouse_sku_generator.exe"; Tasks: desktopicon; IconFilename: "{app}\Assets\app_icon.ico"

[Run]
Filename: "{app}\warehouse_sku_generator.exe"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[Code]
var
  DbDirPage: TInputDirWizardPage;
  BackupDirPage: TInputDirWizardPage;
  LogConsentPage: TInputOptionWizardPage;

function BootstrapConfigPath(): string;
begin
  Result := ExpandConstant('{commonappdata}\{#AppName}\{#BootstrapFileName}');
end;

function NormalizePathValue(Value: string): string;
begin
  Result := RemoveBackslashUnlessRoot(Trim(Value));
end;

function ReadBootstrapValue(const KeyName: string): string;
begin
  Result := Trim(GetIniString('bootstrap', KeyName, '', BootstrapConfigPath()));
end;

function DefaultLogConsentValue(): Boolean;
var
  ExistingConsent: string;
begin
  ExistingConsent := Lowercase(ReadBootstrapValue('log_consent'));
  if ExistingConsent = '' then
    Result := True
  else
    Result := (ExistingConsent = '1') or (ExistingConsent = 'true') or (ExistingConsent = 'yes');
end;

function HasExistingBootstrapConfig(): Boolean;
begin
  Result := (ReadBootstrapValue('db_path') <> '') and (ReadBootstrapValue('backup_path') <> '');
end;

function HasExistingInstallAtPath(const Value: string): Boolean;
var
  InstallDir: string;
begin
  InstallDir := NormalizePathValue(Value);
  Result := (InstallDir <> '') and FileExists(AddBackslash(InstallDir) + 'warehouse_sku_generator.exe');
end;

function IsSameLocationUpgrade(): Boolean;
begin
  Result := HasExistingBootstrapConfig() and HasExistingInstallAtPath(WizardDirValue());
end;

function DefaultDbDirValue(): string;
var
  ExistingDbPath: string;
begin
  ExistingDbPath := ReadBootstrapValue('db_path');
  if ExistingDbPath <> '' then
    Result := ExtractFileDir(ExistingDbPath)
  else
    Result := ExpandConstant('{userdocs}\\Warehouse SKU Generator');

  Result := NormalizePathValue(Result);
end;

function DefaultBackupDirValue(): string;
begin
  Result := ReadBootstrapValue('backup_path');
  if Result <> '' then
    Result := NormalizePathValue(Result)
  else
    Result := '';
end;

function GetLogConsentValue(Param: string): string;
begin
  if LogConsentPage.Values[0] then
    Result := 'true'
  else
    Result := 'false';
end;

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
  DbDirPage.Values[0] := DefaultDbDirValue();

  BackupDirPage := CreateInputDirPage(
    DbDirPage.ID,
    'Backup Location',
    'Provide required backup location',
    'Select a secure folder for automated encrypted backups. This field is required.',
    False,
    '');
  BackupDirPage.Add('Backup folder:');
  BackupDirPage.Values[0] := DefaultBackupDirValue();

  LogConsentPage := CreateInputOptionPage(
    BackupDirPage.ID,
    'Runtime Log Permission',
    'Allow runtime log storage',
    'The application stores runtime logs for diagnostics and support troubleshooting.',
    False,
    False);
  LogConsentPage.Add('Allow storing logs at: ' + ExpandConstant('{localappdata}\\Warehouse SKU Logs'));
  LogConsentPage.Values[0] := DefaultLogConsentValue();
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if IsSameLocationUpgrade() and
     ((PageID = DbDirPage.ID) or (PageID = BackupDirPage.ID) or (PageID = LogConsentPage.ID)) then
    Result := True;
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
