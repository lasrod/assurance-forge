import type { Removal } from './core/Core';
import { kindName } from './addMenu';
import { Button, Dialog } from './design';
import type { MessageKey } from './i18n/messages';

type T = (key: MessageKey, values?: Record<string, string | number>) => string;

/**
 * Asks before a node is removed with what is under it: it lists every
 * element that goes, the chosen one first, and removes them only when the
 * user says so. A node removed alone is not asked about: Undo brings it back.
 */
export function RemoveDialog({
  removal,
  t,
  onRemove,
  onCancel,
}: {
  removal: Removal | null;
  t: T;
  onRemove: (removal: Removal) => void;
  onCancel: () => void;
}) {
  const first = removal?.nodes[0];
  const name = first ? `${kindName(first.kind, t)} ${first.identifier}` : '';
  return (
    <Dialog
      role="alertdialog"
      title={t('removeTitle', { name })}
      open={removal !== null}
      onOpenChange={(open) => {
        if (!open) onCancel();
      }}
      actions={(close) => (
        <>
          <Button onClick={close}>{t('cancel')}</Button>
          <Button
            variant="danger"
            data-testid="remove-confirm"
            onClick={() => {
              if (removal) onRemove(removal);
            }}
          >
            {t('remove')}
          </Button>
        </>
      )}
    >
      <p>{t('removeIntro', { count: removal?.nodes.length ?? 0 })}</p>
      <ul className="af-remove-list" data-testid="remove-list">
        {removal?.nodes.map((node) => {
          const text = node.name || node.statement;
          return (
            <li key={node.id}>
              <span className="af-remove-id">
                {node.acpDescription ? t('acpDescription') : kindName(node.kind, t)} {node.identifier}
              </span>
              {text ? <span className="af-remove-text"> {text}</span> : null}
            </li>
          );
        })}
      </ul>
    </Dialog>
  );
}
