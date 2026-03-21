/*
 * Copyright (c) 2018-2020, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2025, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/CharacterTypes.h>
#include <AK/Utf8View.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Position.h>
#include <LibWeb/Layout/Box.h>
#include <LibWeb/Layout/LineBox.h>
#include <LibWeb/Layout/Node.h>
#include <LibWeb/Layout/TextNode.h>

namespace Web::Layout {

CSSPixels LineBox::width() const
{
    if (m_writing_mode != CSS::WritingMode::HorizontalTb)
        return m_block_length;
    return m_inline_length;
}

CSSPixels LineBox::height() const
{
    if (m_writing_mode != CSS::WritingMode::HorizontalTb)
        return m_inline_length;
    return m_block_length;
}

CSSPixels LineBox::bottom() const
{
    if (m_writing_mode != CSS::WritingMode::HorizontalTb)
        return m_inline_length;
    return m_bottom;
}

void LineBox::add_fragment(Node const& layout_node, size_t start, size_t length, CSSPixels leading_size,
    CSSPixels trailing_size, CSSPixels leading_margin, CSSPixels trailing_margin, CSSPixels content_width,
    CSSPixels content_height, CSSPixels border_box_top, CSSPixels border_box_bottom, RefPtr<Gfx::GlyphRun> glyph_run)
{
    bool text_align_is_justify = layout_node.computed_values().text_align() == CSS::TextAlign::Justify;
    if (glyph_run && !text_align_is_justify && !m_fragments.is_empty()
        && &m_fragments.last().layout_node() == &layout_node
        && &m_fragments.last().m_glyph_run->font() == &glyph_run->font()
        && m_fragments.last().start() + m_fragments.last().length_in_code_units() == start) {
        // The fragment we're adding is from the last Layout::Node on the line.
        // Expand the last fragment instead of adding a new one with the same Layout::Node.
        m_fragments.last().m_length_in_code_units += length;
        m_fragments.last().append_glyph_run(glyph_run, content_width);
    } else {
        CSSPixels inline_offset = leading_margin + leading_size + m_inline_length;
        CSSPixels block_offset = 0;
        m_fragments.append(LineBoxFragment { layout_node, start, length, inline_offset, block_offset, content_width,
            content_height, border_box_top, m_direction, m_writing_mode, move(glyph_run) });
    }
    m_inline_length += leading_margin + leading_size + content_width + trailing_size + trailing_margin;
    m_block_length = max(m_block_length, content_height + border_box_top + border_box_bottom);
}

void LineBox::add_static_position_marker(Box const& box)
{
    m_static_position_markers.append(StaticPositionMarker {
        .box = &box,
        .inline_offset = m_inline_length,
        .block_offset = 0,
        .writing_mode = m_writing_mode,
    });
}

void LineBox::clamp_static_position_markers_to_inline_length()
{
    for (auto& marker : m_static_position_markers) {
        if (marker.inline_offset > m_inline_length)
            marker.inline_offset = m_inline_length;
    }
}

CSSPixels LineBox::calculate_or_trim_trailing_whitespace(RemoveTrailingWhitespace should_remove)
{
    auto should_trim = [](LineBoxFragment* fragment) {
        auto white_space_collapse = fragment->layout_node().computed_values().white_space_collapse();

        return white_space_collapse == CSS::WhiteSpaceCollapse::Collapse || white_space_collapse == CSS::WhiteSpaceCollapse::PreserveBreaks;
    };

    CSSPixels whitespace_width = 0;
    CSSPixels trailing_whitespace_width = 0;
    LineBoxFragment* last_fragment = nullptr;
    size_t fragment_index = m_fragments.size();
    for (;;) {
        if (fragment_index == 0)
            return whitespace_width;

        last_fragment = &m_fragments[--fragment_index];
        if (auto const* dom_node = last_fragment->layout_node().dom_node()) {
            auto cursor_position = dom_node->document().cursor_position();
            if (cursor_position && cursor_position->node() == dom_node)
                return whitespace_width;
        }
        if (!should_trim(last_fragment))
            return whitespace_width;
        if (!last_fragment->is_justifiable_whitespace())
            break;

        whitespace_width += last_fragment->inline_length();
        trailing_whitespace_width += last_fragment->inline_length();
        if (should_remove == RemoveTrailingWhitespace::Yes) {
            m_inline_length -= last_fragment->inline_length();
            m_fragments.remove(fragment_index);
            clamp_static_position_markers_to_inline_length();
        }
    }

    auto last_text = last_fragment->text();
    if (last_text.is_empty()) {
        // No text to trim, but we may have removed whitespace-only fragments.
        if (should_remove == RemoveTrailingWhitespace::Yes && trailing_whitespace_width > 0)
            last_fragment->set_has_trailing_whitespace(true);
        return whitespace_width;
    }

    // Trim trailing whitespace characters from the last fragment.
    size_t last_text_length = last_text.length_in_code_units();
    while (last_text_length) {
        auto last_character = last_text.code_unit_at(--last_text_length);
        if (!is_ascii_space(last_character))
            break;

        auto const& font = last_fragment->glyph_run() ? last_fragment->glyph_run()->font() : last_fragment->layout_node().first_available_font();
        CSSPixels last_character_width = CSSPixels(font.glyph_width(last_character)) + last_fragment->layout_node().computed_values().letter_spacing();
        whitespace_width += last_character_width;
        trailing_whitespace_width += last_character_width;
        if (should_remove == RemoveTrailingWhitespace::Yes) {
            --last_fragment->m_length_in_code_units;
            last_fragment->set_inline_length(last_fragment->inline_length() - last_character_width);
            m_inline_length -= last_character_width;
            clamp_static_position_markers_to_inline_length();
        }
    }

    // Track trimmed whitespace for selection purposes, but don't overwrite a value
    // that was already set during line breaking (for wrapped lines).
    if (should_remove == RemoveTrailingWhitespace::Yes
        && (trailing_whitespace_width > 0 || !last_fragment->has_trailing_whitespace())) {
        last_fragment->set_has_trailing_whitespace(trailing_whitespace_width > 0);
    }

    return whitespace_width;
}

CSSPixels LineBox::get_trailing_whitespace_width() const
{
    return const_cast<LineBox&>(*this).calculate_or_trim_trailing_whitespace(RemoveTrailingWhitespace::No);
}

void LineBox::trim_trailing_whitespace()
{
    calculate_or_trim_trailing_whitespace(RemoveTrailingWhitespace::Yes);
}

bool LineBox::is_empty_or_ends_in_whitespace() const
{
    if (m_fragments.is_empty())
        return true;

    return m_fragments.last().ends_in_whitespace();
}

void LineBox::reorder_fragments(Vector<size_t> const& visual_order)
{
    if (visual_order.size() != m_fragments.size())
        return;

    // Check if any actual reordering is needed to avoid corrupting positions.
    bool needs_reorder = false;
    for (size_t i = 0; i < visual_order.size(); ++i) {
        if (visual_order[i] != i) {
            needs_reorder = true;
            break;
        }
    }
    if (!needs_reorder)
        return;

    // Compute the slot width and leading offset for each fragment in logical order.
    // slot_width[i] = the full inline space occupied by fragment i (leading + content + trailing).
    // leading[i] = leading_margin + leading_size = fragment.inline_offset() - running_before_i.
    size_t n = m_fragments.size();
    Vector<CSSPixels> slot_widths;
    Vector<CSSPixels> leading_offsets;
    slot_widths.resize(n);
    leading_offsets.resize(n);
    {
        CSSPixels running = 0;
        for (size_t i = 0; i < n; ++i) {
            leading_offsets[i] = m_fragments[i].inline_offset() - running;
            // Slot width for all but the last: distance to next fragment's slot start.
            // For the last: remaining space up to total m_inline_length.
            if (i + 1 < n)
                slot_widths[i] = (m_fragments[i + 1].inline_offset() - leading_offsets[i + 1]) - running;
            else
                slot_widths[i] = m_inline_length - running;
            running += slot_widths[i];
        }
    }

    Vector<LineBoxFragment> reordered_fragments;
    reordered_fragments.ensure_capacity(n);
    CSSPixels inline_position = 0;
    for (auto fragment_index : visual_order) {
        auto& fragment = m_fragments[fragment_index];
        fragment.set_inline_offset(inline_position + leading_offsets[fragment_index]);
        inline_position += slot_widths[fragment_index];
        reordered_fragments.append(move(fragment));
    }
    m_fragments = move(reordered_fragments);
}

struct GlyphRange {
    size_t glyph_start { 0 };
    size_t glyph_count { 0 };
    float width { 0 };
};

// Given a GlyphRun and a code-unit range [cu_start, cu_start+cu_length), find the
// corresponding glyph index range and total pixel width.
static GlyphRange find_glyph_range_for_code_units(Gfx::GlyphRun const& glyph_run, size_t cu_start, size_t cu_length)
{
    auto const& glyphs = glyph_run.glyphs();
    size_t cumulative_cu = 0;
    GlyphRange result;
    size_t cu_end = cu_start + cu_length;

    for (size_t i = 0; i < glyphs.size(); ++i) {
        auto glyph_cu = glyphs[i].length_in_code_units;
        // Check if this glyph's code unit range overlaps [cu_start, cu_end).
        if (cumulative_cu < cu_end && cumulative_cu + glyph_cu > cu_start) {
            if (result.glyph_count == 0)
                result.glyph_start = i;
            ++result.glyph_count;
            result.width += glyphs[i].glyph_width;
        }
        cumulative_cu += glyph_cu;
        if (cumulative_cu >= cu_end)
            break;
    }

    return result;
}

void LineBox::reorder_and_split_fragments(Vector<BidiSubFragment> const& sub_fragments)
{
    if (sub_fragments.is_empty())
        return;

    // Check if splitting is actually needed: all sub_fragments cover complete original fragments
    // in potentially different order. If so, fall back to simple reorder for efficiency.
    bool needs_split = false;
    for (auto const& sub : sub_fragments) {
        if (sub.length_in_code_units == 0)
            continue; // atomic inline — always whole fragment
        auto const& orig = m_fragments[sub.fragment_index];
        if (sub.start_in_fragment != 0 || sub.length_in_code_units != orig.length_in_code_units()) {
            needs_split = true;
            break;
        }
    }

    if (!needs_split) {
        // Check if any actual reordering is needed to avoid corrupting positions.
        bool needs_reorder = false;
        for (size_t i = 0; i < sub_fragments.size(); ++i) {
            if (sub_fragments[i].fragment_index != i) {
                needs_reorder = true;
                break;
            }
        }
        if (!needs_reorder)
            return;

        // Compute slot widths and leading offsets for each fragment in logical order.
        size_t n = m_fragments.size();
        Vector<CSSPixels> slot_widths;
        Vector<CSSPixels> leading_offsets;
        slot_widths.resize(n);
        leading_offsets.resize(n);
        {
            CSSPixels running = 0;
            for (size_t i = 0; i < n; ++i) {
                leading_offsets[i] = m_fragments[i].inline_offset() - running;
                if (i + 1 < n)
                    slot_widths[i] = (m_fragments[i + 1].inline_offset() - leading_offsets[i + 1]) - running;
                else
                    slot_widths[i] = m_inline_length - running;
                running += slot_widths[i];
            }
        }

        // Simple reorder — no splitting needed.
        Vector<LineBoxFragment> reordered;
        reordered.ensure_capacity(sub_fragments.size());
        CSSPixels inline_position = 0;
        for (auto const& sub : sub_fragments) {
            auto& frag = m_fragments[sub.fragment_index];
            frag.set_inline_offset(inline_position + leading_offsets[sub.fragment_index]);
            inline_position += slot_widths[sub.fragment_index];
            reordered.append(move(frag));
        }
        m_fragments = move(reordered);
        return;
    }

    // Splitting is needed: build new fragments, slicing GlyphRuns at code-unit boundaries.
    Vector<LineBoxFragment> new_fragments;
    new_fragments.ensure_capacity(sub_fragments.size());
    CSSPixels inline_position = 0;

    for (auto const& sub : sub_fragments) {
        auto const& orig = m_fragments[sub.fragment_index];

        bool is_whole_fragment = (sub.length_in_code_units == 0)
            || (sub.start_in_fragment == 0 && sub.length_in_code_units == orig.length_in_code_units());

        if (is_whole_fragment) {
            // No split needed for this entry.
            LineBoxFragment frag = orig;
            frag.set_inline_offset(inline_position);
            inline_position += frag.inline_length();
            new_fragments.append(move(frag));
            continue;
        }

        // Split: create a sub-fragment by slicing the GlyphRun.
        RefPtr<Gfx::GlyphRun> sub_glyph_run;
        CSSPixels sub_width = 0;

        if (orig.glyph_run() && !orig.glyph_run()->glyphs().is_empty()) {
            auto range = find_glyph_range_for_code_units(
                *orig.glyph_run(), sub.start_in_fragment, sub.length_in_code_units);
            if (range.glyph_count > 0) {
                sub_glyph_run = orig.glyph_run()->slice(range.glyph_start, range.glyph_count);
                sub_width = CSSPixels::nearest_value_for(range.width);
            }
        }

        new_fragments.append(LineBoxFragment {
            orig.layout_node(),
            orig.start() + sub.start_in_fragment,
            sub.length_in_code_units,
            inline_position,
            orig.block_offset(),
            sub_width,
            orig.block_length(),
            orig.border_box_top(),
            m_direction,
            m_writing_mode,
            move(sub_glyph_run),
        });
        inline_position += sub_width;
    }

    m_fragments = move(new_fragments);
}

}
