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

#include <libxml/xmlstring.h>
#include <libxml/xmlwriter.h>
#include <stdlib.h>
#include <hintids.hxx>
#include <osl/diagnose.h>
#include <sot/exchange.hxx>
#include <svl/stritem.hxx>
#include <sfx2/docfile.hxx>
#include <editeng/protitem.hxx>
#include <sfx2/linkmgr.hxx>
#include <sfx2/sfxsids.hrc>
#include <docary.hxx>
#include <fmtcntnt.hxx>
#include <fmtpdsc.hxx>
#include <doc.hxx>
#include <IDocumentUndoRedo.hxx>
#include <DocumentLinksAdministrationManager.hxx>
#include <DocumentContentOperationsManager.hxx>
#include <IDocumentRedlineAccess.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentStylePoolAccess.hxx>
#include <IDocumentState.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <IDocumentStatistics.hxx>
#include <docstat.hxx>
#include <fmtanchr.hxx>
#include <node.hxx>
#include <pam.hxx>
#include <frmatr.hxx>
#include <frmtool.hxx>
#include <editsh.hxx>
#include <hints.hxx>
#include <docsh.hxx>
#include <ndtxt.hxx>
#include <section.hxx>
#include <swserv.hxx>
#include <shellio.hxx>
#include <poolfmt.hxx>
#include <swbaslnk.hxx>
#include <mvsave.hxx>
#include <ftnidx.hxx>
#include <doctxm.hxx>
#include <fmteiro.hxx>
#include <unosection.hxx>
#include <calbck.hxx>
#include <fmtclds.hxx>
#include <algorithm>
#include <utility>
#include "ndsect.hxx"

using namespace ::com::sun::star;

namespace {
    class SwIntrnlSectRefLink : public SwBaseLink
    {
        SwSectionFormat& m_rSectFormat;

    public:
        SwIntrnlSectRefLink(SwSectionFormat& rFormat, SfxLinkUpdateMode nUpdateType)
            : SwBaseLink(nUpdateType, SotClipboardFormatId::RTF)
            , m_rSectFormat(rFormat)
        {}

        virtual void Closed() override;
        virtual ::sfx2::SvBaseLink::UpdateResult DataChanged(
            const OUString& rMimeType, const css::uno::Any & rValue ) override;

        virtual const SwNode* GetAnchor() const override;
        virtual bool IsInRange( SwNodeOffset nSttNd, SwNodeOffset nEndNd ) const override;

        SwSectionNode* GetSectNode()
        {
            const SwNode* pSectNd( GetAnchor() );
            return const_cast<SwSectionNode*>( pSectNd->GetSectionNode() );
        }
    };
}

SwSectionData::SwSectionData(SectionType const eType, UIName aName)
    : m_eType(eType)
    , m_sSectionName(std::move(aName))
    , m_nPage(0)
    , m_bHiddenFlag(false)
    , m_bProtectFlag(false)
    , m_bEditInReadonlyFlag(false) // edit in readonly sections
    , m_bHidden(false)
    , m_bCondHiddenFlag(true)
    , m_bConnectFlag(true)
{
}

// this must have the same semantics as operator=()
SwSectionData::SwSectionData(SwSection const& rSection)
    : m_eType(rSection.GetType())
    , m_sSectionName(rSection.GetSectionName())
    , m_sCondition(rSection.GetCondition())
    , m_sLinkFileName(rSection.GetLinkFileName())
    , m_sLinkFilePassword(rSection.GetLinkFilePassword())
    , m_Password(rSection.GetPassword())
    , m_nPage(rSection.GetPageNum())
    , m_bHiddenFlag(rSection.IsHiddenFlag())
    , m_bProtectFlag(rSection.IsProtect())
    // edit in readonly sections
    , m_bEditInReadonlyFlag(rSection.IsEditInReadonly())
    , m_bHidden(rSection.IsHidden())
    , m_bCondHiddenFlag(true)
    , m_bConnectFlag(rSection.IsConnectFlag())
{
}

// this must have the same semantics as operator=()
SwSectionData::SwSectionData(SwSectionData const& rOther)
    : m_eType(rOther.m_eType)
    , m_sSectionName(rOther.m_sSectionName)
    , m_sCondition(rOther.m_sCondition)
    , m_sLinkFileName(rOther.m_sLinkFileName)
    , m_sLinkFilePassword(rOther.m_sLinkFilePassword)
    , m_Password(rOther.m_Password)
    , m_nPage(rOther.GetPageNum())
    , m_bHiddenFlag(rOther.m_bHiddenFlag)
    , m_bProtectFlag(rOther.m_bProtectFlag)
    // edit in readonly sections
    , m_bEditInReadonlyFlag(rOther.m_bEditInReadonlyFlag)
    , m_bHidden(rOther.m_bHidden)
    , m_bCondHiddenFlag(true)
    , m_bConnectFlag(rOther.m_bConnectFlag)
{
}

// the semantics here are weird for reasons of backward compatibility
SwSectionData & SwSectionData::operator= (SwSectionData const& rOther)
{
    m_eType = rOther.m_eType;
    m_sSectionName = rOther.m_sSectionName;
    m_sCondition = rOther.m_sCondition;
    m_sLinkFileName = rOther.m_sLinkFileName;
    m_sLinkFilePassword = rOther.m_sLinkFilePassword;
    m_bConnectFlag = rOther.m_bConnectFlag;
    m_Password = rOther.m_Password;
    m_nPage = rOther.m_nPage;

    m_bEditInReadonlyFlag = rOther.m_bEditInReadonlyFlag;
    m_bProtectFlag = rOther.m_bProtectFlag;

    m_bHidden = rOther.m_bHidden;
    // FIXME: old code did not assign m_bHiddenFlag ?
    // FIXME: why should m_bCondHiddenFlag always default to true?
    m_bCondHiddenFlag = true;

    return *this;
}

// the semantics here are weird for reasons of backward compatibility
bool SwSectionData::operator==(SwSectionData const& rOther) const
{
    return (m_eType == rOther.m_eType)
        && (m_sSectionName == rOther.m_sSectionName)
        && (m_sCondition == rOther.m_sCondition)
        && (m_bHidden == rOther.m_bHidden)
        && (m_bProtectFlag == rOther.m_bProtectFlag)
        && (m_bEditInReadonlyFlag == rOther.m_bEditInReadonlyFlag)
        && (m_sLinkFileName == rOther.m_sLinkFileName)
        && (m_sLinkFilePassword == rOther.m_sLinkFilePassword)
        && (m_Password == rOther.m_Password)
        && (m_nPage == rOther.m_nPage);
    // FIXME: old code ignored m_bCondHiddenFlag m_bHiddenFlag m_bConnectFlag
}

void SwSectionData::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SwSectionData"));
    (void)xmlTextWriterWriteFormatAttribute(pWriter, BAD_CAST("ptr"), "%p", this);
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("section-name"), BAD_CAST(m_sSectionName.toString().toUtf8().getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}

SwSection::SwSection(
        SectionType const eType, UIName const& rName, SwSectionFormat & rFormat)
    : SwClient(& rFormat)
    , m_Data(eType, rName)
{
    StartListening(rFormat.GetNotifier());

    SwSection *const pParentSect = GetParent();
    if( pParentSect )
    {
        // edit in readonly sections
        m_Data.SetEditInReadonlyFlag( pParentSect->IsEditInReadonlyFlag() );
    }

    m_Data.SetProtectFlag( rFormat.GetProtect().IsContentProtected() );

    if (!m_Data.IsEditInReadonlyFlag()) // edit in readonly sections
    {
        m_Data.SetEditInReadonlyFlag( rFormat.GetEditInReadonly().GetValue() );
    }
}

