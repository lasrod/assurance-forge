{ Assurance Forge installer code. Included in the [Code] section of the script
  CPack writes (CPACK_INNOSETUP_CODE_FILES); installer.iss holds the messages
  and [Run] entries that use it. }

const
  CredTypeGeneric = 1;

var
  WasInstalled: Boolean;

{ The application keeps its OpenAI API key in the Windows Credential Manager
  under "AssuranceForge:openai" (kSecretServiceName and kOpenAiSecretAccount in
  src/ai/ai_types.h). A new stored secret needs a line in RemoveUserData. }
function CredDeleteW(TargetName: String; CredType: Cardinal; Flags: Cardinal): Boolean;
  external 'CredDeleteW@advapi32.dll stdcall uninstallonly';

function UninstallRegistryKey(): String;
begin
  Result := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\' + '{' + '{#AfAppGuid}' + '}' + '_is1';
end;

function InitializeSetup(): Boolean;
begin
  WasInstalled := RegKeyExists(HKCU, UninstallRegistryKey()) or RegKeyExists(HKLM, UninstallRegistryKey());
  if WasInstalled then
    Log('Assurance Forge: upgrading an existing installation')
  else
    Log('Assurance Forge: first installation');
  Result := True;
end;

function IsUpgrade(): Boolean;
begin
  Result := WasInstalled;
end;

function IsFreshInstall(): Boolean;
begin
  Result := not WasInstalled;
end;

{ Settings (settings.json, hello_imgui.ini) live in %APPDATA%\AssuranceForge,
  running-instance records in %LOCALAPPDATA%\AssuranceForge, and the API key in
  the Credential Manager. Projects are never here. }
procedure RemoveUserData();
begin
  DelTree(ExpandConstant('{userappdata}\AssuranceForge'), True, True, True);
  DelTree(ExpandConstant('{localappdata}\AssuranceForge'), True, True, True);
  CredDeleteW('AssuranceForge:openai', CredTypeGeneric, 0);
end;

{ Keeping is the first button and therefore the default: an uninstall that
  also discards someone's setup should take a deliberate choice. A silent
  uninstall keeps everything. (The labels are built before the call because
  Inno Setup reads any line that starts with a bracket as a section header.) }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Labels: TArrayOfString;
begin
  if (CurUninstallStep <> usPostUninstall) or UninstallSilent() then
    Exit;
  SetArrayLength(Labels, 2);
  Labels[0] := CustomMessage('KeepUserDataKeep');
  Labels[1] := CustomMessage('KeepUserDataRemove');
  if TaskDialogMsgBox(CustomMessage('KeepUserDataInstruction'), CustomMessage('KeepUserDataText'),
                      mbConfirmation, MB_YESNO, Labels, 0) = IDNO then
    RemoveUserData();
end;
