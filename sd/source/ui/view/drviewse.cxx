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

#include <config_features.h>

#include <com/sun/star/presentation/XPresentation2.hpp>
#include <com/sun/star/form/FormButtonType.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <i18nutil/unicode.hxx>
#include <i18nutil/transliteration.hxx>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/uno/Any.hxx>

#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <comphelper/lok.hxx>
#include <comphelper/propertyvalue.hxx>
#include <editeng/editstat.hxx>
#include <editeng/outlobj.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld.hxx>
#include <svl/urlbmk.hxx>
#include <svx/clipfmtitem.hxx>
#include <svx/svdpagv.hxx>
#include <svx/svdopath.hxx>
#include <svx/svdundo.hxx>
#include <svx/svdorect.hxx>
#include <svl/eitem.hxx>
#include <svl/intitem.hxx>
#include <svl/poolitem.hxx>
#include <svl/stritem.hxx>
#include <editeng/eeitem.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/viewfrm.hxx>
#include <sfx2/request.hxx>
#include <svx/svxids.hrc>
#include <editeng/flditem.hxx>
#include <svx/obj3d.hxx>
#include <svx/svdobjkind.hxx>
#include <svx/svdouno.hxx>
#include <svx/dataaccessdescriptor.hxx>
#include <tools/urlobj.hxx>
#include <sfx2/ipclient.hxx>
#include <avmedia/mediawindow.hxx>
#include <svl/urihelper.hxx>
#include <sfx2/docfile.hxx>
#include <sfx2/notebookbar/SfxNotebookBar.hxx>
#include <osl/diagnose.h>
#include <svl/cryptosign.hxx>

#include <DrawViewShell.hxx>
#include <slideshow.hxx>
#include <ViewShellHint.hxx>
#include <framework/FrameworkHelper.hxx>
#include <app.hrc>
#include <strings.hrc>

#include <drawdoc.hxx>
#include <fusel.hxx>
#include <futext.hxx>
#include <fuconrec.hxx>
#include <fuconcs.hxx>
#include <fuconuno.hxx>
#include <fuconbez.hxx>
#include <fuediglu.hxx>
#include <fuconarc.hxx>
#include <fucon3d.hxx>
#include <sdresid.hxx>
#include <unokywds.hxx>
#include <Outliner.hxx>
#include <sdpage.hxx>
#include <FrameView.hxx>
#include <zoomlist.hxx>
#include <drawview.hxx>
#include <DrawDocShell.hxx>
#include <ViewShellBase.hxx>
#include <ToolBarManager.hxx>
#include <anminfo.hxx>
#include <optsitem.hxx>
#include <Window.hxx>
#include <fuformatpaintbrush.hxx>
#include <fuzoom.hxx>
#include <sdmod.hxx>
#include <basegfx/utils/zoomtools.hxx>
#include <officecfg/Office/Draw.hxx>
#include <officecfg/Office/Impress.hxx>
#include <sfx2/lokhelper.hxx>
#include <SlideSorter.hxx>
#include <SlideSorterViewShell.hxx>
#include <controller/SlideSorterController.hxx>
#include <controller/SlsClipboard.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::presentation;
using namespace ::com::sun::star::beans;