SwSection::~SwSection()
{
    SwSectionFormat* pFormat = GetFormat();
    if( !pFormat )
        return;

    SwDoc& rDoc = pFormat->GetDoc();
    if( rDoc.IsInDtor() )
    {
        // We reattach our Format to the default FrameFormat
        // to not get any dependencies
        if( pFormat->DerivedFrom() != rDoc.GetDfltFrameFormat() )
            pFormat->RegisterToFormat( *rDoc.GetDfltFrameFormat() );
    }
    else
    {
        pFormat->Remove(*this); // remove
        SvtListener::EndListeningAll();

        if (SectionType::Content != m_Data.GetType())
        {
            rDoc.getIDocumentLinksAdministration().GetLinkManager().Remove( m_RefLink.get() );
        }

        if (m_RefObj.is())
        {
            rDoc.getIDocumentLinksAdministration().GetLinkManager().RemoveServer( m_RefObj.get() );
        }

        // If the Section is the last Client in the Format we can delete it
        pFormat->RemoveAllUnos();
        if( !pFormat->HasWriterListeners() )
        {
            // Do not add to the Undo. This should've happened earlier.
            ::sw::UndoGuard const undoGuard(rDoc.GetIDocumentUndoRedo());
            rDoc.DelSectionFormat( pFormat );
        }
    }
    if (m_RefObj.is())
    {
        m_RefObj->Closed();
    }
}

void SwSection::SetSectionData(SwSectionData const& rData)
{
    bool const bOldHidden( m_Data.IsHidden() );
    bool const bOldCondHidden{m_Data.IsCondHidden()};
    m_Data = rData;
    // The next two may actually overwrite the m_Data.m_bProtect or EditInReadonly Flag
    // in Modify, which should result in same flag value as the old code!
    SetProtect(m_Data.IsProtectFlag());
    SetEditInReadonly(m_Data.IsEditInReadonlyFlag());
    if (bOldHidden != m_Data.IsHidden()
        || bOldCondHidden != m_Data.IsCondHidden()) // check if changed...
    {
        ImplSetHiddenFlag(m_Data.IsHidden(), m_Data.IsCondHidden());
    }
}

bool SwSection::DataEquals(SwSectionData const& rCmp) const
{
    // note that the old code compared the flags of the parameter with the
    // format attributes of this; the following mess should do the same...
    (void) GetLinkFileName(); // updates m_sLinkFileName
    bool const bProtect(m_Data.IsProtectFlag());
    bool const bEditInReadonly(m_Data.IsEditInReadonlyFlag());
    m_Data.SetProtectFlag(IsProtect());
    m_Data.SetEditInReadonlyFlag(IsEditInReadonly());
    bool const bResult( m_Data == rCmp );
    m_Data.SetProtectFlag(bProtect);
    m_Data.SetEditInReadonlyFlag(bEditInReadonly);
    return bResult;
}

void SwSection::ImplSetHiddenFlag(bool const bTmpHidden, bool const bCondition)
{
    SwSectionFormat* pFormat = GetFormat();
    OSL_ENSURE(pFormat, "ImplSetHiddenFlag: no format?");
    if( !pFormat )
        return;

    const bool bHide = bTmpHidden && bCondition;

    if (bHide) // should be hidden
    {
        if (!m_Data.IsHiddenFlag()) // is not hidden
        {
            // Is the Parent hidden?
            // This should be shown by the bHiddenFlag.

            // Tell all Children that they are hidden
            const sw::SectionHidden aHint;
            pFormat->CallSwClientNotify(aHint);
        }
    }
    else if (m_Data.IsHiddenFlag()) // show Nodes again
    {
        // Show all Frames
        // Only if the Parent Section is not restricting us!
        SwSection* pParentSect = pFormat->GetParentSection();
        if( !pParentSect || !pParentSect->IsHiddenFlag() )
        {
            // Tell all Children that the Parent is not hidden anymore
            const sw::SectionHidden aHint(false);
            pFormat->CallSwClientNotify(aHint);
        }
    }
}

bool SwSection::CalcHiddenFlag() const
{
    const SwSection* pSect = this;
    do {
        if( pSect->IsHidden() && pSect->IsCondHidden() )
            return true;
    } while( nullptr != ( pSect = pSect->GetParent()) );

    return false;
}

bool SwSection::IsProtect() const
{
    SwSectionFormat const *const pFormat( GetFormat() );
    OSL_ENSURE(pFormat, "SwSection::IsProtect: no format?");
    return pFormat
        ?   pFormat->GetProtect().IsContentProtected()
        :   IsProtectFlag();
}

// edit in readonly sections
bool SwSection::IsEditInReadonly() const
{
    SwSectionFormat const *const pFormat( GetFormat() );
    OSL_ENSURE(pFormat, "SwSection::IsEditInReadonly: no format?");
    return pFormat
        ?   pFormat->GetEditInReadonly().GetValue()
        :   IsEditInReadonlyFlag();
}

void SwSection::SetHidden(bool const bFlag)
{
    if (!m_Data.IsHidden() == !bFlag)
        return;

    m_Data.SetHidden(bFlag);
    ImplSetHiddenFlag(bFlag, m_Data.IsCondHidden());
}

void SwSection::SetProtect(bool const bFlag)
{
    SwSectionFormat *const pFormat( GetFormat() );
    OSL_ENSURE(pFormat, "SwSection::SetProtect: no format?");
    if (pFormat)
    {
        SvxProtectItem aItem( RES_PROTECT );
        aItem.SetContentProtect( bFlag );
        pFormat->SetFormatAttr( aItem );
        // note: this will call m_Data.SetProtectFlag via Modify!
    }
    else
    {
        m_Data.SetProtectFlag(bFlag);
    }
}

// edit in readonly sections
void SwSection::SetEditInReadonly(bool const bFlag)
{
    SwSectionFormat *const pFormat( GetFormat() );
    OSL_ENSURE(pFormat, "SwSection::SetEditInReadonly: no format?");
    if (pFormat)
    {
        SwFormatEditInReadonly aItem;
        aItem.SetValue( bFlag );
        pFormat->SetFormatAttr( aItem );
        // note: this will call m_Data.SetEditInReadonlyFlag via Modify!
    }
    else
    {
        m_Data.SetEditInReadonlyFlag(bFlag);
    }
}

void SwSection::SwClientNotify(const SwModify&, const SfxHint& rHint)
{
    Notify(rHint);
}

