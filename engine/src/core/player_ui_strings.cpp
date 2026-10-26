// player_ui_strings.cpp — D05-1: the player menus' fixed strings, Turkish and
// English, as one data table indexed by UiText. Turkish texts use real Turkish
// characters (UTF-8); the menu font (Assets/fonts/default.ttf) carries them.

#include "rowl/core/player_ui_strings.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string>

namespace Rowl::Core {

namespace {

struct Entry {
    const char* tr;
    const char* en;
};

// Order MUST follow the UiText enum (checked by the static_assert below and by
// tests/test_player_ui_language.cpp).
constexpr std::array<Entry, static_cast<std::size_t>(UiText::Count)> kTable = {{
    /* None */ {"", ""},

    /* Paused */ {"Duraklatıldı", "Paused"},
    /* Resume */ {"Devam Et", "Resume"},
    /* SaveGame */ {"Oyunu Kaydet", "Save Game"},
    /* LoadGame */ {"Oyunu Yükle", "Load Game"},
    /* MasterVolume */ {"Ana Ses", "Master Volume"},
    /* Music */ {"Müzik", "Music"},
    /* Sfx */ {"SFX", "SFX"},
    /* Voice */ {"Seslendirme", "Voice"},
    /* TextSpeed */ {"Metin Hızı", "Text Speed"},
    /* History */ {"Geçmiş", "History"},
    /* MainMenu */ {"Ana Menü", "Main Menu"},
    /* Quit */ {"Çıkış", "Quit"},
    /* PickSlotValue */ {"yuva seç >", "pick slot >"},
    /* LinesValue */ {"satırlar >", "lines >"},
    /* AreYouSure */ {"emin misin?", "are you sure?"},
    /* HintPauseMain */
    {"YUKARI/AŞAĞI: seç   ENTER: tamam   ESC: devam et",
     "UP/DOWN: select   ENTER: OK   ESC: resume"},
    /* HintConfirmMainMenu */
    {"ENTER: ana menüye dön (kaydedilmemiş ilerleme kaybolur)   ESC: vazgeç",
     "ENTER: back to main menu (unsaved progress is lost)   ESC: cancel"},
    /* HintConfirmQuit */
    {"ENTER: çıkışı onayla   ESC: vazgeç", "ENTER: confirm quit   ESC: cancel"},

    /* SaveSlotTitle */ {"Kayıt Yuvası Seç", "Choose Save Slot"},
    /* LoadSlotTitle */ {"Yükleme Yuvası Seç", "Choose Load Slot"},
    /* SlotLabel */ {"Yuva", "Slot"},
    /* SlotFull */ {"dolu", "full"},
    /* SlotEmpty */ {"boş", "empty"},
    /* SlotCorrupt */ {"bozuk", "corrupt"},
    /* HintSlots */
    {"ENTER/tık: seç   0-9: doğrudan seç   ESC: geri",
     "ENTER/click: choose   0-9: pick directly   ESC: back"},
    /* SlotPage */ {"Sayfa", "Page"},
    /* HintSlotsPaged */
    {"ENTER/tık: seç   0-9: doğrudan seç   SOL/SAĞ: sayfa   ESC: geri",
     "ENTER/click: choose   0-9: pick directly   LEFT/RIGHT: page   ESC: back"},
    /* SlotIncompatible */ {"uyumsuz", "incompatible"},
    /* SlotSaveFailed */ {"kaydedilemedi", "save failed"},
    /* HintIncompatibleSave */ {"uyumsuz kayıt", "incompatible save"},
    /* HintSaved */ {"kaydedildi", "saved"},
    /* HintSaveFailed */
    {"kaydedilemedi - eski kayıt korundu", "save failed - the old save is kept"},
    /* NoticeSaved */ {"Kaydedildi: ", "Saved: "},
    /* NoticeSaveFailed */ {"Kaydedilemedi: ", "Save failed: "},
    /* NoticeLoaded */ {"Yüklendi: ", "Loaded: "},
    /* NoticeLoadFailed */ {"Yüklenemedi: ", "Load failed: "},
    /* NoticeNoSave */ {"Kayıt yok: ", "No save: "},
    /* ReasonCorrupt */ {"kayıt bozuk", "save is corrupt"},
    /* ReasonIncompatible */ {"kayıt uyumsuz", "save is incompatible"},
    /* ReasonEmpty */ {"yuva boş", "slot is empty"},
    /* ReasonUnreadable */ {"okunamadı", "could not be read"},
    /* NoticeProfileUnwritable */
    {"Tercihler kaydedilemedi: bu oturumda geçerli", "Preferences not saved: valid for this session only"},

    /* HistoryEmpty */ {"Geçmiş boş", "History is empty"},
    /* HintBacklog */ {"YUKARI/AŞAĞI: gez   ESC/B: kapat",
                       "UP/DOWN: scroll   ESC/B: close"},
    /* HintBacklogVoiced */
    {"YUKARI/AŞAĞI: gez   ENTER: sesi dinle   ESC/B: kapat",
     "UP/DOWN: scroll   ENTER: play voice   ESC/B: close"},
    /* VoiceMark */ {"(ses)", "(voice)"},

    /* NewGame */ {"Yeni Oyun", "New Game"},
    /* Continue */ {"Devam Et", "Continue"},
    /* Preferences */ {"Tercihler", "Preferences"},
    /* NoSaveValue */ {"kayıt yok", "no save"},
    /* HintTitle */ {"YUKARI/AŞAĞI: seç   ENTER: tamam",
                     "UP/DOWN: select   ENTER: OK"},
    /* HintTitleQuit */ {"ENTER: çıkışı onayla   ESC: vazgeç",
                         "ENTER: confirm quit   ESC: cancel"},
    /* HintEmptySlot */ {"boş yuva", "empty slot"},
    /* HintCorruptSave */ {"bozuk kayıt", "corrupt save"},
    /* HintLoadFailed */ {"kayıt yüklenemedi", "could not load save"},
    /* HintNoSaveToContinue */ {"devam için kayıt yok", "no save to continue"},

    /* PreferencesTitle */ {"Tercihler", "Preferences"},
    /* AutoSpeed */ {"Otomatik Hızı", "Auto Speed"},
    /* Skip */ {"Atlama", "Skip"},
    /* Off */ {"Kapalı", "Off"},
    /* All */ {"Hepsi", "All"},
    /* ReadOnly */ {"Okunan", "Read only"},
    /* Back */ {"Geri", "Back"},
    /* HintPreferences */
    {"YUKARI/AŞAĞI: seç   SOL/SAĞ: ayar   ESC: geri",
     "UP/DOWN: select   LEFT/RIGHT: adjust   ESC: back"},
    /* Language */ {"Dil", "Language"},
    /* TextSize */ {"Metin Boyutu", "Text Size"},
    /* HighContrast */ {"Yüksek Kontrast", "High Contrast"},
    /* ReduceMotion */ {"Hareketi Azalt", "Reduce Motion"},
    /* On */ {"Açık", "On"},

    /* Extras */ {"Ekstralar", "Extras"},
    /* Gallery */ {"Galeri", "Gallery"},
    /* MusicRoom */ {"Müzik Odası", "Music Room"},
    /* Achievements */ {"Başarımlar", "Achievements"},
    /* LockedMark */ {"???", "???"},
    /* NothingYet */ {"Henüz bir şey yok", "Nothing here yet"},
    /* NowPlaying */ {"çalıyor", "playing"},
    /* HintExtras */
    {"YUKARI/AŞAĞI: seç   ENTER: aç   ESC: geri",
     "UP/DOWN: select   ENTER: open   ESC: back"},
    /* HintGallery */
    {"OKLAR: gez   ENTER/tık: büyüt   ESC: geri",
     "ARROWS: browse   ENTER/click: enlarge   ESC: back"},
    /* HintGalleryViewer */
    {"SOL/SAĞ: önceki/sonraki   ESC/tık: kapat",
     "LEFT/RIGHT: previous/next   ESC/click: close"},
    /* HintMusicRoom */
    {"YUKARI/AŞAĞI: seç   ENTER: çal/durdur   ESC: geri",
     "UP/DOWN: select   ENTER: play/stop   ESC: back"},
    /* HintAchievements */
    {"YUKARI/AŞAĞI: gez   ESC: geri", "UP/DOWN: scroll   ESC: back"},
    /* ToastAchievement */ {"Başarım: ", "Achievement: "},

    /* SecondsUnit */ {"sn", "s"},
}};

}  // namespace

const char* uiText(UiText id, UiLang lang) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= kTable.size()) return "";
    return lang == UiLang::English ? kTable[index].en : kTable[index].tr;
}

