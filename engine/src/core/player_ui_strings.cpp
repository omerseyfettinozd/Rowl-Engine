// player_ui_strings.cpp — D05-1: the player menus' fixed strings, Turkish and
// English, as one data table indexed by UiText. Turkish texts use real Turkish
// characters (UTF-8); the menu font (Assets/fonts/default.ttf) carries them.

#include "rowl/core/player_ui_strings.hpp"

#include <array>
#include <cstddef>
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

    /* SecondsUnit */ {"sn", "s"},
}};

}  // namespace

const char* uiText(UiText id, UiLang lang) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= kTable.size()) return "";
    return lang == UiLang::English ? kTable[index].en : kTable[index].tr;
}

UiLang uiLangForLocale(std::string_view activeLocale, bool localeDeclared) {
    if (!localeDeclared) return UiLang::Turkish;
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