void SwSection::Notify(SfxHint const& rHint)
{
    if (rHint.GetId() == SfxHintId::SwSectionHidden)
    {
        auto rSectionHidden = static_cast<const sw::SectionHidden&>(rHint);
        m_Data.SetHiddenFlag(rSectionHidden.m_isHidden || (m_Data.IsHidden() && m_Data.IsCondHidden()));
        return;
    }
    if (rHint.GetId() == SfxHintId::SwObjectDying)
    {
        auto pDyingHint = static_cast<const sw::ObjectDyingHint*>(&rHint);
        CheckRegistration( *pDyingHint );
        return;
    }
    if (rHint.GetId() != SfxHintId::SwLegacyModify && rHint.GetId() != SfxHintId::SwAttrSetChange)
        return;

    bool bUpdateFootnote = false;
    if (rHint.GetId() == SfxHintId::SwLegacyModify)
    {
        auto pLegacy = static_cast<const sw::LegacyModifyHint*>(&rHint);
        auto pOld = pLegacy->m_pOld;
        auto pNew = pLegacy->m_pNew;
        switch(pLegacy->GetWhich())
        {
        case RES_PROTECT:
            if( pNew )
            {
                bool bNewFlag =
                    static_cast<const SvxProtectItem*>(pNew)->IsContentProtected();
                // this used to inherit the flag from the parent, but then there is
                // no way to turn it off in an inner section
                m_Data.SetProtectFlag( bNewFlag );
            }
            return;
        // edit in readonly sections
        case RES_EDIT_IN_READONLY:
            if( pNew )
            {
                const bool bNewFlag =
                    static_cast<const SwFormatEditInReadonly*>(pNew)->GetValue();
                m_Data.SetEditInReadonlyFlag( bNewFlag );
            }
            return;

        case RES_COL:
            // Is handled by the Layout, if appropriate
            break;

        case RES_FTN_AT_TXTEND:
        case RES_END_AT_TXTEND:
            if( pNew && pOld )
            {
                bUpdateFootnote = true;
            }
            break;
        }
    }
    else // rHint.GetId() == SfxHintId::SwAttrSetChange
    {
        auto pChangeHint = static_cast<const sw::AttrSetChangeHint*>(&rHint);
        const SwAttrSetChg* pOld = pChangeHint->m_pOld;
        const SwAttrSetChg* pNew = pChangeHint->m_pNew;
        if (pNew && pOld)
        {
            SfxItemSet* pNewSet = const_cast<SwAttrSetChg*>(pNew)->GetChgSet();
            SfxItemSet* pOldSet = const_cast<SwAttrSetChg*>(pOld)->GetChgSet();

            if( const SvxProtectItem* pItem = pNewSet->GetItemIfSet(
                        RES_PROTECT, false ) )
            {
                m_Data.SetProtectFlag( pItem->IsContentProtected() );
                pNewSet->ClearItem( RES_PROTECT );
                pOldSet->ClearItem( RES_PROTECT );
            }

            // --> edit in readonly sections
            if( const SwFormatEditInReadonly* pItem = pNewSet->GetItemIfSet(
                        RES_EDIT_IN_READONLY, false ) )
            {
                m_Data.SetEditInReadonlyFlag(pItem->GetValue());
                pNewSet->ClearItem( RES_EDIT_IN_READONLY );
                pOldSet->ClearItem( RES_EDIT_IN_READONLY );
            }

            if( SfxItemState::SET == pNewSet->GetItemState(
                        RES_FTN_AT_TXTEND, false ) ||
                SfxItemState::SET == pNewSet->GetItemState(
                        RES_END_AT_TXTEND, false ))
            {
                    bUpdateFootnote = true;
            }

            if( !pNewSet->Count() )
                return;
        }
    }

    if( bUpdateFootnote )
    {
        SwSectionNode* pSectNd = GetFormat()->GetSectionNode();
        if( pSectNd )
            pSectNd->GetDoc().GetFootnoteIdxs().UpdateFootnote(*pSectNd);
    }
}

void SwSection::SetRefObject( SwServerObject* pObj )
{
    m_RefObj = pObj;
}

void SwSection::SetCondHidden(bool const bFlag)
{
    if (!m_Data.IsCondHidden() == !bFlag)
        return;

    m_Data.SetCondHidden(bFlag);
    ImplSetHiddenFlag(m_Data.IsHidden(), bFlag);
}

// Set/remove the linked FileName
OUString const & SwSection::GetLinkFileName() const
{
    if (m_RefLink.is())
    {
        OUString sTmp;
        switch (m_Data.GetType())
        {
        case SectionType::DdeLink:
            sTmp = m_RefLink->GetLinkSourceName();
            break;

        case SectionType::FileLink:
            {
                OUString sRange;
                OUString sFilter;
                if (m_RefLink->GetLinkManager() &&
                    sfx2::LinkManager::GetDisplayNames(
                        m_RefLink.get(), nullptr, &sTmp, &sRange, &sFilter ))
                {
                    sTmp += OUStringChar(sfx2::cTokenSeparator) + sFilter
                        + OUStringChar(sfx2::cTokenSeparator) + sRange;
                }
                else if( GetFormat() && !GetFormat()->GetSectionNode() )
                {
                    // If the Section is in the UndoNodesArray, the LinkManager
                    // does not contain the Link, thus it cannot be queried for it.
                    // Thus return the current Name.
                    return m_Data.GetLinkFileName();
                }
            }
            break;
        default: break;
        }
        m_Data.SetLinkFileName(sTmp);
    }
    return m_Data.GetLinkFileName();
}

void SwSection::SetLinkFileName(const OUString& rNew)
{
    if (m_RefLink.is())
    {
        m_RefLink->SetLinkSourceName( rNew );
    }
    m_Data.SetLinkFileName(rNew);
}

// If it was a Linked Section, we need to make all Child Links visible
void SwSection::MakeChildLinksVisible( const SwSectionNode& rSectNd )
{
    const SwNode* pNd;
    const ::sfx2::SvBaseLinks& rLnks = rSectNd.GetDoc().getIDocumentLinksAdministration().GetLinkManager().GetLinks();
    for( auto n = rLnks.size(); n; )
    {
        sfx2::SvBaseLink& rBLnk = *rLnks[--n];
        if (!rBLnk.IsVisible() && dynamic_cast<const SwBaseLink*>(&rBLnk) != nullptr
            && nullptr != (pNd = static_cast<SwBaseLink&>(rBLnk).GetAnchor()))
        {
            pNd = pNd->StartOfSectionNode(); // If it's a SectionNode
            const SwSectionNode* pParent;
            while( nullptr != ( pParent = pNd->FindSectionNode() ) &&
                    ( SectionType::Content == pParent->GetSection().GetType()
                        || pNd == &rSectNd ))
                    pNd = pParent->StartOfSectionNode();

            // It's within a normal Section, so show again
            if( !pParent )
                rBLnk.SetVisible(true);
        }
    }
}

const SwTOXBase* SwSection::GetTOXBase() const
{
    const SwTOXBase* pRet = nullptr;
    if( SectionType::ToxContent == GetType() )
        pRet = dynamic_cast<const SwTOXBaseSection*>(this);
    return pRet;
}

SwSectionFormat::SwSectionFormat( SwFrameFormat* pDrvdFrame, SwDoc& rDoc )
    : SwFrameFormat( rDoc.GetAttrPool(), UIName(), pDrvdFrame )
{
    LockModify();
    SetFormatAttr( *GetDfltAttr( RES_COL ) );
    UnlockModify();
}

SwSectionFormat::~SwSectionFormat()
{
    if( GetDoc().IsInDtor() )
        return;

    SwSectionNode* pSectNd;
    const SwNodeIndex* pIdx = GetContent( false ).GetContentIdx();
    if( pIdx && &GetDoc().GetNodes() == &pIdx->GetNodes() &&
        nullptr != (pSectNd = pIdx->GetNode().GetSectionNode() ))
    {
        SwSection& rSect = pSectNd->GetSection();
        // If it was a linked Section, we need to make all Child Links
        // visible again
        if( rSect.IsConnected() )
            SwSection::MakeChildLinksVisible( *pSectNd );

        // Check whether we need to be visible, before deleting the Nodes
        if( rSect.IsHiddenFlag() )
        {
            SwSection* pParentSect = rSect.GetParent();
            if( !pParentSect || !pParentSect->IsHiddenFlag() )
            {
                // Make Nodes visible again
                rSect.SetHidden(false);
            }
        }
        // mba: test iteration; objects are removed while iterating
        // use hint which allows to specify, if the content shall be saved or not
        CallSwClientNotify( SwSectionFrameMoveAndDeleteHint( true ) );

        // Raise the Section up
        SwNodeRange aRg( *pSectNd, SwNodeOffset(0), *pSectNd->EndOfSectionNode() );
        GetDoc().GetNodes().SectionUp( &aRg );
    }
    LockModify();
    ResetFormatAttr( RES_CNTNT );
    UnlockModify();
}

