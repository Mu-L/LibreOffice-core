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

#include <vcl/themecolors.hxx>
#include <officecfg/Office/Common.hxx>
#include <config_wasm_strip.h>

#include <com/sun/star/accessibility/XAccessible.hpp>
#include <sfx2/viewfrm.hxx>
#include <sfx2/progress.hxx>
#include <svx/srchdlg.hxx>
#include <sfx2/viewsh.hxx>
#include <sfx2/ipclient.hxx>
#include <sal/log.hxx>
#include <drawdoc.hxx>
#include <swwait.hxx>
#include <crsrsh.hxx>
#include <doc.hxx>
#include <IDocumentDeviceAccess.hxx>
#include <IDocumentDrawModelAccess.hxx>
#include <IDocumentOutlineNodes.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <IDocumentState.hxx>
#include <rootfrm.hxx>
#include <pagefrm.hxx>
#include <viewimp.hxx>
#include <frmtool.hxx>
#include <viewopt.hxx>
#include <dview.hxx>
#include <swregion.hxx>
#include <hints.hxx>
#include <docufld.hxx>
#include <txtfrm.hxx>
#include <layact.hxx>
#include <mdiexp.hxx>
#include <fntcache.hxx>
#include <ptqueue.hxx>
#include <docsh.hxx>
#include <bookmark.hxx>
#include <ndole.hxx>
#include <ndindex.hxx>
#include <accmap.hxx>
#include <vcl/bitmapex.hxx>
#include <accessibilityoptions.hxx>
#include <strings.hrc>
#include <bitmaps.hlst>
#include <pagepreviewlayout.hxx>
#include <sortedobjs.hxx>
#include <anchoredobject.hxx>
#include <DocumentSettingManager.hxx>
#include <DocumentRedlineManager.hxx>
#include <DocumentLayoutManager.hxx>

#include <unotxdoc.hxx>
#include <view.hxx>
#include <PostItMgr.hxx>
#include <unotools/configmgr.hxx>
#include <vcl/virdev.hxx>
#include <vcl/svapp.hxx>
#include <svx/sdrpaintwindow.hxx>
#include <svx/sdrpagewindow.hxx>
#include <svx/svdpagv.hxx>
#include <comphelper/lok.hxx>
#include <sfx2/lokhelper.hxx>
#include <tools/UnitConversion.hxx>

#if !HAVE_FEATURE_DESKTOP
#include <vcl/sysdata.hxx>
#endif

#include <frameformats.hxx>
#include <fmtcntnt.hxx>

bool SwViewShell::sbLstAct = false;
ShellResource *SwViewShell::spShellRes = nullptr;

static tools::DeleteOnDeinit<std::shared_ptr<weld::Window>>& getCareDialog()
{
    static tools::DeleteOnDeinit<std::shared_ptr<weld::Window>> spCareDialog {}; ///< Avoid this window.
    return spCareDialog;
}

static bool bInSizeNotify = false;


using namespace ::com::sun::star;

void SwViewShell::SetShowHeaderFooterSeparator( FrameControlType eControl, bool bShow ) {

    //tdf#118621 - Optionally disable floating header/footer menu
    if ( bShow )
        bShow = GetViewOptions()->IsUseHeaderFooterMenu();

    if ( eControl == FrameControlType::Header )
        mbShowHeaderSeparator = bShow;
    else
        mbShowFooterSeparator = bShow;
}

void SwViewShell::ToggleHeaderFooterEdit()
{
    mbHeaderFooterEdit = !mbHeaderFooterEdit;
    if ( !mbHeaderFooterEdit )
    {
        SetShowHeaderFooterSeparator( FrameControlType::Header, false );
        SetShowHeaderFooterSeparator( FrameControlType::Footer, false );
    }

    // Avoid corner case
    if ( ( GetViewOptions()->IsUseHeaderFooterMenu() ) &&
         ( !IsShowHeaderFooterSeparator( FrameControlType::Header ) &&
           !IsShowHeaderFooterSeparator( FrameControlType::Footer ) ) )
    {
        mbHeaderFooterEdit = false;
    }

    InvalidatePageAndHFSubsidiaryLines();
}

// Invalidate Subsidiary Lines around headers/footers and page frames to repaint
void SwViewShell::InvalidatePageAndHFSubsidiaryLines()
{
    RectangleVector aInvalidRects;
    SwPageFrame *pPg = static_cast<SwPageFrame*>(GetLayout()->Lower());
    while (pPg)
    {
        pPg->AddSubsidiaryLinesBounds(*this, aInvalidRects);
        pPg = static_cast<SwPageFrame*>(pPg->GetNext());
    }

    for (const auto &rRect : aInvalidRects)
        GetWin()->Invalidate(rRect);
}

void SwViewShell::setOutputToWindow(bool bOutputToWindow)
{
    mbOutputToWindow = bOutputToWindow;
}

bool SwViewShell::isOutputToWindow() const
{
    return mbOutputToWindow;
}

void SwViewShell::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SwViewShell"));
    if (mpOpt)
    {
        mpOpt->dumpAsXml(pWriter);
    }
    (void)xmlTextWriterEndElement(pWriter);
}

static void
lcl_PaintTransparentFormControls(SwViewShell const & rShell, SwRect const& rRect)
{
    // Direct paint has been performed: the background of transparent child
    // windows has been painted, so need to paint the child windows now.
    if (rShell.GetWin())
    {
        vcl::Window& rWindow = *(rShell.GetWin());
        const tools::Rectangle aRectanglePixel(rShell.GetOut()->LogicToPixel(rRect.SVRect()));
        PaintTransparentChildren(rWindow, aRectanglePixel);
    }
}

// #i72754# 2nd set of Pre/PostPaints
// This time it uses the lock counter (mPrePostPaintRegions empty/non-empty) to allow only one activation
// and deactivation and mpPrePostOutDev to remember the OutDev from the BeginDrawLayers
// call. That way, all places where paint take place can be handled the same way, even
// when calling other paint methods. This is the case at the places where SW paints
// buffered into VDevs to avoid flicker. It is in general problematic and should be
// solved once using the BufferedOutput functionality of the DrawView.

void SwViewShell::PrePaint()
{
    // forward PrePaint event from VCL Window to DrawingLayer
    if(HasDrawView())
    {
        Imp()->GetDrawView()->PrePaint();
    }
}

void SwViewShell::DLPrePaint2(const vcl::Region& rRegion)
{
    if(mPrePostPaintRegions.empty())
    {
        mPrePostPaintRegions.push( rRegion );
        // #i75172# ensure DrawView to use DrawingLayer bufferings
        if ( !HasDrawView() )
            MakeDrawView();

        // Prefer window; if not available, get mpOut (e.g. printer)
        const bool bWindow = GetWin() && !comphelper::LibreOfficeKit::isActive() && !isOutputToWindow();
        mpPrePostOutDev = bWindow ? GetWin()->GetOutDev() : GetOut();

        // #i74769# use SdrPaintWindow now direct
        mpTargetPaintWindow = Imp()->GetDrawView()->BeginDrawLayers(mpPrePostOutDev, rRegion);
        assert(mpTargetPaintWindow && "BeginDrawLayers: Got no SdrPaintWindow (!)");

        // #i74769# if prerender, save OutDev and redirect to PreRenderDevice
        if(mpTargetPaintWindow->GetPreRenderDevice())
        {
            mpBufferedOut = mpOut;
            mpOut = &(mpTargetPaintWindow->GetTargetOutputDevice());
        }
        else if (isOutputToWindow())
            // In case mpOut is used without buffering and we're not printing, need to set clipping.
            mpOut->SetClipRegion(rRegion);

        // remember original paint MapMode for wrapped FlyFrame paints
        maPrePostMapMode = mpOut->GetMapMode();
    }
    else
    {
        // region needs to be updated to the given one
        if( mPrePostPaintRegions.top() != rRegion )
            Imp()->GetDrawView()->UpdateDrawLayersRegion(mpPrePostOutDev, rRegion);
        mPrePostPaintRegions.push( rRegion );
    }
}

void SwViewShell::DLPostPaint2(bool bPaintFormLayer)
{
    OSL_ENSURE(!mPrePostPaintRegions.empty(), "SwViewShell::DLPostPaint2: Pre/PostPaint encapsulation broken (!)");

    if( mPrePostPaintRegions.size() > 1 )
    {
        vcl::Region current = std::move(mPrePostPaintRegions.top());
        mPrePostPaintRegions.pop();
        if( current != mPrePostPaintRegions.top())
            Imp()->GetDrawView()->UpdateDrawLayersRegion(mpPrePostOutDev, mPrePostPaintRegions.top());
        return;
    }
    mPrePostPaintRegions.pop(); // clear
    if(nullptr != mpTargetPaintWindow)
    {
        // #i74769# restore buffered OutDev
        if(mpTargetPaintWindow->GetPreRenderDevice())
        {
            mpOut = mpBufferedOut;
        }

        // #i74769# use SdrPaintWindow now direct
        SwViewObjectContactRedirector aSwRedirector(*this);
        Imp()->GetDrawView()->EndDrawLayers(*mpTargetPaintWindow, bPaintFormLayer, &aSwRedirector);
        mpTargetPaintWindow = nullptr;
    }
}
// end of Pre/PostPaints

void SwViewShell::StartAllAction()
{
    for (SwViewShell & rCurrentShell : GetRingContainer())
    {
        rCurrentShell.StartAction();
    }
}

void SwViewShell::EndAllAction()
{
    for (SwViewShell & rCurrentShell : GetRingContainer())
    {
        rCurrentShell.EndAction();
    }
}

void SwViewShell::StartAction()
{
    if (!mnStartAction++)
        ImplStartAction();
}

void SwViewShell::EndAction(const bool bIdleEnd)
{
    if (0 == (mnStartAction - 1))
        ImplEndAction(bIdleEnd);
    --mnStartAction;
}

void SwViewShell::ImplEndAction( const bool bIdleEnd )
{
    // Nothing to do for the printer?
    if ( !GetWin() || IsPreview() )
    {
        mbPaintWorks = true;
        UISizeNotify();
        // tdf#101464 print preview may generate events if another view shell
        // performs layout...
#if !ENABLE_WASM_STRIP_ACCESSIBILITY
        if (IsPreview() && Imp()->IsAccessible())
        {
            Imp()->FireAccessibleEvents();
        }
#endif
        return;
    }

    mbInEndAction = true;
    //will this put the EndAction of the last shell in the sequence?

    SwViewShell::sbLstAct = true;
    for(SwViewShell& rShell : GetRingContainer())
    {
        if(&rShell != this && rShell.ActionPend())
        {
            SwViewShell::sbLstAct = false;
            break;
        }
    }

    const bool bIsShellForCheckViewLayout = ( this == GetLayout()->GetCurrShell() );

    CurrShell aCurr( this );
    if ( Imp()->HasDrawView() && !Imp()->GetDrawView()->areMarkHandlesHidden() )
        Imp()->StartAction();

    if ( Imp()->HasPaintRegion() && Imp()->GetPaintRegion()->GetOrigin() != VisArea() )
        Imp()->DeletePaintRegion();

    const bool bExtraData = ::IsExtraData( *GetDoc() );

    if ( !bIdleEnd )
    {
        SwLayAction aAction( GetLayout(), Imp() );
        aAction.SetComplete( false );
        if ( mnLockPaint )
            aAction.SetPaint( false );
        aAction.SetInputType( VclInputFlags::KEYBOARD );
        aAction.Action(GetWin()->GetOutDev());
    }

    if ( bIsShellForCheckViewLayout )
        GetLayout()->CheckViewLayout( GetViewOptions(), &maVisArea );

    //If we don't call Paints, we wait for the Paint of the system.
    //Then the clipping is set correctly; e.g. shifting of a Draw object
    if ( Imp()->HasPaintRegion()     ||
         maInvalidRect.HasArea() ||
         bExtraData )
    {
        if ( !mnLockPaint )
        {
            SolarMutexGuard aGuard;

            bool bPaintsFromSystem = maInvalidRect.HasArea();
            GetWin()->PaintImmediately();
            if ( maInvalidRect.HasArea() )
            {
                if ( bPaintsFromSystem )
                    Imp()->AddPaintRect( maInvalidRect );

                ResetInvalidRect();
                bPaintsFromSystem = true;
            }
            mbPaintWorks = true;

            std::optional<SwRegionRects> oRegion = Imp()->TakePaintRegion();

            //JP 27.11.97: what hid the selection, must also Show it,
            //             else we get Paint errors!
            // e.g. additional mode, page half visible vertically, in the
            // middle a selection and with another cursor jump to left
            // right border. Without ShowCursor the selection disappears.
            bool bShowCursor = oRegion && dynamic_cast<const SwCursorShell*>(this) !=  nullptr;
            if( bShowCursor )
                static_cast<SwCursorShell*>(this)->HideCursors();

            if ( oRegion )
            {
                SwRootFrame* pCurrentLayout = GetLayout();

                oRegion->LimitToOrigin();
                oRegion->Compress( SwRegionRects::CompressFuzzy );

                while ( !oRegion->empty() )
                {
                    SwRect aRect( oRegion->back() );
                    oRegion->pop_back();

                    if (GetWin()->SupportsDoubleBuffering())
                        InvalidateWindows(aRect);
                    else
                    {
                        // #i75172# begin DrawingLayer paint
                        // need to do begin/end DrawingLayer preparation for each single rectangle of the
                        // repaint region. I already tried to prepare only once for the whole Region. This
                        // seems to work (and does technically) but fails with transparent objects. Since the
                        // region given to BeginDrawLayers() defines the clip region for DrawingLayer paint,
                        // transparent objects in the single rectangles will indeed be painted multiple times.
                        if (!comphelper::LibreOfficeKit::isActive())
                        {
                            DLPrePaint2(vcl::Region(aRect.SVRect()));
                        }

                        if ( bPaintsFromSystem )
                            PaintDesktop(*GetOut(), aRect);
                        if (!comphelper::LibreOfficeKit::isActive())
                            pCurrentLayout->PaintSwFrame( *mpOut, aRect );
                        else
                            pCurrentLayout->GetCurrShell()->InvalidateWindows(aRect);

                        // #i75172# end DrawingLayer paint
                        if (!comphelper::LibreOfficeKit::isActive())
                        {
                            DLPostPaint2(true);
                        }
                    }

                    lcl_PaintTransparentFormControls(*this, aRect); // i#107365
                }
            }
            if( bShowCursor )
                static_cast<SwCursorShell*>(this)->ShowCursors( true );
        }
        else
        {
            Imp()->DeletePaintRegion();
            mbPaintWorks =  true;
        }
    }
    else
        mbPaintWorks = true;

    mbInEndAction = false;
    SwViewShell::sbLstAct = false;
    Imp()->EndAction();

    //We artificially end the action here to enable the automatic scrollbars
    //to adjust themselves correctly
    //EndAction sends a Notify, and that must call Start-/EndAction to
    //adjust the scrollbars correctly
    --mnStartAction;
    UISizeNotify();
    ++mnStartAction;

#if !ENABLE_WASM_STRIP_ACCESSIBILITY
    if( Imp()->IsAccessible() )
        Imp()->FireAccessibleEvents();
#endif
}

