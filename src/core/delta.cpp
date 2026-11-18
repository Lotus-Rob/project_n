#include "core/delta.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace core::detail {

std::span<const std::byte> FieldBytes::Field(std::size_t index) const {
    const std::size_t begin = index == 0 ? 0 : m_ends.at(index - 1);
    return std::span{ m_bytes }.subspan(begin, m_ends.at(index) - begin);
}

u64 ChangedFields(const FieldBytes& base, const FieldBytes& now) {
    PN_ASSERT(base.Count() == now.Count(), "two values of one type with different field counts");
    u64 mask = 0;
    for (std::size_t field = 0; field < base.Count(); ++field) {
        if (!std::ranges::equal(base.Field(field), now.Field(field))) {
            mask |= u64{ 1 } << field;
        }
    }
    return mask;
}

std::vector<StableId> SortedIds(const Domain& domain) {
    const auto* ids = domain.Entities().storage<StableId>();
    std::vector<StableId> sorted;
    if (ids != nullptr) {
        sorted.reserve(ids->size());
        sorted.assign(ids->begin(), ids->end());
    }
    std::ranges::sort(sorted);
    return sorted;
}

EntityChanges CompareEntities(const Domain& base, const Domain& now) {
    EntityChanges changes{ .base = SortedIds(base), .removed = {}, .created = {} };
    for (std::size_t place = 0; place < changes.base.size(); ++place) {
        if (now.Find(changes.base[place]) == Entity{ entt::null }) {
            changes.removed.push_back(place);
        }
    }
    for (const StableId stableId : SortedIds(now)) {
        if (base.Find(stableId) == Entity{ entt::null }) {
            changes.created.push_back(stableId);
        }
    }
    return changes;
}

std::vector<u64> ReadPlaces(Reader& archive, std::size_t baseCount, std::span<const u64> taken) {
    u64 count = 0;
    archive(count);
    // Strictly ascending places below the base's count can't outnumber it; checked first, so a claimed count never
    // reserves more than the base or the bytes left could hold.
    if (count > baseCount || count > archive.Remaining() / sizeof(u64)) {
        archive.Fail();
    }
    std::vector<u64> places;
    if (archive.Failed()) {
        return places;
    }
    places.reserve(count);
    for (u64 i = 0; i < count; ++i) {
        u64 place = 0;
        archive(place);
        const bool ascends = places.empty() || place > places.back();
        if (archive.Failed() || place >= baseCount || !ascends || std::ranges::binary_search(taken, place)) {
            archive.Fail();
            break;
        }
        places.push_back(place);
    }
    return places;
}

std::vector<Entity> EntitiesAt(
    Reader& archive,
    const Domain& domain,
    std::span<const StableId> baseIds,
    std::span<const u64> places
) {
    std::vector<Entity> entities;
    entities.reserve(places.size());
    for (const u64 place : places) {
        const Entity entity = domain.Find(baseIds[place]);
        if (entity == Entity{ entt::null }) {
            archive.Fail();
            break;
        }
        entities.push_back(entity);
    }
    return entities;
}

void MarkChanged(const ComponentChanges& changes, std::vector<u8>& changed) {
    for (const std::span<const u64> places :
         { std::span{ changes.gained }, std::span{ changes.lost }, std::span{ changes.changed } }) {
        for (const u64 place : places) {
            changed.at(place) = 1;
        }
    }
}

} // namespace core::detail
