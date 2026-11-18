#pragma once

/// @file
/// @ingroup core
/// A domain written as its difference from a base domain (the one a seed regenerates), so a save holds only what
/// changed: the base entities removed, the entities created (whole), and per component the base entities that gained
/// it (whole), lost it, or changed any of its fields. A field is one argument of the type's Fields(), compared by the
/// bytes a Writer writes for it, so no type lists its fields a second time.
///
/// Layout: the removed entities' places (a count, then each place in the base's stable-ID order, ascending); the
/// created entities (a count, then each one's stable ID, a mask of the components it has, a bit per place in the
/// list, and each of those components' values); then, per component in list order: the places that gained it and
/// their values, the places that lost it, and (unless it is empty) the places whose fields changed, a mask of changed
/// fields for each, and then, field by field, each of those entities' new value of it, in place order. Every count
/// and place is a u64.

#include "core/archive.hpp"
#include "core/assert.hpp"
#include "core/ecs.hpp"
#include "core/state.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace core {

/// The most fields a type, and the most components a list, can have in a delta: its masks give each one bit of a u64.
inline constexpr std::size_t DELTA_MASK_BITS = 64;

/// How far a domain is from its base, in entities.
struct DeltaCounts {
    /// Base entities no longer there.
    u64 removed = 0;
    /// Entities the base doesn't have.
    u64 created = 0;
    /// Entities in both with at least one component gained or lost, or with a changed field.
    u64 changed = 0;
};

namespace detail {

/// An archive that records each top-level field of a value (each argument its Fields() passes) as the bytes a Writer
/// writes for it, so two values compare field by field.
class FieldBytes {
public:
    /// Forgets the fields recorded, keeping the memory for the next value's.
    void Clear() {
        m_bytes.clear();
        m_ends.clear();
    }

    /// Records each of `values` as one field.
    template<typename... T>
    void operator()(const T&... values) {
        static_assert(sizeof...(T) <= DELTA_MASK_BITS, "a delta marks a type's changed fields in one u64");
        (Record(values), ...);
        PN_ASSERT(m_ends.size() <= DELTA_MASK_BITS, "a type with more fields than a delta's mask has bits");
    }

    /// How many fields are recorded.
    [[nodiscard]] std::size_t Count() const {
        return m_ends.size();
    }

    /// The bytes of field `index`, which must be below Count().
    [[nodiscard]] std::span<const std::byte> Field(std::size_t index) const;

private:
    template<typename T>
    void Record(const T& value) {
        Writer{ m_bytes }(value);
        m_ends.push_back(m_bytes.size());
    }

    std::vector<std::byte> m_bytes;
    std::vector<std::size_t> m_ends;
};

/// An archive that hands one top-level field of a value, the `wanted`th argument of its Fields(), to another archive
/// (a Writer or Hasher to write it, a Reader to read it in place) and skips the rest.
template<typename Inner>
class OneField {
public:
    /// Passes field `wanted` to `inner`, which must outlive it.
    OneField(Inner& inner, std::size_t wanted)
      : m_inner{ &inner }
      , m_wanted{ wanted } {
    }

    /// Passes on the one of `values` that is the wanted field, counting from the fields before.
    template<typename... T>
    void operator()(T&... values) {
        static_assert(sizeof...(T) <= DELTA_MASK_BITS, "a delta marks a type's changed fields in one u64");
        (Pass(values), ...);
    }

private:
    template<typename T>
    void Pass(T& value) {
        if (m_index == m_wanted) {
            (*m_inner)(value);
        }
        ++m_index;
    }

