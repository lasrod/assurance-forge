// What starting a core takes, and how it fails, in either shell.

/** The measured faces, as whole font files, in their fallback order. */
export interface FontBytes {
  sansRegular: Uint8Array;
  sansSemiBold: Uint8Array;
  japaneseRegular: Uint8Array;
  japaneseSemiBold: Uint8Array;
  monoRegular: Uint8Array;
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
