#pragma once

/// @file
/// @ingroup core
/// Archives (design doc 0002 §7.6, §7.4): one list of fields per type serves saving, loading and
/// hashing the state. A type lists its fields once, in a member every archive calls:
///
///     template<typename Self, typename Archive> void Fields(this Self& self, Archive& archive) {
///         archive(self.cell, self.power);
///     }
///
/// Numbers, enums and bools are stored as their bytes, little-endian; anything else through its
/// Fields(). The Reader checks every read against what is left, since a save file is untrusted.

#include "core/random.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <span>
#include <type_traits>
#include <vector>

namespace core {

static_assert(std::endian::native == std::endian::little, "archives are little-endian, as every supported platform is");

/// A value an archive stores as its bytes: a number, an enum or a bool.
template<typename T>
concept Scalar = std::is_arithmetic_v<T> || std::is_enum_v<T>;

/// Appends values to a byte buffer.
class Writer {
public:
    /// A writer appending to `bytes`, which must outlive it.
    explicit Writer(std::vector<std::byte>& bytes)
      : m_bytes{ &bytes } {
    }

    /// Appends each of `values`, in order.
    template<typename... T>
    void operator()(const T&... values) {
        (Put(values), ...);
    }

private:
    template<typename T>
    void Put(const T& value) {
        if constexpr (Scalar<T>) {
            const auto raw = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
            m_bytes->insert(m_bytes->end(), raw.begin(), raw.end());
        }
        else {
            value.Fields(*this);
        }
    }

    std::vector<std::byte>* m_bytes;
};

/// Hashes values as a Writer would write them: two states hash alike when every value, in order,
/// has the same bits. The algorithm is part of the replay format.
class Hasher {
public:
    /// Mixes each of `values` into the hash, in order.
    template<typename... T>
    void operator()(const T&... values) {
        (Put(values), ...);
    }

    /// The hash of everything so far.
    [[nodiscard]] u64 Value() const {
        return m_hash;
    }

private:
    template<typename T>
    void Put(const T& value) {
        if constexpr (Scalar<T>) {
            constexpr u32 BITS_PER_BYTE = 8;
            const auto raw = std::bit_cast<std::array<u8, sizeof(T)>>(value);
            u64 bits = 0;
            for (std::size_t i = 0; i < raw.size(); ++i) {
                bits |= u64{ raw.at(i) } << (BITS_PER_BYTE * i);
            }
            m_hash = detail::Mix(m_hash ^ detail::Mix(bits + sizeof(T)));
        }
        else {
            value.Fields(*this);
        }
    }

    u64 m_hash = 0;
};

/// Reads values back from bytes a Writer wrote, checking each read: past the end, or a bool that
/// is neither 0 nor 1, it fails, and every read after that fails too and leaves its value alone.
class Reader {
public:
    /// A reader over `bytes`, which must outlive it.
    explicit Reader(std::span<const std::byte> bytes)
      : m_bytes{ bytes } {
    }

    /// Reads each of `values`, in order; check Failed() afterwards.
    template<typename... T>
    void operator()(T&... values) {
        (Get(values), ...);
    }

    /// Steps over the next `bytes` bytes, unread; fails when fewer are left.
    void Skip(std::size_t bytes) {
        if (m_failed || Remaining() < bytes) {
            Fail();
            return;
        }
        m_offset += bytes;
    }

    /// Marks the bytes as malformed: a check of a value read (an unknown version, a duplicate ID)
    /// failed. Every read after it fails.
    void Fail() {
        m_failed = true;
    }

    /// Whether a read went past the end, a value was malformed, or Fail() was called.
    [[nodiscard]] bool Failed() const {
        return m_failed;
    }

    /// How many bytes are left to read.
    [[nodiscard]] std::size_t Remaining() const {
        return m_bytes.size() - m_offset;
    }

private:
    template<typename T>
    void Get(T& value) {
        if constexpr (std::same_as<T, bool>) {
            u8 raw = 0;
            Get(raw);
            if (raw > 1) {
                Fail();
            }
            if (!m_failed) {
                value = raw == 1;
            }
        }
        else if constexpr (Scalar<T>) {
            if (m_failed || Remaining() < sizeof(T)) {
                Fail();
                return;
            }
            std::array<std::byte, sizeof(T)> raw{};
            std::ranges::copy(m_bytes.subspan(m_offset, sizeof(T)), raw.begin());
            value = std::bit_cast<T>(raw);
            m_offset += sizeof(T);
        }
        else {
            value.Fields(*this);
        }
    }

    std::span<const std::byte> m_bytes;
    std::size_t m_offset = 0;
    bool m_failed = false;
};

} // namespace core
