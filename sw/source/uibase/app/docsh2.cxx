/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
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

#include <osl/diagnose.h>
#include <com/sun/star/drawing/ModuleDispatcher.hpp>
#include <com/sun/star/frame/DispatchHelper.hpp>
#include <ooo/vba/word/XDocument.hpp>
#include <comphelper/fileformat.h>
#include <comphelper/processfactory.hxx>
#include <comphelper/propertyvalue.hxx>
#include <comphelper/string.hxx>

#include <sal/log.hxx>
#include <edtwin.hxx>
#include <tools/urlobj.hxx>
#include <unotools/tempfile.hxx>
#include <unotools/configmgr.hxx>
#include <vcl/errinf.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld.hxx>
#include <svl/eitem.hxx>
#include <svl/macitem.hxx>
#include <unotools/pathoptions.hxx>
#include <vcl/transfer.hxx>
#include <sfx2/dinfdlg.hxx>
#include <sfx2/request.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/new.hxx>
#include <sfx2/notebookbar/SfxNotebookBar.hxx>
#include <sfx2/filedlghelper.hxx>
#include <sfx2/printer.hxx>
#include <sfx2/evntconf.hxx>
#include <sfx2/docfile.hxx>
#include <sfx2/docfilt.hxx>
#include <svx/dialogs.hrc>
#include <svx/drawitem.hxx>
#include <editeng/svxacorr.hxx>
#include <svx/fmshell.hxx>
#include <sfx2/linkmgr.hxx>
#include <sfx2/classificationhelper.hxx>
#include <sfx2/watermarkitem.hxx>

#include <svx/ofaitem.hxx>
#include <SwSmartTagMgr.hxx>
#include <sfx2/app.hxx>
#include <basic/sbstar.hxx>
#include <basic/basmgr.hxx>
#include <comphelper/classids.hxx>
#include <fmtcol.hxx>
#include <istype.hxx>
#include <view.hxx>
#include <docsh.hxx>
#include <docary.hxx>
#include <wrtsh.hxx>
#include <rootfrm.hxx>
#include <fldbas.hxx>
#include <viewopt.hxx>
#include <globdoc.hxx>
#include <fldwrap.hxx>
#include <redlndlg.hxx>
#include <doc.hxx>
#include <IDocumentUndoRedo.hxx>
#include <IDocumentSettingAccess.hxx>
#include <IDocumentDeviceAccess.hxx>
#include <IDocumentLinksAdministration.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentStatistics.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <IDocumentRedlineAccess.hxx>
#include <IDocumentState.hxx>
#include <shellio.hxx>
#include <pview.hxx>
#include <srcview.hxx>
#include <wdocsh.hxx>
#include <unotxdoc.hxx>
#include <acmplwrd.hxx>
#include <swmodule.hxx>
#include <unobaseclass.hxx>
#include <swwait.hxx>
#include <swcli.hxx>

#include <cmdid.h>
#include <helpids.h>
#include <strings.hrc>
#include <com/sun/star/ui/dialogs/XFilePicker3.hpp>
#include <com/sun/star/ui/dialogs/XFilePickerControlAccess.hpp>
#include <com/sun/star/ui/dialogs/ExtendedFilePickerElementIds.hpp>
#include <com/sun/star/ui/dialogs/ListboxControlActions.hpp>
#include <com/sun/star/ui/dialogs/CommonFilePickerElementIds.hpp>
#include <com/sun/star/ui/dialogs/TemplateDescription.hpp>
#include <com/sun/star/script/vba/XVBAEventProcessor.hpp>
#include <com/sun/star/script/vba/VBAEventId.hpp>
#include <editeng/acorrcfg.hxx>
#include <officecfg/Office/Security.hxx>
#include <officecfg/Office/Common.hxx>

#include <sfx2/fcontnr.hxx>
#include <svx/ClassificationDialog.hxx>
#include <svtools/embedhlp.hxx>

#include <swabstdlg.hxx>
#include <watermarkdialog.hxx>

#include <ndtxt.hxx>
#include <iodetect.hxx>

#include <memory>

using namespace ::com::sun::star::ui::dialogs;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star;
using namespace ::sfx2;

// create DocInfo (virtual)
std::shared_ptr<SfxDocumentInfoDialog> SwDocShell::CreateDocumentInfoDialog(weld::Window* pParent, const SfxItemSet &rSet)
{
    std::shared_ptr<SfxDocumentInfoDialog> xDlg = std::make_shared<SfxDocumentInfoDialog>(pParent, rSet);
    //only with statistics, when this document is being shown, not
    //from within the Doc-Manager
    SwDocShell* pDocSh = static_cast<SwDocShell*>( SfxObjectShell::Current());
    if( pDocSh == this )
    {
        //Not for SourceView.
        SfxViewShell *pVSh = SfxViewShell::Current();
        if ( pVSh && dynamic_cast< const SwSrcView *>( pVSh ) ==  nullptr )
        {
            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            xDlg->AddFontTabPage();
            xDlg->AddTabPage(u"writerstats"_ustr, SwResId(STR_DOC_STAT), pFact->GetTabPageCreatorFunc(RID_SW_TP_DOC_STAT));
        }
    }
    return xDlg;
}

void SwDocShell::ToggleLayoutMode(SwView* pView)
{
    assert(pView && "SwDocShell::ToggleLayoutMode, pView is null.");

    const SwViewOption& rViewOptions = *pView->GetWrtShell().GetViewOptions();

    //TODO: Should HideWhitespace flag be saved in the document settings?
    GetDoc()->getIDocumentSettingAccess().set(DocumentSettingId::BROWSE_MODE, rViewOptions.getBrowseMode());
    UpdateFontList();  // Why is this necessary here?

    pView->GetViewFrame().GetBindings().Invalidate(FN_SHADOWCURSOR);
    if( !GetDoc()->getIDocumentDeviceAccess().getPrinter( false ) )
        pView->SetPrinter( GetDoc()->getIDocumentDeviceAccess().getPrinter( false ), SfxPrinterChangeFlags::PRINTER | SfxPrinterChangeFlags::JOBSETUP );
    GetDoc()->CheckDefaultPageFormat();
    SfxViewFrame *pTmpFrame = SfxViewFrame::GetFirst(this, false);
    while (pTmpFrame)
    {
        if( pTmpFrame != &pView->GetViewFrame() )
        {
            pTmpFrame->DoClose();
            pTmpFrame = SfxViewFrame::GetFirst(this, false);
        }
        else
            pTmpFrame = SfxViewFrame::GetNext(*pTmpFrame, this, false);
    }

    pView->GetWrtShell().InvalidateLayout(true);

    pView->RecheckBrowseMode();

    pView->SetNewWindowAllowed(!rViewOptions.getBrowseMode());
}

// update text fields on document properties changes
void SwDocShell::DoFlushDocInfo()
{
    if (!m_xDoc)
        return;

    bool bUnlockView(true);
    if (m_pWrtShell)
    {
        bUnlockView = !m_pWrtShell->IsViewLocked();
        m_pWrtShell->LockView( true );    // lock visible section
        m_pWrtShell->StartAllAction();
    }

    m_xDoc->getIDocumentStatistics().DocInfoChgd(IsEnableSetModified());

    if (m_pWrtShell)
    {
        m_pWrtShell->EndAllAction();
        if (bUnlockView)
        {
            m_pWrtShell->LockView( false );
        }
    }
}

static void lcl_processCompatibleSfxHint( const uno::Reference< script::vba::XVBAEventProcessor >& xVbaEvents, const SfxHint& rHint )
{
    using namespace com::sun::star::script::vba::VBAEventId;
    if (rHint.GetId() != SfxHintId::ThisIsAnSfxEventHint)
        return;

    uno::Sequence< uno::Any > aArgs;
    switch (static_cast<const SfxEventHint&>(rHint).GetEventId())
    {
        case SfxEventHintId::CreateDoc:
            xVbaEvents->processVbaEvent(AUTO_NEW, aArgs);
            xVbaEvents->processVbaEvent(DOCUMENT_NEW, aArgs);
        break;
        case SfxEventHintId::OpenDoc:
            xVbaEvents->processVbaEvent(AUTO_OPEN, aArgs);
            xVbaEvents->processVbaEvent(DOCUMENT_OPEN, aArgs);
        break;
        default: break;
    }
}

