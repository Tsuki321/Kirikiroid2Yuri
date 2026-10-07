#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace krkr {

// getLangName returns an English language name, not an ISO language tag.
// In particular, game language alias tables distinguish Traditional Chinese.
inline std::string EnglishLanguageName(std::string tag,
                                       const std::string &displayLanguage = std::string()) {
    std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c) {
        return c == '_' ? '-' : static_cast<char>(std::tolower(c));
    });
    const std::string language = tag.substr(0, tag.find('-'));
    if (language == "zh") {
        const std::string parts = "-" + tag + "-";
        const bool traditional = parts.find("-hant-") != std::string::npos ||
            (parts.find("-hans-") == std::string::npos &&
             (parts.find("-tw-") != std::string::npos ||
              parts.find("-hk-") != std::string::npos ||
              parts.find("-mo-") != std::string::npos));
        return traditional ? "Chinese (Traditional)" : "Chinese (Simplified)";
    }
    if (!displayLanguage.empty()) return displayLanguage;
    struct Entry { const char *code; const char *name; };
    static const Entry entries[] = {
        {"ja", "Japanese"}, {"en", "English"}, {"ko", "Korean"},
        {"de", "German"}, {"fr", "French"}, {"es", "Spanish"},
        {"pt", "Portuguese"}, {"it", "Italian"}, {"ru", "Russian"},
        {"uk", "Ukrainian"}, {"pl", "Polish"}, {"nl", "Dutch"},
        {"ar", "Arabic"}, {"he", "Hebrew"}, {"iw", "Hebrew"},
        {"id", "Indonesian"}, {"in", "Indonesian"}, {"vi", "Vietnamese"},
        {"th", "Thai"}, {"tr", "Turkish"}, {"hi", "Hindi"},
        {"sv", "Swedish"}, {"fi", "Finnish"}, {"da", "Danish"},
        {"nb", "Norwegian (Bokmal)"}, {"nn", "Norwegian (Nynorsk)"},
        {"cs", "Czech"}, {"el", "Greek"}, {"hu", "Hungarian"},
        {"ro", "Romanian"}, {"bg", "Bulgarian"}, {"ms", "Malay"}
    };
    for (const auto &entry : entries)
        if (language == entry.code) return entry.name;
    return language;
}

} // namespace krkr
