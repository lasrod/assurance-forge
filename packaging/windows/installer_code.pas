{ Assurance Forge installer code. Included in the [Code] section of the script
  CPack writes (CPACK_INNOSETUP_CODE_FILES); installer.iss holds the messages,
  components and [Run] entries that use it.

  Two pages are built here: "A quick look" (a screenshot and what the tool
  does, on a first install) and "AI assistance", which puts the two kinds of AI
  side by side because they are easy to confuse -- the built-in review needs
  the user's own API key and installs nothing, while the MCP server lets the
  user's own assistant work with the app and needs no key.

  Command line, for silent installs: /MCP=0 leaves the MCP server out (it is
  installed by default), and /CONNECT=claudecode,codex connects the named
  clients when they are found.

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
  TourPage: TWizardPage;
  AiPage: TWizardPage;
  InstallMcpBox: TNewCheckBox;
  ClaudeCodeBox: TNewCheckBox;
  CodexBox: TNewCheckBox;

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

{ [Files] and [InstallDelete] ask these. }
function ShouldInstallMcp(): Boolean;
begin
  Result := InstallMcpBox.Checked;
end;

function ShouldRemoveMcp(): Boolean;
begin
  Result := not InstallMcpBox.Checked;
end;

{ --- Running a client's CLI ------------------------------------------------ }

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
  this installer added is recorded, with the path it launches, and only an
  entry that still launches that path is removed again on uninstall. An upgrade
  finds its own earlier entry and leaves it too. }
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
    RegWriteStringValue(HKA, InstallerRegistryKey, MarkerName, McpServerPath());
end;

{ Whether the client's "assurance-forge" entry still launches ServerPath, the
  server this installer registered. The user may have edited it, or removed it
  and made their own under the same name since; either way it is theirs now. }
function ClientEntryLaunches(const ClientPath, ServerPath: String): Boolean;
var
  Filename, Parameters, Text: String;
  ResultCode, Line: Integer;
  Output: TExecOutput;
begin
  Result := False;
  Parameters := 'mcp get ' + McpServerName;
  if CompareText(ExtractFileExt(ClientPath), '.exe') = 0 then
    Filename := ClientPath
  else
  begin
    Filename := ExpandConstant('{cmd}');
    Parameters := '/C ""' + ClientPath + '" ' + Parameters + '"';
  end;
  try
    if not ExecAndCaptureOutput(Filename, Parameters, GetEnv('USERPROFILE'), SW_HIDE, ewWaitUntilTerminated,
                                ResultCode, Output) or (ResultCode <> 0) then
      Exit;
  except
    Log('Assurance Forge: ' + GetExceptionMessage());
    Exit;
  end;
  Text := '';
  for Line := 0 to GetArrayLength(Output.StdOut) - 1 do
    Text := Text + Output.StdOut[Line] + #10;
  { Clients print the path with either slash. }
  StringChangeEx(Text, '/', '\', True);
  Result := Pos(Lowercase(ServerPath), Lowercase(Text)) > 0;
end;

{ Removes the server from a client only if this installer added it and the
  entry still launches the server it added. The client is looked up again now:
  it may have moved or gone since. }
procedure UnregisterClient(const Names, Scope, MarkerName: String);
var
  ClientPath, ServerPath: String;
begin
  if not RegQueryStringValue(HKA, InstallerRegistryKey, MarkerName, ServerPath) then
    Exit;
  ClientPath := FindOnPath(Names);
  if ClientPath <> '' then
  begin
    if ClientEntryLaunches(ClientPath, ServerPath) then
      RunClient(ClientPath, 'mcp remove ' + Scope + McpServerName)
    else
      Log('Assurance Forge: the ' + McpServerName + ' entry no longer launches ' + ServerPath + '; leaving it');
  end;
  RegDeleteValue(HKA, InstallerRegistryKey, MarkerName);
end;

procedure UnregisterClients();
begin
  UnregisterClient('claude.exe,claude.cmd', '--scope user ', 'RegisteredClaudeCode');
  UnregisterClient('codex.exe,codex.cmd', '', 'RegisteredCodex');
  RegDeleteKeyIfEmpty(HKA, InstallerRegistryKey);
  RegDeleteKeyIfEmpty(HKA, 'Software\Assurance Forge');
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

function BoxChecked(Box: TNewCheckBox): Boolean;
begin
  Result := (Box <> nil) and Box.Checked and Box.Enabled;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then
    Exit;
  if not ShouldInstallMcp() then
  begin
    { An upgrade that drops the MCP server also withdraws the registrations
      that pointed at it. }
    UnregisterClients();
    Exit;
  end;
  if BoxChecked(ClaudeCodeBox) then
    NoteClient('Claude Code', RegisterWithClient(ClaudeCodePath, '--scope user ', 'RegisteredClaudeCode'));
  if BoxChecked(CodexBox) then
    NoteClient('Codex', RegisterWithClient(CodexPath, '', 'RegisteredCodex'));
end;

{ --- Page building ----------------------------------------------------------- }

function AddText(Page: TWizardPage; Top: Integer; const Caption: String; Bold: Boolean): TNewStaticText;
begin
  Result := TNewStaticText.Create(Page);
  Result.Parent := Page.Surface;
  Result.AutoSize := False;
  Result.WordWrap := True;
  Result.Left := 0;
  Result.Top := Top;
  Result.Width := Page.SurfaceWidth;
  Result.Caption := Caption;
  if Bold then
    Result.Font.Style := [fsBold];
  Result.AdjustHeight();
end;

function AddCheckBox(Page: TWizardPage; Top, Indent: Integer; const Caption: String; Checked: Boolean): TNewCheckBox;
begin
  Result := TNewCheckBox.Create(Page);
  Result.Parent := Page.Surface;
  Result.Left := Indent;
  Result.Top := Top;
  Result.Width := Page.SurfaceWidth - Indent;
  Result.Height := ScaleY(17);
  Result.Caption := Caption;
  Result.Checked := Checked;
end;

procedure CreateTourPage();
var
  Points: TNewStaticText;
  Image: TBitmapImage;
  Png: TPngImage;
  Stream: TFileStream;
  ImageWidth, ImageHeight: Integer;
begin
  TourPage := CreateCustomPage(wpWelcome, CustomMessage('TourCaption'), CustomMessage('TourDescription'));

  Points := AddText(TourPage, 0, CustomMessage('TourPoints'), False);
  Points.Top := TourPage.SurfaceHeight - Points.Height;

  { The screenshot fills what is left above the points, keeping its shape. }
  ExtractTemporaryFile('tour.png');
  Png := TPngImage.Create();
  Stream := TFileStream.Create(ExpandConstant('{tmp}\tour.png'), fmOpenRead);
  try
    Png.LoadFromStream(Stream);
  finally
    Stream.Free();
  end;
  ImageHeight := Points.Top - ScaleY(10);
  ImageWidth := ImageHeight * Png.Width div Png.Height;
  if ImageWidth > TourPage.SurfaceWidth then
  begin
    ImageWidth := TourPage.SurfaceWidth;
    ImageHeight := ImageWidth * Png.Height div Png.Width;
  end;
  Image := TBitmapImage.Create(TourPage);
  Image.Parent := TourPage.Surface;
  Image.Stretch := True;
  Image.PngImage := Png;
  Image.SetBounds((TourPage.SurfaceWidth - ImageWidth) div 2, 0, ImageWidth, ImageHeight);
end;

procedure InstallMcpBoxClick(Sender: TObject);
begin
  if ClaudeCodeBox <> nil then
    ClaudeCodeBox.Enabled := InstallMcpBox.Checked;
  if CodexBox <> nil then
    CodexBox.Enabled := InstallMcpBox.Checked;
end;

{ Whether Name is in the /CONNECT= list. }
function ConnectRequested(const Name: String): Boolean;
begin
  Result := Pos(',' + Name + ',', ',' + Lowercase(ExpandConstant('{param:CONNECT|}')) + ',') > 0;
end;

procedure CreateAiPage();
var
  Top, Indent: Integer;
  InstallMcp: Boolean;
  McpParam: String;
  Text: TNewStaticText;
begin
  AiPage := CreateCustomPage(wpSelectComponents, CustomMessage('AiCaption'), CustomMessage('AiDescription'));
  Indent := ScaleX(20);

  Text := AddText(AiPage, 0, CustomMessage('AiReviewHeading'), True);
  Top := Text.Top + Text.Height + ScaleY(3);
  Text := AddText(AiPage, Top, CustomMessage('AiReviewText'), False);
  Top := Text.Top + Text.Height + ScaleY(14);

  Text := AddText(AiPage, Top, CustomMessage('AiMcpHeading'), True);
  Top := Text.Top + Text.Height + ScaleY(3);
  Text := AddText(AiPage, Top, CustomMessage('AiMcpText'), False);
  Top := Text.Top + Text.Height + ScaleY(8);

  { /MCP= on the command line decides; otherwise the choice made on the last
    install; otherwise installed. }
  McpParam := ExpandConstant('{param:MCP|}');
  if McpParam <> '' then
    InstallMcp := McpParam <> '0'
  else
    InstallMcp := GetPreviousData('InstallMcp', '1') = '1';
  InstallMcpBox := AddCheckBox(AiPage, Top, 0, CustomMessage('AiInstallMcp'), InstallMcp);
  InstallMcpBox.OnClick := @InstallMcpBoxClick;
  Top := Top + InstallMcpBox.Height + ScaleY(4);

  { Only the clients actually found are offered, never ticked for the user:
    connecting writes into that client's own configuration. }
  if ClaudeCodePath <> '' then
  begin
    ClaudeCodeBox := AddCheckBox(AiPage, Top, Indent, FmtMessage(CustomMessage('AiConnect'), ['Claude Code']),
                                 ConnectRequested('claudecode'));
    Top := Top + ClaudeCodeBox.Height + ScaleY(4);
  end;
  if CodexPath <> '' then
  begin
    CodexBox := AddCheckBox(AiPage, Top, Indent, FmtMessage(CustomMessage('AiConnect'), ['Codex']),
                            ConnectRequested('codex'));
    Top := Top + CodexBox.Height + ScaleY(4);
  end;
  InstallMcpBoxClick(nil);

  Text := AddText(AiPage, 0, CustomMessage('AiFooter'), False);
  Text.Top := AiPage.SurfaceHeight - Text.Height;
end;

procedure InitializeWizard();
begin
  CreateTourPage();
  CreateAiPage();
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  if InstallMcpBox.Checked then
    SetPreviousData(PreviousDataKey, 'InstallMcp', '1')
  else
    SetPreviousData(PreviousDataKey, 'InstallMcp', '0');
end;

{ Someone upgrading has seen the tour. }
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = TourPage.ID) and WasInstalled;
end;

{ The finish text depends on what was chosen: one line about the AI assistant
  when the MCP server was installed. The example tip is always shown: the
  example project ships with every install. }
procedure CurPageChanged(CurPageID: Integer);
var
  Text: String;
begin
  if CurPageID <> wpFinished then
    Exit;
  Text := CustomMessage('FinishReady');
  Text := Text + #13#10#13#10 + CustomMessage('FinishExample');
  if ClientsConnected <> '' then
    Text := Text + #13#10#13#10 + FmtMessage(CustomMessage('FinishMcpConnected'), [ClientsConnected]);
  if ClientsFailed <> '' then
    Text := Text + #13#10#13#10 + FmtMessage(CustomMessage('FinishMcpFailed'), [ClientsFailed]);
  if ShouldInstallMcp() and (ClientsConnected = '') and (ClientsFailed = '') then
    Text := Text + #13#10#13#10 + CustomMessage('FinishMcpManual');
  Text := Text + #13#10#13#10 + CustomMessage('FinishFeedback');
  WizardForm.FinishedLabel.Caption := Text;
  WizardForm.AdjustLabelHeight(WizardForm.FinishedLabel);
  WizardForm.RunList.Top := WizardForm.FinishedLabel.Top + WizardForm.FinishedLabel.Height + ScaleY(12);
end;

{ --- Uninstall ----------------------------------------------------------------- }

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
