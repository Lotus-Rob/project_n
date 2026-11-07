#pragma once

/// @file
/// @ingroup game
/// The demonstration arena (design doc 0002 §2, §16 phase 3): a painted ground with rocks, pines
/// and a shrine, a cultivator, a pack of foxes circling it and glowing orbs circling closer, drawn through the sprite pipeline.
/// Its stress variant adds a crowd and a bullet spiral, about 100,000 instances in all (§8.1), a few
/// NPCs walking in the active tier and 50,000 more in the world tier, whose months pass on request (§7.7).

#include "core/draw_list.hpp"
#include "core/ecs.hpp"
#include "core/frame.hpp"
#include "core/logic.hpp"
#include "core/state.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "game/components.hpp"
#include "game/npcs.hpp"

#include <cmath>
#include <stop_token>

namespace game {

/// A path around a point: the entity's foot circles `centre` at `radius`, a little in and out.
struct Orbit {
    /// The point circled.
    core::Vec2 centre{};
    /// The mean distance from it.
    f32 radius = 0.0F;
    /// How far it strays in and out of that distance, three times a lap.
    f32 wobble = 0.0F;
    /// The angle at tick 0, in radians.
    f32 phase = 0.0F;
    /// Radians per tick; negative goes anticlockwise.
    f32 speed = 0.0F;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.centre, self.radius, self.wobble, self.phase, self.speed);
    }

    /// Whether a value read from a save can be used: finite numbers.
    [[nodiscard]] bool Valid() const {
        return core::IsFinite(centre) && std::isfinite(radius) && std::isfinite(wobble) && std::isfinite(phase) &&
               std::isfinite(speed);
    }
};

/// Where an Orbit puts its entity's foot at `tick`.
[[nodiscard]] core::Vec2 OrbitPoint(const Orbit& orbit, u64 tick);

/// Which arena to build.
enum class Scene : u8 {
    Arena,   ///< The demonstration arena.
    Stress,  ///< The arena with a crowd of 90,000, a spiral of 10,000 bullets and 50,000 world NPCs, for profiling.
    Painted, ///< A still scene of painted sprites and overlapping glows, for judging art and blending (§8.4).
    Cells,   ///< A still grid of 640 map cells, for judging the mips as the view zooms out (§16 phase 3).
    Npcs,    ///< The arena with the stress scene's NPCs but a world of 5,000: every NPC system, cheaply, for tests.
};

/// The arena scene as a simulation: it moves its creatures each tick and publishes every sprite.
class Arena final : public core::Simulation {
public:
    /// Builds `scene`: the same entities, in the same places, every time.
    explicit Arena(Scene scene = Scene::Arena);

    /// Moves every entity on an Orbit to where it is at `counters.simulation`, and walks the
    /// active NPCs, whose decisions draw random numbers keyed on `counters.state`.
    void Tick(const core::TickCounters& counters, const core::TickInput& input) override;

    /// Lets a month pass for the world NPCs (MonthStep).
    void AdvanceCalendar(const core::TickCounters& counters, core::Jobs& jobs, const std::stop_token& stop) override;

    /// Sorts the state into canonical order and writes it, with the scene, the months passed and
    /// the relationships, into `archive`.
    void Save(core::Writer& archive) override;

    /// Replaces the state with the one Save() wrote; false, changing nothing, when the bytes are
    /// malformed, of another version, or don't end where the state does.
    [[nodiscard]] bool Load(core::Reader& archive) override;

    /// A hash of everything Save() writes, in the state's current order.
    [[nodiscard]] u64 StateHash() const override;

    /// How many months have passed.
    [[nodiscard]] u64 Months() const {
        return m_months;
    }

    /// The game state: the arena's entities in the active domain, NPCs in the world domain.
    [[nodiscard]] const core::State& State() const {
        return m_state;
    }

    /// What the NPCs think of each other.
    [[nodiscard]] const Relationships& NpcRelationships() const {
        return m_relationships;
    }

    /// Publishes the ground, then a shadow under each entity that casts one, then the world's
    /// entities in foot-y order, then the effects, packed in fixed chunks on `jobs`: the bytes are
    /// the same whatever its worker count.
    void Publish(core::Snapshot& snapshot, core::Jobs& jobs) override;

private:
    template<typename Archive>
    void Write(Archive& archive) const;

    core::Entity Spawn(core::Vec2 foot, const Look& look);
    void SpawnStress();
    void SpawnNpcs(u32 worldNpcs);
    void SpawnPainted();
    void SpawnCells();

    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer for
    // members whose default constructor is defaulted.
    core::DrawList m_drawList{};
    Relationships m_relationships{};
    MonthStep m_monthStep{};
    // NOLINTEND(readability-redundant-member-init)
    core::State m_state;
    Scene m_scene;
    u64 m_months = 0;    // the calendar: months passed
    core::Rect m_view{}; // what a publish culls to
};

} // namespace game
