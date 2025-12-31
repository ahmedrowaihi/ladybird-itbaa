/*
 * Itbaa (اطبع) - HTML to PDF Conversion Library
 * Copyright (c) 2025, sudorw <ahmedrowaihi@sudorw.com>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Renderer.h"
#include "DisplayListPlayerPDF.h"
#include <LibCore/AnonymousBuffer.h>
#include <LibCore/EventLoop.h>
#include <LibCore/MappedFile.h>
#include <LibCore/Socket.h>
#include <LibCore/System.h>
#include <LibGfx/Font/FontDatabase.h>
#include <LibGfx/Font/PathFontProvider.h>
#include <LibGfx/Palette.h>
#include <LibIPC/Transport.h>
#include <LibJS/Runtime/VM.h>
#include <LibRequests/RequestClient.h>
#include <LibURL/URL.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/BrowsingContext.h>
#include <LibWeb/HTML/PaintConfig.h>
#include <LibWeb/HTML/TraversableNavigable.h>
#include <LibWeb/Loader/ResourceLoader.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/Painting/DisplayList.h>
#include <LibWeb/Painting/ViewportPaintable.h>
#include <LibWeb/Platform/EventLoopPlugin.h>
#include <LibWeb/Platform/EventLoopPluginSerenity.h>
#include <LibWeb/Platform/FontPlugin.h>
#include <LibWebView/Plugins/FontPlugin.h>

#include <core/SkCanvas.h>
#include <core/SkPicture.h>
#include <core/SkPictureRecorder.h>

using namespace Web::DOM;

namespace Itbaa {

// Internal PageClient for headless rendering
class RendererPageClient final : public Web::PageClient {
    GC_CELL(RendererPageClient, Web::PageClient);
    GC_DECLARE_ALLOCATOR(RendererPageClient);

public:
    static GC::Ref<RendererPageClient> create(JS::VM& vm)
    {
        return vm.heap().allocate<RendererPageClient>();
    }

    virtual ~RendererPageClient() override = default;

    virtual u64 id() const override { return 1; }
    virtual Web::Page& page() override
    {
        VERIFY(m_page);
        return *m_page;
    }
    virtual Web::Page const& page() const override
    {
        VERIFY(m_page);
        return *m_page;
    }
    virtual bool is_connection_open() const override { return true; }

    virtual Gfx::Palette palette() const override
    {
        if (!m_palette_impl) {
            auto buffer = Core::AnonymousBuffer::create_with_size(sizeof(Gfx::SystemTheme)).release_value_but_fixme_should_propagate_errors();
            auto* theme = buffer.data<Gfx::SystemTheme>();
            memset(theme, 0, sizeof(Gfx::SystemTheme));
            theme->color[to_underlying(Gfx::ColorRole::Window)] = Color(Color::White).value();
            theme->color[to_underlying(Gfx::ColorRole::WindowText)] = Color(Color::Black).value();
            theme->color[to_underlying(Gfx::ColorRole::Base)] = Color(Color::White).value();
            theme->color[to_underlying(Gfx::ColorRole::BaseText)] = Color(Color::Black).value();
            m_palette_impl = Gfx::PaletteImpl::create_with_anonymous_buffer(buffer);
        }
        return Gfx::Palette(*m_palette_impl);
    }

    virtual Web::DevicePixelRect screen_rect() const override
    {
        return Web::DevicePixelRect { 0, 0, m_viewport_size.width(), m_viewport_size.height() };
    }

    virtual double device_pixels_per_css_pixel() const override { return 1.0; }
    virtual Web::CSS::PreferredColorScheme preferred_color_scheme() const override { return Web::CSS::PreferredColorScheme::Auto; }
    virtual Web::CSS::PreferredContrast preferred_contrast() const override { return Web::CSS::PreferredContrast::Auto; }
    virtual Web::CSS::PreferredMotion preferred_motion() const override { return Web::CSS::PreferredMotion::Auto; }
    virtual Queue<Web::QueuedInputEvent>& input_event_queue() override { return m_input_event_queue; }
    virtual void report_finished_handling_input_event(u64, Web::EventResult) override { }
    virtual Web::DisplayListPlayerType display_list_player_type() const override { return Web::DisplayListPlayerType::SkiaCPU; }
    virtual bool is_headless() const override { return true; }
    virtual void request_file(Web::FileRequest) override { }

    void set_page(GC::Ref<Web::Page> page) { m_page = page; }
    void set_viewport_size(Web::DevicePixelSize size) { m_viewport_size = size; }
    void set_full_page_mode(bool full_page) { m_full_page_mode = full_page; }

    ErrorOr<Vector<RenderedPage>> render_pages()
    {
        auto* document = page().top_level_browsing_context().active_document();
        if (!document)
            return Error::from_string_literal("No active document");

        int page_width = m_viewport_size.width().value();
        int page_height = m_viewport_size.height().value();

        if (page_width <= 0 || page_height <= 0) {
            page_width = 794;
            page_height = 1123;
        }

        float device_pixels_per_css = page().client().device_pixels_per_css_pixel();

        // Set viewport size for proper layout
        document->navigable()->set_viewport_size(Web::CSSPixelSize { page_width / device_pixels_per_css, page_height / device_pixels_per_css });
        document->set_needs_full_style_update(true);
        document->update_style();
        document->update_layout(UpdateLayoutReason::Debugging);

        int total_height = page_height;
        if (m_full_page_mode && document->paintable()) {
            if (auto scroll_rect = document->paintable()->scrollable_overflow_rect(); scroll_rect.has_value()) {
                int full_height = static_cast<int>(scroll_rect->height().to_double() * device_pixels_per_css);
                if (full_height > page_height) {
                    total_height = full_height;
                }
            }
        }

        document->set_needs_full_style_update(true);
        document->update_style();
        document->update_layout(UpdateLayoutReason::Debugging);
        document->invalidate_display_list();

        Web::HTML::PaintConfig paint_config {
            .paint_overlay = false,
            .should_show_line_box_borders = false,
            .canvas_fill_rect = Gfx::IntRect { {}, { page_width, total_height } }
        };

        auto display_list = document->record_display_list(paint_config);
        if (!display_list)
            return Error::from_string_literal("Failed to record display list");

        int num_pages = (total_height + page_height - 1) / page_height;
        Vector<RenderedPage> pages;

        for (int page_num = 0; page_num < num_pages; page_num++) {
            int y_offset = page_num * page_height;
            int this_page_height = AK::min(page_height, total_height - y_offset);

            SkPictureRecorder recorder;
            SkCanvas* canvas = recorder.beginRecording(page_width, this_page_height);
            canvas->translate(0, -y_offset);

            Itbaa::DisplayListPlayerPDF player(*canvas);
            player.execute(*display_list);

            sk_sp<SkPicture> picture = recorder.finishRecordingAsPicture();

            pages.append(RenderedPage {
                .picture = picture,
                .width = page_width,
                .height = this_page_height });
        }

        return pages;
    }

    DocumentMetrics get_document_metrics()
    {
        auto* document = page().top_level_browsing_context().active_document();
        if (!document)
            return {};

        int page_width = m_viewport_size.width().value();
        int page_height = m_viewport_size.height().value();
        float device_pixels_per_css = page().client().device_pixels_per_css_pixel();

        // Set viewport size for proper layout
        document->navigable()->set_viewport_size(Web::CSSPixelSize { page_width / device_pixels_per_css, page_height / device_pixels_per_css });
        document->set_needs_full_style_update(true);
        document->update_style();
        document->update_layout(UpdateLayoutReason::Debugging);

        int total_height = page_height;
        int content_width = page_width;

        if (document->paintable()) {
            if (auto scroll_rect = document->paintable()->scrollable_overflow_rect(); scroll_rect.has_value()) {
                total_height = static_cast<int>(scroll_rect->height().to_double() * device_pixels_per_css);
                content_width = static_cast<int>(scroll_rect->width().to_double() * device_pixels_per_css);
            }
        }

        int page_count = (total_height + page_height - 1) / page_height;

        return DocumentMetrics {
            .page_count = static_cast<uint32_t>(page_count),
            .content_width = static_cast<uint32_t>(content_width),
            .content_height = static_cast<uint32_t>(total_height),
        };
    }

private:
    RendererPageClient() = default;

    virtual void visit_edges(JS::Cell::Visitor& visitor) override
    {
        Base::visit_edges(visitor);
        visitor.visit(m_page);
    }

    GC::Ptr<Web::Page> m_page;
    Queue<Web::QueuedInputEvent> m_input_event_queue;
    Web::DevicePixelSize m_viewport_size { 794, 1123 };
    bool m_full_page_mode { true };
    mutable RefPtr<Gfx::PaletteImpl> m_palette_impl;
};

GC_DEFINE_ALLOCATOR(RendererPageClient);

// Static initialization state
static bool s_renderer_initialized = false;
static OwnPtr<Core::EventLoop> s_event_loop;

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

ErrorOr<void> Renderer::initialize()
{
    if (s_renderer_initialized)
        return {};

    s_event_loop = make<Core::EventLoop>();

    Web::Platform::EventLoopPlugin::install(*new Web::Platform::EventLoopPluginSerenity);
    Web::Bindings::initialize_main_thread_vm(Web::Bindings::AgentType::SimilarOriginWindow);

    auto& vm = Web::Bindings::main_thread_vm();

    auto& font_provider = static_cast<Gfx::PathFontProvider&>(
        Gfx::FontDatabase::the().install_system_font_provider(make<Gfx::PathFontProvider>()));
    font_provider.load_all_fonts_from_uri("resource://fonts"sv);
    Web::Platform::FontPlugin::install(*new WebView::FontPlugin(false, &font_provider));

    int socket_fds[2] {};
    TRY(Core::System::socketpair(AF_LOCAL, SOCK_STREAM, 0, socket_fds));
    auto client_socket = TRY(Core::LocalSocket::adopt_fd(socket_fds[0]));
    TRY(client_socket->set_blocking(true));
    TRY(Core::System::close(socket_fds[1]));

    auto request_client = TRY(try_make_ref_counted<Requests::RequestClient>(make<IPC::Transport>(move(client_socket))));
    Web::ResourceLoader::initialize(vm.heap(), move(request_client));

    s_renderer_initialized = true;
    return {};
}

void Renderer::shutdown()
{
    s_event_loop = nullptr;
    s_renderer_initialized = false;
}

ErrorOr<void> Renderer::load_html(StringView html)
{
    if (!s_renderer_initialized)
        return Error::from_string_literal("Renderer not initialized");

    auto& vm = Web::Bindings::main_thread_vm();

    auto page_client = RendererPageClient::create(vm);
    auto page = Web::Page::create(vm, page_client);
    page_client->set_page(page);

    // Use create_a_new_top_level_traversable (like SVGDecodedImageData) to avoid BackingStoreManager issues
    page->set_top_level_traversable(Web::HTML::TraversableNavigable::create_a_new_top_level_traversable(page, nullptr, {}).release_value_but_fixme_should_propagate_errors());

    m_html_content = TRY(String::from_utf8(html));
    page->load_html(m_html_content);

    // Process events until document is ready
    while (Core::EventLoop::current().pump(Core::EventLoop::WaitMode::PollForEvents))
        ;

    auto* document = page->top_level_browsing_context().active_document();
    if (!document)
        return Error::from_string_literal("Failed to load document");

    document->set_needs_full_style_update(true);
    document->update_style();

    while (Core::EventLoop::current().pump(Core::EventLoop::WaitMode::PollForEvents))
        ;

    m_page = page;
    m_page_client = page_client;
    m_loaded = true;

    return {};
}

ErrorOr<void> Renderer::load_html_file(StringView path)
{
    auto file = TRY(Core::MappedFile::map(path));
    return load_html(StringView { file->bytes() });
}

ErrorOr<DocumentMetrics> Renderer::get_metrics(RenderConfig const& config)
{
    TRY(ensure_loaded());

    auto* client = static_cast<RendererPageClient*>(m_page_client.ptr());
    client->set_viewport_size({ config.page_width, config.page_height });
    client->set_full_page_mode(config.full_page);

    return client->get_document_metrics();
}

ErrorOr<Vector<RenderedPage>> Renderer::render(RenderConfig const& config)
{
    TRY(ensure_loaded());

    auto* client = static_cast<RendererPageClient*>(m_page_client.ptr());
    client->set_viewport_size({ config.page_width, config.page_height });
    client->set_full_page_mode(config.full_page);

    auto pages = TRY(client->render_pages());

    if (config.max_pages > 0 && pages.size() > config.max_pages)
        pages.resize(config.max_pages);

    return pages;
}

ErrorOr<void> Renderer::ensure_loaded()
{
    if (!m_loaded)
        return Error::from_string_literal("No HTML content loaded");
    return {};
}

} // namespace Itbaa
