; Assurance Forge installer: the pages, words and actions CPack does not
; generate. Included at the top of the script CPack writes
; (CPACK_INNOSETUP_EXTRA_SCRIPTS); the [Setup] directives, images and colours
; are set in cmake/packaging.cmake, which files belong to which choice in
; cmake/packaging_project_config.cmake, and the [Code] -- including the "A quick
; look" and "AI assistance" pages -- is installer_code.pas. Saved as UTF-8 with
; a byte-order mark, which Inno Setup needs to read the Japanese text.
;
; Every message exists in English and Japanese, the application's two UI
; languages. Setup picks the one matching Windows and asks only when neither
; does.
;
; The installer is the first thing a new user sees, so it says what they are
; getting. Every feature named below is a `supported` row in
; docs/features/feature-matrix.md; keep it that way when editing the text.

#if Ver < EncodeVer(6, 6, 0)
  #error The Assurance Forge installer needs Inno Setup 6.6 or later (dark mode, WizardBackColor, modern image sizes).
#endif

[Files]
; The "A quick look" page's screenshot; extracted for the page, never installed.
Source: "{#AfInstallerArtDir}\tour.png"; Flags: dontcopy

[InstallDelete]
; An upgrade where the MCP server was unticked removes the one installed before.
Type: files; Name: "{app}\assurance-forge-mcp.exe"; Check: ShouldRemoveMcp

[Types]
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
; What comes built in: listed, ticked and locked, so the page shows what the
; application does rather than hiding it behind one "Assurance Forge" line.
; These carry no files of their own -- they are all in the one executable.
Name: "app"; Description: "{cm:CompApp}"; Types: custom; Flags: fixed
Name: "app\sacm"; Description: "{cm:CompSacm}"; Types: custom; Flags: fixed
Name: "app\gsn"; Description: "{cm:CompGsn}"; Types: custom; Flags: fixed
Name: "app\sccg"; Description: "{cm:CompSccg}"; Types: custom; Flags: fixed
Name: "app\records"; Description: "{cm:CompRecords}"; Types: custom; Flags: fixed
Name: "app\export"; Description: "{cm:CompExport}"; Types: custom; Flags: fixed
Name: "app\languages"; Description: "{cm:CompLanguages}"; Types: custom; Flags: fixed
; The one optional part here. The MCP server has a page of its own, next to the
; built-in AI review, because the two are easy to confuse.
Name: "samples"; Description: "{cm:CompSamples}"; Types: custom

[Messages]
english.WelcomeLabel2=Welcome! Let's get [name/ver] set up on your computer.%n%nAssurance Forge helps you build, review and navigate safety cases: GSN arguments that lay themselves out on the canvas, kept on your computer as SACM 2.3 files that stay yours.%n%nThis is an early build and it is growing fast. We'd love to hear what you think at github.com/lasrod/assurance-forge.
japanese.WelcomeLabel2=ようこそ！[name/ver] のセットアップを始めましょう。%n%nAssurance Forge は安全ケースの作成・レビュー・閲覧を支援します。GSN の議論はキャンバス上で自動的にレイアウトされ、SACM 2.3 ファイルとしてお使いのコンピューターに保存されます。ファイルはずっとあなたのものです。%n%nこれは早期ビルドで、どんどん進化しています。ご意見を github.com/lasrod/assurance-forge でお待ちしています。
english.SelectTasksLabel2=Almost there! Would you like a shortcut on your desktop as well?
japanese.SelectTasksLabel2=あと少しです！デスクトップにもショートカットを作成しますか？
english.SelectComponentsLabel2=Here is everything Assurance Forge brings along. The ticked and locked items are built in. AI features come on the next page.
japanese.SelectComponentsLabel2=Assurance Forge に含まれる機能の一覧です。チェックが固定された項目は標準で組み込まれています。AI 機能は次のページで説明します。

