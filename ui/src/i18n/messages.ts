// The interface's words. The core sends codes and their arguments; they are
// worded here. Japanese comes with the workspace's titles (goal G6).

const en = {
  appName: 'Assurance Forge',
  open: 'Open…',
  canvas: 'GSN diagram',
  starting: 'Starting…',
  failed: 'The core did not start: {reason}',
  empty: 'Open a SACM 2.3 file, or drop one here.',
  opened: '{nodes} nodes, {edges} edges',
  findings: '{errors} errors, {warnings} warnings',
  refused: 'Not opened: {code} at line {line}, column {column}',
  refusedNoPlace: 'Not opened: {code}',
  unreadable: 'Could not read the file.',
} as const;

export type MessageKey = keyof typeof en;

export function t(key: MessageKey, values: Record<string, string | number> = {}): string {
  return en[key].replace(/\{(\w+)\}/g, (_, name: string) => String(values[name] ?? `{${name}}`));
}
