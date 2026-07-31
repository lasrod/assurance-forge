#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Author Japanese translations for the full Assurance Forge UI surface and
rewrite assets/locale/ja/LC_MESSAGES/assurance_forge.po, then recompile the
.mo via tools/i18n/compile_po.py. Existing JA translations in the .po are
preserved; only new msgids get the machine-authored translations below.
"""

import re
import subprocess
import sys
from pathlib import Path


# Japanese translations for every msgid referenced from source. Keys are the
# English source strings (msgid). Values are JA (msgstr). Empty value means
# "untranslated — fall back to English".
TRANSLATIONS = {
    # ===== Already in catalog (kept for completeness / consistency) =====
    "File": "ファイル",
    "Create Empty Assurance Project": "空の保証プロジェクトを作成",
    "Open Project": "プロジェクトを開く",
    "Save Project": "プロジェクトを保存",
    "Export": "エクスポート",
    "GSN SVG": "GSN SVG",
    "Exit": "終了",
    "Add": "追加",
    "New GSN / SACM File": "新規 GSN / SACM ファイル",
    "New Evidence Register": "新規エビデンス登録簿",
    "New J3377 CAE Register": "新規 J3377 CAE 登録簿",
    # ProjectFileCreateTitle fallback (unreachable in practice; here for completeness).
    "New Project File": "新規プロジェクトファイル",
    "Edit": "編集",
    "Preferences...": "設定…",
    "Preferences": "設定",
    "View": "表示",
    "GSN Canvas": "GSN キャンバス",
    "CSE Register": "CSE 登録簿",
    "Evidence Register": "エビデンス登録簿",
    "Active assurance case: {0}": "アクティブな保証ケース: {0}",
    "Advanced": "詳細",
    "All evidence is linked": "すべてのエビデンスがリンクされています",
    "Arguments": "議論",
    "Assessment workspace available": "評価ワークスペースを利用できます",
    "Case Explorer": "ケースエクスプローラー",
    "Change Proposals": "変更提案",
    "Claim-Evidence Traceability": "主張–エビデンスのトレーサビリティ",
    "Conformance": "適合性",
    "Generated reports: {0}": "生成済みレポート: {0}",
    "Needs Attention": "要確認",
    "No arguments yet.": "議論はまだありません。",
    "No open project alerts.": "未解決のプロジェクト警告はありません。",
    "No terminology packages.": "用語パッケージはありません。",
    "No undeveloped elements": "未展開の要素はありません",
    "Not assessed": "未評価",
    "Open Findings": "未解決の指摘",
    "Overview": "概要",
    "Project Details": "プロジェクト詳細",
    "Project Overview": "プロジェクト概要",
    "Project Readiness": "プロジェクトの準備状況",
    "Project files: {0}": "プロジェクトファイル: {0}",
    "Report Builder": "レポートビルダー",
    "Reports": "レポート",
    "Reviews": "レビュー",
    "Search project...": "プロジェクトを検索...",
    "Start Conformance Assessment": "適合性評価を開始",
    "Terminology": "用語",
    "elements": "要素",
    "items": "項目",
    "open": "未解決",
    "{0} argument elements are undeveloped": "{0} 件の議論要素が未展開です",
    "{0} broken": "{0} 件破損",
    "{0} change proposals no longer apply cleanly": "{0} 件の変更提案を正常に適用できません",
    "{0} errors require attention": "{0} 件のエラーに対応が必要です",
    "{0} evidence items are not linked to a claim": "{0} 件のエビデンスが主張にリンクされていません",
    "{0} proposals": "{0} 件の提案",
    "{0} undeveloped": "{0} 件未展開",
    "{0} unlinked": "{0} 件未リンク",
    "{0} warnings require review": "{0} 件の警告を確認する必要があります",
    "Welcome Screen": "ウェルカム画面",
    "Ignored terms": "無視された用語",
    "No ignored terms.": "無視された用語はありません。",
    "Restore": "復元",
    "Appearance": "外観",
    "Theme": "テーマ",
    "Language": "言語",
    "Show FPS": "FPS を表示",
    "High culling": "高カリング",
    "Medium culling": "中カリング",
    "Low culling": "低カリング",
    "AI": "AI",
    "AI settings are unavailable.": "AI 設定を使用できません。",
    "Enable AI support": "AI サポートを有効にする",
    "Provider": "プロバイダー",
    "Model": "モデル",
    "Save AI Settings": "AI 設定を保存",
    "API Key": "API キー",
    "Secure storage is unavailable on this platform.": "このプラットフォームでは安全な保存を使用できません。",
    "Key stored: ********": "キー保存済み: ********",
    "No API key stored.": "保存済み API キーはありません。",
    "Update Key": "キーを更新",
    "Save Key": "キーを保存",
    "Remove Key": "キーを削除",
    "Test Connection": "接続をテスト",
    "OK": "OK",
    "Walkthroughs": "ウォークスルー",
    "Walkthroughs are not yet implemented.": "ウォークスルーは未実装です。",
    "Import SACM": "SACM をインポート",
    "Import SACM is not yet implemented.": "SACM のインポートは未実装です。",
    "Forge Confidence in Safety": "安全への確信を鍛える",
    "Start": "開始",
    "Open Recent Projects": "最近のプロジェクトを開く",
    "No recent projects.": "最近のプロジェクトはありません。",
    "Start with a blank assurance project workspace": "空の保証プロジェクトワークスペースから開始します",
    "Create Assurance Project from Template": "テンプレートから保証プロジェクトを作成",
    "Create a project from a predefined assurance case template": "定義済みの保証ケーステンプレートからプロジェクトを作成します",
    "Create Assurance Project from Template is not yet implemented.": "テンプレートから保証プロジェクトを作成は未実装です。",
    "Open an existing Assurance Forge project": "既存の Assurance Forge プロジェクトを開きます",
    "Import a SACM XML assurance case": "SACM XML 保証ケースをインポートします",
    "Get started with Assurance Forge": "Assurance Forge の開始ガイド",
    "Create, inspect, and navigate a safety case": "安全ケースの作成・確認・操作を行います",
    "Learn the Fundamentals": "基本を学ぶ",
    "GSN structure, SACM imports, evidence, and registers": "GSN 構造、SACM インポート、エビデンス、登録簿を学びます",
    "Prepare a Conformance Review": "適合性レビューを準備する",
    "Trace claims, evidence, and review outputs": "主張、エビデンス、レビュー出力を追跡します",
    "Undo": "元に戻す",
    "Reached snapshot or baseline — restore from history to go further back.": "スナップショットまたはベースラインに到達しました — さらに戻すには履歴から復元してください。",

    # ===== Common buttons / actions =====
    "Cancel": "キャンセル",
    "Save": "保存",
    "Create": "作成",
    "Delete": "削除",
    "Remove": "除去",
    "Close": "閉じる",
    "Load": "読み込み",
    "Later": "後で",
    "Don't Save": "保存しない",
    "Resolve": "解決",
    "Dismiss": "閉じる",
    "Ignore": "無視",
    "Define": "定義",
    "Link existing": "既存にリンク",
    "Define term": "用語を定義",
    "Open term": "用語を開く",
    "Edit term": "用語を編集",
    "Add as context": "コンテキストとして追加",
    "Find usages": "使用箇所を検索",
    "Find Usages": "使用箇所を検索",
    "Go": "移動",
    "Open review": "レビューを開く",
    "Pause": "一時停止",
    "Resume": "再開",
    "Send": "送信",
    "Sending...": "送信中…",
    "Yes": "はい",
    "No": "いいえ",
    "Yes, Overwrite": "はい、上書きする",
    "Reset toggles": "切替をリセット",
    "Copy report to clipboard": "レポートをクリップボードにコピー",
    "Save report to file": "レポートをファイルに保存",
    "Reconcile": "再構築",
    "Use for this element": "この要素に使用",

    # ===== Element types / GSN node kinds =====
    "Goal": "ゴール",
    "Strategy": "戦略",
    "Solution": "ソリューション",
    "Context": "コンテキスト",
    "Assumption": "仮定",
    "Justification": "正当化",
    "Claim": "主張",
    "Claims": "主張",
    "Strategies": "戦略",
    "Evidence": "エビデンス",
    "Challenge": "チャレンジ",
    "Assurance Claim Point": "保証主張点",
    "Assurance Claim Points": "保証主張点",
    "Assurance case": "保証ケース",
    "Assurance Case: {0}": "保証ケース: {0}",
    "Element": "要素",
    "Element Properties": "要素プロパティ",
    "Element {0}": "要素 {0}",
    "Element not found in the active model.": "アクティブモデルに要素が見つかりません。",
    "Element not found: {0}": "要素が見つかりません: {0}",

    # ===== Element panel headers =====
    "Name": "名前",
    "Name:": "名前:",
    "Name: {0}": "名前: {0}",
    "Name ({0})": "名前 ({0})",
    "Content": "内容",
    "Content ({0})": "内容 ({0})",
    "Description": "説明",
    "Description ({0})": "説明 ({0})",
    "Description (optional)": "説明 (省略可)",
    "Text": "テキスト",
    "Undeveloped": "未展開",
    "ID": "ID",
    "ID:": "ID:",
    "ID: {0}": "ID: {0}",
    "GID: {0}": "GID: {0}",
    "GSN identifier": "GSN 識別子",
    "Type": "種別",
    "Type:": "種別:",
    "Type: {0}": "種別: {0}",
    "Translation Language": "翻訳言語",
    "Add Translation": "翻訳を追加",
    "Terminology suggestions": "用語の提案",
    "Unnamed term": "名前なしの用語",
    "{0} has multiple meanings.": "{0} には複数の意味があります。",
    "{0} is not defined.": "{0} は定義されていません。",
    "Create new meaning": "新しい意味を作成",
    "Unresolved review": "未解決のレビュー",
    "Text changed — update both languages, then mark reviewed.": "テキストが変更されました — 両方の言語を更新してから、レビュー済みにしてください。",
    "Mark reviewed": "レビュー済みにする",
    "Review element": "要素を確認",
    "Text changed — review the secondary-language translation for consistency.": "テキストが変更されました — 第二言語の翻訳の整合性を確認してください。",
    "Open review comments or AI review failures for this element.": "この要素のレビューコメントまたは AI レビュー失敗を開きます。",
    "{0}\nClick to open the Review tab.": "{0}\nクリックしてレビュータブを開きます。",
    "No element selected.": "要素が選択されていません。",
    "No safety case loaded.": "安全ケースが読み込まれていません。",
    "Historical preview — fields are read-only. Return to latest to edit.": "過去の表示 — フィールドは読み取り専用です。編集するには最新に戻ってください。",
    "Proposal preview is active. Exit preview before editing element properties.": "提案プレビューが有効です。要素プロパティを編集する前にプレビューを終了してください。",

    # ===== History =====
    "History": "履歴",
    "Last changed": "最終変更",
    "Changes": "変更",
    "Changed since baseline": "ベースライン以降に変更",
    "{0}  by {1}": "{0}  作業者 {1}",
    "View Element History": "要素履歴を表示",
    "Compare to Baseline": "ベースラインと比較",
    "Available in a later release.": "後のリリースで利用可能になります。",
    "No recorded changes for this element.": "この要素に記録された変更はありません。",
    "Transaction": "トランザクション",
    "Drag to scrub transaction history": "ドラッグしてトランザクション履歴をスクラブします",
    "Timestamp (UTC)": "タイムスタンプ (UTC)",
    "Author": "作成者",
    "Command": "コマンド",
    "(no element changes)": "(要素の変更なし)",
    "This project does not have an audit store yet.": "このプロジェクトにはまだ監査ストアがありません。",
    "An audit log is created automatically the first time a model-mutating command is recorded for a SACM file in this project.": "このプロジェクトの SACM ファイルに対してモデル変更コマンドが初めて記録されたとき、監査ログが自動的に作成されます。",
    "No transactions have been recorded yet.": "トランザクションはまだ記録されていません。",
    "Open a SACM model and use any model-mutating action (add or remove a node) — each command will appear here.": "SACM モデルを開いて、モデルを変更するアクション (ノードの追加または削除) を使用してください — 各コマンドがここに表示されます。",

    # ===== Audit / baselines / snapshots =====
    "Audit log divergence detected": "監査ログの不一致を検出しました",
    "Audit store error: {0}": "監査ストアのエラー: {0}",
    "Autosave write failed": "自動保存の書き込みに失敗しました",
    "The replayed audit history does not reproduce the on-disk SACM. This usually means edits were applied through a path that did not record transactions. Pinned historical views may be inaccurate.": "再生された監査履歴がディスク上の SACM を再現できません。通常、これはトランザクションを記録しない経路で編集が適用されたことを意味します。固定された履歴ビューは正確でない可能性があります。",
    "replay={0}  on_disk={1}": "再生={0}  ディスク={1}",
    "Reconcile audit log…": "監査ログを再構築…",
    "(archives current .af/ artifacts and rebuilds from the current SACM file)": "(現在の .af/ 成果物をアーカイブし、現在の SACM ファイルから再構築します)",
    "This will rebuild the audit store.": "監査ストアを再構築します。",
    "The current `.af/manifest.af.json`, `.af/snapshots/`, and `.af/audit/` will be moved to a timestamped `.af/backup_<UTC>/` folder, and a fresh audit store will be initialized from the current SACM file on disk.": "現在の `.af/manifest.af.json`、`.af/snapshots/`、`.af/audit/` はタイムスタンプ付きの `.af/backup_<UTC>/` フォルダーに移動され、ディスク上の現在の SACM ファイルから新しい監査ストアが初期化されます。",
    "Your existing transaction history is preserved on disk under the backup folder, but the application timeline will start over from a new initial snapshot. Pinned historical views from before this operation will no longer be browsable in-app.": "既存のトランザクション履歴はバックアップフォルダー内のディスク上に保持されますが、アプリのタイムラインは新しい初期スナップショットからやり直しになります。この操作以前に固定された履歴ビューはアプリ内で閲覧できなくなります。",
    "Continue?": "続行しますか?",
    "Your most recent change is recorded in the audit log but the on-disk SACM file may be out of date. Try a manual Save (File → Save) or verify free disk space, file permissions, and any external sync agent.": "直近の変更は監査ログに記録されていますが、ディスク上の SACM ファイルが古い可能性があります。手動で保存 (ファイル → 保存) を試すか、空きディスク容量、ファイル権限、外部同期エージェントを確認してください。",
    "Reconstruction failed: {0}": "再構築に失敗しました: {0}",
    "No reconstructed model to display.": "表示する再構築モデルがありません。",
    "Preview: Tx {0} (read-only)": "プレビュー: Tx {0} (読み取り専用)",
    "Snapshot created at sequence {0}.": "スナップショットをシーケンス {0} で作成しました。",
    "Failed to create snapshot: {0}": "スナップショットの作成に失敗しました: {0}",
    "Pin a named baseline to transaction sequence {0}.": "トランザクションシーケンス {0} に名前付きベースラインを固定します。",
    'Baseline "{0}" created at sequence {1}.': "ベースライン \"{0}\" をシーケンス {1} で作成しました。",
    "Failed to create baseline.": "ベースラインの作成に失敗しました。",
    "Create baseline at current state": "現在の状態でベースラインを作成",
    "Create snapshot at current state": "現在の状態でスナップショットを作成",
    "Return to latest": "最新に戻る",
    "Return to live": "ライブに戻る",
    "Return to live view": "ライブ表示に戻る",
    "Live": "ライブ",
    "Timeline actions": "タイムラインの操作",

    # ===== History panel =====
    "No project is currently open.": "現在開いているプロジェクトはありません。",
    "Filter:": "フィルター:",
    "author": "作成者",
    "(any command)": "(すべてのコマンド)",
    "Clear all": "すべてクリア",
    "Transactions: {0}   |   Current: Tx {1} / {2}": "トランザクション: {0}   |   現在: Tx {1} / {2}",
    " (open a package canvas to preview)": " (プレビューするにはパッケージキャンバスを開いてください)",
    " | Showing {0} of {1} (filtered)": " | {0} / {1} 件を表示 (フィルター適用)",

    # ===== Modals =====
    "Project Loading Status": "プロジェクト読み込み状況",
    "External changes detected": "外部の変更を検出しました",
    "Project name": "プロジェクト名",
    "Parent location": "親の場所",
    "File name": "ファイル名",
    "Unsaved Changes": "未保存の変更",
    "You have unsaved changes. Save before closing?": "未保存の変更があります。閉じる前に保存しますか?",
    "Open Project File": "プロジェクトファイルを開く",
    "You have unsaved changes in the current SACM file. Save before opening {0}?": "現在の SACM ファイルに未保存の変更があります。{0} を開く前に保存しますか?",
    "the selected project file": "選択したプロジェクトファイル",
    "Reviewer Name": "レビュー担当者名",
    "Enter the name to use for review comments.": "レビューコメントに使用する名前を入力してください。",
    "Reviewer name is required for new reviews.": "新しいレビューにはレビュー担当者名が必要です。",
    "Reviewer name saved.": "レビュー担当者名を保存しました。",
    "Delete Review Comment": "レビューコメントを削除",
    "Delete this review comment?": "このレビューコメントを削除しますか?",
    "The attached proposal will also be deleted.": "添付された提案も削除されます。",
    "Proposal: {0}": "提案: {0}",
    "Delete Both": "両方とも削除",
    "Remove {0}?": "{0} を除去しますか?",
    "this node and its attachments": "このノードとその付随要素",
    "this node and its descendants": "このノードとその子孫",
    # Delete confirmation backed by SACM library operation previews
    # (SACM23-INT-002).
    "Will be removed:": "削除されるもの:",
    "Will be modified (references removed):": "変更されるもの (参照が削除されます):",
    "Reported by the SACM library:": "SACM ライブラリからの報告:",
    "The SACM library could not preview this removal.":
        "SACM ライブラリはこの削除をプレビューできませんでした。",
    "{0} ({1})": "{0} ({1})",
    "{0} is not implemented yet.": "{0} はまだ実装されていません。",
    "Overwrite File?": "ファイルを上書きしますか?",
    "File already exists:\n{0}": "ファイルは既に存在します:\n{0}",
    "Are you sure you want to overwrite it?": "本当に上書きしてもよろしいですか?",

    # ===== AI settings / status =====
    "AI features may send selected safety case content and prompts to the configured AI provider. Assurance Forge will not send project data automatically; data is sent only when you explicitly use an AI action.": "AI 機能は、選択した安全ケースの内容とプロンプトを構成された AI プロバイダーに送信することがあります。Assurance Forge はプロジェクトデータを自動的には送信しません。データは明示的に AI アクションを使用した場合のみ送信されます。",
    "AI settings saved.": "AI 設定を保存しました。",
    "Enter an API key before saving.": "保存する前に API キーを入力してください。",
    "API key saved securely.": "API キーを安全に保存しました。",
    "API key removed.": "API キーを削除しました。",
    "Testing connection...": "接続をテスト中…",

    # ===== AI Debug / Review =====
    "AI Debug": "AI デバッグ",
    "AI Review": "AI レビュー",
    "AI review in progress.": "AI レビューを実行中です。",
    "AI review is already running.": "AI レビューは既に実行中です。",
    "AI review OK": "AI レビュー OK",
    "AI review OK is set by AI review outcomes.": "AI レビュー OK は AI レビューの結果によって設定されます。",
    # MCP change sets: what a connected AI client is proposing, and the
    # controls a person uses to accept or reject it.
    "Accept change": "変更を受け入れる",
    "Reject change": "変更を却下する",
    "Proposed by {0}": "{0} による提案",
    "Why: {0}": "理由: {0}",
    "{0} added, {1} changed, {2} removed": "追加 {0}、変更 {1}、削除 {2}",
    "Shown on the canvas as you watch it build.": "作成の様子がキャンバスに表示されています。",
    "The argument changed while this was being prepared, so it no longer applies. Ask the AI client to rebuild it.":
        "準備中に論証が変更されたため、この提案は適用できません。AI クライアントに再作成を依頼してください。",
    "This change was written against {0}. Open that argument to review it.":
        "この変更は {0} に対して作成されました。レビューするにはその論証を開いてください。",
    "Manual review OK": "手動レビュー OK",
    "Mark review OK manually": "レビュー OK を手動で記録",
    "Mark this element as manually reviewed OK.": "この要素を手動でレビュー OK としてマークします。",
    "Mark as reviewed": "レビュー済みとしてマーク",
    "Open or create a project before marking review status.": "レビュー状態を記録する前にプロジェクトを開くか作成してください。",
    "Open an assurance case before marking review status.": "レビュー状態を記録する前に保証ケースを開いてください。",
    "Select a GSN/SACM element before marking review status.": "レビュー状態を記録する前に GSN/SACM 要素を選択してください。",
    "Open an assurance case before running SCCG profile reviews.": "SCCG プロファイルレビューを実行する前に保証ケースを開いてください。",
    "Select a GSN/SACM element before running SCCG profile reviews.": "SCCG プロファイルレビューを実行する前に GSN/SACM 要素を選択してください。",
    "This profile does not apply to the selected element type.": "このプロファイルは選択した要素の種別には適用されません。",
    "SCCG profiles unavailable.": "SCCG プロファイルを使用できません。",
    "SCCG profiles unavailable: {0}": "SCCG プロファイルを使用できません: {0}",
    "SCCG guidelines are not available.": "SCCG ガイドラインは使用できません。",
    "Prompt": "プロンプト",
    "Response": "応答",
    "waiting...": "待機中…",
    "Parse error: {0}": "パースエラー: {0}",
    "No response yet.": "応答はまだありません。",

    # ===== Performance overlay =====
    "Performance overlay": "パフォーマンスオーバーレイ",
    "Open Performance overlay": "パフォーマンスオーバーレイを開く",
    "(history & values frozen)": "(履歴と値が固定されています)",
    "(live)": "(ライブ)",
    "● PAUSED": "● 一時停止中",
    "VSync": "VSync",
    "Disable to uncap the frame rate (useful for measuring raw render cost).": "無効にするとフレームレートの上限を解除します (生の描画コスト計測に便利です)。",
    "Idling": "アイドリング",
    "hello_imgui FpsIdling: when the UI is quiet, throttle to {0:.0f} FPS to save power.\nUncheck to keep the app running at full speed even when idle.": "hello_imgui FpsIdling: UI が静かなときに {0:.0f} FPS にスロットルして電力を節約します。\nアイドル時もフルスピードで動作させるにはオフにしてください。",
    "disabled": "無効",
    "throttled": "スロットル中",
    "unthrottled": "非スロットル",
    "Idling feature is turned off — the main loop runs at full speed.": "アイドリング機能はオフです — メインループはフルスピードで動作します。",
    "Main loop is currently throttled because no input was detected.": "入力が検出されないため、メインループは現在スロットルされています。",
    "Idling is enabled but not currently throttling — input is active.": "アイドリングは有効ですが、現在スロットルしていません — 入力がアクティブです。",
    "Live status from runnerParams.fpsIdling.\n{0}": "runnerParams.fpsIdling のライブ状態。\n{0}",
    "Frame interval (4s) —": "フレーム間隔 (4 秒) —",
    "frame interval (colored by FPS)": "フレーム間隔 (FPS で色分け)",
    "render cost only": "描画コストのみ",
    "This frame: {0:.2f} ms render across {1} buckets": "このフレーム: {0:.2f} ms ({1} バケット)",
    "Buckets (sorted by cost)": "バケット (コスト順)",
    "Sampling enabled": "サンプリングを有効化",
    "Raw bucket table": "生バケットテーブル",
    "Bucket": "バケット",
    "Time (ms)": "時間 (ms)",
    "% Frame": "% フレーム",
    "Hits": "ヒット数",
    "Canvas render stats": "キャンバス描画統計",
    "Feature toggles (A/B) & report": "機能切替 (A/B) とレポート",
    "Disable individual cost contributors to measure their impact on FPS. Defaults match production behaviour.": "個別のコスト要因を無効にして FPS への影響を計測します。デフォルトは本番動作と同じです。",
    "Node drop shadows": "ノードのドロップシャドウ",
    "Interior shading": "内部シェーディング",
    "Selection glow": "選択時のグロー",
    "Terminology spans": "用語スパン",
    "ACP decorators": "ACP デコレーター",
    "High-segment circles": "高セグメント円",
    "Freeze ACP rebuilds (not yet wired)": "ACP の再構築を凍結 (未接続)",
    "Saved: {0}": "保存しました: {0}",

    # ===== Confidence =====
    "Confidence": "信頼度",
    "Projected confidence": "投影信頼度",
    "Mode": "モード",
    "Direct value": "直接値",
    "Opinion triangle": "意見三角形",
    "Drag to adjust {0}": "{0} を調整するにはドラッグしてください",
    "Belief": "信念",
    "Disbelief": "不信",
    "Uncertainty": "不確実性",
    "Base rate {0:.2f}": "基底率 {0:.2f}",
    "Base rate controls how much unresolved uncertainty counts toward projected confidence.\nProjected confidence = belief + base rate * uncertainty.": "基底率は未解決の不確実性が投影信頼度にどれだけ寄与するかを制御します。\n投影信頼度 = 信念 + 基底率 * 不確実性。",
    "Belief {0:.2f}\nDisbelief {1:.2f}\nUncertainty {2:.2f}": "信念 {0:.2f}\n不信 {1:.2f}\n不確実性 {2:.2f}",
    "Jøsang's opinion triangle": "Jøsang の意見三角形",
    "No confidence assessment stored for this element.": "この要素に保存された信頼度評価はありません。",
    "Add fixed confidence": "固定信頼度を追加",
    "Add Jøsang confidence": "Jøsang 信頼度を追加",
    "Back up and start new confidence file": "バックアップして新しい信頼度ファイルを開始",
    "Method: {0}": "方式: {0}",
    "Status: {0}": "ステータス: {0}",
    "Status": "ステータス",
    "Active": "アクティブ",
    "Inactive": "非アクティブ",
    "Fixed value": "固定値",
    "Jøsang opinion": "Jøsang 意見",
    "Enable confidence for this element": "この要素の信頼度を有効化",
    "This confidence assessment may be stale because the element changed after the value was stored.": "この信頼度評価は古い可能性があります — 値が保存された後に要素が変更されました。",
    "Open or create a project before saving confidence.": "信頼度を保存する前にプロジェクトを開くか作成してください。",
    "Could not assign a SACM gid for confidence storage.": "信頼度保存用の SACM gid を割り当てられませんでした。",
    "Confidence save failed: {0}": "信頼度の保存に失敗しました: {0}",
    "Confidence update failed: {0}": "信頼度の更新に失敗しました: {0}",
    "Confidence reset failed: {0}": "信頼度のリセットに失敗しました: {0}",
    "Backed up invalid confidence file; new confidence storage will be saved with the project.": "無効な信頼度ファイルをバックアップしました。新しい信頼度ストレージはプロジェクトと共に保存されます。",

    # ===== ACP panel & decorators =====
    "ACP": "ACP",
    "ACP not found: {0}": "ACP が見つかりません: {0}",
    "Resolution mode": "解決モード",
    "Incomplete": "未完了",
    "Text confidence argument": "テキスト信頼度論証",
    "Text confidence argument is empty.": "テキスト信頼度論証が空です。",
    "Separate confidence argument tree": "個別の信頼度論証ツリー",
    "Confidence argument tree": "信頼度論証ツリー",
    "Confidence argument tree is not linked.": "信頼度論証ツリーがリンクされていません。",
    "Native text confidence claim is missing.": "ネイティブのテキスト信頼度主張がありません。",
    "Native claim:": "ネイティブ主張:",
    "Linked:": "リンク済み:",
    "Open confidence argument tree": "信頼度論証ツリーを開く",
    "Unlink": "リンク解除",
    "Detach this ACP from its current confidence argument tree.": "この ACP を現在の信頼度論証ツリーから切り離します。",
    "Create new confidence argument tree": "新しい信頼度論証ツリーを作成",
    "Create a new SACM argument package and link this ACP to its top goal.": "新しい SACM 論証パッケージを作成し、この ACP をそのトップゴールにリンクします。",
    "No existing confidence argument trees available to link.": "リンク可能な既存の信頼度論証ツリーはありません。",
    "Or link to an existing confidence argument tree:": "または既存の信頼度論証ツリーにリンク:",
    "Link selected confidence argument tree": "選択した信頼度論証ツリーをリンク",
    "Delete ACP": "ACP を削除",
    "Remove the SACM-backed ACP metadata from its target.": "対象から SACM ベースの ACP メタデータを除去します。",
    "No confidence argument has been selected.": "信頼度論証が選択されていません。",
    "Target:": "対象:",
    "Target: {0}": "対象: {0}",
    "Target element: {0}": "対象要素: {0}",
    "Relationship {0}": "関係 {0}",
    "Relationship {0} -> {1}": "関係 {0} -> {1}",
    "SACM relationship: {0}": "SACM 関係: {0}",
    "Remove ACP": "ACP を除去",
    "Select {0}": "{0} を選択",
    "Add ACP": "ACP を追加",
    "Add Counter Argument": "反論を追加",
    "Add Counter Evidence": "反証を追加",
    "ACP is not supported for this relationship.": "この関係では ACP はサポートされていません。",
    "Add ACP failed: {0}": "ACP の追加に失敗しました: {0}",
    "Eligible": "適格",
    "Blocked": "ブロック",
    "Open ACP": "ACP を開く",

    # ===== Relationship panel =====
    "No relationship selected.": "関係が選択されていません。",
    "Relationship not found: {0}": "関係が見つかりません: {0}",
    "Summary:": "要約:",
    "Sources:": "ソース:",
    "Targets:": "ターゲット:",
    "Reasoning:": "推論:",
    "ACP target:": "ACP 対象:",
    "ACP:": "ACP:",

    # ===== Terminology =====
    "Term": "用語",
    "Terms": "用語",
    "Term Card": "用語カード",
    "Term Usages": "用語の使用箇所",
    "Run Find usages from a term card or glossary row.": "用語カードまたは用語表の行から使用箇所検索を実行してください。",
    "Find usages: {0}": "使用箇所検索: {0}",
    "Find usages: {0}  {1}": "使用箇所検索: {0}  {1}",
    "No usages found.": "使用箇所が見つかりません。",
    "Snippet": "抜粋",
    "Package": "パッケージ",
    "Terminology Package": "用語パッケージ",
    "Terminology Packages": "用語パッケージ",
    "No terminology package selected.": "用語パッケージが選択されていません。",
    "Categories": "分類",
    "No categories": "分類なし",
    "No categories are available in this terminology package.": "この用語パッケージには利用可能な分類がありません。",
    "Definition": "定義",
    "Full Name / Display Name": "正式名 / 表示名",
    "External Reference": "外部参照",
    "Origin": "出典",
    "Origin: {0}": "出典: {0}",
    "Category: {0}": "分類: {0}",
    "Reference: {0}": "参照: {0}",
    "Usage count: {0}": "使用回数: {0}",
    "Category filter": "分類フィルター",
    "All categories": "すべての分類",
    "Uncategorized": "未分類",
    "Used By": "使用元",
    "Used In": "使用先",
    "Used By CSE Count": "CSE での使用回数",
    "<empty>": "<空>",
    "No terms": "用語なし",
    "Add Term": "用語を追加",
    "Edit Term": "用語を編集",
    "Delete Term": "用語を削除",
    "Delete this term?": "この用語を削除しますか?",
    "Add Category": "分類を追加",
    "Edit Category": "分類を編集",
    "Create Category": "分類を作成",
    "Delete Category": "分類を削除",
    "Delete this category?": "この分類を削除しますか?",
    "Add Terminology Package": "用語パッケージを追加",
    "Create Terminology Package": "用語パッケージを作成",
    "Delete Terminology Package": "用語パッケージを削除",
    "Delete this terminology package?": "この用語パッケージを削除しますか?",
    "Delete Package": "パッケージを削除",
    "Package name": "パッケージ名",
    "Package description": "パッケージの説明",
    "Edit Term": "用語を編集",
    "Create Term": "用語を作成",
    "Create + Add as Context": "作成してコンテキストに追加",
    "Store in": "保存先",
    "No TerminologyPackage is available.": "利用可能な TerminologyPackage がありません。",
    "Term value is required.": "用語の値は必須です。",
    "Choose a target TerminologyPackage.": "対象の TerminologyPackage を選択してください。",
    "Duplicate term value and definition exist in this package.": "このパッケージ内に同じ用語と定義の重複があります。",
    "Concrete term has no description.": "具体的な用語に説明がありません。",
    "Term has no category.": "用語に分類がありません。",
    "Category name": "分類名",
    "Category description": "分類の説明",
    "Category name is required.": "分類名は必須です。",
    "Add Recommended": "推奨を追加",
    "Search": "検索",
    "Missing term": "用語が見つかりません",
    "Term reference could not be resolved.": "用語参照を解決できませんでした。",
    "Define this term from the active terminology scope.": "アクティブな用語スコープからこの用語を定義してください。",

    # ===== Workbench / canvas tabs =====
    "Package Details": "パッケージの詳細",
    "PROPOSAL CREATOR": "提案クリエーター",
    "PROPOSAL PREVIEW": "提案プレビュー",
    "Changes are recorded in the proposal draft. Save it from the review panel.": "変更は提案ドラフトに記録されています。レビューパネルから保存してください。",
    "This is a preview. The project model has not been changed.": "これはプレビューです。プロジェクトモデルは変更されていません。",
    "Edit Proposal": "提案を編集",
    "Discard Draft": "ドラフトを破棄",
    "Exit Preview": "プレビューを終了",
    "No SACM argument model is loaded.": "SACM 論証モデルが読み込まれていません。",
    "Argument package was not found in the loaded SACM model.": "読み込まれた SACM モデルに論証パッケージが見つかりませんでした。",
    "J3377 CAE register file: {0}": "J3377 CAE 登録簿ファイル: {0}",
    "Editable CAE register content will be implemented in a later workflow.": "編集可能な CAE 登録簿の内容は後のワークフローで実装されます。",
    "Evidence register file: {0}": "エビデンス登録簿ファイル: {0}",
    "Editable evidence register content will be implemented in a later workflow.": "編集可能なエビデンス登録簿の内容は後のワークフローで実装されます。",

    # ===== Proposal editor =====
    "Proposal Creator": "提案クリエーター",
    "Edits are recorded in the proposal draft only.": "編集は提案ドラフトにのみ記録されます。",
    "Select a proposal preview element to edit its proposed properties.": "提案するプロパティを編集するには、提案プレビュー要素を選択してください。",
    "The selected proposal preview element no longer exists.": "選択された提案プレビュー要素は既に存在しません。",
    "Could not resolve this preview element for proposal edits.": "提案編集のためのこのプレビュー要素を解決できませんでした。",
    "Existing": "既存",
    "New": "新規",
    "Remove Subtree": "サブツリーを除去",
    "Load a SACM model before editing proposal drafts.": "提案ドラフトを編集する前に SACM モデルを読み込んでください。",
    "Recorded proposal property change.": "提案プロパティの変更を記録しました。",

    # ===== Review panel =====
    "Review": "レビュー",
    "Review status: {0}": "レビュー状態: {0}",
    "Not reviewed": "未レビュー",
    "Open or create a project to store review comments.": "レビューコメントを保存するには、プロジェクトを開くか作成してください。",
    "Select an element to review.": "レビューする要素を選択してください。",
    "Comments ({0})": "コメント ({0})",
    "No review comments for this element.": "この要素のレビューコメントはありません。",
    "Review comment": "レビューコメント",
    "Reviewed by {0}": "レビュー担当者: {0}",
    "not recorded": "未記録",
    "Other Problems ({0})": "その他の問題 ({0})",
    "New Comment": "新しいコメント",
    "Add SCCG violation": "SCCG 違反を追加",
    "Add Review Comment": "レビューコメントを追加",
    "Resolved": "解決済み",
    "Open": "オープン",
    "Original text": "元のテキスト",
    "(empty)": "(空)",
    "SCCG Guideline": "SCCG ガイドライン",
    "Browse SCCG Guidelines": "SCCG ガイドラインを参照",
    "Not Implemented": "未実装",
    "No SCCG guideline violations selected.": "SCCG ガイドライン違反が選択されていません。",
    "Selected": "選択済み",
    "SCCG Guideline Violations": "SCCG ガイドライン違反",
    "Filter SCCG IDs or titles": "SCCG ID またはタイトルでフィルター",
    "Keep filtering to narrow the remaining guidelines.": "残りのガイドラインを絞り込むには、さらにフィルターを続けてください。",
    "No matching SCCG guideline IDs.": "一致する SCCG ガイドライン ID がありません。",
    "Save Proposal": "提案を保存",
    "No proposal for resolved comment.": "解決済みコメントに対する提案はありません。",
    "Create Proposed Change": "提案変更を作成",
    "Proposed change: Valid": "提案変更: 有効",
    "Proposed change: Broken": "提案変更: 破損",
    "Reason: {0}": "理由: {0}",
    "View Proposal": "提案を表示",
    "Apply Proposal": "提案を適用",
    "Delete Proposal": "提案を削除",
    "Problem": "問題",
    "Source": "ソース",
    "Source: {0}": "ソース: {0}",

    # ===== Problems panel =====
    "Problems": "問題",
    "All": "すべて",
    "Validation": "検証",
    "Warnings": "警告",
    "Info": "情報",
    "No problems found.": "問題は見つかりませんでした。",
    "No problems match the current filter.": "現在のフィルターに一致する問題はありません。",
    "Severity": "重大度",
    "Message": "メッセージ",
    "Guideline": "ガイドライン",
    "Fix": "修正",

    # ===== Project explorer =====
    "Project Explorer": "プロジェクトエクスプローラー",
    "No project open.": "プロジェクトが開かれていません。",
    "Argument Packages": "論証パッケージ",
    "Artifact Packages": "成果物パッケージ",
    "Terminology Packages": "用語パッケージ",
    "Interfaces": "インターフェース",
    "Bindings": "バインディング",
    "Other Packages": "その他のパッケージ",
    "(Valid)": "(有効)",
    "(Broken)": "(破損)",
    "Package tree unavailable: {0}": "パッケージツリーを利用できません: {0}",
    "Open in File Explorer": "ファイルエクスプローラーで開く",
    "Add New GSN / SACM File": "新規 GSN / SACM ファイルを追加",
    "Add Evidence Register": "エビデンス登録簿を追加",
    "Add J3377 CAE Register": "J3377 CAE 登録簿を追加",
    "No files": "ファイルなし",

    # ===== SACM viewer =====
    "SACM Viewer": "SACM ビューア",
    "XML File:": "XML ファイル:",
    "No XML files found": "XML ファイルが見つかりません",
    "Directory:": "ディレクトリ:",
    "Project Summary": "プロジェクト概要",
    "CSE Rows": "CSE 行",
    "Evidence Rows": "エビデンス行",
    "  Content: {0}": "  内容: {0}",

    # ===== Registers =====
    "Register assessments could not be loaded, so edits cannot be saved: {0}":
        "登録簿の評価を読み込めなかったため、編集を保存できません: {0}",
    "The assessment of {0} is kept, but the argument no longer links that claim to that evidence. Stored: {1}":
        "{0} の評価は保持されていますが、論証はその主張とエビデンスを結び付けていません。保存内容: {1}",
    "The assessment of evidence {0} is kept, but that evidence is no longer in the argument. Stored: {1}":
        "エビデンス {0} の評価は保持されていますが、そのエビデンスは論証にありません。保存内容: {1}",
    "Discard assessment": "評価を破棄",
    "No CSE rows were derived from direct claim-evidence relations.": "直接の主張–エビデンス関係から CSE 行が導出されませんでした。",
    "No evidence/work-product rows were derived from the model.": "モデルからエビデンス/作業成果物の行が導出されませんでした。",
    "CSE ID": "CSE ID",
    "Claim ID": "主張 ID",
    "Claim Owner": "主張責任者",
    "Claim Criteria": "主張基準",
    "Evidence ID": "エビデンス ID",
    "Evidence Owner": "エビデンス責任者",
    "Safety Case Owner": "安全ケース責任者",
    "Evidence Criteria": "エビデンス基準",
    "Assessment Status": "評価ステータス",
    "Notes": "メモ",
    "Recency": "新しさ",
    "Maturity": "成熟度",
    "Controlled Environment": "管理環境",

    # ===== Tree view / context menu =====
    "Orphans ({0})": "孤立 ({0})",
    "Click to open in the Problems panel.": "クリックして問題パネルで開きます。",
    "(no message)": "(メッセージなし)",
    "error": "エラー",
    "warning": "警告",
    "info": "情報",
    "problem": "問題",
    "1 {0}: {1}": "1 件の{0}: {1}",
    "{0} problems · top {1}: {2}": "{0} 件の問題 · トップ {1}: {2}",
    "This node only ({0})": "このノードのみ ({0})",
    "Node and descendants ({0})": "ノードとその子孫 ({0})",
    "Add New Top Goal": "新規トップゴールを追加",
    "(unnamed)": "(名前なし)",
    "No SACM package selected.": "SACM パッケージが選択されていません。",
    "A full editor for this SACM package type will be added in a later workflow.": "この SACM パッケージ種別の完全なエディターは後のワークフローで追加されます。",
    "XML element: {0}": "XML 要素: {0}",
    "Source file: {0}": "ソースファイル: {0}",

    # ===== Misc / GSN canvas / acp_decorator =====
    "(via": "(経由",
    "Add as context": "コンテキストとして追加",

    # ===== Window titles =====
    "Welcome!": "ようこそ!",
    "Argument Navigator": "論証ナビゲーター",

    # ===== Themes =====
    "Dark": "ダーク",
    "Light": "ライト",

    # ===== Welcome screen recent project stats =====
    "{0} claims · {1} strategies · {2} evidence · {3} undeveloped":
        "主張 {0} 件 · 戦略 {1} 件 · エビデンス {2} 件 · 未展開 {3} 件",

    # ===== Modal titles previously left English in ##id constants =====
    "Create baseline": "ベースラインを作成",
    "Reconcile audit log": "監査ログを再構築",

    # ===== Preferences MCP section =====
    "MCP Server": "MCP サーバー",
    "Allow AI clients to read and propose changes":
        "AI クライアントに読み取りと変更提案を許可する",
    "When this is on, an AI client you launch yourself (such as Claude Desktop) can read the open project's safety case and save proposed changes for you to review. Proposals never change the case until you accept them. While this is off, nothing is shared.":
        "これを有効にすると、自分で起動した AI クライアント（Claude Desktop など）が開いているプロジェクトのセーフティケースを読み取り、変更提案を保存できます。提案は承認するまでケースを変更しません。無効の間は何も共有されません。",
    "Could not save the MCP setting: {0}":
        "MCP 設定を保存できませんでした: {0}",
    "Client configuration": "クライアント設定",
    "Copy Configuration": "設定をコピー",
    "Paste into your AI client's MCP settings.":
        "AI クライアントの MCP 設定に貼り付けてください。",
    "The MCP server program was not found next to Assurance Forge.":
        "Assurance Forge の隣に MCP サーバープログラムが見つかりませんでした。",
    "Open a project to see its client configuration.":
        "クライアント設定を表示するにはプロジェクトを開いてください。",

    # ===== Preferences Review section =====
    "Reviewer name": "レビュー担当者名",
    "Save Reviewer Name": "レビュー担当者名を保存",

    # ===== Performance overlay window =====
    "Performance": "パフォーマンス",

    # ===== Misc =====
    "SCCG": "SCCG",

    # ===== Terminology issue messages (set in core/, translated at display) =====
    "Term has no value.": "用語に値がありません。",
    "Duplicate term value and definition exist in this terminology package.":
        "この用語パッケージ内に同じ用語と定義の重複があります。",
    "Term has no external reference/source.": "用語に外部参照/出典がありません。",
    # "Concrete term has no description." and "Term has no category." are already in dict.

    # ===== Delete-block reasons from core/terminology_package_service =====
    "Terminology package contains categories.": "用語パッケージに分類が含まれています。",
    "Terminology package contains terms.": "用語パッケージに用語が含まれています。",
    "Terminology package contains terminology entries.": "用語パッケージに用語項目が含まれています。",
    "Terminology package not found.": "用語パッケージが見つかりません。",

    # ===== Quick-fix labels stored in ProblemItem (translated at display) =====
    "Open duplicates": "重複を開く",
    "Open glossary": "用語集を開く",
    # "Edit term", "Define term", "Open ACP" already in dict.

    # ===== Dynamic problem messages (trf'd at sync time in app/) =====
    "{0} looks like an undefined terminology entry.":
        "{0} は未定義の用語項目のように見えます。",
    "{0} has {1} visible meanings. Choose the intended terminology entry.":
        "{0} には {1} 件の表示可能な意味があります。意図する用語項目を選択してください。",
    "Circular support: {0}. An argument that assumes its own conclusion establishes nothing.":
        "循環した支持関係: {0}。自らの結論を前提とする議論は何も立証しません。",
    "{0} is supported by itself, so it establishes nothing.":
        "{0} は自分自身によって支持されているため、何も立証しません。",
    "Show cycle": "循環を表示",

    # ===== Static problem messages (translated at display via AF_TR) =====
    "The stored confidence assessment needs review because this element changed. Review the value "
    "and reactivate confidence if it is still valid.":
        "この要素が変更されたため、保存された信頼度評価の確認が必要です。値を確認し、まだ有効であれば "
        "信頼度を再アクティブ化してください。",
}


# Plural translations: (singular_msgid, plural_msgid) -> [japanese_form]
# Japanese has nplurals=1, so we provide a single form.
PLURAL_TRANSLATIONS = {
    ("{0} element", "{0} elements"): ["{0} 要素"],
    ("{0} open finding", "{0} open findings"): ["未解決の指摘 {0} 件"],
    ("{0} item needs attention", "{0} items need attention"):
        ["{0} 件の項目に対応が必要です"],
    ("{0} SCCG finding", "{0} SCCG findings"):
        ["SCCG 指摘 {0} 件"],
    ("AI client connected: {0}", "AI clients connected: {0}"):
        ["接続中の AI クライアント: {0}"],
    ("{0} item", "{0} items"): ["{0} 個"],
    ("{0} element will be deleted (highlighted in red).",
     "{0} elements will be deleted (highlighted in red).") :
        ["{0} 個の要素が削除されます (赤でハイライト)。"],
    ("This term value appears {0} time in the current SACM model.",
     "This term value appears {0} times in the current SACM model."):
        ["この用語値は現在の SACM モデルに {0} 回出現します。"],
    ("This category is assigned to {0} term. Remove those assignments before deleting it.",
     "This category is assigned to {0} terms. Remove those assignments before deleting it."):
        ["この分類は {0} 件の用語に割り当てられています。削除前にそれらの割り当てを除去してください。"],
    ("{0} usage found", "{0} usages found"):
        ["{0} 件の使用箇所"],
    ("{0} problem · top: {1}\nClick to open the Problems panel.",
     "{0} problems · top: {1}\nClick to open the Problems panel."):
        ["{0} 件の問題 · トップ: {1}\nクリックして問題パネルを開きます。"],
    ("{0} confidence assessment was marked inactive because its target element changed.",
     "{0} confidence assessments were marked inactive because their target elements changed."):
        ["対象要素が変更されたため、{0} 件の信頼度評価が非アクティブとしてマークされました。"],
    ("{0} more candidate.", "{0} more candidates."):
        ["他に {0} 件の候補"],
    ("{0} legacy expression entry is present and shown read-only by older tooling.",
     "{0} legacy expression entries are present and shown read-only by older tooling."):
        ["{0} 件のレガシー表現項目があり、旧ツールにより読み取り専用で表示されます。"],
    ("{0} operation", "{0} operations"):
        ["{0} 件の操作"],
    ("Proposal draft: {0} operation", "Proposal draft: {0} operations"):
        ["提案ドラフト: {0} 件の操作"],
}


PO_HEADER = (
    'msgid ""\n'
    'msgstr ""\n'
    '"Project-Id-Version: Assurance Forge\\n"\n'
    '"Content-Type: text/plain; charset=UTF-8\\n"\n'
    '"Content-Transfer-Encoding: 8bit\\n"\n'
    '"Language: ja\\n"\n'
    '"Plural-Forms: nplurals=1; plural=0;\\n"\n'
)


def escape_po(value):
    return value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")


def write_po(path):
    lines = [PO_HEADER]
    for msgid in sorted(TRANSLATIONS.keys()):
        msgstr = TRANSLATIONS[msgid]
        lines.append("")
        lines.append(f'msgid "{escape_po(msgid)}"')
        lines.append(f'msgstr "{escape_po(msgstr)}"')
    for (sing, plur), forms in sorted(PLURAL_TRANSLATIONS.items()):
        lines.append("")
        lines.append(f'msgid "{escape_po(sing)}"')
        lines.append(f'msgid_plural "{escape_po(plur)}"')
        for i, form in enumerate(forms):
            lines.append(f'msgstr[{i}] "{escape_po(form)}"')
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {path} ({len(TRANSLATIONS)} plain + {len(PLURAL_TRANSLATIONS)} plural)")


def main():
    repo_root = Path(__file__).resolve().parents[2]
    po_path = repo_root / "assets/locale/ja/LC_MESSAGES/assurance_forge.po"
    mo_path = po_path.with_suffix(".mo")
    write_po(po_path)
    # Recompile .mo using sibling compile_po.py
    compile_script = Path(__file__).with_name("compile_po.py")
    result = subprocess.run([sys.executable, str(compile_script), str(po_path), str(mo_path)],
                            capture_output=True, text=True)
    sys.stderr.write(result.stderr)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.exit(result.returncode)


if __name__ == "__main__":
    main()
