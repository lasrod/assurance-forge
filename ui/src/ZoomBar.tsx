// The zoom controls the diagram's centre carries, as DATUM's diagram
// composition has them: the route that needs no drag or wheel (WCAG 2.5.7).

import { MAX_SCALE, MIN_SCALE } from './canvas/draw';
import { Button, Maximize, Minus, Plus, QuantityField, Tooltip, type QuantityDef } from './design';
import type { MessageKey } from './i18n/messages';

const ZOOM: QuantityDef = { unit: '%', displayDecimals: 0, min: MIN_SCALE * 100, max: MAX_SCALE * 100, step: 10 };

interface ZoomBarProps {
  t: (key: MessageKey) => string;
  /** The zoom as a factor, 1 at 100%, or null while nothing is drawn. */
  scale: number | null;
  onZoomBy: (factor: number) => void;
  onFit: () => void;
  step: number;
}

export function ZoomBar({ t, scale, onZoomBy, onFit, step }: ZoomBarProps) {
  const none = scale === null;
  return (
    <div className="af-zoom-bar">
      <Tooltip text={t('zoomOutTip')}>
        <Button variant="ghost" iconOnly icon={<Minus />} aria-label={t('zoomOut')} onClick={() => onZoomBy(1 / step)} unavailable={none} />
      </Tooltip>
      <QuantityField
        label={t('zoomField')}
        hideLabel
        def={ZOOM}
        value={none ? null : Math.round(scale * 100)}
        onCommit={(value) => {
          if (value !== null && scale !== null) onZoomBy(value / 100 / scale);
        }}
      />
      <Tooltip text={t('zoomInTip')}>
        <Button variant="ghost" iconOnly icon={<Plus />} aria-label={t('zoomIn')} onClick={() => onZoomBy(step)} unavailable={none} />
      </Tooltip>
      <Tooltip text={t('fitTip')}>
        <Button variant="ghost" iconOnly icon={<Maximize />} aria-label={t('fit')} onClick={onFit} unavailable={none} />
      </Tooltip>
    </div>
  );
}
