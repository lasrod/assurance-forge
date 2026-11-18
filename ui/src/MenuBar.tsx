// The menu bar, arranged as the C++ application's (plan decision D9): File and
// View now; Edit and Add come with editing.
//
// A stopgap until DATUM has a menu bar with submenus: it is a toolbar of menu
// buttons, and what the C++ application put in submenus (View ▸ Appearance ▸
// Theme, Language) are sections of the View menu.

import type { ThemeChoice, Language, MessageKey } from './i18n/messages';
import { Button, Menu, MenuItem, MenuSection, Toolbar } from './design';

export type PanelId = 'caseExplorer' | 'argumentNavigator' | 'elementProperties' | 'problems';

export const PANELS: readonly PanelId[] = ['caseExplorer', 'argumentNavigator', 'elementProperties', 'problems'];

interface MenuBarProps {
  t: (key: MessageKey) => string;
  canOpen: boolean;
  onOpen: () => void;
  /** The panels in view. */
  panels: readonly PanelId[];
  onPanelsChange: (panels: PanelId[]) => void;
  theme: ThemeChoice;
  onThemeChange: (theme: ThemeChoice) => void;
  language: Language;
  onLanguageChange: (language: Language) => void;
}

/** `Ctrl+O` on Windows and Linux, `⌘O` on macOS. */
export const OPEN_SHORTCUT = /Mac|iPhone|iPad/.test(navigator.platform) ? '⌘O' : 'Ctrl+O';

export function MenuBar({
  t,
  canOpen,
  onOpen,
  panels,
  onPanelsChange,
  theme,
  onThemeChange,
  language,
  onLanguageChange,
}: MenuBarProps) {
  const title = (key: MessageKey) => (
    <Button variant="ghost" data-menu={key}>
      {t(key)}
    </Button>
  );
  return (
    <Toolbar label={t('menu')} className="af-menubar">
      <Menu label={t('file')} trigger={title('file')}>
        <MenuItem id="open" shortcut={OPEN_SHORTCUT} disabled={!canOpen} onAction={onOpen}>
          {t('open')}
        </MenuItem>
      </Menu>
      <Menu label={t('view')} trigger={title('view')}>
        <MenuSection
          title={t('panels')}
          selection="multiple"
          selected={panels}
          onSelectionChange={(next) => onPanelsChange(next.filter((id): id is PanelId => PANELS.includes(id as PanelId)))}
        >
          {PANELS.map((id) => (
            <MenuItem key={id} id={id}>
              {t(id)}
            </MenuItem>
          ))}
        </MenuSection>
        <MenuSection
          title={`${t('appearance')}: ${t('theme')}`}
          selection="single"
          selected={[theme]}
          onSelectionChange={([next]) => {
            if (next === 'system' || next === 'light' || next === 'dark') onThemeChange(next);
          }}
        >
          <MenuItem id="system">{t('themeSystem')}</MenuItem>
          <MenuItem id="light">{t('themeLight')}</MenuItem>
          <MenuItem id="dark">{t('themeDark')}</MenuItem>
        </MenuSection>
        <MenuSection
          title={`${t('appearance')}: ${t('language')}`}
          selection="single"
          selected={[language]}
          onSelectionChange={([next]) => {
            if (next === 'en' || next === 'ja') onLanguageChange(next);
          }}
        >
          <MenuItem id="en">{t('english')}</MenuItem>
          <MenuItem id="ja">{t('japanese')}</MenuItem>
        </MenuSection>
      </Menu>
    </Toolbar>
  );
}
