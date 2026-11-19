#include "core/save_file.hpp"

#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"

#include <zstd.h>

namespace core {

namespace {

constexpr u64 MAGIC = 0x0045'5641'535f'4e50ULL; // "PN_SAVE\0" as little-endian bytes
/// Level 1: on the shipped world's three-month save it was smaller than level 3 and a third faster, and level 9 saved
/// another 16% only at four times the time, which a save on a frozen tick doesn't spend.
constexpr i32 COMPRESSION_LEVEL = 1;
/// Zstandard's most extreme ratio: a block of one repeated byte takes 4 bytes and stands for 128 KiB.
constexpr u64 MAX_RATIO = 32'768;
constexpr std::size_t HEADER_BYTES = sizeof(u64) + sizeof(u32) + (3 * sizeof(u64));

/// The streams take chars: the one place bytes become chars.
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

/// Zstandard's frames are written without a checksum, and a flipped bit inside a literal would decompress all the same.
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
    std::vector<std::byte> compressed(ZSTD_compressBound(state.size()));
    const std::size_t count =
        ZSTD_compress(compressed.data(), compressed.size(), state.data(), state.size(), COMPRESSION_LEVEL);
    if (ZSTD_isError(count) != 0U) {
        return std::unexpected{
            FileError("couldn't save", path, std::format("Zstandard failed: {}", ZSTD_getErrorName(count)))
        };
    }
    compressed.resize(count);
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
    const u64 bound = ZSTD_compressBound(MAX_SAVE_BYTES);
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
    // Zstandard expands at most MAX_RATIO to 1, so a larger claim can't be real: refused before allocating it.
    if (stateBytes > MAX_SAVE_BYTES || compressedBytes != size - HEADER_BYTES || compressedBytes == 0 ||
        stateBytes > compressedBytes * MAX_RATIO) {
        return std::unexpected{ FileError("a broken save file:", path, "its sizes don't add up") };
    }
    const std::span<const std::byte> body = std::span<const std::byte>{ file }.subspan(HEADER_BYTES);
    // The frame states its own content size: a body that isn't a frame, or one holding another size than the header
    // claims, is refused before a state that large is allocated for it (the ratio alone allows a small file 1 GiB).
    if (ZSTD_getFrameContentSize(body.data(), body.size()) != stateBytes) {
        return std::unexpected{ FileError("a broken save file:", path, "its contents don't decompress") };
    }
    std::vector<std::byte> state(static_cast<std::size_t>(stateBytes));
    const std::size_t decompressed = ZSTD_decompress(state.data(), state.size(), body.data(), body.size());
    if (ZSTD_isError(decompressed) != 0U || decompressed != stateBytes) {
        return std::unexpected{ FileError("a broken save file:", path, "its contents don't decompress") };
    }
    if (Checksum(state) != checksum) {
        return std::unexpected{ FileError("a broken save file:", path, "its checksum doesn't match") };
    }
    return state;
}

} // namespace core
