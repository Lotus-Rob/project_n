#include "app/app.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/seals.hpp"
#include "core/world_look.hpp"
#include "data/world_look.hpp"
#include "game/scenes.hpp"

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

// A map of two meadow tiles, `name` the key of its one city's name.
std::shared_ptr<const core::WorldMap> TwoTiles(std::string_view name) {
    constexpr f32 TILE = 256.0F;
    constexpr u16 MEADOW_COST = 10;
    core::WorldMapSource source;
    source.columns = 2;
    source.rows = 1;
    source.tileSize = TILE;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = MEADOW_COST } };
    source.grid = { 0, 0 };
    source.places = { { .key = "capital",
                        .kind = core::PlaceKind::City,
                        .sort = "",
                        .tile = { .column = 0, .row = 0 },
                        .nameKey = std::string{ name },
                        .name = {},
                        .people = 1,
                        .start = true } };
    std::expected<core::WorldMap, core::Error> map = core::WorldMap::Build(std::move(source));
    REQUIRE(map.has_value());
    return std::make_shared<const core::WorldMap>(std::move(*map));
}

TEST_CASE("a reload keeps the map as loaded") {
    core::GameData running = game::BuiltInData();
    running.world = TwoTiles("place_capital");
    std::expected<core::GameData, core::Error> read = game::BuiltInData();
    read->world = TwoTiles("place_other"); // a world.yaml edited since the start
    running.names = std::make_shared<const core::Names>();
    read->names = std::make_shared<const core::Names>(); // a names.yaml edited since the start
    CHECK(app::ReloadOf(core::Effects{}, core::Effects{}, running, read).gameData);
    app::KeepWorld(running, read);
    REQUIRE(read.has_value());
    CHECK(read->world == running.world);
    CHECK(read->names == running.names);
    CHECK_FALSE(app::ReloadOf(core::Effects{}, core::Effects{}, running, read).gameData);
    constexpr f32 FASTER = 9.0F;
    read->fox.walkSpeed = FASTER;
    CHECK(app::ReloadOf(core::Effects{}, core::Effects{}, running, read).gameData); // another table's edit still reloads
}

TEST_CASE("keeping the map leaves a game data that couldn't be read as it was") {
    core::GameData running = game::BuiltInData();
    running.world = TwoTiles("place_capital");
    std::expected<core::GameData, core::Error> read = std::unexpected{ core::Error{ "broken" } };
    app::KeepWorld(running, read);
    REQUIRE_FALSE(read.has_value());
    CHECK(read.error().message == "broken");
}

// A look whose meadow has the one stamp sprite `stamp`.
core::WorldLook LookOf(core::atlas::SpriteId stamp) {
    core::WorldLook look;
    look.kinds = { { .stamps = { .sprites = { stamp } } } };
    return look;
}

TEST_CASE("a look that couldn't be read leaves the running one, and reports no change") {
    core::WorldLook current = LookOf(core::atlas::SpriteId::StampMeadow0);
    const u64 before = current.Hash();
    const bool replaced = app::KeepLook(current, std::unexpected{ core::Error{ "broken" } });
    CHECK_FALSE(replaced);
    CHECK(current.Hash() == before);
}

TEST_CASE("a different look replaces the running one, and reports the change") {
    core::WorldLook current = LookOf(core::atlas::SpriteId::StampMeadow0);
    const core::WorldLook edited = LookOf(core::atlas::SpriteId::StampMeadow1);
    const bool replaced = app::KeepLook(current, edited);
    CHECK(replaced);
    CHECK(current.Hash() == edited.Hash());
}

// A seals table holding the one seal `key`, plain.
core::Seals SealsOf(std::string_view key) {
    core::Seals seals;
    seals.looks = { { core::SealKeyOf(key), core::SealLook{} } };
    return seals;
}

TEST_CASE("a seals table that couldn't be read leaves the running one, and reports no change") {
    core::Seals current = SealsOf("a");
    const bool replaced = app::KeepSeals(current, std::unexpected{ core::Error{ "broken" } });
    CHECK_FALSE(replaced);
    REQUIRE(current.looks.size() == 1);
    const core::SealLook* kept = current.Find(core::SealKeyOf("a"));
    CHECK(kept != nullptr);
}

TEST_CASE("a seals table that reads replaces the running one, and reports the change") {
    core::Seals current = SealsOf("a");
    const bool replaced = app::KeepSeals(current, SealsOf("b"));
    CHECK(replaced);
    const core::SealLook* gone = current.Find(core::SealKeyOf("a"));
    const core::SealLook* read = current.Find(core::SealKeyOf("b"));
    CHECK(gone == nullptr);
    CHECK(read != nullptr);
}

// The look reader checks sprites against the manifest scene it names, which must be the one game shows the map in.
TEST_CASE("the look's sprites are checked against the world scene's") {
    const std::string_view world = game::SceneName(game::WORLD_SCENE);
    CHECK(world == data::WORLD_LOOK_SCENE);
}

TEST_CASE("an identical look changes nothing") {
    core::WorldLook current = LookOf(core::atlas::SpriteId::StampMeadow0);
    const bool replaced = app::KeepLook(current, LookOf(core::atlas::SpriteId::StampMeadow0));
    CHECK_FALSE(replaced);
}

} // namespace
