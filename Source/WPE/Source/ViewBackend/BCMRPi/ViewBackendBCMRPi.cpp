/*
 * Copyright (C) 2015 Igalia S.L.
 * Copyright (C) 2015 Metrological
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Config.h"
#include "ViewBackendBCMRPi.h"

#if WPE_BACKEND(BCM_RPI)

#include "LibinputServer.h"
#include "WaylandDisplay.h"
#include "wayland-bcmrpi-dispmanx-client-protocol.h"
#include <wayland-client.h>

namespace WPE {

namespace ViewBackend {

static const struct wl_bcmrpi_dispmanx_listener g_BCMRPiDispmanxListener = {
    // element_created
    [](void* data, struct wl_bcmrpi_dispmanx*, struct wl_surface* surface, uint32_t handle)
    {
        auto& elementData = *static_cast<ViewBackendBCMRPi::ElementData*>(data);
        if (elementData.surface == surface)
            elementData.handle = handle;
    }
};

static const struct wl_callback_listener g_callbackListener = {
    // frame
    [](void* data, struct wl_callback* callback, uint32_t)
    {
        auto& callbackData = *static_cast<ViewBackendBCMRPi::CallbackListenerData*>(data);
        if (callbackData.client)
            callbackData.client->frameComplete();
        callbackData.frameCallback = nullptr;
        wl_callback_destroy(callback);
    },
};

ViewBackendBCMRPi::ViewBackendBCMRPi()
    : m_display(WaylandDisplay::singleton())
    , m_width(0)
    , m_height(0)
{
    fprintf(stderr, "ViewBackendBCMRPi::ViewBackendBCMRPi()\n");
    m_elementData.surface = wl_compositor_create_surface(m_display.interfaces().compositor);
    fprintf(stderr, "\tcreated wl_surface %p\n", m_elementData.surface);

    wl_bcmrpi_dispmanx_add_listener(m_display.interfaces().bcmrpi_dispmanx, &g_BCMRPiDispmanxListener, &m_elementData);
#if 0
    m_displayHandle = vc_dispmanx_display_open(0);
#endif
}

ViewBackendBCMRPi::~ViewBackendBCMRPi()
{
    LibinputServer::singleton().setClient(nullptr);
}

void ViewBackendBCMRPi::setClient(Client* client)
{
    m_callbackData.client = client;
    m_client = client;
}

uint32_t ViewBackendBCMRPi::createBCMElement(int32_t width, int32_t height)
{
    fprintf(stderr, "ViewBackendBCMRPi::createBCMElement() (%u,%u)\n", width, height);

    wl_bcmrpi_dispmanx_create_element(m_display.interfaces().bcmrpi_dispmanx, m_elementData.surface, width, height);
    wl_display_roundtrip(m_display.display());
    fprintf(stderr, "ViewBackendBCMRPi: handle %u\n", m_elementData.handle);

#if 0
    static VC_DISPMANX_ALPHA_T alpha = {
        static_cast<DISPMANX_FLAGS_ALPHA_T>(DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS),
        255, 0
    };

    if (m_elementHandle != DISPMANX_NO_HANDLE)
        return 0;

    m_width = std::max(width, 0);
    m_height = std::max(height, 0);

    DISPMANX_UPDATE_HANDLE_T updateHandle = vc_dispmanx_update_start(0);

    VC_RECT_T srcRect, destRect;
    vc_dispmanx_rect_set(&srcRect, 0, 0, m_width << 16, m_height << 16);
    vc_dispmanx_rect_set(&destRect, 0, 0, m_width, m_height);

    m_elementHandle = vc_dispmanx_element_add(updateHandle, m_displayHandle, 0,
        &destRect, DISPMANX_NO_HANDLE, &srcRect, DISPMANX_PROTECTION_NONE,
        &alpha, nullptr, DISPMANX_NO_ROTATE);

    vc_dispmanx_update_submit_sync(updateHandle);
    return m_elementHandle;
#endif
    return m_elementData.handle;
}

void ViewBackendBCMRPi::commitBCMBuffer(uint32_t elementHandle, uint32_t width, uint32_t height)
{
    fprintf(stderr, "ViewBackendBCMRPi::commitBCMBuffer()\n");
    m_callbackData.frameCallback = wl_surface_frame(m_elementData.surface);
    wl_callback_add_listener(m_callbackData.frameCallback, &g_callbackListener, &m_callbackData);

    wl_surface_damage(m_elementData.surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(m_elementData.surface);
    wl_display_flush(m_display.display());

#if 0
    DISPMANX_UPDATE_HANDLE_T updateHandle = vc_dispmanx_update_start(0);

    m_width = width;
    m_height = height;

    VC_RECT_T srcRect, destRect;
    vc_dispmanx_rect_set(&srcRect, 0, 0, m_width << 16, m_height << 16);
    vc_dispmanx_rect_set(&destRect, 0, 0, m_width, m_height);

    vc_dispmanx_element_change_attributes(updateHandle, m_elementHandle, 1 << 3 | 1 << 2, 0, 0, &destRect, &srcRect, 0, DISPMANX_NO_ROTATE);

    vc_dispmanx_update_submit_sync(updateHandle);

    if (m_client)
        m_client->frameComplete();
#endif
}

void ViewBackendBCMRPi::setInputClient(Input::Client* client)
{
    // LibinputServer::singleton().setClient(client);
}

} // namespace ViewBackend

} // namespace WPE

#endif // WPE_BACKEND(BCM_RPI)
