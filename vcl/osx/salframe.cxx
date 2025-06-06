/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <IconThemeSelector.hxx>
#include <string>

#include <comphelper/fileurl.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <tools/long.hxx>
#include <o3tl/safeint.hxx>
#include <osl/diagnose.h>

#include <osl/file.h>

#include <vcl/event.hxx>
#include <vcl/inputctx.hxx>
#include <vcl/svapp.hxx>
#include <vcl/window.hxx>
#include <vcl/syswin.hxx>
#include <vcl/settings.hxx>
#include <vcl/themecolors.hxx>

#include <osx/saldata.hxx>
#include <quartz/salgdi.h>
#include <osx/salframe.h>
#include <osx/salmenu.h>
#include <osx/salinst.h>
#include <osx/salframeview.h>
#include <osx/a11yfactory.h>
#include <osx/runinmain.hxx>
#include <quartz/utils.h>

#include <salwtype.hxx>

#include <premac.h>
#include <objc/objc-runtime.h>
// needed for theming
// FIXME: move theming code to salnativewidgets.cxx
#include <Carbon/Carbon.h>
#include <quartz/CGHelpers.hxx>
#include <postmac.h>

const int nMinBlinkCursorDelay = 500;

AquaSalFrame* AquaSalFrame::s_pCaptureFrame = nullptr;

AquaSalFrame::AquaSalFrame( SalFrame* pParent, SalFrameStyleFlags salFrameStyle ) :
    mpNSWindow(nil),
    mpNSView(nil),
    mpDockMenuEntry(nil),
    mpGraphics(nullptr),
    mpParent(nullptr),
    mnMinWidth(0),
    mnMinHeight(0),
    mnMaxWidth(0),
    mnMaxHeight(0),
    mbGraphicsAcquired(false),
    mbShown(false),
    mbInitShow(true),
    mbPositioned(false),
    mbSized(false),
    mbPresentation( false ),
    mnStyle( salFrameStyle ),
    mnStyleMask( 0 ),
    mnLastEventTime( 0 ),
    mnLastModifierFlags( 0 ),
    mpMenu( nullptr ),
    mnExtStyle( 0 ),
    mePointerStyle( PointerStyle::Arrow ),
    mrClippingPath( nullptr ),
    mnICOptions( InputContextFlags::NONE ),
    mnBlinkCursorDelay( nMinBlinkCursorDelay ),
    mbForceFlushScrolling( false ),
    mbForceFlushProgressBar( false ),
    mbInternalFullScreen( false ),
    maInternalFullScreenRestoreRect( NSZeroRect ),
    maInternalFullScreenExpectedRect( NSZeroRect ),
    mbNativeFullScreen( false ),
    maNativeFullScreenRestoreRect( NSZeroRect )
{
    mpParent = dynamic_cast<AquaSalFrame*>(pParent);

    initWindowAndView();

    SalData* pSalData = GetSalData();
    pSalData->mpInstance->insertFrame( this );
    NSUserDefaults *userDefaults = [NSUserDefaults standardUserDefaults];

    // tdf#150177 Limit minimum blink cursor rate
    // This bug occurs when the values for NSTextInsertionPointBlinkPeriodOn or
    // NSTextInsertionPointBlinkPeriodOff are set to zero or close to zero.
    // LibreOffice becomes very sluggish opening documents when either is set
    // at 100 milliseconds or less so set the blink rate to the maximum of
    // nMinBlinkCursorDelay, NSTextInsertionPointBlinkPeriodOn, and
    // NSTextInsertionPointBlinkPeriodOff.
    mnBlinkCursorDelay = nMinBlinkCursorDelay;
    if (userDefaults != nil)
    {
        id setting = [userDefaults objectForKey: @"NSTextInsertionPointBlinkPeriodOn"];
        if (setting && [setting isKindOfClass:[NSNumber class]])
            mnBlinkCursorDelay = std::max(mnBlinkCursorDelay, [setting intValue]);

        setting = [userDefaults objectForKey: @"NSTextInsertionPointBlinkPeriodOff"];
        if (setting && [setting isKindOfClass:[NSNumber class]])
            mnBlinkCursorDelay = std::max(mnBlinkCursorDelay, [setting intValue]);
    }
}

AquaSalFrame::~AquaSalFrame()
{
    if (mbInternalFullScreen)
        doShowFullScreen(false, maGeometry.screen());

    assert( GetSalData()->mpInstance->IsMainThread() );

    // if the frame is destroyed and has the current menubar
    // set the default menubar
    if( mpMenu && mpMenu->mbMenuBar && AquaSalMenu::pCurrentMenuBar == mpMenu )
        AquaSalMenu::setDefaultMenu();

    // cleanup clipping stuff
    doResetClipRegion();

    [SalFrameView unsetMouseFrame: this];

    SalData* pSalData = GetSalData();
    pSalData->mpInstance->eraseFrame( this );
    pSalData->maPresentationFrames.remove( this );

    SAL_WARN_IF( this == s_pCaptureFrame, "vcl", "capture frame destroyed" );
    if( this == s_pCaptureFrame )
        s_pCaptureFrame = nullptr;

    delete mpGraphics;

    if( mpDockMenuEntry )
    {
        NSMenu* pDock = AquaSalInstance::GetDynamicDockMenu();
        // life cycle comment: the menu has ownership of the item, so no release
        [pDock removeItem: mpDockMenuEntry];
        if ([pDock numberOfItems] != 0
            && [[pDock itemAtIndex: 0] isSeparatorItem])
        {
            [pDock removeItemAtIndex: 0];
        }
    }
    if ( mpNSView ) {
        if ([mpNSView isKindOfClass:[SalFrameView class]])
            [static_cast<SalFrameView*>(mpNSView) revokeWrapper];
        [mpNSView release];
    }
    if ( mpNSWindow )
        [mpNSWindow release];
}

void AquaSalFrame::initWindowAndView()
{
    OSX_SALDATA_RUNINMAIN( initWindowAndView() )

    // initialize mirroring parameters
    // FIXME: screens changing
    NSScreen* pNSScreen = [mpNSWindow screen];
    if( pNSScreen == nil )
        pNSScreen = [NSScreen mainScreen];
    maScreenRect = [pNSScreen frame];

    // calculate some default geometry
    NSRect aVisibleRect = [pNSScreen visibleFrame];
    CocoaToVCL( aVisibleRect );

    maGeometry.setX(static_cast<sal_Int32>(aVisibleRect.origin.x + aVisibleRect.size.width / 10));
    maGeometry.setY(static_cast<sal_Int32>(aVisibleRect.origin.y + aVisibleRect.size.height / 10));
    maGeometry.setWidth(static_cast<sal_uInt32>(aVisibleRect.size.width * 0.8));
    maGeometry.setHeight(static_cast<sal_uInt32>(aVisibleRect.size.height * 0.8));

    // calculate style mask
    if( (mnStyle & SalFrameStyleFlags::FLOAT) ||
        (mnStyle & SalFrameStyleFlags::OWNERDRAWDECORATION) )
        mnStyleMask = NSWindowStyleMaskBorderless;
    else if( mnStyle & SalFrameStyleFlags::DEFAULT )
    {
        mnStyleMask = NSWindowStyleMaskTitled         |
                      NSWindowStyleMaskMiniaturizable |
                      NSWindowStyleMaskResizable      |
                      NSWindowStyleMaskClosable;
        // make default window "maximized"
        maGeometry.setX(static_cast<sal_Int32>(aVisibleRect.origin.x));
        maGeometry.setY(static_cast<sal_Int32>(aVisibleRect.origin.y));
        maGeometry.setWidth(static_cast<sal_uInt32>(aVisibleRect.size.width));
        maGeometry.setHeight(static_cast<sal_uInt32>(aVisibleRect.size.height));
        mbPositioned = mbSized = true;
    }
    else
    {
        if( mnStyle & SalFrameStyleFlags::MOVEABLE )
        {
            mnStyleMask |= NSWindowStyleMaskTitled;
            if( mpParent == nullptr )
                mnStyleMask |= NSWindowStyleMaskMiniaturizable;
        }
        if( mnStyle & SalFrameStyleFlags::SIZEABLE )
            mnStyleMask |= NSWindowStyleMaskResizable;
        if( mnStyle & SalFrameStyleFlags::CLOSEABLE )
            mnStyleMask |= NSWindowStyleMaskClosable;
        // documentation says anything other than NSWindowStyleMaskBorderless (=0)
        // should also include NSWindowStyleMaskTitled;
        if( mnStyleMask != 0 )
            mnStyleMask |= NSWindowStyleMaskTitled;
    }

    if (Application::IsBitmapRendering())
        return;

    // #i91990# support GUI-less (daemon) execution
    @try
    {
        mpNSWindow = [[SalFrameWindow alloc] initWithSalFrame: this];
        mpNSView = [[SalFrameView alloc] initWithSalFrame: this];
    }
    @catch ( id )
    {
        std::abort();
    }

    if( mnStyle & SalFrameStyleFlags::TOOLTIP )
        [mpNSWindow setIgnoresMouseEvents: YES];
    else
        // Related: tdf#155092 mouse events are now handled by tracking areas
        [mpNSWindow setAcceptsMouseMovedEvents: NO];
    [mpNSWindow setHasShadow: YES];

    [mpNSWindow setDelegate: static_cast<id<NSWindowDelegate> >(mpNSWindow)];

    [mpNSWindow setRestorable:NO];

    maSysData.mpNSView = mpNSView;

    UpdateFrameGeometry();

    [mpNSWindow setContentView: mpNSView];
}

void AquaSalFrame::CocoaToVCL( NSRect& io_rRect, bool bRelativeToScreen )
{
    if( bRelativeToScreen )
        io_rRect.origin.y = maScreenRect.size.height - (io_rRect.origin.y+io_rRect.size.height);
    else
        io_rRect.origin.y = maGeometry.height() - (io_rRect.origin.y+io_rRect.size.height);
}

void AquaSalFrame::VCLToCocoa( NSRect& io_rRect, bool bRelativeToScreen )
{
    if( bRelativeToScreen )
        io_rRect.origin.y = maScreenRect.size.height - (io_rRect.origin.y+io_rRect.size.height);
    else
        io_rRect.origin.y = maGeometry.height() - (io_rRect.origin.y+io_rRect.size.height);
}

void AquaSalFrame::CocoaToVCL( NSPoint& io_rPoint, bool bRelativeToScreen )
{
    if( bRelativeToScreen )
        io_rPoint.y = maScreenRect.size.height - io_rPoint.y;
    else
        io_rPoint.y = maGeometry.height() - io_rPoint.y;
}

void AquaSalFrame::VCLToCocoa( NSPoint& io_rPoint, bool bRelativeToScreen )
{
    if( bRelativeToScreen )
        io_rPoint.y = maScreenRect.size.height - io_rPoint.y;
    else
        io_rPoint.y = maGeometry.height() - io_rPoint.y;
}

void AquaSalFrame::screenParametersChanged()
{
    OSX_SALDATA_RUNINMAIN( screenParametersChanged() )

    sal::aqua::resetTotalScreenBounds();
    sal::aqua::resetWindowScaling();

    UpdateFrameGeometry();

    if( mpGraphics )
        mpGraphics->updateResolution();

    if (!mbGeometryDidChange)
        return;

    CallCallback( SalEvent::DisplayChanged, nullptr );
}

SalGraphics* AquaSalFrame::AcquireGraphics()
{
    if ( mbGraphicsAcquired )
        return nullptr;

    if ( !mpGraphics )
    {
        mpGraphics = new AquaSalGraphics;
        mpGraphics->SetWindowGraphics( this );
    }

    mbGraphicsAcquired = true;
    return mpGraphics;
}

