#pragma once

#include <string>
#include <string_view>

namespace Rowl::Core {

/// D05-1: language of the player's own UI (pause menu, title screen,
/// preferences, backlog). The AUTO / SKIP badges are universal and stay out.
enum class UiLang : unsigned char { Turkish = 0, English = 1 };

/// Every fixed string of the player menus. Values are keys into a small
/// data table (player_ui_strings.cpp); never compared as text.
enum class UiText : unsigned char {
    None = 0,  // "no text" (empty string in every language)

    // Pause main page
    Paused, Resume, SaveGame, LoadGame, MasterVolume, Music, Sfx, Voice,
    TextSpeed, History, MainMenu, Quit,
    PickSlotValue, LinesValue, AreYouSure,
    HintPauseMain, HintConfirmMainMenu, HintConfirmQuit,

    // Slot pages
    SaveSlotTitle, LoadSlotTitle, SlotLabel, SlotFull, SlotEmpty, SlotCorrupt,
    HintSlots,
    SlotPage, HintSlotsPaged,  // T2b: "Sayfa" and the hint of a slot page with several pages
    // F2: save / load feedback (B-3). Notice* heads end in ": " (the toast capsule
    // colours the head); Hint* are the slot-page / title hint line.
    SlotIncompatible, SlotSaveFailed, HintIncompatibleSave, HintSaved, HintSaveFailed,
    NoticeSaved, NoticeSaveFailed, NoticeLoaded, NoticeLoadFailed, NoticeNoSave,
    ReasonCorrupt, ReasonIncompatible, ReasonEmpty, ReasonUnreadable,
    NoticeProfileUnwritable,

    // Backlog
    HistoryEmpty, HintBacklog, HintBacklogVoiced, VoiceMark,

    // Title screen
    NewGame, Continue, Preferences, NoSaveValue,
    HintTitle, HintTitleQuit,
    HintEmptySlot, HintCorruptSave, HintLoadFailed, HintNoSaveToContinue,

    // Preferences page
    PreferencesTitle, AutoSpeed, Skip, Off, All, ReadOnly, Back,
    HintPreferences,
    // D05-2: language / accessibility rows
    Language, TextSize, HighContrast, ReduceMotion, On,

    // D07-2: Ekstralar (title screen) and the achievement toast
    Extras, Gallery, MusicRoom, Achievements,
    LockedMark, NothingYet, NowPlaying,
    HintExtras, HintGallery, HintGalleryViewer, HintMusicRoom, HintAchievements,
    ToastAchievement,

    // Units
    SecondsUnit,

    Count
};

/// Text of `id` in `lang`; always a valid, NUL-terminated UTF-8 string.
const char* uiText(UiText id, UiLang lang);

/// UI language for an active locale tag: a tag starting with "tr" (alone or as
/// "tr-..." / "tr_...") is Turkish, any other tag English. `localeDeclared`
/// false means the project declares no locales at all (no manifest keys): the
/// menus then follow the SYSTEM language (F2 B-13/B-14: Turkish on a Turkish
/// system, English anywhere else) instead of the historical always-Turkish
/// menus, whatever the "en" fallback locale says. A project that declares its
/// locales is untouched.
UiLang uiLangForLocale(std::string_view activeLocale, bool localeDeclared);
/// Same decision with the system language given (tests / hosts that know it).
UiLang uiLangForLocale(std::string_view activeLocale, bool localeDeclared, UiLang systemLanguage);

/// The player's system language as a menu language: Turkish when the first preferred locale
/// (or, failing that, LC_ALL / LC_MESSAGES / LANG) starts with "tr", English otherwise.
/// `ROWL_SYSTEM_LOCALE` (e.g. "tr", "en_US") overrides the query - the test suite pins it so
/// results do not depend on the machine it runs on.
UiLang systemUiLanguage();

/// D05-2: player-facing name of a locale tag for the Language row: "Türkçe" /
/// "English" / "Deutsch" for "tr" / "en" / "de" (any case), the tag itself for
/// every other (or regional) tag. Never empty for a non-empty tag.
std::string localeDisplayName(std::string_view tag);

}  // namespace Rowl::Core
