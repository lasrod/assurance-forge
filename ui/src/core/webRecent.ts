// The browser's recent files. A page cannot read a file again by its path, so
// it keeps a copy of each file as it was when opened, in this browser's
// IndexedDB: on this machine, never sent anywhere. Without IndexedDB (a
// private window, blocked site data) there are no recent files, and the page
// works the same.

import type { OpenedFile, OpenResult, Recent, RecentFile } from './Core';

const DATABASE = 'assurance-forge';
/** What the list shows of each file, without its bytes: listing reads only this. */
const LISTED = 'recent';
/** Each file's bytes, under the same id. */
const BYTES = 'recent-bytes';
const VERSION = 2;
/** How many files the list keeps, as the desktop application's. */
export const RECENT_LIMIT = 10;
/** A larger file is opened but not kept. */
export const RECENT_MAX_BYTES = 32 * 1024 * 1024;

interface Kept {
  id: string;
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
    const opening = indexedDB.open(DATABASE, VERSION);
    opening.onupgradeneeded = () => {
      const db = opening.result;
      // Version 1 kept the bytes with the list. Recent files are a
      // convenience: they are started again rather than moved.
      for (const name of Array.from(db.objectStoreNames)) db.deleteObjectStore(name);
      db.createObjectStore(LISTED, { keyPath: 'id' });
      db.createObjectStore(BYTES, { keyPath: 'id' });
    };
    opening.onsuccess = () => resolve(opening.result);
    opening.onerror = () => reject(opening.error ?? new Error('indexeddb'));
    opening.onblocked = () => reject(new Error('indexeddb-blocked'));
  });
}

async function stores<T>(mode: IDBTransactionMode, work: (listed: IDBObjectStore, bytes: IDBObjectStore) => Promise<T>): Promise<T> {
  const db = await database();
  try {
    const transaction = db.transaction([LISTED, BYTES], mode);
    return await work(transaction.objectStore(LISTED), transaction.objectStore(BYTES));
  } finally {
    db.close();
  }
}

/** The browser's recent files, opening a kept copy with `open`. */
export function createWebRecent(open: (bytes: Uint8Array) => Promise<OpenResult>): Recent {
  const all = () => stores('readonly', (listed) => request(listed.getAll() as IDBRequest<RecentFile[]>));
  // Writes run one after another, and the list waits for those started
  // before it: a file is listed once it opened, though its copy is written
  // after it is shown.
  let writing: Promise<void> = Promise.resolve();
  const write = async (name: string, bytes: Uint8Array, result: OpenResult) => {
    if (result.result !== 'opened' || bytes.length > RECENT_MAX_BYTES) return;
    try {
      // A file is known by its name: the page is told nothing else about where it is.
      const file: RecentFile = { id: name, name, location: null, openedAt: Date.now(), nodes: result.nodes, edges: result.edges };
      const { drop } = withRecent(await all(), file);
      await stores('readwrite', async (listed, kept) => {
        await request(listed.put(file));
        await request(kept.put({ id: name, bytes } satisfies Kept));
        for (const old of drop) {
          await request(listed.delete(old.id));
          await request(kept.delete(old.id));
        }
      });
    } catch {
      // Recent files are a convenience.
    }
  };
  const remember = (name: string, bytes: Uint8Array, result: OpenResult) => {
    writing = writing.then(() => write(name, bytes, result));
    return writing;
  };
  return {
    keepsCopies: true,
    list: async () => {
      await writing;
      try {
        return (await all()).sort((a, b) => b.openedAt - a.openedAt);
      } catch {
        return [];
      }
    },
    open: async (id, wanted): Promise<OpenedFile | null> => {
      let file: RecentFile | undefined;
      let kept: Kept | undefined;
      try {
        [file, kept] = await stores('readonly', (listed, bytes) =>
          Promise.all([request(listed.get(id) as IDBRequest<RecentFile | undefined>), request(bytes.get(id) as IDBRequest<Kept | undefined>)]),
        );
      } catch {
        return null;
      }
      // A later request started while the copy was read: it must not reach the core.
      if (!wanted()) return null;
      if (!file || !kept) return null;
      const result = await open(kept.bytes);
      void remember(file.name, kept.bytes, result);
      return { name: file.name, result };
    },
    clear: async () => {
      await writing;
      try {
        await stores('readwrite', async (listed, bytes) => {
          await request(listed.clear());
          await request(bytes.clear());
        });
      } catch {
        // Nothing was kept.
      }
    },
    remember,
  };
}
