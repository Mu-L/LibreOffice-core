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

#include <config_feature_desktop.h>
#include <config_wasm_strip.h>

#include <o3tl/test_info.hxx>
#include <osl/file.hxx>
#include <sfx2/docfilt.hxx>
#include <sfx2/infobar.hxx>
#include <sfx2/sfxdlg.hxx>
#include <sfx2/sfxsids.hrc>
#include <sfx2/viewfrm.hxx>
#include <sfx2/classificationhelper.hxx>
#include <sfx2/notebookbar/SfxNotebookBar.hxx>
#include <sfx2/pageids.hxx>
#include <com/sun/star/document/MacroExecMode.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/DispatchRecorder.hpp>
#include <com/sun/star/frame/DispatchRecorderSupplier.hpp>
#include <com/sun/star/frame/XLoadable.hpp>
#include <com/sun/star/frame/XLayoutManager.hpp>
#include <com/sun/star/frame/XComponentLoader.hpp>
#include <com/sun/star/task/PasswordContainer.hpp>
#include <com/sun/star/security/DocumentDigitalSignatures.hpp>
#include <officecfg/Office/Common.hxx>
#include <officecfg/Setup.hxx>
#include <toolkit/helper/vclunohelper.hxx>
#include <vcl/wrkwin.hxx>
#include <unotools/moduleoptions.hxx>
#include <svl/intitem.hxx>
#include <svl/visitem.hxx>
#include <svl/stritem.hxx>
#include <svl/eitem.hxx>
#include <svl/whiter.hxx>
#include <svl/undo.hxx>
#include <vcl/help.hxx>
#include <vcl/stdtext.hxx>
#include <vcl/weld.hxx>
#include <vcl/weldutils.hxx>
#if !ENABLE_WASM_STRIP_PINGUSER
#include <unotools/VersionConfig.hxx>
#endif
#include <unotools/securityoptions.hxx>
#include <svtools/miscopt.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/frame/XFramesSupplier.hpp>
#include <com/sun/star/frame/FrameSearchFlag.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/awt/XWindow.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/util/URLTransformer.hpp>
#include <com/sun/star/util/XURLTransformer.hpp>
#include <com/sun/star/util/XCloseable.hpp>
#include <com/sun/star/frame/XDispatchRecorderSupplier.hpp>
#include <com/sun/star/document/UpdateDocMode.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/uri/UriReferenceFactory.hpp>
#include <com/sun/star/uri/XVndSunStarScriptUrl.hpp>
#include <com/sun/star/document/XViewDataSupplier.hpp>
#include <com/sun/star/container/XIndexContainer.hpp>
#include <com/sun/star/task/InteractionHandler.hpp>
#include <com/sun/star/drawing/XDrawView.hpp>
#include <com/sun/star/drawing/XDrawPagesSupplier.hpp>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <com/sun/star/frame/ModuleManager.hpp>

#include <unotools/ucbhelper.hxx>
#include <comphelper/lok.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/namedvaluecollection.hxx>
#include <comphelper/docpasswordrequest.hxx>
#include <comphelper/docpasswordhelper.hxx>

#include <com/sun/star/uno/Reference.h>

#include <basic/basmgr.hxx>
#include <basic/sbmod.hxx>
#include <basic/sbmeth.hxx>
#include <svtools/strings.hrc>
#include <svtools/svtresid.hxx>
#include <framework/framelistanalyzer.hxx>

#include <optional>

#include <comphelper/sequenceashashmap.hxx>

#include <commandpopup/CommandPopup.hxx>

// Due to ViewFrame::Current
#include <appdata.hxx>
#include <sfx2/app.hxx>
#include <sfx2/objface.hxx>
#include <openflag.hxx>
#include <objshimp.hxx>
#include <sfx2/viewsh.hxx>
#include <sfx2/objsh.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/request.hxx>
#include <sfx2/docfac.hxx>
#include <sfx2/ipclient.hxx>
#include <sfx2/sfxresid.hxx>
#include <sfx2/viewfac.hxx>
#include <sfx2/event.hxx>
#include <sfx2/fcontnr.hxx>
#include <sfx2/docfile.hxx>
#include <sfx2/module.hxx>
#include <sfx2/sfxuno.hxx>
#include <sfx2/progress.hxx>
#include <sfx2/sidebar/Sidebar.hxx>
#include <workwin.hxx>
#include <sfx2/minfitem.hxx>
#include <sfx2/strings.hrc>
#include "impviewframe.hxx"
#include <vcl/commandinfoprovider.hxx>
#include <vcl/svapp.hxx>
#include <svl/cryptosign.hxx>

#define ShellClass_SfxViewFrame
#include <sfxslots.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::ucb;
using namespace ::com::sun::star::frame;
using namespace ::com::sun::star::lang;
using ::com::sun::star::awt::XWindow;
using ::com::sun::star::beans::PropertyValue;
using ::com::sun::star::document::XViewDataSupplier;
using ::com::sun::star::container::XIndexContainer;

constexpr OUString CHANGES_STR = u"private:resource/toolbar/changes"_ustr;

SFX_IMPL_SUPERCLASS_INTERFACE(SfxViewFrame,SfxShell)

void SfxViewFrame::InitInterface_Impl()
{
    GetStaticInterface()->RegisterChildWindow(SID_BROWSER);
    GetStaticInterface()->RegisterChildWindow(SID_RECORDING_FLOATWINDOW);
#if HAVE_FEATURE_DESKTOP
    GetStaticInterface()->RegisterObjectBar(SFX_OBJECTBAR_FULLSCREEN, SfxVisibilityFlags::FullScreen, ToolbarId::FullScreenToolbox);
    GetStaticInterface()->RegisterObjectBar(SFX_OBJECTBAR_APPLICATION, SfxVisibilityFlags::Standard, ToolbarId::EnvToolbox);
#endif
}

namespace {
/// Asks the user if editing a read-only document is really wanted.
class SfxEditDocumentDialog : public weld::MessageDialogController
{
public:
    SfxEditDocumentDialog(weld::Widget* pParent);
};

SfxEditDocumentDialog::SfxEditDocumentDialog(weld::Widget* pParent)
    : MessageDialogController(pParent, u"sfx/ui/editdocumentdialog.ui"_ustr,
            u"EditDocumentDialog"_ustr)
{
}

class SfxQueryOpenAsTemplate
{
private:
    std::unique_ptr<weld::MessageDialog> m_xQueryBox;
public:
    SfxQueryOpenAsTemplate(weld::Window* pParent, bool bAllowIgnoreLock, LockFileEntry& rLockData)
        : m_xQueryBox(Application::CreateMessageDialog(pParent, VclMessageType::Question,
                                                       VclButtonsType::NONE, u""_ustr))
    {
        m_xQueryBox->add_button(SfxResId(STR_QUERY_OPENASTEMPLATE_OPENCOPY_BTN), RET_YES);
        bAllowIgnoreLock
            = bAllowIgnoreLock && officecfg::Office::Common::Misc::AllowOverrideLocking::get();
        if (bAllowIgnoreLock)
            m_xQueryBox->add_button(SfxResId(STR_QUERY_OPENASTEMPLATE_OPEN_BTN), RET_IGNORE);
        m_xQueryBox->add_button(GetStandardText( StandardButtonType::Cancel ), RET_CANCEL);
        m_xQueryBox->set_primary_text(QueryString(bAllowIgnoreLock, rLockData));
        m_xQueryBox->set_default_response(RET_YES);
    }
    short run() { return m_xQueryBox->run(); }

private:
    static OUString QueryString(bool bAllowIgnoreLock, LockFileEntry& rLockData)
    {
        OUString sLockUserData;
        if (!rLockData[LockFileComponent::OOOUSERNAME].isEmpty())
            sLockUserData = rLockData[LockFileComponent::OOOUSERNAME];
        else
            sLockUserData = rLockData[LockFileComponent::SYSUSERNAME];

        if (!sLockUserData.isEmpty() && !rLockData[LockFileComponent::EDITTIME].isEmpty())
            sLockUserData += " ( " + rLockData[LockFileComponent::EDITTIME] + " )";

        if (!sLockUserData.isEmpty())
            sLockUserData = "\n\n" + sLockUserData + "\n";

        const bool bUseLockStr = bAllowIgnoreLock || !sLockUserData.isEmpty();

        OUString sMsg(
            SfxResId(bUseLockStr ? STR_QUERY_OPENASTEMPLATE_LOCKED : STR_QUERY_OPENASTEMPLATE));

        if (bAllowIgnoreLock)
            sMsg += "\n\n" + SfxResId(STR_QUERY_OPENASTEMPLATE_ALLOW_IGNORE);

        return sMsg.replaceFirst("%LOCKINFO", sLockUserData);
    }
};

bool AskPasswordToModify_Impl( const uno::Reference< task::XInteractionHandler >& xHandler, const OUString& aPath, const std::shared_ptr<const SfxFilter>& pFilter, sal_uInt32 nPasswordHash, const uno::Sequence< beans::PropertyValue >& aInfo )
{
    // TODO/LATER: In future the info should replace the direct hash completely
    bool bResult = ( !nPasswordHash && !aInfo.hasElements() );

    SAL_WARN_IF( !(pFilter && ( pFilter->GetFilterFlags() & SfxFilterFlags::PASSWORDTOMODIFY )), "sfx.view",
                       "PasswordToModify feature is active for a filter that does not support it!");

    if ( pFilter && xHandler.is() )
    {
        bool bCancel = false;
        bool bFirstTime = true;

        while ( !bResult && !bCancel )
        {
            bool bMSType = !pFilter->IsOwnFormat();

            ::rtl::Reference< ::comphelper::DocPasswordRequest > pPasswordRequest(
                 new ::comphelper::DocPasswordRequest(
                 bMSType ? ::comphelper::DocPasswordRequestType::MS : ::comphelper::DocPasswordRequestType::Standard,
                 bFirstTime ? css::task::PasswordRequestMode_PASSWORD_ENTER : css::task::PasswordRequestMode_PASSWORD_REENTER,
                 aPath,
                 true ) );

            xHandler->handle( pPasswordRequest );

            if ( pPasswordRequest->isPassword() )
            {
                if ( aInfo.hasElements() )
                {
                    bResult = ::comphelper::DocPasswordHelper::IsModifyPasswordCorrect( pPasswordRequest->getPasswordToModify(), aInfo );
                }
                else
                {
                    // the binary format
                    bResult = ( SfxMedium::CreatePasswordToModifyHash( pPasswordRequest->getPasswordToModify(), pFilter->GetServiceName()=="com.sun.star.text.TextDocument" ) == nPasswordHash );
                }
            }
            else
                bCancel = true;

            bFirstTime = false;
        }
    }

    return bResult;
}

bool physObjIsOlder(INetURLObject const & aMedObj, INetURLObject const & aPhysObj) {
    return ::utl::UCBContentHelper::IsYounger(aMedObj.GetMainURL( INetURLObject::DecodeMechanism::NONE),
                                           aPhysObj.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );
}
}

