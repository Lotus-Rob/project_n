#include "data/names.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "data/internal/yaml.hpp"

namespace data {

namespace {

constexpr std::string_view NAMES_READER = "names";
constexpr i64 MOST_WEIGHT = std::numeric_limits<u16>::max();

using Failure = std::unexpected<core::Error>;

Failure RefuseNames(const YAML::Node& node, std::string_view why) {
    return Failure{ internal::RefusalAt(NAMES_READER, node, why) };
}

bool IsLowerLetter(char character) {
    return character >= 'a' && character <= 'z';
}

/// The text at `key` in `entry`, non-empty, which `list` names in refusals.
std::expected<std::string, core::Error> EntryText(const YAML::Node& entry, const char* key, std::string_view list) {
    const YAML::Node node = entry[key];
    if (!node) {
        return RefuseNames(entry, std::format("an entry of {} has no {}", list, key));
    }
    if (!node.IsScalar() || node.Scalar().empty()) {
        return RefuseNames(node, std::format("an entry of {} has a {} that isn't text", list, key));
    }
    return node.Scalar();
}

std::expected<std::vector<core::NameEntry>, core::Error> ReadList(
    const YAML::Node& root,
    const char* key,
    bool weighted,
    std::vector<std::string>* warnings
) {
    const YAML::Node list = root[key];
    if (!list) {
        return RefuseNames(root, std::format("there is no {} list", key));
    }
    if (!list.IsSequence() || list.size() == 0) {
        return RefuseNames(list, std::format("{} isn't a list with entries", key));
    }
    if (list.size() > core::MOST_NAMES) {
        return RefuseNames(list, std::format("{} holds {} entries: at most {}", key, list.size(), core::MOST_NAMES));
    }
    std::vector<core::NameEntry> entries;
    for (const YAML::Node& item : list) {
        if (!item.IsMap()) {
            return RefuseNames(item, std::format("an entry of {} isn't a map", key));
        }
        internal::NoteUnknownKeys(
            NAMES_READER, item, std::format("an entry of {}", key), "key", { "zh", "pinyin", "weight" }, warnings
        );
        const std::expected<std::string, core::Error> characters = EntryText(item, "zh", key);
        if (!characters) {
            return std::unexpected{ characters.error() };
        }
        const std::expected<std::string, core::Error> pinyin = EntryText(item, "pinyin", key);
        if (!pinyin) {
            return std::unexpected{ pinyin.error() };
        }
        if (!std::ranges::all_of(*pinyin, IsLowerLetter)) {
            return RefuseNames(
                item["pinyin"], std::format("the pinyin \"{}\" in {} isn't lower-case ASCII letters", *pinyin, key)
            );
        }
        core::NameEntry entry{ .characters = *characters, .pinyin = *pinyin, .weight = 1 };
        if (const YAML::Node weight = item["weight"]) {
            if (!weighted) {
                return RefuseNames(weight, std::format("an entry of {} has a weight, which only surnames have", key));
            }
            f64 value = 0.0;
            try {
                value = weight.as<f64>();
            }
            catch (const YAML::Exception&) {
                return RefuseNames(weight, std::format("the weight of {} isn't a number", *characters));
            }
            if (!(value >= 1.0) || value != std::floor(value) || value > static_cast<f64>(MOST_WEIGHT)) {
                return RefuseNames(
                    weight, std::format("the weight of {} isn't a whole number from 1 to {}", *characters, MOST_WEIGHT)
                );
            }
            entry.weight = static_cast<u16>(value);
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

} // namespace

std::expected<core::Names, core::Error> ParseNames(std::string_view yaml, std::vector<std::string>* warnings) {
    try {
        const YAML::Node root = YAML::Load(std::string{ yaml });
        if (!root.IsMap()) {
            return RefuseNames(root, "not a map");
        }
        internal::NoteUnknownKeys(NAMES_READER, root, "", "key", { "surnames", "given" }, warnings);
        core::Names names;
        auto surnames = ReadList(root, "surnames", true, warnings);
        if (!surnames) {
            return std::unexpected{ std::move(surnames.error()) };
        }
        names.surnames = std::move(*surnames);
        auto given = ReadList(root, "given", false, warnings);
        if (!given) {
            return std::unexpected{ std::move(given.error()) };
        }
        names.given = std::move(*given);
        return names;
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf(NAMES_READER, error) };
    }
}

std::expected<core::Names, core::Error> LoadNames(const std::filesystem::path& file) {
    std::vector<std::string> warnings;
    std::expected<core::Names, core::Error> names =
        internal::ReadText(file).and_then([&](const std::string& text) { return ParseNames(text, &warnings); });
    for (const std::string& warning : warnings) {
        PN_LOG_WARNING("{}", warning);
    }
    return names;
}

} // namespace data
