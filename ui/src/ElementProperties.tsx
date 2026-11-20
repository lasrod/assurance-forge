// The Element Properties panel: what the selected node is, read from the core
// (`element(id)`). Nothing here is edited yet.

import type { ElementDetails, NodeKind } from './core/Core';
import { PropertyGrid, PropertyRow, Section } from './design';
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
    <div data-testid="element-properties">
      <Section title={t('sectionElement')}>
        {/* The statement is prose, so it is a paragraph that wraps, not a property value. */}
        {details.statement && (
          <p className="af-statement" data-testid="element-statement">
            {details.statement}
          </p>
        )}
        {/* The values are words, not quantities: they start-align and wrap. */}
        <PropertyGrid data-values="text" className="af-properties">
          <PropertyRow label={t('propertyType')} value={kindLabel(details, t)} />
          <PropertyRow label={t('propertyId')} value={details.identifier} mono />
          <PropertyRow label={t('propertyName')} value={details.name} missingLabel={none} />
          {!details.statement && <PropertyRow label={t('propertyStatement')} value={null} missingLabel={none} />}
          <PropertyRow label={t('propertySacmId')} value={details.id} mono />
          {details.codes.length > 0 && (
            <PropertyRow label={t('propertyCodes')} value={details.codes.map((code) => code.code).join(', ')} mono />
          )}
        </PropertyGrid>
      </Section>
    </div>
  );
}
