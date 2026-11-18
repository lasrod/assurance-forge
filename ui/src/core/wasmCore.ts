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

export async function createWasmCore(fonts: FontBytes, language: string): Promise<Core> {
  await init();
  const core = new WasmCore(fonts.sansRegular, fonts.sansSemiBold, fonts.japaneseRegular, fonts.japaneseSemiBold, language);
  const parse = <T>(json: string | undefined): T | null => (json === undefined ? null : (JSON.parse(json) as T));
  return {
    open: async (bytes) => JSON.parse(core.open(bytes)) as OpenResult,
    diagram: async () => parse<DiagramAt>(core.diagram()),
    element: async (id) => parse<ElementDetails>(core.element(id)),
    setLanguage: async (language) => core.setLanguage(language),
    setStyle: async (style) =>
      core.setStyle(style.textSize, style.textLineHeight, style.identifierSize, style.identifierLineHeight),
    dispose: () => core.free(),
  };
}
