import { useCallback, useEffect, useMemo, useRef, useState, type DragEvent } from 'react';
import { DiagramCanvas, ZOOM_STEP } from './canvas/DiagramCanvas';
import type { DragEnd } from './canvas/drag';
import { useViewport } from './canvas/useViewport';
import type { AddChoice, Core, ElementKind, MoveTargets, NodeKind, Removal, TextField, TextStyle } from './core/Core';
import { addItems, kindName, refusalText } from './addMenu';
import { ElementProperties } from './ElementProperties';
import { RemoveDialog } from './RemoveDialog';
import { ProblemsPanel } from './ProblemsPanel';
import { RecoveryDialog } from './RecoveryDialog';
import { UnsavedDialog } from './UnsavedDialog';
import {
  Button,
  ChevronDown,
  ChevronUp,
  ContextMenu,
  DatumRoot,
  MenuItem,
  MenuSeparator,
  PanelLeftClose,
  PanelLeftOpen,
  PanelRightClose,
  PanelRightOpen,
  Status,
  StatusBar,
  StatusBarSpacer,
  Tree,
  Workspace,
  type ContextMenuSource,
  type WorkspaceCopy,
  type WorkspaceHandle,
  type WorkspaceLayout,
  type WorkspaceMeasure,
} from './design';
import { initialTheme, translate, type Language, type MessageKey, type ThemeChoice } from './i18n/messages';
import { wordRefusal } from './i18n/refusals';
import { loadLayout, saveLayout } from './layout';
import { editShortcut, isOpenShortcut, saveShortcutOf } from './shortcuts';
import { MenuBar } from './MenuBar';
import { ancestors, outlineTree, packageTree } from './outline';
import { packageKind } from './i18n/packages';
import { report } from './testHook';
import { useDetails, useDocument, type DocumentState } from './useDocument';
import { Welcome } from './Welcome';
import { ZoomBar } from './ZoomBar';

// The page: the workspace and what is in it. What it shows comes from two
// places: the open document's answers from the core (useDocument.ts), and the
// view of the canvas and its selection (canvas/useViewport.ts).

/** The Case Explorer's row for the file itself, above its packages. */
const FILE_ROW = ':file';
/** The menu item that removes the node; the others are the kinds to add. */
const REMOVE = 'remove';
/** The menu items of a move: start one, make it here, give it up. */
const MOVE = 'move';
const MOVE_HERE = 'moveHere';
const MOVE_CANCEL = 'moveCancel';
/** The menu item that marks a Goal undeveloped, or clears the mark. */
const UNDEVELOPED = 'undeveloped';

type T = (key: MessageKey, values?: Record<string, string | number>) => string;

/** A text of an element, as Element Properties labels it. */
const TEXT_FIELDS: Record<TextField, MessageKey> = {
  identifier: 'propertyId',
  name: 'propertyName',
  statement: 'propertyStatement',
};

/** An `AF-RECOVER-*` code in words; another code as it is. */
function recoveryReason(code: string, t: T): string {
  return /^AF-RECOVER-00[1-4]$/.test(code) ? t(code as MessageKey) : code;
}

