/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "config.h"
#include "MediaDevicesRequest.h"

#if ENABLE(MEDIA_STREAM)

#include "Document.h"
#include "ExceptionCode.h"
#include "Frame.h"
#include "JSMediaDeviceInfo.h"
#include "RealtimeMediaSourceCenter.h"
#include "SecurityOrigin.h"
#include <wtf/MainThread.h>

namespace WebCore {

RefPtr<MediaDevicesRequest> MediaDevicesRequest::create(Document* document, MediaDevices::EnumerateDevicesPromise&& promise, ExceptionCode&)
{
    return adoptRef(*new MediaDevicesRequest(document, WTF::move(promise)));
}

MediaDevicesRequest::MediaDevicesRequest(ScriptExecutionContext* context, MediaDevices::EnumerateDevicesPromise&& promise)
    : ContextDestructionObserver(context)
    , m_promise(WTF::move(promise))
{
}

MediaDevicesRequest::~MediaDevicesRequest()
{
}

SecurityOrigin* MediaDevicesRequest::securityOrigin() const
{
    if (scriptExecutionContext())
        return scriptExecutionContext()->securityOrigin();

    return nullptr;
}

void MediaDevicesRequest::contextDestroyed()
{
    ContextDestructionObserver::contextDestroyed();
    m_protector = nullptr;
}

void MediaDevicesRequest::start()
{
    m_protector = this;
    RealtimeMediaSourceCenter::singleton().getMediaStreamTrackSources(this);
}

void MediaDevicesRequest::didCompleteRequest(const TrackSourceInfoVector& capturedDevices)
{
    if (!m_scriptExecutionContext)
        return;

    Vector<RefPtr<MediaDeviceInfo>> deviceInfo;
    for (auto device : capturedDevices) {
        TrackSourceInfo* trackInfo = device.get();
        String deviceType = trackInfo->kind() == TrackSourceInfo::SourceKind::Audio ? MediaDeviceInfo::audioInputType() : MediaDeviceInfo::videoInputType();

        // FIXME: label is supposed to be empty unless a device is attached to an active MediaStreamTrack in the current browsing context, or
        // persistent permission to access these local devices has been granted to the page's origin.
        deviceInfo.append(MediaDeviceInfo::create(m_scriptExecutionContext, trackInfo->label(), trackInfo->id(), trackInfo->groupId(), deviceType));
    }

    RefPtr<MediaDevicesRequest> protectedThis(this);
    callOnMainThread([protectedThis, deviceInfo] {
        protectedThis->m_promise.resolve(deviceInfo);
    });
    m_protector = nullptr;

}

const String& MediaDevicesRequest::requestOrigin() const
{
    if (scriptExecutionContext()) {
        Document* document = downcast<Document>(scriptExecutionContext());
        if (document)
            return document->url();
    }

    return emptyString();
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
