// The one interface the interface talks to. The browser build serves it with
// the core compiled to WebAssembly (wasmCore.ts); the desktop application
// serves it through Tauri (tauriCore.ts). Its types are generated from the
// Rust contract in crates/af-api.

import type {
  AddChoices,
  DiagramAt,
  EditResult,
  ElementDetails,
  ElementKind,
  History,
  OpenedFile,
  OpenResult,
  Outline,
  Package,
  RelationshipDetails,
  Removal,
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
   * Sets the identifier, name or statement of the node with this SACM id.
   * `unchanged` when it was that already; refused (AF-EDIT-011 to 014) when
   * it cannot be set.
   */
  setText(id: string, field: TextField, text: string): Promise<EditResult>;
  /** Takes back the last command. */
  undo(): Promise<EditResult>;
  /** Makes again the last command undo took back. */
  redo(): Promise<EditResult>;
  /** Whether Edit ▸ Undo and Redo can be chosen. */
  history(): Promise<History>;
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
}
