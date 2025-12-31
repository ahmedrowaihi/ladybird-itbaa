/*
 * Itbaa (اطبع) - HTML to PDF Conversion Library
 * Copyright (c) 2025, sudorw <ahmedrowaihi@sudorw.com>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define SK_SUPPORT_UNSPANNED_APIS

#include "DisplayListPlayerPDF.h"

#include <core/SkCanvas.h>
#include <core/SkFont.h>
#include <core/SkPaint.h>
#include <core/SkPath.h>
#include <core/SkRRect.h>
#include <effects/SkDashPathEffect.h>

#include <LibGfx/Font/Font.h>
#include <LibGfx/ImmutableBitmap.h>
#include <LibGfx/PathSkia.h>
#include <LibGfx/SkiaUtils.h>
#include <LibWeb/Painting/DisplayListCommand.h>

namespace Itbaa {

static SkRRect to_skia_rrect(auto const& rect, Web::Painting::CornerRadii const& corner_radii)
{
    SkRRect rrect;
    SkVector radii[4];
    radii[0].set(corner_radii.top_left.horizontal_radius, corner_radii.top_left.vertical_radius);
    radii[1].set(corner_radii.top_right.horizontal_radius, corner_radii.top_right.vertical_radius);
    radii[2].set(corner_radii.bottom_right.horizontal_radius, corner_radii.bottom_right.vertical_radius);
    radii[3].set(corner_radii.bottom_left.horizontal_radius, corner_radii.bottom_left.vertical_radius);
    rrect.setRectRadii(to_skia_rect(rect), radii);
    return rrect;
}

static SkM44 to_skia_matrix4x4(Gfx::FloatMatrix4x4 const& matrix)
{
    return SkM44(
        matrix.elements()[0][0], matrix.elements()[0][1], matrix.elements()[0][2], matrix.elements()[0][3],
        matrix.elements()[1][0], matrix.elements()[1][1], matrix.elements()[1][2], matrix.elements()[1][3],
        matrix.elements()[2][0], matrix.elements()[2][1], matrix.elements()[2][2], matrix.elements()[2][3],
        matrix.elements()[3][0], matrix.elements()[3][1], matrix.elements()[3][2], matrix.elements()[3][3]);
}

DisplayListPlayerPDF::DisplayListPlayerPDF(SkCanvas& canvas)
    : m_canvas(canvas)
{
}

void DisplayListPlayerPDF::execute(Web::Painting::DisplayList& display_list)
{
    using namespace Web::Painting;

    for (auto const& command_with_scroll : display_list.commands()) {
        auto const& command = command_with_scroll.command;

        command.visit(
            [&](DrawGlyphRun const& cmd) { draw_glyph_run(cmd); },
            [&](FillRect const& cmd) { fill_rect(cmd); },
            [&](DrawScaledImmutableBitmap const& cmd) { draw_scaled_immutable_bitmap(cmd); },
            [&](AddClipRect const& cmd) { add_clip_rect(cmd); },
            [&](Save const&) { save(); },
            [&](SaveLayer const&) { save_layer(); },
            [&](Restore const&) { restore(); },
            [&](Translate const& cmd) { translate(cmd); },
            [&](PushStackingContext const& cmd) { push_stacking_context(cmd); },
            [&](PopStackingContext const&) { pop_stacking_context(); },
            [&](FillRectWithRoundedCorners const& cmd) { fill_rect_with_rounded_corners(cmd); },
            [&](FillPath const& cmd) { fill_path(cmd); },
            [&](StrokePath const& cmd) { stroke_path(cmd); },
            [&](DrawEllipse const& cmd) { draw_ellipse(cmd); },
            [&](FillEllipse const& cmd) { fill_ellipse(cmd); },
            [&](DrawLine const& cmd) { draw_line(cmd); },
            [&](DrawRect const& cmd) { draw_rect(cmd); },
            [&](AddRoundedRectClip const& cmd) { add_rounded_rect_clip(cmd); },
            [&](ApplyOpacity const& cmd) { apply_opacity(cmd); },
            [&](ApplyTransform const& cmd) { apply_transform(cmd); },
            [&](auto const&) { /* Skip unsupported commands */ });
    }
}

