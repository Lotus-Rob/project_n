#pragma once

/// @file
/// @ingroup core
/// The game data (design doc 0002.3 §7): what the data tables in `assets/data/` say, read by the
/// data module and used by the simulation. A replay records its hash and plays only under the same.

#include "core/abilities.hpp"
#include "core/archive.hpp"
#include "core/types.hpp"

#include "core/generated/atlas.hpp" // by `project_n atlas`

namespace core {

/// One kind of creature: how it looks and moves. A kind that doesn't walk has a walk speed of zero;
/// one that circles nothing, an orbit radius, wobble and speed of zero.
struct CreatureData {
    /// Its sprite.
    atlas::SpriteId sprite = atlas::SpriteId{};
    /// Its health at full.
    i64 health = 0;
    /// World units per tick at full movement.
    f32 walkSpeed = 0.0F;
    /// The mean distance of its orbit from the point circled, in world units.
    f32 orbitRadius = 0.0F;
    /// How far it strays in and out of that distance, in world units.
    f32 orbitWobble = 0.0F;
    /// Radians per tick around its orbit; negative goes anticlockwise.
    f32 orbitSpeed = 0.0F;
    /// Whether its orbit circles the player, wherever the player walks, rather than the player's starting point.
    bool orbitsPlayer = false;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(
            self.sprite, self.health, self.walkSpeed, self.orbitRadius, self.orbitWobble, self.orbitSpeed, self.orbitsPlayer
        );
    }
};

/// Every data table: the test bed's creatures (`assets/data/creatures.yaml`) and the abilities (`abilities.yaml`).
struct GameData {
    /// The cultivator the player walks.
    CreatureData cultivator{};
    /// The foxes circling the cultivator.
    CreatureData fox{};
    /// The player's abilities and loadout.
    Abilities abilities{};

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.cultivator, self.fox, self.abilities);
    }

    /// A hash of every value: two GameData hash alike when every value has the same bits.
    [[nodiscard]] u64 Hash() const {
        Hasher hasher;
        hasher(*this);
        return hasher.Value();
    }
};

} // namespace core
