#pragma once

/// @file
/// @ingroup core
/// The abilities (`assets/data/abilities.yaml`): each ability's numbers, the Qi the player has, and which ability sits
/// in which slot of the loadout.

#include "core/archive.hpp"
#include "core/hit.hpp"
#include "core/seal.hpp"
#include "core/types.hpp"
#include "core/volley.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

namespace core {

/// A place in the loadout, one per control.
enum class Slot : u8 {
    Generator, ///< Left click.
    Spender,   ///< Right click.
    Movement,  ///< Space.
    Special1,  ///< Key 1.
    Special2,  ///< Key 2.
    Special3,  ///< Key 3.
    Special4,  ///< Key 4.
    Ultimate,  ///< Key 5.
};
/// How many slots there are.
inline constexpr std::size_t SLOTS = 8;
/// The slots' names in abilities.yaml, in Slot's order.
inline constexpr std::array<std::string_view, SLOTS> SLOT_NAMES{ {
    "generator",
    "spender",
    "movement",
    "special_1",
    "special_2",
    "special_3",
    "special_4",
    "ultimate",
} };

/// What an ability is for, and so which slots take it.
enum class SlotKind : u8 {
    Generator, ///< Gains Qi.
    Spender,   ///< Costs Qi.
    Movement,  ///< Moves the caster.
    Special,   ///< One of the four specials.
    Ultimate,  ///< The ultimate.
};
/// How many kinds of slot there are.
inline constexpr std::size_t SLOT_KINDS = 5;
/// The kinds' names in abilities.yaml, in SlotKind's order.
inline constexpr std::array<std::string_view, SLOT_KINDS> SLOT_KIND_NAMES{ {
    "generator",
    "spender",
    "movement",
    "special",
    "ultimate",
} };

/// The kind of ability `slot` takes.
[[nodiscard]] constexpr SlotKind KindOf(Slot slot) {
    if (slot == Slot::Generator) {
        return SlotKind::Generator;
    }
    if (slot == Slot::Spender) {
        return SlotKind::Spender;
    }
    if (slot == Slot::Movement) {
        return SlotKind::Movement;
    }
    if (slot == Slot::Ultimate) {
        return SlotKind::Ultimate;
    }
    return SlotKind::Special;
}

/// An ability the game has.
enum class AbilityKind : u8 {
    None,         ///< An empty slot.
    FireBolt,     ///< The generator: a bolt that burns.
    PoisonLance,  ///< The spender: a strike that poisons.
    Dash,         ///< The movement ability.
    FlyingSwords, ///< The first special: a volley of swords.
};
/// How many abilities AbilityKind names, None included.
inline constexpr std::size_t ABILITY_KINDS = 5;
/// The abilities' names in abilities.yaml, in AbilityKind's order.
inline constexpr std::array<std::string_view, ABILITY_KINDS> ABILITY_NAMES{ {
    "none",
    "fire_bolt",
    "poison_lance",
    "dash",
    "flying_swords",
} };

/// Damage over time an ability leaves on what it hits.
struct Affliction {
    /// How many times it deals its damage, once a second.
    u32 ticks = 0;
    /// Damage each time.
    i64 damage = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.ticks, self.damage);
    }

    /// Whether its damage is not negative.
    [[nodiscard]] bool Valid() const {
        return damage >= 0;
    }
};

/// One ability's numbers. Times are simulation ticks; distances are world units.
struct AbilityData {
    /// Which slots take it.
    SlotKind slot = SlotKind::Generator;
    /// Ticks before it can be cast again.
    u32 cooldownTicks = 0;
    /// Qi a cast costs.
    i64 qiCost = 0;
    /// Qi a cast gives.
    i64 qiGain = 0;
    /// Damage a hit deals.
    i64 damage = 0;
    /// The kind of its hits.
    HitKind hit = HitKind::Damage;
    /// The radius of the area it hits.
    f32 radius = 0.0F;
    /// What it leaves on what it hits.
    Affliction affliction{};
    /// The dash: how far it moves the caster.
    f32 distance = 0.0F;
    /// The dash: ticks it takes.
    u32 durationTicks = 0;
    /// The bolt: world units a tick.
    f32 speed = 0.0F;
    /// The swords' and the bolt's shape.
    VolleyShape volley{};
    /// The seal a cast raises under the caster's volley, from the cast until its last sword leaves its ring; none for most.
    std::optional<SealCast> seal = std::nullopt;

    /// Lists its fields for a writing archive (core/archive.hpp: a Writer or a Hasher, as game data is hashed but never
    /// read back from bytes): the seal as whether there is one, then it if so.
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(
            self.slot,
            self.cooldownTicks,
            self.qiCost,
            self.qiGain,
            self.damage,
            self.hit,
            self.radius,
            self.affliction,
            self.distance,
            self.durationTicks,
            self.speed,
            self.volley
        );
        const bool sealed = self.seal.has_value();
        archive(sealed);
        if (self.seal) {
            archive(*self.seal);
        }
    }

    /// Whether every amount is not negative, every distance finite and not negative, the enums are known and any seal is
    /// valid.
    [[nodiscard]] bool Valid() const {
        const auto distanceOk = [](f32 value) { return std::isfinite(value) && value >= 0.0F; };
        return std::to_underlying(slot) < SLOT_KINDS && std::to_underlying(hit) < HIT_KINDS && qiCost >= 0 && qiGain >= 0 &&
               damage >= 0 && distanceOk(radius) && affliction.Valid() && distanceOk(distance) && distanceOk(speed) &&
               volley.Valid() && (!seal || seal->Valid());
    }
};

/// The player's Qi and every ability, and what each slot holds.
struct Abilities {
    /// The most Qi the player holds.
    i64 qiMax = 0;
    /// The Qi the player starts with.
    i64 qiStart = 0;
    /// Every ability's numbers, by AbilityKind.
    std::array<AbilityData, ABILITY_KINDS> table{};
    /// The ability in each slot, by Slot; None leaves it empty.
    std::array<AbilityKind, SLOTS> loadout{};

    /// The numbers of `kind`.
    [[nodiscard]] const AbilityData& Of(AbilityKind kind) const {
        return table.at(std::to_underlying(kind));
    }

    /// The ability in `slot`.
    [[nodiscard]] AbilityKind In(Slot slot) const {
        return loadout.at(std::to_underlying(slot));
    }

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.qiMax, self.qiStart);
        for (auto& ability : self.table) {
            archive(ability);
        }
        for (auto& kind : self.loadout) {
            archive(kind);
        }
    }

    /// Whether Qi is in range, every ability valid and every slot holds an ability of its kind (or none).
    [[nodiscard]] bool Valid() const {
        if (qiMax < 0 || qiStart < 0 || qiStart > qiMax) {
            return false;
        }
        for (const AbilityData& ability : table) {
            if (!ability.Valid()) {
                return false;
            }
        }
        for (std::size_t slot = 0; slot < SLOTS; ++slot) {
            const std::size_t kind = std::to_underlying(loadout.at(slot));
            if (kind >= ABILITY_KINDS) {
                return false;
            }
            if (kind != 0 && table.at(kind).slot != KindOf(static_cast<Slot>(slot))) {
                return false;
            }
        }
        return true;
    }
};

} // namespace core
