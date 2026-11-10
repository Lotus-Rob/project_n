#pragma once

/// @file
/// @ingroup core
/// The game's strings (design doc 0002.4.2 §5): the IDs the code names, and their text in each language, as
/// `assets/data/strings.yaml` holds them.

#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace core {

/// A string the game shows, by the name STRING_NAMES gives it in strings.yaml.
enum class StringId : u8 {
    CultivatorName, ///< The player's name label.
    TabletCaption,  ///< The vertical caption beside the stone tablet.
    FoxName,        ///< The fox's name label.
    LabelStack,     ///< A name label for several creatures in one place: `{name}` and `{count}` are filled in.
};
/// How many strings StringId names.
inline constexpr std::size_t STRINGS = 4;
/// Each StringId's name in strings.yaml, in StringId's order.
inline constexpr std::array<std::string_view, STRINGS> STRING_NAMES{
    { "cultivator_name", "tablet_caption", "fox_name", "label_stack" }
};

/// A language the strings come in.
enum class Language : u8 {
    English, ///< `en`.
    Chinese, ///< `zh`: Simplified Chinese.
};
/// How many languages Language names.
inline constexpr std::size_t LANGUAGES = 2;
/// Each Language's code in strings.yaml and settings.yaml, in Language's order.
inline constexpr std::array<std::string_view, LANGUAGES> LANGUAGE_CODES{ { "en", "zh" } };

/// Every string in every language.
struct Strings {
    /// The text of each string (by StringId) in each language (by Language).
    std::array<std::array<std::string, LANGUAGES>, STRINGS> text{};

    /// The text of `string` in `language`.
    [[nodiscard]] const std::string& Text(StringId string, Language language) const {
        return text.at(std::to_underlying(string)).at(std::to_underlying(language));
    }
};

} // namespace core
