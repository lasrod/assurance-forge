// The browser's core: crates/af-wasm, built by `pnpm wasm` into src/wasm/.

import init, { WasmCore } from '../wasm/af_wasm';
import type { Core, DiagramAt, ElementDetails, OpenResult } from './Core';

/** The measured faces, as whole font files, in their fallback order. */
export interface FontBytes {
  sansRegular: Uint8Array;
  sansSemiBold: Uint8Array;
  japaneseRegular: Uint8Array;
  japaneseSemiBold: Uint8Array;
}

/** Why the core did not start: a code the interface words. */
export class StartError extends Error {
  constructor(
    readonly code: 'font-unreadable' | 'font-missing' | 'core',
    readonly detail = '',
  ) {
    super(`${code}${detail ? `:${detail}` : ''}`);
  }
}

export async function createWasmCore(fonts: FontBytes, language: string): Promise<Core> {
  await init();
  let core: WasmCore;
  try {
    core = new WasmCore(fonts.sansRegular, fonts.sansSemiBold, fonts.japaneseRegular, fonts.japaneseSemiBold, language);
  } catch (error) {
    // af-wasm refuses a face as `font-unreadable:<FaceName>`.
    const match = /font-unreadable:(\w+)/.exec(String(error));
    throw match ? new StartError('font-unreadable', match[1]) : new StartError('core');
  }
  const parse = <T>(json: string | undefined): T | null => (json === undefined ? null : (JSON.parse(json) as T));
  return {
    open: async (bytes) => JSON.parse(core.open(bytes)) as OpenResult,
    // The methods below throw `core-serialize` if the core's answer cannot be
    // written, which is the core's failure, not the file's.
    diagram: async () => parse<DiagramAt>(core.diagram()),
    element: async (id) => parse<ElementDetails>(core.element(id)),
    setLanguage: async (language) => core.setLanguage(language),
    setStyle: async (style) =>
      core.setStyle(style.textSize, style.textLineHeight, style.identifierSize, style.identifierLineHeight),
    dispose: () => core.free(),
  };
}