namespace {

bool isTurkishTag(std::string_view tag) {
    return tag.size() >= 2 && (tag[0] == 't' || tag[0] == 'T') && (tag[1] == 'r' || tag[1] == 'R') &&
           (tag.size() == 2 || tag[2] == '-' || tag[2] == '_' || tag[2] == '.' || tag[2] == '@');
}

}  // namespace

UiLang systemUiLanguage() {
    if (const char* forced = std::getenv("ROWL_SYSTEM_LOCALE"); forced != nullptr && *forced != '\0') {
        return isTurkishTag(forced) ? UiLang::Turkish : UiLang::English;
    }
    // The OS answer does not change while the process runs: ask once.
    static const UiLang system = [] {
        int count = 0;
        if (SDL_Locale** locales = SDL_GetPreferredLocales(&count); locales != nullptr) {
            const bool turkish = count > 0 && locales[0] != nullptr && locales[0]->language != nullptr &&
                                 isTurkishTag(locales[0]->language);
            const bool known = count > 0;
            SDL_free(locales);
            if (known) return turkish ? UiLang::Turkish : UiLang::English;
        }
        for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
            if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
                return isTurkishTag(value) ? UiLang::Turkish : UiLang::English;
            }
        }
        return UiLang::English;
    }();
    return system;
}

UiLang uiLangForLocale(std::string_view activeLocale, bool localeDeclared) {
    return uiLangForLocale(activeLocale, localeDeclared, systemUiLanguage());
}

UiLang uiLangForLocale(std::string_view activeLocale, bool localeDeclared, UiLang systemLanguage) {
    if (!localeDeclared) return systemLanguage;
    const bool turkish =
        activeLocale.size() >= 2 && (activeLocale[0] == 't' || activeLocale[0] == 'T') &&
        (activeLocale[1] == 'r' || activeLocale[1] == 'R') &&
        (activeLocale.size() == 2 || activeLocale[2] == '-' || activeLocale[2] == '_');
    return turkish ? UiLang::Turkish : UiLang::English;
}

std::string localeDisplayName(std::string_view tag) {
    std::string lower(tag);
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (lower == "tr") return "Türkçe";
    if (lower == "en") return "English";
    if (lower == "de") return "Deutsch";
    return std::string(tag);
}

}  // namespace Rowl::Core