void SwViewShell::ImplStartAction()
{
    mbPaintWorks = false;
    Imp()->StartAction();
}

void SwViewShell::ImplLockPaint()
{
    if ( GetWin() && GetWin()->IsVisible() && !comphelper::LibreOfficeKit::isActive())
        GetWin()->EnablePaint( false ); //Also cut off the controls.
    Imp()->LockPaint();
}

void SwViewShell::ImplUnlockPaint(std::vector<LockPaintReason>& rReasons, bool bVirDev)
{
    CurrShell aCurr( this );
    if ( GetWin() && GetWin()->IsVisible() )
    {
        if ( (bInSizeNotify || bVirDev ) && VisArea().HasArea() && !comphelper::LibreOfficeKit::isActive())
        {
            //Refresh with virtual device to avoid flickering.
            VclPtrInstance<VirtualDevice> pVout( *mpOut );
            pVout->SetMapMode( mpOut->GetMapMode() );
            Size aSize( VisArea().SSize() );
            aSize.AdjustWidth(20 );
            aSize.AdjustHeight(20 );
            if( pVout->SetOutputSize( aSize ) )
            {
                GetWin()->EnablePaint( true );
                GetWin()->Validate();

                Imp()->UnlockPaint();
                if (mpOut->IsLineColor())
                    pVout->SetLineColor( mpOut->GetLineColor() );
                else
                    pVout->SetLineColor();
                if (mpOut->IsFillColor())
                    pVout->SetFillColor( mpOut->GetFillColor() );
                else
                    pVout->SetFillColor();

                // #i72754# start Pre/PostPaint encapsulation before mpOut is changed to the buffering VDev
                const vcl::Region aRepaintRegion(VisArea().SVRect());
                DLPrePaint2(aRepaintRegion);

                OutputDevice *pOld = mpOut;
                mpOut = pVout.get();
                Paint(*mpOut, VisArea().SVRect());
                mpOut = pOld;
                mpOut->DrawOutDev( VisArea().Pos(), aSize,
                                  VisArea().Pos(), aSize, *pVout );

                // #i72754# end Pre/PostPaint encapsulation when mpOut is back and content is painted
                DLPostPaint2(true);

                lcl_PaintTransparentFormControls(*this, VisArea()); // fdo#63949
            }
            else
            {
                Imp()->UnlockPaint();
                GetWin()->EnablePaint( true );
                InvalidateAll(rReasons);
            }
            pVout.disposeAndClear();
        }
        else
        {
            Imp()->UnlockPaint();
            GetWin()->EnablePaint( true );
            InvalidateAll(rReasons);
        }
    }
    else
        Imp()->UnlockPaint();
}

namespace
{
    std::string_view to_string(LockPaintReason eReason)
    {
        switch(eReason)
        {
            case LockPaintReason::ViewLayout:
                return "ViewLayout";
            case LockPaintReason::OuterResize:
                return "OuterResize";
            case LockPaintReason::Undo:
                return "Undo";
            case LockPaintReason::Redo:
                return "Redo";
            case LockPaintReason::OutlineFolding:
                return "OutlineFolding";
            case LockPaintReason::EndSdrCreate:
                return "EndSdrCreate";
            case LockPaintReason::SwLayIdle:
                return "SwLayIdle";
            case LockPaintReason::InvalidateLayout:
                return "InvalidateLayout";
            case LockPaintReason::StartDrag:
                return "StartDrag";
            case LockPaintReason::DataChanged:
                return "DataChanged";
            case LockPaintReason::InsertFrame:
                return "InsertFrame";
            case LockPaintReason::GotoPage:
                return "GotoPage";
            case LockPaintReason::InsertGraphic:
                return "InsertGraphic";
            case LockPaintReason::SetZoom:
                return "SetZoom";
            case LockPaintReason::ExampleFrame:
                return "ExampleFram";
        }
        return "";
    };
}

void SwViewShell::InvalidateAll(std::vector<LockPaintReason>& rReasons)
{
    assert(!rReasons.empty() && "there must be a reason to InvalidateAll");

    for (const auto& reason : rReasons)
        SAL_INFO("sw.core", "InvalidateAll because of: " << to_string(reason));

    if (comphelper::LibreOfficeKit::isActive())
    {
        // https://github.com/CollaboraOnline/online/issues/6379
        // ditch OuterResize as a reason to invalidate all in the online case
        std::erase(rReasons, LockPaintReason::OuterResize);
    }

    if (!rReasons.empty())
        GetWin()->Invalidate(InvalidateFlags::Children);
    rReasons.clear();
}

bool SwViewShell::AddPaintRect( const SwRect & rRect )
{
    bool bRet = false;
    for(SwViewShell& rSh : GetRingContainer())
    {
        if( rSh.Imp() )
        {
            if ( rSh.IsPreview() && rSh.GetWin() )
                ::RepaintPagePreview( &rSh, rRect );
            else
                bRet |= rSh.Imp()->AddPaintRect( rRect );
        }
    }
    return bRet;
}

void SwViewShell::InvalidateWindows( const SwRect &rRect )
{
    if ( Imp()->IsCalcLayoutProgress() )
        return;

    if(comphelper::LibreOfficeKit::isActive())
    {
        // If we are inside tiled painting, invalidations are ignored.
        // Ignore them right now to save work, but also to avoid the problem
        // that this state could be reset before FlushPendingLOKInvalidateTiles()
        // gets called.
        if(comphelper::LibreOfficeKit::isTiledPainting())
            return;
        // First collect all invalidations and perform them only later,
        // otherwise the number of Invalidate() calls would be at least
        // O(n^2) if not worse. The problem is that if any change in a document
        // is made, SwEditShell::EndAllAction() is called, which calls EndAction()
        // for every view. And every view does it own handling of paint rectangles,
        // and then calls InvalidateWindows() based on that. On then this code
        // would call Invalidate() for all views for each rectangle.
        // So collect the rectangles, avoid duplicates (which there usually will
        // be many because of the repetitions), FlushPendingLOKInvalidateTiles()
        // will collect all rectangles from all related views, compress them
        // and only with those relatively few rectangle it'd call Invalidate()
        // for all views.
        Imp()->AddPendingLOKInvalidation(rRect);
        return;
    }

    for(SwViewShell& rSh : GetRingContainer())
    {
        if ( rSh.GetWin() )
        {
            if ( rSh.IsPreview() )
                ::RepaintPagePreview( &rSh, rRect );
            // In case of tiled rendering, invalidation is wanted even if
            // the rectangle is outside the visual area.
            else if ( rSh.VisArea().Overlaps( rRect ) || comphelper::LibreOfficeKit::isActive() )
                rSh.GetWin()->Invalidate( rRect.SVRect() );
        }
    }
}

void SwViewShell::FlushPendingLOKInvalidateTiles()
{
    assert(comphelper::LibreOfficeKit::isActive());
    SwRegionRects rects;
    for(SwViewShell& rSh : GetRingContainer())
    {
        std::vector<SwRect> tmpRects = rSh.Imp()->TakePendingLOKInvalidations();
        rects.insert( rects.end(), tmpRects.begin(), tmpRects.end());
    }
    rects.Compress( SwRegionRects::CompressFuzzy );
    if(rects.empty())
        return;
    // This is basically the loop from SwViewShell::InvalidateWindows().
    for(SwViewShell& rSh : GetRingContainer())
    {
        if ( rSh.GetWin() )
        {
            if ( rSh.IsPreview() )
            {
                for( const SwRect& rect : rects )
                    ::RepaintPagePreview( &rSh, rect );
            }
            else
            {
                for( const SwRect& rect : rects )
                    rSh.GetWin()->Invalidate( rect.SVRect() );
            }
        }
    }
}

const SwRect& SwViewShell::VisArea() const
{
    // when using the tiled rendering, consider the entire document as our
    // visible area
    return comphelper::LibreOfficeKit::isActive()? GetLayout()->getFrameArea(): maVisArea;
}

void SwViewShell::MakeVisible( const SwRect &rRect, ScrollSizeMode eScrollSizeMode )
{
    if ( !(!VisArea().Contains( rRect ) || IsScrollMDI( *this, rRect ) || GetCareDialog(*this)) )
        return;

    if ( IsViewLocked() )
        return;

    if( mpWin )
    {
        const SwFrame* pRoot = GetLayout();
        int nLoopCnt = 3;
        tools::Long nOldH;
        do{
            nOldH = pRoot->getFrameArea().Height();
            SwViewShell::StartAction();
            ScrollMDI( *this, rRect, USHRT_MAX, USHRT_MAX, eScrollSizeMode );
            SwViewShell::EndAction(); // DO NOT call virtual here!
        } while( nOldH != pRoot->getFrameArea().Height() && nLoopCnt-- );
    }
#if OSL_DEBUG_LEVEL > 0
    else
    {
        //MA: 04. Nov. 94, no one needs this, does one?
        OSL_ENSURE( false, "Is MakeVisible still needed for printers?" );
    }

#endif
}

weld::Window* SwViewShell::CareChildWin(SwViewShell const & rVSh)
{
    if (!rVSh.mpSfxViewShell)
        return nullptr;
#if HAVE_FEATURE_DESKTOP
    const sal_uInt16 nId = SvxSearchDialogWrapper::GetChildWindowId();
    SfxViewFrame& rVFrame = rVSh.mpSfxViewShell->GetViewFrame();
    SfxChildWindow* pChWin = rVFrame.GetChildWindow( nId );
    if (!pChWin)
        return nullptr;
    weld::DialogController* pController = pChWin->GetController().get();
    if (!pController)
        return nullptr;
    weld::Window* pWin = pController->getDialog();
    if (pWin && pWin->get_visible())
        return pWin;
#endif
    return nullptr;
}

Point SwViewShell::GetPagePos( sal_uInt16 nPageNum ) const
{
    return GetLayout()->GetPagePos( nPageNum );
}

sal_uInt16 SwViewShell::GetNumPages() const
{
    //It is possible that no layout exists when the method from
    //root-Ctor is called.
    return GetLayout() ? GetLayout()->GetPageNum() : 0;
}

bool SwViewShell::IsDummyPage( sal_uInt16 nPageNum ) const
{
    return GetLayout() && GetLayout()->IsDummyPage( nPageNum );
}

/**
 * Forces update of each field.
 * It notifies all fields with pNewHt. If that is 0 (default), the field
 * type is sent (???).
 * @param[in] bCloseDB Passed in to GetDoc()->UpdateFields. [TODO] Purpose???
 */
void SwViewShell::UpdateFields(bool bCloseDB, bool bSetModified)
{
    CurrShell aCurr( this );

    StartAction();

    GetDoc()->getIDocumentFieldsAccess().UpdateFields(bCloseDB, bSetModified);

    EndAction();
}

void SwViewShell::UpdateOleObjectPreviews()
{
    SwDoc* pDoc = GetDoc();
    for(sw::SpzFrameFormat* pFormat: *pDoc->GetSpzFrameFormats())
    {
        if (pFormat->Which() != RES_FLYFRMFMT)
        {
            continue;
        }

        const SwNodeIndex* pNodeIndex = pFormat->GetContent().GetContentIdx();
        if (!pNodeIndex || !pNodeIndex->GetNodes().IsDocNodes())
        {
            continue;
        }

        SwNode* pNode = pDoc->GetNodes()[pNodeIndex->GetIndex() + 1];
        SwOLENode* pOleNode = pNode->GetOLENode();
        if (!pOleNode)
        {
            continue;
        }

        SwOLEObj& rOleObj = pOleNode->GetOLEObj();
        svt::EmbeddedObjectRef& rObject = rOleObj.GetObject();
        rObject.UpdateReplacement( true );
        // Trigger the repaint.
        pOleNode->SetChanged();
    }
}

/** update all charts for which any table exists */
void SwViewShell::UpdateAllCharts()
{
    CurrShell aCurr( this );
    // Start-/EndAction handled in the SwDoc-Method!
    GetDoc()->UpdateAllCharts();
}

bool SwViewShell::HasCharts() const
{
    bool bRet = false;
    SwNodeIndex aIdx( *GetDoc()->GetNodes().GetEndOfAutotext().
                        StartOfSectionNode(), 1 );
    while (aIdx.GetNode().GetStartNode())
    {
        ++aIdx;
        const SwOLENode *pNd = aIdx.GetNode().GetOLENode();
        if( pNd && !pNd->GetChartTableName().isEmpty() )
        {
            bRet = true;
            break;
        }
    }
    return bRet;
}

void SwViewShell::LayoutIdle()
{
    if( !mpOpt->IsIdle() || !GetWin() || HasDrawViewDrag() )
        return;

    //No idle when printing is going on.
    for(const SwViewShell& rSh : GetRingContainer())
    {
        if ( !rSh.GetWin() )
            return;
    }

    CurrShell aCurr( this );

#ifdef DBG_UTIL
    // If Test5 has been set, the IdleFormatter is disabled.
    if( mpOpt->IsTest5() )
        return;
#endif

    {
        // Preserve top of the text frame cache.
        SwSaveSetLRUOfst aSaveLRU;
        // #125243# there are lots of stacktraces indicating that Imp() returns NULL
        // this SwViewShell seems to be invalid - but it's not clear why
        // this return is only a workaround!
        OSL_ENSURE(Imp(), "SwViewShell already deleted?");
        if(!Imp())
            return;
        SwLayIdle aIdle( GetLayout(), Imp() );
    }
}

static void lcl_InvalidateAllContent( SwViewShell& rSh, SwInvalidateFlags nInv )
{
    rSh.StartAction();
    rSh.GetLayout()->InvalidateAllContent( nInv );
    rSh.EndAction();

    rSh.GetDoc()->getIDocumentState().SetModified();
}

/** local method to invalidate/re-calculate positions of floating screen
 *  objects (Writer fly frame and drawing objects), which are anchored
 *  to paragraph or to character. #i11860#
 */