// Notification on DocInfo changes
void SwDocShell::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    if (!m_xDoc)
    {
        return ;
    }

    uno::Reference< script::vba::XVBAEventProcessor > const xVbaEvents =
        m_xDoc->GetVbaEventProcessor();
    if( xVbaEvents.is() )
        lcl_processCompatibleSfxHint( xVbaEvents, rHint );

    if (rHint.GetId() == SfxHintId::ThisIsAnSfxEventHint)
    {
        switch (static_cast<const SfxEventHint&>(rHint).GetEventId())
        {
            case SfxEventHintId::ActivateDoc:
            case SfxEventHintId::CreateDoc:
            case SfxEventHintId::OpenDoc:
            {
                uno::Sequence< css::uno::Any > aArgs;
                SwModule::get()->CallAutomationApplicationEventSinks(u"DocumentChange"_ustr, aArgs);
                break;
            }
            default:
                break;
        }

        switch (static_cast<const SfxEventHint&>(rHint).GetEventId())
        {
            case SfxEventHintId::CreateDoc:
                {
                    uno::Any aDocument;
                    aDocument <<= mxAutomationDocumentObject;
                    uno::Sequence< uno::Any > aArgs{ aDocument };
                    SwModule::get()->CallAutomationApplicationEventSinks( u"NewDocument"_ustr, aArgs );
                }
                break;
            case SfxEventHintId::OpenDoc:
                {
                    uno::Any aDocument;
                    aDocument <<= mxAutomationDocumentObject;
                    uno::Sequence< uno::Any > aArgs{ aDocument };
                    SwModule::get()->CallAutomationApplicationEventSinks( u"DocumentOpen"_ustr, aArgs );
                }
                break;
            default:
                break;
        }
    }

    sal_uInt16 nAction = 0;
    if (rHint.GetId() == SfxHintId::ThisIsAnSfxEventHint &&
        static_cast<const SfxEventHint&>(rHint).GetEventId() == SfxEventHintId::LoadFinished)
    {
        // #i38126# - own action id
        nAction = 3;
    }
    else
    {
        // switch for more actions
        if( rHint.GetId() == SfxHintId::TitleChanged)
        {
            if( GetMedium() )
                nAction = 2;
        }
    }

    if( !nAction )
        return;

    bool bUnlockView = true; //initializing prevents warning
    if (m_pWrtShell)
    {
        bUnlockView = !m_pWrtShell->IsViewLocked();
        m_pWrtShell->LockView( true );    //lock visible section
        m_pWrtShell->StartAllAction();
    }
    switch( nAction )
    {
    case 2:
        m_xDoc->getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::Filename )->UpdateFields();
        break;
    // #i38126# - own action for event LOADFINISHED
    // in order to avoid a modified document.
    // #i41679# - Also for the instance of <SwDoc>
    // it has to be assured, that it's not modified.
    // Perform the same as for action id 1, but disable <SetModified>.
    case 3:
        {
            const bool bResetModified = IsEnableSetModified();
            if ( bResetModified )
                EnableSetModified( false );
            // #i41679#
            const bool bIsDocModified = m_xDoc->getIDocumentState().IsModified();
            // TODO: is the ResetModified() below because of only the direct call from DocInfoChgd, or does UpdateFields() set it too?

            m_xDoc->getIDocumentStatistics().DocInfoChgd(false);

            // #i41679#
            if ( !bIsDocModified )
                m_xDoc->getIDocumentState().ResetModified();
            if ( bResetModified )
                EnableSetModified();
        }
        break;
    }

    if (m_pWrtShell)
    {
        m_pWrtShell->EndAllAction();
        if( bUnlockView )
            m_pWrtShell->LockView( false );
    }
}

// Notification Close Doc
bool SwDocShell::PrepareClose( bool bUI )
{
    bool bRet = SfxObjectShell::PrepareClose( bUI );

    // If we are going to close it at this point, let potential DocumentBeforeClose event handlers
    // in Automation clients veto it.
    if (bRet && m_xDoc && IsInPrepareClose())
    {
        uno::Any aDocument;
        aDocument <<= mxAutomationDocumentObject;

        uno::Sequence<uno::Any> aArgs{ // Arg 0: Document
                                       aDocument,
                                       // Arg 1: Cancel
                                       uno::Any(false)
        };

        SwModule::get()->CallAutomationApplicationEventSinks(u"DocumentBeforeClose"_ustr, aArgs);

        // If the Cancel argument was set to True by an event handler, return false.
        bool bCancel(false);
        aArgs[1] >>= bCancel;
        if (bCancel)
            bRet = false;
    }

    if( bRet )
        EndListening( *this );

    if (m_xDoc && IsInPrepareClose())
    {
        uno::Reference< script::vba::XVBAEventProcessor > const xVbaEvents =
            m_xDoc->GetVbaEventProcessor();
        if( xVbaEvents.is() )
        {
            using namespace com::sun::star::script::vba::VBAEventId;
            uno::Sequence< uno::Any > aNoArgs;
            xVbaEvents->processVbaEvent(AUTO_CLOSE, aNoArgs);
            xVbaEvents->processVbaEvent(DOCUMENT_CLOSE, aNoArgs);
        }
    }
    return bRet;
}

