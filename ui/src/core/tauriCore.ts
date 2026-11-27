// The desktop application's core: crates/af-desktop, through Tauri commands.
// The commands are the browser core's, so the interface is the same code on
// both (plan decision D1).

import { invoke, isTauri } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';
import type {
  AddChoices,
  Core,
  DiagramAt,
  EditResult,
  ElementDetails,
  FoundRecovery,
  History,
  KeepResult,
  OpenedFile,
  OpenResult,
  Outline,
  Package,
  Problems,
  RecentFile,
  RelationshipDetails,
  MoveTargets,
  Removal,
  SaveResult,
} from './Core';
import { StartError, type FontBytes } from './start';

/** Whether the page runs in the desktop application's window. */
export function inDesktop(): boolean {
  return isTauri();
}

/** The five faces as one body, in the order `Fonts::new` takes them, and their lengths. */
export function packFonts(fonts: FontBytes): { body: Uint8Array; lengths: string } {
  const faces = [fonts.sansRegular, fonts.sansSemiBold, fonts.japaneseRegular, fonts.japaneseSemiBold, fonts.monoRegular];
  const body = new Uint8Array(faces.reduce((total, face) => total + face.length, 0));
  let offset = 0;
  for (const face of faces) {
    body.set(face, offset);
    offset += face.length;
  }
  return { body, lengths: faces.map((face) => face.length).join(',') };
}

/**
 * The core in the desktop shell. The page hands it the fonts it loaded, once,
 * as one body: the same bytes the canvas draws with.
 */
export async function createTauriCore(fonts: FontBytes, language: string): Promise<Core> {
  const { body, lengths } = packFonts(fonts);
  try {
    await invoke('start', body, { headers: { 'af-font-lengths': lengths, 'af-language': language } });
  } catch (error) {
    // af-desktop refuses a face as `font-unreadable:<FaceName>`, as af-wasm does.
    const match = /font-unreadable:(\w+)/.exec(String(error));
    throw match ? new StartError('font-unreadable', match[1]) : new StartError('core');
  }
  return {
    open: (bytes) => invoke<OpenResult>('open', bytes),
    close: () => invoke('close'),
    diagram: () => invoke<DiagramAt | null>('diagram'),
    element: (id) => invoke<ElementDetails | null>('element', { id }),
    relationship: (id) => invoke<RelationshipDetails | null>('relationship', { id }),
    outline: () => invoke<Outline | null>('outline'),
    packages: () => invoke<Package[]>('packages'),
    problems: () => invoke<Problems | null>('problems'),
    addChoices: (parent) => invoke<AddChoices | null>('add_choices', { parent }),
    challengeChoices: (at, line) => invoke<AddChoices | null>('challenge_choices', { at, from: line?.from ?? null, to: line?.to ?? null }),
    challenge: (at, line, kind, revision) =>
      invoke<EditResult>('challenge', { at, from: line?.from ?? null, to: line?.to ?? null, kind, revision }),
    add: (parent, kind, revision) => invoke<EditResult>('add', { parent, kind, revision }),
    removal: (id) => invoke<Removal | null>('removal', { id }),
    remove: (id, revision) => invoke<EditResult>('remove', { id, revision }),
    moveTargets: (id) => invoke<MoveTargets | null>('move_targets', { id }),
    moveNode: (id, to, revision) => invoke<EditResult>('move_node', { id, to, revision }),
    setUndeveloped: (id, undeveloped, revision) => invoke<EditResult>('set_undeveloped', { id, undeveloped, revision }),
    repairJoin: (id, relationship, revision) => invoke<EditResult>('repair_join', { id, relationship, revision }),
    setText: (id, field, text) => invoke<EditResult>('set_text', { id, field, text }),
    undo: () => invoke<EditResult>('undo'),
    redo: () => invoke<EditResult>('redo'),
    history: () => invoke<History>('history'),
    save: (name, revision) => invoke<SaveResult>('save', { name, revision }),
    saveAs: (name, revision) => invoke<SaveResult>('save_as', { name, revision }),
    // The shell holds the window's close while changes are not saved
    // (af-desktop's CLOSE_REQUESTED), and closes it on `quit`.
    closeGuard: {
      // Rust is told the page heard, so a page that has stopped does not
      // hold the window open for good.
      listen: (ask) =>
        listen('af-close-requested', () => {
          void invoke('close_heard').catch(() => undefined);
          ask();
        }),
      quit: () => invoke('quit'),
    },
    setLanguage: (language) => invoke('set_language', { language }),
    setStyle: (style) => invoke('set_style', { style }),
    // The core lives as long as the window.
    dispose: () => undefined,
    initialFile: () => invoke<OpenedFile | null>('initial_file'),
    openDialog: () => invoke<OpenedFile | null>('open_dialog'),
    // The shell keeps the paths of the files it read itself, and reads them again.
    recent: {
      list: () => invoke<RecentFile[]>('recent_files'),
      // The shell reads the file and opens it in one command, and runs
      // commands in the order they are sent: `wanted` is asked before.
      open: async (id, wanted) => (wanted() ? invoke<OpenedFile | null>('open_recent', { id }) : null),
      clear: async () => {
        for (const file of await invoke<RecentFile[]>('recent_files')) await invoke('forget_recent', { id: file.id });
      },
      remember: async () => undefined,
      keepsCopies: false,
    },
    // The shell keeps the copy in the working folder beside the file, and
    // knows which file is open: the name is the page's, for the browser.
    recovery: {
      found: () => invoke<FoundRecovery | null>('recovery_found'),
      restore: () => invoke<EditResult>('restore_recovery'),
      discard: () => invoke('discard_recovery'),
      keep: (_name, revision) => invoke<KeepResult>('keep_recovery', { revision }),
    },
  };
}