static void lcl_InvalidateAllObjPos( SwViewShell &_rSh )
{
    _rSh.StartAction();

    _rSh.GetLayout()->InvalidateAllObjPos();

    _rSh.EndAction();

    _rSh.GetDoc()->getIDocumentState().SetModified();
}

void SwViewShell::SetParaSpaceMax( bool bNew )
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if( rIDSA.get(DocumentSettingId::PARA_SPACE_MAX) != bNew )
    {
        SwWait aWait( *GetDoc()->GetDocShell(), true );
        rIDSA.set(DocumentSettingId::PARA_SPACE_MAX, bNew );
        const SwInvalidateFlags nInv = SwInvalidateFlags::PrtArea | SwInvalidateFlags::Table | SwInvalidateFlags::Section;
        lcl_InvalidateAllContent( *this,  nInv );
    }
}

void SwViewShell::SetParaSpaceMaxAtPages( bool bNew )
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if( rIDSA.get(DocumentSettingId::PARA_SPACE_MAX_AT_PAGES) != bNew )
    {
        SwWait aWait( *GetDoc()->GetDocShell(), true );
        rIDSA.set(DocumentSettingId::PARA_SPACE_MAX_AT_PAGES, bNew );
        const SwInvalidateFlags nInv = SwInvalidateFlags::PrtArea | SwInvalidateFlags::Table | SwInvalidateFlags::Section;
        lcl_InvalidateAllContent( *this,  nInv );
    }
}

void SwViewShell::SetTabCompat( bool bNew )
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if( rIDSA.get(DocumentSettingId::TAB_COMPAT) != bNew  )
    {
        SwWait aWait( *GetDoc()->GetDocShell(), true );
        rIDSA.set(DocumentSettingId::TAB_COMPAT, bNew );
        const SwInvalidateFlags nInv = SwInvalidateFlags::PrtArea | SwInvalidateFlags::Size | SwInvalidateFlags::Table | SwInvalidateFlags::Section;
        lcl_InvalidateAllContent( *this, nInv );
    }
}

void SwViewShell::SetAddExtLeading( bool bNew )
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if ( rIDSA.get(DocumentSettingId::ADD_EXT_LEADING) != bNew )
    {
        SwWait aWait( *GetDoc()->GetDocShell(), true );
        rIDSA.set(DocumentSettingId::ADD_EXT_LEADING, bNew );
        SwDrawModel* pTmpDrawModel = getIDocumentDrawModelAccess().GetDrawModel();
        if ( pTmpDrawModel )
            pTmpDrawModel->SetAddExtLeading( bNew );
        const SwInvalidateFlags nInv = SwInvalidateFlags::PrtArea | SwInvalidateFlags::Size | SwInvalidateFlags::Table | SwInvalidateFlags::Section;
        lcl_InvalidateAllContent( *this, nInv );
    }
}

/** Sets if paragraph and table spacing is added at bottom of table cells.
 * #106629#
 * @param[in] (bool) setting of the new value
 */
void SwViewShell::SetAddParaSpacingToTableCells( bool _bAddParaSpacingToTableCells )
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::ADD_PARA_SPACING_TO_TABLE_CELLS) != _bAddParaSpacingToTableCells
        || rIDSA.get(DocumentSettingId::ADD_PARA_LINE_SPACING_TO_TABLE_CELLS) != _bAddParaSpacingToTableCells)
    {
        SwWait aWait( *GetDoc()->GetDocShell(), true );
        rIDSA.set(DocumentSettingId::ADD_PARA_SPACING_TO_TABLE_CELLS, _bAddParaSpacingToTableCells );
        // note: the dialog can't change the value to indeterminate, so only false/false and true/true
        rIDSA.set(DocumentSettingId::ADD_PARA_LINE_SPACING_TO_TABLE_CELLS, _bAddParaSpacingToTableCells );
        const SwInvalidateFlags nInv = SwInvalidateFlags::PrtArea;
        lcl_InvalidateAllContent( *this, nInv );
    }
}

/**
 * Sets if former formatting of text lines with proportional line spacing should used.
 * #i11859#
 * @param[in] (bool) setting of the new value
 */
void SwViewShell::SetUseFormerLineSpacing( bool _bUseFormerLineSpacing )
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if ( rIDSA.get(DocumentSettingId::OLD_LINE_SPACING) != _bUseFormerLineSpacing )
    {
        SwWait aWait( *GetDoc()->GetDocShell(), true );
        rIDSA.set(DocumentSettingId::OLD_LINE_SPACING, _bUseFormerLineSpacing );
        const SwInvalidateFlags nInv = SwInvalidateFlags::PrtArea;
        lcl_InvalidateAllContent( *this, nInv );
    }
}

/**
 * Sets IDocumentSettingAccess if former object positioning should be used.
 * #i11860#
 * @param[in] (bool) setting the new value
 */
void SwViewShell::SetUseFormerObjectPositioning( bool _bUseFormerObjPos )
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if ( rIDSA.get(DocumentSettingId::USE_FORMER_OBJECT_POS) != _bUseFormerObjPos )
    {
        SwWait aWait( *GetDoc()->GetDocShell(), true );
        rIDSA.set(DocumentSettingId::USE_FORMER_OBJECT_POS, _bUseFormerObjPos );
        lcl_InvalidateAllObjPos( *this );
    }
}

// #i28701#
void SwViewShell::SetConsiderWrapOnObjPos( bool _bConsiderWrapOnObjPos )
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if ( rIDSA.get(DocumentSettingId::CONSIDER_WRAP_ON_OBJECT_POSITION) != _bConsiderWrapOnObjPos )
    {
        SwWait aWait( *GetDoc()->GetDocShell(), true );
        rIDSA.set(DocumentSettingId::CONSIDER_WRAP_ON_OBJECT_POSITION, _bConsiderWrapOnObjPos );
        lcl_InvalidateAllObjPos( *this );
    }
}

void SwViewShell::SetUseFormerTextWrapping( bool _bUseFormerTextWrapping )
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if ( rIDSA.get(DocumentSettingId::USE_FORMER_TEXT_WRAPPING) != _bUseFormerTextWrapping )
    {
        SwWait aWait( *GetDoc()->GetDocShell(), true );
        rIDSA.set(DocumentSettingId::USE_FORMER_TEXT_WRAPPING, _bUseFormerTextWrapping );
        const SwInvalidateFlags nInv = SwInvalidateFlags::PrtArea | SwInvalidateFlags::Size | SwInvalidateFlags::Table | SwInvalidateFlags::Section;
        lcl_InvalidateAllContent( *this, nInv );
    }
}

// #i45491#
void SwViewShell::SetDoNotJustifyLinesWithManualBreak( bool _bDoNotJustifyLinesWithManualBreak )
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if ( rIDSA.get(DocumentSettingId::DO_NOT_JUSTIFY_LINES_WITH_MANUAL_BREAK) != _bDoNotJustifyLinesWithManualBreak )
    {
        SwWait aWait( *GetDoc()->GetDocShell(), true );
        rIDSA.set(DocumentSettingId::DO_NOT_JUSTIFY_LINES_WITH_MANUAL_BREAK, _bDoNotJustifyLinesWithManualBreak );
        const SwInvalidateFlags nInv = SwInvalidateFlags::PrtArea | SwInvalidateFlags::Size | SwInvalidateFlags::Table | SwInvalidateFlags::Section;
        lcl_InvalidateAllContent( *this, nInv );
    }
}

void SwViewShell::SetProtectForm( bool _bProtectForm )
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    rIDSA.set(DocumentSettingId::PROTECT_FORM, _bProtectForm );
}

void SwViewShell::SetMsWordCompTrailingBlanks( bool _bMsWordCompTrailingBlanks )
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::MS_WORD_COMP_TRAILING_BLANKS) != _bMsWordCompTrailingBlanks)
    {
        SwWait aWait(*GetDoc()->GetDocShell(), true);
        rIDSA.set(DocumentSettingId::MS_WORD_COMP_TRAILING_BLANKS, _bMsWordCompTrailingBlanks);
        const SwInvalidateFlags nInv = SwInvalidateFlags::PrtArea | SwInvalidateFlags::Size | SwInvalidateFlags::Table | SwInvalidateFlags::Section;
        lcl_InvalidateAllContent(*this, nInv);
    }
}

void SwViewShell::SetSubtractFlysAnchoredAtFlys(bool bSubtractFlysAnchoredAtFlys)
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    rIDSA.set(DocumentSettingId::SUBTRACT_FLYS, bSubtractFlysAnchoredAtFlys);
}

void SwViewShell::SetEmptyDbFieldHidesPara(bool bEmptyDbFieldHidesPara)
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::EMPTY_DB_FIELD_HIDES_PARA) == bEmptyDbFieldHidesPara)
        return;

    SwWait aWait(*GetDoc()->GetDocShell(), true);
    rIDSA.set(DocumentSettingId::EMPTY_DB_FIELD_HIDES_PARA, bEmptyDbFieldHidesPara);
    SwViewShell::StartAction();
    GetDoc()->getIDocumentState().SetModified();
    for (auto const & pFieldType : *GetDoc()->getIDocumentFieldsAccess().GetFieldTypes())
    {
        if(pFieldType->Which() == SwFieldIds::Database)
            pFieldType->UpdateFields();
    }
    SwViewShell::EndAction();
}

void SwViewShell::SetNoGapAfterNoteNumber(bool bNew)
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::NO_GAP_AFTER_NOTE_NUMBER) != bNew)
    {
        SwWait aWait(*GetDoc()->GetDocShell(), true);
        rIDSA.set(DocumentSettingId::NO_GAP_AFTER_NOTE_NUMBER, bNew);
        const SwInvalidateFlags nInv = SwInvalidateFlags::Size | SwInvalidateFlags::Pos | SwInvalidateFlags::PrtArea;
        lcl_InvalidateAllContent(*this, nInv);
    }
}

void SwViewShell::SetTabsRelativeToIndent(bool bNew)
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::TABS_RELATIVE_TO_INDENT) != bNew)
    {
        SwWait aWait(*GetDoc()->GetDocShell(), true);
        rIDSA.set(DocumentSettingId::TABS_RELATIVE_TO_INDENT, bNew);
        const SwInvalidateFlags nInv = SwInvalidateFlags::Size | SwInvalidateFlags::Section | SwInvalidateFlags::PrtArea | SwInvalidateFlags::Table | SwInvalidateFlags::Pos;
        lcl_InvalidateAllContent(*this, nInv);
    }
}

void SwViewShell::SetTabOverMargin(bool bNew)
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::TAB_OVER_MARGIN) != bNew)
    {
        SwWait aWait(*GetDoc()->GetDocShell(), true);
        rIDSA.set(DocumentSettingId::TAB_OVER_MARGIN, bNew);
        const SwInvalidateFlags nInv = SwInvalidateFlags::Size | SwInvalidateFlags::Section | SwInvalidateFlags::PrtArea | SwInvalidateFlags::Table | SwInvalidateFlags::Pos;
        lcl_InvalidateAllContent(*this, nInv);
    }
}

void SwViewShell::SetDoNotMirrorRtlDrawObjs(bool bDoNotMirrorRtlDrawObjs)
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::DO_NOT_MIRROR_RTL_DRAW_OBJS) != bDoNotMirrorRtlDrawObjs)
    {
        SwWait aWait(*GetDoc()->GetDocShell(), true);
        rIDSA.set(DocumentSettingId::DO_NOT_MIRROR_RTL_DRAW_OBJS, bDoNotMirrorRtlDrawObjs);
        lcl_InvalidateAllObjPos(*this);
    }
}

void SwViewShell::SetContinuousEndnotes(bool bContinuousEndnotes)
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::CONTINUOUS_ENDNOTES) != bContinuousEndnotes)
    {
        SwWait aWait(*GetDoc()->GetDocShell(), true);
        rIDSA.set(DocumentSettingId::CONTINUOUS_ENDNOTES, bContinuousEndnotes);
        SwViewShell::StartAction();
        GetLayout()->RemoveFootnotes(/*pPage=*/nullptr, /*pPageOnly=*/false, /*bEndNotes=*/true);
        SwViewShell::EndAction();
        GetDoc()->getIDocumentState().SetModified();
    }
}

void SwViewShell::SetMsWordCompGridMetrics(bool _bMsWordCompGridMetrics)
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::MS_WORD_COMP_GRID_METRICS) != _bMsWordCompGridMetrics)
    {
        SwWait aWait(*GetDoc()->GetDocShell(), true);
        rIDSA.set(DocumentSettingId::MS_WORD_COMP_GRID_METRICS, _bMsWordCompGridMetrics);
        const SwInvalidateFlags nInv = SwInvalidateFlags::PrtArea | SwInvalidateFlags::Pos
                                       | SwInvalidateFlags::Size | SwInvalidateFlags::Table
                                       | SwInvalidateFlags::Section;
        lcl_InvalidateAllContent(*this, nInv);
    }
}

void SwViewShell::SetIgnoreTabsAndBlanksForLineCalculation(bool val)
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::IGNORE_TABS_AND_BLANKS_FOR_LINE_CALCULATION) != val)
    {
        SwWait aWait(*GetDoc()->GetDocShell(), true);
        rIDSA.set(DocumentSettingId::IGNORE_TABS_AND_BLANKS_FOR_LINE_CALCULATION, val);
        const SwInvalidateFlags nInv = SwInvalidateFlags::Size | SwInvalidateFlags::Section
                                        | SwInvalidateFlags::PrtArea | SwInvalidateFlags::Table
                                        | SwInvalidateFlags::Pos;
        lcl_InvalidateAllContent(*this, nInv);
    }
}

void SwViewShell::SetMsWordUlTrailSpace(bool val)
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::MS_WORD_UL_TRAIL_SPACE) != val)
    {
        SwWait aWait(*GetDoc()->GetDocShell(), true);
        rIDSA.set(DocumentSettingId::MS_WORD_UL_TRAIL_SPACE, val);
        const SwInvalidateFlags nInv = SwInvalidateFlags::Size;
        lcl_InvalidateAllContent(*this, nInv);
    }
}

void SwViewShell::SetBalanceSpacesAndIdeographicSpaces(bool bValue)
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::BALANCE_SPACES_AND_IDEOGRAPHIC_SPACES) != bValue)
    {
        SwWait aWait(*GetDoc()->GetDocShell(), true);
        rIDSA.set(DocumentSettingId::BALANCE_SPACES_AND_IDEOGRAPHIC_SPACES, bValue);
        const SwInvalidateFlags nInv
            = SwInvalidateFlags::Size | SwInvalidateFlags::Pos | SwInvalidateFlags::PrtArea;
        lcl_InvalidateAllContent(*this, nInv);
    }
}

