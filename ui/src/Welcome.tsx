// The welcome page: what the window shows while no file is open. It starts
// work in one click: open a file, open the example, or open a recent file
// again. Arranged as the C++ application's welcome page, in DATUM's parts: a
// block of panel on the frame, buttons for the actions, a list for the files.

import type { RecentFile } from './core/Core';
import { Button, FileText, FolderOpen, ListBox } from './design';
import type { Language, MessageKey } from './i18n/messages';

type T = (key: MessageKey, values?: Record<string, string | number>) => string;

interface WelcomeProps {
  t: T;
  language: Language;
  /** Nothing can be opened into until the core has started. */
  ready: boolean;
  onOpen: () => void;
  onExample: () => void;
  recent: readonly RecentFile[];
  onRecent: (id: string) => void;
  onClearRecent: () => void;
  /** The list holds copies of the files (the browser), which the page says. */
  keepsCopies: boolean;
}

/** When a file was opened, as a date in the interface language. */
export function openedOn(openedAt: number, language: Language): string {
  return new Intl.DateTimeFormat(language, { dateStyle: 'medium' }).format(new Date(openedAt));
}

export function Welcome({ t, language, ready, onOpen, onExample, recent, onRecent, onClearRecent, keepsCopies }: WelcomeProps) {
  return (
    <main className="af-welcome" aria-labelledby="af-welcome-title" data-testid="welcome">
      <div className="af-welcome-block">
        <header className="af-welcome-header">
          <h1 id="af-welcome-title" className="af-welcome-title">
            Assurance Forge
          </h1>
          <p className="datum-meta af-welcome-tagline">{t('tagline')}</p>
        </header>
        <div className="af-welcome-columns">
          <section aria-labelledby="af-welcome-start">
            <h2 id="af-welcome-start" className="af-welcome-heading">
              {t('start')}
            </h2>
            <ul className="af-welcome-actions">
              <li>
                <Button variant="primary" icon={<FolderOpen aria-hidden="true" />} onClick={onOpen} unavailable={!ready}>
                  {t('openFile')}
                </Button>
                <p className="datum-meta">{t('openFileHint')}</p>
              </li>
              <li>
                <Button icon={<FileText aria-hidden="true" />} onClick={onExample} unavailable={!ready}>
                  {t('openExample')}
                </Button>
                <p className="datum-meta">{t('openExampleHint')}</p>
              </li>
            </ul>
          </section>
          <section aria-labelledby="af-welcome-recent">
            <h2 id="af-welcome-recent" className="af-welcome-heading">
              {t('recentFiles')}
            </h2>
            {recent.length === 0 ? (
              <p className="datum-meta">{t('noRecent')}</p>
            ) : (
              <>
                {/* Choosing a file opens it, as choosing a case does in a case list. */}
                <ListBox
                  label={t('recentFiles')}
                  className="af-welcome-recent"
                  value={null}
                  onChange={onRecent}
                  items={recent.map((file) => ({
                    value: file.id,
                    label: (
                      <span className="af-recent">
                        <span className="af-recent-name">{file.name}</span>
                        <span className="datum-meta af-recent-where">
                          {file.location ?? openedOn(file.openedAt, language)}
                        </span>
                      </span>
                    ),
                    // On one line: beside a long path it would shrink to a word a line.
                    meta: <span className="af-tree-meta">{t('recentMeta', { nodes: file.nodes, edges: file.edges })}</span>,
                    disabled: !ready,
                  }))}
                />
                {keepsCopies && <p className="datum-meta af-welcome-note">{t('recentCopies')}</p>}
                <div className="af-welcome-clear">
                  <Button variant="ghost" onClick={onClearRecent}>
                    {t('clearRecent')}
                  </Button>
                </div>
              </>
            )}
          </section>
        </div>
      </div>
    </main>
  );
}
