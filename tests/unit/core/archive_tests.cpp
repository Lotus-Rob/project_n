#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

#include <doctest/doctest.h>

namespace {

enum class Colour : u8 {
    Red,
    Blue,
};

struct Pair {
    i32 first = 0;
    f32 second = 0.0F;

    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.first, self.second);
    }
};

TEST_CASE("values written are read back in order, through Fields for structs") {
    constexpr u64 BIG = 0x0123'4567'89ab'cdefULL;
    constexpr Pair PAIR{ .first = -5, .second = 2.5F };
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(BIG, Colour::Blue, true, PAIR);

    core::Reader reader{ bytes };
    u64 big = 0;
    Colour colour = Colour::Red;
    bool flag = false;
    Pair pair;
    reader(big, colour, flag, pair);
    CHECK_FALSE(reader.Failed());
    CHECK(reader.Remaining() == 0);
    CHECK(big == BIG);
    CHECK(colour == Colour::Blue);
    CHECK(flag);
    CHECK(pair.first == PAIR.first);
    CHECK(pair.second == PAIR.second);
}

TEST_CASE("reading past the end fails, and leaves the value alone") {
    constexpr u32 SMALL = 7;
    constexpr u64 UNTOUCHED = 99;
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(SMALL);

    core::Reader reader{ bytes };
    u64 value = UNTOUCHED;
    reader(value);
    CHECK(reader.Failed());
    CHECK(value == UNTOUCHED);
}

TEST_CASE("a read after a failure fails too") {
    constexpr u8 BYTE = 1;
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(BYTE);

    core::Reader reader{ bytes };
    reader.Fail();
    u8 value = 0;
    reader(value);
    CHECK(reader.Failed());
    CHECK(value == 0);
}

TEST_CASE("a bool that is neither 0 nor 1 is malformed") {
    constexpr u8 NOT_A_BOOL = 2;
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(NOT_A_BOOL);

    core::Reader reader{ bytes };
    bool flag = false;
    reader(flag);
    CHECK(reader.Failed());
}

u64 Hash(u32 first, u32 second) {
    core::Hasher hasher;
    hasher(first, second);
    return hasher.Value();
}

u64 Hash(f32 value) {
    core::Hasher hasher;
    hasher(value);
    return hasher.Value();
}

u64 Hash(u64 value) {
    core::Hasher hasher;
    hasher(value);
    return hasher.Value();
}

u64 Hash(u32 value) {
    core::Hasher hasher;
    hasher(value);
    return hasher.Value();
}

TEST_CASE("the hash depends on every value's bits and their order") {
    constexpr u32 ONE = 1;
    constexpr u32 TWO = 2;
    CHECK(Hash(ONE, TWO) == Hash(ONE, TWO));
    CHECK(Hash(ONE, TWO) != Hash(TWO, ONE));
    CHECK(Hash(0.0F) != Hash(-0.0F));
    CHECK(Hash(u32{ 0 }) != Hash(u64{ 0 }));
}

} // namespace
