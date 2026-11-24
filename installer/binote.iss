; Inno Setup script of Binote. Build: iscc installer\binote.iss  (writes dist\Binote-<version>-setup.exe)
; The folder bin\Release must hold a Release build (see docs\BUILD.md).

#define AppName "Binote"
#define AppVersion "1.0.0"
#define AppPublisher "Thomas Gourgnier"
#define AppURL "https://www.rebisway.com"

[Setup]
AppId={{B3A5F0C2-6D1E-4E57-9C41-7A2B8E3D5F10}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\binote.exe
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=Binote-{#AppVersion}-setup
Compression=lzma2/ultra
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
SetupIconFile=..\binote-app\icons\binote.ico

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "assoc"; Description: "Open .md and .txt files with Binote (Open with)"; GroupDescription: "File types:"; Flags: unchecked

[Files]
Source: "..\bin\Release\binote.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\bin\Release\dict\*"; DestDir: "{app}\dict"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\bin\Release\fonts\*"; DestDir: "{app}\fonts"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\bin\Release\icons\*"; DestDir: "{app}\icons"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\bin\Release\lang\*"; DestDir: "{app}\lang"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\bin\Release\lexicon\*"; DestDir: "{app}\lexicon"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\bin\Release\scripts\*"; DestDir: "{app}\scripts"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\bin\Release\grammar-engine\*"; DestDir: "{app}\grammar-engine"; Excludes: "__pycache__"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\binote.exe"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\binote.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\Applications\binote.exe\shell\open\command"; ValueType: string; ValueData: """{app}\binote.exe"" ""%1"""; Tasks: assoc; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\.md\OpenWithList\binote.exe"; ValueType: none; Tasks: assoc; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\.txt\OpenWithList\binote.exe"; ValueType: none; Tasks: assoc; Flags: uninsdeletekey

[Run]
Filename: "{app}\binote.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
