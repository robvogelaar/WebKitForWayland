/*
 * Copyright (C) 2010, 2011 Apple Inc. All rights reserved.
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

#import "config.h"
#import "WKView.h"

#if PLATFORM(MAC)

#if USE(DICTATION_ALTERNATIVES)
#import <AppKit/NSTextAlternatives.h>
#import <AppKit/NSAttributedString.h>
#endif

#import "APILegacyContextHistoryClient.h"
#import "APIPageConfiguration.h"
#import "AttributedString.h"
#import "DataReference.h"
#import "EditingRange.h"
#import "EditorState.h"
#import "LayerTreeContext.h"
#import "Logging.h"
#import "NativeWebKeyboardEvent.h"
#import "NativeWebMouseEvent.h"
#import "NativeWebWheelEvent.h"
#import "PageClientImpl.h"
#import "PasteboardTypes.h"
#import "RemoteLayerTreeDrawingAreaProxy.h"
#import "RemoteObjectRegistry.h"
#import "RemoteObjectRegistryMessages.h"
#import "StringUtilities.h"
#import "TextChecker.h"
#import "TextCheckerState.h"
#import "TiledCoreAnimationDrawingAreaProxy.h"
#import "WKAPICast.h"
#import "WKFullScreenWindowController.h"
#import "WKLayoutMode.h"
#import "WKPrintingView.h"
#import "WKProcessPoolInternal.h"
#import "WKStringCF.h"
#import "WKTextInputWindowController.h"
#import "WKViewInternal.h"
#import "WKViewPrivate.h"
#import "WKWebView.h"
#import "WebBackForwardList.h"
#import "WebEventFactory.h"
#import "WebHitTestResultData.h"
#import "WebInspectorProxy.h"
#import "WebKit2Initialize.h"
#import "WebPage.h"
#import "WebPageGroup.h"
#import "WebPageProxy.h"
#import "WebPreferences.h"
#import "WebProcessPool.h"
#import "WebProcessProxy.h"
#import "WebSystemInterface.h"
#import "WebViewImpl.h"
#import "_WKRemoteObjectRegistryInternal.h"
#import <QuartzCore/QuartzCore.h>
#import <WebCore/AXObjectCache.h>
#import <WebCore/ColorMac.h>
#import <WebCore/CoreGraphicsSPI.h>
#import <WebCore/DataDetectorsSPI.h>
#import <WebCore/DictionaryLookup.h>
#import <WebCore/DragController.h>
#import <WebCore/DragData.h>
#import <WebCore/FloatRect.h>
#import <WebCore/Image.h>
#import <WebCore/IntRect.h>
#import <WebCore/FileSystem.h>
#import <WebCore/KeyboardEvent.h>
#import <WebCore/LocalizedStrings.h>
#import <WebCore/NSMenuSPI.h>
#import <WebCore/PlatformEventFactoryMac.h>
#import <WebCore/PlatformScreen.h>
#import <WebCore/Region.h>
#import <WebCore/RuntimeApplicationChecks.h>
#import <WebCore/SharedBuffer.h>
#import <WebCore/TextAlternativeWithRange.h>
#import <WebCore/TextIndicator.h>
#import <WebCore/TextIndicatorWindow.h>
#import <WebCore/TextUndoInsertionMarkupMac.h>
#import <WebCore/WebCoreCALayerExtras.h>
#import <WebCore/WebCoreFullScreenPlaceholderView.h>
#import <WebCore/WebCoreFullScreenWindow.h>
#import <WebCore/WebCoreNSStringExtras.h>
#import <WebKitSystemInterface.h>
#import <sys/stat.h>
#import <wtf/RefPtr.h>
#import <wtf/RetainPtr.h>
#import <wtf/RunLoop.h>

/* API internals. */
#import "WKBrowsingContextControllerInternal.h"
#import "WKBrowsingContextGroupPrivate.h"
#import "WKProcessGroupPrivate.h"

@interface NSApplication (WKNSApplicationDetails)
- (void)speakString:(NSString *)string;
- (void)_setCurrentEvent:(NSEvent *)event;
@end

#if USE(ASYNC_NSTEXTINPUTCLIENT)
@interface NSTextInputContext (WKNSTextInputContextDetails)
- (void)handleEvent:(NSEvent *)theEvent completionHandler:(void(^)(BOOL handled))completionHandler;
- (void)handleEventByInputMethod:(NSEvent *)theEvent completionHandler:(void(^)(BOOL handled))completionHandler;
- (BOOL)handleEventByKeyboardLayout:(NSEvent *)theEvent;
@end
#endif

using namespace WebKit;
using namespace WebCore;

namespace WebKit {

typedef id <NSValidatedUserInterfaceItem> ValidationItem;
typedef Vector<RetainPtr<ValidationItem>> ValidationVector;
typedef HashMap<String, ValidationVector> ValidationMap;

}

#if !USE(ASYNC_NSTEXTINPUTCLIENT)
struct WKViewInterpretKeyEventsParameters {
    bool eventInterpretationHadSideEffects;
    bool consumedByIM;
    bool executingSavedKeypressCommands;
    Vector<KeypressCommand>* commands;
};
#endif

@interface WKViewData : NSObject {
@public
    std::unique_ptr<PageClientImpl> _pageClient;
    RefPtr<WebPageProxy> _page;
    std::unique_ptr<WebViewImpl> _impl;

#if WK_API_ENABLED
    RetainPtr<WKBrowsingContextController> _browsingContextController;
    RetainPtr<NSView> _inspectorAttachmentView;

    RetainPtr<_WKRemoteObjectRegistry> _remoteObjectRegistry;
#endif

    // For asynchronous validation.
    ValidationMap _validationMap;

    // We keep here the event when resending it to
    // the application to distinguish the case of a new event from one 
    // that has been already sent to WebCore.
    RetainPtr<NSEvent> _keyDownEventBeingResent;
#if USE(ASYNC_NSTEXTINPUTCLIENT)
    Vector<KeypressCommand>* _collectedKeypressCommands;
#else
    WKViewInterpretKeyEventsParameters* _interpretKeyEventsParameters;
#endif

    BOOL _willBecomeFirstResponderAgain;

    BOOL _windowOcclusionDetectionEnabled;

    CGFloat _totalHeightOfBanners;
}

@end

@implementation WKViewData
@end

@interface WKResponderChainSink : NSResponder {
    NSResponder *_lastResponderInChain;
    bool _didReceiveUnhandledCommand;
}
- (id)initWithResponderChain:(NSResponder *)chain;
- (void)detach;
- (bool)didReceiveUnhandledCommand;
@end

@interface WKView () <WebViewImplDelegate>
@end

@implementation WKView

#if WK_API_ENABLED

- (id)initWithFrame:(NSRect)frame processGroup:(WKProcessGroup *)processGroup browsingContextGroup:(WKBrowsingContextGroup *)browsingContextGroup
{
    return [self initWithFrame:frame contextRef:processGroup._contextRef pageGroupRef:browsingContextGroup._pageGroupRef relatedToPage:nil];
}

- (id)initWithFrame:(NSRect)frame processGroup:(WKProcessGroup *)processGroup browsingContextGroup:(WKBrowsingContextGroup *)browsingContextGroup relatedToView:(WKView *)relatedView
{
    return [self initWithFrame:frame contextRef:processGroup._contextRef pageGroupRef:browsingContextGroup._pageGroupRef relatedToPage:relatedView ? toAPI(relatedView->_data->_page.get()) : nil];
}

#endif // WK_API_ENABLED

- (void)dealloc
{
#if WK_API_ENABLED
    if (_data->_remoteObjectRegistry) {
        _data->_page->process().processPool().removeMessageReceiver(Messages::RemoteObjectRegistry::messageReceiverName(), _data->_page->pageID());
        [_data->_remoteObjectRegistry _invalidate];
    }
#endif

    _data->_page->close();

    _data->_impl = nullptr;

    [_data release];
    _data = nil;

    NSNotificationCenter* workspaceNotificationCenter = [[NSWorkspace sharedWorkspace] notificationCenter];
    [workspaceNotificationCenter removeObserver:self name:NSWorkspaceActiveSpaceDidChangeNotification object:nil];

    WebProcessPool::statistics().wkViewCount--;

    [super dealloc];
}

#if WK_API_ENABLED

- (WKBrowsingContextController *)browsingContextController
{
    if (!_data->_browsingContextController)
        _data->_browsingContextController = adoptNS([[WKBrowsingContextController alloc] _initWithPageRef:toAPI(_data->_page.get())]);

    return _data->_browsingContextController.get();
}

#endif // WK_API_ENABLED

- (void)setDrawsBackground:(BOOL)drawsBackground
{
    _data->_impl->setDrawsBackground(drawsBackground);
}

- (BOOL)drawsBackground
{
    return _data->_impl->drawsBackground();
}

- (void)setDrawsTransparentBackground:(BOOL)drawsTransparentBackground
{
    _data->_impl->setDrawsTransparentBackground(drawsTransparentBackground);
}

- (BOOL)drawsTransparentBackground
{
    return _data->_impl->drawsTransparentBackground();
}

- (BOOL)acceptsFirstResponder
{
    return _data->_impl->acceptsFirstResponder();
}

- (BOOL)becomeFirstResponder
{
    return _data->_impl->becomeFirstResponder();
}

- (BOOL)resignFirstResponder
{
    return _data->_impl->resignFirstResponder();
}

- (void)viewWillStartLiveResize
{
    _data->_impl->viewWillStartLiveResize();
}

- (void)viewDidEndLiveResize
{
    _data->_impl->viewDidEndLiveResize();
}

- (BOOL)isFlipped
{
    return YES;
}

- (NSSize)intrinsicContentSize
{
    return NSSizeFromCGSize(_data->_impl->intrinsicContentSize());
}

- (void)prepareContentInRect:(NSRect)rect
{
    _data->_impl->setContentPreparationRect(NSRectToCGRect(rect));
    _data->_impl->updateViewExposedRect();
}

- (void)setFrameSize:(NSSize)size
{
    [super setFrameSize:size];
    _data->_impl->setFrameSize(NSSizeToCGSize(size));
}

- (void)renewGState
{
    _data->_impl->renewGState();
    [super renewGState];
}

typedef HashMap<SEL, String> SelectorNameMap;

// Map selectors into Editor command names.
// This is not needed for any selectors that have the same name as the Editor command.
static const SelectorNameMap* createSelectorExceptionMap()
{
    SelectorNameMap* map = new HashMap<SEL, String>;
    
    map->add(@selector(insertNewlineIgnoringFieldEditor:), "InsertNewline");
    map->add(@selector(insertParagraphSeparator:), "InsertNewline");
    map->add(@selector(insertTabIgnoringFieldEditor:), "InsertTab");
    map->add(@selector(pageDown:), "MovePageDown");
    map->add(@selector(pageDownAndModifySelection:), "MovePageDownAndModifySelection");
    map->add(@selector(pageUp:), "MovePageUp");
    map->add(@selector(pageUpAndModifySelection:), "MovePageUpAndModifySelection");
    map->add(@selector(scrollPageDown:), "ScrollPageForward");
    map->add(@selector(scrollPageUp:), "ScrollPageBackward");
    
    return map;
}

static String commandNameForSelector(SEL selector)
{
    // Check the exception map first.
    static const SelectorNameMap* exceptionMap = createSelectorExceptionMap();
    SelectorNameMap::const_iterator it = exceptionMap->find(selector);
    if (it != exceptionMap->end())
        return it->value;
    
    // Remove the trailing colon.
    // No need to capitalize the command name since Editor command names are
    // not case sensitive.
    const char* selectorName = sel_getName(selector);
    size_t selectorNameLength = strlen(selectorName);
    if (selectorNameLength < 2 || selectorName[selectorNameLength - 1] != ':')
        return String();
    return String(selectorName, selectorNameLength - 1);
}

// Editing commands

#define WEBCORE_COMMAND(command) - (void)command:(id)sender { _data->_impl->executeEditCommand(commandNameForSelector(_cmd)); }

