/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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

#ifndef WebContextMenuProxyMac_h
#define WebContextMenuProxyMac_h

#if PLATFORM(MAC)

#include "WebContextMenuProxy.h"
#include <wtf/RetainPtr.h>

OBJC_CLASS NSPopUpButtonCell;
OBJC_CLASS NSWindow;
OBJC_CLASS WKView;

namespace WebKit {

class ShareableBitmap;
class UserData;
class WebContextMenuItemData;
class WebPageProxy;

class WebContextMenuProxyMac : public WebContextMenuProxy {
public:
    WebContextMenuProxyMac(WKView*, WebPageProxy&, const ContextMenuContextData&, const UserData&);
    ~WebContextMenuProxyMac();

    void contextMenuItemSelected(const WebContextMenuItemData&);

#if ENABLE(SERVICE_CONTROLS)
    void clearServicesMenu();
#endif

    WebPageProxy& page() const { return m_page; }
    NSWindow *window() const;

private:
    virtual void show() override;

    RetainPtr<NSMenuItem> createContextMenuItem(const WebContextMenuItemData&);
    RetainPtr<NSMenu> createContextMenuFromItems(const Vector<WebContextMenuItemData>&);
    void showContextMenu();

#if ENABLE(SERVICE_CONTROLS)
    void showServicesMenu();
    void setupServicesMenu();
    WebCore::ContextMenuItem shareMenuItem();
#endif

    RetainPtr<NSMenu> m_menu;

    WKView* m_webView;
    WebPageProxy& m_page;
};

} // namespace WebKit

#endif // PLATFORM(MAC)

#endif // WebContextMenuProxyMac_h
