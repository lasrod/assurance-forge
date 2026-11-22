// The Element Properties panel: what the selected node or connection is, read
// from the core (`element(id)`, `relationship(id)`). Nothing here is edited
// yet.

import type { DiagramEdge, EdgeKind, ElementDetails, NodeKind, RelationshipDetails, RelationshipEnd } from './core/Core';
import { PropertyGrid, PropertyRow, Section } from './design';
import type { MessageKey } from './i18n/messages';

type T = (key: MessageKey, values?: Record<string, string | number>) => string;

/**
 * What is selected: a node of the diagram, or one of its lines with the SACM
 * relationship the line is drawn for.
 */
export type Details =
  | { type: 'element'; element: ElementDetails }
  | { type: 'relationship'; relationship: RelationshipDetails; edge: Pick<DiagramEdge, 'from' | 'to' | 'toRelationship'> };

const KINDS: Record<NodeKind, MessageKey> = {
  goal: 'kindGoal',
  strategy: 'kindStrategy',
  solution: 'kindSolution',
  context: 'kindContext',
  assumption: 'kindAssumption',
  justification: 'kindJustification',
  neutral: 'kindNeutral',
};

const RELATIONSHIPS: Record<EdgeKind, MessageKey> = {
  supportedBy: 'relSupportedBy',
  inContextOf: 'relInContextOf',
  challenge: 'relChallenge',
  neutral: 'relNeutral',
};

/** The GSN kind in words, and whether it is undeveloped. */
export function kindLabel(details: Pick<ElementDetails, 'kind' | 'undeveloped'>, t: T): string {
  const kind = t(KINDS[details.kind]);
  return details.undeveloped ? t('kindUndeveloped', { kind }) : kind;
}

/** An end as the diagram names it: a node's GSN identifier, or the relationship's id. */
export function endLabel(end: RelationshipEnd, t: T): string {
  if (end.identifier !== null) return end.identifier;
  return end.relationship ? t('endRelationship', { id: end.id }) : end.id;
}

/**
 * The ends of one line, as the diagram names them. The line's ends are among
 * its relationship's: its target, its sources and its Strategy.
 */
export function lineEnds(details: Extract<Details, { type: 'relationship' }>, t: T): { from: string; to: string } {
  const { relationship, edge } = details;
  const known = [relationship.target, relationship.reasoning, ...relationship.sources];
  const label = (id: string, isRelationship: boolean) => {
    const end = known.find((candidate) => candidate?.id === id);
    if (end) return endLabel(end, t);
    return isRelationship ? t('endRelationship', { id }) : id;
  };
  return { from: label(edge.from, false), to: label(edge.to, edge.toRelationship) };
}

function ElementSection({ details, t }: { details: ElementDetails; t: T }) {
  const none = t('propertyNone');
  return (
    <div data-testid="element-properties">
      <Section title={t('sectionElement')}>
        {/* What is selected, as the diagram names it: its name, then its identifier and kind. */}
        <div className="af-element-title" data-testid="element-title">
          <p className="af-element-name">{details.name || details.identifier}</p>
          <p className="af-element-meta">
            <span className="af-element-identifier">{details.identifier}</span>
            <span aria-hidden="true"> · </span>
            {kindLabel(details, t)}
          </p>
        </div>
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

function RelationshipSection({ selected, t }: { selected: Extract<Details, { type: 'relationship' }>; t: T }) {
  const none = t('propertyNone');
  const details = selected.relationship;
  const { from, to } = lineEnds(selected, t);
  const ends = (list: RelationshipEnd[]) => (list.length > 0 ? list.map((end) => endLabel(end, t)).join(', ') : null);
  return (
    <div data-testid="relationship-properties">
      {/* The line that is selected, as its arrow points. */}
      <Section title={t('sectionConnection')}>
        <PropertyGrid data-values="text" className="af-properties" data-testid="connection">
          <PropertyRow label={t('propertyType')} value={t(RELATIONSHIPS[details.kind])} />
          <PropertyRow label={t('propertyFrom')} value={from} mono />
          <PropertyRow label={t('propertyTo')} value={to} mono />
        </PropertyGrid>
      </Section>
      {/* The SACM relationship it is drawn for, which may have other lines too. Its ends are named as SACM names them. */}
      <Section title={t('sectionRelationship')}>
        <PropertyGrid data-values="text" className="af-properties" data-testid="sacm-relationship">
          <PropertyRow label={t('propertySacmClass')} value={details.class} mono />
          <PropertyRow label={t('propertySacmId')} value={details.id} mono />
          <PropertyRow label={t('propertyName')} value={details.name} missingLabel={none} />
          <PropertyRow label={t('propertyTarget')} value={details.target && endLabel(details.target, t)} missingLabel={none} mono />
          <PropertyRow label={t('propertySources')} value={ends(details.sources)} missingLabel={none} mono />
          {details.reasoning && <PropertyRow label={t('propertyReasoning')} value={endLabel(details.reasoning, t)} mono />}
          {details.codes.length > 0 && (
            <PropertyRow label={t('propertyCodes')} value={details.codes.map((code) => code.code).join(', ')} mono />
          )}
        </PropertyGrid>
      </Section>
    </div>
  );
}

export function ElementProperties({ details, t }: { details: Details | null; t: T }) {
  if (!details) return <p className="datum-meta af-dock-empty">{t('elementPropertiesEmpty')}</p>;
  return details.type === 'element' ? (
    <ElementSection details={details.element} t={t} />
  ) : (
    <RelationshipSection selected={details} t={t} />
  );
}