void DisplayListPlayerPDF::draw_glyph_run(Web::Painting::DrawGlyphRun const& command)
{
    auto const& gfx_font = command.glyph_run->font();
    auto sk_font = gfx_font.skia_font(command.scale);

    auto glyph_count = command.glyph_run->glyphs().size();
    Vector<SkGlyphID> glyphs;
    glyphs.ensure_capacity(glyph_count);
    Vector<SkPoint> positions;
    positions.ensure_capacity(glyph_count);
    auto font_ascent = gfx_font.pixel_metrics().ascent;

    for (auto const& glyph : command.glyph_run->glyphs()) {
        auto transformed_glyph = glyph;
        transformed_glyph.position.set_y(glyph.position.y() + font_ascent);
        transformed_glyph.position = transformed_glyph.position.scaled(command.scale);
        auto const& point = transformed_glyph.position;
        glyphs.append(transformed_glyph.glyph_id);
        positions.append(to_skia_point(point));
    }

    SkPaint paint;
    paint.setColor(to_skia_color(command.color));

    switch (command.orientation) {
    case Gfx::Orientation::Horizontal:
        canvas().drawGlyphs(glyphs.size(), glyphs.data(), positions.data(), to_skia_point(command.translation), sk_font, paint);
        break;
    case Gfx::Orientation::Vertical:
        canvas().save();
        canvas().translate(command.rect.width(), 0);
        canvas().rotate(90, command.rect.top_left().x(), command.rect.top_left().y());
        canvas().drawGlyphs(glyphs.size(), glyphs.data(), positions.data(), to_skia_point(command.translation), sk_font, paint);
        canvas().restore();
        break;
    }
}

void DisplayListPlayerPDF::fill_rect(Web::Painting::FillRect const& command)
{
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(to_skia_color(command.color));
    canvas().drawRect(to_skia_rect(command.rect), paint);
}

void DisplayListPlayerPDF::draw_scaled_immutable_bitmap(Web::Painting::DrawScaledImmutableBitmap const& command)
{
    auto dst_rect = to_skia_rect(command.dst_rect);
    auto clip_rect = to_skia_rect(command.clip_rect);
    SkPaint paint;
    paint.setAntiAlias(true);
    canvas().save();
    canvas().clipRect(clip_rect, true);
    canvas().drawImageRect(command.bitmap->sk_image(), dst_rect, to_skia_sampling_options(command.scaling_mode), &paint);
    canvas().restore();
}

void DisplayListPlayerPDF::add_clip_rect(Web::Painting::AddClipRect const& command)
{
    canvas().clipRect(to_skia_rect(command.rect), true);
}

void DisplayListPlayerPDF::save()
{
    canvas().save();
}

void DisplayListPlayerPDF::save_layer()
{
    canvas().saveLayer(nullptr, nullptr);
}

void DisplayListPlayerPDF::restore()
{
    canvas().restore();
}

void DisplayListPlayerPDF::translate(Web::Painting::Translate const& command)
{
    canvas().translate(command.delta.x(), command.delta.y());
}

void DisplayListPlayerPDF::push_stacking_context(Web::Painting::PushStackingContext const& command)
{
    auto new_transform = Gfx::translation_matrix(Gfx::Vector3<float>(command.transform.origin.x(), command.transform.origin.y(), 0));
    new_transform = new_transform * command.transform.matrix;
    new_transform = new_transform * Gfx::translation_matrix(Gfx::Vector3<float>(-command.transform.origin.x(), -command.transform.origin.y(), 0));
    if (command.transform.parent_perspective_matrix.has_value())
        new_transform = command.transform.parent_perspective_matrix.value() * new_transform;
    auto matrix = to_skia_matrix4x4(new_transform);

    canvas().save();
    if (command.clip_path.has_value())
        canvas().clipPath(to_skia_path(command.clip_path.value()), true);
    canvas().concat(matrix);

    if (command.opacity < 1 || command.compositing_and_blending_operator != Gfx::CompositingAndBlendingOperator::Normal || command.isolate) {
        SkPaint paint;
        paint.setAlphaf(command.opacity);
        paint.setBlender(Gfx::to_skia_blender(command.compositing_and_blending_operator));
        if (command.bounding_rect.has_value()) {
            auto bounds = to_skia_rect(command.bounding_rect.value());
            canvas().saveLayer(bounds, &paint);
        } else {
            canvas().saveLayer(nullptr, &paint);
        }
    } else {
        canvas().save();
    }
}

void DisplayListPlayerPDF::pop_stacking_context()
{
    canvas().restore();
    canvas().restore();
}

