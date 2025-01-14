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

#ifndef WebViewImpl_h
#define WebViewImpl_h

#if PLATFORM(MAC)

#include "PluginComplexTextInputState.h"
#include "WKLayoutMode.h"
#include "WebPageProxy.h"
#include <WebCore/TextIndicatorWindow.h>
#include <functional>
#include <wtf/RetainPtr.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/WTFString.h>

OBJC_CLASS NSImmediateActionGestureRecognizer;
OBJC_CLASS NSTextInputContext;
OBJC_CLASS NSView;
OBJC_CLASS WKEditorUndoTargetObjC;
OBJC_CLASS WKFullScreenWindowController;
OBJC_CLASS WKImmediateActionController;
OBJC_CLASS WKViewLayoutStrategy;
OBJC_CLASS WKWindowVisibilityObserver;
OBJC_CLASS _WKThumbnailView;

@protocol WebViewImplDelegate

- (NSTextInputContext *)_superInputContext;
- (void)_superQuickLookWithEvent:(NSEvent *)event;
- (void)_superRemoveTrackingRect:(NSTrackingRectTag)tag;
- (void)_superSwipeWithEvent:(NSEvent *)event;
- (void)_superMagnifyWithEvent:(NSEvent *)event;
- (void)_superSmartMagnifyWithEvent:(NSEvent *)event;
- (id)_superAccessibilityAttributeValue:(NSString *)attribute;

// This is a hack; these things live can live on a category (e.g. WKView (Private)) but WKView itself conforms to this protocol.
// They're not actually optional.
@optional

- (id)_immediateActionAnimationControllerForHitTestResult:(WKHitTestResultRef)hitTestResult withType:(uint32_t)type userData:(WKTypeRef)userData;
- (void)_prepareForImmediateActionAnimation;
- (void)_cancelImmediateActionAnimation;
- (void)_completeImmediateActionAnimation;
- (void)_dismissContentRelativeChildWindows;
- (void)_dismissContentRelativeChildWindowsWithAnimation:(BOOL)animate;
- (void)_gestureEventWasNotHandledByWebCore:(NSEvent *)event;

@end

namespace WebKit {

class ViewGestureController;
class WebEditCommandProxy;
class WebPageProxy;
struct ColorSpaceData;

class WebViewImpl {
    WTF_MAKE_FAST_ALLOCATED;
    WTF_MAKE_NONCOPYABLE(WebViewImpl);
public:
    WebViewImpl(NSView <WebViewImplDelegate> *, WebPageProxy&, PageClient&);

    ~WebViewImpl();

    void setDrawsBackground(bool);
    bool drawsBackground() const;
    void setDrawsTransparentBackground(bool);
    bool drawsTransparentBackground() const;

    bool acceptsFirstResponder();
    bool becomeFirstResponder();
    bool resignFirstResponder();
    bool isFocused() const;

    void viewWillStartLiveResize();
    void viewDidEndLiveResize();

    void renewGState();
    void setFrameSize(CGSize);
    void disableFrameSizeUpdates();
    void enableFrameSizeUpdates();
    bool frameSizeUpdatesDisabled() const;
    void setFrameAndScrollBy(CGRect, CGSize);
    void updateWindowAndViewFrames();

    void setFixedLayoutSize(CGSize);
    CGSize fixedLayoutSize() const;

    void setDrawingAreaSize(CGSize);

    void setAutomaticallyAdjustsContentInsets(bool);
    bool automaticallyAdjustsContentInsets() const { return m_automaticallyAdjustsContentInsets; }
    void updateContentInsetsIfAutomatic();
    void setTopContentInset(CGFloat);
    CGFloat topContentInset() const { return m_topContentInset; }

    void setContentPreparationRect(CGRect);
    void updateViewExposedRect();
    void setClipsToVisibleRect(bool);
    bool clipsToVisibleRect() const { return m_clipsToVisibleRect; }

    void setIntrinsicContentSize(CGSize);
    CGSize intrinsicContentSize() const;

    void setViewScale(CGFloat);
    CGFloat viewScale() const;

    WKLayoutMode layoutMode() const;
    void setLayoutMode(WKLayoutMode);
    void updateSupportsArbitraryLayoutModes();

    void setOverrideDeviceScaleFactor(CGFloat);
    CGFloat overrideDeviceScaleFactor() const { return m_overrideDeviceScaleFactor; }

    void windowDidOrderOffScreen();
    void windowDidOrderOnScreen();
    void windowDidBecomeKey(NSWindow *);
    void windowDidResignKey(NSWindow *);
    void windowDidMiniaturize();
    void windowDidDeminiaturize();
    void windowDidMove();
    void windowDidResize();
    void windowDidChangeBackingProperties(CGFloat oldBackingScaleFactor);
    void windowDidChangeScreen();
    void windowDidChangeLayerHosting();
    void windowDidChangeOcclusionState();

