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

#ifndef CompositingManager_h
#define CompositingManager_h

#include "Connection.h"
#include "MessageReceiver.h"

#if PLATFORM(GBM)
#include <WebCore/PlatformDisplayGBM.h>
#endif

#if PLATFORM(BCM_RPI)
#include <WebCore/PlatformDisplayBCMRPi.h>
#endif

#if PLATFORM(BCM_NEXUS)
#include <WebCore/PlatformDisplayBCMNexus.h>
#endif

#if PLATFORM(INTEL_CE)
#include <WebCore/PlatformDisplayIntelCE.h>
#endif

namespace WebKit {

class WebPage;

class CompositingManager final : public IPC::Connection::Client {
public:
    class Client {
    public:
        virtual void releaseBuffer(uint32_t) = 0;
        virtual void frameComplete() = 0;
    };

    CompositingManager(Client&);

    void establishConnection(WebPage&, WTF::RunLoop&);

#if PLATFORM(GBM)
    void commitPrimeBuffer(const WebCore::PlatformDisplayGBM::GBMBufferExport&);
    void destroyPrimeBuffer(uint32_t);
#endif

#if PLATFORM(BCM_RPI)
    uint32_t createBCMElement(int32_t width, int32_t height);
    void commitBCMBuffer(const WebCore::PlatformDisplayBCMRPi::BCMBufferExport&);
    // void destroyBCMBuffer(...);
#endif

#if PLATFORM(BCM_NEXUS)
    uint32_t createBCMNexusElement(int32_t width, int32_t height);
    void commitBCMNexusBuffer(const WebCore::PlatformDisplayBCMNexus::BufferExport&);
#endif

#if PLATFORM(INTEL_CE)
    uint32_t createIntelCEElement(int32_t width, int32_t height);
    void commitIntelCEBuffer(const WebCore::PlatformDisplayIntelCE::BufferExport&);
#endif

    CompositingManager(const CompositingManager&) = delete;
    CompositingManager& operator=(const CompositingManager&) = delete;
    CompositingManager(CompositingManager&&) = delete;
    CompositingManager& operator=(CompositingManager&&) = delete;

private:
    // IPC::MessageReceiver
    virtual void didReceiveMessage(IPC::Connection&, IPC::MessageDecoder&) override;

    virtual void releaseBuffer(uint32_t) final;
    virtual void frameComplete() final;

    // IPC::Connection::Client
    void didClose(IPC::Connection&) override { }
    void didReceiveInvalidMessage(IPC::Connection&, IPC::StringReference, IPC::StringReference) override { }
    IPC::ProcessType localProcessType() override { return IPC::ProcessType::Web; }
    IPC::ProcessType remoteProcessType() override { return IPC::ProcessType::UI; }

    Client& m_client;

    RefPtr<IPC::Connection> m_connection;
};

} // namespace WebKit

#endif // CompositingManager
