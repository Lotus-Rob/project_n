#include "gfx/shelf_packer.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace gfx {

std::optional<PackedSlot> ShelfPacker::Pack(u32 width, u32 height) {
    constexpr u32 SHELF_STEP = 4;
    const u32 paddedWidth = width + m_padding;
    const u32 shelfHeight = (height + m_padding + SHELF_STEP - 1) / SHELF_STEP * SHELF_STEP;
    if (paddedWidth > m_pageSize || shelfHeight > m_pageSize) {
        return std::nullopt;
    }
    const auto fits = [&](const Shelf& shelf) { return shelf.height == shelfHeight && shelf.used + paddedWidth <= m_pageSize; };
    auto shelf = std::ranges::find_if(m_shelves, fits);
    if (shelf == m_shelves.end()) {
        if (m_pages == 0 || m_bottom + shelfHeight > m_pageSize) {
            ++m_pages;
            m_bottom = 0;
        }
        m_shelves.push_back({ .page = m_pages - 1, .y = m_bottom, .height = shelfHeight, .used = 0 });
        m_bottom += shelfHeight;
        shelf = std::prev(m_shelves.end());
    }
    const PackedSlot slot{ .page = shelf->page, .x = shelf->used, .y = shelf->y };
    shelf->used += paddedWidth;
    return slot;
}

} // namespace gfx
