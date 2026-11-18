// The one interface the interface talks to. The browser build serves it with
// the core compiled to WebAssembly (wasmCore.ts); the desktop application will
// serve it through Tauri (goal G9). Its types are generated from the Rust
// contract in crates/af-api.

import type { DiagramAt, ElementDetails, OpenResult } from '../../../crates/af-api/bindings/api';

export type * from '../../../crates/af-api/bindings/api';

export interface Core {
  /** Reads a SACM 2.3 XMI file. A refused file leaves nothing open. */
  open(bytes: Uint8Array): Promise<OpenResult>;
  /** The diagram of what is open, or null. */
  diagram(): Promise<DiagramAt | null>;
  /** What the details panel shows for the node with this SACM id. */
  element(id: string): Promise<ElementDetails | null>;
  /** A BCP 47 tag such as `en` or `ja`: the node text follows it. */
  setLanguage(language: string): Promise<void>;
  /** Frees what the core holds. It is not used after. */
  dispose(): void;
}