void SwViewShell::SetAdjustTableLineHeightsToGridHeight(bool bValue)
{
    IDocumentSettingAccess& rIDSA = getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::ADJUST_TABLE_LINE_HEIGHTS_TO_GRID_HEIGHT) != bValue)
    {
        SwWait aWait(*GetDoc()->GetDocShell(), true);
        rIDSA.set(DocumentSettingId::ADJUST_TABLE_LINE_HEIGHTS_TO_GRID_HEIGHT, bValue);
        const SwInvalidateFlags nInv = SwInvalidateFlags::PrtArea | SwInvalidateFlags::Pos
                                       | SwInvalidateFlags::Size | SwInvalidateFlags::Table;
        lcl_InvalidateAllContent(*this, nInv);
    }
}

void SwViewShell::Reformat()
{
    SwWait aWait( *GetDoc()->GetDocShell(), true );

    // we go for safe: get rid of the old font information,
    // when the printer resolution or zoom factor changes.
    // Init() and Reformat() are the safest locations.
    pFntCache->Flush( );

    if( GetLayout()->IsCallbackActionEnabled() )
    {
        SwViewShell::StartAction();
        GetLayout()->InvalidateAllContent( SwInvalidateFlags::Size | SwInvalidateFlags::Pos | SwInvalidateFlags::PrtArea );
        SwViewShell::EndAction();
    }
}

void SwViewShell::ChgNumberDigits()
{
    SdrModel* pTmpDrawModel = getIDocumentDrawModelAccess().GetDrawModel();
    if ( pTmpDrawModel )
           pTmpDrawModel->ReformatAllTextObjects();
    Reformat();
}

void SwViewShell::CalcLayout()
{
    // extremely likely to be a Bad Idea to call this without StartAction
    // (except the Page Preview apparently only has a non-subclassed ViewShell)
    assert((typeid(*this) == typeid(SwViewShell)) || mnStartAction);

    CurrShell aCurr( this );
    SwWait aWait( *GetDoc()->GetDocShell(), true );

    // Preserve top of the text frame cache.
    SwSaveSetLRUOfst aSaveLRU;

    //switch on Progress when none is running yet.
    const bool bEndProgress = SfxProgress::GetActiveProgress( GetDoc()->GetDocShell() ) == nullptr;
    if ( bEndProgress )
    {
        tools::Long nEndPage = GetLayout()->GetPageNum();
        nEndPage += nEndPage * 10 / 100;
        ::StartProgress( STR_STATSTR_REFORMAT, 0, nEndPage, GetDoc()->GetDocShell() );
    }

    SwLayAction aAction( GetLayout(), Imp() );
    aAction.SetPaint( false );
    aAction.SetStatBar( true );
    aAction.SetCalcLayout( true );
    aAction.SetReschedule( true );
    GetDoc()->getIDocumentFieldsAccess().LockExpFields();
    aAction.Action(GetOut());
    GetDoc()->getIDocumentFieldsAccess().UnlockExpFields();

    //the SetNewFieldLst() on the Doc was cut off and must be fetched again
    //(see flowfrm.cxx, txtfld.cxx)
    if ( aAction.IsExpFields() )
    {
        aAction.Reset();
        aAction.SetPaint( false );
        aAction.SetStatBar( true );
        aAction.SetReschedule( true );

        GetDoc()->getIDocumentFieldsAccess().UpdatePageFields(0);
        GetDoc()->getIDocumentFieldsAccess().UpdateExpFields(nullptr, true);

        aAction.Action(GetOut());
    }

    if ( VisArea().HasArea() )
        InvalidateWindows( VisArea() );
    if ( bEndProgress )
        ::EndProgress( GetDoc()->GetDocShell() );
}

void SwViewShell::SetFirstVisPageInvalid()
{
    for(SwViewShell& rSh : GetRingContainer())
    {
        if ( rSh.Imp() )
            rSh.Imp()->SetFirstVisPageInvalid();
    }
}

void SwViewShell::SizeChgNotify()
{
    if ( !mpWin )
        mbDocSizeChgd = true;
    else if( ActionPend() || Imp()->IsCalcLayoutProgress() || mbPaintInProgress )
    {
        mbDocSizeChgd = true;

        if ( !Imp()->IsCalcLayoutProgress() && dynamic_cast<const SwCursorShell*>( this ) !=  nullptr )
        {
            PageNumNotify(*this);

            if (SfxViewShell* pNotifySh = comphelper::LibreOfficeKit::isActive() ? GetSfxViewShell() : nullptr)
            {
                Size aDocSize = GetDocSize();
                OString sPayload = OString::number(aDocSize.Width() + 2 * DOCUMENTBORDER) +
                    ", " + OString::number(aDocSize.Height() + 2 * DOCUMENTBORDER);

                SwXTextDocument* pModel = comphelper::getFromUnoTunnel<SwXTextDocument>(pNotifySh->GetCurrentDocument());
                SfxLokHelper::notifyDocumentSizeChanged(pNotifySh, sPayload, pModel);
            }
        }
    }
    else
    {
        mbDocSizeChgd = false;
        ::SizeNotify( *this, GetDocSize() );
    }
}

void SwViewShell::VisPortChgd( const SwRect &rRect)
{
    OSL_ENSURE( GetWin(), "VisPortChgd without Window." );

    if ( rRect == VisArea() )
        return;

    // Is someone spuriously rescheduling again?
    SAL_WARN_IF(mbInEndAction, "sw.core", "Scrolling during EndAction");

    //First get the old visible page, so we don't have to look
    //for it afterwards.
    const SwFrame *pOldPage = Imp()->GetFirstVisPage(GetWin()->GetOutDev());

    const SwRect aPrevArea( VisArea() );
    const bool bFull = aPrevArea.IsEmpty();
    maVisArea = rRect;
    SetFirstVisPageInvalid();

    //When there a PaintRegion still exists and the VisArea has changed,
    //the PaintRegion is at least by now obsolete. The PaintRegion can
    //have been created by RootFrame::PaintSwFrame.
    if ( !mbInEndAction &&
         Imp()->HasPaintRegion() && Imp()->GetPaintRegion()->GetOrigin() != VisArea() )
        Imp()->DeletePaintRegion();

    CurrShell aCurr( this );

    bool bScrolled = false;

    SwPostItMgr* pPostItMgr = GetPostItMgr();

    if ( bFull )
        GetWin()->Invalidate();
    else
    {
        //Calculate amount to be scrolled.
        const tools::Long nXDiff = aPrevArea.Left() - VisArea().Left();
        const tools::Long nYDiff = aPrevArea.Top()  - VisArea().Top();

        if( !nXDiff && !GetViewOptions()->getBrowseMode() &&
            (!Imp()->HasDrawView() || !Imp()->GetDrawView()->IsGridVisible() ) )
        {
            // If possible, don't scroll the application background
            // (PaintDesktop).  Also limit the left and right side of
            // the scroll range to the pages.
            const SwPageFrame *pPage = static_cast<SwPageFrame*>(GetLayout()->Lower());
            if ( pPage->getFrameArea().Top() > pOldPage->getFrameArea().Top() )
                pPage = static_cast<const SwPageFrame*>(pOldPage);
            SwRect aBoth( VisArea() );
            aBoth.Union( aPrevArea );
            const SwTwips nBottom = aBoth.Bottom();
            SwTwips nMinLeft = SAL_MAX_INT32;
            SwTwips nMaxRight= 0;

            const bool bBookMode = GetViewOptions()->IsViewLayoutBookMode();

            while ( pPage && pPage->getFrameArea().Top() <= nBottom )
            {
                SwRect aPageRect( pPage->GetBoundRect(GetWin()->GetOutDev()) );
                if ( bBookMode )
                {
                    const SwPageFrame& rFormatPage = pPage->GetFormatPage();
                    aPageRect.SSize( rFormatPage.GetBoundRect(GetWin()->GetOutDev()).SSize() );
                }

                // #i9719# - consider new border and shadow width
                if ( aPageRect.Overlaps( aBoth ) )
                {
                    SwTwips nPageLeft = 0;
                    SwTwips nPageRight = 0;
                    const sw::sidebarwindows::SidebarPosition aSidebarPos = pPage->SidebarPosition();

                    if( aSidebarPos != sw::sidebarwindows::SidebarPosition::NONE )
                    {
                        nPageLeft = aPageRect.Left();
                        nPageRight = aPageRect.Right();
                    }

                    if( nPageLeft < nMinLeft )
                        nMinLeft = nPageLeft;
                    if( nPageRight > nMaxRight )
                        nMaxRight = nPageRight;
                    //match with the draw objects
                    //take nOfst into account as the objects have been
                    //selected and have handles attached.
                    if ( pPage->GetSortedObjs() )
                    {
                        const tools::Long nOfst = GetOut()->PixelToLogic(
                            Size(Imp()->GetDrawView()->GetMarkHdlSizePixel()/2,0)).Width();
                        for (SwAnchoredObject* pObj : *pPage->GetSortedObjs())
                        {
                            // ignore objects that are not actually placed on the page
                            if (pObj->IsFormatPossible())
                            {
                                const tools::Rectangle aBound = pObj->GetObjRect().SVRect();
                                if (aBound.Left() != FAR_AWAY) {
                                    // OD 03.03.2003 #107927# - use correct datatype
                                    const SwTwips nL = std::max( SwTwips(0), SwTwips(aBound.Left() - nOfst) );
                                    if ( nL < nMinLeft )
                                        nMinLeft = nL;
                                    if( aBound.Right() + nOfst > nMaxRight )
                                        nMaxRight = aBound.Right() + nOfst;
                                }
                            }
                        }
                    }
                }
                pPage = static_cast<const SwPageFrame*>(pPage->GetNext());
            }
            tools::Rectangle aRect( aPrevArea.SVRect() );
            aRect.SetLeft( nMinLeft );
            aRect.SetRight( nMaxRight );
            if( VisArea().Overlaps( aPrevArea ) && !mnLockPaint )
            {
                bScrolled = true;
                maVisArea.Pos() = aPrevArea.Pos();
                if ( SmoothScroll( nXDiff, nYDiff, &aRect ) )
                    return;
                maVisArea.Pos() = rRect.Pos();
            }
            else if (!comphelper::LibreOfficeKit::isActive())
                GetWin()->Invalidate( aRect );
        }
        else if ( !mnLockPaint ) //will be released in Unlock
        {
            if( VisArea().Overlaps( aPrevArea ) )
            {
                bScrolled = true;
                maVisArea.Pos() = aPrevArea.Pos();
                if ( SmoothScroll( nXDiff, nYDiff, nullptr ) )
                    return;
                maVisArea.Pos() = rRect.Pos();
            }
            else
                GetWin()->Invalidate();
        }
    }

    // When tiled rendering, the map mode of the window is disabled, avoid
    // enabling it here.
    if (!comphelper::LibreOfficeKit::isActive())
    {
        Point aPt( VisArea().Pos() );
        aPt.setX( -aPt.X() ); aPt.setY( -aPt.Y() );
        MapMode aMapMode( GetWin()->GetMapMode() );
        aMapMode.SetOrigin( aPt );
        GetWin()->SetMapMode( aMapMode );
    }

    if ( HasDrawView() )
    {
        Imp()->GetDrawView()->VisAreaChanged( GetWin()->GetOutDev() );
        Imp()->GetDrawView()->SetActualWin( GetWin()->GetOutDev() );
    }
    GetWin()->PaintImmediately();

    if ( pPostItMgr ) // #i88070#
    {
        pPostItMgr->Rescale();
        pPostItMgr->CalcRects();
        pPostItMgr->LayoutPostIts();
    }

    if ( !bScrolled && pPostItMgr && pPostItMgr->HasNotes() && pPostItMgr->ShowNotes() )
        pPostItMgr->CorrectPositions();

#if !ENABLE_WASM_STRIP_ACCESSIBILITY
    if( Imp()->IsAccessible() )
        Imp()->UpdateAccessible();
#endif
}