namespace sd {

// Permanent Functions

static void ImpAddPrintableCharactersToTextEdit(SfxRequest const & rReq, ::sd::View* pView)
{
    // evtl. feed characters to activated textedit
    const SfxItemSet* pSet = rReq.GetArgs();

    if(!pSet)
        return;

    OUString aInputString;

    if(SfxItemState::SET == pSet->GetItemState(SID_ATTR_CHAR))
        aInputString = pSet->Get(SID_ATTR_CHAR).GetValue();

    if(aInputString.isEmpty())
        return;

    OutlinerView* pOLV = pView->GetTextEditOutlinerView();

    if(pOLV)
    {
        for(sal_Int32 a(0); a < aInputString.getLength(); a++)
        {
            vcl::KeyCode aKeyCode;
            // tdf#38669 - create the key event using a Unicode character
            KeyEvent aKeyEvent(aInputString[a], aKeyCode);

            // add actual character
            pOLV->PostKeyEvent(aKeyEvent);
        }
    }
}

void DrawViewShell::FuPermanent(SfxRequest& rReq)
{
    // We do not execute a thing during a native slide show

    if (SlideShow::IsRunning(GetViewShellBase()) && !SlideShow::IsInteractiveSlideshow(&GetViewShellBase())) // IASS
        return;

    sal_uInt16 nSId = rReq.GetSlot();

    if( HasCurrentFunction() &&
        ( nSId == SID_TEXTEDIT || nSId == SID_ATTR_CHAR || nSId == SID_TEXT_FITTOSIZE ||
          nSId == SID_ATTR_CHAR_VERTICAL || nSId == SID_TEXT_FITTOSIZE_VERTICAL ) )
    {
        rtl::Reference<FuPoor> xFunc( GetCurrentFunction() );

        FuText* pFuText = dynamic_cast< FuText* >( xFunc.get() );

        if( pFuText )
        {
            pFuText->SetPermanent(true);
            xFunc->ReceiveRequest( rReq );

            Invalidate();

            // evtl. feed characters to activated textedit
            if(SID_ATTR_CHAR == nSId && GetView() && GetView()->IsTextEdit())
                ImpAddPrintableCharactersToTextEdit(rReq, GetView());

            rReq.Done();
            return;
        }
    }

    CheckLineTo (rReq);
    sal_uInt16 nOldSId = 0;
    bool bPermanent = false;

    if( !mpDrawView )
        return;

    if(HasCurrentFunction())
    {
        if( (nSId == SID_FORMATPAINTBRUSH) && (GetCurrentFunction()->GetSlotID() == SID_TEXTEDIT) )
        {
            // save text edit mode for format paintbrush!
            SetOldFunction( GetCurrentFunction() );
        }
        else
        {
            if(GetOldFunction() == GetCurrentFunction())
            {
                SetOldFunction(nullptr);
            }
        }

        if ( nSId != SID_TEXTEDIT && nSId != SID_ATTR_CHAR && nSId != SID_TEXT_FITTOSIZE &&
             nSId != SID_ATTR_CHAR_VERTICAL && nSId != SID_TEXT_FITTOSIZE_VERTICAL &&
             nSId != SID_FORMATPAINTBRUSH &&
             mpDrawView->IsTextEdit() )
        {
            mpDrawView->SdrEndTextEdit();
        }

        if( HasCurrentFunction() )
        {
            nOldSId = GetCurrentFunction()->GetSlotID();

            if (nOldSId == nSId ||
                ((nOldSId == SID_TEXTEDIT || nOldSId == SID_ATTR_CHAR || nOldSId == SID_TEXT_FITTOSIZE ||
                nOldSId == SID_ATTR_CHAR_VERTICAL || nOldSId == SID_TEXT_FITTOSIZE_VERTICAL) &&
                (nSId == SID_TEXTEDIT || nSId == SID_ATTR_CHAR || nSId == SID_TEXT_FITTOSIZE ||
                nSId == SID_ATTR_CHAR_VERTICAL || nSId == SID_TEXT_FITTOSIZE_VERTICAL )))
            {
                bPermanent = true;
            }

            GetCurrentFunction()->Deactivate();
        }

        SetCurrentFunction(nullptr);

        SfxBindings& rBind = GetViewFrame()->GetBindings();
        rBind.Invalidate(nOldSId);
        rBind.Update(nOldSId);
    }

    // for LibreOfficeKit - choosing a shape should construct it directly
    bool bCreateDirectly = false;
    bool bRectangle = false;

    switch ( nSId )
    {
        case SID_TEXTEDIT:  // BASIC ???
        case SID_ATTR_CHAR:
        case SID_ATTR_CHAR_VERTICAL:
        case SID_TEXT_FITTOSIZE:
        case SID_TEXT_FITTOSIZE_VERTICAL:
        {
            SetCurrentFunction( FuText::Create(*this, GetActiveWindow(), mpDrawView.get(), *GetDoc(), rReq) );
            GetCurrentFunction()->DoExecute(rReq);

            SfxBindings& rBindings = GetViewFrame()->GetBindings();
            rBindings.Invalidate( SID_ATTR_CHAR );
            rBindings.Invalidate( SID_ATTR_CHAR_VERTICAL );
            rBindings.Invalidate( SID_TEXT_FITTOSIZE );
            rBindings.Invalidate( SID_TEXT_FITTOSIZE_VERTICAL );

            // evtl. feed characters to activated textedit
            if(SID_ATTR_CHAR == nSId && GetView() && GetView()->IsTextEdit())
                ImpAddPrintableCharactersToTextEdit(rReq, GetView());

            rReq.Done();

            const SfxItemSet* pArgs = rReq.GetArgs();
            if (pArgs && pArgs->HasItem(FN_PARAM_1))
                bCreateDirectly = static_cast<const SfxBoolItem&>(pArgs->Get(FN_PARAM_1)).GetValue();
        }
        break;

        case SID_FM_CREATE_CONTROL:
        {
            SetCurrentFunction( FuConstructUnoControl::Create( *this, GetActiveWindow(), mpDrawView.get(), *GetDoc(), rReq, bPermanent ) );
            rReq.Done();
        }
        break;

        case SID_FM_CREATE_FIELDCONTROL:
        {
            const SfxUnoAnyItem* pDescriptorItem = rReq.GetArg<SfxUnoAnyItem>(SID_FM_DATACCESS_DESCRIPTOR);
            DBG_ASSERT( pDescriptorItem, "DrawViewShell::FuPermanent(SID_FM_CREATE_FIELDCONTROL): invalid request args!" );

            if(pDescriptorItem)
            {
                // get the form view
                FmFormView* pFormView = mpDrawView.get();
                SdrPageView* pPageView = pFormView ? pFormView->GetSdrPageView() : nullptr;

                if(pPageView)
                {
                    svx::ODataAccessDescriptor aDescriptor(pDescriptorItem->GetValue());
                    rtl::Reference<SdrObject> pNewDBField = pFormView->CreateFieldControl(aDescriptor);

                    if(pNewDBField)
                    {
                        ::tools::Rectangle aVisArea = GetActiveWindow()->PixelToLogic(::tools::Rectangle(Point(0,0), GetActiveWindow()->GetOutputSizePixel()));
                        Point aObjPos(aVisArea.Center());
                        Size aObjSize(pNewDBField->GetLogicRect().GetSize());
                        aObjPos.AdjustX( -(aObjSize.Width() / 2) );
                        aObjPos.AdjustY( -(aObjSize.Height() / 2) );
                        ::tools::Rectangle aNewObjectRectangle(aObjPos, aObjSize);

                        pNewDBField->SetLogicRect(aNewObjectRectangle);

                        GetView()->InsertObjectAtView(pNewDBField.get(), *pPageView);
                    }
                }
            }
            rReq.Done();
        }
        break;

        case SID_OBJECT_SELECT:
        case SID_OBJECT_ROTATE:
        case SID_OBJECT_MIRROR:
        case SID_OBJECT_CROP:
        case SID_OBJECT_TRANSPARENCE:
        case SID_OBJECT_GRADIENT:
        case SID_OBJECT_SHEAR:
        case SID_OBJECT_CROOK_ROTATE:
        case SID_OBJECT_CROOK_SLANT:
        case SID_OBJECT_CROOK_STRETCH:
        case SID_CONVERT_TO_3D_LATHE:
        {
            sal_uInt16 nSlotId = rReq.GetSlot();

            // toggle function
            if( nOldSId == nSlotId )
            {
                nSlotId = SID_OBJECT_SELECT;
                rReq.SetSlot( nSlotId );
            }

            const SdrMarkList&  rMarkList = mpDrawView->GetMarkedObjectList();
            if (nSlotId == SID_OBJECT_CROOK_ROTATE ||
                nSlotId == SID_OBJECT_CROOK_SLANT ||
                nSlotId == SID_OBJECT_CROOK_STRETCH)
            {
                if ( rMarkList.GetMarkCount() > 0 &&
                    !mpDrawView->IsCrookAllowed( mpDrawView->IsCrookNoContortion() ) )
                {
                    if ( mpDrawView->IsPresObjSelected() )
                    {
                        std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(GetFrameWeld(),
                                                                      VclMessageType::Info, VclButtonsType::Ok,
                                                                      SdResId(STR_ACTION_NOTPOSSIBLE)));
                        xInfoBox->run();
                    }
                    else
                    {
                        std::unique_ptr<weld::MessageDialog> xQueryBox(Application::CreateMessageDialog(GetFrameWeld(),
                                                                       VclMessageType::Question, VclButtonsType::YesNo,
                                                                       SdResId(STR_ASK_FOR_CONVERT_TO_BEZIER)));
                        if (xQueryBox->run() == RET_YES )
                        {
                            // implicit transformation into bezier
                            weld::WaitObject aWait(GetFrameWeld());
                            mpDrawView->ConvertMarkedToPathObj(false);
                        }
                    }
                }
            }
            else if (nSlotId == SID_OBJECT_SHEAR)
            {
                size_t i = 0;
                const size_t nMarkCnt = rMarkList.GetMarkCount();
                bool b3DObjMarked = false;

                while (i < nMarkCnt && !b3DObjMarked)
                {
                    if (DynCastE3dObject( rMarkList.GetMark(i)->GetMarkedSdrObj() ))
                    {
                        b3DObjMarked = true;
                    }
                    else
                    {
                        i++;
                    }
                }

                if ( nMarkCnt > 0 && !b3DObjMarked &&
                     (!mpDrawView->IsShearAllowed() || !mpDrawView->IsDistortAllowed()) )
                {
                    if ( mpDrawView->IsPresObjSelected() )
                    {
                        std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(GetFrameWeld(),
                                                                      VclMessageType::Info, VclButtonsType::Ok,
                                                                      SdResId(STR_ACTION_NOTPOSSIBLE)));
                        xInfoBox->run();
                    }
                    else
                    {
                        std::unique_ptr<weld::MessageDialog> xQueryBox(Application::CreateMessageDialog(GetFrameWeld(),
                                                                       VclMessageType::Question, VclButtonsType::YesNo,
                                                                       SdResId(STR_ASK_FOR_CONVERT_TO_BEZIER)));
                        if (xQueryBox->run() == RET_YES)
                        {
                            // implicit transformation into bezier
                            weld::WaitObject aWait(GetFrameWeld());
                            mpDrawView->ConvertMarkedToPathObj(false);
                        }
                    }
                }
            }

            SetCurrentFunction( FuSelection::Create(*this, GetActiveWindow(), mpDrawView.get(), *GetDoc(), rReq) );
            rReq.Done();
            Invalidate( SID_OBJECT_SELECT );
        }
        break;

        case SID_DRAW_LINE:
        case SID_DRAW_XLINE:
        case SID_DRAW_MEASURELINE:
        case SID_LINE_ARROW_START:
        case SID_LINE_ARROW_END:
        case SID_LINE_ARROWS:
        case SID_LINE_ARROW_CIRCLE:
        case SID_LINE_CIRCLE_ARROW:
        case SID_LINE_ARROW_SQUARE:
        case SID_LINE_SQUARE_ARROW:

