#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "core/saves.hpp"

#include <doctest/doctest.h>

namespace {

// A slot in a folder of its own, removed when the test ends.
class Folder {
public:
    explicit Folder(std::string_view name)
      : m_path{ std::filesystem::temp_directory_path() / std::format("project_n_saves_tests_{}", name) } {
        std::filesystem::remove_all(m_path);
    }
    Folder(const Folder&) = delete;
    Folder& operator=(const Folder&) = delete;
    Folder(Folder&&) = delete;
    Folder& operator=(Folder&&) = delete;
    ~Folder() {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    [[nodiscard]] std::filesystem::path Slot() const {
        return m_path / "slot.pns";
    }

private:
    std::filesystem::path m_path;
};

std::vector<std::byte> State(std::size_t size, u8 fill) {
    return std::vector<std::byte>(size, std::byte{ fill });
}

TEST_CASE("a state written is read back, the read waiting for the write") {
    constexpr std::size_t SIZE = 50'000;
    constexpr u8 FILL = 7;
    const Folder folder{ "round_trip" };
    core::Jobs jobs{ 2 };
    core::Saves saves(folder.Slot(), jobs);
    saves.Write(State(SIZE, FILL));
    const auto read = saves.Read();
    REQUIRE(read.has_value());
    CHECK(*read == State(SIZE, FILL));
}

TEST_CASE("the last state written is the one read") {
    constexpr std::size_t SIZE = 1'000;
    constexpr u8 FIRST = 1;
    constexpr u8 SECOND = 2;
    const Folder folder{ "last" };
    core::Jobs jobs{ 2 };
    core::Saves saves(folder.Slot(), jobs);
    saves.Write(State(SIZE, FIRST));
    saves.Write(State(SIZE, SECOND));
    const auto read = saves.Read();
    REQUIRE(read.has_value());
    CHECK(*read == State(SIZE, SECOND));
}

TEST_CASE("with no slot, writes are refused and reads fail") {
    constexpr std::size_t SIZE = 10;
    core::Jobs jobs{ 1 };
    core::Saves saves({}, jobs);
    saves.Write(State(SIZE, 0));
    const auto read = saves.Read();
    CHECK_FALSE(read.has_value());
}

TEST_CASE("reading an empty slot fails") {
    const Folder folder{ "empty" };
    core::Jobs jobs{ 1 };
    core::Saves saves(folder.Slot(), jobs);
    const auto read = saves.Read();
    CHECK_FALSE(read.has_value());
}

} // namespace
