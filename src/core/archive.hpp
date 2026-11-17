#pragma once

/// @file
/// @ingroup core
/// Archives: one list of fields per type serves saving, loading and
/// hashing the state. A type lists its fields once, in a member every archive calls:
///
///     template<typename Self, typename Archive> void Fields(this Self& self, Archive& archive) {
///         archive(self.cell, self.power);
///     }
///
/// Numbers, enums and bools are stored as their bytes, little-endian; a vector as its size (a u64) and then each
/// element; anything else through its Fields(). The Reader checks every read against what is left, since a save file
/// is untrusted.

#include "core/random.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace core {

static_assert(std::endian::native == std::endian::little, "archives are little-endian, as every supported platform is");

/// A value an archive stores as its bytes: a number, an enum or a bool.
template<typename T>
concept Scalar = std::is_arithmetic_v<T> || std::is_enum_v<T>;

namespace detail {

/// Whether `T` is a std::vector an archive stores: any but `std::vector<bool>`, whose elements aren't objects.
template<typename T>
inline constexpr bool IS_ARCHIVED_VECTOR = false;
/// A std::vector of anything but bool.
template<typename T, typename Allocator>
inline constexpr bool IS_ARCHIVED_VECTOR<std::vector<T, Allocator>> = !std::same_as<T, bool>;

} // namespace detail

/// A value an archive stores as its size and then its elements: a std::vector (of anything but bool).
template<typename T>
concept Sequence = detail::IS_ARCHIVED_VECTOR<T>;

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
        else if constexpr (Sequence<T>) {
            Put(static_cast<u64>(value.size()));
            for (const auto& element : value) {
                Put(element);
            }
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
        else if constexpr (Sequence<T>) {
            Put(static_cast<u64>(value.size()));
            for (const auto& element : value) {
                Put(element);
            }
        }
        else {
            value.Fields(*this);
        }
    }

    u64 m_hash = 0;
};

/// Mixes `text` into `hasher` as its length and then each of its characters, in order.
inline void HashText(Hasher& hasher, std::string_view text) {
    hasher(static_cast<u64>(text.size()));
    for (const char character : text) {
        hasher(character);
    }
}

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
        else if constexpr (Sequence<T>) {
            GetSequence(value);
        }
        else {
            value.Fields(*this);
        }
    }

    // Reads a vector's size and then its elements into `value`, which it changes only when every read succeeds. Each
    // element takes at least its size in bytes (a scalar) or one byte (anything else), so a size past what is left is
    // malformed. The rest is not allocated up front: it reserves no more elements than the bytes left could hold at
    // their size in memory, then grows as elements are read, stopping at the first failure, so what a save makes it
    // allocate follows the bytes it actually holds, never the size it claims.
    template<typename T>
    void GetSequence(T& value) {
        using Element = T::value_type;
        constexpr std::size_t LEAST_BYTES = Scalar<Element> ? sizeof(Element) : 1;
        u64 size = 0;
        Get(size);
        if (!m_failed && size > Remaining() / LEAST_BYTES) {
            Fail();
        }
        if (m_failed) {
            return;
        }
        T read;
        read.reserve(static_cast<std::size_t>(std::min<u64>(size, Remaining() / std::max(LEAST_BYTES, sizeof(Element)))));
        for (u64 index = 0; index < size && !m_failed; ++index) {
            Get(read.emplace_back());
        }
        if (!m_failed) {
            value = std::move(read);
        }
    }

    std::span<const std::byte> m_bytes;
    std::size_t m_offset = 0;
    bool m_failed = false;
};

} // namespace core