void AquaSalFrame::ReleaseGraphics( SalGraphics *pGraphics )
{
    SAL_WARN_IF( pGraphics != mpGraphics, "vcl", "graphics released on wrong frame" );
    mbGraphicsAcquired = false;
}

bool AquaSalFrame::PostEvent(std::unique_ptr<ImplSVEvent> pData)
{
    GetSalData()->mpInstance->PostEvent( this, pData.release(), SalEvent::UserEvent );
    return true;
}

void AquaSalFrame::SetTitle(const OUString& rTitle)
{
    if ( !mpNSWindow )
        return;

    OSX_SALDATA_RUNINMAIN( SetTitle(rTitle) )

    // #i113170# may not be the main thread if called from UNO API
    SalData::ensureThreadAutoreleasePool();

    NSString* pTitle = CreateNSString( rTitle );
    [mpNSWindow setTitle: pTitle];

    // create an entry in the dock menu
    const SalFrameStyleFlags nAppWindowStyle = SalFrameStyleFlags::CLOSEABLE | SalFrameStyleFlags::MOVEABLE;
    if( mpParent == nullptr &&
        (mnStyle & nAppWindowStyle) == nAppWindowStyle )
    {
        if( mpDockMenuEntry == nullptr )
        {
            NSMenu* pDock = AquaSalInstance::GetDynamicDockMenu();

            if ([pDock numberOfItems] != 0) {
                NSMenuItem* pTopItem = [pDock itemAtIndex: 0];
                if ( [pTopItem hasSubmenu] )
                    [pDock insertItem: [NSMenuItem separatorItem] atIndex: 0];
            }

            mpDockMenuEntry = [pDock insertItemWithTitle: pTitle
                                     action: @selector(dockMenuItemTriggered:)
                                     keyEquivalent: @""
                                     atIndex: 0];
            [mpDockMenuEntry setTarget: mpNSWindow];

            // TODO: image (either the generic window image or an icon
            // check mark (for "main" window ?)
        }
        else
            [mpDockMenuEntry setTitle: pTitle];
    }

    if (pTitle)
        [pTitle release];
}

void AquaSalFrame::SetIcon( sal_uInt16 )
{
}

void AquaSalFrame::SetRepresentedURL( const OUString& i_rDocURL )
{
    OSX_SALDATA_RUNINMAIN( SetRepresentedURL( i_rDocURL ) )

    if( comphelper::isFileUrl(i_rDocURL) )
    {
        OUString aSysPath;
        osl_getSystemPathFromFileURL( i_rDocURL.pData, &aSysPath.pData );
        NSString* pStr = CreateNSString( aSysPath );
        if( pStr )
        {
            [pStr autorelease];
            [mpNSWindow setRepresentedFilename: pStr];
        }
    }
}

void AquaSalFrame::initShow()
{
    OSX_SALDATA_RUNINMAIN( initShow() )

    mbInitShow = false;
    if( ! mbPositioned && ! mbInternalFullScreen )
    {
        AbsoluteScreenPixelRectangle aScreenRect;
        GetWorkArea( aScreenRect );
        if( mpParent ) // center relative to parent
        {
            // center on parent
            tools::Long nNewX = mpParent->maGeometry.x() + (static_cast<tools::Long>(mpParent->maGeometry.width()) - static_cast<tools::Long>(maGeometry.width())) / 2;
            if( nNewX < aScreenRect.Left() )
                nNewX = aScreenRect.Left();
            if (static_cast<tools::Long>(nNewX + maGeometry.width()) > aScreenRect.Right())
                nNewX = aScreenRect.Right() - maGeometry.width() - 1;
            tools::Long nNewY = mpParent->maGeometry.y() + (static_cast<tools::Long>(mpParent->maGeometry.height()) - static_cast<tools::Long>(maGeometry.height())) / 2;
            if( nNewY < aScreenRect.Top() )
                nNewY = aScreenRect.Top();
            if( nNewY > aScreenRect.Bottom() )
                nNewY = aScreenRect.Bottom() - maGeometry.height() - 1;
            SetPosSize( nNewX - mpParent->maGeometry.x(),
                        nNewY - mpParent->maGeometry.y(),
                        0, 0,  SAL_FRAME_POSSIZE_X | SAL_FRAME_POSSIZE_Y );
        }
        else if( ! (mnStyle & SalFrameStyleFlags::SIZEABLE) )
        {
            // center on screen
            tools::Long nNewX = (aScreenRect.GetWidth() - maGeometry.width()) / 2;
            tools::Long nNewY = (aScreenRect.GetHeight() - maGeometry.height()) / 2;
            SetPosSize( nNewX, nNewY, 0, 0,  SAL_FRAME_POSSIZE_X | SAL_FRAME_POSSIZE_Y );
        }
    }

    // make sure the view is present in the wrapper list before any children receive focus
    if (mpNSView && [mpNSView isKindOfClass:[SalFrameView class]])
        [static_cast<SalFrameView*>(mpNSView) registerWrapper];
}

void AquaSalFrame::SendPaintEvent( const tools::Rectangle* pRect )
{
    OSX_SALDATA_RUNINMAIN( SendPaintEvent( pRect ) )

    SalPaintEvent aPaintEvt(0, 0, maGeometry.width(), maGeometry.height(), true);
    if( pRect )
    {
        aPaintEvt.mnBoundX      = pRect->Left();
        aPaintEvt.mnBoundY      = pRect->Top();
        aPaintEvt.mnBoundWidth  = pRect->GetWidth();
        aPaintEvt.mnBoundHeight = pRect->GetHeight();
    }

    CallCallback(SalEvent::Paint, &aPaintEvt);
}

void AquaSalFrame::Show(bool bVisible, bool bNoActivate)
{
    if ( !mpNSWindow )
        return;

    OSX_SALDATA_RUNINMAIN( Show(bVisible, bNoActivate) )

    // tdf#152173 Don't display tooltip windows when application is inactive
    // Starting with macOS 13 Ventura, inactive applications receive mouse
    // move events so when LibreOffice is inactive, a mouse move event causes
    // a tooltip to be displayed. Since the tooltip window is attached to its
    // parent window (to ensure that the tooltip is above the parent window),
    // displaying a tooltip pulls the parent window in front of the windows
    // of all other inactive applications.
    // Also, don't display tooltips when mousing over non-key windows even if
    // the application is active as the tooltip window will pull the non-key
    // window in front of the key window.
    if (bVisible && (mnStyle & SalFrameStyleFlags::TOOLTIP))
    {
        if (![NSApp isActive])
            return;

        if (mpParent)
        {
            // tdf#157565 show tooltip if any parent window is the key window
            bool bKeyWindowFound = false;
            NSWindow *pParent = mpParent->mpNSWindow;
            while (pParent)
            {
                if ([pParent isKeyWindow])
                {
                    bKeyWindowFound = true;
                    break;
                }
                pParent = [pParent parentWindow];
            }
            if (!bKeyWindowFound)
                return;
        }
    }

    mbShown = bVisible;
    if(bVisible)
    {
        if( mbInitShow )
            initShow();

        CallCallback(SalEvent::Resize, nullptr);
        // trigger filling our backbuffer
        SendPaintEvent();

        if( bNoActivate || [mpNSWindow canBecomeKeyWindow] == NO )
            [mpNSWindow orderFront: NSApp];
        else
            [mpNSWindow makeKeyAndOrderFront: NSApp];

        if( mpParent )
        {
            /* #i92674# #i96433# we do not want an invisible parent to show up (which adding a visible
               child implicitly does). However we also do not want a parentless toolbar.

               HACK: try to decide when we should not insert a child to its parent
               floaters and ownerdraw windows have not yet shown up in cases where
               we don't want the parent to become visible
            */
            if( mpParent->mbShown || (mnStyle & (SalFrameStyleFlags::OWNERDRAWDECORATION | SalFrameStyleFlags::FLOAT) ) )
            {
                [mpParent->mpNSWindow addChildWindow: mpNSWindow ordered: NSWindowAbove];
            }
        }

        if( mbPresentation )
            [mpNSWindow makeMainWindow];
    }
    else
    {
        // if the frame holding the current menubar gets hidden
        // show the default menubar
        if( mpMenu && mpMenu->mbMenuBar && AquaSalMenu::pCurrentMenuBar == mpMenu )
            AquaSalMenu::setDefaultMenu();

        // #i90440# #i94443# work around the focus going back to some other window
        // if a child gets hidden for a parent window
        if( mpParent && mpParent->mbShown && [mpNSWindow isKeyWindow] )
            [mpParent->mpNSWindow makeKeyAndOrderFront: NSApp];

        [SalFrameView unsetMouseFrame: this];
        if( mpParent && [mpNSWindow parentWindow] == mpParent->mpNSWindow )
            [mpParent->mpNSWindow removeChildWindow: mpNSWindow];

        // Related: tdf#161623 close windows, don't order them out
        // Ordering out a native full screen window would leave the
        // application in a state where there is no Desktop and both
        // the menubar and the Dock are hidden.
        [mpNSWindow close];

        // Related: tdf#165448 move parent window to front after closing window
        // When a floating window such as the dropdown list in the
        // font combobox is open when selecting any of the menu items
        // inserted by macOS in the windows menu, the parent window
        // will be hidden. So if there is a key window, force the key
        // window back to the front.
        // Previously, delaying closing of the window was used to avoid
        // this bug, but that caused Skia/Metal to crash when the
        // window was released before the delayed close occurred. This
        // crash was found when rapidly dragging the border between two
        // column headings side to side in a Calc document.
        NSWindow *pKeyWin = [NSApp keyWindow];
        if( pKeyWin )
            [pKeyWin orderFront: NSApp];
    }
}

void AquaSalFrame::SetMinClientSize( tools::Long nWidth, tools::Long nHeight )
{
    OSX_SALDATA_RUNINMAIN( SetMinClientSize( nWidth, nHeight ) )

    mnMinWidth = nWidth;
    mnMinHeight = nHeight;

    if( mpNSWindow )
    {
        // Always add the decoration as the dimension concerns only
        // the content rectangle
        nWidth += maGeometry.leftDecoration() + maGeometry.rightDecoration();
        nHeight += maGeometry.topDecoration() + maGeometry.bottomDecoration();

        NSSize aSize = { static_cast<CGFloat>(nWidth), static_cast<CGFloat>(nHeight) };

        // Size of full window (content+structure) although we only
        // have the client size in arguments
        [mpNSWindow setMinSize: aSize];
    }
}

void AquaSalFrame::SetMaxClientSize( tools::Long nWidth, tools::Long nHeight )
{
    OSX_SALDATA_RUNINMAIN( SetMaxClientSize( nWidth, nHeight ) )

    mnMaxWidth = nWidth;
    mnMaxHeight = nHeight;

    if( mpNSWindow )
    {
        // Always add the decoration as the dimension concerns only
        // the content rectangle
        nWidth += maGeometry.leftDecoration() + maGeometry.rightDecoration();
        nHeight += maGeometry.topDecoration() + maGeometry.bottomDecoration();

        // Carbon windows can't have a size greater than 32767x32767
        if (nWidth>32767) nWidth=32767;
        if (nHeight>32767) nHeight=32767;

        NSSize aSize = { static_cast<CGFloat>(nWidth), static_cast<CGFloat>(nHeight) };

        // Size of full window (content+structure) although we only
        // have the client size in arguments
        [mpNSWindow setMaxSize: aSize];
    }
}

void AquaSalFrame::GetClientSize( tools::Long& rWidth, tools::Long& rHeight )
{
    if (mbShown || mbInitShow || Application::IsBitmapRendering())
    {
        rWidth = maGeometry.width();
        rHeight = maGeometry.height();
    }
    else
    {
        rWidth  = 0;
        rHeight = 0;
    }
}