bool SwViewShell::SmoothScroll( tools::Long lXDiff, tools::Long lYDiff, const tools::Rectangle *pRect )
{
#if !defined(MACOSX) && !defined(ANDROID) && !defined(IOS)
    // #i98766# - disable smooth scrolling for Mac

    const sal_uLong nBitCnt = mpOut->GetBitCount();
    tools::Long lMult = 1, lMax = LONG_MAX;
    if ( nBitCnt == 16 )
    {
        lMax = 7000;
        lMult = 2;
    }
    if ( nBitCnt == 24 )
    {
        lMax = 5000;
        lMult = 6;
    }
    else if ( nBitCnt == 1 )
    {
        lMax = 3000;
        lMult = 12;
    }

    // #i75172# isolated static conditions
    const bool bOnlyYScroll(!lXDiff && std::abs(lYDiff) != 0 && std::abs(lYDiff) < lMax);
    const bool bAllowedWithChildWindows(GetWin()->GetWindowClipRegionPixel().IsNull());
    const bool bSmoothScrollAllowed(bOnlyYScroll && mbEnableSmooth && GetViewOptions()->IsSmoothScroll() &&  bAllowedWithChildWindows);

    if(bSmoothScrollAllowed)
    {
        Imp()->m_bStopSmooth = false;

        const SwRect aOldVis( VisArea() );

        //create virtual device and set.
        const Size aPixSz = GetWin()->PixelToLogic(Size(1,1));
        VclPtrInstance<VirtualDevice> pVout( *GetWin()->GetOutDev() );
        pVout->SetLineColor( GetWin()->GetOutDev()->GetLineColor() );
        pVout->SetFillColor( GetWin()->GetOutDev()->GetFillColor() );
        MapMode aMapMode( GetWin()->GetMapMode() );
        pVout->SetMapMode( aMapMode );
        Size aSize( maVisArea.Width()+2*aPixSz.Width(), std::abs(lYDiff)+(2*aPixSz.Height()) );
        if ( pRect )
            aSize.setWidth( std::min(aSize.Width(), pRect->GetWidth()+2*aPixSz.Width()) );
        if ( pVout->SetOutputSize( aSize ) )
        {
            mnLockPaint++;

            //First Paint everything in the virtual device.
            SwRect aRect( VisArea() );
            aRect.Height( aSize.Height() );
            if ( pRect )
            {
                aRect.Pos().setX( std::max(aRect.Left(),pRect->Left()-aPixSz.Width()) );
                aRect.Right( std::min(aRect.Right()+2*aPixSz.Width(), pRect->Right()+aPixSz.Width()));
            }
            else
                aRect.AddWidth(2*aPixSz.Width() );
            aRect.Pos().setY( lYDiff < 0 ? aOldVis.Bottom() - aPixSz.Height()
                                         : aRect.Top() - aSize.Height() + aPixSz.Height() );
            aRect.Pos().setX( std::max( tools::Long(0), aRect.Left()-aPixSz.Width() ) );
            aRect.Pos()  = GetWin()->PixelToLogic( GetWin()->LogicToPixel( aRect.Pos()));
            aRect.SSize( GetWin()->PixelToLogic( GetWin()->LogicToPixel( aRect.SSize())) );
            maVisArea = aRect;
            const Point aPt( -aRect.Left(), -aRect.Top() );
            aMapMode.SetOrigin( aPt );
            pVout->SetMapMode( aMapMode );
            OutputDevice *pOld = mpOut;
            mpOut = pVout.get();

            {
                // #i75172# To get a clean repaint, a new ObjectContact is needed here. Without, the
                // repaint would not be correct since it would use the wrong DrawPage visible region.
                // This repaint IS about painting something currently outside the visible part (!).
                // For that purpose, AddDeviceToPaintView is used which creates a new SdrPageViewWindow
                // and all the necessary stuff. It's not cheap, but necessary here. Alone because repaint
                // target really is NOT the current window.
                // Also will automatically NOT use PreRendering and overlay (since target is VirtualDevice)
                if(!HasDrawView())
                    MakeDrawView();
                SdrView* pDrawView = GetDrawView();
                pDrawView->AddDeviceToPaintView(*pVout, nullptr);

                // clear mpWin during DLPrePaint2 to get paint preparation for mpOut, but set it again
                // immediately afterwards. There are many decisions in SW which imply that Printing
                // is used when mpWin == 0 (wrong but widely used).
                vcl::Window* pOldWin = mpWin;
                mpWin = nullptr;
                DLPrePaint2(vcl::Region(aRect.SVRect()));
                mpWin = pOldWin;

                // SW paint stuff
                PaintDesktop(*GetOut(), aRect);
                SwViewShell::sbLstAct = true;
                GetLayout()->PaintSwFrame( *GetOut(), aRect );
                SwViewShell::sbLstAct = false;

                // end paint and destroy ObjectContact again
                DLPostPaint2(true);
                pDrawView->DeleteDeviceFromPaintView(*pVout);
            }

            mpOut = pOld;
            maVisArea = aOldVis;

            //Now shift in parts and copy the new Pixel from the virtual device.

            // ??????????????????????
            // or is it better to get the scrollfactor from the User
            // as option?
            // ??????????????????????
            tools::Long lMaDelta = aPixSz.Height();
            if ( std::abs(lYDiff) > ( maVisArea.Height() / 3 ) )
                lMaDelta *= 6;
            else
                lMaDelta *= 2;

            lMaDelta *= lMult;

            if ( lYDiff < 0 )
                lMaDelta = -lMaDelta;

            tools::Long lDiff = lYDiff;
            while ( lDiff )
            {
                tools::Long lScroll;
                if ( Imp()->m_bStopSmooth || std::abs(lDiff) <= std::abs(lMaDelta) )
                {
                    lScroll = lDiff;
                    lDiff = 0;
                }
                else
                {
                    lScroll = lMaDelta;
                    lDiff -= lMaDelta;
                }

                const SwRect aTmpOldVis = VisArea();
                maVisArea.Pos().AdjustY( -lScroll );
                maVisArea.Pos() = GetWin()->PixelToLogic( GetWin()->LogicToPixel( VisArea().Pos()));
                lScroll = aTmpOldVis.Top() - VisArea().Top();
                if ( pRect )
                {
                    tools::Rectangle aTmp( aTmpOldVis.SVRect() );
                    aTmp.SetLeft( pRect->Left() );
                    aTmp.SetRight( pRect->Right() );
                    GetWin()->Scroll( 0, lScroll, aTmp, ScrollFlags::Children);
                }
                else
                    GetWin()->Scroll( 0, lScroll, ScrollFlags::Children );

                const Point aTmpPt( -VisArea().Left(), -VisArea().Top() );
                MapMode aTmpMapMode( GetWin()->GetMapMode() );
                aTmpMapMode.SetOrigin( aTmpPt );
                GetWin()->SetMapMode( aTmpMapMode );

                if ( Imp()->HasDrawView() )
                    Imp()->GetDrawView()->VisAreaChanged( GetWin()->GetOutDev() );

                SetFirstVisPageInvalid();
                if ( !Imp()->m_bStopSmooth )
                {
                    const bool bScrollDirectionIsUp(lScroll > 0);
                    Imp()->m_aSmoothRect = VisArea();

                    if(bScrollDirectionIsUp)
                    {
                        Imp()->m_aSmoothRect.Bottom( VisArea().Top() + lScroll + aPixSz.Height());
                    }
                    else
                    {
                        Imp()->m_aSmoothRect.Top( VisArea().Bottom() + lScroll - aPixSz.Height());
                    }

                    Imp()->m_bSmoothUpdate = true;
                    GetWin()->PaintImmediately();
                    Imp()->m_bSmoothUpdate = false;

                    if(!Imp()->m_bStopSmooth)
                    {
                            // start paint on logic base
                            const tools::Rectangle aTargetLogic(Imp()->m_aSmoothRect.SVRect());
                            DLPrePaint2(vcl::Region(aTargetLogic));

                            // get target rectangle in discrete pixels
                            OutputDevice& rTargetDevice = mpTargetPaintWindow->GetTargetOutputDevice();
                            const tools::Rectangle aTargetPixel(rTargetDevice.LogicToPixel(aTargetLogic));

                            // get source top-left in discrete pixels
                            const Point aSourceTopLeft(pVout->LogicToPixel(aTargetLogic.TopLeft()));

                            // switch off MapModes
                            const bool bMapModeWasEnabledDest(rTargetDevice.IsMapModeEnabled());
                            const bool bMapModeWasEnabledSource(pVout->IsMapModeEnabled());
                            rTargetDevice.EnableMapMode(false);
                            pVout->EnableMapMode(false);

                            rTargetDevice.DrawOutDev(
                                aTargetPixel.TopLeft(), aTargetPixel.GetSize(), // dest
                                aSourceTopLeft, aTargetPixel.GetSize(), // source
                                *pVout);

                            // restore MapModes
                            rTargetDevice.EnableMapMode(bMapModeWasEnabledDest);
                            pVout->EnableMapMode(bMapModeWasEnabledSource);

                            // end paint on logoc base
                            DLPostPaint2(true);
                    }
                    else
                        --mnLockPaint;
                }
            }
            pVout.disposeAndClear();
            GetWin()->PaintImmediately();
            if ( !Imp()->m_bStopSmooth )
                --mnLockPaint;
            SetFirstVisPageInvalid();
            return true;
        }
        pVout.disposeAndClear();
    }
#endif

    maVisArea.Pos().AdjustX( -lXDiff );
    maVisArea.Pos().AdjustY( -lYDiff );
    if ( pRect )
        GetWin()->Scroll( lXDiff, lYDiff, *pRect, ScrollFlags::Children);
    else
        GetWin()->Scroll( lXDiff, lYDiff, ScrollFlags::Children);
    return false;
}

void SwViewShell::PaintDesktop(const vcl::RenderContext& rRenderContext, const SwRect &rRect)
{
    if ( !GetWin() && !GetOut()->GetConnectMetaFile() )
        return;                     //for the printer we don't do anything here.

    if(comphelper::LibreOfficeKit::isActive())
        return;

    //Catch exceptions, so that it doesn't look so surprising.
    //Can e.g. happen during Idle.
    //Unfortunately we must at any rate Paint the rectangles next to the pages,
    //as these are not painted at VisPortChgd.
    bool bBorderOnly = false;
    const SwRootFrame *pRoot = GetLayout();
    if ( rRect.Top() > pRoot->getFrameArea().Bottom() )
    {
        const SwFrame *pPg = pRoot->Lower();
        while ( pPg && pPg->GetNext() )
            pPg = pPg->GetNext();
        if ( !pPg || !pPg->getFrameArea().Overlaps( VisArea() ) )
            bBorderOnly = true;
    }

    const bool bBookMode = GetViewOptions()->IsViewLayoutBookMode();

    SwRegionRects aRegion( rRect );

    //mod #i6193: remove sidebar area to avoid flickering
    const SwPostItMgr* pPostItMgr = GetPostItMgr();
    const SwTwips nSidebarWidth = pPostItMgr && pPostItMgr->HasNotes() && pPostItMgr->ShowNotes() ?
                                  pPostItMgr->GetSidebarWidth() + pPostItMgr->GetSidebarBorderWidth() :
                                  0;

    if ( bBorderOnly )
    {
        const SwFrame *pPage =pRoot->Lower();
        SwRect aLeft( rRect ), aRight( rRect );
        while ( pPage )
        {
            tools::Long nTmp = pPage->getFrameArea().Left();
            if ( nTmp < aLeft.Right() )
                aLeft.Right( nTmp );
            nTmp = pPage->getFrameArea().Right();
            if ( nTmp > aRight.Left() )
            {
                aRight.Left( nTmp + nSidebarWidth );
            }
            pPage = pPage->GetNext();
        }
        aRegion.clear();
        if ( aLeft.HasArea() )
            aRegion.push_back( aLeft );
        if ( aRight.HasArea() )
            aRegion.push_back( aRight );
    }
    else
    {
        const SwFrame *pPage = Imp()->GetFirstVisPage(&rRenderContext);
        const SwTwips nBottom = rRect.Bottom();
        while ( pPage && !aRegion.empty() &&
                (pPage->getFrameArea().Top() <= nBottom) )
        {
            SwRect aPageRect( pPage->getFrameArea() );
            if ( bBookMode )
            {
                const SwPageFrame& rFormatPage = static_cast<const SwPageFrame*>(pPage)->GetFormatPage();
                aPageRect.SSize( rFormatPage.getFrameArea().SSize() );
            }

            const bool bSidebarRight =
                static_cast<const SwPageFrame*>(pPage)->SidebarPosition() == sw::sidebarwindows::SidebarPosition::RIGHT;
            aPageRect.Pos().AdjustX( -(bSidebarRight ? 0 : nSidebarWidth) );
            aPageRect.AddWidth(nSidebarWidth );

            if ( aPageRect.Overlaps( rRect ) )
                aRegion -= aPageRect;

            pPage = pPage->GetNext();
        }
    }
    if ( !aRegion.empty() )
        PaintDesktop_(aRegion);
}

// PaintDesktop is split in two, this part is also used by PreviewPage
void SwViewShell::PaintDesktop_(const SwRegionRects &rRegion)
{
    if (DrawAppBackgroundBitmap(GetOut(), rRegion.GetOrigin()))
        return;

    // OD 2004-04-23 #116347#
    GetOut()->Push( vcl::PushFlags::FILLCOLOR|vcl::PushFlags::LINECOLOR );
    GetOut()->SetLineColor();

    for ( auto &rRgn : rRegion )
    {
        const tools::Rectangle aRectangle(rRgn.SVRect());

        // #i93170#
        // Here we have a real Problem. On the one hand we have the buffering for paint
        // and overlay which needs an embracing pair of DLPrePaint2/DLPostPaint2 calls,
        // on the other hand the MapMode is not set correctly when this code is executed.
        // This is done in the users of this method, for each SWpage before painting it.
        // Since the MapMode is not correct here, the call to DLPostPaint2 will paint
        // existing FormControls due to the current MapMode.

        // There are basically three solutions for this:

        // (1) Set the MapMode correct, move the background painting to the users of
        //     this code

        // (2) Do no DLPrePaint2/DLPostPaint2 here; no SdrObjects are allowed to lie in
        //     the desktop region. Disadvantage: the desktop will not be part of the
        //     buffers, e.g. overlay. Thus, as soon as overlay will be used over the
        //     desktop, it will not work.

        // (3) expand DLPostPaint2 with a flag to signal if FormControl paints shall
        //     be done or not

        // Currently, (3) will be the best possible solution. It will keep overlay and
        // buffering intact and work without MapMode for single pages. In the medium
        // to long run, (1) will need to be used and the bool bPaintFormLayer needs
        // to be removed again

        // #i68597# inform Drawinglayer about display change
        DLPrePaint2(vcl::Region(aRectangle));

        // #i75172# needed to move line/Fill color setters into loop since DLPrePaint2
        // may exchange GetOut(), that's it's purpose. This happens e.g. at print preview.
        GetOut()->SetFillColor( GetViewOptions()->GetAppBackgroundColor());
        GetOut()->SetLineColor();
        GetOut()->DrawRect(aRectangle);

        DLPostPaint2(false);
    }

    GetOut()->Pop();
}

bool SwViewShell::DrawAppBackgroundBitmap(vcl::RenderContext* rRenderContext, const SwRect& rRect)
{
    if (Application::IsHeadlessModeEnabled() || !ThemeColors::UseBmpForAppBack())
        return false;

    const BitmapEx& aAppBackImg
        = Application::GetSettings().GetStyleSettings().GetAppBackgroundBitmap();
    if (aAppBackImg.IsEmpty())
        return false;

    Wallpaper aWallpaper(aAppBackImg);
    if (ThemeColors::GetAppBackBmpDrawType() == u"Tiled"_ustr)
        aWallpaper.SetStyle(WallpaperStyle::Tile);
    else if (ThemeColors::GetAppBackBmpDrawType() == u"Stretched"_ustr)
        aWallpaper.SetStyle(WallpaperStyle::Scale);
    else
        aWallpaper.SetStyle(WallpaperStyle::Tile);

    rRenderContext->DrawWallpaper(rRect.SVRect(), aWallpaper);
    return true;
}