void SwDocShell::Execute(SfxRequest& rReq)
{
    const SfxItemSet* pArgs = rReq.GetArgs();
    const SfxPoolItem* pItem;
    sal_uInt16 nWhich = rReq.GetSlot();
    bool bDone = false;
    switch ( nWhich )
    {
        case SID_AUTO_CORRECT_DLG:
        {
            SvxSwAutoFormatFlags* pAFlags = &SvxAutoCorrCfg::Get().GetAutoCorrect()->GetSwFlags();
            SwAutoCompleteWord& rACW = SwDoc::GetAutoCompleteWords();

            bool bOldLocked = rACW.IsLockWordLstLocked(),
                 bOldAutoCmpltCollectWords = pAFlags->bAutoCmpltCollectWords;

            rACW.SetLockWordLstLocked( true );

            editeng::SortedAutoCompleteStrings aTmpLst( rACW.GetWordList().createNonOwningCopy() );
            pAFlags->m_pAutoCompleteList = &aTmpLst;

            SfxApplication* pApp = SfxGetpApp();
            SfxRequest aAppReq(SID_AUTO_CORRECT_DLG, SfxCallMode::SYNCHRON, pApp->GetPool());
            SfxBoolItem aSwOptions( SID_AUTO_CORRECT_DLG, true );
            aAppReq.AppendItem(aSwOptions);

            pAFlags->pSmartTagMgr = &SwSmartTagMgr::Get();

            SfxItemSetFixed<SID_AUTO_CORRECT_DLG, SID_AUTO_CORRECT_DLG, SID_OPEN_SMARTTAGOPTIONS, SID_OPEN_SMARTTAGOPTIONS> aSet( pApp->GetPool() );
            aSet.Put( aSwOptions );

            const SfxBoolItem* pOpenSmartTagOptionsItem = nullptr;
            if( pArgs && (pOpenSmartTagOptionsItem = pArgs->GetItemIfSet( SID_OPEN_SMARTTAGOPTIONS, false )) )
                aSet.Put( *pOpenSmartTagOptionsItem );

            SfxAbstractDialogFactory* pFact = SfxAbstractDialogFactory::Create();
            VclPtr<SfxAbstractTabDialog> pDlg = pFact->CreateAutoCorrTabDialog(GetView()->GetFrameWeld(), &aSet);
            pDlg->Execute();
            pDlg.disposeAndClear();


            rACW.SetLockWordLstLocked( bOldLocked );

            SwEditShell::SetAutoFormatFlags( pAFlags );
            rACW.SetMinWordLen( pAFlags->nAutoCmpltWordLen );
            rACW.SetMaxCount( pAFlags->nAutoCmpltListLen );
            if (pAFlags->m_pAutoCompleteList)  // any changes?
            {
                rACW.CheckChangedList( aTmpLst );
                // clear the temp WordList pointer
                pAFlags->m_pAutoCompleteList = nullptr;
            }

            if( !bOldAutoCmpltCollectWords && bOldAutoCmpltCollectWords !=
                pAFlags->bAutoCmpltCollectWords )
            {
                // call on all Docs the idle formatter to start
                // the collection of Words
                for( SwDocShell *pDocSh = static_cast<SwDocShell*>(SfxObjectShell::GetFirst(checkSfxObjectShell<SwDocShell>));
                     pDocSh;
                     pDocSh = static_cast<SwDocShell*>(SfxObjectShell::GetNext( *pDocSh, checkSfxObjectShell<SwDocShell> )) )
                {
                    SwDoc* pTmp = pDocSh->GetDoc();
                    if ( pTmp->getIDocumentLayoutAccess().GetCurrentViewShell() )
                        pTmp->InvalidateAutoCompleteFlag();
                }
            }
        }
        break;

        case SID_PRINTPREVIEW:
            {
                bool bSet = false;
                bool bFound = false, bOnly = true;
                SfxViewFrame *pTmpFrame = SfxViewFrame::GetFirst(this);
                SfxViewShell* pViewShell = SfxViewShell::Current();
                SwView* pCurrView = dynamic_cast< SwView *> ( pViewShell );
                bool bCurrent = isType<SwPagePreview>( pViewShell );

                while( pTmpFrame )    // search Preview
                {
                    if( isType<SwView>( pTmpFrame->GetViewShell()) )
                        bOnly = false;
                    else if( isType<SwPagePreview>( pTmpFrame->GetViewShell()))
                    {
                        pTmpFrame->GetFrame().Appear();
                        bFound = true;
                    }
                    if( bFound && !bOnly )
                        break;
                    pTmpFrame = SfxViewFrame::GetNext(*pTmpFrame, this);
                }

                if( pArgs && SfxItemState::SET ==
                    pArgs->GetItemState( SID_PRINTPREVIEW, false, &pItem ))
                    bSet = static_cast<const SfxBoolItem*>(pItem)->GetValue();
                else
                    bSet = !bCurrent;

                sal_uInt16 nSlotId = 0;
                if( bSet && !bFound )   // Nothing found, so create new Preview
                        nSlotId = SID_VIEWSHELL1;
                else if( bFound && !bSet )
                    nSlotId = bOnly ? SID_VIEWSHELL0 : SID_VIEWSHELL1;

                if( nSlotId )
                {
                    // PagePreview in the WebDocShell
                    // is found under Id VIEWSHELL2.
                    if( dynamic_cast< const SwWebDocShell *>( this ) !=  nullptr && SID_VIEWSHELL1 == nSlotId )
                        nSlotId = SID_VIEWSHELL2;

                    if( pCurrView && pCurrView->GetDocShell() == this )
                        pTmpFrame = &pCurrView->GetViewFrame();
                    else
                        pTmpFrame = SfxViewFrame::GetFirst( this );

                    if (pTmpFrame)
                        pTmpFrame->GetDispatcher()->Execute( nSlotId, SfxCallMode::ASYNCHRON );
                }

                rReq.SetReturnValue(SfxBoolItem(SID_PRINTPREVIEW, bSet ));
            }
            break;
        case SID_TEMPLATE_LOAD:
            {
                OUString aFileName;
                static bool bText = true;
                static bool bFrame = false;
                static bool bPage =  false;
                static bool bNum =   false;
                static bool bMerge = false;

                SfxTemplateFlags nFlags = bFrame ? SfxTemplateFlags::LOAD_FRAME_STYLES : SfxTemplateFlags::NONE;
                if(bPage)
                    nFlags |= SfxTemplateFlags::LOAD_PAGE_STYLES;
                if(bNum)
                    nFlags |= SfxTemplateFlags::LOAD_NUM_STYLES;
                if(nFlags == SfxTemplateFlags::NONE || bText)
                    nFlags |= SfxTemplateFlags::LOAD_TEXT_STYLES;
                if(bMerge)
                    nFlags |= SfxTemplateFlags::MERGE_STYLES;

                if ( pArgs )
                {
                    const SfxStringItem* pTemplateItem = rReq.GetArg<SfxStringItem>(SID_TEMPLATE_NAME);
                    if ( pTemplateItem )
                    {
                        aFileName = pTemplateItem->GetValue();
                        const SfxInt32Item* pFlagsItem = rReq.GetArg<SfxInt32Item>(SID_TEMPLATE_LOAD);
                        if ( pFlagsItem )
                            nFlags = static_cast<SfxTemplateFlags>(o3tl::narrowing<sal_uInt16>(pFlagsItem->GetValue()));
                    }
                }

                if ( aFileName.isEmpty() )
                {
                    weld::Window* pDialogParent = rReq.GetFrameWeld();
                    SAL_WARN_IF(!pDialogParent, "sw.ui", "missing parameter for DialogParent");
                    SfxNewFileDialog aNewFileDlg(pDialogParent, SfxNewFileDialogMode::LoadTemplate);
                    aNewFileDlg.SetTemplateFlags(nFlags);

                    sal_uInt16 nRet = aNewFileDlg.run();
                    if(RET_TEMPLATE_LOAD == nRet)
                    {
                        FileDialogHelper aDlgHelper(TemplateDescription::FILEOPEN_SIMPLE,
                                                    FileDialogFlags::NONE, pDialogParent);
                        aDlgHelper.SetContext(FileDialogHelper::WriterLoadTemplate);
                        uno::Reference < XFilePicker3 > xFP = aDlgHelper.GetFilePicker();

                        SfxObjectFactory &rFact = GetFactory();
                        SfxFilterMatcher aMatcher( rFact.GetFactoryName() );
                        SfxFilterMatcherIter aIter( aMatcher );
                        std::shared_ptr<const SfxFilter> pFlt = aIter.First();
                        while( pFlt )
                        {
                            // --> OD #i117339#
                            if( pFlt->IsAllowedAsTemplate() &&
                                ( pFlt->GetUserData() == "CXML" ||
                                  pFlt->GetUserData() == "CXMLV" ) )
                            {
                                const OUString sWild = pFlt->GetWildcard().getGlob();
                                xFP->appendFilter( pFlt->GetUIName(), sWild );
                            }
                            pFlt = aIter.Next();
                        }
                        bool bWeb = dynamic_cast< SwWebDocShell *>( this ) !=  nullptr;
                        std::shared_ptr<const SfxFilter> pOwnFlt =
                                SwDocShell::Factory().GetFilterContainer()->
                                GetFilter4FilterName(u"writer8"_ustr);

                        // make sure the default file format is also available
                        if(bWeb)
                        {
                            const OUString sWild = pOwnFlt->GetWildcard().getGlob();
                            xFP->appendFilter( pOwnFlt->GetUIName(), sWild );
                        }

                        bool bError = false;
                        // catch exception if wrong filter is selected - should not happen anymore
                        try
                        {
                            xFP->setCurrentFilter( pOwnFlt->GetUIName() );
                        }
                        catch (const uno::Exception&)
                        {
                            bError = true;
                        }

                        if( !bError && ERRCODE_NONE == aDlgHelper.Execute() )
                        {
                            aFileName = xFP->getSelectedFiles().getConstArray()[0];
                        }
                    }
                    else if( RET_OK == nRet)
                    {
                        aFileName = aNewFileDlg.GetTemplateFileName();
                    }

                    nFlags = aNewFileDlg.GetTemplateFlags();
                    rReq.AppendItem( SfxStringItem( SID_TEMPLATE_NAME, aFileName ) );
                    rReq.AppendItem( SfxInt32Item( SID_TEMPLATE_LOAD, static_cast<tools::Long>(nFlags) ) );
                }

                if( !aFileName.isEmpty() )
                {
                    SwgReaderOption aOpt;
                    bText  = bool(nFlags & SfxTemplateFlags::LOAD_TEXT_STYLES );
                    aOpt.SetTextFormats(bText);
                    bFrame = bool(nFlags & SfxTemplateFlags::LOAD_FRAME_STYLES);
                    aOpt.SetFrameFormats(bFrame);
                    bPage  = bool(nFlags & SfxTemplateFlags::LOAD_PAGE_STYLES );
                    aOpt.SetPageDescs(bPage);
                    bNum   = bool(nFlags & SfxTemplateFlags::LOAD_NUM_STYLES  );
                    aOpt.SetNumRules(bNum);
                    //different meaning between SFX_MERGE_STYLES and aOpt.SetMerge!
                    bMerge = bool(nFlags & SfxTemplateFlags::MERGE_STYLES);
                    aOpt.SetMerge( !bMerge );

                    SetError(LoadStylesFromFile(aFileName, aOpt, false));
                    if ( !GetErrorIgnoreWarning() )
                        rReq.Done();
                }
            }
            break;
            case SID_SOURCEVIEW:
            {
                SfxViewShell* pViewShell = GetView()
                                            ? static_cast<SfxViewShell*>(GetView())
                                            : SfxViewShell::Current();
                SfxViewFrame& rViewFrame = pViewShell->GetViewFrame();
                SwSrcView* pSrcView = dynamic_cast< SwSrcView *>( pViewShell );
                if(!pSrcView)
                {
                    // 3 possible state:
                    // 1 - file unsaved -> save as HTML
                    // 2 - file modified and HTML filter active -> save
                    // 3 - file saved in non-HTML -> QueryBox to save as HTML
                    std::shared_ptr<const SfxFilter> pHtmlFlt =
                                    SwIoSystem::GetFilterOfFormat(
                                        u"HTML",
                                        SwWebDocShell::Factory().GetFilterContainer() );
                    bool bLocalHasName = HasName();
                    if(bLocalHasName)
                    {
                        //check for filter type
                        std::shared_ptr<const SfxFilter> pFlt = GetMedium()->GetFilter();
                        if(!pFlt || pFlt->GetUserData() != pHtmlFlt->GetUserData())
                        {
                            std::unique_ptr<weld::Builder> xBuilder(Application::CreateBuilder(rViewFrame.GetFrameWeld(), u"modules/swriter/ui/saveashtmldialog.ui"_ustr));
                            std::unique_ptr<weld::MessageDialog> xQuery(xBuilder->weld_message_dialog(u"SaveAsHTMLDialog"_ustr));
                            if (RET_YES == xQuery->run())
                                bLocalHasName = false;
                            else
                                break;
                        }
                    }
                    if(!bLocalHasName)
                    {
                        FileDialogHelper aDlgHelper(TemplateDescription::FILESAVE_AUTOEXTENSION,
                                                    FileDialogFlags::NONE,
                                                    GetView()->GetFrameWeld());
                        aDlgHelper.SetContext(FileDialogHelper::WriterSaveHTML);
                        aDlgHelper.AddFilter( pHtmlFlt->GetFilterName(), pHtmlFlt->GetDefaultExtension() );
                        aDlgHelper.SetCurrentFilter( pHtmlFlt->GetFilterName() );
                        if( ERRCODE_NONE != aDlgHelper.Execute())
                        {
                            break;
                        }
                        OUString sPath = aDlgHelper.GetPath();
                        SfxStringItem aName(SID_FILE_NAME, sPath);
                        SfxStringItem aFilter(SID_FILTER_NAME, pHtmlFlt->GetName());
                        const SfxPoolItemHolder aResult(
                            rViewFrame.GetDispatcher()->ExecuteList(
                            SID_SAVEASDOC, SfxCallMode::SYNCHRON,
                            { &aName, &aFilter }));
                        const SfxBoolItem* pBool(static_cast<const SfxBoolItem*>(aResult.getItem()));
                        if(!pBool || !pBool->GetValue())
                            break;
                    }
                }

                assert(dynamic_cast<SwWebDocShell*>(this) && "SourceView only in WebDocShell");

                // the SourceView is not the 1 for SwWebDocShell
                sal_uInt16 nSlot = SID_VIEWSHELL1;
                bool bSetModified = false;
                VclPtr<SfxPrinter> pSavePrinter;
                if( nullptr != pSrcView)
                {
                    SfxPrinter* pTemp = GetDoc()->getIDocumentDeviceAccess().getPrinter( false );
                    if(pTemp)
                        pSavePrinter = VclPtr<SfxPrinter>::Create(*pTemp);
                    bSetModified = IsModified() || pSrcView->IsModified();
                    if(pSrcView->IsModified()||pSrcView->HasSourceSaved())
                    {
                        utl::TempFileNamed aTempFile;
                        aTempFile.EnableKillingFile();
                        pSrcView->SaveContent(aTempFile.GetURL());
                        bDone = true;
                        SvxMacro aMac(OUString(), OUString(), STARBASIC);
                        SfxEventConfiguration::ConfigureEvent(GlobalEventConfig::GetEventName( GlobalEventId::OPENDOC ), aMac, this);
                        SfxEventConfiguration::ConfigureEvent(GlobalEventConfig::GetEventName( GlobalEventId::PREPARECLOSEDOC ), aMac, this);
                        SfxEventConfiguration::ConfigureEvent(GlobalEventConfig::GetEventName( GlobalEventId::ACTIVATEDOC ),     aMac, this);
                        SfxEventConfiguration::ConfigureEvent(GlobalEventConfig::GetEventName( GlobalEventId::DEACTIVATEDOC ), aMac, this);
                        ReloadFromHtml(aTempFile.GetURL(), pSrcView);
                        nSlot = 0;
                    }
                    else
                    {
                        nSlot = SID_VIEWSHELL0;
                    }
                }
                if (nSlot)
                    rViewFrame.GetDispatcher()->Execute(nSlot, SfxCallMode::SYNCHRON);
                if(bSetModified)
                    GetDoc()->getIDocumentState().SetModified();
                if(pSavePrinter)
                {
                    GetDoc()->getIDocumentDeviceAccess().setPrinter( pSavePrinter, true, true);
                    //pSavePrinter must not be deleted again
                }
                rViewFrame.GetBindings().SetState(SfxBoolItem(SID_SOURCEVIEW, false)); // not SID_VIEWSHELL2
                rViewFrame.GetBindings().Invalidate( SID_NEWWINDOW );
                rViewFrame.GetBindings().Invalidate( SID_BROWSER_MODE );
                rViewFrame.GetBindings().Invalidate( FN_PRINT_LAYOUT );
            }
            break;
            case SID_GET_COLORLIST:
            {
                const SvxColorListItem* pColItem = GetItem(SID_COLOR_TABLE);
                const XColorListRef& pList = pColItem->GetColorList();
                rReq.SetReturnValue(OfaXColorListItem(SID_GET_COLORLIST, pList));
            }
            break;
        case FN_ABSTRACT_STARIMPRESS:
        case FN_ABSTRACT_NEWDOC:
        {
            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            VclPtr<AbstractSwInsertAbstractDlg> pDlg(pFact->CreateSwInsertAbstractDlg(GetView()->GetFrameWeld()));
            pDlg->StartExecuteAsync(
                [this, pDlg, nWhich] (sal_Int32 nResult)->void
                {
                    if (nResult == RET_OK)
                    {
                        sal_uInt8 nLevel = pDlg->GetLevel();
                        sal_uInt8 nPara = pDlg->GetPara();
                        SwDoc* pSmryDoc = new SwDoc();
                        SfxObjectShellLock xDocSh(new SwDocShell(*pSmryDoc, SfxObjectCreateMode::STANDARD));
                        xDocSh->DoInitNew();

                        bool bImpress = FN_ABSTRACT_STARIMPRESS == nWhich;
                        m_xDoc->Summary(*pSmryDoc, nLevel, nPara, bImpress);
                        if( bImpress )
                        {
                            WriterRef xWrt;
                            // mba: looks as if relative URLs don't make sense here
                            ::GetRTFWriter(std::u16string_view(), OUString(), xWrt);
                            SvMemoryStream *pStrm = new SvMemoryStream();
                            pStrm->SetBufferSize( 16348 );
                            SwWriter aWrt( *pStrm, *pSmryDoc );
                            ErrCodeMsg eErr = aWrt.Write( xWrt );
                            if( !eErr.IgnoreWarning() )
                            {
                                const uno::Reference< uno::XComponentContext >& xContext = ::comphelper::getProcessComponentContext();
                                uno::Reference< frame::XDispatchProvider > xProv = drawing::ModuleDispatcher::create( xContext );

                                uno::Reference< frame::XDispatchHelper > xHelper( frame::DispatchHelper::create(xContext) );
                                pStrm->Seek( STREAM_SEEK_TO_END );
                                pStrm->WriteChar( '\0' );
                                pStrm->Seek( STREAM_SEEK_TO_BEGIN );

                                uno::Sequence< sal_Int8 > aSeq( pStrm->TellEnd() );
                                pStrm->ReadBytes( aSeq.getArray(), aSeq.getLength() );

                                uno::Sequence< beans::PropertyValue > aArgs{
                                    comphelper::makePropertyValue(u"RtfOutline"_ustr, aSeq)
                                };
                                xHelper->executeDispatch( xProv, u"SendOutlineToImpress"_ustr, OUString(), 0, aArgs );
                            }
                            else
                                ErrorHandler::HandleError( eErr );
                        }
                        else
                        {
                            // Create new document
                            SfxViewFrame *pFrame = SfxViewFrame::LoadDocument( *xDocSh, SFX_INTERFACE_NONE );
                            SwView      *pCurrView = static_cast<SwView*>( pFrame->GetViewShell());

                            // Set document's title
                            OUString aTmp = SwResId(STR_ABSTRACT_TITLE) + GetTitle();
                            xDocSh->SetTitle( aTmp );
                            pCurrView->GetWrtShell().SetNewDoc();
                            pFrame->Show();
                            pSmryDoc->getIDocumentState().SetModified();
                        }
                    }
                    pDlg->disposeOnce();
                }
            );
        }
        break;
        case FN_OUTLINE_TO_CLIPBOARD:
        case FN_OUTLINE_TO_IMPRESS:
            {
                bool bEnable = IsEnableSetModified();
                EnableSetModified( false );
                WriterRef xWrt;
                // mba: looks as if relative URLs don't make sense here
                ::GetRTFWriter( u"O", OUString(), xWrt );
                std::unique_ptr<SvMemoryStream> pStrm (new SvMemoryStream());
                pStrm->SetBufferSize( 16348 );
                SwWriter aWrt( *pStrm, *GetDoc() );
                ErrCodeMsg eErr = aWrt.Write( xWrt );
                EnableSetModified( bEnable );
                if( !eErr.IgnoreWarning() )
                {
                    pStrm->Seek( STREAM_SEEK_TO_END );
                    pStrm->WriteChar( '\0' );
                    pStrm->Seek( STREAM_SEEK_TO_BEGIN );
                    if ( nWhich == FN_OUTLINE_TO_IMPRESS )
                    {
                        const uno::Reference< uno::XComponentContext >& xContext = ::comphelper::getProcessComponentContext();
                        uno::Reference< frame::XDispatchProvider > xProv = drawing::ModuleDispatcher::create( xContext );

                        uno::Reference< frame::XDispatchHelper > xHelper( frame::DispatchHelper::create(xContext) );
                        pStrm->Seek( STREAM_SEEK_TO_END );
                        pStrm->WriteChar( '\0' );
                        pStrm->Seek( STREAM_SEEK_TO_BEGIN );

                        uno::Sequence< sal_Int8 > aSeq( pStrm->TellEnd() );
                        pStrm->ReadBytes( aSeq.getArray(), aSeq.getLength() );

                        uno::Sequence< beans::PropertyValue > aArgs{
                            comphelper::makePropertyValue(u"RtfOutline"_ustr, aSeq)
                        };
                        xHelper->executeDispatch( xProv, u"SendOutlineToImpress"_ustr, OUString(), 0, aArgs );
                    }
                    else
                    {
                        rtl::Reference<TransferDataContainer> pClipCntnr = new TransferDataContainer;

                        pClipCntnr->CopyAnyData( SotClipboardFormatId::RTF, static_cast<char const *>(
                                    pStrm->GetData()), pStrm->GetEndOfData() );
                        pClipCntnr->CopyToClipboard(
                            GetView()? &GetView()->GetEditWin() : nullptr );
                    }
                }
                else
                    ErrorHandler::HandleError( eErr );
            }
            break;
            case SID_SPELLCHECKER_CHANGED:
            {
                //! false, true, true is on the save side but a probably overdone
                SwModule::CheckSpellChanges(false, true, true, false );
                Broadcast(SfxHint(SfxHintId::LanguageChanged));
            }
            break;

        case SID_MAIL_PREPAREEXPORT:
        {
            const SfxBoolItem* pNoUpdate = pArgs ?
                pArgs->GetItem<SfxBoolItem>(FN_NOUPDATE, false) :
                nullptr;

            //pWrtShell is not set in page preview
            if (m_pWrtShell)
                m_pWrtShell->StartAllAction();

            if (!pNoUpdate || !pNoUpdate->GetValue())
            {
                m_xDoc->getIDocumentFieldsAccess().UpdateFields( false );
                m_xDoc->getIDocumentLinksAdministration().EmbedAllLinks();
            }

            m_IsRemovedInvisibleContent
                = officecfg::Office::Security::HiddenContent::RemoveHiddenContent::get();
            if (m_IsRemovedInvisibleContent)
                m_xDoc->RemoveInvisibleContent();
            if (m_pWrtShell)
                m_pWrtShell->EndAllAction();
        }
        break;

        case SID_MAIL_EXPORT_FINISHED:
        {
                if (m_pWrtShell)
                    m_pWrtShell->StartAllAction();
                //try to undo the removal of invisible content
                if (m_IsRemovedInvisibleContent)
                    m_xDoc->RestoreInvisibleContent();
                if (m_pWrtShell)
                    m_pWrtShell->EndAllAction();
        }
        break;
        case FN_NEW_HTML_DOC:
        case FN_NEW_GLOBAL_DOC:
            {
                bDone = false;
                bool bCreateHtml = FN_NEW_HTML_DOC == nWhich;

                bool bCreateByOutlineLevel = false;
                sal_Int32  nTemplateOutlineLevel = 0;

                OUString aFileName, aTemplateName;
                if( pArgs && SfxItemState::SET == pArgs->GetItemState( nWhich, false, &pItem ) )
                {
                    aFileName = static_cast<const SfxStringItem*>(pItem)->GetValue();
                    const SfxStringItem* pTemplItem = SfxItemSet::GetItem<SfxStringItem>(pArgs, SID_TEMPLATE_NAME, false);
                    if ( pTemplItem )
                        aTemplateName = pTemplItem->GetValue();
                }
                if ( aFileName.isEmpty() )
                {
                    bool bError = false;

                    FileDialogHelper aDlgHelper(TemplateDescription::FILESAVE_AUTOEXTENSION_TEMPLATE, FileDialogFlags::NONE,
                                                GetView()->GetFrameWeld());
                    aDlgHelper.SetContext(FileDialogHelper::WriterNewHTMLGlobalDoc);

                    const sal_Int16 nControlIds[] = {
                        CommonFilePickerElementIds::PUSHBUTTON_OK,
                        CommonFilePickerElementIds::PUSHBUTTON_CANCEL,
                        CommonFilePickerElementIds::LISTBOX_FILTER,
                        CommonFilePickerElementIds::CONTROL_FILEVIEW,
                        CommonFilePickerElementIds::EDIT_FILEURL,
                        ExtendedFilePickerElementIds::CHECKBOX_AUTOEXTENSION,
                        ExtendedFilePickerElementIds::LISTBOX_TEMPLATE,
                        0
                    };

                    if (bCreateHtml)
                    {
                        const char* aHTMLHelpIds[] =
                        {
                             HID_SEND_HTML_CTRL_PUSHBUTTON_OK,
                             HID_SEND_HTML_CTRL_PUSHBUTTON_CANCEL,
                             HID_SEND_HTML_CTRL_LISTBOX_FILTER,
                             HID_SEND_HTML_CTRL_CONTROL_FILEVIEW,
                             HID_SEND_HTML_CTRL_EDIT_FILEURL,
                             HID_SEND_HTML_CTRL_CHECKBOX_AUTOEXTENSION,
                             HID_SEND_HTML_CTRL_LISTBOX_TEMPLATE,
                             ""
                        };
                        aDlgHelper.SetControlHelpIds( nControlIds, aHTMLHelpIds );
                    }
                    else
                    {
                        const char* aMasterHelpIds[] =
                        {
                             HID_SEND_MASTER_CTRL_PUSHBUTTON_OK,
                             HID_SEND_MASTER_CTRL_PUSHBUTTON_CANCEL,
                             HID_SEND_MASTER_CTRL_LISTBOX_FILTER,
                             HID_SEND_MASTER_CTRL_CONTROL_FILEVIEW,
                             HID_SEND_MASTER_CTRL_EDIT_FILEURL,
                             HID_SEND_MASTER_CTRL_CHECKBOX_AUTOEXTENSION,
                             HID_SEND_MASTER_CTRL_LISTBOX_TEMPLATE,
                             ""
                        };
                        aDlgHelper.SetControlHelpIds( nControlIds, aMasterHelpIds );
                    }
                    uno::Reference < XFilePicker3 > xFP = aDlgHelper.GetFilePicker();

                    std::shared_ptr<const SfxFilter> pFlt;
                    TranslateId pStrId;

                    if( bCreateHtml )
                    {
                        // for HTML there is only one filter!!
                        pFlt = SwIoSystem::GetFilterOfFormat(
                                u"HTML",
                                SwWebDocShell::Factory().GetFilterContainer() );
                        pStrId = STR_LOAD_HTML_DOC;
                    }
                    else
                    {
                        // for Global-documents we now only offer the current one.
                        pFlt = SwGlobalDocShell::Factory().GetFilterContainer()->
                                    GetFilter4Extension( u"odm"_ustr  );
                        pStrId = STR_LOAD_GLOBAL_DOC;
                    }

                    if( pFlt )
                    {
                        const OUString sWild = pFlt->GetWildcard().getGlob();
                        xFP->appendFilter( pFlt->GetUIName(), sWild );
                        try
                        {
                            xFP->setCurrentFilter( pFlt->GetUIName() ) ;
                        }
                        catch (const uno::Exception&)
                        {
                            bError = true;
                        }
                    }
                    if(!bError)
                    {
                        uno::Reference<XFilePickerControlAccess> xCtrlAcc(xFP, UNO_QUERY);

                        bool    bOutline[MAXLEVEL] = {false};
                        const SwOutlineNodes& rOutlNds = m_xDoc->GetNodes().GetOutLineNds();
                        for( size_t n = 0; n < rOutlNds.size(); ++n )
                        {
                            const int nLevel = rOutlNds[n]->GetTextNode()->GetAttrOutlineLevel();
                            if( nLevel > 0 && ! bOutline[nLevel-1] )
                            {
                                bOutline[nLevel-1] = true;
                            }
                        }

                        const sal_uInt16 nStyleCount = m_xDoc->GetTextFormatColls()->size();
                        Sequence<OUString> aListBoxEntries( MAXLEVEL + nStyleCount);
                        OUString* pEntries = aListBoxEntries.getArray();
                        sal_Int32   nIdx = 0 ;

                        OUString    sOutline( SwResId(STR_FDLG_OUTLINE_LEVEL) );
                        for( sal_uInt16 i = 0; i < MAXLEVEL; ++i )
                        {
                            if( bOutline[i] )
                                pEntries[nIdx++] = sOutline + OUString::number( i+1 );
                        }

                        OUString    sStyle( SwResId(STR_FDLG_STYLE) );
                        for(sal_uInt16 i = 0; i < nStyleCount; ++i)
                        {
                            SwTextFormatColl &rTextColl = *(*m_xDoc->GetTextFormatColls())[ i ];
                            if( !rTextColl.IsDefault() && rTextColl.IsAtDocNodeSet() )
                            {
                                pEntries[nIdx++] = sStyle + rTextColl.GetName().toString();
                            }
                        }

                        aListBoxEntries.realloc(nIdx);
                        sal_Int16 nSelect = 0;

                        try
                        {
                            Any aTemplates(&aListBoxEntries, cppu::UnoType<decltype(aListBoxEntries)>::get());

                            xCtrlAcc->setValue( ExtendedFilePickerElementIds::LISTBOX_TEMPLATE,
                                ListboxControlActions::ADD_ITEMS , aTemplates );
                            Any aSelectPos(&nSelect, cppu::UnoType<decltype(nSelect)>::get());
                            xCtrlAcc->setValue( ExtendedFilePickerElementIds::LISTBOX_TEMPLATE,
                                ListboxControlActions::SET_SELECT_ITEM, aSelectPos );
                            xCtrlAcc->setLabel( ExtendedFilePickerElementIds::LISTBOX_TEMPLATE,
                                                    SwResId( STR_FDLG_TEMPLATE_NAME ));
                        }
                        catch (const Exception&)
                        {
                            OSL_FAIL("control access failed");
                        }

                        xFP->setTitle(SwResId(pStrId));
                        SvtPathOptions aPathOpt;
                        xFP->setDisplayDirectory( aPathOpt.GetWorkPath() );
                        if( ERRCODE_NONE == aDlgHelper.Execute())
                        {
                            aFileName = xFP->getSelectedFiles().getConstArray()[0];
                            Any aTemplateValue = xCtrlAcc->getValue(
                                ExtendedFilePickerElementIds::LISTBOX_TEMPLATE,
                                ListboxControlActions::GET_SELECTED_ITEM );
                            OUString sTmpl;
                            aTemplateValue >>= sTmpl;

                            OUString aStyle(SwResId(STR_FDLG_STYLE));
                            OUString aOutline(SwResId(STR_FDLG_OUTLINE_LEVEL));

                            if ( sTmpl.startsWith(aStyle) )
                            {
                                aTemplateName = sTmpl.copy( aStyle.getLength() );   //get string behind "Style: "
                            }
                            else if ( sTmpl.startsWith(aOutline) )
                            {
                                nTemplateOutlineLevel = o3tl::toInt32(sTmpl.subView(aOutline.getLength())); //get string behind "Outline: Level  ";
                                bCreateByOutlineLevel = true;
                            }

                            if ( !aFileName.isEmpty() )
                            {
                                rReq.AppendItem( SfxStringItem( nWhich, aFileName ) );
                                if( !aTemplateName.isEmpty() )
                                    rReq.AppendItem( SfxStringItem( SID_TEMPLATE_NAME, aTemplateName ) );
                            }
                        }
                    }
                }

                if( !aFileName.isEmpty() )
                {
                    if( PrepareClose( false ) )
                    {
                        SwWait aWait( *this, true );

                        if ( bCreateByOutlineLevel )
                        {
                            bDone = bCreateHtml
                                ? m_xDoc->GenerateHTMLDoc( aFileName, nTemplateOutlineLevel )
                                : m_xDoc->GenerateGlobalDoc( aFileName, nTemplateOutlineLevel );
                        }
                        else
                        {
                            const SwTextFormatColl* pSplitColl = nullptr;
                            if ( !aTemplateName.isEmpty() )
                                pSplitColl = m_xDoc->FindTextFormatCollByName(UIName(aTemplateName));
                            bDone = bCreateHtml
                                ? m_xDoc->GenerateHTMLDoc( aFileName, pSplitColl )
                                : m_xDoc->GenerateGlobalDoc( aFileName, pSplitColl );
                        }
                        if( bDone )
                        {
                            SfxStringItem aName( SID_FILE_NAME, aFileName );
                            SfxStringItem aReferer(SID_REFERER, OUString());
                            SfxViewShell* pViewShell = SfxViewShell::GetFirst();
                            while(pViewShell)
                            {
                                //search for the view that created the call
                                if(pViewShell->GetObjectShell() == this && pViewShell->GetDispatcher())
                                {
                                    SfxFrameItem aFrameItem(SID_DOCFRAME, &pViewShell->GetViewFrame());
                                    SfxDispatcher* pDispatch = pViewShell->GetDispatcher();
                                    pDispatch->ExecuteList(SID_OPENDOC,
                                        SfxCallMode::ASYNCHRON,
                                        { &aName, &aReferer, &aFrameItem });
                                    break;
                                }
                                pViewShell = SfxViewShell::GetNext(*pViewShell);
                            }
                        }
                    }
                    if( !bDone && !rReq.IsAPI() )
                    {
                        std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(nullptr,
                                                                      VclMessageType::Info, VclButtonsType::Ok,
                                                                      SwResId(STR_CANTCREATE)));
                        xInfoBox->run();
                    }
                }
            }
            rReq.SetReturnValue(SfxBoolItem( nWhich, bDone ));
            if (bDone)
                rReq.Done();
            else
                rReq.Ignore();
            break;

        case SID_ATTR_YEAR2000:
            if ( pArgs && SfxItemState::SET == pArgs->GetItemState( nWhich , false, &pItem ))
            {
                assert(dynamic_cast< const SfxUInt16Item *>( pItem ) && "wrong Item");
                sal_uInt16 nYear2K = static_cast<const SfxUInt16Item*>(pItem)->GetValue();
                // iterate over Views and put the State to FormShells

                SfxViewFrame* pVFrame = SfxViewFrame::GetFirst( this );
                SfxViewShell* pViewShell = pVFrame ? pVFrame->GetViewShell() : nullptr;
                SwView* pCurrView = dynamic_cast< SwView* >( pViewShell );
                while(pCurrView)
                {
                    FmFormShell* pFormShell = pCurrView->GetFormShell();
                    if(pFormShell)
                        pFormShell->SetY2KState(nYear2K);
                    pVFrame = SfxViewFrame::GetNext( *pVFrame, this );
                    pViewShell = pVFrame ? pVFrame->GetViewShell() : nullptr;
                    pCurrView = dynamic_cast<SwView*>( pViewShell );
                }
                m_xDoc->GetNumberFormatter()->SetYear2000(nYear2K);
            }
        break;
        case FN_OPEN_FILE:
        {
            SfxViewShell* pViewShell = GetView();
            if (!pViewShell)
                pViewShell = SfxViewShell::Current();

            if (!pViewShell)
                // Ok.  I did my best.
                break;

            if (SfxDispatcher* pDispatch = pViewShell->GetDispatcher())
            {
                SfxStringItem aApp(SID_DOC_SERVICE, u"com.sun.star.text.TextDocument"_ustr);
                SfxStringItem aTarget(SID_TARGETNAME, u"_blank"_ustr);
                pDispatch->ExecuteList(SID_OPENDOC, SfxCallMode::API|SfxCallMode::SYNCHRON, { &aApp, &aTarget });
            }
        }
        break;
        case SID_CLASSIFICATION_APPLY:
        {
            if (pArgs && pArgs->GetItemState(nWhich, false, &pItem) == SfxItemState::SET)
            {
                SwWrtShell* pSh = GetWrtShell();
                const OUString& rValue = static_cast<const SfxStringItem*>(pItem)->GetValue();
                auto eType = SfxClassificationPolicyType::IntellectualProperty;
                if (const SfxStringItem* pTypeNameItem = pArgs->GetItemIfSet(SID_TYPE_NAME, false))
                {
                    const OUString& rType = pTypeNameItem->GetValue();
                    eType = SfxClassificationHelper::stringToPolicyType(rType);
                }
                pSh->SetClassification(rValue, eType);
            }
            else
                SAL_WARN("sw.ui", "missing parameter for SID_CLASSIFICATION_APPLY");
        }
        break;
        case SID_CLASSIFICATION_DIALOG:
            if (SfxObjectShell* pObjSh = SfxObjectShell::Current())
            {
                auto xDialog = std::make_shared<svx::ClassificationDialog>(GetView()->GetFrameWeld(), pObjSh->getDocProperties(), false);

                SwWrtShell* pShell = GetWrtShell();
                std::vector<svx::ClassificationResult> aInput = pShell->CollectAdvancedClassification();
                xDialog->setupValues(std::move(aInput));

                weld::DialogController::runAsync(xDialog, [xDialog, pShell](sal_Int32 nResult){
                    if (RET_OK == nResult)
                        pShell->ApplyAdvancedClassification(xDialog->getResult());
                });
            }
            break;
        case SID_PARAGRAPH_SIGN_CLASSIFY_DLG:
            if (SfxObjectShell* pObjSh = SfxObjectShell::Current())
            {
                SwWrtShell* pShell = GetWrtShell();
                auto xDialog = std::make_shared<svx::ClassificationDialog>(GetView()->GetFrameWeld(), pObjSh->getDocProperties(), true, [pShell]()
                {
                    pShell->SignParagraph();
                });

                std::vector<svx::ClassificationResult> aInput = pShell->CollectParagraphClassification();
                xDialog->setupValues(std::move(aInput));

                weld::DialogController::runAsync(xDialog, [xDialog, pShell](sal_Int32 nResult){
                    if (RET_OK == nResult)
                        pShell->ApplyParagraphClassification(xDialog->getResult());
                });
            }
            break;
        case SID_WATERMARK:
        {
            SwWrtShell* pSh = GetWrtShell();
            if ( pSh )
            {
                if (pArgs && pArgs->GetItemState( SID_WATERMARK, false, &pItem ) == SfxItemState::SET)
                {
                    SfxWatermarkItem aItem;
                    aItem.SetText( static_cast<const SfxStringItem*>( pItem )->GetValue() );

                    if ( const SfxStringItem* pFontItem = pArgs->GetItemIfSet( SID_WATERMARK_FONT, false ) )
                        aItem.SetFont( pFontItem->GetValue() );
                    if ( const SfxInt16Item* pAngleItem = pArgs->GetItemIfSet( SID_WATERMARK_ANGLE, false ) )
                        aItem.SetAngle( pAngleItem->GetValue() );
                    if ( const SfxInt16Item* pTransItem = pArgs->GetItemIfSet( SID_WATERMARK_TRANSPARENCY, false ) )
                        aItem.SetTransparency( pTransItem->GetValue() );
                    if ( const SfxUInt32Item* pColorItem = pArgs->GetItemIfSet( SID_WATERMARK_COLOR, false ) )
                        aItem.SetColor( Color(ColorTransparency, pColorItem->GetValue()) );

                    pSh->SetWatermark( aItem );
                }
                else
                {
                    SfxViewShell* pViewShell = GetView() ? GetView() : SfxViewShell::Current();
                    SfxBindings& rBindings( pViewShell->GetViewFrame().GetBindings() );
                    auto xDlg = std::make_shared<SwWatermarkDialog>(pViewShell->GetViewFrame().GetFrameWeld(),
                                                                                  rBindings);
                    weld::DialogController::runAsync(xDlg, [](sal_Int32 /*nResult*/){});
                }
            }
        }
        break;
        case SID_NOTEBOOKBAR:
        {
            const SfxStringItem* pFile = rReq.GetArg<SfxStringItem>( SID_NOTEBOOKBAR );
            SfxViewShell* pViewShell = GetView()? GetView(): SfxViewShell::Current();
            SfxBindings& rBindings( pViewShell->GetViewFrame().GetBindings() );

            if ( SfxNotebookBar::IsActive() )
                sfx2::SfxNotebookBar::ExecMethod( rBindings, pFile ? pFile->GetValue() : u""_ustr );
            else
            {
                sfx2::SfxNotebookBar::CloseMethod( rBindings );
            }
        }
        break;
        case FN_REDLINE_ACCEPT_ALL:
        case FN_REDLINE_REJECT_ALL:
        case FN_REDLINE_REINSTATE_ALL:
        {
            IDocumentRedlineAccess& rRedlineAccess = GetDoc()->getIDocumentRedlineAccess();
            SwWrtShell *pWrtShell = dynamic_cast<SwWrtShell*>(GetDoc()->getIDocumentLayoutAccess().GetCurrentViewShell());

            if (rRedlineAccess.GetRedlineTable().empty())
            {
                break;
            }

            // tables with tracked deletion need Show Changes
            bool bHideChanges = pWrtShell && pWrtShell->GetLayout() &&
                                pWrtShell->GetLayout()->IsHideRedlines();
            bool bChangedHideChanges = false;
            if ( bHideChanges )
            {
                SwTableNode* pOldTableNd = nullptr;
                const SwRedlineTable& aRedlineTable = rRedlineAccess.GetRedlineTable();
                for (SwRedlineTable::size_type n = 0; n < aRedlineTable.size(); ++n)
                {
                    const SwRangeRedline* pRedline = aRedlineTable[n];
                    if ( pRedline->GetType() == RedlineType::Delete )
                    {
                        SwTableNode* pTableNd =
                            pRedline->GetPoint()->GetNode().FindTableNode();
                        if ( pTableNd && pTableNd !=
                                pOldTableNd && pTableNd->GetTable().HasDeletedRowOrCell() )
                        {
                            SfxBoolItem aShow(FN_REDLINE_SHOW, true);
                            SfxViewShell* pViewShell = GetView()
                                    ? GetView()
                                    : SfxViewShell::Current();
                            pViewShell->GetViewFrame().GetDispatcher()->ExecuteList(
                                    FN_REDLINE_SHOW, SfxCallMode::SYNCHRON|SfxCallMode::RECORD,
                                    { &aShow });
                            bChangedHideChanges = true;
                            break;
                        }
                        pOldTableNd = pTableNd;
                    }
                }
            }

            if (pWrtShell)
            {
                pWrtShell->StartAllAction();
            }

            if (nWhich == FN_REDLINE_REINSTATE_ALL)
            {
                pWrtShell->SelAll();
                pWrtShell->ReinstateRedlinesInSelection();
            }
            else
            {
                rRedlineAccess.AcceptAllRedline(nWhich == FN_REDLINE_ACCEPT_ALL);
            }

            if (pWrtShell)
            {
                pWrtShell->EndAllAction();
            }

            if ( bChangedHideChanges )
            {
                SfxBoolItem aShow(FN_REDLINE_SHOW, false);
                SfxViewShell* pViewShell = GetView()? GetView(): SfxViewShell::Current();
                pViewShell->GetViewFrame().GetDispatcher()->ExecuteList(
                        FN_REDLINE_SHOW, SfxCallMode::SYNCHRON|SfxCallMode::RECORD, { &aShow });
            }

            Broadcast(SfxHint(SfxHintId::RedlineChanged));
            rReq.Done();
        }
        break;

        default: OSL_FAIL("wrong Dispatcher");
    }
}

