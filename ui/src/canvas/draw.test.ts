import { describe, expect, it } from 'vitest';
import { fit, MAX_SCALE, MIN_SCALE } from './draw';

describe('fit', () => {
  it('centres a small diagram at 100%', () => {
    const viewport = fit({ x: 0, y: 0, width: 200, height: 100 }, 1000, 800);
    expect(viewport.scale).toBe(1);
    expect(viewport.offsetX).toBe(400);
    expect(viewport.offsetY).toBe(350);
  });

  it('shrinks a large diagram to fit, keeping its aspect', () => {
    const viewport = fit({ x: 20, y: 20, width: 4000, height: 1000 }, 1000, 800);
    expect(viewport.scale).toBeCloseTo((1000 - 32) / 4000);
    // The diagram's left edge lands on the margin.
    expect(20 * viewport.scale + viewport.offsetX).toBeCloseTo(16);
  });

  it('stays within the zoom range', () => {
    expect(fit({ x: 0, y: 0, width: 1e7, height: 1e7 }, 800, 600).scale).toBe(MIN_SCALE);
    expect(fit({ x: 0, y: 0, width: 1, height: 1 }, 800, 600).scale).toBeLessThanOrEqual(MAX_SCALE);
  });
});
