// The interface's words, in English and Japanese. The core sends codes and
// their arguments; they are worded here.

export type Language = 'en' | 'ja';

const en = {
  appName: 'Assurance Forge',
  open: 'Open…',
  language: 'Language',
  theme: 'Theme',
  themeSystem: 'System',
  themeLight: 'Light',
  themeDark: 'Dark',
  canvas: 'GSN diagram',
  diagram: 'Diagram',
  outline: 'Outline',
  outlineInText: 'outline',
  outlineEmpty: 'The argument tree will be here.',
  details: 'Details',
  detailsInText: 'details',
  detailsEmpty: 'Nothing is selected.',
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
  failed: 'The core did not start: {reason}',
  empty: 'Open a SACM 2.3 file, or drop one here.',
  opened: '{nodes} nodes, {edges} edges',
  findings: '{errors} errors, {warnings} warnings',
  refused: 'Not opened: {code} at line {line}, column {column}',
  refusedNoPlace: 'Not opened: {code}',
  unreadable: 'Could not read the file.',
};

export type MessageKey = keyof typeof en;

const ja: Record<MessageKey, string> = {
  appName: 'Assurance Forge',
  open: '開く…',
  language: '言語',
  theme: 'テーマ',
  themeSystem: 'システム',
  themeLight: 'ライト',
  themeDark: 'ダーク',
  canvas: 'GSN 図',
  diagram: '図',
  outline: 'アウトライン',
  outlineInText: 'アウトライン',
  outlineEmpty: '論証のツリーはここに表示されます。',
  details: '詳細',
  detailsInText: '詳細',
  detailsEmpty: '何も選択されていません。',
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
  failed: 'コアを起動できませんでした: {reason}',
  empty: 'SACM 2.3 ファイルを開くか、ここにドロップしてください。',
  opened: 'ノード {nodes} 個、エッジ {edges} 本',
  findings: 'エラー {errors} 件、警告 {warnings} 件',
  refused: '開けませんでした: {code}（{line} 行 {column} 列）',
  refusedNoPlace: '開けませんでした: {code}',
  unreadable: 'ファイルを読み込めませんでした。',
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
