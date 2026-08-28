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
    "draft": "ドラフト",
    "term": "用語",
    "definition": "定義",
    "name": "名前",
    "categories": "カテゴリ",
    "external reference": "外部参照",
    "origin": "出典",
    "Added by the working draft; not yet accepted.": "作業ドラフトで追加されました。まだ受理されていません。",
    "Changed by the working draft ({0}); not yet accepted.": "作業ドラフトで変更されました（{0}）。まだ受理されていません。",
    "{0} changes the accepted argument, which a working draft is open against. Accept or discard the draft from the argument canvas first.": "{0}は、作業ドラフトの元になっている受理済みの論証を変更します。先に論証キャンバスでドラフトを受理または破棄してください。",
    "Terms and categories you add, change or delete here go into the working draft until it is accepted.": "ここで追加・変更・削除した用語とカテゴリは、受理されるまで作業ドラフトに入ります。",
    "Editing the package or deleting a category": "パッケージの編集またはカテゴリの削除",
    "Adding a terminology package": "用語パッケージの追加",
    "Editing a terminology package": "用語パッケージの編集",
    "Deleting a terminology package": "用語パッケージの削除",
    "Deleting a category": "カテゴリの削除",
    "Linking a term to an element": "用語と要素の関連付け",
    "Adding a term as context": "用語をコンテキストとして追加すること",
    "While a working draft is open, new terms and categories go into the case's first glossary ({0}). Select it, or accept or discard the draft first.": "作業ドラフトが開いている間、新しい用語とカテゴリはケースの最初の用語集（{0}）に入ります。それを選択するか、先にドラフトを受理または破棄してください。",
    "Added term {0} to the working draft.": "用語 {0} を作業ドラフトに追加しました。",
    "Updated term {0} in the working draft.": "作業ドラフトの用語 {0} を更新しました。",
    "Deleted term {0} in the working draft.": "作業ドラフトの用語 {0} を削除しました。",
    "Added category {0} to the working draft.": "カテゴリ {0} を作業ドラフトに追加しました。",
    "Updated category {0} in the working draft.": "作業ドラフトのカテゴリ {0} を更新しました。",
    "Added recommended terminology categories to the working draft.": "推奨用語カテゴリを作業ドラフトに追加しました。",
    "Added term {0} to the working draft. Adding it as context changes the accepted argument, so do that once the draft is accepted.": "用語 {0} を作業ドラフトに追加しました。コンテキストとしての追加は受理済みの論証を変更するため、ドラフトの受理後に行ってください。",
    "A glossary will be created in the working draft.": "用語集は作業ドラフトに作成されます。",
    "There is no working draft to edit.": "編集できる作業ドラフトがありません。",
    "Cannot edit while viewing history. Return to Latest to make changes.": "履歴の表示中は編集できません。変更するには「最新」に戻ってください。",
    "Working draft: {0} added, {1} changed, {2} removed in this glossary. Accept the draft on the argument canvas to make them part of the case.": "作業ドラフト: この用語集で {0} 件追加、{1} 件変更、{2} 件削除。ケースに反映するには論証キャンバスでドラフトを受理してください。",
    "The draft was accepted, but the audit log could not record it: {0}": "ドラフトは受理されましたが、監査ログに記録できませんでした: {0}",
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
    "Developer": "開発者",
    "Developer tools": "開発者ツール",
    "Shows frame and culling counters in the menu bar, and the AI Debug tab.": "メニューバーにフレームおよびカリングのカウンターを表示し、AI デバッグタブを表示します。",
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
    "Remove {0}": "{0}を除去",
    "New {0}": "新規：{0}",
    "Support": "支持関係",
    "Promotion is awaiting durable SACM completion.": "プロモーションは SACM への確実な保存完了を待っています。",
    "Promotion is recorded, but the accepted SACM file is not yet confirmed. The draft is retained and cannot be edited or discarded.":
        "プロモーションは記録されましたが、承認済み SACM ファイルへの保存はまだ確認されていません。ドラフトは保持され、編集または破棄できません。",
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
    # ===== Working draft (ADR 0009 / ADR 0010) =====
    "Sources: {0}": "提案元: {0}",
    "The argument changed since this draft was written, so none of it is being applied. Inspect or discard it before continuing.": "このドラフトの作成後に議論が変更されたため、いずれも適用されていません。続行する前に内容を確認するか破棄してください。",
    "This draft cannot be shown because one of its changes no longer applies. The accepted argument is displayed instead.": "このドラフトの変更のいずれかが適用できないため表示できません。代わりに受理済みの議論を表示しています。",
    "Working draft": "作業ドラフト",
    "Accepted baseline": "受理済みベースライン",
    "Changes only": "変更のみ",
    "Accept all": "すべて受理",
    "Accept draft": "ドラフトを受理",
    "Discard draft": "ドラフトを破棄",
    "{0} added, {1} changed, {2} removed.": "追加 {0} 件、変更 {1} 件、削除 {2} 件。",
    "Accepted the working draft: {0} added, {1} changed, {2} removed.": "作業ドラフトを受理しました: 追加 {0} 件、変更 {1} 件、削除 {2} 件。",
    "The draft was accepted, but its change record could not be cleared: {0}": "ドラフトは受理されましたが、その変更記録を消去できませんでした: {0}",
    "The draft was accepted and written, but the argument could not be re-read: {0}": "ドラフトは受理され書き込まれましたが、議論を再読み込みできませんでした: {0}",
    "The edit was made, but the draft could not be written to disk: {0}": "編集は適用されましたが、ドラフトをディスクに書き込めませんでした: {0}",
    "The argument changed since this draft was written.": "このドラフトの作成後に議論が変更されました。",
    "One of this draft's changes no longer applies.": "このドラフトの変更のいずれかが適用できなくなっています。",
    "Could not accept the working draft: {0}": "作業ドラフトを受理できませんでした: {0}",
    "Discarded the working draft. The accepted argument is unchanged.": "作業ドラフトを破棄しました。受理済みの議論は変更されていません。",
    "Could not discard the working draft: {0}": "作業ドラフトを破棄できませんでした: {0}",
    "This element is proposed and is not in the accepted argument.": "この要素は提案されたもので、受理済みの議論には含まれていません。",
    "This element is proposed to change.": "この要素は変更が提案されています。",
    "This element is proposed for removal.": "この要素は削除が提案されています。",
    "Accepted": "受理済み",
    "Contributions": "提案の内訳",
    "{0} — {1}": "{0} — {1}",
    "Accepting this also accepts: {0}": "これを受理すると次も受理されます: {0}",
    "Accept this change": "この変更を受理",
    "Reject this change": "この変更を却下",
    "Could not accept this change: {0}": "この変更を受理できませんでした: {0}",
    "Could not reject this change: {0}": "この変更を却下できませんでした: {0}",
    "My edits": "自分の編集",
    "{0} — accepted": "{0} — 受理済み",
    "{0} — working draft": "{0} — 作業ドラフト",
    "\"{0}\" cannot be edited while a working draft is active.": "作業ドラフトが有効な間は「{0}」を編集できません。",
    "Could not record the edit in the draft: {0}": "ドラフトに編集を記録できませんでした: {0}",
    "The working draft does not change this element. Select an element marked NEW, EDIT or MULTIPLE CHANGES to review and accept it.": "作業ドラフトはこの要素を変更しません。NEW、EDIT、MULTIPLE CHANGES と表示された要素を選択すると内容を確認して受理できます。",
    "Select the element to add under first.": "先に追加先の要素を選択してください。",
    "Select the element to remove first.": "先に削除する要素を選択してください。",
    "Could not add to the draft: {0}": "ドラフトに追加できませんでした: {0}",
    "Could not remove in the draft: {0}": "ドラフトで削除できませんでした: {0}",
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
    "AI review completed with no findings.": "AI レビューが完了し、指摘事項はありませんでした。",
    "AI review is already running.": "AI レビューは既に実行中です。",
    "Profile: {0}": "プロファイル: {0}",
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
    "SCCG profiles unavailable.": "SCCG プロファイルを使用できません。",
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
    "Method": "方式",
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

    # ===== SACM element type display names =====
    "Claim": "主張",
    "Argument Reasoning": "議論の根拠",
    "Artifact": "成果物",
    "Artifact Reference": "成果物参照",
    "Expression": "式",
    "Asserted Inference": "主張された推論",
    "Asserted Context": "主張された文脈",
    "Asserted Evidence": "主張された証拠",

    # ===== Toolbar =====
    "Fit to view": "全体を表示",
    "Export GSN SVG": "GSN SVG をエクスポート",
    "Unavailable right now.": "現在は利用できません。",

    # ===== Status bar =====
    "No project open": "プロジェクトが開かれていません",
    "Unsaved changes": "未保存の変更",
    "Saved": "保存済み",
    "Selected: {0}": "選択中: {0}",
    "Open the Problems panel": "問題パネルを開く",

    # ===== Problems panel =====
    "Problems": "問題",
    "All": "すべて",
    "Validation": "検証",
    "Warnings": "警告",
    "Info": "情報",
    "Warning": "警告",
    "Error": "エラー",
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

    # ===== SCCG staged-check findings (set in core/sccg, translated at display
    # by check id in app/areas/staged_finding_text.cpp; the English template
    # must stay byte-identical to the detail core/sccg/staged_checks.cpp
    # builds -- a test holds the two together) =====
    "This claim has no support and is not marked undeveloped, so a reviewer cannot tell whether "
    "evidence is missing or still to come. Give it support, or mark it undeveloped to say so "
    "deliberately.":
        "この主張には支持がなく、未展開とも示されていないため、レビュー担当者はエビデンスが欠けているのか"
        "これから追加されるのかを判断できません。支持を与えるか、意図的である旨を示すために未展開として"
        "マークしてください。",
    "This strategy develops into nothing. A decomposition step that produces no sub-claims states "
    "an inference the argument never makes.":
        "この戦略は何にも展開されていません。サブ主張を生まない分解ステップは、議論が一度も行わない推論を"
        "宣言していることになります。",
    "This claim is broken into sub-claims with no reasoning step saying how they were chosen or "
    "why together they support it. Add a strategy stating the decomposition rule.":
        "この主張はサブ主張に分解されていますが、それらがどのように選ばれ、なぜ合わせて主張を支持するのかを"
        "述べる推論ステップがありません。分解の根拠を示す戦略を追加してください。",
    "This is a solution -- the artefact or observation the argument rests on -- so it should be a "
    "leaf. Elements hanging beneath it are not carrying the role the structure says they are.":
        "これはソリューション、すなわち議論が依拠する成果物または観察であり、末端要素であるべきです。"
        "その下にぶら下がる要素は、構造が示す役割を果たしていません。",
    "These operations put a claim in its own support chain, so the argument supports itself and "
    "establishes nothing.":
        "これらの操作は主張を自身の支持連鎖に組み込むため、議論が自らを支持することになり、何も立証しません。",
    "This claim uses \"{0}\", which SCCG names as a term needing bounds. Say what it means here -- "
    "against which hazards, in which operating conditions, to what standard -- in the claim or in "
    "attached context.":
        "この主張は「{0}」を使用しており、SCCG はこれを境界付けが必要な用語として挙げています。どのハザード"
        "に対して、どの運用条件で、どの基準に照らしてなのか、その意味を主張内または付属するコンテキストで"
        "明示してください。",
    "This claim joins \"{0} and {1}\" -- two distinct properties needing different evidence and "
    "review. Give each its own goal, so one can fail without hiding the other.":
        "この主張は「{0} と {1}」を結合しており、これらは異なるエビデンスと審査を必要とする別個の性質です。"
        "それぞれを独立したゴールにして、一方の不成立がもう一方を隠さないようにしてください。",
    "This claim chains \"{0} and {1}\" -- different logical steps answering different review "
    "questions. Give each step its own claim, and let the structure show the decomposition.":
        "この主張は「{0} と {1}」を連結しており、これらは異なる審査上の問いに答える別個の論理ステップです。"
        "各ステップを独立した主張にして、分解は構造で示してください。",
    "This claim carries its own reasoning (\"{0}\"), so a reviewer cannot tell the claim from the "
    "argument for it. State the claim alone; the reasoning belongs in a strategy and the evidence "
    "in a solution.":
        "この主張は自らの論拠（「{0}」）を含んでいるため、レビュー担当者は主張とその論証を区別できません。"
        "主張は主張のみを述べ、論拠は戦略に、エビデンスはソリューションに置いてください。",
    "This text calls the work \"{0}\". Promotional language persuades nobody reviewing a safety "
    "argument; state what was shown, and under which assumptions.":
        "このテキストは成果を「{0}」と称しています。宣伝的な言葉は安全性議論のレビューでは何の説得力も"
        "持ちません。何がどの前提の下で示されたのかを述べてください。",
    "This evidence reference carries no owner, version, date, or status, so a reviewer cannot tell "
    "which artifact was assessed or whether it has changed since. Cite the controlled version.":
        "このエビデンス参照には所有者・バージョン・日付・ステータスのいずれもないため、レビュー担当者は"
        "どの成果物が評価されたのか、その後変更されていないかを判断できません。管理されたバージョンを"
        "引用してください。",
    "This evidence names an artifact but no part of it, so a reviewer cannot find the material "
    "that supports the claim. Cite the section, table, figure or scenarios the argument rests on.":
        "このエビデンスは成果物を示していますが、その一部を特定していないため、レビュー担当者は主張を"
        "支持する箇所を見つけられません。議論が依拠するセクション、表、図、またはシナリオを引用して"
        "ください。",
    "This evidence cites \"{0}\", which reads as live mutable content. Cite a fixed version, "
    "revision, or archived snapshot, so the reviewed argument always refers to the same content.":
        "このエビデンスは「{0}」を引用しており、これは変更されうるライブコンテンツと読めます。レビューされた"
        "議論が常に同じ内容を参照するよう、固定されたバージョン、リビジョン、またはアーカイブ済み"
        "スナップショットを引用してください。",
    "This text treats the absence of discovered evidence as support. Not finding something does "
    "not establish the claim; argue from what the applied methods can show.":
        "このテキストは、反証が見つからなかったことを支持として扱っています。何かが見つからないことは主張を"
        "立証しません。適用した手法が示せることに基づいて論証してください。",
    "Submitted despite these problem findings, acknowledged by the author:":
        "作成者が以下の問題指摘を承知の上で提出しました:",

    # ===== GSN v3 well-formedness diagnostics (app/structure_problem_sync) =====
    "Relationship {0} refers to {1}, which is not in this case. The relationship is not drawn, "
    "so the argument shown is smaller than the one stored.":
        "関係 {0} はこのケースに存在しない {1} を参照しています。この関係は描画されないため、"
        "表示される議論は保存されている議論より小さくなります。",
    "Challenge {0} targets {1}, which is not in this case. The challenge is stored but never "
    "shown against anything.":
        "反証 {0} はこのケースに存在しない {1} を対象にしています。この反証は保存されていますが、"
        "どの要素に対しても表示されません。",
    "{0} is supported by relationship {1}, but only a Goal or a Strategy can be supported in GSN.":
        "{0} は関係 {1} によって支持されていますが、GSN で支持を受けられるのはゴールまたは戦略のみです。",
    "{0} is given context by relationship {1}, but only a Goal or a Strategy is declared in "
    "context in GSN.":
        "{0} は関係 {1} によってコンテキストを与えられていますが、"
        "GSN でコンテキストの中で述べられるのはゴールまたは戦略のみです。",
    "Strategy {0} is wired as an end of relationship {1}. A Strategy is the reasoning of an "
    "inference, not one of its ends.":
        "戦略 {0} が関係 {1} の端点として接続されています。戦略は推論の理由付けであり、"
        "その端点ではありません。",
    "{0} discharges a goal through relationship {1}, but it is not a Solution. Only a reference "
    "to evidence can discharge a goal.":
        "{0} は関係 {1} を通じてゴールを立証していますが、ソリューションではありません。"
        "ゴールを立証できるのは証拠への参照のみです。",
    "{0} and {1} both use the GSN identifier {2}, so neither can be referred to unambiguously.":
        "{0} と {1} が同じ GSN 識別子 {2} を使用しているため、どちらも一意に参照できません。",
    "{0} is marked undeveloped but is already supported. Either the decorator or the support "
    "is out of date.":
        "{0} は未展開と示されていますが、すでに支持されています。"
        "装飾か支持のいずれかが古くなっています。",

    # ===== GSN repair affordances (canvas edge menu, relationship inspector,
    # and the per-rule quick fixes on the Problems panel) =====
    "Remove relationship": "関係を削除",
    "Withdraws the relationship. Both elements are kept; one left with no remaining parent "
    "shows as an orphan.":
        "関係を取り消します。両方の要素は保持され、親がなくなった要素は孤立要素として表示されます。",
    "Drop broken reference": "壊れた参照を削除",
    "Remove challenge": "反証を削除",
    "Move to reasoning": "理由付けへ移動",
    "Renumber": "採番し直す",
    "Clear decorator": "装飾を解除",

    # ===== Static problem messages (translated at display via AF_TR) =====
    "The stored confidence assessment needs review because this element changed. Review the value "
    "and reactivate confidence if it is still valid.":
        "この要素が変更されたため、保存された信頼度評価の確認が必要です。値を確認し、まだ有効であれば "
        "信頼度を再アクティブ化してください。",

    # ===== Integrated draft workspace: Draft Changes panel and rejection scope =====
    'A change this one was built on was rejected, so it is no longer applied. Its author can retarget it at what remains.': 'この変更が前提としていた変更が却下されたため、現在は適用されていません。作成者が残っている内容に向けて修正できます。',
    'A change this one was built on was rejected, so it no longer applies.': 'この変更が前提としていた変更が却下されたため、適用できません。',
    'AI client': 'AI クライアント',
    'Accept': '受理',
    'Accepted the change. The rest of the draft is still unaccepted.': '変更を受理しました。ドラフトの残りは未受理のままです。',
    'Accepting this also accepts:': 'これを受理すると次も受理されます:',
    'Built on it:': 'これを前提とする変更:',
    'Could not accept the draft: {0}': 'ドラフトを受理できませんでした: {0}',
    'The last accept did not happen: {0}': '直前の受理は行われませんでした: {0}',
    'A project named "{0}" is already in this folder. Choose another name.': 'このフォルダーには「{0}」という名前のプロジェクトが既にあります。別の名前を選んでください。',
    'Choose a parent location.': '親フォルダーを選んでください。',
    'Depends on:': '前提とする変更:',
    'Draft Changes': 'ドラフトの変更',
    'Findings (advisory):': '指摘 (参考):',
    'Glossary:': '用語集:',
    '{0} (removed)': '{0}（削除）',
    'Guidelines: {0}': 'ガイドライン: {0}',
    'Keep them for review': '確認のために残す',
    'No unaccepted changes. The argument on screen is the accepted one.': '未受理の変更はありません。画面上の議論は受理済みのものです。',
    'Promotion is recorded, but the accepted SACM file is not yet confirmed. The draft is retained and cannot be edited or accepted.': '受理は記録されましたが、受理済み SACM ファイルはまだ確認されていません。ドラフトは保持され、編集も受理もできません。',
    'Rationale:': '根拠:',
    'Reject': '却下',
    'Reject dependent changes?': '前提とする変更も却下しますか?',
    'Reject them too': 'まとめて却下',
    'Rejecting:': '却下する変更:',
    'Review changes': '変更を確認',
    'Review items: {0}': 'レビュー項目: {0}',
    'SCCG AI review': 'SCCG AI レビュー',
    'State: {0}': '状態: {0}',
    'There is nothing to reject.': '却下できるものがありません。',
    'They stop being applied to the working draft and are marked as needing attention, so their author can retarget them.': '作業ドラフトへの適用が停止され、要対応として表示されます。作成者が対象を修正できます。',
    'This draft cannot be shown because one of its changes no longer applies.': 'このドラフトは、含まれる変更のひとつが適用できなくなったため表示できません。',
    'This draft could not be applied to the accepted argument.': 'このドラフトを受理済みの議論に適用できませんでした。',
    'This draft could not be applied: {0}': 'このドラフトを適用できませんでした: {0}',
    'Untitled change ({0})': '無題の変更 ({0})',
    'being written': '作成中',
    'imported proposal': '取り込んだ提案',
    'needs attention': '要対応',
    'person': '人',
    'ready for a decision': '判断待ち',
    'rejected': '却下済み',
    'session {0}': 'セッション {0}',
    'unknown': '不明',

    # ===== Status-bar messages (#252) =====
    # The status line was the one user-visible surface that bypassed ui::i18n:
    # 65 sites in src/app, exactly one of them localized, so a Japanese user got
    # a fully translated UI and an English status line. Converted at the source,
    # because the render site (sacm_viewer_panel.cpp) prints the stored string
    # and cannot translate a message that was built at runtime.
    "Create or open a project first.":
        "まずプロジェクトを作成するか開いてください。",
    "Review item save failed: {0}":
        "レビュー項目の保存に失敗しました: {0}",
    "Project saved: {0}":
        "プロジェクトを保存しました: {0}",
    "Warning: could not read the draft for this argument: {0}":
        "警告: この議論のドラフトを読み込めませんでした: {0}",
    "Warning: could not read the working draft for this argument: {0}":
        "警告: この議論の作業ドラフトを読み込めませんでした: {0}",
    "Discarded proposal draft.":
        "提案ドラフトを破棄しました。",
    "Select an element before changing manual review status.":
        "手動レビュー状態を変更する前に要素を選択してください。",
    "Open or create a project before changing review status.":
        "レビュー状態を変更する前にプロジェクトを開くか作成してください。",
    "Enter a reviewer name before changing review status.":
        "レビュー状態を変更する前にレビュー担当者名を入力してください。",
    "Could not update manual review status.":
        "手動レビュー状態を更新できませんでした。",
    "Change could not be accepted: {0}":
        "変更を受け入れられませんでした: {0}",
    "Change accepted. Undo reverses it like any other edit.":
        "変更を受け入れました。他の編集と同様に元に戻せます。",
    "Change could not be rejected: {0}":
        "変更を却下できませんでした: {0}",
    "Change rejected.":
        "変更を却下しました。",
    "Confidence save failed: {0}":
        "確信度の保存に失敗しました: {0}",
    "Register assessment save failed: {0}":
        "登録簿評価の保存に失敗しました: {0}",
    "GSN SVG export failed: no project is open.":
        "GSN SVG のエクスポートに失敗しました: プロジェクトが開かれていません。",
    "GSN SVG export failed: no SACM safety case is open.":
        "GSN SVG のエクスポートに失敗しました: SACM セーフティケースが開かれていません。",
    "GSN SVG export failed: {0}":
        "GSN SVG のエクスポートに失敗しました: {0}",
    "GSN SVG exported with warnings. See Problems/Export log.":
        "GSN SVG を警告付きでエクスポートしました。問題／エクスポートログを確認してください。",
    "GSN SVG exported to {0}":
        "GSN SVG を {0} にエクスポートしました",
    "Browse failed: {0}":
        "参照に失敗しました: {0}",
    "SACM file is already open: {0}":
        "SACM ファイルは既に開かれています: {0}",
    "Strategy encoding migration failed: {0}":
        "ストラテジ表現の移行に失敗しました: {0}",
    "Audit bus init failed: {0}":
        "監査バスの初期化に失敗しました: {0}",
    "Cannot reconcile audit log: no project is open.":
        "監査ログを整合できません: プロジェクトが開かれていません。",
    "Cannot reconcile audit log: no SACM file is active.":
        "監査ログを整合できません: 有効な SACM ファイルがありません。",
    "Cannot reconcile audit log: active SACM file is no longer listed in the project.":
        "監査ログを整合できません: 有効な SACM ファイルがプロジェクトに登録されていません。",
    "Cannot reconcile audit log: failed to save current SACM file.":
        "監査ログを整合できません: 現在の SACM ファイルを保存できませんでした。",
    "Audit reconciliation failed: {0}":
        "監査ログの整合に失敗しました: {0}",
    "Audit log reconciled. Previous artifacts backed up to {0}.":
        "監査ログを整合しました。以前の成果物は {0} にバックアップしました。",
    "Save the current SACM file before opening another package.":
        "別のパッケージを開く前に現在の SACM ファイルを保存してください。",
    "Opened argument package; no focusable argument element was found in the package.":
        "議論パッケージを開きましたが、フォーカスできる議論要素が見つかりませんでした。",
    "Opened SACM file, but no editable package model was available.":
        "SACM ファイルを開きましたが、編集可能なパッケージモデルがありませんでした。",
    "Terminology package was not found in the editable model.":
        "編集可能なモデルに用語パッケージが見つかりませんでした。",
    "Open a project before removing files.":
        "ファイルを削除する前にプロジェクトを開いてください。",
    "Removing this file type is not supported here.":
        "この種類のファイルの削除はここではサポートされていません。",
    "Remove file failed: {0}":
        "ファイルの削除に失敗しました: {0}",
    "Removed {0}.":
        "{0} を削除しました。",
    "Open a project before revealing files.":
        "ファイルの場所を表示する前にプロジェクトを開いてください。",
    "Could not open File Explorer: {0}":
        "エクスプローラーを開けませんでした: {0}",
    "Save the current SACM file before removing a package.":
        "パッケージを削除する前に現在の SACM ファイルを保存してください。",
    "Could not load an editable SACM package model.":
        "編集可能な SACM パッケージモデルを読み込めませんでした。",
    "Removing this package type is not supported yet.":
        "この種類のパッケージの削除はまだサポートされていません。",
    "terminology package":
        "用語パッケージ",
    "argument package":
        "議論パッケージ",
    "artifact package":
        "成果物パッケージ",
    "Remove {0} failed: {1}":
        "{0} の削除に失敗しました: {1}",
    "Removed {0} {1}.":
        "{0} {1} を削除しました。",
    "Register problem does not identify an assessment.":
        "登録簿の問題が評価を特定していません。",
    "Register assessments could not be loaded, so nothing can be discarded: {0}":
        "登録簿の評価を読み込めなかったため、破棄できるものがありません: {0}",
    "That register assessment was already discarded.":
        "その登録簿評価は既に破棄されています。",
    "Discarded the assessment of {0}. Close the project without saving to keep it after all.":
        "{0} の評価を破棄しました。保存せずにプロジェクトを閉じれば元に戻せます。",
    "ACP problem does not identify an ACP.":
        "ACP の問題が ACP を特定していません。",
    "Opened {0}":
        "{0} を開きました",
    "Project opened, but no SACM file could be loaded.":
        "プロジェクトを開きましたが、SACM ファイルを読み込めませんでした。",
    "Review items could not be loaded: {0}":
        "レビュー項目を読み込めませんでした: {0}",
    "Confidence assessments could not be loaded: {0}":
        "確信度評価を読み込めませんでした: {0}",
    "Register assessments could not be loaded: {0}":
        "登録簿評価を読み込めませんでした: {0}",
    "AI clients cannot connect: {0}":
        "AI クライアントは接続できません: {0}",
    "AI client access request":
        "AI クライアントのアクセス要求",
    "nothing is ready to accept -- {0} being written, {1} needing your attention":
        "受理できる変更がありません -- 作成中 {0} 件、要確認 {1} 件",
    "\"{0}\" is asking to read the open project and propose draft changes.":
        "「{0}」が、開いているプロジェクトの読み取りとドラフト変更の提案を求めています。",
    "Project: {0}":
        "プロジェクト: {0}",
    "Nothing is shared until you allow it. Access lasts while this project stays open, "
    "and you can withdraw it at any time by disabling MCP in Preferences.":
        "許可するまで何も共有されません。アクセスはこのプロジェクトが開いている間だけ有効で、"
        "環境設定で MCP を無効にすればいつでも取り消せます。",
    "Allow while open":
        "開いている間許可",
    "Deny":
        "拒否",
    "{0} — access granted":
        "{0} — アクセス許可済み",
    "{0} — awaiting your approval":
        "{0} — 承認待ち",
    "{0} — access denied":
        "{0} — アクセス拒否",
    "This project appears to be open in another Assurance Forge instance (process {0}). "
    "Two instances editing one project can corrupt its draft state.":
        "このプロジェクトは別の Assurance Forge インスタンス（プロセス {0}）で開かれているようです。"
        "2 つのインスタンスで同じプロジェクトを編集すると、ドラフト状態が破損するおそれがあります。",
    "Accepted, but undoing this will not bring the draft back: {0}":
        "受け入れましたが、これを元に戻してもドラフトは復元されません: {0}",
    "Accepted, but the draft could not be updated: {0}":
        "受け入れましたが、ドラフトを更新できませんでした: {0}",
    "Please select an af.proj file.":
        "af.proj ファイルを選択してください。",
    "Undo failed: {0}":
        "元に戻す操作に失敗しました: {0}",
    "Undo unavailable: no project audit bus.":
        "元に戻せません: プロジェクトの監査バスがありません。",
    "Undo unavailable: no project loaded.":
        "元に戻せません: プロジェクトが読み込まれていません。",
    "Cannot undo while viewing history. Return to Latest to make changes.":
        "履歴表示中は元に戻せません。変更するには「最新」に戻ってください。",
    "Nothing to undo.":
        "元に戻す操作はありません。",
    "Reached snapshot or baseline — restore from history to go further back.":
        "スナップショットまたは基準点に到達しました — さらに遡るには履歴から復元してください。",
    "Cannot undo this acceptance: the draft it came from could not be read ({0}). Remove {1} to undo without restoring it.":
        "この受け入れを元に戻せません: 元のドラフトを読み込めませんでした ({0})。復元せずに元に戻すには {1} を削除してください。",
    "Cannot undo this acceptance here: it belongs to {0}. Open that argument and undo there, so its draft is restored with it.":
        "この受け入れはここでは元に戻せません: {0} に属しています。その議論を開いてそこで元に戻すと、ドラフトも一緒に復元されます。",
    " — but the draft it came from was not restored: {0}":
        " — ただし元のドラフトは復元されませんでした: {0}",
    "Undid: {0}{1}":
        "元に戻しました: {0}{1}",

    # Secure-storage unavailability (#53). Two causes, two fixes: a build
    # with no keyring support needs a rebuild, a build whose keyring is not
    # running needs the keyring started.
    "Secure storage is unavailable: this build has no keyring support.":
        "セキュアストレージを利用できません: このビルドにはキーリング対応が含まれていません。",
    "Secure storage is unavailable: no keyring service is running.":
        "セキュアストレージを利用できません: キーリングサービスが実行されていません。",
    # Evidence register actions.
    "Actions": "操作",
    "Location": "場所",
    "Show in argument": "アーギュメントで表示",
    "Remove evidence": "エビデンスを削除",
    "Open the file or URL": "ファイルまたは URL を開く",
    "Path or URL": "パスまたは URL",
    "Could not record the location in the draft: {0}": "ドラフトに場所を記録できませんでした: {0}",
    "Could not open the evidence location: {0}": "エビデンスの場所を開けませんでした: {0}",
    "Version": "バージョン",
    "Date": "日付",
    "Move into SACM": "SACM に移動",
    "Browse for a file": "ファイルを参照",
    "Click to open the file or URL.": "クリックしてファイルまたは URL を開きます。",
    "Add evidence": "エビデンスを追加",
    "Add Evidence": "エビデンスの追加",
    "Statement": "記述",
    "Supports": "支持対象",
    "(none)": "(なし)",
    "Used by": "使用元",
    "What this evidence supports": "このエビデンスが支持する対象",
    "No claim rests on this evidence.": "このエビデンスに依拠するクレームはありません。",
    "Link to": "リンク先",
    "Choose an element…": "要素を選択…",
    "Part of a relationship that also supports other elements; withdraw it from the canvas.": "他の要素も支持する関係の一部です。キャンバスから解除してください。",
    "Could not link in the draft: {0}": "ドラフトでリンクできませんでした: {0}",
    "Could not unlink in the draft: {0}": "ドラフトでリンクを解除できませんでした: {0}",
    "There is no link from {0} to {1}.": "{0} から {1} へのリンクはありません。",
    "That link is part of a relationship that also supports other elements; remove it from the canvas.": "そのリンクは他の要素も支持する関係の一部です。キャンバスから削除してください。",
    "Stored in the project file until moved into SACM.": "SACM に移動するまでプロジェクトファイルに保存されます。",
    "No project-file assessments match evidence in the argument.": "アーギュメント内のエビデンスに一致するプロジェクトファイルの評価はありません。",
    "Could not record the evidence attribute in the draft: {0}": "ドラフトにエビデンス属性を記録できませんでした: {0}",
    "Could not move assessments into the draft: {0}": "評価をドラフトに移動できませんでした: {0}",
}