WEBCORE_COMMAND(alignCenter)
WEBCORE_COMMAND(alignJustified)
WEBCORE_COMMAND(alignLeft)
WEBCORE_COMMAND(alignRight)
WEBCORE_COMMAND(copy)
WEBCORE_COMMAND(cut)
WEBCORE_COMMAND(delete)
WEBCORE_COMMAND(deleteBackward)
WEBCORE_COMMAND(deleteBackwardByDecomposingPreviousCharacter)
WEBCORE_COMMAND(deleteForward)
WEBCORE_COMMAND(deleteToBeginningOfLine)
WEBCORE_COMMAND(deleteToBeginningOfParagraph)
WEBCORE_COMMAND(deleteToEndOfLine)
WEBCORE_COMMAND(deleteToEndOfParagraph)
WEBCORE_COMMAND(deleteToMark)
WEBCORE_COMMAND(deleteWordBackward)
WEBCORE_COMMAND(deleteWordForward)
WEBCORE_COMMAND(ignoreSpelling)
WEBCORE_COMMAND(indent)
WEBCORE_COMMAND(insertBacktab)
WEBCORE_COMMAND(insertLineBreak)
WEBCORE_COMMAND(insertNewline)
WEBCORE_COMMAND(insertNewlineIgnoringFieldEditor)
WEBCORE_COMMAND(insertParagraphSeparator)
WEBCORE_COMMAND(insertTab)
WEBCORE_COMMAND(insertTabIgnoringFieldEditor)
WEBCORE_COMMAND(makeTextWritingDirectionLeftToRight)
WEBCORE_COMMAND(makeTextWritingDirectionNatural)
WEBCORE_COMMAND(makeTextWritingDirectionRightToLeft)
WEBCORE_COMMAND(moveBackward)
WEBCORE_COMMAND(moveBackwardAndModifySelection)
WEBCORE_COMMAND(moveDown)
WEBCORE_COMMAND(moveDownAndModifySelection)
WEBCORE_COMMAND(moveForward)
WEBCORE_COMMAND(moveForwardAndModifySelection)
WEBCORE_COMMAND(moveLeft)
WEBCORE_COMMAND(moveLeftAndModifySelection)
WEBCORE_COMMAND(moveParagraphBackwardAndModifySelection)
WEBCORE_COMMAND(moveParagraphForwardAndModifySelection)
WEBCORE_COMMAND(moveRight)
WEBCORE_COMMAND(moveRightAndModifySelection)
WEBCORE_COMMAND(moveToBeginningOfDocument)
WEBCORE_COMMAND(moveToBeginningOfDocumentAndModifySelection)
WEBCORE_COMMAND(moveToBeginningOfLine)
WEBCORE_COMMAND(moveToBeginningOfLineAndModifySelection)
WEBCORE_COMMAND(moveToBeginningOfParagraph)
WEBCORE_COMMAND(moveToBeginningOfParagraphAndModifySelection)
WEBCORE_COMMAND(moveToBeginningOfSentence)
WEBCORE_COMMAND(moveToBeginningOfSentenceAndModifySelection)
WEBCORE_COMMAND(moveToEndOfDocument)
WEBCORE_COMMAND(moveToEndOfDocumentAndModifySelection)
WEBCORE_COMMAND(moveToEndOfLine)
WEBCORE_COMMAND(moveToEndOfLineAndModifySelection)
WEBCORE_COMMAND(moveToEndOfParagraph)
WEBCORE_COMMAND(moveToEndOfParagraphAndModifySelection)
WEBCORE_COMMAND(moveToEndOfSentence)
WEBCORE_COMMAND(moveToEndOfSentenceAndModifySelection)
WEBCORE_COMMAND(moveToLeftEndOfLine)
WEBCORE_COMMAND(moveToLeftEndOfLineAndModifySelection)
WEBCORE_COMMAND(moveToRightEndOfLine)
WEBCORE_COMMAND(moveToRightEndOfLineAndModifySelection)
WEBCORE_COMMAND(moveUp)
WEBCORE_COMMAND(moveUpAndModifySelection)
WEBCORE_COMMAND(moveWordBackward)
WEBCORE_COMMAND(moveWordBackwardAndModifySelection)
WEBCORE_COMMAND(moveWordForward)
WEBCORE_COMMAND(moveWordForwardAndModifySelection)
WEBCORE_COMMAND(moveWordLeft)
WEBCORE_COMMAND(moveWordLeftAndModifySelection)
WEBCORE_COMMAND(moveWordRight)
WEBCORE_COMMAND(moveWordRightAndModifySelection)
WEBCORE_COMMAND(outdent)
WEBCORE_COMMAND(pageDown)
WEBCORE_COMMAND(pageDownAndModifySelection)
WEBCORE_COMMAND(pageUp)
WEBCORE_COMMAND(pageUpAndModifySelection)
WEBCORE_COMMAND(paste)
WEBCORE_COMMAND(pasteAsPlainText)
WEBCORE_COMMAND(scrollPageDown)
WEBCORE_COMMAND(scrollPageUp)
WEBCORE_COMMAND(scrollLineDown)
WEBCORE_COMMAND(scrollLineUp)
WEBCORE_COMMAND(scrollToBeginningOfDocument)
WEBCORE_COMMAND(scrollToEndOfDocument)
WEBCORE_COMMAND(selectAll)
WEBCORE_COMMAND(selectLine)
WEBCORE_COMMAND(selectParagraph)
WEBCORE_COMMAND(selectSentence)
WEBCORE_COMMAND(selectToMark)
WEBCORE_COMMAND(selectWord)
WEBCORE_COMMAND(setMark)
WEBCORE_COMMAND(subscript)
WEBCORE_COMMAND(superscript)
WEBCORE_COMMAND(swapWithMark)
WEBCORE_COMMAND(takeFindStringFromSelection)
WEBCORE_COMMAND(transpose)
WEBCORE_COMMAND(underline)
WEBCORE_COMMAND(unscript)
WEBCORE_COMMAND(yank)
WEBCORE_COMMAND(yankAndSelect)

#undef WEBCORE_COMMAND

// This method is needed to support Mac OS X services.

- (BOOL)writeSelectionToPasteboard:(NSPasteboard *)pasteboard types:(NSArray *)types
{
    return _data->_impl->writeSelectionToPasteboard(pasteboard, types);
}

- (void)centerSelectionInVisibleArea:(id)sender 
{ 
    _data->_impl->centerSelectionInVisibleArea();
}

// This method is needed to support Mac OS X services.

- (id)validRequestorForSendType:(NSString *)sendType returnType:(NSString *)returnType
{
    EditorState editorState = _data->_page->editorState();
    BOOL isValidSendType = NO;

    if (sendType && !editorState.selectionIsNone) {
        if (editorState.isInPlugin)
            isValidSendType = [sendType isEqualToString:NSStringPboardType];
        else
            isValidSendType = [PasteboardTypes::forSelection() containsObject:sendType];
    }

    BOOL isValidReturnType = NO;
    if (!returnType)
        isValidReturnType = YES;
    else if ([PasteboardTypes::forEditing() containsObject:returnType] && editorState.isContentEditable) {
        // We can insert strings in any editable context.  We can insert other types, like images, only in rich edit contexts.
        isValidReturnType = editorState.isContentRichlyEditable || [returnType isEqualToString:NSStringPboardType];
    }
    if (isValidSendType && isValidReturnType)
        return self;
    return [[self nextResponder] validRequestorForSendType:sendType returnType:returnType];
}

// This method is needed to support Mac OS X services.

- (BOOL)readSelectionFromPasteboard:(NSPasteboard *)pasteboard 
{
    return _data->_page->readSelectionFromPasteboard([pasteboard name]);
}

// Font panel support.

- (void)changeFont:(id)sender
{
    NSFontManager *fontManager = [NSFontManager sharedFontManager];
    NSFont *font = [fontManager convertFont:[fontManager selectedFont]];
    if (!font)
        return;
    _data->_page->setFont([font familyName], [font pointSize], [[font fontDescriptor] symbolicTraits]);
}

/*

When possible, editing-related methods should be implemented in WebCore with the
EditorCommand mechanism and invoked via WEBCORE_COMMAND, rather than implementing
individual methods here with Mac-specific code.

Editing-related methods still unimplemented that are implemented in WebKit1:

- (void)complete:(id)sender;
- (void)copyFont:(id)sender;
- (void)makeBaseWritingDirectionLeftToRight:(id)sender;
- (void)makeBaseWritingDirectionNatural:(id)sender;
- (void)makeBaseWritingDirectionRightToLeft:(id)sender;
- (void)pasteFont:(id)sender;
- (void)scrollLineDown:(id)sender;
- (void)scrollLineUp:(id)sender;
- (void)showGuessPanel:(id)sender;

Some other editing-related methods still unimplemented:

- (void)changeCaseOfLetter:(id)sender;
- (void)copyRuler:(id)sender;
- (void)insertContainerBreak:(id)sender;
- (void)insertDoubleQuoteIgnoringSubstitution:(id)sender;
- (void)insertSingleQuoteIgnoringSubstitution:(id)sender;
- (void)pasteRuler:(id)sender;
- (void)toggleRuler:(id)sender;
- (void)transposeWords:(id)sender;

*/

// Menu items validation

static NSMenuItem *menuItem(id <NSValidatedUserInterfaceItem> item)
{
    if (![(NSObject *)item isKindOfClass:[NSMenuItem class]])
        return nil;
    return (NSMenuItem *)item;
}

static NSToolbarItem *toolbarItem(id <NSValidatedUserInterfaceItem> item)
{
    if (![(NSObject *)item isKindOfClass:[NSToolbarItem class]])
        return nil;
    return (NSToolbarItem *)item;
}

- (BOOL)validateUserInterfaceItem:(id <NSValidatedUserInterfaceItem>)item
{
    SEL action = [item action];

    if (action == @selector(showGuessPanel:)) {
        if (NSMenuItem *menuItem = ::menuItem(item))
            [menuItem setTitle:contextMenuItemTagShowSpellingPanel(![[[NSSpellChecker sharedSpellChecker] spellingPanel] isVisible])];
        return _data->_page->editorState().isContentEditable;
    }

    if (action == @selector(checkSpelling:) || action == @selector(changeSpelling:))
        return _data->_page->editorState().isContentEditable;

    if (action == @selector(toggleContinuousSpellChecking:)) {
        bool enabled = TextChecker::isContinuousSpellCheckingAllowed();
        bool checked = enabled && TextChecker::state().isContinuousSpellCheckingEnabled;
        [menuItem(item) setState:checked ? NSOnState : NSOffState];
        return enabled;
    }

    if (action == @selector(toggleGrammarChecking:)) {
        bool checked = TextChecker::state().isGrammarCheckingEnabled;
        [menuItem(item) setState:checked ? NSOnState : NSOffState];
        return YES;
    }

    if (action == @selector(toggleAutomaticSpellingCorrection:)) {
        bool checked = TextChecker::state().isAutomaticSpellingCorrectionEnabled;
        [menuItem(item) setState:checked ? NSOnState : NSOffState];
        return _data->_page->editorState().isContentEditable;
    }

    if (action == @selector(orderFrontSubstitutionsPanel:)) {
        if (NSMenuItem *menuItem = ::menuItem(item))
            [menuItem setTitle:contextMenuItemTagShowSubstitutions(![[[NSSpellChecker sharedSpellChecker] substitutionsPanel] isVisible])];
        return _data->_page->editorState().isContentEditable;
    }

    if (action == @selector(toggleSmartInsertDelete:)) {
        bool checked = _data->_page->isSmartInsertDeleteEnabled();
        [menuItem(item) setState:checked ? NSOnState : NSOffState];
        return _data->_page->editorState().isContentEditable;
    }

    if (action == @selector(toggleAutomaticQuoteSubstitution:)) {
        bool checked = TextChecker::state().isAutomaticQuoteSubstitutionEnabled;
        [menuItem(item) setState:checked ? NSOnState : NSOffState];
        return _data->_page->editorState().isContentEditable;
    }

    if (action == @selector(toggleAutomaticDashSubstitution:)) {
        bool checked = TextChecker::state().isAutomaticDashSubstitutionEnabled;
        [menuItem(item) setState:checked ? NSOnState : NSOffState];
        return _data->_page->editorState().isContentEditable;
    }

    if (action == @selector(toggleAutomaticLinkDetection:)) {
        bool checked = TextChecker::state().isAutomaticLinkDetectionEnabled;
        [menuItem(item) setState:checked ? NSOnState : NSOffState];
        return _data->_page->editorState().isContentEditable;
    }

    if (action == @selector(toggleAutomaticTextReplacement:)) {
        bool checked = TextChecker::state().isAutomaticTextReplacementEnabled;
        [menuItem(item) setState:checked ? NSOnState : NSOffState];
        return _data->_page->editorState().isContentEditable;
    }

    if (action == @selector(uppercaseWord:) || action == @selector(lowercaseWord:) || action == @selector(capitalizeWord:))
        return _data->_page->editorState().selectionIsRange && _data->_page->editorState().isContentEditable;
    
    if (action == @selector(stopSpeaking:))
        return [NSApp isSpeaking];
    
    // The centerSelectionInVisibleArea: selector is enabled if there's a selection range or if there's an insertion point in an editable area.
    if (action == @selector(centerSelectionInVisibleArea:))
        return _data->_page->editorState().selectionIsRange || (_data->_page->editorState().isContentEditable && !_data->_page->editorState().selectionIsNone);

    // Next, handle editor commands. Start by returning YES for anything that is not an editor command.
    // Returning YES is the default thing to do in an AppKit validate method for any selector that is not recognized.
    String commandName = commandNameForSelector([item action]);
    if (!Editor::commandIsSupportedFromMenuOrKeyBinding(commandName))
        return YES;

    // Add this item to the vector of items for a given command that are awaiting validation.
    ValidationMap::AddResult addResult = _data->_validationMap.add(commandName, ValidationVector());
    addResult.iterator->value.append(item);
    if (addResult.isNewEntry) {
        // If we are not already awaiting validation for this command, start the asynchronous validation process.
        // FIXME: Theoretically, there is a race here; when we get the answer it might be old, from a previous time
        // we asked for the same command; there is no guarantee the answer is still valid.
        _data->_page->validateCommand(commandName, [self](const String& commandName, bool isEnabled, int32_t state, WebKit::CallbackBase::Error error) {
            // If the process exits before the command can be validated, we'll be called back with an error.
            if (error != WebKit::CallbackBase::Error::None)
                return;
            
            [self _setUserInterfaceItemState:commandName enabled:isEnabled state:state];
        });
    }

    // Treat as enabled until we get the result back from the web process and _setUserInterfaceItemState is called.
    // FIXME <rdar://problem/8803459>: This means disabled items will flash enabled at first for a moment.
    // But returning NO here would be worse; that would make keyboard commands such as command-C fail.
    return YES;
}

- (IBAction)startSpeaking:(id)sender
{
    _data->_page->getSelectionOrContentsAsString([self](const String& string, WebKit::CallbackBase::Error error) {
        if (error != WebKit::CallbackBase::Error::None)
            return;
        if (!string)
            return;

        [NSApp speakString:string];
    });
}

- (IBAction)stopSpeaking:(id)sender
{
    [NSApp stopSpeaking:sender];
}

- (IBAction)showGuessPanel:(id)sender
{
    NSSpellChecker *checker = [NSSpellChecker sharedSpellChecker];
    if (!checker) {
        LOG_ERROR("No NSSpellChecker");
        return;
    }
    
    NSPanel *spellingPanel = [checker spellingPanel];
    if ([spellingPanel isVisible]) {
        [spellingPanel orderOut:sender];
        return;
    }
    
    _data->_page->advanceToNextMisspelling(true);
    [spellingPanel orderFront:sender];
}

- (IBAction)checkSpelling:(id)sender
{
    _data->_page->advanceToNextMisspelling(false);
}

- (void)changeSpelling:(id)sender
{
    NSString *word = [[sender selectedCell] stringValue];

    _data->_page->changeSpellingToWord(word);
}

