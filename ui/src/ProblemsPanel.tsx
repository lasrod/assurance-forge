import { useState } from 'react';
import type { Problem, ProblemSeverity, Problems } from './core/Core';
import { CircleX, Info, ListBox, TriangleAlert } from './design';
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
 * first element it names. The list follows the revision on screen.
 */
export function ProblemsPanel({
  problems,
  language,
  t,
  onShow,
}: {
  problems: Problems | null;
  language: Language;
  t: T;
  /** Select the element with this SACM id and bring it into view. */
  onShow: (id: string) => void;
}) {
  // The row chosen, by its place in the list of the revision it was chosen in.
  const [chosen, setChosen] = useState<{ revision: number; row: string } | null>(null);
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
  return (
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