        case SID_DRAW_RECT:
        case SID_DRAW_RECT_NOFILL:
        case SID_DRAW_RECT_ROUND:
        case SID_DRAW_RECT_ROUND_NOFILL:
        case SID_DRAW_SQUARE:
        case SID_DRAW_SQUARE_NOFILL:
        case SID_DRAW_SQUARE_ROUND:
        case SID_DRAW_SQUARE_ROUND_NOFILL:
        case SID_DRAW_ELLIPSE:
        case SID_DRAW_ELLIPSE_NOFILL:
        case SID_DRAW_CIRCLE:
        case SID_DRAW_CIRCLE_NOFILL:
        case SID_DRAW_CAPTION:
        case SID_DRAW_CAPTION_VERTICAL:
        case SID_TOOL_CONNECTOR:
        case SID_CONNECTOR_ARROW_START:
        case SID_CONNECTOR_ARROW_END:
        case SID_CONNECTOR_ARROWS:
        case SID_CONNECTOR_CIRCLE_START:
        case SID_CONNECTOR_CIRCLE_END:
        case SID_CONNECTOR_CIRCLES:
        case SID_CONNECTOR_LINE:
        case SID_CONNECTOR_LINE_ARROW_START:
        case SID_CONNECTOR_LINE_ARROW_END:
        case SID_CONNECTOR_LINE_ARROWS:
        case SID_CONNECTOR_LINE_CIRCLE_START:
        case SID_CONNECTOR_LINE_CIRCLE_END:
        case SID_CONNECTOR_LINE_CIRCLES:
        case SID_CONNECTOR_CURVE:
        case SID_CONNECTOR_CURVE_ARROW_START:
        case SID_CONNECTOR_CURVE_ARROW_END:
        case SID_CONNECTOR_CURVE_ARROWS:
        case SID_CONNECTOR_CURVE_CIRCLE_START:
        case SID_CONNECTOR_CURVE_CIRCLE_END:
        case SID_CONNECTOR_CURVE_CIRCLES:
        case SID_CONNECTOR_LINES:
        case SID_CONNECTOR_LINES_ARROW_START:
        case SID_CONNECTOR_LINES_ARROW_END:
        case SID_CONNECTOR_LINES_ARROWS:
        case SID_CONNECTOR_LINES_CIRCLE_START:
        case SID_CONNECTOR_LINES_CIRCLE_END:
        case SID_CONNECTOR_LINES_CIRCLES:
        case SID_INSERT_SIGNATURELINE:
        {
            if (nSId == SID_INSERT_SIGNATURELINE)
            {
                // See if a signing cert is passed as a parameter: if so, parse that.
                std::string aSignatureCert;
                std::string aSignatureKey;
                const SfxStringItem* pSignatureCert = rReq.GetArg<SfxStringItem>(FN_PARAM_1);
                if (pSignatureCert)
                {
                    aSignatureCert = pSignatureCert->GetValue().toUtf8();
                }
                const SfxStringItem* pSignatureKey = rReq.GetArg<SfxStringItem>(FN_PARAM_2);
                if (pSignatureKey)
                {
                    aSignatureKey = pSignatureKey->GetValue().toUtf8();
                }
                bool bExternal = false;
                const SfxBoolItem* pExternal = rReq.GetArg<SfxBoolItem>(FN_PARAM_3);
                if (pExternal)
                {
                    bExternal = pExternal->GetValue();
                }

                SfxViewFrame* pFrame = GetFrame();
                SfxViewShell* pViewShell = pFrame ? pFrame->GetViewShell() : nullptr;
                if (pViewShell)
                {
                    svl::crypto::CertificateOrName aCertificateOrName;
                    if (!aSignatureCert.empty() && !aSignatureKey.empty())
                    {
                        aCertificateOrName.m_xCertificate = SfxLokHelper::getSigningCertificate(aSignatureCert, aSignatureKey);
                    }
                    else if (bExternal)
                    {
                        aCertificateOrName.m_aName = mpDrawView->GetAuthor();
                    }
                    // Always set the signing certificate, to clear data from a previous dispatch.
                    pViewShell->SetSigningCertificate(aCertificateOrName);
                }
            }

            bCreateDirectly = comphelper::LibreOfficeKit::isActive();
            bRectangle = true;
            SetCurrentFunction( FuConstructRectangle::Create( *this, GetActiveWindow(), mpDrawView.get(), *GetDoc(), rReq, bPermanent ) );
            rReq.Done();
        }
        break;
        case SID_DRAW_POLYGON:
        case SID_DRAW_POLYGON_NOFILL:
        case SID_DRAW_XPOLYGON:
        case SID_DRAW_XPOLYGON_NOFILL:
        case SID_DRAW_FREELINE:
        case SID_DRAW_FREELINE_NOFILL:
        case SID_DRAW_BEZIER_FILL:          // BASIC
        case SID_DRAW_BEZIER_NOFILL:        // BASIC
        {
            // Direct mode means no interactive drawing, just insert the shape with reasonable
            // defaults -- to be consistent with the line insert case above.
            bCreateDirectly = comphelper::LibreOfficeKit::isActive();

            SetCurrentFunction( FuConstructBezierPolygon::Create(*this, GetActiveWindow(), mpDrawView.get(), *GetDoc(), rReq, bPermanent) );
            rReq.Done();
        }
        break;

        case SID_GLUE_EDITMODE:
        {
            if (nOldSId != SID_GLUE_EDITMODE)
            {
                SetCurrentFunction( FuEditGluePoints::Create( *this, GetActiveWindow(), mpDrawView.get(), *GetDoc(), rReq, bPermanent ) );
            }
            else
            {
                GetViewFrame()->GetDispatcher()->Execute(SID_OBJECT_SELECT, SfxCallMode::ASYNCHRON);
            }

            rReq.Done();
        }
        break;

        case SID_DRAW_ARC:
        case SID_DRAW_CIRCLEARC:
        case SID_DRAW_PIE:
        case SID_DRAW_PIE_NOFILL:
        case SID_DRAW_CIRCLEPIE:
        case SID_DRAW_CIRCLEPIE_NOFILL:
        case SID_DRAW_ELLIPSECUT:
        case SID_DRAW_ELLIPSECUT_NOFILL:
        case SID_DRAW_CIRCLECUT:
        case SID_DRAW_CIRCLECUT_NOFILL:
        {
            SetCurrentFunction( FuConstructArc::Create( *this, GetActiveWindow(), mpDrawView.get(), *GetDoc(), rReq, bPermanent) );
            rReq.Done();
        }
        break;

        case SID_3D_CUBE:
        case SID_3D_SHELL:
        case SID_3D_SPHERE:
        case SID_3D_TORUS:
        case SID_3D_HALF_SPHERE:
        case SID_3D_CYLINDER:
        case SID_3D_CONE:
        case SID_3D_PYRAMID:
        {
            SetCurrentFunction( FuConstruct3dObject::Create(*this, GetActiveWindow(), mpDrawView.get(), *GetDoc(), rReq, bPermanent ) );
            rReq.Done();
        }
        break;

        case SID_DRAWTBX_CS_BASIC :
        case SID_DRAWTBX_CS_SYMBOL :
        case SID_DRAWTBX_CS_ARROW :
        case SID_DRAWTBX_CS_FLOWCHART :
        case SID_DRAWTBX_CS_CALLOUT :
        case SID_DRAWTBX_CS_STAR :
        case SID_DRAW_CS_ID :
        {
            SetCurrentFunction( FuConstructCustomShape::Create( *this, GetActiveWindow(), mpDrawView.get(), *GetDoc(), rReq, bPermanent ) );
            rReq.Done();

            bCreateDirectly = comphelper::LibreOfficeKit::isActive();
            const SfxItemSet* pArgs = rReq.GetArgs();
            if (pArgs && pArgs->HasItem(FN_PARAM_1))
            {
                bCreateDirectly = static_cast<const SfxBoolItem&>(pArgs->Get(FN_PARAM_1)).GetValue();
            }

            if ( nSId != SID_DRAW_CS_ID )
            {
                SfxBindings& rBind = GetViewFrame()->GetBindings();
                rBind.Invalidate( nSId );
                rBind.Update( nSId );
            }
        }
        break;

        case SID_FORMATPAINTBRUSH:
        {
            SetCurrentFunction( FuFormatPaintBrush::Create( *this, GetActiveWindow(), mpDrawView.get(), *GetDoc(), rReq ) );
            rReq.Done();
            SfxBindings& rBind = GetViewFrame()->GetBindings();
            rBind.Invalidate( nSId );
            rBind.Update( nSId );
            break;
        }

        case SID_ZOOM_MODE:
        case SID_ZOOM_PANNING:
        {
            if (nOldSId != nSId)
            {
                mbZoomOnPage = false;
                SetCurrentFunction( FuZoom::Create(*this, GetActiveWindow(), mpDrawView.get(), *GetDoc(), rReq ) );
            }
            else
            {
                GetViewFrame()->GetDispatcher()->Execute(SID_OBJECT_SELECT, SfxCallMode::ASYNCHRON);
            }
            rReq.Done();
        }
        break;