#if defined(_WIN32)
bool SwDocShell::DdeGetData( const OUString& rItem, const OUString& rMimeType,
                             uno::Any & rValue )
{
    return m_xDoc->getIDocumentLinksAdministration().GetData( rItem, rMimeType, rValue );
}

bool SwDocShell::DdeSetData( const OUString& rItem, const OUString& /*rMimeType*/,
                             const uno::Any & /*rValue*/ )
{
    m_xDoc->getIDocumentLinksAdministration().SetData( rItem );
    return false;
}

#endif

::sfx2::SvLinkSource* SwDocShell::DdeCreateLinkSource( const OUString& rItem )
{
    if(officecfg::Office::Common::Security::Scripting::DisableActiveContent::get())
        return nullptr;
    return m_xDoc->getIDocumentLinksAdministration().CreateLinkSource( rItem );
}

void SwDocShell::ReconnectDdeLink(SfxObjectShell& rServer)
{
    if (m_xDoc)
    {
        ::sfx2::LinkManager& rLinkManager = m_xDoc->getIDocumentLinksAdministration().GetLinkManager();
        rLinkManager.ReconnectDdeLink(rServer);
    }
}

void SwDocShell::FillClass( SvGlobalName * pClassName,
                                   SotClipboardFormatId * pClipFormat,
                                   OUString * pLongUserName,
                                   sal_Int32 nVersion,
                                   bool bTemplate /* = false */) const
{
    if (nVersion == SOFFICE_FILEFORMAT_60)
    {
        *pClassName     = SvGlobalName( SO3_SW_CLASSID_60 );
        *pClipFormat    = SotClipboardFormatId::STARWRITER_60;
        *pLongUserName = SwResId(STR_WRITER_DOCUMENT_FULLTYPE);
    }
    else if (nVersion == SOFFICE_FILEFORMAT_8)
    {
        *pClassName     = SvGlobalName( SO3_SW_CLASSID_60 );
        *pClipFormat    = bTemplate ? SotClipboardFormatId::STARWRITER_8_TEMPLATE : SotClipboardFormatId::STARWRITER_8;
        *pLongUserName = SwResId(STR_WRITER_DOCUMENT_FULLTYPE);
    }
// #FIXME check with new Event handling
#if 0
    uno::Reference< document::XVbaEventsHelper > xVbaEventsHelper = m_xDoc->GetVbaEventsHelper();
    if( xVbaEventsHelper.is() )
        lcl_processCompatibleSfxHint( xVbaEventsHelper, rHint );
#endif
}