    Inner* m_inner;
    std::size_t m_wanted;
    std::size_t m_index = 0;
};

/// The fields of `base` whose bytes differ in `now` (both recorded from values of one type), a bit each.
[[nodiscard]] u64 ChangedFields(const FieldBytes& base, const FieldBytes& now);

/// How many top-level fields `Component` has.
template<typename Component>
[[nodiscard]] std::size_t FieldCount() {
    const Component probe{};
    FieldBytes fields;
    probe.Fields(fields);
    return fields.Count();
}

/// Whether `mask` marks field `field`.
[[nodiscard]] constexpr bool HasField(u64 mask, std::size_t field) {
    return ((mask >> field) & 1U) == 1U;
}

/// What changed in one component's storage between a base and now, by base place (ascending).
struct ComponentChanges {
    /// Places of base entities that have it now and didn't.
    std::vector<u64> gained;
    /// Places of base entities that had it and don't now.
    std::vector<u64> lost;
    /// Places of base entities with a field of it changed.
    std::vector<u64> changed;
    /// Each changed place's changed fields, a bit each.
    std::vector<u64> masks;
};

/// Which entities a base and now don't share.
struct EntityChanges {
    /// The base's stable IDs, ascending: a base entity's place is its index here.
    std::vector<StableId> base;
    /// Places of base entities no longer in now, ascending.
    std::vector<u64> removed;
    /// Stable IDs of entities in now the base lacks, ascending.
    std::vector<StableId> created;
};

/// Compares the entities of `base` and `now`.
[[nodiscard]] EntityChanges CompareEntities(const Domain& base, const Domain& now);

/// `domain`'s stable IDs, ascending.
[[nodiscard]] std::vector<StableId> SortedIds(const Domain& domain);

/// Reads a count and that many places of a base of `baseCount` entities, which must ascend strictly, stay below
/// `baseCount` and be none of `taken` (ascending: places the same section named already); fails `archive` otherwise.
[[nodiscard]] std::vector<u64> ReadPlaces(Reader& archive, std::size_t baseCount, std::span<const u64> taken = {});

/// Marks in `changed` (one entry per base place) every place `changes` gains, loses or changes.
void MarkChanged(const ComponentChanges& changes, std::vector<u8>& changed);

/// Records in `changes` the places of the entities `base` and `now` share that gained or lost `Component`, and calls
/// `kept(place, old, current)` for each that has it in both; `baseIds` is the base's IDs, ascending.
template<typename Component, typename Kept>
void ComparePresence(
    const Domain& base,
    const Domain& now,
    std::span<const StableId> baseIds,
    ComponentChanges& changes,
    Kept kept
) {
    const Registry& before = base.Entities();
    const Registry& after = now.Entities();
    for (std::size_t place = 0; place < baseIds.size(); ++place) {
        const Entity current = now.Find(baseIds[place]);
        if (current == Entity{ entt::null }) {
            continue;
        }
        const Entity old = base.Find(baseIds[place]);
        const bool had = before.all_of<Component>(old);
        const bool has = after.all_of<Component>(current);
        if (has && !had) {
            changes.gained.push_back(place);
        }
        else if (had && !has) {
            changes.lost.push_back(place);
        }
        else if (had) {
            kept(place, old, current);
        }
    }
}

/// Compares `Component` on every entity `base` and `now` share; `baseIds` is the base's IDs, ascending.
template<typename Component>
[[nodiscard]] ComponentChanges CompareComponent(const Domain& base, const Domain& now, std::span<const StableId> baseIds) {
    ComponentChanges changes;
    if constexpr (std::is_empty_v<Component>) {
        ComparePresence<Component>(base, now, baseIds, changes, [](std::size_t /*place*/, Entity /*old*/, Entity /*current*/) {
        });
    }
    else {
        // Two buffers for the whole pass: comparing thousands of entities allocates only while a value outgrows them.
        FieldBytes beforeFields;
        FieldBytes afterFields;
        ComparePresence<Component>(base, now, baseIds, changes, [&](std::size_t place, Entity old, Entity current) {
            beforeFields.Clear();
            afterFields.Clear();
            base.Entities().get<Component>(old).Fields(beforeFields);
            now.Entities().get<Component>(current).Fields(afterFields);
            const u64 mask = ChangedFields(beforeFields, afterFields);
            if (mask != 0) {
                changes.changed.push_back(place);
                changes.masks.push_back(mask);
            }
        });
    }
    return changes;
}

/// Writes how many `places` there are, then each of them.
template<typename Archive>
void SavePlaces(Archive& archive, std::span<const u64> places) {
    archive(static_cast<u64>(places.size()));
    for (const u64 place : places) {
        archive(place);
    }
}

/// Writes `Component`'s section of a delta (the layout in this file's comment).
template<typename Component, typename Archive>
void SaveComponentChanges(Archive& archive, const Domain& base, const Domain& now, std::span<const StableId> baseIds) {
    const ComponentChanges changes = CompareComponent<Component>(base, now, baseIds);
    const Registry& after = now.Entities();
    SavePlaces(archive, changes.gained);
    if constexpr (!std::is_empty_v<Component>) {
        for (const u64 place : changes.gained) {
            archive(after.get<Component>(now.Find(baseIds[place])));
        }
    }
    SavePlaces(archive, changes.lost);
    if constexpr (!std::is_empty_v<Component>) {
        SavePlaces(archive, changes.changed);
        u64 fields = 0;
        for (const u64 mask : changes.masks) {
            archive(mask);
            fields |= mask;
        }
        for (std::size_t field = 0; std::cmp_less(field, std::bit_width(fields)); ++field) {
            for (std::size_t i = 0; i < changes.changed.size(); ++i) {
                if (HasField(changes.masks[i], field)) {
                    OneField<Archive> one{ archive, field };
                    after.get<Component>(now.Find(baseIds[changes.changed[i]])).Fields(one);
                }
            }
        }
    }
}

/// The entity at each of `places` in `domain`, whose base IDs are `baseIds`; fails `archive` when one is gone.
[[nodiscard]] std::vector<Entity> EntitiesAt(
    Reader& archive,
    const Domain& domain,
    std::span<const StableId> baseIds,
    std::span<const u64> places
);

/// Reads the places of base entities that gained `Component`, then their values, into `domain`; returns the places.
template<typename Component>
[[nodiscard]] std::vector<u64> LoadGained(Reader& archive, Domain& domain, std::span<const StableId> baseIds) {
    Registry& registry = domain.Entities();
    std::vector<u64> places = ReadPlaces(archive, baseIds.size());
    for (const Entity entity : EntitiesAt(archive, domain, baseIds, places)) {
        if (archive.Failed() || registry.all_of<Component>(entity)) {
            archive.Fail();
            break;
        }
        EmplaceRead<Component>(archive, registry, entity);
    }
    return places;
}

/// Reads the places of base entities that lost `Component`, none of them `gained` in the same section, and takes it
/// from them in `domain`.
template<typename Component>
void LoadLost(Reader& archive, Domain& domain, std::span<const StableId> baseIds, std::span<const u64> gained) {
    Registry& registry = domain.Entities();
    for (const Entity entity : EntitiesAt(archive, domain, baseIds, ReadPlaces(archive, baseIds.size(), gained))) {
        if (archive.Failed() || !registry.all_of<Component>(entity)) {
            archive.Fail();
            return;
        }
        registry.remove<Component>(entity);
    }
}

/// Reads the places of base entities whose `Component` changed (none of them `gained` in the same section), their masks
/// and their new values field by field, into `domain`. A value is replaced only once every field is read and its
/// type's `Valid()` (when it has one) accepts it.
template<typename Component>
void LoadChanged(Reader& archive, Domain& domain, std::span<const StableId> baseIds, std::span<const u64> gained) {
    Registry& registry = domain.Entities();
    const std::vector<Entity> changed = EntitiesAt(archive, domain, baseIds, ReadPlaces(archive, baseIds.size(), gained));
    const std::size_t fields = FieldCount<Component>();
    std::vector<u64> masks;
    std::vector<Component> values;
    masks.reserve(changed.size());
    values.reserve(changed.size());
    for (const Entity entity : changed) {
        u64 mask = 0;
        archive(mask);
        const bool pastFields = fields < DELTA_MASK_BITS && (mask >> fields) != 0;
        if (archive.Failed() || mask == 0 || pastFields || !registry.all_of<Component>(entity)) {
            archive.Fail();
            return;
        }
        masks.push_back(mask);
        values.push_back(registry.get<Component>(entity));
    }
    for (std::size_t field = 0; field < fields; ++field) {
        for (std::size_t i = 0; i < values.size() && !archive.Failed(); ++i) {
            if (HasField(masks[i], field)) {
                OneField<Reader> one{ archive, field };
                values[i].Fields(one);
            }
        }
    }
    for (std::size_t i = 0; i < values.size() && !archive.Failed(); ++i) {
        if constexpr (requires { values[i].Valid(); }) {
            if (!values[i].Valid()) {
                archive.Fail();
                return;
            }
        }
        registry.replace<Component>(changed[i], std::move(values[i]));
    }
}

/// Reads `Component`'s section of a delta into `domain` (the layout in this file's comment); fails `archive` on
/// anything malformed.
template<typename Component>
void LoadComponentChanges(Reader& archive, Domain& domain, std::span<const StableId> baseIds) {
    // A gained value is written whole, so a section that also loses or changes it would spell one state two ways.
    const std::vector<u64> gained = LoadGained<Component>(archive, domain, baseIds);
    LoadLost<Component>(archive, domain, baseIds, gained);
    if constexpr (!std::is_empty_v<Component>) {
        LoadChanged<Component>(archive, domain, baseIds, gained);
    }
}

} // namespace detail

