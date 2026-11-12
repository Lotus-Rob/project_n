#include "app/app.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

#include <doctest/doctest.h>

namespace {

constexpr std::chrono::seconds OLDER{ 10 };
constexpr std::chrono::hours BACKUP_AGE{ 1 };

void WriteFile(const std::filesystem::path& path, std::filesystem::file_time_type time) {
    std::ofstream{ path } << "x";
    std::filesystem::last_write_time(path, time);
}

// A folder of its own holding newest.yaml (now) and older.yaml (OLDER ago), removed when the test ends.
class DataFolder {
public:
    explicit DataFolder(std::string_view name)
      : m_path{ std::filesystem::temp_directory_path() / std::format("project_n_data_watch_tests_{}", name) } {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directory(m_path);
        WriteFile(m_path / "newest.yaml", m_now);
        WriteFile(m_path / "older.yaml", m_now - OLDER);
    }
    DataFolder(const DataFolder&) = delete;
    DataFolder& operator=(const DataFolder&) = delete;
    DataFolder(DataFolder&&) = delete;
    DataFolder& operator=(DataFolder&&) = delete;
    ~DataFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    [[nodiscard]] const std::filesystem::path& Path() const {
        return m_path;
    }

    [[nodiscard]] std::filesystem::file_time_type Now() const {
        return m_now;
    }

private:
    std::filesystem::path m_path;
    std::filesystem::file_time_type m_now = std::filesystem::file_time_type::clock::now();
};

TEST_CASE("the data watch sees a file deleted") {
    const DataFolder folder{ "deleted" };
    app::DataWatch watch{ folder.Path() };
    std::filesystem::remove(folder.Path() / "older.yaml");
    CHECK(watch.Changed());
}

TEST_CASE("the data watch sees a file replaced by an older copy") {
    const DataFolder folder{ "older_copy" };
    app::DataWatch watch{ folder.Path() };
    WriteFile(folder.Path() / "older.yaml", folder.Now() - BACKUP_AGE);
    CHECK(watch.Changed());
}

TEST_CASE("the data watch sees nothing when nothing changed") {
    const DataFolder folder{ "unchanged" };
    app::DataWatch watch{ folder.Path() };
    CHECK_FALSE(watch.Changed());
}

// The data folder's parts as read again: the shipped effects and game data unless changed.
struct Read {
    std::expected<core::Effects, core::Error> effects = core::Effects{};
    std::expected<core::GameData, core::Error> data = game::BuiltInData();
};

app::DataReload ReloadAfter(const Read& read) {
    return app::ReloadOf(core::Effects{}, read.effects, game::BuiltInData(), read.data);
}

core::Effects LargerSwords() {
    constexpr f32 LARGER = 2.0F;
    core::Effects effects;
    effects.flyingSwords.swordScale = LARGER;
    return effects;
}

core::GameData FasterFoxes() {
    constexpr f32 FASTER = 9.0F;
    core::GameData data = game::BuiltInData();
    data.fox.walkSpeed = FASTER;
    return data;
}

TEST_CASE("an edit to the effects alone reloads them, never the game data") {
    const app::DataReload reload = ReloadAfter({ .effects = LargerSwords() });
    CHECK(reload.effects);
    CHECK_FALSE(reload.gameData); // a ReloadData would end a replay playing
}

TEST_CASE("an edit to the game data alone reloads it, not the effects") {
    const app::DataReload reload = ReloadAfter({ .data = FasterFoxes() });
    CHECK_FALSE(reload.effects);
    CHECK(reload.gameData);
}

TEST_CASE("edits to both reload both") {
    const app::DataReload reload = ReloadAfter({ .effects = LargerSwords(), .data = FasterFoxes() });
    CHECK(reload.effects);
    CHECK(reload.gameData);
}

TEST_CASE("a change that leaves both as they were, a strings edit, reloads nothing") {
    const app::DataReload reload = ReloadAfter({});
    CHECK_FALSE(reload.effects);
    CHECK_FALSE(reload.gameData);
}

TEST_CASE("a part that can't be read is not reloaded") {
    const app::DataReload reload = ReloadAfter(
        { .effects = std::unexpected{ core::Error{ "broken" } }, .data = std::unexpected{ core::Error{ "broken" } } }
    );
    CHECK_FALSE(reload.effects);
    CHECK_FALSE(reload.gameData);
}

} // namespace