void SwDocShell::SetModified( bool bSet )
{
    if (comphelper::IsFuzzing())
        return;
    SfxObjectShell::SetModified( bSet );
    if( !IsEnableSetModified())
        return;

    if (!m_xDoc->getIDocumentState().IsInCallModified())
    {
        EnableSetModified( false );
        if( bSet )
        {
            bool const bOld = m_xDoc->getIDocumentState().IsModified();
            m_xDoc->getIDocumentState().SetModified();
            if( !bOld )
            {
                m_xDoc->GetIDocumentUndoRedo().SetUndoNoResetModified();
            }
        }
        else
            m_xDoc->getIDocumentState().ResetModified();

        EnableSetModified();
    }

    UpdateChildWindows();
    Broadcast(SfxHint(SfxHintId::DocChanged));
}

void SwDocShell::UpdateChildWindows()
{
    // if necessary newly initialize Fielddlg (i.e. for TYP_SETVAR)
    if(!GetView())
        return;
    SfxViewFrame& rVFrame = GetView()->GetViewFrame();
    SwFieldDlgWrapper *pWrp = static_cast<SwFieldDlgWrapper*>(rVFrame.
            GetChildWindow( SwFieldDlgWrapper::GetChildWindowId() ));
    if( pWrp )
        pWrp->ReInitDlg();

    // if necessary newly initialize RedlineDlg
    SwRedlineAcceptChild *pRed = static_cast<SwRedlineAcceptChild*>(rVFrame.
            GetChildWindow( SwRedlineAcceptChild::GetChildWindowId() ));
    if( pRed )
        pRed->ReInitDlg();
}

