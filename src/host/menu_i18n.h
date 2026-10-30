#pragma once

// Helia's Dream: the F10 menu's languages. Every string the menu shows is
// written in English in the code (host/options.cpp); `menu_tr` gives it in the
// Menu language setting's language, from tools/menu_i18n.tsv (built in: one row
// per English string, one column per language). A string with no translation
// is shown in English. The game's own text is the Game language setting's
// (hle/system.cpp), not this.
//
// The overlay's fonts carry Latin (Western and Central European, Turkish) and
// Cyrillic letters (tools/make_overlay_fonts.py), which is what decides the
// languages here.

#include <cstddef>

// The languages, in the Menu language setting's order: their codes (as the
// table's header row names its columns).
inline constexpr const char* kMenuLanguageCodes[] = {"en", "ru", "es", "pt_BR", "fr", "de", "it", "pl", "tr"};
inline constexpr int kMenuLanguageCount = static_cast<int>(sizeof(kMenuLanguageCodes) / sizeof(kMenuLanguageCodes[0]));

// `english` in the menu's language (UTF-8); `english` itself when English is
// chosen or the table has no translation of it. The result lives as long as
// the program. Any thread.
const char* menu_tr(const char* english);

// The language now (0 English); set by the options (host/options.cpp).
void menu_i18n_set_language(int index);
int menu_i18n_language();

// For tests and the --menu-strings check: how many of `strings` the table
// translates into language `index`.
std::size_t menu_i18n_covered(int index, const char* const* strings, std::size_t count);