SalEvent AquaSalFrame::PreparePosSize(tools::Long nX, tools::Long nY, tools::Long nWidth, tools::Long nHeight, sal_uInt16 nFlags)
{
    SalEvent nEvent = SalEvent::NONE;
    assert(mpNSWindow || Application::IsBitmapRendering());

    if (nFlags & (SAL_FRAME_POSSIZE_X | SAL_FRAME_POSSIZE_Y))
    {
        mbPositioned = true;
        nEvent = SalEvent::Move;
    }

    if (nFlags & (SAL_FRAME_POSSIZE_WIDTH | SAL_FRAME_POSSIZE_HEIGHT))
    {
        mbSized = true;
        nEvent = (nEvent == SalEvent::Move) ? SalEvent::MoveResize : SalEvent::Resize;
    }

    if (Application::IsBitmapRendering())
    {
        if (nFlags & SAL_FRAME_POSSIZE_X)
            maGeometry.setX(nX);
        if (nFlags & SAL_FRAME_POSSIZE_Y)
            maGeometry.setY(nY);
        if (nFlags & SAL_FRAME_POSSIZE_WIDTH)
        {
            maGeometry.setWidth(nWidth);
            if (mnMaxWidth > 0 && maGeometry.width() > mnMaxWidth)
                maGeometry.setWidth(mnMaxWidth);
            if (mnMinWidth > 0 && maGeometry.width() < mnMinWidth)
                maGeometry.setWidth(mnMinWidth);
        }
        if (nFlags & SAL_FRAME_POSSIZE_HEIGHT)
        {
            maGeometry.setHeight(nHeight);
            if (mnMaxHeight > 0 && maGeometry.height() > mnMaxHeight)
                maGeometry.setHeight(mnMaxHeight);
            if (mnMinHeight > 0 && maGeometry.height() < mnMinHeight)
                maGeometry.setHeight(mnMinHeight);
        }
        if (nEvent != SalEvent::NONE)
            CallCallback(nEvent, nullptr);
    }

    return nEvent;
}

void AquaSalFrame::SetWindowState(const vcl::WindowData* pState)
{
    if (!mpNSWindow && !Application::IsBitmapRendering())
        return;

    OSX_SALDATA_RUNINMAIN( SetWindowState( pState ) )

    sal_uInt16 nFlags = 0;
    nFlags |= ((pState->mask() & vcl::WindowDataMask::X) ? SAL_FRAME_POSSIZE_X : 0);
    nFlags |= ((pState->mask() & vcl::WindowDataMask::Y) ? SAL_FRAME_POSSIZE_Y : 0);
    nFlags |= ((pState->mask() & vcl::WindowDataMask::Width) ? SAL_FRAME_POSSIZE_WIDTH : 0);
    nFlags |= ((pState->mask() & vcl::WindowDataMask::Height) ? SAL_FRAME_POSSIZE_HEIGHT : 0);

    SalEvent nEvent = PreparePosSize(pState->x(), pState->y(), pState->width(), pState->height(), nFlags);
    if (Application::IsBitmapRendering())
        return;

    // set normal state
    NSRect aStateRect = [mpNSWindow frame];
    aStateRect = [mpNSWindow contentRectForFrameRect: aStateRect];
    CocoaToVCL(aStateRect);
    if (pState->mask() & vcl::WindowDataMask::X)
        aStateRect.origin.x = float(pState->x());
    if (pState->mask() & vcl::WindowDataMask::Y)
        aStateRect.origin.y = float(pState->y());
    if (pState->mask() & vcl::WindowDataMask::Width)
        aStateRect.size.width = float(pState->width());
    if (pState->mask() & vcl::WindowDataMask::Height)
        aStateRect.size.height = float(pState->height());
    VCLToCocoa(aStateRect);

    // Related: tdf#128186 don't change size of native full screen windows
    if ([mpNSWindow styleMask] & NSWindowStyleMaskFullScreen)
    {
        maNativeFullScreenRestoreRect = aStateRect;
        return;
    }

    aStateRect = [mpNSWindow frameRectForContentRect: aStateRect];
    [mpNSWindow setFrame: aStateRect display: NO];

    if (pState->state() == vcl::WindowState::Minimized)
        [mpNSWindow miniaturize: NSApp];
    else if ([mpNSWindow isMiniaturized])
        [mpNSWindow deminiaturize: NSApp];

    /* ZOOMED is not really maximized (actually it toggles between a user set size and
       the program specified one), but comes closest since the default behavior is
       "maximized" if the user did not intervene
     */
    if (pState->state() == vcl::WindowState::Maximized)
    {
        if (![mpNSWindow isZoomed])
            [mpNSWindow zoom: NSApp];
    }
    else
    {
        if ([mpNSWindow isZoomed])
            [mpNSWindow zoom: NSApp];
    }

    // get new geometry
    UpdateFrameGeometry();

    // send event that we were moved/sized
    if( nEvent != SalEvent::NONE )
        CallCallback( nEvent, nullptr );

    if (mbShown)
    {
        // trigger filling our backbuffer
        SendPaintEvent();

        // tell the system the views need to be updated
        [mpNSWindow display];
    }
}

bool AquaSalFrame::GetWindowState(vcl::WindowData* pState)
{
    if (!mpNSWindow)
    {
        if (Application::IsBitmapRendering())
        {
            pState->setMask(vcl::WindowDataMask::PosSizeState);
            pState->setPosSize(maGeometry.posSize());
            pState->setState(vcl::WindowState::Normal);
            return true;
        }
        return false;
    }

    OSX_SALDATA_RUNINMAIN_UNION( GetWindowState( pState ), boolean )

    pState->setMask(vcl::WindowDataMask::PosSizeState);

    NSRect aStateRect = [mpNSWindow frame];
    aStateRect = [mpNSWindow contentRectForFrameRect: aStateRect];
    CocoaToVCL( aStateRect );

    if( mbInternalFullScreen && !NSIsEmptyRect( maInternalFullScreenRestoreRect ) )
    {
        pState->setX(maInternalFullScreenRestoreRect.origin.x);
        pState->setY(maInternalFullScreenRestoreRect.origin.y);
        pState->setWidth(maInternalFullScreenRestoreRect.size.width);
        pState->setHeight(maInternalFullScreenRestoreRect.size.height);
        pState->SetMaximizedX(static_cast<sal_Int32>(aStateRect.origin.x));
        pState->SetMaximizedY(static_cast<sal_Int32>(aStateRect.origin.x));
        pState->SetMaximizedWidth(static_cast<sal_uInt32>(aStateRect.size.width));
        pState->SetMaximizedHeight(static_cast<sal_uInt32>(aStateRect.size.height));

        pState->rMask() |= vcl::WindowDataMask::MaximizedX | vcl::WindowDataMask::MaximizedY | vcl::WindowDataMask::MaximizedWidth | vcl::WindowDataMask::MaximizedHeight;

        pState->setState(vcl::WindowState::FullScreen);
    }
    else if( mbNativeFullScreen && !NSIsEmptyRect( maNativeFullScreenRestoreRect ) )
    {
        pState->setX(maNativeFullScreenRestoreRect.origin.x);
        pState->setY(maNativeFullScreenRestoreRect.origin.y);
        pState->setWidth(maNativeFullScreenRestoreRect.size.width);
        pState->setHeight(maNativeFullScreenRestoreRect.size.height);
        pState->SetMaximizedX(static_cast<sal_Int32>(aStateRect.origin.x));
        pState->SetMaximizedY(static_cast<sal_Int32>(aStateRect.origin.x));
        pState->SetMaximizedWidth(static_cast<sal_uInt32>(aStateRect.size.width));
        pState->SetMaximizedHeight(static_cast<sal_uInt32>(aStateRect.size.height));

        pState->rMask() |= vcl::WindowDataMask::MaximizedX | vcl::WindowDataMask::MaximizedY | vcl::WindowDataMask::MaximizedWidth | vcl::WindowDataMask::MaximizedHeight;

        // tdf#128186 use non-full screen values for native full screen windows
        pState->setState(vcl::WindowState::Normal);
    }
    else
    {
        pState->setX(static_cast<sal_Int32>(aStateRect.origin.x));
        pState->setY(static_cast<sal_Int32>(aStateRect.origin.y));
        pState->setWidth(static_cast<sal_uInt32>(aStateRect.size.width));
        pState->setHeight(static_cast<sal_uInt32>(aStateRect.size.height));

        if( [mpNSWindow isMiniaturized] )
            pState->setState(vcl::WindowState::Minimized);
        else if( ! [mpNSWindow isZoomed] )
            pState->setState(vcl::WindowState::Normal);
        else
            pState->setState(vcl::WindowState::Maximized);
    }

    return true;
}

void AquaSalFrame::SetScreenNumber(unsigned int nScreen)
{
    if ( !mpNSWindow )
        return;

    OSX_SALDATA_RUNINMAIN( SetScreenNumber( nScreen ) )

    NSArray* pScreens = [NSScreen screens];
    NSScreen* pScreen = nil;
    if( pScreens && nScreen < [pScreens count] )
    {
        // get new screen frame
        pScreen = [pScreens objectAtIndex: nScreen];
        NSRect aNewScreen = [pScreen frame];

        // get current screen frame
        pScreen = [mpNSWindow screen];
        if( pScreen )
        {
            NSRect aCurScreen = [pScreen frame];
            if( aCurScreen.origin.x != aNewScreen.origin.x ||
                aCurScreen.origin.y != aNewScreen.origin.y )
            {
                NSRect aFrameRect = [mpNSWindow frame];
                aFrameRect.origin.x += aNewScreen.origin.x - aCurScreen.origin.x;
                aFrameRect.origin.y += aNewScreen.origin.y - aCurScreen.origin.y;
                [mpNSWindow setFrame: aFrameRect display: NO];
                UpdateFrameGeometry();
            }
        }
    }
}

void AquaSalFrame::SetApplicationID( const OUString &/*rApplicationID*/ )
{
}

void AquaSalFrame::ShowFullScreen( bool bFullScreen, sal_Int32 nDisplay )
{
    doShowFullScreen(bFullScreen, nDisplay);
}