namespace {

// #i48748#
class SwReloadFromHtmlReader : public SwReader
{
    public:
        SwReloadFromHtmlReader( SfxMedium& _rTmpMedium,
                                const OUString& _rFilename,
                                SwDoc* _pDoc )
            : SwReader( _rTmpMedium, _rFilename, _pDoc )
        {
            SetBaseURL( _rFilename );
        }
};

}

void SwDocShell::ReloadFromHtml( const OUString& rStreamName, SwSrcView* pSrcView )
{
    bool bModified = IsModified();

    // The HTTP-Header fields have to be removed, otherwise
    // there are some from Meta-Tags duplicated or triplicated afterwards.
    ClearHeaderAttributesForSourceViewHack();

#if HAVE_FEATURE_SCRIPTING
    // The Document-Basic also bites the dust ...
    // A EnterBasicCall is not needed here, because nothing is called and
    // there can't be any Dok-Basic, that has not yet been loaded inside
    // of an HTML document.
    //#59620# HasBasic() shows, that there already is a BasicManager at the DocShell.
    //          That was always generated in HTML-Import, when there are
    //          Macros in the source code.
    if( officecfg::Office::Common::Filter::HTML::Export::Basic::get() && HasBasic())
    {
        BasicManager *pBasicMan = GetBasicManager();
        if( pBasicMan && (pBasicMan != SfxApplication::GetBasicManager()) )
        {
            sal_uInt16 nLibCount = pBasicMan->GetLibCount();
            while( nLibCount )
            {
                StarBASIC *pBasic = pBasicMan->GetLib( --nLibCount );
                if( pBasic )
                {
                    // Notify the IDE
                    SfxUnoAnyItem aShellItem( SID_BASICIDE_ARG_DOCUMENT_MODEL, Any( GetModel() ) );
                    OUString aLibName( pBasic->GetName() );
                    SfxStringItem aLibNameItem( SID_BASICIDE_ARG_LIBNAME, aLibName );
                    pSrcView->GetViewFrame().GetDispatcher()->ExecuteList(
                                            SID_BASICIDE_LIBREMOVED,
                                            SfxCallMode::SYNCHRON,
                                            { &aShellItem, &aLibNameItem });

                    // Only the modules are deleted from the standard-lib
                    if( nLibCount )
                        pBasicMan->RemoveLib( nLibCount, true );
                    else
                        pBasic->Clear();
                }
            }

            OSL_ENSURE( pBasicMan->GetLibCount() <= 1,
                    "Deleting Basics didn't work" );
        }
    }
#endif
    bool bWasBrowseMode = m_xDoc->getIDocumentSettingAccess().get(DocumentSettingId::BROWSE_MODE);
    RemoveLink();

    // now also the UNO-Model has to be informed about the new Doc #51535#
    rtl::Reference<SwXTextDocument> xDoc(GetBaseModel());
    xDoc->InitNewDoc();

    AddLink();
    //#116402# update font list when new document is created
    UpdateFontList();
    m_xDoc->getIDocumentSettingAccess().set(DocumentSettingId::BROWSE_MODE, bWasBrowseMode);
    pSrcView->SetPool(&GetPool());

    const OUString& rMedname = GetMedium()->GetName();

    // The HTML template still has to be set
    SetHTMLTemplate( *GetDoc() );   //Styles from HTML.vor

    if (SfxViewShell* pViewShell = GetView() ? static_cast<SfxViewShell*>(GetView())
                                             : SfxViewShell::Current())
    {
        SfxViewFrame& rViewFrame = pViewShell->GetViewFrame();
        rViewFrame.GetDispatcher()->Execute( SID_VIEWSHELL0, SfxCallMode::SYNCHRON );
    }

    SubInitNew();

    SfxMedium aMed( rStreamName, StreamMode::READ );
    // #i48748# - use class <SwReloadFromHtmlReader>, because
    // the base URL has to be set to the filename of the document <rMedname>
    // and not to the base URL of the temporary file <aMed> in order to get
    // the URLs of the linked graphics correctly resolved.
    SwReloadFromHtmlReader aReader( aMed, rMedname, m_xDoc.get() );

    aReader.Read( *ReadHTML );

    const SwView* pCurrView = GetView();
    //in print layout the first page(s) may have been formatted as a mix of browse
    //and print layout
    if(!bWasBrowseMode && pCurrView)
    {
        SwWrtShell& rWrtSh = pCurrView->GetWrtShell();
        if( rWrtSh.GetLayout())
            rWrtSh.InvalidateLayout( true );
    }

    // Take HTTP-Header-Attributes over into the DocInfo again.
    // The Base-URL doesn't matter here because TLX uses the one from the document
    // for absolutization.
    SetHeaderAttributesForSourceViewHack();

    if(bModified && !IsReadOnly())
        SetModified();
    else
        m_xDoc->getIDocumentState().ResetModified();
}

