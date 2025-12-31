/*
 * Itbaa (اطبع) - HTML to PDF Conversion Library
 * Copyright (c) 2025, sudorw <ahmedrowaihi@sudorw.com>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGfx/Forward.h>
#include <LibWeb/Painting/DisplayList.h>
#include <LibWeb/Painting/DisplayListCommand.h>

class SkCanvas;

namespace Itbaa {

class DisplayListPlayerPDF {
public:
    DisplayListPlayerPDF(SkCanvas& canvas);
    ~DisplayListPlayerPDF() = default;

    void execute(Web::Painting::DisplayList&);

private:
    SkCanvas& canvas() { return m_canvas; }

    void draw_glyph_run(Web::Painting::DrawGlyphRun const&);
    void fill_rect(Web::Painting::FillRect const&);
    void draw_scaled_immutable_bitmap(Web::Painting::DrawScaledImmutableBitmap const&);
    void add_clip_rect(Web::Painting::AddClipRect const&);
    void save();
    void save_layer();
    void restore();
    void translate(Web::Painting::Translate const&);
    void push_stacking_context(Web::Painting::PushStackingContext const&);
    void pop_stacking_context();
    void fill_rect_with_rounded_corners(Web::Painting::FillRectWithRoundedCorners const&);
    void fill_path(Web::Painting::FillPath const&);
    void stroke_path(Web::Painting::StrokePath const&);
    void draw_ellipse(Web::Painting::DrawEllipse const&);
    void fill_ellipse(Web::Painting::FillEllipse const&);
    void draw_line(Web::Painting::DrawLine const&);
    void draw_rect(Web::Painting::DrawRect const&);
    void add_rounded_rect_clip(Web::Painting::AddRoundedRectClip const&);
    void apply_opacity(Web::Painting::ApplyOpacity const&);
    void apply_transform(Web::Painting::ApplyTransform const&);

    SkCanvas& m_canvas;
};

}
