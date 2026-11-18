import { useCallback, useEffect, useMemo, useRef, useState, type DragEvent } from 'react';
import { DiagramCanvas } from './canvas/DiagramCanvas';
import type { Core, DiagramAt, OpenResult, TextStyle } from './core/Core';
import {
  Button,
  ChevronDown,
  ChevronUp,
  DatumRoot,
  FolderOpen,
  PanelLeftClose,
  PanelLeftOpen,
  PanelRightClose,
  PanelRightOpen,
  Status,
  StatusBar,
  StatusBarSpacer,
  Toolbar,
  ToolbarGroup,
  Workspace,
  type WorkspaceCopy,
  type WorkspaceLayout,
  type WorkspaceDockId,
} from './design';
import { initialTheme, translate, type Language, type MessageKey, type ThemeChoice } from './i18n/messages';
import { StartError } from './core/wasmCore';
import { wordRefusal } from './i18n/refusals';
import { loadWorkspace, saveWorkspace, togglePanel } from './layout';
import { isOpenShortcut } from './shortcuts';
import { MenuBar, PANELS, type PanelId } from './MenuBar';

type State =
  | { kind: 'starting' }
  | { kind: 'failed'; code: string; detail: string }
  | { kind: 'empty' }
  | { kind: 'unreadable' }
  | { kind: 'relayoutFailed' }
  | { kind: 'result'; result: OpenResult };

type T = (key: MessageKey, values?: Record<string, string | number>) => string;

function message(state: State, t: T, language: Language): { text: string; tone?: 'error' } {
  switch (state.kind) {
    case 'starting':
      return { text: t('starting') };
    case 'failed':
      if (state.code === 'font-unreadable') return { text: t('fontUnreadable', { face: state.detail }), tone: 'error' };
      if (state.code === 'font-missing') return { text: t('fontMissing'), tone: 'error' };
      return { text: t('failed'), tone: 'error' };
    case 'empty':
      return { text: t('empty') };
    case 'unreadable':
      return { text: t('unreadable'), tone: 'error' };
    case 'relayoutFailed':
      return { text: t('relayoutFailed'), tone: 'error' };
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
      return { text: t('notOpened', { reason: wordRefusal(result, language) }), tone: 'error' };
    }
  }
}

