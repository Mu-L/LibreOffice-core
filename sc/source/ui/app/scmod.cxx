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

#include <com/sun/star/ui/dialogs/XSLTFilterDialog.hpp>
#include <comphelper/lok.hxx>
#include <comphelper/processfactory.hxx>
#include <scitems.hxx>
#include <sfx2/app.hxx>

#include <editeng/flditem.hxx>
#include <editeng/outliner.hxx>

#include <sfx2/viewfrm.hxx>
#include <sfx2/objface.hxx>

#include <IAnyRefDialog.hxx>

#include <svtools/ehdl.hxx>
#include <svtools/accessibilityoptions.hxx>
#include <svl/ctloptions.hxx>
#include <unotools/useroptions.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/request.hxx>
#include <sfx2/printer.hxx>
#include <editeng/langitem.hxx>
#include <svtools/colorcfg.hxx>

#include <svl/whiter.hxx>
#include <svx/dialogs.hrc>
#include <svl/inethist.hxx>
#include <vcl/svapp.hxx>
#include <svx/svxerr.hxx>
#include <comphelper/diagnose_ex.hxx>

#include <editeng/unolingu.hxx>
#include <unotools/lingucfg.hxx>
#include <i18nlangtag/mslangid.hxx>
#include <i18nlangtag/languagetag.hxx>
#include <com/sun/star/i18n/ScriptType.hpp>
#include <com/sun/star/linguistic2/XThesaurus.hpp>
#include <ooo/vba/XSinkCaller.hpp>

#include <scmod.hxx>
#include <global.hxx>
#include <viewopti.hxx>
#include <docoptio.hxx>
#include <appoptio.hxx>
#include <defaultsoptions.hxx>
#include <formulaopt.hxx>
#include <inputopt.hxx>
#include <printopt.hxx>
#include <navicfg.hxx>
#include <addincfg.hxx>
#include <tabvwsh.hxx>
#include <prevwsh.hxx>
#include <docsh.hxx>
#include <drwlayer.hxx>
#include <uiitems.hxx>
#include <sc.hrc>
#include <scerrors.hrc>
#include <scstyles.hrc>
#include <globstr.hrc>
#include <scresid.hxx>
#include <bitmaps.hlst>
#include <inputhdl.hxx>
#include <inputwin.hxx>
#include <msgpool.hxx>
#include <detfunc.hxx>
#include <preview.hxx>
#include <dragdata.hxx>
#include <markdata.hxx>
#include <transobj.hxx>
#include <funcdesc.hxx>

#define ShellClass_ScModule
#include <scslots.hxx>

#include <scabstdlg.hxx>
#include <formula/errorcodes.hxx>
#include <documentlinkmgr.hxx>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <sfx2/lokhelper.hxx>

#define SC_IDLE_MIN     150
#define SC_IDLE_MAX     3000
#define SC_IDLE_STEP    75
#define SC_IDLE_COUNT   50

static sal_uInt16 nIdleCount = 0;

SFX_IMPL_INTERFACE(ScModule, SfxShell)

void ScModule::InitInterface_Impl()
{
    GetStaticInterface()->RegisterObjectBar(SFX_OBJECTBAR_APPLICATION,
                                            SfxVisibilityFlags::Standard | SfxVisibilityFlags::Client | SfxVisibilityFlags::Viewer,
                                            ToolbarId::Objectbar_App);

    GetStaticInterface()->RegisterStatusBar(StatusBarId::CalcStatusBar);
}

ScModule::ScModule( SfxObjectFactory* pFact ) :
    SfxModule("sc"_ostr, {pFact}),
    m_aIdleTimer("sc ScModule IdleTimer"),
    m_pDragData(new ScDragData),
    m_pSelTransfer( nullptr ),
    m_pRefInputHandler( nullptr ),
    m_nCurRefDlgId( 0 ),
    m_bIsWaterCan( false ),
    m_bIsInEditCommand( false ),
    m_bIsInExecuteDrop( false ),
    m_bIsInSharedDocLoading( false ),
    m_bIsInSharedDocSaving( false )
{
    // The ResManager (DLL data) is not yet initialized in the ctor!
    SetName(u"StarCalc"_ustr); // for Basic

    ResetDragObject();

    // InputHandler does not need to be created

    // Create ErrorHandler - was in Init()
    // Between OfficeApplication::Init and ScGlobal::Init
    SvxErrorHandler::ensure();
    m_pErrorHdl.reset( new SfxErrorHandler(RID_ERRHDLSC,
                                       ErrCodeArea::Sc,
                                       ErrCodeArea::Sc,
                                       GetResLocale()) );

    m_aIdleTimer.SetPriority(TaskPriority::DEFAULT_IDLE);
    m_aIdleTimer.SetTimeout(SC_IDLE_MIN);
    m_aIdleTimer.SetInvokeHandler( LINK( this, ScModule, IdleHandler ) );
    m_aIdleTimer.Start();

    m_pMessagePool = new ScMessagePool;
    SetPool( m_pMessagePool.get() );

    ScGlobal::InitTextHeight( *m_pMessagePool );

    StartListening( *SfxGetpApp() );       // for SfxHintId::Deinitializing

    // Initialize the color config
    GetColorConfig();
}

ScModule::~ScModule()
{
    OSL_ENSURE( !m_pSelTransfer, "Selection Transfer object not deleted" );

    // InputHandler does not need to be deleted (there's none in the App anymore)

    m_pMessagePool.clear();

    m_pDragData.reset();
    m_pErrorHdl.reset();

    ScGlobal::Clear(); // Also calls ScDocumentPool::DeleteVersionMaps();

    ScInterpreterContextPool::ModuleExiting();

    DeleteCfg(); // Called from Exit()
}

void ScModule::ConfigurationChanged(utl::ConfigurationBroadcaster* p, ConfigurationHints eHints)
{
    if ( p == m_pColorConfig.get() )
    {
        // Test if detective objects have to be updated with new colors
        // (if the detective colors haven't been used yet, there's nothing to update)
        if ( ScDetectiveFunc::IsColorsInitialized() )
        {
            const svtools::ColorConfig& rColors = GetColorConfig();
            bool bArrows =
                ( ScDetectiveFunc::GetArrowColor() != rColors.GetColorValue(svtools::CALCDETECTIVE).nColor ||
                  ScDetectiveFunc::GetErrorColor() != rColors.GetColorValue(svtools::CALCDETECTIVEERROR).nColor );
            bool bComments =
                ( ScDetectiveFunc::GetCommentColor() != rColors.GetColorValue(svtools::CALCNOTESBACKGROUND).nColor );
            if ( bArrows || bComments )
            {
                ScDetectiveFunc::InitializeColors(); // get the new colors

                // update detective objects in all open documents
                SfxObjectShell* pObjSh = SfxObjectShell::GetFirst();
                while ( pObjSh )
                {
                    if ( auto pDocSh = dynamic_cast<ScDocShell * >(pObjSh) )
                    {
                        if ( bArrows )
                            ScDetectiveFunc( pDocSh->GetDocument(), 0 ).UpdateAllArrowColors();
                        if ( bComments )
                            ScDetectiveFunc::UpdateAllComments( pDocSh->GetDocument() );
                    }
                    pObjSh = SfxObjectShell::GetNext( *pObjSh );
                }
            }
        }

        const bool bKit = comphelper::LibreOfficeKit::isActive();

        //invalidate only the current view in tiled rendering mode, or all views otherwise
        SfxViewShell* pViewShell = bKit ? SfxViewShell::Current() : SfxViewShell::GetFirst();
        while (pViewShell)
        {
            if (ScTabViewShell* pViewSh = dynamic_cast<ScTabViewShell*>(pViewShell))
            {
                ScViewRenderingOptions aViewRenderingOptions(pViewSh->GetViewRenderingData());
                Color aFillColor(m_pColorConfig->GetColorValue(svtools::DOCCOLOR).nColor);
                aViewRenderingOptions.SetDocColor(aFillColor);
                aViewRenderingOptions.SetColorSchemeName(svtools::ColorConfig::GetCurrentSchemeName());
                const bool bUnchanged(aViewRenderingOptions == pViewSh->GetViewRenderingData());
                if (!bUnchanged)
                    pViewSh->SetViewRenderingData(aViewRenderingOptions);

                if (SfxObjectShell* pKitCurrentObjSh = bKit ? SfxObjectShell::Current() : nullptr)
                {
                    ScModelObj* pScModelObj = comphelper::getFromUnoTunnel<ScModelObj>(pKitCurrentObjSh->GetModel());
                    SfxLokHelper::notifyViewRenderState(pViewSh, pScModelObj);
                    // In Online, the document color is the one used for the background, contrary to
                    // Writer and Draw that use the application background color.
                    pViewSh->libreOfficeKitViewCallback(LOK_CALLBACK_APPLICATION_BACKGROUND_COLOR,
                            aFillColor.AsRGBHexString().toUtf8());
                }

                // if nothing changed, and the hint was OnlyCurrentDocumentColorScheme we can skip invalidate
                const bool bSkipInvalidate = bKit ||(bUnchanged && eHints == ConfigurationHints::OnlyCurrentDocumentColorScheme);
                if (!bSkipInvalidate)
                {
                    pViewSh->PaintGrid();
                    pViewSh->PaintTop();
                    pViewSh->PaintLeft();
                    pViewSh->PaintExtras();
                }

                ScInputHandler* pHdl = pViewSh->GetInputHandler();
                if ( pHdl )
                    pHdl->ForgetLastPattern(); // EditEngine BackgroundColor may change
            }
            else if ( dynamic_cast<const ScPreviewShell*>( pViewShell) !=  nullptr )
            {
                vcl::Window* pWin = pViewShell->GetWindow();
                if (pWin)
                    pWin->Invalidate();
            }
            if (bKit)
                break;
            pViewShell = SfxViewShell::GetNext( *pViewShell );
        }
    }
    else if ( p == m_pCTLOptions.get() )
    {
        // for all documents: set digit language for printer, recalc output factor, update row heights
        SfxObjectShell* pObjSh = SfxObjectShell::GetFirst();
        while ( pObjSh )
        {
            if ( auto pDocSh = dynamic_cast<ScDocShell *>(pObjSh) )
            {
                OutputDevice* pPrinter = pDocSh->GetPrinter();
                if ( pPrinter )
                    pPrinter->SetDigitLanguage( GetOptDigitLanguage() );

                pDocSh->CalcOutputFactor();

                SCTAB nTabCount = pDocSh->GetDocument().GetTableCount();
                for (SCTAB nTab=0; nTab<nTabCount; nTab++)
                    pDocSh->AdjustRowHeight( 0, pDocSh->GetDocument().MaxRow(), nTab );
            }
            pObjSh = SfxObjectShell::GetNext( *pObjSh );
        }

        // for all views (table and preview): update digit language
        SfxViewShell* pSh = SfxViewShell::GetFirst();
        while ( pSh )
        {
            if (ScTabViewShell* pViewSh = dynamic_cast<ScTabViewShell*>(pSh))
            {
                // set ref-device for EditEngine (re-evaluates digit settings)
                ScInputHandler* pHdl = GetInputHdl(pViewSh);
                if (pHdl)
                    pHdl->UpdateRefDevice();

                pViewSh->DigitLanguageChanged();
                pViewSh->PaintGrid();
            }
            else if (ScPreviewShell* pPreviewSh = dynamic_cast<ScPreviewShell*>(pSh))
            {
                ScPreview* pPreview = pPreviewSh->GetPreview();

                pPreview->GetOutDev()->SetDigitLanguage( GetOptDigitLanguage() );
                pPreview->Invalidate();
            }

            pSh = SfxViewShell::GetNext( *pSh );
        }
    }
}

