import type { LineEnds, LineRemoval, Removal } from './core/Core';
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
        {removal?.acps.map((label) => (
          <li key={`acp ${label}`}>
            <span className="af-remove-id">{t('acpGoes', { label })}</span>
          </li>
        ))}
      </ul>
    </Dialog>
  );
}

/**
 * Asks before a line is removed with the assurance claim points on it
 * (W37): it lists them, and removes the line only when the user says so. A
 * line with none is not asked about: Undo brings it back.
 */
export function LineRemoveDialog({
  asked,
  t,
  onRemove,
  onCancel,
}: {
  /** What the removal takes, and the identifiers of the line's ends. */
  asked: { removal: LineRemoval; named: LineEnds } | null;
  t: T;
  onRemove: () => void;
  onCancel: () => void;
}) {
  return (
    <Dialog
      role="alertdialog"
      title={asked ? t('removeLineTitle', { from: asked.named.from, to: asked.named.to }) : ''}
      open={asked !== null}
      onOpenChange={(open) => {
        if (!open) onCancel();
      }}
      actions={(close) => (
        <>
          <Button onClick={close}>{t('cancel')}</Button>
          <Button variant="danger" data-testid="remove-line-confirm" onClick={onRemove}>
            {t('remove')}
          </Button>
        </>
      )}
    >
      <p>{t('removeLineIntro')}</p>
      <ul className="af-remove-list" data-testid="remove-line-list">
        {asked?.removal.acps.map((label) => (
          <li key={label}>
            <span className="af-remove-id">{label}</span>
          </li>
        ))}
      </ul>
      <p>{t('removeLineUndo')}</p>
    </Dialog>
  );
}
