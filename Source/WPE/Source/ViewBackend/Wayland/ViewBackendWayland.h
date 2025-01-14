/*
 * Copyright (C) 2015 Igalia S.L.
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

#ifndef WPE_ViewBackend_ViewBackendWayland_h
#define WPE_ViewBackend_ViewBackendWayland_h

#if WPE_BACKEND(WAYLAND)

#include <WPE/ViewBackend/ViewBackend.h>
#include <unordered_map>
#include <utility>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

struct ivi_surface;
struct wl_buffer;
struct wl_callback;
struct wl_keyboard;
struct wl_pointer;
struct wl_surface;
struct xdg_surface;

namespace WPE {

namespace Input {
class Client;
}

namespace ViewBackend {

class Client;
class WaylandDisplay;

class ViewBackendWayland final : public ViewBackend {
public:
    ViewBackendWayland();
    virtual ~ViewBackendWayland();

    void setClient(Client* client) override;
    void commitPrimeBuffer(int fd, uint32_t handle, uint32_t width, uint32_t height, uint32_t stride, uint32_t format) override;
    void destroyPrimeBuffer(uint32_t handle) override;

    void setInputClient(Input::Client*) override;

    struct SeatData {
        Input::Client* client;
        struct wl_pointer* pointer;
        struct wl_keyboard* keyboard;

        std::pair<int, int> pointerCoords;

        struct {
            struct xkb_context* context;
            struct xkb_keymap* keymap;
            struct xkb_state* state;
            struct {
                xkb_mod_index_t control;
                xkb_mod_index_t alt;
                xkb_mod_index_t shift;
            } indexes;
            uint8_t modifiers;
            struct xkb_compose_table* composeTable;
            struct xkb_compose_state* composeState;
        } xkb;

        struct {
            int32_t rate;
            int32_t delay;
        } repeatInfo;

        struct {
            uint32_t key;
            uint32_t time;
            uint32_t state;
            uint32_t eventSource;
        } repeatData;
    };

    struct BufferListenerData {
        Client* client;
        std::unordered_map<uint32_t, struct wl_buffer*> map;
    };

    struct CallbackListenerData {
        Client* client;
        struct wl_callback* frameCallback;
    };

    struct ResizingData {
        Client* client;
        uint32_t width;
        uint32_t height;
    };

private:
    const WaylandDisplay& m_display;

    struct wl_surface* m_surface;
    struct xdg_surface* m_xdgSurface;
    struct ivi_surface* m_iviSurface;

    SeatData m_seatData;
    BufferListenerData m_bufferData;
    CallbackListenerData m_callbackData;
    ResizingData m_resizingData;
};

} // namespace ViewBackend

} // namespace WPE

#endif // WPE_BACKEND(WAYLAND)

#endif // WPE_ViewBackend_ViewBackendWayland_h