void ScModule::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    if ( rHint.GetId() == SfxHintId::Deinitializing )
    {
        // ConfigItems must be removed before ConfigManager
        DeleteCfg();
    }
}

void ScModule::DeleteCfg()
{
    m_pViewCfg.reset(); // Saving happens automatically before Exit()
    m_pDocCfg.reset();
    m_pAppCfg.reset();
    m_pDefaultsCfg.reset();
    m_pFormulaCfg.reset();
    m_pInputCfg.reset();
    m_pPrintCfg.reset();
    m_pNavipiCfg.reset();
    m_pAddInCfg.reset();

    if ( m_pColorConfig )
    {
        m_pColorConfig->RemoveListener(this);
        m_pColorConfig.reset();
    }
    if ( m_pCTLOptions )
    {
        m_pCTLOptions->RemoveListener(this);
        m_pCTLOptions.reset();
    }
    m_pUserOptions.reset();
}

// Moved here from the App

void ScModule::Execute( SfxRequest& rReq )
{
    SfxViewFrame* pViewFrm = SfxViewFrame::Current();
    SfxBindings* pBindings = pViewFrm ? &pViewFrm->GetBindings() : nullptr;

    const SfxItemSet*   pReqArgs    = rReq.GetArgs();
    sal_uInt16              nSlot       = rReq.GetSlot();

    switch ( nSlot )
    {
        case SID_CHOOSE_DESIGN:
            SfxApplication::CallAppBasic( u"Template.Samples.ShowStyles"_ustr );
            break;
        case SID_EURO_CONVERTER:
            SfxApplication::CallAppBasic( u"Euro.ConvertRun.Main"_ustr );
            break;
        case SID_AUTOSPELL_CHECK:
            {
                bool bSet;
                const SfxPoolItem* pItem;
                if (pReqArgs && SfxItemState::SET == pReqArgs->GetItemState( FN_PARAM_1, true, &pItem ))
                    bSet = static_cast<const SfxBoolItem*>(pItem)->GetValue();
                else if ( pReqArgs && SfxItemState::SET == pReqArgs->GetItemState( nSlot, true, &pItem ) )
                    bSet = static_cast<const SfxBoolItem*>(pItem)->GetValue();
                else
                {   // Toggle
                    ScTabViewShell* pViewSh = dynamic_cast<ScTabViewShell*>(SfxViewShell::Current());
                    if (pViewSh)
                        bSet = !pViewSh->IsAutoSpell();
                    else
                        bSet = !ScModule::GetAutoSpellProperty();
                }

                SfxItemSetFixed<SID_AUTOSPELL_CHECK, SID_AUTOSPELL_CHECK> aSet( GetPool() );
                aSet.Put( SfxBoolItem( SID_AUTOSPELL_CHECK, bSet ) );
                ModifyOptions( aSet );
                rReq.Done();
            }
            break;

        case SID_ATTR_METRIC:
            {
                const SfxPoolItem* pItem;
                if ( pReqArgs && SfxItemState::SET == pReqArgs->GetItemState( nSlot, true, &pItem ) )
                {
                    FieldUnit eUnit = static_cast<FieldUnit>(static_cast<const SfxUInt16Item*>(pItem)->GetValue());
                    switch( eUnit )
                    {
                        case FieldUnit::MM:      // Just the units that are also in the dialog
                        case FieldUnit::CM:
                        case FieldUnit::INCH:
                        case FieldUnit::PICA:
                        case FieldUnit::POINT:
                            {
                                PutItem( *pItem );
                                ScAppOptions aNewOpts( GetAppOptions() );
                                aNewOpts.SetAppMetric( eUnit );
                                SetAppOptions( aNewOpts );
                                rReq.Done();
                            }
                            break;
                        default:
                        {
                            // added to avoid warnings
                        }
                    }
                }
            }
            break;

        case FID_AUTOCOMPLETE:
            {
                ScAppOptions aNewOpts( GetAppOptions() );
                bool bNew = !aNewOpts.GetAutoComplete();
                aNewOpts.SetAutoComplete( bNew );
                SetAppOptions( aNewOpts );
                if (pBindings)
                    pBindings->Invalidate( FID_AUTOCOMPLETE );
                rReq.Done();
            }
            break;

        case SID_DETECTIVE_AUTO:
            {
                ScAppOptions aNewOpts( GetAppOptions() );
                bool bNew = !aNewOpts.GetDetectiveAuto();
                const SfxBoolItem* pAuto = rReq.GetArg<SfxBoolItem>(SID_DETECTIVE_AUTO);
                if ( pAuto )
                    bNew = pAuto->GetValue();

                aNewOpts.SetDetectiveAuto( bNew );
                SetAppOptions( aNewOpts );
                if (pBindings)
                    pBindings->Invalidate( SID_DETECTIVE_AUTO );
                rReq.AppendItem( SfxBoolItem( SID_DETECTIVE_AUTO, bNew ) );
                rReq.Done();
            }
            break;

        case SID_PSZ_FUNCTION:
            if (pReqArgs)
            {
                const SfxUInt32Item & rItem = pReqArgs->Get(SID_PSZ_FUNCTION);

                ScAppOptions aNewOpts( GetAppOptions() );
                aNewOpts.SetStatusFunc( rItem.GetValue() );
                SetAppOptions( aNewOpts );

                if (pBindings)
                {
                    pBindings->Invalidate( SID_TABLE_CELL );
                    pBindings->Update( SID_TABLE_CELL ); // Immediately

                    pBindings->Invalidate( SID_PSZ_FUNCTION );
                    pBindings->Update( SID_PSZ_FUNCTION );
                    // If the menu is opened again immediately
                }
            }
            break;

        case SID_ATTR_LANGUAGE:
        case SID_ATTR_CHAR_CJK_LANGUAGE:
        case SID_ATTR_CHAR_CTL_LANGUAGE:
            {
                const SfxPoolItem* pItem;
                if ( pReqArgs && SfxItemState::SET == pReqArgs->GetItemState( GetPool().GetWhichIDFromSlotID(nSlot), true, &pItem ) )
                {
                    ScDocShell* pDocSh = dynamic_cast<ScDocShell*>( SfxObjectShell::Current() );
                    if ( pDocSh )
                    {
                        ScDocument& rDoc = pDocSh->GetDocument();
                        LanguageType eNewLang = static_cast<const SvxLanguageItem*>(pItem)->GetLanguage();
                        LanguageType eLatin, eCjk, eCtl;
                        rDoc.GetLanguage( eLatin, eCjk, eCtl );
                        LanguageType eOld = ( nSlot == SID_ATTR_CHAR_CJK_LANGUAGE ) ? eCjk :
                                            ( ( nSlot == SID_ATTR_CHAR_CTL_LANGUAGE ) ? eCtl : eLatin );
                        if ( eNewLang != eOld )
                        {
                            if ( nSlot == SID_ATTR_CHAR_CJK_LANGUAGE )
                                eCjk = eNewLang;
                            else if ( nSlot == SID_ATTR_CHAR_CTL_LANGUAGE )
                                eCtl = eNewLang;
                            else
                                eLatin = eNewLang;

                            rDoc.SetLanguage( eLatin, eCjk, eCtl );

                            ScInputHandler* pInputHandler = GetInputHdl();
                            if ( pInputHandler )
                                pInputHandler->UpdateSpellSettings(); // EditEngine flags
                            ScTabViewShell* pViewSh = dynamic_cast<ScTabViewShell*>( SfxViewShell::Current() );
                            if ( pViewSh )
                                pViewSh->UpdateDrawTextOutliner(); // EditEngine flags

                            pDocSh->SetDocumentModified();
                        }
                    }
                }
            }
            break;

        case FID_FOCUS_POSWND:
            {
                ScInputHandler* pHdl = GetInputHdl();
                if (pHdl)
                {
                    ScInputWindow* pWin = pHdl->GetInputWindow();
                    if (pWin)
                        pWin->PosGrabFocus();
                }
                rReq.Done();
            }
            break;

        case SID_OPEN_XML_FILTERSETTINGS:
        {
            try
            {
                css::uno::Reference < css::ui::dialogs::XExecutableDialog > xDialog = css::ui::dialogs::XSLTFilterDialog::create( ::comphelper::getProcessComponentContext());
                (void)xDialog->execute();
            }
            catch( css::uno::RuntimeException& )
            {
                DBG_UNHANDLED_EXCEPTION("sc.ui");
            }
        }
        break;

        default:
            OSL_FAIL( "ScApplication: Unknown Message." );
            break;
    }
}

void ScModule::GetState( SfxItemSet& rSet )
{
    ScDocShell* pDocSh = dynamic_cast<ScDocShell*>( SfxObjectShell::Current() );
    ScTabViewShell* pTabViewShell = pDocSh ? pDocSh->GetBestViewShell() : nullptr;

    SfxWhichIter aIter(rSet);
    for (sal_uInt16 nWhich = aIter.FirstWhich(); nWhich; nWhich = aIter.NextWhich())
    {
        if (!pTabViewShell)
        {
            // Not in the normal calc view shell (most likely in preview shell). Disable all actions.
            rSet.DisableItem(nWhich);
            continue;
        }

        switch ( nWhich )
        {
            case FID_AUTOCOMPLETE:
                rSet.Put( SfxBoolItem( nWhich, GetAppOptions().GetAutoComplete() ) );
                break;
            case SID_DETECTIVE_AUTO:
                rSet.Put( SfxBoolItem( nWhich, GetAppOptions().GetDetectiveAuto() ) );
                break;
            case SID_PSZ_FUNCTION:
                rSet.Put( SfxUInt32Item( nWhich, GetAppOptions().GetStatusFunc() ) );
                break;
            case SID_ATTR_METRIC:
                rSet.Put(SfxUInt16Item(nWhich, sal::static_int_cast<sal_uInt16>(GetMetric())));
                break;
            case SID_AUTOSPELL_CHECK:
                rSet.Put( SfxBoolItem( nWhich, pTabViewShell->IsAutoSpell()) );
                break;
            case SID_ATTR_LANGUAGE:
            case ATTR_CJK_FONT_LANGUAGE:        // WID for SID_ATTR_CHAR_CJK_LANGUAGE
            case ATTR_CTL_FONT_LANGUAGE:        // WID for SID_ATTR_CHAR_CTL_LANGUAGE
                {
                    LanguageType eLatin, eCjk, eCtl;
                    pDocSh->GetDocument().GetLanguage( eLatin, eCjk, eCtl );
                    LanguageType eLang = ( nWhich == ATTR_CJK_FONT_LANGUAGE ) ? eCjk :
                                        ( ( nWhich == ATTR_CTL_FONT_LANGUAGE ) ? eCtl : eLatin );
                    rSet.Put( SvxLanguageItem( eLang, nWhich ) );
                }
                break;
        }
    }
}

