import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState, type DragEvent } from 'react';
import { DiagramCanvas, ZOOM_STEP } from './canvas/DiagramCanvas';
import { fit, zoomAbout, type Viewport } from './canvas/draw';
import type { Core, DiagramAt, ElementDetails, OpenResult, TextStyle } from './core/Core';
import { ElementProperties } from './ElementProperties';
import {
  Button,
  ChevronDown,
  ChevronUp,
  DatumRoot,
  FolderOpen,
  Maximize,
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
  ZoomIn,
  ZoomOut,
  type WorkspaceCopy,
  type WorkspaceLayout,
} from './design';
import { initialTheme, translate, type Language, type MessageKey, type ThemeChoice } from './i18n/messages';
import { StartError } from './core/wasmCore';
import { wordRefusal } from './i18n/refusals';
import { loadLayout, saveLayout } from './layout';
import { isOpenShortcut } from './shortcuts';
import { MenuBar } from './MenuBar';

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
  // Pan and zoom, and the selection, stay in the frontend. A new file starts
  // fitted (null until the canvas has a size) with nothing selected.
  const [viewport, setViewport] = useState<Viewport | null>(null);
  const [selected, setSelected] = useState<string | null>(null);
  const [details, setDetails] = useState<ElementDetails | null>(null);
  const [layout, setLayout] = useState<WorkspaceLayout>(loadLayout);
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
    setViewport(null);
    setSelected(null);
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
        setViewport(null);
        setSelected(null);
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

  // The canvas takes its size from the workspace's centre surface (D5). When
  // it changes, the point at the centre of the view stays at its centre.
  const [centreSize, setCentreSize] = useState({ width: 0, height: 0 });
  const onCentreSize = useCallback((size: { inline: number; block: number }) => {
    setCentreSize((before) => {
      const after = { width: size.inline, height: size.block };
      if (before.width > 0 && before.height > 0 && after.width > 0 && after.height > 0) {
        const dx = (after.width - before.width) / 2;
        const dy = (after.height - before.height) / 2;
        setViewport((view) => view && { ...view, offsetX: view.offsetX + dx, offsetY: view.offsetY + dy });
      }
      return after;
    });
  }, []);

  // A new file is fitted once the canvas has a size, before it is painted.
  useLayoutEffect(() => {
    if (!viewport && diagram && centreSize.width > 0 && centreSize.height > 0) {
      setViewport(fit(diagram.diagram.bounds, centreSize.width, centreSize.height));
    }
  }, [viewport, diagram, centreSize]);

  const fitAll = () => {
    if (diagram) setViewport(fit(diagram.diagram.bounds, centreSize.width, centreSize.height));
  };
  const zoomBy = (factor: number) =>
    setViewport((view) => view && zoomAbout(view, factor, centreSize.width / 2, centreSize.height / 2));

  // What Element Properties shows: the core's details of the selected node, read
  // again when the diagram is (its text follows the language). An answer for a
  // selection that has since changed is dropped.
  useEffect(() => {
    if (!core || !selected || !diagram) {
      setDetails(null);
      return;
    }
    let current = true;
    core.element(selected).then(
      (next) => {
        if (current) setDetails(next);
      },
      () => {
        if (current) setDetails(null);
      },
    );
    return () => {
      current = false;
    };
  }, [core, selected, diagram]);

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

  // DATUM's Workspace owns the layout: which docks are open, their sizes, and
  // which stacked views are hidden. The page keeps it between visits.
  useEffect(() => saveLayout(layout), [layout]);

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
        starting={state.kind === 'starting'}
        onOpen={chooseFile}
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
        {/* Panning and zooming never need a drag (WCAG 2.5.7): buttons and keys too. */}
        <ToolbarGroup>
          <Button icon={<ZoomOut aria-hidden="true" />} onClick={() => zoomBy(1 / ZOOM_STEP)} unavailable={!viewport}>
            {t('zoomOut')}
          </Button>
          <Button icon={<ZoomIn aria-hidden="true" />} onClick={() => zoomBy(ZOOM_STEP)} unavailable={!viewport}>
            {t('zoomIn')}
          </Button>
          <Button icon={<Maximize aria-hidden="true" />} onClick={fitAll} unavailable={!viewport}>
            {t('fit')}
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
          onLayoutChange={setLayout}
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
            views: [
              { id: 'elementProperties', label: t('elementProperties'), content: <ElementProperties details={details} t={t} /> },
            ],
          }}
          bottom={{
            name: t('problems'),
            nameInText: t('problemsInText'),
            min: 7,
            icons: { hide: <ChevronDown />, show: <ChevronUp /> },
            views: [{ id: 'problems', label: t('problems'), content: empty('problemsEmpty') }],
          }}
        >
          <DiagramCanvas
            diagram={diagram}
            label={t('canvas')}
            size={centreSize}
            viewport={viewport}
            onViewport={setViewport}
            selected={selected}
            onSelect={setSelected}
            onTextStyle={onTextStyle}
          />
        </Workspace>
      </div>
      <StatusBar>
        <Status tone={status.tone} data-testid="status">
          {status.text}
        </Status>
        <StatusBarSpacer />
        {viewport && diagram && <Status data-testid="zoom">{t('zoom', { percent: Math.round(viewport.scale * 100) })}</Status>}
      </StatusBar>
    </DatumRoot>
  );
}
