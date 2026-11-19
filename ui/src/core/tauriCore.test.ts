import { describe, expect, it } from 'vitest';
import { packFonts } from './tauriCore';

describe('packFonts', () => {
  it('joins the five faces in order, with their lengths', () => {
    const face = (...bytes: number[]) => new Uint8Array(bytes);
    const { body, lengths } = packFonts({
      sansRegular: face(1),
      sansSemiBold: face(2, 2),
      japaneseRegular: face(3, 3, 3),
      japaneseSemiBold: face(),
      monoRegular: face(5),
    });
    expect([...body]).toEqual([1, 2, 2, 3, 3, 3, 5]);
    expect(lengths).toBe('1,2,3,0,1');
  });
});