void ScModule::HideDisabledSlots( SfxItemSet& rSet )
{
    if( SfxViewFrame* pViewFrm = SfxViewFrame::Current() )
    {
        SfxBindings& rBindings = pViewFrm->GetBindings();
        SfxWhichIter aIter( rSet );
        for( sal_uInt16 nWhich = aIter.FirstWhich(); nWhich != 0; nWhich = aIter.NextWhich() )
        {
            ScViewUtil::HideDisabledSlot( rSet, rBindings, nWhich );
            // always disable the slots
            rSet.DisableItem( nWhich );
        }
    }
}

void ScModule::ResetDragObject()
{
    if (comphelper::LibreOfficeKit::isActive())
    {
        ScTabViewShell* pViewShell = ScTabViewShell::GetActiveViewShell();
        if (pViewShell)
            pViewShell->ResetDragObject();
    }
    else
    {
        m_pDragData->pCellTransfer = nullptr;
        m_pDragData->pDrawTransfer = nullptr;
        m_pDragData->pJumpLocalDoc = nullptr;
        m_pDragData->aLinkDoc.clear();
        m_pDragData->aLinkTable.clear();
        m_pDragData->aLinkArea.clear();
        m_pDragData->aJumpTarget.clear();
        m_pDragData->aJumpText.clear();
    }
}

const ScDragData* ScModule::GetDragData() const
{
    if (comphelper::LibreOfficeKit::isActive())
    {
        ScTabViewShell* pViewShell = ScTabViewShell::GetActiveViewShell();
        return pViewShell ? &pViewShell->GetDragData() : nullptr;
    }

    return m_pDragData.get();
}

void ScModule::SetDragObject( ScTransferObj* pCellObj, ScDrawTransferObj* pDrawObj )
{
    if (comphelper::LibreOfficeKit::isActive())
    {
        ScTabViewShell* pViewShell = ScTabViewShell::GetActiveViewShell();
        if (pViewShell)
            pViewShell->SetDragObject(pCellObj, pDrawObj);
    }
    else
    {
        ResetDragObject();
        m_pDragData->pCellTransfer = pCellObj;
        m_pDragData->pDrawTransfer = pDrawObj;
    }
}

void ScModule::SetDragLink(
    const OUString& rDoc, const OUString& rTab, const OUString& rArea )
{
    if (comphelper::LibreOfficeKit::isActive())
    {
        ScTabViewShell* pViewShell = ScTabViewShell::GetActiveViewShell();
        if (pViewShell)
            pViewShell->SetDragLink(rDoc, rTab, rArea);
    }
    else
    {
        ResetDragObject();
        m_pDragData->aLinkDoc   = rDoc;
        m_pDragData->aLinkTable = rTab;
        m_pDragData->aLinkArea  = rArea;
    }
}

void ScModule::SetDragJump(
    ScDocument* pLocalDoc, const OUString& rTarget, const OUString& rText )
{
    if (comphelper::LibreOfficeKit::isActive())
    {
        ScTabViewShell* pViewShell = ScTabViewShell::GetActiveViewShell();
        if (pViewShell)
            pViewShell->SetDragJump(pLocalDoc, rTarget, rText);
    }
    else
    {
        ResetDragObject();

        m_pDragData->pJumpLocalDoc = pLocalDoc;
        m_pDragData->aJumpTarget = rTarget;
        m_pDragData->aJumpText = rText;
    }
}

ScDocument* ScModule::GetClipDoc()
{
    // called from document
    SfxViewFrame* pViewFrame = nullptr;
    ScTabViewShell* pViewShell = nullptr;
    css::uno::Reference<css::datatransfer::XTransferable2> xTransferable;

    if ((pViewShell = dynamic_cast<ScTabViewShell*>(SfxViewShell::Current())))
        xTransferable.set(ScTabViewShell::GetClipData(pViewShell->GetViewData().GetActiveWin()));
    else if ((pViewShell = dynamic_cast<ScTabViewShell*>(SfxViewShell::GetFirst())))
        xTransferable.set(ScTabViewShell::GetClipData(pViewShell->GetViewData().GetActiveWin()));
    else if ((pViewFrame = SfxViewFrame::GetFirst()))
    {
        css::uno::Reference<css::datatransfer::clipboard::XClipboard> xClipboard =
            pViewFrame->GetWindow().GetClipboard();
        xTransferable.set(xClipboard.is() ? xClipboard->getContents() : nullptr, css::uno::UNO_QUERY);
    }

    const ScTransferObj* pObj = ScTransferObj::GetOwnClipboard(xTransferable);
    if (pObj)
    {
        ScDocument* pDoc = pObj->GetDocument();
        assert((!pDoc || pDoc->IsClipboard()) && "Document is not clipboard, how can that be?");
        return pDoc;
    }

    return nullptr;
}

void ScModule::SetSelectionTransfer( ScSelectionTransferObj* pNew )
{
    m_pSelTransfer = pNew;
}

void ScModule::SetViewOptions( const ScViewOptions& rOpt )
{
    if ( !m_pViewCfg )
        m_pViewCfg.reset(new ScViewCfg);

    m_pViewCfg->SetOptions( rOpt );
}

const ScViewOptions& ScModule::GetViewOptions()
{
    if ( !m_pViewCfg )
        m_pViewCfg.reset( new ScViewCfg );

    return *m_pViewCfg;
}

void ScModule::SetDocOptions( const ScDocOptions& rOpt )
{
    if ( !m_pDocCfg )
        m_pDocCfg.reset( new ScDocCfg );

    m_pDocCfg->SetOptions( rOpt );
}

const ScDocOptions& ScModule::GetDocOptions()
{
    if ( !m_pDocCfg )
        m_pDocCfg.reset( new ScDocCfg );

    return *m_pDocCfg;
}

void ScModule::InsertEntryToLRUList(sal_uInt16 nFIndex)
{
    if(nFIndex == 0)
        return;

    const ScAppOptions& rAppOpt = GetAppOptions();
    sal_uInt16 nLRUFuncCount = std::min( rAppOpt.GetLRUFuncListCount(), sal_uInt16(LRU_MAX) );
    sal_uInt16* pLRUListIds = rAppOpt.GetLRUFuncList();

    sal_uInt16  aIdxList[LRU_MAX];
    sal_uInt16  n = 0;
    bool    bFound = false;

    while ((n < LRU_MAX) && n<nLRUFuncCount)                        // Iterate through old list
    {
        if (!bFound && (pLRUListIds[n]== nFIndex))
            bFound = true;                                          // First hit!
        else if (bFound)
            aIdxList[n  ] = pLRUListIds[n];                         // Copy after hit
        else if ((n+1) < LRU_MAX)
            aIdxList[n+1] = pLRUListIds[n];                         // Move before hit
        n++;
    }
    if (!bFound && (n < LRU_MAX))                                   // Entry not found?
        n++;                                                        // One more
    aIdxList[0] = nFIndex;                                          // Current on Top

    ScAppOptions aNewOpts(rAppOpt);                                 // Let App know
    aNewOpts.SetLRUFuncList(aIdxList, n);
    SetAppOptions(aNewOpts);
}

void ScModule::InsertOrEraseFavouritesListEntry(sal_uInt16 nFIndex, bool bInsert)
{
    const ScAppOptions& rAppOpt = GetAppOptions();
    std::unordered_set<sal_uInt16> sFavouriteFunctions = rAppOpt.GetFavouritesList();

    if (bInsert)
        sFavouriteFunctions.insert(nFIndex);
    else
        sFavouriteFunctions.erase(nFIndex);

    ScAppOptions aNewOpts(rAppOpt);
    aNewOpts.SetFavouritesList(sFavouriteFunctions);
    SetAppOptions(aNewOpts);
}

void ScModule::SetAppOptions( const ScAppOptions& rOpt )
{
    if ( !m_pAppCfg )
        m_pAppCfg.reset( new ScAppCfg );

    m_pAppCfg->SetOptions( rOpt );
}

void global_InitAppOptions()
{
    ScModule::get()->GetAppOptions();
}

const ScAppOptions& ScModule::GetAppOptions()
{
    if ( !m_pAppCfg )
        m_pAppCfg.reset( new ScAppCfg );

    return m_pAppCfg->GetOptions();
}

void ScModule::SetDefaultsOptions( const ScDefaultsOptions& rOpt )
{
    if ( !m_pDefaultsCfg )
        m_pDefaultsCfg.reset( new ScDefaultsCfg );

    m_pDefaultsCfg->SetOptions( rOpt );
}

const ScDefaultsOptions& ScModule::GetDefaultsOptions()
{
    if ( !m_pDefaultsCfg )
        m_pDefaultsCfg.reset( new ScDefaultsCfg );

    return *m_pDefaultsCfg;
}

void ScModule::SetFormulaOptions( const ScFormulaOptions& rOpt )
{
    if ( !m_pFormulaCfg )
        m_pFormulaCfg.reset( new ScFormulaCfg );

    m_pFormulaCfg->SetOptions( rOpt );
}

const ScFormulaOptions& ScModule::GetFormulaOptions()
{
    if ( !m_pFormulaCfg )
        m_pFormulaCfg.reset( new ScFormulaCfg );

    return *m_pFormulaCfg;
}

void ScModule::SetInputOptions( const ScInputOptions& rOpt )
{
    if ( !m_pInputCfg )
        m_pInputCfg.reset( new ScInputCfg );

    m_pInputCfg->SetOptions( rOpt );
}

const ScInputOptions& ScModule::GetInputOptions()
{
    if ( !m_pInputCfg )
        m_pInputCfg.reset( new ScInputCfg );

    return m_pInputCfg->GetOptions();
}

void ScModule::SetPrintOptions( const ScPrintOptions& rOpt )
{
    if ( !m_pPrintCfg )
        m_pPrintCfg.reset( new ScPrintCfg );

    m_pPrintCfg->SetOptions( rOpt );
}

const ScPrintOptions& ScModule::GetPrintOptions()
{
    if ( !m_pPrintCfg )
        m_pPrintCfg.reset( new ScPrintCfg );

    return m_pPrintCfg->GetOptions();
}

ScNavipiCfg& ScModule::GetNavipiCfg()
{
    if ( !m_pNavipiCfg )
        m_pNavipiCfg.reset( new ScNavipiCfg );

    return *m_pNavipiCfg;
}

ScAddInCfg& ScModule::GetAddInCfg()
{
    if ( !m_pAddInCfg )
        m_pAddInCfg.reset( new ScAddInCfg );

    return *m_pAddInCfg;
}

svtools::ColorConfig& ScModule::GetColorConfig()
{
    if ( !m_pColorConfig )
    {
        m_pColorConfig.reset( new svtools::ColorConfig );
        m_pColorConfig->AddListener(this);
    }

    return *m_pColorConfig;
}

bool ScModule::IsLOKViewInDarkMode()
{
    SfxViewShell* pKitSh = comphelper::LibreOfficeKit::isActive() ? SfxViewShell::Current() : nullptr;
    if( pKitSh )
    {
        Color aDocColor = pKitSh->GetColorConfigColor(svtools::DOCCOLOR);
        if( aDocColor.IsDark() )
            return true;
    }
    return false;
}

SvtUserOptions&  ScModule::GetUserOptions()
{
    if( !m_pUserOptions )
    {
        m_pUserOptions.reset( new SvtUserOptions );
    }
    return *m_pUserOptions;
}

FieldUnit ScModule::GetMetric()
{
    if (comphelper::LibreOfficeKit::isActive())
        return SfxModule::GetFieldUnit();
    return GetAppOptions().GetAppMetric();
}