function message(state: DocumentState, t: T, language: Language): { text: string; tone?: 'error' } {
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
    case 'added':
      return { text: t('added', { kind: kindName(state.added, t), identifier: state.identifier }) };
    case 'textChanged':
      return { text: t('textChanged', { field: t(TEXT_FIELDS[state.field]) }) };
    case 'removed':
      return {
        text:
          state.more === 0
            ? t('removed', { kind: kindName(state.removed, t), identifier: state.identifier })
            : t('removedWithMore', { kind: kindName(state.removed, t), identifier: state.identifier, more: state.more }),
      };
    case 'moved':
      return {
        text:
          state.to === null
            ? t('movedToTop', { kind: kindName(state.moved, t), identifier: state.identifier })
            : t('moved', { kind: kindName(state.moved, t), identifier: state.identifier, to: state.to }),
      };
    case 'undevelopedSet':
      return { text: t(state.undeveloped ? 'markedUndeveloped' : 'clearedUndeveloped', { identifier: state.identifier }) };
    case 'repaired':
      return { text: t('repaired', { kind: state.repaired ? kindName(state.repaired, t) : '', identifier: state.identifier }).trim() };
    case 'undone':
      return { text: t('undone') };
    case 'redone':
      return { text: t('redone') };
    case 'editRefused':
      return { text: t('editRefused', { reason: refusalText(state.code, state.about, t) }), tone: 'error' };
    case 'saved':
      return { text: t(state.downloaded ? 'downloaded' : 'saved', { name: state.name }) };
    case 'saveRefused': {
      const known = /^AF-SAVE-00[1-7]$/.test(state.code);
      return { text: t('saveRefused', { reason: known ? t(state.code as MessageKey) : state.code }), tone: 'error' };
    }
    case 'restored':
      return { text: t('restored') };
    case 'recoveryDiscarded':
      return { text: t('recoveryDiscarded') };
    case 'recoverySetAside':
      return { text: t('recoverySetAside', { reason: recoveryReason(state.code, t) }), tone: 'error' };
    case 'recoveryNotKept':
      return { text: t('recoveryNotKept', { reason: recoveryReason(state.code, t) }), tone: 'error' };
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
  const [layout, setLayout] = useState<WorkspaceLayout>(loadLayout);
  const [theme, setTheme] = useState<ThemeChoice>(() => initialTheme(location.search));
  const input = useRef<HTMLInputElement>(null);
  const t: T = useCallback((key, values) => translate(language, key, values), [language]);

  const doc = useDocument(start, initialLanguage, language);
  const { core, state, diagram, outline, packages, fileName, recent, reading, asked, history, opened } = doc;
  const view = useViewport(diagram, opened);
  const { viewport, selected, setSelected, centreSize, moveView, onCentreSize, fitAll, zoomBy, selectAndShow, refitIfFitted, followEdit } =
    view;
  const details = useDetails(core, selected, diagram);
  // The case has changes that are not saved: the title and the status bar say so.
  const modified = diagram !== null && (history?.modified ?? false);

  // Edit ▸ Undo and Redo, with the view following each.
  const { undo: undoWith, redo: redoWith, addChoices, add, removal, remove: removeWith, setText: setTextWith } = doc;
  const { moveTargets, move: moveWith, moveRefused, setUndeveloped: setUndevelopedWith, repair: repairWith } = doc;
  /** The undeveloped mark of a Goal set or cleared; the view keeps the Goal in place. */
  const setUndeveloped = useCallback(
    (id: string, undeveloped: boolean, identifier: string) => setUndevelopedWith(id, undeveloped, identifier, followEdit),
    [setUndevelopedWith, followEdit],
  );
  /** A text of an element changed in Element Properties; the view keeps the element in place. */
  const setText = useCallback(
    (id: string, field: TextField, text: string, about: NodeKind) => setTextWith(id, field, text, about, followEdit),
    [setTextWith, followEdit],
  );
  const undo = useCallback(() => undoWith(followEdit), [undoWith, followEdit]);
  const redo = useCallback(() => redoWith(followEdit), [redoWith, followEdit]);

  // The add menu while it is open: for a node or the empty canvas, at a point
  // in client coordinates, with the revision its choices are of. Another file
  // being read, or none open, closes it: its choices are of the file before.
  const [addMenu, setAddMenu] = useState<{
    revision: number;
    parent: string | null;
    about: NodeKind | null;
    at: { x: number; y: number };
    source: ContextMenuSource;
    choices: AddChoice[];
    /** For a Goal whose undeveloped mark can be changed: what it is now, and its identifier. */
    undeveloped?: { marked: boolean; identifier: string } | null;
  } | null>(null);
  const noDiagram = diagram === null;
  useEffect(() => {
    setAddMenu(null);
  }, [opened]);
  useEffect(() => {
    if (reading || noDiagram) setAddMenu(null);
  }, [reading, noDiagram]);
  // A move being chosen: the node to move and where the core says it may
  // go, at a revision. Another revision on screen, or another file, ends it:
  // its targets are of the document before.
  // `dragged`: the node is being dragged there, so no prompt is shown and
  // the choice ends with the drop.
  const [moving, setMoving] = useState<{ targets: MoveTargets; kind: NodeKind; identifier: string; dragged: boolean } | null>(
    null,
  );
  // The node being dragged: places that come after it was dropped are not shown.
  const dragged = useRef<string | null>(null);
  const shownRevision = diagram?.revision ?? null;
  if (moving && moving.targets.revision !== shownRevision) setMoving(null);
  const moveMarks = useMemo(
    () =>
      moving
        ? {
            source: moving.targets.id,
            allowed: new Set(moving.targets.nodes.filter((target) => target.refusal === null).map((target) => target.id)),
          }
        : null,
    [moving],
  );
  /**
   * Starts choosing where the node `id` goes, from the menu or by a drag;
   * the status says why when it cannot be moved. Answers whether it can.
   */
  const startMove = useCallback(
    async (id: string, byDrag: boolean): Promise<boolean> => {
      dragged.current = byDrag ? id : null;
      const targets = await moveTargets(id);
      const node = diagram?.diagram.nodes.find((candidate) => candidate.id === id);
      if (!targets || !node) return false;
      // Dropped, or given up, before the core answered.
      if (byDrag && dragged.current !== id) return true;
      const row = outline.find((candidate) => candidate.id === id);
      setSelected(id);
      setMoving({ targets, kind: node.kind, identifier: row?.identifier ?? id, dragged: byDrag });
      return true;
    },
    [moveTargets, diagram, outline, setSelected],
  );
  /** Moves the node being moved under `to` (null: to the top level), or says why it cannot go there. */
  const moveTo = useCallback(
    (to: string | null) => {
      if (!moving) return;
      const place = to === null ? null : moving.targets.nodes.find((target) => target.id === to);
      const refusal = to === null ? moving.targets.canvas : place ? place.refusal : 'AF-EDIT-011';
      if (refusal !== null) {
        // The kind a leaf's refusal names is the place's, not the node's moved.
        const kind = to ? (diagram?.diagram.nodes.find((node) => node.id === to)?.kind ?? null) : null;
        moveRefused(refusal, kind);
        return;
      }
      const toIdentifier = to ? (outline.find((row) => row.id === to)?.identifier ?? to) : null;
      setMoving(null);
      moveWith(moving.targets, to, moving, toIdentifier, followEdit);
    },
    [moving, diagram, outline, moveRefused, moveWith, followEdit],
  );
  /**
   * A dragged node was dropped: on a place it may go, it moves there; on one
   * it may not, the status says why. A drop on nothing gives the drag up: a
   * Goal goes to the top level from the menu, not by a drop that missed.
   * The choice ends with the drop either way.
   */
  const endDrag = useCallback(
    (end: DragEnd) => {
      dragged.current = null;
      if (!moving) return;
      if (end.on === 'node') moveTo(end.id);
      setMoving(null);
    },
    [moving, moveTo],
  );
  /**
   * Opens the menu of a node, or of the empty canvas, at a client point: the
   * node is selected first. It lists what can be added there and, for a
   * node, Remove; with nothing to list, it does not open.
   */
  // Each request for a menu: an answer to an earlier one is dropped.
  const menuRequest = useRef(0);
  const openAddMenu = useCallback(
    (parent: string | null, at: { x: number; y: number }, source: ContextMenuSource) => {
      const request = ++menuRequest.current;
      void addChoices(parent).then((answer) => {
        if (!answer || request !== menuRequest.current) return;
        // While a move is chosen the selection stays the node moved, and
        // the menu opens even with nothing to add: it holds the move.
        if (moving) {
          setAddMenu({ revision: answer.revision, parent, about: null, at, source, choices: [] });
          return;
        }
        if (parent) setSelected(parent);
        if (!parent && answer.choices.every((choice) => choice.refusal !== null)) return;
        const about = parent ? (diagram?.diagram.nodes.find((node) => node.id === parent)?.kind ?? null) : null;
        const menu = { revision: answer.revision, parent, about, at, source, choices: answer.choices };
        if (!parent || about !== 'goal' || !core) {
          setAddMenu(menu);
          return;
        }
        // A Goal's menu also sets or clears its undeveloped mark, where it can be.
        void core.element(parent).then(
          (details) =>
            // Not if another menu was asked for, or a move began, meanwhile.
            request === menuRequest.current &&
            !dragged.current &&
            setAddMenu({
              ...menu,
              undeveloped: details && details.undevelopedRefusal === null ? { marked: details.undeveloped, identifier: details.identifier } : null,
            }),
          () => {
            if (request === menuRequest.current) setAddMenu(menu);
          },
        );
      });
    },
    [addChoices, diagram, setSelected, moving, core],
  );

  // A removal that waits for the user: it deletes more than the node chosen.
  const [confirmRemoval, setConfirmRemoval] = useState<Removal | null>(null);
  useEffect(() => {
    setConfirmRemoval(null);
  }, [opened]);
  /** Removes the node `id`: at once when it goes alone, else once the user has seen what goes with it. */
  const askRemove = useCallback(
    (id: string) => {
      void removal(id).then((answer) => {
        if (!answer) return;
        if (answer.nodes.length > 1) setConfirmRemoval(answer);
        else removeWith(answer, followEdit);
      });
    },
    [removal, removeWith, followEdit],
  );
  /** A choice of the menu: a kind to add, Remove, or a step of a move. */
  const chooseMenu = (id: string) => {
    const menu = addMenu;
    setAddMenu(null);
    if (!menu) return;
    if (id === REMOVE) {
      if (menu.parent) askRemove(menu.parent);
      return;
    }
    if (id === MOVE) {
      if (menu.parent) void startMove(menu.parent, false);
      return;
    }
    if (id === MOVE_HERE) {
      moveTo(menu.parent);
      return;
    }
    if (id === MOVE_CANCEL) {
      setMoving(null);
      return;
    }
    if (id === UNDEVELOPED) {
      if (menu.parent && menu.undeveloped) setUndeveloped(menu.parent, !menu.undeveloped.marked, menu.undeveloped.identifier);
      return;
    }
    add(menu.parent, id as ElementKind, menu.revision, menu.about, followEdit);
  };

  useEffect(() => {
    document.documentElement.lang = language;
  }, [language]);

  // The diagram's text follows DATUM's type tokens, so the user's text size.
  // A view that is still the fit made for the earlier sizes is made again:
  // the canvas, which says them, mounts only once the first file is drawn.
  const { setTextStyle } = doc;
  const onTextStyle = useCallback(
    (style: TextStyle) => {
      void setTextStyle(style).then((laidOut) => {
        if (laidOut) refitIfFitted();
      });
    },
    [setTextStyle, refitIfFitted],
  );

  const onDrop = (event: DragEvent) => {
    event.preventDefault();
    const file = event.dataTransfer.files[0];
    if (file) guard(() => doc.openFile(file));
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

  // The Argument Navigator: the tree the diagram is laid out from. It and the
  // canvas share one selection, so the tree is the canvas's parallel structure.
  const argument = useMemo(() => outlineTree(outline), [outline]);
  const [expanded, setExpanded] = useState<Set<string>>(new Set());
  // A new file's tree starts open, as the C++ application's does. The same
  // file in another language, or at a new revision, keeps the branches the
  // user closed.
  const expandedFor = useRef<number | null>(null);
  useEffect(() => {
    if (opened === expandedFor.current) return;
    expandedFor.current = opened;
    setExpanded(new Set(argument.branches));
  }, [opened, argument]);
  // A node selected on the canvas shows in the tree: its ancestors open.
  useEffect(() => {
    if (!selected) return;
    const closed = ancestors(argument.parents, selected);
    setExpanded((open) => (closed.every((id) => open.has(id)) ? open : new Set([...open, ...closed])));
  }, [selected, argument]);

  // The Case Explorer: the file, and its packages under it.
  const explorer = useMemo(() => {
    const kind = (sacmClass: string) => packageKind(sacmClass, t);
    const built = packageTree(packages, kind);
    return fileName === null ? [] : [{ value: FILE_ROW, label: <span title={fileName}>{fileName}</span>, textValue: fileName, children: built.nodes }];
  }, [packages, fileName, t]);
  const explorerOpen = useMemo(() => new Set([FILE_ROW, ...packageTree(packages, () => '').branches]), [packages]);
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
  // The dock's tab and rail carry the count (P7).
  const problemCount = doc.problems?.problems.length ?? 0;
  const problemsTitle = problemCount > 0 ? t('problemsCount', { count: problemCount }) : t('problems');
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

  // The window's title names the file, marked while changes are not saved.
  useEffect(() => {
    document.title = fileName ? `${modified ? '● ' : ''}${fileName} – Assurance Forge` : 'Assurance Forge';
  }, [fileName, modified]);

  // DATUM's Workspace owns the layout: which docks are open, their sizes, and
  // which stacked views are hidden. The page keeps it between visits.
  useEffect(() => saveLayout(layout), [layout]);

  // Changes that are not saved are asked about before anything loses them
  // (plan decision S8): `guard` runs `then` at once, or once the user has
  // saved or chosen not to.
  const modifiedNow = useRef(modified);
  modifiedNow.current = modified;
  const [unsaved, setUnsaved] = useState<(() => void) | null>(null);
  useEffect(() => {
    if (!modified) setUnsaved(null);
  }, [modified]);
  const guard = useCallback((then: () => void) => {
    if (modifiedNow.current) setUnsaved(() => then);
    else then();
  }, []);
  const { save: saveWith } = doc;
  const save = useCallback(() => void saveWith(false), [saveWith]);
  const saveAs = useCallback(() => void saveWith(true), [saveWith]);

  // The desktop window's close, held by the shell while changes are not saved.
  useEffect(() => {
    const closing = core?.closeGuard;
    if (!closing) return;
    let stop: (() => void) | null = null;
    let wanted = true;
    void closing
      .listen(() => guard(() => void closing.quit()))
      .then((unlisten) => {
        if (wanted) stop = unlisten;
        else unlisten();
      });
    return () => {
      wanted = false;
      stop?.();
    };
  }, [core, guard]);
  // The browser asks itself, in words of its own, before the page goes.
  useEffect(() => {
    if (!modified || core?.closeGuard) return;
    const onLeave = (event: BeforeUnloadEvent) => {
      event.preventDefault();
      event.returnValue = '';
    };
    window.addEventListener('beforeunload', onLeave);
    return () => window.removeEventListener('beforeunload', onLeave);
  }, [modified, core]);

  // The desktop's own open dialog, which knows the file's path; in the browser,
  // the file input.
  const { chooseFile: chooseWith, editsPending } = doc;
  const chooseFile = useCallback(() => guard(() => chooseWith(() => input.current?.click())), [chooseWith, guard]);
  const openRecent = (id: string) => guard(() => doc.openRecent(id));
  const closeFile = () => guard(doc.closeFile);

  // Ctrl+O (⌘O on macOS) opens a file, as File ▸ Open… says.
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      const target = event.target instanceof Element ? event.target : null;
      // Ctrl+S and Ctrl+Shift+S (⌘S and ⇧⌘S), as File ▸ Save and Save As…
      // say: never the browser's own Save Page. Not under an open menu or
      // dialog; from a field, once the field has applied what it holds.
      const saving = saveShortcutOf(event);
      if (saving) {
        // A held key's repeats are kept from the browser too, and save once.
        event.preventDefault();
        if (event.repeat) return;
        if (target?.closest('[role="menu"], [role="dialog"], [role="alertdialog"]')) return;
        if (target instanceof HTMLElement && target.closest('input, textarea')) target.blur();
        if (diagram !== null) {
          if (saving === 'save') save();
          else saveAs();
        }
        return;
      }
      // Not when something else has the key: a component that handled it, an
      // open menu or dialog, or a field being typed in.
      const busy = target?.closest('input, textarea, select, [contenteditable="true"], [role="menu"], [role="dialog"], [role="alertdialog"]');
      if (event.defaultPrevented || busy) return;
      // Ctrl+Z and Ctrl+Y (⌘Z and ⇧⌘Z), as Edit ▸ Undo and Redo say.
      const command = editShortcut(event);
      if (command) {
        event.preventDefault();
        if (event.repeat && editsPending()) return;
        if (command === 'undo' && history?.canUndo) undo();
        if (command === 'redo' && history?.canRedo) redo();
        return;
      }
      if (!isOpenShortcut(event)) return;
      event.preventDefault();
      // As the File menu: nothing to open into until the core has started.
      if (core) chooseFile();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [chooseFile, core, history, undo, redo, editsPending, diagram, save, saveAs]);

  const status = message(state, t, language);
  // The add menu is named after what it adds to: "Goal G3", or "Add" on the empty canvas.
  const menuNode = addMenu?.parent ? diagram?.diagram.nodes.find((node) => node.id === addMenu.parent) : undefined;
  const menuOutline = addMenu?.parent ? outline.find((row) => row.id === addMenu.parent) : undefined;
  const addMenuLabel = menuNode && menuOutline ? `${kindName(menuNode.kind, t)} ${menuOutline.identifier}` : t('addMenu');
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
        onSave={save}
        onSaveAs={core?.saveAs ? saveAs : null}
        history={hasFile ? history : null}
        onUndo={undo}
        onRedo={redo}
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
          doc.openFile(event.target.files?.[0]);
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
            onExample={doc.openExample}
            recent={recent}
            onRecent={doc.openRecent}
            onClearRecent={doc.clearRecent}
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
            centreHeader={
              // Beside the zoom controls, not over the diagram: a prompt on
              // the canvas would cover the places to choose from.
              <div className="af-centre-header">
                <ZoomBar t={t} scale={viewport?.scale ?? null} onZoomBy={zoomBy} onFit={fitAll} step={ZOOM_STEP} />
                {moving && !moving.dragged && (
                  <div className="af-move-banner" role="status">
                    <span className="af-move-prompt">{t('movePrompt', { kind: kindName(moving.kind, t), identifier: moving.identifier })}</span>
                    {moving.targets.canvas === null && (
                      <Button onClick={() => moveTo(null)}>{t('moveToTop', { identifier: moving.identifier })}</Button>
                    )}
                    <Button onClick={() => setMoving(null)}>{t('moveCancel')}</Button>
                  </div>
                )}
              </div>
            }
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
                        onSelect={selectAndShow}
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
              views: [{ id: 'elementProperties', label: t('elementProperties'), content: <ElementProperties details={details} t={t} onText={setText} onUndeveloped={setUndeveloped} /> }],
            }}
            bottom={{
              name: problemsTitle,
              nameInText: t('problemsInText'),
              min: 7,
              icons: { hide: <ChevronDown />, show: <ChevronUp /> },
              views: [
                {
                  id: 'problems',
                  label: problemsTitle,
                  content: (
                    <ProblemsPanel
                      problems={doc.problems}
                      language={language}
                      t={t}
                      onShow={selectAndShow}
                      onRepair={(repair, revision, problem) => {
                        const element = problem.elements.find((candidate) => candidate.id === repair.element);
                        repairWith(repair, revision, { kind: element?.kind ?? null, identifier: element?.identifier ?? repair.element }, followEdit);
                      }}
                    />
                  ),
                },
              ],
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
              onContextMenu={openAddMenu}
              onRemove={askRemove}
              move={moveMarks}
              onMoveTo={moveTo}
              onMoveCancel={() => setMoving(null)}
              onDragStart={(id) => startMove(id, true)}
              onDragEnd={endDrag}
            />
          </Workspace>
        )}
        {hasFile && (
          // Beside the canvas, not in it: the menu's keys must not reach the canvas.
          <ContextMenu
            label={addMenuLabel}
            open={addMenu !== null}
            position={addMenu?.at ?? null}
            openedBy={addMenu?.source}
            onOpenChange={(open) => {
              if (!open) {
                // A menu still being asked for does not open after this.
                menuRequest.current += 1;
                setAddMenu(null);
              }
            }}
            onAction={(id) => chooseMenu(String(id))}
          >
            {addMenu && addItems(addMenu.choices, t).map((item) => <MenuItem key={item.kind} id={item.kind}>{item.label}</MenuItem>)}
            {addMenu?.parent && !moving && addItems(addMenu.choices, t).length > 0 && <MenuSeparator />}
            {addMenu?.parent && !moving && addMenu.undeveloped && (
              <MenuItem id={UNDEVELOPED}>{t(addMenu.undeveloped.marked ? 'clearUndeveloped' : 'markUndeveloped')}</MenuItem>
            )}
            {addMenu?.parent && !moving && <MenuItem id={MOVE}>{t('move')}</MenuItem>}
            {addMenu?.parent && !moving && (
              <MenuItem id={REMOVE} danger shortcut={t('removeKey')}>
                {t('remove')}
              </MenuItem>
            )}
            {addMenu && moving && (addMenu.parent ? moveMarks?.allowed.has(addMenu.parent) : moving.targets.canvas === null) && (
              <MenuItem id={MOVE_HERE}>
                {t(addMenu.parent ? 'moveHere' : 'moveToTop', { identifier: moving.identifier })}
              </MenuItem>
            )}
            {addMenu && moving && <MenuItem id={MOVE_CANCEL}>{t('moveCancel')}</MenuItem>}
          </ContextMenu>
        )}
        {hasFile && (
          <UnsavedDialog
            name={fileName ?? ''}
            open={unsaved !== null}
            t={t}
            onCancel={() => setUnsaved(null)}
            onDiscard={() => {
              const then = unsaved;
              setUnsaved(null);
              then?.();
            }}
            onSave={() => {
              const then = unsaved;
              setUnsaved(null);
              void saveWith(false).then((saved) => {
                if (saved) then?.();
              });
            }}
          />
        )}
        {hasFile && (
          <RecoveryDialog
            offered={doc.recovery}
            time={doc.recovery ? new Date(doc.recovery.writtenAt).toLocaleString(language) : ''}
            asksWhere={core?.saveAs != null}
            t={t}
            onRestore={() => doc.restoreRecovery(followEdit)}
            onDiscard={doc.discardRecovery}
          />
        )}
        {hasFile && (
          <RemoveDialog
            removal={confirmRemoval}
            t={t}
            onCancel={() => setConfirmRemoval(null)}
            onRemove={(answer) => {
              setConfirmRemoval(null);
              removeWith(answer, followEdit);
            }}
          />
        )}
      </div>
      <StatusBar>
        <Status tone={status.tone} data-testid="status">
          {status.text}
        </Status>
        <StatusBarSpacer />
        {hasFile && doc.problems && (
          // The counts open the Problems panel (P8).
          <Status
            tone={doc.problems.counts.errors > 0 ? 'error' : doc.problems.counts.warnings > 0 ? 'warning' : undefined}
            data-testid="problem-counts"
          >
            <button
              type="button"
              className="af-status-button"
              title={t('problemsShow')}
              onClick={() => workspace.current?.showView('problems')}
            >
              {t('findings', { errors: doc.problems.counts.errors, warnings: doc.problems.counts.warnings })}
            </button>
          </Status>
        )}
        {modified && (
          <Status title={t('modifiedTip')} data-testid="modified">
            {t('modified')}
          </Status>
        )}
        {viewport && diagram && <Status data-testid="zoom">{t('zoom', { percent: Math.round(viewport.scale * 100) })}</Status>}
      </StatusBar>
    </DatumRoot>
  );
}
