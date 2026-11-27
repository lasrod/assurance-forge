// The one place the interface imports DATUM.
//
// DATUM is private and installed from tarballs packed locally
// (scripts/pack-datum.mjs). Keeping every DATUM import here means a plain
// replacement, for builds without DATUM, has one module to stand in for.
// The icons DATUM leaves to each product come from lucide-react, here too.

import './styles.css';

export {
  Button,
  Checkbox,
  ContextMenu,
  DatumRoot,
  Dialog,
  ListBox,
  MenuBar,
  MenuBarMenu,
  MenuItem,
  MenuSection,
  MenuSeparator,
  PropertyGrid,
  PropertyRow,
  QuantityField,
  Section,
  Submenu,
  Status,
  StatusBar,
  StatusBarSpacer,
  Tooltip,
  Tree,
  Workspace,
} from '@datum-ui/react';
export type {
  ContextMenuSource,
  QuantityDef,
  Theme,
  TreeNode,
  WorkspaceCopy,
  WorkspaceDockId,
  WorkspaceHandle,
  WorkspaceLayout,
  WorkspaceMeasure,
} from '@datum-ui/react';
export { watchCanvasStyle, type CanvasStyle } from '@datum-ui/charts';
export {
  ChevronDown,
  ChevronUp,
  CircleX,
  FileText,
  FolderOpen,
  Info,
  Maximize,
  Minus,
  PanelLeftClose,
  PanelLeftOpen,
  PanelRightClose,
  PanelRightOpen,
  Plus,
  TriangleAlert,
} from 'lucide-react';

import sansRegular from '@datum-ui/fonts/measured/IBMPlexSans-Regular.ttf?url';
import sansSemiBold from '@datum-ui/fonts/measured/IBMPlexSans-SemiBold.ttf?url';
import japaneseRegular from '@datum-ui/fonts/measured/IBMPlexSansJP-Regular.ttf?url';
import japaneseSemiBold from '@datum-ui/fonts/measured/IBMPlexSansJP-SemiBold.ttf?url';
import monoRegular from '@datum-ui/fonts/measured/IBMPlexMono-Regular.ttf?url';

/** The measured faces' files, which the layout reads and the canvas draws with. */
export const measuredFontUrls = { sansRegular, sansSemiBold, japaneseRegular, japaneseSemiBold, monoRegular };

/**
 * The families declared by `@datum-ui/fonts/measured.css`, in fallback order:
 * names and statements in the sans stack, identifiers in Plex Mono with the
 * sans faces after it for what it lacks (DATUM decision 0027).
 */
export const measuredFamilies = {
  text: "'DATUM Measured Sans', 'DATUM Measured Sans JP'",
  identifier: "'DATUM Measured Mono', 'DATUM Measured Sans', 'DATUM Measured Sans JP'",
};
