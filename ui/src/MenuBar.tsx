// The menu bar, arranged as the C++ application's (plan decision D9): File and
// View now; Edit and Add come with editing.
//
// A stopgap until DATUM has a menu bar with submenus: it is a toolbar of menu
// buttons, and what the C++ application put in submenus (View ▸ Appearance ▸
// Theme, Language) are sections of the View menu.
//
// Until DATUM's Workspace can stack views, the Case Explorer and the Argument
// Navigator share the start dock as tabs: both are checked while it is open,
// unchecking either closes it, and checking either opens it (on that tab at
// the wide tier: see App.tsx).

import type { ThemeChoice, Language, MessageKey } from './i18n/messages';
import { Button, Menu, MenuItem, MenuSection, Toolbar } from './design';
import { OPEN_SHORTCUT } from './shortcuts';

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
    <Button variant="ghost">
      {t(key)}
    </Button>
  );
  return (
    <Toolbar label={t('menu')}>
      <Menu label={t('file')} trigger={title('file')}>
        <MenuItem id="open" shortcut={OPEN_SHORTCUT} textValue={t('open')} disabled={!canOpen} onAction={onOpen}>
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
          title={t('appearanceTheme')}
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
          title={t('appearanceLanguage')}
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
