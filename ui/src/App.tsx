import { useCallback, useEffect, useMemo, useRef, useState, type DragEvent } from 'react';
import { DiagramCanvas } from './canvas/DiagramCanvas';
import type { Core, DiagramAt, OpenResult } from './core/Core';
import {
  Button,
  ChevronDown,
  ChevronUp,
  DatumRoot,
  PanelLeftClose,
  PanelLeftOpen,
  PanelRightClose,
  PanelRightOpen,
  SegmentedControl,
  Status,
  StatusBar,
  StatusBarSpacer,
  Toolbar,
  ToolbarGroup,
  ToolbarSpacer,
  Workspace,
  type WorkspaceCopy,
  type WorkspaceLayout,
} from './design';
import { initialTheme, translate, type Language, type MessageKey, type ThemeChoice } from './i18n/messages';
import { loadLayout, saveLayout } from './layout';

type State =
  | { kind: 'starting' }
  | { kind: 'failed'; reason: string }
  | { kind: 'empty' }
  | { kind: 'unreadable' }
  | { kind: 'result'; result: OpenResult };

type T = (key: MessageKey, values?: Record<string, string | number>) => string;

function message(state: State, t: T): { text: string; tone?: 'error' } {
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

export function App({ start, initialLanguage }: { start: (language: Language) => Promise<Core>; initialLanguage: Language }) {
  const [language, setLanguage] = useState<Language>(initialLanguage);
  const [core, setCore] = useState<Core | null>(null);
  const [state, setState] = useState<State>({ kind: 'starting' });
  const [diagram, setDiagram] = useState<DiagramAt | null>(null);
  const [layout, setLayout] = useState<WorkspaceLayout>(loadLayout);
  const [theme, setTheme] = useState<ThemeChoice>(() => initialTheme(location.search));
  const input = useRef<HTMLInputElement>(null);
  const t: T = useCallback((key, values) => translate(language, key, values), [language]);

  const open = useCallback(
    async (bytes: Uint8Array) => {
      if (!core) return;
      const result = await core.open(bytes);
      setState({ kind: 'result', result });
      setDiagram(result.result === 'opened' ? await core.diagram() : null);
    },
    [core],
  );

  // The core starts once, in the language the page opened in.
  const [startLanguage] = useState(initialLanguage);
  useEffect(() => {
    start(startLanguage).then(
      (started) => {
        setCore(started);
        setState({ kind: 'empty' });
      },
      (error: unknown) => setState({ kind: 'failed', reason: String(error) }),
    );
  }, [start, startLanguage]);

  // The node text follows the interface language.
  useEffect(() => {
    document.documentElement.lang = language;
    if (!core) return;
    let current = true;
    core
      .setLanguage(language)
      .then(() => core.diagram())
      .then((next) => {
        if (current && next) setDiagram(next);
      });
    return () => {
      current = false;
    };
  }, [core, language]);

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

  const copy: WorkspaceCopy = useMemo(
    () => ({
      hide: (name) => t('hide', { name }),
      show: (name) => t('show', { name }),
      resize: (name) => t('resize', { name }),
      splitterHint: t('splitterHint'),
      sideDock: t('sideDock'),
      sideDockInText: t('sideDockInText'),
    }),
    [t],
  );

  const empty = (key: MessageKey) => <p className="datum-meta af-dock-empty">{t(key)}</p>;
  const status = message(state, t);
  return (
    <DatumRoot app className="af-app" lang={language} theme={theme === 'system' ? undefined : theme}>
      <Toolbar label={t('appName')}>
        <ToolbarGroup>
          <Button onClick={() => input.current?.click()} unavailable={!core}>
            {t('open')}
          </Button>
        </ToolbarGroup>
        <ToolbarSpacer />
        <SegmentedControl<ThemeChoice>
          legend={t('theme')}
          options={[
            { value: 'system', label: t('themeSystem') },
            { value: 'light', label: t('themeLight') },
            { value: 'dark', label: t('themeDark') },
          ]}
          value={theme}
          onChange={setTheme}
        />
        <SegmentedControl<Language>
          legend={t('language')}
          options={[
            { value: 'en', label: 'EN' },
            { value: 'ja', label: '日本語' },
          ]}
          value={language}
          onChange={setLanguage}
        />
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
      <div className="af-workspace" onDragOver={(event) => event.preventDefault()} onDrop={onDrop}>
        <Workspace
          centreLabel={t('diagram')}
          layout={layout}
          onLayoutChange={(next) => {
            setLayout(next);
            saveLayout(next);
          }}
          copy={copy}
          start={{
            name: t('outline'),
            nameInText: t('outlineInText'),
            min: 14,
            icons: { hide: <PanelLeftClose />, show: <PanelLeftOpen /> },
            views: [{ id: 'outline', label: t('outline'), content: empty('outlineEmpty') }],
          }}
          end={{
            name: t('details'),
            nameInText: t('detailsInText'),
            min: 14,
            icons: { hide: <PanelRightClose />, show: <PanelRightOpen /> },
            views: [{ id: 'details', label: t('details'), content: empty('detailsEmpty') }],
          }}
          bottom={{
            name: t('problems'),
            nameInText: t('problemsInText'),
            min: 7,
            icons: { hide: <ChevronDown />, show: <ChevronUp /> },
            views: [{ id: 'problems', label: t('problems'), content: empty('problemsEmpty') }],
          }}
        >
          <DiagramCanvas diagram={diagram} label={t('canvas')} />
        </Workspace>
      </div>
      <StatusBar>
        <Status tone={status.tone} data-testid="status">
          {status.text}
        </Status>
        <StatusBarSpacer />
      </StatusBar>
    </DatumRoot>
  );
}
