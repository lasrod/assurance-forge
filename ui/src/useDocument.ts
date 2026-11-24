// The open document as the page sees it: starting the core, opening files in
// order, and asking the core for the views the page shows, each checked
// against what is on screen before it is shown (freshness.ts). The document
// itself is in the core; this keeps only the answers.

import { useCallback, useEffect, useRef, useState } from 'react';
import { findEdge } from './canvas/draw';
import type {
  AddChoice,
  Core,
  DiagramAt,
  EditResult,
  ElementKind,
  History,
  NodeKind,
  OpenedFile,
  OpenResult,
  OutlineRow,
  Package,
  RecentFile,
  TextField,
  TextStyle,
} from './core/Core';
import { StartError } from './core/start';
import type { Details } from './ElementProperties';
import { newerDiagram } from './freshness';
import type { Language } from './i18n/messages';
import exampleUrl from '../../testdata/sacm/sacm_example_core_argument.sacm.xmi?url';

/** The example the welcome page opens: its name, as a file would have one. */
const EXAMPLE_NAME = 'sacm_example_core_argument.sacm.xmi';

export type DocumentState =
  | { kind: 'starting' }
  | { kind: 'recentGone' }
  | { kind: 'failed'; code: string; detail: string }
  | { kind: 'empty' }
  | { kind: 'unreadable' }
  | { kind: 'relayoutFailed' }
  | { kind: 'result'; result: OpenResult }
  | { kind: 'added'; added: NodeKind; identifier: string }
  | { kind: 'textChanged'; field: TextField }
  | { kind: 'undone' }
  | { kind: 'redone' }
  | { kind: 'editRefused'; code: string; about: NodeKind | null };

/**
 * What an accepted edit gave, for the view to follow before it is painted:
 * the diagram on screen before, the new one, the node to keep in place first,
 * and the element added (null for undo and redo).
 */
export type EditShown = (before: DiagramAt | null, next: DiagramAt, anchor: string | null, added: string | null) => void;

