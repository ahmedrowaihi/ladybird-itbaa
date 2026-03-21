/*
 * Copyright (c) 2025, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Layout/BidiParagraph.h>

namespace Web::Layout {

static bool is_strong_ltr(Unicode::BidiClass bc)
{
    return bc == Unicode::BidiClass::LeftToRight;
}

static bool is_strong_rtl(Unicode::BidiClass bc)
{
    return bc == Unicode::BidiClass::RightToLeft || bc == Unicode::BidiClass::RightToLeftArabic;
}

static bool is_neutral(Unicode::BidiClass bc)
{
    return bc == Unicode::BidiClass::OtherNeutral
        || bc == Unicode::BidiClass::WhiteSpaceNeutral
        || bc == Unicode::BidiClass::SegmentSeparator
        || bc == Unicode::BidiClass::BlockSeparator;
}

BidiParagraph::BidiParagraph(CSS::Direction paragraph_direction, CSS::UnicodeBidi unicode_bidi)
    : m_paragraph_direction(paragraph_direction)
    , m_paragraph_unicode_bidi(unicode_bidi)
{
    m_paragraph_embedding_level = (paragraph_direction == CSS::Direction::Rtl) ? 1 : 0;
}

void BidiParagraph::add_fragment(size_t fragment_index, Utf16View text, CSS::Direction direction, CSS::UnicodeBidi unicode_bidi)
{
    BidiRun run;
    run.fragment_index = fragment_index;
    run.start_in_fragment = 0;
    run.length_in_code_units = text.length_in_code_units();

    // Determine the bidi class: use the first strong character, or the paragraph direction class as default.
    run.original_class = (m_paragraph_direction == CSS::Direction::Rtl)
        ? Unicode::BidiClass::RightToLeft
        : Unicode::BidiClass::LeftToRight;

    for (size_t i = 0; i < text.length_in_code_units();) {
        auto code_point = text.code_point_at(i);
        auto bc = Unicode::bidirectional_class(code_point);

        if (is_strong_ltr(bc) || is_strong_rtl(bc)) {
            run.original_class = bc;
            break;
        }
        i += (code_point > 0xFFFF) ? 2u : 1u;
    }

    run.resolved_class = run.original_class;

    // NOTE: For text fragments, unicode-bidi: isolate should NOT create isolate initiators.
    // The isolate boundary is at the element level, not the fragment level.
    // Text fragments keep their intrinsic bidi class (AL, R, L) from the actual text content.
    // Only Embed, BidiOverride, IsolateOverride, and Plaintext should override the intrinsic class.
    if (unicode_bidi == CSS::UnicodeBidi::Embed) {
        // Wrap the text run in a prefix (LRE/RLE) + suffix (PDF) pair so that the text run
        // itself gets the INNER embedding level, not the outer level.
        auto prefix_class = (direction == CSS::Direction::Ltr)
            ? Unicode::BidiClass::LeftToRightEmbedding
            : Unicode::BidiClass::RightToLeftEmbedding;
        BidiRun prefix;
        prefix.fragment_index = fragment_index;
        prefix.original_class = prefix_class;
        prefix.resolved_class = prefix_class;
        prefix.is_control = true;
        m_runs.append(move(prefix));
        m_runs.append(move(run));
        BidiRun suffix;
        suffix.fragment_index = fragment_index;
        suffix.original_class = Unicode::BidiClass::PopDirectionalFormat;
        suffix.resolved_class = Unicode::BidiClass::PopDirectionalFormat;
        suffix.is_control = true;
        m_runs.append(move(suffix));
        return;
    } else if (unicode_bidi == CSS::UnicodeBidi::BidiOverride) {
        // Wrap the text run in a prefix (RLO/LRO) + suffix (PDF) pair so that the text run
        // itself gets the INNER embedding level and is covered by the override context.
        auto prefix_class = (direction == CSS::Direction::Ltr)
            ? Unicode::BidiClass::LeftToRightOverride
            : Unicode::BidiClass::RightToLeftOverride;
        BidiRun prefix;
        prefix.fragment_index = fragment_index;
        prefix.original_class = prefix_class;
        prefix.resolved_class = prefix_class;
        prefix.is_control = true;
        m_runs.append(move(prefix));
        m_runs.append(move(run));
        BidiRun suffix;
        suffix.fragment_index = fragment_index;
        suffix.original_class = Unicode::BidiClass::PopDirectionalFormat;
        suffix.resolved_class = Unicode::BidiClass::PopDirectionalFormat;
        suffix.is_control = true;
        m_runs.append(move(suffix));
        return;
    } else if (unicode_bidi == CSS::UnicodeBidi::IsolateOverride) {
        run.original_class = Unicode::BidiClass::FirstStrongIsolate;
        run.is_isolate_initiator = true;
    } else if (unicode_bidi == CSS::UnicodeBidi::Plaintext) {
        run.original_class = Unicode::BidiClass::FirstStrongIsolate;
        run.is_isolate_initiator = true;
    }

    m_runs.append(move(run));
}

void BidiParagraph::add_atomic_inline(size_t fragment_index, CSS::Direction direction, CSS::UnicodeBidi unicode_bidi)
{
    BidiRun run;
    run.fragment_index = fragment_index;
    run.start_in_fragment = 0;
    run.length_in_code_units = 0; // signals atomic inline
    run.original_class = Unicode::BidiClass::OtherNeutral;
    run.resolved_class = run.original_class;

    if (unicode_bidi == CSS::UnicodeBidi::Embed) {
        run.original_class = (direction == CSS::Direction::Ltr)
            ? Unicode::BidiClass::LeftToRightEmbedding
            : Unicode::BidiClass::RightToLeftEmbedding;
    } else if (unicode_bidi == CSS::UnicodeBidi::Isolate) {
        run.original_class = (direction == CSS::Direction::Ltr)
            ? Unicode::BidiClass::LeftToRightIsolate
            : Unicode::BidiClass::RightToLeftIsolate;
        run.is_isolate_initiator = true;
    }

    m_runs.append(move(run));
}

void BidiParagraph::resolve_levels()
{
    if (m_runs.is_empty())
        return;

    resolve_explicit_embedding_levels();
    resolve_weak_types();
    resolve_neutral_types();
    resolve_implicit_levels();
    reset_levels_for_line_end_whitespace();
}

u8 BidiParagraph::determine_paragraph_level() const
{
    if (m_paragraph_unicode_bidi == CSS::UnicodeBidi::Plaintext) {
        for (auto const& run : m_runs) {
            if (is_strong_ltr(run.original_class))
                return 0;
            if (is_strong_rtl(run.original_class))
                return 1;
        }
    }
    return (m_paragraph_direction == CSS::Direction::Rtl) ? 1 : 0;
}

void BidiParagraph::resolve_explicit_embedding_levels()
{
    m_directional_status_stack.clear();
    m_directional_status_stack.append({
        .embedding_level = m_paragraph_embedding_level,
        .direction = m_paragraph_direction,
        .is_override = false,
        .is_isolate = false,
    });

    u8 overflow_isolate_count = 0;
    u8 overflow_embedding_count = 0;
    u8 valid_isolate_count = 0;

    for (auto& run : m_runs) {
        auto const& current_status = m_directional_status_stack.last();
        auto bc = run.original_class;

        auto compute_next_odd_level = [](u8 level) -> u8 {
            return (level + 1) | 1;
        };
        auto compute_next_even_level = [](u8 level) -> u8 {
            return (level + 2) & ~1;
        };

        switch (bc) {
        case Unicode::BidiClass::RightToLeftEmbedding: {
            auto new_level = compute_next_odd_level(current_status.embedding_level);
            if (new_level <= MAX_DEPTH && overflow_isolate_count == 0 && overflow_embedding_count == 0) {
                m_directional_status_stack.append({
                    .embedding_level = new_level,
                    .direction = CSS::Direction::Rtl,
                    .is_override = false,
                    .is_isolate = false,
                });
            } else if (overflow_isolate_count == 0) {
                ++overflow_embedding_count;
            }
            run.embedding_level = current_status.embedding_level;
            break;
        }

        case Unicode::BidiClass::LeftToRightEmbedding: {
            auto new_level = compute_next_even_level(current_status.embedding_level);
            if (new_level <= MAX_DEPTH && overflow_isolate_count == 0 && overflow_embedding_count == 0) {
                m_directional_status_stack.append({
                    .embedding_level = new_level,
                    .direction = CSS::Direction::Ltr,
                    .is_override = false,
                    .is_isolate = false,
                });
            } else if (overflow_isolate_count == 0) {
                ++overflow_embedding_count;
            }
            run.embedding_level = current_status.embedding_level;
            break;
        }

        case Unicode::BidiClass::RightToLeftOverride: {
            auto new_level = compute_next_odd_level(current_status.embedding_level);
            if (new_level <= MAX_DEPTH && overflow_isolate_count == 0 && overflow_embedding_count == 0) {
                m_directional_status_stack.append({
                    .embedding_level = new_level,
                    .direction = CSS::Direction::Rtl,
                    .is_override = true,
                    .is_isolate = false,
                });
            } else if (overflow_isolate_count == 0) {
                ++overflow_embedding_count;
            }
            run.embedding_level = current_status.embedding_level;
            break;
        }

        case Unicode::BidiClass::LeftToRightOverride: {
            auto new_level = compute_next_even_level(current_status.embedding_level);
            if (new_level <= MAX_DEPTH && overflow_isolate_count == 0 && overflow_embedding_count == 0) {
                m_directional_status_stack.append({
                    .embedding_level = new_level,
                    .direction = CSS::Direction::Ltr,
                    .is_override = true,
                    .is_isolate = false,
                });
            } else if (overflow_isolate_count == 0) {
                ++overflow_embedding_count;
            }
            run.embedding_level = current_status.embedding_level;
            break;
        }

        case Unicode::BidiClass::RightToLeftIsolate: {
            run.embedding_level = current_status.embedding_level;
            if (current_status.is_override)
                run.resolved_class = (current_status.direction == CSS::Direction::Ltr)
                    ? Unicode::BidiClass::LeftToRight
                    : Unicode::BidiClass::RightToLeft;

            auto new_level = compute_next_odd_level(current_status.embedding_level);
            if (new_level <= MAX_DEPTH && overflow_isolate_count == 0 && overflow_embedding_count == 0) {
                ++valid_isolate_count;
                m_directional_status_stack.append({
                    .embedding_level = new_level,
                    .direction = CSS::Direction::Rtl,
                    .is_override = false,
                    .is_isolate = true,
                });
            } else {
                ++overflow_isolate_count;
            }
            break;
        }

        case Unicode::BidiClass::LeftToRightIsolate: {
            run.embedding_level = current_status.embedding_level;
            if (current_status.is_override)
                run.resolved_class = (current_status.direction == CSS::Direction::Ltr)
                    ? Unicode::BidiClass::LeftToRight
                    : Unicode::BidiClass::RightToLeft;

            auto new_level = compute_next_even_level(current_status.embedding_level);
            if (new_level <= MAX_DEPTH && overflow_isolate_count == 0 && overflow_embedding_count == 0) {
                ++valid_isolate_count;
                m_directional_status_stack.append({
                    .embedding_level = new_level,
                    .direction = CSS::Direction::Ltr,
                    .is_override = false,
                    .is_isolate = true,
                });
            } else {
                ++overflow_isolate_count;
            }
            break;
        }

        case Unicode::BidiClass::FirstStrongIsolate: {
            run.embedding_level = current_status.embedding_level;
            if (current_status.is_override)
                run.resolved_class = (current_status.direction == CSS::Direction::Ltr)
                    ? Unicode::BidiClass::LeftToRight
                    : Unicode::BidiClass::RightToLeft;

            u8 new_level;
            CSS::Direction new_direction;
            bool found_strong = false;
            size_t current_index = static_cast<size_t>(&run - m_runs.data());
            for (size_t j = current_index + 1; j < m_runs.size(); ++j) {
                if (is_strong_ltr(m_runs[j].original_class)) {
                    new_level = compute_next_even_level(current_status.embedding_level);
                    new_direction = CSS::Direction::Ltr;
                    found_strong = true;
                    break;
                }
                if (is_strong_rtl(m_runs[j].original_class)) {
                    new_level = compute_next_odd_level(current_status.embedding_level);
                    new_direction = CSS::Direction::Rtl;
                    found_strong = true;
                    break;
                }
            }
            if (!found_strong) {
                new_level = compute_next_even_level(current_status.embedding_level);
                new_direction = CSS::Direction::Ltr;
            }

            if (new_level <= MAX_DEPTH && overflow_isolate_count == 0 && overflow_embedding_count == 0) {
                ++valid_isolate_count;
                m_directional_status_stack.append({
                    .embedding_level = new_level,
                    .direction = new_direction,
                    .is_override = false,
                    .is_isolate = true,
                });
            } else {
                ++overflow_isolate_count;
            }
            break;
        }

        case Unicode::BidiClass::PopDirectionalFormat: {
            if (overflow_isolate_count > 0) {
            } else if (overflow_embedding_count > 0) {
                --overflow_embedding_count;
            } else if (!current_status.is_isolate && m_directional_status_stack.size() >= 2) {
                m_directional_status_stack.take_last();
            }
            run.embedding_level = m_directional_status_stack.last().embedding_level;
            break;
        }

        case Unicode::BidiClass::PopDirectionalIsolate: {
            run.is_isolate_terminator = true;
            if (overflow_isolate_count > 0) {
                --overflow_isolate_count;
            } else if (valid_isolate_count > 0) {
                overflow_embedding_count = 0;
                while (m_directional_status_stack.size() > 1 && !m_directional_status_stack.last().is_isolate) {
                    m_directional_status_stack.take_last();
                }
                if (m_directional_status_stack.size() > 1) {
                    m_directional_status_stack.take_last();
                }
                --valid_isolate_count;
            }
            run.embedding_level = m_directional_status_stack.last().embedding_level;
            if (m_directional_status_stack.last().is_override)
                run.resolved_class = (m_directional_status_stack.last().direction == CSS::Direction::Ltr)
                    ? Unicode::BidiClass::LeftToRight
                    : Unicode::BidiClass::RightToLeft;
            break;
        }

        case Unicode::BidiClass::BoundaryNeutral:
            run.embedding_level = current_status.embedding_level;
            break;

        default:
            run.embedding_level = current_status.embedding_level;
            if (current_status.is_override) {
                run.resolved_class = (current_status.direction == CSS::Direction::Ltr)
                    ? Unicode::BidiClass::LeftToRight
                    : Unicode::BidiClass::RightToLeft;
            }
            break;
        }
    }
}

void BidiParagraph::resolve_weak_types()
{
    Optional<Unicode::BidiClass> prev_strong_class;
    for (size_t i = 0; i < m_runs.size(); ++i) {
        auto& run = m_runs[i];

        // W3: Change all ALs to R.
        if (run.resolved_class == Unicode::BidiClass::RightToLeftArabic)
            run.resolved_class = Unicode::BidiClass::RightToLeft;

        if (is_strong_ltr(run.resolved_class) || is_strong_rtl(run.resolved_class)) {
            prev_strong_class = run.resolved_class;
            continue;
        }

        // W1: Non-spacing marks take the type of the preceding character.
        if (run.resolved_class == Unicode::BidiClass::DirNonSpacingMark) {
            if (i > 0) {
                run.resolved_class = m_runs[i - 1].resolved_class;
            } else {
                run.resolved_class = (run.embedding_level % 2 == 0)
                    ? Unicode::BidiClass::LeftToRight
                    : Unicode::BidiClass::RightToLeft;
            }
        }

        // W2: European numbers preceded by a strong Arabic character become Arabic numbers.
        if (run.resolved_class == Unicode::BidiClass::EuropeanNumber) {
            if (prev_strong_class.has_value() && prev_strong_class.value() == Unicode::BidiClass::RightToLeft) {
                run.resolved_class = Unicode::BidiClass::ArabicNumber;
            }
        }

        // W4/W5: Separators/terminators between like-typed numbers.
        if (run.resolved_class == Unicode::BidiClass::EuropeanNumberSeparator || run.resolved_class == Unicode::BidiClass::CommonNumberSeparator) {
            bool is_between_numbers = false;
            if (i > 0 && i + 1 < m_runs.size()) {
                auto prev_class = m_runs[i - 1].resolved_class;
                auto next_class = m_runs[i + 1].resolved_class;
                if ((prev_class == Unicode::BidiClass::EuropeanNumber && next_class == Unicode::BidiClass::EuropeanNumber)
                    || (prev_class == Unicode::BidiClass::ArabicNumber && next_class == Unicode::BidiClass::ArabicNumber && run.resolved_class == Unicode::BidiClass::CommonNumberSeparator)) {
                    is_between_numbers = true;
                    run.resolved_class = prev_class;
                }
            }
            if (!is_between_numbers) {
                // W6: Separators/terminators not adjacent to numbers become neutral.
                run.resolved_class = Unicode::BidiClass::OtherNeutral;
            }
        }

        // W5/W6: European number terminators.
        if (run.resolved_class == Unicode::BidiClass::EuropeanNumberTerminator) {
            bool adjacent_to_en = false;
            if (i > 0 && m_runs[i - 1].resolved_class == Unicode::BidiClass::EuropeanNumber) {
                adjacent_to_en = true;
            } else if (i + 1 < m_runs.size() && m_runs[i + 1].resolved_class == Unicode::BidiClass::EuropeanNumber) {
                adjacent_to_en = true;
            }
            if (adjacent_to_en) {
                run.resolved_class = Unicode::BidiClass::EuropeanNumber;
            } else {
                run.resolved_class = Unicode::BidiClass::OtherNeutral;
            }
        }
    }

    // W7: If the last strong type is L, change EN to L.
    if (prev_strong_class.has_value() && prev_strong_class.value() == Unicode::BidiClass::LeftToRight) {
        for (auto& run : m_runs) {
            if (run.resolved_class == Unicode::BidiClass::EuropeanNumber) {
                run.resolved_class = Unicode::BidiClass::LeftToRight;
            }
        }
    }
}

void BidiParagraph::resolve_neutral_types()
{
    for (size_t i = 0; i < m_runs.size(); ++i) {
        auto& run = m_runs[i];

        if (!is_neutral(run.resolved_class))
            continue;

        Optional<Unicode::BidiClass> prev_strong;
        for (ssize_t j = static_cast<ssize_t>(i) - 1; j >= 0; --j) {
            if (is_strong_ltr(m_runs[j].resolved_class) || is_strong_rtl(m_runs[j].resolved_class)
                || m_runs[j].resolved_class == Unicode::BidiClass::EuropeanNumber
                || m_runs[j].resolved_class == Unicode::BidiClass::ArabicNumber) {
                prev_strong = m_runs[j].resolved_class;
                break;
            }
        }

        Optional<Unicode::BidiClass> next_strong;
        for (size_t j = i + 1; j < m_runs.size(); ++j) {
            if (is_strong_ltr(m_runs[j].resolved_class) || is_strong_rtl(m_runs[j].resolved_class)
                || m_runs[j].resolved_class == Unicode::BidiClass::EuropeanNumber
                || m_runs[j].resolved_class == Unicode::BidiClass::ArabicNumber) {
                next_strong = m_runs[j].resolved_class;
                break;
            }
        }

        auto effective_prev = prev_strong.value_or(
            (run.embedding_level % 2 == 0) ? Unicode::BidiClass::LeftToRight : Unicode::BidiClass::RightToLeft);
        auto effective_next = next_strong.value_or(
            (run.embedding_level % 2 == 0) ? Unicode::BidiClass::LeftToRight : Unicode::BidiClass::RightToLeft);

        bool prev_is_ltr = is_strong_ltr(effective_prev) || effective_prev == Unicode::BidiClass::EuropeanNumber;
        bool next_is_ltr = is_strong_ltr(effective_next) || effective_next == Unicode::BidiClass::EuropeanNumber;
        bool prev_is_rtl = is_strong_rtl(effective_prev) || effective_prev == Unicode::BidiClass::ArabicNumber;
        bool next_is_rtl = is_strong_rtl(effective_next) || effective_next == Unicode::BidiClass::ArabicNumber;

        if (prev_is_ltr && next_is_ltr) {
            run.resolved_class = Unicode::BidiClass::LeftToRight;
        } else if (prev_is_rtl && next_is_rtl) {
            run.resolved_class = Unicode::BidiClass::RightToLeft;
        } else {
            // N2: embedding direction.
            run.resolved_class = (run.embedding_level % 2 == 0)
                ? Unicode::BidiClass::LeftToRight
                : Unicode::BidiClass::RightToLeft;
        }
    }
}

void BidiParagraph::resolve_implicit_levels()
{
    for (auto& run : m_runs) {
        // I1: For even (LTR) embedding levels:
        //   R or AL → level + 1
        //   AN or EN → level + 2
        if (run.embedding_level % 2 == 0) {
            if (run.resolved_class == Unicode::BidiClass::RightToLeft
                || run.resolved_class == Unicode::BidiClass::RightToLeftArabic) {
                run.embedding_level += 1;
            } else if (run.resolved_class == Unicode::BidiClass::ArabicNumber
                || run.resolved_class == Unicode::BidiClass::EuropeanNumber) {
                run.embedding_level += 2;
            }
        } else {
            // I2: For odd (RTL) embedding levels:
            //   L, EN, or AN → level + 1
            if (run.resolved_class == Unicode::BidiClass::LeftToRight
                || run.resolved_class == Unicode::BidiClass::EuropeanNumber
                || run.resolved_class == Unicode::BidiClass::ArabicNumber) {
                run.embedding_level += 1;
            }
        }
    }
}

void BidiParagraph::reset_levels_for_line_end_whitespace()
{
    // https://www.unicode.org/reports/tr9/#L1
    if (m_runs.is_empty())
        return;

    ssize_t i = static_cast<ssize_t>(m_runs.size()) - 1;
    while (i >= 0) {
        auto bc = m_runs[i].resolved_class;
        bool is_resettable = (bc == Unicode::BidiClass::WhiteSpaceNeutral
            || bc == Unicode::BidiClass::SegmentSeparator
            || bc == Unicode::BidiClass::BlockSeparator
            || bc == Unicode::BidiClass::LeftToRightIsolate
            || bc == Unicode::BidiClass::RightToLeftIsolate
            || bc == Unicode::BidiClass::FirstStrongIsolate
            || bc == Unicode::BidiClass::PopDirectionalIsolate);

        if (is_resettable) {
            m_runs[i].embedding_level = m_paragraph_embedding_level;
            --i;
        } else {
            break;
        }
    }
}

Vector<BidiSubFragment> BidiParagraph::reordered_sub_fragments() const
{
    // Get the visual order of runs using L2 reordering.
    auto run_order = reorder_runs();
    if (run_order.is_empty())
        return {};

    // Convert to BidiSubFragments — one per run, covering the full fragment.
    Vector<BidiSubFragment> result;
    result.ensure_capacity(run_order.size());
    for (auto run_index : run_order) {
        auto const& run = m_runs[run_index];
        result.unchecked_append({
            .fragment_index = run.fragment_index,
            .start_in_fragment = run.start_in_fragment,
            .length_in_code_units = run.length_in_code_units,
        });
    }
    return result;
}

Vector<size_t> BidiParagraph::reorder_runs() const
{
    if (m_runs.is_empty())
        return {};

    u8 max_level = m_paragraph_embedding_level;
    for (auto const& run : m_runs) {
        if (!run.is_control)
            max_level = max(max_level, run.embedding_level);
    }

    // Build run_order containing only non-control runs; control runs are formatting markers
    // that don't correspond to visible content and should not participate in reordering.
    Vector<size_t> run_order;
    run_order.ensure_capacity(m_runs.size());
    for (size_t i = 0; i < m_runs.size(); ++i) {
        if (!m_runs[i].is_control)
            run_order.unchecked_append(i);
    }

    // https://www.unicode.org/reports/tr9/#L2
    // From the highest level found in the text to the lowest odd level on each line,
    // reverse any contiguous sequence of characters that are at that level or higher.
    for (u8 level = max_level; level >= 1; --level) {
        size_t run_index = 0;
        while (run_index < run_order.size()) {
            if (m_runs[run_order[run_index]].embedding_level >= level) {
                size_t segment_start = run_index;
                while (run_index < run_order.size() && m_runs[run_order[run_index]].embedding_level >= level) {
                    ++run_index;
                }
                size_t segment_end = run_index;

                for (size_t offset = 0; offset < (segment_end - segment_start) / 2; ++offset) {
                    swap(run_order[segment_start + offset], run_order[segment_end - 1 - offset]);
                }
            } else {
                ++run_index;
            }
        }
    }

    return run_order;
}

void BidiParagraph::dump_runs() const
{
    dbgln("[BIDI] Runs after resolve_levels() - paragraph_level={}:", m_paragraph_embedding_level);
    for (size_t i = 0; i < m_runs.size(); ++i) {
        auto const& run = m_runs[i];
        dbgln("[BIDI]   Run[{}]: frag_idx={}, start={}, len={}, level={}, orig={}, resolved={}, control={}",
            i, run.fragment_index, run.start_in_fragment, run.length_in_code_units,
            run.embedding_level,
            Unicode::bidi_class_to_string_view(run.original_class),
            Unicode::bidi_class_to_string_view(run.resolved_class),
            run.is_control);
    }
}

}
