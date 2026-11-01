#pragma once

/// @file
/// @ingroup core
/// Putting a tick's sprites in drawing order (design doc 0002 §8.5): sort keys by layer, then
/// depth, then pipeline; a stable radix sort of (key, index) pairs; and the batch ranges the
/// renderer draws, one per run of a pipeline.

#include "core/frame.hpp"
#include "core/types.hpp"

#include <span>
#include <vector>

namespace core {

/// The drawing layers, back to front.
enum class Layer : u8 {
    Ground,  ///< The ground under everything.
    Decals,  ///< Flat on the ground: shadows, marks.
    World,   ///< Props and units, sorted by foot y so anything can stand behind anything.
    Effects, ///< Light over the world (additive orbs, glows), where order doesn't matter.
};

/// A sort key: layer, then depth, then pipeline, high bits first. The low 16 bits are kept for the
/// texture binding (shortcut: one texture array per scene, so none yet; add it with the second binding).
using SortKey = u64;

/// The key of an instance in `layer` at `depth` (QuantiseDepth() in the world layer; 0 elsewhere,
/// where the stable sort keeps submission order), drawn with `pipeline`.
[[nodiscard]] constexpr SortKey MakeSortKey(Layer layer, u32 depth, Pipeline pipeline) {
    constexpr u32 LAYER_SHIFT = 56;
    constexpr u32 DEPTH_SHIFT = 24;
    constexpr u32 PIPELINE_SHIFT = 16;
    return (SortKey{ static_cast<u8>(layer) } << LAYER_SHIFT) | (SortKey{ depth } << DEPTH_SHIFT) |
           (SortKey{ static_cast<u8>(pipeline) } << PIPELINE_SHIFT);
}

/// The pipeline a key was made with.
[[nodiscard]] constexpr Pipeline PipelineOf(SortKey key) {
    constexpr u32 PIPELINE_SHIFT = 16;
    constexpr SortKey PIPELINE_MASK = 0xFF;
    return static_cast<Pipeline>((key >> PIPELINE_SHIFT) & PIPELINE_MASK);
}

/// A foot y as a depth: sixteenths of a world unit below `viewTop`, clamped to u32's range, so
/// lower on the screen sorts later and everything above the view sorts first.
[[nodiscard]] u32 QuantiseDepth(f32 footY, f32 viewTop);

/// One instance to sort: its key and its place in submission order.
struct SortEntry {
    /// Where it sorts.
    SortKey key = 0;
    /// Its index among the instances as submitted.
    u32 index = 0;
};

/// Sorts `entries` by key, keeping equal keys in their order (a least-significant-digit radix
/// sort, a byte per pass; passes where every key has the same byte are skipped). `scratch` is
/// resized to match and its contents are lost.
void RadixSort(std::vector<SortEntry>& entries, std::vector<SortEntry>& scratch);

/// Appends to `batches` one Batch per run of `sorted` with the same pipeline.
void AppendBatches(std::span<const SortEntry> sorted, std::vector<Batch>& batches);

/// A tick's sprites as they're published: added in any order, written to the snapshot sorted and
/// batched. Its buffers keep their capacity from publish to publish.
class DrawList {
public:
    /// Empties the list for a publish whose view starts at world y `viewTop`.
    void Begin(f32 viewTop);

    /// Adds `instance` in `layer`, drawn with `pipeline`. `footY` orders it within the world layer;
    /// elsewhere instances keep the order they're added in.
    void Add(Layer layer, Pipeline pipeline, const Instance& instance, f32 footY = 0.0F);

    /// Appends the instances in drawing order to `snapshot`'s, and their batches to its batches.
    void Publish(Snapshot& snapshot);

private:
    f32 m_viewTop = 0.0F;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer for
    // members whose default constructor is defaulted.
    std::vector<Instance> m_instances{};
    std::vector<SortEntry> m_entries{};
    std::vector<SortEntry> m_scratch{};
    // NOLINTEND(readability-redundant-member-init)
};

} // namespace core
