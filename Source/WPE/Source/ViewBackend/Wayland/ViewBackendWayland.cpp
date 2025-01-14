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

#include "Config.h"
#include "ViewBackendWayland.h"

#if WPE_BACKEND(WAYLAND)

#include "WaylandDisplay.h"
#include "ivi-application-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "wayland-drm-client-protocol.h"
#include <WPE/Input/Handling.h>
#include <WPE/ViewBackend/ViewBackend.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <glib.h>
#include <linux/input.h>
#include <locale.h>
#include <sys/mman.h>
#include <unistd.h>

namespace WPE {

namespace ViewBackend {

static const struct wl_pointer_listener g_pointerListener = {
    // enter
    [](void*, struct wl_pointer*, uint32_t, struct wl_surface*, wl_fixed_t, wl_fixed_t) { },
    // leave
    [](void*, struct wl_pointer*, uint32_t, struct wl_surface*) { },
    // motion
    [](void* data, struct wl_pointer*, uint32_t time, wl_fixed_t fixedX, wl_fixed_t fixedY)
    {
        auto x = wl_fixed_to_int(fixedX);
        auto y = wl_fixed_to_int(fixedY);

        auto& seatData = *static_cast<ViewBackendWayland::SeatData*>(data);
        seatData.pointerCoords = { x, y };
        if (seatData.client)
            seatData.client->handlePointerEvent({ Input::PointerEvent::Motion, time, x, y, 0, 0 });
    },
    // button
    [](void* data, struct wl_pointer*, uint32_t, uint32_t time, uint32_t button, uint32_t state)
    {
        if (button >= BTN_MOUSE)
            button = button - BTN_MOUSE + 1;
        else
            button = 0;

        auto& seatData = *static_cast<ViewBackendWayland::SeatData*>(data);
        auto& coords = seatData.pointerCoords;
        if (seatData.client)
            seatData.client->handlePointerEvent(
                { Input::PointerEvent::Button, time, coords.first, coords.second, button, state });
    },
    // axis
    [](void* data, struct wl_pointer*, uint32_t time, uint32_t axis, wl_fixed_t value)
    {
        auto& seatData = *static_cast<ViewBackendWayland::SeatData*>(data);
        auto& coords = seatData.pointerCoords;
        if (seatData.client)
            seatData.client->handleAxisEvent(
                { Input::AxisEvent::Motion, time, coords.first, coords.second, axis, -wl_fixed_to_int(value) });
    },
};

static void
handleKeyEvent(ViewBackendWayland::SeatData& seatData, uint32_t key, uint32_t state, uint32_t time)
{
    auto& xkb = seatData.xkb;
    uint32_t keysym = xkb_state_key_get_one_sym(xkb.state, key);
    uint32_t unicode = xkb_state_key_get_utf32(xkb.state, key);

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED
        && xkb_compose_state_feed(xkb.composeState, keysym) == XKB_COMPOSE_FEED_ACCEPTED
        && xkb_compose_state_get_status(xkb.composeState) == XKB_COMPOSE_COMPOSED)
    {
        keysym = xkb_compose_state_get_one_sym(xkb.composeState);
        unicode = xkb_keysym_to_utf32(keysym);
    }

