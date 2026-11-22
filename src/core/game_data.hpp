#pragma once

/// @file
/// @ingroup core
/// The game data: what the data tables in `assets/data/` say, read by the
/// data module and used by the simulation. A replay records its hash and plays only under the same.

#include "core/abilities.hpp"
#include "core/archive.hpp"
#include "core/combat_rules.hpp"
#include "core/names.hpp"
#include "core/types.hpp"
#include "core/world_map.hpp"

#include "core/generated/atlas.hpp" // by `project_n atlas`

#include <memory>

namespace core {

/// The slowest walk speed a creature that walks may have, in world units per tick. A tile of 256 units then takes over
/// seven hours of play to cross, slower than anything is meant to walk, and a walk of 160,000 such tiles still fits a
/// leg's 32-bit arrive ticks.
inline constexpr f32 MIN_WALK_SPEED = 0.01F;

/// One kind of creature: how it looks and moves. A kind that doesn't walk has a walk speed of zero;
/// one that circles nothing, an orbit radius, wobble and speed of zero.
struct CreatureData {
    /// Its sprite.
    atlas::SpriteId sprite = atlas::SpriteId{};
    /// Its health at full.
    i64 health = 0;
    /// World units per tick at full movement: 0, or at least MIN_WALK_SPEED.
    f32 walkSpeed = 0.0F;
    /// The mean distance of its orbit from the point circled, in world units.
    f32 orbitRadius = 0.0F;
    /// How far it strays in and out of that distance, in world units.
    f32 orbitWobble = 0.0F;
    /// Radians per tick around its orbit; negative goes anticlockwise.
    f32 orbitSpeed = 0.0F;
    /// Whether its orbit circles the player, wherever the player walks, rather than the player's starting point.
    bool orbitsPlayer = false;
    /// How far it sees over the world map, in tiles; 0 for no sight.
    u32 sight = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(
            self.sprite,
            self.health,
            self.walkSpeed,
            self.orbitRadius,
            self.orbitWobble,
            self.orbitSpeed,
            self.orbitsPlayer,
            self.sight
        );
    }
};

/// Every data table: the test bed's creatures (`assets/data/creatures.yaml`), the abilities (`abilities.yaml`), the
/// world map (`world.yaml`) and the names (`names.yaml`).
struct GameData {
    /// The cultivator the player walks.
    CreatureData cultivator{};
    /// The foxes circling the cultivator.
    CreatureData fox{};
    /// The player's abilities and loadout.
    Abilities abilities{};
    /// The times and amounts the fight runs by (abilities.yaml's `combat:` section).
    CombatRules combat{};
    /// The world map (`world.yaml`), shared with the scenes and the drawing; null in game::BuiltInData(). Read once at
    /// the start and kept through a reload, so an edit to `world.yaml` alone changes nothing. Hashed, but not listed in
    /// Fields(): a pointer isn't archived.
    std::shared_ptr<const WorldMap> world = nullptr;
    /// The names NPCs draw theirs from (`names.yaml`); null in game::BuiltInData(). Read once at the start and kept through
    /// a reload as `world` is. Hashed, but not listed in Fields(): a pointer isn't archived.
    std::shared_ptr<const Names> names = nullptr;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.cultivator, self.fox, self.abilities, self.combat);
    }

    /// A hash of every value: two GameData hash alike when every value has the same bits.
    [[nodiscard]] u64 Hash() const {
        Hasher hasher;
        hasher(*this, world ? world->Hash() : u64{ 0 }, names ? names->Hash() : u64{ 0 });
        return hasher.Value();
    }

    /// A hash of the world map and the names alone, what a world generated from a seed is made of and named by: two
    /// GameData hash alike here when their maps and names do, whatever their creatures, abilities and combat say, so an
    /// edit to those leaves the worlds a seed makes, and the saves made against them, as they were.
    [[nodiscard]] u64 BaseHash() const {
        Hasher hasher;
        hasher(world ? world->Hash() : u64{ 0 }, names ? names->Hash() : u64{ 0 });
        return hasher.Value();
    }
};

} // namespace core
