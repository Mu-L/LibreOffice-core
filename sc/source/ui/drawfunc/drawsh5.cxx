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

#include <editeng/eeitem.hxx>

#include <sfx2/viewfrm.hxx>
#include <sfx2/request.hxx>
#include <sfx2/bindings.hxx>
#include <tools/urlobj.hxx>
#include <cliputil.hxx>
#include <svx/svxdlg.hxx>
#include <svx/hlnkitem.hxx>
#include <svx/svdoole2.hxx>
#include <svx/svdouno.hxx>
#include <svx/extrusionbar.hxx>
#include <svx/fontworkbar.hxx>
#include <svx/svdogrp.hxx>
#include <sfx2/docfile.hxx>
#include <osl/diagnose.h>
#include <svx/diagram/IDiagramHelper.hxx>

#include <com/sun/star/form/FormButtonType.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/beans/XPropertySetInfo.hpp>

#include <drawsh.hxx>
#include <drawview.hxx>
#include <viewdata.hxx>
#include <tabvwsh.hxx>
#include <docsh.hxx>
#include <undotab.hxx>
#include <drwlayer.hxx>
#include <drtxtob.hxx>
#include <memory>

#include <sc.hrc>

using namespace com::sun::star;

void ScDrawShell::GetHLinkState( SfxItemSet& rSet )             //  Hyperlink
{
    ScDrawView* pView = rViewData.GetScDrawView();
    const SdrMarkList& rMarkList = pView->GetMarkedObjectList();

        //  Hyperlink

    SvxHyperlinkItem aHLinkItem;

    if ( rMarkList.GetMarkCount() == 1 )              // URL-Button marked ?
    {
        SdrObject* pObj = rMarkList.GetMark(0)->GetMarkedSdrObj();
        if ( pObj && !pObj->getHyperlink().isEmpty() )
        {
            aHLinkItem.SetURL( pObj->getHyperlink() );
            aHLinkItem.SetInsertMode(HLINK_FIELD);
        }
        SdrUnoObj* pUnoCtrl = dynamic_cast<SdrUnoObj*>( pObj );
        if (pUnoCtrl && SdrInventor::FmForm == pUnoCtrl->GetObjInventor())
        {
            const uno::Reference<awt::XControlModel>& xControlModel = pUnoCtrl->GetUnoControlModel();
            OSL_ENSURE( xControlModel.is(), "UNO-Control without model" );
            if( !xControlModel.is() )
                return;

            uno::Reference< beans::XPropertySet > xPropSet( xControlModel, uno::UNO_QUERY );
            uno::Reference< beans::XPropertySetInfo > xInfo = xPropSet->getPropertySetInfo();

            OUString sPropButtonType( u"ButtonType"_ustr );

            if(xInfo->hasPropertyByName( sPropButtonType ))
            {
                uno::Any aAny = xPropSet->getPropertyValue( sPropButtonType );
                form::FormButtonType eTmp;
                if ( (aAny >>= eTmp) && eTmp == form::FormButtonType_URL )
                {
                    OUString sTmp;
                    // Label
                    OUString sPropLabel( u"Label"_ustr );
                    if(xInfo->hasPropertyByName( sPropLabel ))
                    {
                        aAny = xPropSet->getPropertyValue( sPropLabel );
                        if ( (aAny >>= sTmp) && !sTmp.isEmpty() )
                        {
                            aHLinkItem.SetName(sTmp);
                        }
                    }
                    // URL
                    OUString sPropTargetURL( u"TargetURL"_ustr );
                    if(xInfo->hasPropertyByName( sPropTargetURL ))
                    {
                        aAny = xPropSet->getPropertyValue( sPropTargetURL );
                        if ( (aAny >>= sTmp) && !sTmp.isEmpty() )
                        {
                            aHLinkItem.SetURL(sTmp);
                        }
                    }
                    // Target
                    OUString sPropTargetFrame( u"TargetFrame"_ustr );
                    if(xInfo->hasPropertyByName( sPropTargetFrame ))
                    {
                        aAny = xPropSet->getPropertyValue( sPropTargetFrame );
                        if ( (aAny >>= sTmp) && !sTmp.isEmpty() )
                        {
                            aHLinkItem.SetTargetFrame(sTmp);
                        }
                    }
                    aHLinkItem.SetInsertMode(HLINK_BUTTON);
                }
            }
        }
    }

    rSet.Put(aHLinkItem);
}