bool SwViewShell::CheckInvalidForPaint( const SwRect &rRect )
{
    if ( !GetWin() )
        return false;

    const SwPageFrame *pPage = Imp()->GetFirstVisPage(GetOut());
    const SwTwips nBottom = VisArea().Bottom();
    const SwTwips nRight  = VisArea().Right();
    bool bRet = false;
    while ( !bRet && pPage && ((pPage->getFrameArea().Top() <= nBottom) &&
                                (pPage->getFrameArea().Left() <= nRight)))
    {
        if ( pPage->IsInvalid() || pPage->IsInvalidFly() )
            bRet = true;
        pPage = static_cast<const SwPageFrame*>(pPage->GetNext());
    }

    if ( bRet )
    {
        //Unfortunately Start/EndAction won't help here, as the Paint originated
        //from GUI and so Clipping has been set against getting through.
        //Ergo: do it all yourself (see ImplEndAction())
        if ( Imp()->HasPaintRegion() && Imp()->GetPaintRegion()->GetOrigin() != VisArea())
             Imp()->DeletePaintRegion();

        SwLayAction aAction( GetLayout(), Imp() );
        aAction.SetComplete( false );
        // We increment the action counter to avoid a recursive call of actions
        // e.g. from a SwFEShell::RequestObjectResize(..) in bug 95829.
        // A recursive call of actions is no good idea because the inner action
        // can't format frames which are locked by the outer action. This may
        // cause and endless loop.
        ++mnStartAction;
        aAction.Action(GetWin()->GetOutDev());
        --mnStartAction;

        std::optional<SwRegionRects> oRegion = Imp()->TakePaintRegion();
        if ( oRegion && aAction.IsBrowseActionStop() )
        {
            //only of interest when something has changed in the visible range
            bool bAllNoOverlap = std::all_of(oRegion->begin(), oRegion->end(), [this](const SwRect &rTmp) {
                return rTmp.Overlaps( VisArea() );
            });
            if ( bAllNoOverlap )
                oRegion.reset();
        }

        if ( oRegion )
        {
            oRegion->LimitToOrigin();
            oRegion->Compress( SwRegionRects::CompressFuzzy );
            bRet = false;
            if ( !oRegion->empty() )
            {
                SwRegionRects aRegion( rRect );
                for ( const SwRect &rTmp : *oRegion )
                {
                    if ( !rRect.Contains( rTmp ) )
                    {
                        InvalidateWindows( rTmp );
                        if ( rTmp.Overlaps( VisArea() ) )
                        {   aRegion -= rTmp;
                            bRet = true;
                        }
                    }
                }
                if ( bRet )
                {
                    for ( size_t i = 0; i < aRegion.size(); ++i )
                        GetWin()->Invalidate( aRegion[i].SVRect() );

                    if ( rRect != VisArea() )
                    {
                        //rRect == VisArea is the special case for new or
                        //Shift-Ctrl-R, when it shouldn't be necessary to
                        //hold the rRect again in Document coordinates.
                        if ( maInvalidRect.IsEmpty() )
                            maInvalidRect = rRect;
                        else
                            maInvalidRect.Union( rRect );
                    }
                }
            }
            else
                bRet = false;
        }
        else
            bRet = false;
    }
    return bRet;
}

namespace
{
/// Similar to comphelper::FlagRestorationGuard, but for vcl::RenderContext.
class RenderContextGuard
{
    std::unique_ptr<SdrPaintWindow> m_TemporaryPaintWindow;
    SdrPageWindow* m_pPatchedPageWindow;
    SdrPaintWindow* m_pPreviousPaintWindow = nullptr;

public:
    RenderContextGuard(VclPtr<vcl::RenderContext>& pRef, vcl::RenderContext* pValue, SwViewShell* pShell)
        : m_pPatchedPageWindow(nullptr)
    {
        pRef = pValue;

        if (pValue == pShell->GetWin()->GetOutDev())
            return;

        SdrView* pDrawView(pShell->Imp()->GetDrawView());

        if (nullptr == pDrawView)
            return;

        SdrPageView* pSdrPageView(pDrawView->GetSdrPageView());

        if (nullptr != pSdrPageView)
        {
            m_pPatchedPageWindow = pSdrPageView->FindPageWindow(*pShell->GetWin()->GetOutDev());

            if (nullptr != m_pPatchedPageWindow)
            {
                m_TemporaryPaintWindow.reset(new SdrPaintWindow(*pDrawView, *pValue));
                m_pPreviousPaintWindow = m_pPatchedPageWindow->patchPaintWindow(*m_TemporaryPaintWindow);
            }
        }
    }

    ~RenderContextGuard()
    {
        if(nullptr != m_pPatchedPageWindow)
        {
            m_pPatchedPageWindow->unpatchPaintWindow(m_pPreviousPaintWindow);
        }
    }
};
}

void SwViewShell::Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle &rRect)
{
    RenderContextGuard aGuard(mpOut, &rRenderContext, this);
    if ( mnLockPaint )
    {
        if ( Imp()->m_bSmoothUpdate )
        {
            SwRect aTmp( rRect );
            if ( !Imp()->m_aSmoothRect.Contains( aTmp ) )
                Imp()->m_bStopSmooth = true;
            else
            {
                Imp()->m_aSmoothRect = aTmp;
                return;
            }
        }
        else
            return;
    }

    if ( SwRootFrame::IsInPaint() )
    {
        //During the publication of a page at printing the Paint is buffered.
        SwPaintQueue::Add( this, SwRect( rRect ) );
        return;
    }

    //With !nStartAction I try to protect me against erroneous code at other places.
    //Hopefully it will not lead to problems!?
    if ( mbPaintWorks && !mnStartAction )
    {
        if( GetWin() && GetWin()->IsVisible() )
        {
            SwRect aRect( rRect );
            if ( mbPaintInProgress ) //Guard against double Paints!
            {
                GetWin()->Invalidate( rRect );
                return;
            }

            mbPaintInProgress = true;
            CurrShell aCurr( this );
            SwRootFrame::SetNoVirDev( true );

            //We don't want to Clip to and from, we trust that all are limited
            //to the rectangle and only need to calculate the clipping once.
            //The ClipRect is removed here once and not recovered, as externally
            //no one needs it anymore anyway.
            //Not when we paint a Metafile.
            if( !GetOut()->GetConnectMetaFile() && GetOut()->IsClipRegion())
                GetOut()->SetClipRegion();

            if ( IsPreview() )
            {
                //When useful, process or destroy the old InvalidRect.
                if ( aRect.Contains( maInvalidRect ) )
                    ResetInvalidRect();
                SwViewShell::sbLstAct = true;
                GetLayout()->PaintSwFrame( rRenderContext, aRect );
                SwViewShell::sbLstAct = false;
            }
            else
            {
                //When one of the visible pages still has anything entered for
                //Repaint, Repaint must be triggered.
                if ( !CheckInvalidForPaint( aRect ) )
                {
                    // --> OD 2009-08-12 #i101192#
                    // start Pre/PostPaint encapsulation to avoid screen blinking
                    const vcl::Region aRepaintRegion(aRect.SVRect());
                    DLPrePaint2(aRepaintRegion);

                    // <--
                    PaintDesktop(rRenderContext, aRect);

                    //When useful, process or destroy the old InvalidRect.
                    if ( aRect.Contains( maInvalidRect ) )
                        ResetInvalidRect();
                    SwViewShell::sbLstAct = true;
                    GetLayout()->PaintSwFrame( rRenderContext, aRect );
                    SwViewShell::sbLstAct = false;
                    // --> OD 2009-08-12 #i101192#
                    // end Pre/PostPaint encapsulation
                    DLPostPaint2(true);
                    // <--
                }
            }
            SwRootFrame::SetNoVirDev( false );
            mbPaintInProgress = false;
            UISizeNotify();
        }
    }
    else
    {
        if ( maInvalidRect.IsEmpty() )
            maInvalidRect = SwRect( rRect );
        else
            maInvalidRect.Union( SwRect( rRect ) );

        if ( mbInEndAction && GetWin() )
        {
            const vcl::Region aRegion(GetWin()->GetPaintRegion());
            RectangleVector aRectangles;
            aRegion.GetRegionRectangles(aRectangles);

            for(const auto& rRectangle : aRectangles)
            {
                Imp()->AddPaintRect(SwRect(rRectangle));
            }

            //RegionHandle hHdl( aRegion.BeginEnumRects() );
            //Rectangle aRect;
            //while ( aRegion.GetEnumRects( hHdl, aRect ) )
            //  Imp()->AddPaintRect( aRect );
            //aRegion.EndEnumRects( hHdl );
        }
        else if ( SfxProgress::GetActiveProgress( GetDoc()->GetDocShell() ) &&
                  GetOut() == GetWin()->GetOutDev() )
        {
            // #i68597#
            const vcl::Region aDLRegion(rRect);
            DLPrePaint2(aDLRegion);

            rRenderContext.Push( vcl::PushFlags::FILLCOLOR|vcl::PushFlags::LINECOLOR );
            rRenderContext.SetFillColor( Imp()->GetRetoucheColor() );
            rRenderContext.SetLineColor();
            rRenderContext.DrawRect( rRect );
            rRenderContext.Pop();
            // #i68597#
            DLPostPaint2(true);
        }
    }
}

void SwViewShell::PaintTile(VirtualDevice &rDevice, int contextWidth, int contextHeight, int tilePosX, int tilePosY, tools::Long tileWidth, tools::Long tileHeight)
{
    // SwViewShell's output device setup
    // TODO clean up SwViewShell's approach to output devices (the many of
    // them - mpBufferedOut, mpOut, mpWin, ...)
    OutputDevice *pSaveOut = mpOut;
    comphelper::LibreOfficeKit::setTiledPainting(true);
    mpOut = &rDevice;

    // resizes the virtual device so to contain the entries context
    rDevice.SetOutputSizePixel(Size(contextWidth, contextHeight), /*bErase*/false);

    // setup the output device to draw the tile
    MapMode aMapMode(rDevice.GetMapMode());
    aMapMode.SetMapUnit(MapUnit::MapTwip);
    aMapMode.SetOrigin(Point(-tilePosX, -tilePosY));

    // Scaling. Must convert from pixels to twips. We know
    // that VirtualDevices use a DPI of 96.
    const Fraction scale = conversionFract(o3tl::Length::px, o3tl::Length::twip);
    Fraction scaleX = Fraction(contextWidth, tileWidth) * scale;
    Fraction scaleY = Fraction(contextHeight, tileHeight) * scale;
    aMapMode.SetScaleX(scaleX);
    aMapMode.SetScaleY(scaleY);
    rDevice.SetMapMode(aMapMode);

    // Update scaling of SwEditWin and its sub-widgets, needed for comments.
    sal_uInt16 nOldZoomValue = 0;
    if (GetWin() && GetWin()->GetMapMode().GetScaleX() != scaleX)
    {
        double fScale = double(scaleX);
        SwViewOption aOption(*GetViewOptions());
        nOldZoomValue = aOption.GetZoom();
        aOption.SetZoom(fScale * 100);
        ApplyViewOptions(aOption);
        // Make sure the map mode (disabled in SwXTextDocument::initializeForTiledRendering()) is still disabled.
        GetWin()->EnableMapMode(false);
    }

    tools::Rectangle aOutRect(Point(tilePosX, tilePosY),
                              rDevice.PixelToLogic(Size(contextWidth, contextHeight)));

    // Make the requested area visible -- we can't use MakeVisible as that will
    // only scroll the contents, but won't zoom/resize if needed.
    // Without this, items/text that are outside the visible area (in the SwView)
    // won't be painted when rendering tiles (at least when using either the
    // tiledrendering app, or the gtktiledviewer) -- although ultimately we
    // probably want to fix things so that the SwView's area doesn't affect
    // tiled rendering?
    VisPortChgd(SwRect(aOutRect));

    // Invoke SwLayAction if layout is not yet ready.
    CheckInvalidForPaint(SwRect(aOutRect));

    // draw - works in logic coordinates
    Paint(rDevice, aOutRect);

    SwPostItMgr* pPostItMgr = GetPostItMgr();
    if (GetViewOptions()->IsPostIts() && pPostItMgr)
        pPostItMgr->PaintTile(rDevice);

    // SwViewShell's output device tear down

    // A view shell can get a PaintTile call for a tile at a zoom level
    // different from the one, the related client really is.
    // In such a case it is better to reset the current scale value to
    // the original one, since such a value should be in synchronous with
    // the zoom level in the client (see setClientZoom).
    // At present the zoom value returned by GetViewOptions()->GetZoom() is
    // used in SwXTextDocument methods (postMouseEvent and setGraphicSelection)
    // for passing the correct mouse position to an edited chart (if any).
    if (nOldZoomValue !=0)
    {
        SwViewOption aOption(*GetViewOptions());
        aOption.SetZoom(nOldZoomValue);
        ApplyViewOptions(aOption);

        // Changing the zoom value doesn't always trigger the updating of
        // the client ole object area, so we call it directly.
        if (SfxViewShell* pNotifySh = GetSfxViewShell())
        {
            if (SfxInPlaceClient* pIPClient = pNotifySh->GetIPClient())
            {
                pIPClient->VisAreaChanged();
            }
        }

        // Make sure the map mode (disabled in SwXTextDocument::initializeForTiledRendering()) is still disabled.
        GetWin()->EnableMapMode(false);
    }

    mpOut = pSaveOut;
    comphelper::LibreOfficeKit::setTiledPainting(false);
}

void SwViewShell::SetBrowseBorder( const Size& rNew )
{
    if( rNew != maBrowseBorder )
    {
        maBrowseBorder = rNew;
        if ( maVisArea.HasArea() )
            InvalidateLayout( false );
    }
}

const Size& SwViewShell::GetBrowseBorder() const
{
    return maBrowseBorder;
}

sal_Int32 SwViewShell::GetBrowseWidth() const
{
    const SwPostItMgr* pPostItMgr = GetPostItMgr();
    if ( pPostItMgr && pPostItMgr->HasNotes() && pPostItMgr->ShowNotes() )
    {
        Size aBorder( maBrowseBorder );
        aBorder.AdjustWidth(maBrowseBorder.Width() );
        aBorder.AdjustWidth(pPostItMgr->GetSidebarWidth(true) + pPostItMgr->GetSidebarBorderWidth(true) );
        return maVisArea.Width() - GetOut()->PixelToLogic(aBorder).Width();
    }
    else
        return maVisArea.Width() - 2 * GetOut()->PixelToLogic(maBrowseBorder).Width();
}