LanguageType ScModule::GetOptDigitLanguage()
{
    SvtCTLOptions::TextNumerals eNumerals = SvtCTLOptions::GetCTLTextNumerals();
    return ( eNumerals == SvtCTLOptions::NUMERALS_ARABIC ) ? LANGUAGE_ENGLISH_US :
           ( eNumerals == SvtCTLOptions::NUMERALS_HINDI)   ? LANGUAGE_ARABIC_SAUDI_ARABIA :
                                                             LANGUAGE_SYSTEM;
}

/**
 * Options
 *
 * Items from Calc options dialog and SID_AUTOSPELL_CHECK
 */
void ScModule::ModifyOptions( const SfxItemSet& rOptSet )
{
    bool bOldAutoSpell = GetAutoSpellProperty();
    LanguageType nOldSpellLang, nOldCjkLang, nOldCtlLang;
    GetSpellSettings( nOldSpellLang, nOldCjkLang, nOldCtlLang );

    if (!m_pAppCfg)
        GetAppOptions();
    OSL_ENSURE( m_pAppCfg, "AppOptions not initialised :-(" );

    if (!m_pInputCfg)
        GetInputOptions();
    OSL_ENSURE( m_pInputCfg, "InputOptions not initialised :-(" );

    SfxViewFrame* pViewFrm = SfxViewFrame::Current();
    SfxBindings* pBindings = pViewFrm ? &pViewFrm->GetBindings() : nullptr;

    ScTabViewShell*         pViewSh = dynamic_cast<ScTabViewShell*>( SfxViewShell::Current() );
    ScDocShell*             pDocSh  = dynamic_cast<ScDocShell*>( SfxObjectShell::Current() );
    ScDocument*             pDoc    = pDocSh ? &pDocSh->GetDocument() : nullptr;
    bool bRepaint = false;
    bool bUpdateMarks = false;
    bool bUpdateRefDev = false;
    bool bCalcAll = false;
    bool bSaveAppOptions = false;
    bool bSaveInputOptions = false;
    bool bCompileErrorCells = false;

    //  SfxGetpApp()->SetOptions( rOptSet );

    ScAppOptions aAppOptions = m_pAppCfg->GetOptions();

    // No more linguistics
    if (const SfxUInt16Item* pItem = rOptSet.GetItemIfSet(SID_ATTR_METRIC))
    {
        PutItem( *pItem );
        aAppOptions.SetAppMetric( static_cast<FieldUnit>(pItem->GetValue()) );
        bSaveAppOptions = true;
    }

    if (const ScUserListItem* pItem = rOptSet.GetItemIfSet(SCITEM_USERLIST))
    {
        ScGlobal::SetUserList( pItem->GetUserList() );
        bSaveAppOptions = true;
    }

    if (const SfxBoolItem* pItem = rOptSet.GetItemIfSet(SID_SC_OPT_SYNCZOOM))
    {
        aAppOptions.SetSynchronizeZoom( pItem->GetValue() );
        bSaveAppOptions = true;
    }

    if (const SfxUInt16Item* pItem = rOptSet.GetItemIfSet(SID_SC_OPT_KEY_BINDING_COMPAT))
    {
        sal_uInt16 nVal = pItem->GetValue();
        ScOptionsUtil::KeyBindingType eOld = aAppOptions.GetKeyBindingType();
        ScOptionsUtil::KeyBindingType eNew = static_cast<ScOptionsUtil::KeyBindingType>(nVal);
        if (eOld != eNew)
        {
            aAppOptions.SetKeyBindingType(eNew);
            bSaveAppOptions = true;
            ScDocShell::ResetKeyBindings(eNew);
        }
    }

    if (const SfxBoolItem* pItem = rOptSet.GetItemIfSet(SID_SC_OPT_LINKS))
    {
        aAppOptions.SetLinksInsertedLikeMSExcel(pItem->GetValue());
        bSaveAppOptions = true;
    }

    // DefaultsOptions
    if (const ScTpDefaultsItem* pItem = rOptSet.GetItemIfSet(SID_SCDEFAULTSOPTIONS))
    {
        const ScDefaultsOptions& rOpt = pItem->GetDefaultsOptions();
        SetDefaultsOptions( rOpt );
    }

    // FormulaOptions
    if (const ScTpFormulaItem* pItem = rOptSet.GetItemIfSet(SID_SCFORMULAOPTIONS))
    {
        const ScFormulaOptions& rOpt = pItem->GetFormulaOptions();

        if (!m_pFormulaCfg || (*m_pFormulaCfg != rOpt))
            // Formula options have changed. Repaint the column headers.
            bRepaint = true;

        if (m_pFormulaCfg && m_pFormulaCfg->GetUseEnglishFuncName() != rOpt.GetUseEnglishFuncName())
        {
            // Re-compile formula cells with error as the error may have been
            // caused by unresolved function names.
            bCompileErrorCells = true;
        }

        // Recalc for interpreter options changes.
        if (m_pFormulaCfg && m_pFormulaCfg->GetCalcConfig() != rOpt.GetCalcConfig())
            bCalcAll = true;

        if ( pDocSh )
        {
            pDocSh->SetFormulaOptions( rOpt );
            pDocSh->SetDocumentModified();
        }

        // ScDocShell::SetFormulaOptions() may check for changed settings, so
        // set the new options here after that has been called.
        if (!bCalcAll || rOpt.GetWriteCalcConfig())
        {
            // CalcConfig is new, didn't change or is global, simply set all.
            SetFormulaOptions( rOpt );
        }
        else
        {
            // If "only for current document" was checked, reset those affected
            // by that setting to previous values.
            ScFormulaOptions aNewOpt( rOpt);
            aNewOpt.GetCalcConfig().MergeDocumentSpecific( m_pFormulaCfg->GetCalcConfig());
            SetFormulaOptions( aNewOpt);
        }
    }

    // ViewOptions
    if (const ScTpViewItem* pItem = rOptSet.GetItemIfSet(SID_SCVIEWOPTIONS))
    {
        const ScViewOptions& rNewOpt = pItem->GetViewOptions();

        if ( pViewSh )
        {
            ScViewData&             rViewData = pViewSh->GetViewData();
            const ScViewOptions&    rOldOpt   = rViewData.GetOptions();

            bool bAnchorList = rOldOpt.GetOption(sc::ViewOption::ANCHOR) !=
                               rNewOpt.GetOption(sc::ViewOption::ANCHOR);

            if ( rOldOpt != rNewOpt )
            {
                rViewData.SetOptions( rNewOpt ); // Changes rOldOpt
                rViewData.GetDocument().SetViewOptions( rNewOpt );
                if (pDocSh)
                    pDocSh->SetDocumentModified();
                bRepaint = true;
            }
            if ( bAnchorList )
                pViewSh->UpdateAnchorHandles();
        }
        SetViewOptions( rNewOpt );
        if (pBindings)
        {
            pBindings->Invalidate(SID_HELPLINES_MOVE);
        }
    }

    // GridOptions
    // Evaluate after ViewOptions, as GridOptions is a member of ViewOptions
    if ( const SvxGridItem* pItem = rOptSet.GetItemIfSet(SID_ATTR_GRID_OPTIONS) )
    {
        ScGridOptions aNewGridOpt( *pItem );

        if ( pViewSh )
        {
            ScViewData&          rViewData = pViewSh->GetViewData();
            ScViewOptions        aNewViewOpt( rViewData.GetOptions() );
            const ScGridOptions& rOldGridOpt = aNewViewOpt.GetGridOptions();

            if ( rOldGridOpt != aNewGridOpt )
            {
                aNewViewOpt.SetGridOptions( aNewGridOpt );
                rViewData.SetOptions( aNewViewOpt );
                rViewData.GetDocument().SetViewOptions( aNewViewOpt );
                if (pDocSh)
                    pDocSh->SetDocumentModified();
                bRepaint = true;
            }
        }
        ScViewOptions aNewViewOpt ( GetViewOptions() );
        aNewViewOpt.SetGridOptions( aNewGridOpt );
        SetViewOptions( aNewViewOpt );
        if (pBindings)
        {
            pBindings->Invalidate(SID_GRID_VISIBLE);
            pBindings->Invalidate(SID_GRID_USE);
        }
    }

    // DocOptions
    if ( const ScTpCalcItem* pItem = rOptSet.GetItemIfSet(SID_SCDOCOPTIONS) )
    {
        const ScDocOptions& rNewOpt = pItem->GetDocOptions();

        if ( pDoc )
        {
            const ScDocOptions& rOldOpt = pDoc->GetDocOptions();

            bRepaint = ( bRepaint || ( rOldOpt != rNewOpt )   );
            bCalcAll =   bRepaint &&
                         (  rOldOpt.IsIter()       != rNewOpt.IsIter()
                         || rOldOpt.GetIterCount() != rNewOpt.GetIterCount()
                         || rOldOpt.GetIterEps()   != rNewOpt.GetIterEps()
                         || rOldOpt.IsIgnoreCase() != rNewOpt.IsIgnoreCase()
                         || rOldOpt.IsCalcAsShown() != rNewOpt.IsCalcAsShown()
                         || (rNewOpt.IsCalcAsShown() &&
                            rOldOpt.GetStdPrecision() != rNewOpt.GetStdPrecision())
                         || rOldOpt.IsMatchWholeCell() != rNewOpt.IsMatchWholeCell()
                         || rOldOpt.GetYear2000()   != rNewOpt.GetYear2000()
                         || rOldOpt.IsFormulaRegexEnabled() != rNewOpt.IsFormulaRegexEnabled()
                         || rOldOpt.IsFormulaWildcardsEnabled() != rNewOpt.IsFormulaWildcardsEnabled()
                         );
            pDoc->SetDocOptions( rNewOpt );
            pDocSh->SetDocumentModified();
        }
        SetDocOptions( rNewOpt );
    }

    // Set TabDistance after the actual DocOptions
    if ( const SfxUInt16Item* pItem = rOptSet.GetItemIfSet(SID_ATTR_DEFTABSTOP) )
    {
        sal_uInt16 nTabDist = pItem->GetValue();
        ScDocOptions aOpt(GetDocOptions());
        aOpt.SetTabDistance(nTabDist);
        SetDocOptions( aOpt );

        if ( pDoc )
        {
            ScDocOptions aDocOpt(pDoc->GetDocOptions());
            aDocOpt.SetTabDistance(nTabDist);
            pDoc->SetDocOptions( aDocOpt );
            pDocSh->SetDocumentModified();
            if(pDoc->GetDrawLayer())
                pDoc->GetDrawLayer()->SetDefaultTabulator(nTabDist);
        }
    }

    // AutoSpell after the DocOptions (due to being a member)
    if ( const SfxBoolItem* pItem = rOptSet.GetItemIfSet(SID_AUTOSPELL_CHECK) ) // At DocOptions
    {
        bool bDoAutoSpell = pItem->GetValue();

        if (pViewSh)
        {
            if (pViewSh->IsAutoSpell() != bDoAutoSpell)
            {
                pViewSh->EnableAutoSpell(bDoAutoSpell);

                bRepaint = true;            // Because HideAutoSpell might be invalid
                                            //TODO: Paint all Views?
            }
        }

        if ( bOldAutoSpell != bDoAutoSpell )
            SetAutoSpellProperty( bDoAutoSpell );
        if ( pDocSh )
            pDocSh->PostPaintGridAll();                     // Due to marks
        ScInputHandler* pInputHandler = GetInputHdl();
        if ( pInputHandler )
            pInputHandler->UpdateSpellSettings();           // EditEngine flags
        if ( pViewSh )
            pViewSh->UpdateDrawTextOutliner();              // EditEngine flags

        if (pBindings)
            pBindings->Invalidate( SID_AUTOSPELL_CHECK );
    }

    // InputOptions
    ScInputOptions aInputOptions = m_pInputCfg->GetOptions();
    if ( const SfxUInt16Item* pItem = rOptSet.GetItemIfSet(SID_SC_INPUT_SELECTIONPOS) )
    {
        aInputOptions.SetMoveDir( pItem->GetValue() );
        bSaveInputOptions = true;
    }
    if ( const SfxBoolItem* pItem = rOptSet.GetItemIfSet(SID_SC_INPUT_SELECTION) )
    {
        aInputOptions.SetMoveSelection( pItem->GetValue() );
        bSaveInputOptions = true;
    }
    if ( const SfxBoolItem* pItem = rOptSet.GetItemIfSet(SID_SC_INPUT_EDITMODE) )
    {
        aInputOptions.SetEnterEdit( pItem->GetValue() );
        bSaveInputOptions = true;
    }
    if ( const SfxBoolItem* pItem = rOptSet.GetItemIfSet(SID_SC_INPUT_FMT_EXPAND) )
    {
        aInputOptions.SetExtendFormat( pItem->GetValue() );
        bSaveInputOptions = true;
    }
    if ( const SfxBoolItem* pItem = rOptSet.GetItemIfSet(SID_SC_INPUT_RANGEFINDER) )
    {
        aInputOptions.SetRangeFinder( pItem->GetValue() );
        bSaveInputOptions = true;
    }
    if ( const SfxBoolItem* pItem = rOptSet.GetItemIfSet(SID_SC_INPUT_REF_EXPAND) )
    {
        aInputOptions.SetExpandRefs( pItem->GetValue() );
        bSaveInputOptions = true;
    }
    if (const SfxBoolItem* pItem = rOptSet.GetItemIfSet(SID_SC_OPT_SORT_REF_UPDATE))
    {
        aInputOptions.SetSortRefUpdate( pItem->GetValue());
        bSaveInputOptions = true;
    }

    if ( const SfxBoolItem* pItem = rOptSet.GetItemIfSet(SID_SC_INPUT_MARK_HEADER) )
    {
        aInputOptions.SetMarkHeader( pItem->GetValue() );
        bSaveInputOptions = true;
        bUpdateMarks = true;
    }
    if ( const SfxBoolItem* pItem = rOptSet.GetItemIfSet(SID_SC_INPUT_TEXTWYSIWYG) )
    {
        bool bNew = pItem->GetValue();
        if ( bNew != aInputOptions.GetTextWysiwyg() )
        {
            aInputOptions.SetTextWysiwyg( bNew );
            bSaveInputOptions = true;
            bUpdateRefDev = true;
        }
    }
    if( const SfxBoolItem* pItem = rOptSet.GetItemIfSet( SID_SC_INPUT_REPLCELLSWARN ) )
    {
        aInputOptions.SetReplaceCellsWarn( pItem->GetValue() );
        bSaveInputOptions = true;
    }

    if( const SfxBoolItem* pItem = rOptSet.GetItemIfSet( SID_SC_INPUT_LEGACY_CELL_SELECTION ) )
    {
        aInputOptions.SetLegacyCellSelection( pItem->GetValue() );
        bSaveInputOptions = true;
    }

    if( const SfxBoolItem* pItem = rOptSet.GetItemIfSet( SID_SC_INPUT_ENTER_PASTE_MODE ) )
    {
        aInputOptions.SetEnterPasteMode( pItem->GetValue() );
        bSaveInputOptions = true;
    }

    if( const SfxBoolItem* pItem = rOptSet.GetItemIfSet( SID_SC_INPUT_WARNACTIVESHEET ) )
    {
        aInputOptions.SetWarnActiveSheet( pItem->GetValue() );
        bSaveInputOptions = true;
    }

    // PrintOptions
    if ( const ScTpPrintItem* pItem = rOptSet.GetItemIfSet(SID_SCPRINTOPTIONS) )
    {
        const ScPrintOptions& rNewOpt = pItem->GetPrintOptions();
        SetPrintOptions( rNewOpt );

        // broadcast causes all previews to recalc page numbers
        SfxGetpApp()->Broadcast( SfxHint( SfxHintId::ScPrintOptions ) );
    }

    if ( bSaveAppOptions )
        m_pAppCfg->SetOptions(aAppOptions);

    if ( bSaveInputOptions )
        m_pInputCfg->SetOptions(aInputOptions);

    // Kick off recalculation?
    if (pDoc && bCompileErrorCells)
    {
        // Re-compile cells with name error, and recalc if at least one cell
        // has been re-compiled.  In the future we may want to find a way to
        // recalc only those that are affected.
        if (pDoc->CompileErrorCells(FormulaError::NoName))
            bCalcAll = true;
    }

    if ( pDoc && bCalcAll )
    {
        weld::WaitObject aWait( ScDocShell::GetActiveDialogParent() );
        pDoc->CalcAll();
        if ( pViewSh )
            pViewSh->UpdateCharts( true );
        else
            ScDBFunc::DoUpdateCharts( ScAddress(), *pDoc, true );
        if (pBindings)
            pBindings->Invalidate( SID_ATTR_SIZE ); //SvxPosSize StatusControl Update
    }

    if ( pViewSh && bUpdateMarks )
        pViewSh->UpdateAutoFillMark();

    // Repaint View?
    if ( pViewSh && bRepaint )
    {
        pViewSh->UpdateFixPos();
        pViewSh->PaintGrid();
        pViewSh->PaintTop();
        pViewSh->PaintLeft();
        pViewSh->PaintExtras();
        pViewSh->InvalidateBorder();
        if (pBindings)
        {
            pBindings->Invalidate( FID_TOGGLEHEADERS ); // -> Checks in menu
            pBindings->Invalidate( FID_TOGGLESYNTAX );
        }
    }

    // update ref device (for all documents)
    if ( !bUpdateRefDev )
        return;

    // for all documents: recalc output factor, update row heights
    SfxObjectShell* pObjSh = SfxObjectShell::GetFirst();
    while ( pObjSh )
    {
        if ( auto pOneDocSh = dynamic_cast<ScDocShell *>(pObjSh) )
        {
            pOneDocSh->CalcOutputFactor();
            SCTAB nTabCount = pOneDocSh->GetDocument().GetTableCount();
            for (SCTAB nTab=0; nTab<nTabCount; nTab++)
                pOneDocSh->AdjustRowHeight( 0, pDocSh->GetDocument().MaxRow(), nTab );
        }
        pObjSh = SfxObjectShell::GetNext( *pObjSh );
    }

    // for all (tab-) views:
    SfxViewShell* pSh = SfxViewShell::GetFirst( true, checkSfxViewShell<ScTabViewShell> );
    while ( pSh )
    {
        ScTabViewShell* pOneViewSh = static_cast<ScTabViewShell*>(pSh);

        // set ref-device for EditEngine
        ScInputHandler* pHdl = GetInputHdl(pOneViewSh);
        if (pHdl)
            pHdl->UpdateRefDevice();

        // update view scale
        ScViewData& rViewData = pOneViewSh->GetViewData();
        pOneViewSh->SetZoom( rViewData.GetZoomX(), rViewData.GetZoomY(), false );

        // repaint
        pOneViewSh->PaintGrid();
        pOneViewSh->PaintTop();
        pOneViewSh->PaintLeft();

        pSh = SfxViewShell::GetNext( *pSh, true, checkSfxViewShell<ScTabViewShell> );
    }
}