        default:
           break;
    }

    if(HasOldFunction())
    {
        sal_uInt16 nSlotId = GetOldFunction()->GetSlotID();

        GetOldFunction()->Deactivate();
        SetOldFunction(nullptr);

        SfxBindings& rBind = GetViewFrame()->GetBindings();
        rBind.Invalidate( nSlotId );
        rBind.Update( nSlotId );
    }

    if(HasCurrentFunction())
    {
        GetCurrentFunction()->Activate();
        SetOldFunction( GetCurrentFunction() );
    }

    // invalidate shell, is faster than every individually (says MI)
    // now explicit the last slot incl. Update()
    Invalidate();

    // CTRL-SID_OBJECT_SELECT -> select first draw object if none is selected yet
    if(SID_OBJECT_SELECT == nSId && HasCurrentFunction() && (rReq.GetModifier() & KEY_MOD1))
    {
        const SdrMarkList&  rMarkList = GetView()->GetMarkedObjectList();
        if(rMarkList.GetMarkCount() == 0)
        {
            // select first object
            GetView()->UnmarkAllObj();
            GetView()->MarkNextObj(true);

            // ...and make it visible
            if(rMarkList.GetMarkCount() != 0)
                GetView()->MakeVisible(GetView()->GetAllMarkedRect(), *GetActiveWindow());
        }
    }

    // with qualifier construct directly
    if(!(HasCurrentFunction() && ((rReq.GetModifier() & KEY_MOD1) || bCreateDirectly)))
        return;

    // get SdOptions
    SdOptions* pOptions = SdModule::get()->GetSdOptions(GetDoc()->GetDocumentType());
    sal_uInt32 nDefaultObjectSizeWidth(pOptions->GetDefaultObjectSizeWidth());
    sal_uInt32 nDefaultObjectSizeHeight(pOptions->GetDefaultObjectSizeHeight());
    if (nSId == SID_INSERT_SIGNATURELINE)
    {
        // Half of the default to better match the space available for signatures in many real-world
        // documents.
        nDefaultObjectSizeWidth *= 0.5;
        nDefaultObjectSizeHeight *= 0.5;
    }

    // calc position and size
    ::tools::Rectangle aVisArea = GetActiveWindow()->PixelToLogic(::tools::Rectangle(Point(0,0), GetActiveWindow()->GetOutputSizePixel()));
    if (comphelper::LibreOfficeKit::isActive())
    {
        // aVisArea is nonsensical in the LOK case, use the slide size
        aVisArea = ::tools::Rectangle(Point(), getCurrentPage()->GetSize());
    }

    Point aPagePos = aVisArea.Center();
    aPagePos.AdjustX( -sal_Int32(nDefaultObjectSizeWidth / 2) );
    aPagePos.AdjustY( -sal_Int32(nDefaultObjectSizeHeight / 2) );
    ::tools::Rectangle aNewObjectRectangle(aPagePos, Size(nDefaultObjectSizeWidth, nDefaultObjectSizeHeight));
    SdrPageView* pPageView = mpDrawView->GetSdrPageView();

    if(!pPageView)
        return;

    // create the default object
    rtl::Reference<SdrObject> pObj = GetCurrentFunction()->CreateDefaultObject(nSId, aNewObjectRectangle);

    if(!pObj)
        return;

    auto pObjTmp = pObj.get();
    // insert into page
    GetView()->InsertObjectAtView(pObj.get(), *pPageView);

    // Now that pFuActual has done what it was created for we
    // can switch on the edit mode for callout objects.
    switch (nSId)
    {
        case SID_DRAW_CAPTION:
        case SID_DRAW_CAPTION_VERTICAL:
        case SID_ATTR_CHAR:
        case SID_ATTR_CHAR_VERTICAL:
        case SID_TEXT_FITTOSIZE:
        case SID_TEXT_FITTOSIZE_VERTICAL:
        {
            // Make FuText the current function.
            SfxUInt16Item aItem (SID_TEXTEDIT, 1);
            GetViewFrame()->GetDispatcher()->
                ExecuteList(SID_TEXTEDIT, SfxCallMode::SYNCHRON |
                    SfxCallMode::RECORD, { &aItem });
            // Put text object into edit mode.
            GetView()->SdrBeginTextEdit(static_cast<SdrTextObj*>(pObjTmp), pPageView);
            break;
        }
    }

    if (bRectangle && !bPermanent)
    {
        GetViewFrame()->GetDispatcher()->Execute(SID_OBJECT_SELECT, SfxCallMode::ASYNCHRON);
    }
}

void DrawViewShell::FuDeleteSelectedObjects()
{
    if( !mpDrawView )
        return;

    bool bConsumed = false;

    //if any placeholders are selected
    if (mpDrawView->IsPresObjSelected(false))
    {
        //If there are placeholders in the list which can be toggled
        //off in edit->master->master elements then do that here,
        std::vector<SdrObject*> aPresMarksToRemove;
        const SdrMarkList& rMarkList = mpDrawView->GetMarkedObjectList();
        for (size_t i=0; i < rMarkList.GetMarkCount(); ++i)
        {
            SdrObject* pObj = rMarkList.GetMark(i)->GetMarkedSdrObj();
            SdPage* pPage = static_cast<SdPage*>(pObj->getSdrPageFromSdrObject());
            PresObjKind eKind = pPage->GetPresObjKind(pObj);
            if (eKind == PresObjKind::Footer || eKind == PresObjKind::Header ||
                eKind == PresObjKind::DateTime || eKind == PresObjKind::SlideNumber)
            {
                aPresMarksToRemove.push_back(pObj);
            }
        }

        for (SdrObject* pObj : aPresMarksToRemove)
        {
            //Unmark object
            mpDrawView->MarkObj(pObj, mpDrawView->GetSdrPageView(), true);
            SdPage* pPage = static_cast<SdPage*>(pObj->getSdrPageFromSdrObject());
            //remove placeholder from master page
            pPage->DestroyDefaultPresObj(pPage->GetPresObjKind(pObj));
        }

        bConsumed = true;
    }

    // placeholders which cannot be deleted selected
    if (mpDrawView->IsPresObjSelected(false, true, false, true))
    {
        std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(GetFrameWeld(),
                                                      VclMessageType::Info, VclButtonsType::Ok,
                                                      SdResId(STR_ACTION_NOTPOSSIBLE)));
        xInfoBox->run();
        bConsumed = true;
    }

    if (bConsumed)
        return;

    vcl::KeyCode aKCode(KEY_DELETE);
    KeyEvent aKEvt( 0, aKCode);

    bConsumed = mpDrawView->getSmartTags().KeyInput( aKEvt );

    if (!bConsumed && HasCurrentFunction())
        bConsumed = GetCurrentFunction()->KeyInput(aKEvt);

    if (!bConsumed)
        mpDrawView->DeleteMarked();
}

