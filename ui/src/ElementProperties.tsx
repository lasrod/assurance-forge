// The Element Properties panel: what the selected node is, read from the core
// (`element(id)`). Nothing here is edited yet.

import type { ElementDetails, NodeKind } from './core/Core';
import { PropertyGrid, PropertyRow } from './design';
import type { MessageKey } from './i18n/messages';

type T = (key: MessageKey, values?: Record<string, string | number>) => string;

const KINDS: Record<NodeKind, MessageKey> = {
  goal: 'kindGoal',
  strategy: 'kindStrategy',
  solution: 'kindSolution',
  context: 'kindContext',
  assumption: 'kindAssumption',
  justification: 'kindJustification',
  neutral: 'kindNeutral',
};

/** The GSN kind in words, and whether it is undeveloped. */
export function kindLabel(details: Pick<ElementDetails, 'kind' | 'undeveloped'>, t: T): string {
  const kind = t(KINDS[details.kind]);
  return details.undeveloped ? t('kindUndeveloped', { kind }) : kind;
}

export function ElementProperties({ details, t }: { details: ElementDetails | null; t: T }) {
  if (!details) return <p className="datum-meta af-dock-empty">{t('elementPropertiesEmpty')}</p>;
  const none = t('propertyNone');
  return (
    <div className="af-properties" data-testid="element-properties">
      <PropertyGrid>
        <PropertyRow label={t('propertyType')} value={kindLabel(details, t)} />
        <PropertyRow label={t('propertyId')} value={details.identifier} mono />
        <PropertyRow label={t('propertyName')} value={details.name} missingLabel={none} />
        <PropertyRow label={t('propertyStatement')} value={details.statement} missingLabel={none} />
        <PropertyRow label={t('propertySacmId')} value={details.id} mono />
        {details.codes.length > 0 && (
          <PropertyRow label={t('propertyCodes')} value={details.codes.map((code) => code.code).join(', ')} mono />
        )}
      </PropertyGrid>
    </div>
  );
}
