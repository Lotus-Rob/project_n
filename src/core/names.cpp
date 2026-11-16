#include "core/names.hpp"

#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/random.hpp"

namespace core {

namespace {

// The purpose of the draws NameOf makes, apart from every other use of core::Random.
constexpr u32 NAME_PURPOSE = 0x4e41'4d45; // "NAME"
constexpr u32 DRAW_SURNAME = 0;
constexpr u32 DRAW_COUNT = 1;
constexpr u32 DRAW_FIRST = 2;
constexpr u32 DRAW_SECOND = 3;
// Of this many equal chances, TWO_GIVEN_SHARE give two given characters; the rest give one.
constexpr u32 DRAWS_OF_COUNT = 100;
constexpr u32 TWO_GIVEN_SHARE = 70;
// The bits of a key's parts: the surname's, the first given's, and the second's plus one (0 for none).
constexpr u32 SURNAME_BITS = 10;
constexpr u32 FIRST_BITS = 10;
constexpr u32 SECOND_MASK = (1U << 11U) - 1U;
constexpr u32 PART_MASK = (1U << 10U) - 1U;

void HashText(Hasher& hasher, const std::string& text) {
    hasher(static_cast<u64>(text.size()));
    for (const char character : text) {
        hasher(character);
    }
}

void HashTable(Hasher& hasher, const std::vector<NameEntry>& table) {
    hasher(static_cast<u64>(table.size()));
    for (const NameEntry& entry : table) {
        HashText(hasher, entry.characters);
        HashText(hasher, entry.pinyin);
        hasher(entry.weight);
    }
}

// The characters of entry `index` of `table`, or nothing when it is outside.
std::string Characters(const std::vector<NameEntry>& table, u16 index) {
    return index < table.size() ? table[index].characters : std::string{};
}

// The pinyin of entry `index` of `table`, or nothing when it is outside.
std::string Pinyin(const std::vector<NameEntry>& table, u16 index) {
    return index < table.size() ? table[index].pinyin : std::string{};
}

// `text` with its first letter capitalised (ASCII).
std::string Capitalised(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    }
    return text;
}

} // namespace

u64 Names::Hash() const {
    Hasher hasher;
    HashTable(hasher, surnames);
    HashTable(hasher, given);
    return hasher.Value();
}

NpcName NameOf(const Names& names, u64 seed, StableId npc) {
    if (names.surnames.empty() || names.given.empty()) {
        return {};
    }
    const u64 who = std::to_underlying(npc);
    u64 total = 0;
    for (const NameEntry& entry : names.surnames) {
        total += entry.weight;
    }
    // A table holds at most MOST_NAMES (1,024) entries of at most 65,535, so the total fits a u32.
    u64 pick = ToBelow(Random(seed, 0, who, NAME_PURPOSE, DRAW_SURNAME), static_cast<u32>(total));
    NpcName name;
    for (std::size_t index = 0; index < names.surnames.size(); ++index) {
        const u64 weight = names.surnames[index].weight;
        if (pick < weight) {
            name.surname = static_cast<u16>(index);
            break;
        }
        pick -= weight;
    }
    const auto given = static_cast<u32>(names.given.size());
    name.first = static_cast<u16>(ToBelow(Random(seed, 0, who, NAME_PURPOSE, DRAW_FIRST), given));
    if (ToBelow(Random(seed, 0, who, NAME_PURPOSE, DRAW_COUNT), DRAWS_OF_COUNT) >= TWO_GIVEN_SHARE) {
        return name;
    }
    name.second = static_cast<u16>(ToBelow(Random(seed, 0, who, NAME_PURPOSE, DRAW_SECOND), given));
    return name;
}

std::string NameText(const Names& names, NpcName name, Language language) {
    if (language == Language::Chinese) {
        return Characters(names.surnames, name.surname) + Characters(names.given, name.first) +
               Characters(names.given, name.second);
    }
    const std::string family = Capitalised(Pinyin(names.surnames, name.surname));
    // A second syllable that starts with a vowel needs an apostrophe, so "Tian'an" isn't read as "Tia nan".
    const std::string second = Pinyin(names.given, name.second);
    const bool apostrophe = !second.empty() && std::string_view{ "aoe" }.contains(second.front());
    const std::string givenName = Capitalised(Pinyin(names.given, name.first) + (apostrophe ? "'" : "") + second);
    if (family.empty() || givenName.empty()) {
        return family + givenName;
    }
    return family + " " + givenName;
}

u32 NameKey(NpcName name) {
    const u32 second = name.second == NO_GIVEN ? 0U : (u32{ name.second } + 1U) & SECOND_MASK;
    return (u32{ name.surname } & PART_MASK) | ((u32{ name.first } & PART_MASK) << SURNAME_BITS) |
           (second << (SURNAME_BITS + FIRST_BITS));
}

} // namespace core
