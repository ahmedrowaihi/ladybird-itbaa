/*
 * Itbaa (اطبع) - HTML to PDF CLI Tool
 * Copyright (c) 2025, sudorw <ahmedrowaihi@sudorw.com>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Itbaa.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void print_usage(char const* program)
{
    printf("Itbaa (اطبع) - HTML to PDF Converter v%s\n", itbaa_version());
    printf("Copyright (c) 2025, sudorw <ahmedrowaihi@sudorw.com>\n\n");
    printf("Usage: %s [OPTIONS] <input.html> [output.pdf]\n\n", program);
    printf("Options:\n");
    printf("  -i, --info         Show document info without generating PDF\n");
    printf("  -p, --pages <N>    Maximum number of pages (default: all)\n");
    printf("  -w, --width <N>    Page width in pixels (default: 794 for A4)\n");
    printf("  -h, --height <N>   Page height in pixels (default: 1123 for A4)\n");
    printf("  -s, --size <SIZE>  Page size preset: a4, letter, legal, a3\n");
    printf("  --no-full-page     Don't capture full scrollable content\n");
    printf("  --version          Show version information\n");
    printf("  --help             Show this help message\n\n");
    printf("Examples:\n");
    printf("  %s document.html output.pdf\n", program);
    printf("  %s --info document.html\n", program);
    printf("  %s -p 5 --size letter document.html output.pdf\n", program);
}

static void print_version()
{
    printf("Itbaa (اطبع) v%s\n", itbaa_version());
    printf("HTML to PDF Converter built on Ladybird\n");
    printf("Copyright (c) 2025, sudorw <ahmedrowaihi@sudorw.com>\n");
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    // Parse arguments
    char const* input_path = nullptr;
    char const* output_path = nullptr;
    ItbaaOptions options = itbaa_default_options();
    bool info_only = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--info") == 0) {
            info_only = true;
            continue;
        }
        if (strcmp(argv[i], "--no-full-page") == 0) {
            options.full_page = 0;
            continue;
        }
        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--pages") == 0) && i + 1 < argc) {
            options.max_pages = atoi(argv[++i]);
            continue;
        }
        if ((strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--width") == 0) && i + 1 < argc) {
            options.width = atoi(argv[++i]);
            options.page_size = ITBAA_PAGE_CUSTOM;
            continue;
        }
        if ((strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--height") == 0) && i + 1 < argc) {
            options.height = atoi(argv[++i]);
            options.page_size = ITBAA_PAGE_CUSTOM;
            continue;
        }
        if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--size") == 0) && i + 1 < argc) {
            char const* size = argv[++i];
            if (strcmp(size, "a4") == 0)
                options.page_size = ITBAA_PAGE_A4;
            else if (strcmp(size, "letter") == 0)
                options.page_size = ITBAA_PAGE_LETTER;
            else if (strcmp(size, "legal") == 0)
                options.page_size = ITBAA_PAGE_LEGAL;
            else if (strcmp(size, "a3") == 0)
                options.page_size = ITBAA_PAGE_A3;
            else {
                fprintf(stderr, "Unknown page size: %s\n", size);
                return 1;
            }
            continue;
        }
        // Positional arguments
        if (argv[i][0] != '-') {
            if (!input_path)
                input_path = argv[i];
            else if (!output_path)
                output_path = argv[i];
            continue;
        }
        fprintf(stderr, "Unknown option: %s\n", argv[i]);
        return 1;
    }

    if (!input_path) {
        fprintf(stderr, "Error: Input file required\n");
        print_usage(argv[0]);
        return 1;
    }

    if (!info_only && !output_path) {
        fprintf(stderr, "Error: Output file required (or use --info)\n");
        return 1;
    }

    // Initialize library
    ItbaaError err = itbaa_init();
    if (err != ITBAA_OK) {
        fprintf(stderr, "Error: Failed to initialize: %s\n", itbaa_error_string(err));
        return 1;
    }

    // Create context
    ItbaaContext* ctx = itbaa_context_create();
    if (!ctx) {
        fprintf(stderr, "Error: Failed to create context\n");
        itbaa_shutdown();
        return 1;
    }

    // Load HTML
    err = itbaa_load_html_file(ctx, input_path);
    if (err != ITBAA_OK) {
        fprintf(stderr, "Error: %s: %s\n", input_path, itbaa_error_string(err));
        itbaa_context_destroy(ctx);
        itbaa_shutdown();
        return 1;
    }

    if (info_only) {
        // Show document info
        ItbaaDocumentInfo info;
        err = itbaa_get_info(ctx, &options, &info);
        if (err != ITBAA_OK) {
            fprintf(stderr, "Error: %s\n", itbaa_error_string(err));
            itbaa_context_destroy(ctx);
            itbaa_shutdown();
            return 1;
        }
        printf("Document: %s\n", input_path);
        printf("  Pages: %u\n", info.page_count);
        printf("  Content size: %ux%u pixels\n", info.content_width, info.content_height);
        printf("  Page size: %ux%u pixels\n", options.width, options.height);
    } else {
        // Convert to PDF
        err = itbaa_convert_to_file(ctx, &options, output_path);
        if (err != ITBAA_OK) {
            if (err == ITBAA_ERROR_FILE_WRITE) {
                fprintf(stderr, "Error: Failed to write PDF file '%s': %s\n", output_path, itbaa_error_string(err));
                fprintf(stderr, "  Check that:\n");
                fprintf(stderr, "  - The directory exists and is writable\n");
                fprintf(stderr, "  - You have write permissions for the file\n");
                fprintf(stderr, "  - There is sufficient disk space\n");
            } else {
                fprintf(stderr, "Error: %s\n", itbaa_error_string(err));
            }
            itbaa_context_destroy(ctx);
            itbaa_shutdown();
            return 1;
        }

        ItbaaDocumentInfo info;
        itbaa_get_info(ctx, &options, &info);
        printf("Generated %u page(s) -> %s\n", info.page_count, output_path);
    }

    itbaa_context_destroy(ctx);
    itbaa_shutdown();
    return 0;
}
