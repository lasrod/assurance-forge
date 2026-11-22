// The desktop application's core: crates/af-desktop, through Tauri commands.
// The commands are the browser core's, so the interface is the same code on
// both (plan decision D1).

import { invoke, isTauri } from '@tauri-apps/api/core';
import type {
  AddChoices,
  Core,
  DiagramAt,
  EditResult,
  ElementDetails,
  History,
  OpenedFile,
  OpenResult,
  Outline,
  Package,
  RecentFile,
  RelationshipDetails,
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
    addChoices: (parent) => invoke<AddChoices | null>('add_choices', { parent }),
    add: (parent, kind) => invoke<EditResult>('add', { parent, kind }),
    undo: () => invoke<EditResult>('undo'),
    redo: () => invoke<EditResult>('redo'),
    history: () => invoke<History>('history'),
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
  };
}
