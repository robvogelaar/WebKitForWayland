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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "PlatformDisplayGBM.h"

#if PLATFORM(GBM)

#include "GLContextEGL.h"
#include "IntSize.h"
#include <fcntl.h>
#include <gbm.h>
#include <unistd.h>
#include <xf86drm.h>

#include <cstdio>

namespace WebCore {

PlatformDisplayGBM::PlatformDisplayGBM()
{
    m_gbm.fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
    if (m_gbm.fd < 0) {
        fprintf(stderr, "PlatformDisplayGBM: cannot open the render node\n");
        return;
    }

    m_gbm.device = gbm_create_device(m_gbm.fd);
    if (!m_gbm.device) {
        fprintf(stderr, "PlatformDisplayGBM: cannot create the GBM device\n");
        close(m_gbm.fd);
        m_gbm.fd = -1;
        return;
    }

    m_eglDisplay = eglGetDisplay(m_gbm.device);
    if (m_eglDisplay == EGL_NO_DISPLAY) {
        fprintf(stderr, "PlatformDisplayGBM: cannot create the EGL display\n");
        return;
    }

    PlatformDisplay::initializeEGLDisplay();
}

PlatformDisplayGBM::~PlatformDisplayGBM()
{
    if (m_gbm.device)
        gbm_device_destroy(m_gbm.device);
    if (m_gbm.fd >= 0)
        close(m_gbm.fd);
    m_gbm = { -1, nullptr };
}

std::unique_ptr<GLContextEGL> PlatformDisplayGBM::createOffscreenContext(GLContext* sharingContext)
{
    class OffscreenContextData : public GLContext::Data {
    public:
        virtual ~OffscreenContextData()
        {
            gbm_surface_destroy(surface);
        }

        struct gbm_surface* surface;
    };

    auto contextData = std::make_unique<OffscreenContextData>();
    contextData->surface = gbm_surface_create(m_gbm.device, 1, 1, GBM_FORMAT_ARGB8888, 0);

    auto* surface = contextData->surface;
    return GLContextEGL::createWindowContext(surface, sharingContext, WTF::move(contextData));
}

} // namespace WebCore

#endif // PLATFORM(GBM)