SwSection * SwSectionFormat::GetSection() const
{
    return SwIterator<SwSection,SwSectionFormat>( *this ).First();
}

// Do not destroy all Frames in aDepend (Frames are recognized with a dynamic_cast).
void SwSectionFormat::DelFrames()
{
    SwSectionNode* pSectNd;
    const SwNodeIndex* pIdx = GetContent(false).GetContentIdx();
    if( pIdx && &GetDoc().GetNodes() == &pIdx->GetNodes() &&
        nullptr != (pSectNd = pIdx->GetNode().GetSectionNode() ))
    {
        // First delete the <SwSectionFrame> of the <SwSectionFormat> instance
        // mba: test iteration as objects are removed in iteration
        // use hint which allows to specify, if the content shall be saved or not
        CallSwClientNotify( SwSectionFrameMoveAndDeleteHint( false ) );

        // Then delete frames of the nested <SwSectionFormat> instances
        SwIterator<SwSectionFormat,SwSectionFormat> aIter( *this );
        SwSectionFormat *pLast = aIter.First();
        while ( pLast )
        {
            pLast->DelFrames();
            pLast = aIter.Next();
        }

        SwNodeOffset nEnd = pSectNd->EndOfSectionIndex();
        SwNodeOffset nStart = pSectNd->GetIndex()+1;
        sw_DeleteFootnote( pSectNd, nStart, nEnd );
    }
    if( !pIdx )
        return;

    // Send Hint for PageDesc. Actually the Layout contained in the
    // Paste of the Frame itself would need to do this. But that leads
    // to subsequent errors, which we'd need to solve at run-time.
    SwNodeIndex aNextNd( *pIdx );
    SwContentNode* pCNd = SwNodes::GoNextSection(&aNextNd, true, false);
    if( pCNd )
    {
        const SfxPoolItem& rItem = pCNd->GetSwAttrSet().Get(RES_PAGEDESC);
        pCNd->CallSwClientNotify(sw::LegacyModifyHint(&rItem, &rItem));
    }
}

// Create the Views
void SwSectionFormat::MakeFrames()
{
    SwSectionNode* pSectNd;
    const SwNodeIndex* pIdx = GetContent(false).GetContentIdx();

    if( pIdx && &GetDoc().GetNodes() == &pIdx->GetNodes() &&
        nullptr != (pSectNd = pIdx->GetNode().GetSectionNode() ))
    {
        SwNodeIndex aIdx( *pIdx );
        pSectNd->MakeOwnFrames( &aIdx );
    }
}

void SwSectionFormat::SwClientNotify(const SwModify& rMod, const SfxHint& rHint)
{
    if (rHint.GetId() == SfxHintId::SwSectionHidden)
    {
        auto rSectionHidden = static_cast<const sw::SectionHidden&>(rHint);
        auto pSect = GetSection();
        if(!pSect || rSectionHidden.m_isHidden == pSect->IsHiddenFlag()) // already at target state, skipping.
            return;
        GetNotifier().Broadcast(rSectionHidden);
        return;
    }
    else if(SfxHintId::SwRemoveUnoObject == rHint.GetId())
    {
        SwFrameFormat::SwClientNotify(rMod, rHint);
        // invalidate cached uno object
        SetXTextSection(nullptr);
        return;
    }
    else if (rHint.GetId() == SfxHintId::SwFormatChange)
    {
        auto pChangeHint = static_cast<const SwFormatChangeHint*>(&rHint);
        if( !GetDoc().IsInDtor() &&
            pChangeHint->m_pNewFormat == static_cast<void*>(GetRegisteredIn()) &&
            dynamic_cast<const SwSectionFormat*>(pChangeHint->m_pNewFormat) != nullptr )
        {
            // My Parent will be changed, thus I need to update
            SwFrameFormat::SwClientNotify(rMod, rHint);
            UpdateParent();
            return;
        }
        SwFrameFormat::SwClientNotify(rMod, rHint);
        return;
    }
    else if (rHint.GetId() == SfxHintId::SwAttrSetChange)
    {
        auto pChangeHint = static_cast<const sw::AttrSetChangeHint*>(&rHint);
        const SwAttrSetChg* pOld = pChangeHint->m_pOld;
        const SwAttrSetChg* pNew = pChangeHint->m_pNew;
        if (HasWriterListeners() && pOld && pNew)
        {
            SfxItemSet* pNewSet = const_cast<SwAttrSetChg*>(pNew)->GetChgSet();
            SfxItemSet* pOldSet = const_cast<SwAttrSetChg*>(pOld)->GetChgSet();
            const SfxPoolItem *pItem;
            if( SfxItemState::SET == pNewSet->GetItemState(
                                        RES_PROTECT, false, &pItem ))
            {
                GetNotifier().Broadcast(sw::LegacyModifyHint(pItem, pItem));
                pNewSet->ClearItem( RES_PROTECT );
                pOldSet->ClearItem( RES_PROTECT );
            }

            // --> edit in readonly sections
            if( SfxItemState::SET == pNewSet->GetItemState(
                        RES_EDIT_IN_READONLY, false, &pItem ) )
            {
                GetNotifier().Broadcast(sw::LegacyModifyHint(pItem, pItem));
                pNewSet->ClearItem( RES_EDIT_IN_READONLY );
                pOldSet->ClearItem( RES_EDIT_IN_READONLY );
            }

            if( SfxItemState::SET == pNewSet->GetItemState(
                                    RES_FTN_AT_TXTEND, false, &pItem ))
            {
                GetNotifier().Broadcast(sw::LegacyModifyHint(pItem, pItem));
                pNewSet->ClearItem( RES_FTN_AT_TXTEND );
                pOldSet->ClearItem( RES_FTN_AT_TXTEND );
            }
            if( SfxItemState::SET == pNewSet->GetItemState(
                                    RES_END_AT_TXTEND, false, &pItem ))
            {
                GetNotifier().Broadcast(sw::LegacyModifyHint(pItem, pItem));
                pNewSet->ClearItem( RES_END_AT_TXTEND );
                pOldSet->ClearItem( RES_END_AT_TXTEND );
            }
            if( !pOld->GetChgSet()->Count() )
                return;
        }
        SwFrameFormat::SwClientNotify(rMod, rHint);
        return;
    }
    else if (rHint.GetId() == SfxHintId::SwObjectDying)
    {
        auto pDyingHint = static_cast<const sw::ObjectDyingHint*>(&rHint);
        if( !GetDoc().IsInDtor() &&
            pDyingHint->m_pDying == GetRegisteredIn() )
        {
            // My Parents will be destroyed, so get the Parent's Parent
            // and update
            SwFrameFormat::SwClientNotify(rMod, rHint);
            UpdateParent();
        }
        else
            SwFrameFormat::SwClientNotify(rMod, rHint);
        return;
    }
    else if (rHint.GetId() == SfxHintId::SwUpdateAttr)
    {
        SwFrameFormat::SwClientNotify(rMod, rHint);
        return;
    }
    else if (rHint.GetId() != SfxHintId::SwLegacyModify)
        return;
    auto pLegacy = static_cast<const sw::LegacyModifyHint*>(&rHint);
    sal_uInt16 nWhich = pLegacy->GetWhich();
    auto pOld = pLegacy->m_pOld;
    auto pNew = pLegacy->m_pNew;
    switch( nWhich )
    {
    case RES_FTN_AT_TXTEND:
    case RES_END_AT_TXTEND:
    case RES_PROTECT:
    case RES_EDIT_IN_READONLY: // edit in readonly sections
        // Pass through these Messages until the End of the tree!
        GetNotifier().Broadcast(sw::LegacyModifyHint(pOld, pNew));
        return; // That's it!
    }
    SwFrameFormat::SwClientNotify(rMod, rHint);
}

