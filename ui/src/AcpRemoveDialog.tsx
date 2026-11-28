import type { AcpRemoval } from './core/Core';
import { kindName, refusalText } from './addMenu';
import { Button, Dialog } from './design';
import type { MessageKey } from './i18n/messages';

type T = (key: MessageKey, values?: Record<string, string | number>) => string;

/**
 * Asks, before an ACP with a confidence argument of its own is removed,
 * whether that argument stays or goes with it (W35). It lists what deleting
 * the argument would remove. An ACP with a description, or on a goal of the
 * argument itself, is not asked about: Undo brings it back.
 */
export function AcpRemoveDialog({
  removal,
  t,
  onRemove,
  onCancel,
}: {
  removal: AcpRemoval | null;
  t: T;
  /** `withArgument`: the confidence argument goes too. */
  onRemove: (removal: AcpRemoval, withArgument: boolean) => void;
  onCancel: () => void;
}) {
  const argument = removal?.argument ?? null;
  const name = argument?.name ?? argument?.package ?? '';
  return (
    <Dialog
      role="alertdialog"
      title={t('removeAcpTitle', { label: removal?.label ?? '' })}
      open={removal !== null && argument !== null}
      onOpenChange={(open) => {
        if (!open) onCancel();
      }}
      actions={(close) => (
        <>
          <Button onClick={close}>{t('cancel')}</Button>
          <Button
            data-testid="remove-acp-keep"
            onClick={() => {
              if (removal) onRemove(removal, false);
            }}
          >
            {t('removeAcpKeep')}
          </Button>
          <Button
            variant="danger"
            data-testid="remove-acp-delete"
            disabled={argument?.refusal != null}
            onClick={() => {
              if (removal) onRemove(removal, true);
            }}
          >
            {t('removeAcpDelete')}
          </Button>
        </>
      )}
    >
      <p>{t('removeAcpIntro', { name, count: argument?.nodes.length ?? 0 })}</p>
      <ul className="af-remove-list" data-testid="remove-acp-list">
        {argument?.nodes.map((node) => {
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
      {argument?.refusal ? <p data-testid="remove-acp-refused">{refusalText(argument.refusal, null, t)}</p> : <p>{t('removeAcpUndo')}</p>}
    </Dialog>
  );
}
