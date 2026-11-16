#pragma once

/// @file
/// @ingroup core
/// The names of the world's people (phase 6 chunk 5b2): the table `assets/data/names.yaml` holds, and the pure function
/// that gives each NPC a name from its stable ID and the game's seed. A name is never saved or put in core::Strings.

#include "core/stable_id.hpp"
#include "core/strings.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace core {

/// One name in a table: a surname or one given character.
struct NameEntry {
    /// The characters it is written in.
    std::string characters;
    /// Its pinyin, ASCII letters only and lower case, which the English form is written from.
    std::string pinyin;
    /// How often a surname is drawn against the others (at least 1); a given character's weight is unused.
    u16 weight = 1;
};

/// The surnames and given-name characters NPCs draw their names from (`names.yaml`).
struct Names {
    /// The surnames, drawn by weight.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<NameEntry> surnames{};
    /// The given-name characters, drawn uniformly.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<NameEntry> given{};

    /// A hash of every entry: two tables hash alike when every text and weight is the same.
    [[nodiscard]] u64 Hash() const;
};

/// The `second` of an NpcName that has only one given character.
inline constexpr u16 NO_GIVEN = 0xFFFF;
/// The most entries a table may hold: what NameKey() packs into a u32.
inline constexpr std::size_t MOST_NAMES = 1'024;

/// A name as indices into a Names table: a surname, a given character, and perhaps a second.
struct NpcName {
    /// Index into Names::surnames.
    u16 surname = 0;
    /// Index into Names::given.
    u16 first = 0;
    /// Index into Names::given, or NO_GIVEN.
    u16 second = NO_GIVEN;

    /// Whether `other` is the same name.
    [[nodiscard]] bool operator==(const NpcName& other) const = default;
};

/// The name the NPC `npc` has in a game of `seed`: a surname by weight, then one given character (30 in 100) or two
/// (70 in 100), each uniform over Names::given. A pure function of its arguments (core::Random), so a name is never
/// stored. An empty table gives the zero name, which NameText() reads as empty text.
[[nodiscard]] NpcName NameOf(const Names& names, u64 seed, StableId npc);

/// `name` as text: the characters run together in Chinese ("王小明"), `Surname Givenname` in English ("Wang Xiaoming",
/// the given pinyin joined, an apostrophe before a second syllable that starts with a, o or e as in "Tian'an", its first
/// letter capitalised). An index outside the table reads as nothing.
[[nodiscard]] std::string NameText(const Names& names, NpcName name, Language language);

/// A key that labels stack by: distinct names give distinct keys, as each index of a table of at most MOST_NAMES
/// entries fits the key's bits.
[[nodiscard]] u32 NameKey(NpcName name);

} // namespace core
