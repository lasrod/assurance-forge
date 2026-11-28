// The one interface the interface talks to. The browser build serves it with
// the core compiled to WebAssembly (wasmCore.ts); the desktop application
// serves it through Tauri (tauriCore.ts). Its types are generated from the
// Rust contract in crates/af-api.

import type {
  AcpChoices,
  AcpKind,
  AddChoices,
  DiagramAt,
  EditResult,
  ElementDetails,
  ElementKind,
  FoundRecovery,
  History,
  KeepResult,
  MoveTargets,
  OpenedFile,
  OpenResult,
  Outline,
  Package,
  Problems,
  RelationshipDetails,
  Removal,
  AcpRemoval,
  SaveResult,
  TextField,
} from '../../../crates/af-api/bindings/api';

export type * from '../../../crates/af-api/bindings/api';

/** `type.control` and `type.metadata`, resolved: they follow the user's text size. */
export interface TextStyle {
  textSize: number;
  textLineHeight: number;
  identifierSize: number;
  identifierLineHeight: number;
}

/** A file opened lately, as the welcome page lists it. */
export interface RecentFile {
  /** What names it to `Recent.open`. */
  id: string;
  /** Its file name. */
  name: string;
  /** Where it is, when the shell knows: its path in the desktop application, null in the browser. */
  location: string | null;
  /** When it was last opened, in milliseconds since 1970. */
  openedAt: number;
  nodes: number;
  edges: number;
}

/**
 * The files opened lately, kept on this machine only. The desktop application
 * keeps their paths and reads them again; the browser, which cannot read a
 * file again, keeps a copy of each as it was when opened.
 */
export interface Recent {
  /** Most recent first. Empty when there are none, or the shell cannot keep any. */
  list(): Promise<RecentFile[]>;
  /**
   * Opens one, or answers null when it is no longer there: it is then off the
   * list, and what was open stays open. `wanted` is asked once the file is
   * read and before it reaches the core: when it answers false the core is
   * left as it was, and the answer is null.
   */
  open(id: string, wanted: () => boolean): Promise<OpenedFile | null>;
  /** Takes every file off the list. */
  clear(): Promise<void>;
  /**
   * A file the page read itself (a file input, a drop) has opened. The
   * browser keeps it; the desktop application, which was given no path,
   * does not. The page need not wait for it: `list` answers with it.
   */
  remember(name: string, bytes: Uint8Array, result: OpenResult): Promise<void>;
  /** Whether the list holds copies of the files (the browser), which the welcome page says. */
  readonly keepsCopies: boolean;
}

/**
 * Copies of the changes not saved, against a crash (plan decisions R1 to R8).
 * The core makes and reads each copy; the shell keeps it: the desktop
 * application in the working folder beside the file, the browser in this
 * browser's IndexedDB. A copy goes when its changes are saved, undone or
 * given up (Close, opening another file, closing the window); only an end
 * without a close leaves it.
 */
export interface Recovery {
  /**
   * The copy kept for the file just opened as `name`: one to offer, until it
   * is restored or discarded, or one that cannot be read and was set aside,
   * said once. Null when there is none. While one is offered, no copy is
   * written over it, and closing the file leaves it.
   */
  found(name: string): Promise<FoundRecovery | null>;
  /** Restores the copy found, as one command Edit ▸ Undo takes back. */
  restore(): Promise<EditResult>;
  /** Discards the copy found: the only way one is deleted. */
  discard(): Promise<void>;
  /**
   * Keeps a copy of the document at `revision`, the file `name`, while it
   * has changes that are not saved, and removes it when it has none.
   * `skipped` when the document is no longer at `revision`, or a copy found
   * is still to be decided.
   */
  keep(name: string, revision: number): Promise<KeepResult>;
}

/** A line of a relationship: the nodes it runs from and to. */
export interface LineEnds {
  from: string;
  to: string;
}