void SfxViewFrame::ExecReload_Impl( SfxRequest& rReq )
{
    SfxObjectShell* pSh = GetObjectShell();
    switch ( rReq.GetSlot() )
    {
        case SID_EDITDOC:
        case SID_READONLYDOC:
        {
            // Due to Double occupancy in toolboxes (with or without Ctrl),
            // it is also possible that the slot is enabled, but Ctrl-click
            // despite this is not!
            if( !pSh || !pSh->HasName() || !(pSh->Get_Impl()->nLoadedFlags & SfxLoadedFlags::MAINDOCUMENT ))
                break;

            if (pSh->isEditDocLocked())
                break;

            // Only change read-only UI and remove info bar when we succeed
            struct ReadOnlyUIGuard
            {
                SfxViewFrame* m_pFrame;
                SfxObjectShell* m_pSh;
                SfxMedium* m_pMed = nullptr;
                bool m_bSetRO;
                ReadOnlyUIGuard(SfxViewFrame* pFrame, SfxObjectShell* p_Sh)
                    : m_pFrame(pFrame), m_pSh(p_Sh), m_bSetRO(p_Sh->IsReadOnlyUI())
                {}
                ~ReadOnlyUIGuard() COVERITY_NOEXCEPT_FALSE
                {
                    if (m_bSetRO != m_pSh->IsReadOnlyUI())
                    {
                        m_pSh->SetReadOnlyUI(m_bSetRO);
                        if (!m_bSetRO)
                            m_pFrame->RemoveInfoBar(u"readonly");
                        if (m_pMed)
                        {
                            bool const isEnableSetModified(m_pSh->IsEnableSetModified());
                            m_pSh->EnableSetModified(false);
                            // tdf#116066: DoSaveCompleted should be called after SetReadOnlyUI
                            m_pSh->DoSaveCompleted(m_pMed);
                            m_pSh->Broadcast(SfxHint(SfxHintId::ModeChanged));
                            m_pSh->EnableSetModified(isEnableSetModified);
                        }
                    }
                }
            } aReadOnlyUIGuard(this, pSh);

            SfxMedium* pMed = pSh->GetMedium();

            std::shared_ptr<std::recursive_mutex> pChkEditMutex = pMed->GetCheckEditableMutex();
            std::unique_lock<std::recursive_mutex> chkEditLock;
            if (pChkEditMutex != nullptr)
                chkEditLock = std::unique_lock<std::recursive_mutex>(*pChkEditMutex);
            pMed->CancelCheckEditableEntry();

            const SfxBoolItem* pItem = pMed->GetItemSet().GetItem(SID_VIEWONLY, false);
            if ( pItem && pItem->GetValue() )
            {
                SfxApplication* pApp = SfxGetpApp();
                SfxAllItemSet aSet( pApp->GetPool() );
                aSet.Put( SfxStringItem( SID_FILE_NAME, pMed->GetURLObject().GetMainURL(INetURLObject::DecodeMechanism::NONE) ) );
                aSet.Put( SfxBoolItem( SID_TEMPLATE, true ) );
                aSet.Put( SfxStringItem( SID_TARGETNAME, u"_blank"_ustr ) );
                const SfxStringItem* pReferer = pMed->GetItemSet().GetItem(SID_REFERER, false);
                if ( pReferer )
                    aSet.Put( *pReferer );
                const SfxInt16Item* pVersionItem = pMed->GetItemSet().GetItem(SID_VERSION, false);
                if ( pVersionItem )
                    aSet.Put( *pVersionItem );

                if( pMed->GetFilter() )
                {
                    aSet.Put( SfxStringItem( SID_FILTER_NAME, pMed->GetFilter()->GetFilterName() ) );
                    const SfxStringItem* pOptions = pMed->GetItemSet().GetItem(SID_FILE_FILTEROPTIONS, false);
                    if ( pOptions )
                        aSet.Put( *pOptions );
                }

                GetDispatcher()->Execute( SID_OPENDOC, SfxCallMode::ASYNCHRON, aSet );
                return;
            }

            StreamMode nOpenMode;
            bool bNeedsReload = false;
            bool bPasswordEntered = false;
            if ( !pSh->IsReadOnly() )
            {
                // Save and reload Readonly
                if( pSh->IsModified() )
                {
                    if ( pSh->PrepareClose() )
                    {
                        // the storing could let the medium be changed
                        pMed = pSh->GetMedium();
                        bNeedsReload = true;
                    }
                    else
                    {
                        rReq.SetReturnValue( SfxBoolItem( rReq.GetSlot(), false ) );
                        return;
                    }
                }
                nOpenMode = SFX_STREAM_READONLY;
                aReadOnlyUIGuard.m_bSetRO = true;
            }
            else
            {
                if ( pSh->IsReadOnlyMedium()
                  && ( pSh->GetModifyPasswordHash() || pSh->GetModifyPasswordInfo().hasElements() )
                  && !pSh->IsModifyPasswordEntered() )
                {
                    const OUString aDocumentName = INetURLObject( pMed->GetOrigURL() ).GetMainURL( INetURLObject::DecodeMechanism::WithCharset );
                    if( !AskPasswordToModify_Impl( pMed->GetInteractionHandler(), aDocumentName, pMed->GetFilter(), pSh->GetModifyPasswordHash(), pSh->GetModifyPasswordInfo() ) )
                    {
                        // this is a read-only document, if it has "Password to modify"
                        // the user should enter password before he can edit the document
                        rReq.SetReturnValue( SfxBoolItem( rReq.GetSlot(), false ) );
                        return;
                    }

                    pSh->SetModifyPasswordEntered();
                    bPasswordEntered = true;
                }

                nOpenMode = pSh->IsOriginallyReadOnlyMedium() ? SFX_STREAM_READONLY : SFX_STREAM_READWRITE;
                aReadOnlyUIGuard.m_bSetRO = false;

                // if only the view was in the readonly mode then there is no need to do the reload
                if ( !pSh->IsReadOnlyMedium() )
                {
                    // SetReadOnlyUI causes recomputation of window title, using
                    // open mode among other things, so call SetOpenMode before
                    // SetReadOnlyUI:
                    pMed->SetOpenMode( nOpenMode );
                    return;
                }
            }

            if ( rReq.IsAPI() )
            {
                // Control through API if r/w or r/o
                const SfxBoolItem* pEditItem = rReq.GetArg<SfxBoolItem>(SID_EDITDOC);
                if ( pEditItem )
                    nOpenMode = pEditItem->GetValue() ? SFX_STREAM_READWRITE : SFX_STREAM_READONLY;
            }

            // doing

            OUString sTemp;
            osl::FileBase::getFileURLFromSystemPath( pMed->GetPhysicalName(), sTemp );
            INetURLObject aPhysObj( sTemp );
            const SfxInt16Item* pVersionItem = pMed->GetItemSet().GetItem(SID_VERSION, false);

            INetURLObject aMedObj( pMed->GetName() );

            // -> tdf#82744
            // the logic below is following:
            // if the document seems not to need to be reloaded
            //     and the physical name is different to the logical one,
            // then on file system it can be checked that the copy is still newer than the original and no document reload is required.
            // Did some semplification to enhance readability of the 'if' expression
            //
            // when the 'http/https' protocol is active, the bool bPhysObjIsYounger relies upon the getlastmodified Property of a WebDAV resource.
            // Said property should be implemented, but sometimes it's not.
            // implemented. On this case the reload activated here will not work properly.
            // TODO: change the check age method for WebDAV to etag (entity-tag) property value, need some rethinking, since the
            // etag tells that the cache representation (e.g. in LO) is different from the one on the server,
            // but tells nothing about the age
            // Details at this link: http://tools.ietf.org/html/rfc4918#section-15, section 15.7
            bool bIsWebDAV = aMedObj.isAnyKnownWebDAVScheme();

            // tdf#118938 Reload the document when the user enters the editing password,
            //            even if the physical name isn't different to the logical name.
            if ( ( !bNeedsReload && ( ( aMedObj.GetProtocol() == INetProtocol::File &&
                                        ( aMedObj.getFSysPath( FSysStyle::Detect ) != aPhysObj.getFSysPath( FSysStyle::Detect )
                                          || bPasswordEntered ) &&
                                        !physObjIsOlder(aMedObj, aPhysObj))
                                      || (bIsWebDAV && !physObjIsOlder(aMedObj, aPhysObj))
                                      || ( pMed->IsRemote() && !bIsWebDAV ) ) )
                 || pVersionItem )
            // <- tdf#82744
            {
                bool bOK = false;
                bool bRetryIgnoringLock = false;
                bool bOpenTemplate = false;
                std::optional<bool> aOrigROVal;
                if (!pVersionItem)
                {
                    auto pRO = pMed->GetItemSet().GetItem<SfxBoolItem>(SID_DOC_READONLY, false);
                    if (pRO)
                        aOrigROVal = pRO->GetValue();
                }
                do {
                    LockFileEntry aLockData;
                    if ( !pVersionItem )
                    {
                        if (bRetryIgnoringLock)
                            pMed->ResetError();

                        bool bHasStorage = pMed->HasStorage_Impl();
                        // switching edit mode could be possible without reload
                        if ( bHasStorage && pMed->GetStorage() == pSh->GetStorage() )
                        {
                            // TODO/LATER: faster creation of copy
                            if ( !pSh->ConnectTmpStorage_Impl( pMed->GetStorage(), pMed ) )
                                return;
                        }

                        pMed->CloseAndRelease();
                        pMed->SetOpenMode( nOpenMode );
                        // We need to clear the SID_DOC_READONLY item from the set, to allow
                        // MediaDescriptor::impl_openStreamWithURL (called indirectly by
                        // SfxMedium::CompleteReOpen) to properly fill input stream of the
                        // descriptor, even when the file can't be open in read-write mode.
                        // Only then can following call to SfxMedium::LockOrigFileOnDemand
                        // return proper information about who has locked the file, to show
                        // in the SfxQueryOpenAsTemplate box below; otherwise it exits right
                        // after call to SfxMedium::GetMedium_Impl. This mimics what happens
                        // when the file is opened initially, when filter detection code also
                        // calls MediaDescriptor::impl_openStreamWithURL without the item set.
                        pMed->GetItemSet().ClearItem(SID_DOC_READONLY);
                        pMed->CompleteReOpen();
                        pMed->GetItemSet().Put(
                            SfxBoolItem(SID_DOC_READONLY, !(nOpenMode & StreamMode::WRITE)));
                        if ( nOpenMode & StreamMode::WRITE )
                        {
                            auto eResult = pMed->LockOrigFileOnDemand(
                                true, true, bRetryIgnoringLock, &aLockData);
                            bRetryIgnoringLock
                                = eResult == SfxMedium::LockFileResult::FailedLockFile;
                        }

                        // LockOrigFileOnDemand might set the readonly flag itself, it should be set back
                        pMed->GetItemSet().Put( SfxBoolItem( SID_DOC_READONLY, !( nOpenMode & StreamMode::WRITE ) ) );

                        if ( !pMed->GetErrorCode() )
                            bOK = true;
                    }

                    if( !bOK )
                    {
                        if (nOpenMode == SFX_STREAM_READWRITE && !rReq.IsAPI())
                        {
                            // css::sdbcx::User offering to open it as a template
                            SfxQueryOpenAsTemplate aBox(GetWindow().GetFrameWeld(),
                                                        bRetryIgnoringLock, aLockData);

                            short nUserAnswer = aBox.run();
                            bOpenTemplate = RET_YES == nUserAnswer;
                            // Always reset this here to avoid infinite loop
                            bRetryIgnoringLock = RET_IGNORE == nUserAnswer;
                            if (RET_CANCEL == nUserAnswer)
                                pMed->AddToCheckEditableWorkerList();
                        }
                        else
                            bRetryIgnoringLock = false;
                    }
                }
                while ( !bOK && bRetryIgnoringLock );

                if( !bOK )
                {
                    ErrCodeMsg nErr = pMed->GetErrorCode();
                    if ( pVersionItem )
                        nErr = ERRCODE_IO_ACCESSDENIED;
                    else
                    {
                        pMed->ResetError();
                        pMed->SetOpenMode( SFX_STREAM_READONLY );
                        if (aOrigROVal)
                            pMed->GetItemSet().Put(SfxBoolItem(SID_DOC_READONLY, *aOrigROVal));
                        else
                            pMed->GetItemSet().ClearItem(SID_DOC_READONLY);
                        pMed->ReOpen();
                        pSh->DoSaveCompleted( pMed );
                    }

                    // Readonly document can not be switched to edit mode?
                    rReq.Done();

                    if ( nOpenMode == SFX_STREAM_READWRITE && !rReq.IsAPI() )
                    {
                        if ( bOpenTemplate )
                        {
                            SfxApplication* pApp = SfxGetpApp();
                            SfxAllItemSet aSet( pApp->GetPool() );
                            aSet.Put( SfxStringItem( SID_FILE_NAME, pMed->GetName() ) );
                            const SfxStringItem* pReferer = pMed->GetItemSet().GetItem(SID_REFERER, false);
                            if ( pReferer )
                                aSet.Put( *pReferer );
                            aSet.Put( SfxBoolItem( SID_TEMPLATE, true ) );
                            if ( pVersionItem )
                                aSet.Put( *pVersionItem );

                            if( pMed->GetFilter() )
                            {
                                aSet.Put( SfxStringItem( SID_FILTER_NAME, pMed->GetFilter()->GetFilterName() ) );
                                const SfxStringItem* pOptions = pMed->GetItemSet().GetItem(SID_FILE_FILTEROPTIONS, false);
                                if ( pOptions )
                                    aSet.Put( *pOptions );
                            }

                            GetDispatcher()->Execute( SID_OPENDOC, SfxCallMode::ASYNCHRON, aSet );
                            return;
                        }

                        nErr = ERRCODE_NONE;
                    }

                    // Keep the read-only UI
                    aReadOnlyUIGuard.m_bSetRO = true;

                    ErrorHandler::HandleError( nErr );
                    rReq.SetReturnValue(
                        SfxBoolItem( rReq.GetSlot(), false ) );
                    return;
                }
                else
                {
                    aReadOnlyUIGuard.m_pMed = pMed;
                    rReq.SetReturnValue( SfxBoolItem( rReq.GetSlot(), true ) );
                    rReq.Done( true );
                    return;
                }
            }

            rReq.AppendItem( SfxBoolItem(SID_FORCERELOAD,
                    (rReq.GetSlot() == SID_EDITDOC
                     // tdf#151715 exclude files loaded from /tmp to avoid Notebookbar bugs
                     && (!pSh->IsOriginallyReadOnlyMedium() || pSh->IsOriginallyLoadedReadOnlyMedium()))
                    || bNeedsReload) );
            rReq.AppendItem( SfxBoolItem( SID_SILENT, true ));

            [[fallthrough]]; //TODO ???
        }

        case SID_RELOAD:
        {
            // Due to Double occupancy in toolboxes (with or without Ctrl),
            // it is also possible that the slot is enabled, but Ctrl-click
            // despite this is not!
            if ( !pSh || !pSh->CanReload_Impl() )
                break;
            SfxApplication* pApp = SfxGetpApp();
            const SfxBoolItem* pForceReloadItem = rReq.GetArg<SfxBoolItem>(SID_FORCERELOAD);
            if(  pForceReloadItem && !pForceReloadItem->GetValue() &&
                !pSh->GetMedium()->IsExpired() )
                return;
            if( m_pImpl->bReloading || pSh->IsInModalMode() )
                return;

            // AutoLoad is prohibited if possible
            const SfxBoolItem* pAutoLoadItem = rReq.GetArg<SfxBoolItem>(SID_AUTOLOAD);
            if ( pAutoLoadItem && pAutoLoadItem->GetValue() &&
                 GetFrame().IsAutoLoadLocked_Impl() )
                return;

            SfxObjectShellLock xOldObj( pSh );
            m_pImpl->bReloading = true;
            const SfxStringItem* pURLItem = rReq.GetArg<SfxStringItem>(SID_FILE_NAME);
            // Open as editable?
            bool bForEdit = !pSh->IsReadOnly();

            // If possible ask the User
            bool bDo = GetViewShell()->PrepareClose();
            const SfxBoolItem* pSilentItem = rReq.GetArg<SfxBoolItem>(SID_SILENT);
            if (getenv("SAL_NO_QUERYSAVE"))
                bDo = true;
            else if (bDo && GetFrame().DocIsModified_Impl() && !rReq.IsAPI()
                     && (!pSilentItem || !pSilentItem->GetValue()))
            {
                std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(
                    GetWindow().GetFrameWeld(), VclMessageType::Question, VclButtonsType::YesNo,
                    SfxResId(STR_QUERY_LASTVERSION)));
                bDo = RET_YES == xBox->run();
            }

            if ( bDo )
            {
                SfxMedium *pMedium = xOldObj->GetMedium();
                std::shared_ptr<std::recursive_mutex> pChkEditMutex
                    = pMedium->GetCheckEditableMutex();
                std::unique_lock<std::recursive_mutex> chkEditLock;
                if (pChkEditMutex != nullptr)
                    chkEditLock = std::unique_lock<std::recursive_mutex>(*pChkEditMutex);
                pMedium->CancelCheckEditableEntry();

                bool bHandsOff =
                    ( pMedium->GetURLObject().GetProtocol() == INetProtocol::File && !xOldObj->IsDocShared() );

                // Empty existing SfxMDIFrames for this Document
                // in native format or R/O, open it now for editing?
                SfxObjectShellLock xNewObj;

                // collect the views of the document
                // TODO: when UNO ViewFactories are available for SFX-based documents, the below code should
                // be UNOized, too
                typedef ::std::pair< Reference< XFrame >, SfxInterfaceId >  ViewDescriptor;
                ::std::vector< ViewDescriptor > aViewFrames;
                SfxViewFrame *pView = GetFirst( xOldObj );
                while ( pView )
                {
                    Reference< XFrame > xFrame( pView->GetFrame().GetFrameInterface() );
                    SAL_WARN_IF( !xFrame.is(), "sfx.view", "SfxViewFrame::ExecReload_Impl: no XFrame?!");
                    aViewFrames.emplace_back( xFrame, pView->GetCurViewId() );

                    pView = GetNext( *pView, xOldObj );
                }

                xOldObj->Get_Impl()->pReloadTimer.reset();

                std::optional<SfxAllItemSet> pNewSet;
                std::shared_ptr<const SfxFilter> pFilter = pMedium->GetFilter();
                if( pURLItem )
                {
                    pNewSet.emplace( pApp->GetPool() );
                    pNewSet->Put( *pURLItem );

                    // Filter Detection
                    OUString referer;
                    const SfxStringItem* refererItem = rReq.GetArg<SfxStringItem>(SID_REFERER);
                    if (refererItem != nullptr) {
                        referer = refererItem->GetValue();
                    }
                    SfxMedium aMedium( pURLItem->GetValue(), referer, SFX_STREAM_READWRITE );
                    SfxFilterMatcher().GuessFilter( aMedium, pFilter );
                    if ( pFilter )
                        pNewSet->Put( SfxStringItem( SID_FILTER_NAME, pFilter->GetName() ) );
                    pNewSet->Put( aMedium.GetItemSet() );
                }
                else
                {
                    pNewSet.emplace( pMedium->GetItemSet() );
                    pNewSet->ClearItem( SID_VIEW_ID );
                    pNewSet->ClearItem( SID_STREAM );
                    pNewSet->ClearItem( SID_INPUTSTREAM );
                    pNewSet->Put( SfxStringItem( SID_FILTER_NAME, pMedium->GetFilter()->GetName() ) );

                    // let the current security settings be checked again
                    pNewSet->Put( SfxUInt16Item( SID_MACROEXECMODE, document::MacroExecMode::USE_CONFIG ) );

                    if ( pSh->IsOriginallyReadOnlyMedium()
                         || pSh->IsOriginallyLoadedReadOnlyMedium() )
                        // edit mode is switched or reload of readonly document
                        pNewSet->Put( SfxBoolItem( SID_DOC_READONLY, true ) );
                    else
                        // Reload of file opened for writing
                        pNewSet->ClearItem( SID_DOC_READONLY );
                }

                // If a salvaged file is present, do not enclose the OrigURL
                // again, since the Template is invalid after reload.
                const SfxStringItem* pSalvageItem = SfxItemSet::GetItem<SfxStringItem>(&*pNewSet, SID_DOC_SALVAGE, false);
                if( pSalvageItem )
                {
                    pNewSet->ClearItem( SID_DOC_SALVAGE );
                }

#if HAVE_FEATURE_MULTIUSER_ENVIRONMENT
                // TODO/LATER: Temporary solution, the SfxMedium must know the original URL as aLogicName
                //             SfxMedium::Transfer_Impl() will be forbidden then.
                if ( xOldObj->IsDocShared() )
                    pNewSet->Put( SfxStringItem( SID_FILE_NAME, xOldObj->GetSharedFileURL() ) );
#endif
                if ( pURLItem )
                    pNewSet->Put( SfxStringItem( SID_REFERER, pMedium->GetName() ) );
                else
                    pNewSet->Put( SfxStringItem( SID_REFERER, OUString() ) );

                xOldObj->CancelTransfers();


                if ( pSilentItem && pSilentItem->GetValue() )
                    pNewSet->Put( SfxBoolItem( SID_SILENT, true ) );

                const SfxUnoAnyItem* pInteractionItem = SfxItemSet::GetItem<SfxUnoAnyItem>(&*pNewSet, SID_INTERACTIONHANDLER, false);
                const SfxUInt16Item* pMacroExecItem = SfxItemSet::GetItem<SfxUInt16Item>(&*pNewSet, SID_MACROEXECMODE, false);
                const SfxUInt16Item* pDocTemplateItem = SfxItemSet::GetItem<SfxUInt16Item>(&*pNewSet, SID_UPDATEDOCMODE, false);

                if (!pInteractionItem)
                {
                    Reference < task::XInteractionHandler2 > xHdl = task::InteractionHandler::createWithParent( ::comphelper::getProcessComponentContext(), nullptr );
                    if (xHdl.is())
                        pNewSet->Put( SfxUnoAnyItem(SID_INTERACTIONHANDLER,css::uno::Any(xHdl)) );
                }

                if (!pMacroExecItem)
                    pNewSet->Put( SfxUInt16Item(SID_MACROEXECMODE,css::document::MacroExecMode::USE_CONFIG) );
                if (!pDocTemplateItem)
                    pNewSet->Put( SfxUInt16Item(SID_UPDATEDOCMODE,css::document::UpdateDocMode::ACCORDING_TO_CONFIG) );

                xOldObj->SetModified( false );
                // Do not cache the old Document! Is invalid when loading
                // another document.

                bool bHasStorage = pMedium->HasStorage_Impl();
                if( bHandsOff )
                {
                    if ( bHasStorage && pMedium->GetStorage() == xOldObj->GetStorage() )
                    {
                        // TODO/LATER: faster creation of copy
                        if ( !xOldObj->ConnectTmpStorage_Impl( pMedium->GetStorage(), pMedium ) )
                            return;
                    }

                    pMedium->CloseAndRelease();
                }

                xNewObj = SfxObjectShell::CreateObject( pFilter->GetServiceName() );

                if ( xOldObj->IsModifyPasswordEntered() )
                    xNewObj->SetModifyPasswordEntered();

                uno::Sequence < beans::PropertyValue > aLoadArgs;
                TransformItems( SID_OPENDOC, *pNewSet, aLoadArgs );
                try
                {
                    uno::Reference < frame::XLoadable > xLoad( xNewObj->GetModel(), uno::UNO_QUERY );
                    xLoad->load( aLoadArgs );
                }
                catch ( uno::Exception& )
                {
                    xNewObj->DoClose();
                    xNewObj = nullptr;
                    pMedium->AddToCheckEditableWorkerList();
                }

                pNewSet.reset();

                if( !xNewObj.Is() )
                {
                    if( bHandsOff )
                    {
                        // back to old medium
                        pMedium->ReOpen();
                        pMedium->LockOrigFileOnDemand( false, true );

                        xOldObj->DoSaveCompleted( pMedium );
                    }
                }
                else
                {
                    if ( xNewObj->GetModifyPasswordHash() && xNewObj->GetModifyPasswordHash() != xOldObj->GetModifyPasswordHash() )
                    {
                        xNewObj->SetModifyPasswordEntered( false );
                        xNewObj->SetReadOnly();
                    }
                    else if ( rReq.GetSlot() == SID_EDITDOC || rReq.GetSlot() == SID_READONLYDOC )
                    {
                        xNewObj->SetReadOnlyUI( !bForEdit );
                    }

#if HAVE_FEATURE_MULTIUSER_ENVIRONMENT
                    if ( xNewObj->IsDocShared() )
                    {
                        // the file is shared but the closing can change the sharing control file
                        xOldObj->DoNotCleanShareControlFile();
                    }
#endif
                    // the Reload and Silent items were only temporary, remove them
                    xNewObj->GetMedium()->GetItemSet().ClearItem( SID_RELOAD );
                    xNewObj->GetMedium()->GetItemSet().ClearItem( SID_SILENT );
                    TransformItems( SID_OPENDOC, xNewObj->GetMedium()->GetItemSet(), aLoadArgs );

                    UpdateDocument_Impl();

                    auto sModule = vcl::CommandInfoProvider::GetModuleIdentifier(GetFrame().GetFrameInterface());
                    OUString sReloadNotebookBar;
                    if (sModule == "com.sun.star.text.TextDocument")
                        sReloadNotebookBar = u"modules/swriter/ui/"_ustr;
                    else if (sModule == "com.sun.star.sheet.SpreadsheetDocument")
                        sReloadNotebookBar = u"modules/scalc/ui/"_ustr;
                    else if (sfx2::SfxNotebookBar::IsActive()
                             && sModule != "presentation.PresentationDocument"
                             && sModule != "com.sun.star.drawing.DrawingDocument")
                    {
                        assert(false && "SID_RELOAD Notebookbar active, but not refreshed here");
                    }

                    try
                    {
                        for (auto const& viewFrame : aViewFrames)
                        {
                            LoadViewIntoFrame_Impl( *xNewObj, viewFrame.first, aLoadArgs, viewFrame.second, false );
                        }
                        aViewFrames.clear();
                    }
                    catch( const Exception& )
                    {
                        // close the remaining frames
                        // Don't catch exceptions herein, if this fails, then we're left in an indetermined state, and
                        // crashing is better than trying to proceed
                        for (auto const& viewFrame : aViewFrames)
                        {
                            Reference< util::XCloseable > xClose( viewFrame.first, UNO_QUERY_THROW );
                            xClose->close( true );
                        }
                        aViewFrames.clear();
                    }

                    const SfxInt32Item* pPageNumber = rReq.GetArg<SfxInt32Item>(SID_PAGE_NUMBER);
                    if (pPageNumber && pPageNumber->GetValue() >= 0)
                    {
                        // Restore current page after reload.
                        uno::Reference<drawing::XDrawView> xController(
                            xNewObj->GetModel()->getCurrentController(), uno::UNO_QUERY);
                        uno::Reference<drawing::XDrawPagesSupplier> xSupplier(xNewObj->GetModel(),
                                                                              uno::UNO_QUERY);
                        uno::Reference<drawing::XDrawPages> xDrawPages = xSupplier->getDrawPages();
                        uno::Reference<drawing::XDrawPage> xDrawPage(
                            xDrawPages->getByIndex(pPageNumber->GetValue()), uno::UNO_QUERY);
                        xController->setCurrentPage(xDrawPage);
                    }

                    // Propagate document closure.
                    SfxGetpApp()->NotifyEvent( SfxEventHint( SfxEventHintId::CloseDoc, GlobalEventConfig::GetEventName( GlobalEventId::CLOSEDOC ), xOldObj ) );

                    // tdf#126006 Calc needs to reload the notebookbar after closing the document
                    if (!sReloadNotebookBar.isEmpty())
                        sfx2::SfxNotebookBar::ReloadNotebookBar(sReloadNotebookBar);
                }

                // Record as done
                rReq.Done( true );
                rReq.SetReturnValue(SfxBoolItem(rReq.GetSlot(), true));
                return;
            }
            else
            {
                // Record as not done
                rReq.Done();
                rReq.SetReturnValue(SfxBoolItem(rReq.GetSlot(), false));
                m_pImpl->bReloading = false;
                return;
            }
        }
    }
}

