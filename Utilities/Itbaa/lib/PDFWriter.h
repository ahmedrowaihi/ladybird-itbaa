/*
 * Itbaa (اطبع) - HTML to PDF Conversion Library
 * Copyright (c) 2025, sudorw <ahmedrowaihi@sudorw.com>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Renderer.h"
#include <AK/ByteBuffer.h>
#include <AK/Error.h>
#include <AK/Vector.h>

namespace Itbaa {

// PDF metadata
struct PDFMetadata {
    StringView title { "Itbaa PDF"sv };
    StringView author {};
    StringView subject {};
    StringView creator { "Itbaa (اطبع)"sv };
};

class PDFWriter {
public:
    PDFWriter() = default;
    ~PDFWriter() = default;

    // Set PDF metadata
    void set_metadata(PDFMetadata const& metadata) { m_metadata = metadata; }

    // Generate PDF from rendered pages
    ErrorOr<ByteBuffer> generate(Vector<RenderedPage> const& pages);

    // Generate PDF and write to file
    ErrorOr<void> generate_to_file(Vector<RenderedPage> const& pages, StringView path);

private:
    PDFMetadata m_metadata;
};

} // namespace Itbaa