    void viewWillMoveToWindow(NSWindow *);
    void viewDidMoveToWindow();
    void viewDidChangeBackingProperties();

    ColorSpaceData colorSpace();

    void beginDeferringViewInWindowChanges();
    // FIXME: Merge these two?
    void endDeferringViewInWindowChanges();
    void endDeferringViewInWindowChangesSync();
    bool isDeferringViewInWindowChanges() const { return m_shouldDeferViewInWindowChanges; }

    void prepareForMoveToWindow(NSWindow *targetWindow, std::function<void()> completionHandler);
    NSWindow *targetWindowForMovePreparation() const { return m_targetWindowForMovePreparation; }

    void updateSecureInputState();
    void resetSecureInputState();
    bool inSecureInputState() const { return m_inSecureInputState; }
    void notifyInputContextAboutDiscardedComposition();
    void setPluginComplexTextInputStateAndIdentifier(PluginComplexTextInputState, uint64_t identifier);
    void disableComplexTextInputIfNecessary();
    bool handlePluginComplexTextInputKeyDown(NSEvent *);
    bool tryHandlePluginComplexTextInputKeyDown(NSEvent *);
    void pluginFocusOrWindowFocusChanged(bool pluginHasFocusAndWindowHasFocus, uint64_t pluginComplexTextInputIdentifier);
    bool tryPostProcessPluginComplexTextInputKeyDown(NSEvent *);
    PluginComplexTextInputState pluginComplexTextInputState() const { return m_pluginComplexTextInputState; }
    uint64_t pluginComplexTextInputIdentifier() const { return m_pluginComplexTextInputIdentifier; }
    
    void handleAcceptedAlternativeText(const String&);
    NSInteger spellCheckerDocumentTag();

    void pressureChangeWithEvent(NSEvent *);
    NSEvent *lastPressureEvent() { return m_lastPressureEvent.get(); }

#if ENABLE(FULLSCREEN_API)
    bool hasFullScreenWindowController() const;
    WKFullScreenWindowController *fullScreenWindowController();
    void closeFullScreenWindowController();
#endif
    NSView *fullScreenPlaceholderView();
    NSWindow *createFullScreenWindow();

    bool isEditable() const;
    void executeEditCommand(const String& commandName, const String& argument = String());
    void registerEditCommand(RefPtr<WebEditCommandProxy>, WebPageProxy::UndoOrRedo);
    void clearAllEditCommands();
    bool writeSelectionToPasteboard(NSPasteboard *, NSArray *types);
    void centerSelectionInVisibleArea();
    void selectionDidChange();
    void startObservingFontPanel();
    void updateFontPanelIfNeeded();

    void preferencesDidChange();

    void setTextIndicator(WebCore::TextIndicator&, WebCore::TextIndicatorWindowLifetime = WebCore::TextIndicatorWindowLifetime::Permanent);
    void clearTextIndicatorWithAnimation(WebCore::TextIndicatorWindowDismissalAnimation);
    void setTextIndicatorAnimationProgress(float);
    void dismissContentRelativeChildWindows();
    void dismissContentRelativeChildWindowsFromViewOnly();
    void dismissContentRelativeChildWindowsWithAnimation(bool);
    void dismissContentRelativeChildWindowsWithAnimationFromViewOnly(bool);

    void quickLookWithEvent(NSEvent *);
    void prepareForDictionaryLookup();
    void setAllowsLinkPreview(bool);
    bool allowsLinkPreview() const { return m_allowsLinkPreview; }
    void* immediateActionAnimationControllerForHitTestResult(WKHitTestResultRef, uint32_t type, WKTypeRef userData);
    void* immediateActionAnimationControllerForHitTestResultFromViewOnly(WKHitTestResultRef, uint32_t type, WKTypeRef userData);
    void didPerformImmediateActionHitTest(const WebHitTestResultData&, bool contentPreventsDefault, API::Object* userData);
    void prepareForImmediateActionAnimation();
    void cancelImmediateActionAnimation();
    void completeImmediateActionAnimation();

    void setIgnoresNonWheelEvents(bool);
    bool ignoresNonWheelEvents() const { return m_ignoresNonWheelEvents; }
    void setIgnoresAllEvents(bool);
    bool ignoresAllEvents() const { return m_ignoresAllEvents; }
    void setIgnoresMouseDraggedEvents(bool);
    bool ignoresMouseDraggedEvents() const { return m_ignoresMouseDraggedEvents; }