void SfxViewFrame::StateReload_Impl( SfxItemSet& rSet )
{
    SfxObjectShell* pSh = GetObjectShell();
    if ( !pSh )
    {
        // I'm just on reload and am yielding myself ...
        return;
    }

    SfxWhichIter aIter( rSet );
    for ( sal_uInt16 nWhich = aIter.FirstWhich(); nWhich; nWhich = aIter.NextWhich() )
    {
        switch ( nWhich )
        {
            case SID_EDITDOC:
            case SID_READONLYDOC:
            {
                const SfxViewShell *pVSh;
                const SfxShell *pFSh;
                if ( !pSh->HasName() ||
                     !( pSh->Get_Impl()->nLoadedFlags &  SfxLoadedFlags::MAINDOCUMENT ) ||
                     (pSh->isEditDocLocked()) ||
                     ( pSh->GetCreateMode() == SfxObjectCreateMode::EMBEDDED &&
                       ( !(pVSh = pSh->GetViewShell())  ||
                         !(pFSh = pVSh->GetFormShell()) ||
                         !pFSh->IsDesignMode())))
                    rSet.DisableItem( nWhich );
                else
                {
                    SfxMedium* pMedium = pSh->GetMedium();
                    const SfxBoolItem* pItem = pMedium ? pMedium->GetItemSet().GetItem(SID_EDITDOC, false) : nullptr;
                    if ( pItem && !pItem->GetValue() )
                        rSet.DisableItem( nWhich );
                    else
                    {
                        if (nWhich==SID_EDITDOC)
                            rSet.Put( SfxBoolItem( nWhich, !pSh->IsReadOnly() ) );
                        else if (nWhich==SID_READONLYDOC)
                            rSet.Put( SfxBoolItem( nWhich, pSh->IsReadOnly() ) );
                    }
                }
                break;
            }

            case SID_RELOAD:
            {
                if ( !pSh->CanReload_Impl() || pSh->GetCreateMode() == SfxObjectCreateMode::EMBEDDED )
                    rSet.DisableItem(nWhich);
                else
                {
                    // If any ChildFrame is reloadable, the slot is enabled,
                    // so you can perform CTRL-Reload
                    rSet.Put( SfxBoolItem( nWhich, false));
                }

                break;
            }
        }
    }
}

void SfxViewFrame::ExecHistory_Impl( SfxRequest &rReq )
{
    // Is there an Undo-Manager on the top Shell?
    SfxShell *pSh = GetDispatcher()->GetShell(0);
    if (!pSh)
        return;

    SfxUndoManager* pShUndoMgr = pSh->GetUndoManager();
    bool bOK = false;
    if ( pShUndoMgr )
    {
        switch ( rReq.GetSlot() )
        {
            case SID_CLEARHISTORY:
                pShUndoMgr->Clear();
                bOK = true;
                break;

            case SID_UNDO:
                pShUndoMgr->Undo();
                GetBindings().InvalidateAll(false);
                bOK = true;
                break;

            case SID_REDO:
                pShUndoMgr->Redo();
                GetBindings().InvalidateAll(false);
                bOK = true;
                break;

            case SID_REPEAT:
                if ( pSh->GetRepeatTarget() )
                    pShUndoMgr->Repeat( *pSh->GetRepeatTarget() );
                bOK = true;
                break;
        }
    }
    else if ( GetViewShell() )
    {
        // The SW has its own undo in the View
        const SfxPoolItemHolder& rResult(GetViewShell()->ExecuteSlot(rReq));
        if (rResult)
            bOK = static_cast<const SfxBoolItem*>(rResult.getItem())->GetValue();
    }

    rReq.SetReturnValue( SfxBoolItem( rReq.GetSlot(), bOK ) );
    rReq.Done();
}

void SfxViewFrame::StateHistory_Impl( SfxItemSet &rSet )
{
    // Search for Undo-Manager
    SfxShell *pSh = GetDispatcher()->GetShell(0);
    if ( !pSh )
        // I'm just on reload and am yielding myself ...
        return;

    SfxUndoManager *pShUndoMgr = pSh->GetUndoManager();
    if ( !pShUndoMgr )
    {
        // The SW has its own undo in the View
        SfxWhichIter aIter( rSet );
        SfxViewShell *pViewSh = GetViewShell();
        if( !pViewSh ) return;
        for ( sal_uInt16 nSID = aIter.FirstWhich(); nSID; nSID = aIter.NextWhich() )
            pViewSh->GetSlotState( nSID, nullptr, &rSet );
        return;
    }

    if ( pShUndoMgr->GetUndoActionCount() == 0 &&
         pShUndoMgr->GetRedoActionCount() == 0 &&
         pShUndoMgr->GetRepeatActionCount() == 0 )
        rSet.DisableItem( SID_CLEARHISTORY );

    if (pShUndoMgr->GetUndoActionCount())
    {
        const SfxUndoAction* pAction = pShUndoMgr->GetUndoAction();
        SfxViewShell *pViewSh = GetViewShell();
        if (pViewSh && pAction->GetViewShellId() != pViewSh->GetViewShellId())
        {
            rSet.Put(SfxUInt32Item(SID_UNDO, static_cast<sal_uInt32>(SID_REPAIRPACKAGE)));
        }
        else
        {
            rSet.Put( SfxStringItem( SID_UNDO, SvtResId(STR_UNDO)+pShUndoMgr->GetUndoActionComment() ) );
        }
    }
    else
        rSet.DisableItem( SID_UNDO );

    if (pShUndoMgr->GetRedoActionCount())
    {
        const SfxUndoAction* pAction = pShUndoMgr->GetRedoAction();
        SfxViewShell *pViewSh = GetViewShell();
        if (pViewSh && pAction->GetViewShellId() != pViewSh->GetViewShellId())
        {
            rSet.Put(SfxUInt32Item(SID_REDO, static_cast<sal_uInt32>(SID_REPAIRPACKAGE)));
        }
        else
        {
            rSet.Put(SfxStringItem(SID_REDO, SvtResId(STR_REDO) + pShUndoMgr->GetRedoActionComment()));
        }
    }
    else
        rSet.DisableItem( SID_REDO );

    SfxRepeatTarget *pTarget = pSh->GetRepeatTarget();
    if (pTarget && pShUndoMgr->GetRepeatActionCount() && pShUndoMgr->CanRepeat(*pTarget))
        rSet.Put( SfxStringItem( SID_REPEAT, SvtResId(STR_REPEAT)+pShUndoMgr->GetRepeatActionComment(*pTarget) ) );
    else
        rSet.DisableItem( SID_REPEAT );
}

void SfxViewFrame::PopShellAndSubShells_Impl( SfxViewShell& i_rViewShell )
{
    i_rViewShell.PopSubShells_Impl();
    sal_uInt16 nLevel = m_pDispatcher->GetShellLevel( i_rViewShell );
    if ( nLevel != USHRT_MAX )
    {
        if ( nLevel )
        {
            // more sub shells on the stack, which were not affected by PopSubShells_Impl
            if (SfxShell *pSubShell = m_pDispatcher->GetShell( nLevel-1 ))
                m_pDispatcher->Pop( *pSubShell, SfxDispatcherPopFlags::POP_UNTIL | SfxDispatcherPopFlags::POP_DELETE );
        }
        m_pDispatcher->Pop( i_rViewShell );
        m_pDispatcher->Flush();
    }

}

/*  [Description]

    This method empties the SfxViewFrame, i.e. takes the <SfxObjectShell>
    from the dispatcher and ends its <SfxListener> Relationship to this
    SfxObjectShell (by which they may even destroy themselves).

    Thus, by invoking ReleaseObjectShell() and  SetObjectShell() the
    SfxObjectShell can be replaced.

    Between ReleaseObjectShell() and SetObjectShell() the control cannot
    be handed over to the system.

    [Cross-reference]

    <SfxViewFrame::SetObjectShell(SfxObjectShell&)>
*/
void SfxViewFrame::ReleaseObjectShell_Impl()
{
    DBG_ASSERT( m_xObjSh.is(), "no SfxObjectShell to release!" );

    GetFrame().ReleasingComponent_Impl();
    if ( GetWindow().HasChildPathFocus( true ) )
    {
        GetWindow().GrabFocus();
    }

    SfxViewShell *pDyingViewSh = GetViewShell();
    if ( pDyingViewSh )
    {
        PopShellAndSubShells_Impl( *pDyingViewSh );
        pDyingViewSh->DisconnectAllClients();
        SetViewShell_Impl(nullptr);
        delete pDyingViewSh;
    }
#ifdef DBG_UTIL
    else
        OSL_FAIL("No Shell");
#endif

    if ( m_xObjSh.is() )
    {
        m_pDispatcher->Pop( *m_xObjSh );
        SfxModule* pModule = m_xObjSh->GetModule();
        if( pModule )
            m_pDispatcher->RemoveShell_Impl( *pModule );
        m_pDispatcher->Flush();
        EndListening( *m_xObjSh );

        Notify( *m_xObjSh, SfxHint(SfxHintId::TitleChanged) );
        Notify( *m_xObjSh, SfxHint(SfxHintId::DocChanged) );

        if ( 1 == m_xObjSh->GetOwnerLockCount() && m_pImpl->bObjLocked && m_xObjSh->GetCreateMode() == SfxObjectCreateMode::EMBEDDED )
            m_xObjSh->DoClose();
        SfxObjectShellRef xDyingObjSh = m_xObjSh;
        m_xObjSh.clear();
        if( GetFrame().GetHasTitle() && m_pImpl->nDocViewNo )
            xDyingObjSh->GetNoSet_Impl().ReleaseIndex(m_pImpl->nDocViewNo-1);
        if ( m_pImpl->bObjLocked )
        {
            xDyingObjSh->OwnerLock( false );
            m_pImpl->bObjLocked = false;
        }
    }

    GetDispatcher()->SetDisableFlags( SfxDisableFlags::NONE );
}

void SfxViewFrame::Close()
{

    DBG_ASSERT( GetFrame().IsClosing_Impl() || !GetFrame().GetFrameInterface().is(), "ViewFrame closed too early!" );

    // If no saving have been made up until now, then embedded Objects should
    // not be saved automatically anymore.
    if ( GetViewShell() )
        GetViewShell()->DisconnectAllClients();
    Broadcast( SfxHint( SfxHintId::Dying ) );

    if (SfxViewFrame::Current() == this)
        SfxViewFrame::SetViewFrame( nullptr );

    // Since the Dispatcher is emptied, it can not be used in any reasonable
    // manner, thus it is better to let the dispatcher be.
    GetDispatcher()->Lock(true);
    delete this;
}

void SfxViewFrame::DoActivate( bool bUI )
{
    m_pDispatcher->DoActivate_Impl( bUI );
}

void SfxViewFrame::DoDeactivate(bool bUI, SfxViewFrame const * pNewFrame )
{
    m_pDispatcher->DoDeactivate_Impl( bUI, pNewFrame );
}

void SfxViewFrame::InvalidateBorderImpl( const SfxViewShell* pSh )
{
    if( !pSh || m_nAdjustPosPixelLock )
        return;

    if ( GetViewShell() && GetWindow().IsVisible() )
    {
        if ( GetFrame().IsInPlace() )
        {
            return;
        }

        DoAdjustPosSizePixel( GetViewShell(), Point(),
                                        GetWindow().GetOutputSizePixel(),
                                        false );
    }
}

void SfxViewFrame::SetBorderPixelImpl
(
    const SfxViewShell* pVSh,
    const SvBorder&     rBorder
)

{
    m_pImpl->aBorder = rBorder;

    if ( m_pImpl->bResizeInToOut && !GetFrame().IsInPlace() )
    {
        Size aSize = pVSh->GetWindow()->GetOutputSizePixel();
        if ( aSize.Width() && aSize.Height() )
        {
            aSize.AdjustWidth(rBorder.Left() + rBorder.Right() );
            aSize.AdjustHeight(rBorder.Top() + rBorder.Bottom() );

            Size aOldSize = GetWindow().GetOutputSizePixel();
            GetWindow().SetOutputSizePixel( aSize );
            vcl::Window* pParent = &GetWindow();
            while ( pParent->GetParent() )
                pParent = pParent->GetParent();
            Size aOuterSize = pParent->GetOutputSizePixel();
            aOuterSize.AdjustWidth( aSize.Width() - aOldSize.Width() );
            aOuterSize.AdjustHeight( aSize.Height() - aOldSize.Height() );
            pParent->SetOutputSizePixel( aOuterSize );
        }
    }
    else
    {
        tools::Rectangle aEditArea( Point(), GetWindow().GetOutputSizePixel() );
        aEditArea.AdjustLeft(rBorder.Left() );
        aEditArea.AdjustRight( -(rBorder.Right()) );
        aEditArea.AdjustTop(rBorder.Top() );
        aEditArea.AdjustBottom( -(rBorder.Bottom()) );
        pVSh->GetWindow()->SetPosSizePixel( aEditArea.TopLeft(), aEditArea.GetSize() );
    }
}

const SvBorder& SfxViewFrame::GetBorderPixelImpl() const
{
    return m_pImpl->aBorder;
}

void SfxViewFrame::AppendReadOnlyInfobar()
{
    if (officecfg::Office::Common::Misc::ViewerAppMode::get())
        return;

    bool bSignPDF = m_xObjSh->IsSignPDF();
    bool bSignWithCert = false;
    if (bSignPDF)
    {
        SfxViewShell* pViewShell = GetViewShell();
        uno::Reference<security::XCertificate> xCertificate = pViewShell->GetSignPDFCertificate().m_xCertificate;
        bSignWithCert = xCertificate.is();
    }

    auto pInfoBar = AppendInfoBar(u"readonly"_ustr, u""_ustr,
                                  SfxResId(bSignPDF ? STR_READONLY_PDF : STR_READONLY_DOCUMENT),
                                  InfobarType::INFO);
    if (!pInfoBar)
        return;

    if (bSignPDF)
    {
        // SID_SIGNPDF opened a read-write PDF
        // read-only for signing purposes.
        weld::Button& rSignButton = pInfoBar->addButton();
        if (bSignWithCert)
        {
            rSignButton.set_label(SfxResId(STR_READONLY_FINISH_SIGN));
        }
        else
        {
            rSignButton.set_label(SfxResId(STR_READONLY_SIGN));
        }

        rSignButton.connect_clicked(LINK(this, SfxViewFrame, SignDocumentHandler));
    }

    bool showEditDocumentButton = true;
    if (m_xObjSh->isEditDocLocked())
        showEditDocumentButton = false;

    if (showEditDocumentButton)
    {
        weld::Button& rBtn = pInfoBar->addButton();
        rBtn.set_label(SfxResId(STR_READONLY_EDIT));
        rBtn.connect_clicked(LINK(this, SfxViewFrame, SwitchReadOnlyHandler));
    }
}

void SfxViewFrame::HandleSecurityInfobar(const OUString& sSecondaryMessage)
{
    if (!HasInfoBarWithID(u"securitywarn"))
    {
        // new info bar
        if (!sSecondaryMessage.isEmpty())
        {
            auto pInfoBar = AppendInfoBar(u"securitywarn"_ustr, SfxResId(STR_HIDDENINFO_CONTAINS).replaceAll("\n\n", " "),
                sSecondaryMessage, InfobarType::WARNING);
            if (!pInfoBar)
                return;

            weld::Button& rGetInvolvedButton = pInfoBar->addButton();
            rGetInvolvedButton.set_label(SfxResId(STR_SECURITY_OPTIONS));
            rGetInvolvedButton.connect_clicked(LINK(this, SfxViewFrame, SecurityButtonHandler));
        }
    }
    else
    {
        // info bar exists already
        if (sSecondaryMessage.isEmpty())
        {
            RemoveInfoBar(u"securitywarn");
        }
        else
        {
            UpdateInfoBar(u"securitywarn", SfxResId(STR_HIDDENINFO_CONTAINS).replaceAll("\n\n", " "),
                sSecondaryMessage, InfobarType::WARNING);
        }
    }
}