- (IBAction)toggleContinuousSpellChecking:(id)sender
{
    bool spellCheckingEnabled = !TextChecker::state().isContinuousSpellCheckingEnabled;
    TextChecker::setContinuousSpellCheckingEnabled(spellCheckingEnabled);

    _data->_page->process().updateTextCheckerState();
}

- (BOOL)isGrammarCheckingEnabled
{
    return TextChecker::state().isGrammarCheckingEnabled;
}

- (void)setGrammarCheckingEnabled:(BOOL)flag
{
    if (static_cast<bool>(flag) == TextChecker::state().isGrammarCheckingEnabled)
        return;
    
    TextChecker::setGrammarCheckingEnabled(flag);
    _data->_page->process().updateTextCheckerState();
}

- (IBAction)toggleGrammarChecking:(id)sender
{
    bool grammarCheckingEnabled = !TextChecker::state().isGrammarCheckingEnabled;
    TextChecker::setGrammarCheckingEnabled(grammarCheckingEnabled);

    _data->_page->process().updateTextCheckerState();
}

- (IBAction)toggleAutomaticSpellingCorrection:(id)sender
{
    TextChecker::setAutomaticSpellingCorrectionEnabled(!TextChecker::state().isAutomaticSpellingCorrectionEnabled);

    _data->_page->process().updateTextCheckerState();
}

- (void)orderFrontSubstitutionsPanel:(id)sender
{
    NSSpellChecker *checker = [NSSpellChecker sharedSpellChecker];
    if (!checker) {
        LOG_ERROR("No NSSpellChecker");
        return;
    }
    
    NSPanel *substitutionsPanel = [checker substitutionsPanel];
    if ([substitutionsPanel isVisible]) {
        [substitutionsPanel orderOut:sender];
        return;
    }
    [substitutionsPanel orderFront:sender];
}

- (IBAction)toggleSmartInsertDelete:(id)sender
{
    _data->_page->setSmartInsertDeleteEnabled(!_data->_page->isSmartInsertDeleteEnabled());
}

- (BOOL)isAutomaticQuoteSubstitutionEnabled
{
    return TextChecker::state().isAutomaticQuoteSubstitutionEnabled;
}

- (void)setAutomaticQuoteSubstitutionEnabled:(BOOL)flag
{
    if (static_cast<bool>(flag) == TextChecker::state().isAutomaticQuoteSubstitutionEnabled)
        return;

    TextChecker::setAutomaticQuoteSubstitutionEnabled(flag);
    _data->_page->process().updateTextCheckerState();
}

- (void)toggleAutomaticQuoteSubstitution:(id)sender
{
    TextChecker::setAutomaticQuoteSubstitutionEnabled(!TextChecker::state().isAutomaticQuoteSubstitutionEnabled);
    _data->_page->process().updateTextCheckerState();
}

- (BOOL)isAutomaticDashSubstitutionEnabled
{
    return TextChecker::state().isAutomaticDashSubstitutionEnabled;
}

- (void)setAutomaticDashSubstitutionEnabled:(BOOL)flag
{
    if (static_cast<bool>(flag) == TextChecker::state().isAutomaticDashSubstitutionEnabled)
        return;

    TextChecker::setAutomaticDashSubstitutionEnabled(flag);
    _data->_page->process().updateTextCheckerState();
}

- (void)toggleAutomaticDashSubstitution:(id)sender
{
    TextChecker::setAutomaticDashSubstitutionEnabled(!TextChecker::state().isAutomaticDashSubstitutionEnabled);
    _data->_page->process().updateTextCheckerState();
}

- (BOOL)isAutomaticLinkDetectionEnabled
{
    return TextChecker::state().isAutomaticLinkDetectionEnabled;
}

- (void)setAutomaticLinkDetectionEnabled:(BOOL)flag
{
    if (static_cast<bool>(flag) == TextChecker::state().isAutomaticLinkDetectionEnabled)
        return;

    TextChecker::setAutomaticLinkDetectionEnabled(flag);
    _data->_page->process().updateTextCheckerState();
}

- (void)toggleAutomaticLinkDetection:(id)sender
{
    TextChecker::setAutomaticLinkDetectionEnabled(!TextChecker::state().isAutomaticLinkDetectionEnabled);
    _data->_page->process().updateTextCheckerState();
}

- (BOOL)isAutomaticTextReplacementEnabled
{
    return TextChecker::state().isAutomaticTextReplacementEnabled;
}

- (void)setAutomaticTextReplacementEnabled:(BOOL)flag
{
    if (static_cast<bool>(flag) == TextChecker::state().isAutomaticTextReplacementEnabled)
        return;

    TextChecker::setAutomaticTextReplacementEnabled(flag);
    _data->_page->process().updateTextCheckerState();
}

- (void)toggleAutomaticTextReplacement:(id)sender
{
    TextChecker::setAutomaticTextReplacementEnabled(!TextChecker::state().isAutomaticTextReplacementEnabled);
    _data->_page->process().updateTextCheckerState();
}

- (void)uppercaseWord:(id)sender
{
    _data->_page->uppercaseWord();
}

- (void)lowercaseWord:(id)sender
{
    _data->_page->lowercaseWord();
}

- (void)capitalizeWord:(id)sender
{
    _data->_page->capitalizeWord();
}

// Events

// Override this so that AppKit will send us arrow keys as key down events so we can
// support them via the key bindings mechanism.
- (BOOL)_wantsKeyDownForEvent:(NSEvent *)event
{
    return YES;
}