    void setAccessibilityWebProcessToken(NSData *);
    void accessibilityRegisterUIProcessTokens();
    void updateRemoteAccessibilityRegistration(bool registerProcess);
    id accessibilityFocusedUIElement();
    bool accessibilityIsIgnored() const { return false; }
    id accessibilityHitTest(CGPoint);
    void enableAccessibilityIfNecessary();
    id accessibilityAttributeValue(NSString *);

    NSTrackingArea *primaryTrackingArea() const { return m_primaryTrackingArea.get(); }
    void setPrimaryTrackingArea(NSTrackingArea *);

    NSTrackingRectTag addTrackingRect(CGRect, id owner, void* userData, bool assumeInside);
    NSTrackingRectTag addTrackingRectWithTrackingNum(CGRect, id owner, void* userData, bool assumeInside, int tag);
    void addTrackingRectsWithTrackingNums(CGRect*, id owner, void** userDataList, bool assumeInside, NSTrackingRectTag *trackingNums, int count);
    void removeTrackingRect(NSTrackingRectTag);
    void removeTrackingRects(NSTrackingRectTag *, int count);
    NSString *stringForToolTip(NSToolTipTag tag);
    void toolTipChanged(const String& oldToolTip, const String& newToolTip);

    void setAcceleratedCompositingRootLayer(CALayer *);
    CALayer *acceleratedCompositingRootLayer() const { return m_rootLayer.get(); }
    NSView *layerHostingView() const { return m_layerHostingView.get(); }

#if WK_API_ENABLED
    void setThumbnailView(_WKThumbnailView *);
    _WKThumbnailView *thumbnailView() const { return m_thumbnailView; }
#endif // WK_API_ENABLED

#if ENABLE(DRAG_SUPPORT)
    void draggedImage(NSImage *, CGPoint endPoint, NSDragOperation);
    NSDragOperation draggingEntered(id <NSDraggingInfo>);
    NSDragOperation draggingUpdated(id <NSDraggingInfo>);
    void draggingExited(id <NSDraggingInfo>);
    bool prepareForDragOperation(id <NSDraggingInfo>);
    bool performDragOperation(id <NSDraggingInfo>);
    NSView *hitTestForDragTypes(CGPoint, NSSet *types);
    void registerDraggedTypes();
#endif

#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 101100
    void startWindowDrag();
#endif

    void dragImageForView(NSView *, NSImage *, CGPoint clientPoint, bool linkDrag);
    void setFileAndURLTypes(NSString *filename, NSString *extension, NSString *title, NSString *url, NSString *visibleURL, NSPasteboard *);
    void setPromisedDataForImage(WebCore::Image*, NSString *filename, NSString *extension, NSString *title, NSString *url, NSString *visibleURL, WebCore::SharedBuffer* archiveBuffer, NSString *pasteboardName);
#if ENABLE(ATTACHMENT_ELEMENT)
    void setPromisedDataForAttachment(NSString *filename, NSString *extension, NSString *title, NSString *url, NSString *visibleURL, NSString *pasteboardName);
#endif
    void pasteboardChangedOwner(NSPasteboard *);
    void provideDataForPasteboard(NSPasteboard *, NSString *type);
    NSArray *namesOfPromisedFilesDroppedAtDestination(NSURL *dropDestination);

    RefPtr<ViewSnapshot> takeViewSnapshot();

    ViewGestureController* gestureController() { return m_gestureController.get(); }
    ViewGestureController& ensureGestureController();
    void resetGestureController();
    void setAllowsBackForwardNavigationGestures(bool);
    bool allowsBackForwardNavigationGestures() const { return m_allowsBackForwardNavigationGestures; }
    void setAllowsMagnification(bool);
    bool allowsMagnification() const { return m_allowsMagnification; }

    void setMagnification(double, CGPoint centerPoint);
    void setMagnification(double);
    double magnification() const;
    void setCustomSwipeViews(NSArray *);
    void setCustomSwipeViewsTopContentInset(float);
    bool tryToSwipeWithEvent(NSEvent *, bool ignoringPinnedState);
    void setDidMoveSwipeSnapshotCallback(void(^)(CGRect));

    void scrollWheel(NSEvent *);
    void swipeWithEvent(NSEvent *);
    void magnifyWithEvent(NSEvent *);
    void rotateWithEvent(NSEvent *);
    void smartMagnifyWithEvent(NSEvent *);

    void setLastMouseDownEvent(NSEvent *);