export function App({ start, initialLanguage }: { start: (language: Language) => Promise<Core>; initialLanguage: Language }) {
  const [language, setLanguage] = useState<Language>(initialLanguage);
  const [core, setCore] = useState<Core | null>(null);
  const [state, setState] = useState<State>({ kind: 'starting' });
  const [diagram, setDiagram] = useState<DiagramAt | null>(null);
  const [workspace, setWorkspace] = useState(loadWorkspace);
  const { layout } = workspace;
  const [theme, setTheme] = useState<ThemeChoice>(() => initialTheme(location.search));
  const input = useRef<HTMLInputElement>(null);
  const t: T = useCallback((key, values) => translate(language, key, values), [language]);

  // A file given before the core is ready waits for it.
  const [pending, setPending] = useState<{ bytes: Uint8Array; request: number } | null>(null);
  // Each open is numbered, so a slower earlier read cannot replace the file
  // chosen last.
  const latest = useRef(0);
  // The revision on screen, or null. A relayout (language, text size) shows a
  // diagram only of that revision: after a failed read the core may still hold
  // an earlier document, which must not come back.
  const shown = useRef<number | null>(null);
  const showRelayout = useCallback((next: DiagramAt | null) => {
    if (!next || next.revision !== shown.current) return;
    setDiagram((current) => (current && current.layout === next.layout && current.revision === next.revision ? current : next));
  }, []);
  const coreRef = useRef<Core | null>(null);
  coreRef.current = core;
  /** A read that failed, unless a later request has started: nothing is open, in the core too. */
  const unreadable = useCallback((request: number) => {
    if (request !== latest.current) return;
    shown.current = null;
    setDiagram(null);
    setState({ kind: 'unreadable' });
    void coreRef.current?.close();
  }, []);
  const open = useCallback(
    async (bytes: Uint8Array, request: number) => {
      // A later request has started: this one must not reach the core, which
      // holds one document, nor wait for it.
      if (request !== latest.current) return;
      if (!core) {
        setPending({ bytes, request });
        return;
      }
      try {
        const result = await core.open(bytes);
        const next = result.result === 'opened' ? await core.diagram() : null;
        if (request !== latest.current) return;
        shown.current = result.result === 'opened' ? result.revision : null;
        setState({ kind: 'result', result });
        setDiagram(next);
      } catch {
        unreadable(request);
      }
    },
    [core, unreadable],
  );
  /** Starts a request: the bytes it reads open only if no later one started. */
  const begin = () => {
    latest.current += 1;
    return latest.current;
  };
  useEffect(() => {
    if (core && pending) {
      setPending(null);
      void open(pending.bytes, pending.request);
    }
  }, [core, pending, open]);

  // The core starts once, in the language the page opened in. A start that
  // is no longer wanted (StrictMode runs effects twice in development) frees
  // its core.
  const [startLanguage] = useState(initialLanguage);
  useEffect(() => {
    let wanted = true;
    let started: Core | null = null;
    start(startLanguage).then(
      (core) => {
        if (!wanted) {
          core.dispose();
          return;
        }
        started = core;
        setCore(core);
        setState({ kind: 'empty' });
      },
      (error: unknown) => {
        if (!wanted) return;
        const known = error instanceof StartError;
        setState({ kind: 'failed', code: known ? error.code : 'core', detail: known ? error.detail : '' });
      },
    );
    return () => {
      wanted = false;
      started?.dispose();
    };
  }, [start, startLanguage]);

  // The node text follows the interface language.
  useEffect(() => {
    document.documentElement.lang = language;
    if (!core) return;
    let current = true;
    core
      .setLanguage(language)
      .then(() => core.diagram())
      .then(
        (next) => {
          if (current) showRelayout(next);
        },
        () => {
          if (current) setState({ kind: 'relayoutFailed' });
        },
      );
    return () => {
      current = false;
    };
  }, [core, language, showRelayout]);

  // In development and the browser tests, `?sample=<file>` opens a sample case.
  useEffect(() => {
    const sample = new URLSearchParams(location.search).get('sample');
    if (!core || !sample) return;
    const request = begin();
    fetch(`/samples/${encodeURIComponent(sample)}`)
      .then((response) => (response.ok ? response.arrayBuffer() : Promise.reject(new Error(String(response.status)))))
      .then((buffer) => open(new Uint8Array(buffer), request))
      .catch(() => unreadable(request));
  }, [core, open, unreadable]);

  const openFile = (file: File | undefined) => {
    if (!file) return;
    const request = begin();
    file.arrayBuffer().then(
      (buffer) => void open(new Uint8Array(buffer), request),
      () => unreadable(request),
    );
  };

  const onDrop = (event: DragEvent) => {
    event.preventDefault();
    openFile(event.dataTransfer.files[0]);
  };

  // The diagram's text follows DATUM's type tokens, so the user's text size.
  const sentStyle = useRef('');
  const onTextStyle = useCallback(
    (style: TextStyle) => {
      // A theme or pixel-ratio change gives the same sizes: nothing to lay out.
      const key = JSON.stringify(style);
      if (!core || key === sentStyle.current) return;
      core
        .setStyle(style)
        .then(() => core.diagram())
        .then(
          (next) => {
            // Kept only once the core has it, so a failure is tried again.
            sentStyle.current = key;
            showRelayout(next);
          },
          () => setState({ kind: 'relayoutFailed' }),
        );
    },
    [core, showRelayout],
  );

  // The canvas takes its size from the workspace's centre surface (D5).
  const [centreSize, setCentreSize] = useState({ width: 0, height: 0 });
  const onCentreSize = useCallback(
    (size: { inline: number; block: number }) => setCentreSize({ width: size.inline, height: size.block }),
    [],
  );

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

  // The start dock stacks the Case Explorer above the Argument Navigator, as
  // the C++ application did (D9); below the wide tier DATUM shows them as tabs.
  // DATUM's own changes hand over a whole layout; the View menu's change from
  // the latest state. The layout is saved when it changes.
  useEffect(() => saveWorkspace(workspace), [workspace]);
  const changeLayout = (next: WorkspaceLayout) => setWorkspace({ layout: next });

  // Which dock holds a panel depends on the tier, as DATUM's measure reports
  // it: at the medium tier the side panels are in one side dock, at the narrow
  // tier every panel is in the bottom dock. A copy of DATUM's rule, read from
  // the docks it measures, until its Workspace says which dock holds a view and
  // can show one (raised there).
  const [shownDocks, setShownDocks] = useState<WorkspaceDockId[]>(['start', 'end', 'bottom']);
  const host = (id: PanelId): WorkspaceDockId => {
    const side = shownDocks.filter((dock) => dock !== 'bottom');
    if (side.length === 0) return 'bottom';
    if (id === 'problems') return 'bottom';
    if (side.length === 1) return side[0];
    return id === 'elementProperties' ? 'end' : 'start';
  };
  // `hidden` applies only where the start dock stacks its views.
  const hidden = layout.hidden ?? [];
  const panels: PanelId[] = PANELS.filter((id) => layout.open[host(id)] && !(host(id) === 'start' && hidden.includes(id)));
  const onPanelsChange = (next: PanelId[]) => {
    const added = next.find((id) => !panels.includes(id));
    const removed = panels.find((id) => !next.includes(id));
    const changed = added ?? removed;
    if (!changed) return;
    setWorkspace((current) => ({ layout: togglePanel(current.layout, changed, added !== undefined, host(changed)) }));
  };
  const chooseFile = useCallback(() => input.current?.click(), []);

  // Ctrl+O (⌘O on macOS) opens a file, as File ▸ Open… says.
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      // Not when something else has the key: a component that handled it, an
      // open menu or dialog, or a field being typed in.
      const target = event.target instanceof Element ? event.target : null;
      const busy = target?.closest('input, textarea, select, [contenteditable="true"], [role="menu"], [role="dialog"]');
      if (event.defaultPrevented || busy || !isOpenShortcut(event)) return;
      event.preventDefault();
      // As the File menu: nothing to open into until the core has started.
      if (core) chooseFile();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [chooseFile, core]);

  const status = message(state, t, language);
  return (
    <DatumRoot app className="af-app" lang={language} theme={theme === 'system' ? undefined : theme}>
      <MenuBar
        t={t}
        canOpen={core !== null}
        onOpen={chooseFile}
        panels={panels}
        onPanelsChange={onPanelsChange}
        theme={theme}
        onThemeChange={setTheme}
        language={language}
        onLanguageChange={setLanguage}
      />
      <Toolbar label={t('toolbar')}>
        <ToolbarGroup>
          <Button icon={<FolderOpen aria-hidden="true" />} onClick={chooseFile} unavailable={!core}>
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
      <div className="af-workspace" onDragOver={(event) => event.preventDefault()} onDrop={onDrop}>
        <Workspace
          centreLabel={t('diagram')}
          onCentreSize={onCentreSize}
          layout={layout}
          onLayoutChange={changeLayout}
          onMeasure={(measure) => {
            const docks = measure.docks.map((dock) => dock.id);
            setShownDocks((current) => (current.join() === docks.join() ? current : docks));
          }}
          copy={copy}
          start={{
            // Both C++ panels, stacked; the dock's own name is for its rail.
            name: t('startDock'),
            nameInText: t('startDockInText'),
            min: 14,
            arrangement: 'stacked',
            icons: { hide: <PanelLeftClose />, show: <PanelLeftOpen /> },
            views: [
              {
                id: 'caseExplorer',
                label: t('caseExplorer'),
                nameInText: t('caseExplorerInText'),
                content: empty('caseExplorerEmpty'),
              },
              {
                id: 'argumentNavigator',
                label: t('argumentNavigator'),
                nameInText: t('argumentNavigatorInText'),
                content: empty('argumentNavigatorEmpty'),
              },
            ],
          }}
          end={{
            name: t('elementProperties'),
            nameInText: t('elementPropertiesInText'),
            min: 14,
            icons: { hide: <PanelRightClose />, show: <PanelRightOpen /> },
            views: [{ id: 'elementProperties', label: t('elementProperties'), content: empty('elementPropertiesEmpty') }],
          }}
          bottom={{
            name: t('problems'),
            nameInText: t('problemsInText'),
            min: 7,
            icons: { hide: <ChevronDown />, show: <ChevronUp /> },
            views: [{ id: 'problems', label: t('problems'), content: empty('problemsEmpty') }],
          }}
        >
          <DiagramCanvas diagram={diagram} label={t('canvas')} size={centreSize} onTextStyle={onTextStyle} />
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
