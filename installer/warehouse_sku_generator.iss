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

[Run]
Filename: "{app}\warehouse_sku_generator.exe"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[Code]
var
  DbDirPage: TInputDirWizardPage;

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
end;

function GetDbPath(Param: string): string;
begin
  Result := AddBackslash(Trim(DbDirPage.Values[0])) + 'sku.db';
end;
