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

#include <View.hxx>

#include <vector>
#include <com/sun/star/embed/XEmbedPersist.hpp>
#include <com/sun/star/embed/XEmbeddedObject.hpp>
#include <comphelper/sequenceashashmap.hxx>
#include <tools/urlobj.hxx>
#include <svx/svdoole2.hxx>
#include <svx/svxdlg.hxx>
#include <sfx2/docfile.hxx>
#include <svx/svdundo.hxx>
#include <svx/svdpagv.hxx>
#include <svl/urlbmk.hxx>
#include <editeng/outliner.hxx>
#include <svx/xflclit.hxx>
#include <sot/formats.hxx>
#include <editeng/editeng.hxx>

#include <svtools/embedtransfer.hxx>
#include <tools/debug.hxx>

#include <anminfo.hxx>
#include <strings.hrc>
#include <sdxfer.hxx>
#include <sdresid.hxx>
#include <sdmod.hxx>
#include <sdtreelb.hxx>
#include <DrawViewShell.hxx>
#include <DrawDocShell.hxx>
#include <fudraw.hxx>
#include <drawdoc.hxx>
#include <Window.hxx>
#include <sdpage.hxx>
#include <unoaprms.hxx>
#include <helpids.h>
#include <vcl/svapp.hxx>

#include <slideshow.hxx>
#include <memory>

namespace sd {

using namespace ::com::sun::star;

namespace {

struct SdNavigatorDropEvent : public ExecuteDropEvent
{
    VclPtr< ::sd::Window>    mpTargetWindow;

