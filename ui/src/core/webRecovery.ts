// The browser's recovery copies (plan decisions R1 to R8). The core makes and
// reads each copy; this keeps it in this browser's IndexedDB, by the file's
// name, as recent files are kept: on this machine, never sent anywhere. A
// copy that cannot be read is set aside under another key (R7). Without
// IndexedDB (a private window, blocked site data) no copy is kept, and the
// status says so once (R8).

import type { EditResult, FoundRecovery, KeepResult, RecoveryCopy, Recovery } from './Core';

/** What the core is asked: af-wasm's methods, answering the contract's JSON. */
export interface RecoveryCore {
  recoveryCopy(name: string, writtenAt: number): string;
  readRecovery(copy: Uint8Array): string;
  restore(copy: Uint8Array): string;
}

/** Where the copies are kept: IndexedDB in the page, a map in the tests. */
export interface RecoveryStore {
  get(name: string): Promise<string | undefined>;
  put(name: string, copy: string): Promise<void>;
  delete(name: string): Promise<void>;
  /** Keeps `copy` where no copy is looked for, and removes it from `name`. */
  setAside(name: string, copy: string, at: number): Promise<void>;
}

/** The recovery the page uses, and what the browser core tells it of opens, closes and saves. */
export interface WebRecovery extends Recovery {
  /** The open file is given up by the user's choice (Close, opening another): its copy goes, unless it is one found and not yet decided on. */
  giveUp(): Promise<void>;
  /** The case is saved as `name`: its copy goes (R3). */
  saved(name: string): Promise<void>;
}

const NOT_WRITTEN = 'AF-RECOVER-004';

export function createWebRecovery(core: RecoveryCore, store: RecoveryStore, now: () => number = Date.now): WebRecovery {
  // The copy found for the file just opened, while it is to be decided on.
  let found: { name: string; copy: string } | null = null;
  // The name of the file open, whose copy is the session's.
  let current: string | null = null;
  // Writes run one after another, in the order asked.
  let writing: Promise<unknown> = Promise.resolve();
  const queue = <T>(work: () => Promise<T>): Promise<T> => {
    const next = writing.then(work, work);
    writing = next.catch(() => undefined);
    return next;
  };
  const encode = (text: string) => new TextEncoder().encode(text);
  const remove = (name: string) => queue(() => store.delete(name)).catch(() => undefined);

  return {
    found: async (name) => {
      found = null;
      current = name;
      let copy: string | undefined;
      try {
        copy = await queue(() => store.get(name));
      } catch {
        return null;
      }
      if (copy === undefined) return null;
      const said = JSON.parse(core.readRecovery(encode(copy))) as FoundRecovery;
      if (said.result === 'found') {
        found = { name, copy };
      } else {
        const kept = copy;
        await queue(() => store.setAside(name, kept, now())).catch(() => undefined);
      }
      return said;
    },
    restore: async (): Promise<EditResult> => {
      if (!found) return { result: 'refused', code: 'AF-RECOVER-001' };
      const result = JSON.parse(core.restore(encode(found.copy))) as EditResult;
      // The copy is the session's now: the next one is written over it.
      if (result.result !== 'refused') found = null;
      return result;
    },
    discard: async () => {
      if (!found) return;
      const { name } = found;
      found = null;
      await remove(name);
    },
    keep: async (name, revision): Promise<KeepResult> => {
      if (found) return { result: 'skipped' };
      const answer = JSON.parse(core.recoveryCopy(name, now())) as RecoveryCopy;
      if (answer.result === 'refused') return answer;
      if (answer.revision !== revision) return { result: 'skipped' };
      current = name;
      try {
        if (answer.result === 'remove') {
          await queue(() => store.delete(name));
          return { result: 'removed' };
        }
        const { copy } = answer;
        await queue(() => store.put(name, copy));
        return { result: 'kept' };
      } catch {
        return { result: 'refused', code: NOT_WRITTEN };
      }
    },
    giveUp: async () => {
      const name = current;
      const undecided = found !== null;
      found = null;
      current = null;
      if (name !== null && !undecided) await remove(name);
    },
    saved: async (name) => {
      if (found) return;
      const before = current;
      current = name;
      await remove(name);
      if (before !== null && before !== name) await remove(before);
    },
  };
}

const DATABASE = 'assurance-forge-recovery';
const COPIES = 'copies';
const ASIDE = 'set-aside';
const VERSION = 1;

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
      if (!db.objectStoreNames.contains(COPIES)) db.createObjectStore(COPIES, { keyPath: 'name' });
      if (!db.objectStoreNames.contains(ASIDE)) db.createObjectStore(ASIDE, { autoIncrement: true });
    };
    opening.onsuccess = () => resolve(opening.result);
    opening.onerror = () => reject(opening.error ?? new Error('indexeddb'));
    opening.onblocked = () => reject(new Error('indexeddb-blocked'));
  });
}

async function stores<T>(mode: IDBTransactionMode, work: (copies: IDBObjectStore, aside: IDBObjectStore) => Promise<T>): Promise<T> {
  const db = await database();
  try {
    const transaction = db.transaction([COPIES, ASIDE], mode);
    const done = new Promise<void>((resolve, reject) => {
      transaction.oncomplete = () => resolve();
      transaction.onerror = () => reject(transaction.error ?? new Error('indexeddb'));
      transaction.onabort = () => reject(transaction.error ?? new Error('indexeddb-abort'));
    });
    const result = await work(transaction.objectStore(COPIES), transaction.objectStore(ASIDE));
    // A write is kept once its transaction has committed.
    await done;
    return result;
  } finally {
    db.close();
  }
}

interface Kept {
  name: string;
  copy: string;
}

/** The copies in this browser's IndexedDB, in a database of their own. */
export const indexedDbRecovery: RecoveryStore = {
  get: (name) => stores('readonly', async (copies) => (await request(copies.get(name) as IDBRequest<Kept | undefined>))?.copy),
  put: (name, copy) =>
    stores('readwrite', async (copies) => {
      await request(copies.put({ name, copy } satisfies Kept));
    }),
  delete: (name) =>
    stores('readwrite', async (copies) => {
      await request(copies.delete(name));
    }),
  setAside: (name, copy, at) =>
    stores('readwrite', async (copies, aside) => {
      await request(aside.add({ name, copy, setAsideAt: at }));
      await request(copies.delete(name));
    }),
};