/**
 * Input-Handler
 */
ScInputHandler* ScModule::GetInputHdl( ScTabViewShell* pViewSh, bool bUseRef )
{
    if ( !comphelper::LibreOfficeKit::isActive() && m_pRefInputHandler && bUseRef )
        return m_pRefInputHandler;

    ScInputHandler* pHdl = nullptr;
    if ( !pViewSh )
    {
        // in case a UIActive embedded object has no ViewShell (UNO component)
        // the own calc view shell will be set as current, but no handling should happen
        ScTabViewShell* pCurViewSh = dynamic_cast<ScTabViewShell*>( SfxViewShell::Current()  );
        if ( pCurViewSh && !pCurViewSh->GetUIActiveClient() )
            pViewSh = pCurViewSh;
    }

    if ( pViewSh )
        pHdl = pViewSh->GetInputHandler(); // Viewshell always has one, from now on

    // If no ViewShell passed or active, we can get NULL
    OSL_ENSURE( pHdl || !pViewSh, "GetInputHdl: no InputHandler found!" );
    return pHdl;
}

void ScModule::ViewShellChanged(bool bStopEditing /*=true*/)
{
    ScInputHandler* pHdl   = GetInputHdl();
    ScTabViewShell* pShell = ScTabViewShell::GetActiveViewShell();
    if ( pShell && pHdl )
        pShell->UpdateInputHandler(false, bStopEditing);
}

void ScModule::SetInputMode( ScInputMode eMode, const OUString* pInitText )
{
    ScInputHandler* pHdl = GetInputHdl();
    if (pHdl)
        pHdl->SetMode(eMode, pInitText);
}

bool ScModule::IsEditMode()
{
    ScInputHandler* pHdl = GetInputHdl();
    return pHdl && pHdl->IsEditMode();
}

bool ScModule::IsInputMode()
{
    ScInputHandler* pHdl = GetInputHdl();
    return pHdl && pHdl->IsInputMode();
}

bool ScModule::InputKeyEvent( const KeyEvent& rKEvt, bool bStartEdit )
{
    ScInputHandler* pHdl = GetInputHdl();
    return pHdl && pHdl->KeyInput( rKEvt, bStartEdit );
}

void ScModule::InputEnterHandler( ScEnterMode nBlockMode, bool bBeforeSavingInLOK )
{
    if ( !SfxGetpApp()->IsDowning() ) // Not when quitting the program
    {
        ScInputHandler* pHdl = GetInputHdl();
        if (pHdl)
            pHdl->EnterHandler( nBlockMode, bBeforeSavingInLOK );
    }
}

void ScModule::InputCancelHandler()
{
    ScInputHandler* pHdl = GetInputHdl();
    if (pHdl)
        pHdl->CancelHandler();
}

void ScModule::InputSelection( const EditView* pView )
{
    ScInputHandler* pHdl = GetInputHdl();
    if (pHdl)
        pHdl->InputSelection( pView );
}

void ScModule::InputChanged( const EditView* pView )
{
    ScInputHandler* pHdl = GetInputHdl();
    if (pHdl)
        pHdl->InputChanged( pView, false );
}

void ScModule::ViewShellGone( const ScTabViewShell* pViewSh )
{
    ScInputHandler* pHdl = GetInputHdl();
    if (pHdl)
        pHdl->ViewShellGone( pViewSh );
}

void ScModule::SetRefInputHdl( ScInputHandler* pNew )
{
    m_pRefInputHandler = pNew;
}

void ScModule::InputGetSelection( sal_Int32& rStart, sal_Int32& rEnd )
{
    ScInputHandler* pHdl = GetInputHdl();
    if (pHdl)
        pHdl->InputGetSelection( rStart, rEnd );
}

