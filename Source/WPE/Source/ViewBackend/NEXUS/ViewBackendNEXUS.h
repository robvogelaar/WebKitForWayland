/*
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

#ifndef WPE_ViewBackend_ViewBackendNexus_h
#define WPE_ViewBackend_ViewBackendNexus_h

#if WPE_BACKEND(BCM_NEXUS)

#include <WPE/ViewBackend/ViewBackend.h>

namespace WPE {

namespace ViewBackend {

class ViewBackendNexus final : public ViewBackend {
public:
    ViewBackendNexus();
    virtual ~ViewBackendNexus();

    void setClient(Client*) override;
    uint32_t createBCMNexusElement(int32_t width, int32_t height) override;
    void commitBCMNexusBuffer(uint32_t handle, uint32_t width, uint32_t height) override;

    void setInputClient(Input::Client*) override;

private:
    Client* m_client;

    uint32_t m_width;
    uint32_t m_height;
};

} // namespace ViewBackend

} // namespace WPE


#endif // WPE_BACKEND(NEXUS)

#endif // WPE_ViewBackend_ViewBackendNexus_h
