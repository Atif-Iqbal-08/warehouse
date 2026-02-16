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
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayIcon={app}\warehouse_sku_generator.exe
DisableDirPage=no
UsePreviousAppDir=no

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop icon"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\warehouse_sku_generator.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\warehouse_sku_generator.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\db"; ValueType: string; ValueName: "path"; ValueData: "{code:GetDbPath}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\log"; ValueType: string; ValueName: "path"; ValueData: "{code:GetLogPath}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\backup"; ValueType: string; ValueName: "path"; ValueData: "{code:GetBackupPath}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\bootstrap\\master_admin"; ValueType: string; ValueName: "full_name"; ValueData: "{code:GetMasterFullName}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\bootstrap\\master_admin"; ValueType: string; ValueName: "user_id"; ValueData: "{code:GetMasterUserId}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\bootstrap\\master_admin"; ValueType: string; ValueName: "email"; ValueData: "{code:GetMasterEmail}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\bootstrap\\master_admin"; ValueType: string; ValueName: "username"; ValueData: "{code:GetMasterUsername}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\bootstrap\\master_admin"; ValueType: string; ValueName: "password"; ValueData: "{code:GetMasterPassword}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\bootstrap\\developer_break_glass"; ValueType: string; ValueName: "username"; ValueData: "{code:GetDevUsername}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\\Skylark Drones\\Warehouse SKU Generator\\bootstrap\\developer_break_glass"; ValueType: string; ValueName: "password"; ValueData: "{code:GetDevPassword}"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\warehouse_sku_generator.exe"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[Code]
var
  DbDirPage: TInputDirWizardPage;
  LogDirPage: TInputDirWizardPage;
  BackupDirPage: TInputDirWizardPage;
  MasterPage: TInputQueryWizardPage;
  DevPage: TInputQueryWizardPage;

function IsStrongPassword(Value: string): Boolean; forward;

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

  LogDirPage := CreateInputDirPage(
    DbDirPage.ID,
    'Log File Location',
    'Choose where runtime logs should be stored',
    'Select a folder for app runtime logs (app_run.log).',
    False,
    '');
  LogDirPage.Add('Log folder:');
  LogDirPage.Values[0] := ExpandConstant('{userdocs}\\Warehouse SKU Generator\\logs');

  BackupDirPage := CreateInputDirPage(
    LogDirPage.ID,
    'Backup Location',
    'Provide required backup location',
    'Select a secure folder for automated encrypted backups. This field is required.',
    False,
    '');
  BackupDirPage.Add('Backup folder:');
  BackupDirPage.Values[0] := '';

  MasterPage := CreateInputQueryPage(
    BackupDirPage.ID,
    'Master Admin Setup',
    'Create Master Admin account',
    'Provide master admin details. Installation continues only after valid credentials are provided.');
  MasterPage.Add('Full Name:', False);
  MasterPage.Add('User ID:', False);
  MasterPage.Add('Email:', False);
  MasterPage.Add('Username:', False);
  MasterPage.Add('Password:', True);
  MasterPage.Add('Confirm Password:', True);
  MasterPage.Values[0] := 'Master Admin';
  MasterPage.Values[1] := 'MASTER-001';
  MasterPage.Values[2] := 'admin@localhost';
  MasterPage.Values[3] := 'master_admin';

  DevPage := CreateInputQueryPage(
    MasterPage.ID,
    'Developer Break-Glass (Optional)',
    'Optional emergency developer account',
    'Leave blank to disable. If set, username and password are both required.');
  DevPage.Add('Username:', False);
  DevPage.Add('Password:', True);
  DevPage.Add('Confirm Password:', True);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Path: string;
  Password: string;
  ConfirmPassword: string;
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

  if CurPageID = LogDirPage.ID then
  begin
    Path := Trim(LogDirPage.Values[0]);
    if Path = '' then
    begin
      MsgBox('Please choose a folder for runtime logs.', mbError, MB_OK);
      Result := False;
      exit;
    end;
    if not DirExists(Path) then
    begin
      if not CreateDir(Path) then
      begin
        MsgBox('Unable to create the selected log folder.', mbError, MB_OK);
        Result := False;
        exit;
      end;
    end;
  end;

  if CurPageID = MasterPage.ID then
  begin
    if (Trim(MasterPage.Values[0]) = '') or
       (Trim(MasterPage.Values[1]) = '') or
       (Trim(MasterPage.Values[2]) = '') or
       (Trim(MasterPage.Values[3]) = '') then
    begin
      MsgBox('All Master Admin fields are required.', mbError, MB_OK);
      Result := False;
      exit;
    end;

    Password := MasterPage.Values[4];
    ConfirmPassword := MasterPage.Values[5];
    if Password <> ConfirmPassword then
    begin
      MsgBox('Master Admin passwords do not match.', mbError, MB_OK);
      Result := False;
      exit;
    end;
    if not IsStrongPassword(Password) then
    begin
      MsgBox('Password must be 10+ chars and include upper, lower, number, and special character.', mbError, MB_OK);
      Result := False;
      exit;
    end;
  end;

  if CurPageID = DevPage.ID then
  begin
    if Trim(DevPage.Values[0]) <> '' then
    begin
      if (Trim(DevPage.Values[1]) = '') or (Trim(DevPage.Values[2]) = '') then
      begin
        MsgBox('Provide both password fields for break-glass user or leave all fields empty.', mbError, MB_OK);
        Result := False;
        exit;
      end;
      if DevPage.Values[1] <> DevPage.Values[2] then
      begin
        MsgBox('Developer break-glass passwords do not match.', mbError, MB_OK);
        Result := False;
        exit;
      end;
      if not IsStrongPassword(DevPage.Values[1]) then
      begin
        MsgBox('Break-glass password must meet password policy.', mbError, MB_OK);
        Result := False;
        exit;
      end;
    end
    else
    begin
      DevPage.Values[1] := '';
      DevPage.Values[2] := '';
    end;
  end;