void ScModule::InputSetSelection( sal_Int32 nStart, sal_Int32 nEnd )
{
    ScInputHandler* pHdl = GetInputHdl();
    if (pHdl)
        pHdl->InputSetSelection( nStart, nEnd );
}

void ScModule::InputReplaceSelection( std::u16string_view aStr )
{
    ScInputHandler* pHdl = GetInputHdl();
    if (pHdl)
        pHdl->InputReplaceSelection( aStr );
}

void ScModule::InputTurnOffWinEngine()
{
    ScInputHandler* pHdl = GetInputHdl();
    if (pHdl)
        pHdl->InputTurnOffWinEngine();
}

void ScModule::ActivateInputWindow( const OUString* pStrFormula, bool bMatrix )
{
    ScInputHandler* pHdl = GetInputHdl();
    if ( !pHdl )
        return;

    ScInputWindow* pWin = pHdl->GetInputWindow();
    if ( pStrFormula )
    {
        // Take over formula
        if ( pWin )
        {
            pWin->SetFuncString( *pStrFormula, false );
            // SetSumAssignMode due to sal_False not necessary
        }
        ScEnterMode nMode = bMatrix ? ScEnterMode::MATRIX : ScEnterMode::NORMAL;
        pHdl->EnterHandler( nMode );

        // Without Invalidate the selection remains active, if the formula has not changed
        if (pWin)
            pWin->TextInvalidate();
    }
    else
    {
        // Cancel
        if ( pWin )
        {
            pWin->SetFuncString( OUString(), false );
            // SetSumAssignMode due to sal_False no necessary
        }
        pHdl->CancelHandler();
    }
}

/**
 * Reference dialogs
 */
void ScModule::SetRefDialog( sal_uInt16 nId, bool bVis, SfxViewFrame* pViewFrm )
{
    //TODO: Move reference dialog handling to view
    //      Just keep function autopilot here for references to other documents
    if ( !(m_nCurRefDlgId == 0 || ( nId == m_nCurRefDlgId && !bVis )
       || ( comphelper::LibreOfficeKit::isActive() )) )
        return;

    if ( !pViewFrm )
        pViewFrm = SfxViewFrame::Current();

    // bindings update causes problems with update of stylist if
    // current style family has changed
    //if ( pViewFrm )
    //  pViewFrm->GetBindings().Update();       // to avoid trouble in LockDispatcher

    // before SetChildWindow
    if ( comphelper::LibreOfficeKit::isActive() )
    {
        if ( bVis )
            m_nCurRefDlgId = nId;
    }
    else
    {
        m_nCurRefDlgId = bVis ? nId : 0;
    }

    if ( pViewFrm )
    {
        //  store the dialog id also in the view shell
        SfxViewShell* pViewSh = pViewFrm->GetViewShell();
        if (ScTabViewShell* pTabViewSh = dynamic_cast<ScTabViewShell*>(pViewSh))
            pTabViewSh->SetCurRefDlgId(m_nCurRefDlgId);
        else
        {
            // no ScTabViewShell - possible for example from a Basic macro
            bVis = false;
            m_nCurRefDlgId = 0;   // don't set nCurRefDlgId if no dialog is created
        }

        pViewFrm->SetChildWindow( nId, bVis );
    }

    SfxApplication* pSfxApp = SfxGetpApp();
    pSfxApp->Broadcast( SfxHint( SfxHintId::ScRefModeChanged ) );
}

static SfxChildWindow* lcl_GetChildWinFromCurrentView( sal_uInt16 nId )
{
    SfxViewFrame* pViewFrm = SfxViewFrame::Current();

    // #i46999# current view frame can be null (for example, when closing help)
    return pViewFrm ? pViewFrm->GetChildWindow( nId ) : nullptr;
}

static SfxChildWindow* lcl_GetChildWinFromAnyView( sal_uInt16 nId )
{
    // First, try the current view
    SfxChildWindow* pChildWnd = lcl_GetChildWinFromCurrentView( nId );
    if ( pChildWnd )
        return pChildWnd;           // found in the current view

    //  if not found there, get the child window from any open view
    //  it can be open only in one view because nCurRefDlgId is global

    SfxViewFrame* pViewFrm = SfxViewFrame::GetFirst();
    while ( pViewFrm )
    {
        pChildWnd = pViewFrm->GetChildWindow( nId );
        if ( pChildWnd )
            return pChildWnd;       // found in any view

        pViewFrm = SfxViewFrame::GetNext( *pViewFrm );
    }

    return nullptr;                    // none found
}

bool ScModule::IsModalMode(SfxObjectShell* pDocSh)
{
    //TODO: Move reference dialog handling to view
    //      Just keep function autopilot here for references to other documents
    bool bIsModal = false;

    if ( m_nCurRefDlgId )
    {
        SfxChildWindow* pChildWnd = lcl_GetChildWinFromCurrentView( m_nCurRefDlgId );
        if ( pChildWnd )
        {
            if (pChildWnd->GetController())
            {
                IAnyRefDialog* pRefDlg = dynamic_cast<IAnyRefDialog*>(pChildWnd->GetController().get());
                assert(pRefDlg);
                bIsModal = pChildWnd->IsVisible() && pRefDlg &&
                    !( pRefDlg->IsRefInputMode() && pRefDlg->IsDocAllowed(pDocSh) );
            }
        }
        else if ( pDocSh && comphelper::LibreOfficeKit::isActive() )
        {
            // m_nCurRefDlgId is not deglobalized so it can be set by other view
            // in LOK case when no ChildWindow for this view was detected -> fallback
            ScInputHandler* pHdl = GetInputHdl();
            if ( pHdl )
                bIsModal = pHdl->IsModalMode(*pDocSh);
        }
    }
    else if (pDocSh)
    {
        ScInputHandler* pHdl = GetInputHdl();
        if ( pHdl )
            bIsModal = pHdl->IsModalMode(*pDocSh);
    }

    return bIsModal;
}

bool ScModule::IsTableLocked()
{
    //TODO: Move reference dialog handling to view
    //      Just keep function autopilot here for references to other documents
    bool bLocked = false;

    // Up until now just for ScAnyRefDlg
    if ( m_nCurRefDlgId )
    {
        SfxChildWindow* pChildWnd = lcl_GetChildWinFromAnyView( m_nCurRefDlgId );
        if ( pChildWnd )
        {
            if (pChildWnd->GetController())
            {
                IAnyRefDialog* pRefDlg = dynamic_cast<IAnyRefDialog*>(pChildWnd->GetController().get());
                assert(pRefDlg);
                if (pRefDlg)
                    bLocked = pRefDlg->IsTableLocked();
            }
        }
        else if (!comphelper::LibreOfficeKit::isActive())
            bLocked = true;     // for other views, see IsModalMode
    }

    // We can't stop LOK clients from switching part, and getting out of sync.
    assert(!bLocked || !comphelper::LibreOfficeKit::isActive());

    return bLocked;
}

bool ScModule::IsRefDialogOpen()
{
    //TODO: Move reference dialog handling to view
    //      Just keep function autopilot here for references to other documents
    bool bIsOpen = false;

    if ( m_nCurRefDlgId )
    {
        SfxChildWindow* pChildWnd = lcl_GetChildWinFromCurrentView( m_nCurRefDlgId );
        if ( pChildWnd )
            bIsOpen = pChildWnd->IsVisible();
    }

    return bIsOpen;
}

bool ScModule::IsFormulaMode()
{
    //TODO: Move reference dialog handling to view
    //      Just keep function autopilot here for references to other documents
    bool bIsFormula = false;

    if ( m_nCurRefDlgId )
    {
        SfxChildWindow* pChildWnd = nullptr;

        if ( comphelper::LibreOfficeKit::isActive() )
            pChildWnd = lcl_GetChildWinFromCurrentView( m_nCurRefDlgId );
        else
            pChildWnd = lcl_GetChildWinFromAnyView( m_nCurRefDlgId );

        if ( pChildWnd )
        {
            if (pChildWnd->GetController())
            {
                IAnyRefDialog* pRefDlg = dynamic_cast<IAnyRefDialog*>(pChildWnd->GetController().get());
                assert(pRefDlg);
                bIsFormula = pChildWnd->IsVisible() && pRefDlg && pRefDlg->IsRefInputMode();
            }
        }
        else if ( comphelper::LibreOfficeKit::isActive() )
        {
            // m_nCurRefDlgId is not deglobalized so it can be set by other view
            // in LOK case when no ChildWindow for this view was detected -> fallback
            ScInputHandler* pHdl = GetInputHdl();
            if ( pHdl )
                bIsFormula = pHdl->IsFormulaMode();
        }
    }
    else
    {
        ScInputHandler* pHdl = GetInputHdl();
        if ( pHdl )
            bIsFormula = pHdl->IsFormulaMode();
    }

    if (m_bIsInEditCommand)
        bIsFormula = true;

    return bIsFormula;
}

static void lcl_MarkedTabs( const ScMarkData& rMark, SCTAB& rStartTab, SCTAB& rEndTab )
{
    if (rMark.GetSelectCount() > 1)
    {
        rEndTab = rMark.GetLastSelected();
        rStartTab = rMark.GetFirstSelected();
    }
}

void ScModule::SetReference( const ScRange& rRef, ScDocument& rDoc,
                                    const ScMarkData* pMarkData )
{
    //TODO: Move reference dialog handling to view
    //      Just keep function autopilot here for references to other documents

    // In RefDialogs we also trigger the ZoomIn, if the Ref's Start and End are different
    ScRange aNew = rRef;
    aNew.PutInOrder(); // Always in the right direction

    if( m_nCurRefDlgId )
    {
        SfxChildWindow* pChildWnd = nullptr;

        if ( comphelper::LibreOfficeKit::isActive() )
            pChildWnd = lcl_GetChildWinFromCurrentView( m_nCurRefDlgId );
        else
            pChildWnd = lcl_GetChildWinFromAnyView( m_nCurRefDlgId );

        OSL_ENSURE( pChildWnd, "NoChildWin" );
        if ( pChildWnd )
        {
            if ( m_nCurRefDlgId == SID_OPENDLG_CONSOLIDATE && pMarkData )
            {
                SCTAB nStartTab = aNew.aStart.Tab();
                SCTAB nEndTab   = aNew.aEnd.Tab();
                lcl_MarkedTabs( *pMarkData, nStartTab, nEndTab );
                aNew.aStart.SetTab(nStartTab);
                aNew.aEnd.SetTab(nEndTab);
            }

            if (pChildWnd->GetController())
            {
                IAnyRefDialog* pRefDlg = dynamic_cast<IAnyRefDialog*>(pChildWnd->GetController().get());
                assert(pRefDlg);
                if(pRefDlg)
                {
                    // hide the (color) selection now instead of later from LoseFocus,
                    // don't abort the ref input that causes this call (bDoneRefMode = sal_False)
                    pRefDlg->HideReference( false );
                    pRefDlg->SetReference( aNew, rDoc );
                }
            }
        }
        else if ( comphelper::LibreOfficeKit::isActive() )
        {
            // m_nCurRefDlgId is not deglobalized so it can be set by other view
            // in LOK case when no ChildWindow for this view was detected -> fallback
            ScInputHandler* pHdl = GetInputHdl();
            if (pHdl)
                pHdl->SetReference( aNew, rDoc );
        }
    }
    else
    {
        ScInputHandler* pHdl = GetInputHdl();
        if (pHdl)
            pHdl->SetReference( aNew, rDoc );
        else
        {
            OSL_FAIL("SetReference without receiver");
        }
    }
}

