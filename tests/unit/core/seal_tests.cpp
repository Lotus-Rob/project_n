#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/seal.hpp"

#include <doctest/doctest.h>

namespace {

// FNV-1a 64's published value for "a", and the swords' seal's key, computed once and pinned: a key never changes
// between runs, builds or platforms, as the looks' table finds a seal by it.
constexpr core::SealKey KEY_OF_A = 0xaf63'dc4c'8601'ec8cULL;
constexpr core::SealKey SWORD_FORMATION = 0xc74c'275c'95fd'fe22ULL;
static_assert(core::SealKeyOf("sword_formation") == SWORD_FORMATION);

TEST_CASE("a seal key is the same for the same name") {
    const core::SealKey keyOfA = core::SealKeyOf("a");
    const core::SealKey keyOfAgain = core::SealKeyOf("a");
    const core::SealKey keyOfB = core::SealKeyOf("b");
    const core::SealKey keyOfSwords = core::SealKeyOf("sword_formation");
    CHECK(keyOfA == keyOfAgain);
    CHECK(keyOfA != keyOfB);
    CHECK(keyOfA == KEY_OF_A);
    CHECK(keyOfSwords == SWORD_FORMATION);
}

TEST_CASE("a seal's saved fields leave out whether it is the showcase's") {
    constexpr core::Seal SHOWCASE{ .key = SWORD_FORMATION, .size = 1.0F, .showcase = true };
    constexpr core::Seal RAISED{ .key = SWORD_FORMATION, .size = 1.0F, .showcase = false };
    core::Hasher showcase;
    showcase(SHOWCASE);
    core::Hasher raised;
    raised(RAISED);
    CHECK(showcase.Value() == raised.Value());
}

} // namespace