void SwSectionFormat::SetXTextSection(rtl::Reference<SwXTextSection> const& xTextSection)
{
    m_wXTextSection = xTextSection.get();
}

bool SwSectionFormat::IsVisible() const
{
    if(SwFrameFormat::IsVisible())
        return true;
    SwIterator<SwSectionFormat,SwSectionFormat> aFormatIter(*this);
    for(SwSectionFormat* pChild = aFormatIter.First(); pChild; pChild = aFormatIter.Next())
        if(pChild->IsVisible())
            return true;
    return false;
}

// Get info from the Format
bool SwSectionFormat::GetInfo(SwFindNearestNode& rInfo) const
{
    if(GetFormatAttr( RES_PAGEDESC ).GetPageDesc())
    {
        const SwSectionNode* pNd = GetSectionNode();
        if(pNd)
            rInfo.CheckNode(*pNd);
    }
    return true;
}

static bool lcl_SectionCmpPos( const SwSection *pFirst, const SwSection *pSecond)
{
    const SwSectionFormat* pFSectFormat = pFirst->GetFormat();
    const SwSectionFormat* pSSectFormat = pSecond->GetFormat();
    assert( pFSectFormat && pSSectFormat &&
            pFSectFormat->GetContent(false).GetContentIdx() &&
            pSSectFormat->GetContent(false).GetContentIdx() &&
                "Invalid sections" );
    return pFSectFormat->GetContent(false).GetContentIdx()->GetIndex() <
                  pSSectFormat->GetContent(false).GetContentIdx()->GetIndex();
}

// get all Sections that have been derived from this one
void SwSectionFormat::GetChildSections( SwSections& rArr,
                                        SectionSort eSort,
                                        bool bAllSections ) const
{
    rArr.clear();

    if( !HasWriterListeners() )
        return;

    SwIterator<SwSectionFormat,SwSectionFormat> aIter(*this);
    const SwNodeIndex* pIdx;
    for( SwSectionFormat* pLast = aIter.First(); pLast; pLast = aIter.Next() )
        if( bAllSections ||
            ( nullptr != ( pIdx = pLast->GetContent(false).
            GetContentIdx()) && &pIdx->GetNodes() == &GetDoc().GetNodes() ))
        {
            SwSection* pDummy = pLast->GetSection();
            rArr.push_back( pDummy );
        }

    // Do we need any sorting?
    if( 1 < rArr.size() )
        switch( eSort )
        {
        case SectionSort::Pos:
            std::sort( rArr.begin(), rArr.end(), lcl_SectionCmpPos );
            break;
        case SectionSort::Not: break;
        }
}

// See whether the Section is within the Nodes or the UndoNodes array
bool SwSectionFormat::IsInNodesArr() const
{
    const SwNodeIndex* pIdx = GetContent(false).GetContentIdx();
    return pIdx && &pIdx->GetNodes() == &GetDoc().GetNodes();
}

// Parent was changed
void SwSectionFormat::UpdateParent()
{
    if(!HasWriterListeners())
        return;

    const SwSection* pSection = GetSection();
    const SvxProtectItem* pProtect = &GetProtect();
    // edit in readonly sections
    const SwFormatEditInReadonly* pEditInReadonly = &GetEditInReadonly();
    bool bIsHidden = pSection->IsHidden();
    if(GetRegisteredIn())
    {
        const SwSection* pPS = GetParentSection();
        pProtect = &pPS->GetFormat()->GetProtect();
        pEditInReadonly = &pPS->GetFormat()->GetEditInReadonly();
        bIsHidden = pPS->IsHiddenFlag();
    }
    if(!pProtect->IsContentProtected() != !pSection->IsProtectFlag())
        CallSwClientNotify(sw::LegacyModifyHint(pProtect, pProtect));

    // edit in readonly sections
    if(!pEditInReadonly->GetValue() != !pSection->IsEditInReadonlyFlag())
        CallSwClientNotify(sw::LegacyModifyHint(pEditInReadonly, pEditInReadonly));

    if(bIsHidden == pSection->IsHiddenFlag())
    {
        const sw::SectionHidden aHint(bIsHidden);
        CallSwClientNotify(aHint);
    }
}

SwSectionNode* SwSectionFormat::GetSectionNode()
{
    const SwNodeIndex* pIdx = GetContent(false).GetContentIdx();
    if( pIdx && ( &pIdx->GetNodes() == &GetDoc().GetNodes() ))
        return pIdx->GetNode().GetSectionNode();
    return nullptr;
}

// Is this Section valid for the GlobalDocument?
const SwSection* SwSectionFormat::GetGlobalDocSection() const
{
    const SwSectionNode* pNd = GetSectionNode();
    if( pNd &&
        ( SectionType::FileLink == pNd->GetSection().GetType() ||
          SectionType::ToxContent == pNd->GetSection().GetType() ) &&
        pNd->GetIndex() > pNd->GetNodes().GetEndOfExtras().GetIndex() &&
        !pNd->StartOfSectionNode()->IsSectionNode() &&
        !pNd->StartOfSectionNode()->FindSectionNode() )
        return &pNd->GetSection();
    return nullptr;
}

// sw::Metadatable
::sfx2::IXmlIdRegistry& SwSectionFormat::GetRegistry()
{
    return GetDoc().GetXmlIdRegistry();
}

bool SwSectionFormat::IsInClipboard() const
{
    return GetDoc().IsClipBoard();
}

bool SwSectionFormat::IsInUndo() const
{
    return !IsInNodesArr();
}

bool SwSectionFormat::IsInContent() const
{
    SwNodeIndex const*const pIdx = GetContent(false).GetContentIdx();
    OSL_ENSURE(pIdx, "SwSectionFormat::IsInContent: no index?");
    return pIdx == nullptr || !GetDoc().IsInHeaderFooter(pIdx->GetNode());
}

// n.b.: if the section format represents an index, then there is both a
// SwXDocumentIndex and a SwXTextSection instance for this single core object.
// these two can both implement XMetadatable and forward to the same core
// section format.  but here only one UNO object can be returned,
// so always return the text section.
uno::Reference< rdf::XMetadatable >
SwSectionFormat::MakeUnoObject()
{
    rtl::Reference<SwXTextSection> xMeta;
    SwSection *const pSection( GetSection() );
    if (pSection)
    {
        xMeta = SwXTextSection::CreateXTextSection(this,
                        SectionType::ToxHeader == pSection->GetType());
    }
    return xMeta;
}

bool SwSectionFormat::supportsFullDrawingLayerFillAttributeSet() const
{
    return false;
}

void SwSectionFormat::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SwSectionFormat"));
    SwFormat::dumpAsXml(pWriter);
    (void)xmlTextWriterEndElement(pWriter);
}

void SwSectionFormats::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SwSectionFormats"));
    for (size_t i = 0; i < size(); ++i)
        GetFormat(i)->dumpAsXml(pWriter);
    (void)xmlTextWriterEndElement(pWriter);
}

