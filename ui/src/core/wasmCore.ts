// The browser's core: crates/af-wasm, built by `pnpm wasm` into src/wasm/.

import init, { WasmCore } from '../wasm/af_wasm';
import type {
  AddChoices,
  Core,
  DiagramAt,
  EditResult,
  ElementDetails,
  History,
  OpenResult,
  Outline,
  Package,
  Problems,
  RelationshipDetails,
  MoveTargets,
  Removal,
  SaveResult,
  WriteResult,
} from './Core';
import { createWebRecent } from './webRecent';
import { createWebRecovery, indexedDbRecovery } from './webRecovery';
import { StartError, type FontBytes } from './start';

/** Hands `text` to the browser as a download named `name`: where the browser keeps downloads. */
export function download(text: string, name: string): void {
  const url = URL.createObjectURL(new Blob([text], { type: 'application/xml' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = name;
  link.hidden = true;
  document.body.append(link);
  link.click();
  link.remove();
  // The browser has the file once the click is handled.
  setTimeout(() => URL.revokeObjectURL(url), 60_000);
}

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
  const recovery = createWebRecovery(core, indexedDbRecovery);
  // Opening a file gives up the one open, after the page asked about its
  // changes: their copy goes (R3).
  const open = async (bytes: Uint8Array) => {
    await recovery.giveUp();
    return JSON.parse(core.open(bytes)) as OpenResult;
  };
  return {
    open,
    // The methods below throw `core-serialize` if the core's answer cannot be
    // written, which is the core's failure, not the file's.
    close: async () => {
      await recovery.giveUp();
      core.close();
    },
    diagram: async () => parse<DiagramAt>(core.diagram()),
    element: async (id) => parse<ElementDetails>(core.element(id)),
    relationship: async (id) => parse<RelationshipDetails>(core.relationship(id)),
    outline: async () => parse<Outline>(core.outline()),
    packages: async () => JSON.parse(core.packages()) as Package[],
    problems: async () => parse<Problems>(core.problems()),
    addChoices: async (parent) => parse<AddChoices>(core.addChoices(parent ?? undefined)),
    challengeChoices: async (at, line) => parse<AddChoices>(core.challengeChoices(at, line?.from, line?.to)),
    challenge: async (at, line, kind, revision) => JSON.parse(core.challenge(at, line?.from, line?.to, kind, revision)) as EditResult,
    add: async (parent, kind, revision) => JSON.parse(core.add(parent ?? undefined, kind, revision)) as EditResult,
    removal: async (id) => parse<Removal>(core.removal(id)),
    remove: async (id, revision) => JSON.parse(core.remove(id, revision)) as EditResult,
    moveTargets: async (id) => parse<MoveTargets>(core.moveTargets(id)),
    moveNode: async (id, to, revision) => JSON.parse(core.moveNode(id, to ?? undefined, revision)) as EditResult,
    showPackage: async (id) => core.showPackage(id ?? undefined),
    moveToLine: async (id, line, revision) => JSON.parse(core.moveToLine(id, line.relationship, line.from, line.to, revision)) as EditResult,
    setUndeveloped: async (id, undeveloped, revision) => JSON.parse(core.setUndeveloped(id, undeveloped, revision)) as EditResult,
    repairJoin: async (id, relationship, revision) => JSON.parse(core.repairJoin(id, relationship, revision)) as EditResult,
    setText: async (id, field, text) => JSON.parse(core.setText(id, field, text)) as EditResult,
    undo: async () => JSON.parse(core.undo()) as EditResult,
    redo: async () => JSON.parse(core.redo()) as EditResult,
    history: async () => JSON.parse(core.history()) as History,
    // The browser downloads what the core wrote and checked (plan decision S7).
    // It cannot tell whether the user kept the download: the page says it
    // was downloaded, not saved.
    save: async (name, revision): Promise<SaveResult> => {
      const written = JSON.parse(core.write()) as WriteResult;
      if (written.result === 'refused') return written;
      if (written.revision !== revision) return { result: 'refused', code: 'AF-SAVE-006' };
      download(written.xmi, name);
      if (!core.markSaved(written.revision)) return { result: 'refused', code: 'AF-SAVE-006' };
      // The changes are in the download: their copy goes (R3).
      await recovery.saved(name);
      return { result: 'saved', revision: written.revision, name };
    },
    saveAs: null,
    closeGuard: null,
    setLanguage: async (language) => core.setLanguage(language),
    setStyle: async (style) =>
      core.setStyle(style.textSize, style.textLineHeight, style.identifierSize, style.identifierLineHeight),
    dispose: () => core.free(),
    // A page has no command line, and chooses a file with a file input.
    initialFile: async () => null,
    openDialog: null,
    recent: createWebRecent(open),
    recovery,
  };
}
