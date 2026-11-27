import { useState } from 'react';
import type { Problem, ProblemRepair, ProblemSeverity, Problems } from './core/Core';
import { Button, CircleX, Info, ListBox, TriangleAlert } from './design';
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
 * problem the editor can repair says so above the list once it is chosen,
 * with the action that repairs it (`onRepair`).
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
  // asked at: its button is off until the list of the repaired revision
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
  const repair = picked?.repair ?? null;
  const repaired = repair ? picked?.elements.find((element) => element.id === repair.element) : undefined;
  return (
    <>
      {picked && repair && (
        <div className="af-problem-repair" data-testid="problem-repair">
          <span>{t('repairJoin', { identifier: repaired?.identifier ?? repair.element })}</span>
          <Button
            disabled={repairing?.revision === problems.revision && repairing.row === value}
            onClick={() => {
              if (value !== null) setRepairing({ revision: problems.revision, row: value });
              onRepair(repair, problems.revision, picked);
            }}
          >
            {t('repair')}
          </Button>
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
