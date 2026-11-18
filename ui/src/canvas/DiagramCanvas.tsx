import { useEffect, useRef, useState } from 'react';
import type { DiagramAt } from '../core/Core';
import { measuredFontFamily, watchCanvasStyle, type CanvasStyle } from '../design';
import { report } from '../testHook';
import { draw, fit, type Palette } from './draw';

function palette(style: CanvasStyle): Palette {
  return {
    ground: style.color('surface.canvas'),
    line: style.color('line.control'),
    text: style.color('text.primary'),
    secondary: style.color('text.secondary'),
  };
}

/** The diagram, fitted to the canvas, which fills its parent. */
export function DiagramCanvas({ diagram, label }: { diagram: DiagramAt | null; label: string }) {
  const host = useRef<HTMLDivElement>(null);
  const canvas = useRef<HTMLCanvasElement>(null);
  const [size, setSize] = useState({ width: 0, height: 0 });
  const [style, setStyle] = useState<CanvasStyle | null>(null);

  useEffect(() => {
    const element = host.current;
    if (!element) return;
    const observer = new ResizeObserver(([entry]) => {
      setSize({ width: entry.contentRect.width, height: entry.contentRect.height });
    });
    observer.observe(element);
    return () => observer.disconnect();
  }, []);

  useEffect(() => {
    const element = canvas.current;
    if (!element) return;
    const watch = watchCanvasStyle(element, (next) => setStyle(next));
    setStyle(watch.style);
    return () => watch.dispose();
  }, []);

  useEffect(() => {
    const element = canvas.current;
    const context = element?.getContext('2d');
    if (!element || !context || !style || size.width === 0 || size.height === 0) return;
    const dpr = style.dpr;
    element.width = Math.round(size.width * dpr);
    element.height = Math.round(size.height * dpr);
    if (!diagram) {
      context.setTransform(1, 0, 0, 1, 0, 0);
      context.fillStyle = style.color('surface.canvas');
      context.fillRect(0, 0, element.width, element.height);
      report({ revision: null, drawn: { nodes: 0, edges: 0 } });
      return;
    }
    const viewport = fit(diagram.diagram.bounds, size.width, size.height);
    const drawn = draw(context, diagram.diagram, viewport, palette(style), measuredFontFamily, { ...size, dpr });
    report({ revision: diagram.revision, drawn });
  }, [diagram, size, style]);

  return (
    <div ref={host} className="af-canvas-host">
      <canvas ref={canvas} className="af-canvas" role="img" aria-label={label} />
    </div>
  );
}