void DrawViewShell::FuSupport(SfxRequest& rReq)
{
    if( rReq.GetSlot() == SID_STYLE_FAMILY && rReq.GetArgs())
        GetDocSh()->SetStyleFamily(static_cast<SfxStyleFamily>(rReq.GetArgs()->Get( SID_STYLE_FAMILY ).GetValue()));

    // We do not execute a thing during a native slide show
    if((SlideShow::IsRunning(GetViewShellBase())
        && !SlideShow::IsInteractiveSlideshow(&GetViewShellBase())) // IASS
        && (rReq.GetSlot() != SID_PRESENTATION_END && rReq.GetSlot() != SID_SIZE_PAGE))
        return;

    CheckLineTo (rReq);

    if( !mpDrawView )
        return;

    sal_uInt16 nSId = rReq.GetSlot();

    switch ( nSId )
    {
        case SID_CLEAR_UNDO_STACK:
        {
            GetDocSh()->ClearUndoBuffer();
            rReq.Ignore ();
        }
        break;

        case SID_PRESENTATION:
        case SID_PRESENTATION_CURRENT_SLIDE:
        case SID_REHEARSE_TIMINGS:
        {
            slideshowhelp::ShowSlideShow(rReq, *GetDoc());
            rReq.Ignore ();
        }
        break;

        case SID_PRESENTATION_END:
        {
            StopSlideShow();

            rReq.Ignore ();
        }
        break;

        case SID_BEZIER_EDIT:
        {
            mpDrawView->SetFrameDragSingles(!mpDrawView->IsFrameDragSingles());

            /******************************************************************
            * turn ObjectBar on
            ******************************************************************/
            if( dynamic_cast< FuSelection* >( GetCurrentFunction().get() ) || dynamic_cast< FuConstructBezierPolygon* >( GetCurrentFunction().get() ) )
            {
                // Tell the tool bar manager about the context change.
                GetViewShellBase().GetToolBarManager()->SelectionHasChanged(*this,*mpDrawView);
            }

            Invalidate(SID_BEZIER_EDIT);
            rReq.Ignore();
        }
        break;

        case SID_OBJECT_CLOSE:
        {
            const SdrMarkList& rMarkList = mpDrawView->GetMarkedObjectList();
            if ( rMarkList.GetMark(0) && !mpDrawView->IsAction() )
            {
                SdrPathObj* pPathObj = static_cast<SdrPathObj*>( rMarkList.GetMark(0)->GetMarkedSdrObj());
                const bool bUndo = mpDrawView->IsUndoEnabled();
                if( bUndo )
                    mpDrawView->BegUndo(SdResId(STR_UNDO_BEZCLOSE));

                mpDrawView->UnmarkAllPoints();

                if( bUndo )
                    mpDrawView->AddUndo(std::make_unique<SdrUndoGeoObj>(*pPathObj));

                pPathObj->ToggleClosed();

                if( bUndo )
                    mpDrawView->EndUndo();
            }
            rReq.Done();
        }
        break;

        case SID_CUT:
        {
            if ( mpDrawView->IsPresObjSelected(false, true, false, true) )
            {
                std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(GetFrameWeld(),
                                                              VclMessageType::Info, VclButtonsType::Ok,
                                                              SdResId(STR_ACTION_NOTPOSSIBLE)));
                xInfoBox->run();
            }
            else
            {
                //tdf#126197: EndTextEdit in all views if current one is not in TextEdit
                if ( !mpDrawView->IsTextEdit() )
                    mpDrawView->EndTextEditAllViews();

                if(HasCurrentFunction())
                {
                    GetCurrentFunction()->DoCut();
                }
                else if(mpDrawView)
                {
                    mpDrawView->DoCut();
                }
            }
            rReq.Ignore ();
        }
        break;

        case SID_COPY:
        {
            if ( mpDrawView->IsPresObjSelected(false, true, false, true) )
            {
                std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(GetFrameWeld(),
                                                              VclMessageType::Info, VclButtonsType::Ok,
                                                              SdResId(STR_ACTION_NOTPOSSIBLE)));
                xInfoBox->run();
            }
            else
            {
                if(HasCurrentFunction())
                {
                    GetCurrentFunction()->DoCopy();
                }
                else if( mpDrawView )
                {
                    mpDrawView->DoCopy();
                }
            }
            rReq.Ignore ();
        }
        break;

        case SID_PASTE:
        {
            weld::WaitObject aWait(GetFrameWeld());

            if(HasCurrentFunction())
            {
                GetCurrentFunction()->DoPaste();
            }
            else if(mpDrawView)
            {
                mpDrawView->DoPaste();
            }

            rReq.Ignore ();
        }
        break;

        case SID_PASTE_SLIDE:
        case SID_COPY_SLIDE:
        {
            sd::slidesorter::SlideSorterViewShell::GetSlideSorter(GetViewShellBase())
                ->GetSlideSorter()
                .GetController()
                .FuSupport(rReq);
            Cancel();
            rReq.Done();
        }
        break;

        case SID_UNICODE_NOTATION_TOGGLE:
        {
            if( mpDrawView->IsTextEdit() )
            {
                OutlinerView* pOLV = mpDrawView->GetTextEditOutlinerView();
                if( pOLV )
                {
                    OUString sInput = pOLV->GetSurroundingText();
                    ESelection aSel( pOLV->GetSelection() );
                    if (aSel.start.nIndex > aSel.end.nIndex)
                        aSel.end.nIndex = aSel.start.nIndex;

                    //calculate a valid end-position by reading logical characters
                    sal_Int32 nUtf16Pos=0;
                    while ((nUtf16Pos < sInput.getLength()) && (nUtf16Pos < aSel.end.nIndex))
                    {
                        sInput.iterateCodePoints(&nUtf16Pos);
                        //The mouse can set the cursor in the middle of a multi-unit character,
                        //    so reset to the proper end of the logical characters
                        if (nUtf16Pos > aSel.end.nIndex)
                            aSel.end.nIndex = nUtf16Pos;
                    }

                    ToggleUnicodeCodepoint aToggle;
                    while( nUtf16Pos && aToggle.AllowMoreInput( sInput[nUtf16Pos-1]) )
                        --nUtf16Pos;
                    OUString sReplacement = aToggle.ReplacementString();
                    if( !sReplacement.isEmpty() )
                    {
                        OUString sStringToReplace = aToggle.StringToReplace();
                        mpDrawView->BegUndo(sStringToReplace +"->"+ sReplacement);
                        aSel.start.nIndex = aSel.end.nIndex - sStringToReplace.getLength();
                        pOLV->SetSelection( aSel );
                        pOLV->InsertText(sReplacement, true);
                        mpDrawView->EndUndo();
                    }
                }
            }
        }
        break;

        case SID_PASTE_UNFORMATTED:
        {
            weld::WaitObject aWait(GetFrameWeld());

            if(HasCurrentFunction())
            {
                GetCurrentFunction()->DoPasteUnformatted();
            }
            else if(mpDrawView)
            {
                TransferableDataHelper aDataHelper( TransferableDataHelper::CreateFromSystemClipboard( GetActiveWindow() ) );
                if (aDataHelper.GetTransferable().is())
                {
                    sal_Int8 nAction = DND_ACTION_COPY;
                    mpDrawView->InsertData( aDataHelper,
                                            GetActiveWindow()->PixelToLogic( ::tools::Rectangle( Point(), GetActiveWindow()->GetOutputSizePixel() ).Center() ),
                                            nAction, false, SotClipboardFormatId::STRING);
                }
            }

            rReq.Ignore ();
        }
        break;

        case SID_CLIPBOARD_FORMAT_ITEMS:
        {
            weld::WaitObject aWait(GetFrameWeld());
            TransferableDataHelper  aDataHelper( TransferableDataHelper::CreateFromSystemClipboard( GetActiveWindow() ) );
            const SfxItemSet*       pReqArgs = rReq.GetArgs();
            SotClipboardFormatId    nFormat = SotClipboardFormatId::NONE;

            if( pReqArgs )
            {
                const SfxUInt32Item* pIsActive = rReq.GetArg<SfxUInt32Item>(SID_CLIPBOARD_FORMAT_ITEMS);
                nFormat = static_cast<SotClipboardFormatId>(pIsActive->GetValue());
            }

            if( nFormat != SotClipboardFormatId::NONE && aDataHelper.GetTransferable().is() )
            {
                sal_Int8 nAction = DND_ACTION_COPY;

                if( !mpDrawView->InsertData( aDataHelper,
                                          GetActiveWindow()->PixelToLogic( ::tools::Rectangle( Point(), GetActiveWindow()->GetOutputSizePixel() ).Center() ),
                                          nAction, false, nFormat ) )
                {
                    INetBookmark    aINetBookmark( u""_ustr, u""_ustr );

                    if( ( aDataHelper.HasFormat( SotClipboardFormatId::NETSCAPE_BOOKMARK ) &&
                          aDataHelper.GetINetBookmark( SotClipboardFormatId::NETSCAPE_BOOKMARK, aINetBookmark ) ) ||
                        ( aDataHelper.HasFormat( SotClipboardFormatId::FILEGRPDESCRIPTOR ) &&
                          aDataHelper.GetINetBookmark( SotClipboardFormatId::FILEGRPDESCRIPTOR, aINetBookmark ) ) ||
                        ( aDataHelper.HasFormat( SotClipboardFormatId::UNIFORMRESOURCELOCATOR ) &&
                          aDataHelper.GetINetBookmark( SotClipboardFormatId::UNIFORMRESOURCELOCATOR, aINetBookmark ) ) )
                    {
                        InsertURLField(aINetBookmark.GetURL(), aINetBookmark.GetDescription(), u""_ustr, u""_ustr);
                    }
                }
            }
        }
        break;

        case SID_DELETE:
        {
            if ( mpDrawView->IsTextEdit() )
            {
                OutlinerView* pOLV = mpDrawView->GetTextEditOutlinerView();

                if (pOLV)
                {
                    vcl::KeyCode aKCode(KEY_DELETE);
                    KeyEvent aKEvt( 0, aKCode);
                    // We use SdrObjEditView to handle DEL for underflow handling
                    (void)mpDrawView->KeyInput(aKEvt, nullptr);
                }
            }
            else
            {
                mpDrawView->EndTextEditAllViews();
                FuDeleteSelectedObjects();
            }
            rReq.Ignore ();
        }
        break;

        case SID_NOTES_MODE:
        case SID_SLIDE_MASTER_MODE:
        case SID_NOTES_MASTER_MODE:
        case SID_HANDOUT_MASTER_MODE:

            // AutoLayouts have to be ready.
            GetDoc()->StopWorkStartupDelay();
            [[fallthrough]];

        case SID_DRAWINGMODE:
        case SID_SLIDE_SORTER_MODE:
        case SID_OUTLINE_MODE:
            // Let the sub-shell manager handle the slot handling.
            framework::FrameworkHelper::Instance(GetViewShellBase())->HandleModeChangeSlot(
                nSId,
                rReq);
            rReq.Ignore ();
            break;

        case SID_MASTERPAGE:          // BASIC
        {
            // AutoLayouts needs to be finished
            GetDoc()->StopWorkStartupDelay();

            const SfxItemSet* pReqArgs = rReq.GetArgs();

            if ( pReqArgs )
            {
                const SfxBoolItem* pIsActive = rReq.GetArg<SfxBoolItem>(SID_MASTERPAGE);
                mbIsLayerModeActive = pIsActive->GetValue ();
            }

            Broadcast (
                ViewShellHint(ViewShellHint::HINT_CHANGE_EDIT_MODE_START));

            // turn on default layer of MasterPage
            mpDrawView->SetActiveLayer(sUNO_LayerName_background_objects);

            ChangeEditMode(EditMode::MasterPage, mbIsLayerModeActive);

            if(HasCurrentFunction(SID_BEZIER_EDIT))
                GetViewFrame()->GetDispatcher()->Execute(SID_OBJECT_SELECT, SfxCallMode::ASYNCHRON);

            Broadcast (
                ViewShellHint(ViewShellHint::HINT_CHANGE_EDIT_MODE_END));

            InvalidateWindows();
            Invalidate();

            rReq.Done();
        }
        break;

        case SID_CLOSE_MASTER_VIEW:
        {
            Broadcast (
                ViewShellHint(ViewShellHint::HINT_CHANGE_EDIT_MODE_START));

            // Switch page back to the first one.  Not doing so leads to a
            // crash.  This seems to be some bug in the edit mode switching
            // and page switching methods.
            SwitchPage (0);
            ChangeEditMode(EditMode::Page, IsLayerModeActive());
            Broadcast (
                ViewShellHint(ViewShellHint::HINT_CHANGE_EDIT_MODE_END));

            if(HasCurrentFunction(SID_BEZIER_EDIT))
            {
                GetViewFrame()->GetDispatcher()->Execute(
                    SID_OBJECT_SELECT,
                    SfxCallMode::ASYNCHRON);
            }

            rReq.Done();
        }
        break;

        case SID_RULER:
        {
            const SfxItemSet* pReqArgs = rReq.GetArgs();

            // Remember old ruler state
            bool bOldHasRuler(HasRuler());

            if ( pReqArgs )
            {
                const SfxBoolItem* pIsActive = rReq.GetArg<SfxBoolItem>(SID_RULER);
                SetRuler (pIsActive->GetValue ());
            }
            else SetRuler (!HasRuler());

            // Did ruler state change? Tell that to SdOptions, too.
            bool bHasRuler(HasRuler());

            if(bOldHasRuler != bHasRuler)
            {
                std::shared_ptr<comphelper::ConfigurationChanges> batch(
                    comphelper::ConfigurationChanges::create());

                if (GetDoc()->GetDocumentType() == DocumentType::Impress)
                {
                    officecfg::Office::Impress::Layout::Display::Ruler::set(bHasRuler, batch);
                }
                else
                {
                    officecfg::Office::Draw::Layout::Display::Ruler::set(bHasRuler, batch);
                }

                batch->commit();
            }

            Invalidate (SID_RULER);
            Resize();
            rReq.Done ();
        }
        break;

        case SID_SIZE_PAGE:
        case SID_SIZE_PAGE_WIDTH:  // BASIC
        {
            mbZoomOnPage = ( rReq.GetSlot() == SID_SIZE_PAGE );

            SdrPageView* pPageView = mpDrawView->GetSdrPageView();

            if ( pPageView )
            {
                Point aPagePos(0, 0); // = pPageView->GetOffset();
                Size aPageSize = pPageView->GetPage()->GetSize();

                aPagePos.AdjustX(aPageSize.Width()  / 2 );
                aPageSize.setWidth( static_cast<::tools::Long>(aPageSize.Width() * 1.03) );

                if( rReq.GetSlot() == SID_SIZE_PAGE )
                {
                    aPagePos.AdjustY(aPageSize.Height() / 2 );
                    aPageSize.setHeight( static_cast<::tools::Long>(aPageSize.Height() * 1.03) );
                    aPagePos.AdjustY( -(aPageSize.Height() / 2) );
                }
                else
                {
                    Point aPt = GetActiveWindow()->PixelToLogic( Point( 0, GetActiveWindow()->GetSizePixel().Height() / 2 ) );
                    aPagePos.AdjustY(aPt.Y() );
                    aPageSize.setHeight( 2 );
                }

                aPagePos.AdjustX( -(aPageSize.Width()  / 2) );

                SetZoomRect( ::tools::Rectangle( aPagePos, aPageSize ) );

                ::tools::Rectangle aVisAreaWin = GetActiveWindow()->PixelToLogic( ::tools::Rectangle( Point(0,0),
                                              GetActiveWindow()->GetOutputSizePixel()) );
                mpZoomList->InsertZoomRect(aVisAreaWin);
            }
            Invalidate( SID_ZOOM_IN );
            Invalidate( SID_ZOOM_OUT );
            Invalidate( SID_ZOOM_PANNING );
            rReq.Done ();
        }
        break;

        case SID_SIZE_REAL:  // BASIC
        {
            mbZoomOnPage = false;
            SetZoom( 100 );
            ::tools::Rectangle aVisAreaWin = GetActiveWindow()->PixelToLogic( ::tools::Rectangle( Point(0,0),
                                              GetActiveWindow()->GetOutputSizePixel()) );
            mpZoomList->InsertZoomRect(aVisAreaWin);
            Invalidate( SID_ZOOM_IN );
            Invalidate( SID_ZOOM_OUT );
            Invalidate( SID_ZOOM_PANNING );
            rReq.Done ();
        }
        break;

        case SID_ZOOM_OUT:  // BASIC
        {
            const sal_uInt16 nOldZoom = GetActiveWindow()->GetZoom();
            const sal_uInt16 nNewZoom = basegfx::zoomtools::zoomOut(nOldZoom);
            SetZoom(nNewZoom);

            mbZoomOnPage = false;
            ::tools::Rectangle aVisAreaWin = GetActiveWindow()->PixelToLogic( ::tools::Rectangle( Point(0,0),
                                              GetActiveWindow()->GetOutputSizePixel()) );
            mpZoomList->InsertZoomRect(aVisAreaWin);
            Invalidate( SID_ZOOM_IN );
            Invalidate( SID_ZOOM_OUT );
            Invalidate( SID_ZOOM_PANNING );
            rReq.Done ();
        }
        break;

        case SID_ZOOM_IN:
        {
            const sal_uInt16 nOldZoom = GetActiveWindow()->GetZoom();
            const sal_uInt16 nNewZoom = basegfx::zoomtools::zoomIn(nOldZoom);
            SetZoom(nNewZoom);

            mbZoomOnPage = false;
            ::tools::Rectangle aVisAreaWin = GetActiveWindow()->PixelToLogic( ::tools::Rectangle( Point(0,0),
                                              GetActiveWindow()->GetOutputSizePixel()) );
            mpZoomList->InsertZoomRect(aVisAreaWin);
            Invalidate( SID_ZOOM_IN );
            Invalidate(SID_ZOOM_OUT);
            Invalidate( SID_ZOOM_PANNING );
            rReq.Done ();
        }
        break;

        case SID_SIZE_VISAREA:
        {
            ::tools::Rectangle aVisArea = mpFrameView->GetVisArea();
            Size aVisAreaSize = aVisArea.GetSize();

            if (!aVisAreaSize.IsEmpty())
            {
                mbZoomOnPage = false;
                SetZoomRect(aVisArea);
                Invalidate( SID_ZOOM_IN );
                Invalidate( SID_ZOOM_OUT );
                Invalidate( SID_ZOOM_PANNING );
            }
            rReq.Done ();
        }
        break;

        // name confusion: SID_SIZE_OPTIMAL -> Zoom onto selected objects
        // --> Is offered as object zoom in program
        case SID_SIZE_OPTIMAL:  // BASIC
        {
            mbZoomOnPage = false;
            const SdrMarkList& rMarkList = mpDrawView->GetMarkedObjectList();
            if ( rMarkList.GetMarkCount() != 0 )
            {
                maMarkRect = mpDrawView->GetAllMarkedRect();
                ::tools::Long nW = static_cast<::tools::Long>(maMarkRect.GetWidth()  * 1.03);
                ::tools::Long nH = static_cast<::tools::Long>(maMarkRect.GetHeight() * 1.03);
                Point aPos = maMarkRect.Center();
                aPos.AdjustX( -(nW / 2) );
                aPos.AdjustY( -(nH / 2) );
                if ( nW && nH )
                {
                    SetZoomRect(::tools::Rectangle(aPos, Size(nW, nH)));

                    ::tools::Rectangle aVisAreaWin = GetActiveWindow()->PixelToLogic( ::tools::Rectangle( Point(0,0),
                                              GetActiveWindow()->GetOutputSizePixel()) );
                    mpZoomList->InsertZoomRect(aVisAreaWin);
                }
            }
            Invalidate( SID_ZOOM_IN );
            Invalidate( SID_ZOOM_OUT );
            Invalidate( SID_ZOOM_PANNING );
            rReq.Done ();
        }
        break;

        // name confusion: SID_SIZE_ALL -> Zoom onto all objects
        // --> Is offered as optimal in program
        case SID_SIZE_ALL:  // BASIC
        {
            mbZoomOnPage = false;
            SdrPageView* pPageView = mpDrawView->GetSdrPageView();

            if( pPageView )
            {
                ::tools::Rectangle aBoundRect( pPageView->GetObjList()->GetAllObjBoundRect() );

                ::tools::Long nW = static_cast<::tools::Long>(aBoundRect.GetWidth() * 1.03);
                ::tools::Long nH = static_cast<::tools::Long>(aBoundRect.GetHeight() * 1.03);
                Point aPos = aBoundRect.Center();
                aPos.AdjustX( -(nW / 2) );
                aPos.AdjustY( -(nH / 2) );
                if ( nW && nH )
                {
                    SetZoomRect( ::tools::Rectangle( aPos, Size( nW, nH ) ) );

                    ::tools::Rectangle aVisAreaWin = GetActiveWindow()->PixelToLogic( ::tools::Rectangle( Point(0,0),
                                              GetActiveWindow()->GetOutputSizePixel()) );
                    mpZoomList->InsertZoomRect(aVisAreaWin);
                }

                Invalidate( SID_ZOOM_IN );
                Invalidate( SID_ZOOM_OUT );
                Invalidate( SID_ZOOM_PANNING );
            }
            rReq.Done ();
        }
        break;

        case SID_ZOOM_PREV:
        {
            if (mpDrawView->IsTextEdit())
            {
                mpDrawView->SdrEndTextEdit();
            }

            if (mpZoomList->IsPreviousPossible())
            {
                // set previous ZoomRect
                SetZoomRect(mpZoomList->GetPreviousZoomRect());
            }
            rReq.Done ();
        }
        break;

        case SID_ZOOM_NEXT:
        {
            if (mpDrawView->IsTextEdit())
            {
                mpDrawView->SdrEndTextEdit();
            }

            if (mpZoomList->IsNextPossible())
            {
                // set next ZoomRect
                SetZoomRect(mpZoomList->GetNextZoomRect());
            }
            rReq.Done ();
        }
        break;

        case SID_GLUE_INSERT_POINT:
        case SID_GLUE_PERCENT:
        case SID_GLUE_ESCDIR:
        case SID_GLUE_ESCDIR_LEFT:
        case SID_GLUE_ESCDIR_RIGHT:
        case SID_GLUE_ESCDIR_TOP:
        case SID_GLUE_ESCDIR_BOTTOM:
        case SID_GLUE_HORZALIGN_CENTER:
        case SID_GLUE_HORZALIGN_LEFT:
        case SID_GLUE_HORZALIGN_RIGHT:
        case SID_GLUE_VERTALIGN_CENTER:
        case SID_GLUE_VERTALIGN_TOP:
        case SID_GLUE_VERTALIGN_BOTTOM:
        {
            rtl::Reference<FuPoor> xFunc( GetCurrentFunction() );
            FuEditGluePoints* pFunc = dynamic_cast< FuEditGluePoints* >( xFunc.get() );

            if(pFunc)
                pFunc->ReceiveRequest(rReq);

            rReq.Done();
        }
        break;

        case SID_AUTOSPELL_CHECK:
        {
            bool bOnlineSpell;
            const SfxPoolItem* pItem;

            if (rReq.GetArgs()->HasItem(FN_PARAM_1, &pItem))
                bOnlineSpell = static_cast<const SfxBoolItem*>(pItem)->GetValue();
            else // Toggle
                bOnlineSpell = !GetDoc()->GetOnlineSpell();

            GetDoc()->SetOnlineSpell(bOnlineSpell);

            ::Outliner* pOL = mpDrawView->GetTextEditOutliner();

            if (pOL)
            {
                EEControlBits nCntrl = pOL->GetControlWord();

                if (bOnlineSpell)
                    nCntrl |= EEControlBits::ONLINESPELLING;
                else
                    nCntrl &= ~EEControlBits::ONLINESPELLING;

                pOL->SetControlWord(nCntrl);
            }

            GetActiveWindow()->Invalidate();
            rReq.Done ();
        }
        break;

        case SID_TRANSLITERATE_SENTENCE_CASE:
        case SID_TRANSLITERATE_TITLE_CASE:
        case SID_TRANSLITERATE_TOGGLE_CASE:
        case SID_TRANSLITERATE_UPPER:
        case SID_TRANSLITERATE_LOWER:
        case SID_TRANSLITERATE_HALFWIDTH:
        case SID_TRANSLITERATE_FULLWIDTH:
        case SID_TRANSLITERATE_HIRAGANA:
        case SID_TRANSLITERATE_KATAKANA:
        {
            OutlinerView* pOLV = GetView()->GetTextEditOutlinerView();
            if( pOLV )
            {
                TransliterationFlags nType = TransliterationFlags::NONE;

                switch( nSId )
                {
                    case SID_TRANSLITERATE_SENTENCE_CASE:
                        nType = TransliterationFlags::SENTENCE_CASE;
                        break;
                    case SID_TRANSLITERATE_TITLE_CASE:
                        nType = TransliterationFlags::TITLE_CASE;
                        break;
                    case SID_TRANSLITERATE_TOGGLE_CASE:
                        nType = TransliterationFlags::TOGGLE_CASE;
                        break;
                    case SID_TRANSLITERATE_UPPER:
                        nType = TransliterationFlags::LOWERCASE_UPPERCASE;
                        break;
                    case SID_TRANSLITERATE_LOWER:
                        nType = TransliterationFlags::UPPERCASE_LOWERCASE;
                        break;
                    case SID_TRANSLITERATE_HALFWIDTH:
                        nType = TransliterationFlags::FULLWIDTH_HALFWIDTH;
                        break;
                    case SID_TRANSLITERATE_FULLWIDTH:
                        nType = TransliterationFlags::HALFWIDTH_FULLWIDTH;
                        break;
                    case SID_TRANSLITERATE_HIRAGANA:
                        nType = TransliterationFlags::KATAKANA_HIRAGANA;
                        break;
                    case SID_TRANSLITERATE_KATAKANA:
                        nType = TransliterationFlags::HIRAGANA_KATAKANA;
                        break;
                }

                pOLV->TransliterateText( nType );
            }

            rReq.Done();
        }
        break;

        // #UndoRedo#
        case SID_UNDO :
        {
            // moved implementation to BaseClass
            ImpSidUndo(rReq);
        }
        break;
        case SID_REDO :
        {
            // moved implementation to BaseClass
            ImpSidRedo(rReq);
        }
        break;

        default:
        break;
    }
}

