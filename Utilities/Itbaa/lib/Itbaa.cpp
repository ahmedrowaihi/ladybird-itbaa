/*
 * Itbaa (اطبع) - HTML to PDF Conversion Library
 * Copyright (c) 2025, sudorw <ahmedrowaihi@sudorw.com>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Itbaa.h"
#include "PDFWriter.h"
#include "Renderer.h"
#include <AK/ByteString.h>
#include <AK/String.h>
#include <LibCore/File.h>
#include <LibCore/MappedFile.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

// Global initialization state
static bool s_initialized = false;

static char s_last_error_message[512] = { 0 };

// Error messages
static char const* s_error_messages[] = {
    "Success",
    "Invalid argument",
    "Memory allocation failed",
    "File not found",
    "File write error",
    "Rendering failed",
    "PDF generation failed",
    "Library not initialized",
};

// C API Implementation
extern "C" {

ItbaaError itbaa_init(void)
{
    if (s_initialized)
        return ITBAA_OK;

    auto result = Itbaa::Renderer::initialize();
    if (result.is_error())
        return ITBAA_ERROR_MEMORY_ALLOCATION;

    s_initialized = true;
    return ITBAA_OK;
}

void itbaa_shutdown(void)
{
    if (!s_initialized)
        return;

    Itbaa::Renderer::shutdown();
    s_initialized = false;
}

char const* itbaa_version(void)
{
    return ITBAA_VERSION_STRING;
}

char const* itbaa_error_string(ItbaaError error)
{
    int index = -static_cast<int>(error);
    if (index < 0 || index >= static_cast<int>(sizeof(s_error_messages) / sizeof(s_error_messages[0])))
        return "Unknown error";
    return s_error_messages[index];
}

ItbaaOptions itbaa_default_options(void)
{
    return ItbaaOptions {
        .page_size = ITBAA_PAGE_A4,
        .width = 794,
        .height = 1123,
        .max_pages = 0,
        .full_page = 1,
    };
}

} // extern "C"

// Internal context structure
struct ItbaaContext {
    Itbaa::Renderer renderer;
    AK::String html_content;
};

extern "C" {

ItbaaContext* itbaa_context_create(void)
{
    if (!s_initialized)
        return nullptr;

    auto* ctx = new (std::nothrow) ItbaaContext();
    return ctx;
}

void itbaa_context_destroy(ItbaaContext* ctx)
{
    delete ctx;
}

ItbaaError itbaa_load_html(ItbaaContext* ctx, char const* html, size_t html_len)
{
    if (!s_initialized)
        return ITBAA_ERROR_NOT_INITIALIZED;
    if (!ctx || !html)
        return ITBAA_ERROR_INVALID_ARGUMENT;

    auto string_result = AK::String::from_utf8(StringView { html, html_len });
    if (string_result.is_error())
        return ITBAA_ERROR_MEMORY_ALLOCATION;

    ctx->html_content = string_result.release_value();

    auto result = ctx->renderer.load_html(ctx->html_content);
    if (result.is_error())
        return ITBAA_ERROR_RENDER_FAILED;

    return ITBAA_OK;
}

ItbaaError itbaa_load_html_file(ItbaaContext* ctx, char const* path)
{
    if (!s_initialized)
        return ITBAA_ERROR_NOT_INITIALIZED;
    if (!ctx || !path)
        return ITBAA_ERROR_INVALID_ARGUMENT;

    auto file_result = Core::MappedFile::map(StringView { path, strlen(path) });
    if (file_result.is_error())
        return ITBAA_ERROR_FILE_NOT_FOUND;

    auto file = file_result.release_value();
    return itbaa_load_html(ctx, reinterpret_cast<char const*>(file->bytes().data()), file->bytes().size());
}

static Itbaa::RenderConfig options_to_config(ItbaaOptions const* options)
{
    Itbaa::RenderConfig config;

    if (!options) {
        return config; // Use defaults
    }

    switch (options->page_size) {
    case ITBAA_PAGE_A4:
        config.page_width = 794;
        config.page_height = 1123;
        break;
    case ITBAA_PAGE_LETTER:
        config.page_width = 816;
        config.page_height = 1056;
        break;
    case ITBAA_PAGE_LEGAL:
        config.page_width = 816;
        config.page_height = 1344;
        break;
    case ITBAA_PAGE_A3:
        config.page_width = 1123;
        config.page_height = 1587;
        break;
    case ITBAA_PAGE_CUSTOM:
        config.page_width = options->width;
        config.page_height = options->height;
        break;
    }

    config.full_page = options->full_page != 0;
    config.max_pages = options->max_pages;

    return config;
}

ItbaaError itbaa_get_info(ItbaaContext* ctx, ItbaaOptions const* options, ItbaaDocumentInfo* info)
{
    if (!s_initialized)
        return ITBAA_ERROR_NOT_INITIALIZED;
    if (!ctx || !info)
        return ITBAA_ERROR_INVALID_ARGUMENT;

    auto config = options_to_config(options);
    auto metrics_result = ctx->renderer.get_metrics(config);
    if (metrics_result.is_error())
        return ITBAA_ERROR_RENDER_FAILED;

    auto metrics = metrics_result.release_value();
    info->page_count = metrics.page_count;
    info->content_width = metrics.content_width;
    info->content_height = metrics.content_height;

    return ITBAA_OK;
}

ItbaaError itbaa_convert(ItbaaContext* ctx, ItbaaOptions const* options, ItbaaPDFBuffer* output)
{
    if (!s_initialized)
        return ITBAA_ERROR_NOT_INITIALIZED;
    if (!ctx || !output)
        return ITBAA_ERROR_INVALID_ARGUMENT;

    auto config = options_to_config(options);

    // Render pages
    auto pages_result = ctx->renderer.render(config);
    if (pages_result.is_error())
        return ITBAA_ERROR_RENDER_FAILED;

    auto pages = pages_result.release_value();

    // Generate PDF
    Itbaa::PDFWriter writer;
    auto pdf_result = writer.generate(pages);
    if (pdf_result.is_error())
        return ITBAA_ERROR_PDF_GENERATION;

    auto pdf_data = pdf_result.release_value();

    // Copy to output buffer
    output->size = pdf_data.size();
    output->data = static_cast<uint8_t*>(malloc(output->size));
    if (!output->data)
        return ITBAA_ERROR_MEMORY_ALLOCATION;

    memcpy(output->data, pdf_data.data(), output->size);
    return ITBAA_OK;
}

ItbaaError itbaa_convert_to_file(ItbaaContext* ctx, ItbaaOptions const* options, char const* output_path)
{
    if (!s_initialized)
        return ITBAA_ERROR_NOT_INITIALIZED;
    if (!ctx || !output_path)
        return ITBAA_ERROR_INVALID_ARGUMENT;

    auto config = options_to_config(options);

    // Render pages
    auto pages_result = ctx->renderer.render(config);
    if (pages_result.is_error())
        return ITBAA_ERROR_RENDER_FAILED;

    auto pages = pages_result.release_value();

    // Generate PDF to file
    Itbaa::PDFWriter writer;
    auto result = writer.generate_to_file(pages, StringView { output_path, strlen(output_path) });
    if (result.is_error()) {
        auto error = result.release_error();
        auto error_str = error.is_errno()
            ? ByteString::formatted("{} (errno {})", strerror(error.code()), error.code())
            : ByteString(error.string_literal());

        auto error_bytes = error_str.bytes();
        size_t copy_len = error_bytes.size() < sizeof(s_last_error_message) - 1
            ? error_bytes.size()
            : sizeof(s_last_error_message) - 1;
        memcpy(s_last_error_message, error_bytes.data(), copy_len);
        s_last_error_message[copy_len] = '\0';

        fprintf(stderr, "DEBUG: File write error details: %s\n", s_last_error_message);

        return ITBAA_ERROR_FILE_WRITE;
    }

    return ITBAA_OK;
}

void itbaa_free_buffer(ItbaaPDFBuffer* buffer)
{
    if (buffer && buffer->data) {
        free(buffer->data);
        buffer->data = nullptr;
        buffer->size = 0;
    }
}

ItbaaError itbaa_html_to_pdf_file(
    char const* html,
    size_t html_len,
    char const* output_path,
    ItbaaOptions const* options)
{
    ItbaaContext* ctx = itbaa_context_create();
    if (!ctx)
        return ITBAA_ERROR_MEMORY_ALLOCATION;

    ItbaaError err = itbaa_load_html(ctx, html, html_len);
    if (err != ITBAA_OK) {
        itbaa_context_destroy(ctx);
        return err;
    }

    err = itbaa_convert_to_file(ctx, options, output_path);
    itbaa_context_destroy(ctx);
    return err;
}

ItbaaError itbaa_file_to_pdf_file(
    char const* input_path,
    char const* output_path,
    ItbaaOptions const* options)
{
    ItbaaContext* ctx = itbaa_context_create();
    if (!ctx)
        return ITBAA_ERROR_MEMORY_ALLOCATION;

    ItbaaError err = itbaa_load_html_file(ctx, input_path);
    if (err != ITBAA_OK) {
        itbaa_context_destroy(ctx);
        return err;
    }

    err = itbaa_convert_to_file(ctx, options, output_path);
    itbaa_context_destroy(ctx);
    return err;
}

} // extern "C"