[CustomMessages]
english.TypeCustom=Custom
japanese.TypeCustom=カスタム
english.CompApp=Assurance Forge: GSN canvas with automatic layout
japanese.CompApp=Assurance Forge：自動レイアウトの GSN キャンバス
english.CompSacm=SACM 2.3 import, editing and export
japanese.CompSacm=SACM 2.3 のインポート・編集・エクスポート
english.CompGsn=GSN with patterns, away goals, challenges and claim points
japanese.CompGsn=GSN（パターン、他モジュールの目標、チャレンジ、保証主張点）
english.CompSccg=Safety Case Core Guidelines checks
japanese.CompSccg=Safety Case Core Guidelines のチェック
english.CompRecords=Evidence and CSE registers, baselines and full history
japanese.CompRecords=エビデンス登録簿・CSE 登録簿、ベースライン、全履歴
english.CompExport=SVG diagram export
japanese.CompExport=SVG 図のエクスポート
english.CompLanguages=English and Japanese, for the app and your argument
japanese.CompLanguages=英語と日本語（アプリと議論の両方）
english.CompSamples=Sample assurance cases to explore
japanese.CompSamples=試せるサンプルのアシュアランスケース

; "A quick look": a first install only.
english.TourCaption=A quick look
japanese.TourCaption=Assurance Forge の紹介
english.TourDescription=What you can do with Assurance Forge.
japanese.TourDescription=Assurance Forge でできること
english.TourPoints=•  Build GSN arguments that lay themselves out, then navigate and edit them on the canvas.%n•  Check them against the Safety Case Core Guidelines as you go.%n•  Keep every change: undo, baselines, a replayable history, and evidence registers.
japanese.TourPoints=•  自動でレイアウトされる GSN の議論を作成し、キャンバス上で閲覧・編集できます。%n•  作業しながら Safety Case Core Guidelines に沿ってチェックできます。%n•  すべての変更を残せます：元に戻す、ベースライン、再生できる履歴、エビデンス登録簿。

; "AI assistance": two different things, side by side.
english.AiCaption=AI assistance
japanese.AiCaption=AI アシスタンス
english.AiDescription=Two ways to bring AI into your work. Both are optional: pick what suits you, or skip it for now.
japanese.AiDescription=AI を作業に取り入れる 2 つの方法です。どちらも任意です。必要なものを選ぶか、今はスキップしてもかまいません。
english.AiReviewHeading=Built-in AI review
japanese.AiReviewHeading=組み込みの AI レビュー
english.AiReviewText=A second pair of eyes on your argument: Assurance Forge asks an AI model to review it against the Safety Case Core Guidelines. Add your own API key for an OpenAI-compatible service whenever you're ready, under Edit → Preferences → AI. Nothing to install now.
japanese.AiReviewText=議論にもう一つの視点を。Assurance Forge が AI モデルに、Safety Case Core Guidelines に沿ったレビューを依頼します。準備ができたら、OpenAI 互換サービスの API キーを 編集 → 設定 → AI で登録してください。ここでインストールするものはありません。
english.AiMcpHeading=Your own AI assistant, connected over MCP
japanese.AiMcpHeading=お使いの AI アシスタントを MCP で接続
english.AiMcpText=Already working with Claude Code, Codex or another assistant? Let it read your case and suggest changes, which you review and accept in the app. It runs on your assistant's own account, so no API key is needed here, and it only sees your case once you switch it on and allow it in the app.
japanese.AiMcpText=Claude Code や Codex などのアシスタントをすでにお使いですか？ケースを読んで変更を提案させ、アプリの中で確認して承認できます。アシスタント自身のアカウントで動くため、ここで API キーは不要です。アプリで有効にして許可するまで、ケースは見えません。
english.AiInstallMcp=Install the MCP server
japanese.AiInstallMcp=MCP サーバーをインストールする
english.AiConnect=Connect %1 to Assurance Forge
japanese.AiConnect=%1 を Assurance Forge に接続する
english.AiFooter=Assurance Forge is open source (MIT), and the choice of AI provider is always yours.
japanese.AiFooter=Assurance Forge はオープンソース（MIT）です。AI プロバイダーは、いつでもあなたが選べます。

