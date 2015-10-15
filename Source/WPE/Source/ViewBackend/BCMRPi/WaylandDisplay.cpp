#include "Config.h"
#include "WaylandDisplay.h"

#if WPE_BACKEND(BCM_RPI)

#include "wayland-bcmrpi-dispmanx-client-protocol.h"
#include <cstring>
#include <glib.h>
#include <wayland-client.h>

namespace WPE {

namespace ViewBackend {

class EventSource {
public:
    static GSourceFuncs sourceFuncs;

    GSource source;
    GPollFD pfd;
    struct wl_display* display;
};

GSourceFuncs EventSource::sourceFuncs = {
    // prepare
    [](GSource* base, gint* timeout) -> gboolean
    {
        auto* source = reinterpret_cast<EventSource*>(base);
        struct wl_display* display = source->display;

        *timeout = -1;

        wl_display_flush(display);
        wl_display_dispatch_pending(display);

        return FALSE;
    },
    // check
    [](GSource* base) -> gboolean
    {
        auto* source = reinterpret_cast<EventSource*>(base);
        return !!source->pfd.revents;
    },
    // dispatch
    [](GSource* base, GSourceFunc, gpointer) -> gboolean
    {
        auto* source = reinterpret_cast<EventSource*>(base);
        struct wl_display* display = source->display;

        if (source->pfd.revents & G_IO_IN)
            wl_display_dispatch(display);

        if (source->pfd.revents & (G_IO_ERR | G_IO_HUP))
            return FALSE;

        source->pfd.revents = 0;
        return TRUE;
    },
    nullptr, // finalize
    nullptr, // closure_callback
    nullptr, // closure_marshall
};

const struct wl_registry_listener g_registryListener = {
    // global
    [](void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t)
    {
        auto& interfaces = *static_cast<WaylandDisplay::Interfaces*>(data);

        if (!std::strcmp(interface, "wl_compositor"))
            interfaces.compositor = static_cast<struct wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 1));

        if (!std::strcmp(interface, "wl_bcmrpi_dispmanx"))
            interfaces.bcmrpi_dispmanx = static_cast<struct wl_bcmrpi_dispmanx*>(wl_registry_bind(registry, name, &wl_bcmrpi_dispmanx_interface, 1));
    },
    // global_remove
    [](void*, struct wl_registry*, uint32_t) { },
};

const WaylandDisplay& WaylandDisplay::singleton()
{
    static WaylandDisplay display;
    return display;
}

WaylandDisplay::WaylandDisplay()
{
    m_display = wl_display_connect(nullptr);
    m_registry = wl_display_get_registry(m_display);

    wl_registry_add_listener(m_registry, &g_registryListener, &m_interfaces);
    wl_display_roundtrip(m_display);

    m_eventSource = g_source_new(&EventSource::sourceFuncs, sizeof(EventSource));
    auto* source = reinterpret_cast<EventSource*>(m_eventSource);
    source->display = m_display;

    source->pfd.fd = wl_display_get_fd(m_display);
    source->pfd.events = G_IO_IN | G_IO_ERR | G_IO_HUP;
    source->pfd.revents = 0;
    g_source_add_poll(m_eventSource, &source->pfd);

    g_source_set_name(m_eventSource, "[WPE] WaylandDisplay");
    g_source_set_priority(m_eventSource, G_PRIORITY_HIGH + 30);
    g_source_set_can_recurse(m_eventSource, TRUE);
    g_source_attach(m_eventSource, g_main_context_get_thread_default());
}

WaylandDisplay::~WaylandDisplay()
{
    if (m_eventSource)
        g_source_unref(m_eventSource);
    m_eventSource = nullptr;

    if (m_interfaces.bcmrpi_dispmanx)
        wl_bcmrpi_dispmanx_destroy(m_interfaces.bcmrpi_dispmanx);
    if (m_interfaces.compositor)
        wl_compositor_destroy(m_interfaces.compositor);
    m_interfaces = { nullptr, nullptr };

    if (m_registry)
        wl_registry_destroy(m_registry);
    m_registry = nullptr;
    if (m_display)
        wl_display_disconnect(m_display);
    m_display = nullptr;
}

} // namespace ViewBackend

} // namespace WPE

#endif // WPE_BACKEND(BCM_RPI)