void SfxViewFrame::AppendContainsMacrosInfobar()
{
    if (officecfg::Office::Common::Misc::ViewerAppMode::get())
        return;

    SfxObjectShell_Impl* pObjImpl = m_xObjSh->Get_Impl();

    auto aResId = STR_CONTAINS_MACROS;
    if (SvtSecurityOptions::IsMacroDisabled())
        aResId = STR_MACROS_DISABLED;
    else if (pObjImpl->aMacroMode.hasUnsignedContentError())
        aResId = STR_MACROS_DISABLED_CONTENT_UNSIGNED;
    else if(pObjImpl->aMacroMode.hasInvalidSignaturesError())
        aResId = STR_MACROS_DISABLED_SIGNATURE_INVALID;
    // The idea here is to always present an infobar is there was some
    // macro/script related potential hazard disabled in the source document
    auto pInfoBar = AppendInfoBar(u"macro"_ustr, SfxResId(STR_MACROS_DISABLED_TITLE),
                                  SfxResId(aResId), InfobarType::WARNING);
    if (!pInfoBar)
        return;

    // Then show buttons to help navigate to whatever that hazard is.  Whether
    // that is included macros, so the user could delete them.  Or events bound
    // to scripts which could be cleared.  But there are likely other cases not
    // captured here, which could be added, various blocked features where its
    // likely still worth displaying the infobar that they have been disabled,
    // even if we don't currently provide a way to indicate what exactly those
    // are and how to remove them.

    // No access to macro dialog when macros are disabled globally, so return
    // early without adding buttons to help explore what the macros/script/events
    // might be.
    if (SvtSecurityOptions::IsMacroDisabled())
        return;

    // what's the difference between pObjImpl->documentStorageHasMacros() and pObjImpl->aMacroMode.hasMacroLibrary() ?
    bool bHasDocumentMacros = pObjImpl->aMacroMode.hasMacroLibrary();

    Reference<XModel> xModel = m_xObjSh->GetModel();
    uno::Reference<document::XEventsSupplier> xSupplier(xModel, uno::UNO_QUERY);
    bool bHasBoundConfigEvents(false);
    if (xSupplier.is())
    {
        css::uno::Reference<css::container::XNameReplace> xDocumentEvents = xSupplier->getEvents();

        Sequence<OUString> eventNames = xDocumentEvents->getElementNames();
        sal_Int32 nEventCount = eventNames.getLength();
        for (sal_Int32 nEvent = 0; nEvent < nEventCount; ++nEvent)
        {
            OUString url;
            try
            {
                Any aAny(xDocumentEvents->getByName(eventNames[nEvent]));
                Sequence<beans::PropertyValue> props;
                if (aAny >>= props)
                {
                    ::comphelper::NamedValueCollection aProps(props);
                    url = aProps.getOrDefault(u"Script"_ustr, url);
                }
            }
            catch (const Exception&)
            {
            }
            if (!url.isEmpty())
            {
                bHasBoundConfigEvents = true;
                break;
            }
        }
    }

    if (bHasDocumentMacros)
    {
        weld::Button& rMacroButton = pInfoBar->addButton();
        rMacroButton.set_label(SfxResId(STR_MACROS));
        rMacroButton.connect_clicked(LINK(this, SfxViewFrame, MacroButtonHandler));
    }

    if (bHasBoundConfigEvents)
    {
        weld::Button& rEventButton = pInfoBar->addButton();
        rEventButton.set_label(SfxResId(STR_EVENTS));
        rEventButton.connect_clicked(LINK(this, SfxViewFrame, EventButtonHandler));
    }

    if (pObjImpl->aMacroMode.hasInvalidSignaturesError())
    {
        weld::Button& rSignaturesButton = pInfoBar->addButton();
        rSignaturesButton.set_label(SfxResId(STR_SIGNATURE_SHOW));
        rSignaturesButton.connect_clicked(LINK(this, SfxViewFrame, ViewSignaturesButtonHandler));
    }
}

namespace
{
css::uno::Reference<css::frame::XLayoutManager> getLayoutManager(const SfxFrame& rFrame)
{
    css::uno::Reference<css::frame::XLayoutManager> xLayoutManager;
    css::uno::Reference<css::beans::XPropertySet> xPropSet(rFrame.GetFrameInterface(),
                                                 uno::UNO_QUERY);
    if (xPropSet.is())
    {
        try
        {
            xLayoutManager.set(xPropSet->getPropertyValue(u"LayoutManager"_ustr), uno::UNO_QUERY);
        }
        catch (const Exception& e)
        {
            SAL_WARN("sfx.view", "Failure getting layout manager: " + e.Message);
        }
    }
    return xLayoutManager;
}
}

bool SfxApplication::IsHeadlessOrUITest()
{
    if (Application::IsHeadlessModeEnabled())
        return true;

    bool bRet = o3tl::IsRunningUITest(); //uitest.uicheck fails when the dialog is open
    for (sal_uInt16 i = 0, nCount = Application::GetCommandLineParamCount(); i < nCount; ++i)
    {
        if (Application::GetCommandLineParam(i) == "--nologo")
        {
            bRet = true;
            break;
        }
    }
    return bRet;
}

bool SfxApplication::IsTipOfTheDayDue()
{
    const bool bShowTipOfTheDay = officecfg::Office::Common::Misc::ShowTipOfTheDay::get();
    if (!bShowTipOfTheDay)
        return false;

    const auto t0 = std::chrono::system_clock::now().time_since_epoch();

    // show tip-of-the-day dialog ?
    const sal_Int32 nLastTipOfTheDay = officecfg::Office::Common::Misc::LastTipOfTheDayShown::get();
    const sal_Int32 nDay = std::chrono::duration_cast<std::chrono::hours>(t0).count()/24; // days since 1970-01-01
    return nDay - nLastTipOfTheDay > 0; //only once per day
}

void SfxViewFrame::Notify( SfxBroadcaster& /*rBC*/, const SfxHint& rHint )
{
    if(m_pImpl->bIsDowning)
        return;

    // we know only SfxEventHint or simple SfxHint
    if (rHint.GetId() == SfxHintId::ThisIsAnSfxEventHint)
    {
        // When the Document is loaded asynchronously, was the Dispatcher
        // set as ReadOnly, to what must be returned when the document itself
        // is not read only, and the loading is finished.
        switch (static_cast<const SfxEventHint&>(rHint).GetEventId())
        {
            case SfxEventHintId::ModifyChanged:
            {
                SfxBindings& rBind = GetBindings();
                rBind.Invalidate( SID_DOC_MODIFIED );
                rBind.Invalidate( SID_RELOAD );
                rBind.Invalidate( SID_EDITDOC );
                break;
            }

            case SfxEventHintId::OpenDoc:
            case SfxEventHintId::CreateDoc:
            {
                if ( !m_xObjSh.is() )
                    break;

                SfxBindings& rBind = GetBindings();
                rBind.Invalidate( SID_RELOAD );
                rBind.Invalidate( SID_EDITDOC );

                bool bIsInfobarShown(false);

                if (officecfg::Office::Common::Passwords::HasMaster::get() &&
                    officecfg::Office::Common::Passwords::StorageVersion::get() == 0)
                {
                    // master password stored in deprecated format
                    VclPtr<SfxInfoBarWindow> pOldMasterPasswordInfoBar =
                        AppendInfoBar(u"oldmasterpassword"_ustr, u""_ustr,
                                      SfxResId(STR_REFRESH_MASTER_PASSWORD), InfobarType::DANGER, false);
                    bIsInfobarShown = true;
                    if (pOldMasterPasswordInfoBar)
                    {
                        weld::Button& rButton = pOldMasterPasswordInfoBar->addButton();
                        rButton.set_label(SfxResId(STR_REFRESH_PASSWORD));
                        rButton.connect_clicked(LINK(this,
                                                   SfxViewFrame, RefreshMasterPasswordHdl));
                        if (Application::GetHelp())
                        {
                            weld::Button& rHelp = pOldMasterPasswordInfoBar->addButton();
                            rHelp.set_label(SfxResId(RID_STR_HELP));
                            rHelp.connect_clicked(LINK(this, SfxViewFrame, HelpMasterPasswordHdl));
                        }
                    }
                }

                const bool bEmbedded = m_xObjSh->GetCreateMode() == SfxObjectCreateMode::EMBEDDED;

                // read-only infobar if necessary
                const SfxViewShell *pVSh;
                const SfxShell *pFSh;
                if ( m_xObjSh->IsReadOnly() &&
                    ! m_xObjSh->IsSecurityOptOpenReadOnly() &&
                    ( !bEmbedded ||
                        (( pVSh = m_xObjSh->GetViewShell()) && (pFSh = pVSh->GetFormShell()) && pFSh->IsDesignMode())))
                {
                    AppendReadOnlyInfobar();
                    bIsInfobarShown = true;
                }

                if (!bEmbedded && m_xObjSh->Get_Impl()->getCurrentMacroExecMode() == css::document::MacroExecMode::NEVER_EXECUTE)
                {
                    AppendContainsMacrosInfobar();
                    bIsInfobarShown = true;
                }

                if (vcl::CommandInfoProvider::GetModuleIdentifier(GetFrame().GetFrameInterface()) == "com.sun.star.text.TextDocument")
                    sfx2::SfxNotebookBar::ReloadNotebookBar(u"modules/swriter/ui/");

                if (SfxClassificationHelper::IsClassified(m_xObjSh->getDocProperties()))
                {
                    // Document has BAILS properties, display an infobar accordingly.
                    SfxClassificationHelper aHelper(m_xObjSh->getDocProperties());
                    aHelper.UpdateInfobar(*this);
                }

                // Add pending infobars
                std::vector<InfobarData>& aPendingInfobars = m_xObjSh->getPendingInfobars();
                while (!aPendingInfobars.empty())
                {
                    InfobarData& aInfobarData = aPendingInfobars.back();

                    // don't show Track Changes infobar, if Track Changes toolbar is visible
                    if (aInfobarData.msId == "hiddentrackchanges")
                    {
                        if (auto xLayoutManager = getLayoutManager(GetFrame()))
                        {
                            if ( xLayoutManager->getElement(CHANGES_STR).is() )
                            {
                                aPendingInfobars.pop_back();
                                continue;
                            }
                        }
                    }

                    // Track Changes infobar: add a button to show/hide Track Changes functions
                    // Hyphenation infobar: add a button to get more information
                    // tdf#148913 limit VclPtr usage for these
                    bool bTrackChanges = aInfobarData.msId == "hiddentrackchanges";
                    if ( bTrackChanges || aInfobarData.msId == "hyphenationmissing" )
                    {
                        VclPtr<SfxInfoBarWindow> pInfoBar =
                            AppendInfoBar(aInfobarData.msId, aInfobarData.msPrimaryMessage,
                                  aInfobarData.msSecondaryMessage, aInfobarData.maInfobarType,
                                  aInfobarData.mbShowCloseButton);
                        bIsInfobarShown = true;

                        // tdf#148913 don't extend this condition to keep it thread-safe
                        if (pInfoBar)
                        {
                            weld::Button& rButton = pInfoBar->addButton();
                            rButton.set_label(SfxResId(bTrackChanges
                                    ? STR_TRACK_CHANGES_BUTTON
                                    : STR_HYPHENATION_BUTTON));
                            if (bTrackChanges)
                            {
                                rButton.connect_clicked(LINK(this,
                                                    SfxViewFrame, HiddenTrackChangesHandler));
                            }
                            else
                            {
                                rButton.connect_clicked(LINK(this,
                                                    SfxViewFrame, HyphenationMissingHandler));
                            }
                        }
                    }
                    else
                    {
                        AppendInfoBar(aInfobarData.msId, aInfobarData.msPrimaryMessage,
                                  aInfobarData.msSecondaryMessage, aInfobarData.maInfobarType,
                                  aInfobarData.mbShowCloseButton);
                        bIsInfobarShown = true;
                    }

                    aPendingInfobars.pop_back();
                }

#if !ENABLE_WASM_STRIP_PINGUSER
                if (!SfxApplication::IsHeadlessOrUITest()) //uitest.uicheck fails when the dialog is open
                {
                    bool bIsWhatsNewShown = false; //suppress tipoftheday if whatsnew was shown

                    static const bool bRunningUnitTest = o3tl::IsRunningUnitTest() || o3tl::IsRunningUITest();
                    //what's new dialog
                    static bool wantsWhatsNew = officecfg::Setup::Product::WhatsNew::get()
                                                && !IsInModalMode() && !bRunningUnitTest
                                                && utl::isProductVersionUpgraded(); //sets isProductVersionNew
                    if (wantsWhatsNew)
                    {
                        wantsWhatsNew = false;

                        if (utl::isProductVersionNew()) //welcome dialog
                        {
                            SfxAbstractDialogFactory* pFact = SfxAbstractDialogFactory::Create();
                            ScopedVclPtr<SfxAbstractTabDialog> pDlg(
                                pFact->CreateWelcomeDialog(GetWindow().GetFrameWeld(), true));
                            pDlg->Execute();
                        }
                        else if (officecfg::Setup::Product::WhatsNewDialog::get()) //whatsnew dialog
                        {
                            SfxAbstractDialogFactory* pFact = SfxAbstractDialogFactory::Create();
                            ScopedVclPtr<SfxAbstractTabDialog> pDlg(
                                pFact->CreateWelcomeDialog(GetWindow().GetFrameWeld(), false));
                            pDlg->Execute();
                        }
                        else //whatsnew infobar
                        {
                            OUString sText(SfxResId(STR_WHATSNEW_TEXT));
                            VclPtr<SfxInfoBarWindow> pInfoBar = AppendInfoBar(u"whatsnew"_ustr, u""_ustr, sText.replaceAll("\n",""), InfobarType::INFO);
                            if (pInfoBar)
                            {
                                weld::Button& rWhatsNewButton = pInfoBar->addButton();
                                rWhatsNewButton.set_label(SfxResId(STR_WHATSNEW_BUTTON));
                                rWhatsNewButton.connect_clicked(LINK(this, SfxViewFrame, WhatsNewHandler));
                            }
                        }
                        bIsInfobarShown = true;
                        bIsWhatsNewShown = true;
                    }

                    // show tip-of-the-day dialog if it due, but not if there is the impress modal template dialog
                    // open where SdModule::ExecuteNewDocument will launch it instead when that dialog is dismissed
                    if (SfxApplication::IsTipOfTheDayDue() && !IsInModalMode() && !bIsWhatsNewShown)
                    {
                        bool bIsBaseFormOpen = false;

                        const auto xCurrentFrame = GetFrame().GetFrameInterface();
                        const auto& xContext = comphelper::getProcessComponentContext();
                        const auto xModuleManager = css::frame::ModuleManager::create(xContext);
                        switch (vcl::EnumContext::GetApplicationEnum(
                            vcl::CommandInfoProvider::GetModuleIdentifier(xCurrentFrame)))
                        {
                            case vcl::EnumContext::Application::WriterForm:
                            case vcl::EnumContext::Application::WriterReport:
                                bIsBaseFormOpen = true;
                                break;
                            default:
                                break;
                        }
                        if (!bIsBaseFormOpen)
                        {
                            // tdf#127946 pass in argument for dialog parent
                            SfxUnoFrameItem aDocFrame(SID_FILLFRAME, xCurrentFrame);
                            GetDispatcher()->ExecuteList(SID_TIPOFTHEDAY, SfxCallMode::SLOT, {},
                                                        { &aDocFrame });
                        }
                    }

                    // inform about the community involvement
                    const auto t0 = std::chrono::system_clock::now().time_since_epoch();
                    const sal_Int64 nLastGetInvolvedShown = officecfg::Setup::Product::LastTimeGetInvolvedShown::get();
                    const sal_Int64 nNow = std::chrono::duration_cast<std::chrono::seconds>(t0).count();
                    const sal_Int64 nPeriodSec(60 * 60 * 24 * 180); // 180 days in seconds
                    bool bUpdateLastTimeGetInvolvedShown = false;

                    if (nLastGetInvolvedShown == 0)
                        bUpdateLastTimeGetInvolvedShown = true;
                    else if (!bIsInfobarShown && nPeriodSec < nNow && nLastGetInvolvedShown < (nNow + nPeriodSec/2) - nPeriodSec) // 90d alternating with donation
                    {
                        bUpdateLastTimeGetInvolvedShown = true;

                        VclPtr<SfxInfoBarWindow> pInfoBar = AppendInfoBar(u"getinvolved"_ustr, u""_ustr, SfxResId(STR_GET_INVOLVED_TEXT), InfobarType::INFO);
                        bIsInfobarShown = true;
                        if (pInfoBar)
                        {
                            weld::Button& rGetInvolvedButton = pInfoBar->addButton();
                            rGetInvolvedButton.set_label(SfxResId(STR_GET_INVOLVED_BUTTON));
                            rGetInvolvedButton.connect_clicked(LINK(this, SfxViewFrame, GetInvolvedHandler));
                        }
                    }

                    if (bUpdateLastTimeGetInvolvedShown
                        && !officecfg::Setup::Product::LastTimeGetInvolvedShown::isReadOnly())
                    {
                        std::shared_ptr<comphelper::ConfigurationChanges> batch(comphelper::ConfigurationChanges::create());
                        officecfg::Setup::Product::LastTimeGetInvolvedShown::set(nNow, batch);
                        batch->commit();
                    }

                    // inform about donations
                    const sal_Int64 nLastDonateShown = officecfg::Setup::Product::LastTimeDonateShown::get();
                    bool bUpdateLastTimeDonateShown = false;

                    if (nLastDonateShown == 0)
                        bUpdateLastTimeDonateShown = true;
                    else if (!bIsInfobarShown && nPeriodSec < nNow && nLastDonateShown < nNow - nPeriodSec) // 90d alternating with getinvolved
                    {
                        bUpdateLastTimeDonateShown = true;

                        VclPtr<SfxInfoBarWindow> pInfoBar = AppendInfoBar(u"donate"_ustr, u""_ustr, SfxResId(STR_DONATE_TEXT), InfobarType::INFO);
                        if (pInfoBar)
                        {
                            weld::Button& rDonateButton = pInfoBar->addButton();
                            rDonateButton.set_label(SfxResId(STR_DONATE_BUTTON));
                            rDonateButton.connect_clicked(LINK(this, SfxViewFrame, DonationHandler));
                        }
                    }

                    if (bUpdateLastTimeDonateShown
                        && !officecfg::Setup::Product::LastTimeDonateShown::isReadOnly())
                    {
                        std::shared_ptr<comphelper::ConfigurationChanges> batch(comphelper::ConfigurationChanges::create());
                        officecfg::Setup::Product::LastTimeDonateShown::set(nNow, batch);
                        batch->commit();
                    }
                }
#else
                (void) bIsInfobarShown;
#endif

                break;
            }
            default: break;
        }
    }
    else
    {
        switch( rHint.GetId() )
        {
            case SfxHintId::ModeChanged:
            {
                UpdateTitle();

                if ( !m_xObjSh.is() )
                    break;

                // Switch r/o?
                SfxBindings& rBind = GetBindings();
                rBind.Invalidate( SID_RELOAD );
                SfxDispatcher *pDispat = GetDispatcher();
                bool bWasReadOnly = pDispat->GetReadOnly_Impl();
                bool bIsReadOnly = m_xObjSh->IsReadOnly();
                if ( bWasReadOnly != bIsReadOnly )
                {
                    // Then also TITLE_CHANGED
                    UpdateTitle();
                    rBind.Invalidate( SID_FILE_NAME );
                    rBind.Invalidate( SID_DOCINFO_TITLE );
                    rBind.Invalidate( SID_EDITDOC );

                    pDispat->GetBindings()->InvalidateAll(true);
                    pDispat->SetReadOnly_Impl( bIsReadOnly );

                    // Only force and Dispatcher-Update, if it is done next
                    // anyway, otherwise flickering or GPF is possible since
                    // the Writer for example prefers in Resize perform some
                    // actions which has a SetReadOnlyUI in Dispatcher as a
                    // result!

                    if ( pDispat->IsUpdated_Impl() )
                        pDispat->Update_Impl(true);
                }

                Enable( !m_xObjSh->IsInModalMode() );
                break;
            }

            case SfxHintId::TitleChanged:
            {
                UpdateTitle();
                SfxBindings& rBind = GetBindings();
                rBind.Invalidate( SID_FILE_NAME );
                rBind.Invalidate( SID_DOCINFO_TITLE );
                rBind.Invalidate( SID_EDITDOC );
                rBind.Invalidate( SID_RELOAD );
                break;
            }

            case SfxHintId::DocumentRepair:
            {
                GetBindings().Invalidate( SID_DOC_REPAIR );
                break;
            }

            case SfxHintId::Deinitializing:
            {
                vcl::Window* pFrameWin = GetWindow().GetFrameWindow();
                if (pFrameWin && pFrameWin->GetLOKNotifier())
                    pFrameWin->ReleaseLOKNotifier();

                GetFrame().DoClose();
                break;
            }
            case SfxHintId::Dying:
                // when the Object is being deleted, destroy the view too
                if ( m_xObjSh.is() )
                    ReleaseObjectShell_Impl();
                else
                    GetFrame().DoClose();
                break;
            default: break;
        }
    }
}

