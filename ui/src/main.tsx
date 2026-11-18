import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { createWasmCore } from './core/wasmCore';
import { measuredFontFamily, measuredFontUrls } from './design';
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
  await Promise.all(
    ['400', '600'].flatMap((weight) => [
      document.fonts.load(`${weight} 14px ${measuredFontFamily}`, 'A'),
      document.fonts.load(`${weight} 14px ${measuredFontFamily}`, '日'),
    ]),
  );
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
