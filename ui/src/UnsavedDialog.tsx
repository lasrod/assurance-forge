import { Button, Dialog } from './design';
import type { MessageKey } from './i18n/messages';

type T = (key: MessageKey, values?: Record<string, string | number>) => string;

/**
 * Asks before changes that are not saved would be lost: by Close, Open, a
 * recent file, a dropped file or closing the window (plan decision S8).
 * Save saves first and goes on only if the save was made; Don't save goes
 * on without; Cancel does neither.
 */
export function UnsavedDialog({
  name,
  open,
  t,
  onSave,
  onDiscard,
  onCancel,
}: {
  /** The open file's name. */
  name: string;
  open: boolean;
  t: T;
  onSave: () => void;
  onDiscard: () => void;
  onCancel: () => void;
}) {
  return (
    <Dialog
      role="alertdialog"
      title={t('unsavedTitle', { name })}
      open={open}
      onOpenChange={(next) => {
        if (!next) onCancel();
      }}
      actions={(close) => (
        <>
          <Button onClick={close}>{t('cancel')}</Button>
          <Button variant="danger" data-testid="unsaved-discard" onClick={onDiscard}>
            {t('dontSave')}
          </Button>
          <Button variant="primary" data-testid="unsaved-save" onClick={onSave}>
            {t('save')}
          </Button>
        </>
      )}
    >
      <p>{t('unsavedIntro')}</p>
    </Dialog>
  );
}