ErrCodeMsg SwDocShell::LoadStylesFromFile(const OUString& rURL, SwgReaderOption& rOpt, bool bUnoCall)
{
    ErrCodeMsg nErr = ERRCODE_NONE;

    // Set filter:
    SfxFilterMatcher aMatcher( SwDocShell::Factory().GetFactoryName() );

    // search for filter in WebDocShell, too
    SfxMedium aMed( rURL, StreamMode::STD_READ );
    if (rURL == "private:stream")
        aMed.setStreamToLoadFrom(rOpt.GetInputStream(), true);
    std::shared_ptr<const SfxFilter> pFlt;
    aMatcher.DetectFilter( aMed, pFlt );
    if(!pFlt)
    {
        SfxFilterMatcher aWebMatcher( SwWebDocShell::Factory().GetFactoryName() );
        aWebMatcher.DetectFilter( aMed, pFlt );
    }
    // --> OD #i117339# - trigger import only for own formats
    bool bImport( false );
    if ( aMed.IsStorage() )
    {
        // As <SfxMedium.GetFilter().IsOwnFormat() resp. IsOwnTemplateFormat()
        // does not work correct (e.g., MS Word 2007 XML Template),
        // use workaround provided by MAV.
        uno::Reference< embed::XStorage > xStorage = aMed.GetStorage();
        if ( xStorage.is() )
        {
            // use <try-catch> on retrieving <MediaType> in order to check,
            // if the storage is one of our own ones.
            try
            {
                uno::Reference< beans::XPropertySet > xProps( xStorage, uno::UNO_QUERY_THROW );
                xProps->getPropertyValue( u"MediaType"_ustr );
                bImport = true;
            }
            catch (const uno::Exception&)
            {
                bImport = false;
            }
        }
    }
    if ( bImport )
    {
        Reader* pRead =  ReadXML;
        SwReaderPtr pReader;
        std::optional<SwPaM> pPam;
        // the SW3IO - Reader need the pam/wrtshell, because only then he
        // insert the styles!
        if( bUnoCall )
        {
            SwNodeIndex aIdx( m_xDoc->GetNodes().GetEndOfContent(), -1 );
            pPam.emplace( aIdx );
            pReader.reset(new SwReader( aMed, rURL, *pPam ));
        }
        else
        {
            pReader.reset(new SwReader( aMed, rURL, *m_pWrtShell->GetCursor() ));
        }

        pRead->GetReaderOpt().SetTextFormats( rOpt.IsTextFormats() );
        pRead->GetReaderOpt().SetFrameFormats( rOpt.IsFrameFormats() );
        pRead->GetReaderOpt().SetPageDescs( rOpt.IsPageDescs() );
        pRead->GetReaderOpt().SetNumRules( rOpt.IsNumRules() );
        pRead->GetReaderOpt().SetMerge( rOpt.IsMerge() );

        if( bUnoCall )
        {
            UnoActionContext aAction( m_xDoc.get() );
            nErr = pReader->Read( *pRead );
        }
        else
        {
            m_pWrtShell->StartAllAction();
            nErr = pReader->Read( *pRead );
            m_pWrtShell->EndAllAction();
        }
    }

    return nErr;
}