/// Writes `now` as its difference from `base` into `archive` (a Writer or a Hasher), `Components` by their fields: the
/// layout is in this file's comment. A component missing from the list is neither compared nor saved. Equal domains
/// write only empty counts, whatever order their storages are in.
template<typename Archive, typename... Components>
void SaveDomainDelta(Archive& archive, const Domain& base, const Domain& now, ComponentList<Components...> /*components*/) {
    static_assert(sizeof...(Components) <= DELTA_MASK_BITS, "a created entity's components are one u64's bits");
    const detail::EntityChanges entities = detail::CompareEntities(base, now);
    detail::SavePlaces(archive, entities.removed);
    archive(static_cast<u64>(entities.created.size()));
    const Registry& after = now.Entities();
    for (const StableId stableId : entities.created) {
        const Entity entity = now.Find(stableId);
        u64 mask = 0;
        u64 bit = 1;
        const auto mark = [&]<typename Component>() {
            mask |= after.all_of<Component>(entity) ? bit : 0;
            bit <<= 1U;
        };
        (mark.template operator()<Components>(), ...);
        archive(stableId, mask);
        const auto save = [&]<typename Component>() {
            if constexpr (!std::is_empty_v<Component>) {
                if (after.all_of<Component>(entity)) {
                    archive(after.get<Component>(entity));
                }
            }
        };
        (save.template operator()<Components>(), ...);
    }
    (detail::SaveComponentChanges<Components>(archive, base, now, entities.base), ...);
}