    void gestureEventWasNotHandledByWebCore(NSEvent *);
    void gestureEventWasNotHandledByWebCoreFromViewOnly(NSEvent *);

private:
    WeakPtr<WebViewImpl> createWeakPtr() { return m_weakPtrFactory.createWeakPtr(); }

    bool supportsArbitraryLayoutModes() const;
    float intrinsicDeviceScaleFactor() const;
    void dispatchSetTopContentInset();

    void postFakeMouseMovedEventForFlagsChangedEvent(NSEvent *);

    void setPluginComplexTextInputState(PluginComplexTextInputState);

    void sendToolTipMouseExited();
    void sendToolTipMouseEntered();

    void reparentLayerTreeInThumbnailView();
    void updateThumbnailViewLayer();

    NSView <WebViewImplDelegate> *m_view;
    WebPageProxy& m_page;
    PageClient& m_pageClient;

    WeakPtrFactory<WebViewImpl> m_weakPtrFactory;

    bool m_willBecomeFirstResponderAgain { false };
    bool m_inBecomeFirstResponder { false };
    bool m_inResignFirstResponder { false };

    CGRect m_contentPreparationRect { { 0, 0 }, { 0, 0 } };
    bool m_useContentPreparationRectForVisibleRect { false };
    bool m_clipsToVisibleRect { false };
    bool m_needsViewFrameInWindowCoordinates;
    bool m_didScheduleWindowAndViewFrameUpdate { false };
    bool m_isDeferringViewInWindowChanges { false };

    bool m_automaticallyAdjustsContentInsets { false };
    CGFloat m_topContentInset { 0 };
    bool m_didScheduleSetTopContentInset { false };

    CGSize m_resizeScrollOffset { 0, 0 };

    CGSize m_intrinsicContentSize { 0, 0 };
    CGFloat m_overrideDeviceScaleFactor { 0 };

    RetainPtr<WKViewLayoutStrategy> m_layoutStrategy;
    WKLayoutMode m_lastRequestedLayoutMode { kWKLayoutModeViewSize };
    CGFloat m_lastRequestedViewScale { 1 };

    bool m_inSecureInputState { false };
    RetainPtr<WKEditorUndoTargetObjC> m_undoTarget;

    // The identifier of the plug-in we want to send complex text input to, or 0 if there is none.
    uint64_t m_pluginComplexTextInputIdentifier { 0 };

    // The state of complex text input for the plug-in.
    PluginComplexTextInputState m_pluginComplexTextInputState { PluginComplexTextInputDisabled };

#if ENABLE(FULLSCREEN_API)
    RetainPtr<WKFullScreenWindowController> m_fullScreenWindowController;
#endif

    RetainPtr<WKWindowVisibilityObserver> m_windowVisibilityObserver;

    bool m_shouldDeferViewInWindowChanges { false };
    bool m_viewInWindowChangeWasDeferred { false };
    NSWindow *m_targetWindowForMovePreparation { nullptr };

    id m_flagsChangedEventMonitor { nullptr };

    std::unique_ptr<WebCore::TextIndicatorWindow> m_textIndicatorWindow;

    RetainPtr<NSColorSpace> m_colorSpace;

    RetainPtr<NSEvent> m_lastMouseDownEvent;
    RetainPtr<NSEvent> m_lastPressureEvent;

    bool m_ignoresNonWheelEvents { false };
    bool m_ignoresAllEvents { false };
    bool m_ignoresMouseDraggedEvents { false };

#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 101000
    RetainPtr<WKImmediateActionController> m_immediateActionController;
    RetainPtr<NSImmediateActionGestureRecognizer> m_immediateActionGestureRecognizer;
#endif

    bool m_allowsLinkPreview { true };
    bool m_didRegisterForLookupPopoverCloseNotifications { false };

    RetainPtr<NSTrackingArea> m_primaryTrackingArea;

    NSToolTipTag m_lastToolTipTag { 0 };
    id m_trackingRectOwner { nil };
    void* m_trackingRectUserData { nullptr };

    RetainPtr<CALayer> m_rootLayer;
    RetainPtr<NSView> m_layerHostingView;

#if WK_API_ENABLED
    _WKThumbnailView *m_thumbnailView { nullptr };
#endif

    std::unique_ptr<ViewGestureController> m_gestureController;
    bool m_allowsBackForwardNavigationGestures { false };
    bool m_allowsMagnification { false };

    RetainPtr<id> m_remoteAccessibilityChild;

    RefPtr<WebCore::Image> m_promisedImage;
    String m_promisedFilename;
    String m_promisedURL;

    WTF::Optional<NSInteger> m_spellCheckerDocumentTag;
};
    
} // namespace WebKit

#endif // PLATFORM(MAC)

#endif // WebViewImpl_h