#if !ENABLE_WASM_STRIP_PINGUSER
IMPL_LINK_NOARG(SfxViewFrame, WhatsNewHandler, weld::Button&, void)
{
    GetDispatcher()->Execute(SID_WHATSNEW);
}

IMPL_LINK_NOARG(SfxViewFrame, GetInvolvedHandler, weld::Button&, void)
{
    GetDispatcher()->Execute(SID_GETINVOLVED);
}

IMPL_LINK_NOARG(SfxViewFrame, DonationHandler, weld::Button&, void)
{
    GetDispatcher()->Execute(SID_DONATION);
}
#endif

IMPL_LINK(SfxViewFrame, SwitchReadOnlyHandler, weld::Button&, rButton, void)
{
    if (m_xObjSh.is() && m_xObjSh->IsSignPDF())
    {
        SfxEditDocumentDialog aDialog(&rButton);
        if (aDialog.run() != RET_OK)
            return;
    }
    GetDispatcher()->Execute(SID_EDITDOC);
}

IMPL_LINK_NOARG(SfxViewFrame, SignDocumentHandler, weld::Button&, void)
{
    GetDispatcher()->Execute(SID_SIGNATURE);
}

IMPL_LINK(SfxViewFrame, HiddenTrackChangesHandler, weld::Button&, rButton, void)
{
    // enable Track Changes toolbar, if it is disabled.
    // Otherwise disable the toolbar, and close the infobar
    auto xLayoutManager = getLayoutManager(GetFrame());
    if (!xLayoutManager)
        return;

    if (!xLayoutManager->getElement(CHANGES_STR).is())
    {
        xLayoutManager->createElement(CHANGES_STR);
        xLayoutManager->showElement(CHANGES_STR);
        rButton.set_label(SfxResId(STR_TRACK_CHANGES_BUTTON_HIDE));
    }
    else
    {
        xLayoutManager->hideElement(CHANGES_STR);
        xLayoutManager->destroyElement(CHANGES_STR);
        RemoveInfoBar(u"hiddentrackchanges");
    }
}

IMPL_LINK_NOARG(SfxViewFrame, HyphenationMissingHandler, weld::Button&, void)
{
    GetDispatcher()->Execute(SID_HYPHENATIONMISSING);
    RemoveInfoBar(u"hyphenationmissing");
}

IMPL_LINK_NOARG(SfxViewFrame, MacroButtonHandler, weld::Button&, void)
{
    // start with tab 0 displayed
    SfxUInt16Item aTabItem(SID_MACROORGANIZER, 0);
    SfxBoolItem aCurrentDocItem(FN_PARAM_2, true);
    SfxUnoFrameItem aDocFrame(SID_FILLFRAME, GetFrame().GetFrameInterface());
    GetDispatcher()->ExecuteList(SID_MACROORGANIZER, SfxCallMode::ASYNCHRON,
                                 { &aTabItem, &aCurrentDocItem }, { &aDocFrame });
}

IMPL_LINK_NOARG(SfxViewFrame, SecurityButtonHandler, weld::Button&, void)
{
    GetDispatcher()->Execute(SID_OPTIONS_SECURITY, SfxCallMode::SYNCHRON);
    RemoveInfoBar(u"securitywarn");
}

IMPL_LINK_NOARG(SfxViewFrame, EventButtonHandler, weld::Button&, void)
{
    SfxUnoFrameItem aDocFrame(SID_FILLFRAME, GetFrame().GetFrameInterface());
    GetDispatcher()->ExecuteList(SID_CONFIGEVENT, SfxCallMode::ASYNCHRON,
                                 {}, { &aDocFrame });
}

IMPL_LINK_NOARG(SfxViewFrame, ViewSignaturesButtonHandler, weld::Button&, void)
{
    SfxObjectShell* pDoc = GetObjectShell();
    if (!pDoc)
        return;

    SfxMedium* pMedium = pDoc->GetMedium();
    if (!pMedium)
        return;

    OUString maODFVersion{};
    try
    {
        uno::Reference<beans::XPropertySet> xPropSet(pDoc->GetStorage(), uno::UNO_QUERY_THROW);
        xPropSet->getPropertyValue(u"Version"_ustr) >>= maODFVersion;
    }
    catch (uno::Exception&)
    {
    }

    uno::Reference<security::XDocumentDigitalSignatures> xDigitalSignatures(
        security::DocumentDigitalSignatures::createWithVersion(
            comphelper::getProcessComponentContext(), maODFVersion));

    if (!xDigitalSignatures.is())
        return;

    if (auto xScriptingStorage = pMedium->GetScriptingStorageToSign_Impl())
        xDigitalSignatures->showScriptingContentSignatures(xScriptingStorage, {});
}

IMPL_LINK_NOARG(SfxViewFrame, RefreshMasterPasswordHdl, weld::Button&, void)
{
    bool bChanged = false;
    try
    {
        Reference< task::XPasswordContainer2 > xMasterPasswd(
            task::PasswordContainer::create(comphelper::getProcessComponentContext()));

        css::uno::Reference<css::frame::XFrame> xFrame = GetFrame().GetFrameInterface();
        css::uno::Reference<css::awt::XWindow> xContainerWindow = xFrame->getContainerWindow();

        uno::Reference<task::XInteractionHandler> xTmpHandler(task::InteractionHandler::createWithParent(comphelper::getProcessComponentContext(),
                                                              xContainerWindow));
        bChanged = xMasterPasswd->changeMasterPassword(xTmpHandler);
    }
    catch (const Exception&)
    {}
    if (bChanged)
        RemoveInfoBar(u"oldmasterpassword");
}

IMPL_STATIC_LINK_NOARG(SfxViewFrame, HelpMasterPasswordHdl, weld::Button&, void)
{
    if (Help* pHelp = Application::GetHelp())
        pHelp->Start(u"cui/ui/optsecuritypage/savepassword"_ustr);
}

void SfxViewFrame::Construct_Impl( SfxObjectShell *pObjSh )
{
    m_pImpl->bResizeInToOut = true;
    m_pImpl->bObjLocked = false;
    m_pImpl->nCurViewId = SFX_INTERFACE_NONE;
    m_pImpl->bReloading = false;
    m_pImpl->bIsDowning = false;
    m_pImpl->bModal = false;
    m_pImpl->bEnabled = true;
    m_pImpl->nDocViewNo = 0;
    m_pImpl->aMargin = Size( -1, -1 );
    m_pImpl->pWindow = nullptr;

    SetPool( &SfxGetpApp()->GetPool() );
    m_pDispatcher.reset( new SfxDispatcher(this) );
    if ( !GetBindings().GetDispatcher() )
        GetBindings().SetDispatcher( m_pDispatcher.get() );

    m_xObjSh = pObjSh;
    if ( m_xObjSh.is() && m_xObjSh->IsPreview() )
        GetDispatcher()->SetQuietMode_Impl( true );

    if ( pObjSh )
    {
        m_pDispatcher->Push( *SfxGetpApp() );
        SfxModule* pModule = m_xObjSh->GetModule();
        if( pModule )
            m_pDispatcher->Push( *pModule );
        m_pDispatcher->Push( *this );
        m_pDispatcher->Push( *pObjSh );
        m_pDispatcher->Flush();
        StartListening( *pObjSh );
        Notify( *pObjSh, SfxHint(SfxHintId::TitleChanged) );
        Notify( *pObjSh, SfxHint(SfxHintId::DocChanged) );
        m_pDispatcher->SetReadOnly_Impl( pObjSh->IsReadOnly() );
    }
    else
    {
        m_pDispatcher->Push( *SfxGetpApp() );
        m_pDispatcher->Push( *this );
        m_pDispatcher->Flush();
    }

    SfxGetpApp()->GetViewFrames_Impl().push_back(this);
}

/*  [Description]

    Constructor of SfxViewFrame for a <SfxObjectShell> from the Resource.
    The 'nViewId' to the created <SfxViewShell> can be returned.
    (default is the SfxViewShell-Subclass that was registered first).
*/
SfxViewFrame::SfxViewFrame
(
    SfxFrame&           rFrame,
    SfxObjectShell*     pObjShell
)
    : m_pImpl( new SfxViewFrame_Impl( rFrame ) )
    , m_pBindings( new SfxBindings )
    , m_pHelpData(CreateSVHelpData())
    , m_pWinData(CreateSVWinData())
    , m_nAdjustPosPixelLock( 0 )
    , m_pCommandPopupHandler(new CommandPopupHandler)
{

    rFrame.SetCurrentViewFrame_Impl( this );
    rFrame.SetHasTitle( true );
    Construct_Impl( pObjShell );

    m_pImpl->pWindow = VclPtr<SfxFrameViewWindow_Impl>::Create( this, rFrame.GetWindow() );
    m_pImpl->pWindow->SetSizePixel( rFrame.GetWindow().GetOutputSizePixel() );
    rFrame.SetOwnsBindings_Impl( true );
    rFrame.CreateWorkWindow_Impl();
}

SfxViewFrame::~SfxViewFrame()
{
    m_pImpl->bIsDowning = true;

    if ( SfxViewFrame::Current() == this )
        SfxViewFrame::SetViewFrame( nullptr );

    ReleaseObjectShell_Impl();

    if ( GetFrame().OwnsBindings_Impl() )
        // The Bindings delete the Frame!
        KillDispatcher_Impl();

    m_pImpl->pWindow.disposeAndClear();

    if ( GetFrame().GetCurrentViewFrame() == this )
        GetFrame().SetCurrentViewFrame_Impl( nullptr );

    // Unregister from the Frame List.
    SfxApplication *pSfxApp = SfxApplication::Get();
    if (pSfxApp)
    {
        auto &rFrames = pSfxApp->GetViewFrames_Impl();
        auto it = std::find( rFrames.begin(), rFrames.end(), this );
        rFrames.erase( it );
    }

    // Delete Member
    KillDispatcher_Impl();

    DestroySVHelpData(m_pHelpData);
    m_pHelpData = nullptr;

    DestroySVWinData(m_pWinData);
    m_pWinData = nullptr;
}

// Remove and delete the Dispatcher.
void SfxViewFrame::KillDispatcher_Impl()
{

    SfxModule* pModule = m_xObjSh.is() ? m_xObjSh->GetModule() : nullptr;
    if ( m_xObjSh.is() )
        ReleaseObjectShell_Impl();
    if ( m_pDispatcher )
    {
        if( pModule )
            m_pDispatcher->Pop( *pModule, SfxDispatcherPopFlags::POP_UNTIL );
        else
            m_pDispatcher->Pop( *this );
        m_pDispatcher.reset();
    }
}

SfxViewFrame* SfxViewFrame::Current()
{
    SfxApplication* pApp = SfxApplication::Get();
    return pApp ? pApp->Get_Impl()->pViewFrame : nullptr;
}

// returns the first window of spec. type viewing the specified doc.
SfxViewFrame* SfxViewFrame::GetFirst
(
    const SfxObjectShell*   pDoc,
    bool                    bOnlyIfVisible
)
{
    SfxApplication *pSfxApp = SfxApplication::Get();
    if (!pSfxApp)
        return nullptr;

    // search for a SfxDocument of the specified type
    for (SfxViewFrame* pFrame : pSfxApp->GetViewFrames_Impl())
    {
        if  (   ( !pDoc || pDoc == pFrame->GetObjectShell() )
            &&  ( !bOnlyIfVisible || pFrame->IsVisible() )
            )
            return pFrame;
    }

    return nullptr;
}

// returns the next window of spec. type viewing the specified doc.
SfxViewFrame* SfxViewFrame::GetNext
(
    const SfxViewFrame&     rPrev,
    const SfxObjectShell*   pDoc,
    bool                    bOnlyIfVisible
)
{
    SfxApplication *pSfxApp = SfxApplication::Get();
    if (!pSfxApp)
        return nullptr;

    auto &rFrames = pSfxApp->GetViewFrames_Impl();

    // refind the specified predecessor
    size_t nPos;
    for ( nPos = 0; nPos < rFrames.size(); ++nPos )
        if ( rFrames[nPos] == &rPrev )
            break;

    // search for a Frame of the specified type
    for ( ++nPos; nPos < rFrames.size(); ++nPos )
    {
        SfxViewFrame *pFrame = rFrames[nPos];
        if  (   ( !pDoc || pDoc == pFrame->GetObjectShell() )
            &&  ( !bOnlyIfVisible || pFrame->IsVisible() )
            )
            return pFrame;
    }
    return nullptr;
}

SfxProgress* SfxViewFrame::GetProgress() const
{
    SfxObjectShell *pObjSh = m_xObjSh.get();
    return pObjSh ? pObjSh->GetProgress() : nullptr;
}

void SfxViewFrame::DoAdjustPosSizePixel //! divide on Inner.../Outer...
(
    SfxViewShell*   pSh,
    const Point&    rPos,
    const Size&     rSize,
    bool inplaceEditModeChange
)
{

    // Components do not use this Method!
    if( pSh && pSh->GetWindow() && !m_nAdjustPosPixelLock )
    {
        m_nAdjustPosPixelLock++;
        if ( m_pImpl->bResizeInToOut )
            pSh->InnerResizePixel( rPos, rSize, inplaceEditModeChange );
        else
            pSh->OuterResizePixel( rPos, rSize );
        m_nAdjustPosPixelLock--;
    }
}

bool SfxViewFrameItem::operator==( const SfxPoolItem &rItem ) const
{
    return SfxPoolItem::operator==(rItem) &&
        static_cast<const SfxViewFrameItem&>(rItem).pFrame == pFrame;
}

SfxViewFrameItem* SfxViewFrameItem::Clone( SfxItemPool *) const
{
    return new SfxViewFrameItem( *this );
}

void SfxViewFrame::SetViewShell_Impl( SfxViewShell *pVSh )
/*  [Description]

    Internal Method to set the current <SfxViewShell> Instance,
    that is active int this SfxViewFrame at the moment.
*/
{
    SfxShell::SetViewShell_Impl( pVSh );

    // Hack: InPlaceMode
    if ( pVSh )
        m_pImpl->bResizeInToOut = false;
}

void SfxViewFrame::ForceOuterResize_Impl()
{
    m_pImpl->bResizeInToOut = true;
}

void SfxViewFrame::GetDocNumber_Impl()
{
    DBG_ASSERT( GetObjectShell(), "No Document!" );
    GetObjectShell()->SetNamedVisibility_Impl();
    m_pImpl->nDocViewNo = GetObjectShell()->GetNoSet_Impl().GetFreeIndex()+1;
}

void SfxViewFrame::Enable( bool bEnable )
{
    if ( bEnable == m_pImpl->bEnabled )
        return;

    m_pImpl->bEnabled = bEnable;

    vcl::Window *pWindow = &GetFrame().GetWindow();
    if ( !bEnable )
        m_pImpl->bWindowWasEnabled = pWindow->IsInputEnabled();
    if ( !bEnable || m_pImpl->bWindowWasEnabled )
        pWindow->EnableInput( bEnable );

    // cursor and focus
    SfxViewShell* pViewSh = GetViewShell();
    if ( bEnable )
    {
        // show cursor
        if ( pViewSh )
            pViewSh->ShowCursor();
    }
    else
    {
        // hide cursor
        if ( pViewSh )
            pViewSh->ShowCursor(false);
    }
}