    if (seatData.client)
        seatData.client->handleKeyboardEvent({ time, keysym, unicode, !!state, xkb.modifiers });
}

static gboolean
repeatRateTimeout(void* data)
{
    auto& seatData = *static_cast<ViewBackendWayland::SeatData*>(data);
    handleKeyEvent(seatData, seatData.repeatData.key, seatData.repeatData.state, seatData.repeatData.time);
    return G_SOURCE_CONTINUE;
}

static gboolean
repeatDelayTimeout(void* data)
{
    auto& seatData = *static_cast<ViewBackendWayland::SeatData*>(data);
    handleKeyEvent(seatData, seatData.repeatData.key, seatData.repeatData.state, seatData.repeatData.time);
    seatData.repeatData.eventSource = g_timeout_add(seatData.repeatInfo.rate, static_cast<GSourceFunc>(repeatRateTimeout), data);
    return G_SOURCE_REMOVE;
}

static const struct wl_keyboard_listener g_keyboardListener = {
    // keymap
    [](void* data, struct wl_keyboard*, uint32_t format, int fd, uint32_t size)
    {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
            close(fd);
            return;
        }

        void* mapping = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        if (mapping == MAP_FAILED) {
            close(fd);
            return;
        }

        auto& xkb = static_cast<ViewBackendWayland::SeatData*>(data)->xkb;
        xkb.keymap = xkb_keymap_new_from_string(xkb.context, static_cast<char*>(mapping),
            XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        munmap(mapping, size);
        close(fd);

        if (!xkb.keymap)
            return;

        xkb.state = xkb_state_new(xkb.keymap);
        if (!xkb.state)
            return;

        xkb.indexes.control = xkb_keymap_mod_get_index(xkb.keymap, XKB_MOD_NAME_CTRL);
        xkb.indexes.alt = xkb_keymap_mod_get_index(xkb.keymap, XKB_MOD_NAME_ALT);
        xkb.indexes.shift = xkb_keymap_mod_get_index(xkb.keymap, XKB_MOD_NAME_SHIFT);
    },
    // enter
    [](void*, struct wl_keyboard*, uint32_t, wl_surface*, struct wl_array*) { },
    // leave
    [](void*, struct wl_keyboard*, uint32_t, struct wl_surface*) { },
    // key
    [](void* data, struct wl_keyboard*, uint32_t, uint32_t time, uint32_t key, uint32_t state)
    {
        // IDK.
        key += 8;

        auto& seatData = *static_cast<ViewBackendWayland::SeatData*>(data);
        handleKeyEvent(seatData, key, state, time);

        if (!seatData.repeatInfo.rate)
            return;

        if (state == WL_KEYBOARD_KEY_STATE_RELEASED
            && seatData.repeatData.key == key) {
            if (seatData.repeatData.eventSource)
                g_source_remove(seatData.repeatData.eventSource);
            seatData.repeatData = { 0, 0, 0, 0 };
        } else if (state == WL_KEYBOARD_KEY_STATE_PRESSED
            && xkb_keymap_key_repeats(seatData.xkb.keymap, key)) {

            if (seatData.repeatData.eventSource)
                g_source_remove(seatData.repeatData.eventSource);

            seatData.repeatData = { key, time, state, g_timeout_add(seatData.repeatInfo.delay, static_cast<GSourceFunc>(repeatDelayTimeout), data) };
        }
    },
    // modifiers
    [](void* data, struct wl_keyboard*, uint32_t, uint32_t depressedMods, uint32_t latchedMods, uint32_t lockedMods, uint32_t group)
    {
        auto& xkb = static_cast<ViewBackendWayland::SeatData*>(data)->xkb;
        xkb_state_update_mask(xkb.state, depressedMods, latchedMods, lockedMods, 0, 0, group);

        auto& modifiers = xkb.modifiers;
        modifiers = 0;
        auto component = static_cast<xkb_state_component>(XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
        if (xkb_state_mod_index_is_active(xkb.state, xkb.indexes.control, component))
            modifiers |= Input::KeyboardEvent::Control;
        if (xkb_state_mod_index_is_active(xkb.state, xkb.indexes.alt, component))
            modifiers |= Input::KeyboardEvent::Alt;
        if (xkb_state_mod_index_is_active(xkb.state, xkb.indexes.shift, component))
            modifiers |= Input::KeyboardEvent::Shift;
    },
    // repeat_info
    [](void* data, struct wl_keyboard*, int32_t rate, int32_t delay)
    {
        auto& repeatInfo = static_cast<ViewBackendWayland::SeatData*>(data)->repeatInfo;
        repeatInfo = { rate, delay };

        // A rate of zero disables any repeating.
        if (!rate) {
            auto& repeatData = static_cast<ViewBackendWayland::SeatData*>(data)->repeatData;
            if (repeatData.eventSource) {
                g_source_remove(repeatData.eventSource);
                repeatData = { 0, 0, 0, 0 };
            }
        }
    },
};

static const struct wl_seat_listener g_seatListener = {
    // capabilities
    [](void* data, struct wl_seat* seat, uint32_t capabilities)
    {
        auto& seatData = *static_cast<ViewBackendWayland::SeatData*>(data);

        // WL_SEAT_CAPABILITY_POINTER
        const bool hasPointerCap = capabilities & WL_SEAT_CAPABILITY_POINTER;
        if (hasPointerCap && !seatData.pointer) {
            seatData.pointer = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(seatData.pointer, &g_pointerListener, &seatData);
        }
        if (!hasPointerCap && seatData.pointer) {
            wl_pointer_destroy(seatData.pointer);
            seatData.pointer = nullptr;
        }

        // WL_SEAT_CAPABILITY_KEYBOARD
        const bool hasKeyboardCap = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
        if (hasKeyboardCap && !seatData.keyboard) {
            seatData.keyboard = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(seatData.keyboard, &g_keyboardListener, &seatData);
        }
        if (!hasKeyboardCap && seatData.keyboard) {
            wl_keyboard_destroy(seatData.keyboard);
            seatData.keyboard = nullptr;
        }
    },
    // name
    [](void*, struct wl_seat*, const char*) { }
};

static const struct xdg_surface_listener g_xdgSurfaceListener = {
    // configure
    [](void*, struct xdg_surface* surface, int32_t, int32_t, struct wl_array*, uint32_t serial)
    {
        xdg_surface_ack_configure(surface, serial);
    },
    // delete
    [](void*, struct xdg_surface*) { },
};

static const struct ivi_surface_listener g_iviSurfaceListener = {
    // configure
    [](void* data, struct ivi_surface*, int32_t width, int32_t height)
    {
        auto& resizingData = *static_cast<ViewBackendWayland::ResizingData*>(data);
        if (resizingData.client)
            resizingData.client->setSize(std::max(0, width), std::max(0, height));
    },
};

const struct wl_buffer_listener g_bufferListener = {
    // release
    [](void* data, struct wl_buffer* buffer)
    {
        auto& bufferData = *static_cast<ViewBackendWayland::BufferListenerData*>(data);
        auto& bufferMap = bufferData.map;

        auto it = std::find_if(bufferMap.begin(), bufferMap.end(),
            [buffer] (const std::pair<uint32_t, struct wl_buffer*>& entry) { return entry.second == buffer; });

        if (it == bufferMap.end())
            return;

        if (bufferData.client)
            bufferData.client->releaseBuffer(it->first);
    },
};

const struct wl_callback_listener g_callbackListener = {
    // frame
    [](void* data, struct wl_callback* callback, uint32_t)
    {
        auto& callbackData = *static_cast<ViewBackendWayland::CallbackListenerData*>(data);
        if (callbackData.client)
            callbackData.client->frameComplete();
        callbackData.frameCallback = nullptr;
        wl_callback_destroy(callback);
    },
};

ViewBackendWayland::ViewBackendWayland()
    : m_display(WaylandDisplay::singleton())
{
    m_surface = wl_compositor_create_surface(m_display.interfaces().compositor);

    wl_seat_add_listener(m_display.interfaces().seat, &g_seatListener, &m_seatData);
    m_seatData.xkb.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    m_seatData.xkb.composeTable = xkb_compose_table_new_from_locale(m_seatData.xkb.context, setlocale(LC_CTYPE, nullptr), XKB_COMPOSE_COMPILE_NO_FLAGS);
    m_seatData.xkb.composeState = xkb_compose_state_new(m_seatData.xkb.composeTable, XKB_COMPOSE_STATE_NO_FLAGS);

    if (m_display.interfaces().xdg) {
        m_xdgSurface = xdg_shell_get_xdg_surface(m_display.interfaces().xdg, m_surface);
        xdg_surface_add_listener(m_xdgSurface, &g_xdgSurfaceListener, nullptr);
        xdg_surface_set_title(m_xdgSurface, "WPE");
    }

    if (m_display.interfaces().ivi_application) {
        m_iviSurface = ivi_application_surface_create(m_display.interfaces().ivi_application,
            4200 + getpid(), // a unique identifier
            m_surface);
        ivi_surface_add_listener(m_iviSurface, &g_iviSurfaceListener, &m_resizingData);
    }
}

ViewBackendWayland::~ViewBackendWayland()
{
    if (m_callbackData.frameCallback)
        wl_callback_destroy(m_callbackData.frameCallback);
    m_callbackData = { nullptr, nullptr };

    m_bufferData = { nullptr, decltype(m_bufferData.map){ } };

    m_resizingData = { nullptr, 0, 0 };

    if (m_seatData.pointer)
        wl_pointer_destroy(m_seatData.pointer);
    if (m_seatData.keyboard)
        wl_keyboard_destroy(m_seatData.keyboard);
    if (m_seatData.xkb.context)
        xkb_context_unref(m_seatData.xkb.context);
    if (m_seatData.xkb.keymap)
        xkb_keymap_unref(m_seatData.xkb.keymap);
    if (m_seatData.xkb.state)
        xkb_state_unref(m_seatData.xkb.state);
    if (m_seatData.xkb.composeTable)
        xkb_compose_table_unref(m_seatData.xkb.composeTable);
    if (m_seatData.xkb.composeState)
        xkb_compose_state_unref(m_seatData.xkb.composeState);
    if (m_seatData.repeatData.eventSource)
        g_source_remove(m_seatData.repeatData.eventSource);

    m_seatData = { nullptr, nullptr, nullptr, { 0, 0},
        { nullptr, nullptr, nullptr, { 0, 0, 0 }, 0, nullptr, nullptr }, { 0, 0}, { 0, 0, 0, 0} };

    if (m_iviSurface)
        ivi_surface_destroy(m_iviSurface);
    m_iviSurface = nullptr;
    if (m_xdgSurface)
        xdg_surface_destroy(m_xdgSurface);
    m_xdgSurface = nullptr;
    if (m_surface)
        wl_surface_destroy(m_surface);
    m_surface = nullptr;
}

void ViewBackendWayland::setClient(Client* client)
{
    m_bufferData.client = client;
    m_callbackData.client = client;
    m_resizingData.client = client;
}

void ViewBackendWayland::commitPrimeBuffer(int fd, uint32_t handle, uint32_t width, uint32_t height, uint32_t stride, uint32_t)
{
    struct wl_buffer* buffer = nullptr;
    auto& bufferMap = m_bufferData.map;
    auto it = bufferMap.find(handle);

    if (fd >= 0) {
        buffer = wl_drm_create_prime_buffer(m_display.interfaces().drm, fd, width, height, WL_DRM_FORMAT_ARGB8888, 0, stride, 0, 0, 0, 0);
        wl_buffer_add_listener(buffer, &g_bufferListener, &m_bufferData);

        if (it != bufferMap.end()) {
            wl_buffer_destroy(it->second);
            it->second = buffer;
        } else
            bufferMap.insert({ handle, buffer });
    } else {
        assert(it != bufferMap.end());
        buffer = it->second;
    }

    if (!buffer) {
        fprintf(stderr, "ViewBackendWayland: failed to create/find a buffer for PRIME handle %u\n", handle);
        return;
    }

    m_callbackData.frameCallback = wl_surface_frame(m_surface);
    wl_callback_add_listener(m_callbackData.frameCallback, &g_callbackListener, &m_callbackData);

    wl_surface_attach(m_surface, buffer, 0, 0);
    wl_surface_damage(m_surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(m_surface);
    wl_display_flush(m_display.display());
}

void ViewBackendWayland::destroyPrimeBuffer(uint32_t handle)
{
    auto& bufferMap = m_bufferData.map;
    auto it = bufferMap.find(handle);
    assert(it != bufferMap.end());

    wl_buffer_destroy(it->second);
    bufferMap.erase(it);
}

void ViewBackendWayland::setInputClient(Input::Client* client)
{
    m_seatData.client = client;
}

} // namespace ViewBackend

} // namespace WPE

#endif // WPE_BACKEND(WAYLAND)
