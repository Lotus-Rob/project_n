#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/names.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 SEED = 2'026;
constexpr u64 NAMED_IDS = 20;
constexpr u64 FEW_IDS = 5;
constexpr u16 OUTSIDE = 9;
constexpr u16 HEAVY = 9;
constexpr u64 MOST_FIRST = 15;
constexpr u64 LEAST_TWO = 10;
constexpr u64 MOST_TWO = 18;

core::Names Table() {
    core::Names names;
    names.surnames = { { .characters = "王", .pinyin = "wang", .weight = 1 } };
    names.given = { { .characters = "小", .pinyin = "xiao", .weight = 1 },
                    { .characters = "明", .pinyin = "ming", .weight = 1 } };
    return names;
}

TEST_CASE("the same identity and seed give the same name") {
    core::Names names = Table();
    names.surnames.push_back({ .characters = "李", .pinyin = "li", .weight = 3 });
    for (u64 id = 1; id <= FEW_IDS; ++id) {
        CHECK(core::NameOf(names, SEED, core::StableId{ id }) == core::NameOf(names, SEED, core::StableId{ id }));
    }
}

TEST_CASE("a name reads as characters in Chinese and pinyin in English") {
    const core::Names names = Table();
    const core::NpcName two{ .surname = 0, .first = 0, .second = 1 };
    CHECK(core::NameText(names, two, core::Language::Chinese) == "王小明");
    CHECK(core::NameText(names, two, core::Language::English) == "Wang Xiaoming");
    const core::NpcName one{ .surname = 0, .first = 1, .second = core::NO_GIVEN };
    CHECK(core::NameText(names, one, core::Language::Chinese) == "王明");
    CHECK(core::NameText(names, one, core::Language::English) == "Wang Ming");
    core::Names vowels = Table();
    vowels.given.push_back({ .characters = "安", .pinyin = "an", .weight = 1 });
    CHECK(core::NameText(vowels, { .surname = 0, .first = 1, .second = 2 }, core::Language::English) == "Wang Ming'an");
    CHECK(core::NameText(vowels, { .surname = 0, .first = 2, .second = 1 }, core::Language::English) == "Wang Anming");
    CHECK(
        core::NameText(names, { .surname = OUTSIDE, .first = OUTSIDE, .second = core::NO_GIVEN }, core::Language::English)
            .empty()
    );
}

TEST_CASE("surnames follow their weights") {
    core::Names names = Table();
    names.surnames = { { .characters = "王", .pinyin = "wang", .weight = HEAVY },
                       { .characters = "李", .pinyin = "li", .weight = 1 } };
    u64 first = 0;
    for (u64 id = 1; id <= NAMED_IDS; ++id) {
        first += core::NameOf(names, SEED, core::StableId{ id }).surname == 0 ? 1 : 0;
    }
    CHECK(first >= MOST_FIRST);
}

TEST_CASE("two given characters about seven times in ten") {
    const core::Names names = Table();
    u64 two = 0;
    for (u64 id = 1; id <= NAMED_IDS; ++id) {
        two += core::NameOf(names, SEED, core::StableId{ id }).second == core::NO_GIVEN ? 0 : 1;
    }
    CHECK(two >= LEAST_TWO);
    CHECK(two <= MOST_TWO);
}

TEST_CASE("distinct names give distinct keys") {
    const std::array<core::NpcName, 6> names{ core::NpcName{ .surname = 0, .first = 0, .second = core::NO_GIVEN },
                                              core::NpcName{ .surname = 0, .first = 0, .second = 0 },
                                              core::NpcName{ .surname = 0, .first = 1, .second = 0 },
                                              core::NpcName{ .surname = 1, .first = 0, .second = 0 },
                                              core::NpcName{ .surname = 5, .first = 7, .second = 3 },
                                              core::NpcName{ .surname = 7, .first = 5, .second = 3 } };
    for (std::size_t left = 0; left < names.size(); ++left) {
        for (std::size_t right = left + 1; right < names.size(); ++right) {
            CHECK(core::NameKey(names.at(left)) != core::NameKey(names.at(right)));
        }
    }
}

TEST_CASE("the names' hash changes with any text or weight") {
    const core::Names base = Table();
    core::Names weighted = Table();
    weighted.surnames[0].weight = 2;
    core::Names spelt = Table();
    spelt.given[1].pinyin = "mingg";
    CHECK(base.Hash() == Table().Hash());
    CHECK(base.Hash() != weighted.Hash());
    CHECK(base.Hash() != spelt.Hash());
}

} // namespace
