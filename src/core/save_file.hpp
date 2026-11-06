#pragma once

/// @file
/// @ingroup core
/// Save files on disk (design doc 0002 §7.6): a state's bytes, compressed with LZ4 behind a short
/// header with their checksum, written so that a crash never leaves a broken save, and read back with every size
/// checked, since a file on disk is untrusted.

#include "core/error.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

namespace core {

/// The file format's version: the header and compression around a state's bytes. The state's own
/// format carries its own versions.
inline constexpr u32 SAVE_FILE_VERSION = 1;
/// The largest state a save file holds, uncompressed: far beyond design doc 0002 §13's budget, and
/// within what LZ4 compresses in one block.
inline constexpr std::size_t MAX_SAVE_BYTES = std::size_t{ 1 } << 30U;

/// Writes `state` (at most MAX_SAVE_BYTES) compressed to `path`, creating its folder: into a
/// temporary file beside it first, flushed and closed, then renamed over `path`, so a crash leaves
/// either the old save or the new one, never part of one. On failure the old save is untouched.
/// shortcut: safe against the game crashing, not against the machine losing power (no fsync);
/// add a platform flush once saves matter to players.
[[nodiscard]] std::expected<void, Error> WriteSaveFile(const std::filesystem::path& path, std::span<const std::byte> state);

/// Reads the state saved at `path`: fails on a missing or unreadable file, a wrong header or
/// version, sizes that don't add up, or bytes that don't decompress to exactly the size and
/// checksum recorded.
[[nodiscard]] std::expected<std::vector<std::byte>, Error> ReadSaveFile(const std::filesystem::path& path);

} // namespace core