// Get a client for an embedded object if possible.
SfxInPlaceClient* SwDocShell::GetIPClient( const ::svt::EmbeddedObjectRef& xObjRef )
{
    SfxInPlaceClient* pResult = nullptr;

    SwWrtShell* pShell = GetWrtShell();
    if ( pShell )
    {
        pResult = pShell->GetView().FindIPClient( xObjRef.GetObject(), &pShell->GetView().GetEditWin() );
        if ( !pResult )
            pResult = new SwOleClient( &pShell->GetView(), &pShell->GetView().GetEditWin(), xObjRef );
    }

    return pResult;
}

int SwFindDocShell( SfxObjectShellRef& xDocSh,
                    SfxObjectShellLock& xLockRef,
                    std::u16string_view rFileName,
                    const OUString& rPasswd,
                    const OUString& rFilter,
                    sal_Int16 nVersion,
                    SwDocShell* pDestSh )
{
    if ( rFileName.empty() )
        return 0;

    // 1. Does the file already exist in the list of all Documents?
    INetURLObject aTmpObj( rFileName );
    aTmpObj.SetMark( u"" );

    // Iterate over the DocShell and get the ones with the name

    SfxObjectShell* pShell = pDestSh;
    bool bFirst = nullptr != pShell;

    if( !bFirst )
        // No DocShell passed, starting with the first from the DocShell list
        pShell = SfxObjectShell::GetFirst( checkSfxObjectShell<SwDocShell> );

    while( pShell )
    {
        // We want this one
        SfxMedium* pMed = pShell->GetMedium();
        if( pMed && pMed->GetURLObject() == aTmpObj )
        {
            const SfxPoolItem* pItem;
            if( ( SfxItemState::SET == pMed->GetItemSet().GetItemState(
                                            SID_VERSION, false, &pItem ) )
                    ? (nVersion == static_cast<const SfxInt16Item*>(pItem)->GetValue())
                    : !nVersion )
            {
                // Found, thus return
                xDocSh = pShell;
                return 1;
            }
        }

        if( bFirst )
        {
            bFirst = false;
            pShell = SfxObjectShell::GetFirst( checkSfxObjectShell<SwDocShell> );
        }
        else
            pShell = SfxObjectShell::GetNext( *pShell, checkSfxObjectShell<SwDocShell> );
    }

    // 2. Open the file ourselves
    std::unique_ptr<SfxMedium> xMed(new SfxMedium( aTmpObj.GetMainURL(
                             INetURLObject::DecodeMechanism::NONE ), StreamMode::READ ));
    if( INetProtocol::File == aTmpObj.GetProtocol() )
        xMed->Download(); // Touch the medium (download it)

    std::shared_ptr<const SfxFilter> pSfxFlt;
    if (!xMed->GetErrorIgnoreWarning())
    {
        SfxFilterMatcher aMatcher( rFilter == "writerglobal8"
            ? SwGlobalDocShell::Factory().GetFactoryName()
            : SwDocShell::Factory().GetFactoryName() );

        // No Filter, so search for it. Else test if the one passed is a valid one
        if( !rFilter.isEmpty() )
        {
            pSfxFlt = aMatcher.GetFilter4FilterName( rFilter );
        }

        if( nVersion )
            xMed->GetItemSet().Put( SfxInt16Item( SID_VERSION, nVersion ));

        if( !rPasswd.isEmpty() )
            xMed->GetItemSet().Put( SfxStringItem( SID_PASSWORD, rPasswd ));

        if( !pSfxFlt )
            aMatcher.DetectFilter( *xMed, pSfxFlt );

        if( pSfxFlt )
        {
            // We cannot do anything without a Filter
            xMed->SetFilter( pSfxFlt );

            // If the new shell is created, SfxObjectShellLock should be used to let it be closed later for sure
            xLockRef = new SwDocShell(SfxObjectCreateMode::INTERNAL);
            xDocSh = static_cast<SfxObjectShell*>(xLockRef);
            if (xDocSh->DoLoad(xMed.release()))
            {
                return 2;
            }
        }
    }

    return 0;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
