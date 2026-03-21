/*
 * Copyright (c) 2025, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Vector.h>
#include <LibUnicode/CharacterTypes.h>
#include <LibWeb/CSS/ComputedValues.h>
#include <LibWeb/Forward.h>

namespace Web::Layout {

// Represents a sub-range of a line box fragment after character-level bidi splitting.
struct BidiSubFragment {
    size_t fragment_index { 0 };
    size_t start_in_fragment { 0 };    // code unit offset within the original fragment's text
    size_t length_in_code_units { 0 }; // 0 means atomic inline (whole fragment, no split)
};

// Represents an element in the bidi paragraph for analysis.
// Each element corresponds to a single character (or atomic inline) for character-level analysis.
struct BidiRun {
    size_t fragment_index { 0 };
    size_t start_in_fragment { 0 };    // code unit offset within the fragment
    size_t length_in_code_units { 1 }; // 0 = atomic inline or control marker
    u8 embedding_level { 0 };
    Unicode::BidiClass original_class { Unicode::BidiClass::LeftToRight };
    Unicode::BidiClass resolved_class { Unicode::BidiClass::LeftToRight };
    bool is_isolate_initiator { false };
    bool is_isolate_terminator { false };
    bool is_control { false }; // true = embedding/isolate control marker, not a real character
};

// Implements the Unicode Bidirectional Algorithm (UAX#9) at the paragraph level.
// This class analyzes a sequence of inline-level content at character granularity
// and computes embedding levels for proper visual reordering of fragments.
//
// Reference: https://www.unicode.org/reports/tr9/
class BidiParagraph {
public:
    BidiParagraph(CSS::Direction paragraph_direction, CSS::UnicodeBidi unicode_bidi);

    // Add a text fragment to the paragraph for bidi analysis.
    // Processes the text character by character for proper UAX#9 compliance.
    void add_fragment(size_t fragment_index, Utf16View text, CSS::Direction direction, CSS::UnicodeBidi unicode_bidi);

    // Add an atomic inline (replaced element, inline-block, etc.)
    // These are treated as neutral characters for bidi purposes.
    void add_atomic_inline(size_t fragment_index, CSS::Direction direction, CSS::UnicodeBidi unicode_bidi);

    // Run the UAX#9 algorithm and compute embedding levels for all runs.
    void resolve_levels();

    // Returns the visual order of sub-fragments after bidi reordering.
    // Adjacent characters from the same fragment that remain contiguous are merged.
    Vector<BidiSubFragment> reordered_sub_fragments() const;

    // Debug: dump all runs with their properties
    void dump_runs() const;

private:
    // UAX#9 Rule P2/P3: Determine paragraph embedding level
    u8 determine_paragraph_level() const;

    // UAX#9 Rules X1-X10: Resolve explicit embedding levels
    void resolve_explicit_embedding_levels();

    // UAX#9 Rules W1-W7: Resolve weak types
    void resolve_weak_types();

    // UAX#9 Rules N0-N2: Resolve neutral and isolate types
    void resolve_neutral_types();

    // UAX#9 Rules I1-I2: Resolve implicit levels
    void resolve_implicit_levels();

    // UAX#9 Rule L1: Reset levels for line-end whitespace
    void reset_levels_for_line_end_whitespace();

    // UAX#9 Rules L2: Reorder resolved levels, returns run indices in visual order
    Vector<size_t> reorder_runs() const;

    // The base paragraph direction (from containing block's CSS direction)
    CSS::Direction m_paragraph_direction;
    CSS::UnicodeBidi m_paragraph_unicode_bidi;
    u8 m_paragraph_embedding_level { 0 };

    // All runs in logical order (one per character for text, one for atomic inlines)
    Vector<BidiRun> m_runs;

    // Stack for explicit embedding level computation
    struct DirectionalStatus {
        u8 embedding_level;
        CSS::Direction direction;
        bool is_override;
        bool is_isolate;
    };
    Vector<DirectionalStatus> m_directional_status_stack;

    // Maximum embedding depth per UAX#9
    static constexpr u8 MAX_DEPTH = 125;
};

}