export function useDocument(start: (language: Language) => Promise<Core>, initialLanguage: Language, language: Language) {
  const [core, setCore] = useState<Core | null>(null);
  const [state, setState] = useState<DocumentState>({ kind: 'starting' });
  const [diagram, setDiagram] = useState<DiagramAt | null>(null);
  // What the left panels show: the argument's tree and the file's packages.
  const [outline, setOutline] = useState<OutlineRow[]>([]);
  const [packages, setPackages] = useState<Package[]>([]);
  const [fileName, setFileName] = useState<string | null>(null);
  const [recent, setRecent] = useState<RecentFile[]>([]);
  // A file is being read, or the shell has not yet said whether it was started
  // with one: the welcome page waits, so it does not flash before a diagram.
  const [reading, setReading] = useState(false);
  const [asked, setAsked] = useState(false);
  // What Edit ▸ Undo and Redo can do; null while nothing is open.
  const [history, setHistory] = useState<History | null>(null);
  // Counts the files opened: what resets for a new file (the view, the
  // selection, the tree's branches) does not reset for a new revision of it.
  const [opened, setOpened] = useState(0);

  // A file given before the core is ready waits for it.
  const [pending, setPending] = useState<{ name: string; bytes: Uint8Array; request: number } | null>(null);
  // Each open is numbered, so a slower earlier read cannot replace the file
  // chosen last.
  const latest = useRef(0);
  // The revision on screen, or null. A relayout (language, text size) shows a
  // diagram only of that revision, and only a newer layout of it.
  const shown = useRef<number | null>(null);
  // A relayout answered for a revision newer than the one on screen: an edit
  // is on its way, and reads its picture again once shown, so a language or
  // text size chosen meanwhile is not lost.
  const missed = useRef(false);
  const showRelayout = useCallback((next: DiagramAt | null) => {
    if (next && shown.current !== null && next.revision > shown.current) missed.current = true;
    setDiagram((current) => newerDiagram(current, next, shown.current));
  }, []);
  const coreRef = useRef<Core | null>(null);
  coreRef.current = core;
  // The diagram on screen, for an edit to say what it replaced.
  const diagramRef = useRef(diagram);
  diagramRef.current = diagram;
  /** Nothing is open on the page: the welcome page shows. */
  const clear = useCallback(() => {
    shown.current = null;
    setDiagram(null);
    setOutline([]);
    setPackages([]);
    setFileName(null);
    setReading(false);
    setHistory(null);
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
   * of a file that opened, or its refusal. The core may have read the bytes
   * itself (a file named at start, or chosen in the desktop's open dialog).
   */
  const show = useCallback(
    async (on: Core, opening: () => Promise<OpenedFile>, request: number) => {
      try {
        const { name, result } = await opening();
        const isOpen = result.result === 'opened';
        const [next, rows, held, past] = isOpen
          ? await Promise.all([on.diagram(), on.outline(), on.packages(), on.history()])
          : [null, null, [], null];
        if (request !== latest.current) return;
        clear();
        shown.current = isOpen ? result.revision : null;
        if (isOpen) setOpened((count) => count + 1);
        setState({ kind: 'result', result });
        setDiagram(next);
        setOutline(rows?.rows ?? []);
        setPackages(held);
        setHistory(past);
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

  /**
   * The desktop's own open dialog, which knows the file's path; in the
   * browser, `fallback` (the file input).
   */
  const chooseFile = useCallback(
    (fallback: () => void) => {
      const dialog = core?.openDialog;
      if (!core || !dialog) {
        fallback();
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
    },
    [core, show, unreadable],
  );

  /**
   * Runs a command and shows what it gave: the new revision's diagram, trees
   * and history, with `onShown` told in the same update so the view follows
   * before it is painted. A refusal changes nothing and says why. `about` is
   * the kind of node an add was asked for, which a refusal names. Answers
   * whether the document changed.
   */
  const edit = useCallback(
    async (
      run: (on: Core) => Promise<EditResult>,
      what: 'add' | 'undo' | 'redo' | TextField,
      anchor: string | null,
      about: NodeKind | null,
      onShown: EditShown,
    ): Promise<boolean> => {
      const on = coreRef.current;
      if (!on || shown.current === null) return false;
      const request = latest.current;
      let result: EditResult;
      try {
        result = await run(on);
      } catch {
        if (request === latest.current) setState({ kind: 'relayoutFailed' });
        return false;
      }
      // A text set to what it was: nothing to show.
      if (result.result === 'unchanged') return false;
      const history = what === 'undo' || what === 'redo';
      if (result.result === 'refused') {
        // Edit ▸ Undo and Redo are greyed when there is nothing to do, so
        // AF-EDIT-008 comes only from keys sent before an answer: the undos
        // the user wanted were made, and the status says so already.
        const raced = history && result.code === 'AF-EDIT-008';
        if (request === latest.current && !raced) setState({ kind: 'editRefused', code: result.code, about });
        return false;
      }
      const added = result.element;
      let answers;
      try {
        answers = await Promise.all([
          on.diagram(),
          on.outline(),
          on.packages(),
          on.history(),
          added ? on.element(added) : Promise.resolve(null),
        ]);
      } catch {
        // The core made the change but its new picture could not be read. The
        // revision on screen follows the core all the same, so the next menu
        // and relayout are taken as current and bring the picture back.
        if (request === latest.current) {
          shown.current = result.revision;
          setState({ kind: 'relayoutFailed' });
        }
        return true;
      }
      const [next, rows, held, past, details] = answers;
      // Another file was opened meanwhile, or a later command is on its way.
      if (request !== latest.current || !next || next.revision !== result.revision) return true;
      shown.current = result.revision;
      onShown(diagramRef.current, next, anchor, added);
      // As every answer: shown only if no older a layout than the one shown.
      setDiagram((current) => newerDiagram(current, next, shown.current));
      if (rows && rows.revision === shown.current) setOutline(rows.rows);
      setPackages(held);
      setHistory(past);
      if (missed.current) {
        missed.current = false;
        void Promise.all([on.diagram(), on.outline()]).then(
          ([again, outlineAgain]) => {
            if (request !== latest.current) return;
            showRelayout(again);
            if (outlineAgain && outlineAgain.revision === shown.current) setOutline(outlineAgain.rows);
          },
          () => undefined,
        );
      }
      if (what === 'add') {
        if (details) setState({ kind: 'added', added: details.kind, identifier: details.identifier });
      } else if (history) setState({ kind: what === 'undo' ? 'undone' : 'redone' });
      else setState({ kind: 'textChanged', field: what });
      return true;
    },
    [showRelayout],
  );
  // Undos and redos on their way: a key held down sends no more until they
  // are answered, since Edit ▸ Undo's state is of the revision before them.
  const editing = useRef(0);
  const track = useCallback((running: Promise<unknown>) => {
    editing.current += 1;
    void running.finally(() => {
      editing.current -= 1;
    });
  }, []);
  const undo = useCallback((onShown: EditShown) => track(edit((on) => on.undo(), 'undo', null, null, onShown)), [edit, track]);
  const redo = useCallback((onShown: EditShown) => track(edit((on) => on.redo(), 'redo', null, null, onShown)), [edit, track]);
  /** Whether undos or redos are still on their way. */
  const editsPending = useCallback(() => editing.current > 0, []);
  /**
   * Adds an element of `kind` under `parent` (null: at the top level), from
   * the add menu of `revision`; the core refuses it if that is not current.
   */
  const add = useCallback(
    (parent: string | null, kind: ElementKind, revision: number, about: NodeKind | null, onShown: EditShown) =>
      void edit((on) => on.add(parent, kind, revision), 'add', parent, about, onShown),
    [edit],
  );
  /**
   * Sets the identifier, name or statement of the node `id`, of kind `about`.
   * Answers whether the document changed: false when the text was that
   * already, or the core refused it, which the status then says.
   */
  const setText = useCallback(
    (id: string, field: TextField, text: string, about: NodeKind | null, onShown: EditShown) =>
      edit((on) => on.setText(id, field, text), field, id, about, onShown),
    [edit],
  );
  /**
   * The add menu's choices for `parent` (null: the empty canvas), with the
   * revision they are of; null if the document changed before they came.
   */
  const addChoices = useCallback(async (parent: string | null): Promise<{ revision: number; choices: AddChoice[] } | null> => {
    const on = coreRef.current;
    const revision = shown.current;
    if (!on || revision === null) return null;
    try {
      const answer = await on.addChoices(parent);
      if (!answer || answer.revision !== revision || shown.current !== revision) return null;
      return { revision, choices: answer.choices };
    } catch {
      return null;
    }
  }, []);

  // The diagram's text follows DATUM's type tokens, so the user's text size.
  const sentStyle = useRef('');
  // Each change of text sizes is numbered: only the last one's answer says
  // what the core holds, whatever order the answers come in.
  const styleRequest = useRef(0);
  /**
   * Lays the diagram out again for other text sizes. Answers whether a new
   * layout is shown. A theme or pixel-ratio change gives the same sizes:
   * nothing to lay out.
   */
  const setTextStyle = useCallback(
    async (style: TextStyle): Promise<boolean> => {
      const key = JSON.stringify(style);
      if (!core || key === sentStyle.current) return false;
      const mine = ++styleRequest.current;
      try {
        await core.setStyle(style);
        const next = await core.diagram();
        // Kept only once the core has it, so a failure is tried again.
        if (mine === styleRequest.current) sentStyle.current = key;
        showRelayout(next);
        return true;
      } catch {
        setState({ kind: 'relayoutFailed' });
        return false;
      }
    },
    [core, showRelayout],
  );

  return {
    core,
    state,
    diagram,
    outline,
    packages,
    fileName,
    recent,
    reading,
    asked,
    history,
    opened,
    openFile,
    openExample,
    openRecent,
    clearRecent,
    closeFile,
    chooseFile,
    setTextStyle,
    addChoices,
    add,
    setText,
    undo,
    redo,
    editsPending,
  };
}

/**
 * What Element Properties shows: the core's details of the selected node or
 * relationship, read again when the diagram is (its text follows the
 * language). An answer for a selection that has since changed is dropped.
 */
export function useDetails(core: Core | null, selected: string | null, diagram: DiagramAt | null) {
  const [details, setDetails] = useState<Details | null>(null);
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
  return details;
}
