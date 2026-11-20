// The browser's recent files. A page cannot read a file again by its path, so
// it keeps a copy of each file as it was when opened, in this browser's
// IndexedDB: on this machine, never sent anywhere. Without IndexedDB (a
// private window, blocked site data) there are no recent files, and the page
// works the same.

import type { OpenedFile, OpenResult, Recent, RecentFile } from './Core';

const DATABASE = 'assurance-forge';
const STORE = 'recent';
/** How many files the list keeps, as the desktop application's. */
export const RECENT_LIMIT = 10;
/** A larger file is opened but not kept. */
export const RECENT_MAX_BYTES = 32 * 1024 * 1024;

interface Kept extends RecentFile {
  bytes: Uint8Array;
}

/** The list after `file` was opened: it first, once, and the latest ten. */
export function withRecent<T extends RecentFile>(files: readonly T[], file: T): { keep: T[]; drop: T[] } {
  const ordered = [file, ...files.filter((other) => other.id !== file.id)].sort((a, b) => b.openedAt - a.openedAt);
  return { keep: ordered.slice(0, RECENT_LIMIT), drop: ordered.slice(RECENT_LIMIT) };
}

function request<T>(made: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    made.onsuccess = () => resolve(made.result);
    made.onerror = () => reject(made.error ?? new Error('indexeddb'));
  });
}

function database(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const opening = indexedDB.open(DATABASE, 1);
    opening.onupgradeneeded = () => opening.result.createObjectStore(STORE, { keyPath: 'id' });
    opening.onsuccess = () => resolve(opening.result);
    opening.onerror = () => reject(opening.error ?? new Error('indexeddb'));
    opening.onblocked = () => reject(new Error('indexeddb-blocked'));
  });
}

async function store<T>(mode: IDBTransactionMode, work: (store: IDBObjectStore) => Promise<T>): Promise<T> {
  const db = await database();
  try {
    return await work(db.transaction(STORE, mode).objectStore(STORE));
  } finally {
    db.close();
  }
}

const listed = ({ id, name, location, openedAt, nodes, edges }: Kept): RecentFile => ({ id, name, location, openedAt, nodes, edges });

/** The browser's recent files, opening a kept copy with `open`. */
export function createWebRecent(open: (bytes: Uint8Array) => Promise<OpenResult>): Recent {
  const all = () => store('readonly', (kept) => request(kept.getAll() as IDBRequest<Kept[]>));
  const remember = async (name: string, bytes: Uint8Array, result: OpenResult) => {
    if (result.result !== 'opened' || bytes.length > RECENT_MAX_BYTES) return;
    try {
      // A file is known by its name: the page is told nothing else about where it is.
      const file: Kept = { id: name, name, location: null, openedAt: Date.now(), nodes: result.nodes, edges: result.edges, bytes };
      const { drop } = withRecent(await all(), file);
      await store('readwrite', async (kept) => {
        await request(kept.put(file));
        for (const old of drop) await request(kept.delete(old.id));
      });
    } catch {
      // Recent files are a convenience.
    }
  };
  return {
    keepsCopies: true,
    list: async () => {
      try {
        return (await all()).sort((a, b) => b.openedAt - a.openedAt).map(listed);
      } catch {
        return [];
      }
    },
    open: async (id): Promise<OpenedFile | null> => {
      let file: Kept | undefined;
      try {
        file = await store('readonly', (kept) => request(kept.get(id) as IDBRequest<Kept | undefined>));
      } catch {
        return null;
      }
      if (!file) return null;
      const result = await open(file.bytes);
      await remember(file.name, file.bytes, result);
      return { name: file.name, result };
    },
    clear: async () => {
      try {
        await store('readwrite', (kept) => request(kept.clear()));
      } catch {
        // Nothing was kept.
      }
    },
    remember,
  };
}
