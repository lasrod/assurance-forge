// The browser's core: crates/af-wasm, built by `pnpm wasm` into src/wasm/.

import init, { WasmCore } from '../wasm/af_wasm';
import type { Core, DiagramAt, ElementDetails, OpenResult, Outline, Package } from './Core';
import { createWebRecent } from './webRecent';
import { StartError, type FontBytes } from './start';

export async function createWasmCore(fonts: FontBytes, language: string): Promise<Core> {
  await init();
  let core: WasmCore;
  try {
    core = new WasmCore(fonts.sansRegular, fonts.sansSemiBold, fonts.japaneseRegular, fonts.japaneseSemiBold, fonts.monoRegular, language);
  } catch (error) {
    // af-wasm refuses a face as `font-unreadable:<FaceName>`.
    const match = /font-unreadable:(\w+)/.exec(String(error));
    throw match ? new StartError('font-unreadable', match[1]) : new StartError('core');
  }
  const parse = <T>(json: string | undefined): T | null => (json === undefined ? null : (JSON.parse(json) as T));
  const open = async (bytes: Uint8Array) => JSON.parse(core.open(bytes)) as OpenResult;
  return {
    open,
    // The methods below throw `core-serialize` if the core's answer cannot be
    // written, which is the core's failure, not the file's.
    close: async () => core.close(),
    diagram: async () => parse<DiagramAt>(core.diagram()),
    element: async (id) => parse<ElementDetails>(core.element(id)),
    outline: async () => parse<Outline>(core.outline()),
    packages: async () => JSON.parse(core.packages()) as Package[],
    setLanguage: async (language) => core.setLanguage(language),
    setStyle: async (style) =>
      core.setStyle(style.textSize, style.textLineHeight, style.identifierSize, style.identifierLineHeight),
    dispose: () => core.free(),
    // A page has no command line, and chooses a file with a file input.
    initialFile: async () => null,
    openDialog: null,
    recent: createWebRecent(open),
  };
}
