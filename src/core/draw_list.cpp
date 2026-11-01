#include "core/draw_list.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"

namespace core {

namespace {

constexpr f64 DEPTH_STEPS_PER_UNIT = 16.0;
constexpr std::size_t RADIX = 256;
constexpr u32 BITS_PER_PASS = 8;
constexpr u32 KEY_BITS = 64;

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
    std::array<std::size_t, RADIX> offsets{};
    for (u32 shift = 0; shift < KEY_BITS; shift += BITS_PER_PASS) {
        offsets.fill(0);
        for (const SortEntry& entry : entries) {
            ++offsets.at(Digit(entry.key, shift));
        }
        if (std::ranges::contains(offsets, entries.size())) {
            continue; // every key has this byte: the pass would copy them in order
        }
        std::exclusive_scan(offsets.begin(), offsets.end(), offsets.begin(), std::size_t{ 0 });
        for (const SortEntry& entry : entries) {
            scratch[offsets.at(Digit(entry.key, shift))++] = entry;
        }
        entries.swap(scratch);
    }
}

void AppendBatches(std::span<const SortEntry> sorted, std::vector<Batch>& batches) {
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const Pipeline pipeline = PipelineOf(sorted[i].key);
        if (i == 0 || batches.back().pipeline != pipeline) {
            batches.push_back({ .first = static_cast<u32>(i), .count = 0, .pipeline = pipeline });
        }
        ++batches.back().count;
    }
}

void DrawList::Begin(f32 viewTop) {
    m_viewTop = viewTop;
    m_instances.clear();
    m_entries.clear();
}

void DrawList::Add(Layer layer, Pipeline pipeline, const Instance& instance, f32 footY) {
    const u32 depth = layer == Layer::World ? QuantiseDepth(footY, m_viewTop) : 0;
    m_entries.push_back({ .key = MakeSortKey(layer, depth, pipeline), .index = static_cast<u32>(m_instances.size()) });
    m_instances.push_back(instance);
}

void DrawList::Publish(Snapshot& snapshot) {
    PN_PROFILE_ZONE();
    RadixSort(m_entries, m_scratch);
    const auto first = static_cast<u32>(snapshot.instances.size());
    const std::size_t firstBatch = snapshot.batches.size();
    snapshot.instances.reserve(snapshot.instances.size() + m_entries.size());
    for (const SortEntry& entry : m_entries) {
        snapshot.instances.push_back(m_instances[entry.index]);
    }
    AppendBatches(m_entries, snapshot.batches);
    for (std::size_t i = firstBatch; i < snapshot.batches.size(); ++i) {
        snapshot.batches[i].first += first;
    }
}

} // namespace core
