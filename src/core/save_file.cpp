#include "core/save_file.hpp"

#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"

#include <lz4.h>

namespace core {

namespace {

constexpr u64 MAGIC = 0x0045'5641'535f'4e50ULL; // "PN_SAVE\0" as little-endian bytes
constexpr u64 LZ4_MAX_RATIO = 255;
constexpr std::size_t HEADER_BYTES = sizeof(u64) + sizeof(u32) + (3 * sizeof(u64));

// LZ4 and the streams take chars: the one place bytes become chars.
const char* Chars(std::span<const std::byte> bytes) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): char APIs read bytes; chars may alias any object
    return reinterpret_cast<const char*>(bytes.data());
}

char* WritableChars(std::span<std::byte> bytes) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): char APIs write bytes; chars may alias any object
    return reinterpret_cast<char*>(bytes.data());
}

Error FileError(std::string_view what, const std::filesystem::path& path, std::string_view why) {
    return { .message = std::format("{} {}: {}", what, path.string(), why) };
}

// LZ4 checks only that its input decompresses: a flipped bit inside a literal gets through.
u64 Checksum(std::span<const std::byte> bytes) {
    Hasher hasher;
    std::size_t offset = 0;
    for (; offset + sizeof(u64) <= bytes.size(); offset += sizeof(u64)) {
        std::array<std::byte, sizeof(u64)> word{};
        std::ranges::copy(bytes.subspan(offset, sizeof(u64)), word.begin());
        hasher(std::bit_cast<u64>(word));
    }
    for (; offset < bytes.size(); ++offset) {
        hasher(bytes[offset]);
    }
    return hasher.Value();
}

} // namespace

std::expected<void, Error> WriteSaveFile(const std::filesystem::path& path, std::span<const std::byte> state) {
    PN_PROFILE_ZONE();
    if (state.size() > MAX_SAVE_BYTES) {
        return std::unexpected{ FileError("couldn't save", path, std::format("{} bytes is over the limit", state.size())) };
    }
    const auto bound = static_cast<std::size_t>(LZ4_compressBound(static_cast<i32>(state.size())));
    std::vector<std::byte> compressed(bound);
    const i32 count =
        LZ4_compress_default(Chars(state), WritableChars(compressed), static_cast<i32>(state.size()), static_cast<i32>(bound));
    if (count <= 0 && !state.empty()) {
        return std::unexpected{ FileError("couldn't save", path, "LZ4 failed to compress it") };
    }
    compressed.resize(static_cast<std::size_t>(std::max(count, 0)));
    std::vector<std::byte> file;
    file.reserve(HEADER_BYTES + compressed.size());
    Writer header{ file };
    header(MAGIC, SAVE_FILE_VERSION, u64{ state.size() }, u64{ compressed.size() }, Checksum(state));
    file.insert(file.end(), compressed.begin(), compressed.end());

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return std::unexpected{ FileError("couldn't create the folder for", path, error.message()) };
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
        output.write(Chars(file), static_cast<std::streamsize>(file.size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            return std::unexpected{ FileError("couldn't write", temporary, "the write or flush failed") };
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return std::unexpected{ FileError("couldn't replace", path, error.message()) };
    }
    return {};
}

std::expected<std::vector<std::byte>, Error> ReadSaveFile(const std::filesystem::path& path) {
    PN_PROFILE_ZONE();
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return std::unexpected{ FileError("couldn't read", path, error.message()) };
    }
    const auto bound = static_cast<u64>(LZ4_compressBound(static_cast<i32>(MAX_SAVE_BYTES)));
    if (size < HEADER_BYTES || size > HEADER_BYTES + bound) {
        return std::unexpected{ FileError("not a save file:", path, std::format("{} bytes", size)) };
    }
    std::vector<std::byte> file(static_cast<std::size_t>(size));
    {
        std::ifstream input{ path, std::ios::binary };
        input.read(WritableChars(file), static_cast<std::streamsize>(file.size()));
        if (!input) {
            return std::unexpected{ FileError("couldn't read", path, "the read failed") };
        }
    }

    Reader header{ file };
    u64 magic = 0;
    u32 version = 0;
    u64 stateBytes = 0;
    u64 compressedBytes = 0;
    u64 checksum = 0;
    header(magic, version, stateBytes, compressedBytes, checksum);
    if (magic != MAGIC) {
        return std::unexpected{ FileError("not a save file:", path, "its header is wrong") };
    }
    if (version != SAVE_FILE_VERSION) {
        return std::unexpected{
            FileError("can't read", path, std::format("its format is version {}, not {}", version, SAVE_FILE_VERSION))
        };
    }
    // LZ4 expands at most 255 to 1, so a larger claim can't be real: refused before allocating it.
    if (stateBytes > MAX_SAVE_BYTES || compressedBytes != size - HEADER_BYTES || compressedBytes == 0 ||
        stateBytes > compressedBytes * LZ4_MAX_RATIO) {
        return std::unexpected{ FileError("a broken save file:", path, "its sizes don't add up") };
    }
    std::vector<std::byte> state(static_cast<std::size_t>(stateBytes));
    const i32 decompressed = LZ4_decompress_safe(
        Chars(std::span<const std::byte>{ file }.subspan(HEADER_BYTES)),
        WritableChars(state),
        static_cast<i32>(compressedBytes),
        static_cast<i32>(stateBytes)
    );
    if (std::cmp_not_equal(decompressed, stateBytes)) {
        return std::unexpected{ FileError("a broken save file:", path, "its contents don't decompress") };
    }
    if (Checksum(state) != checksum) {
        return std::unexpected{ FileError("a broken save file:", path, "its checksum doesn't match") };
    }
    return state;
}

} // namespace core
