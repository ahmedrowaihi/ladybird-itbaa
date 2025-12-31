/*
 * Itbaa (اطبع) - HTML to PDF Conversion Library
 * Copyright (c) 2025, sudorw <ahmedrowaihi@sudorw.com>
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * A high-quality HTML to PDF conversion library built on Ladybird's rendering engine.
 * Produces vector PDFs with selectable text, proper font embedding, and full CSS support.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Version Information
 * ============================================================================ */

#define ITBAA_VERSION_MAJOR 1
#define ITBAA_VERSION_MINOR 0
#define ITBAA_VERSION_PATCH 0
#define ITBAA_VERSION_STRING "1.0.0"

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    ITBAA_OK = 0,
    ITBAA_ERROR_INVALID_ARGUMENT = -1,
    ITBAA_ERROR_MEMORY_ALLOCATION = -2,
    ITBAA_ERROR_FILE_NOT_FOUND = -3,
    ITBAA_ERROR_FILE_WRITE = -4,
    ITBAA_ERROR_RENDER_FAILED = -5,
    ITBAA_ERROR_PDF_GENERATION = -6,
    ITBAA_ERROR_NOT_INITIALIZED = -7,
} ItbaaError;

/* ============================================================================
 * Types
 * ============================================================================ */

/* Opaque handle to the Itbaa context */
typedef struct ItbaaContext ItbaaContext;

/* Page size presets (in pixels at 96 DPI) */
typedef enum {
    ITBAA_PAGE_A4,     /* 794 x 1123 */
    ITBAA_PAGE_LETTER, /* 816 x 1056 */
    ITBAA_PAGE_LEGAL,  /* 816 x 1344 */
    ITBAA_PAGE_A3,     /* 1123 x 1587 */
    ITBAA_PAGE_CUSTOM, /* Use width/height from options */
} ItbaaPageSize;

/* Conversion options */
typedef struct {
    ItbaaPageSize page_size;
    uint32_t width;     /* Custom width (pixels at 96 DPI) */
    uint32_t height;    /* Custom height (pixels at 96 DPI) */
    uint32_t max_pages; /* 0 = unlimited */
    int full_page;      /* 1 = capture entire scrollable content */
} ItbaaOptions;

/* Document information */
typedef struct {
    uint32_t page_count;
    uint32_t content_width;
    uint32_t content_height;
} ItbaaDocumentInfo;

/* PDF output buffer */
typedef struct {
    uint8_t* data;
    size_t size;
} ItbaaPDFBuffer;

/* ============================================================================
 * Initialization & Cleanup
 * ============================================================================ */

/**
 * Initialize the Itbaa library.
 * Must be called before any other functions.
 * Returns ITBAA_OK on success.
 */
ItbaaError itbaa_init(void);

/**
 * Shutdown the Itbaa library and free all resources.
 */
void itbaa_shutdown(void);

/**
 * Get the version string.
 */
char const* itbaa_version(void);

/**
 * Get a human-readable error message.
 */
char const* itbaa_error_string(ItbaaError error);

/* ============================================================================
 * Context Management
 * ============================================================================ */

/**
 * Create a new conversion context.
 * Returns NULL on failure.
 */
ItbaaContext* itbaa_context_create(void);

/**
 * Destroy a conversion context.
 */
void itbaa_context_destroy(ItbaaContext* ctx);

/* ============================================================================
 * Options
 * ============================================================================ */

/**
 * Get default options (A4, full page capture).
 */
ItbaaOptions itbaa_default_options(void);

/* ============================================================================
 * Conversion Functions
 * ============================================================================ */

/**
 * Load HTML content into the context.
 * The HTML string is copied internally.
 */
ItbaaError itbaa_load_html(ItbaaContext* ctx, char const* html, size_t html_len);

/**
 * Load HTML from a file.
 */
ItbaaError itbaa_load_html_file(ItbaaContext* ctx, char const* path);

/**
 * Get document information (page count, dimensions).
 * Must be called after itbaa_load_html.
 */
ItbaaError itbaa_get_info(ItbaaContext* ctx, ItbaaOptions const* options, ItbaaDocumentInfo* info);

/**
 * Convert loaded HTML to PDF.
 * Returns a buffer containing the PDF data.
 * Caller must free with itbaa_free_buffer().
 */
ItbaaError itbaa_convert(ItbaaContext* ctx, ItbaaOptions const* options, ItbaaPDFBuffer* output);

/**
 * Convert HTML to PDF and write directly to a file.
 */
ItbaaError itbaa_convert_to_file(ItbaaContext* ctx, ItbaaOptions const* options, char const* output_path);

/**
 * Free a PDF buffer returned by itbaa_convert().
 */
void itbaa_free_buffer(ItbaaPDFBuffer* buffer);

/* ============================================================================
 * Convenience Functions
 * ============================================================================ */

/**
 * One-shot conversion: HTML string to PDF file.
 */
ItbaaError itbaa_html_to_pdf_file(
    char const* html,
    size_t html_len,
    char const* output_path,
    ItbaaOptions const* options /* NULL for defaults */
);

/**
 * One-shot conversion: HTML file to PDF file.
 */
ItbaaError itbaa_file_to_pdf_file(
    char const* input_path,
    char const* output_path,
    ItbaaOptions const* options /* NULL for defaults */
);

#ifdef __cplusplus
}
#endif
