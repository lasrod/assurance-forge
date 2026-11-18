// The menu bar, arranged as the C++ application's (plan decision D9): File and
// View now; Edit and Add come with editing. View ▸ Appearance holds Theme and
// Language, as in the C++ application.

import type { ThemeChoice, Language, MessageKey } from './i18n/messages';
import { MenuBar as DatumMenuBar, MenuBarMenu, MenuItem, MenuSection, MenuSeparator, Submenu } from './design';
import { openShortcut } from './shortcuts';

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
  return (
    <DatumMenuBar label={t('menu')}>
      <MenuBarMenu id="file" label={t('file')}>
        <MenuItem
          id="open"
          shortcut={openShortcut()}
          textValue={t('open')}
          unavailable={!canOpen}
          note={canOpen ? undefined : t('starting')}
          onAction={onOpen}
        >
          {t('open')}
        </MenuItem>
      </MenuBarMenu>
      <MenuBarMenu id="view" label={t('view')}>
        <MenuSection
          label={t('panels')}
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
        <MenuSeparator />
        <Submenu id="appearance" label={t('appearance')}>
          <Submenu id="theme" label={t('theme')}>
            <MenuSection
              label={t('theme')}
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
          </Submenu>
          <Submenu id="language" label={t('language')}>
            <MenuSection
              label={t('language')}
              selection="single"
              selected={[language]}
              onSelectionChange={([next]) => {
                if (next === 'en' || next === 'ja') onLanguageChange(next);
              }}
            >
              <MenuItem id="en">{t('english')}</MenuItem>
              <MenuItem id="ja">{t('japanese')}</MenuItem>
            </MenuSection>
          </Submenu>
        </Submenu>
      </MenuBarMenu>
    </DatumMenuBar>
  );
}