void DrawViewShell::FuSupportRotate(SfxRequest const &rReq)
{
    if( rReq.GetSlot() != SID_TRANSLITERATE_ROTATE_CASE )
        return;

    ::sd::View* pView = GetView();

    if (!pView)
        return;

    OutlinerView* pOLV = pView->GetTextEditOutlinerView();

    if (!pOLV)
        return;
    TransliterationFlags transFlags = m_aRotateCase.getNextMode();
    if (TransliterationFlags::SENTENCE_CASE == transFlags)
    {
        OUString SelectedText = pOLV->GetSelected().trim();
        if (SelectedText.getLength() <= 2 || (SelectedText.indexOf(' ') < 0 && SelectedText.indexOf('\t') < 0))
            transFlags = m_aRotateCase.getNextMode();
    }
    pOLV->TransliterateText( transFlags );
}

void DrawViewShell::InsertURLField(const OUString& rURL, const OUString& rText,
                                   const OUString& rTarget, OUString const& rAltText)
{
    OutlinerView* pOLV = mpDrawView->GetTextEditOutlinerView();

    SvxURLField aURLField(rURL, rText, SvxURLFormat::Repr);
    aURLField.SetTargetFrame(rTarget);
    aURLField.SetName(rAltText);

    if (pOLV)
    {
        ESelection aSel( pOLV->GetSelection() );
        SvxFieldItem const aURLItem(aURLField, EE_FEATURE_FIELD);
        pOLV->InsertField( aURLItem );
        if (aSel.start.nIndex <= aSel.end.nIndex)
            aSel.end.nIndex = aSel.start.nIndex + 1;
        else
            aSel.start.nIndex = aSel.end.nIndex + 1;
        pOLV->SetSelection( aSel );
    }
    else
    {
        Outliner* pOutl = GetDoc()->GetInternalOutliner();
        pOutl->Init( OutlinerMode::TextObject );
        OutlinerMode nOutlMode = pOutl->GetOutlinerMode();

        SvxFieldItem aURLItem(aURLField, EE_FEATURE_FIELD);
        pOutl->QuickInsertField( aURLItem, ESelection() );
        std::optional<OutlinerParaObject> pOutlParaObject = pOutl->CreateParaObject();

        rtl::Reference<SdrRectObj> pRectObj = new SdrRectObj(
            GetView()->getSdrModelFromSdrView(), ::tools::Rectangle(), SdrObjKind::Text);

        pOutl->UpdateFields();
        pOutl->SetUpdateLayout( true );
        Size aSize(pOutl->CalcTextSize());
        pOutl->SetUpdateLayout( false );

        Point aPos;
        ::tools::Rectangle aRect(aPos, GetActiveWindow()->GetOutputSizePixel() );
        aPos = aRect.Center();
        aPos = GetActiveWindow()->PixelToLogic(aPos);

        if (aPos.getX() - (aSize.Width() / 2) >= 0)
            aPos.AdjustX( -(aSize.Width() / 2) );
        if (aPos.getY() - (aSize.Height() / 2) >= 0)
            aPos.AdjustY( -(aSize.Height() / 2) );

        ::tools::Rectangle aLogicRect(aPos, aSize);
        pRectObj->SetLogicRect(aLogicRect);
        pRectObj->SetOutlinerParaObject( std::move(pOutlParaObject) );
        mpDrawView->InsertObjectAtView(pRectObj.get(), *mpDrawView->GetSdrPageView());
        pOutl->Init( nOutlMode );
    }
}