void SwViewShell::InvalidateLayout( bool bSizeChanged )
{
    if ( !bSizeChanged && !GetViewOptions()->getBrowseMode() &&
         !GetViewOptions()->IsWhitespaceHidden() )
        return;

    CurrShell aCurr( this );

    OSL_ENSURE( GetLayout(), "Layout not ready" );

    // When the Layout doesn't have a height yet, nothing is formatted.
    // That leads to problems with Invalidate, e.g. when setting up a new View
    // the content is inserted and formatted (regardless of empty VisArea).
    // Therefore the pages must be roused for formatting.
    if( !GetLayout()->getFrameArea().Height() )
    {
        SwFrame* pPage = GetLayout()->Lower();
        while( pPage )
        {
            pPage->InvalidateSize_();
            pPage = pPage->GetNext();
        }
        return;
    }

    LockPaint(LockPaintReason::InvalidateLayout);
    SwViewShell::StartAction();

    SwPageFrame *pPg = static_cast<SwPageFrame*>(GetLayout()->Lower());
    do
    {   pPg->InvalidateSize();
        pPg->InvalidatePrt_();
        pPg->InvaPercentLowers();
        if ( bSizeChanged )
        {
            pPg->PrepareHeader();
            pPg->PrepareFooter();
        }
        pPg = static_cast<SwPageFrame*>(pPg->GetNext());
    } while ( pPg );

    // When the size ratios in browse mode change,
    // the Position and PrtArea of the Content and Tab frames must be Invalidated.
    SwInvalidateFlags nInv = SwInvalidateFlags::PrtArea | SwInvalidateFlags::Table | SwInvalidateFlags::Pos;
    // In case of layout or mode change, the ContentFrames need a size-Invalidate
    // because of printer/screen formatting.
    if ( bSizeChanged )
        nInv |= SwInvalidateFlags::Size | SwInvalidateFlags::Direction;

    GetLayout()->InvalidateAllContent( nInv );

    SwFrame::CheckPageDescs( static_cast<SwPageFrame*>(GetLayout()->Lower()) );

    SwViewShell::EndAction();
    UnlockPaint();
}

SwRootFrame *SwViewShell::GetLayout() const
{
    return mpLayout.get();
}

vcl::RenderContext& SwViewShell::GetRefDev() const
{
    OutputDevice* pTmpOut = nullptr;
    if (  GetWin() &&
          GetViewOptions()->getBrowseMode() &&
         !GetViewOptions()->IsPrtFormat() )
        pTmpOut = GetWin()->GetOutDev();
    else
        pTmpOut = GetDoc()->getIDocumentDeviceAccess().getReferenceDevice( true );

    return *pTmpOut;
}

const SwNodes& SwViewShell::GetNodes() const
{
    return mxDoc->GetNodes();
}

void SwViewShell::DrawSelChanged()
{
}

Size SwViewShell::GetDocSize() const
{
    Size aSz;
    const SwRootFrame* pRoot = GetLayout();
    if( pRoot )
        aSz = pRoot->getFrameArea().SSize();

    return aSz;
}

SfxItemPool& SwViewShell::GetAttrPool()
{
    return GetDoc()->GetAttrPool();
}

void SwViewShell::ApplyViewOptions( const SwViewOption &rOpt )
{
    for(SwViewShell& rSh : GetRingContainer())
        rSh.SwViewShell::StartAction();

    ImplApplyViewOptions( rOpt );

    // With one layout per view it is no longer necessary
    // to sync these "layout related" view options
    // But as long as we have to disable "multiple layout"

    for(SwViewShell& rSh : GetRingContainer())
    {
        if(&rSh == this)
            continue;
        SwViewOption aOpt( *rSh.GetViewOptions() );
        aOpt.SyncLayoutRelatedViewOptions(rOpt);
        if ( !(aOpt == *rSh.GetViewOptions()) )
            rSh.ImplApplyViewOptions( aOpt );
    }
    // End of disabled multiple window

    for(SwViewShell& rSh : GetRingContainer())
        rSh.SwViewShell::EndAction();
}

static bool
IsCursorInFieldmarkHidden(SwPaM const& rCursor, sw::FieldmarkMode const eMode)
{
    if (eMode == sw::FieldmarkMode::ShowBoth)
    {
        return false;
    }
    IDocumentMarkAccess const& rIDMA(*rCursor.GetDoc().getIDocumentMarkAccess());
    // iterate, for nested fieldmarks
    for (auto iter = rIDMA.getFieldmarksBegin(); iter != rIDMA.getFieldmarksEnd(); ++iter)
    {
        if (*rCursor.GetPoint() <= (**iter).GetMarkStart())
        {
            return false;
        }
        if (*rCursor.GetPoint() < (**iter).GetMarkEnd())
        {
            SwPosition const sepPos(sw::mark::FindFieldSep(**iter));
            if (eMode == sw::FieldmarkMode::ShowResult)
            {
                if (*rCursor.GetPoint() <= sepPos
                    && *rCursor.GetPoint() != (**iter).GetMarkStart())
                {
                    return true;
                }
            }
            else
            {
                if (sepPos < *rCursor.GetPoint())
                {
                    return true;
                }
            }
        }
    }
    return false;
}

void SwViewShell::ImplApplyViewOptions( const SwViewOption &rOpt )
{
    if (*mpOpt == rOpt)
        return;

    vcl::Window *pMyWin = GetWin();
    if( !pMyWin )
    {
        OSL_ENSURE( pMyWin, "SwViewShell::ApplyViewOptions: no window" );
        return;
    }

    CurrShell aCurr( this );

    bool bReformat   = false;

    if( mpOpt->IsShowHiddenField() != rOpt.IsShowHiddenField() )
    {
        static_cast<SwHiddenTextFieldType*>(mxDoc->getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::HiddenText ))->
                                            SetHiddenFlag( !rOpt.IsShowHiddenField() );
        bReformat = true;
    }
    if ( mpOpt->IsShowHiddenPara() != rOpt.IsShowHiddenPara() )
    {
        SwHiddenParaFieldType* pFieldType = static_cast<SwHiddenParaFieldType*>(GetDoc()->
                                          getIDocumentFieldsAccess().GetSysFieldType(SwFieldIds::HiddenPara));
        if( pFieldType && pFieldType->HasWriterListeners() )
            pFieldType->PrintHiddenPara();
        bReformat = true;
    }
    if ( !bReformat && mpOpt->IsShowHiddenChar() != rOpt.IsShowHiddenChar() )
    {
        bReformat = GetDoc()->ContainsHiddenChars();
    }
    if ( mpOpt->IsShowChangesInMargin() != rOpt.IsShowChangesInMargin() ||
            mpOpt->IsShowChangesInMargin2() != rOpt.IsShowChangesInMargin2() )
    {
        if (rOpt.IsShowChangesInMargin())
            GetDoc()->GetDocumentRedlineManager().HideAll(
                   /*bDeletion=*/!rOpt.IsShowChangesInMargin2() );
        else
            GetDoc()->GetDocumentRedlineManager().ShowAll();
        GetLayout()->SetHideRedlines( false );
    }

    // bReformat becomes true, if ...
    // - fieldnames apply or not ...
    // ( - SwEndPortion must _no_ longer be generated. )
    // - Of course, the screen is something completely different than the printer ...
    bool const isToggleFieldNames(mpOpt->IsFieldName() != rOpt.IsFieldName());
    bool const isToggleLayoutHide{isToggleFieldNames
                || mpOpt->IsParagraph() != rOpt.IsParagraph()
                || mpOpt->IsShowHiddenChar() != rOpt.IsShowHiddenChar()};

    // The map mode is changed, minima/maxima will be attended by UI
    if( mpOpt->GetZoom() != rOpt.GetZoom() && !IsPreview() )
    {
        MapMode aMode( pMyWin->GetMapMode() );
        Fraction aNewFactor( rOpt.GetZoom(), 100 );
        aMode.SetScaleX( aNewFactor );
        aMode.SetScaleY( aNewFactor );
        pMyWin->SetMapMode( aMode );
        // if not a reference device (printer) is used for formatting,
        // but the screen, new formatting is needed for zoomfactor changes.
        if (mpOpt->getBrowseMode() || mpOpt->IsWhitespaceHidden())
            bReformat = true;
    }

    bool bBrowseModeChanged = false;
    if( mpOpt->getBrowseMode() != rOpt.getBrowseMode() )
    {
        bBrowseModeChanged = true;
        bReformat = true;
    }
    else if( mpOpt->getBrowseMode() && mpOpt->IsPrtFormat() != rOpt.IsPrtFormat() )
        bReformat = true;

    bool bHideWhitespaceModeChanged = false;
    if (mpOpt->IsWhitespaceHidden() != rOpt.IsWhitespaceHidden())
    {
        // When whitespace is hidden, view change needs reformatting.
        bHideWhitespaceModeChanged = true;
        bReformat = true;
    }

    if ( HasDrawView() || rOpt.IsGridVisible() )
    {
        if ( !HasDrawView() )
            MakeDrawView();

        SwDrawView *pDView = Imp()->GetDrawView();
        if ( pDView->IsDragStripes() != rOpt.IsCrossHair() )
            pDView->SetDragStripes( rOpt.IsCrossHair() );

        if ( pDView->IsGridSnap() != rOpt.IsSnap() )
            pDView->SetGridSnap( rOpt.IsSnap() );

        if ( pDView->IsGridVisible() != rOpt.IsGridVisible() )
            pDView->SetGridVisible( rOpt.IsGridVisible() );

        const Size &rSz = rOpt.GetSnapSize();
        pDView->SetGridCoarse( rSz );

        const Size aFSize
            ( rSz.Width() ? rSz.Width() / (rOpt.GetDivisionX()+1) : 0,
              rSz.Height()? rSz.Height()/ (rOpt.GetDivisionY()+1) : 0);
        pDView->SetGridFine( aFSize );
        Fraction aSnGrWdtX(rSz.Width(), rOpt.GetDivisionX() + 1);
        Fraction aSnGrWdtY(rSz.Height(), rOpt.GetDivisionY() + 1);
        pDView->SetSnapGridWidth( aSnGrWdtX, aSnGrWdtY );

        // set handle size to 9 pixels, always
        pDView->SetMarkHdlSizePixel(9);
    }

    bool bOnlineSpellChgd = mpOpt->IsOnlineSpell() != rOpt.IsOnlineSpell();

    *mpOpt = rOpt;   // First the options are taken.
    mpOpt->SetUIOptions(rOpt);

    mxDoc->GetDocumentSettingManager().set(DocumentSettingId::HTML_MODE, 0 != ::GetHtmlMode(mxDoc->GetDocShell()));

    if (isToggleLayoutHide)
    {
        GetLayout()->SetFieldmarkMode( rOpt.IsFieldName()
                    ? sw::FieldmarkMode::ShowCommand
                    : sw::FieldmarkMode::ShowResult,
                sw::IsShowHiddenChars(this)
                    ? sw::ParagraphBreakMode::Shown
                    : sw::ParagraphBreakMode::Hidden);
        bReformat = true;
    }

    if( bBrowseModeChanged || bHideWhitespaceModeChanged )
    {
        // #i44963# Good occasion to check if page sizes in
        // page descriptions are still set to (LONG_MAX, LONG_MAX) (html import)
        mxDoc->CheckDefaultPageFormat();
        InvalidateLayout( true );
    }

    if (SfxViewShell* pNotifySh = GetSfxViewShell())
    {
        SwXTextDocument* pModel = comphelper::getFromUnoTunnel<SwXTextDocument>(pNotifySh->GetCurrentDocument());
        SfxLokHelper::notifyViewRenderState(pNotifySh, pModel);
    }

    pMyWin->Invalidate();
    if ( bReformat )
    {
        // Nothing helps, we need to send all ContentFrames a
        // Prepare, we format anew:
        SwViewShell::StartAction();
        Reformat();
        SwViewShell::EndAction();
    }

    if (isToggleFieldNames)
    {
        for(SwViewShell& rSh : GetRingContainer())
        {
            if (SwCursorShell *const pSh = dynamic_cast<SwCursorShell *>(&rSh))
            {
                if ((mpOpt->IsFieldName() && pSh->CursorInsideInputField())
                    || IsCursorInFieldmarkHidden(*pSh->GetCursor(),
                            pSh->GetLayout()->GetFieldmarkMode()))
                {   // move cursor out of field
                    pSh->Left(1, SwCursorSkipMode::Chars);
                }
            }
        }
        // the layout changes but SetModified() wasn't called so do it here:
        mxDoc->GetDocumentLayoutManager().ClearSwLayouterEntries();
    }

    if( !bOnlineSpellChgd )
        return;

    if ( !comphelper::LibreOfficeKit::isActive() )
    {
        bool bOnlineSpl = rOpt.IsOnlineSpell();
        for(SwViewShell& rSh : GetRingContainer())
        {
            if(&rSh == this)
                continue;
            rSh.mpOpt->SetOnlineSpell( bOnlineSpl );
            vcl::Window *pTmpWin = rSh.GetWin();
            if( pTmpWin )
                pTmpWin->Invalidate();
        }
    }
}

void SwViewShell::SetUIOptions( const SwViewOption &rOpt )
{
    mpOpt->SetUIOptions(rOpt);
    //the API-Flag of the view options is set but never reset
    //it is required to set scroll bars in readonly documents
    if(rOpt.IsStarOneSetting())
        mpOpt->SetStarOneSetting(true);

    mpOpt->SetSymbolFont(rOpt.GetSymbolFont());
}

void SwViewShell::SetReadonlyOption(bool bSet)
{
    //JP 01.02.99: at readonly flag query properly
    //              and if need be format; Bug 61335

    // Are we switching from readonly to edit?
    if( bSet == mpOpt->IsReadonly() )
        return;

    // so that the flags can be queried properly.
    mpOpt->SetReadonly( false );

    bool bReformat = mpOpt->IsFieldName();

    mpOpt->SetReadonly( bSet );

    if( bReformat )
    {
        SwViewShell::StartAction();
        Reformat();
        if ( GetWin() && !comphelper::LibreOfficeKit::isActive() )
            GetWin()->Invalidate();
        SwViewShell::EndAction();
    }
    else if ( GetWin() && !comphelper::LibreOfficeKit::isActive() )
        GetWin()->Invalidate();
#if !ENABLE_WASM_STRIP_ACCESSIBILITY
    if( Imp()->IsAccessible() )
        Imp()->InvalidateAccessibleEditableState( false );
#endif
}

void  SwViewShell::SetPDFExportOption(bool bSet)
{
    if( bSet != mpOpt->IsPDFExport() )
    {
        if( bSet && mpOpt->getBrowseMode() )
            mpOpt->SetPrtFormat( true );
        mpOpt->SetPDFExport(bSet);
    }
}

void  SwViewShell::SetReadonlySelectionOption(bool bSet)
{
    if( bSet != mpOpt->IsSelectionInReadonly() )
    {
        mpOpt->SetSelectionInReadonly(bSet);
    }
}

