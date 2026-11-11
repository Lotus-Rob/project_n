#include "data/internal/yaml.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

namespace data::internal {

std::expected<std::string, core::Error> ReadText(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        return std::unexpected{ core::Error{ std::format("couldn't open {}", path.string()) } };
    }
    std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    if (input.bad()) {
        return std::unexpected{ core::Error{ std::format("couldn't read {}", path.string()) } };
    }
    return text;
}

std::expected<void, core::Error> WriteText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    if (!output) {
        return std::unexpected{ core::Error{ std::format("couldn't write {}", path.string()) } };
    }
    return {};
}

core::Error ErrorOf(std::string_view what, const YAML::Exception& error) {
    if (error.mark.is_null()) {
        return { std::format("{}: {}", what, error.msg) };
    }
    return { std::format("{} line {}: {}", what, error.mark.line + 1, error.msg) };
}

std::expected<i64, core::Error> WholeNumber(
    std::string_view what,
    const YAML::Node& map,
    std::string_view name,
    const char* key,
    bool required,
    i64 limit
) {
    const auto fail = [&](const YAML::Node& node, std::string_view why) {
        return std::unexpected{ core::Error{ std::format("{} line {}: {}", what, node.Mark().line + 1, why) } };
    };
    const YAML::Node node = map[key];
    if (!node) {
        if (required) {
            return fail(map, std::format("{} has no {}", name, key));
        }
        return 0;
    }
    f64 value = 0.0;
    try {
        value = node.as<f64>();
    }
    catch (const YAML::Exception&) {
        return fail(node, std::format("{}'s {} isn't a number", name, key));
    }
    // An f64 holds every whole number up to 2^53 exactly; `limit` is below it.
    if (!std::isfinite(value) || value < 0.0 || value != std::floor(value) || value > static_cast<f64>(limit)) {
        return fail(node, std::format("{}'s {} isn't a whole number from 0 to {}", name, key, limit));
    }
    return static_cast<i64>(value);
}

void NoteUnknownKeys(
    std::string_view what,
    const YAML::Node& map,
    std::string_view owner,
    std::string_view kind,
    std::initializer_list<std::string_view> known,
    std::vector<std::string>* warnings
) {
    if (warnings == nullptr || !map.IsMap()) {
        return;
    }
    for (const auto& entry : map) {
        const auto key = entry.first.as<std::string>();
        if (std::ranges::contains(known, std::string_view{ key })) {
            continue;
        }
        const std::string where =
            owner.empty() ? std::format("there is no {} {}", kind, key) : std::format("{} has no {} {}", owner, kind, key);
        warnings->push_back(std::format("{} line {}: {}; it is ignored", what, entry.first.Mark().line + 1, where));
    }
}

} // namespace data::internal