/// Applies a delta SaveDomainDelta() wrote to domain `index` of `state`, which holds the base it was written against,
/// creating entities through State::Restore(). Fails `archive` on anything malformed: a place past the base's count
/// or out of order, a base entity named after it was removed, a created ID the state already has or never handed
/// out, a component gained by an entity that has it or lost or changed by one without it, a mask naming no field or
/// one past its type's, a value its type's `Valid()` (when it has one) rejects, or bytes running out. A state that
/// failed is to be thrown away.
template<typename... Components>
void LoadDomainDelta(Reader& archive, State& state, DomainIndex index, ComponentList<Components...> /*components*/) {
    static_assert(sizeof...(Components) <= DELTA_MASK_BITS, "a created entity's components are one u64's bits");
    constexpr u64 KNOWN = sizeof...(Components) == DELTA_MASK_BITS ? ~u64{ 0 } : (u64{ 1 } << sizeof...(Components)) - 1;
    const std::vector<StableId> baseIds = detail::SortedIds(state[index]);
    const std::vector<u64> removed = detail::ReadPlaces(archive, baseIds.size());
    u64 created = 0;
    archive(created);
    if (created > archive.Remaining() / (sizeof(StableId) + sizeof(u64))) {
        archive.Fail();
    }
    // Created before the removed go, so a created ID equal to a removed one is refused: IDs are never reused.
    for (u64 i = 0; i < created && !archive.Failed(); ++i) {
        StableId stableId{};
        u64 mask = 0;
        archive(stableId, mask);
        if (archive.Failed() || (mask & ~KNOWN) != 0 || !state.Restore(index, stableId)) {
            archive.Fail();
            return;
        }
        Registry& registry = state[index].Entities();
        const Entity entity = state[index].Find(stableId);
        u64 bit = 1;
        const auto load = [&]<typename Component>() {
            if ((mask & bit) != 0 && !archive.Failed()) {
                detail::EmplaceRead<Component>(archive, registry, entity);
            }
            bit <<= 1U;
        };
        (load.template operator()<Components>(), ...);
    }
    if (archive.Failed()) {
        return;
    }
    for (const u64 place : removed) {
        state.Destroy(baseIds[place]);
    }
    (detail::LoadComponentChanges<Components>(archive, state[index], baseIds), ...);
}

/// How far `now` is from `base`, as SaveDomainDelta() would write it with `components`.
template<typename... Components>
[[nodiscard]] DeltaCounts CountDelta(const Domain& base, const Domain& now, ComponentList<Components...> /*components*/) {
    const detail::EntityChanges entities = detail::CompareEntities(base, now);
    std::vector<u8> changed(entities.base.size(), 0);
    (detail::MarkChanged(detail::CompareComponent<Components>(base, now, entities.base), changed), ...);
    return {
        .removed = entities.removed.size(),
        .created = entities.created.size(),
        .changed = static_cast<u64>(std::ranges::count(changed, u8{ 1 })),
    };
}

} // namespace core