void DrawViewShell::InsertURLButton(const OUString& rURL, const OUString& rText,
                                    const OUString& rTarget, const Point* pPos)
{
    bool bNewObj = true;

    const OUString sTargetURL( ::URIHelper::SmartRel2Abs( INetURLObject( GetDocSh()->GetMedium()->GetBaseURL() ), rURL, URIHelper::GetMaybeFileHdl(), true, false,
                                                                INetURLObject::EncodeMechanism::WasEncoded,
                                                                INetURLObject::DecodeMechanism::Unambiguous ) );
    const SdrMarkList&  rMarkList = mpDrawView->GetMarkedObjectList();
    if (rMarkList.GetMarkCount() > 0)
    {
        SdrObject* pMarkedObj = rMarkList.GetMark(0)->GetMarkedSdrObj();
        if( pMarkedObj ) try
        {
            // change first marked object
            if (SdrInventor::FmForm == pMarkedObj->GetObjInventor())
            {
                SdrUnoObj* pUnoCtrl = static_cast< SdrUnoObj* >( pMarkedObj );

                Reference< awt::XControlModel > xControlModel( pUnoCtrl->GetUnoControlModel(), UNO_SET_THROW );
                Reference< beans::XPropertySet > xPropSet( xControlModel, UNO_QUERY_THROW );

                bool bIsButton = pMarkedObj->GetObjIdentifier() == SdrObjKind::FormButton;
                if (!bIsButton && pMarkedObj->GetObjIdentifier() == SdrObjKind::UNO)
                {
                    const Reference<beans::XPropertySetInfo> xInfo(xPropSet->getPropertySetInfo());
                    bIsButton = xInfo.is() && xInfo->hasPropertyByName(u"ButtonType"_ustr)
                                && xInfo->hasPropertyByName(u"Label"_ustr)
                                && xInfo->hasPropertyByName(u"TargetURL"_ustr);
                }
                if (bIsButton)
                {
                    bNewObj = false;

                    xPropSet->setPropertyValue(u"Label"_ustr, Any(rText));
                    xPropSet->setPropertyValue(u"TargetURL"_ustr, Any(sTargetURL));

                    if (!rTarget.isEmpty())
                        xPropSet->setPropertyValue(u"TargetFrame"_ustr, Any(rTarget));

                    xPropSet->setPropertyValue(u"ButtonType"_ustr, Any(form::FormButtonType_URL));
#if HAVE_FEATURE_AVMEDIA
                    if (::avmedia::MediaWindow::isMediaURL(rURL, u""_ustr/*TODO?*/))
                    {
                        xPropSet->setPropertyValue(u"DispatchURLInternal"_ustr, Any(true));
                    }
#endif
                }
            }
            if (bNewObj)
            {
                // add url as interaction for first selected shape
                bNewObj = false;

                SdAnimationInfo* pInfo = SdDrawDocument::GetShapeUserData(*pMarkedObj, true);
                pInfo->meClickAction = presentation::ClickAction_DOCUMENT;
                pInfo->SetBookmark( sTargetURL );
            }
        }
        catch( uno::Exception& )
        {
        }
    }

    if (!bNewObj)
        return;

    try
    {
        rtl::Reference<SdrUnoObj> pUnoCtrl = static_cast< SdrUnoObj* >(
            SdrObjFactory::MakeNewObject(
                GetView()->getSdrModelFromSdrView(),
                SdrInventor::FmForm,
                SdrObjKind::FormButton).get()); //,
                //mpDrawView->GetSdrPageView()->GetPage()));

        Reference< awt::XControlModel > xControlModel( pUnoCtrl->GetUnoControlModel(), uno::UNO_SET_THROW );
        Reference< beans::XPropertySet > xPropSet( xControlModel, uno::UNO_QUERY_THROW );

        xPropSet->setPropertyValue( u"Label"_ustr , Any( rText ) );
        xPropSet->setPropertyValue( u"TargetURL"_ustr , Any( sTargetURL ) );

        if( !rTarget.isEmpty() )
            xPropSet->setPropertyValue( u"TargetFrame"_ustr , Any( rTarget ) );

        xPropSet->setPropertyValue( u"ButtonType"_ustr , Any(  form::FormButtonType_URL ) );
#if HAVE_FEATURE_AVMEDIA
        if ( ::avmedia::MediaWindow::isMediaURL( rURL, u""_ustr/*TODO?*/ ) )
            xPropSet->setPropertyValue( u"DispatchURLInternal"_ustr , Any( true ) );
#endif

        Point aPos;

        if (pPos)
        {
            aPos = *pPos;
        }
        else
        {
            aPos = ::tools::Rectangle(aPos, GetActiveWindow()->GetOutputSizePixel()).Center();
            aPos = GetActiveWindow()->PixelToLogic(aPos);
        }

        Size aSize(4000, 1000);
        aPos.AdjustX( -(aSize.Width() / 2) );
        aPos.AdjustY( -(aSize.Height() / 2) );
        pUnoCtrl->SetLogicRect(::tools::Rectangle(aPos, aSize));

        SdrInsertFlags nOptions = SdrInsertFlags::SETDEFLAYER;

        OSL_ASSERT (GetViewShell()!=nullptr);
        SfxInPlaceClient* pIpClient = GetViewShell()->GetIPClient();
        if (pIpClient!=nullptr && pIpClient->IsObjectInPlaceActive())
        {
            nOptions |= SdrInsertFlags::DONTMARK;
        }

        mpDrawView->InsertObjectAtView(pUnoCtrl.get(), *mpDrawView->GetSdrPageView(), nOptions);
    }
    catch( Exception& )
    {
    }
}