void SwViewShell::SetPrtFormatOption( bool bSet )
{
    mpOpt->SetPrtFormat( bSet );
}

void SwViewShell::UISizeNotify()
{
    if ( mbDocSizeChgd )
    {
        mbDocSizeChgd = false;
        bool bOld = bInSizeNotify;
        bInSizeNotify = true;
        ::SizeNotify( *this, GetDocSize() );
        bInSizeNotify = bOld;
    }
}

void    SwViewShell::SetRestoreActions(sal_uInt16 nSet)
{
    OSL_ENSURE(!GetRestoreActions()||!nSet, "multiple restore of the Actions ?");
    Imp()->SetRestoreActions(nSet);
}
sal_uInt16  SwViewShell::GetRestoreActions() const
{
    return Imp()->GetRestoreActions();
}

bool SwViewShell::IsNewLayout() const
{
    return GetLayout()->IsNewLayout();
}

#if !ENABLE_WASM_STRIP_ACCESSIBILITY
rtl::Reference<comphelper::OAccessible> SwViewShell::CreateAccessible()
{
    rtl::Reference<comphelper::OAccessible> pAcc;

    // We require a layout and an XModel to be accessible.
    OSL_ENSURE( mpLayout, "no layout, no access" );
    OSL_ENSURE( GetWin(), "no window, no access" );

    if( mxDoc->getIDocumentLayoutAccess().GetCurrentViewShell() && GetWin() )
        pAcc = Imp()->GetAccessibleMap().GetDocumentView();

    return pAcc;
}

rtl::Reference<comphelper::OAccessible> SwViewShell::CreateAccessiblePreview()
{
    OSL_ENSURE( IsPreview(),
                "Can't create accessible preview for non-preview SwViewShell" );

    // We require a layout and an XModel to be accessible.
    OSL_ENSURE( mpLayout, "no layout, no access" );
    OSL_ENSURE( GetWin(), "no window, no access" );

    if ( IsPreview() && GetLayout()&& GetWin() )
    {
        return Imp()->GetAccessibleMap().GetDocumentPreview(
                    PagePreviewLayout()->maPreviewPages,
                    GetWin()->GetMapMode().GetScaleX(),
                    GetLayout()->GetPageByPageNum( PagePreviewLayout()->mnSelectedPageNum ),
                    PagePreviewLayout()->maWinSize );
    }
    return nullptr;
}

void SwViewShell::InvalidateAccessibleFocus()
{
    if( Imp() && Imp()->IsAccessible() )
        Imp()->GetAccessibleMap().InvalidateFocus();
}

/**
 * invalidate CONTENT_FLOWS_FROM/_TO relation for paragraphs #i27138#
 */
void SwViewShell::InvalidateAccessibleParaFlowRelation( const SwTextFrame* _pFromTextFrame,
                                                      const SwTextFrame* _pToTextFrame )
{
    if ( GetLayout() && GetLayout()->IsAnyShellAccessible() )
    {
        Imp()->InvalidateAccessibleParaFlowRelation_( _pFromTextFrame, _pToTextFrame );
    }
}

/**
 * invalidate text selection for paragraphs #i27301#
 */
void SwViewShell::InvalidateAccessibleParaTextSelection()
{
    if ( GetLayout() && GetLayout()->IsAnyShellAccessible() )
    {
        Imp()->InvalidateAccessibleParaTextSelection_();
    }
}

/**
 * invalidate attributes for paragraphs #i88069#
 */
void SwViewShell::InvalidateAccessibleParaAttrs( const SwTextFrame& rTextFrame )
{
    if ( GetLayout() && GetLayout()->IsAnyShellAccessible() )
    {
        Imp()->InvalidateAccessibleParaAttrs_( rTextFrame );
    }
}

SwAccessibleMap* SwViewShell::GetAccessibleMap()
{
    if ( Imp()->IsAccessible() )
    {
        return &(Imp()->GetAccessibleMap());
    }

    return nullptr;
}

void SwViewShell::ApplyAccessibilityOptions()
{
    if (comphelper::IsFuzzing())
        return;
    if (mpOpt->IsPagePreview() && !officecfg::Office::Common::Accessibility::IsForPagePreviews::get())
    {
        mpAccOptions->SetAlwaysAutoColor(false);
        mpAccOptions->SetStopAnimatedGraphics(false);
    }
    else
    {
        mpAccOptions->SetAlwaysAutoColor(officecfg::Office::Common::Accessibility::IsAutomaticFontColor::get());
        // tdf#161765: Let user choose which animation settings to use: OS's / LO's
        // New options: "System"/"No"/"Yes".
        // Do respect OS's animation setting if the user has selected the option "System"
        mpAccOptions->SetStopAnimatedGraphics(! MiscSettings::IsAnimatedGraphicAllowed());

        // Form view
        // Always set this option, not only if document is read-only:
        mpOpt->SetSelectionInReadonly(officecfg::Office::Common::Accessibility::IsSelectionInReadonly::get());
    }
}
#endif // ENABLE_WASM_STRIP_ACCESSIBILITY

ShellResource* SwViewShell::GetShellRes()
{
    return spShellRes;
}

//static
void SwViewShell::SetCareDialog(const std::shared_ptr<weld::Window>& rNew)
{
    auto& spCareDialog = getCareDialog();
    (*spCareDialog.get()) = rNew;
}

//static
weld::Window* SwViewShell::GetCareDialog(SwViewShell const & rVSh)
{
    auto& spCareDialog = getCareDialog();
    return (*spCareDialog.get()) ? spCareDialog.get()->get() : CareChildWin(rVSh);
}

sal_uInt16 SwViewShell::GetPageCount() const
{
    return GetLayout() ? GetLayout()->GetPageNum() : 1;
}

Size SwViewShell::GetPageSize( sal_uInt16 nPageNum, bool bSkipEmptyPages ) const
{
    Size aSize;
    const SwRootFrame* pTmpRoot = GetLayout();
    if( pTmpRoot && nPageNum )
    {
        const SwPageFrame* pPage = static_cast<const SwPageFrame*>
                                 (pTmpRoot->Lower());

        while( --nPageNum && pPage && pPage->GetNext() )
            pPage = static_cast<const SwPageFrame*>( pPage->GetNext() );

        if( !bSkipEmptyPages && pPage && pPage->IsEmptyPage() && pPage->GetNext() )
            pPage = static_cast<const SwPageFrame*>( pPage->GetNext() );

        if (pPage)
            aSize = pPage->getFrameArea().SSize();
    }
    return aSize;
}

void SwViewShell::OnGraphicArrived(const SwRect& rRect)
{
    for(SwViewShell& rShell : GetRingContainer())
    {
        CurrShell aCurr(&rShell);
        if(rShell.IsPreview())
        {
            if(rShell.GetWin())
                ::RepaintPagePreview(&rShell, rRect);
        }
        else if(rShell.VisArea().Overlaps(rRect) && OUTDEV_WINDOW == rShell.GetOut()->GetOutDevType())
        {
            // invalidate instead of painting
            rShell.GetWin()->Invalidate(rRect.SVRect());
        }
    }
}
// #i12836# enhanced pdf export
sal_Int32 SwViewShell::GetPageNumAndSetOffsetForPDF( OutputDevice& rOut, const SwRect& rRect ) const
{
    OSL_ENSURE( GetLayout(), "GetPageNumAndSetOffsetForPDF assumes presence of layout" );

    sal_Int32 nRet = -1;

    // #i40059# Position out of bounds:
    SwRect aRect( rRect );
    aRect.Pos().setX( std::max( aRect.Left(), GetLayout()->getFrameArea().Left() ) );

    const SwPageFrame* pPage = GetLayout()->GetPageAtPos( aRect.Center() );
    if ( pPage )
    {
        OSL_ENSURE( pPage, "GetPageNumAndSetOffsetForPDF: No page found" );

        Point aOffset( pPage->getFrameArea().Pos() );
        aOffset.setX( -aOffset.X() );
        aOffset.setY( -aOffset.Y() );

        MapMode aMapMode( rOut.GetMapMode() );
        aMapMode.SetOrigin( aOffset );
        rOut.SetMapMode( aMapMode );

        nRet = pPage->GetPhyPageNum() - 1;
    }

    return nRet;
}

// --> PB 2007-05-30 #146850#
const BitmapEx& SwViewShell::GetReplacementBitmap( bool bIsErrorState )
{
    if (bIsErrorState)
    {
        if (!m_xErrorBmp)
            m_xErrorBmp.reset(new BitmapEx(RID_GRAPHIC_ERRORBMP));
        return *m_xErrorBmp;
    }

    if (!m_xReplaceBmp)
        m_xReplaceBmp.reset(new BitmapEx(RID_GRAPHIC_REPLACEBMP));
    return *m_xReplaceBmp;
}

void SwViewShell::DeleteReplacementBitmaps()
{
    m_xErrorBmp.reset();
    m_xReplaceBmp.reset();
}

SwPostItMgr* SwViewShell::GetPostItMgr()
{
    SwView* pView =  GetDoc()->GetDocShell() ? GetDoc()->GetDocShell()->GetView() : nullptr;
    if ( pView )
        return pView->GetPostItMgr();

    return nullptr;
}

void SwViewShell::GetFirstLastVisPageNumbers(SwVisiblePageNumbers& rVisiblePageNumbers, const SwView& rView)
{
    SwRect rViewVisArea(rView.GetVisArea());
    vcl::RenderContext* pRenderContext = GetOut();
    const SwPageFrame* pPageFrame = Imp()->GetFirstVisPage(pRenderContext);
    SwRect rPageRect = pPageFrame->getFrameArea();
    rPageRect.AddBottom(-pPageFrame->GetBottomMargin());
    while (!rPageRect.Overlaps(rViewVisArea) && pPageFrame->GetNext())
    {
        pPageFrame = static_cast<const SwPageFrame*>(pPageFrame->GetNext());
        rPageRect = pPageFrame->getFrameArea();
        if (rPageRect.Top() > 0)
            rPageRect.AddBottom(-pPageFrame->GetBottomMargin());
    }
    rVisiblePageNumbers.nFirstPhy = pPageFrame->GetPhyPageNum();
    rVisiblePageNumbers.nFirstVirt = pPageFrame->GetVirtPageNum();
    const SvxNumberType& rFirstVisNum = pPageFrame->GetPageDesc()->GetNumType();
    rVisiblePageNumbers.sFirstCustomPhy = rFirstVisNum.GetNumStr(rVisiblePageNumbers.nFirstPhy);
    rVisiblePageNumbers.sFirstCustomVirt = rFirstVisNum.GetNumStr(rVisiblePageNumbers.nFirstVirt);
    pPageFrame = Imp()->GetLastVisPage(pRenderContext);
    rPageRect = pPageFrame->getFrameArea();
    rPageRect.AddTop(pPageFrame->GetTopMargin());
    while (!rPageRect.Overlaps(rViewVisArea) && pPageFrame->GetPrev())
    {
        pPageFrame = static_cast<const SwPageFrame*>(pPageFrame->GetPrev());
        rPageRect = pPageFrame->getFrameArea();
        rPageRect.AddTop(pPageFrame->GetTopMargin());
    }
    rVisiblePageNumbers.nLastPhy = pPageFrame->GetPhyPageNum();
    rVisiblePageNumbers.nLastVirt = pPageFrame->GetVirtPageNum();
    const SvxNumberType& rLastVisNum = pPageFrame->GetPageDesc()->GetNumType();
    rVisiblePageNumbers.sLastCustomPhy = rLastVisNum.GetNumStr(rVisiblePageNumbers.nLastPhy);
    rVisiblePageNumbers.sLastCustomVirt = rLastVisNum.GetNumStr(rVisiblePageNumbers.nLastVirt);
}

/*
 * Document Interface Access
 */
const IDocumentSettingAccess& SwViewShell::getIDocumentSettingAccess() const { return mxDoc->GetDocumentSettingManager(); }
IDocumentSettingAccess& SwViewShell::getIDocumentSettingAccess() { return mxDoc->GetDocumentSettingManager(); }
const IDocumentDeviceAccess& SwViewShell::getIDocumentDeviceAccess() const { return mxDoc->getIDocumentDeviceAccess(); }
IDocumentDeviceAccess& SwViewShell::getIDocumentDeviceAccess() { return mxDoc->getIDocumentDeviceAccess(); }
const IDocumentMarkAccess* SwViewShell::getIDocumentMarkAccess() const { return mxDoc->getIDocumentMarkAccess(); }
IDocumentMarkAccess* SwViewShell::getIDocumentMarkAccess() { return mxDoc->getIDocumentMarkAccess(); }
const IDocumentDrawModelAccess& SwViewShell::getIDocumentDrawModelAccess() const { return mxDoc->getIDocumentDrawModelAccess(); }
IDocumentDrawModelAccess& SwViewShell::getIDocumentDrawModelAccess() { return mxDoc->getIDocumentDrawModelAccess(); }
const IDocumentRedlineAccess& SwViewShell::getIDocumentRedlineAccess() const { return mxDoc->getIDocumentRedlineAccess(); }
IDocumentRedlineAccess& SwViewShell::getIDocumentRedlineAccess() { return mxDoc->getIDocumentRedlineAccess(); }
const IDocumentLayoutAccess& SwViewShell::getIDocumentLayoutAccess() const { return mxDoc->getIDocumentLayoutAccess(); }
IDocumentLayoutAccess& SwViewShell::getIDocumentLayoutAccess() { return mxDoc->getIDocumentLayoutAccess(); }
IDocumentContentOperations& SwViewShell::getIDocumentContentOperations() { return mxDoc->getIDocumentContentOperations(); }
IDocumentStylePoolAccess& SwViewShell::getIDocumentStylePoolAccess() { return mxDoc->getIDocumentStylePoolAccess(); }
const IDocumentStatistics& SwViewShell::getIDocumentStatistics() const { return mxDoc->getIDocumentStatistics(); }

IDocumentUndoRedo      & SwViewShell::GetIDocumentUndoRedo()
{ return mxDoc->GetIDocumentUndoRedo(); }
IDocumentUndoRedo const& SwViewShell::GetIDocumentUndoRedo() const
{ return mxDoc->GetIDocumentUndoRedo(); }

// --> OD 2007-11-14 #i83479#
const IDocumentListItems* SwViewShell::getIDocumentListItemsAccess() const
{
    return &mxDoc->getIDocumentListItems();
}

const IDocumentOutlineNodes* SwViewShell::getIDocumentOutlineNodesAccess() const
{
    return &mxDoc->getIDocumentOutlineNodes();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
