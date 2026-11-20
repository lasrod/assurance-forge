import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { StartError } from './core/start';
import { createTauriCore, inDesktop } from './core/tauriCore';
import { measuredFontUrls } from './design';
import { initialLanguage, type Language } from './i18n/messages';
import './app.css';

async function bytes(url: string): Promise<Uint8Array> {
  const response = await fetch(url).catch(() => null);
  if (!response?.ok) throw new StartError('font-missing', url);
  return new Uint8Array(await response.arrayBuffer());
}

/** Loads the measured faces once: the core measures with the bytes, the canvas draws with the same files. */
async function start(language: Language) {
  const [sansRegular, sansSemiBold, japaneseRegular, japaneseSemiBold, monoRegular] = await Promise.all([
    bytes(measuredFontUrls.sansRegular),
    bytes(measuredFontUrls.sansSemiBold),
    bytes(measuredFontUrls.japaneseRegular),
    bytes(measuredFontUrls.japaneseSemiBold),
    bytes(measuredFontUrls.monoRegular),
  ]);
  // The canvas draws with the very bytes the core measures with: each face is
  // added from them, under the families DATUM's measured.css declares, so the
  // files are downloaded once. Each is loaded by its own family; given the
  // whole list, WebKit loaded only the first family's face.
  const faces: Array<[string, FontFace]> = [
    ['SansRegular', new FontFace('DATUM Measured Sans', sansRegular, { weight: '400', display: 'block' })],
    ['SansSemiBold', new FontFace('DATUM Measured Sans', sansSemiBold, { weight: '600', display: 'block' })],
    ['JapaneseRegular', new FontFace('DATUM Measured Sans JP', japaneseRegular, { weight: '400', display: 'block' })],
    ['JapaneseSemiBold', new FontFace('DATUM Measured Sans JP', japaneseSemiBold, { weight: '600', display: 'block' })],
    ['MonoRegular', new FontFace('DATUM Measured Mono', monoRegular, { weight: '400', display: 'block' })],
  ];
  // A file the browser cannot read as a font is named, as af-wasm names one.
  const loaded = await Promise.all(
    faces.map(([name, face]) =>
      face.load().catch(() => {
        throw new StartError('font-unreadable', name);
      }),
    ),
  );
  for (const face of loaded) document.fonts.add(face);
  const fonts = { sansRegular, sansSemiBold, japaneseRegular, japaneseSemiBold, monoRegular };
  // The desktop application's core is Rust in its shell; the browser's is
  // WebAssembly, loaded only there.
  if (inDesktop()) return createTauriCore(fonts, language);
  const { createWasmCore } = await import('./core/wasmCore');
  return createWasmCore(fonts, language);
}

const root = document.getElementById('root');
if (root) {
  createRoot(root).render(
    <StrictMode>
      <App start={start} initialLanguage={initialLanguage(location.search, navigator.languages)} />
    </StrictMode>,
  );
}
