// The interface's words, in English and Japanese. The core sends codes and
// their arguments; they are worded here.

export type Language = 'en' | 'ja';

const en = {
  open: 'Open…',
  english: 'English',
  japanese: '日本語',
  language: 'Language',
  theme: 'Theme',
  themeSystem: 'System',
  themeLight: 'Light',
  themeDark: 'Dark',
  canvas: 'GSN diagram',
  diagram: 'Diagram',
  menu: 'Menu',
  file: 'File',
  view: 'View',
  panels: 'Panels',
  appearanceTheme: 'Appearance: Theme',
  appearanceLanguage: 'Appearance: Language',
  toolbar: 'Tools',
  caseExplorerInText: 'case explorer',
  argumentNavigatorInText: 'argument navigator',
  caseExplorer: 'Case Explorer',
  caseExplorerEmpty: "The file’s packages will be here.",
  argumentNavigator: 'Argument Navigator',
  argumentNavigatorEmpty: 'The argument tree will be here.',
  elementProperties: 'Element Properties',
  elementPropertiesInText: 'element properties',
  elementPropertiesEmpty: 'Nothing is selected.',
  problems: 'Problems',
  problemsInText: 'problems',
  problemsEmpty: 'The problem list will be here.',
  hide: 'Hide {name}',
  show: 'Show {name}',
  resize: 'Resize {name}',
  splitterHint: 'Drag to resize. Double-click to reset.',
  sideDock: 'Side dock',
  sideDockInText: 'side dock',
  starting: 'Starting…',
  failed: 'The core did not start.',
  fontUnreadable: 'The core did not start: a font file could not be read ({face}).',
  fontMissing: 'The core did not start: a font file could not be loaded.',
  empty: 'Open a SACM 2.3 file, or drop one here.',
  opened: '{nodes} nodes, {edges} edges',
  findings: '{errors} errors, {warnings} warnings',
  notOpened: 'Not opened: {reason}',
  unreadable: 'Could not read the file.',
  relayoutFailed: 'The diagram could not be laid out again.',
};

export type MessageKey = keyof typeof en;

const ja: Record<MessageKey, string> = {
  open: '開く…',
  english: 'English',
  japanese: '日本語',
  language: '言語',
  theme: 'テーマ',
  themeSystem: 'システム',
  themeLight: 'ライト',
  themeDark: 'ダーク',
  canvas: 'GSN 図',
  diagram: '図',
  menu: 'メニュー',
  file: 'ファイル',
  view: '表示',
  panels: 'パネル',
  appearanceTheme: '外観：テーマ',
  appearanceLanguage: '外観：言語',
  toolbar: 'ツール',
  caseExplorerInText: 'ケースエクスプローラー',
  argumentNavigatorInText: '論証ナビゲーター',
  caseExplorer: 'ケースエクスプローラー',
  caseExplorerEmpty: 'ファイルのパッケージはここに表示されます。',
  argumentNavigator: '論証ナビゲーター',
  argumentNavigatorEmpty: '論証のツリーはここに表示されます。',
  elementProperties: '要素プロパティ',
  elementPropertiesInText: '要素プロパティ',
  elementPropertiesEmpty: '何も選択されていません。',
  problems: '問題',
  problemsInText: '問題',
  problemsEmpty: '問題の一覧はここに表示されます。',
  hide: '{name}を隠す',
  show: '{name}を表示',
  resize: '{name}のサイズを変更',
  splitterHint: 'ドラッグでサイズを変更。ダブルクリックで元に戻す。',
  sideDock: 'サイドドック',
  sideDockInText: 'サイドドック',
  starting: '起動しています…',
  failed: 'コアを起動できませんでした。',
  fontUnreadable: 'コアを起動できませんでした: フォントファイルを読めませんでした（{face}）。',
  fontMissing: 'コアを起動できませんでした: フォントファイルを読み込めませんでした。',
  empty: 'SACM 2.3 ファイルを開くか、ここにドロップしてください。',
  opened: 'ノード {nodes} 個、エッジ {edges} 本',
  findings: 'エラー {errors} 件、警告 {warnings} 件',
  notOpened: '開けませんでした: {reason}',
  unreadable: 'ファイルを読み込めませんでした。',
  relayoutFailed: '図を再レイアウトできませんでした。',
};

const MESSAGES: Record<Language, Record<MessageKey, string>> = { en, ja };

export function translate(language: Language, key: MessageKey, values: Record<string, string | number> = {}): string {
  return MESSAGES[language][key].replace(/\{(\w+)\}/g, (_, name: string) => String(values[name] ?? `{${name}}`));
}

export type ThemeChoice = 'system' | 'light' | 'dark';

/** `?theme=light` or `?theme=dark` chooses; otherwise the system's. */
export function initialTheme(search: string): ThemeChoice {
  const asked = new URLSearchParams(search).get('theme');
  return asked === 'light' || asked === 'dark' ? asked : 'system';
}

/** `?lang=ja` or `?lang=en` chooses; otherwise the browser's first language, if Japanese. */
export function initialLanguage(search: string, preferred: readonly string[]): Language {
  const asked = new URLSearchParams(search).get('lang');
  if (asked === 'en' || asked === 'ja') return asked;
  return preferred[0]?.toLowerCase().startsWith('ja') ? 'ja' : 'en';
}