/*  [Description]

    This method makes the Frame-Window visible and before transmits the
    window name. In addition, the document is held. In general one can never
    show the window directly!
*/
void SfxViewFrame::Show()
{
    // First lock the objectShell so that UpdateTitle() is valid:
    // IsVisible() == true (:#)
    if ( m_xObjSh.is() )
    {
        m_xObjSh->GetMedium()->GetItemSet().ClearItem( SID_HIDDEN );
        if ( !m_pImpl->bObjLocked )
            LockObjectShell_Impl();

        // Adjust Doc-Shell title number, get unique view-no
        if ( 0 == m_pImpl->nDocViewNo  )
        {
            GetDocNumber_Impl();
            UpdateTitle();
        }
    }
    else
        UpdateTitle();

    // Display Frame-window, but only if the ViewFrame has no window of its
    // own or if it does not contain a Component
    GetWindow().Show();
    GetFrame().GetWindow().Show();
}


bool SfxViewFrame::IsVisible() const
{
    return m_pImpl->bObjLocked;
}


void SfxViewFrame::LockObjectShell_Impl()
{
    DBG_ASSERT( !m_pImpl->bObjLocked, "Wrong Locked status!" );

    DBG_ASSERT( GetObjectShell(), "No Document!" );
    GetObjectShell()->OwnerLock(true);
    m_pImpl->bObjLocked = true;
}


void SfxViewFrame::MakeActive_Impl( bool bGrabFocus )
{
    if ( !GetViewShell() || GetFrame().IsClosing_Impl() )
        return;

    if ( !IsVisible() )
        return;

    bool bPreview = false;
    if (GetObjectShell()->IsPreview())
    {
        bPreview = true;
    }

    css::uno::Reference<css::frame::XFrame> xFrame = GetFrame().GetFrameInterface();
    if (!bPreview)
    {
        SetViewFrame(this);
        GetBindings().SetActiveFrame(css::uno::Reference<css::frame::XFrame>());
        uno::Reference<frame::XFramesSupplier> xSupp(xFrame, uno::UNO_QUERY);
        if (xSupp.is())
            xSupp->setActiveFrame(uno::Reference<frame::XFrame>());

        css::uno::Reference< css::awt::XWindow > xContainerWindow = xFrame->getContainerWindow();
        VclPtr<vcl::Window> pWindow = VCLUnoHelper::GetWindow(xContainerWindow);
        if (pWindow && pWindow->HasChildPathFocus() && bGrabFocus)
        {
            SfxInPlaceClient *pCli = GetViewShell()->GetUIActiveClient();
            if (!pCli || !pCli->IsObjectUIActive())
                GetFrame().GrabFocusOnComponent_Impl();
        }
    }
    else
    {
        GetBindings().SetDispatcher(GetDispatcher());
        GetBindings().SetActiveFrame(css::uno::Reference<css::frame::XFrame>());
        GetDispatcher()->Update_Impl();
    }
}

SfxObjectShell* SfxViewFrame::GetObjectShell()
{
    return m_xObjSh.get();
}

const Size& SfxViewFrame::GetMargin_Impl() const
{
    return m_pImpl->aMargin;
}

SfxViewFrame* SfxViewFrame::LoadViewIntoFrame_Impl_NoThrow( const SfxObjectShell& i_rDoc, const Reference< XFrame >& i_rFrame,
                                                   const SfxInterfaceId i_nViewId, const bool i_bHidden )
{
    Reference< XFrame > xFrame( i_rFrame );
    bool bOwnFrame = false;
    SfxViewShell* pSuccessView = nullptr;
    try
    {
        if ( !xFrame.is() )
        {
            Reference < XDesktop2 > xDesktop = Desktop::create( ::comphelper::getProcessComponentContext() );

            if ( !i_bHidden )
            {
                try
                {
                    // if there is a backing component, use it
                    ::framework::FrameListAnalyzer aAnalyzer( xDesktop, Reference< XFrame >(), FrameAnalyzerFlags::BackingComponent );

                    if ( aAnalyzer.m_xBackingComponent.is() )
                        xFrame = aAnalyzer.m_xBackingComponent;
                }
                catch( uno::Exception& )
                {}
            }

            if ( !xFrame.is() )
                xFrame.set( xDesktop->findFrame( u"_blank"_ustr, 0 ), UNO_SET_THROW );

            bOwnFrame = true;
        }

        pSuccessView = LoadViewIntoFrame_Impl(
            i_rDoc,
            xFrame,
            Sequence< PropertyValue >(),    // means "reuse existing model's args"
            i_nViewId,
            i_bHidden
        );

        if ( bOwnFrame && !i_bHidden )
        {
            // ensure the frame/window is visible
            Reference< XWindow > xContainerWindow( xFrame->getContainerWindow(), UNO_SET_THROW );
            xContainerWindow->setVisible( true );
        }
    }
    catch( const Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("sfx.view");
    }

    if ( pSuccessView )
        return &pSuccessView->GetViewFrame();

    if ( bOwnFrame )
    {
        try
        {
            xFrame->dispose();
        }
        catch( const Exception& )
        {
            DBG_UNHANDLED_EXCEPTION("sfx.view");
        }
    }

    return nullptr;
}

SfxViewShell* SfxViewFrame::LoadViewIntoFrame_Impl( const SfxObjectShell& i_rDoc, const Reference< XFrame >& i_rFrame,
                                           const Sequence< PropertyValue >& i_rLoadArgs, const SfxInterfaceId i_nViewId,
                                           const bool i_bHidden )
{
    Reference< XModel > xDocument( i_rDoc.GetModel(), UNO_SET_THROW );

    ::comphelper::NamedValueCollection aTransformLoadArgs( i_rLoadArgs.hasElements() ? i_rLoadArgs : xDocument->getArgs() );
    aTransformLoadArgs.put( u"Model"_ustr, xDocument );
    if ( i_nViewId )
        aTransformLoadArgs.put( u"ViewId"_ustr, sal_uInt16( i_nViewId ) );
    if ( i_bHidden )
        aTransformLoadArgs.put( u"Hidden"_ustr, i_bHidden );
    else
        aTransformLoadArgs.remove( u"Hidden"_ustr );

    Reference< XComponentLoader > xLoader( i_rFrame, UNO_QUERY_THROW );
    xLoader->loadComponentFromURL( u"private:object"_ustr, u"_self"_ustr, 0,
        aTransformLoadArgs.getPropertyValues() );

    SfxViewShell* pViewShell = SfxViewShell::Get( i_rFrame->getController() );
    ENSURE_OR_THROW( pViewShell,
        "SfxViewFrame::LoadViewIntoFrame_Impl: loading an SFX doc into a frame resulted in a non-SFX view - quite impossible" );
    return pViewShell;
}

SfxViewFrame* SfxViewFrame::LoadHiddenDocument( SfxObjectShell const & i_rDoc, SfxInterfaceId i_nViewId )
{
    return LoadViewIntoFrame_Impl_NoThrow( i_rDoc, Reference< XFrame >(), i_nViewId, true );
}

SfxViewFrame* SfxViewFrame::LoadDocument( SfxObjectShell const & i_rDoc, SfxInterfaceId i_nViewId )
{
    return LoadViewIntoFrame_Impl_NoThrow( i_rDoc, Reference< XFrame >(), i_nViewId, false );
}

SfxViewFrame* SfxViewFrame::LoadDocumentIntoFrame( SfxObjectShell const & i_rDoc, const Reference< XFrame >& i_rTargetFrame )
{
    return LoadViewIntoFrame_Impl_NoThrow( i_rDoc, i_rTargetFrame, SFX_INTERFACE_NONE, false );
}

SfxViewFrame* SfxViewFrame::LoadDocumentIntoFrame( SfxObjectShell const & i_rDoc, const SfxFrameItem* i_pFrameItem, SfxInterfaceId i_nViewId )
{
    return LoadViewIntoFrame_Impl_NoThrow( i_rDoc, i_pFrameItem && i_pFrameItem->GetFrame() ? i_pFrameItem->GetFrame()->GetFrameInterface() : nullptr, i_nViewId, false );
}

SfxViewFrame* SfxViewFrame::DisplayNewDocument( SfxObjectShell const & i_rDoc, const SfxRequest& i_rCreateDocRequest )
{
    const SfxUnoFrameItem* pFrameItem = i_rCreateDocRequest.GetArg<SfxUnoFrameItem>(SID_FILLFRAME);
    const SfxBoolItem* pHiddenItem = i_rCreateDocRequest.GetArg<SfxBoolItem>(SID_HIDDEN);

    return LoadViewIntoFrame_Impl_NoThrow(
        i_rDoc,
        pFrameItem ? pFrameItem->GetFrame() : nullptr,
        SFX_INTERFACE_NONE,
        pHiddenItem && pHiddenItem->GetValue()
    );
}

SfxViewFrame* SfxViewFrame::Get( const Reference< XController>& i_rController, const SfxObjectShell* i_pDoc )
{
    if ( !i_rController.is() )
        return nullptr;

    const SfxObjectShell* pDoc = i_pDoc;
    if ( !pDoc )
    {
        Reference< XModel > xDocument( i_rController->getModel() );
        for (   pDoc = SfxObjectShell::GetFirst( nullptr, false );
                pDoc;
                pDoc = SfxObjectShell::GetNext( *pDoc, nullptr, false )
            )
        {
            if ( pDoc->GetModel() == xDocument )
                break;
        }
    }

    SfxViewFrame* pViewFrame = nullptr;
    for (   pViewFrame = SfxViewFrame::GetFirst( pDoc, false );
            pViewFrame;
            pViewFrame = SfxViewFrame::GetNext( *pViewFrame, pDoc, false )
        )
    {
        if ( pViewFrame->GetViewShell()->GetController() == i_rController )
            break;
    }

    return pViewFrame;
}

void SfxViewFrame::SaveCurrentViewData_Impl( const SfxInterfaceId i_nNewViewId )
{
    SfxViewShell* pCurrentShell = GetViewShell();
    ENSURE_OR_RETURN_VOID( pCurrentShell != nullptr, "SfxViewFrame::SaveCurrentViewData_Impl: no current view shell -> no current view data!" );

    // determine the logical (API) view name
    const SfxObjectFactory& rDocFactory( pCurrentShell->GetObjectShell()->GetFactory() );
    const sal_uInt16 nCurViewNo = rDocFactory.GetViewNo_Impl( GetCurViewId(), 0 );
    const OUString sCurrentViewName = rDocFactory.GetViewFactory( nCurViewNo ).GetAPIViewName();
    const sal_uInt16 nNewViewNo = rDocFactory.GetViewNo_Impl( i_nNewViewId, 0 );
    const OUString sNewViewName = rDocFactory.GetViewFactory( nNewViewNo ).GetAPIViewName();
    if ( sCurrentViewName.isEmpty() || sNewViewName.isEmpty() )
    {
        // can't say anything about the view, the respective application did not yet migrate its code to
        // named view factories => bail out
        OSL_FAIL( "SfxViewFrame::SaveCurrentViewData_Impl: views without API names? Shouldn't happen anymore?" );
        return;
    }
    SAL_WARN_IF(sNewViewName == sCurrentViewName, "sfx.view", "SfxViewFrame::SaveCurrentViewData_Impl: suspicious: new and old view name are identical!");

    // save the view data only when we're moving from a non-print-preview to the print-preview view
    if ( sNewViewName != "PrintPreview" )
        return;

    // retrieve the view data from the view
    Sequence< PropertyValue > aViewData;
    pCurrentShell->WriteUserDataSequence( aViewData );

    try
    {
        // retrieve view data (for *all* views) from the model
        const Reference< XController > xController( pCurrentShell->GetController(), UNO_SET_THROW );
        const Reference< XViewDataSupplier > xViewDataSupplier( xController->getModel(), UNO_QUERY_THROW );
        const Reference< XIndexContainer > xViewData( xViewDataSupplier->getViewData(), UNO_QUERY_THROW );

        // look up the one view data item which corresponds to our current view, and remove it
        const sal_Int32 nCount = xViewData->getCount();
        for ( sal_Int32 i=0; i<nCount; ++i )
        {
            const ::comphelper::NamedValueCollection aCurViewData( xViewData->getByIndex(i) );
            const OUString sViewId( aCurViewData.getOrDefault( u"ViewId"_ustr, OUString() ) );
            if ( sViewId.isEmpty() )
                continue;

            const SfxViewFactory* pViewFactory = rDocFactory.GetViewFactoryByViewName( sViewId );
            if ( pViewFactory == nullptr )
                continue;

            if ( pViewFactory->GetOrdinal() == GetCurViewId() )
            {
                xViewData->removeByIndex(i);
                break;
            }
        }

        // then replace it with the most recent view data we just obtained
        xViewData->insertByIndex( 0, Any( aViewData ) );
    }
    catch( const Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("sfx.view");
    }
}

/*  [Description]

    Internal Method for switching to another <SfxViewShell> subclass,
    which should be created in this SfxMDIFrame. If no SfxViewShell exist
    in this SfxMDIFrame, then one will first be created.


    [Return Value]

    bool                        true
                                requested SfxViewShell was created and a
                                possibly existing one deleted

                                false
                                SfxViewShell requested could not be created,
                                the existing SfxViewShell thus continue to exist
*/
bool SfxViewFrame::SwitchToViewShell_Impl
(
    sal_uInt16  nViewIdOrNo,    /*  > 0
                                Registration-Id of the View, to which the
                                method should switch, for example the one
                                that will be created.

                                == 0
                                First use the Default view. */

    bool        bIsIndex        /*  true
                                'nViewIdOrNo' is no Registration-Id instead
                                an Index of <SfxViewFrame> in <SfxObjectShell>.
                                */
)
{
    try
    {
        ENSURE_OR_THROW( GetObjectShell() != nullptr, "not possible without a document" );

        // if we already have a view shell, remove it
        SfxViewShell* pOldSh = GetViewShell();
        OSL_PRECOND( pOldSh, "SfxViewFrame::SwitchToViewShell_Impl: that's called *switch* (not for *initial-load*) for a reason" );
        if ( pOldSh )
        {
            // ask whether it can be closed
            if ( !pOldSh->PrepareClose() )
                return false;

            // remove sub shells from Dispatcher before switching to new ViewShell
            PopShellAndSubShells_Impl( *pOldSh );
        }

        GetBindings().ENTERREGISTRATIONS();
        LockAdjustPosSizePixel();

        // ID of the new view
        SfxObjectFactory& rDocFact = GetObjectShell()->GetFactory();
        const SfxInterfaceId nViewId = ( bIsIndex || !nViewIdOrNo ) ? rDocFact.GetViewFactory( nViewIdOrNo ).GetOrdinal() : SfxInterfaceId(nViewIdOrNo);

        // save the view data of the old view, so it can be restored later on (when needed)
        SaveCurrentViewData_Impl( nViewId );

        if (pOldSh)
            pOldSh->SetDying();

        // create and load new ViewShell
        SfxViewShell* pNewSh = LoadViewIntoFrame_Impl(
            *GetObjectShell(),
            GetFrame().GetFrameInterface(),
            Sequence< PropertyValue >(),    // means "reuse existing model's args"
            nViewId,
            false
        );

        // allow resize events to be processed
        UnlockAdjustPosSizePixel();

        if ( GetWindow().IsReallyVisible() )
            DoAdjustPosSizePixel( pNewSh, Point(), GetWindow().GetOutputSizePixel(), false );

        GetBindings().LEAVEREGISTRATIONS();
        delete pOldSh;
    }
    catch ( const css::uno::Exception& )
    {
        // the SfxCode is not able to cope with exceptions thrown while creating views
        // the code will crash in the stack unwinding procedure, so we shouldn't let exceptions go through here
        DBG_UNHANDLED_EXCEPTION("sfx.view");
        return false;
    }

    DBG_ASSERT( SfxGetpApp()->GetViewFrames_Impl().size() == SfxGetpApp()->GetViewShells_Impl().size(), "Inconsistent view arrays!" );
    return true;
}

void SfxViewFrame::SetCurViewId_Impl( const SfxInterfaceId i_nID )
{
    m_pImpl->nCurViewId = i_nID;
}

SfxInterfaceId SfxViewFrame::GetCurViewId() const
{
    return m_pImpl->nCurViewId;
}

/*  [Description]

    Internal method to run the slot for the <SfxShell> Subclass in the
    SfxViewFrame <SVIDL> described slots.
*/
void SfxViewFrame::ExecView_Impl
(
    SfxRequest& rReq        // The executable <SfxRequest>
)
{

    // If the Shells are just being replaced...
    if ( !GetObjectShell() || !GetViewShell() )
        return;

    switch ( rReq.GetSlot() )
    {
        case SID_TERMINATE_INPLACEACTIVATION :
        {
            SfxInPlaceClient* pClient = GetViewShell()->GetUIActiveClient();
            if ( pClient )
                pClient->DeactivateObject();
            break;
        }

        case SID_VIEWSHELL:
        {
            const SfxUInt16Item *pItem = nullptr;
            if  (   rReq.GetArgs()
                &&  (pItem = rReq.GetArgs()->GetItemIfSet( SID_VIEWSHELL, false ))
                )
            {
                const sal_uInt16 nViewId = pItem->GetValue();
                bool bSuccess = SwitchToViewShell_Impl( nViewId );
                rReq.SetReturnValue( SfxBoolItem( 0, bSuccess ) );
            }
            break;
        }

        case SID_VIEWSHELL0:
        case SID_VIEWSHELL1:
        case SID_VIEWSHELL2:
        case SID_VIEWSHELL3:
        case SID_VIEWSHELL4:
        {
            const sal_uInt16 nViewNo = rReq.GetSlot() - SID_VIEWSHELL0;
            bool bSuccess = SwitchToViewShell_Impl( nViewNo, true );
            rReq.SetReturnValue( SfxBoolItem( 0, bSuccess ) );
            break;
        }

        case SID_NEWWINDOW:
        {
            // Hack. at the moment a virtual Function
            if ( !GetViewShell()->NewWindowAllowed() )
            {
                OSL_FAIL( "You should have disabled the 'Window/New Window' slot!" );
                return;
            }

            // Get ViewData of FrameSets recursively.
            GetFrame().GetViewData_Impl();
            SfxMedium* pMed = GetObjectShell()->GetMedium();

            // do not open the new window hidden
            pMed->GetItemSet().ClearItem( SID_HIDDEN );

            // the view ID (optional arg. TODO: this is currently not supported in the slot definition ...)
            const SfxUInt16Item* pViewIdItem = rReq.GetArg<SfxUInt16Item>(SID_VIEW_ID);
            const SfxInterfaceId nViewId = pViewIdItem ? SfxInterfaceId(pViewIdItem->GetValue()) : GetCurViewId();

            Reference < XFrame > xFrame;
            // the frame (optional arg. TODO: this is currently not supported in the slot definition ...)
            const SfxUnoFrameItem* pFrameItem = rReq.GetArg<SfxUnoFrameItem>(SID_FILLFRAME);
            if ( pFrameItem )
                xFrame = pFrameItem->GetFrame();

            LoadViewIntoFrame_Impl_NoThrow( *GetObjectShell(), xFrame, nViewId, false );

            rReq.Done();
            break;
        }

        case SID_OBJECT:
        {
            const SfxInt16Item* pItem = rReq.GetArg<SfxInt16Item>(SID_OBJECT);

            if (pItem)
            {
                GetViewShell()->DoVerb( pItem->GetValue() );
                rReq.Done();
                break;
            }
        }
    }
}

