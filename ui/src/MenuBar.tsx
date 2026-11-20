// The menu bar, arranged as the C++ application's (plan decision D9): File and
// View now; Edit and Add come with editing. File opens a file, a recent one,
// and closes what is open. View holds the panels, which are DATUM Workspace's
// views, and Appearance ▸ Theme and Language, as in the C++ application.

import type { RecentFile } from './core/Core';
import type { ThemeChoice, Language, MessageKey } from './i18n/messages';
import { MenuBar as DatumMenuBar, MenuBarMenu, MenuItem, MenuSection, MenuSeparator, Submenu } from './design';
import { openShortcut } from './shortcuts';

export interface PanelChoice {
  id: string;
  name: string;
}

interface MenuBarProps {
  t: (key: MessageKey) => string;
  canOpen: boolean;
  /** The core is still starting: why Open… is unavailable, if it is. */
  starting: boolean;
  onOpen: () => void;
  recent: readonly RecentFile[];
  onRecent: (id: string) => void;
  /** A file is open, so there is one to close. */
  canClose: boolean;
  onClose: () => void;
  /** The workspace's views, and the ones showing now; null while there is no workspace. */
  panels: { all: readonly PanelChoice[]; showing: readonly string[]; onChange: (showing: string[]) => void } | null;
  theme: ThemeChoice;
  onThemeChange: (theme: ThemeChoice) => void;
  language: Language;
  onLanguageChange: (language: Language) => void;
}

export function MenuBar({
  t,
  canOpen,
  starting,
  onOpen,
  recent,
  onRecent,
  canClose,
  onClose,
  panels,
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
          note={!canOpen && starting ? t('starting') : undefined}
          onAction={onOpen}
        >
          {t('open')}
        </MenuItem>
        {recent.length > 0 ? (
          <Submenu id="recent" label={t('openRecent')}>
            {recent.map((file) => (
              <MenuItem key={file.id} id={`recent:${file.id}`} textValue={file.name} unavailable={!canOpen} onAction={() => onRecent(file.id)}>
                {file.name}
              </MenuItem>
            ))}
          </Submenu>
        ) : (
          <MenuItem id="recent" unavailable note={t('noRecent')}>
            {t('openRecent')}
          </MenuItem>
        )}
        <MenuSeparator />
        <MenuItem id="close" unavailable={!canClose} onAction={onClose}>
          {t('close')}
        </MenuItem>
      </MenuBarMenu>
      <MenuBarMenu id="view" label={t('view')}>
        {panels && (
          <MenuSection
            title={t('panels')}
            selection="multiple"
            selected={panels.all.filter((panel) => panels.showing.includes(panel.id)).map((panel) => panel.id)}
            onSelectionChange={panels.onChange}
          >
            {panels.all.map((panel) => (
              <MenuItem key={panel.id} id={panel.id}>
                {panel.name}
              </MenuItem>
            ))}
          </MenuSection>
        )}
        {panels && <MenuSeparator />}
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