    SdNavigatorDropEvent (
        const ExecuteDropEvent& rEvt,
        ::sd::Window* pTargetWindow )
        : ExecuteDropEvent( rEvt ),
          mpTargetWindow( pTargetWindow )
    {}
};

}

css::uno::Reference< css::datatransfer::XTransferable > View::CreateClipboardDataObject()
{
    // since SdTransferable::CopyToClipboard is called, this
    // dynamically created object is destroyed automatically
    rtl::Reference<SdTransferable> pTransferable = new SdTransferable( &mrDoc, nullptr, false );

    SdModule::get()->pTransferClip = pTransferable.get();

    mrDoc.CreatingDataObj( pTransferable.get() );
    pTransferable->SetWorkDocument( static_cast<SdDrawDocument*>(CreateMarkedObjModel().release()) );
    mrDoc.CreatingDataObj( nullptr );

    // #112978# need to use GetAllMarkedBoundRect instead of GetAllMarkedRect to get
    // fat lines correctly
    const ::tools::Rectangle                 aMarkRect( GetAllMarkedBoundRect() );
    // tdf#118171 - snap rectangles of objects without line width
    const ::tools::Rectangle aMarkBoundRect(GetAllMarkedRect());
    std::unique_ptr<TransferableObjectDescriptor> pObjDesc(new TransferableObjectDescriptor);
    SdrOle2Obj*                     pSdrOleObj = nullptr;
    SdrPageView*                    pPgView = GetSdrPageView();
    SdPage*                         pOldPage = pPgView ? static_cast<SdPage*>( pPgView->GetPage() ) : nullptr;
    SdPage*                         pNewPage = const_cast<SdPage*>(static_cast<const SdPage*>( pTransferable->GetWorkDocument()->GetPage( 0 ) ));

    if( pOldPage )
    {
        pNewPage->SetSize( pOldPage->GetSize() );
        pNewPage->SetLayoutName( pOldPage->GetLayoutName() );
    }

    const SdrMarkList& rMarkList = GetMarkedObjectList();
    if( rMarkList.GetMarkCount() == 1 )
    {
        SdrObject* pObj = rMarkList.GetMark(0)->GetMarkedSdrObj();

        if( auto pOle2Obj = dynamic_cast<SdrOle2Obj *>( pObj ) )
            if( pOle2Obj->GetObjRef() )
            {
                // If object has no persistence it must be copied as part of the document
                try
                {
                    uno::Reference< embed::XEmbedPersist > xPersObj( pOle2Obj->GetObjRef(), uno::UNO_QUERY );
                    if ( xPersObj.is() && xPersObj->hasEntry() )
                         pSdrOleObj = pOle2Obj;
                }
                catch( uno::Exception& )
                {}
            }
    }

    if( pSdrOleObj )
        SvEmbedTransferHelper::FillTransferableObjectDescriptor( *pObjDesc, pSdrOleObj->GetObjRef(), pSdrOleObj->GetGraphic(), pSdrOleObj->GetAspect() );
    else
        pTransferable->GetWorkDocument()->GetDocSh()->FillTransferableObjectDescriptor( *pObjDesc );

    if( mpDocSh )
        pObjDesc->maDisplayName = mpDocSh->GetMedium()->GetURLObject().GetURLNoPass();

    pObjDesc->maSize = aMarkRect.GetSize();

    pTransferable->SetStartPos( aMarkRect.TopLeft() );
    pTransferable->SetBoundStartPos(aMarkBoundRect.TopLeft());
    pTransferable->SetObjectDescriptor( std::move(pObjDesc) );
    pTransferable->CopyToClipboard( mpViewSh->GetActiveWindow() );

    return pTransferable;
}

css::uno::Reference< css::datatransfer::XTransferable > View::CreateDragDataObject( View* pWorkView, vcl::Window& rWindow, const Point& rDragPos )
{
    rtl::Reference<SdTransferable> pTransferable = new SdTransferable( &mrDoc, pWorkView, false );

    SdModule::get()->pTransferDrag = pTransferable.get();

    std::unique_ptr<TransferableObjectDescriptor> pObjDesc(new TransferableObjectDescriptor);
    OUString                        aDisplayName;
    SdrOle2Obj*                     pSdrOleObj = nullptr;

    const SdrMarkList& rMarkList = GetMarkedObjectList();
    if( rMarkList.GetMarkCount() == 1 )
    {
        SdrObject* pObj = rMarkList.GetMark(0)->GetMarkedSdrObj();

        if( auto pOle2Obj = dynamic_cast<SdrOle2Obj *>( pObj ) )
            if( pOle2Obj->GetObjRef() )
            {
                // If object has no persistence it must be copied as part of the document
                try
                {
                    uno::Reference< embed::XEmbedPersist > xPersObj( pOle2Obj->GetObjRef(), uno::UNO_QUERY );
                    if ( xPersObj.is() && xPersObj->hasEntry() )
                         pSdrOleObj = pOle2Obj;
                }
                catch( uno::Exception& )
                {}
            }
    }

    if( mpDocSh )
        aDisplayName = mpDocSh->GetMedium()->GetURLObject().GetURLNoPass();

    if( pSdrOleObj )
        SvEmbedTransferHelper::FillTransferableObjectDescriptor( *pObjDesc, pSdrOleObj->GetObjRef(), pSdrOleObj->GetGraphic(), pSdrOleObj->GetAspect() );
    else if (mpDocSh)
        mpDocSh->FillTransferableObjectDescriptor( *pObjDesc );

    pObjDesc->maSize = GetAllMarkedRect().GetSize();
    pObjDesc->maDragStartPos = rDragPos;
    pObjDesc->maDisplayName = aDisplayName;

    pTransferable->SetStartPos( rDragPos );
    pTransferable->SetObjectDescriptor( std::move(pObjDesc) );
    pTransferable->StartDrag( &rWindow, DND_ACTION_COPYMOVE | DND_ACTION_LINK );

    return pTransferable;
}

css::uno::Reference< css::datatransfer::XTransferable > View::CreateSelectionDataObject( View* pWorkView )
{
    rtl::Reference<SdTransferable> pTransferable = new SdTransferable( &mrDoc, pWorkView, true );
    std::unique_ptr<TransferableObjectDescriptor> pObjDesc(new TransferableObjectDescriptor);
    const ::tools::Rectangle                 aMarkRect( GetAllMarkedRect() );

    SdModule::get()->pTransferSelection = pTransferable.get();

    if( mpDocSh )
    {
        mpDocSh->FillTransferableObjectDescriptor( *pObjDesc );
        pObjDesc->maDisplayName = mpDocSh->GetMedium()->GetURLObject().GetURLNoPass();
    }

    pObjDesc->maSize = aMarkRect.GetSize();

    pTransferable->SetStartPos( aMarkRect.TopLeft() );
    pTransferable->SetObjectDescriptor( std::move(pObjDesc) );
    pTransferable->CopyToPrimarySelection();

    return pTransferable;
}

void View::UpdateSelectionClipboard() // false case
{
    if (!mpViewSh)
        return;
    if (!mpViewSh->GetActiveWindow())
        return;
    const SdrMarkList& rMarkList = GetMarkedObjectList();
    if (rMarkList.GetMarkCount())
        CreateSelectionDataObject( this );
    else
        ClearSelectionClipboard();
}

void View::ClearSelectionClipboard() // true case
{
    if (!mpViewSh)
        return;
    if (!mpViewSh->GetActiveWindow())
        return;
    SdModule* mod = SdModule::get();
    if (mod->pTransferSelection && mod->pTransferSelection->GetView() == this)
    {
        TransferableHelper::ClearPrimarySelection();
        mod->pTransferSelection = nullptr;
    }
}

void View::DoCut()
{
    const OutlinerView* pOLV = GetTextEditOutlinerView();

    const SdrMarkList& rMarkList = GetMarkedObjectList();
    if( pOLV )
        const_cast<OutlinerView*>(pOLV)->Cut();
    else if( rMarkList.GetMarkCount() != 0 )
    {
        OUString aStr(SdResId(STR_UNDO_CUT));

        DoCopy();
        BegUndo(aStr + " " + rMarkList.GetMarkDescription());
        DeleteMarked();
        EndUndo();
    }
}

void View::DoCopy(bool /*bMergeMasterPagesOnly*/)
{
    const OutlinerView* pOLV = GetTextEditOutlinerView();

    const SdrMarkList& rMarkList = GetMarkedObjectList();
    if( pOLV )
        const_cast<OutlinerView*>(pOLV)->Copy();
    else if( rMarkList.GetMarkCount() != 0 )
    {
        BrkAction();
        CreateClipboardDataObject();
    }
}

void View::DoPaste (::sd::Window* pWindow,bool /*bMergeMasterPagesOnly*/)
{
    TransferableDataHelper aDataHelper( TransferableDataHelper::CreateFromSystemClipboard( mpViewSh->GetActiveWindow() ) );
    if( !aDataHelper.GetTransferable().is() )
        return; // empty clipboard?

    const OutlinerView* pOLV = GetTextEditOutlinerView();

    if( pOLV && EditEngine::HasValidData( aDataHelper.GetTransferable() ) )
    {
        const_cast< OutlinerView* >(pOLV)->PasteSpecial();

        SdrObject*  pObj = GetTextEditObject();
        SdPage*     pPage = static_cast<SdPage*>( pObj ? pObj->getSdrPageFromSdrObject() : nullptr );
        ::Outliner& rOutliner = pOLV->GetOutliner();

        if( pObj && pPage && pPage->GetPresObjKind(pObj) == PresObjKind::Title )
        {
            // remove all hard linebreaks from the title
            if (rOutliner.GetParagraphCount() > 1)
            {
                bool bOldUpdateMode = rOutliner.SetUpdateLayout( false );

                const EditEngine& rEdit = rOutliner.GetEditEngine();
                const sal_Int32 nParaCount = rEdit.GetParagraphCount();

                for( sal_Int32 nPara = nParaCount - 2; nPara >= 0; nPara-- )
                {
                    const sal_Int32 nParaLen = rEdit.GetTextLen( nPara );
                    rOutliner.QuickInsertLineBreak(ESelection(nPara, nParaLen, nPara + 1, 0));
                }

                DBG_ASSERT( rEdit.GetParagraphCount() <= 1, "Titleobject contains hard line breaks" );
                rOutliner.SetUpdateLayout(bOldUpdateMode);
            }
        }

        if( !mrDoc.IsChanged() )
        {
            if (rOutliner.IsModified())
                mrDoc.SetChanged();
        }
    }
    else
    {
        Point aPos = pWindow->GetVisibleCenter();
        DrawViewShell* pDrViewSh = static_cast<DrawViewShell*>( mpDocSh->GetViewShell() );

        if (pDrViewSh != nullptr)
        {
            sal_Int8    nDnDAction = DND_ACTION_COPY;
            if( !InsertData( aDataHelper, aPos, nDnDAction, false ) )
            {
                INetBookmark    aINetBookmark( u""_ustr, u""_ustr );

                if( ( aDataHelper.HasFormat( SotClipboardFormatId::NETSCAPE_BOOKMARK ) &&
                      aDataHelper.GetINetBookmark( SotClipboardFormatId::NETSCAPE_BOOKMARK, aINetBookmark ) ) ||
                    ( aDataHelper.HasFormat( SotClipboardFormatId::FILEGRPDESCRIPTOR ) &&
                      aDataHelper.GetINetBookmark( SotClipboardFormatId::FILEGRPDESCRIPTOR, aINetBookmark ) ) ||
                    ( aDataHelper.HasFormat( SotClipboardFormatId::UNIFORMRESOURCELOCATOR ) &&
                      aDataHelper.GetINetBookmark( SotClipboardFormatId::UNIFORMRESOURCELOCATOR, aINetBookmark ) ) )
                {
                    pDrViewSh->InsertURLField(aINetBookmark.GetURL(), aINetBookmark.GetDescription(), u""_ustr, u""_ustr);
                }
            }
        }
    }
}

void View::StartDrag( const Point& rStartPos, vcl::Window* pWindow )
{
    const SdrMarkList& rMarkList = GetMarkedObjectList();
    if (rMarkList.GetMarkCount() == 0 || !IsAction() || !mpViewSh || !pWindow)
        return;

    BrkAction();

    if( IsTextEdit() )
        SdrEndTextEdit();

    if (DrawViewShell* pDrawViewShell = dynamic_cast<DrawViewShell*>(mpDocSh ? mpDocSh->GetViewShell() : nullptr))
    {
        const rtl::Reference<FuPoor>& xFunction(pDrawViewShell->GetCurrentFunction());
        if (FuDraw* pFunction = dynamic_cast<FuDraw*>(xFunction.get()))
            pFunction->ForcePointer();
    }

    mpDragSrcMarkList.reset( new SdrMarkList(GetMarkedObjectList()) );
    mnDragSrcPgNum = GetSdrPageView()->GetPage()->GetPageNum();

    CreateDragDataObject( this, *pWindow, rStartPos );
}

void View::DragFinished( sal_Int8 nDropAction )
{
    const bool bUndo = IsUndoEnabled();
    const bool bGroupUndo = bUndo && mpDragSrcMarkList;
    if (bGroupUndo)
    {
        OUString aStr(SdResId(STR_UNDO_DRAGDROP));
        BegUndo(aStr + " " + mpDragSrcMarkList->GetMarkDescription());
    }

    SdTransferable* pDragTransferable = SdModule::get()->pTransferDrag;

    if( pDragTransferable )
        pDragTransferable->SetView( nullptr );

    if( ( nDropAction & DND_ACTION_MOVE ) &&
        pDragTransferable && !pDragTransferable->IsInternalMove() &&
        mpDragSrcMarkList && mpDragSrcMarkList->GetMarkCount() &&
        !IsPresObjSelected() )
    {
        mpDragSrcMarkList->ForceSort();

        if( bUndo )
            BegUndo();

        const size_t nCnt = mpDragSrcMarkList->GetMarkCount();

        for( size_t nm = nCnt; nm>0; )
        {
            --nm;
            SdrMark* pM=mpDragSrcMarkList->GetMark(nm);
            if( bUndo )
                AddUndo(mrDoc.GetSdrUndoFactory().CreateUndoDeleteObject(*pM->GetMarkedSdrObj()));
        }

        mpDragSrcMarkList->GetMark(0)->GetMarkedSdrObj()->GetOrdNum();

        for (size_t nm = nCnt; nm>0;)
        {
            --nm;
            SdrMark* pM=mpDragSrcMarkList->GetMark(nm);
            SdrObject* pObj=pM->GetMarkedSdrObj();

            if( pObj && pObj->getSdrPageFromSdrObject() )
            {
                const size_t nOrdNum = pObj->GetOrdNumDirect();
                rtl::Reference<SdrObject> pChkObj = pObj->getSdrPageFromSdrObject()->RemoveObject(nOrdNum);
                DBG_ASSERT(pChkObj.get()==pObj,"pChkObj!=pObj in RemoveObject()");
            }
        }

        if( bUndo )
            EndUndo();
    }

    if( pDragTransferable )
        pDragTransferable->SetInternalMove( false );

    if (bGroupUndo)
        EndUndo();
    mnDragSrcPgNum = SDRPAGE_NOTFOUND;
    mpDragSrcMarkList.reset();
}

sal_Int8 View::AcceptDrop( const AcceptDropEvent& rEvt, DropTargetHelper& rTargetHelper,
                           SdrLayerID nLayer )
{
    OUString        aLayerName = GetActiveLayer();
    SdrPageView*    pPV = GetSdrPageView();
    sal_Int8        nDropAction = rEvt.mnAction;
    sal_Int8        nRet = DND_ACTION_NONE;

    if( nLayer != SDRLAYER_NOTFOUND )
    {
        SdrLayerAdmin& rLayerAdmin = mrDoc.GetLayerAdmin();
        SdrLayer* pLayer = rLayerAdmin.GetLayerPerID(nLayer);
        assert(pLayer && "layer missing");
        aLayerName = pLayer->GetName();
    }

    if( mbIsDropAllowed && !pPV->IsLayerLocked( aLayerName ) && pPV->IsLayerVisible( aLayerName ) )
    {
        const OutlinerView* pOLV = GetTextEditOutlinerView();
        bool                bIsInsideOutlinerView = false;

        if( pOLV )
        {
            ::tools::Rectangle aRect( pOLV->GetOutputArea() );

            const SdrMarkList& rMarkList = GetMarkedObjectList();
            if (rMarkList.GetMarkCount() == 1)
            {
                SdrMark* pMark = rMarkList.GetMark(0);
                SdrObject* pObj = pMark->GetMarkedSdrObj();
                aRect.Union( pObj->GetLogicRect() );
            }

            if( aRect.Contains( pOLV->GetWindow()->PixelToLogic( rEvt.maPosPixel ) ) )
            {
                bIsInsideOutlinerView = true;
            }
        }

        if( !bIsInsideOutlinerView )
        {
            SdTransferable* pDragTransferable = SdModule::get()->pTransferDrag;

            if(pDragTransferable && (nDropAction & DND_ACTION_LINK))
            {
                // suppress own data when it's intention is to use it as fill information
                pDragTransferable = nullptr;
            }

            if( pDragTransferable )
            {
                const View* pSourceView = pDragTransferable->GetView();

                if( pDragTransferable->IsPageTransferable() )
                {
                    nRet = DND_ACTION_COPY;
                }
                else if( pSourceView )
                {
                    if( !( nDropAction & DND_ACTION_LINK ) ||
                        !pSourceView->GetDocSh()->GetMedium()->GetName().isEmpty() )
                    {
                        nRet = nDropAction;
                    }
                }
            }
            else
            {
                const bool  bDrawing = rTargetHelper.IsDropFormatSupported( SotClipboardFormatId::DRAWING );
                const bool  bGraphic = rTargetHelper.IsDropFormatSupported( SotClipboardFormatId::SVXB );
                const bool  bMtf = rTargetHelper.IsDropFormatSupported( SotClipboardFormatId::GDIMETAFILE );
                const bool  bBitmap = rTargetHelper.IsDropFormatSupported( SotClipboardFormatId::BITMAP );
                bool        bBookmark = rTargetHelper.IsDropFormatSupported( SotClipboardFormatId::NETSCAPE_BOOKMARK );
                bool        bXFillExchange = rTargetHelper.IsDropFormatSupported( SotClipboardFormatId::XFA );

                // check handle insert
                if ((bXFillExchange && (SdrDragMode::Gradient == GetDragMode()))
                    || (SdrDragMode::Transparence == GetDragMode()))
                {
                    const SdrHdlList& rHdlList = GetHdlList();

                    for( size_t n = 0; n < rHdlList.GetHdlCount(); ++n )
                    {
                        SdrHdl* pIAOHandle = rHdlList.GetHdl( n );

                        if( pIAOHandle && ( SdrHdlKind::Color == pIAOHandle->GetKind() ) )
                        {
                            if(pIAOHandle->getOverlayObjectList().isHitPixel(rEvt.maPosPixel))
                            {
                                nRet = nDropAction;
                                static_cast< SdrHdlColor* >( pIAOHandle )->SetSize( SDR_HANDLE_COLOR_SIZE_SELECTED );
                            }
                            else
                            {
                                static_cast< SdrHdlColor* >( pIAOHandle )->SetSize( SDR_HANDLE_COLOR_SIZE_NORMAL );
                            }
                        }
                    }
                }

                // check object insert
                if( !nRet && ( bXFillExchange || ( ( bDrawing || bGraphic || bMtf || bBitmap || bBookmark ) && ( nDropAction & DND_ACTION_LINK ) ) ) )
                {
                    SdrPageView*    pPageView = nullptr;
                    ::sd::Window* pWindow = mpViewSh->GetActiveWindow();
                    Point           aPos( pWindow->PixelToLogic( rEvt.maPosPixel ) );
                    SdrObject* pPickObj = PickObj(aPos, getHitTolLog(), pPageView);
                    bool            bIsPresTarget = false;

                    if (pPickObj && (pPickObj->IsEmptyPresObj() || pPickObj->GetUserCall()))
                    {
                        SdPage* pPage = static_cast<SdPage*>( pPickObj->getSdrPageFromSdrObject() );

                        if( pPage && pPage->IsMasterPage() )
                            bIsPresTarget = pPage->IsPresObj( pPickObj );
                    }

                    if (pPickObj && !bIsPresTarget && (bGraphic || bMtf || bBitmap || bXFillExchange))
                    {
                        if( mpDropMarkerObj != pPickObj )
                        {
                            mpDropMarkerObj = pPickObj;
                            ImplClearDrawDropMarker();

                            if(mpDropMarkerObj)
                            {
                                mpDropMarker.reset( new SdrDropMarkerOverlay(*this, *mpDropMarkerObj) );
                            }
                        }

                        nRet = nDropAction;
                    }
                    else
                        bXFillExchange = false;
                }

                // check normal insert
                if( !nRet )
                {
                    const bool  bSBAFormat = rTargetHelper.IsDropFormatSupported( SotClipboardFormatId::SVX_FORMFIELDEXCH );
                    const bool  bEditEngineODF = rTargetHelper.IsDropFormatSupported( SotClipboardFormatId::EDITENGINE_ODF_TEXT_FLAT );
                    const bool  bString = rTargetHelper.IsDropFormatSupported( SotClipboardFormatId::STRING );
                    const bool  bRTF = rTargetHelper.IsDropFormatSupported( SotClipboardFormatId::RTF );
                    const bool  bFile = rTargetHelper.IsDropFormatSupported( SotClipboardFormatId::SIMPLE_FILE );
                    const bool  bFileList = rTargetHelper.IsDropFormatSupported( SotClipboardFormatId::FILE_LIST );

                    if( mpDropMarker )
                    {
                        ImplClearDrawDropMarker();
                        mpDropMarkerObj = nullptr;
                    }

                    if( bBookmark && bFile && ( nDropAction & DND_ACTION_MOVE ) && mpViewSh
                        && ( SlideShow::IsRunning(mpViewSh->GetViewShellBase())
                        && !SlideShow::IsInteractiveSlideshow(&mpViewSh->GetViewShellBase()) )) // IASS
                        bBookmark = false;

                    if( bDrawing || bGraphic || bMtf || bBitmap || bBookmark || bFile || bFileList || bXFillExchange || bSBAFormat || bEditEngineODF || bString || bRTF )
                        nRet = nDropAction;

                    // For entries from the navigator, change action copy.
                    if (bBookmark
                        && rTargetHelper.IsDropFormatSupported(
                            SdPageObjsTLV::SdPageObjsTransferable::GetListBoxDropFormatId())
                        && (nDropAction & DND_ACTION_MOVE)!=0)
                    {
                        nRet = DND_ACTION_COPY;
                    }
                }
            }
        }
    }

    // destroy drop marker if this is a leaving event
    if( rEvt.mbLeaving && mpDropMarker )
    {
        ImplClearDrawDropMarker();
        mpDropMarkerObj = nullptr;
    }

    return nRet;
}

sal_Int8 View::ExecuteDrop( const ExecuteDropEvent& rEvt,
                              ::sd::Window* pTargetWindow, sal_uInt16 nPage, SdrLayerID nLayer )
{
    SdrPageView*    pPV = GetSdrPageView();
    OUString        aActiveLayer = GetActiveLayer();
    sal_Int8        nDropAction = rEvt.mnAction;
    sal_Int8        nRet = DND_ACTION_NONE;

    // destroy drop marker if it is shown
    if( mpDropMarker )
    {
        ImplClearDrawDropMarker();
        mpDropMarkerObj = nullptr;
    }

    if( !pPV->IsLayerLocked( aActiveLayer ) )
    {
        const OutlinerView* pOLV = GetTextEditOutlinerView();
        bool                bIsInsideOutlinerView = false;

        if( pOLV )
        {
            ::tools::Rectangle aRect( pOLV->GetOutputArea() );

            const SdrMarkList& rMarkList = GetMarkedObjectList();
            if( rMarkList.GetMarkCount() == 1 )
            {
                SdrMark* pMark = rMarkList.GetMark(0);
                SdrObject* pObj = pMark->GetMarkedSdrObj();
                aRect.Union( pObj->GetLogicRect() );
            }

            Point aPos( pOLV->GetWindow()->PixelToLogic( rEvt.maPosPixel ) );

            if( aRect.Contains( aPos ) )
            {
                bIsInsideOutlinerView = true;
            }
        }

        if( !bIsInsideOutlinerView )
        {
            Point                   aPos;
            TransferableDataHelper  aDataHelper( rEvt.maDropEvent.Transferable );

            if( pTargetWindow )
                aPos = pTargetWindow->PixelToLogic( rEvt.maPosPixel );

            // handle insert?
            if ((SdrDragMode::Gradient == GetDragMode())
                || ((SdrDragMode::Transparence == GetDragMode())
                    && aDataHelper.HasFormat(SotClipboardFormatId::XFA)))
            {
                const SdrHdlList& rHdlList = GetHdlList();

                for( size_t n = 0; !nRet && n < rHdlList.GetHdlCount(); ++n )
                {
                    SdrHdl* pIAOHandle = rHdlList.GetHdl( n );

                    if( pIAOHandle && ( SdrHdlKind::Color == pIAOHandle->GetKind() ) )
                    {
                        if(pIAOHandle->getOverlayObjectList().isHitPixel(rEvt.maPosPixel))
                        {
                            uno::Any const data(aDataHelper.GetAny(SotClipboardFormatId::XFA, u""_ustr));
                            uno::Sequence<beans::NamedValue> props;
                            if (data >>= props)
                            {
                                ::comphelper::SequenceAsHashMap const map(props);
                                Color aColor(COL_BLACK);
                                auto const it = map.find(u"FillColor"_ustr);
                                if (it != map.end())
                                {
                                    XFillColorItem color;
                                    color.PutValue(it->second, 0);
                                    aColor = color.GetColorValue();
                                }
                                static_cast< SdrHdlColor* >( pIAOHandle )->SetColor( aColor, true );
                                nRet = nDropAction;
                            }
                        }
                    }
                }
            }

            // standard insert?
            if( !nRet && InsertData( aDataHelper, aPos, nDropAction, true, SotClipboardFormatId::NONE, nPage, nLayer ) )
                nRet = nDropAction;

            // special insert?
            if( !nRet && mpViewSh )
            {
                INetBookmark    aINetBookmark( (OUString()), (OUString()) );

                // insert bookmark
                if( aDataHelper.HasFormat( SotClipboardFormatId::NETSCAPE_BOOKMARK ) &&
                    aDataHelper.GetINetBookmark( SotClipboardFormatId::NETSCAPE_BOOKMARK, aINetBookmark ) )
                {
                    SdPageObjsTLV::SdPageObjsTransferable* pPageObjsTransferable = SdPageObjsTLV::SdPageObjsTransferable::getImplementation( aDataHelper.GetXTransferable() );

                    if( pPageObjsTransferable &&
                        ( NAVIGATOR_DRAGTYPE_LINK == pPageObjsTransferable->GetDragType() ||
                          NAVIGATOR_DRAGTYPE_EMBEDDED == pPageObjsTransferable->GetDragType() ) )
                    {
                        // insert bookmark from own navigator (handled async. due to possible message box )
                        Application::PostUserEvent( LINK( this, View, ExecuteNavigatorDrop ),
                                                    new SdNavigatorDropEvent( rEvt, pTargetWindow ) );
                        nRet = nDropAction;
                    }
                    else
                    {
                        SdrPageView*    pPageView = nullptr;

                        SdrObject* pPickObj = PickObj(aPos, getHitTolLog(), pPageView);
                        if (pPickObj)
                        {
                            // insert as clip action => jump
                            OUString       aBookmark( aINetBookmark.GetURL() );
                            SdAnimationInfo*    pInfo = SdDrawDocument::GetAnimationInfo( pPickObj );

                            if( !aBookmark.isEmpty() )
                            {
                                bool bCreated = false;

                                presentation::ClickAction eClickAction = presentation::ClickAction_DOCUMENT;

                                sal_Int32 nIndex = aBookmark.indexOf( '#' );
                                if( nIndex != -1 )
                                {
                                    const std::u16string_view aDocName( aBookmark.subView( 0, nIndex ) );

                                    if (mpDocSh->GetMedium()->GetName() == aDocName || aDocName == mpDocSh->GetName())
                                    {
                                        // internal jump, only use the part after and including '#'
                                        eClickAction = presentation::ClickAction_BOOKMARK;
                                        aBookmark = aBookmark.copy( nIndex+1 );
                                    }
                                }

                                if( !pInfo )
                                {
                                    pInfo = SdDrawDocument::GetShapeUserData( *pPickObj, true );
                                    bCreated = true;
                                }

                                // create undo action with old and new sizes
                                std::unique_ptr<SdAnimationPrmsUndoAction> pAction(new SdAnimationPrmsUndoAction(mrDoc, pPickObj, bCreated));
                                pAction->SetActive(pInfo->mbActive, pInfo->mbActive);
                                pAction->SetEffect(pInfo->meEffect, pInfo->meEffect);
                                pAction->SetTextEffect(pInfo->meTextEffect, pInfo->meTextEffect);
                                pAction->SetSpeed(pInfo->meSpeed, pInfo->meSpeed);
                                pAction->SetDim(pInfo->mbDimPrevious, pInfo->mbDimPrevious);
                                pAction->SetDimColor(pInfo->maDimColor, pInfo->maDimColor);
                                pAction->SetDimHide(pInfo->mbDimHide, pInfo->mbDimHide);
                                pAction->SetSoundOn(pInfo->mbSoundOn, pInfo->mbSoundOn);
                                pAction->SetSound(pInfo->maSoundFile, pInfo->maSoundFile);
                                pAction->SetPlayFull(pInfo->mbPlayFull, pInfo->mbPlayFull);
                                pAction->SetClickAction(pInfo->meClickAction, eClickAction);
                                pAction->SetBookmark(pInfo->GetBookmark(), aBookmark);
                                pAction->SetVerb(pInfo->mnVerb, pInfo->mnVerb);
                                pAction->SetSecondEffect(pInfo->meSecondEffect, pInfo->meSecondEffect);
                                pAction->SetSecondSpeed(pInfo->meSecondSpeed, pInfo->meSecondSpeed);
                                pAction->SetSecondSoundOn(pInfo->mbSecondSoundOn, pInfo->mbSecondSoundOn);
                                pAction->SetSecondPlayFull(pInfo->mbSecondPlayFull, pInfo->mbSecondPlayFull);

                                OUString aString(SdResId(STR_UNDO_ANIMATION));
                                pAction->SetComment(aString);
                                mpDocSh->GetUndoManager()->AddUndoAction(std::move(pAction));
                                pInfo->meClickAction = eClickAction;
                                pInfo->SetBookmark( aBookmark );
                                mrDoc.SetChanged();

                                nRet = nDropAction;
                            }
                        }
                        else if( auto pDrawViewShell = dynamic_cast< DrawViewShell *>( mpViewSh ) )
                        {
                            // insert as normal URL button
                            pDrawViewShell->InsertURLButton( aINetBookmark.GetURL(), aINetBookmark.GetDescription(), OUString(), &aPos );
                            nRet = nDropAction;
                        }
                    }
                }
            }
        }
    }

    return nRet;
}

IMPL_LINK( View, ExecuteNavigatorDrop, void*, p, void )
{
    SdNavigatorDropEvent*                   pSdNavigatorDropEvent = static_cast<SdNavigatorDropEvent*>(p);
    TransferableDataHelper                  aDataHelper( pSdNavigatorDropEvent->maDropEvent.Transferable );
    SdPageObjsTLV::SdPageObjsTransferable*  pPageObjsTransferable = SdPageObjsTLV::SdPageObjsTransferable::getImplementation( aDataHelper.GetXTransferable() );
    INetBookmark                            aINetBookmark;

    if( pPageObjsTransferable && aDataHelper.GetINetBookmark( SotClipboardFormatId::NETSCAPE_BOOKMARK, aINetBookmark ) )
    {
        Point   aPos;
        OUString  aBookmark;
        SdPage* pPage = static_cast<SdPage*>( GetSdrPageView()->GetPage() );
        sal_uInt16  nPgPos = 0xFFFF;

        if( pSdNavigatorDropEvent->mpTargetWindow )
            aPos = pSdNavigatorDropEvent->mpTargetWindow->PixelToLogic( pSdNavigatorDropEvent->maPosPixel );

        const OUString& aURL( aINetBookmark.GetURL() );
        sal_Int32 nIndex = aURL.indexOf( '#' );
        if( nIndex != -1 )
            aBookmark = aURL.copy( nIndex+1 );

        std::vector<OUString> aExchangeList;
        std::vector<OUString> aBookmarkList(1,aBookmark);

        if( !pPage->IsMasterPage() )
        {
            if( pPage->GetPageKind() == PageKind::Standard )
                nPgPos = pPage->GetPageNum() + 2;
            else if( pPage->GetPageKind() == PageKind::Notes )
                nPgPos = pPage->GetPageNum() + 1;
        }

        /* In order t ensure unique page names, we test the ones we want to
           insert. If necessary. we put them into and replacement list (bNameOK
           == sal_False -> User canceled).  */
        bool    bLink = pPageObjsTransferable->GetDragType() == NAVIGATOR_DRAGTYPE_LINK;
        bool    bNameOK = GetExchangeList( aExchangeList, aBookmarkList, 2 );

        /* Since we don't know the type (page or object), we fill a list with
           pages and objects.
           Of course we have problems if there are pages and objects with the
           same name!!! */
        if( bNameOK )
        {
            mrDoc.InsertBookmark( aBookmarkList, aExchangeList,
                                  bLink, nPgPos,
                                  &pPageObjsTransferable->GetDocShell(),
                                  &aPos );
        }
    }

    delete pSdNavigatorDropEvent;
}

bool View::GetExchangeList (std::vector<OUString> &rExchangeList,
                            std::vector<OUString> &rBookmarkList,
                            const sal_uInt16 nType)
{
    assert(rExchangeList.empty());

    bool bListIdentical = true; ///< Bookmark list and exchange list are identical
    bool bNameOK = true;        ///< name is unique

    for ( const auto& rBookmark : rBookmarkList )
    {
        OUString aNewName = rBookmark;

        if( nType == 0  || nType == 2 )
            bNameOK = mpDocSh->CheckPageName(mpViewSh->GetFrameWeld(), aNewName);

        if( bNameOK && ( nType == 1  || nType == 2 ) )
        {
            if( mrDoc.GetObj( aNewName ) )
            {
                OUString aTitle(SdResId(STR_TITLE_NAMEGROUP));
                OUString aDesc(SdResId(STR_DESC_NAMEGROUP));

                SvxAbstractDialogFactory* pFact = SvxAbstractDialogFactory::Create();
                ScopedVclPtr<AbstractSvxNameDialog> pDlg(pFact->CreateSvxNameDialog(mpViewSh->GetFrameWeld(), aNewName, aDesc));

                pDlg->SetEditHelpId( HID_SD_NAMEDIALOG_OBJECT );

                bNameOK = false;
                pDlg->SetText( aTitle );

                while( !bNameOK && pDlg->Execute() == RET_OK )
                {
                    aNewName = pDlg->GetName();

                    if( !mrDoc.GetObj( aNewName ) )
                        bNameOK = true;
                }
            }
        }

        bListIdentical = rBookmark == aNewName;

        rExchangeList.push_back(aNewName);

        if (!bNameOK)
            break;
    }

    // Exchange list is identical to bookmark list
    if( !rExchangeList.empty() && bListIdentical )
        rExchangeList.clear();

    return bNameOK;
}

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