/* TODO as96863:
        This method try to collect information about the count of currently open documents.
        But the algorithm is implemented very simple ...
        E.g. hidden documents should be ignored here ... but they are counted.
        TODO: export special helper "framework::FrameListAnalyzer" within the framework module
        and use it here.
*/
static bool impl_maxOpenDocCountReached()
{
    const css::uno::Reference< css::uno::XComponentContext >& xContext = ::comphelper::getProcessComponentContext();
    std::optional<sal_Int32> x(officecfg::Office::Common::Misc::MaxOpenDocuments::get());
    // NIL means: count of allowed documents = infinite !
    if (!x)
        return false;
    sal_Int32 nMaxDocs(*x);
    sal_Int32 nOpenDocs = 0;

    css::uno::Reference< css::frame::XDesktop2 >  xDesktop = css::frame::Desktop::create(xContext);
    css::uno::Reference< css::container::XIndexAccess > xCont(xDesktop->getFrames(), css::uno::UNO_QUERY_THROW);

    sal_Int32 c = xCont->getCount();
    sal_Int32 i = 0;

    for (i=0; i<c; ++i)
    {
        try
        {
            css::uno::Reference< css::frame::XFrame > xFrame;
            xCont->getByIndex(i) >>= xFrame;
            if ( ! xFrame.is())
                continue;

            // a) do not count the help window
            if ( xFrame->getName() == "OFFICE_HELP_TASK" )
                continue;

            // b) count all other frames
            ++nOpenDocs;
        }
        catch(const css::uno::Exception&)
            // An IndexOutOfBoundsException can happen in multithreaded
            // environments, where any other thread can change this
            // container !
            { continue; }
    }

    return (nOpenDocs >= nMaxDocs);
}

/*  [Description]

    This internal method returns in 'rSet' the Status for the  <SfxShell>
    Subclass SfxViewFrame in the <SVIDL> described <Slots>.

    Thus exactly those Slots-IDs that are recognized as being invalid by Sfx
    are included as Which-ranges in 'rSet'. If there exists a mapping for
    single slot-IDs of the <SfxItemPool> set in the shell, then the respective
    Which-IDs are used so that items can be replaced directly with a working
    Core::sun::com::star::script::Engine of the Which-IDs if possible. .
*/
void SfxViewFrame::StateView_Impl
(
    SfxItemSet&     rSet            /*  empty <SfxItemSet> with <Which-Ranges>,
                                        which describes the Slot Ids */
)
{

    SfxObjectShell *pDocSh = GetObjectShell();

    if ( !pDocSh )
        // I'm just on reload and am yielding myself ...
        return;

    const WhichRangesContainer & pRanges = rSet.GetRanges();
    assert(!pRanges.empty() && "Set with no Range");
    for ( auto const & pRange : pRanges )
    {
        sal_uInt16 nStartWhich = pRange.first;
        sal_uInt16 nEndWhich = pRange.second;
        for ( sal_uInt16 nWhich = nStartWhich; nWhich <= nEndWhich; ++nWhich )
        {
            switch(nWhich)
            {
                case SID_VIEWSHELL:
                {
                    rSet.Put( SfxUInt16Item( nWhich, sal_uInt16(m_pImpl->nCurViewId )) );
                    break;
                }

                case SID_VIEWSHELL0:
                case SID_VIEWSHELL1:
                case SID_VIEWSHELL2:
                case SID_VIEWSHELL3:
                case SID_VIEWSHELL4:
                {
                    sal_uInt16 nViewNo = nWhich - SID_VIEWSHELL0;
                    if ( GetObjectShell()->GetFactory().GetViewFactoryCount() >
                         nViewNo && !GetObjectShell()->IsInPlaceActive() )
                    {
                        SfxViewFactory &rViewFactory =
                            GetObjectShell()->GetFactory().GetViewFactory(nViewNo);
                        rSet.Put( SfxBoolItem(
                            nWhich, m_pImpl->nCurViewId == rViewFactory.GetOrdinal() ) );
                    }
                    else
                        rSet.DisableItem( nWhich );
                    break;
                }

                case SID_NEWWINDOW:
                {
                    if  (   !GetViewShell()->NewWindowAllowed()
                        ||  impl_maxOpenDocCountReached()
                        )
                        rSet.DisableItem( nWhich );
                    break;
                }
            }
        }
    }
}


void SfxViewFrame::ToTop()
{
    GetFrame().Appear();
}


/*  [Description]

    GetFrame returns the Frame, in which the ViewFrame is located.
*/
SfxFrame& SfxViewFrame::GetFrame() const
{
    return m_pImpl->rFrame;
}

SfxViewFrame* SfxViewFrame::GetTopViewFrame() const
{
    return GetFrame().GetCurrentViewFrame();
}

vcl::Window& SfxViewFrame::GetWindow() const
{
    return m_pImpl->pWindow ? *m_pImpl->pWindow : GetFrame().GetWindow();
}

weld::Window* SfxViewFrame::GetFrameWeld() const
{
    return GetWindow().GetFrameWeld();
}

bool SfxViewFrame::DoClose()
{
    return GetFrame().DoClose();
}

const OUString & SfxViewFrame::GetActualPresentationURL_Impl() const
{
    if ( m_xObjSh.is() )
        return m_xObjSh->GetMedium()->GetName();
    return EMPTY_OUSTRING;
}

void SfxViewFrame::SetModalMode( bool bModal )
{
    // no real modality for LOK
    if (comphelper::LibreOfficeKit::isActive())
        return;

    m_pImpl->bModal = bModal;
    if ( m_xObjSh.is() )
    {
        for ( SfxViewFrame* pFrame = SfxViewFrame::GetFirst( m_xObjSh.get() );
              !bModal && pFrame; pFrame = SfxViewFrame::GetNext( *pFrame, m_xObjSh.get() ) )
            bModal = pFrame->m_pImpl->bModal;
        m_xObjSh->SetModalMode_Impl( bModal );
    }
}

bool SfxViewFrame::IsInModalMode() const
{
    return m_pImpl->bModal || GetFrame().GetWindow().IsInModalMode();
}

void SfxViewFrame::Resize( bool bForce )
{
    Size aSize = GetWindow().GetOutputSizePixel();
    if ( !bForce && aSize == m_pImpl->aSize )
        return;

    m_pImpl->aSize = aSize;
    SfxViewShell *pShell = GetViewShell();
    if ( pShell )
    {
        if ( GetFrame().IsInPlace() )
        {
            Point aPoint = GetWindow().GetPosPixel();
            DoAdjustPosSizePixel( pShell, aPoint, aSize, true );
        }
        else
        {
            DoAdjustPosSizePixel( pShell, Point(), aSize, false );
        }
    }
}

#if HAVE_FEATURE_SCRIPTING

#define LINE_SEP 0x0A

static void CutLines( OUString& rStr, sal_Int32 nStartLine, sal_Int32 nLines )
{
    sal_Int32 nStartPos = 0;
    sal_Int32 nLine = 0;
    while ( nLine < nStartLine )
    {
        nStartPos = rStr.indexOf( LINE_SEP, nStartPos );
        if( nStartPos == -1 )
            break;
        nStartPos++;    // not the \n.
        nLine++;
    }

    SAL_WARN_IF(nStartPos == -1, "sfx.view", "CutLines: Start row not found!");

    if ( nStartPos != -1 )
    {
        sal_Int32 nEndPos = nStartPos;
        for ( sal_Int32 i = 0; i < nLines; i++ )
            nEndPos = rStr.indexOf( LINE_SEP, nEndPos+1 );

        if ( nEndPos == -1 ) // Can happen at the last row.
            nEndPos = rStr.getLength();
        else
            nEndPos++;

        rStr = OUString::Concat(rStr.subView( 0, nStartPos )) + rStr.subView( nEndPos );
    }
    // erase trailing lines
    if ( nStartPos != -1 )
    {
        sal_Int32 n = nStartPos;
        sal_Int32 nLen = rStr.getLength();
        while ( ( n < nLen ) && ( rStr[ n ] == LINE_SEP ) )
            n++;

        if ( n > nStartPos )
            rStr = OUString::Concat(rStr.subView( 0, nStartPos )) + rStr.subView( n );
    }
}

#endif

/*
    add new recorded dispatch macro script into the application global basic
    lib container. It generates a new unique id for it and insert the macro
    by using this number as name for the module
 */
void SfxViewFrame::AddDispatchMacroToBasic_Impl( const OUString& sMacro )
{
#if !HAVE_FEATURE_SCRIPTING
    (void) sMacro;
#else
    if ( sMacro.isEmpty() )
        return;

    SfxApplication* pSfxApp = SfxGetpApp();
    SfxItemPool& rPool = pSfxApp->GetPool();
    SfxRequest aReq(SID_BASICCHOOSER, SfxCallMode::SYNCHRON, rPool);

    //seen in tdf#122598, no parent for subsequent dialog
    SfxAllItemSet aSet(rPool);
    aSet.Put(SfxUnoFrameItem(SID_FILLFRAME,
                GetFrame().GetFrameInterface()));
    aReq.SetInternalArgs_Impl(aSet);

    aReq.AppendItem( SfxBoolItem(SID_RECORDMACRO,true) );
    const SfxPoolItemHolder& rResult(SfxGetpApp()->ExecuteSlot(aReq));
    OUString aScriptURL;
    if (rResult)
        aScriptURL = static_cast<const SfxStringItem*>(rResult.getItem())->GetValue();
    if ( !aScriptURL.isEmpty() )
    {
        // parse scriptURL
        OUString aLibName;
        OUString aModuleName;
        OUString aMacroName;
        OUString aLocation;
        const Reference< XComponentContext >& xContext = ::comphelper::getProcessComponentContext();
        Reference< css::uri::XUriReferenceFactory > xFactory =
            css::uri::UriReferenceFactory::create( xContext );
        Reference< css::uri::XVndSunStarScriptUrl > xUrl( xFactory->parse( aScriptURL ), UNO_QUERY );
        if ( xUrl.is() )
        {
            // get name
            const OUString aName = xUrl->getName();
            const sal_Unicode cTok = '.';
            sal_Int32 nIndex = 0;
            aLibName = aName.getToken( 0, cTok, nIndex );
            if ( nIndex != -1 )
                aModuleName = aName.getToken( 0, cTok, nIndex );
            if ( nIndex != -1 )
                aMacroName = aName.getToken( 0, cTok, nIndex );

            // get location
            aLocation = xUrl->getParameter( u"location"_ustr );
        }

        BasicManager* pBasMgr = nullptr;
        if ( aLocation == "application" )
        {
            // application basic
            pBasMgr = SfxApplication::GetBasicManager();
        }
        else if ( aLocation == "document" )
        {
            pBasMgr = GetObjectShell()->GetBasicManager();
        }

        OUString aOUSource;
        if ( pBasMgr)
        {
            StarBASIC* pBasic = pBasMgr->GetLib( aLibName );
            if ( pBasic )
            {
                SbModule* pModule = pBasic->FindModule( aModuleName );
                SbMethod* pMethod = pModule ? pModule->FindMethod(aMacroName, SbxClassType::Method) : nullptr;
                if (pMethod)
                {
                    aOUSource = pModule->GetSource32();
                    sal_uInt16 nStart, nEnd;
                    pMethod->GetLineRange( nStart, nEnd );
                    sal_uInt16 nlStart = nStart;
                    sal_uInt16 nlEnd = nEnd;
                    CutLines( aOUSource, nlStart-1, nlEnd-nlStart+1 );
                }
            }
        }

        // open lib container and break operation if it couldn't be opened
        css::uno::Reference< css::script::XLibraryContainer > xLibCont;
        if ( aLocation == "application" )
        {
            xLibCont = SfxGetpApp()->GetBasicContainer();
        }
        else if ( aLocation == "document" )
        {
            xLibCont = GetObjectShell()->GetBasicContainer();
        }

        if(!xLibCont.is())
        {
            SAL_WARN("sfx.view", "couldn't get access to the basic lib container. Adding of macro isn't possible.");
            return;
        }

        // get LibraryContainer
        css::uno::Any aTemp;

        css::uno::Reference< css::container::XNameAccess > xLib;
        if(xLibCont->hasByName(aLibName))
        {
            // library must be loaded
            aTemp = xLibCont->getByName(aLibName);
            xLibCont->loadLibrary(aLibName);
            aTemp >>= xLib;
        }
        else
        {
            xLib = xLibCont->createLibrary(aLibName);
        }

        // pack the macro as direct usable "sub" routine
        OUStringBuffer sRoutine(10000);
        bool bReplace = false;

        // get module
        if(xLib->hasByName(aModuleName))
        {
            if ( !aOUSource.isEmpty() )
            {
                sRoutine.append( aOUSource );
            }
            else
            {
                OUString sCode;
                aTemp = xLib->getByName(aModuleName);
                aTemp >>= sCode;
                sRoutine.append( sCode );
            }

            bReplace = true;
        }

        // append new method
        sRoutine.append( "\nsub "
            + aMacroName
            + "\n"
            + sMacro
            + "\nend sub\n" );

        // create the module inside the library and insert the macro routine
        aTemp <<= sRoutine.makeStringAndClear();
        if ( bReplace )
        {
            css::uno::Reference< css::container::XNameContainer > xModulCont(
                xLib,
                css::uno::UNO_QUERY);
            xModulCont->replaceByName(aModuleName,aTemp);
        }
        else
        {
            css::uno::Reference< css::container::XNameContainer > xModulCont(
                xLib,
                css::uno::UNO_QUERY);
            xModulCont->insertByName(aModuleName,aTemp);
        }

        // #i17355# update the Basic IDE
        for ( SfxViewShell* pViewShell = SfxViewShell::GetFirst(); pViewShell; pViewShell = SfxViewShell::GetNext( *pViewShell ) )
        {
            if ( pViewShell->GetName() == "BasicIDE" )
            {
                SfxViewFrame& rViewFrame = pViewShell->GetViewFrame();
                SfxDispatcher* pDispat = rViewFrame.GetDispatcher();
                if ( pDispat )
                {
                    SfxMacroInfoItem aInfoItem( SID_BASICIDE_ARG_MACROINFO, pBasMgr, aLibName, aModuleName, OUString(), OUString() );
                    pDispat->ExecuteList(SID_BASICIDE_UPDATEMODULESOURCE,
                            SfxCallMode::SYNCHRON, { &aInfoItem });
                }
            }
        }
    }
    else
    {
        // add code for "session only" macro
    }
#endif
}

void SfxViewFrame::MiscExec_Impl( SfxRequest& rReq )
{
    switch ( rReq.GetSlot() )
    {
        case SID_STOP_RECORDING :
        case SID_RECORDMACRO :
        {
            // try to find any active recorder on this frame
            static constexpr OUString sProperty(u"DispatchRecorderSupplier"_ustr);
            css::uno::Reference< css::frame::XFrame > xFrame =
                    GetFrame().GetFrameInterface();

            css::uno::Reference< css::beans::XPropertySet > xSet(xFrame,css::uno::UNO_QUERY);
            css::uno::Any aProp = xSet->getPropertyValue(sProperty);
            css::uno::Reference< css::frame::XDispatchRecorderSupplier > xSupplier;
            aProp >>= xSupplier;
            css::uno::Reference< css::frame::XDispatchRecorder > xRecorder;
            if (xSupplier.is())
                xRecorder = xSupplier->getDispatchRecorder();

            bool bIsRecording = xRecorder.is();
            const SfxBoolItem* pItem = rReq.GetArg<SfxBoolItem>(SID_RECORDMACRO);
            if ( pItem && pItem->GetValue() == bIsRecording )
                return;

            if ( xRecorder.is() )
            {
                // disable active recording
                aProp <<= css::uno::Reference< css::frame::XDispatchRecorderSupplier >();
                xSet->setPropertyValue(sProperty,aProp);

                const SfxBoolItem* pRecordItem = rReq.GetArg<SfxBoolItem>(FN_PARAM_1);
                if ( !pRecordItem || !pRecordItem->GetValue() )
                    // insert script into basic library container of application
                    AddDispatchMacroToBasic_Impl(xRecorder->getRecordedMacro());

                xRecorder->endRecording();
                xRecorder = nullptr;
                GetBindings().SetRecorder_Impl( xRecorder );

                SetChildWindow( SID_RECORDING_FLOATWINDOW, false );
                if ( rReq.GetSlot() != SID_RECORDMACRO )
                    GetBindings().Invalidate( SID_RECORDMACRO );
            }
            else if ( rReq.GetSlot() == SID_RECORDMACRO )
            {
                // enable recording
                const css::uno::Reference< css::uno::XComponentContext >& xContext(
                        ::comphelper::getProcessComponentContext());

                xRecorder = css::frame::DispatchRecorder::create( xContext );

                xSupplier = css::frame::DispatchRecorderSupplier::create( xContext );

                xSupplier->setDispatchRecorder(xRecorder);
                xRecorder->startRecording(xFrame);
                aProp <<= xSupplier;
                xSet->setPropertyValue(sProperty,aProp);
                GetBindings().SetRecorder_Impl( xRecorder );
                SetChildWindow( SID_RECORDING_FLOATWINDOW, true );
            }

            rReq.Done();
            break;
        }

        case SID_TOGGLESTATUSBAR:
        {
            if ( auto xLayoutManager = getLayoutManager(GetFrame()) )
            {
                static constexpr OUString aStatusbarResString( u"private:resource/statusbar/statusbar"_ustr );
                // Evaluate parameter.
                const SfxBoolItem* pShowItem = rReq.GetArg<SfxBoolItem>(rReq.GetSlot());
                bool bShow( true );
                if ( !pShowItem )
                    bShow = xLayoutManager->isElementVisible( aStatusbarResString );
                else
                    bShow = pShowItem->GetValue();

                if ( bShow )
                {
                    xLayoutManager->createElement( aStatusbarResString );
                    xLayoutManager->showElement( aStatusbarResString );
                }
                else
                    xLayoutManager->hideElement( aStatusbarResString );

                if ( !pShowItem )
                    rReq.AppendItem( SfxBoolItem( SID_TOGGLESTATUSBAR, bShow ) );
            }
            rReq.Done();
            break;
        }
        case SID_COMMAND_POPUP:
        {
            tools::Rectangle aRectangle(Point(0,0), GetWindow().GetSizePixel());
            weld::Window* pParent = weld::GetPopupParent(GetWindow(), aRectangle);
            m_pCommandPopupHandler->showPopup(pParent, GetFrame().GetFrameInterface());

            rReq.Done();
            break;
        }
        case SID_WIN_FULLSCREEN:
        {
            const SfxBoolItem* pItem = rReq.GetArg<SfxBoolItem>(rReq.GetSlot());
            SfxViewFrame *pTop = GetTopViewFrame();
            if ( pTop )
            {
                WorkWindow* pWork = static_cast<WorkWindow*>( pTop->GetFrame().GetTopWindow_Impl() );
                if ( pWork )
                {
                    Reference< css::frame::XLayoutManager > xLayoutManager = getLayoutManager(GetFrame());
                    bool bNewFullScreenMode = pItem ? pItem->GetValue() : !pWork->IsFullScreenMode();
                    if ( bNewFullScreenMode != pWork->IsFullScreenMode() )
                    {
                        if ( bNewFullScreenMode )
                            sfx2::SfxNotebookBar::LockNotebookBar();
                        else
                            sfx2::SfxNotebookBar::UnlockNotebookBar();

                        Reference< css::beans::XPropertySet > xLMPropSet( xLayoutManager, UNO_QUERY );
                        if ( xLMPropSet.is() )
                        {
                            try
                            {
                                xLMPropSet->setPropertyValue(
                                    u"HideCurrentUI"_ustr,
                                    Any( bNewFullScreenMode ));
                            }
                            catch ( css::beans::UnknownPropertyException& )
                            {
                            }
                        }
                        pWork->ShowFullScreenMode( bNewFullScreenMode );
                        pWork->SetMenuBarMode( bNewFullScreenMode ? MenuBarMode::Hide : MenuBarMode::Normal );
                        GetFrame().GetWorkWindow_Impl()->SetFullScreen_Impl( bNewFullScreenMode );
                        if ( !pItem )
                            rReq.AppendItem( SfxBoolItem( SID_WIN_FULLSCREEN, bNewFullScreenMode ) );
                        rReq.Done();
                    }
                    else
                        rReq.Ignore();
                }
            }
            else
                rReq.Ignore();

            GetDispatcher()->Update_Impl( true );
            break;
        }
    }
}