void DrawViewShell::ShowUIControls (bool bVisible)
{
    ViewShell::ShowUIControls (bVisible);
    maTabControl->Show (bVisible);
}

namespace slideshowhelp
{
    void ShowSlideShow(SfxRequest const & rReq, SdDrawDocument &rDoc)
    {
        Reference< XPresentation2 > xPresentation( rDoc.getPresentation() );
        if( !xPresentation.is() )
            return;

        sfx2::SfxNotebookBar::LockNotebookBar();
        if (SID_REHEARSE_TIMINGS == rReq.GetSlot())
            xPresentation->rehearseTimings();
        else if (rDoc.getPresentationSettings().mbCustomShow)
        {
            //fdo#69975 if a custom show has been set, then
            //use it whether or not we've been asked to
            //start from the current or first slide
            xPresentation->start();

            // if the custom show not set by default, only show it.
            if (rDoc.getPresentationSettings().mbStartCustomShow)
                rDoc.getPresentationSettings().mbCustomShow = false;
        }
        else if (SID_PRESENTATION_CURRENT_SLIDE == rReq.GetSlot())
        {
            //If there is no custom show set, start will automatically
            //start at the current page
            xPresentation->start();
        }
        else
        {
            //Start at page 0, this would blow away any custom
            //show settings if any were set
            const SfxUInt16Item* pStartingSlide = rReq.GetArg<SfxUInt16Item>(FN_PARAM_1);
            const sal_uInt16 nStartingSlide = pStartingSlide ? pStartingSlide->GetValue() - 1 : 0;
            SdPage* pSlide = rDoc.GetSdPage(nStartingSlide, PageKind::Standard);
            const OUString aStartingSlide = pSlide ? pSlide->GetName() : OUString();

            Sequence< PropertyValue > aArguments{ comphelper::makePropertyValue(u"FirstPage"_ustr,
                                                                                aStartingSlide) };
            xPresentation->startWithArguments( aArguments );
        }
        sfx2::SfxNotebookBar::UnlockNotebookBar();
    }
}

void DrawViewShell::StopSlideShow()
{
    Reference< XPresentation2 > xPresentation( GetDoc()->getPresentation() );
    if(xPresentation.is() && xPresentation->isRunning())
    {
        if( mpDrawView->IsTextEdit() )
            mpDrawView->SdrEndTextEdit();

        xPresentation->end();
    }
}

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