# Plural translations: (singular_msgid, plural_msgid) -> [japanese_form]
# Japanese has nplurals=1, so we provide a single form.
PLURAL_TRANSLATIONS = {
    ('{0} assessment is stored in the project file rather than the SACM document.',
     '{0} assessments are stored in the project file rather than the SACM document.'):
        ['{0} 件の評価が SACM ドキュメントではなくプロジェクトファイルに保存されています。'],
    ('Moved {0} assessment into the SACM document.',
     'Moved {0} assessments into the SACM document.'):
        ['{0} 件の評価を SACM ドキュメントに移動しました。'],
    ('Rejected {0} change.',
     'Rejected {0} changes.'):
        ['変更 {0} 件を却下しました。'],
    ('Rejected {0} change. The accepted argument is unchanged.',
     'Rejected {0} changes. The accepted argument is unchanged.'):
        ['変更 {0} 件を却下しました。受理済みの議論は変更されていません。'],
    ('{0} dependent change was rejected too.',
     '{0} dependent changes were rejected too.'):
        ['前提とする変更 {0} 件もあわせて却下しました。'],
    ('{0} change now needs attention before it can be accepted.',
     '{0} changes now need attention before they can be accepted.'):
        ['受理するには {0} 件の変更に対応が必要です。'],
    ('{0} element added',
     '{0} elements added'):
        ['要素 {0} 件を追加'],
    ('{0} element changed',
     '{0} elements changed'):
        ['要素 {0} 件を変更'],
    ('{0} element removed',
     '{0} elements removed'):
        ['要素 {0} 件を削除'],
    ('{0} operation, nothing applied yet',
     '{0} operations, nothing applied yet'):
        ['操作 {0} 件、まだ何も適用されていません'],
    ('{0} other change is built on the one you are rejecting, so it can no longer be applied on its own.',
     '{0} other changes are built on the one you are rejecting, so they can no longer be applied on their own.'):
        ['却下しようとしている変更を前提とする変更が {0} 件あり、単独では適用できなくなります。'],
    ('{0} relationship added',
     '{0} relationships added'):
        ['関係 {0} 件を追加'],
    ('{0} relationship removed',
     '{0} relationships removed'):
        ['関係 {0} 件を削除'],
    ('{0} unaccepted change',
     '{0} unaccepted changes'):
        ['未受理の変更 {0} 件'],
    ("WORKING DRAFT — {0} unaccepted change", "WORKING DRAFT — {0} unaccepted changes"): ["作業ドラフト — 未受理の変更 {0} 件"],
    ("Accepted {0} draft change.", "Accepted {0} draft changes."): ["ドラフトの変更 {0} 件を受理しました。"],
    ("{0} change still being written was left for its author.",
     "{0} changes still being written were left for their authors."):
        ["作成中の変更 {0} 件は作成者に委ねられました。"],
    ("{0} change needs your attention before it can be accepted.",
     "{0} changes need your attention before they can be accepted."):
        ["変更 {0} 件は受理する前に確認が必要です。"],
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
    ("Deleting it also removes {0} element that references it:",
     "Deleting it also removes {0} elements that reference it:"):
        ["削除すると、この用語を参照する {0} 個の要素も削除されます:"],
    ("Deleted term and {0} element that referenced it.",
     "Deleted term and {0} elements that referenced it."):
        ["用語と、それを参照していた {0} 個の要素を削除しました。"],
    ("Deleting it would also remove {0} element that references it, which the working draft cannot do. Remove that reference first, or accept or discard the draft.",
     "Deleting it would also remove {0} elements that reference it, which the working draft cannot do. Remove those references first, or accept or discard the draft."):
        ["削除すると、それを参照している {0} 個の要素も削除されますが、作業ドラフトではできません。先にその参照を削除するか、ドラフトを受理または破棄してください。"],
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
