import { useState } from 'react';
import type { Problem, ProblemRepair, ProblemSeverity, Problems } from './core/Core';
import { refusalText } from './addMenu';
import { Button, CircleX, Info, ListBox, Menu, MenuItem, TriangleAlert } from './design';
import type { Language, MessageKey } from './i18n/messages';
import { problemMeta, wordProblem } from './i18n/problems';

type T = (key: MessageKey, values?: Record<string, string | number>) => string;

const MARKS: Record<ProblemSeverity, { icon: typeof CircleX; name: MessageKey }> = {
  error: { icon: CircleX, name: 'severityError' },
  warning: { icon: TriangleAlert, name: 'severityWarning' },
  info: { icon: Info, name: 'severityInfo' },
};

/**
 * The problem list (plan decision P7): one row per problem, errors first,
 * each with its severity, its words and its code. Choosing a row shows the
 * first element it names. The list follows the revision on screen. A
 * problem the editor can repair says so above the list once it is chosen:
 * each repair it offers, worded by what it does, with the action that makes
 * it (`onRepair`); or, when none can be made here, why (F4 of the repair
 * plan).
 */
export function ProblemsPanel({
  problems,
  language,
  t,
  onShow,
  onRepair,
}: {
  problems: Problems | null;
  language: Language;
  t: T;
  /** Select the element with this SACM id and bring it into view. */
  onShow: (id: string) => void;
  /** Make the repair a problem of `revision` offers. */
  onRepair: (repair: ProblemRepair, revision: number, problem: Problem) => void;
}) {
  // The row chosen, by its place in the list of the revision it was chosen in.
  const [chosen, setChosen] = useState<{ revision: number; row: string } | null>(null);
  // The problem a repair was asked for, by its row in the revision it was
  // asked at: its repairs are off until the list of the repaired revision
  // comes, so a second click does not ask again. Other problems keep theirs.
  const [repairing, setRepairing] = useState<{ revision: number; row: string } | null>(null);
  if (!problems || problems.problems.length === 0) {
    return <p className="datum-meta af-dock-empty">{t('problemsNone')}</p>;
  }
  const items = problems.problems.map((problem, index) => ({
    value: String(index),
    label: wordProblem(problem, language),
    icon: <Mark problem={problem} t={t} />,
    meta: problemMeta(problem, language),
  }));
  const value = chosen?.revision === problems.revision ? chosen.row : null;
  const picked = value === null ? undefined : problems.problems[Number(value)];
  const repairs = picked?.repairs ?? [];
  const offered = repairs.map((repair, index) => ({ repair, index })).filter(({ repair }) => refusalOf(repair) === null);
  // None can be made here: why, from the first.
  const refused = offered.length === 0 ? repairs.map(refusalOf).find((code) => code !== null) : undefined;
  const named = (id: string) => picked?.elements.find((element) => element.id === id)?.identifier ?? id;
  // Off once asked, until the list of the repaired revision comes.
  const busy = repairing?.revision === problems.revision && repairing.row === value;
  const make = (repair: ProblemRepair) => {
    if (!picked || value === null) return;
    setRepairing({ revision: problems.revision, row: value });
    onRepair(repair, problems.revision, picked);
  };
  return (
    <>
      {picked && offered.length === 1 && (
        <div className="af-problem-repair" data-testid="problem-repair">
          <span>{repairText(offered[0].repair, named, t)}</span>
          <Button disabled={busy} onClick={() => make(offered[0].repair)}>
            {t('repair')}
          </Button>
        </div>
      )}
      {picked && offered.length > 1 && (
        // Several, as a cycle's lines: one row, and a menu of them, so the
        // list stays in view.
        <div className="af-problem-repair" data-testid="problem-repair">
          <span>{t('repairChoose', { count: offered.length })}</span>
          <Menu
            label={t('repairs')}
            trigger={<Button disabled={busy}>{t('repairMenu')}</Button>}
            onAction={(id) => {
              const chosen = offered[Number(id)];
              if (chosen) make(chosen.repair);
            }}
          >
            {offered.map(({ repair }, index) => (
              <MenuItem key={index} id={String(index)}>
                {repairText(repair, named, t)}
              </MenuItem>
            ))}
          </Menu>
        </div>
      )}
      {picked && refused && (
        <div className="af-problem-repair" data-testid="problem-repair-refused">
          <span>{t('repairNotHere', { reason: refusalText(refused, null, t) })}</span>
        </div>
      )}
    <ListBox
      label={t('problems')}
      className="af-problems"
      items={items}
      value={value}
      onChange={(row) => {
        setChosen({ revision: problems.revision, row });
        const first = problems.problems[Number(row)]?.elements[0];
        if (first) onShow(first.id);
      }}
    />
    </>
  );
}

function Mark({ problem, t }: { problem: Problem; t: T }) {
  const { icon: Icon, name } = MARKS[problem.severity];
  return (
    <span className="af-problem-mark" data-severity={problem.severity} role="img" aria-label={t(name)} title={t(name)}>
      <Icon aria-hidden size="1em" />
    </span>
  );
}

/** Why a repair cannot be made here, or null when it can. */
function refusalOf(repair: ProblemRepair): string | null {
  return repair.kind === 'makeContext' ? null : repair.refusal;
}

/** What a repair does, naming nodes by their identifiers. */
function repairText(repair: ProblemRepair, named: (id: string) => string, t: T): string {
  switch (repair.kind) {
    case 'makeContext':
      return t('repairJoin', { identifier: named(repair.element) });
    case 'removeLine':
      return t('repairRemoveLine', { from: named(repair.from), to: named(repair.to) });
    case 'clearUndeveloped':
      return t('repairClearUndeveloped', { identifier: named(repair.element) });
    case 'foldStrategy':
      return t('repairFoldStrategy', { strategy: named(repair.strategy), goal: named(repair.goal ?? '') });
    case 'renumber':
      return t('repairRenumber', {
        identifier: named(repair.nodes[0] ?? ''),
        identifiers: repair.given.map((node) => node.identifier).join(', '),
      });
  }
}