/**
 * Multiple selection
 */
void ScModule::AddRefEntry()
{
    //TODO: Move reference dialog handling to view
    //      Just keep function autopilot here for references to other documents
    if ( m_nCurRefDlgId )
    {
        SfxChildWindow* pChildWnd = lcl_GetChildWinFromAnyView( m_nCurRefDlgId );
        OSL_ENSURE( pChildWnd, "NoChildWin" );
        if ( pChildWnd )
        {
            if (pChildWnd->GetController())
            {
                IAnyRefDialog* pRefDlg = dynamic_cast<IAnyRefDialog*>(pChildWnd->GetController().get());
                assert(pRefDlg);
                if (pRefDlg)
                {
                    pRefDlg->AddRefEntry();
                }
            }
        }
    }
    else
    {
        ScInputHandler* pHdl = GetInputHdl();
        if (pHdl)
            pHdl->AddRefEntry();
    }
}

void ScModule::EndReference()
{
    //TODO: Move reference dialog handling to view
    //      Just keep function autopilot here for references to other documents

    // We also annul the ZoomIn again in RefDialogs

    //FIXME: ShowRefFrame at InputHdl, if the Function AutoPilot is open?
    if ( !m_nCurRefDlgId )
        return;

    SfxChildWindow* pChildWnd = nullptr;

    if ( comphelper::LibreOfficeKit::isActive() )
        pChildWnd = lcl_GetChildWinFromCurrentView( m_nCurRefDlgId );
    else
        pChildWnd = lcl_GetChildWinFromAnyView( m_nCurRefDlgId );

    OSL_ENSURE( pChildWnd, "NoChildWin" );
    if ( pChildWnd )
    {
        if (pChildWnd->GetController())
        {
            IAnyRefDialog* pRefDlg = dynamic_cast<IAnyRefDialog*>(pChildWnd->GetController().get());
            assert(pRefDlg);
            if(pRefDlg)
            {
                pRefDlg->SetActive();
            }
        }
    }
}

/**
 * Idle/OnlineSpelling
 */
void ScModule::AnythingChanged()
{
    sal_uInt64 nOldTime = m_aIdleTimer.GetTimeout();
    if ( nOldTime != SC_IDLE_MIN )
        m_aIdleTimer.SetTimeout( SC_IDLE_MIN );

    nIdleCount = 0;
}

static void lcl_CheckNeedsRepaint( const ScDocShell* pDocShell )
{
    SfxViewFrame* pFrame = SfxViewFrame::GetFirst( pDocShell );
    while ( pFrame )
    {
        SfxViewShell* p = pFrame->GetViewShell();
        ScTabViewShell* pViewSh = dynamic_cast< ScTabViewShell *>( p );
        if ( pViewSh )
            pViewSh->CheckNeedsRepaint();
        pFrame = SfxViewFrame::GetNext( *pFrame, pDocShell );
    }
}

IMPL_LINK_NOARG(ScModule, IdleHandler, Timer *, void)
{
    if ( Application::AnyInput( VclInputFlags::MOUSE | VclInputFlags::KEYBOARD ) )
    {
        m_aIdleTimer.Start(); // Timeout unchanged
        return;
    }

    bool bMore = false;
    ScDocShell* pDocSh = dynamic_cast<ScDocShell*>(SfxObjectShell::Current());

    if ( pDocSh )
    {
        ScDocument& rDoc = pDocSh->GetDocument();
        sc::DocumentLinkManager& rLinkMgr = rDoc.GetDocLinkManager();
        bool bLinks = rLinkMgr.idleCheckLinks();
        bool bWidth = rDoc.IdleCalcTextWidth();

        bMore = bLinks || bWidth; // Still something at all?

        // While calculating a Basic formula, a paint event may have occurred,
        // so check the bNeedsRepaint flags for this document's views
        if (bWidth)
            lcl_CheckNeedsRepaint( pDocSh );
    }


    sal_uInt64 nOldTime = m_aIdleTimer.GetTimeout();
    sal_uInt64 nNewTime = nOldTime;
    if ( bMore )
    {
        nNewTime = SC_IDLE_MIN;
        nIdleCount = 0;
    }
    else
    {
        // Set SC_IDLE_COUNT to initial Timeout - increase afterwards
        if ( nIdleCount < SC_IDLE_COUNT )
            ++nIdleCount;
        else
        {
            nNewTime += SC_IDLE_STEP;
            if ( nNewTime > SC_IDLE_MAX )
                nNewTime = SC_IDLE_MAX;
        }
    }
    if ( nNewTime != nOldTime )
        m_aIdleTimer.SetTimeout( nNewTime );


    m_aIdleTimer.Start();
}

/**
 * Virtual methods for the OptionsDialog
 */
std::optional<SfxItemSet> ScModule::CreateItemSet( sal_uInt16 nId )
{
    std::optional<SfxItemSet> pRet;
    if(SID_SC_EDITOPTIONS == nId)
    {
        pRet.emplace(
            GetPool(),
            svl::Items<
                // TP_USERLISTS:
                SCITEM_USERLIST, SCITEM_USERLIST,
                // TP_GRID:
                SID_ATTR_GRID_OPTIONS, SID_ATTR_GRID_OPTIONS,
                SID_ATTR_METRIC, SID_ATTR_METRIC,
                SID_ATTR_DEFTABSTOP, SID_ATTR_DEFTABSTOP,
                // TP_INPUT:
                SID_SC_INPUT_LEGACY_CELL_SELECTION, SID_SC_OPT_SORT_REF_UPDATE,
                // TP_FORMULA, TP_DEFAULTS:
                SID_SCFORMULAOPTIONS, SID_SCDEFAULTSOPTIONS,
                // TP_VIEW, TP_CALC:
                SID_SCVIEWOPTIONS, SID_SCDOCOPTIONS,
                // TP_INPUT:
                SID_SC_INPUT_WARNACTIVESHEET, SID_SC_INPUT_ENTER_PASTE_MODE,
                // TP_PRINT:
                SID_SCPRINTOPTIONS, SID_SCPRINTOPTIONS,
                // TP_INPUT:
                SID_SC_INPUT_SELECTION, SID_SC_INPUT_MARK_HEADER,
                SID_SC_INPUT_TEXTWYSIWYG, SID_SC_INPUT_TEXTWYSIWYG,
                SID_SC_INPUT_REPLCELLSWARN, SID_SC_INPUT_REPLCELLSWARN,
                // TP_VIEW:
                SID_SC_OPT_SYNCZOOM, SID_SC_OPT_KEY_BINDING_COMPAT,
                SID_SC_OPT_LINKS, SID_SC_OPT_LINKS>);

        const ScAppOptions& rAppOpt = GetAppOptions();

        ScDocShell*     pDocSh = dynamic_cast< ScDocShell *>( SfxObjectShell::Current() );
        ScDocOptions    aCalcOpt = pDocSh
                            ? pDocSh->GetDocument().GetDocOptions()
                            : GetDocOptions();

        ScTabViewShell* pViewSh = dynamic_cast< ScTabViewShell *>( SfxViewShell::Current() );
        ScViewOptions   aViewOpt = pViewSh
                            ? pViewSh->GetViewData().GetOptions()
                            : GetViewOptions();

        ScUserListItem  aULItem( SCITEM_USERLIST );
        const ScUserList& rUL = ScGlobal::GetUserList();

        //  SfxGetpApp()->GetOptions( aSet );

        pRet->Put( SfxUInt16Item( SID_ATTR_METRIC,
                        sal::static_int_cast<sal_uInt16>(rAppOpt.GetAppMetric()) ) );

        // TP_CALC
        pRet->Put( SfxUInt16Item( SID_ATTR_DEFTABSTOP,
                        aCalcOpt.GetTabDistance()));
        pRet->Put( ScTpCalcItem( SID_SCDOCOPTIONS, aCalcOpt ) );

        // TP_VIEW
        pRet->Put( ScTpViewItem( aViewOpt ) );
        pRet->Put( SfxBoolItem( SID_SC_OPT_SYNCZOOM, rAppOpt.GetSynchronizeZoom() ) );

        // TP_INPUT
        const ScInputOptions& rInpOpt = GetInputOptions();
        pRet->Put( SfxUInt16Item( SID_SC_INPUT_SELECTIONPOS,
                    rInpOpt.GetMoveDir() ) );
        pRet->Put( SfxBoolItem( SID_SC_INPUT_SELECTION,
                    rInpOpt.GetMoveSelection() ) );
        pRet->Put( SfxBoolItem( SID_SC_INPUT_EDITMODE,
                    rInpOpt.GetEnterEdit() ) );
        pRet->Put( SfxBoolItem( SID_SC_INPUT_FMT_EXPAND,
                    rInpOpt.GetExtendFormat() ) );
        pRet->Put( SfxBoolItem( SID_SC_INPUT_RANGEFINDER,
                    rInpOpt.GetRangeFinder() ) );
        pRet->Put( SfxBoolItem( SID_SC_INPUT_REF_EXPAND,
                    rInpOpt.GetExpandRefs() ) );
        pRet->Put( SfxBoolItem(SID_SC_OPT_SORT_REF_UPDATE, rInpOpt.GetSortRefUpdate()));
        pRet->Put( SfxBoolItem( SID_SC_INPUT_MARK_HEADER,
                    rInpOpt.GetMarkHeader() ) );
        pRet->Put( SfxBoolItem( SID_SC_INPUT_TEXTWYSIWYG,
                    rInpOpt.GetTextWysiwyg() ) );
        pRet->Put( SfxBoolItem( SID_SC_INPUT_REPLCELLSWARN,
                    rInpOpt.GetReplaceCellsWarn() ) );
        pRet->Put( SfxBoolItem( SID_SC_INPUT_LEGACY_CELL_SELECTION,
                    rInpOpt.GetLegacyCellSelection() ) );
        pRet->Put( SfxBoolItem( SID_SC_INPUT_ENTER_PASTE_MODE,
                    rInpOpt.GetEnterPasteMode() ) );
        pRet->Put( SfxBoolItem( SID_SC_INPUT_WARNACTIVESHEET,
                    rInpOpt.GetWarnActiveSheet() ) );

        // RID_SC_TP_PRINT
        pRet->Put( ScTpPrintItem( GetPrintOptions() ) );

        // TP_GRID
        pRet->Put( aViewOpt.CreateGridItem() );

        // TP_USERLISTS
        aULItem.SetUserList(rUL);
        pRet->Put(aULItem);

        // TP_COMPATIBILITY
        pRet->Put( SfxUInt16Item( SID_SC_OPT_KEY_BINDING_COMPAT,
                                   rAppOpt.GetKeyBindingType() ) );
        pRet->Put( SfxBoolItem( SID_SC_OPT_LINKS, rAppOpt.GetLinksInsertedLikeMSExcel()));

        // TP_DEFAULTS
        pRet->Put( ScTpDefaultsItem( GetDefaultsOptions() ) );

        // TP_FORMULA
        ScFormulaOptions aOptions = GetFormulaOptions();
        if (pDocSh)
        {
            ScCalcConfig aConfig( aOptions.GetCalcConfig());
            aConfig.MergeDocumentSpecific( pDocSh->GetDocument().GetCalcConfig());
            aOptions.SetCalcConfig( aConfig);
        }
        pRet->Put( ScTpFormulaItem( std::move(aOptions) ) );
    }
    return pRet;
}