export interface Core {
  /** Reads a SACM 2.3 XMI file. A refused file leaves nothing open. */
  open(bytes: Uint8Array): Promise<OpenResult>;
  /** Closes what is open: after it nothing is. */
  close(): Promise<void>;
  /** The diagram of what is open, or null. */
  diagram(): Promise<DiagramAt | null>;
  /** What the details panel shows for the node with this SACM id. */
  element(id: string): Promise<ElementDetails | null>;
  /** What the details panel shows for the relationship with this SACM id, when the diagram draws it. */
  relationship(id: string): Promise<RelationshipDetails | null>;
  /** The argument as a tree, the one the diagram is laid out from; null when nothing is open. */
  outline(): Promise<Outline | null>;
  /** The open file's packages, depth first; empty when nothing is open. */
  packages(): Promise<Package[]>;
  /** The problem list at the revision: errors, then warnings, then information; null when nothing is open. */
  problems(): Promise<Problems | null>;
  /**
   * The add menu for the node with this SACM id, or for the empty canvas
   * when null: the six kinds, each with the code of why it cannot be added
   * there. Null when nothing is open.
   */
  addChoices(parent: string | null): Promise<AddChoices | null>;
  /**
   * Adds an element of `kind` under the node `parent`, or at the top level
   * when null, from the add menu of `revision`: refused (AF-EDIT-009) when
   * the document has changed since.
   */
  add(parent: string | null, kind: ElementKind, revision: number): Promise<EditResult>;
  /**
   * Which challengers, a Goal and a Solution, may challenge the node with
   * this SACM id, or with `line` the line between those two nodes of the
   * relationship with this id, each with the code of why it cannot. Null
   * when nothing is open.
   */
  challengeChoices(at: string, line: LineEnds | null): Promise<AddChoices | null>;
  /**
   * Challenges the node or line `at` with a new element of `kind`, from the
   * menu of `revision`: refused (AF-EDIT-009) when the document has changed
   * since. The element answered is the new challenger.
   */
  challenge(at: string, line: LineEnds | null, kind: ElementKind, revision: number): Promise<EditResult>;
  /** The assurance claim points the line `line` of `relationship` may take, and the identifier the next one takes; null when nothing is open. */
  acpChoices(relationship: string, line: LineEnds): Promise<AcpChoices | null>;
  /** Why the node `id` cannot take an ACP: `AF-EDIT-030` for a Solution or a Context, `AF-EDIT-029` otherwise. */
  acpNodeRefusal(id: string): Promise<string | null>;
  /**
   * Adds an ACP of `kind` to the line `line` of `relationship`, from the
   * menu of `revision`; a confidence argument's package is named
   * `packageName`. The confidence Claim is the element answered.
   */
  addAcp(relationship: string, line: LineEnds, kind: AcpKind, packageName: string, revision: number): Promise<EditResult>;
  /**
   * What removing the node with this SACM id would delete: the node, then
   * what goes with it; or the code of why it cannot be removed. Null when
   * nothing is open.
   */
  removal(id: string): Promise<Removal | null>;
  /**
   * Removes the node with this SACM id and what goes with it, as the
   * removal of `revision` said: refused (AF-EDIT-009) when the document has
   * changed since. `element` is the node it was drawn under, to select.
   */
  remove(id: string, revision: number): Promise<EditResult>;
  /**
   * What removing the ACP that `relationship` carries for the Claim `claim`
   * would delete: its description, or its confidence argument, which the
   * user keeps or deletes; or the code of why it cannot be removed. Null
   * when nothing is open.
   */
  acpRemoval(relationship: string, claim: string): Promise<AcpRemoval | null>;
  /**
   * Removes that ACP, with its confidence argument when `withArgument`, as
   * the removal of `revision` said: refused (AF-EDIT-009) when the document
   * has changed since.
   */
  removeAcp(relationship: string, claim: string, withArgument: boolean, revision: number): Promise<EditResult>;
  /**
   * Where the node with this SACM id may be moved: for every node of the
   * diagram, and for the top level, the code of why not, or null; or the
   * code of why it cannot be moved at all. Null when nothing is open.
   */
  moveTargets(id: string): Promise<MoveTargets | null>;
  /**
   * Moves the node with this SACM id, with what is under it, under the node
   * `to`, or to the top level when null, as the targets of `revision` said:
   * refused (AF-EDIT-009) when the document has changed since. `element` is
   * the node moved.
   */
  moveNode(id: string, to: string | null, revision: number): Promise<EditResult>;
  /** Shows the argument package `id` as the diagram, or the whole case for null. Whether it is shown. */
  showPackage(id: string | null): Promise<boolean>;
  /** Moves the challenge the node `id` makes onto the line `line` of a relationship, as the targets of `revision` said. */
  moveToLine(id: string, line: { relationship: string } & LineEnds, revision: number): Promise<EditResult>;
  /**
   * Marks the Goal with this SACM id undeveloped, or clears the mark.
   * `unchanged` when it was that already; refused when it is not a Goal
   * (AF-EDIT-021) or is supported (AF-EDIT-022).
   */
  setUndeveloped(id: string, undeveloped: boolean, revision: number): Promise<EditResult>;
  /**
   * Joins the node with this SACM id by context in place of the support
   * line `relationship` draws, as a problem of `revision` offered: refused
   * (AF-EDIT-009) when the document has changed since.
   */
  repairJoin(id: string, relationship: string, revision: number): Promise<EditResult>;
  /**
   * Sets the identifier, name or statement of the node with this SACM id.
   * `unchanged` when it was that already; refused (AF-EDIT-011 to 014) when
   * it cannot be set.
   */
  setText(id: string, field: TextField, text: string): Promise<EditResult>;
  /** Takes back the last command. */
  undo(): Promise<EditResult>;
  /** Makes again the last command undo took back. */
  redo(): Promise<EditResult>;
  /** Whether Edit ▸ Undo and Redo can be chosen, and whether the document has changes not saved. */
  history(): Promise<History>;
  /**
   * File ▸ Save of the document at `revision`, the one on screen. The
   * desktop application writes the file the document was read from or last
   * saved to, or asks where when there is none; the browser downloads it as
   * `name`. Refused with an `AF-SAVE-*` code when nothing was saved (the
   * file on disk is then as it was), and with `AF-SAVE-006` when the
   * document is no longer at `revision`.
   */
  save(name: string, revision: number): Promise<SaveResult>;
  /** File ▸ Save As…, where the shell has a save dialog; null in the browser, which downloads. */
  readonly saveAs: ((name: string, revision: number) => Promise<SaveResult>) | null;
  /**
   * The desktop window is to close while changes are not saved: the shell
   * holds it and calls `ask`, which answers with `quit` or not at all. Null
   * in the browser, where the page asks through `beforeunload`.
   */
  readonly closeGuard: { listen(ask: () => void): Promise<() => void>; quit(): Promise<void> } | null;
  /** A BCP 47 tag such as `en` or `ja`: the node text follows it. */
  setLanguage(language: string): Promise<void>;
  /** The diagram's text sizes in CSS px, from DATUM's type tokens. */
  setStyle(style: TextStyle): Promise<void>;
  /** Frees what the core holds. It is not used after. */
  dispose(): void;
  /** The file named when the application started, the first time it is asked; else null. */
  initialFile(): Promise<OpenedFile | null>;
  /**
   * The system's open dialog, where the shell has one: it opens the file
   * chosen, or answers null when the dialog is cancelled. Null in the
   * browser, which chooses a file with a file input.
   */
  readonly openDialog: (() => Promise<OpenedFile | null>) | null;
  readonly recent: Recent;
  readonly recovery: Recovery;
}
