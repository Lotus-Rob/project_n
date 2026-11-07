#pragma once

/// @file
/// @ingroup core
/// Putting a tick's sprites in drawing order (design doc 0002 §8.5): culling to the view, sort keys
/// by layer, then depth, then pipeline; a stable radix sort of (key, index) pairs; and the batch
/// ranges the renderer draws, one per run of a pipeline. Filled in parts on the job system.

#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/stable_id.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

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

/// An axis-aligned rectangle of the world, in world units.
struct Rect {
    /// The top-left corner.
    Vec2 min{};
    /// The bottom-right corner.
    Vec2 max{};
};

/// A tick's sprites as they're published: added in any order, culled to the view, written to the
/// snapshot sorted and batched. It fills in parts, each from one job chunk at a time, so a publish
/// can pack its instances in parallel; the parts in order, each in the order its instances were
/// added, are the submission order. Its buffers keep their capacity from publish to publish.
class DrawList {
public:
    /// How far outside the view an instance's position may lie and still be drawn, in world units:
    /// more than half the diagonal of the largest moving sprite, plus a tick's movement.
    /// shortcut: one margin for every sprite, not each one's size; a sprite scaled past it pops at the
    /// view's edge, so compare its own extent if one ever does.
    static constexpr f32 CULL_MARGIN = 512.0F;

    /// Empties the list for a publish showing `view`, filled in `parts` parts (at least one).
    void Begin(Rect view, u32 parts = 1);

    /// Adds `instance`, drawing `entity` (zero for none), to part `part`, in `layer`, drawn with
    /// `pipeline`, unless its current position lies more than CULL_MARGIN outside the view. `footY`
    /// orders it within the world layer; elsewhere instances keep the submission order. Different
    /// parts may be added to at once.
    void Add(u32 part, Layer layer, Pipeline pipeline, const Instance& instance, f32 footY = 0.0F, StableId entity = {});

    /// Appends the instances in drawing order to `snapshot`'s, their IDs to its IDs and their
    /// batches to its batches, gathering in chunks on `jobs`: the bytes are the same whatever its
    /// worker count.
    void Publish(Snapshot& snapshot, Jobs& jobs);

private:
    static constexpr std::size_t CACHE_LINE_BYTES = 64;

#ifdef _MSC_VER
#pragma warning(push)
// C4324 (padded due to an alignment specifier): the padding is the point.
#pragma warning(disable : 4324)
#endif
    // On its own cache line: chunks push to their parts at once, and the vectors' ends change each
    // time. Worth about 13% of a publish at 30 workers.
    struct alignas(CACHE_LINE_BYTES) Part {
        // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
        // for members whose default constructor is defaulted.
        std::vector<Instance> instances{};
        std::vector<SortEntry> entries{}; // indices into this part's instances
        std::vector<StableId> ids{};      // parallel to instances
        // NOLINTEND(readability-redundant-member-init)
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    Rect m_view{};
    u32 m_partCount = 0;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer for
    // members whose default constructor is defaulted.
    std::vector<Part> m_parts{}; // grows to the most parts asked for; only the first m_partCount are in use
    std::vector<std::size_t> m_offsets{};
    std::vector<Instance> m_instances{};
    std::vector<StableId> m_ids{};
    std::vector<SortEntry> m_entries{};
    std::vector<SortEntry> m_scratch{};
    // NOLINTEND(readability-redundant-member-init)
};

} // namespace core