// Method to break section links inside a linked section
static void lcl_BreakSectionLinksInSect( const SwSectionNode& rSectNd )
{
    if ( !rSectNd.GetSection().IsConnected() )
    {
        OSL_FAIL( "method <lcl_RemoveSectionLinksInSect(..)> - no Link at Section of SectionNode" );
        return;
    }
    const ::sfx2::SvBaseLink* pOwnLink( &(rSectNd.GetSection().GetBaseLink() ) );
    const ::sfx2::SvBaseLinks& rLnks = rSectNd.GetDoc().getIDocumentLinksAdministration().GetLinkManager().GetLinks();
    for ( auto n = rLnks.size(); n > 0; )
    {
        SwIntrnlSectRefLink* pSectLnk = dynamic_cast<SwIntrnlSectRefLink*>(&(*rLnks[ --n ]));
        if ( pSectLnk && pSectLnk != pOwnLink &&
             pSectLnk->IsInRange( rSectNd.GetIndex(), rSectNd.EndOfSectionIndex() ) )
        {
            // break the link of the corresponding section.
            // the link is also removed from the link manager
            SwSectionNode* pSectNode = pSectLnk->GetSectNode();
            assert(pSectNode);
            pSectNode->GetSection().BreakLink();

            // for robustness, because link is removed from the link manager
            if ( n > rLnks.size() )
            {
                n = rLnks.size();
            }
        }
    }
}

static void lcl_UpdateLinksInSect( const SwBaseLink& rUpdLnk, SwSectionNode& rSectNd )
{
    SwDoc& rDoc = rSectNd.GetDoc();
    SwDocShell* pDShell = rDoc.GetDocShell();
    if( !pDShell || !pDShell->GetMedium() )
        return ;

    const OUString sName( pDShell->GetMedium()->GetName() );
    const OUString sMimeType( SotExchange::GetFormatMimeType( SotClipboardFormatId::SIMPLE_FILE ));
    uno::Any aValue;
    aValue <<= sName; // Arbitrary name

    const ::sfx2::SvBaseLinks& rLnks = rDoc.getIDocumentLinksAdministration().GetLinkManager().GetLinks();
    for( auto n = rLnks.size(); n; )
    {
        ::sfx2::SvBaseLink* pLnk = &(*rLnks[ --n ]);
        if( pLnk == &rUpdLnk )
            continue;
        if( sfx2::SvBaseLinkObjectType::ClientFile != pLnk->GetObjType() )
            continue;
        SwBaseLink* pBLink = dynamic_cast<SwBaseLink*>( pLnk );
        if( pBLink && pBLink->IsInRange( rSectNd.GetIndex(),
                                        rSectNd.EndOfSectionIndex() ) )
        {
            // It's in the Section, so update. But only if it's not in the same File!
            OUString sFName;
            sfx2::LinkManager::GetDisplayNames( pBLink, nullptr, &sFName );
            if( sFName != sName )
            {
                pBLink->DataChanged( sMimeType, aValue );

                // If needed find the Link pointer to avoid skipping one or calling one twice
                if( n >= rLnks.size() && 0 != ( n = rLnks.size() ))
                    --n;

                if( n && pLnk != &(*rLnks[ n ]) )
                {
                    // Find - it can only precede it!
                    while( n )
                        if( pLnk == &(*rLnks[ --n ] ) )
                            break;
                }
            }
        }
    }
}