void AquaSalFrame::doShowFullScreen( bool bFullScreen, sal_Int32 nDisplay )
{
    if (!mpNSWindow)
    {
        if (Application::IsBitmapRendering() && bFullScreen)
            SetPosSize(0, 0, 1024, 768, SAL_FRAME_POSSIZE_WIDTH | SAL_FRAME_POSSIZE_HEIGHT);
        return;
    }

    SAL_INFO("vcl.osx", __func__ << ": mbInternalFullScreen=" << mbInternalFullScreen << ", bFullScreen=" << bFullScreen);

    if( mbInternalFullScreen == bFullScreen )
        return;

    OSX_SALDATA_RUNINMAIN( ShowFullScreen( bFullScreen, nDisplay ) )

    mbInternalFullScreen = bFullScreen;

    if( bFullScreen )
    {
        NSRect aNewFrameRect = NSZeroRect;
        // get correct screen
        NSScreen* pScreen = nil;
        NSArray* pScreens = [NSScreen screens];
        if( pScreens )
        {
            if( nDisplay >= 0 && o3tl::make_unsigned(nDisplay) < [pScreens count] )
                pScreen = [pScreens objectAtIndex: nDisplay];
            else
            {
                // this means span all screens
                NSEnumerator* pEnum = [pScreens objectEnumerator];
                while( (pScreen = [pEnum nextObject]) != nil )
                {
                    NSRect aScreenRect = [pScreen frame];
                    if( aScreenRect.origin.x < aNewFrameRect.origin.x )
                    {
                        aNewFrameRect.size.width += aNewFrameRect.origin.x - aScreenRect.origin.x;
                        aNewFrameRect.origin.x = aScreenRect.origin.x;
                    }
                    if( aScreenRect.origin.y < aNewFrameRect.origin.y )
                    {
                        aNewFrameRect.size.height += aNewFrameRect.origin.y - aScreenRect.origin.y;
                        aNewFrameRect.origin.y = aScreenRect.origin.y;
                    }
                    if( aScreenRect.origin.x + aScreenRect.size.width > aNewFrameRect.origin.x + aNewFrameRect.size.width )
                        aNewFrameRect.size.width = aScreenRect.origin.x + aScreenRect.size.width - aNewFrameRect.origin.x;
                    if( aScreenRect.origin.y + aScreenRect.size.height > aNewFrameRect.origin.y + aNewFrameRect.size.height )
                        aNewFrameRect.size.height = aScreenRect.origin.y + aScreenRect.size.height - aNewFrameRect.origin.y;
                }
            }
        }
        if( aNewFrameRect.size.width == 0 && aNewFrameRect.size.height == 0 )
        {
            if( pScreen == nil )
                pScreen = [mpNSWindow screen];
            if( pScreen == nil )
                pScreen = [NSScreen mainScreen];

            aNewFrameRect = [pScreen frame];
        }

        // Hide the dock and the menubar if this or one of its child
        // windows are the key window
        if( AquaSalFrame::isAlive( this ) )
        {
            bool bNativeFullScreen = false;
            const AquaSalFrame *pParentFrame = this;
            while( pParentFrame )
            {
                bNativeFullScreen |= pParentFrame->mbNativeFullScreen;
                pParentFrame = AquaSalFrame::isAlive( pParentFrame->mpParent ) ? pParentFrame->mpParent : nullptr;
            }

            if( !bNativeFullScreen )
            {
                const NSWindow *pParentWindow = [NSApp keyWindow];
                while( pParentWindow && pParentWindow != mpNSWindow )
                    pParentWindow = [pParentWindow parentWindow];
                if( pParentWindow == mpNSWindow )
                    [NSMenu setMenuBarVisible: NO];
            }
        }

        if( mbNativeFullScreen && !NSIsEmptyRect( maNativeFullScreenRestoreRect ) )
        {
            maInternalFullScreenRestoreRect = maNativeFullScreenRestoreRect;

            // Show the menubar if application is in native full screen mode
            // since hiding the menubar in that mode will cause the window's
            // titlebar to fail to display or hide as expected.
            [NSMenu setMenuBarVisible: YES];
        }
        else
        {
            // Related: tdf#128186 restore rectangles are in VCL coordinates
            NSRect aFrame = [mpNSWindow frame];
            NSRect aContentRect = [NSWindow contentRectForFrameRect: aFrame styleMask: [mpNSWindow styleMask] & ~NSWindowStyleMaskFullScreen];
            CocoaToVCL( aContentRect );
            maInternalFullScreenRestoreRect = aContentRect;
        }

        // Related: tdf#161623 do not add the window's titlebar height
        // to the window's frame as that will cause the titlebar to be
        // pushed offscreen.
        maInternalFullScreenExpectedRect = aNewFrameRect;

        [mpNSWindow setFrame: maInternalFullScreenExpectedRect display: mbShown ? YES : NO];
    }
    else
    {
        // Show the dock and the menubar if this or one of its children are
        // the key window
        const NSWindow *pParentWindow = [NSApp keyWindow];
        while( pParentWindow && pParentWindow != mpNSWindow )
            pParentWindow = [pParentWindow parentWindow];
        if( pParentWindow == mpNSWindow )
        {
            [NSMenu setMenuBarVisible: YES];
        }
        // Show the dock and the menubar if there is no native modal dialog
        // and if the key window is nil or is not a SalFrameWindow instance.
        // If a SalFrameWindow is the key window, it should have already set
        // the menubar visibility to match its LibreOffice full screen mode
        // state.
        else if( ![NSApp modalWindow] )
        {
            NSWindow *pKeyWindow = [NSApp keyWindow];
            if( !pKeyWindow || ![pKeyWindow isKindOfClass: [SalFrameWindow class]] )
                [NSMenu setMenuBarVisible: YES];
        }

        if( !NSIsEmptyRect( maInternalFullScreenRestoreRect ) )
        {
            if( mbNativeFullScreen && !NSIsEmptyRect( maNativeFullScreenRestoreRect ) )
            {
                // Related: tdf#128186 force window to unzoom
                // If we exit LibreOffice's internal full screen mode while
                // the window is in native full screen mode, the window will
                // be zoomed after exiting native full screen mode.
                [mpNSWindow setIsZoomed: NO];
            }
            else
            {
                NSRect aContentRect = maInternalFullScreenRestoreRect;
                VCLToCocoa( aContentRect );
                NSRect aFrame = [NSWindow frameRectForContentRect: aContentRect styleMask: [mpNSWindow styleMask] & ~NSWindowStyleMaskFullScreen];
                [mpNSWindow setFrame: aFrame display: mbShown ? YES : NO];
            }

            maInternalFullScreenRestoreRect = NSZeroRect;
        }

        maInternalFullScreenExpectedRect = NSZeroRect;
    }

    UpdateFrameGeometry();
    if (mbShown)
    {
        CallCallback(SalEvent::MoveResize, nullptr);

        // trigger filling our backbuffer
        SendPaintEvent();
    }
}

void AquaSalFrame::StartPresentation( bool bStart )
{
    if ( !mpNSWindow )
        return;

    OSX_SALDATA_RUNINMAIN( StartPresentation( bStart ) )

    if( bStart )
    {
        GetSalData()->maPresentationFrames.push_back( this );
        IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep,
                                    kIOPMAssertionLevelOn,
                                    CFSTR("LibreOffice presentation running"),
                                    &mnAssertionID);
        [mpNSWindow setLevel: NSPopUpMenuWindowLevel];
        if( mbShown )
            [mpNSWindow makeMainWindow];
    }
    else
    {
        GetSalData()->maPresentationFrames.remove( this );
        IOPMAssertionRelease(mnAssertionID);
        [mpNSWindow setLevel: NSNormalWindowLevel];
    }
}

void AquaSalFrame::SetAlwaysOnTop( bool )
{
}

void AquaSalFrame::ToTop(SalFrameToTop nFlags)
{
    if ( !mpNSWindow )
        return;

    OSX_SALDATA_RUNINMAIN( ToTop( nFlags ) )

    if( ! (nFlags & SalFrameToTop::RestoreWhenMin) )
    {
        if( ! [mpNSWindow isVisible] || [mpNSWindow isMiniaturized] )
            return;
    }
    if( nFlags & SalFrameToTop::GrabFocus )
        [mpNSWindow makeKeyAndOrderFront: NSApp];
    else
        [mpNSWindow orderFront: NSApp];
}

NSCursor* AquaSalFrame::getCurrentCursor()
{
    OSX_SALDATA_RUNINMAIN_POINTER( getCurrentCursor(), NSCursor* )

    NSCursor* pCursor = nil;
    switch( mePointerStyle )
    {
    case PointerStyle::Text:      pCursor = [NSCursor IBeamCursor];           break;
    case PointerStyle::Cross:     pCursor = [NSCursor crosshairCursor];       break;
    case PointerStyle::Hand:
    case PointerStyle::Move:      pCursor = [NSCursor openHandCursor];        break;
    case PointerStyle::NSize:     pCursor = [NSCursor resizeUpCursor];        break;
    case PointerStyle::SSize:     pCursor = [NSCursor resizeDownCursor];      break;
    case PointerStyle::ESize:     pCursor = [NSCursor resizeRightCursor];      break;
    case PointerStyle::WSize:     pCursor = [NSCursor resizeLeftCursor];     break;
    case PointerStyle::Arrow:     pCursor = [NSCursor arrowCursor];           break;
    case PointerStyle::VSplit:
    case PointerStyle::VSizeBar:
    case PointerStyle::WindowNSize:
    case PointerStyle::WindowSSize:
                            pCursor = [NSCursor resizeUpDownCursor];    break;
    case PointerStyle::HSplit:
    case PointerStyle::HSizeBar:
    case PointerStyle::WindowESize:
    case PointerStyle::WindowWSize:
                            pCursor = [NSCursor resizeLeftRightCursor]; break;
    case PointerStyle::RefHand:   pCursor = [NSCursor pointingHandCursor];    break;

    default:
        pCursor = GetSalData()->getCursor( mePointerStyle );
        if( pCursor == nil )
        {
            assert( false && "unmapped cursor" );
            pCursor = [NSCursor arrowCursor];
        }
        break;
    }
    return pCursor;
}

void AquaSalFrame::SetPointer( PointerStyle ePointerStyle )
{
    if ( !mpNSWindow )
        return;
    if( ePointerStyle == mePointerStyle )
        return;

    OSX_SALDATA_RUNINMAIN( SetPointer( ePointerStyle ) )

    mePointerStyle = ePointerStyle;

    [mpNSWindow invalidateCursorRectsForView: mpNSView];
}

void AquaSalFrame::SetPointerPos( tools::Long nX, tools::Long nY )
{
    OSX_SALDATA_RUNINMAIN( SetPointerPos( nX, nY ) )

    // FIXME: use Cocoa functions
    // FIXME: multiscreen support
    CGPoint aPoint = { static_cast<CGFloat>(nX + maGeometry.x()), static_cast<CGFloat>(nY + maGeometry.y()) };
    CGDirectDisplayID mainDisplayID = CGMainDisplayID();
    CGDisplayMoveCursorToPoint( mainDisplayID, aPoint );
}

void AquaSalFrame::Flush()
{
    if( !(mbGraphicsAcquired && mpGraphics && mpNSView && mbShown) )
        return;

    OSX_SALDATA_RUNINMAIN( Flush() )

    [mpNSView setNeedsDisplay: YES];

    // outside of the application's event loop (e.g. IntroWindow)
    // nothing would trigger paint event handling
    // => fall back to synchronous painting
    if( doFlush() )
        [mpNSView display];
}

void AquaSalFrame::Flush( const tools::Rectangle& rRect )
{
    if( !(mbGraphicsAcquired && mpGraphics && mpNSView && mbShown) )
        return;

    OSX_SALDATA_RUNINMAIN( Flush( rRect ) )

    NSRect aNSRect = { { static_cast<CGFloat>(rRect.Left()), static_cast<CGFloat>(rRect.Top()) }, { static_cast<CGFloat>(rRect.GetWidth()), static_cast<CGFloat>(rRect.GetHeight()) } };
    VCLToCocoa( aNSRect, false );
    [mpNSView setNeedsDisplayInRect: aNSRect];

    // outside of the application's event loop (e.g. IntroWindow)
    // nothing would trigger paint event handling
    // => fall back to synchronous painting
    if( doFlush() )
        [mpNSView displayRect: aNSRect];
}

bool AquaSalFrame::doFlush()
{
    bool bRet = false;

    if( mbForceFlushScrolling || mbForceFlushProgressBar || ImplGetSVData()->maAppData.mnDispatchLevel <= 0 )
    {
        mpGraphics->Flush();

        // Related: tdf#155266 skip redisplay of the view when forcing flush
        // It appears that calling -[NSView display] overwhelms some Intel Macs
        // so only flush the graphics and skip immediate redisplay of the view.
        bRet = ImplGetSVData()->maAppData.mnDispatchLevel <= 0;

        mbForceFlushScrolling = false;
        mbForceFlushProgressBar = false;
    }

    return bRet;
}

void AquaSalFrame::SetInputContext( SalInputContext* pContext )
{
    if (!pContext)
    {
        mnICOptions = InputContextFlags::NONE;
        return;
    }

    mnICOptions = pContext->mnOptions;

    if(!(pContext->mnOptions & InputContextFlags::Text))
        return;
}