#if USE(ASYNC_NSTEXTINPUTCLIENT)
#define NATIVE_MOUSE_EVENT_HANDLER(Selector) \
    - (void)Selector:(NSEvent *)theEvent \
    { \
        if (_data->_impl->ignoresNonWheelEvents()) \
            return; \
        if (NSTextInputContext *context = [self inputContext]) { \
            [context handleEvent:theEvent completionHandler:^(BOOL handled) { \
                if (handled) \
                    LOG(TextInput, "%s was handled by text input context", String(#Selector).substring(0, String(#Selector).find("Internal")).ascii().data()); \
                else { \
                    NativeWebMouseEvent webEvent(theEvent, _data->_impl->lastPressureEvent(), self); \
                    _data->_page->handleMouseEvent(webEvent); \
                } \
            }]; \
            return; \
        } \
        NativeWebMouseEvent webEvent(theEvent, _data->_impl->lastPressureEvent(), self); \
        _data->_page->handleMouseEvent(webEvent); \
    }
#define NATIVE_MOUSE_EVENT_HANDLER_INTERNAL(Selector) \
    - (void)Selector:(NSEvent *)theEvent \
    { \
        if (_data->_impl->ignoresNonWheelEvents()) \
            return; \
        if (NSTextInputContext *context = [self inputContext]) { \
            [context handleEvent:theEvent completionHandler:^(BOOL handled) { \
                if (handled) \
                    LOG(TextInput, "%s was handled by text input context", String(#Selector).substring(0, String(#Selector).find("Internal")).ascii().data()); \
                else { \
                    NativeWebMouseEvent webEvent(theEvent, _data->_impl->lastPressureEvent(), self); \
                    _data->_page->handleMouseEvent(webEvent); \
                } \
            }]; \
            return; \
        } \
        NativeWebMouseEvent webEvent(theEvent, _data->_impl->lastPressureEvent(), self); \
        _data->_page->handleMouseEvent(webEvent); \
    }
#else
#define NATIVE_MOUSE_EVENT_HANDLER(Selector) \
    - (void)Selector:(NSEvent *)theEvent \
    { \
        if (_data->_impl->ignoresNonWheelEvents()) \
            return; \
        if ([[self inputContext] handleEvent:theEvent]) { \
            LOG(TextInput, "%s was handled by text input context", String(#Selector).substring(0, String(#Selector).find("Internal")).ascii().data()); \
            return; \
        } \
        NativeWebMouseEvent webEvent(theEvent, _data->_impl->lastPressureEvent(), self); \
        _data->_page->handleMouseEvent(webEvent); \
    }
#define NATIVE_MOUSE_EVENT_HANDLER_INTERNAL(Selector) \
    - (void)Selector:(NSEvent *)theEvent \
    { \
        if (_data->_impl->ignoresNonWheelEvents()) \
            return; \
        if ([[self inputContext] handleEvent:theEvent]) { \
            LOG(TextInput, "%s was handled by text input context", String(#Selector).substring(0, String(#Selector).find("Internal")).ascii().data()); \
            return; \
        } \
        NativeWebMouseEvent webEvent(theEvent, _data->_impl->lastPressureEvent(), self); \
        _data->_page->handleMouseEvent(webEvent); \
    }
#endif

NATIVE_MOUSE_EVENT_HANDLER(mouseEntered)
NATIVE_MOUSE_EVENT_HANDLER(mouseExited)
NATIVE_MOUSE_EVENT_HANDLER(otherMouseDown)
NATIVE_MOUSE_EVENT_HANDLER(otherMouseDragged)
NATIVE_MOUSE_EVENT_HANDLER(otherMouseUp)
NATIVE_MOUSE_EVENT_HANDLER(rightMouseDown)
NATIVE_MOUSE_EVENT_HANDLER(rightMouseDragged)
NATIVE_MOUSE_EVENT_HANDLER(rightMouseUp)

NATIVE_MOUSE_EVENT_HANDLER_INTERNAL(mouseMovedInternal)
NATIVE_MOUSE_EVENT_HANDLER_INTERNAL(mouseDownInternal)
NATIVE_MOUSE_EVENT_HANDLER_INTERNAL(mouseUpInternal)
NATIVE_MOUSE_EVENT_HANDLER_INTERNAL(mouseDraggedInternal)

#undef NATIVE_MOUSE_EVENT_HANDLER

- (void)scrollWheel:(NSEvent *)event
{
    _data->_impl->scrollWheel(event);
}

- (void)swipeWithEvent:(NSEvent *)event
{
    _data->_impl->swipeWithEvent(event);
}

- (void)mouseMoved:(NSEvent *)event
{
    if (_data->_impl->ignoresNonWheelEvents())
        return;

    // When a view is first responder, it gets mouse moved events even when the mouse is outside its visible rect.
    if (self == [[self window] firstResponder] && !NSPointInRect([self convertPoint:[event locationInWindow] fromView:nil], [self visibleRect]))
        return;

    [self mouseMovedInternal:event];
}

- (void)mouseDown:(NSEvent *)event
{
    if (_data->_impl->ignoresNonWheelEvents())
        return;

    _data->_impl->setLastMouseDownEvent(event);
    _data->_impl->setIgnoresMouseDraggedEvents(false);

    [self mouseDownInternal:event];
}

- (void)mouseUp:(NSEvent *)event
{
    if (_data->_impl->ignoresNonWheelEvents())
        return;

    _data->_impl->setLastMouseDownEvent(nil);
    [self mouseUpInternal:event];
}

- (void)mouseDragged:(NSEvent *)event
{
    if (_data->_impl->ignoresNonWheelEvents())
        return;
    if (_data->_impl->ignoresMouseDraggedEvents())
        return;

    [self mouseDraggedInternal:event];
}

- (void)pressureChangeWithEvent:(NSEvent *)event
{
    _data->_impl->pressureChangeWithEvent(event);
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
    // There's a chance that responding to this event will run a nested event loop, and
    // fetching a new event might release the old one. Retaining and then autoreleasing
    // the current event prevents that from causing a problem inside WebKit or AppKit code.
    [[event retain] autorelease];
    
    if (![self hitTest:[event locationInWindow]])
        return NO;
    
    _data->_impl->setLastMouseDownEvent(event);
    bool result = _data->_page->acceptsFirstMouse([event eventNumber], WebEventFactory::createWebMouseEvent(event, _data->_impl->lastPressureEvent(), self));
    _data->_impl->setLastMouseDownEvent(nil);
    return result;
}

- (BOOL)shouldDelayWindowOrderingForEvent:(NSEvent *)event
{
    // If this is the active window or we don't have a range selection, there is no need to perform additional checks
    // and we can avoid making a synchronous call to the WebProcess.
    if ([[self window] isKeyWindow] || _data->_page->editorState().selectionIsNone || !_data->_page->editorState().selectionIsRange)
        return NO;

    // There's a chance that responding to this event will run a nested event loop, and
    // fetching a new event might release the old one. Retaining and then autoreleasing
    // the current event prevents that from causing a problem inside WebKit or AppKit code.
    [[event retain] autorelease];
    
    if (![self hitTest:[event locationInWindow]])
        return NO;
    
    _data->_impl->setLastMouseDownEvent(event);
    bool result = _data->_page->shouldDelayWindowOrderingForEvent(WebEventFactory::createWebMouseEvent(event, _data->_impl->lastPressureEvent(), self));
    _data->_impl->setLastMouseDownEvent(nil);
    return result;
}

static void extractUnderlines(NSAttributedString *string, Vector<CompositionUnderline>& result)
{
    int length = [[string string] length];
    
    int i = 0;
    while (i < length) {
        NSRange range;
        NSDictionary *attrs = [string attributesAtIndex:i longestEffectiveRange:&range inRange:NSMakeRange(i, length - i)];
        
        if (NSNumber *style = [attrs objectForKey:NSUnderlineStyleAttributeName]) {
            Color color = Color::black;
            if (NSColor *colorAttr = [attrs objectForKey:NSUnderlineColorAttributeName])
                color = colorFromNSColor([colorAttr colorUsingColorSpaceName:NSDeviceRGBColorSpace]);
            result.append(CompositionUnderline(range.location, NSMaxRange(range), color, [style intValue] > 1));
        }
        
        i = range.location + range.length;
    }
}

#if USE(ASYNC_NSTEXTINPUTCLIENT)

- (void)_collectKeyboardLayoutCommandsForEvent:(NSEvent *)event to:(Vector<KeypressCommand>&)commands
{
    if ([event type] != NSKeyDown)
        return;

    ASSERT(!_data->_collectedKeypressCommands);
    _data->_collectedKeypressCommands = &commands;

    if (NSTextInputContext *context = [self inputContext])
        [context handleEventByKeyboardLayout:event];
    else
        [self interpretKeyEvents:[NSArray arrayWithObject:event]];

    _data->_collectedKeypressCommands = nullptr;
}

- (void)_interpretKeyEvent:(NSEvent *)event completionHandler:(void(^)(BOOL handled, const Vector<KeypressCommand>& commands))completionHandler
{
    // For regular Web content, input methods run before passing a keydown to DOM, but plug-ins get an opportunity to handle the event first.
    // There is no need to collect commands, as the plug-in cannot execute them.
    if (_data->_impl->pluginComplexTextInputIdentifier()) {
        completionHandler(NO, Vector<KeypressCommand>());
        return;
    }

    if (![self inputContext]) {
        Vector<KeypressCommand> commands;
        [self _collectKeyboardLayoutCommandsForEvent:event to:commands];
        completionHandler(NO, commands);
        return;
    }

    LOG(TextInput, "-> handleEventByInputMethod:%p %@", event, event);
    [[self inputContext] handleEventByInputMethod:event completionHandler:^(BOOL handled) {
        
        LOG(TextInput, "... handleEventByInputMethod%s handled", handled ? "" : " not");
        if (handled) {
            completionHandler(YES, Vector<KeypressCommand>());
            return;
        }

        Vector<KeypressCommand> commands;
        [self _collectKeyboardLayoutCommandsForEvent:event to:commands];
        completionHandler(NO, commands);
    }];
}

- (void)doCommandBySelector:(SEL)selector
{
    LOG(TextInput, "doCommandBySelector:\"%s\"", sel_getName(selector));

    Vector<KeypressCommand>* keypressCommands = _data->_collectedKeypressCommands;

    if (keypressCommands) {
        KeypressCommand command(NSStringFromSelector(selector));
        keypressCommands->append(command);
        LOG(TextInput, "...stored");
        _data->_page->registerKeypressCommandName(command.commandName);
    } else {
        // FIXME: Send the command to Editor synchronously and only send it along the
        // responder chain if it's a selector that does not correspond to an editing command.
        [super doCommandBySelector:selector];
    }
}

- (void)insertText:(id)string
{
    // Unlike an NSTextInputClient variant with replacementRange, this NSResponder method is called when there is no input context,
    // so text input processing isn't performed. We are not going to actually insert any text in that case, but saving an insertText
    // command ensures that a keypress event is dispatched as appropriate.
    [self insertText:string replacementRange:NSMakeRange(NSNotFound, 0)];
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
    BOOL isAttributedString = [string isKindOfClass:[NSAttributedString class]];
    ASSERT(isAttributedString || [string isKindOfClass:[NSString class]]);

    if (replacementRange.location != NSNotFound)
        LOG(TextInput, "insertText:\"%@\" replacementRange:(%u, %u)", isAttributedString ? [string string] : string, replacementRange.location, replacementRange.length);
    else
        LOG(TextInput, "insertText:\"%@\"", isAttributedString ? [string string] : string);

    NSString *text;
    Vector<TextAlternativeWithRange> dictationAlternatives;

    bool registerUndoGroup = false;
    if (isAttributedString) {
#if USE(DICTATION_ALTERNATIVES)
        collectDictationTextAlternatives(string, dictationAlternatives);
#endif
#if USE(INSERTION_UNDO_GROUPING)
        registerUndoGroup = shouldRegisterInsertionUndoGroup(string);
#endif
        // FIXME: We ignore most attributes from the string, so for example inserting from Character Palette loses font and glyph variation data.
        text = [string string];
    } else
        text = string;

    // insertText can be called for several reasons:
    // - If it's from normal key event processing (including key bindings), we save the action to perform it later.
    // - If it's from an input method, then we should insert the text now.
    // - If it's sent outside of keyboard event processing (e.g. from Character Viewer, or when confirming an inline input area with a mouse),
    // then we also execute it immediately, as there will be no other chance.
    Vector<KeypressCommand>* keypressCommands = _data->_collectedKeypressCommands;
    if (keypressCommands) {
        ASSERT(replacementRange.location == NSNotFound);
        KeypressCommand command("insertText:", text);
        keypressCommands->append(command);
        LOG(TextInput, "...stored");
        _data->_page->registerKeypressCommandName(command.commandName);
        return;
    }

    String eventText = text;
    eventText.replace(NSBackTabCharacter, NSTabCharacter); // same thing is done in KeyEventMac.mm in WebCore
    if (!dictationAlternatives.isEmpty())
        _data->_page->insertDictatedTextAsync(eventText, replacementRange, dictationAlternatives, registerUndoGroup);
    else
        _data->_page->insertTextAsync(eventText, replacementRange, registerUndoGroup);
}

- (void)selectedRangeWithCompletionHandler:(void(^)(NSRange selectedRange))completionHandlerPtr
{
    RetainPtr<id> completionHandler = adoptNS([completionHandlerPtr copy]);

    LOG(TextInput, "selectedRange");
    _data->_page->getSelectedRangeAsync([completionHandler](const EditingRange& editingRangeResult, WebKit::CallbackBase::Error error) {
        void (^completionHandlerBlock)(NSRange) = (void (^)(NSRange))completionHandler.get();
        if (error != WebKit::CallbackBase::Error::None) {
            LOG(TextInput, "    ...selectedRange failed.");
            completionHandlerBlock(NSMakeRange(NSNotFound, 0));
            return;
        }
        NSRange result = editingRangeResult;
        if (result.location == NSNotFound)
            LOG(TextInput, "    -> selectedRange returned (NSNotFound, %llu)", result.length);
        else
            LOG(TextInput, "    -> selectedRange returned (%llu, %llu)", result.location, result.length);
        completionHandlerBlock(result);
    });
}

- (void)markedRangeWithCompletionHandler:(void(^)(NSRange markedRange))completionHandlerPtr
{
    RetainPtr<id> completionHandler = adoptNS([completionHandlerPtr copy]);

    LOG(TextInput, "markedRange");
    _data->_page->getMarkedRangeAsync([completionHandler](const EditingRange& editingRangeResult, WebKit::CallbackBase::Error error) {
        void (^completionHandlerBlock)(NSRange) = (void (^)(NSRange))completionHandler.get();
        if (error != WebKit::CallbackBase::Error::None) {
            LOG(TextInput, "    ...markedRange failed.");
            completionHandlerBlock(NSMakeRange(NSNotFound, 0));
            return;
        }
        NSRange result = editingRangeResult;
        if (result.location == NSNotFound)
            LOG(TextInput, "    -> markedRange returned (NSNotFound, %llu)", result.length);
        else
            LOG(TextInput, "    -> markedRange returned (%llu, %llu)", result.location, result.length);
        completionHandlerBlock(result);
    });
}

- (void)hasMarkedTextWithCompletionHandler:(void(^)(BOOL hasMarkedText))completionHandlerPtr
{
    RetainPtr<id> completionHandler = adoptNS([completionHandlerPtr copy]);

    LOG(TextInput, "hasMarkedText");
    _data->_page->getMarkedRangeAsync([completionHandler](const EditingRange& editingRangeResult, WebKit::CallbackBase::Error error) {
        void (^completionHandlerBlock)(BOOL) = (void (^)(BOOL))completionHandler.get();
        if (error != WebKit::CallbackBase::Error::None) {
            LOG(TextInput, "    ...hasMarkedText failed.");
            completionHandlerBlock(NO);
            return;
        }
        BOOL hasMarkedText = editingRangeResult.location != notFound;
        LOG(TextInput, "    -> hasMarkedText returned %u", hasMarkedText);
        completionHandlerBlock(hasMarkedText);
    });
}

- (void)attributedSubstringForProposedRange:(NSRange)nsRange completionHandler:(void(^)(NSAttributedString *attrString, NSRange actualRange))completionHandlerPtr
{
    RetainPtr<id> completionHandler = adoptNS([completionHandlerPtr copy]);

    LOG(TextInput, "attributedSubstringFromRange:(%u, %u)", nsRange.location, nsRange.length);
    _data->_page->attributedSubstringForCharacterRangeAsync(nsRange, [completionHandler](const AttributedString& string, const EditingRange& actualRange, WebKit::CallbackBase::Error error) {
        void (^completionHandlerBlock)(NSAttributedString *, NSRange) = (void (^)(NSAttributedString *, NSRange))completionHandler.get();
        if (error != WebKit::CallbackBase::Error::None) {
            LOG(TextInput, "    ...attributedSubstringFromRange failed.");
            completionHandlerBlock(0, NSMakeRange(NSNotFound, 0));
            return;
        }
        LOG(TextInput, "    -> attributedSubstringFromRange returned %@", [string.string.get() string]);
        completionHandlerBlock([[string.string.get() retain] autorelease], actualRange);
    });
}

- (void)firstRectForCharacterRange:(NSRange)theRange completionHandler:(void(^)(NSRect firstRect, NSRange actualRange))completionHandlerPtr
{
    RetainPtr<id> completionHandler = adoptNS([completionHandlerPtr copy]);

    LOG(TextInput, "firstRectForCharacterRange:(%u, %u)", theRange.location, theRange.length);

    // Just to match NSTextView's behavior. Regression tests cannot detect this;
    // to reproduce, use a test application from http://bugs.webkit.org/show_bug.cgi?id=4682
    // (type something; try ranges (1, -1) and (2, -1).
    if ((theRange.location + theRange.length < theRange.location) && (theRange.location + theRange.length != 0))
        theRange.length = 0;

    if (theRange.location == NSNotFound) {
        LOG(TextInput, "    -> NSZeroRect");
        completionHandlerPtr(NSZeroRect, theRange);
        return;
    }

    _data->_page->firstRectForCharacterRangeAsync(theRange, [self, completionHandler](const IntRect& rect, const EditingRange& actualRange, WebKit::CallbackBase::Error error) {
        void (^completionHandlerBlock)(NSRect, NSRange) = (void (^)(NSRect, NSRange))completionHandler.get();
        if (error != WebKit::CallbackBase::Error::None) {
            LOG(TextInput, "    ...firstRectForCharacterRange failed.");
            completionHandlerBlock(NSZeroRect, NSMakeRange(NSNotFound, 0));
            return;
        }

        NSRect resultRect = [self convertRect:rect toView:nil];
        resultRect = [self.window convertRectToScreen:resultRect];

        LOG(TextInput, "    -> firstRectForCharacterRange returned (%f, %f, %f, %f)", resultRect.origin.x, resultRect.origin.y, resultRect.size.width, resultRect.size.height);
        completionHandlerBlock(resultRect, actualRange);
    });
}

- (void)characterIndexForPoint:(NSPoint)thePoint completionHandler:(void(^)(NSUInteger))completionHandlerPtr
{
    RetainPtr<id> completionHandler = adoptNS([completionHandlerPtr copy]);

    LOG(TextInput, "characterIndexForPoint:(%f, %f)", thePoint.x, thePoint.y);

    NSWindow *window = [self window];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if (window)
        thePoint = [window convertScreenToBase:thePoint];
#pragma clang diagnostic pop
    thePoint = [self convertPoint:thePoint fromView:nil];  // the point is relative to the main frame

    _data->_page->characterIndexForPointAsync(IntPoint(thePoint), [completionHandler](uint64_t result, WebKit::CallbackBase::Error error) {
        void (^completionHandlerBlock)(NSUInteger) = (void (^)(NSUInteger))completionHandler.get();
        if (error != WebKit::CallbackBase::Error::None) {
            LOG(TextInput, "    ...characterIndexForPoint failed.");
            completionHandlerBlock(0);
            return;
        }
        if (result == notFound)
            result = NSNotFound;
        LOG(TextInput, "    -> characterIndexForPoint returned %lu", result);
        completionHandlerBlock(result);
    });
}

- (NSTextInputContext *)inputContext
{
    if (_data->_impl->pluginComplexTextInputIdentifier()) {
        ASSERT(!_data->_collectedKeypressCommands); // Should not get here from -_interpretKeyEvent:completionHandler:, we only use WKTextInputWindowController after giving the plug-in a chance to handle keydown natively.
        return [[WKTextInputWindowController sharedTextInputWindowController] inputContext];
    }

    // Disable text input machinery when in non-editable content. An invisible inline input area affects performance, and can prevent Expose from working.
    if (!_data->_page->editorState().isContentEditable)
        return nil;

    return [super inputContext];
}

- (void)unmarkText
{
    LOG(TextInput, "unmarkText");

    _data->_page->confirmCompositionAsync();
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange
{
    BOOL isAttributedString = [string isKindOfClass:[NSAttributedString class]];
    ASSERT(isAttributedString || [string isKindOfClass:[NSString class]]);

    LOG(TextInput, "setMarkedText:\"%@\" selectedRange:(%u, %u) replacementRange:(%u, %u)", isAttributedString ? [string string] : string, selectedRange.location, selectedRange.length, replacementRange.location, replacementRange.length);

    Vector<CompositionUnderline> underlines;
    NSString *text;

    if (isAttributedString) {
        // FIXME: We ignore most attributes from the string, so an input method cannot specify e.g. a font or a glyph variation.
        text = [string string];
        extractUnderlines(string, underlines);
    } else
        text = string;

    if (_data->_impl->inSecureInputState()) {
        // In password fields, we only allow ASCII dead keys, and don't allow inline input, matching NSSecureTextInputField.
        // Allowing ASCII dead keys is necessary to enable full Roman input when using a Vietnamese keyboard.
        ASSERT(!_data->_page->editorState().hasComposition);
        _data->_impl->notifyInputContextAboutDiscardedComposition();
        // FIXME: We should store the command to handle it after DOM event processing, as it's regular keyboard input now, not a composition.
        if ([text length] == 1 && isASCII([text characterAtIndex:0]))
            _data->_page->insertTextAsync(text, replacementRange);
        else
            NSBeep();
        return;
    }

    _data->_page->setCompositionAsync(text, underlines, selectedRange, replacementRange);
}

// Synchronous NSTextInputClient is still implemented to catch spurious sync calls. Remove when that is no longer needed.

- (NSRange)selectedRange NO_RETURN_DUE_TO_ASSERT
{
    ASSERT_NOT_REACHED();
    return NSMakeRange(NSNotFound, 0);
}

- (BOOL)hasMarkedText NO_RETURN_DUE_TO_ASSERT
{
    ASSERT_NOT_REACHED();
    return NO;
}

- (NSRange)markedRange NO_RETURN_DUE_TO_ASSERT
{
    ASSERT_NOT_REACHED();
    return NSMakeRange(NSNotFound, 0);
}

- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)nsRange actualRange:(NSRangePointer)actualRange NO_RETURN_DUE_TO_ASSERT
{
    ASSERT_NOT_REACHED();
    return nil;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)thePoint NO_RETURN_DUE_TO_ASSERT
{
    ASSERT_NOT_REACHED();
    return 0;
}

- (NSRect)firstRectForCharacterRange:(NSRange)theRange actualRange:(NSRangePointer)actualRange NO_RETURN_DUE_TO_ASSERT
{ 
    ASSERT_NOT_REACHED();
    return NSMakeRect(0, 0, 0, 0);
}

- (BOOL)performKeyEquivalent:(NSEvent *)event
{
    if (_data->_impl->ignoresNonWheelEvents())
        return NO;

    // There's a chance that responding to this event will run a nested event loop, and
    // fetching a new event might release the old one. Retaining and then autoreleasing
    // the current event prevents that from causing a problem inside WebKit or AppKit code.
    [[event retain] autorelease];

    // We get Esc key here after processing either Esc or Cmd+period. The former starts as a keyDown, and the latter starts as a key equivalent,
    // but both get transformed to a cancelOperation: command, executing which passes an Esc key event to -performKeyEquivalent:.
    // Don't interpret this event again, avoiding re-entrancy and infinite loops.
    if ([[event charactersIgnoringModifiers] isEqualToString:@"\e"] && !([event modifierFlags] & NSDeviceIndependentModifierFlagsMask))
        return [super performKeyEquivalent:event];

    if (_data->_keyDownEventBeingResent) {
        // WebCore has already seen the event, no need for custom processing.
        // Note that we can get multiple events for each event being re-sent. For example, for Cmd+'=' AppKit
        // first performs the original key equivalent, and if that isn't handled, it dispatches a synthetic Cmd+'+'.
        return [super performKeyEquivalent:event];
    }

    ASSERT(event == [NSApp currentEvent]);

    _data->_impl->disableComplexTextInputIfNecessary();

    // Pass key combos through WebCore if there is a key binding available for
    // this event. This lets webpages have a crack at intercepting key-modified keypresses.
    // FIXME: Why is the firstResponder check needed?
    if (self == [[self window] firstResponder]) {
        [self _interpretKeyEvent:event completionHandler:^(BOOL handledByInputMethod, const Vector<KeypressCommand>& commands) {
            _data->_page->handleKeyboardEvent(NativeWebKeyboardEvent(event, handledByInputMethod, commands));
        }];
        return YES;
    }
    
    return [super performKeyEquivalent:event];
}

- (void)keyUp:(NSEvent *)theEvent
{
    if (_data->_impl->ignoresNonWheelEvents())
        return;

    LOG(TextInput, "keyUp:%p %@", theEvent, theEvent);

    [self _interpretKeyEvent:theEvent completionHandler:^(BOOL handledByInputMethod, const Vector<KeypressCommand>& commands) {
        ASSERT(!handledByInputMethod || commands.isEmpty());
        _data->_page->handleKeyboardEvent(NativeWebKeyboardEvent(theEvent, handledByInputMethod, commands));
    }];
}

- (void)keyDown:(NSEvent *)theEvent
{
    if (_data->_impl->ignoresNonWheelEvents())
        return;

    LOG(TextInput, "keyDown:%p %@%s", theEvent, theEvent, (theEvent == _data->_keyDownEventBeingResent) ? " (re-sent)" : "");

    if (_data->_impl->tryHandlePluginComplexTextInputKeyDown(theEvent)) {
        LOG(TextInput, "...handled by plug-in");
        return;
    }

    // We could be receiving a key down from AppKit if we have re-sent an event
    // that maps to an action that is currently unavailable (for example a copy when
    // there is no range selection).
    // If this is the case we should ignore the key down.
    if (_data->_keyDownEventBeingResent == theEvent) {
        [super keyDown:theEvent];
        return;
    }

    [self _interpretKeyEvent:theEvent completionHandler:^(BOOL handledByInputMethod, const Vector<KeypressCommand>& commands) {
        ASSERT(!handledByInputMethod || commands.isEmpty());
        _data->_page->handleKeyboardEvent(NativeWebKeyboardEvent(theEvent, handledByInputMethod, commands));
    }];
}

- (void)flagsChanged:(NSEvent *)theEvent
{
    if (_data->_impl->ignoresNonWheelEvents())
        return;

    LOG(TextInput, "flagsChanged:%p %@", theEvent, theEvent);

    unsigned short keyCode = [theEvent keyCode];

    // Don't make an event from the num lock and function keys
    if (!keyCode || keyCode == 10 || keyCode == 63)
        return;

    [self _interpretKeyEvent:theEvent completionHandler:^(BOOL handledByInputMethod, const Vector<KeypressCommand>& commands) {
        _data->_page->handleKeyboardEvent(NativeWebKeyboardEvent(theEvent, handledByInputMethod, commands));
    }];
}

#else // USE(ASYNC_NSTEXTINPUTCLIENT)

- (BOOL)_interpretKeyEvent:(NSEvent *)event savingCommandsTo:(Vector<WebCore::KeypressCommand>&)commands
{
    ASSERT(!_data->_interpretKeyEventsParameters);
    ASSERT(commands.isEmpty());

    if ([event type] == NSFlagsChanged)
        return NO;

    WKViewInterpretKeyEventsParameters parameters;
    parameters.eventInterpretationHadSideEffects = false;
    parameters.executingSavedKeypressCommands = false;
    // We assume that an input method has consumed the event, and only change this assumption if one of the NSTextInput methods is called.
    // We assume the IM will *not* consume hotkey sequences.
    parameters.consumedByIM = !([event modifierFlags] & NSCommandKeyMask);
    parameters.commands = &commands;
    _data->_interpretKeyEventsParameters = &parameters;

    LOG(TextInput, "-> interpretKeyEvents:%p %@", event, event);
    [self interpretKeyEvents:[NSArray arrayWithObject:event]];

    _data->_interpretKeyEventsParameters = nullptr;

    // An input method may consume an event and not tell us (e.g. when displaying a candidate window),
    // in which case we should not bubble the event up the DOM.
    if (parameters.consumedByIM) {
        ASSERT(commands.isEmpty());
        LOG(TextInput, "...event %p was consumed by an input method", event);
        return YES;
    }

    LOG(TextInput, "...interpretKeyEvents for event %p done, returns %d", event, parameters.eventInterpretationHadSideEffects);

    // If we have already executed all or some of the commands, the event is "handled". Note that there are additional checks on web process side.
    return parameters.eventInterpretationHadSideEffects;
}

- (void)_executeSavedKeypressCommands
{
    WKViewInterpretKeyEventsParameters* parameters = _data->_interpretKeyEventsParameters;
    if (!parameters || parameters->commands->isEmpty())
        return;

    // We could be called again if the execution of one command triggers a call to selectedRange.
    // In this case, the state is up to date, and we don't need to execute any more saved commands to return a result.
    if (parameters->executingSavedKeypressCommands)
        return;

    LOG(TextInput, "Executing %u saved keypress commands...", parameters->commands->size());

    parameters->executingSavedKeypressCommands = true;
    parameters->eventInterpretationHadSideEffects |= _data->_page->executeKeypressCommands(*parameters->commands);
    parameters->commands->clear();
    parameters->executingSavedKeypressCommands = false;

    LOG(TextInput, "...done executing saved keypress commands.");
}

- (void)doCommandBySelector:(SEL)selector
{
    LOG(TextInput, "doCommandBySelector:\"%s\"", sel_getName(selector));

    WKViewInterpretKeyEventsParameters* parameters = _data->_interpretKeyEventsParameters;
    if (parameters)
        parameters->consumedByIM = false;

    // As in insertText:replacementRange:, we assume that the call comes from an input method if there is marked text.
    bool isFromInputMethod = _data->_page->editorState().hasComposition;

    if (parameters && !isFromInputMethod) {
        KeypressCommand command(NSStringFromSelector(selector));
        parameters->commands->append(command);
        LOG(TextInput, "...stored");
        _data->_page->registerKeypressCommandName(command.commandName);
    } else {
        // FIXME: Send the command to Editor synchronously and only send it along the
        // responder chain if it's a selector that does not correspond to an editing command.
        [super doCommandBySelector:selector];
    }
}

- (void)insertText:(id)string
{
    // Unlike an NSTextInputClient variant with replacementRange, this NSResponder method is called when there is no input context,
    // so text input processing isn't performed. We are not going to actually insert any text in that case, but saving an insertText
    // command ensures that a keypress event is dispatched as appropriate.
    [self insertText:string replacementRange:NSMakeRange(NSNotFound, 0)];
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
    BOOL isAttributedString = [string isKindOfClass:[NSAttributedString class]];
    ASSERT(isAttributedString || [string isKindOfClass:[NSString class]]);

    if (replacementRange.location != NSNotFound)
        LOG(TextInput, "insertText:\"%@\" replacementRange:(%u, %u)", isAttributedString ? [string string] : string, replacementRange.location, replacementRange.length);
    else
        LOG(TextInput, "insertText:\"%@\"", isAttributedString ? [string string] : string);
    WKViewInterpretKeyEventsParameters* parameters = _data->_interpretKeyEventsParameters;
    if (parameters)
        parameters->consumedByIM = false;

    NSString *text;
    bool isFromInputMethod = _data->_page->editorState().hasComposition;

    Vector<TextAlternativeWithRange> dictationAlternatives;

    if (isAttributedString) {
#if USE(DICTATION_ALTERNATIVES)
        collectDictationTextAlternatives(string, dictationAlternatives);
#endif
        // FIXME: We ignore most attributes from the string, so for example inserting from Character Palette loses font and glyph variation data.
        text = [string string];
    } else
        text = string;

    // insertText can be called for several reasons:
    // - If it's from normal key event processing (including key bindings), we may need to save the action to perform it later.
    // - If it's from an input method, then we should insert the text now. We assume it's from the input method if we have marked text.
    // FIXME: In theory, this could be wrong for some input methods, so we should try to find another way to determine if the call is from the input method.
    // - If it's sent outside of keyboard event processing (e.g. from Character Viewer, or when confirming an inline input area with a mouse),
    // then we also execute it immediately, as there will be no other chance.
    if (parameters && !isFromInputMethod) {
        // FIXME: Handle replacementRange in this case, too. It's known to occur in practice when canceling Press and Hold (see <rdar://11940670>).
        ASSERT(replacementRange.location == NSNotFound);
        KeypressCommand command("insertText:", text);
        parameters->commands->append(command);
        _data->_page->registerKeypressCommandName(command.commandName);
        return;
    }

    String eventText = text;
    eventText.replace(NSBackTabCharacter, NSTabCharacter); // same thing is done in KeyEventMac.mm in WebCore
    bool eventHandled;
    if (!dictationAlternatives.isEmpty())
        eventHandled = _data->_page->insertDictatedText(eventText, replacementRange, dictationAlternatives);
    else
        eventHandled = _data->_page->insertText(eventText, replacementRange);

    if (parameters)
        parameters->eventInterpretationHadSideEffects |= eventHandled;
}

- (NSTextInputContext *)inputContext
{
    WKViewInterpretKeyEventsParameters* parameters = _data->_interpretKeyEventsParameters;

    if (_data->_impl->pluginComplexTextInputIdentifier() && !parameters)
        return [[WKTextInputWindowController sharedTextInputWindowController] inputContext];

    // Disable text input machinery when in non-editable content. An invisible inline input area affects performance, and can prevent Expose from working.
    if (!_data->_page->editorState().isContentEditable)
        return nil;

    return [super inputContext];
}

- (NSRange)selectedRange
{
    [self _executeSavedKeypressCommands];

    EditingRange selectedRange;
    _data->_page->getSelectedRange(selectedRange);

    NSRange result = selectedRange;
    if (result.location == NSNotFound)
        LOG(TextInput, "selectedRange -> (NSNotFound, %u)", result.length);
    else
        LOG(TextInput, "selectedRange -> (%u, %u)", result.location, result.length);

    return result;
}

- (BOOL)hasMarkedText
{
    WKViewInterpretKeyEventsParameters* parameters = _data->_interpretKeyEventsParameters;

    BOOL result;
    if (parameters) {
        result = _data->_page->editorState().hasComposition;
        if (result) {
            // A saved command can confirm a composition, but it cannot start a new one.
            [self _executeSavedKeypressCommands];
            result = _data->_page->editorState().hasComposition;
        }
    } else {
        EditingRange markedRange;
        _data->_page->getMarkedRange(markedRange);
        result = markedRange.location != notFound;
    }

    LOG(TextInput, "hasMarkedText -> %u", result);
    return result;
}

- (void)unmarkText
{
    [self _executeSavedKeypressCommands];

    LOG(TextInput, "unmarkText");

    // Use pointer to get parameters passed to us by the caller of interpretKeyEvents.
    WKViewInterpretKeyEventsParameters* parameters = _data->_interpretKeyEventsParameters;

    if (parameters) {
        parameters->eventInterpretationHadSideEffects = true;
        parameters->consumedByIM = false;
    }

    _data->_page->confirmComposition();
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)newSelectedRange replacementRange:(NSRange)replacementRange
{
    [self _executeSavedKeypressCommands];

    BOOL isAttributedString = [string isKindOfClass:[NSAttributedString class]];
    ASSERT(isAttributedString || [string isKindOfClass:[NSString class]]);

    LOG(TextInput, "setMarkedText:\"%@\" selectedRange:(%u, %u)", isAttributedString ? [string string] : string, newSelectedRange.location, newSelectedRange.length);

    // Use pointer to get parameters passed to us by the caller of interpretKeyEvents.
    WKViewInterpretKeyEventsParameters* parameters = _data->_interpretKeyEventsParameters;

    if (parameters) {
        parameters->eventInterpretationHadSideEffects = true;
        parameters->consumedByIM = false;
    }
    
    Vector<CompositionUnderline> underlines;
    NSString *text;

    if (isAttributedString) {
        // FIXME: We ignore most attributes from the string, so an input method cannot specify e.g. a font or a glyph variation.
        text = [string string];
        extractUnderlines(string, underlines);
    } else
        text = string;

    if (_data->_page->editorState().isInPasswordField) {
        // In password fields, we only allow ASCII dead keys, and don't allow inline input, matching NSSecureTextInputField.
        // Allowing ASCII dead keys is necessary to enable full Roman input when using a Vietnamese keyboard.
        ASSERT(!_data->_page->editorState().hasComposition);
        _data->_impl->notifyInputContextAboutDiscardedComposition();
        if ([text length] == 1 && [[text decomposedStringWithCanonicalMapping] characterAtIndex:0] < 0x80) {
            _data->_page->insertText(text, replacementRange);
        } else
            NSBeep();
        return;
    }

    _data->_page->setComposition(text, underlines, newSelectedRange, replacementRange);
}

- (NSRange)markedRange
{
    [self _executeSavedKeypressCommands];

    EditingRange markedRange;
    _data->_page->getMarkedRange(markedRange);

    NSRange result = markedRange;
    if (result.location == NSNotFound)
        LOG(TextInput, "markedRange -> (NSNotFound, %u)", result.length);
    else
        LOG(TextInput, "markedRange -> (%u, %u)", result.location, result.length);

    return result;
}

- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)nsRange actualRange:(NSRangePointer)actualRange
{
    [self _executeSavedKeypressCommands];

    if (!_data->_page->editorState().isContentEditable) {
        LOG(TextInput, "attributedSubstringFromRange:(%u, %u) -> nil", nsRange.location, nsRange.length);
        return nil;
    }

    if (_data->_page->editorState().isInPasswordField)
        return nil;

    AttributedString result;
    _data->_page->getAttributedSubstringFromRange(nsRange, result);

    if (actualRange) {
        *actualRange = nsRange;
        actualRange->length = [result.string length];
    }

    LOG(TextInput, "attributedSubstringFromRange:(%u, %u) -> \"%@\"", nsRange.location, nsRange.length, [result.string string]);
    return [[result.string retain] autorelease];
}

- (NSUInteger)characterIndexForPoint:(NSPoint)thePoint
{
    [self _executeSavedKeypressCommands];

    NSWindow *window = [self window];
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if (window)
        thePoint = [window convertScreenToBase:thePoint];
#pragma clang diagnostic pop
    thePoint = [self convertPoint:thePoint fromView:nil];  // the point is relative to the main frame
    
    uint64_t result = _data->_page->characterIndexForPoint(IntPoint(thePoint));
    if (result == notFound)
        result = NSNotFound;
    LOG(TextInput, "characterIndexForPoint:(%f, %f) -> %u", thePoint.x, thePoint.y, result);
    return result;
}

- (NSRect)firstRectForCharacterRange:(NSRange)theRange actualRange:(NSRangePointer)actualRange
{ 
    [self _executeSavedKeypressCommands];

    // Just to match NSTextView's behavior. Regression tests cannot detect this;
    // to reproduce, use a test application from http://bugs.webkit.org/show_bug.cgi?id=4682
    // (type something; try ranges (1, -1) and (2, -1).
    if ((theRange.location + theRange.length < theRange.location) && (theRange.location + theRange.length != 0))
        theRange.length = 0;

    if (theRange.location == NSNotFound) {
        if (actualRange)
            *actualRange = theRange;
        LOG(TextInput, "firstRectForCharacterRange:(NSNotFound, %u) -> NSZeroRect", theRange.length);
        return NSZeroRect;
    }

    NSRect resultRect = _data->_page->firstRectForCharacterRange(theRange);
    resultRect = [self convertRect:resultRect toView:nil];
    resultRect = [self.window convertRectToScreen:resultRect];

    if (actualRange) {
        // FIXME: Update actualRange to match the range of first rect.
        *actualRange = theRange;
    }

    LOG(TextInput, "firstRectForCharacterRange:(%u, %u) -> (%f, %f, %f, %f)", theRange.location, theRange.length, resultRect.origin.x, resultRect.origin.y, resultRect.size.width, resultRect.size.height);
    return resultRect;
}

- (BOOL)performKeyEquivalent:(NSEvent *)event
{
    if (_data->_impl->ignoresNonWheelEvents())
        return NO;

    // There's a chance that responding to this event will run a nested event loop, and
    // fetching a new event might release the old one. Retaining and then autoreleasing
    // the current event prevents that from causing a problem inside WebKit or AppKit code.
    [[event retain] autorelease];

    // We get Esc key here after processing either Esc or Cmd+period. The former starts as a keyDown, and the latter starts as a key equivalent,
    // but both get transformed to a cancelOperation: command, executing which passes an Esc key event to -performKeyEquivalent:.
    // Don't interpret this event again, avoiding re-entrancy and infinite loops.
    if ([[event charactersIgnoringModifiers] isEqualToString:@"\e"] && !([event modifierFlags] & NSDeviceIndependentModifierFlagsMask))
        return [super performKeyEquivalent:event];

    if (_data->_keyDownEventBeingResent) {
        // WebCore has already seen the event, no need for custom processing.
        // Note that we can get multiple events for each event being re-sent. For example, for Cmd+'=' AppKit
        // first performs the original key equivalent, and if that isn't handled, it dispatches a synthetic Cmd+'+'.
        return [super performKeyEquivalent:event];
    }

    ASSERT(event == [NSApp currentEvent]);

    _data->_impl->disableComplexTextInputIfNecessary();

    // Pass key combos through WebCore if there is a key binding available for
    // this event. This lets webpages have a crack at intercepting key-modified keypresses.
    // FIXME: Why is the firstResponder check needed?
    if (self == [[self window] firstResponder]) {
        Vector<KeypressCommand> commands;
        BOOL handledByInputMethod = [self _interpretKeyEvent:event savingCommandsTo:commands];
        _data->_page->handleKeyboardEvent(NativeWebKeyboardEvent(event, handledByInputMethod, commands));
        return YES;
    }
    
    return [super performKeyEquivalent:event];
}

- (void)keyUp:(NSEvent *)theEvent
{
    if (_data->_impl->ignoresNonWheelEvents())
        return;

    LOG(TextInput, "keyUp:%p %@", theEvent, theEvent);
    // We don't interpret the keyUp event, as this breaks key bindings (see <https://bugs.webkit.org/show_bug.cgi?id=130100>).
    _data->_page->handleKeyboardEvent(NativeWebKeyboardEvent(theEvent, false, Vector<KeypressCommand>()));
}

- (void)keyDown:(NSEvent *)theEvent
{
    if (_data->_impl->ignoresNonWheelEvents())
        return;

    LOG(TextInput, "keyDown:%p %@%s", theEvent, theEvent, (theEvent == _data->_keyDownEventBeingResent) ? " (re-sent)" : "");

    // There's a chance that responding to this event will run a nested event loop, and
    // fetching a new event might release the old one. Retaining and then autoreleasing
    // the current event prevents that from causing a problem inside WebKit or AppKit code.
    [[theEvent retain] autorelease];

    if (_data->_impl->tryHandlePluginComplexTextInputKeyDown(theEvent)) {
        LOG(TextInput, "...handled by plug-in");
        return;
    }

    // We could be receiving a key down from AppKit if we have re-sent an event
    // that maps to an action that is currently unavailable (for example a copy when
    // there is no range selection).
    // If this is the case we should ignore the key down.
    if (_data->_keyDownEventBeingResent == theEvent) {
        [super keyDown:theEvent];
        return;
    }

    Vector<KeypressCommand> commands;
    BOOL handledByInputMethod = [self _interpretKeyEvent:theEvent savingCommandsTo:commands];
    if (!commands.isEmpty()) {
        // An input method may make several actions per keypress. For example, pressing Return with Korean IM both confirms it and sends a newline.
        // IM-like actions are handled immediately (so the return value from UI process is true), but there are saved commands that
        // should be handled like normal text input after DOM event dispatch.
        handledByInputMethod = NO;
    }

    _data->_page->handleKeyboardEvent(NativeWebKeyboardEvent(theEvent, handledByInputMethod, commands));
}

- (void)flagsChanged:(NSEvent *)theEvent
{
    if (_data->_impl->ignoresNonWheelEvents())
        return;

    LOG(TextInput, "flagsChanged:%p %@", theEvent, theEvent);

    // There's a chance that responding to this event will run a nested event loop, and
    // fetching a new event might release the old one. Retaining and then autoreleasing
    // the current event prevents that from causing a problem inside WebKit or AppKit code.
    [[theEvent retain] autorelease];

    unsigned short keyCode = [theEvent keyCode];

    // Don't make an event from the num lock and function keys
    if (!keyCode || keyCode == 10 || keyCode == 63)
        return;

    _data->_page->handleKeyboardEvent(NativeWebKeyboardEvent(theEvent, false, Vector<KeypressCommand>()));
}

#endif // USE(ASYNC_NSTEXTINPUTCLIENT)

- (NSTextInputContext *)_superInputContext
{
    return [super inputContext];
}

- (void)_superQuickLookWithEvent:(NSEvent *)event
{
    [super quickLookWithEvent:event];
}

- (void)_superSwipeWithEvent:(NSEvent *)event
{
    [super swipeWithEvent:event];
}

- (void)_superMagnifyWithEvent:(NSEvent *)event
{
    [super magnifyWithEvent:event];
}

- (void)_superSmartMagnifyWithEvent:(NSEvent *)event
{
    [super smartMagnifyWithEvent:event];
}

- (void)_superRemoveTrackingRect:(NSTrackingRectTag)tag
{
    [super removeTrackingRect:tag];
}

- (id)_superAccessibilityAttributeValue:(NSString *)attribute
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return [super accessibilityAttributeValue:attribute];
#pragma clang diagnostic pop
}

- (NSArray *)validAttributesForMarkedText
{
    static NSArray *validAttributes;
    if (!validAttributes) {
        validAttributes = [[NSArray alloc] initWithObjects:
                           NSUnderlineStyleAttributeName, NSUnderlineColorAttributeName,
                           NSMarkedClauseSegmentAttributeName,
#if USE(DICTATION_ALTERNATIVES)
                           NSTextAlternativesAttributeName,
#endif
#if USE(INSERTION_UNDO_GROUPING)
                           NSTextInsertionUndoableAttributeName,
#endif
                           nil];
        // NSText also supports the following attributes, but it's
        // hard to tell which are really required for text input to
        // work well; I have not seen any input method make use of them yet.
        //     NSFontAttributeName, NSForegroundColorAttributeName,
        //     NSBackgroundColorAttributeName, NSLanguageAttributeName.
        CFRetain(validAttributes);
    }
    LOG(TextInput, "validAttributesForMarkedText -> (...)");
    return validAttributes;
}

#if ENABLE(DRAG_SUPPORT)
- (void)draggedImage:(NSImage *)image endedAt:(NSPoint)endPoint operation:(NSDragOperation)operation
{
    _data->_impl->draggedImage(image, NSPointToCGPoint(endPoint), operation);
}

- (NSDragOperation)draggingEntered:(id <NSDraggingInfo>)draggingInfo
{
    return _data->_impl->draggingEntered(draggingInfo);
}

- (NSDragOperation)draggingUpdated:(id <NSDraggingInfo>)draggingInfo
{
    return _data->_impl->draggingUpdated(draggingInfo);
}

- (void)draggingExited:(id <NSDraggingInfo>)draggingInfo
{
    _data->_impl->draggingExited(draggingInfo);
}

- (BOOL)prepareForDragOperation:(id <NSDraggingInfo>)draggingInfo
{
    return _data->_impl->prepareForDragOperation(draggingInfo);
}

- (BOOL)performDragOperation:(id <NSDraggingInfo>)draggingInfo
{
    return _data->_impl->performDragOperation(draggingInfo);
}

- (NSView *)_hitTest:(NSPoint *)point dragTypes:(NSSet *)types
{
    return _data->_impl->hitTestForDragTypes(NSPointToCGPoint(*point), types);
}
#endif // ENABLE(DRAG_SUPPORT)

- (BOOL)_windowResizeMouseLocationIsInVisibleScrollerThumb:(NSPoint)loc
{
    NSPoint localPoint = [self convertPoint:loc fromView:nil];
    NSRect visibleThumbRect = NSRect(_data->_page->visibleScrollerThumbRect());
    return NSMouseInRect(localPoint, visibleThumbRect, [self isFlipped]);
}

- (void)_addFontPanelObserver
{
    _data->_impl->startObservingFontPanel();
}

- (void)viewWillMoveToWindow:(NSWindow *)window
{
    _data->_impl->viewWillMoveToWindow(window);
}

- (void)viewDidMoveToWindow
{
    _data->_impl->viewDidMoveToWindow();
}

- (void)drawRect:(NSRect)rect
{
    LOG(Printing, "drawRect: x:%g, y:%g, width:%g, height:%g", rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
    _data->_page->endPrinting();
}

- (BOOL)isOpaque
{
    return _data->_page->drawsBackground();
}

- (BOOL)mouseDownCanMoveWindow
{
    // -[NSView mouseDownCanMoveWindow] returns YES when the NSView is transparent,
    // but we don't want a drag in the NSView to move the window, even if it's transparent.
    return NO;
}

- (void)viewDidHide
{
    _data->_page->viewStateDidChange(ViewState::IsVisible);
}

- (void)viewDidUnhide
{
    _data->_page->viewStateDidChange(ViewState::IsVisible);
}

- (void)viewDidChangeBackingProperties
{
    _data->_impl->viewDidChangeBackingProperties();
}

- (void)_activeSpaceDidChange:(NSNotification *)notification
{
    _data->_page->viewStateDidChange(ViewState::IsVisible);
}

- (id)accessibilityFocusedUIElement
{
    return _data->_impl->accessibilityFocusedUIElement();
}

- (BOOL)accessibilityIsIgnored
{
    return _data->_impl->accessibilityIsIgnored();
}

- (id)accessibilityHitTest:(NSPoint)point
{
    return _data->_impl->accessibilityHitTest(NSPointToCGPoint(point));
}

- (id)accessibilityAttributeValue:(NSString *)attribute
{
    return _data->_impl->accessibilityAttributeValue(attribute);
}

- (NSView *)hitTest:(NSPoint)point
{
    NSView *hitView = [super hitTest:point];
    if (hitView && _data && hitView == _data->_impl->layerHostingView())
        hitView = self;

    return hitView;
}

- (NSInteger)conversationIdentifier
{
    return (NSInteger)self;
}

- (void)quickLookWithEvent:(NSEvent *)event
{
    _data->_impl->quickLookWithEvent(event);
}

- (std::unique_ptr<WebKit::DrawingAreaProxy>)_createDrawingAreaProxy
{
    if ([[[NSUserDefaults standardUserDefaults] objectForKey:@"WebKit2UseRemoteLayerTreeDrawingArea"] boolValue])
        return std::make_unique<RemoteLayerTreeDrawingAreaProxy>(*_data->_page);

    return std::make_unique<TiledCoreAnimationDrawingAreaProxy>(*_data->_page);
}

- (void)_processDidExit
{
    _data->_impl->notifyInputContextAboutDiscardedComposition();

    if (_data->_impl->layerHostingView())
        _data->_impl->setAcceleratedCompositingRootLayer(nil);

    _data->_impl->updateRemoteAccessibilityRegistration(false);

    _data->_impl->resetGestureController();
}

- (void)_pageClosed
{
    _data->_impl->updateRemoteAccessibilityRegistration(false);
}

- (void)_didRelaunchProcess
{
    _data->_impl->accessibilityRegisterUIProcessTokens();
}

- (void)_setUserInterfaceItemState:(NSString *)commandName enabled:(BOOL)isEnabled state:(int)newState
{
    ValidationVector items = _data->_validationMap.take(commandName);
    size_t size = items.size();
    for (size_t i = 0; i < size; ++i) {
        ValidationItem item = items[i].get();
        [menuItem(item) setState:newState];
        [menuItem(item) setEnabled:isEnabled];
        [toolbarItem(item) setEnabled:isEnabled];
        // FIXME <rdar://problem/8803392>: If the item is neither a menu nor toolbar item, it will be left enabled.
    }
}

- (void)_doneWithKeyEvent:(NSEvent *)event eventWasHandled:(BOOL)eventWasHandled
{
    if ([event type] != NSKeyDown)
        return;

    if (_data->_impl->tryPostProcessPluginComplexTextInputKeyDown(event))
        return;
    
    if (eventWasHandled) {
        [NSCursor setHiddenUntilMouseMoves:YES];
        return;
    }

    // resending the event may destroy this WKView
    RetainPtr<WKView> protector(self);

    ASSERT(!_data->_keyDownEventBeingResent);
    _data->_keyDownEventBeingResent = event;
    [NSApp _setCurrentEvent:event];
    [NSApp sendEvent:event];

    _data->_keyDownEventBeingResent = nullptr;
}

- (NSRect)_convertToDeviceSpace:(NSRect)rect
{
    return toDeviceSpace(rect, [self window]);
}

- (NSRect)_convertToUserSpace:(NSRect)rect
{
    return toUserSpace(rect, [self window]);
}

- (NSTrackingRectTag)addTrackingRect:(NSRect)rect owner:(id)owner userData:(void *)data assumeInside:(BOOL)assumeInside
{
    return _data->_impl->addTrackingRect(NSRectToCGRect(rect), owner, data, assumeInside);
}

- (NSTrackingRectTag)_addTrackingRect:(NSRect)rect owner:(id)owner userData:(void *)data assumeInside:(BOOL)assumeInside useTrackingNum:(int)tag
{
    return _data->_impl->addTrackingRectWithTrackingNum(NSRectToCGRect(rect), owner, data, assumeInside, tag);
}

- (void)_addTrackingRects:(NSRect *)rects owner:(id)owner userDataList:(void **)userDataList assumeInsideList:(BOOL *)assumeInsideList trackingNums:(NSTrackingRectTag *)trackingNums count:(int)count
{
    CGRect *cgRects = (CGRect *)calloc(1, sizeof(CGRect));
    for (int i = 0; i < count; i++)
        cgRects[i] = NSRectToCGRect(rects[i]);
    _data->_impl->addTrackingRectsWithTrackingNums(cgRects, owner, userDataList, assumeInsideList, trackingNums, count);
    free(cgRects);
}

- (void)removeTrackingRect:(NSTrackingRectTag)tag
{
    if (!_data)
        return;
    _data->_impl->removeTrackingRect(tag);
}

- (void)_removeTrackingRects:(NSTrackingRectTag *)tags count:(int)count
{
    if (!_data)
        return;
    _data->_impl->removeTrackingRects(tags, count);
}

- (NSString *)view:(NSView *)view stringForToolTip:(NSToolTipTag)tag point:(NSPoint)point userData:(void *)data
{
    return _data->_impl->stringForToolTip(tag);
}

- (void)pasteboardChangedOwner:(NSPasteboard *)pasteboard
{
    _data->_impl->pasteboardChangedOwner(pasteboard);
}

- (void)pasteboard:(NSPasteboard *)pasteboard provideDataForType:(NSString *)type
{
    _data->_impl->provideDataForPasteboard(pasteboard, type);
}

- (NSArray *)namesOfPromisedFilesDroppedAtDestination:(NSURL *)dropDestination
{
    return _data->_impl->namesOfPromisedFilesDroppedAtDestination(dropDestination);
}

- (bool)_executeSavedCommandBySelector:(SEL)selector
{
    LOG(TextInput, "Executing previously saved command %s", sel_getName(selector));
    // The sink does two things: 1) Tells us if the responder went unhandled, and
    // 2) prevents any NSBeep; we don't ever want to beep here.
    RetainPtr<WKResponderChainSink> sink = adoptNS([[WKResponderChainSink alloc] initWithResponderChain:self]);
    [super doCommandBySelector:selector];
    [sink detach];
    return ![sink didReceiveUnhandledCommand];
}

- (instancetype)initWithFrame:(NSRect)frame processPool:(WebProcessPool&)processPool configuration:(Ref<API::PageConfiguration>&&)configuration webView:(WKWebView *)webView
{
    self = [super initWithFrame:frame];
    if (!self)
        return nil;

    [NSApp registerServicesMenuSendTypes:PasteboardTypes::forSelection() returnTypes:PasteboardTypes::forEditing()];

    InitializeWebKit2();

    _data = [[WKViewData alloc] init];

    _data->_pageClient = std::make_unique<PageClientImpl>(self, webView);
    _data->_page = processPool.createWebPage(*_data->_pageClient, WTF::move(configuration));

    _data->_impl = std::make_unique<WebViewImpl>(self, *_data->_page, *_data->_pageClient);
    static_cast<PageClientImpl&>(*_data->_pageClient).setImpl(*_data->_impl);

    _data->_page->setAddsVisitedLinks(processPool.historyClient().addsVisitedLinks());

    _data->_page->initializeWebPage();

    _data->_windowOcclusionDetectionEnabled = YES;

    _data->_impl->registerDraggedTypes();

    self.wantsLayer = YES;

    // Explicitly set the layer contents placement so AppKit will make sure that our layer has masksToBounds set to YES.
    self.layerContentsPlacement = NSViewLayerContentsPlacementTopLeft;

    WebProcessPool::statistics().wkViewCount++;

    NSNotificationCenter* workspaceNotificationCenter = [[NSWorkspace sharedWorkspace] notificationCenter];
    [workspaceNotificationCenter addObserver:self selector:@selector(_activeSpaceDidChange:) name:NSWorkspaceActiveSpaceDidChangeNotification object:nil];

    return self;
}

#if WK_API_ENABLED
- (void)_setThumbnailView:(_WKThumbnailView *)thumbnailView
{
    _data->_impl->setThumbnailView(thumbnailView);
}

- (_WKThumbnailView *)_thumbnailView
{
    if (!_data->_impl)
        return nil;
    return _data->_impl->thumbnailView();
}
#endif // WK_API_ENABLED

#if WK_API_ENABLED
- (_WKRemoteObjectRegistry *)_remoteObjectRegistry
{
    if (!_data->_remoteObjectRegistry) {
        _data->_remoteObjectRegistry = adoptNS([[_WKRemoteObjectRegistry alloc] _initWithMessageSender:*_data->_page]);
        _data->_page->process().processPool().addMessageReceiver(Messages::RemoteObjectRegistry::messageReceiverName(), _data->_page->pageID(), [_data->_remoteObjectRegistry remoteObjectRegistry]);
    }

    return _data->_remoteObjectRegistry.get();
}
#endif

#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 101100
- (void)_startWindowDrag
{
    _data->_impl->startWindowDrag();
}
#endif

// FIXME: Get rid of this when we have better plumbing to WKViewLayoutStrategy.
- (void)_updateViewExposedRect
{
    _data->_impl->updateViewExposedRect();
}

@end

@implementation WKView (Private)

- (void)saveBackForwardSnapshotForCurrentItem
{
    _data->_page->recordNavigationSnapshot();
}

- (void)saveBackForwardSnapshotForItem:(WKBackForwardListItemRef)item
{
    _data->_page->recordNavigationSnapshot(*toImpl(item));
}

- (id)initWithFrame:(NSRect)frame contextRef:(WKContextRef)contextRef pageGroupRef:(WKPageGroupRef)pageGroupRef
{
    return [self initWithFrame:frame contextRef:contextRef pageGroupRef:pageGroupRef relatedToPage:nil];
}

- (id)initWithFrame:(NSRect)frame contextRef:(WKContextRef)contextRef pageGroupRef:(WKPageGroupRef)pageGroupRef relatedToPage:(WKPageRef)relatedPage
{
    auto configuration = API::PageConfiguration::create();
    configuration->setProcessPool(toImpl(contextRef));
    configuration->setPageGroup(toImpl(pageGroupRef));
    configuration->setRelatedPage(toImpl(relatedPage));

    return [self initWithFrame:frame processPool:*toImpl(contextRef) configuration:WTF::move(configuration) webView:nil];
}

- (id)initWithFrame:(NSRect)frame configurationRef:(WKPageConfigurationRef)configurationRef
{
    Ref<API::PageConfiguration> configuration = toImpl(configurationRef)->copy();
    auto& processPool = *configuration->processPool();

    return [self initWithFrame:frame processPool:processPool configuration:WTF::move(configuration) webView:nil];
}

- (BOOL)wantsUpdateLayer
{
    return YES;
}

- (void)updateLayer
{
    if ([self drawsBackground] && ![self drawsTransparentBackground])
        self.layer.backgroundColor = CGColorGetConstantColor(kCGColorWhite);
    else
        self.layer.backgroundColor = CGColorGetConstantColor(kCGColorClear);

    // If asynchronous geometry updates have been sent by forceAsyncDrawingAreaSizeUpdate,
    // then subsequent calls to setFrameSize should not result in us waiting for the did
    // udpate response if setFrameSize is called.
    if ([self frameSizeUpdatesDisabled])
        return;

    if (DrawingAreaProxy* drawingArea = _data->_page->drawingArea())
        drawingArea->waitForPossibleGeometryUpdate();
}

- (WKPageRef)pageRef
{
    return toAPI(_data->_page.get());
}

- (BOOL)canChangeFrameLayout:(WKFrameRef)frameRef
{
    // PDF documents are already paginated, so we can't change them to add headers and footers.
    return !toImpl(frameRef)->isDisplayingPDFDocument();
}

- (NSPrintOperation *)printOperationWithPrintInfo:(NSPrintInfo *)printInfo forFrame:(WKFrameRef)frameRef
{
    LOG(Printing, "Creating an NSPrintOperation for frame '%s'", toImpl(frameRef)->url().utf8().data());

    // FIXME: If the frame cannot be printed (e.g. if it contains an encrypted PDF that disallows
    // printing), this function should return nil.
    RetainPtr<WKPrintingView> printingView = adoptNS([[WKPrintingView alloc] initWithFrameProxy:toImpl(frameRef) view:self]);
    // NSPrintOperation takes ownership of the view.
    NSPrintOperation *printOperation = [NSPrintOperation printOperationWithView:printingView.get() printInfo:printInfo];
    [printOperation setCanSpawnSeparateThread:YES];
    [printOperation setJobTitle:toImpl(frameRef)->title()];
    printingView->_printOperation = printOperation;
    return printOperation;
}

- (void)setFrame:(NSRect)rect andScrollBy:(NSSize)offset
{
    _data->_impl->setFrameAndScrollBy(NSRectToCGRect(rect), NSSizeToCGSize(offset));
}

- (void)disableFrameSizeUpdates
{
    _data->_impl->disableFrameSizeUpdates();
}

- (void)enableFrameSizeUpdates
{
    _data->_impl->enableFrameSizeUpdates();
}

- (BOOL)frameSizeUpdatesDisabled
{
    return _data->_impl->frameSizeUpdatesDisabled();
}

+ (void)hideWordDefinitionWindow
{
    DictionaryLookup::hidePopup();
}

- (NSSize)minimumSizeForAutoLayout
{
    return _data->_page->minimumLayoutSize();
}

- (void)setMinimumSizeForAutoLayout:(NSSize)minimumSizeForAutoLayout
{
    BOOL expandsToFit = minimumSizeForAutoLayout.width > 0;

    _data->_page->setMinimumLayoutSize(IntSize(minimumSizeForAutoLayout.width, minimumSizeForAutoLayout.height));
    _data->_page->setMainFrameIsScrollable(!expandsToFit);

    [self setShouldClipToVisibleRect:expandsToFit];
}

- (BOOL)shouldExpandToViewHeightForAutoLayout
{
    return _data->_page->autoSizingShouldExpandToViewHeight();
}

- (void)setShouldExpandToViewHeightForAutoLayout:(BOOL)shouldExpand
{
    return _data->_page->setAutoSizingShouldExpandToViewHeight(shouldExpand);
}

- (BOOL)shouldClipToVisibleRect
{
    return _data->_impl->clipsToVisibleRect();
}

- (void)setShouldClipToVisibleRect:(BOOL)clipsToVisibleRect
{
    _data->_impl->setClipsToVisibleRect(clipsToVisibleRect);
}

- (NSColor *)underlayColor
{
    Color webColor = _data->_page->underlayColor();
    if (!webColor.isValid())
        return nil;

    return nsColor(webColor);
}

- (void)setUnderlayColor:(NSColor *)underlayColor
{
    _data->_page->setUnderlayColor(colorFromNSColor(underlayColor));
}

#if WK_API_ENABLED
- (NSView *)_inspectorAttachmentView
{
    NSView *attachmentView = _data->_inspectorAttachmentView.get();
    return attachmentView ? attachmentView : self;
}

- (void)_setInspectorAttachmentView:(NSView *)newView
{
    NSView *oldView = _data->_inspectorAttachmentView.get();
    if (oldView == newView)
        return;

    _data->_inspectorAttachmentView = newView;
    _data->_page->inspector()->attachmentViewDidChange(oldView ? oldView : self, newView ? newView : self);
}
#endif

- (NSView *)fullScreenPlaceholderView
{
    return _data->_impl->fullScreenPlaceholderView();
}

// FIXME: This returns an autoreleased object. Should it really be prefixed 'create'?
- (NSWindow *)createFullScreenWindow
{
    return _data->_impl->createFullScreenWindow();
}

- (void)beginDeferringViewInWindowChanges
{
    _data->_impl->beginDeferringViewInWindowChanges();
}

- (void)endDeferringViewInWindowChanges
{
    _data->_impl->endDeferringViewInWindowChanges();
}

- (void)endDeferringViewInWindowChangesSync
{
    _data->_impl->endDeferringViewInWindowChangesSync();
}

- (void)_prepareForMoveToWindow:(NSWindow *)targetWindow withCompletionHandler:(void(^)(void))completionHandler
{
    _data->_impl->prepareForMoveToWindow(targetWindow, completionHandler);
}

- (BOOL)isDeferringViewInWindowChanges
{
    return _data->_impl->isDeferringViewInWindowChanges();
}

- (BOOL)windowOcclusionDetectionEnabled
{
    return _data->_windowOcclusionDetectionEnabled;
}

- (void)setWindowOcclusionDetectionEnabled:(BOOL)flag
{
    _data->_windowOcclusionDetectionEnabled = flag;
}

- (void)setAllowsBackForwardNavigationGestures:(BOOL)allowsBackForwardNavigationGestures
{
    _data->_impl->setAllowsBackForwardNavigationGestures(allowsBackForwardNavigationGestures);
}

- (BOOL)allowsBackForwardNavigationGestures
{
    return _data->_impl->allowsBackForwardNavigationGestures();
}

- (BOOL)allowsLinkPreview
{
    return _data->_impl->allowsLinkPreview();
}

- (void)setAllowsLinkPreview:(BOOL)allowsLinkPreview
{
    _data->_impl->setAllowsLinkPreview(allowsLinkPreview);
}

- (void)_setIgnoresAllEvents:(BOOL)ignoresAllEvents
{
    _data->_impl->setIgnoresAllEvents(ignoresAllEvents);
}

// Forward _setIgnoresNonWheelMouseEvents to _setIgnoresNonWheelEvents to avoid breaking existing clients.
- (void)_setIgnoresNonWheelMouseEvents:(BOOL)ignoresNonWheelMouseEvents
{
    _data->_impl->setIgnoresNonWheelEvents(ignoresNonWheelMouseEvents);
}

- (void)_setIgnoresNonWheelEvents:(BOOL)ignoresNonWheelEvents
{
    _data->_impl->setIgnoresNonWheelEvents(ignoresNonWheelEvents);
}

- (BOOL)_ignoresNonWheelEvents
{
    return _data->_impl->ignoresNonWheelEvents();
}

- (BOOL)_ignoresAllEvents
{
    return _data->_impl->ignoresAllEvents();
}

- (void)_setOverrideDeviceScaleFactor:(CGFloat)deviceScaleFactor
{
    _data->_impl->setOverrideDeviceScaleFactor(deviceScaleFactor);
}

- (CGFloat)_overrideDeviceScaleFactor
{
    return _data->_impl->overrideDeviceScaleFactor();
}

- (WKLayoutMode)_layoutMode
{
    return _data->_impl->layoutMode();
}

- (void)_setLayoutMode:(WKLayoutMode)layoutMode
{
    _data->_impl->setLayoutMode(layoutMode);
}

- (CGSize)_fixedLayoutSize
{
    return _data->_impl->fixedLayoutSize();
}

- (void)_setFixedLayoutSize:(CGSize)fixedLayoutSize
{
    _data->_impl->setFixedLayoutSize(fixedLayoutSize);
}

- (CGFloat)_viewScale
{
    return _data->_impl->viewScale();
}

- (void)_setViewScale:(CGFloat)viewScale
{
    _data->_impl->setViewScale(viewScale);
}

- (void)_setTopContentInset:(CGFloat)contentInset
{
    return _data->_impl->setTopContentInset(contentInset);
}

- (CGFloat)_topContentInset
{
    return _data->_impl->topContentInset();
}

- (void)_setTotalHeightOfBanners:(CGFloat)totalHeightOfBanners
{
    _data->_totalHeightOfBanners = totalHeightOfBanners;
}

- (CGFloat)_totalHeightOfBanners
{
    return _data->_totalHeightOfBanners;
}

- (void)_setOverlayScrollbarStyle:(_WKOverlayScrollbarStyle)scrollbarStyle
{
    WTF::Optional<WebCore::ScrollbarOverlayStyle> coreScrollbarStyle;

    switch (scrollbarStyle) {
    case _WKOverlayScrollbarStyleDark:
        coreScrollbarStyle = ScrollbarOverlayStyleDark;
        break;
    case _WKOverlayScrollbarStyleLight:
        coreScrollbarStyle = ScrollbarOverlayStyleLight;
        break;
    case _WKOverlayScrollbarStyleDefault:
        coreScrollbarStyle = ScrollbarOverlayStyleDefault;
        break;
    case _WKOverlayScrollbarStyleAutomatic:
    default:
        break;
    }

    _data->_page->setOverlayScrollbarStyle(coreScrollbarStyle);
}

- (_WKOverlayScrollbarStyle)_overlayScrollbarStyle
{
    WTF::Optional<WebCore::ScrollbarOverlayStyle> coreScrollbarStyle = _data->_page->overlayScrollbarStyle();

    if (!coreScrollbarStyle)
        return _WKOverlayScrollbarStyleAutomatic;

    switch (coreScrollbarStyle.value()) {
    case ScrollbarOverlayStyleDark:
        return _WKOverlayScrollbarStyleDark;
    case ScrollbarOverlayStyleLight:
        return _WKOverlayScrollbarStyleLight;
    case ScrollbarOverlayStyleDefault:
        return _WKOverlayScrollbarStyleDefault;
    default:
        return _WKOverlayScrollbarStyleAutomatic;
    }
}

- (NSColor *)_pageExtendedBackgroundColor
{
    WebCore::Color color = _data->_page->pageExtendedBackgroundColor();
    if (!color.isValid())
        return nil;

    return nsColor(color);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
// This method forces a drawing area geometry update, even if frame size updates are disabled.
// The updated is performed asynchronously; we don't wait for the geometry update before returning.
// The area drawn need not match the current frame size - if it differs it will be anchored to the
// frame according to the current contentAnchor.
- (void)forceAsyncDrawingAreaSizeUpdate:(NSSize)size
{
    // This SPI is only used on 10.9 and below, and is incompatible with the fence-based drawing area size synchronization in 10.10+.
#if __MAC_OS_X_VERSION_MIN_REQUIRED <= 1090
    if (_data->_impl->clipsToVisibleRect())
        _data->_impl->updateViewExposedRect();
    _data->_impl->setDrawingAreaSize(NSSizeToCGSize(size));

    // If a geometry update is pending the new update won't be sent. Poll without waiting for any
    // pending did-update message now, such that the new update can be sent. We do so after setting
    // the drawing area size such that the latest update is sent.
    if (DrawingAreaProxy* drawingArea = _data->_page->drawingArea())
        drawingArea->waitForPossibleGeometryUpdate(std::chrono::milliseconds::zero());
#else
    ASSERT_NOT_REACHED();
#endif
}

- (void)waitForAsyncDrawingAreaSizeUpdate
{
    // This SPI is only used on 10.9 and below, and is incompatible with the fence-based drawing area size synchronization in 10.10+.
#if __MAC_OS_X_VERSION_MIN_REQUIRED <= 1090
    if (DrawingAreaProxy* drawingArea = _data->_page->drawingArea()) {
        // If a geometry update is still pending then the action of receiving the
        // first geometry update may result in another update being scheduled -
        // we should wait for this to complete too.
        drawingArea->waitForPossibleGeometryUpdate(DrawingAreaProxy::didUpdateBackingStoreStateTimeout() / 2);
        drawingArea->waitForPossibleGeometryUpdate(DrawingAreaProxy::didUpdateBackingStoreStateTimeout() / 2);
    }
#else
    ASSERT_NOT_REACHED();
#endif
}
#pragma clang diagnostic pop

- (BOOL)isUsingUISideCompositing
{
    if (DrawingAreaProxy* drawingArea = _data->_page->drawingArea())
        return drawingArea->type() == DrawingAreaTypeRemoteLayerTree;

    return NO;
}

- (void)setAllowsMagnification:(BOOL)allowsMagnification
{
    _data->_impl->setAllowsMagnification(allowsMagnification);
}

- (BOOL)allowsMagnification
{
    return _data->_impl->allowsMagnification();
}

- (void)magnifyWithEvent:(NSEvent *)event
{
    _data->_impl->magnifyWithEvent(event);
}

#if ENABLE(MAC_GESTURE_EVENTS)
- (void)rotateWithEvent:(NSEvent *)event
{
    _data->_impl->rotateWithEvent(event);
}
#endif

- (void)_gestureEventWasNotHandledByWebCore:(NSEvent *)event
{
    _data->_impl->gestureEventWasNotHandledByWebCoreFromViewOnly(event);
}

- (void)smartMagnifyWithEvent:(NSEvent *)event
{
    _data->_impl->smartMagnifyWithEvent(event);
}

- (void)setMagnification:(double)magnification centeredAtPoint:(NSPoint)point
{
    _data->_impl->setMagnification(magnification, NSPointToCGPoint(point));
}

- (void)setMagnification:(double)magnification
{
    _data->_impl->setMagnification(magnification);
}

- (double)magnification
{
    return _data->_impl->magnification();
}

- (void)_setCustomSwipeViews:(NSArray *)customSwipeViews
{
    _data->_impl->setCustomSwipeViews(customSwipeViews);
}

- (void)_setCustomSwipeViewsTopContentInset:(float)topContentInset
{
    _data->_impl->setCustomSwipeViewsTopContentInset(topContentInset);
}

- (BOOL)_tryToSwipeWithEvent:(NSEvent *)event ignoringPinnedState:(BOOL)ignoringPinnedState
{
    return _data->_impl->tryToSwipeWithEvent(event, ignoringPinnedState);
}

- (void)_setDidMoveSwipeSnapshotCallback:(void(^)(CGRect))callback
{
    _data->_impl->setDidMoveSwipeSnapshotCallback(callback);
}

- (id)_immediateActionAnimationControllerForHitTestResult:(WKHitTestResultRef)hitTestResult withType:(uint32_t)type userData:(WKTypeRef)userData
{
    return nil;
}

- (void)_prepareForImmediateActionAnimation
{
}

- (void)_cancelImmediateActionAnimation
{
}

- (void)_completeImmediateActionAnimation
{
}

- (void)_didChangeContentSize:(NSSize)newSize
{
}

- (void)_dismissContentRelativeChildWindows
{
    _data->_impl->dismissContentRelativeChildWindowsFromViewOnly();
}

- (void)_dismissContentRelativeChildWindowsWithAnimation:(BOOL)withAnimation
{
    _data->_impl->dismissContentRelativeChildWindowsWithAnimationFromViewOnly(withAnimation);
}

#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 101000

- (void)_setAutomaticallyAdjustsContentInsets:(BOOL)automaticallyAdjustsContentInsets
{
    _data->_impl->setAutomaticallyAdjustsContentInsets(automaticallyAdjustsContentInsets);
}

- (BOOL)_automaticallyAdjustsContentInsets
{
    return _data->_impl->automaticallyAdjustsContentInsets();
}

#endif

@end

@implementation WKResponderChainSink

- (id)initWithResponderChain:(NSResponder *)chain
{
    self = [super init];
    if (!self)
        return nil;
    _lastResponderInChain = chain;
    while (NSResponder *next = [_lastResponderInChain nextResponder])
        _lastResponderInChain = next;
    [_lastResponderInChain setNextResponder:self];
    return self;
}

- (void)detach
{
    // This assumes that the responder chain was either unmodified since
    // -initWithResponderChain: was called, or was modified in such a way
    // that _lastResponderInChain is still in the chain, and self was not
    // moved earlier in the chain than _lastResponderInChain.
    NSResponder *responderBeforeSelf = _lastResponderInChain;    
    NSResponder *next = [responderBeforeSelf nextResponder];
    for (; next && next != self; next = [next nextResponder])
        responderBeforeSelf = next;
    
    // Nothing to be done if we are no longer in the responder chain.
    if (next != self)
        return;
    
    [responderBeforeSelf setNextResponder:[self nextResponder]];
    _lastResponderInChain = nil;
}

- (bool)didReceiveUnhandledCommand
{
    return _didReceiveUnhandledCommand;
}

- (void)noResponderFor:(SEL)selector
{
    _didReceiveUnhandledCommand = true;
}

- (void)doCommandBySelector:(SEL)selector
{
    _didReceiveUnhandledCommand = true;
}

- (BOOL)tryToPerform:(SEL)action with:(id)object
{
    _didReceiveUnhandledCommand = true;
    return YES;
}

@end

#endif // PLATFORM(MAC)
