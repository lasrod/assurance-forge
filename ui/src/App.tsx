import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState, type DragEvent } from 'react';
import { DiagramCanvas, ZOOM_STEP } from './canvas/DiagramCanvas';
import { findEdge, fit, zoomAbout, type Viewport } from './canvas/draw';
import type { Core, DiagramAt, OpenedFile, OpenResult, OutlineRow, Package, RecentFile, TextStyle } from './core/Core';
import { ElementProperties, type Details } from './ElementProperties';
import {
  ChevronDown,
  ChevronUp,
  DatumRoot,
  PanelLeftClose,
  PanelLeftOpen,
  PanelRightClose,
  PanelRightOpen,
  Status,
  StatusBar,
  StatusBarSpacer,
  Tree,
  Workspace,
  type WorkspaceCopy,
  type WorkspaceHandle,
  type WorkspaceLayout,
  type WorkspaceMeasure,
} from './design';
import { initialTheme, translate, type Language, type MessageKey, type ThemeChoice } from './i18n/messages';
import { StartError } from './core/start';
import { wordRefusal } from './i18n/refusals';
import { loadLayout, saveLayout } from './layout';
import { isOpenShortcut } from './shortcuts';
import { MenuBar } from './MenuBar';
import { ancestors, outlineTree, packageTree } from './outline';
import { packageKind } from './i18n/packages';
import { report } from './testHook';
import { Welcome } from './Welcome';
import { ZoomBar } from './ZoomBar';
import exampleUrl from '../../testdata/sacm/sacm_example_core_argument.sacm.xmi?url';

/** The example the welcome page opens: its name, as a file would have one. */
const EXAMPLE_NAME = 'sacm_example_core_argument.sacm.xmi';
/** The Case Explorer's row for the file itself, above its packages. */
const FILE_ROW = ':file';

