#include "data/strings.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "data/internal/yaml.hpp"

namespace data {

namespace {

std::unexpected<core::Error> FailAt(const YAML::Node& node, std::string_view why) {
    return std::unexpected{ core::Error{ std::format("strings line {}: {}", node.Mark().line + 1, why) } };
}

} // namespace

std::expected<core::Strings, core::Error> ParseStrings(std::string_view yaml, std::vector<std::string>* warnings) {
    try {
        const YAML::Node root = YAML::Load(std::string{ yaml });
        if (!root.IsMap()) {
            return FailAt(root, "not a map of strings");
        }
        core::Strings strings;
        std::array<bool, core::STRINGS> found{};
        const std::span<const std::string_view> names{ core::STRING_NAMES };
        // Walked rather than looked up: yaml-cpp keeps both entries of a name given twice, and a lookup finds the first.
        for (const auto& entry : root) {
            const auto name = entry.first.as<std::string>();
            const auto known = std::ranges::find(names, name);
            if (known == names.end()) {
                if (warnings != nullptr) {
                    warnings->push_back(
                        std::format("strings line {}: there is no string {}; it is ignored", entry.first.Mark().line + 1, name)
                    );
                }
                continue;
            }
            const auto index = static_cast<std::size_t>(known - names.begin());
            if (found.at(index)) {
                return FailAt(entry.first, std::format("{} is there twice", name));
            }
            found.at(index) = true;
            const YAML::Node& texts = entry.second;
            if (!texts.IsMap()) {
                return FailAt(texts, std::format("{} isn't a map of languages", name));
            }
            internal::NoteUnknownKeys("strings", texts, name, "language", { "en", "zh" }, warnings);
            for (std::size_t language = 0; language < core::LANGUAGES; ++language) {
                const std::string_view code = core::LANGUAGE_CODES.at(language);
                const YAML::Node text = texts[std::string{ code }];
                if (!text) {
                    return FailAt(texts, std::format("{} has no {}", name, code));
                }
                if (!text.IsScalar() || text.Scalar().empty()) {
                    return FailAt(text, std::format("{}'s {} isn't text", name, code));
                }
                strings.text.at(index).at(language) = text.Scalar();
            }
        }
        const std::span<const bool> present{ found };
        if (const auto missing = std::ranges::find(present, false); missing != present.end()) {
            return FailAt(root, std::format("there is no {}", names[static_cast<std::size_t>(missing - present.begin())]));
        }
        return strings;
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf("strings", error) };
    }
}

std::expected<core::Strings, core::Error> LoadStrings(const std::filesystem::path& folder) {
    std::vector<std::string> warnings;
    std::expected<core::Strings, core::Error> strings =
        internal::ReadText(folder / "strings.yaml").and_then([&warnings](const std::string& text) {
            return ParseStrings(text, &warnings);
        });
    for (const std::string& warning : warnings) {
        PN_LOG_WARNING("{}", warning);
    }
    return strings;
}

} // namespace data
