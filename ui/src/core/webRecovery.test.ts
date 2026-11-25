import { describe, expect, it } from 'vitest';
import { createWebRecovery, type RecoveryCore, type RecoveryStore } from './webRecovery';

/** A store in a map, with what was set aside. */
function memory() {
  const copies = new Map<string, string>();
  const aside: { name: string; copy: string; at: number }[] = [];
  const store: RecoveryStore = {
    get: async (name) => copies.get(name),
    put: async (name, copy) => void copies.set(name, copy),
    delete: async (name) => void copies.delete(name),
    setAside: async (name, copy, at) => {
      aside.push({ name, copy, at });
      copies.delete(name);
    },
  };
  return { copies, aside, store };
}

/** A core whose document is at `revision`, modified or not, that reads copies starting `ok`. */
function fakeCore(state: { revision: number; modified: boolean }) {
  const restored: string[] = [];
  const decode = (bytes: Uint8Array) => new TextDecoder().decode(bytes);
  const core: RecoveryCore = {
    recoveryCopy: (name) =>
      JSON.stringify(
        state.modified
          ? { result: 'keep', revision: state.revision, copy: `ok copy of ${name} at ${state.revision}` }
          : { result: 'remove', revision: state.revision },
      ),
    readRecovery: (bytes) =>
      JSON.stringify(
        decode(bytes).startsWith('ok')
          ? { result: 'found', name: 'a.sacm', writtenAt: 1, baseMatches: true }
          : { result: 'unreadable', code: 'AF-RECOVER-002' },
      ),
    restore: (bytes) => {
      restored.push(decode(bytes));
      return JSON.stringify({ result: 'applied', revision: state.revision + 1, element: null, created: [], changed: [], deleted: [] });
    },
  };
  return { core, restored };
}

describe('the browser recovery copies', () => {
  it('keeps a copy while changes are not saved and removes it when there are none', async () => {
    const { copies, store } = memory();
    const state = { revision: 3, modified: true };
    const recovery = createWebRecovery(fakeCore(state).core, store, () => 7);
    expect(await recovery.keep('a.sacm', 3)).toEqual({ result: 'kept' });
    expect(copies.get('a.sacm')).toBe('ok copy of a.sacm at 3');
    // Asked for a revision that is no longer current: nothing is done.
    expect(await recovery.keep('a.sacm', 2)).toEqual({ result: 'skipped' });
    state.modified = false;
    expect(await recovery.keep('a.sacm', 3)).toEqual({ result: 'removed' });
    expect(copies.has('a.sacm')).toBe(false);
  });

  it('offers a copy found, writes nothing over it, and restores it once', async () => {
    const { copies, store } = memory();
    copies.set('a.sacm', 'ok crashed');
    const state = { revision: 1, modified: false };
    const { core, restored } = fakeCore(state);
    const recovery = createWebRecovery(core, store);
    expect(await recovery.found('a.sacm')).toMatchObject({ result: 'found', baseMatches: true });
    expect(await recovery.keep('a.sacm', 1)).toEqual({ result: 'skipped' });
    expect(copies.get('a.sacm')).toBe('ok crashed');
    expect(await recovery.restore()).toMatchObject({ result: 'applied' });
    expect(restored).toEqual(['ok crashed']);
    expect(await recovery.restore()).toEqual({ result: 'refused', code: 'AF-RECOVER-001' });
    // The copy is the session's now.
    state.revision = 2;
    state.modified = true;
    expect(await recovery.keep('a.sacm', 2)).toEqual({ result: 'kept' });
  });

  it('leaves a copy found when the file is given up before deciding, and discards it when asked', async () => {
    const { copies, store } = memory();
    copies.set('a.sacm', 'ok crashed');
    const recovery = createWebRecovery(fakeCore({ revision: 1, modified: false }).core, store);
    await recovery.found('a.sacm');
    await recovery.giveUp();
    expect(copies.get('a.sacm')).toBe('ok crashed');
    await recovery.found('a.sacm');
    await recovery.discard();
    expect(copies.has('a.sacm')).toBe(false);
  });

  it('removes the copy of changes given up or saved', async () => {
    const { copies, store } = memory();
    const recovery = createWebRecovery(fakeCore({ revision: 4, modified: true }).core, store);
    await recovery.keep('a.sacm', 4);
    await recovery.giveUp();
    expect(copies.has('a.sacm')).toBe(false);
    await recovery.keep('a.sacm', 4);
    await recovery.saved('a.sacm');
    expect(copies.has('a.sacm')).toBe(false);
  });

  it('sets aside a copy that cannot be read and says so', async () => {
    const { copies, aside, store } = memory();
    copies.set('a.sacm', 'from a later version');
    const recovery = createWebRecovery(fakeCore({ revision: 1, modified: false }).core, store, () => 9);
    expect(await recovery.found('a.sacm')).toEqual({ result: 'unreadable', code: 'AF-RECOVER-002' });
    expect(copies.has('a.sacm')).toBe(false);
    expect(aside).toEqual([{ name: 'a.sacm', copy: 'from a later version', at: 9 }]);
    expect(await recovery.found('a.sacm')).toBeNull();
  });

  it('says a copy that cannot be written is not kept', async () => {
    const { store } = memory();
    store.put = async () => Promise.reject(new Error('quota'));
    const recovery = createWebRecovery(fakeCore({ revision: 1, modified: true }).core, store);
    expect(await recovery.keep('a.sacm', 1)).toEqual({ result: 'refused', code: 'AF-RECOVER-004' });
  });
});