type State =
  | { kind: 'starting' }
  | { kind: 'recentGone' }
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
    case 'recentGone':
      return { text: t('recentGone'), tone: 'error' };
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
  const [details, setDetails] = useState<Details | null>(null);
  // What the left panels show: the argument's tree and the file's packages.
  const [outline, setOutline] = useState<OutlineRow[]>([]);
  const [packages, setPackages] = useState<Package[]>([]);
  const [fileName, setFileName] = useState<string | null>(null);
  const [recent, setRecent] = useState<RecentFile[]>([]);
  // A file is being read, or the shell has not yet said whether it was started
  // with one: the welcome page waits, so it does not flash before a diagram.
  const [reading, setReading] = useState(false);
  const [asked, setAsked] = useState(false);
  const [layout, setLayout] = useState<WorkspaceLayout>(loadLayout);
  const [theme, setTheme] = useState<ThemeChoice>(() => initialTheme(location.search));
  const input = useRef<HTMLInputElement>(null);
  const t: T = useCallback((key, values) => translate(language, key, values), [language]);

  // A file given before the core is ready waits for it.
  const [pending, setPending] = useState<{ name: string; bytes: Uint8Array; request: number } | null>(null);
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
  /** Nothing is open on the page: the welcome page shows. */
  const clear = useCallback(() => {
    shown.current = null;
    setDiagram(null);
    setOutline([]);
    setPackages([]);
    setFileName(null);
    setViewport(null);
    setSelected(null);
    setReading(false);
  }, []);
  /** A read that failed, unless a later request has started: nothing is open, in the core too. */
  const unreadable = useCallback(
    (request: number) => {
      if (request !== latest.current) return;
      clear();
      setState({ kind: 'unreadable' });
      void coreRef.current?.close();
    },
    [clear],
  );
  const refreshRecent = useCallback((on: Core) => {
    on.recent.list().then(setRecent, () => setRecent([]));
  }, []);
  /**
   * Shows what an open gave, unless a later request has started: the diagram
   * of a file that opened, fitted with nothing selected, or its refusal. The
   * core may have read the bytes itself (a file named at start, or chosen in
   * the desktop's open dialog).
   */
  const show = useCallback(
    async (on: Core, opening: () => Promise<OpenedFile>, request: number) => {
      try {
        const { name, result } = await opening();
        const isOpen = result.result === 'opened';
        const [next, rows, held] = isOpen ? await Promise.all([on.diagram(), on.outline(), on.packages()]) : [null, null, []];
        if (request !== latest.current) return;
        clear();
        shown.current = isOpen ? result.revision : null;
        setState({ kind: 'result', result });
        setDiagram(next);
        setOutline(rows?.rows ?? []);
        setPackages(held);
        setFileName(isOpen ? name : null);
        refreshRecent(on);
      } catch {
        unreadable(request);
      }
    },
    [clear, refreshRecent, unreadable],
  );
  const open = useCallback(
    async (name: string, bytes: Uint8Array, request: number) => {
      // A later request has started: this one must not reach the core, which
      // holds one document, nor wait for it.
      if (request !== latest.current) return;
      if (!core) {
        setPending({ name, bytes, request });
        return;
      }
      await show(
        core,
        async () => {
          const result = await core.open(bytes);
          // A file the page read itself: the shell keeps it as a recent file
          // if it can. The diagram does not wait for the copy to be written.
          void core.recent.remember(name, bytes, result);
          return { name, result };
        },
        request,
      );
    },
    [core, show],
  );
  /** Starts a request: the bytes it reads open only if no later one started. */
  const begin = () => {
    latest.current += 1;
    setReading(true);
    return latest.current;
  };
  useEffect(() => {
    if (core && pending) {
      setPending(null);
      void open(pending.name, pending.bytes, pending.request);
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
      .then(() => Promise.all([core.diagram(), core.outline()]))
      .then(
        ([next, rows]) => {
          if (!current) return;
          showRelayout(next);
          // The tree's names follow the language, as the nodes' do.
          if (rows && rows.revision === shown.current) setOutline(rows.rows);
        },
        () => {
          if (current) setState({ kind: 'relayoutFailed' });
        },
      );
    return () => {
      current = false;
    };
  }, [core, language, showRelayout]);

  // The desktop application opens a file named on its command line.
  useEffect(() => {
    if (!core) return;
    let wanted = true;
    core.initialFile().then(
      (file) => {
        if (!wanted) return;
        setAsked(true);
        if (!file) return;
        const request = begin();
        void show(core, async () => file, request);
      },
      () => {
        if (wanted) setAsked(true);
      },
    );
    refreshRecent(core);
    return () => {
      wanted = false;
    };
  }, [core, show, refreshRecent]);

  // In development and the browser tests, `?sample=<file>` opens a sample case.
  useEffect(() => {
    const sample = new URLSearchParams(location.search).get('sample');
    if (!core || !sample) return;
    const request = begin();
    fetch(`/samples/${encodeURIComponent(sample)}`)
      .then((response) => (response.ok ? response.arrayBuffer() : Promise.reject(new Error(String(response.status)))))
      .then((buffer) => open(sample, new Uint8Array(buffer), request))
      .catch(() => unreadable(request));
  }, [core, open, unreadable]);

  const openFile = (file: File | undefined) => {
    if (!file) return;
    const request = begin();
    file.arrayBuffer().then(
      (buffer) => void open(file.name, new Uint8Array(buffer), request),
      () => unreadable(request),
    );
  };

  /** The example the welcome page offers: a file built into the application. */
  const openExample = () => {
    const request = begin();
    fetch(exampleUrl)
      .then((response) => (response.ok ? response.arrayBuffer() : Promise.reject(new Error(String(response.status)))))
      .then((buffer) => open(EXAMPLE_NAME, new Uint8Array(buffer), request))
      .catch(() => unreadable(request));
  };

  const openRecent = (id: string) => {
    if (!core) return;
    const request = begin();
    core.recent.open(id, () => request === latest.current).then(
      (file) => {
        if (file) {
          void show(core, async () => file, request);
          return;
        }
        if (request !== latest.current) return;
        // No longer there: off the list, and said so. The core was not
        // touched, so what was open stays open.
        refreshRecent(core);
        setReading(false);
        setState({ kind: 'recentGone' });
      },
      () => unreadable(request),
    );
  };

  const clearRecent = () => {
    if (!core) return;
    core.recent.clear().then(
      () => refreshRecent(core),
      () => refreshRecent(core),
    );
  };

  /** File ▸ Close: nothing is open after, in the core too, and the welcome page shows. */
  const closeFile = () => {
    begin();
    clear();
    setState({ kind: 'empty' });
    void core?.close();
  };

  const onDrop = (event: DragEvent) => {
    event.preventDefault();
    openFile(event.dataTransfer.files[0]);
  };

  // The diagram's text follows DATUM's type tokens, so the user's text size.
  const sentStyle = useRef('');
  // The view is a fit the application made (a new file's, or Fit), which the
  // user has not moved since: it is made again when the diagram is laid out
  // again for other text sizes. A resize keeps its centre, and keeps this.
  const fitted = useRef(false);
  /** A view the user chose: zoom, pan, a node brought into view. */
  const moveView = useCallback((next: Viewport | ((view: Viewport | null) => Viewport | null)) => {
    fitted.current = false;
    setViewport(next);
  }, []);
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
            // A view that is still the fit made for the earlier sizes is made
            // again for these: the canvas, which says them, mounts only once
            // the first file is drawn.
            if (fitted.current) setViewport(null);
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
      fitted.current = true;
      setViewport(fit(diagram.diagram.bounds, centreSize.width, centreSize.height));
    }
  }, [viewport, diagram, centreSize]);

  const fitAll = () => {
    if (!diagram) return;
    fitted.current = true;
    setViewport(fit(diagram.diagram.bounds, centreSize.width, centreSize.height));
  };
  const zoomBy = (factor: number) =>
    moveView((view) => view && zoomAbout(view, factor, centreSize.width / 2, centreSize.height / 2));

  // What Element Properties shows: the core's details of the selected node or
  // relationship, read again when the diagram is (its text follows the
  // language). An answer for a selection that has since changed is dropped.
  useEffect(() => {
    if (!core || !selected || !diagram) {
      setDetails(null);
      return;
    }
    let current = true;
    // A node by its id; else one edge, whose relationship the core describes.
    const edge = diagram.diagram.nodes.some((node) => node.id === selected) ? null : findEdge(diagram.diagram, selected);
    const read: Promise<Details | null> = edge
      ? core.relationship(edge.relationship).then((relationship) => relationship && { type: 'relationship', relationship, edge })
      : core.element(selected).then((element) => element && { type: 'element', element });
    read.then(
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

  // The Argument Navigator: the tree the diagram is laid out from. It and the
  // canvas share one selection, so the tree is the canvas's parallel structure.
  const argument = useMemo(() => outlineTree(outline), [outline]);
  const [expanded, setExpanded] = useState<Set<string>>(new Set());
  // A new file's tree starts open, as the C++ application's does. The same
  // file in another language keeps the branches the user closed.
  const expandedFor = useRef<number | null>(null);
  const revision = diagram?.revision ?? null;
  useEffect(() => {
    if (revision === expandedFor.current) return;
    expandedFor.current = revision;
    setExpanded(new Set(argument.branches));
  }, [revision, argument]);
  // A node selected on the canvas shows in the tree: its ancestors open.
  useEffect(() => {
    if (!selected) return;
    const closed = ancestors(argument.parents, selected);
    setExpanded((open) => (closed.every((id) => open.has(id)) ? open : new Set([...open, ...closed])));
  }, [selected, argument]);
  /** Selects a node from the tree, and brings it into view if it is not wholly in it. */
  const selectFromTree = (id: string) => {
    setSelected(id);
    const node = diagram?.diagram.nodes.find((candidate) => candidate.id === id);
    if (!node || !viewport || centreSize.width <= 0 || centreSize.height <= 0) return;
    const left = node.bounds.x * viewport.scale + viewport.offsetX;
    const top = node.bounds.y * viewport.scale + viewport.offsetY;
    const [width, height] = [node.bounds.width * viewport.scale, node.bounds.height * viewport.scale];
    if (left >= 0 && top >= 0 && left + width <= centreSize.width && top + height <= centreSize.height) return;
    moveView({
      ...viewport,
      offsetX: viewport.offsetX + (centreSize.width - width) / 2 - left,
      offsetY: viewport.offsetY + (centreSize.height - height) / 2 - top,
    });
  };

  // The Case Explorer: the file, and its packages under it.
  const explorer = useMemo(() => {
    const kind = (sacmClass: string) => packageKind(sacmClass, t);
    const built = packageTree(packages, kind);
    return fileName === null ? [] : [{ value: FILE_ROW, label: fileName, textValue: fileName, children: built.nodes }];
  }, [packages, fileName, t]);
  const explorerOpen = useMemo(
    () => new Set([FILE_ROW, ...packageTree(packages, () => '').branches]),
    [packages],
  );
  const [explorerExpanded, setExplorerExpanded] = useState<Set<string>>(new Set());
  useEffect(() => setExplorerExpanded(explorerOpen), [explorerOpen]);
  const [explorerSelected, setExplorerSelected] = useState<string | null>(FILE_ROW);
  useEffect(() => setExplorerSelected(FILE_ROW), [fileName]);

  // The View menu's panels are the workspace's views. The workspace says which
  // show, at whatever tier it is in, and shows or hides one.
  const workspace = useRef<WorkspaceHandle>(null);
  const [measure, setMeasure] = useState<WorkspaceMeasure | null>(null);
  const panelNames = [
    { id: 'caseExplorer', name: t('caseExplorer') },
    { id: 'argumentNavigator', name: t('argumentNavigator') },
    { id: 'elementProperties', name: t('elementProperties') },
    { id: 'problems', name: t('problems') },
  ];
  const showing = measure?.docks.flatMap((dock) => dock.views.filter((item) => item.shown).map((item) => item.id)) ?? [];
  const setPanels = (ids: string[]) => {
    const change = panelNames.find((panel) => ids.includes(panel.id) !== showing.includes(panel.id));
    if (!change) return;
    if (ids.includes(change.id)) workspace.current?.showView(change.id);
    else workspace.current?.hideView(change.id);
  };

  // With no file there is no canvas to say what is drawn: nothing is.
  const noFile = diagram === null;
  useEffect(() => {
    if (noFile) report({ revision: null, drawn: { nodes: 0, edges: 0 }, diagram: null, viewport: null, selected: null, hovered: null });
  }, [noFile, state]);

  // The window's title names the file.
  useEffect(() => {
    document.title = fileName ? `${fileName} – Assurance Forge` : 'Assurance Forge';
  }, [fileName]);

  // DATUM's Workspace owns the layout: which docks are open, their sizes, and
  // which stacked views are hidden. The page keeps it between visits.
  useEffect(() => saveLayout(layout), [layout]);

  // The desktop's own open dialog, which knows the file's path; in the browser,
  // the file input.
  const chooseFile = useCallback(() => {
    const dialog = core?.openDialog;
    if (!core || !dialog) {
      input.current?.click();
      return;
    }
    // Numbered once a file is chosen: a cancelled dialog leaves an open in
    // progress alone. The core has read the file by then.
    dialog().then(
      (file) => {
        if (file) void show(core, async () => file, begin());
      },
      () => unreadable(begin()),
    );
  }, [core, show, unreadable]);

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
  const hasFile = diagram !== null;
  // The welcome page shows while nothing is open, once the shell has said
  // whether it was started with a file and no file is being read.
  const welcome = !hasFile && !reading && (core !== null ? asked : state.kind === 'failed');
  return (
    <DatumRoot app className="af-app" lang={language} theme={theme === 'system' ? undefined : theme}>
      <MenuBar
        t={t}
        canOpen={core !== null}
        starting={state.kind === 'starting'}
        onOpen={chooseFile}
        recent={recent}
        onRecent={openRecent}
        canClose={hasFile}
        onClose={closeFile}
        panels={hasFile ? { all: panelNames, showing, onChange: setPanels } : null}
        theme={theme}
        onThemeChange={setTheme}
        language={language}
        onLanguageChange={setLanguage}
      />
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
        {welcome && (
          <Welcome
            t={t}
            language={language}
            ready={core !== null}
            onOpen={chooseFile}
            onExample={openExample}
            recent={recent}
            onRecent={openRecent}
            onClearRecent={clearRecent}
            keepsCopies={core?.recent.keepsCopies ?? false}
          />
        )}
        {hasFile && (
          <Workspace
            ref={workspace}
            centreLabel={t('diagram')}
            onCentreSize={onCentreSize}
            onMeasure={setMeasure}
            layout={layout}
            onLayoutChange={setLayout}
            copy={copy}
            centreHeader={<ZoomBar t={t} scale={viewport?.scale ?? null} onZoomBy={zoomBy} onFit={fitAll} step={ZOOM_STEP} />}
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
                  min: 7,
                  content:
                    packages.length === 0 && fileName === null ? (
                      empty('caseExplorerEmpty')
                    ) : (
                      <Tree
                        label={t('caseExplorer')}
                        nodes={explorer}
                        selected={explorerSelected}
                        onSelect={setExplorerSelected}
                        expanded={explorerExpanded}
                        onExpandedChange={setExplorerExpanded}
                      />
                    ),
                },
                {
                  id: 'argumentNavigator',
                  label: t('argumentNavigator'),
                  nameInText: t('argumentNavigatorInText'),
                  content:
                    argument.nodes.length === 0 ? (
                      empty('argumentNavigatorEmpty')
                    ) : (
                      <Tree
                        label={t('argumentNavigator')}
                        nodes={argument.nodes}
                        selected={selected}
                        onSelect={selectFromTree}
                        expanded={expanded}
                        onExpandedChange={setExpanded}
                      />
                    ),
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
              onViewport={moveView}
              selected={selected}
              onSelect={setSelected}
              onTextStyle={onTextStyle}
            />
          </Workspace>
        )}
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