void AquaSalFrame::EndExtTextInput( EndExtTextInputFlags nFlags )
{
    // tdf#82115 Commit uncommitted text when a popup menu is opened
    // The Windows implementation of this method commits or discards the native
    // input method session. It appears that very few, if any, macOS
    // applications discard the uncommitted text when cancelling a session so
    // always commit the uncommitted text.
    SalFrameWindow *pWindow = static_cast<SalFrameWindow*>(mpNSWindow);
    if (pWindow && [pWindow isKindOfClass:[SalFrameWindow class]])
        [pWindow endExtTextInput:nFlags];
}

OUString AquaSalFrame::GetKeyName( sal_uInt16 nKeyCode )
{
    static std::map< sal_uInt16, OUString > aKeyMap;
    if( aKeyMap.empty() )
    {
        sal_uInt16 i;
        for( i = KEY_A; i <= KEY_Z; i++ )
            aKeyMap[ i ] = OUString( sal_Unicode( 'A' + (i - KEY_A) ) );
        for( i = KEY_0; i <= KEY_9; i++ )
            aKeyMap[ i ] = OUString( sal_Unicode( '0' + (i - KEY_0) ) );
        for( i = KEY_F1; i <= KEY_F26; i++ )
        {
            aKeyMap[ i ] = "F" + OUString::number(i - KEY_F1 + 1);
        }

        aKeyMap[ KEY_DOWN ]     = OUString( u'\x21e3' );
        aKeyMap[ KEY_UP ]       = OUString( u'\x21e1' );
        aKeyMap[ KEY_LEFT ]     = OUString( u'\x21e0' );
        aKeyMap[ KEY_RIGHT ]    = OUString( u'\x21e2' );
        aKeyMap[ KEY_HOME ]     = OUString( u'\x2196' );
        aKeyMap[ KEY_END ]      = OUString( u'\x2198' );
        aKeyMap[ KEY_PAGEUP ]   = OUString( u'\x21de' );
        aKeyMap[ KEY_PAGEDOWN ] = OUString( u'\x21df' );
        aKeyMap[ KEY_RETURN ]   = OUString( u'\x21a9' );
        aKeyMap[ KEY_ESCAPE ]   = "esc";
        aKeyMap[ KEY_TAB ]      = OUString( u'\x21e5' );
        aKeyMap[ KEY_BACKSPACE ]= OUString( u'\x232b' );
        aKeyMap[ KEY_SPACE ]    = OUString( u'\x2423' );
        aKeyMap[ KEY_DELETE ]   = OUString( u'\x2326' );
        aKeyMap[ KEY_ADD ]      = "+";
        aKeyMap[ KEY_SUBTRACT ] = "-";
        aKeyMap[ KEY_DIVIDE ]   = "/";
        aKeyMap[ KEY_MULTIPLY ] = "*";
        aKeyMap[ KEY_POINT ]    = ".";
        aKeyMap[ KEY_COMMA ]    = ",";
        aKeyMap[ KEY_LESS ]     = "<";
        aKeyMap[ KEY_GREATER ]  = ">";
        aKeyMap[ KEY_EQUAL ]    = "=";
        aKeyMap[ KEY_OPEN ]     = OUString( u'\x23cf' );
        aKeyMap[ KEY_TILDE ]    = "~";
        aKeyMap[ KEY_BRACKETLEFT ] = "[";
        aKeyMap[ KEY_BRACKETRIGHT ] = "]";
        aKeyMap[ KEY_SEMICOLON ] = ";";
        aKeyMap[ KEY_QUOTERIGHT ] = "'";
        aKeyMap[ KEY_RIGHTCURLYBRACKET ] = "}";
        aKeyMap[ KEY_NUMBERSIGN ] = "#";
        aKeyMap[ KEY_COLON ] = ":";

        /* yet unmapped KEYCODES:
        aKeyMap[ KEY_INSERT ]   = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_CUT ]      = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_COPY ]     = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_PASTE ]    = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_UNDO ]     = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_REPEAT ]   = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_FIND ]     = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_PROPERTIES ]     = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_FRONT ]    = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_CONTEXTMENU ]    = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_MENU ]     = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_HELP ]     = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_HANGUL_HANJA ]   = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_DECIMAL ]  = OUString( sal_Unicode( ) );
        aKeyMap[ KEY_QUOTELEFT ]= OUString( sal_Unicode( ) );
        aKeyMap[ KEY_CAPSLOCK ]= OUString( sal_Unicode( ) );
        aKeyMap[ KEY_NUMLOCK ]= OUString( sal_Unicode( ) );
        aKeyMap[ KEY_SCROLLLOCK ]= OUString( sal_Unicode( ) );
        */

    }

    OUStringBuffer aResult( 16 );

    sal_uInt16 nUnmodifiedCode = (nKeyCode & KEY_CODE_MASK);
    std::map< sal_uInt16, OUString >::const_iterator it = aKeyMap.find( nUnmodifiedCode );
    if( it != aKeyMap.end() )
    {
        if( (nKeyCode & KEY_SHIFT) != 0 )
            aResult.append( u'\x21e7' ); // shift
        if( (nKeyCode & KEY_MOD1) != 0 )
            aResult.append( u'\x2318' ); // command
        if( (nKeyCode & KEY_MOD2) != 0 )
            aResult.append( u'\x2325' ); // alternate
        if( (nKeyCode & KEY_MOD3) != 0 )
            aResult.append( u'\x2303' ); // control

        aResult.append( it->second );
    }

    return aResult.makeStringAndClear();
}

static void getAppleScrollBarVariant(StyleSettings &rSettings)
{
    bool bIsScrollbarDoubleMax = true; // default is DoubleMax

    CFStringRef AppleScrollBarType = CFSTR("AppleScrollBarVariant");
    if( AppleScrollBarType )
    {
        CFStringRef ScrollBarVariant = static_cast<CFStringRef>(CFPreferencesCopyAppValue( AppleScrollBarType, kCFPreferencesCurrentApplication ));
        if( ScrollBarVariant )
        {
            if( CFGetTypeID( ScrollBarVariant ) == CFStringGetTypeID() )
            {
                // TODO: check for the less important variants "DoubleMin" and "DoubleBoth" too
                CFStringRef DoubleMax = CFSTR("DoubleMax");
                if (DoubleMax)
                {
                    if ( !CFStringCompare(ScrollBarVariant, DoubleMax, kCFCompareCaseInsensitive) )
                        bIsScrollbarDoubleMax = true;
                    else
                        bIsScrollbarDoubleMax = false;
                    CFRelease(DoubleMax);
                }
            }
            CFRelease( ScrollBarVariant );
        }
        CFRelease(AppleScrollBarType);
    }

    GetSalData()->mbIsScrollbarDoubleMax = bIsScrollbarDoubleMax;

    CFStringRef jumpScroll = CFSTR("AppleScrollerPagingBehavior");
    if( jumpScroll )
    {
        CFBooleanRef jumpStr = static_cast<CFBooleanRef>(CFPreferencesCopyAppValue( jumpScroll, kCFPreferencesCurrentApplication ));
        if( jumpStr )
        {
            if( CFGetTypeID( jumpStr ) == CFBooleanGetTypeID() )
                rSettings.SetPrimaryButtonWarpsSlider(jumpStr == kCFBooleanTrue);
            CFRelease( jumpStr );
        }
        CFRelease( jumpScroll );
    }
}

static Color getNSBoxBackgroundColor(NSColor* pSysColor)
{
    // Figuring out what a NSBox will draw for windowBackground, etc. seems very difficult.
    // So just draw to a 1x1 surface and read what actually gets drawn
    // This is similar to getPixel
#if defined OSL_BIGENDIAN
    struct
    {
        unsigned char b, g, r, a;
    } aPixel;
#else
    struct
    {
        unsigned char a, r, g, b;
    } aPixel;
#endif

    // create a one-pixel bitmap context
    CGContextRef xOnePixelContext = CGBitmapContextCreate(
        &aPixel, 1, 1, 8, 32, GetSalData()->mxRGBSpace,
        uint32_t(kCGImageAlphaNoneSkipFirst) | uint32_t(kCGBitmapByteOrder32Big));

    NSGraphicsContext* graphicsContext = [NSGraphicsContext graphicsContextWithCGContext:xOnePixelContext flipped:NO];

    NSRect rect = { NSZeroPoint, NSMakeSize(1, 1) };
    NSBox* pBox = [[NSBox alloc] initWithFrame: rect];

    [pBox setBoxType: NSBoxCustom];
    [pBox setFillColor: pSysColor];
    SAL_WNODEPRECATED_DECLARATIONS_PUSH // setBorderType first deprecated in macOS 10.15
    [pBox setBorderType: NSNoBorder];
    SAL_WNODEPRECATED_DECLARATIONS_POP

    [pBox displayRectIgnoringOpacity: rect inContext: graphicsContext];

    [pBox release];

    CGContextRelease(xOnePixelContext);

    return Color(aPixel.r, aPixel.g, aPixel.b);
}

static Color getColor( NSColor* pSysColor, const Color& rDefault, NSWindow* pWin )
{
    Color aRet( rDefault );
    if( pSysColor )
    {
        // transform to RGB
SAL_WNODEPRECATED_DECLARATIONS_PUSH
            // "'colorUsingColorSpaceName:device:' is deprecated: first deprecated in macOS 10.14 -
            // Use -colorUsingType: or -colorUsingColorSpace: instead"
        NSColor* pRBGColor = [pSysColor colorUsingColorSpaceName: NSDeviceRGBColorSpace device: [pWin deviceDescription]];
SAL_WNODEPRECATED_DECLARATIONS_POP
        if( pRBGColor )
        {
            CGFloat r = 0, g = 0, b = 0, a = 0;
            [pRBGColor getRed: &r green: &g blue: &b alpha: &a];
            aRet = Color( int(r*255.999), int(g*255.999), int(b*255.999) );
        }
    }
    return aRet;
}

static vcl::Font getFont( NSFont* pFont, sal_Int32 nDPIY, const vcl::Font& rDefault )
{
    vcl::Font aResult( rDefault );
    if( pFont )
    {
        aResult.SetFamilyName( GetOUString( [pFont familyName] ) );
        aResult.SetFontHeight( static_cast<int>(ceil([pFont pointSize] * 72.0 / static_cast<float>(nDPIY))) );
        aResult.SetItalic( ([pFont italicAngle] != 0.0) ? ITALIC_NORMAL : ITALIC_NONE );
        // FIMXE: bold ?
    }

    return aResult;
}

void AquaSalFrame::getResolution( sal_Int32& o_rDPIX, sal_Int32& o_rDPIY )
{
    OSX_SALDATA_RUNINMAIN( getResolution( o_rDPIX, o_rDPIY ) )

    if( ! mpGraphics )
    {
        AcquireGraphics();
        ReleaseGraphics( mpGraphics );
    }
    mpGraphics->GetResolution( o_rDPIX, o_rDPIY );
}

void AquaSalFrame::UpdateDarkMode()
{
    NSAppearance *pCurrentAppearance = [NSApp appearance];

    switch (MiscSettings::GetAppColorMode())
    {
        case AppearanceMode::AUTO:
        default:
            if (pCurrentAppearance)
                [NSApp setAppearance: nil];
            break;
        case AppearanceMode::LIGHT:
            if (!pCurrentAppearance || ![NSAppearanceNameAqua isEqualToString: [pCurrentAppearance name]])
                [NSApp setAppearance: [NSAppearance appearanceNamed: NSAppearanceNameAqua]];
            break;
        case AppearanceMode::DARK:
            if (!pCurrentAppearance || ![NSAppearanceNameDarkAqua isEqualToString: [pCurrentAppearance name]])
                [NSApp setAppearance: [NSAppearance appearanceNamed: NSAppearanceNameDarkAqua]];
            break;
    }

    // Related: tdf#165266 sync NSView's appearance to NSApp's appearance
    // Invoking -[NSApp setAppearance:] does immediately update the
    // appearance of each NSWindow's titlebar, but it does not appear
    // to update any NSView's appearance so explicitly sync appearances.
    NSAppearance *pNewAppearance = [NSApp appearance];
    if (mpNSView.appearance != pNewAppearance)
        mpNSView.appearance = pNewAppearance;
}

