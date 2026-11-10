#pragma once

/// @file
/// @ingroup gfx
/// Packing glyphs onto the glyph cache's pages (design doc 0002.4.2 §3; the distance fields of the phase 6 chunk 3
/// spec). Pure, tested without a device.

#include "core/types.hpp"

#include <optional>
#include <vector>

namespace gfx {

/// Where a packed rectangle went: its page and its top-left corner on it, in texels.
struct PackedSlot {
    /// The page, counted from 0.
    u32 page = 0;
    /// Its left edge.
    u32 x = 0;
    /// Its top edge.
    u32 y = 0;

    /// The same slot.
    friend bool operator==(const PackedSlot&, const PackedSlot&) = default;
};

/// Packs rectangles onto square pages in shelves: rows whose height is a rectangle's and its padding rounded up to a
/// multiple of 4, each rectangle with its padding right and below it. A new shelf opens below the last; a new page when
/// the last page has no room for it. Nothing is ever removed.
class ShelfPacker {
public:
    /// Packs onto pages `pageSize` texels wide and high, keeping `padding` texels free right of and below each
    /// rectangle, so a sample near one never reads its neighbour.
    ShelfPacker(u32 pageSize, u32 padding)
      : m_pageSize{ pageSize }
      , m_padding{ padding } {
    }

    /// Finds room for a rectangle `width` by `height` texels, opening a shelf or a page if it must; none when, with its
    /// padding, it is larger than a page.
    [[nodiscard]] std::optional<PackedSlot> Pack(u32 width, u32 height);

    /// How many pages the packed rectangles use.
    [[nodiscard]] u32 Pages() const {
        return m_pages;
    }

private:
    struct Shelf {
        u32 page = 0;
        u32 y = 0;
        u32 height = 0;
        u32 used = 0; // texels from its left edge
    };

    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<Shelf> m_shelves{};
    u32 m_pageSize = 0;
    u32 m_padding = 0;
    u32 m_pages = 0;
    u32 m_bottom = 0; // the last page's first texel row below its shelves
};

} // namespace gfx