void ScDrawShell::ExecuteHLink( const SfxRequest& rReq )
{
    const SfxItemSet* pReqArgs = rReq.GetArgs();

    sal_uInt16 nSlot = rReq.GetSlot();
    switch ( nSlot )
    {
        case SID_HYPERLINK_SETLINK:
            if( pReqArgs )
            {
                const SfxPoolItem* pItem;
                if ( pReqArgs->GetItemState( SID_HYPERLINK_SETLINK, true, &pItem ) == SfxItemState::SET )
                {
                    const SvxHyperlinkItem* pHyper = static_cast<const SvxHyperlinkItem*>(pItem);
                    const OUString& rName     = pHyper->GetName();
                    const OUString& rURL      = pHyper->GetURL();
                    const OUString& rTarget   = pHyper->GetTargetFrame();
                    SvxLinkInsertMode eMode = pHyper->GetInsertMode();

                    bool bDone = false;
                    if ( eMode == HLINK_FIELD || eMode == HLINK_BUTTON )
                    {
                        ScDrawView* pView = rViewData.GetScDrawView();
                        const SdrMarkList& rMarkList = pView->GetMarkedObjectList();
                        if ( rMarkList.GetMarkCount() == 1 )
                        {
                            SdrObject* pObj = rMarkList.GetMark(0)->GetMarkedSdrObj();
                            SdrUnoObj* pUnoCtrl = dynamic_cast<SdrUnoObj*>( pObj  );
                            if (pUnoCtrl && SdrInventor::FmForm == pUnoCtrl->GetObjInventor())
                            {
                                const uno::Reference<awt::XControlModel>& xControlModel =
                                                        pUnoCtrl->GetUnoControlModel();
                                OSL_ENSURE( xControlModel.is(), "UNO-Control without model" );
                                if( !xControlModel.is() )
                                    return;

                                uno::Reference< beans::XPropertySet > xPropSet( xControlModel, uno::UNO_QUERY );
                                uno::Reference< beans::XPropertySetInfo > xInfo = xPropSet->getPropertySetInfo();

                                OUString sPropTargetURL( u"TargetURL"_ustr );

                                // Is it possible to set a URL in the object?
                                if (xInfo->hasPropertyByName( sPropTargetURL ))
                                {

                                    OUString sPropButtonType( u"ButtonType"_ustr);
                                    OUString sPropTargetFrame( u"TargetFrame"_ustr );
                                    OUString sPropLabel( u"Label"_ustr );

                                    if ( xInfo->hasPropertyByName( sPropLabel ) )
                                    {
                                        xPropSet->setPropertyValue( sPropLabel, uno::Any(rName) );
                                    }

                                    OUString aTmp = INetURLObject::GetAbsURL( rViewData.GetDocShell().GetMedium()->GetBaseURL(), rURL );
                                    xPropSet->setPropertyValue( sPropTargetURL, uno::Any(aTmp) );

                                    if( !rTarget.isEmpty() && xInfo->hasPropertyByName( sPropTargetFrame ) )
                                    {
                                        xPropSet->setPropertyValue( sPropTargetFrame, uno::Any(rTarget) );
                                    }

                                    if ( xInfo->hasPropertyByName( sPropButtonType ) )
                                    {
                                        xPropSet->setPropertyValue( sPropButtonType, uno::Any(form::FormButtonType_URL) );
                                    }

                                    //! Undo ???
                                    rViewData.GetDocShell().SetDocumentModified();
                                    bDone = true;
                                }
                            }
                            else
                            {
                                pObj->setHyperlink(rURL);
                                setModified();
                                bDone = true;
                            }
                        }
                    }

                    if (!bDone)
                        rViewData.GetViewShell()->
                            InsertURL( rName, rURL, rTarget, static_cast<sal_uInt16>(eMode) );

                    //  If "text" is received by InsertURL of ViewShell, then the DrawShell is turned off !!!
                }
            }
            break;
        default:
            OSL_FAIL("wrong slot");
    }
}