void ScModule::ApplyItemSet( sal_uInt16 nId, const SfxItemSet& rSet )
{
    if(SID_SC_EDITOPTIONS == nId)
    {
        ModifyOptions( rSet );
    }
}

std::unique_ptr<SfxTabPage> ScModule::CreateTabPage( sal_uInt16 nId, weld::Container* pPage, weld::DialogController* pController, const SfxItemSet& rSet )
{
    std::unique_ptr<SfxTabPage> xRet;
    ScAbstractDialogFactory* pFact = ScAbstractDialogFactory::Create();
    switch(nId)
    {
        case SID_SC_TP_LAYOUT:
        {
            ::CreateTabPage ScTpLayoutOptionsCreate = pFact->GetTabPageCreatorFunc(SID_SC_TP_LAYOUT);
            if (ScTpLayoutOptionsCreate)
                xRet = (*ScTpLayoutOptionsCreate)(pPage, pController, &rSet);
            break;
        }
        case SID_SC_TP_CONTENT:
        {
            ::CreateTabPage ScTpContentOptionsCreate = pFact->GetTabPageCreatorFunc(SID_SC_TP_CONTENT);
            if (ScTpContentOptionsCreate)
                xRet = (*ScTpContentOptionsCreate)(pPage, pController, &rSet);
            break;
        }
        case SID_SC_TP_GRID:
            xRet = SvxGridTabPage::Create(pPage, pController, rSet);
            break;
        case SID_SC_TP_USERLISTS:
        {
            ::CreateTabPage ScTpUserListsCreate = pFact->GetTabPageCreatorFunc(SID_SC_TP_USERLISTS);
            if (ScTpUserListsCreate)
                xRet = (*ScTpUserListsCreate)(pPage, pController, &rSet);
            break;
        }
        case SID_SC_TP_CALC:
        {
            ::CreateTabPage ScTpCalcOptionsCreate = pFact->GetTabPageCreatorFunc(SID_SC_TP_CALC);
            if (ScTpCalcOptionsCreate)
                xRet = (*ScTpCalcOptionsCreate)(pPage, pController, &rSet);
            break;
        }
        case SID_SC_TP_FORMULA:
        {
            ::CreateTabPage ScTpFormulaOptionsCreate = pFact->GetTabPageCreatorFunc(SID_SC_TP_FORMULA);
            if (ScTpFormulaOptionsCreate)
                xRet = (*ScTpFormulaOptionsCreate)(pPage, pController, &rSet);
            break;
        }
        case SID_SC_TP_COMPATIBILITY:
        {
            ::CreateTabPage ScTpCompatOptionsCreate = pFact->GetTabPageCreatorFunc(SID_SC_TP_COMPATIBILITY);
            if (ScTpCompatOptionsCreate)
                xRet = (*ScTpCompatOptionsCreate)(pPage, pController, &rSet);
            break;
        }
        case SID_SC_TP_CHANGES:
        {
            ::CreateTabPage ScRedlineOptionsTabPageCreate = pFact->GetTabPageCreatorFunc(SID_SC_TP_CHANGES);
            if (ScRedlineOptionsTabPageCreate)
                xRet =(*ScRedlineOptionsTabPageCreate)(pPage, pController, &rSet);
            break;
        }
        case RID_SC_TP_PRINT:
        {
            ::CreateTabPage ScTpPrintOptionsCreate = pFact->GetTabPageCreatorFunc(RID_SC_TP_PRINT);
            if (ScTpPrintOptionsCreate)
                xRet = (*ScTpPrintOptionsCreate)(pPage, pController, &rSet);
            break;
        }
        case RID_SC_TP_DEFAULTS:
        {
            ::CreateTabPage ScTpDefaultsOptionsCreate = pFact->GetTabPageCreatorFunc(RID_SC_TP_DEFAULTS);
            if (ScTpDefaultsOptionsCreate)
                xRet = (*ScTpDefaultsOptionsCreate)(pPage, pController, &rSet);
            break;
        }
    }

    OSL_ENSURE( xRet, "ScModule::CreateTabPage(): no valid ID for TabPage!" );

    return xRet;
}

IMPL_LINK( ScModule, CalcFieldValueHdl, EditFieldInfo*, pInfo, void )
{
    //TODO: Merge with ScFieldEditEngine!
    if (!pInfo)
        return;

    const SvxFieldItem& rField = pInfo->GetField();
    const SvxFieldData* pField = rField.GetField();

    if (const SvxURLField* pURLField = dynamic_cast<const SvxURLField*>(pField))
    {
        // URLField
        const OUString& aURL = pURLField->GetURL();

        switch ( pURLField->GetFormat() )
        {
            case SvxURLFormat::AppDefault: //TODO: Settable in the App?
            case SvxURLFormat::Repr:
            {
                pInfo->SetRepresentation( pURLField->GetRepresentation() );
            }
            break;

            case SvxURLFormat::Url:
            {
                pInfo->SetRepresentation( aURL );
            }
            break;
        }

        svtools::ColorConfigEntry eEntry =
            INetURLHistory::GetOrCreate()->QueryUrl( aURL ) ? svtools::LINKSVISITED : svtools::LINKS;
        pInfo->SetTextColor( GetColorConfig().GetColorValue(eEntry).nColor );
    }
    else
    {
        OSL_FAIL("Unknown Field");
        pInfo->SetRepresentation(OUString('?'));
    }
}

void ScModule::RegisterRefController(sal_uInt16 nSlotId, std::shared_ptr<SfxDialogController>& rWnd, weld::Window* pWndAncestor)
{
    std::vector<std::pair<std::shared_ptr<SfxDialogController>, weld::Window*>> & rlRefWindow = m_mapRefController[nSlotId];

    if (std::none_of(rlRefWindow.begin(), rlRefWindow.end(),
                         [rWnd](const std::pair<std::shared_ptr<SfxDialogController>, weld::Window*>& rCandidate)
                         {
                             return rCandidate.first.get() == rWnd.get();
                         }))
    {
        rlRefWindow.emplace_back(rWnd, pWndAncestor);
    }
}

void  ScModule::UnregisterRefController(sal_uInt16 nSlotId, const std::shared_ptr<SfxDialogController>& rWnd)
{
    auto iSlot = m_mapRefController.find( nSlotId );

    if( iSlot == m_mapRefController.end() )
        return;

    std::vector<std::pair<std::shared_ptr<SfxDialogController>, weld::Window*>> & rlRefWindow = iSlot->second;

    auto i = std::find_if(rlRefWindow.begin(), rlRefWindow.end(),
                            [rWnd](const std::pair<std::shared_ptr<SfxDialogController>, weld::Window*>& rCandidate)
                            {
                                return rCandidate.first.get() == rWnd.get();
                            });

    if( i == rlRefWindow.end() )
        return;

    rlRefWindow.erase( i );

    if( rlRefWindow.empty() )
        m_mapRefController.erase( nSlotId );
}

std::shared_ptr<SfxDialogController> ScModule::Find1RefWindow(sal_uInt16 nSlotId, const weld::Window *pWndAncestor)
{
    if (!pWndAncestor)
        return nullptr;

    auto iSlot = m_mapRefController.find( nSlotId );

    if( iSlot == m_mapRefController.end() )
        return nullptr;

    std::vector<std::pair<std::shared_ptr<SfxDialogController>, weld::Window*>> & rlRefWindow = iSlot->second;

    for (auto const& refWindow : rlRefWindow)
        if ( refWindow.second == pWndAncestor )
            return refWindow.first;

    return nullptr;
}

using namespace com::sun::star;

constexpr OUStringLiteral LINGUPROP_AUTOSPELL = u"IsSpellAuto";

void ScModule::GetSpellSettings( LanguageType& rDefLang, LanguageType& rCjkLang, LanguageType& rCtlLang )
{
    // use SvtLinguConfig instead of service LinguProperties to avoid
    // loading the linguistic component
    SvtLinguConfig aConfig;

    SvtLinguOptions aOptions;
    aConfig.GetOptions( aOptions );

    rDefLang = MsLangId::resolveSystemLanguageByScriptType(aOptions.nDefaultLanguage, css::i18n::ScriptType::LATIN);
    rCjkLang = MsLangId::resolveSystemLanguageByScriptType(aOptions.nDefaultLanguage_CJK, css::i18n::ScriptType::ASIAN);
    rCtlLang = MsLangId::resolveSystemLanguageByScriptType(aOptions.nDefaultLanguage_CTL, css::i18n::ScriptType::COMPLEX);
}

void ScModule::SetAutoSpellProperty( bool bSet )
{
    // use SvtLinguConfig instead of service LinguProperties to avoid
    // loading the linguistic component
    SvtLinguConfig aConfig;

    aConfig.SetProperty( LINGUPROP_AUTOSPELL, uno::Any(bSet) );
}

bool ScModule::GetAutoSpellProperty()
{
    // use SvtLinguConfig instead of service LinguProperties to avoid
    // loading the linguistic component
    SvtLinguConfig aConfig;

    SvtLinguOptions aOptions;
    aConfig.GetOptions( aOptions );

    return aOptions.bIsSpellAuto;
}

bool ScModule::HasThesaurusLanguage( LanguageType nLang )
{
    if ( nLang == LANGUAGE_NONE )
        return false;

    bool bHasLang = false;
    try
    {
        uno::Reference< linguistic2::XThesaurus > xThes(LinguMgr::GetThesaurus());
        if ( xThes.is() )
            bHasLang = xThes->hasLocale( LanguageTag::convertToLocale( nLang ) );
    }
    catch( uno::Exception& )
    {
        OSL_FAIL("Error in Thesaurus");
    }

    return bHasLang;
}

SfxStyleFamilies ScModule::CreateStyleFamilies()
{
    SfxStyleFamilies aStyleFamilies;
    std::locale resLocale = ScModule::get()->GetResLocale();

    aStyleFamilies.emplace_back(SfxStyleFamilyItem(SfxStyleFamily::Para,
                                                    ScResId(STR_STYLE_FAMILY_CELL),
                                                    BMP_STYLES_FAMILY_CELL,
                                                    RID_CELLSTYLEFAMILY, resLocale));

    aStyleFamilies.emplace_back(SfxStyleFamilyItem(SfxStyleFamily::Page,
                                                    ScResId(STR_STYLE_FAMILY_PAGE),
                                                    BMP_STYLES_FAMILY_PAGE,
                                                    RID_PAGESTYLEFAMILY, resLocale));

    aStyleFamilies.emplace_back(SfxStyleFamilyItem(SfxStyleFamily::Frame,
                                                    ScResId(STR_STYLE_FAMILY_GRAPHICS),
                                                    BMP_STYLES_FAMILY_GRAPHICS,
                                                    RID_GRAPHICSTYLEFAMILY, resLocale));

    return aStyleFamilies;
}

void ScModule::RegisterAutomationApplicationEventsCaller(css::uno::Reference< ooo::vba::XSinkCaller > const& xCaller)
{
    mxAutomationApplicationEventsCaller = xCaller;
}

void ScModule::CallAutomationApplicationEventSinks(const OUString& Method, css::uno::Sequence< css::uno::Any >& Arguments)
{
    if (mxAutomationApplicationEventsCaller.is())
        mxAutomationApplicationEventsCaller->CallSinks(Method, Arguments);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
