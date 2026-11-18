// What the browser tests read about the page. Only a build made with
// `--mode e2e` has it: in any other build the condition is false at build time
// and the code is removed.

export interface TestState {
  revision: number | null;
  drawn: { nodes: number; edges: number };
}

declare global {
  interface Window {
    __assuranceForge?: TestState;
  }
}

export function report(state: TestState) {
  if (import.meta.env.MODE === 'e2e') window.__assuranceForge = state;
}
