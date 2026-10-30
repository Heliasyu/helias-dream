// Helia's Dream: the F10 menu's languages (host/menu_i18n.h).
#include "host/menu_i18n.h"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// tools/menu_i18n.tsv as one string literal (CMakeLists.txt): a header row of
// language codes, then per row the English string and its translations,
// tab-separated; an empty cell is "not translated". Lines starting with # are
// comments.
const char kTable[] =
#include "menu_i18n.inc"
    ;

std::atomic<int> g_language{0};

struct Tables {
    // Per language (by kMenuLanguageCodes' index), English -> translation.
    std::vector<std::unordered_map<std::string, std::string>> by_lang;
};

const Tables& tables() {
    static const Tables t = [] {
        Tables out;
        out.by_lang.resize(kMenuLanguageCount);
        std::vector<int> column_lang;  // the table's column -> language index, -1 unknown
        const std::string all(kTable);
        std::size_t at = 0;
        bool header = true;
        while (at < all.size()) {
            std::size_t end = all.find('\n', at);
            if (end == std::string::npos) end = all.size();
            std::string line = all.substr(at, end - at);
            at = end + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> cells;
            std::size_t c = 0;
            while (true) {
                const std::size_t tab = line.find('\t', c);
                cells.push_back(line.substr(c, tab == std::string::npos ? std::string::npos : tab - c));
                if (tab == std::string::npos) break;
                c = tab + 1;
            }
            if (header) {
                header = false;
                for (const std::string& code : cells) {
                    int lang = -1;
                    for (int i = 0; i < kMenuLanguageCount; ++i)
                        if (code == kMenuLanguageCodes[i]) lang = i;
                    column_lang.push_back(lang);
                }
                continue;
            }
            if (cells.empty() || cells[0].empty()) continue;
            for (std::size_t k = 1; k < cells.size() && k < column_lang.size(); ++k) {
                if (column_lang[k] > 0 && !cells[k].empty()) out.by_lang[static_cast<std::size_t>(column_lang[k])][cells[0]] = cells[k];
            }
        }
        return out;
    }();
    return t;
}

}  // namespace

void menu_i18n_set_language(int index) { g_language.store(index >= 0 && index < kMenuLanguageCount ? index : 0, std::memory_order_relaxed); }

int menu_i18n_language() { return g_language.load(std::memory_order_relaxed); }

const char* menu_tr(const char* english) {
    if (!english || !*english) return english;
    const int lang = g_language.load(std::memory_order_relaxed);
    if (lang <= 0) return english;
    const auto& m = tables().by_lang[static_cast<std::size_t>(lang)];
    const auto it = m.find(english);
    return it == m.end() ? english : it->second.c_str();
}

std::size_t menu_i18n_covered(int index, const char* const* strings, std::size_t count) {
    if (index <= 0 || index >= kMenuLanguageCount) return count;
    const auto& m = tables().by_lang[static_cast<std::size_t>(index)];
    std::size_t n = 0;
    for (std::size_t i = 0; i < count; ++i) n += m.count(strings[i]) ? 1 : 0;
    return n;
}