void SfxViewFrame::MiscState_Impl(SfxItemSet &rSet)
{
    const WhichRangesContainer & pRanges = rSet.GetRanges();
    DBG_ASSERT(!pRanges.empty(), "Set without range");
    for ( auto const & pRange : pRanges )
    {
        for(sal_uInt16 nWhich = pRange.first; nWhich <= pRange.second; ++nWhich)
        {
            switch(nWhich)
            {
                case SID_CURRENT_URL:
                {
                    rSet.Put( SfxStringItem( nWhich, GetActualPresentationURL_Impl() ) );
                    break;
                }

                case SID_RECORDMACRO :
                {
                    const OUString& sName{GetObjectShell()->GetFactory().GetFactoryName()};
                    if (SvtSecurityOptions::IsMacroDisabled() ||
                         !officecfg::Office::Common::Misc::MacroRecorderMode::get() ||
                         ( sName!="swriter" && sName!="scalc" ) )
                    {
                        rSet.DisableItem( nWhich );
                        rSet.Put(SfxVisibilityItem(nWhich, false));
                        break;
                    }

                    css::uno::Reference< css::beans::XPropertySet > xSet(
                            GetFrame().GetFrameInterface(),
                            css::uno::UNO_QUERY);

                    css::uno::Any aProp = xSet->getPropertyValue(u"DispatchRecorderSupplier"_ustr);
                    css::uno::Reference< css::frame::XDispatchRecorderSupplier > xSupplier;
                    if ( aProp >>= xSupplier )
                        rSet.Put( SfxBoolItem( nWhich, xSupplier.is() ) );
                    else
                        rSet.DisableItem( nWhich );
                    break;
                }

                case SID_STOP_RECORDING :
                {
                    const OUString& sName{GetObjectShell()->GetFactory().GetFactoryName()};
                    if ( !officecfg::Office::Common::Misc::MacroRecorderMode::get() ||
                         ( sName!="swriter" && sName!="scalc" ) )
                    {
                        rSet.DisableItem( nWhich );
                        break;
                    }

                    css::uno::Reference< css::beans::XPropertySet > xSet(
                            GetFrame().GetFrameInterface(),
                            css::uno::UNO_QUERY);

                    css::uno::Any aProp = xSet->getPropertyValue(u"DispatchRecorderSupplier"_ustr);
                    css::uno::Reference< css::frame::XDispatchRecorderSupplier > xSupplier;
                    if ( !(aProp >>= xSupplier) || !xSupplier.is() )
                        rSet.DisableItem( nWhich );
                    break;
                }

                case SID_TOGGLESTATUSBAR:
                {
                    css::uno::Reference< css::frame::XLayoutManager > xLayoutManager;
                    css::uno::Reference< css::beans::XPropertySet > xSet(
                            GetFrame().GetFrameInterface(),
                            css::uno::UNO_QUERY);
                    css::uno::Any aProp = xSet->getPropertyValue( u"LayoutManager"_ustr );

                    if ( !( aProp >>= xLayoutManager ))
                        rSet.Put( SfxBoolItem( nWhich, false ));
                    else
                    {
                        bool bShow = xLayoutManager->isElementVisible( u"private:resource/statusbar/statusbar"_ustr );
                        rSet.Put( SfxBoolItem( nWhich, bShow ));
                    }
                    break;
                }

                case SID_WIN_FULLSCREEN:
                {
                    SfxViewFrame* pTop = GetTopViewFrame();
                    if ( pTop )
                    {
                        WorkWindow* pWork = static_cast<WorkWindow*>( pTop->GetFrame().GetTopWindow_Impl() );
                        if ( pWork )
                        {
                            rSet.Put( SfxBoolItem( nWhich, pWork->IsFullScreenMode() ) );
                            break;
                        }
                    }

                    rSet.DisableItem( nWhich );
                    break;
                }

                default:
                    break;
            }
        }
    }
}

/*  [Description]

    This method can be included in the Execute method for the on- and off-
    switching of ChildWindows, to implement this and API-bindings.

    Simply include as 'ExecuteMethod' in the IDL.
*/
void SfxViewFrame::ChildWindowExecute( SfxRequest &rReq )
{
    // Evaluate Parameter
    sal_uInt16 nSID = rReq.GetSlot();

    if (nSID == SID_SIDEBAR_DECK)
    {
        const SfxStringItem* pDeckIdItem = rReq.GetArg<SfxStringItem>(SID_SIDEBAR_DECK);
        if (pDeckIdItem)
        {
            const OUString aDeckId(pDeckIdItem->GetValue());
            // Compatibility with old LOK "toggle always"
            // TODO: check LOK with tdf#142978 Show a11y sidebar when finding issues on PDF export, hash: 53fc5fa
            const bool isLOK = comphelper::LibreOfficeKit::isActive();
            const SfxBoolItem* pToggleItem = rReq.GetArg<SfxBoolItem>(SID_SIDEBAR_DECK_TOGGLE);
            bool bToggle = isLOK || (pToggleItem && pToggleItem->GetValue());
            ::sfx2::sidebar::Sidebar::ShowDeck(aDeckId, this, bToggle);
        }
        rReq.Done();
        return;
    }

    const SfxBoolItem* pShowItem = rReq.GetArg<SfxBoolItem>(nSID);
    if ( nSID == SID_VIEW_DATA_SOURCE_BROWSER )
    {
        if (!SvtModuleOptions().IsDataBaseInstalled())
            return;
        Reference < XFrame > xFrame = GetFrame().GetFrameInterface();
        Reference < XFrame > xBeamer( xFrame->findFrame( u"_beamer"_ustr, FrameSearchFlag::CHILDREN ) );
        bool bHasChild = xBeamer.is();
        bool bShow = pShowItem ? pShowItem->GetValue() : !bHasChild;
        if ( pShowItem )
        {
            if( bShow == bHasChild )
                return;
        }
        else
            rReq.AppendItem( SfxBoolItem( nSID, bShow ) );

        if ( !bShow )
        {
            SetChildWindow( SID_BROWSER, false );
        }
        else
        {
            css::util::URL aTargetURL;
            aTargetURL.Complete = ".component:DB/DataSourceBrowser";
            Reference < css::util::XURLTransformer > xTrans(
                    css::util::URLTransformer::create(
                         ::comphelper::getProcessComponentContext() ) );
            xTrans->parseStrict( aTargetURL );

            Reference < XDispatchProvider > xProv( xFrame, UNO_QUERY );
            Reference < css::frame::XDispatch > xDisp;
            if ( xProv.is() )
                xDisp = xProv->queryDispatch( aTargetURL, u"_beamer"_ustr, 31 );
            if ( xDisp.is() )
            {
                Sequence < css::beans::PropertyValue > aArgs(1);
                css::beans::PropertyValue* pArg = aArgs.getArray();
                pArg[0].Name = "Referer";
                pArg[0].Value <<= u"private:user"_ustr;
                xDisp->dispatch( aTargetURL, aArgs );
            }
        }

        rReq.Done();
        return;
    }
    if (nSID == SID_STYLE_DESIGNER)
    {
        // First make sure that the sidebar is visible
        ShowChildWindow(SID_SIDEBAR);

        ::sfx2::sidebar::Sidebar::ShowPanel(u"StyleListPanel",
                                            GetFrame().GetFrameInterface(), true);
        rReq.Done();
        return;
    }

    bool bHasChild = HasChildWindow(nSID);
    bool bShow = pShowItem ? pShowItem->GetValue() : !bHasChild;
    GetDispatcher()->Update_Impl( true );

    // Perform action.
    if ( !pShowItem || bShow != bHasChild )
        ToggleChildWindow( nSID );

    GetBindings().Invalidate( nSID );

    // Record if possible.
    if ( nSID == SID_HYPERLINK_DIALOG || nSID == SID_SEARCH_DLG )
    {
        rReq.Ignore();
    }
    else
    {
        rReq.AppendItem( SfxBoolItem( nSID, bShow ) );
        rReq.Done();
    }
}

/*  [Description]

    This method can be used in the state method for the on and off-state
    of child-windows, in order to implement this.

    Just register the IDL as 'StateMethod'.
*/
void SfxViewFrame::ChildWindowState( SfxItemSet& rState )
{
    SfxWhichIter aIter( rState );
    for ( sal_uInt16 nSID = aIter.FirstWhich(); nSID; nSID = aIter.NextWhich() )
    {
        if ( nSID == SID_VIEW_DATA_SOURCE_BROWSER )
        {
            rState.Put( SfxBoolItem( nSID, HasChildWindow( SID_BROWSER ) ) );
        }
        else if ( nSID == SID_HYPERLINK_DIALOG )
        {
            SfxPoolItemHolder aDummy;
            SfxItemState eState = GetDispatcher()->QueryState(SID_HYPERLINK_SETLINK, aDummy);
            if ( SfxItemState::DISABLED == eState )
                rState.DisableItem(nSID);
            else
            {
                if ( KnowsChildWindow(nSID) )
                    rState.Put( SfxBoolItem( nSID, HasChildWindow(nSID)) );
                else
                    rState.DisableItem(nSID);
            }
        }
        else if ( nSID == SID_BROWSER )
        {
            Reference < XFrame > xFrame = GetFrame().GetFrameInterface()->
                            findFrame( u"_beamer"_ustr, FrameSearchFlag::CHILDREN );
            if ( !xFrame.is() )
                rState.DisableItem( nSID );
            else if ( KnowsChildWindow(nSID) )
                rState.Put( SfxBoolItem( nSID, HasChildWindow(nSID) ) );
        }
        else if ( nSID == SID_SIDEBAR )
        {
            if  ( !KnowsChildWindow( nSID ) )
            {
                SAL_INFO("sfx.view", "SID_SIDEBAR state requested, but no task pane child window exists for this ID!");
                rState.DisableItem( nSID );
            }
            else
            {
                rState.Put( SfxBoolItem( nSID, HasChildWindow( nSID ) ) );
            }
        }
        else if ( KnowsChildWindow(nSID) )
            rState.Put( SfxBoolItem( nSID, HasChildWindow(nSID) ) );
        else
            rState.DisableItem(nSID);
    }
}

SfxWorkWindow* SfxViewFrame::GetWorkWindow_Impl()
{
    SfxWorkWindow* pWork = GetFrame().GetWorkWindow_Impl();
    return pWork;
}

void SfxViewFrame::SetChildWindow(sal_uInt16 nId, bool bOn, bool bSetFocus )
{
    SfxWorkWindow* pWork = GetWorkWindow_Impl();
    if ( pWork )
        pWork->SetChildWindow_Impl( nId, bOn, bSetFocus );
}

void SfxViewFrame::ToggleChildWindow(sal_uInt16 nId)
{
    SfxWorkWindow* pWork = GetWorkWindow_Impl();
    if ( pWork )
        pWork->ToggleChildWindow_Impl( nId, true );
}

bool SfxViewFrame::HasChildWindow( sal_uInt16 nId )
{
    SfxWorkWindow* pWork = GetWorkWindow_Impl();
    return pWork && pWork->HasChildWindow_Impl(nId);
}

bool SfxViewFrame::KnowsChildWindow( sal_uInt16 nId )
{
    SfxWorkWindow* pWork = GetWorkWindow_Impl();
    return pWork && pWork->KnowsChildWindow_Impl(nId);
}

void SfxViewFrame::ShowChildWindow( sal_uInt16 nId, bool bVisible )
{
    SfxWorkWindow* pWork = GetWorkWindow_Impl();
    if ( pWork )
    {
        GetDispatcher()->Update_Impl(true);
        pWork->ShowChildWindow_Impl(nId, bVisible, true );
    }
}

SfxChildWindow* SfxViewFrame::GetChildWindow(sal_uInt16 nId)
{
    SfxWorkWindow* pWork = GetWorkWindow_Impl();
    return pWork ? pWork->GetChildWindow_Impl(nId) : nullptr;
}

void SfxViewFrame::UpdateDocument_Impl()
{
    SfxObjectShell* pDoc = GetObjectShell();
    if ( pDoc->IsLoadingFinished() )
        pDoc->CheckSecurityOnLoading_Impl();

    // check if document depends on a template
    pDoc->UpdateFromTemplate_Impl();
}

void SfxViewFrame::SetViewFrame( SfxViewFrame* pFrame )
{
    if(pFrame)
        SetSVHelpData(pFrame->m_pHelpData);

    SetSVWinData(pFrame ? pFrame->m_pWinData : nullptr);

    SfxGetpApp()->SetViewFrame_Impl( pFrame );
}

VclPtr<SfxInfoBarWindow> SfxViewFrame::AppendInfoBar(const OUString& sId,
                                               const OUString& sPrimaryMessage,
                                               const OUString& sSecondaryMessage,
                                               InfobarType aInfobarType, bool bShowCloseButton)
{
    SfxChildWindow* pChild = GetChildWindow(SfxInfoBarContainerChild::GetChildWindowId());
    if (!pChild)
        return nullptr;

    if (HasInfoBarWithID(sId))
        return nullptr;

    SfxInfoBarContainerWindow* pInfoBarContainer = static_cast<SfxInfoBarContainerWindow*>(pChild->GetWindow());
    auto pInfoBar = pInfoBarContainer->appendInfoBar(sId, sPrimaryMessage, sSecondaryMessage,
                                                     aInfobarType, bShowCloseButton);
    ShowChildWindow(SfxInfoBarContainerChild::GetChildWindowId());
    return pInfoBar;
}

void SfxViewFrame::UpdateInfoBar(std::u16string_view sId, const OUString& sPrimaryMessage,
                                 const OUString& sSecondaryMessage, InfobarType eType)
{
    const sal_uInt16 nId = SfxInfoBarContainerChild::GetChildWindowId();

    // Make sure the InfoBar container is visible
    if (!HasChildWindow(nId))
        ToggleChildWindow(nId);

    SfxChildWindow* pChild = GetChildWindow(nId);
    if (pChild)
    {
        SfxInfoBarContainerWindow* pInfoBarContainer = static_cast<SfxInfoBarContainerWindow*>(pChild->GetWindow());
        auto pInfoBar = pInfoBarContainer->getInfoBar(sId);

        if (pInfoBar)
             pInfoBar->Update(sPrimaryMessage, sSecondaryMessage, eType);
    }
}

void SfxViewFrame::RemoveInfoBar( std::u16string_view sId )
{
    const sal_uInt16 nId = SfxInfoBarContainerChild::GetChildWindowId();

    // Make sure the InfoBar container is visible
    if (!HasChildWindow(nId))
        ToggleChildWindow(nId);

    SfxChildWindow* pChild = GetChildWindow(nId);
    if (pChild)
    {
        SfxInfoBarContainerWindow* pInfoBarContainer = static_cast<SfxInfoBarContainerWindow*>(pChild->GetWindow());
        auto pInfoBar = pInfoBarContainer->getInfoBar(sId);
        pInfoBarContainer->removeInfoBar(pInfoBar);
        ShowChildWindow(nId);
    }
}

bool SfxViewFrame::HasInfoBarWithID( std::u16string_view sId )
{
    const sal_uInt16 nId = SfxInfoBarContainerChild::GetChildWindowId();

    SfxChildWindow* pChild = GetChildWindow(nId);
    if (pChild)
    {
        SfxInfoBarContainerWindow* pInfoBarContainer = static_cast<SfxInfoBarContainerWindow*>(pChild->GetWindow());
        return pInfoBarContainer->hasInfoBarWithID(sId);
    }

    return false;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
