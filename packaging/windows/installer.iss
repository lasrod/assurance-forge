; Assurance Forge installer: the pages, words and actions CPack does not
; generate. Included at the top of the script CPack writes
; (CPACK_INNOSETUP_EXTRA_SCRIPTS); the [Setup] directives, images and colours
; are set in cmake/packaging.cmake, and the [Code] it calls is in
; installer_code.pas. Saved as UTF-8 with a byte-order mark, which Inno Setup
; needs to read the Japanese text.
;
; Every message exists in English and Japanese, the application's two UI
; languages. Setup picks the one matching Windows and asks only when neither
; does.

#if Ver < EncodeVer(6, 6, 0)
  #error The Assurance Forge installer needs Inno Setup 6.6 or later (dark mode, WizardBackColor, modern image sizes).
#endif

[Messages]
english.WelcomeLabel2=This will install [name/ver] on your computer.%n%nAssurance Forge is a workbench for safety cases: build, navigate and review assurance arguments in GSN, kept as SACM 2.3 files.%n%nThis is an early build. Bug reports and ideas are welcome at github.com/lasrod/assurance-forge.
japanese.WelcomeLabel2=このウィザードは [name/ver] をコンピューターにインストールします。%n%nAssurance Forge は安全ケースのためのワークベンチです。GSN でアシュアランス議論を作成・閲覧・レビューし、SACM 2.3 ファイルとして保存します。%n%nこれは早期ビルドです。不具合の報告やご意見は github.com/lasrod/assurance-forge までお寄せください。
english.FinishedLabel=Setup has finished installing [name].%n%nTo look around, choose Create Project from Existing SACM on the welcome screen and pick one of the sample cases in the data folder where Assurance Forge is installed.
japanese.FinishedLabel=[name] のインストールが完了しました。%n%n試しに使うには、ウェルカム画面で「既存の SACM からプロジェクトを作成」を選び、インストール先の data フォルダーにあるサンプルを選択してください。

[CustomMessages]
english.OpenUserGuide=Open the user guide
japanese.OpenUserGuide=ユーザーガイドを開く
english.ViewReleaseNotes=See what's new in this version
japanese.ViewReleaseNotes=このバージョンの変更点を見る
english.KeepUserDataInstruction=Keep your Assurance Forge settings?
japanese.KeepUserDataInstruction=Assurance Forge の設定を残しますか？
english.KeepUserDataText=Assurance Forge has been removed. Your projects and SACM files are never touched; this is only about the application's own settings.
japanese.KeepUserDataText=Assurance Forge は削除されました。プロジェクトと SACM ファイルには一切影響しません。ここで選ぶのはアプリケーション自身の設定だけです。
english.KeepUserDataKeep=Keep settings%nA later install picks up your preferences and AI provider setup again.
japanese.KeepUserDataKeep=設定を残す%n再インストールしたときに、環境設定と AI プロバイダーの設定をそのまま使えます。
english.KeepUserDataRemove=Remove settings%nPreferences, AI provider settings and the saved API key.
japanese.KeepUserDataRemove=設定を削除する%n環境設定、AI プロバイダーの設定、保存された API キーを削除します。

[Run]
; Launch first, and checked. The two links are left unchecked: opening a browser
; is the user's call, not the installer's. The guide is offered on a first
; install and the release notes on an upgrade, when each is the useful one.
Filename: "{app}\assurance-forge.exe"; Description: "{cm:LaunchProgram,Assurance Forge}"; Flags: nowait postinstall skipifsilent
Filename: "{#AfUserGuideUrl}"; Description: "{cm:OpenUserGuide}"; Flags: shellexec nowait postinstall skipifsilent unchecked; Check: IsFreshInstall
Filename: "{#AfReleaseNotesUrl}"; Description: "{cm:ViewReleaseNotes}"; Flags: shellexec nowait postinstall skipifsilent unchecked; Check: IsUpgrade
