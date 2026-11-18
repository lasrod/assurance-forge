import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { createWasmCore } from './core/wasmCore';
import { measuredFontUrls } from './design';
import { initialLanguage, type Language } from './i18n/messages';
import './app.css';

async function bytes(url: string): Promise<Uint8Array> {
  const response = await fetch(url);
  if (!response.ok) throw new Error(`${url}: ${response.status}`);
  return new Uint8Array(await response.arrayBuffer());
}

/** Loads the measured faces once: the core measures with the bytes, the canvas draws with the same files. */
async function start(language: Language) {
  const [sansRegular, sansSemiBold, japaneseRegular, japaneseSemiBold] = await Promise.all([
    bytes(measuredFontUrls.sansRegular),
    bytes(measuredFontUrls.sansSemiBold),
    bytes(measuredFontUrls.japaneseRegular),
    bytes(measuredFontUrls.japaneseSemiBold),
  ]);
  // The canvas draws with the very bytes the core measures with: each face is
  // added from them, under the families DATUM's measured.css declares, so the
  // files are downloaded once. Each is loaded by its own family; given the
  // whole list, WebKit loaded only the first family's face.
  const faces = [
    new FontFace('DATUM Measured Sans', sansRegular, { weight: '400', display: 'block' }),
    new FontFace('DATUM Measured Sans', sansSemiBold, { weight: '600', display: 'block' }),
    new FontFace('DATUM Measured Sans JP', japaneseRegular, { weight: '400', display: 'block' }),
    new FontFace('DATUM Measured Sans JP', japaneseSemiBold, { weight: '600', display: 'block' }),
  ];
  for (const face of await Promise.all(faces.map((face) => face.load()))) document.fonts.add(face);
  return createWasmCore({ sansRegular, sansSemiBold, japaneseRegular, japaneseSemiBold }, language);
}

const root = document.getElementById('root');
if (root) {
  createRoot(root).render(
    <StrictMode>
      <App start={start} initialLanguage={initialLanguage(location.search, navigator.languages)} />
    </StrictMode>,
  );
}
