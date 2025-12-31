/*
 * Itbaa (اطبع) - HTML to PDF Conversion Library
 * Copyright (c) 2025, sudorw <ahmedrowaihi@sudorw.com>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Error.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibGC/Ptr.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/PixelUnits.h>

#include <core/SkPicture.h>
#include <core/SkRefCnt.h>

namespace Itbaa {

// Represents a single rendered page
struct RenderedPage {
    sk_sp<SkPicture> picture;
    int width;
    int height;
};

// Configuration for rendering
struct RenderConfig {
    uint32_t page_width { 794 };   // A4 width at 96 DPI
    uint32_t page_height { 1123 }; // A4 height at 96 DPI
    bool full_page { true };       // Capture entire scrollable content
    uint32_t max_pages { 0 };      // 0 = unlimited
};

// Document metrics
struct DocumentMetrics {
    uint32_t page_count { 0 };
    uint32_t content_width { 0 };
    uint32_t content_height { 0 };
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Initialize the rendering engine (call once)
    static ErrorOr<void> initialize();

    // Shutdown the rendering engine
    static void shutdown();

    // Load HTML content
    ErrorOr<void> load_html(StringView html);

    // Load HTML from file
    ErrorOr<void> load_html_file(StringView path);

    // Get document metrics (after loading)
    ErrorOr<DocumentMetrics> get_metrics(RenderConfig const& config);

    // Render to vector pages
    ErrorOr<Vector<RenderedPage>> render(RenderConfig const& config);

    // Check if HTML is loaded
    bool is_loaded() const { return m_loaded; }

private:
    ErrorOr<void> ensure_loaded();
    ErrorOr<void> process_events();
    ErrorOr<void> wait_for_document_ready();

    GC::Ptr<Web::Page> m_page;
    GC::Ptr<Web::PageClient> m_page_client;
    String m_html_content;
    bool m_loaded { false };
    bool m_initialized { false };
};

} // namespace Itbaa
