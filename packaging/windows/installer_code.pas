{ Assurance Forge installer code. Included in the [Code] section of the script
  CPack writes (CPACK_INNOSETUP_CODE_FILES); installer.iss holds the pages,
  messages and [Run] entries that use it.

  Inno Setup reads any line that starts with a bracket as a section header, even
  in [Code], so no statement here may begin with one. }

const
  CredTypeGeneric = 1;
  McpServerName = 'assurance-forge';
  InstallerRegistryKey = 'Software\Assurance Forge\Installer';

var
  WasInstalled: Boolean;
  ClaudeCodePath: String;
  CodexPath: String;
  ClientsConnected: String;
  ClientsFailed: String;
  ComponentsNote: TNewStaticText;

{ The application keeps its OpenAI API key in the Windows Credential Manager
  under "AssuranceForge:openai" (kSecretServiceName and kOpenAiSecretAccount in
  src/ai/ai_types.h). A new stored secret needs a line in RemoveUserData. }
function CredDeleteW(TargetName: String; CredType: Cardinal; Flags: Cardinal): Boolean;
  external 'CredDeleteW@advapi32.dll stdcall uninstallonly';

function UninstallRegistryKey(): String;
begin
  Result := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\' + '{' + '{#AfAppGuid}' + '}' + '_is1';
end;

{ The first of Names found on PATH, as a full path, or '' if none is. npm
  installs a client as a .cmd shim, the native installers as an .exe. }
function FindOnPath(const Names: String): String;
var
  Remaining, Name: String;
  Separator: Integer;
begin
  Result := '';
  Remaining := Names;
  while (Result = '') and (Remaining <> '') do
  begin
    Separator := Pos(',', Remaining);
    if Separator = 0 then
    begin
      Name := Remaining;
      Remaining := '';
    end
    else
    begin
      Name := Copy(Remaining, 1, Separator - 1);
      Remaining := Copy(Remaining, Separator + 1, Length(Remaining));
    end;
    Result := FileSearch(Name, GetEnv('PATH'));
  end;
end;

function InitializeSetup(): Boolean;
begin
  WasInstalled := RegKeyExists(HKCU, UninstallRegistryKey()) or RegKeyExists(HKLM, UninstallRegistryKey());
  if WasInstalled then
    Log('Assurance Forge: upgrading an existing installation')
  else
    Log('Assurance Forge: first installation');
  ClaudeCodePath := FindOnPath('claude.exe,claude.cmd');
  CodexPath := FindOnPath('codex.exe,codex.cmd');
  Log('Assurance Forge: Claude Code ' + ClaudeCodePath + ', Codex ' + CodexPath);
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

function ClaudeCodeFound(): Boolean;
begin
  Result := ClaudeCodePath <> '';
end;

function CodexFound(): Boolean;
begin
  Result := CodexPath <> '';
end;

{ Runs a program as the user who started Setup: an all-users install runs
  elevated, but a client's configuration belongs to the user. Uninstall may not
  call ExecAsOriginalUser, and has no original user to return to. }
function ExecAsUser(const Filename, Parameters, WorkingDir: String; var ResultCode: Integer): Boolean;
begin
  if IsUninstaller() then
    Result := Exec(Filename, Parameters, WorkingDir, SW_HIDE, ewWaitUntilTerminated, ResultCode)
  else
    Result := ExecAsOriginalUser(Filename, Parameters, WorkingDir, SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

{ Runs a client's CLI. A .cmd shim has to go through cmd.exe. Run from the
  user's profile folder: the CLIs also see servers scoped to the project in the
  working directory, and wherever Setup happened to be started from is no
  business of the registration. Returns whether it exited 0. }
function RunClient(const ClientPath, Parameters: String): Boolean;
var
  ResultCode: Integer;
  Launched: Boolean;
  WorkingDir: String;
begin
  Log('Assurance Forge: ' + ClientPath + ' ' + Parameters);
  WorkingDir := GetEnv('USERPROFILE');
  if CompareText(ExtractFileExt(ClientPath), '.exe') = 0 then
    Launched := ExecAsUser(ClientPath, Parameters, WorkingDir, ResultCode)
  else
    Launched := ExecAsUser(ExpandConstant('{cmd}'), '/C ""' + ClientPath + '" ' + Parameters + '"', WorkingDir,
                           ResultCode);
  Result := Launched and (ResultCode = 0);
  Log('Assurance Forge: exit code ' + IntToStr(ResultCode));
end;

function McpServerPath(): String;
begin
  Result := ExpandConstant('{app}\assurance-forge-mcp.exe');
end;

{ Registered at user scope with no arguments: the server finds the running
  application by itself and stays unbound until a project is open, MCP is
  switched on in Preferences, and the user allows the client.

  An "assurance-forge" server the client already has is left exactly as it is:
  it may be one the user configured by hand (a development build, a project
  argument), and an installer has no business overwriting that. Only a server
  this installer added is recorded, and only a recorded one is removed again on
  uninstall. An upgrade finds its own earlier entry and leaves it too. }
function RegisterWithClient(const ClientPath, Scope, MarkerName: String): Boolean;
begin
  if RunClient(ClientPath, 'mcp get ' + McpServerName) then
  begin
    Log('Assurance Forge: the client already has an ' + McpServerName + ' server; leaving it unchanged');
    Result := True;
    Exit;
  end;
  Result := RunClient(ClientPath, 'mcp add ' + Scope + McpServerName + ' -- "' + McpServerPath() + '"');
  if Result then
    RegWriteDWordValue(HKA, InstallerRegistryKey, MarkerName, 1);
end;

function RegisterWithClaudeCode(): Boolean;
begin
  Result := RegisterWithClient(ClaudeCodePath, '--scope user ', 'RegisteredClaudeCode');
end;

function RegisterWithCodex(): Boolean;
begin
  Result := RegisterWithClient(CodexPath, '', 'RegisteredCodex');
end;

function JoinClients(const Clients, Client: String): String;
begin
  if Clients = '' then
    Result := Client
  else
    Result := FmtMessage(CustomMessage('ClientsBoth'), [Clients, Client]);
end;

procedure NoteClient(const Client: String; Connected: Boolean);
begin
  if Connected then
    ClientsConnected := JoinClients(ClientsConnected, Client)
  else
    ClientsFailed := JoinClients(ClientsFailed, Client);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then
    Exit;
  if WizardIsTaskSelected('mcpclaudecode') then
    NoteClient('Claude Code', RegisterWithClaudeCode());
  if WizardIsTaskSelected('mcpcodex') then
    NoteClient('Codex', RegisterWithCodex());
end;

{ The components list shows what is built in; the note under it says the two
  things a user has to do themselves before the AI features work. }
procedure InitializeWizard();
var
  List: TNewCheckListBox;
begin
  List := WizardForm.ComponentsList;
  ComponentsNote := TNewStaticText.Create(WizardForm);
  ComponentsNote.Parent := WizardForm.SelectComponentsPage;
  ComponentsNote.AutoSize := False;
  ComponentsNote.WordWrap := True;
  ComponentsNote.Left := List.Left;
  ComponentsNote.Width := List.Width;
  ComponentsNote.Caption := CustomMessage('ComponentsNote');
  ComponentsNote.Height := ScaleY(48);
  WizardForm.AdjustLabelHeight(ComponentsNote);
  ComponentsNote.Top := List.Top + List.Height - ComponentsNote.Height;
  List.Height := List.Height - ComponentsNote.Height - ScaleY(8);
end;

{ The finish text depends on what was chosen: the samples tip only when the
  samples were installed, and one line about the AI assistant when the MCP
  server was. }
procedure CurPageChanged(CurPageID: Integer);
var
  Text: String;
begin
  if CurPageID <> wpFinished then
    Exit;
  Text := CustomMessage('FinishReady');
  if WizardIsComponentSelected('samples') then
    Text := Text + #13#10#13#10 + CustomMessage('FinishSamples');
  if ClientsConnected <> '' then
    Text := Text + #13#10#13#10 + FmtMessage(CustomMessage('FinishMcpConnected'), [ClientsConnected]);
  if ClientsFailed <> '' then
    Text := Text + #13#10#13#10 + FmtMessage(CustomMessage('FinishMcpFailed'), [ClientsFailed]);
  if WizardIsComponentSelected('mcp') and (ClientsConnected = '') and (ClientsFailed = '') then
    Text := Text + #13#10#13#10 + CustomMessage('FinishMcpManual');
  WizardForm.FinishedLabel.Caption := Text;
  WizardForm.AdjustLabelHeight(WizardForm.FinishedLabel);
  WizardForm.RunList.Top := WizardForm.FinishedLabel.Top + WizardForm.FinishedLabel.Height + ScaleY(12);
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

function WasRegistered(const MarkerName: String): Boolean;
var
  Value: Cardinal;
begin
  Result := RegQueryDWordValue(HKA, InstallerRegistryKey, MarkerName, Value) and (Value = 1);
end;

{ Removes the server from a client only if this installer added it, looking the
  client up again now: it may have moved or gone since. }
procedure UnregisterClient(const Names, Scope, MarkerName: String);
var
  ClientPath: String;
begin
  if not WasRegistered(MarkerName) then
    Exit;
  ClientPath := FindOnPath(Names);
  if ClientPath <> '' then
    RunClient(ClientPath, 'mcp remove ' + Scope + McpServerName);
  RegDeleteValue(HKA, InstallerRegistryKey, MarkerName);
end;

procedure UnregisterClients();
begin
  UnregisterClient('claude.exe,claude.cmd', '--scope user ', 'RegisteredClaudeCode');
  UnregisterClient('codex.exe,codex.cmd', '', 'RegisteredCodex');
  RegDeleteKeyIfEmpty(HKA, InstallerRegistryKey);
  RegDeleteKeyIfEmpty(HKA, 'Software\Assurance Forge');
end;

{ Keeping is the first button and therefore the default: an uninstall that
  also discards someone's setup should take a deliberate choice. A silent
  uninstall keeps everything. }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Labels: TArrayOfString;
begin
  if CurUninstallStep = usUninstall then
    UnregisterClients();
  if (CurUninstallStep <> usPostUninstall) or UninstallSilent() then
    Exit;
  SetArrayLength(Labels, 2);
  Labels[0] := CustomMessage('KeepUserDataKeep');
  Labels[1] := CustomMessage('KeepUserDataRemove');
  if TaskDialogMsgBox(CustomMessage('KeepUserDataInstruction'), CustomMessage('KeepUserDataText'),
                      mbConfirmation, MB_YESNO, Labels, 0) = IDNO then
    RemoveUserData();
end;
