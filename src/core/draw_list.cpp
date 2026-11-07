#include "core/draw_list.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"

namespace core {

namespace {

constexpr f64 DEPTH_STEPS_PER_UNIT = 16.0;
constexpr std::size_t RADIX = 256;
constexpr u32 BITS_PER_PASS = 8;
constexpr u32 KEY_BITS = 64;
constexpr u32 PASSES = KEY_BITS / BITS_PER_PASS;
constexpr u32 GATHER_CHUNK = 8'192; // instances copied into the snapshot per job chunk

std::size_t Digit(SortKey key, u32 shift) {
    return static_cast<std::size_t>((key >> shift) & (RADIX - 1));
}

} // namespace

u32 QuantiseDepth(f32 footY, f32 viewTop) {
    // Clamped as f64, where u32's maximum is exact; the comparisons also send NaN to 0.
    const f64 steps = static_cast<f64>(footY - viewTop) * DEPTH_STEPS_PER_UNIT;
    constexpr auto MAX = static_cast<f64>(std::numeric_limits<u32>::max());
    if (!(steps > 0.0)) {
        return 0;
    }
    return steps < MAX ? static_cast<u32>(steps) : std::numeric_limits<u32>::max();
}

void RadixSort(std::vector<SortEntry>& entries, std::vector<SortEntry>& scratch) {
    PN_PROFILE_ZONE();
    scratch.resize(entries.size());
    // Every pass's counts in one read of the entries, not one read per pass.
    std::array<std::array<std::size_t, RADIX>, PASSES> counts{};
    for (const SortEntry& entry : entries) {
        for (u32 pass = 0; pass < PASSES; ++pass) {
            ++counts.at(pass).at(Digit(entry.key, pass * BITS_PER_PASS));
        }
    }
    for (u32 pass = 0; pass < PASSES; ++pass) {
        std::array<std::size_t, RADIX>& offsets = counts.at(pass);
        if (std::ranges::contains(offsets, entries.size())) {
            continue; // every key has this byte: the pass would copy them in order
        }
        std::exclusive_scan(offsets.begin(), offsets.end(), offsets.begin(), std::size_t{ 0 });
        for (const SortEntry& entry : entries) {
            scratch[offsets.at(Digit(entry.key, pass * BITS_PER_PASS))++] = entry;
        }
        entries.swap(scratch);
    }
}

void AppendBatches(std::span<const SortEntry> sorted, std::vector<Batch>& batches) {
    PN_PROFILE_ZONE();
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const Pipeline pipeline = PipelineOf(sorted[i].key);
        if (i == 0 || batches.back().pipeline != pipeline) {
            batches.push_back({ .first = static_cast<u32>(i), .count = 0, .pipeline = pipeline });
        }
        ++batches.back().count;
    }
}

void DrawList::Begin(Rect view, u32 parts) {
    PN_PROFILE_ZONE();
    PN_ASSERT(parts > 0, "a draw list fills in at least one part");
    m_view = view;
    m_partCount = parts;
    if (m_parts.size() < parts) {
        m_parts.resize(parts);
    }
    for (Part& part : std::span{ m_parts }.first(parts)) {
        part.instances.clear();
        part.entries.clear();
        part.ids.clear();
    }
}

void DrawList::Add(u32 part, Layer layer, Pipeline pipeline, const Instance& instance, f32 footY, StableId entity) {
    PN_ASSERT(part < m_partCount, "a draw list part past those Begin asked for");
    const Vec2 position = instance.current;
    if (position.x < m_view.min.x - CULL_MARGIN || position.x > m_view.max.x + CULL_MARGIN ||
        position.y < m_view.min.y - CULL_MARGIN || position.y > m_view.max.y + CULL_MARGIN) {
        return;
    }
    Part& into = m_parts[part];
    const u32 depth = layer == Layer::World ? QuantiseDepth(footY, m_view.min.y) : 0;
    into.entries.push_back({ .key = MakeSortKey(layer, depth, pipeline), .index = static_cast<u32>(into.instances.size()) });
    into.instances.push_back(instance);
    into.ids.push_back(entity);
}

void DrawList::Publish(Snapshot& snapshot, Jobs& jobs) {
    PN_PROFILE_ZONE();
    m_offsets.assign(m_partCount + 1, 0);
    for (u32 part = 0; part < m_partCount; ++part) {
        m_offsets[part + 1] = m_offsets[part] + m_parts[part].instances.size();
    }
    const std::size_t total = m_offsets.back();
    m_instances.resize(total);
    m_ids.resize(total);
    m_entries.resize(total);
    jobs.ParallelFor(m_partCount, [this](u32 part) {
        const std::size_t offset = m_offsets[part];
        std::ranges::copy(m_parts[part].instances, std::span{ m_instances }.subspan(offset).begin());
        std::ranges::copy(m_parts[part].ids, std::span{ m_ids }.subspan(offset).begin());
        std::ranges::transform(
            m_parts[part].entries, std::span{ m_entries }.subspan(offset).begin(), [offset](SortEntry entry) {
                entry.index += static_cast<u32>(offset);
                return entry;
            }
        );
    });
    RadixSort(m_entries, m_scratch);

    const std::size_t first = snapshot.instances.size();
    const std::size_t firstBatch = snapshot.batches.size();
    snapshot.instances.resize(first + total);
    snapshot.ids.resize(first + total); // instances added without IDs before these get zero
    const std::span<Instance> sorted = std::span{ snapshot.instances }.subspan(first);
    const std::span<StableId> sortedIds = std::span{ snapshot.ids }.subspan(first);
    jobs.ParallelFor(ChunkCount(total, GATHER_CHUNK), [&](u32 chunk) {
        const std::size_t begin = std::size_t{ chunk } * GATHER_CHUNK;
        const std::size_t end = std::min(total, begin + GATHER_CHUNK);
        for (std::size_t i = begin; i < end; ++i) {
            sorted[i] = m_instances[m_entries[i].index];
            sortedIds[i] = m_ids[m_entries[i].index];
        }
    });
    AppendBatches(m_entries, snapshot.batches);
    for (std::size_t i = firstBatch; i < snapshot.batches.size(); ++i) {
        snapshot.batches[i].first += static_cast<u32>(first);
    }
}

} // namespace core
