// The Element Properties panel: what the selected node or connection is, read
// from the core (`element(id)`, `relationship(id)`). An element's identifier,
// name and description are edited here; each change is a command of the core.

import type { DiagramEdge, EdgeKind, ElementDetails, NodeKind, RelationshipDetails, RelationshipEnd, TextField } from './core/Core';
import { PropertyGrid, PropertyRow, Section } from './design';
import type { MessageKey } from './i18n/messages';
import { TextProperty } from './TextProperty';
import type { TextOutcome } from './textDraft';
import { applyShortcut } from './shortcuts';

/** Sets a text of the element `id`, of kind `about`, and answers what the core made of it. */
export type OnText = (id: string, field: TextField, text: string, about: NodeKind) => Promise<TextOutcome>;

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

function ElementSection({ details, t, onText }: { details: ElementDetails; t: T; onText: OnText }) {
  const set = (field: TextField) => (text: string) => onText(details.id, field, text, details.kind);
  return (
    <div data-testid="element-properties">
      <Section title={t('sectionElement')}>
        {/* As the element reads: its identifier, its name, then what it says.
            The SACM id is the file's own and is not shown: relationships
            name it, and it never changes. The fields are keyed by the
            element, so a draft never moves to another one. */}
        <div className="af-text-properties" key={details.id}>
          <TextProperty
            label={t('propertyId')}
            value={details.identifier}
            onCommit={set('identifier')}
            hint={t('fieldHint')}
            mono
            data-testid="element-identifier"
          />
          <TextProperty
            label={t('propertyName')}
            value={details.name ?? ''}
            onCommit={set('name')}
            hint={t('fieldHint')}
            data-testid="element-name"
          />
          <TextProperty
            label={t('propertyStatement')}
            value={details.statement ?? ''}
            onCommit={set('statement')}
            hint={t('statementHint', { apply: applyShortcut() })}
            multiline
            data-testid="element-statement"
          />
          {/* What it is, and what the view says of it: not edited here. */}
          <div className="af-text-property">
            <span className="af-text-label">{t('propertyType')}</span>
            <p className="af-text-value" data-testid="element-type">
              {kindLabel(details, t)}
            </p>
          </div>
          {details.codes.length > 0 && (
            <div className="af-text-property">
              <span className="af-text-label">{t('propertyCodes')}</span>
              <p className="af-text-value" data-mono="">
                {details.codes.map((code) => code.code).join(', ')}
              </p>
            </div>
          )}
        </div>
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

export function ElementProperties({ details, t, onText }: { details: Details | null; t: T; onText: OnText }) {
  if (!details) return <p className="datum-meta af-dock-empty">{t('elementPropertiesEmpty')}</p>;
  return details.type === 'element' ? (
    <ElementSection details={details.element} t={t} onText={onText} />
  ) : (
    <RelationshipSection selected={details} t={t} />
  );
}
