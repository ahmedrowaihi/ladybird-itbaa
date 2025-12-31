/*
 * Itbaa (اطبع) - HTML to PDF Conversion Library
 * Copyright (c) 2025, sudorw <ahmedrowaihi@sudorw.com>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "PDFWriter.h"
#include <AK/ByteString.h>
#include <LibCore/File.h>
#include <errno.h>
#include <string.h>

#include <codec/SkCodec.h>
#include <core/SkCanvas.h>
#include <core/SkData.h>
#include <core/SkPicture.h>
#include <core/SkStream.h>
#include <docs/SkPDFDocument.h>

namespace Itbaa {

// Dummy JPEG callbacks (images embedded as deflated data)
static std::unique_ptr<SkCodec> jpeg_decode(sk_sp<SkData const>)
{
    return nullptr;
}

static bool jpeg_encode(SkWStream*, SkPixmap const&, int)
{
    return false;
}

ErrorOr<ByteBuffer> PDFWriter::generate(Vector<RenderedPage> const& pages)
{
    if (pages.is_empty())
        return Error::from_string_literal("No pages to render");

    SkDynamicMemoryWStream stream;

    SkPDF::Metadata metadata;

    // Convert StringView to SkString
    if (!m_metadata.title.is_empty()) {
        auto title_string = String::from_utf8(m_metadata.title);
        if (!title_string.is_error())
            metadata.fTitle = SkString(title_string.value().bytes_as_string_view().characters_without_null_termination(), title_string.value().bytes().size());
    }

    if (!m_metadata.author.is_empty()) {
        auto author_string = String::from_utf8(m_metadata.author);
        if (!author_string.is_error())
            metadata.fAuthor = SkString(author_string.value().bytes_as_string_view().characters_without_null_termination(), author_string.value().bytes().size());
    }

    if (!m_metadata.creator.is_empty()) {
        auto creator_string = String::from_utf8(m_metadata.creator);
        if (!creator_string.is_error())
            metadata.fCreator = SkString(creator_string.value().bytes_as_string_view().characters_without_null_termination(), creator_string.value().bytes().size());
    }

    metadata.jpegDecoder = jpeg_decode;
    metadata.jpegEncoder = jpeg_encode;

    auto document = SkPDF::MakeDocument(&stream, metadata);
    if (!document)
        return Error::from_string_literal("Failed to create PDF document");

    // Convert pixels (96 DPI) to points (72 DPI)
    constexpr float pdf_points_per_inch = 72.0f;
    constexpr float screen_pixels_per_inch = 96.0f;
    constexpr float scale_factor = pdf_points_per_inch / screen_pixels_per_inch;

    for (auto const& page_data : pages) {
        float page_width_pts = page_data.width * scale_factor;
        float page_height_pts = page_data.height * scale_factor;

        SkCanvas* canvas = document->beginPage(page_width_pts, page_height_pts);
        if (!canvas)
            return Error::from_string_literal("Failed to begin PDF page");

        canvas->scale(scale_factor, scale_factor);

        if (page_data.picture)
            canvas->drawPicture(page_data.picture);

        document->endPage();
    }

    document->close();

    sk_sp<SkData> data = stream.detachAsData();
    if (!data)
        return Error::from_string_literal("Failed to get PDF data");

    return ByteBuffer::copy(data->bytes(), data->size());
}

ErrorOr<void> PDFWriter::generate_to_file(Vector<RenderedPage> const& pages, StringView path)
{
    auto pdf_data = TRY(generate(pages));

    auto file_result = Core::File::open(path, Core::File::OpenMode::Write | Core::File::OpenMode::Truncate);
    if (file_result.is_error()) {
        auto error = file_result.release_error();
        auto error_msg = error.is_errno()
            ? ByteString::formatted("Failed to open file '{}': {} (errno {})", path, strerror(error.code()), error.code())
            : ByteString::formatted("Failed to open file '{}': {}", path, error.string_literal());
        return Error::from_string_view(error_msg.view());
    }
    auto file = file_result.release_value();

    auto write_result = file->write_until_depleted(pdf_data);
    if (write_result.is_error()) {
        auto error = write_result.release_error();
        auto error_msg = error.is_errno()
            ? ByteString::formatted("Failed to write file '{}': {} (errno {})", path, strerror(error.code()), error.code())
            : ByteString::formatted("Failed to write file '{}': {}", path, error.string_literal());
        return Error::from_string_view(error_msg.view());
    }

    return {};
}

} // namespace Itbaa
