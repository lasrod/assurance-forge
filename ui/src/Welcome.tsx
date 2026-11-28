// The welcome page: what the window shows while no file is open. It starts
// work in one click: open a file, open the example, or open a recent file
// again. One block of panel on the frame, in DATUM's parts: the product's
// name over a line, buttons for the actions, a list for the files.

import type { RecentFile } from './core/Core';
import { Button, FileText, FolderOpen, ListBox } from './design';
import type { Language, MessageKey } from './i18n/messages';
import { isMac, openShortcut } from './shortcuts';
// The application's mark (assets/icon.png), 96 px high: drawn at 48.
import markUrl from './welcome-mark.png';

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
          {/* Decoration: the heading beside it already names the product. */}
          <img className="af-welcome-mark" src={markUrl} alt="" width={93} height={96} />
          <div className="af-welcome-name">
            <h1 id="af-welcome-title" className="af-welcome-title">
              Assurance Forge
            </h1>
            <p className="af-welcome-tagline">{t('tagline')}</p>
          </div>
        </header>
        <div className="af-welcome-columns">
          <section className="af-welcome-start" aria-labelledby="af-welcome-start">
            <h2 id="af-welcome-start" className="af-welcome-heading">
              {t('start')}
            </h2>
            <ul className="af-welcome-actions">
              <li>
                <div className="af-welcome-action">
                  <Button
                    variant="primary"
                    icon={<FolderOpen aria-hidden="true" />}
                    onClick={onOpen}
                    unavailable={!ready}
                    aria-keyshortcuts={isMac() ? 'Meta+O' : 'Control+O'}
                  >
                    {t('openFile')}
                  </Button>
                  {/* The key the File menu shows for the same command. */}
                  <kbd className="af-welcome-shortcut">{openShortcut()}</kbd>
                </div>
                <p className="af-welcome-hint">{t('openFileHint')}</p>
              </li>
              <li>
                <div className="af-welcome-action">
                  <Button icon={<FileText aria-hidden="true" />} onClick={onExample} unavailable={!ready}>
                    {t('openExample')}
                  </Button>
                </div>
                <p className="af-welcome-hint">{t('openExampleHint')}</p>
              </li>
            </ul>
          </section>
          <section className="af-welcome-files" aria-labelledby="af-welcome-recent">
            <h2 id="af-welcome-recent" className="af-welcome-heading">
              {t('recentFiles')}
            </h2>
            {recent.length === 0 ? (
              <p className="af-welcome-empty">{t('noRecent')}</p>
            ) : (
              <>
                {/* Choosing a file opens it, as choosing a case does in a case list. */}
                <ListBox
                  label={t('recentFiles')}
                  className="af-welcome-recent"
                  value={null}
                  onChange={onRecent}
                  items={recent.map((file) => {
                    const where = file.location ?? openedOn(file.openedAt, language);
                    return {
                      value: file.id,
                      icon: <FileText aria-hidden="true" />,
                      label: (
                        <span className="af-recent">
                          <span className="af-recent-name">{file.name}</span>
                          {/* A long path is cut short; pointing at it shows the rest. */}
                          <span className="datum-meta af-recent-where" title={where}>
                            {where}
                          </span>
                        </span>
                      ),
                      // On one line: beside a long path it would shrink to a word a line.
                      meta: <span className="af-tree-meta">{t('recentMeta', { nodes: file.nodes, edges: file.edges })}</span>,
                      disabled: !ready,
                    };
                  })}
                />
                <div className="af-welcome-foot">
                  {keepsCopies && <p className="af-welcome-note">{t('recentCopies')}</p>}
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