english.FinishReady=You're all set! Assurance Forge is ready when you are. Thank you for trying it out.
japanese.FinishReady=準備が整いました！Assurance Forge はいつでもお使いいただけます。お試しいただきありがとうございます。
english.FinishSamples=New here? A good first step: choose Create Project from Existing SACM on the welcome screen and open one of the sample cases to look around.
japanese.FinishSamples=はじめての方は、まずウェルカム画面で「既存の SACM からプロジェクトを作成」を選び、サンプルを開いてみてください。
english.FinishMcpConnected=%1 is connected and can find Assurance Forge. Whenever you want to work on a case together, switch on Edit → Preferences → MCP Server; the app always asks you before a client reads a project.
japanese.FinishMcpConnected=%1 が接続され、Assurance Forge を利用できるようになりました。一緒にケースに取り組むときは、編集 → 設定 → MCP サーバーで有効にしてください。クライアントがプロジェクトを読む前に、アプリが必ず確認します。
english.FinishMcpManual=Want to bring in an AI assistant later? Its configuration is waiting under Edit → Preferences → MCP Server.
japanese.FinishMcpManual=あとで AI アシスタントを加えたくなったら、編集 → 設定 → MCP サーバーに構成が用意されています。
english.FinishMcpFailed=Connecting %1 did not work. You can connect it later from Edit → Preferences → MCP Server; the setup log in your Temp folder says what went wrong.
japanese.FinishMcpFailed=%1 への接続に失敗しました。あとで 編集 → 設定 → MCP サーバー から接続できます。原因は Temp フォルダーのセットアップログに記録されています。
english.FinishFeedback=Found a rough edge, or have an idea? We'd love to hear it at github.com/lasrod/assurance-forge.
japanese.FinishFeedback=気になる点やアイデアがあれば、ぜひ github.com/lasrod/assurance-forge でお知らせください。
english.ClientsBoth=%1 and %2
japanese.ClientsBoth=%1 と %2
english.OpenUserGuide=Open the user guide
japanese.OpenUserGuide=ユーザーガイドを開く
english.BrowseExamples=Browse example projects
japanese.BrowseExamples=サンプルプロジェクトを見る
english.ViewReleaseNotes=See what's new in this version
japanese.ViewReleaseNotes=このバージョンの新機能を見る
english.KeepUserDataInstruction=Keep your Assurance Forge settings?
japanese.KeepUserDataInstruction=Assurance Forge の設定を残しますか？
english.KeepUserDataText=Assurance Forge has been removed. Your projects and SACM files are never touched; this is only about the application's own settings.
japanese.KeepUserDataText=Assurance Forge は削除されました。プロジェクトと SACM ファイルには一切影響しません。ここで選ぶのはアプリケーション自身の設定だけです。
english.KeepUserDataKeep=Keep settings%nA later install picks up your preferences and AI provider setup again.
japanese.KeepUserDataKeep=設定を残す%n再インストールしたときに、環境設定と AI プロバイダーの設定をそのまま使えます。
english.KeepUserDataRemove=Remove settings%nPreferences, AI provider settings and the saved API key.
japanese.KeepUserDataRemove=設定を削除する%n環境設定、AI プロバイダーの設定、保存された API キーを削除します。

[Run]
; Launch first, and ticked. The links are left unticked: opening a browser is
; the user's call, not the installer's. The guide and the examples are offered
; on a first install, the release notes on an upgrade.
Filename: "{app}\assurance-forge.exe"; Description: "{cm:LaunchProgram,Assurance Forge}"; Flags: nowait postinstall skipifsilent
Filename: "{#AfUserGuideUrl}"; Description: "{cm:OpenUserGuide}"; Flags: shellexec nowait postinstall skipifsilent unchecked; Check: IsFreshInstall
Filename: "{#AfExamplesUrl}"; Description: "{cm:BrowseExamples}"; Flags: shellexec nowait postinstall skipifsilent unchecked; Check: IsFreshInstall
Filename: "{#AfReleaseNotesUrl}"; Description: "{cm:ViewReleaseNotes}"; Flags: shellexec nowait postinstall skipifsilent unchecked; Check: IsUpgrade