::sfx2::SvBaseLink::UpdateResult SwIntrnlSectRefLink::DataChanged(
    const OUString& rMimeType, const uno::Any & rValue )
{
    SwSectionNode* pSectNd = m_rSectFormat.GetSectionNode();
    SwDoc& rDoc = m_rSectFormat.GetDoc();

    SotClipboardFormatId nDataFormat = SotExchange::GetFormatIdFromMimeType( rMimeType );

    if( !pSectNd || rDoc.IsInDtor() || ChkNoDataFlag() ||
        sfx2::LinkManager::RegisterStatusInfoId() == nDataFormat )
    {
        // Should we be in the Undo already?
        return SUCCESS;
    }

    //  #i38810# - Due to possible existing signatures, the
    // document has to be modified after updating a link.
    rDoc.getIDocumentState().SetModified();
    // set additional flag that links have been updated, in order to check this
    // during load.
    rDoc.getIDocumentLinksAdministration().SetLinksUpdated( true );

    // Always switch off Undo
    bool const bWasUndo = rDoc.GetIDocumentUndoRedo().DoesUndo();
    rDoc.GetIDocumentUndoRedo().DoUndo(false);
    bool bWasVisibleLinks = rDoc.getIDocumentLinksAdministration().IsVisibleLinks();
    rDoc.getIDocumentLinksAdministration().SetVisibleLinks( false );

    SwPaM* pPam;
    SwViewShell* pVSh = rDoc.getIDocumentLayoutAccess().GetCurrentViewShell();
    SwEditShell* pESh = rDoc.GetEditShell();
    rDoc.getIDocumentFieldsAccess().LockExpFields();
    {
        // Insert an empty TextNode at the Section's start
        SwNodeIndex aIdx( *pSectNd, +1 );
        SwNodeIndex aEndIdx( *pSectNd->EndOfSectionNode() );
        rDoc.GetNodes().MakeTextNode( aIdx.GetNode(),
                        rDoc.getIDocumentStylePoolAccess().GetTextCollFromPool( RES_POOLCOLL_TEXT ) );

        if( pESh )
            pESh->StartAllAction();
        else if( pVSh )
            pVSh->StartAction();

        SwPosition aPos( aIdx, SwNodeOffset(-1) );
        SwDoc::CorrAbs( aIdx, aEndIdx, aPos, true );

        pPam = new SwPaM( aPos );

        // Delete everything succeeding it
        --aIdx;
        DelFlyInRange( aIdx.GetNode(), aEndIdx.GetNode() );
        DelBookmarks(aIdx.GetNode(), aEndIdx.GetNode());
        ++aIdx;

        rDoc.GetNodes().Delete( aIdx, aEndIdx.GetIndex() - aIdx.GetIndex() );
    }

    SwSection& rSection = pSectNd->GetSection();
    rSection.SetConnectFlag(false);

    Reader* pRead = nullptr;
    switch( nDataFormat )
    {
    case SotClipboardFormatId::STRING:
        pRead = ReadAscii;
        break;

    case SotClipboardFormatId::RICHTEXT:
    case SotClipboardFormatId::RTF:
        pRead = SwReaderWriter::GetRtfReader();
        break;

    case SotClipboardFormatId::SIMPLE_FILE:
        if ( rValue.hasValue() )
        {
            OUString sFileName;
            if ( !(rValue >>= sFileName) )
                break;
            OUString sFilter;
            OUString sRange;
            sfx2::LinkManager::GetDisplayNames( this, nullptr, &sFileName,
                                                    &sRange, &sFilter );

            RedlineFlags eOldRedlineFlags = RedlineFlags::NONE;
            SfxObjectShellRef xDocSh;
            SfxObjectShellLock xLockRef;
            int nRet;
            if( sFileName.isEmpty() )
            {
                xDocSh = rDoc.GetDocShell();
                nRet = 1;
            }
            else
            {
                nRet = SwFindDocShell( xDocSh, xLockRef, sFileName,
                                    rSection.GetLinkFilePassword(),
                                    sFilter, 0, rDoc.GetDocShell() );
                if( nRet )
                {
                    SwDoc* pSrcDoc = static_cast<SwDocShell*>( xDocSh.get() )->GetDoc();
                    eOldRedlineFlags = pSrcDoc->getIDocumentRedlineAccess().GetRedlineFlags();
                    pSrcDoc->getIDocumentRedlineAccess().SetRedlineFlags( RedlineFlags::ShowInsert );
                }
            }

            if( nRet )
            {
                rSection.SetConnectFlag();

                SwNodeIndex aSave( pPam->GetPoint()->GetNode(), -1 );
                std::optional<SwNodeRange> oCpyRg;

                if( xDocSh->GetMedium() &&
                    rSection.GetLinkFilePassword().isEmpty() )
                {
                    if( const SfxStringItem* pItem = xDocSh->GetMedium()->GetItemSet().
                        GetItemIfSet( SID_PASSWORD, false ) )
                        rSection.SetLinkFilePassword( pItem->GetValue() );
                }

                SwDoc* pSrcDoc = static_cast<SwDocShell*>( xDocSh.get() )->GetDoc();

                if( !sRange.isEmpty() )
                {
                    // Catch recursion
                    bool bRecursion = false;
                    if( pSrcDoc == &rDoc )
                    {
                        tools::SvRef<SwServerObject> refObj( static_cast<SwServerObject*>(
                                        rDoc.getIDocumentLinksAdministration().CreateLinkSource( sRange )));
                        if( refObj.is() )
                        {
                            bRecursion = refObj->IsLinkInServer( this ) ||
                                        ChkNoDataFlag();
                        }
                    }

                    SwNode& rInsPos = pPam->GetPoint()->GetNode();

                    SwPaM* pCpyPam = nullptr;
                    if( !bRecursion &&
                        pSrcDoc->GetDocumentLinksAdministrationManager().SelectServerObj( sRange, pCpyPam, oCpyRg )
                        && pCpyPam )
                    {
                        if( pSrcDoc != &rDoc ||
                            pCpyPam->Start()->GetNode() > rInsPos ||
                            rInsPos >= pCpyPam->End()->GetNode() )
                        {
                            pSrcDoc->getIDocumentContentOperations().CopyRange(*pCpyPam, *pPam->GetPoint(), SwCopyFlags::CheckPosInFly);
                        }
                        delete pCpyPam;
                    }
                    if( oCpyRg && pSrcDoc == &rDoc &&
                        oCpyRg->aStart < rInsPos && rInsPos < oCpyRg->aEnd.GetNode() )
                    {
                        oCpyRg.reset();
                    }
                }
                else if( pSrcDoc != &rDoc )
                {
                    // before update, remove obsolete page-anchored flys from the target master document
                    auto pFormats = rDoc.GetSpzFrameFormats();
                    for( sal_uInt16 nCnt = pFormats->size(); nCnt; )
                    {
                        SwFrameFormat* pFormat = (*pFormats)[ --nCnt ];
                        SwFormatAnchor aAnchor( pFormat->GetAnchor() );
                        if ( RndStdIds::FLY_AT_PAGE == aAnchor.GetAnchorId() &&
                                pFormat->GetName().toString().indexOf(sFileName) > -1 )
                        {
                            rDoc.getIDocumentLayoutAccess().DelLayoutFormat( pFormat );
                        }
                    }

                    // store page count of the source document to calculate
                    // the physical page number of the objects anchored at page
                    const SwDocStat& rDStat = pSrcDoc->getIDocumentStatistics().GetDocStat();
                    m_rSectFormat.GetSection()->SetPageNum(rDStat.nPage);

                    // tdf#121119 keep objects anchored at page
                    auto pSrcFormats = pSrcDoc->GetSpzFrameFormats();
                    sal_uInt32 nPrevPages = 0;
                    for( sw::SpzFrameFormat* pCpyFormat: *pSrcFormats)
                    {
                        SwFormatAnchor aAnchor( pCpyFormat->GetAnchor() );
                        if ( RndStdIds::FLY_AT_PAGE == aAnchor.GetAnchorId() )
                        {
                            // add file name of the source document to the name of the copied object
                            // Note: used for the recognition of the copied objects anchored at page
                            pCpyFormat->SetFormatName( UIName(pCpyFormat->GetName().toString() + " (" + sFileName + ")") );

                            // sum page counts of the previous sections
                            if ( nPrevPages == 0 )
                            {
                                const SwSectionFormats& rFormats = rDoc.GetSections();
                                for( size_t n = 0; n < rFormats.size() && rFormats[n] != &m_rSectFormat; ++n )
                                {
                                    if ( const SwSection * pGlobalDocSection = rFormats[n]->GetGlobalDocSection() )
                                        nPrevPages += pGlobalDocSection->GetPageNum();
                                }
                            }

                            // set corrected physical page number of the object
                            aAnchor.SetPageNum( nPrevPages + aAnchor.GetPageNum() );

                            // copy object anchored at page to the target document
                            rDoc.getIDocumentLayoutAccess().CopyLayoutFormat( *pCpyFormat, aAnchor, true, true );
                        }
                    }

                    oCpyRg.emplace( pSrcDoc->GetNodes().GetEndOfExtras(), SwNodeOffset(2),
                                          pSrcDoc->GetNodes().GetEndOfContent() );
                }

                // #i81653#
                // Update links of extern linked document or extern linked
                // document section, if section is protected.
                if ( pSrcDoc != &rDoc &&
                     rSection.IsProtectFlag() )
                {
                    pSrcDoc->getIDocumentLinksAdministration().GetLinkManager().UpdateAllLinks( false, false, nullptr, u""_ustr );
                }

                if( oCpyRg )
                {
                    SwNode& rInsPos = pPam->GetPoint()->GetNode();
                    bool bCreateFrame = rInsPos <= rDoc.GetNodes().GetEndOfExtras() ||
                                rInsPos.FindTableNode();

                    SwTableNumFormatMerge aTNFM( *pSrcDoc, rDoc );

                    pSrcDoc->GetDocumentContentOperationsManager().CopyWithFlyInFly(*oCpyRg, rInsPos, nullptr, bCreateFrame);
                    ++aSave;

                    if( !bCreateFrame )
                        ::MakeFrames( rDoc, aSave.GetNode(), rInsPos );

                    // Delete last Node, only if it was copied successfully
                    // (the Section contains more than one Node)
                    if( SwNodeOffset(2) < pSectNd->EndOfSectionIndex() - pSectNd->GetIndex() )
                    {
                        aSave = rInsPos;
                        pPam->Move( fnMoveBackward, GoInNode );
                        pPam->SetMark(); // Rewire both SwPositions

                        rDoc.CorrAbs( aSave.GetNode(), *pPam->GetPoint(), 0, true );
                        rDoc.GetNodes().Delete( aSave );
                    }
                    oCpyRg.reset();
                }

                lcl_BreakSectionLinksInSect( *pSectNd );

                // Update all Links in this Section
                lcl_UpdateLinksInSect( *this, *pSectNd );
            }
            if( xDocSh.is() )
            {
                if( 2 == nRet )
                    xDocSh->DoClose();
                else if( static_cast<SwDocShell*>( xDocSh.get() )->GetDoc() )
                    static_cast<SwDocShell*>( xDocSh.get() )->GetDoc()->getIDocumentRedlineAccess().SetRedlineFlags(
                                eOldRedlineFlags );
            }
        }
        break;
    default: break;
    }

    // Only create DDE if Shell is available!
    uno::Sequence< sal_Int8 > aSeq;
    if( pRead && rValue.hasValue() && ( rValue >>= aSeq ) )
    {
        if( pESh )
        {
            pESh->Push();
            SwPaM* pCursor = pESh->GetCursor();
            *pCursor->GetPoint() = *pPam->GetPoint();
            delete pPam;
            pPam = pCursor;
        }

        if (SwDocShell* pShell = rDoc.GetDocShell())
        {
            SvMemoryStream aStrm( const_cast<sal_Int8 *>(aSeq.getConstArray()), aSeq.getLength(),
                                    StreamMode::READ );
            aStrm.Seek( 0 );

            // TODO/MBA: it's impossible to set a BaseURL here!
            SwReader aTmpReader( aStrm, OUString(), pShell->GetMedium()->GetBaseURL(), *pPam );

            if( ! aTmpReader.Read( *pRead ).IsError() )
            {
                rSection.SetConnectFlag();
            }
        }

        if( pESh )
        {
            pESh->Pop(SwCursorShell::PopMode::DeleteCurrent);
            pPam = nullptr; // pam was deleted earlier
        }
    }

    // remove all undo actions and turn undo on again
    rDoc.GetIDocumentUndoRedo().DelAllUndoObj();
    rDoc.GetIDocumentUndoRedo().DoUndo(bWasUndo);
    rDoc.getIDocumentLinksAdministration().SetVisibleLinks( bWasVisibleLinks );

    rDoc.getIDocumentFieldsAccess().UnlockExpFields();
    if( !rDoc.getIDocumentFieldsAccess().IsExpFieldsLocked() )
        rDoc.getIDocumentFieldsAccess().UpdateExpFields(nullptr, true);

    if( pESh )
        pESh->EndAllAction();
    else if( pVSh )
        pVSh->EndAction();
    delete pPam; // Was created at the start

    return SUCCESS;
}

