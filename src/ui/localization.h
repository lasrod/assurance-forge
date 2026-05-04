#pragma once

#include <string>

namespace ui {

enum class Language {
    English,
    Japanese,
};

enum class MessageId {
    FileMenu,
    CreateEmptyProject,
    OpenProject,
    SaveProject,
    Exit,
    AddMenu,
    NewGsnSacmFile,
    NewEvidenceRegister,
    NewJ3377CaeRegister,
    EditMenu,
    Preferences,
    PreferencesTitle,
    ViewMenu,
    GsnCanvas,
    CseRegister,
    EvidenceRegister,
    WelcomeScreen,
    Appearance,
    Theme,
    ThemeTweaks,
    Language,
    ShowFps,
    PerfLegendHigh,
    PerfLegendMedium,
    PerfLegendLow,
    English,
    Japanese,
    Ai,
    AiSettingsUnavailable,
    EnableAiSupport,
    Provider,
    Model,
    SaveAiSettings,
    ApiKey,
    SecureStorageUnavailable,
    KeyStored,
    NoApiKeyStored,
    UpdateKey,
    SaveKey,
    RemoveKey,
    TestConnection,
    AiPrivacyNotice,
    Ok,
    Walkthroughs,
    WalkthroughsNotImplemented,
    ImportSacm,
    ImportSacmNotImplemented,
    WelcomeTagline,
    WelcomeStartSection,
    WelcomeOpenRecentProjectsSection,
    WelcomeNoRecentProjects,
    WelcomeActionCreateEmptyTitle,
    WelcomeActionCreateEmptySubtitle,
    WelcomeActionCreateFromTemplateTitle,
    WelcomeActionCreateFromTemplateSubtitle,
    WelcomeActionCreateFromTemplateNotImplemented,
    WelcomeActionOpenProjectTitle,
    WelcomeActionOpenProjectSubtitle,
    WelcomeActionImportSacmTitle,
    WelcomeActionImportSacmSubtitle,
    WelcomeWalkthroughGetStartedTitle,
    WelcomeWalkthroughGetStartedSubtitle,
    WelcomeWalkthroughFundamentalsTitle,
    WelcomeWalkthroughFundamentalsSubtitle,
    WelcomeWalkthroughConformanceTitle,
    WelcomeWalkthroughConformanceSubtitle,
    Count,
};

Language CurrentLanguage();
void SetCurrentLanguage(Language language);
Language ParseLanguageCode(const std::string& code);
const char* LanguageCode(Language language);
const char* LanguageDisplayName(Language language);
const char* Tr(MessageId id);

}  // namespace ui
