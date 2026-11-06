#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/save_file.hpp"

#include <doctest/doctest.h>

namespace {

// A folder of its own under the system's temporary folder, removed when the test ends.
class Scratch {
public:
    explicit Scratch(std::string_view name)
      : m_folder{ std::filesystem::temp_directory_path() / std::format("project_n_save_file_tests_{}", name) } {
        std::filesystem::remove_all(m_folder);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code ignored;
        std::filesystem::remove_all(m_folder, ignored);
    }

    [[nodiscard]] const std::filesystem::path& Folder() const {
        return m_folder;
    }

private:
    std::filesystem::path m_folder;
};

std::vector<std::byte> Pattern(std::size_t size) {
    constexpr std::size_t PERIOD = 251;
    std::vector<std::byte> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::byte>((i * i) % PERIOD);
    }
    return bytes;
}

const char* Chars(std::span<const std::byte> bytes) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): char APIs read bytes; chars may alias any object
    return reinterpret_cast<const char*>(bytes.data());
}

char* WritableChars(std::span<std::byte> bytes) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): char APIs write bytes; chars may alias any object
    return reinterpret_cast<char*>(bytes.data());
}

void WriteRaw(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(Chars(bytes), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::byte> ReadRaw(const std::filesystem::path& path) {
    std::vector<std::byte> bytes(static_cast<std::size_t>(std::filesystem::file_size(path)));
    std::ifstream input{ path, std::ios::binary };
    input.read(WritableChars(bytes), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

TEST_CASE("a state saved is read back byte for byte, with no temporary file left") {
    constexpr std::size_t SIZE = 100'000;
    const Scratch scratch{ "round_trip" };
    const std::filesystem::path path = scratch.Folder() / "saves" / "slot.pns";
    const std::vector<std::byte> state = Pattern(SIZE);

    const auto written = core::WriteSaveFile(path, state);
    REQUIRE(written.has_value());
    CHECK(std::filesystem::file_size(path) < SIZE); // compressed
    CHECK(std::ranges::distance(std::filesystem::directory_iterator{ path.parent_path() }) == 1);
    const auto read = core::ReadSaveFile(path);
    REQUIRE(read.has_value());
    CHECK(*read == state);
}

TEST_CASE("an empty state round-trips") {
    const Scratch scratch{ "empty" };
    const std::filesystem::path path = scratch.Folder() / "slot.pns";
    const auto written = core::WriteSaveFile(path, {});
    REQUIRE(written.has_value());
    const auto read = core::ReadSaveFile(path);
    REQUIRE(read.has_value());
    CHECK(read->empty());
}

TEST_CASE("a new save replaces the old one") {
    constexpr std::size_t OLD = 1'000;
    constexpr std::size_t NEW = 2'000;
    const Scratch scratch{ "replace" };
    const std::filesystem::path path = scratch.Folder() / "slot.pns";
    const auto writtenOld = core::WriteSaveFile(path, Pattern(OLD));
    REQUIRE(writtenOld.has_value());
    const auto writtenNew = core::WriteSaveFile(path, Pattern(NEW));
    REQUIRE(writtenNew.has_value());
    const auto read = core::ReadSaveFile(path);
    REQUIRE(read.has_value());
    CHECK(read->size() == NEW);
}

TEST_CASE("a missing file fails to read") {
    const Scratch scratch{ "missing" };
    const auto missing = core::ReadSaveFile(scratch.Folder() / "none.pns");
    CHECK_FALSE(missing.has_value());
}

TEST_CASE("a file that isn't a save fails to read") {
    constexpr std::size_t SIZE = 64;
    const Scratch scratch{ "not_a_save" };
    std::filesystem::create_directories(scratch.Folder());
    const std::filesystem::path path = scratch.Folder() / "slot.pns";
    WriteRaw(path, Pattern(SIZE));
    const auto notASave = core::ReadSaveFile(path);
    CHECK_FALSE(notASave.has_value());
}

TEST_CASE("a truncated or corrupted save fails to read") {
    constexpr std::size_t SIZE = 10'000;
    constexpr std::size_t CUT = 10;
    const Scratch scratch{ "broken" };
    const std::filesystem::path path = scratch.Folder() / "slot.pns";
    const auto written = core::WriteSaveFile(path, Pattern(SIZE));
    REQUIRE(written.has_value());
    const std::vector<std::byte> good = ReadRaw(path);

    WriteRaw(path, std::span{ good }.first(good.size() - CUT));
    const auto truncated = core::ReadSaveFile(path);
    CHECK_FALSE(truncated.has_value());

    constexpr std::byte FLIP{ 0xff };
    std::vector<std::byte> corrupted = good;
    corrupted.back() ^= FLIP;
    corrupted[corrupted.size() / 2] ^= FLIP;
    WriteRaw(path, corrupted);
    const auto readCorrupted = core::ReadSaveFile(path);
    CHECK_FALSE(readCorrupted.has_value());
}

TEST_CASE("a tiny save whose header claims a huge state is refused as sizes that don't add up") {
    constexpr u64 MAGIC = 0x0045'5641'535f'4e50ULL; // "PN_SAVE\0", as save_file.cpp writes it
    constexpr u64 CLAIMED_STATE_BYTES = core::MAX_SAVE_BYTES;
    constexpr std::array<std::byte, 4> PAYLOAD{};
    constexpr u64 CHECKSUM = 0;
    const Scratch scratch{ "huge_claim" };
    std::filesystem::create_directories(scratch.Folder());
    const std::filesystem::path path = scratch.Folder() / "slot.pns";
    std::vector<std::byte> file;
    core::Writer header{ file };
    header(MAGIC, core::SAVE_FILE_VERSION, CLAIMED_STATE_BYTES, u64{ PAYLOAD.size() }, CHECKSUM);
    file.insert(file.end(), PAYLOAD.begin(), PAYLOAD.end());
    WriteRaw(path, file);

    const auto read = core::ReadSaveFile(path);
    REQUIRE_FALSE(read.has_value());
    const std::string& message = read.error().message;
    const std::string why = message.substr(message.rfind(": ") + 2); // after "a broken save file: <path>: "
    CHECK(why == "its sizes don't add up");
}

TEST_CASE("a save whose final rename fails leaves no temporary file beside it") {
    constexpr std::size_t SIZE = 1'000;
    const Scratch scratch{ "rename_fails" };
    const std::filesystem::path path = scratch.Folder() / "slot.pns";
    // A non-empty folder where the save goes: no file can be renamed over it.
    std::filesystem::create_directories(path);
    WriteRaw(path / "keep", Pattern(SIZE));

    const auto written = core::WriteSaveFile(path, Pattern(SIZE));
    REQUIRE_FALSE(written.has_value());
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    const bool temporaryLeft = std::filesystem::exists(temporary);
    CHECK_FALSE(temporaryLeft);
}

} // namespace
