import { useCallback, useEffect, useRef, useState, type DragEvent } from 'react';
import { DiagramCanvas } from './canvas/DiagramCanvas';
import type { Core, DiagramAt, OpenResult } from './core/Core';
import { Button, DatumRoot, Status, StatusBar, StatusBarSpacer, Toolbar, ToolbarGroup } from './design';
import { t } from './i18n/messages';

type State =
  | { kind: 'starting' }
  | { kind: 'failed'; reason: string }
  | { kind: 'empty' }
  | { kind: 'unreadable' }
  | { kind: 'result'; result: OpenResult };

function message(state: State): { text: string; tone?: 'error' } {
  switch (state.kind) {
    case 'starting':
      return { text: t('starting') };
    case 'failed':
      return { text: t('failed', { reason: state.reason }), tone: 'error' };
    case 'empty':
      return { text: t('empty') };
    case 'unreadable':
      return { text: t('unreadable'), tone: 'error' };
    case 'result': {
      const result = state.result;
      if (result.result === 'opened') {
        return {
          text: `${t('opened', { nodes: result.nodes, edges: result.edges })} · ${t('findings', {
            errors: result.diagnostics.errors,
            warnings: result.diagnostics.warnings,
          })}`,
        };
      }
      return {
        text:
          result.line === null
            ? t('refusedNoPlace', { code: result.code })
            : t('refused', { code: result.code, line: result.line, column: result.column ?? '' }),
        tone: 'error',
      };
    }
  }
}

export function App({ start }: { start: () => Promise<Core> }) {
  const [core, setCore] = useState<Core | null>(null);
  const [state, setState] = useState<State>({ kind: 'starting' });
  const [diagram, setDiagram] = useState<DiagramAt | null>(null);
  const input = useRef<HTMLInputElement>(null);

  const open = useCallback(
    async (bytes: Uint8Array) => {
      if (!core) return;
      const result = await core.open(bytes);
      setState({ kind: 'result', result });
      setDiagram(result.result === 'opened' ? await core.diagram() : null);
    },
    [core],
  );

  useEffect(() => {
    start().then(
      (started) => {
        setCore(started);
        setState({ kind: 'empty' });
      },
      (error: unknown) => setState({ kind: 'failed', reason: String(error) }),
    );
  }, [start]);

  // In development and the browser tests, `?sample=<file>` opens a sample case.
  useEffect(() => {
    const sample = new URLSearchParams(location.search).get('sample');
    if (!core || !sample) return;
    fetch(`/samples/${encodeURIComponent(sample)}`)
      .then((response) => (response.ok ? response.arrayBuffer() : Promise.reject(new Error(String(response.status)))))
      .then((buffer) => open(new Uint8Array(buffer)))
      .catch(() => setState({ kind: 'unreadable' }));
  }, [core, open]);

  const openFile = (file: File | undefined) => {
    if (!file) return;
    file.arrayBuffer().then(
      (buffer) => open(new Uint8Array(buffer)),
      () => setState({ kind: 'unreadable' }),
    );
  };

  const onDrop = (event: DragEvent) => {
    event.preventDefault();
    openFile(event.dataTransfer.files[0]);
  };

  const status = message(state);
  return (
    <DatumRoot app className="af-app" lang="en">
      <Toolbar label={t('appName')}>
        <ToolbarGroup>
          <Button onClick={() => input.current?.click()} unavailable={!core}>
            {t('open')}
          </Button>
        </ToolbarGroup>
      </Toolbar>
      <input
        ref={input}
        type="file"
        accept=".xmi,.sacm,.xml"
        hidden
        data-testid="open-file"
        onChange={(event) => {
          openFile(event.target.files?.[0]);
          event.target.value = '';
        }}
      />
      <main className="af-centre" onDragOver={(event) => event.preventDefault()} onDrop={onDrop}>
        <DiagramCanvas diagram={diagram} label={t('canvas')} />
      </main>
      <StatusBar>
        <Status tone={status.tone} data-testid="status">
          {status.text}
        </Status>
        <StatusBarSpacer />
      </StatusBar>
    </DatumRoot>
  );
}