//          Functions on Drawing-Objects

void ScDrawShell::ExecDrawFunc( SfxRequest& rReq )
{
    SfxBindings& rBindings = rViewData.GetBindings();
    ScTabView*   pTabView  = rViewData.GetView();
    ScDrawView*  pView     = pTabView->GetScDrawView();
    sal_uInt16 nSlotId = rReq.GetSlot();

    switch (nSlotId)
    {
        case SID_OBJECT_HEAVEN:
            pView->SetMarkedToLayer( SC_LAYER_FRONT );
            rBindings.Invalidate(SID_OBJECT_HEAVEN);
            rBindings.Invalidate(SID_OBJECT_HELL);
            break;
        case SID_OBJECT_HELL:
            pView->SetMarkedToLayer( SC_LAYER_BACK );
            rBindings.Invalidate(SID_OBJECT_HEAVEN);
            rBindings.Invalidate(SID_OBJECT_HELL);
            //  leave draw shell if nothing selected (layer may be locked)
            rViewData.GetViewShell()->UpdateDrawShell();
            break;

        case SID_FRAME_TO_TOP:
            pView->PutMarkedToTop();
            break;
        case SID_FRAME_TO_BOTTOM:
            pView->PutMarkedToBtm();
            break;
        case SID_FRAME_UP:
            pView->MovMarkedToTop();
            break;
        case SID_FRAME_DOWN:
            pView->MovMarkedToBtm();
            break;

        case SID_GROUP:
            pView->GroupMarked();
            break;
        case SID_UNGROUP:
            pView->UnGroupMarked();
            break;
        case SID_ENTER_GROUP:
            pView->EnterMarkedGroup();
            break;
        case SID_LEAVE_GROUP:
            pView->LeaveOneGroup();
            break;

        case SID_REGENERATE_DIAGRAM:
        case SID_EDIT_DIAGRAM:
            {
                const SdrMarkList& rMarkList = pView->GetMarkedObjectList();

                if (1 == rMarkList.GetMarkCount())
                {
                    SdrObject* pObj = rMarkList.GetMark(0)->GetMarkedSdrObj();

                    // Support advanced DiagramHelper
                    if(nullptr != pObj && pObj->isDiagram())
                    {
                        if(SID_REGENERATE_DIAGRAM == nSlotId)
                        {
                            pView->UnmarkAll();
                            pObj->getDiagramHelper()->reLayout(*static_cast<SdrObjGroup*>(pObj));
                            pView->MarkObj(pObj, pView->GetSdrPageView());
                        }
                        else // SID_EDIT_DIAGRAM
                        {
                            VclAbstractDialogFactory* pFact = VclAbstractDialogFactory::Create();
                            vcl::Window* pWin = rViewData.GetActiveWin();
                            VclPtr<VclAbstractDialog> pDlg = pFact->CreateDiagramDialog(
                                pWin ? pWin->GetFrameWeld() : nullptr,
                                *static_cast<SdrObjGroup*>(pObj));
                            pDlg->StartExecuteAsync(
                                [pDlg] (sal_Int32 /*nResult*/)->void
                                {
                                    pDlg->disposeOnce();
                                }
                            );
                        }
                    }
                }

                rReq.Done();
            }
            break;

        case SID_MIRROR_HORIZONTAL:
        case SID_FLIP_HORIZONTAL:
            pView->MirrorAllMarkedHorizontal();
            rBindings.Invalidate( SID_ATTR_TRANSFORM_ANGLE );
            break;
        case SID_MIRROR_VERTICAL:
        case SID_FLIP_VERTICAL:
            pView->MirrorAllMarkedVertical();
            rBindings.Invalidate( SID_ATTR_TRANSFORM_ANGLE );
            break;

        case SID_OBJECT_ALIGN_LEFT:
        case SID_ALIGN_ANY_LEFT:
            if (pView->IsAlignPossible())
                pView->AlignMarkedObjects(SdrHorAlign::Left, SdrVertAlign::NONE);
            break;
        case SID_OBJECT_ALIGN_CENTER:
        case SID_ALIGN_ANY_HCENTER:
            if (pView->IsAlignPossible())
                pView->AlignMarkedObjects(SdrHorAlign::Center, SdrVertAlign::NONE);
            break;
        case SID_OBJECT_ALIGN_RIGHT:
        case SID_ALIGN_ANY_RIGHT:
            if (pView->IsAlignPossible())
                pView->AlignMarkedObjects(SdrHorAlign::Right, SdrVertAlign::NONE);
            break;
        case SID_OBJECT_ALIGN_UP:
        case SID_ALIGN_ANY_TOP:
            if (pView->IsAlignPossible())
                pView->AlignMarkedObjects(SdrHorAlign::NONE, SdrVertAlign::Top);
            break;
        case SID_OBJECT_ALIGN_MIDDLE:
        case SID_ALIGN_ANY_VCENTER:
            if (pView->IsAlignPossible())
                pView->AlignMarkedObjects(SdrHorAlign::NONE, SdrVertAlign::Center);
            break;
        case SID_OBJECT_ALIGN_DOWN:
        case SID_ALIGN_ANY_BOTTOM:
            if (pView->IsAlignPossible())
                pView->AlignMarkedObjects(SdrHorAlign::NONE, SdrVertAlign::Bottom);
            break;

        case SID_DELETE:
        case SID_DELETE_CONTENTS:
            pView->DeleteMarked();
            rViewData.GetViewShell()->UpdateDrawShell();
        break;

        case SID_CUT:
            pView->DoCut();
            rViewData.GetViewShell()->UpdateDrawShell();
            break;

        case SID_COPY:
            pView->DoCopy();
            break;

        case SID_PASTE:
            ScClipUtil::PasteFromClipboard(GetViewData(), GetViewData().GetViewShell(), true);
            break;

        case SID_SELECTALL:
            pView->MarkAll();
            break;

        case SID_ANCHOR_PAGE:
            pView->SetPageAnchored();
            rBindings.Invalidate( SID_ANCHOR_PAGE );
            rBindings.Invalidate( SID_ANCHOR_CELL );
            rBindings.Invalidate( SID_ANCHOR_CELL_RESIZE );
            break;

        case SID_ANCHOR_CELL:
            pView->SetCellAnchored(false);
            rBindings.Invalidate( SID_ANCHOR_PAGE );
            rBindings.Invalidate( SID_ANCHOR_CELL );
            rBindings.Invalidate( SID_ANCHOR_CELL_RESIZE );
            break;

        case SID_ANCHOR_CELL_RESIZE:
            pView->SetCellAnchored(true);
            rBindings.Invalidate( SID_ANCHOR_PAGE );
            rBindings.Invalidate( SID_ANCHOR_CELL );
            rBindings.Invalidate( SID_ANCHOR_CELL_RESIZE );
            break;

        case SID_ANCHOR_TOGGLE:
            {
                switch( pView->GetAnchorType() )
                {
                    case SCA_CELL:
                    case SCA_CELL_RESIZE:
                    pView->SetPageAnchored();
                    break;
                    default:
                    pView->SetCellAnchored(false);
                    break;
                }
            }
            rBindings.Invalidate( SID_ANCHOR_PAGE );
            rBindings.Invalidate( SID_ANCHOR_CELL );
            rBindings.Invalidate( SID_ANCHOR_CELL_RESIZE );
            break;

        case SID_OBJECT_ROTATE:
            rViewData.GetViewShell()->SwitchRotateMode();
            break;
        case SID_OBJECT_MIRROR:
            {
                SdrDragMode eMode;
                if (pView->GetDragMode() == SdrDragMode::Mirror)
                    eMode = SdrDragMode::Move;
                else
                    eMode = SdrDragMode::Mirror;
                pView->SetDragMode( eMode );
                rBindings.Invalidate( SID_OBJECT_ROTATE );
                rBindings.Invalidate( SID_OBJECT_MIRROR );
                if (eMode == SdrDragMode::Mirror && !pView->IsFrameDragSingles())
                {
                    pView->SetFrameDragSingles();
                    rBindings.Invalidate( SID_BEZIER_EDIT );
                }
            }
            break;
        case SID_BEZIER_EDIT:
            {
                bool bOld = pView->IsFrameDragSingles();
                pView->SetFrameDragSingles( !bOld );
                rBindings.Invalidate( SID_BEZIER_EDIT );
                if (bOld && pView->GetDragMode() != SdrDragMode::Move)
                {
                    pView->SetDragMode( SdrDragMode::Move );
                    rBindings.Invalidate( SID_OBJECT_ROTATE );
                    rBindings.Invalidate( SID_OBJECT_MIRROR );
                }
            }
            break;

        case SID_FONTWORK:
        {
            sal_uInt16 nId = ScGetFontWorkId();
            SfxViewFrame& rViewFrm = rViewData.GetViewShell()->GetViewFrame();

            if ( rReq.GetArgs() )
                rViewFrm.SetChildWindow( nId,
                                           static_cast<const SfxBoolItem&>(
                                            (rReq.GetArgs()->Get(SID_FONTWORK))).
                                                GetValue() );
            else
                rViewFrm.ToggleChildWindow( nId );

            rBindings.Invalidate( SID_FONTWORK );
            rReq.Done();
        }
        break;

        case SID_ORIGINALSIZE:
            pView->SetMarkedOriginalSize();
            break;

        case SID_FITCELLSIZE:
            pView->FitToCellSize();
            break;

        case SID_ENABLE_HYPHENATION:
            {
                const SfxBoolItem* pItem = rReq.GetArg<SfxBoolItem>(SID_ENABLE_HYPHENATION);
                if( pItem )
                {
                    SfxItemSetFixed<EE_PARA_HYPHENATE, EE_PARA_HYPHENATE> aSet( GetPool() );
                    bool bValue = pItem->GetValue();
                    aSet.Put( SfxBoolItem( EE_PARA_HYPHENATE, bValue ) );
                    pView->SetAttributes( aSet );
                }
                rReq.Done();
            }
            break;

        case SID_RENAME_OBJECT:
            {
                const SdrMarkList& rMarkList = pView->GetMarkedObjectList();
                if(1 == rMarkList.GetMarkCount())
                {
                    // #i68101#
                    SdrObject* pSelected = rMarkList.GetMark(0)->GetMarkedSdrObj();
                    assert(pSelected && "ScDrawShell::ExecDrawFunc: nMarkCount, but no object (!)");

                    if(SC_LAYER_INTERN != pSelected->GetLayer())
                    {
                        OUString aOldName = pSelected->GetName();

                        SvxAbstractDialogFactory* pFact = SvxAbstractDialogFactory::Create();
                        vcl::Window* pWin = rViewData.GetActiveWin();
                        VclPtr<AbstractSvxObjectNameDialog> pDlg(pFact->CreateSvxObjectNameDialog(pWin ? pWin->GetFrameWeld() : nullptr, aOldName));

                        pDlg->SetCheckNameHdl(LINK(this, ScDrawShell, NameObjectHdl));

                        pDlg->StartExecuteAsync(
                            [this, pDlg, pSelected] (sal_Int32 nResult)->void
                            {
                                if (nResult == RET_OK)
                                {
                                    ScDocShell& rDocSh = rViewData.GetDocShell();
                                    OUString aNewName = pDlg->GetName();

                                    if (aNewName != pSelected->GetName())
                                    {
                                        // handle name change
                                        const SdrObjKind nObjType(pSelected->GetObjIdentifier());

                                        if (SdrObjKind::Graphic == nObjType && aNewName.isEmpty())
                                        {
                                            //  graphics objects must have names
                                            //  (all graphics are supposed to be in the navigator)
                                            ScDrawLayer* pModel = rViewData.GetDocument().GetDrawLayer();

                                            if(pModel)
                                            {
                                                aNewName = pModel->GetNewGraphicName();
                                            }
                                        }

                                        //  An undo action for renaming is missing in svdraw (99363).
                                        //  For OLE objects (which can be identified using the persist name),
                                        //  ScUndoRenameObject can be used until there is a common action for all objects.
                                        if(SdrObjKind::OLE2 == nObjType)
                                        {
                                            const OUString aPersistName = static_cast<SdrOle2Obj*>(pSelected)->GetPersistName();

                                            if(!aPersistName.isEmpty())
                                            {
                                                rDocSh.GetUndoManager()->AddUndoAction(
                                                    std::make_unique<ScUndoRenameObject>(rDocSh, aPersistName, pSelected->GetName(), aNewName));
                                            }
                                        }

                                        // set new name
                                        pSelected->SetName(aNewName);
                                    }

                                    // ChartListenerCollectionNeedsUpdate is needed for Navigator update
                                    rDocSh.GetDocument().SetChartListenerCollectionNeedsUpdate( true );
                                    rDocSh.SetDrawModified();
                                }
                                pDlg->disposeOnce();
                            }
                        );
                    }
                }
                break;
            }

        // #i68101#
        case SID_TITLE_DESCRIPTION_OBJECT:
            {
                const SdrMarkList& rMarkList = pView->GetMarkedObjectList();
                if(1 == rMarkList.GetMarkCount())
                {
                    SdrObject* pSelected = rMarkList.GetMark(0)->GetMarkedSdrObj();
                    assert(pSelected && "ScDrawShell::ExecDrawFunc: nMarkCount, but no object (!)");

                    if(SC_LAYER_INTERN != pSelected->GetLayer())
                    {
                        OUString aTitle(pSelected->GetTitle());
                        OUString aDescription(pSelected->GetDescription());
                        bool isDecorative(pSelected->IsDecorative());

                        SvxAbstractDialogFactory* pFact = SvxAbstractDialogFactory::Create();
                        vcl::Window* pWin = rViewData.GetActiveWin();
                        VclPtr<AbstractSvxObjectTitleDescDialog> pDlg(pFact->CreateSvxObjectTitleDescDialog(
                                    pWin ? pWin->GetFrameWeld() : nullptr, aTitle, aDescription, isDecorative));

                        pDlg->StartExecuteAsync(
                            [this, pDlg, pSelected] (sal_Int32 nResult)->void
                            {
                                if (nResult == RET_OK)
                                {
                                    ScDocShell& rDocSh = rViewData.GetDocShell();

                                    // handle Title and Description
                                    pSelected->SetTitle(pDlg->GetTitle());
                                    pSelected->SetDescription(pDlg->GetDescription());
                                    pSelected->SetDecorative(pDlg->IsDecorative());

                                    // ChartListenerCollectionNeedsUpdate is needed for Navigator update
                                    rDocSh.GetDocument().SetChartListenerCollectionNeedsUpdate( true );
                                    rDocSh.SetDrawModified();
                                }
                                pDlg->disposeOnce();
                            }
                        );
                    }
                }
                break;
            }

        case SID_EXTRUSION_TOGGLE:
        case SID_EXTRUSION_TILT_DOWN:
        case SID_EXTRUSION_TILT_UP:
        case SID_EXTRUSION_TILT_LEFT:
        case SID_EXTRUSION_TILT_RIGHT:
        case SID_EXTRUSION_3D_COLOR:
        case SID_EXTRUSION_DEPTH:
        case SID_EXTRUSION_DIRECTION:
        case SID_EXTRUSION_PROJECTION:
        case SID_EXTRUSION_LIGHTING_DIRECTION:
        case SID_EXTRUSION_LIGHTING_INTENSITY:
        case SID_EXTRUSION_SURFACE:
        case SID_EXTRUSION_DEPTH_FLOATER:
        case SID_EXTRUSION_DIRECTION_FLOATER:
        case SID_EXTRUSION_LIGHTING_FLOATER:
        case SID_EXTRUSION_SURFACE_FLOATER:
        case SID_EXTRUSION_DEPTH_DIALOG:
            svx::ExtrusionBar::execute( pView, rReq, rBindings );
            rReq.Ignore ();
            break;

        case SID_FONTWORK_SHAPE:
        case SID_FONTWORK_SHAPE_TYPE:
        case SID_FONTWORK_ALIGNMENT:
        case SID_FONTWORK_SAME_LETTER_HEIGHTS:
        case SID_FONTWORK_CHARACTER_SPACING:
        case SID_FONTWORK_KERN_CHARACTER_PAIRS:
        case SID_FONTWORK_CHARACTER_SPACING_FLOATER:
        case SID_FONTWORK_ALIGNMENT_FLOATER:
        case SID_FONTWORK_CHARACTER_SPACING_DIALOG:
            svx::FontworkBar::execute( *pView, rReq, rBindings );
            rReq.Ignore ();
            break;

        default:
            break;
    }
}

