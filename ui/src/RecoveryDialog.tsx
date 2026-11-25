import { Button, Dialog } from './design';
import type { MessageKey } from './i18n/messages';

type T = (key: MessageKey, values?: Record<string, string | number>) => string;

/** A copy of changes not saved, found when its file was opened. */
export interface OfferedRecovery {
  name: string;
  /** When it was written, in milliseconds since 1970. */
  writtenAt: number;
  /** Whether the file holds what it held when the changes began (R6). */
  baseMatches: boolean;
}

/**
 * Asks, before anything else, whether to restore the changes a copy kept
 * when the application ended without closing the file (plan decision R5).
 * Restore makes them one change, which Edit ▸ Undo takes back, so it is the
 * answer that loses nothing: Escape restores too. Discard deletes the copy.
 */
export function RecoveryDialog({
  offered,
  time,
  asksWhere,
  t,
  onRestore,
  onDiscard,
}: {
  offered: OfferedRecovery | null;
  /** When the copy was written, in the interface language. */
  time: string;
  /** Whether Save asks where after restoring a copy of other content (the desktop). */
  asksWhere: boolean;
  t: T;
  onRestore: () => void;
  onDiscard: () => void;
}) {
  const name = offered?.name ?? '';
  return (
    <Dialog
      role="alertdialog"
      title={t('recoveryTitle', { name })}
      open={offered !== null}
      onOpenChange={(next) => {
        if (!next) onRestore();
      }}
      actions={() => (
        <>
          <Button variant="danger" data-testid="recovery-discard" onClick={onDiscard}>
            {t('discard')}
          </Button>
          <Button variant="primary" data-testid="recovery-restore" onClick={onRestore}>
            {t('restore')}
          </Button>
        </>
      )}
    >
      <p>{t('recoveryIntro', { name, time })}</p>
      {offered && !offered.baseMatches && (
        <p data-testid="recovery-base-changed">{t(asksWhere ? 'recoveryBaseChanged' : 'recoveryBaseChangedWeb')}</p>
      )}
      <p>{t('recoveryUndo')}</p>
    </Dialog>
  );
}