void DisplayListPlayerPDF::fill_rect_with_rounded_corners(Web::Painting::FillRectWithRoundedCorners const& command)
{
    auto rounded_rect = to_skia_rrect(command.rect, command.corner_radii);
    SkPaint paint;
    paint.setColor(to_skia_color(command.color));
    paint.setAntiAlias(true);
    canvas().drawRRect(rounded_rect, paint);
}

void DisplayListPlayerPDF::fill_path(Web::Painting::FillPath const& command)
{
    auto path = to_skia_path(command.path);
    path.setFillType(to_skia_path_fill_type(command.winding_rule));

    SkPaint paint;
    if (command.paint_style_or_color.has<Web::Painting::PaintStyle>()) {
        // Simplified: just use a solid color for PDF
        paint.setColor(SK_ColorBLACK);
        paint.setAlphaf(command.opacity);
    } else {
        auto const& color = command.paint_style_or_color.get<Gfx::Color>();
        paint.setColor(to_skia_color(color));
    }
    paint.setAntiAlias(command.should_anti_alias == Web::Painting::ShouldAntiAlias::Yes);
    canvas().drawPath(path, paint);
}

void DisplayListPlayerPDF::stroke_path(Web::Painting::StrokePath const& command)
{
    auto path = to_skia_path(command.path);
    SkPaint paint;
    if (command.paint_style_or_color.has<Web::Painting::PaintStyle>()) {
        paint.setColor(SK_ColorBLACK);
        paint.setAlphaf(command.opacity);
    } else {
        auto const& color = command.paint_style_or_color.get<Gfx::Color>();
        paint.setColor(to_skia_color(color));
    }
    paint.setAntiAlias(command.should_anti_alias == Web::Painting::ShouldAntiAlias::Yes);
    paint.setStyle(SkPaint::Style::kStroke_Style);
    paint.setStrokeWidth(command.thickness);
    paint.setStrokeCap(to_skia_cap(command.cap_style));
    paint.setStrokeJoin(to_skia_join(command.join_style));
    paint.setStrokeMiter(command.miter_limit);
    if (!command.dash_array.is_empty())
        paint.setPathEffect(SkDashPathEffect::Make(command.dash_array.data(), command.dash_array.size(), command.dash_offset));
    canvas().drawPath(path, paint);
}

void DisplayListPlayerPDF::draw_ellipse(Web::Painting::DrawEllipse const& command)
{
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(command.thickness);
    paint.setColor(to_skia_color(command.color));
    canvas().drawOval(to_skia_rect(command.rect), paint);
}

void DisplayListPlayerPDF::fill_ellipse(Web::Painting::FillEllipse const& command)
{
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(to_skia_color(command.color));
    canvas().drawOval(to_skia_rect(command.rect), paint);
}

void DisplayListPlayerPDF::draw_line(Web::Painting::DrawLine const& command)
{
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(command.thickness);
    paint.setColor(to_skia_color(command.color));
    canvas().drawLine(to_skia_point(command.from.to_type<float>()), to_skia_point(command.to.to_type<float>()), paint);
}

void DisplayListPlayerPDF::draw_rect(Web::Painting::DrawRect const& command)
{
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setColor(to_skia_color(command.color));
    canvas().drawRect(to_skia_rect(command.rect), paint);
}

void DisplayListPlayerPDF::add_rounded_rect_clip(Web::Painting::AddRoundedRectClip const& command)
{
    auto rounded_rect = to_skia_rrect(command.border_rect, command.corner_radii);
    auto clip_op = command.corner_clip == Web::Painting::CornerClip::Inside ? SkClipOp::kDifference : SkClipOp::kIntersect;
    canvas().clipRRect(rounded_rect, clip_op, true);
}

void DisplayListPlayerPDF::apply_opacity(Web::Painting::ApplyOpacity const& command)
{
    SkPaint paint;
    paint.setAlphaf(command.opacity);
    canvas().saveLayer(nullptr, &paint);
}

void DisplayListPlayerPDF::apply_transform(Web::Painting::ApplyTransform const& command)
{
    auto new_transform = Gfx::translation_matrix(Gfx::Vector3<float>(command.origin.x(), command.origin.y(), 0));
    new_transform = new_transform * command.matrix;
    new_transform = new_transform * Gfx::translation_matrix(Gfx::Vector3<float>(-command.origin.x(), -command.origin.y(), 0));
    auto matrix = to_skia_matrix4x4(new_transform);
    canvas().concat(matrix);
}

}