bool AquaSalFrame::GetUseDarkMode() const
{
    if (!mpNSView)
        return false;
    bool bUseDarkMode(false);
    if (@available(macOS 10.14, iOS 13, *))
    {
        NSAppearanceName match = [mpNSView.effectiveAppearance bestMatchFromAppearancesWithNames: @[
                                  NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
        bUseDarkMode = [match isEqualToString: NSAppearanceNameDarkAqua];
    }
    return bUseDarkMode;
}

bool AquaSalFrame::GetUseReducedAnimation() const
{
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}

static void lcl_LoadColorsFromTheme(StyleSettings& rStyleSet)
{
    const ThemeColors& rThemeColors = ThemeColors::GetThemeColors();
    rStyleSet.SetWindowColor(rThemeColors.GetWindowColor());
    rStyleSet.BatchSetBackgrounds(rThemeColors.GetWindowColor());
    rStyleSet.SetActiveTabColor(rThemeColors.GetWindowColor());
    rStyleSet.SetInactiveTabColor(rThemeColors.GetBaseColor());
    rStyleSet.SetDisableColor(rThemeColors.GetDisabledColor()); // tab outline
    // Highlight related colors
    rStyleSet.SetAccentColor(rThemeColors.GetAccentColor());
    rStyleSet.SetHighlightColor(rThemeColors.GetAccentColor());
    rStyleSet.SetListBoxWindowHighlightColor(rThemeColors.GetAccentColor());
    rStyleSet.SetListBoxWindowTextColor(rThemeColors.GetWindowTextColor());
    rStyleSet.SetListBoxWindowBackgroundColor(rThemeColors.GetBaseColor());
    rStyleSet.SetListBoxWindowHighlightTextColor(rThemeColors.GetMenuHighlightTextColor());
    rStyleSet.SetWindowTextColor(rThemeColors.GetWindowTextColor()); // Treeview Lists
    rStyleSet.SetRadioCheckTextColor(rThemeColors.GetWindowTextColor());
    rStyleSet.SetLabelTextColor(rThemeColors.GetWindowTextColor());
    rStyleSet.SetFieldTextColor(rThemeColors.GetWindowTextColor());
    rStyleSet.SetFieldColor(rThemeColors.GetBaseColor());
    rStyleSet.SetMenuBarTextColor(rThemeColors.GetMenuBarTextColor());
    rStyleSet.SetMenuTextColor(rThemeColors.GetMenuTextColor());
    rStyleSet.SetDefaultActionButtonTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetActionButtonTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetShadowColor(rThemeColors.GetShadeColor());
    rStyleSet.SetDefaultButtonTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetDefaultButtonRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetDefaultButtonPressedRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetFlatButtonTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetFlatButtonPressedRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetFlatButtonRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetButtonRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetDefaultActionButtonRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetDefaultActionButtonPressedRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetActionButtonRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetActionButtonPressedRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetFieldRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetButtonRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetButtonPressedRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetHelpColor(rThemeColors.GetWindowColor());
    rStyleSet.SetHelpTextColor(rThemeColors.GetWindowTextColor());
    // rStyleSet.SetHighlightTextColor(rThemeColors.GetActiveTextColor());
    // rStyleSet.SetActiveColor(rThemeColors.GetActiveColor());
    // rStyleSet.SetActiveTextColor(rThemeColors.GetActiveTextColor());
    // rStyleSet.SetLinkColor(rThemeColors.GetAccentColor());
    // Color aVisitedLinkColor = rThemeColors.GetActiveColor();
    // aVisitedLinkColor.Merge(rThemeColors.GetWindowColor(), 100);
    // rStyleSet.SetVisitedLinkColor(aVisitedLinkColor);
    // rStyleSet.SetToolTextColor(Color(255, 0, 0));
    rStyleSet.SetTabRolloverTextColor(rThemeColors.GetMenuBarHighlightTextColor());
}

// on OSX-Aqua the style settings are independent of the frame, so it does
// not really belong here. Since the connection to the Appearance_Manager
// is currently done in salnativewidgets.cxx this would be a good place.
// On the other hand VCL's platform independent code currently only asks
// SalFrames for system settings anyway, so moving the code somewhere else
// doesn't make the anything cleaner for now
void AquaSalFrame::UpdateSettings( AllSettings& rSettings )
{
    if ( !mpNSWindow )
        return;

    OSX_SALDATA_RUNINMAIN( UpdateSettings( rSettings ) )

    // tdf#165266 Force NSColor to use current effective appearance
    // +[NSAppearance setCurrentAppearance:] is deprecated and calling
    // that appears to do less and less with each new version of macos
    // or Xcode so run all system +[NSColor ...] calls in a block passed
    // to -[NSAppearance performAsCurrentDrawingAppearance:].
    UpdateDarkMode();
    if (@available(macOS 11, *))
    {
        assert(mpNSView);
        [mpNSView.effectiveAppearance performAsCurrentDrawingAppearance:^() {
            doUpdateSettings( rSettings );
            return;
        }];
    }
    else
    {
        doUpdateSettings( rSettings );
    }
}

void AquaSalFrame::doUpdateSettings( AllSettings& rSettings )
{
    assert(mpNSWindow);
    assert(mpNSView);

SAL_WNODEPRECATED_DECLARATIONS_PUSH
        // "'lockFocus' is deprecated: first deprecated in macOS 10.14 - To draw, subclass NSView
        // and implement -drawRect:; AppKit's automatic deferred display mechanism will call
        // -drawRect: as necessary to display the view."
    if (![mpNSView lockFocusIfCanDraw])
        return;
SAL_WNODEPRECATED_DECLARATIONS_POP

SAL_WNODEPRECATED_DECLARATIONS_PUSH
        // "'setCurrentAppearance:' is deprecated: first deprecated in macOS 12.0 - Use
        // -performAsCurrentDrawingAppearance: to temporarily set the drawing appearance, or
        // +currentDrawingAppearance to access the currently drawing appearance."
    [NSAppearance setCurrentAppearance: mpNSView.effectiveAppearance];
SAL_WNODEPRECATED_DECLARATIONS_POP

    StyleSettings aStyleSettings = rSettings.GetStyleSettings();

    bool bUseDarkMode(GetUseDarkMode());
    if (!ThemeColors::VclPluginCanUseThemeColors())
    {
        OUString sThemeName(!bUseDarkMode ? u"sukapura_svg" : u"sukapura_dark_svg");
        aStyleSettings.SetPreferredIconTheme(sThemeName, bUseDarkMode);
    }
    else
    {
        aStyleSettings.SetPreferredIconTheme(
            vcl::IconThemeSelector::GetIconThemeForDesktopEnvironment(
                Application::GetDesktopEnvironment(),
                ThemeColors::GetThemeColors().GetWindowColor().IsDark()));
    }

    Color aControlBackgroundColor(getNSBoxBackgroundColor([NSColor controlBackgroundColor]));
    Color aWindowBackgroundColor(getNSBoxBackgroundColor([NSColor windowBackgroundColor]));
    Color aUnderPageBackgroundColor(getNSBoxBackgroundColor([NSColor underPageBackgroundColor]));

    // Background Color
    aStyleSettings.BatchSetBackgrounds( aWindowBackgroundColor, false );
    aStyleSettings.SetLightBorderColor( aWindowBackgroundColor );

    aStyleSettings.SetActiveTabColor(aWindowBackgroundColor);
    Color aInactiveTabColor( aWindowBackgroundColor );
    aInactiveTabColor.DecreaseLuminance( 32 );
    aStyleSettings.SetInactiveTabColor( aInactiveTabColor );

    Color aShadowColor = getColor( [NSColor systemGrayColor ],
                                      aStyleSettings.GetShadowColor(), mpNSWindow );
    aStyleSettings.SetShadowColor( aShadowColor );

    // tdf#152284 for DarkMode brighten it, while darken for BrightMode
    NSColor* pDarkColor = bUseDarkMode ? [[NSColor systemGrayColor] highlightWithLevel: 0.5]
                                       : [[NSColor systemGrayColor] shadowWithLevel: 0.5];
    Color aDarkShadowColor = getColor( pDarkColor, aStyleSettings.GetDarkShadowColor(), mpNSWindow );
    aStyleSettings.SetDarkShadowColor(aDarkShadowColor);

    // get the system font settings
    vcl::Font aAppFont = aStyleSettings.GetAppFont();
    sal_Int32 nDPIX = 72, nDPIY = 72;
    getResolution( nDPIX, nDPIY );
    aAppFont = getFont( [NSFont systemFontOfSize: 0], nDPIY, aAppFont );

    aStyleSettings.SetToolbarIconSize( ToolbarIconSize::Large );

    // TODO: better mapping of macOS<->LibreOffice font settings
    vcl::Font aLabelFont( getFont( [NSFont labelFontOfSize: 0], nDPIY, aAppFont ) );
    aStyleSettings.BatchSetFonts( aAppFont, aLabelFont );
    vcl::Font aMenuFont( getFont( [NSFont menuFontOfSize: 0], nDPIY, aAppFont ) );
    aStyleSettings.SetMenuFont( aMenuFont );

    vcl::Font aTitleFont( getFont( [NSFont titleBarFontOfSize: 0], nDPIY, aAppFont ) );
    aStyleSettings.SetTitleFont( aTitleFont );
    aStyleSettings.SetFloatTitleFont( aTitleFont );

    vcl::Font aTooltipFont(getFont([NSFont toolTipsFontOfSize: 0], nDPIY, aAppFont));
    aStyleSettings.SetHelpFont(aTooltipFont);

    Color aAccentColor( getColor( [NSColor controlAccentColor],
                                   aStyleSettings.GetAccentColor(), mpNSWindow ) );
    aStyleSettings.SetAccentColor( aAccentColor );

    Color aHighlightColor( getColor( [NSColor selectedTextBackgroundColor],
                                      aStyleSettings.GetHighlightColor(), mpNSWindow ) );
    aStyleSettings.SetHighlightColor( aHighlightColor );
    Color aHighlightTextColor( getColor( [NSColor selectedTextColor],
                                         aStyleSettings.GetHighlightTextColor(), mpNSWindow ) );
    aStyleSettings.SetHighlightTextColor( aHighlightTextColor );

    aStyleSettings.SetLinkColor(getColor( [NSColor linkColor],
                                           aStyleSettings.GetLinkColor(), mpNSWindow ) );
    aStyleSettings.SetVisitedLinkColor(getColor( [NSColor purpleColor],
                                                  aStyleSettings.GetVisitedLinkColor(), mpNSWindow ) );

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    Color aMenuHighlightColor( getColor( [NSColor selectedMenuItemColor],
                                         aStyleSettings.GetMenuHighlightColor(), mpNSWindow ) );
#pragma clang diagnostic pop
    aStyleSettings.SetMenuHighlightColor( aMenuHighlightColor );
    Color aMenuHighlightTextColor( getColor( [NSColor selectedMenuItemTextColor],
                                             aStyleSettings.GetMenuHighlightTextColor(), mpNSWindow ) );
    aStyleSettings.SetMenuHighlightTextColor( aMenuHighlightTextColor );

    aStyleSettings.SetMenuColor( aWindowBackgroundColor );
    Color aMenuTextColor( getColor( [NSColor textColor],
                                    aStyleSettings.GetMenuTextColor(), mpNSWindow ) );
    aStyleSettings.SetMenuTextColor( aMenuTextColor );
    aStyleSettings.SetMenuBarTextColor( aMenuTextColor );
    aStyleSettings.SetMenuBarRolloverTextColor( aMenuTextColor );
    aStyleSettings.SetMenuBarHighlightTextColor(aStyleSettings.GetMenuHighlightTextColor());

    aStyleSettings.SetListBoxWindowBackgroundColor( aWindowBackgroundColor );
    aStyleSettings.SetListBoxWindowTextColor( aMenuTextColor );
    aStyleSettings.SetListBoxWindowHighlightColor( aMenuHighlightColor );
    aStyleSettings.SetListBoxWindowHighlightTextColor( aMenuHighlightTextColor );

    // FIXME: Starting with macOS Big Sur, coloring has changed. Currently there is no documentation which system color should be
    // used for some button states and for selected tab text. As a workaround the current OS version has to be considered. This code
    // has to be reviewed once issue is covered by documentation.

    // Set text colors for buttons and their different status according to OS settings, typically white for selected buttons,
    // black otherwise

    NSOperatingSystemVersion aOSVersion = { .majorVersion = 10, .minorVersion = 16, .patchVersion = 0 };
    Color aControlTextColor(getColor([NSColor controlTextColor], COL_BLACK, mpNSWindow ));
    Color aSelectedControlTextColor(getColor([NSColor selectedControlTextColor], COL_BLACK, mpNSWindow ));
    Color aAlternateSelectedControlTextColor(getColor([NSColor alternateSelectedControlTextColor], COL_WHITE, mpNSWindow ));
    aStyleSettings.SetWindowColor(aWindowBackgroundColor);
    aStyleSettings.SetListBoxWindowBackgroundColor(aWindowBackgroundColor);

    aStyleSettings.SetDialogTextColor(aControlTextColor);
    aStyleSettings.SetButtonTextColor(aControlTextColor);
    aStyleSettings.SetActionButtonTextColor(aControlTextColor);
    aStyleSettings.SetRadioCheckTextColor(aControlTextColor);
    aStyleSettings.SetGroupTextColor(aControlTextColor);
    aStyleSettings.SetLabelTextColor(aControlTextColor);
    aStyleSettings.SetWindowTextColor(aControlTextColor);
    aStyleSettings.SetFieldTextColor(aControlTextColor);

    aStyleSettings.SetFieldRolloverTextColor(aControlTextColor);
    aStyleSettings.SetFieldColor(aControlBackgroundColor);
    aStyleSettings.SetDefaultActionButtonTextColor(aAlternateSelectedControlTextColor);
    aStyleSettings.SetFlatButtonTextColor(aControlTextColor);
    aStyleSettings.SetDefaultButtonRolloverTextColor(aAlternateSelectedControlTextColor);
    aStyleSettings.SetButtonRolloverTextColor(aControlTextColor);
    aStyleSettings.SetDefaultActionButtonRolloverTextColor(aAlternateSelectedControlTextColor);
    aStyleSettings.SetActionButtonRolloverTextColor(aControlTextColor);
    aStyleSettings.SetFlatButtonRolloverTextColor(aControlTextColor);
    aStyleSettings.SetDefaultButtonPressedRolloverTextColor(aAlternateSelectedControlTextColor);
    aStyleSettings.SetDefaultActionButtonPressedRolloverTextColor(aAlternateSelectedControlTextColor);
    aStyleSettings.SetFlatButtonPressedRolloverTextColor(aControlTextColor);
    if ([NSProcessInfo.processInfo isOperatingSystemAtLeastVersion: aOSVersion])
    {
        aStyleSettings.SetDefaultButtonTextColor(aAlternateSelectedControlTextColor);
        aStyleSettings.SetButtonPressedRolloverTextColor(aSelectedControlTextColor);
        aStyleSettings.SetActionButtonPressedRolloverTextColor(aSelectedControlTextColor);
    }
    else
    {
        aStyleSettings.SetButtonPressedRolloverTextColor(aAlternateSelectedControlTextColor);
        aStyleSettings.SetActionButtonPressedRolloverTextColor(aAlternateSelectedControlTextColor);
        aStyleSettings.SetDefaultButtonTextColor(aSelectedControlTextColor);
    }

    aStyleSettings.SetWorkspaceColor(aUnderPageBackgroundColor);

    aStyleSettings.SetHelpColor(aControlBackgroundColor);
    aStyleSettings.SetHelpTextColor(aControlTextColor);
    aStyleSettings.SetToolTextColor(aControlTextColor);

    // Set text colors for tabs according to OS settings

    aStyleSettings.SetTabTextColor(aControlTextColor);
    aStyleSettings.SetTabRolloverTextColor(aControlTextColor);
    if ([NSProcessInfo.processInfo isOperatingSystemAtLeastVersion: aOSVersion])
        aStyleSettings.SetTabHighlightTextColor(aSelectedControlTextColor);
    else
        aStyleSettings.SetTabHighlightTextColor(aAlternateSelectedControlTextColor);

    aStyleSettings.SetCursorBlinkTime( mnBlinkCursorDelay );
    aStyleSettings.SetCursorSize(1);

    // no mnemonics on macOS
    aStyleSettings.SetOptions( aStyleSettings.GetOptions() | StyleSettingsOptions::NoMnemonics );

    getAppleScrollBarVariant(aStyleSettings);

    // set scrollbar size
    aStyleSettings.SetScrollBarSize( static_cast<tools::Long>([NSScroller scrollerWidthForControlSize:NSControlSizeRegular scrollerStyle:NSScrollerStyleLegacy]) );
    // images in menus false for MacOSX
    aStyleSettings.SetPreferredUseImagesInMenus( false );
    aStyleSettings.SetHideDisabledMenuItems( true );
    aStyleSettings.SetPreferredContextMenuShortcuts( false );

    if (ThemeColors::VclPluginCanUseThemeColors())
        lcl_LoadColorsFromTheme(aStyleSettings);
    aStyleSettings.SetSystemColorsLoaded(true);

    rSettings.SetStyleSettings( aStyleSettings );

    // don't draw frame around each and every toolbar
    ImplGetSVData()->maNWFData.mbDockingAreaAvoidTBFrames = true;

SAL_WNODEPRECATED_DECLARATIONS_PUSH
        // "'unlockFocus' is deprecated: first deprecated in macOS 10.14 - To draw, subclass NSView
        // and implement -drawRect:; AppKit's automatic deferred display mechanism will call
        // -drawRect: as necessary to display the view."
    [mpNSView unlockFocus];
SAL_WNODEPRECATED_DECLARATIONS_POP
}

const SystemEnvData& AquaSalFrame::GetSystemData() const
{
    return maSysData;
}

void AquaSalFrame::Beep()
{
    OSX_SALDATA_RUNINMAIN( Beep() )
    NSBeep();
}

void AquaSalFrame::SetPosSize(
    tools::Long nX, tools::Long nY, tools::Long nWidth, tools::Long nHeight, sal_uInt16 nFlags)
{
    if (!mpNSWindow && !Application::IsBitmapRendering())
        return;

    OSX_SALDATA_RUNINMAIN( SetPosSize( nX, nY, nWidth, nHeight, nFlags ) )

    SalEvent nEvent = PreparePosSize(nX, nY, nWidth, nHeight, nFlags);
    if (Application::IsBitmapRendering())
        return;

    if( [mpNSWindow isMiniaturized] )
        [mpNSWindow deminiaturize: NSApp]; // expand the window

    NSRect aFrameRect = [mpNSWindow frame];
    NSRect aContentRect = [mpNSWindow contentRectForFrameRect: aFrameRect];

    // position is always relative to parent frame
    NSRect aParentContentRect;

    if( mpParent )
    {
        if( AllSettings::GetLayoutRTL() )
        {
            if( (nFlags & SAL_FRAME_POSSIZE_WIDTH) != 0 )
                nX = static_cast<tools::Long>(mpParent->maGeometry.width()) - nWidth - 1 - nX;
            else
                nX = static_cast<tools::Long>(mpParent->maGeometry.width()) - aContentRect.size.width - 1 - nX;
        }
        NSRect aParentFrameRect = [mpParent->mpNSWindow frame];
        aParentContentRect = [mpParent->mpNSWindow contentRectForFrameRect: aParentFrameRect];
    }
    else
        aParentContentRect = maScreenRect; // use screen if no parent

    CocoaToVCL( aContentRect );
    CocoaToVCL( aParentContentRect );

    // Related: tdf#128186 don't change size of native full screen windows
    if ([mpNSWindow styleMask] & NSWindowStyleMaskFullScreen)
    {
        maNativeFullScreenRestoreRect = aContentRect;
        return;
    }

    bool bPaint = false;
    if( (nFlags & (SAL_FRAME_POSSIZE_WIDTH | SAL_FRAME_POSSIZE_HEIGHT)) != 0 )
    {
        if( nWidth != aContentRect.size.width || nHeight != aContentRect.size.height )
            bPaint = true;
    }

    // use old window pos if no new pos requested
    if( (nFlags & SAL_FRAME_POSSIZE_X) != 0 )
        aContentRect.origin.x = nX + aParentContentRect.origin.x;
    if( (nFlags & SAL_FRAME_POSSIZE_Y) != 0)
        aContentRect.origin.y = nY + aParentContentRect.origin.y;

    // use old size if no new size requested
    if( (nFlags & SAL_FRAME_POSSIZE_WIDTH) != 0 )
        aContentRect.size.width = nWidth;
    if( (nFlags & SAL_FRAME_POSSIZE_HEIGHT) != 0)
        aContentRect.size.height = nHeight;

    VCLToCocoa( aContentRect );

    // do not display yet, we need to update our backbuffer
    {
        [mpNSWindow setFrame: [mpNSWindow frameRectForContentRect: aContentRect] display: NO];
    }

    UpdateFrameGeometry();

    if (nEvent != SalEvent::NONE)
        CallCallback(nEvent, nullptr);

    if( mbShown && bPaint )
    {
        // trigger filling our backbuffer
        SendPaintEvent();

        // now inform the system that the views need to be drawn
        [mpNSWindow display];
    }
}

void AquaSalFrame::GetWorkArea( AbsoluteScreenPixelRectangle& rRect )
{
    if (!mpNSWindow)
    {
        if (Application::IsBitmapRendering())
            rRect = AbsoluteScreenPixelRectangle(AbsoluteScreenPixelPoint(0, 0), AbsoluteScreenPixelSize(1024, 768));
        return;
    }

    OSX_SALDATA_RUNINMAIN( GetWorkArea( rRect ) )

    NSScreen* pScreen = [mpNSWindow screen];
    if( pScreen ==  nil )
        pScreen = [NSScreen mainScreen];
    NSRect aRect = [pScreen visibleFrame];
    CocoaToVCL( aRect );
    rRect.SetLeft( static_cast<tools::Long>(aRect.origin.x) );
    rRect.SetTop( static_cast<tools::Long>(aRect.origin.y) );
    rRect.SetRight( static_cast<tools::Long>(aRect.origin.x + aRect.size.width - 1) );
    rRect.SetBottom( static_cast<tools::Long>(aRect.origin.y + aRect.size.height - 1) );
}

SalFrame::SalPointerState AquaSalFrame::GetPointerState()
{
    OSX_SALDATA_RUNINMAIN_UNION( GetPointerState(), state )

    SalPointerState state;
    state.mnState = 0;

    // get position
    NSPoint aPt = [mpNSWindow mouseLocationOutsideOfEventStream];
    CocoaToVCL( aPt, false );
    state.maPos = Point(static_cast<tools::Long>(aPt.x), static_cast<tools::Long>(aPt.y));

    NSEvent* pCur = [NSApp currentEvent];
    bool bMouseEvent = false;
    if( pCur )
    {
        bMouseEvent = true;
        switch( [pCur type] )
        {
        case NSEventTypeLeftMouseDown:
            state.mnState |= MOUSE_LEFT;
            break;
        case NSEventTypeLeftMouseUp:
            break;
        case NSEventTypeRightMouseDown:
            state.mnState |= MOUSE_RIGHT;
            break;
        case NSEventTypeRightMouseUp:
            break;
        case NSEventTypeOtherMouseDown:
            state.mnState |= ([pCur buttonNumber] == 2) ? MOUSE_MIDDLE : 0;
            break;
        case NSEventTypeOtherMouseUp:
            break;
        case NSEventTypeMouseMoved:
            break;
        case NSEventTypeLeftMouseDragged:
            state.mnState |= MOUSE_LEFT;
            break;
        case NSEventTypeRightMouseDragged:
            state.mnState |= MOUSE_RIGHT;
            break;
        case NSEventTypeOtherMouseDragged:
            state.mnState |= ([pCur buttonNumber] == 2) ? MOUSE_MIDDLE : 0;
            break;
        default:
            bMouseEvent = false;
            break;
        }
    }
    if( bMouseEvent )
    {
        unsigned int nMask = static_cast<unsigned int>([pCur modifierFlags]);
        if( (nMask & NSEventModifierFlagShift) != 0 )
            state.mnState |= KEY_SHIFT;
        if( (nMask & NSEventModifierFlagControl) != 0 )
            state.mnState |= KEY_MOD3;
        if( (nMask & NSEventModifierFlagOption) != 0 )
            state.mnState |= KEY_MOD2;
        if( (nMask & NSEventModifierFlagCommand) != 0 )
            state.mnState |= KEY_MOD1;

    }
    else
    {
        // FIXME: replace Carbon by Cocoa
        // Cocoa does not have an equivalent for GetCurrentEventButtonState
        // and GetCurrentEventKeyModifiers.
        // we could try to get away with tracking all events for modifierKeys
        // and all mouse events for button state in VCL_NSApplication::sendEvent,
        // but it is unclear whether this will get us the same result.
        // leave in GetCurrentEventButtonState and GetCurrentEventKeyModifiers for now

        // fill in button state
        UInt32 nState = GetCurrentEventButtonState();
        state.mnState = 0;
        if( nState & 1 )
            state.mnState |= MOUSE_LEFT;    // primary button
        if( nState & 2 )
            state.mnState |= MOUSE_RIGHT;   // secondary button
        if( nState & 4 )
            state.mnState |= MOUSE_MIDDLE;  // tertiary button

        // fill in modifier state
        nState = GetCurrentEventKeyModifiers();
        if( nState & shiftKey )
            state.mnState |= KEY_SHIFT;
        if( nState & controlKey )
            state.mnState |= KEY_MOD3;
        if( nState & optionKey )
            state.mnState |= KEY_MOD2;
        if( nState & cmdKey )
            state.mnState |= KEY_MOD1;
    }

    return state;
}

KeyIndicatorState AquaSalFrame::GetIndicatorState()
{
    return KeyIndicatorState::NONE;
}

void AquaSalFrame::SimulateKeyPress( sal_uInt16 /*nKeyCode*/ )
{
}

void AquaSalFrame::SetPluginParent( SystemParentData* )
{
    // plugin parent may be killed unexpectedly by
    // plugging process;

    //TODO: implement
}

bool AquaSalFrame::MapUnicodeToKeyCode( sal_Unicode , LanguageType , vcl::KeyCode& )
{
    // not supported yet
    return false;
}

LanguageType AquaSalFrame::GetInputLanguage()
{
    //TODO: implement
    return LANGUAGE_DONTKNOW;
}

void AquaSalFrame::SetMenu( SalMenu* pSalMenu )
{
    OSX_SALDATA_RUNINMAIN( SetMenu( pSalMenu ) )

    AquaSalMenu* pMenu = static_cast<AquaSalMenu*>(pSalMenu);
    SAL_WARN_IF( pMenu && !pMenu->mbMenuBar, "vcl", "setting non menubar on frame" );
    mpMenu = pMenu;
    if( mpMenu  )
        mpMenu->setMainMenu();
}

void AquaSalFrame::SetExtendedFrameStyle( SalExtStyle nStyle )
{
    if ( !mpNSWindow )
    {
        mnExtStyle = nStyle;
        return;
    }

    OSX_SALDATA_RUNINMAIN( SetExtendedFrameStyle( nStyle ) )

    if( (mnExtStyle & SAL_FRAME_EXT_STYLE_DOCMODIFIED) != (nStyle & SAL_FRAME_EXT_STYLE_DOCMODIFIED) )
        [mpNSWindow setDocumentEdited: (nStyle & SAL_FRAME_EXT_STYLE_DOCMODIFIED) ? YES : NO];

    mnExtStyle = nStyle;
}

SalFrame* AquaSalFrame::GetParent() const
{
    return mpParent;
}

void AquaSalFrame::SetParent( SalFrame* pNewParent )
{
    bool bShown = mbShown;
    // remove from child list
    if (bShown)
        Show(false);
    mpParent = static_cast<AquaSalFrame*>(pNewParent);
    // insert to correct parent and paint
    Show( bShown );
}

void AquaSalFrame::UpdateFrameGeometry()
{
    mbGeometryDidChange = false;

    if ( !mpNSWindow )
        return;

    OSX_SALDATA_RUNINMAIN( UpdateFrameGeometry() )

    // keep in mind that view and window coordinates are lower left
    // whereas vcl's are upper left

    // update screen rect
    NSScreen * pScreen = [mpNSWindow screen];
    if( pScreen )
    {
        NSRect aNewScreenRect = [pScreen frame];
        if (!NSEqualRects(maScreenRect, aNewScreenRect))
        {
            mbGeometryDidChange = true;
            maScreenRect = aNewScreenRect;
        }
        NSArray* pScreens = [NSScreen screens];
        if( pScreens )
        {
            unsigned int nNewDisplayScreenNumber = [pScreens indexOfObject: pScreen];
            if (maGeometry.screen() != nNewDisplayScreenNumber)
            {
                mbGeometryDidChange = true;
                maGeometry.setScreen(nNewDisplayScreenNumber);
            }
        }
    }

    NSRect aFrameRect = [mpNSWindow frame];
    NSRect aContentRect = [mpNSWindow contentRectForFrameRect: aFrameRect];

    NSRect aTrackRect = { NSZeroPoint, aContentRect.size };

    if (!NSEqualRects(maTrackingRect, aTrackRect))
    {
        mbGeometryDidChange = true;
        maTrackingRect = aTrackRect;
    }

    // convert to vcl convention
    CocoaToVCL( aFrameRect );
    CocoaToVCL( aContentRect );

    if (!NSEqualRects(maContentRect, aContentRect) || !NSEqualRects(maFrameRect, aFrameRect))
    {
        mbGeometryDidChange = true;

        maContentRect = aContentRect;
        maFrameRect = aFrameRect;

        maGeometry.setX(static_cast<sal_Int32>(aContentRect.origin.x));
        maGeometry.setY(static_cast<sal_Int32>(aContentRect.origin.y));
        maGeometry.setWidth(static_cast<sal_uInt32>(aContentRect.size.width));
        maGeometry.setHeight(static_cast<sal_uInt32>(aContentRect.size.height));

        maGeometry.setLeftDecoration(static_cast<sal_uInt32>(aContentRect.origin.x - aFrameRect.origin.x));
        maGeometry.setRightDecoration(static_cast<sal_uInt32>((aFrameRect.origin.x + aFrameRect.size.width) -
                                      (aContentRect.origin.x + aContentRect.size.width)));
        maGeometry.setTopDecoration(static_cast<sal_uInt32>(aContentRect.origin.y - aFrameRect.origin.y));
        maGeometry.setBottomDecoration(static_cast<sal_uInt32>((aFrameRect.origin.y + aFrameRect.size.height) -
                                       (aContentRect.origin.y + aContentRect.size.height)));
    }
}

void AquaSalFrame::CaptureMouse( bool bCapture )
{
    /* Remark:
       we'll try to use a pidgin version of capture mouse
       on MacOSX (neither carbon nor cocoa) there is a
       CaptureMouse equivalent (in Carbon there is TrackMouseLocation
       but this is useless to use since it is blocking)

       However on cocoa the active frame seems to get mouse events
       also outside the window, so we'll try to forward mouse events
       to the capture frame in the hope that one of our frames
       gets a mouse event.

       This will break as soon as the user activates another app, but
       a mouse click will normally lead to a release of the mouse anyway.

       Let's see how far we get this way. Alternatively we could use one
       large overlay window like we did for the carbon implementation,
       however that is resource intensive.
    */

    if( bCapture )
        s_pCaptureFrame = this;
    else if( ! bCapture && s_pCaptureFrame == this )
        s_pCaptureFrame = nullptr;
}

void AquaSalFrame::ResetClipRegion()
{
    doResetClipRegion();
}

void AquaSalFrame::doResetClipRegion()
{
    if ( !mpNSWindow )
        return;

    OSX_SALDATA_RUNINMAIN( ResetClipRegion() )

    // release old path and indicate no clipping
    CGPathRelease( mrClippingPath );
    mrClippingPath = nullptr;

    if( mpNSView && mbShown )
        [mpNSView setNeedsDisplay: YES];
    [mpNSWindow setOpaque: YES];
    [mpNSWindow invalidateShadow];
}

void AquaSalFrame::BeginSetClipRegion( sal_uInt32 nRects )
{
    if ( !mpNSWindow )
        return;

    OSX_SALDATA_RUNINMAIN( BeginSetClipRegion( nRects ) )

    // release old path
    if( mrClippingPath )
    {
        CGPathRelease( mrClippingPath );
        mrClippingPath = nullptr;
    }

    if( maClippingRects.size() > SAL_CLIPRECT_COUNT && nRects < maClippingRects.size() )
    {
        std::vector<CGRect>().swap(maClippingRects);
    }
    maClippingRects.clear();
    maClippingRects.reserve( nRects );
}

void AquaSalFrame::UnionClipRegion(
    tools::Long nX, tools::Long nY, tools::Long nWidth, tools::Long nHeight )
{
    // #i113170# may not be the main thread if called from UNO API
    SalData::ensureThreadAutoreleasePool();

    if( nWidth && nHeight )
    {
        NSRect aRect = { { static_cast<CGFloat>(nX), static_cast<CGFloat>(nY) }, { static_cast<CGFloat>(nWidth), static_cast<CGFloat>(nHeight) } };
        VCLToCocoa( aRect, false );
        maClippingRects.push_back( CGRectMake(aRect.origin.x, aRect.origin.y, aRect.size.width, aRect.size.height) );
    }
}

void AquaSalFrame::EndSetClipRegion()
{
    if ( !mpNSWindow )
        return;

    OSX_SALDATA_RUNINMAIN( EndSetClipRegion() )

    if( ! maClippingRects.empty() )
    {
        mrClippingPath = CGPathCreateMutable();
        CGPathAddRects( mrClippingPath, nullptr, maClippingRects.data(), maClippingRects.size() );
    }
    if( mpNSView && mbShown )
        [mpNSView setNeedsDisplay: YES];
    [mpNSWindow setOpaque: (mrClippingPath != nullptr) ? NO : YES];
    [mpNSWindow setBackgroundColor: [NSColor clearColor]];
    // shadow is invalidated when view gets drawn again
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