IMPL_LINK( ScDrawShell, NameObjectHdl, AbstractSvxObjectNameDialog&, rDialog, bool )
{
    OUString aName = rDialog.GetName();

    ScDrawLayer* pModel = rViewData.GetDocument().GetDrawLayer();
    if ( !aName.isEmpty() && pModel )
    {
        SCTAB nDummyTab;
        if ( pModel->GetNamedObject( aName, SdrObjKind::NONE, nDummyTab ) )
        {
            // existing object found -> name invalid
            return false;
        }
    }

    return true;   // name is valid
}

void ScDrawShell::ExecFormText(const SfxRequest& rReq)
{
    ScDrawView*         pDrView     = rViewData.GetScDrawView();
    const SdrMarkList&  rMarkList   = pDrView->GetMarkedObjectList();

    if ( rMarkList.GetMarkCount() == 1 && rReq.GetArgs() )
    {
        const SfxItemSet& rSet = *rReq.GetArgs();

        if ( pDrView->IsTextEdit() )
            pDrView->ScEndTextEdit();

        pDrView->SetAttributes(rSet);
    }
}

void ScDrawShell::ExecFormatPaintbrush( const SfxRequest& rReq )
{
    ScViewFunc* pView = rViewData.GetView();
    if ( pView->HasPaintBrush() )
    {
        // cancel paintbrush mode
        pView->ResetBrushDocument();
    }
    else
    {
        bool bLock = false;
        const SfxItemSet *pArgs = rReq.GetArgs();
        if( pArgs && pArgs->Count() >= 1 )
            bLock = pArgs->Get(SID_FORMATPAINTBRUSH).GetValue();

        ScDrawView* pDrawView = rViewData.GetScDrawView();
        if ( pDrawView && pDrawView->GetMarkedObjectList().GetMarkCount() != 0 )
        {
            std::unique_ptr<SfxItemSet> pItemSet(new SfxItemSet( pDrawView->GetAttrFromMarked(true/*bOnlyHardAttr*/) ));
            pView->SetDrawBrushSet( std::move(pItemSet), bLock );
        }
    }
}

void ScDrawShell::StateFormatPaintbrush( SfxItemSet& rSet )
{
    ScDrawView* pDrawView = rViewData.GetScDrawView();
    bool bSelection = pDrawView && pDrawView->GetMarkedObjectList().GetMarkCount() != 0;
    bool bHasPaintBrush = rViewData.GetView()->HasPaintBrush();

    if ( !bHasPaintBrush && !bSelection )
        rSet.DisableItem( SID_FORMATPAINTBRUSH );
    else
        rSet.Put( SfxBoolItem( SID_FORMATPAINTBRUSH, bHasPaintBrush ) );
}

ScDrawView* ScDrawShell::GetDrawView()
{
    return rViewData.GetView()->GetScDrawView();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
