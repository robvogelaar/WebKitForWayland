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

#include "config.h"
#include "CompositingManager.h"

#include "CompositingManagerMessages.h"
#include "CompositingManagerProxyMessages.h"
#include "WebPage.h"
#include "WebProcess.h"

namespace WebKit {

CompositingManager::CompositingManager(Client& client)
    : m_client(client)
{
}

void CompositingManager::establishConnection(WebPage& webPage, WTF::RunLoop& runLoop)
{
    IPC::Connection::SocketPair socketPair = IPC::Connection::createPlatformConnection();
    IPC::Connection::Identifier connectionIdentifier(socketPair.server);
    IPC::Attachment connectionClientPort(socketPair.client);

    m_connection = IPC::Connection::createServerConnection(connectionIdentifier, *this, runLoop);
    m_connection->open();
    webPage.sendSync(Messages::CompositingManagerProxy::EstablishConnection(connectionClientPort),
        Messages::CompositingManagerProxy::EstablishConnection::Reply(), webPage.pageID());
}

#if PLATFORM(GBM)
void CompositingManager::commitPrimeBuffer(const WebCore::PlatformDisplayGBM::GBMBufferExport& bufferExport)
{
    m_connection->send(Messages::CompositingManagerProxy::CommitPrimeBuffer(
        std::get<1>(bufferExport),
        std::get<2>(bufferExport),
        std::get<3>(bufferExport),
        std::get<4>(bufferExport),
        std::get<5>(bufferExport),
        IPC::Attachment(std::get<0>(bufferExport))), 0);
}

void CompositingManager::destroyPrimeBuffer(uint32_t handle)
{
    m_connection->send(Messages::CompositingManagerProxy::DestroyPrimeBuffer(handle), 0);
}
#endif // PLATFORM(GBM)

#if PLATFORM(BCM_RPI)
uint32_t CompositingManager::createBCMElement(int32_t width, int32_t height)
{
    uint32_t handle = 0;
    m_connection->sendSync(Messages::CompositingManagerProxy::CreateBCMElement(width, height), Messages::CompositingManagerProxy::CreateBCMElement::Reply(handle), 0);
    return handle;
}

void CompositingManager::commitBCMBuffer(const WebCore::PlatformDisplayBCMRPi::BCMBufferExport& bufferExport)
{
    m_connection->send(Messages::CompositingManagerProxy::CommitBCMBuffer(
        std::get<0>(bufferExport),
        std::get<1>(bufferExport),
        std::get<2>(bufferExport)), 0);
}
#endif // PLATFORM(BCM_RPI)

#if PLATFORM(BCM_NEXUS)
uint32_t CompositingManager::createBCMNexusElement(int32_t width, int32_t height)
{
    uint32_t handle = 0;
    m_connection->sendSync(Messages::CompositingManagerProxy::CreateBCMNexusElement(width, height), Messages::CompositingManagerProxy::CreateBCMNexusElement::Reply(handle), 0);
    return handle;
}

void CompositingManager::commitBCMNexusBuffer(const WebCore::PlatformDisplayBCMNexus::BufferExport& bufferExport)
{
    m_connection->send(Messages::CompositingManagerProxy::CommitBCMNexusBuffer(
        std::get<0>(bufferExport),
        std::get<1>(bufferExport),
        std::get<2>(bufferExport)), 0);
}
#endif

#if PLATFORM(INTEL_CE)
uint32_t CompositingManager::createIntelCEElement(int32_t width, int32_t height)
{
    uint32_t handle = 0;
    m_connection->sendSync(Messages::CompositingManagerProxy::CreateIntelCEElement(width, height), Messages::CompositingManagerProxy::CreateIntelCEElement::Reply(handle), 0);
    return handle;
}

void CompositingManager::commitIntelCEBuffer(const WebCore::PlatformDisplayIntelCE::BufferExport& bufferExport)
{
    m_connection->send(Messages::CompositingManagerProxy::CommitIntelCEBuffer(
        std::get<0>(bufferExport),
        std::get<1>(bufferExport),
        std::get<2>(bufferExport)), 0);
}
#endif

void CompositingManager::releaseBuffer(uint32_t handle)
{
    m_client.releaseBuffer(handle);
}

void CompositingManager::frameComplete()
{
    m_client.frameComplete();
}

} // namespace WebKit
