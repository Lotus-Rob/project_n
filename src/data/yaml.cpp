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