end;

function GetDbPath(Param: string): string;
begin
  Result := AddBackslash(Trim(DbDirPage.Values[0])) + 'sku.db';
end;

function GetLogPath(Param: string): string;
begin
  Result := Trim(LogDirPage.Values[0]);
end;

function IsStrongPassword(Value: string): Boolean;
var
  I: Integer;
  HasUpper: Boolean;
  HasLower: Boolean;
  HasDigit: Boolean;
  HasSpecial: Boolean;
  Ch: Char;
begin
  HasUpper := False;
  HasLower := False;
  HasDigit := False;
  HasSpecial := False;
  if Length(Value) < 10 then
  begin
    Result := False;
    exit;
  end;

  for I := 1 to Length(Value) do
  begin
    Ch := Value[I];
    if (Ch >= 'A') and (Ch <= 'Z') then
      HasUpper := True
    else if (Ch >= 'a') and (Ch <= 'z') then
      HasLower := True
    else if (Ch >= '0') and (Ch <= '9') then
      HasDigit := True
    else
      HasSpecial := True;
  end;

  Result := HasUpper and HasLower and HasDigit and HasSpecial;
end;

function GetBackupPath(Param: string): string;
begin
  Result := AddBackslash(Trim(BackupDirPage.Values[0]));
end;

function GetMasterFullName(Param: string): string;
begin
  Result := Trim(MasterPage.Values[0]);
end;

function GetMasterUserId(Param: string): string;
begin
  Result := Trim(MasterPage.Values[1]);
end;

function GetMasterEmail(Param: string): string;
begin
  Result := Trim(MasterPage.Values[2]);
end;

function GetMasterUsername(Param: string): string;
begin
  Result := Trim(MasterPage.Values[3]);
end;

function GetMasterPassword(Param: string): string;
begin
  Result := MasterPage.Values[4];
end;

function GetDevUsername(Param: string): string;
begin
  Result := Trim(DevPage.Values[0]);
end;

function GetDevPassword(Param: string): string;
begin
  if Trim(DevPage.Values[0]) = '' then
    Result := ''
  else
    Result := DevPage.Values[1];
end;