void SwIntrnlSectRefLink::Closed()
{
    SwDoc& rDoc = m_rSectFormat.GetDoc();
    if( !rDoc.IsInDtor() )
    {
        // Advise says goodbye: mark the Section as not protected
        // and change the Flag
        const SwSectionFormats& rFormats = rDoc.GetSections();
        for( auto n = rFormats.size(); n; )
            if (rFormats[--n] == &m_rSectFormat)
            {
                SwViewShell* pSh = rDoc.getIDocumentLayoutAccess().GetCurrentViewShell();
                SwEditShell* pESh = rDoc.GetEditShell();

                if( pESh )
                    pESh->StartAllAction();
                else
                    pSh->StartAction();

                SwSectionData aSectionData(*m_rSectFormat.GetSection());
                aSectionData.SetType( SectionType::Content );
                aSectionData.SetLinkFileName( OUString() );
                aSectionData.SetProtectFlag( false );
                // edit in readonly sections
                aSectionData.SetEditInReadonlyFlag( false );

                aSectionData.SetConnectFlag( false );

                rDoc.UpdateSection( n, aSectionData );

                // Make all Links within the Section visible again
                SwSectionNode* pSectNd = m_rSectFormat.GetSectionNode();
                if( pSectNd )
                    SwSection::MakeChildLinksVisible( *pSectNd );

                if( pESh )
                    pESh->EndAllAction();
                else
                    pSh->EndAction();
                break;
            }
    }
    SvBaseLink::Closed();
}

void SwSection::CreateLink( LinkCreateType eCreateType )
{
    SwSectionFormat* pFormat = GetFormat();
    OSL_ENSURE(pFormat, "SwSection::CreateLink: no format?");
    if (!pFormat || (SectionType::Content == m_Data.GetType()))
        return ;

    SfxLinkUpdateMode nUpdateType = SfxLinkUpdateMode::ALWAYS;

    if (!m_RefLink.is())
    {
        // create BaseLink
        m_RefLink = new SwIntrnlSectRefLink( *pFormat, nUpdateType );
    }
    else
    {
        pFormat->GetDoc().getIDocumentLinksAdministration().GetLinkManager().Remove( m_RefLink.get() );
    }

    SwIntrnlSectRefLink *const pLnk =
        static_cast<SwIntrnlSectRefLink*>( m_RefLink.get() );

    const OUString sCmd(m_Data.GetLinkFileName());
    pLnk->SetUpdateMode( nUpdateType );
    pLnk->SetVisible( pFormat->GetDoc().getIDocumentLinksAdministration().IsVisibleLinks() );

    switch (m_Data.GetType())
    {
    case SectionType::DdeLink:
        pLnk->SetLinkSourceName( sCmd );
        pFormat->GetDoc().getIDocumentLinksAdministration().GetLinkManager().InsertDDELink( pLnk );
        break;
    case SectionType::FileLink:
        {
            pLnk->SetContentType( SotClipboardFormatId::SIMPLE_FILE );
            sal_Int32 nIndex = 0;
            const OUString sFile(sCmd.getToken( 0, sfx2::cTokenSeparator, nIndex ));
            const OUString sFltr(sCmd.getToken( 0, sfx2::cTokenSeparator, nIndex ));
            const OUString sRange(sCmd.getToken( 0, sfx2::cTokenSeparator, nIndex ));
            pFormat->GetDoc().getIDocumentLinksAdministration().GetLinkManager().InsertFileLink( *pLnk,
                                static_cast<sfx2::SvBaseLinkObjectType>(m_Data.GetType()),
                                sFile,
                                ( !sFltr.isEmpty() ? &sFltr : nullptr ),
                                ( !sRange.isEmpty() ? &sRange : nullptr ) );
        }
        break;
    default:
        OSL_ENSURE( false, "What kind of Link is this?" );
    }

    switch( eCreateType )
    {
    case LinkCreateType::Connect: // Connect Link right away
        pLnk->Connect();
        break;

    case LinkCreateType::Update: // Connect Link and update
        pLnk->Update();
        break;
    case LinkCreateType::NONE: break;
    }
}

void SwSection::BreakLink()
{
    const SectionType eCurrentType( GetType() );
    if ( eCurrentType == SectionType::Content ||
         eCurrentType == SectionType::ToxHeader ||
         eCurrentType == SectionType::ToxContent )
    {
        // nothing to do
        return;
    }

    // Release link, if it exists
    if (m_RefLink.is())
    {
        SwSectionFormat *const pFormat( GetFormat() );
        OSL_ENSURE(pFormat, "SwSection::BreakLink: no format?");
        if (pFormat)
        {
            pFormat->GetDoc().getIDocumentLinksAdministration().GetLinkManager().Remove( m_RefLink.get() );
        }
        m_RefLink.clear();
    }
    // change type
    SetType( SectionType::Content );
    // reset linked file data
    SetLinkFileName( OUString() );
    SetLinkFilePassword( OUString() );
}

void SwSection::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SwSection"));
    (void)xmlTextWriterWriteFormatAttribute(pWriter, BAD_CAST("ptr"), "%p", this);
    (void)xmlTextWriterWriteFormatAttribute(pWriter, BAD_CAST("registered-in"), "%p",
                                            GetRegisteredIn());
    m_Data.dumpAsXml(pWriter);
    (void)xmlTextWriterEndElement(pWriter);
}

const SwNode* SwIntrnlSectRefLink::GetAnchor() const { return m_rSectFormat.GetSectionNode(); }

bool SwIntrnlSectRefLink::IsInRange( SwNodeOffset nSttNd, SwNodeOffset nEndNd ) const
{
    SwStartNode* pSttNd = m_rSectFormat.GetSectionNode();
    return pSttNd &&
            nSttNd < pSttNd->GetIndex() &&
            pSttNd->EndOfSectionIndex() < nEndNd;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
