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

#include <rolbck.hxx>

#include <libxml/xmlwriter.h>

#include <svl/itemiter.hxx>
#include <editeng/formatbreakitem.hxx>
#include <hints.hxx>
#include <hintids.hxx>
#include <fmtftn.hxx>
#include <fchrfmt.hxx>
#include <fmtflcnt.hxx>
#include <fmtrfmrk.hxx>
#include <fmtfld.hxx>
#include <fmtpdsc.hxx>
#include <txtfld.hxx>
#include <txtrfmrk.hxx>
#include <txttxmrk.hxx>
#include <txtftn.hxx>
#include <txtflcnt.hxx>
#include <fmtanchr.hxx>
#include <fmtcnct.hxx>
#include <frmfmt.hxx>
#include <ftnidx.hxx>
#include <doc.hxx>
#include <IDocumentUndoRedo.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <IDocumentState.hxx>
#include <docary.hxx>
#include <ndtxt.hxx>
#include <paratr.hxx>
#include <cellatr.hxx>
#include <fldbas.hxx>
#include <docufld.hxx>
#include <pam.hxx>
#include <swtable.hxx>
#include <UndoCore.hxx>
#include <IMark.hxx>
#include <charfmt.hxx>
#include <strings.hrc>
#include <bookmark.hxx>
#include <frameformats.hxx>

#include <memory>
#include <utility>

OUString SwHistoryHint::GetDescription() const
{
    return OUString();
}

void SwHistoryHint::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SwHistoryHint"));
    (void)xmlTextWriterWriteFormatAttribute(pWriter, BAD_CAST("ptr"), "%p", this);
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("symbol"), BAD_CAST(typeid(*this).name()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_eWhichId"),
                                BAD_CAST(OString::number(m_eWhichId).getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}

SwHistorySetFormat::SwHistorySetFormat( const SfxPoolItem* pFormatHt, SwNodeOffset nNd )
    :  SwHistoryHint( HSTRY_SETFMTHNT )
    ,  m_pAttr( pFormatHt->Clone() )
    ,  m_nNodeIndex( nNd )
{
    switch ( m_pAttr->Which() )
    {
        case RES_PAGEDESC:
            static_cast<SwFormatPageDesc&>(*m_pAttr).ChgDefinedIn( nullptr );
            break;
        case RES_PARATR_DROP:
            static_cast<SwFormatDrop&>(*m_pAttr).ChgDefinedIn(nullptr);
            break;
        case RES_BOXATR_FORMULA:
        {
            // save formulas always in plain text
            SwTableBoxFormula& rNew = static_cast<SwTableBoxFormula&>(*m_pAttr);
            if ( rNew.IsIntrnlName() )
            {
                const SwTableBoxFormula& rOld =
                    *static_cast<const SwTableBoxFormula*>(pFormatHt);
                const SwNode* pNd = rOld.GetNodeOfFormula();
                if ( pNd )
                {
                    const SwTableNode* pTableNode = pNd->FindTableNode();
                    if (pTableNode)
                    {
                        auto pCpyTable = const_cast<SwTable*>(&pTableNode->GetTable());
                        pCpyTable->SwitchFormulasToExternalRepresentation();
                        rNew.ChgDefinedIn(rOld.GetDefinedIn());
                        rNew.ToRelBoxNm(pCpyTable);
                    }
                }
            }
            rNew.ChgDefinedIn( nullptr );
        }
        break;
    }
}

OUString SwHistorySetFormat::GetDescription() const
{
    OUString aResult;

    switch (m_pAttr->Which())
    {
    case RES_BREAK:
        switch (static_cast<SvxFormatBreakItem &>(*m_pAttr).GetBreak())
        {
        case SvxBreak::PageBefore:
        case SvxBreak::PageAfter:
        case SvxBreak::PageBoth:
            aResult = SwResId(STR_UNDO_PAGEBREAKS);

            break;
        case SvxBreak::ColumnBefore:
        case SvxBreak::ColumnAfter:
        case SvxBreak::ColumnBoth:
            aResult = SwResId(STR_UNDO_COLBRKS);

            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    return aResult;
}

void SwHistorySetFormat::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SwHistorySetFormat"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nNodeIndex"),
                                BAD_CAST(OString::number(sal_Int32(m_nNodeIndex)).getStr()));
    SwHistoryHint::dumpAsXml(pWriter);

    if (m_pAttr)
    {
        m_pAttr->dumpAsXml(pWriter);
    }

    (void)xmlTextWriterEndElement(pWriter);
}

void SwHistorySetFormat::SetInDoc( SwDoc& rDoc, bool bTmpSet )
{
    SwNode * pNode = rDoc.GetNodes()[ m_nNodeIndex ];
    if ( pNode->IsContentNode() )
    {
        static_cast<SwContentNode*>(pNode)->SetAttr( *m_pAttr );
    }
    else if ( pNode->IsTableNode() )
    {
        static_cast<SwTableNode*>(pNode)->GetTable().GetFrameFormat()->SetFormatAttr(
                *m_pAttr );
    }
    else if ( pNode->IsStartNode() && (SwTableBoxStartNode ==
                static_cast<SwStartNode*>(pNode)->GetStartNodeType()) )
    {
        SwTableNode* pTNd = pNode->FindTableNode();
        if ( pTNd )
        {
            SwTableBox* pBox = pTNd->GetTable().GetTableBox( m_nNodeIndex );
            if (pBox)
            {
                pBox->ClaimFrameFormat()->SetFormatAttr( *m_pAttr );
            }
        }
    }

    if ( !bTmpSet )
    {
        m_pAttr.reset();
    }
}

SwHistorySetFormat::~SwHistorySetFormat()
{
}

SwHistoryResetFormat::SwHistoryResetFormat(const SfxPoolItem* pFormatHt, SwNodeOffset nNodeIdx)
    : SwHistoryHint( HSTRY_RESETFMTHNT )
    , m_nNodeIndex( nNodeIdx )
    , m_nWhich( pFormatHt->Which() )
{
}

void SwHistoryResetFormat::SetInDoc( SwDoc& rDoc, bool )
{
    SwNode * pNode = rDoc.GetNodes()[ m_nNodeIndex ];
    if ( pNode->IsContentNode() )
    {
        static_cast<SwContentNode*>(pNode)->ResetAttr( m_nWhich );
    }
    else if ( pNode->IsTableNode() )
    {
        static_cast<SwTableNode*>(pNode)->GetTable().GetFrameFormat()->
            ResetFormatAttr( m_nWhich );
    }
}

SwHistorySetText::SwHistorySetText( SwTextAttr* pTextHt, SwNodeOffset nNodePos )
    : SwHistoryHint( HSTRY_SETTXTHNT )
    , m_nNodeIndex( nNodePos )
    , m_nStart( pTextHt->GetStart() )
    , m_nEnd( pTextHt->GetAnyEnd() )
    , m_bFormatIgnoreStart(pTextHt->IsFormatIgnoreStart())
    , m_bFormatIgnoreEnd  (pTextHt->IsFormatIgnoreEnd  ())
{
    // Caution: the following attributes generate no format attributes:
    //  - NoLineBreak, NoHyphen, Inserted, Deleted
    // These cases must be handled separately !!!

    // a little bit complicated but works: first assign a copy of the
    // default value and afterwards the values from text attribute
    if ( RES_TXTATR_CHARFMT == pTextHt->Which() )
    {
        m_pAttr.reset( new SwFormatCharFormat( pTextHt->GetCharFormat().GetCharFormat() ) );
    }
    else
    {
        m_pAttr.reset( pTextHt->GetAttr().Clone() );
    }
}

SwHistorySetText::~SwHistorySetText()
{
}

void SwHistorySetText::SetInDoc( SwDoc& rDoc, bool )
{
    if (!m_pAttr)
        return;

    if ( RES_TXTATR_CHARFMT == m_pAttr->Which() )
    {
        // ask the Doc if the CharFormat still exists
        if (!rDoc.GetCharFormats()->ContainsFormat(static_cast<SwFormatCharFormat&>(*m_pAttr).GetCharFormat()))
            return; // do not set, format does not exist
    }

    SwTextNode * pTextNd = rDoc.GetNodes()[ m_nNodeIndex ]->GetTextNode();
    OSL_ENSURE( pTextNd, "SwHistorySetText::SetInDoc: not a TextNode" );

    if ( !pTextNd )
        return;

    SwTextAttr *const pAttr = pTextNd->InsertItem(*m_pAttr, m_nStart, m_nEnd,
                    SetAttrMode::NOTXTATRCHR |
                    SetAttrMode::NOHINTADJUST );
    // shouldn't be possible to hit any error/merging path from here
    assert(pAttr);
    if (m_bFormatIgnoreStart)
    {
        pAttr->SetFormatIgnoreStart(true);
    }
    if (m_bFormatIgnoreEnd)
    {
        pAttr->SetFormatIgnoreEnd(true);
    }
}

void SwHistorySetText::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SwHistorySetText"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("node-index"),
                                      BAD_CAST(OString::number(sal_Int32(m_nNodeIndex)).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("start"),
                                      BAD_CAST(OString::number(sal_Int32(m_nStart)).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("end"),
                                      BAD_CAST(OString::number(sal_Int32(m_nEnd)).getStr()));
    SwHistoryHint::dumpAsXml(pWriter);

    if (m_pAttr)
    {
        m_pAttr->dumpAsXml(pWriter);
    }

    (void)xmlTextWriterEndElement(pWriter);
}

SwHistorySetTextField::SwHistorySetTextField( const SwTextField* pTextField, SwNodeOffset nNodePos )
    : SwHistoryHint( HSTRY_SETTXTFLDHNT )
    , m_pField( new SwFormatField( *pTextField->GetFormatField().GetField() ) )
{
    // only copy if not Sys-FieldType
    SwDoc& rDoc = pTextField->GetTextNode().GetDoc();

    m_nFieldWhich = m_pField->GetField()->GetTyp()->Which();
    if (m_nFieldWhich == SwFieldIds::Database ||
        m_nFieldWhich == SwFieldIds::User ||
        m_nFieldWhich == SwFieldIds::SetExp ||
        m_nFieldWhich == SwFieldIds::Dde ||
        !rDoc.getIDocumentFieldsAccess().GetSysFieldType( m_nFieldWhich ))
    {
        m_pFieldType = m_pField->GetField()->GetTyp()->Copy();
        m_pField->GetField()->ChgTyp( m_pFieldType.get() ); // change field type
    }
    m_nNodeIndex = nNodePos;
    m_nPos = pTextField->GetStart();
}

OUString SwHistorySetTextField::GetDescription() const
{
    return m_pField->GetField()->GetDescription();
}

SwHistorySetTextField::~SwHistorySetTextField()
{
}

void SwHistorySetTextField::SetInDoc( SwDoc& rDoc, bool )
{
    if (!m_pField)
        return;

    SwFieldType* pNewFieldType = m_pFieldType.get();
    if ( !pNewFieldType )
    {
        pNewFieldType = rDoc.getIDocumentFieldsAccess().GetSysFieldType( m_nFieldWhich );
    }
    else
    {
        // register type with the document
        pNewFieldType = rDoc.getIDocumentFieldsAccess().InsertFieldType( *m_pFieldType );
    }

    m_pField->GetField()->ChgTyp( pNewFieldType ); // change field type

    SwTextNode * pTextNd = rDoc.GetNodes()[ m_nNodeIndex ]->GetTextNode();
    assert(pTextNd);

    if ( pTextNd )
    {
        pTextNd->InsertItem( *m_pField, m_nPos, m_nPos,
                    SetAttrMode::NOTXTATRCHR );
#if ENABLE_YRS
        if (m_nFieldWhich == SwFieldIds::Postit)
        {
            SwPosition const pos{*pTextNd, m_nPos};
            // do use the same comment id because it's a ymap key!
            OString const commentId{static_cast<SwPostItField const*>(m_pField->GetField())->GetYrsCommentId()};
            assert(!commentId.isEmpty());
            pTextNd->GetDoc().getIDocumentState().YrsAddCommentImpl(pos, commentId);
        }
#endif
    }
}

SwHistorySetRefMark::SwHistorySetRefMark( const SwTextRefMark* pTextHt, SwNodeOffset nNodePos )
    : SwHistoryHint( HSTRY_SETREFMARKHNT )
    , m_RefName( pTextHt->GetRefMark().GetRefName() )
    , m_nNodeIndex( nNodePos )
    , m_nStart( pTextHt->GetStart() )
    , m_nEnd( pTextHt->GetAnyEnd() )
{
}

void SwHistorySetRefMark::SetInDoc( SwDoc& rDoc, bool )
{
    SwTextNode * pTextNd = rDoc.GetNodes()[ m_nNodeIndex ]->GetTextNode();
    OSL_ENSURE( pTextNd, "SwHistorySetRefMark: no TextNode" );
    if ( !pTextNd )
        return;

    SwFormatRefMark aRefMark( m_RefName );

    // if a reference mark without an end already exists here: must not insert!
    if ( m_nStart != m_nEnd ||
         !pTextNd->GetTextAttrForCharAt( m_nStart, RES_TXTATR_REFMARK ) )
    {
        pTextNd->InsertItem( aRefMark, m_nStart, m_nEnd,
                            SetAttrMode::NOTXTATRCHR );
    }
}

SwHistorySetTOXMark::SwHistorySetTOXMark( const SwTextTOXMark* pTextHt, SwNodeOffset nNodePos )
    : SwHistoryHint( HSTRY_SETTOXMARKHNT )
    , m_TOXMark( pTextHt->GetTOXMark() )
    , m_TOXName( m_TOXMark.GetTOXType()->GetTypeName() )
    , m_eTOXTypes( m_TOXMark.GetTOXType()->GetType() )
    , m_nNodeIndex( nNodePos )
    , m_nStart( pTextHt->GetStart() )
    , m_nEnd( pTextHt->GetAnyEnd() )
{
    static_cast<SvtListener*>(&m_TOXMark)->EndListeningAll();
}

SwTOXType* SwHistorySetTOXMark::GetSwTOXType(SwDoc& rDoc, TOXTypes eTOXTypes, const OUString& rTOXName)
{
    // search for respective TOX type
    const sal_uInt16 nCnt = rDoc.GetTOXTypeCount(eTOXTypes);
    SwTOXType* pToxType = nullptr;
    for ( sal_uInt16 n = 0; n < nCnt; ++n )
    {
        pToxType = const_cast<SwTOXType*>(rDoc.GetTOXType(eTOXTypes, n));
        if (pToxType->GetTypeName() == rTOXName)
            break;
        pToxType = nullptr;
    }

    if ( !pToxType )  // TOX type not found, create new
    {
        pToxType = const_cast<SwTOXType*>(
                rDoc.InsertTOXType(SwTOXType(rDoc, eTOXTypes, rTOXName)));
    }

    return pToxType;
}

void SwHistorySetTOXMark::SetInDoc( SwDoc& rDoc, bool )
{
    SwTextNode * pTextNd = rDoc.GetNodes()[ m_nNodeIndex ]->GetTextNode();
    OSL_ENSURE( pTextNd, "SwHistorySetTOXMark: no TextNode" );
    if ( !pTextNd )
        return;

    SwTOXType* pToxType = GetSwTOXType(rDoc, m_eTOXTypes, m_TOXName);

    SwTOXMark aNew( m_TOXMark );
    aNew.RegisterToTOXType( *pToxType );

    pTextNd->InsertItem( aNew, m_nStart, m_nEnd,
                        SetAttrMode::NOTXTATRCHR );
}

bool SwHistorySetTOXMark::IsEqual( const SwTOXMark& rCmp ) const
{
    return m_TOXName   == rCmp.GetTOXType()->GetTypeName() &&
           m_eTOXTypes == rCmp.GetTOXType()->GetType() &&
           m_TOXMark.GetAlternativeText() == rCmp.GetAlternativeText() &&
           ( (TOX_INDEX == m_eTOXTypes)
              ?   ( m_TOXMark.GetPrimaryKey()   == rCmp.GetPrimaryKey()  &&
                    m_TOXMark.GetSecondaryKey() == rCmp.GetSecondaryKey()   )
              :   m_TOXMark.GetLevel() == rCmp.GetLevel()
           );
}

SwHistoryResetText::SwHistoryResetText( sal_uInt16 nWhich,
            sal_Int32 nAttrStart, sal_Int32 nAttrEnd, SwNodeOffset nNodePos )
    : SwHistoryHint( HSTRY_RESETTXTHNT )
    , m_nNodeIndex( nNodePos ), m_nStart( nAttrStart ), m_nEnd( nAttrEnd )
    , m_nAttr( nWhich )
{
}

void SwHistoryResetText::SetInDoc( SwDoc& rDoc, bool )
{
    SwTextNode * pTextNd = rDoc.GetNodes()[ m_nNodeIndex ]->GetTextNode();
    OSL_ENSURE( pTextNd, "SwHistoryResetText: no TextNode" );
    if ( pTextNd )
    {
        pTextNd->DeleteAttributes( m_nAttr, m_nStart, m_nEnd );
    }
}

SwHistorySetFootnote::SwHistorySetFootnote( SwTextFootnote* pTextFootnote, SwNodeOffset nNodePos )
    : SwHistoryHint( HSTRY_SETFTNHNT )
    , m_pUndo( new SwUndoSaveSection )
    , m_FootnoteNumber( pTextFootnote->GetFootnote().GetNumStr() )
    , m_nNodeIndex( nNodePos )
    , m_nStart( pTextFootnote->GetStart() )
    , m_bEndNote( pTextFootnote->GetFootnote().IsEndNote() )
{
    OSL_ENSURE( pTextFootnote->GetStartNode(),
            "SwHistorySetFootnote: Footnote without Section" );

    // keep the old NodePos (because who knows what later will be saved/deleted
    // in SaveSection)
    SwDoc& rDoc = const_cast<SwDoc&>(pTextFootnote->GetTextNode().GetDoc());
    SwNode* pSaveNd = rDoc.GetNodes()[ m_nNodeIndex ];

    // keep pointer to StartNode of FootnoteSection and reset its attribute for now
    // (as a result, its/all Frames will be deleted automatically)
    SwNodeIndex aSttIdx( *pTextFootnote->GetStartNode() );
    pTextFootnote->SetStartNode( nullptr, false );

    m_pUndo->SaveSection( aSttIdx );
    m_nNodeIndex = pSaveNd->GetIndex();
}

SwHistorySetFootnote::SwHistorySetFootnote( const SwTextFootnote &rTextFootnote )
    : SwHistoryHint( HSTRY_SETFTNHNT )
    , m_FootnoteNumber( rTextFootnote.GetFootnote().GetNumStr() )
    , m_nNodeIndex( SwTextFootnote_GetIndex( (&rTextFootnote) ) )
    , m_nStart( rTextFootnote.GetStart() )
    , m_bEndNote( rTextFootnote.GetFootnote().IsEndNote() )
{
    OSL_ENSURE( rTextFootnote.GetStartNode(),
            "SwHistorySetFootnote: Footnote without Section" );
}

OUString SwHistorySetFootnote::GetDescription() const
{
    return SwResId(STR_FOOTNOTE);
}

SwHistorySetFootnote::~SwHistorySetFootnote()
{
}

void SwHistorySetFootnote::SetInDoc( SwDoc& rDoc, bool )
{
    SwTextNode * pTextNd = rDoc.GetNodes()[ m_nNodeIndex ]->GetTextNode();
    OSL_ENSURE( pTextNd, "SwHistorySetFootnote: no TextNode" );
    if ( !pTextNd )
        return;

    if (m_pUndo)
    {
        // set the footnote in the TextNode
        SwFormatFootnote aNew( m_bEndNote );
        if ( !m_FootnoteNumber.isEmpty() )
        {
            aNew.SetNumStr( m_FootnoteNumber );
        }
        SwTextFootnote* pTextFootnote = new SwTextFootnote(
            SfxPoolItemHolder(rDoc.GetAttrPool(), &aNew),
            m_nStart );

        // create the section of the Footnote
        SwNodeIndex aIdx( *pTextNd );
        m_pUndo->RestoreSection( rDoc, &aIdx, SwFootnoteStartNode );
        pTextFootnote->SetStartNode( &aIdx );
        if ( m_pUndo->GetHistory() )
        {
            // create frames only now
            m_pUndo->GetHistory()->Rollback( rDoc );
        }

        pTextNd->InsertHint( pTextFootnote );
    }
    else
    {
        SwTextFootnote * const pFootnote =
            static_cast<SwTextFootnote*>(
                pTextNd->GetTextAttrForCharAt( m_nStart ));
        assert(pFootnote);
        SwFormatFootnote &rFootnote = const_cast<SwFormatFootnote&>(pFootnote->GetFootnote());
        rFootnote.SetNumStr( m_FootnoteNumber  );
        if ( rFootnote.IsEndNote() != m_bEndNote )
        {
            rFootnote.SetEndNote( m_bEndNote );
            pFootnote->CheckCondColl();
        }
    }
}

SwHistoryChangeFormatColl::SwHistoryChangeFormatColl( SwFormatColl* pFormatColl, SwNodeOffset nNd,
                            SwNodeType nNodeWhich )
    : SwHistoryHint( HSTRY_CHGFMTCOLL )
    , m_pColl( pFormatColl )
    , m_nNodeIndex( nNd )
    , m_nNodeType( nNodeWhich )
{
}

void SwHistoryChangeFormatColl::SetInDoc( SwDoc& rDoc, bool )
{
    SwContentNode * pContentNd = rDoc.GetNodes()[ m_nNodeIndex ]->GetContentNode();
    OSL_ENSURE( pContentNd, "SwHistoryChangeFormatColl: no ContentNode" );

    // before setting the format, check if it is still available in the
    // document. if it has been deleted, there is no undo!
    if ( !(pContentNd && m_nNodeType == pContentNd->GetNodeType()) )
        return;

    if ( SwNodeType::Text == m_nNodeType )
    {
        if (rDoc.GetTextFormatColls()->IsAlive(static_cast<SwTextFormatColl *>(m_pColl)))
        {
            pContentNd->ChgFormatColl( m_pColl );
        }
    }
    else if (rDoc.GetGrfFormatColls()->IsAlive(static_cast<SwGrfFormatColl *>(m_pColl)))
    {
        pContentNd->ChgFormatColl( m_pColl );
    }
}

SwHistoryTextFlyCnt::SwHistoryTextFlyCnt( SwFrameFormat* const pFlyFormat )
    : SwHistoryHint( HSTRY_FLYCNT )
    , m_pUndo( new SwUndoDelLayFormat( pFlyFormat ) )
{
    OSL_ENSURE( pFlyFormat, "SwHistoryTextFlyCnt: no Format" );
    m_pUndo->ChgShowSel( false );
}

SwHistoryTextFlyCnt::~SwHistoryTextFlyCnt()
{
}

void SwHistoryTextFlyCnt::SetInDoc( SwDoc& rDoc, bool )
{
    ::sw::IShellCursorSupplier *const pISCS(rDoc.GetIShellCursorSupplier());
    assert(pISCS);
    ::sw::UndoRedoContext context(rDoc, *pISCS);
    m_pUndo->UndoImpl(context);
}

void SwHistoryTextFlyCnt::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SwHistoryTextFlyCnt"));
    SwHistoryHint::dumpAsXml(pWriter);

    if (m_pUndo)
    {
        m_pUndo->dumpAsXml(pWriter);
    }

    (void)xmlTextWriterEndElement(pWriter);
}

SwHistoryBookmark::SwHistoryBookmark(
    const ::sw::mark::MarkBase& rBkmk,
    bool bSavePos,
    bool bSaveOtherPos)
    : SwHistoryHint(HSTRY_BOOKMARK)
    , m_aName(rBkmk.GetName())
    , m_bHidden(false)
    , m_nNode(bSavePos ?
        rBkmk.GetMarkPos().GetNodeIndex() : SwNodeOffset(0))
    , m_nOtherNode(bSaveOtherPos ?
        rBkmk.GetOtherMarkPos().GetNodeIndex() : SwNodeOffset(0))
    , m_nContent(bSavePos ?
        rBkmk.GetMarkPos().GetContentIndex() : 0)
    , m_nOtherContent(bSaveOtherPos ?
        rBkmk.GetOtherMarkPos().GetContentIndex() :0)
    , m_bSavePos(bSavePos)
    , m_bSaveOtherPos(bSaveOtherPos)
    , m_bHadOtherPos(rBkmk.IsExpanded())
    , m_eBkmkType(IDocumentMarkAccess::GetType(rBkmk))
{
    const ::sw::mark::Bookmark* const pBookmark = dynamic_cast< const ::sw::mark::Bookmark* >(&rBkmk);
    if(!pBookmark)
        return;

    m_aKeycode = pBookmark->GetKeyCode();
    m_aShortName = pBookmark->GetShortName();
    m_bHidden = pBookmark->IsHidden();
    m_aHideCondition = pBookmark->GetHideCondition();
    m_pMetadataUndo = pBookmark->CreateUndo();
}

void SwHistoryBookmark::SetInDoc( SwDoc& rDoc, bool )
{
    ::sw::UndoGuard const undoGuard(rDoc.GetIDocumentUndoRedo());

    SwNodes& rNds = rDoc.GetNodes();
    IDocumentMarkAccess* pMarkAccess = rDoc.getIDocumentMarkAccess();
    std::optional<SwPaM> oPam;
    ::sw::mark::MarkBase* pMark = nullptr;

    // now the situation is that m_bSavePos and m_bSaveOtherPos don't determine
    // whether the mark was deleted
    if (auto const iter = pMarkAccess->findMark(m_aName); iter != pMarkAccess->getAllMarksEnd())
    {
        pMark = *iter;
    }
    if(m_bSavePos)
    {
        SwContentNode* const pContentNd = rNds[m_nNode]->GetContentNode();
        assert(pContentNd);
        oPam.emplace(*pContentNd, m_nContent);
    }
    else
    {
        assert(pMark);
        oPam.emplace(pMark->GetMarkPos());
    }
    assert(oPam);

    if(m_bSaveOtherPos)
    {
        SwContentNode* const pContentNd = rNds[m_nOtherNode]->GetContentNode();
        assert(pContentNd);
        oPam->SetMark();
        oPam->GetMark()->Assign(*pContentNd, m_nOtherContent);
    }
    else if(m_bHadOtherPos)
    {
        assert(pMark);
        assert(pMark->IsExpanded());
        oPam->SetMark();
        *oPam->GetMark() = pMark->GetOtherMarkPos();
    }

    if ( pMark != nullptr )
    {
        pMarkAccess->deleteMark( pMark );
    }
    ::sw::mark::Bookmark* const pBookmark =
        dynamic_cast<::sw::mark::Bookmark*>(
            pMarkAccess->makeMark(*oPam, m_aName, m_eBkmkType, sw::mark::InsertMode::New));
    if ( pBookmark == nullptr )
        return;

    pBookmark->SetKeyCode(m_aKeycode);
    pBookmark->SetShortName(m_aShortName);
    pBookmark->Hide(m_bHidden);
    pBookmark->SetHideCondition(m_aHideCondition);

    if (m_pMetadataUndo)
        pBookmark->RestoreMetadata(m_pMetadataUndo);
}

bool SwHistoryBookmark::IsEqualBookmark(const ::sw::mark::MarkBase& rBkmk)
{
    return m_nNode == rBkmk.GetMarkPos().GetNodeIndex()
        && m_nContent == rBkmk.GetMarkPos().GetContentIndex()
        && m_aName == rBkmk.GetName();
}

SwHistoryNoTextFieldmark::SwHistoryNoTextFieldmark(const ::sw::mark::Fieldmark& rFieldMark)
    : SwHistoryHint(HSTRY_NOTEXTFIELDMARK)
    , m_sType(rFieldMark.GetFieldname())
    , m_nNode(rFieldMark.GetMarkStart().GetNodeIndex())
    , m_nContent(rFieldMark.GetMarkStart().GetContentIndex())
{
}

void SwHistoryNoTextFieldmark::SetInDoc(SwDoc& rDoc, bool)
{
    ::sw::UndoGuard const undoGuard(rDoc.GetIDocumentUndoRedo());

    SwNodes& rNds = rDoc.GetNodes();
    std::optional<SwPaM> pPam;

    const SwContentNode* pContentNd = rNds[m_nNode]->GetContentNode();
    if(pContentNd)
        pPam.emplace(*pContentNd, m_nContent);

    if (pPam)
    {
        IDocumentMarkAccess* pMarkAccess = rDoc.getIDocumentMarkAccess();
        pMarkAccess->makeNoTextFieldBookmark(*pPam, SwMarkName(), m_sType);
    }
}

void SwHistoryNoTextFieldmark::ResetInDoc(SwDoc& rDoc)
{
    ::sw::UndoGuard const undoGuard(rDoc.GetIDocumentUndoRedo());

    SwNodes& rNds = rDoc.GetNodes();
    std::optional<SwPaM> pPam;

    const SwContentNode* pContentNd = rNds[m_nNode]->GetContentNode();
    assert(pContentNd);
    pPam.emplace(*pContentNd, m_nContent);

    if (pPam)
    {
        IDocumentMarkAccess* pMarkAccess = rDoc.getIDocumentMarkAccess();
        pMarkAccess->deleteFieldmarkAt(*pPam->GetPoint());
    }
}

SwHistoryTextFieldmark::SwHistoryTextFieldmark(const ::sw::mark::Fieldmark& rFieldMark)
    : SwHistoryHint(HSTRY_TEXTFIELDMARK)
    , m_sName(rFieldMark.GetName())
    , m_sType(rFieldMark.GetFieldname())
    , m_nStartNode(rFieldMark.GetMarkStart().GetNodeIndex())
    , m_nStartContent(rFieldMark.GetMarkStart().GetContentIndex())
    , m_nEndNode(rFieldMark.GetMarkEnd().GetNodeIndex())
    , m_nEndContent(rFieldMark.GetMarkEnd().GetContentIndex())
{
    SwPosition const sepPos(sw::mark::FindFieldSep(rFieldMark));
    m_nSepNode = sepPos.GetNodeIndex();
    m_nSepContent = sepPos.GetContentIndex();
}

void SwHistoryTextFieldmark::SetInDoc(SwDoc& rDoc, bool)
{
    ::sw::UndoGuard const undoGuard(rDoc.GetIDocumentUndoRedo());

    SwNodes& rNds = rDoc.GetNodes();

    assert(rNds[m_nStartNode]->IsContentNode());
    assert(rNds[m_nEndNode]->IsContentNode());
    assert(rNds[m_nSepNode]->IsContentNode());

    SwPaM const pam(*rNds[m_nStartNode]->GetContentNode(), m_nStartContent,
                    *rNds[m_nEndNode]->GetContentNode(),
                        // subtract 1 for the CH_TXT_ATR_FIELDEND itself,
                        // plus more if same node as other CH_TXT_ATR
                        m_nStartNode == m_nEndNode
                            ? (m_nEndContent - 3)
                            : m_nSepNode == m_nEndNode
                                ? (m_nEndContent - 2)
                                : (m_nEndContent - 1));
    SwPosition const sepPos(*rNds[m_nSepNode]->GetContentNode(),
            m_nStartNode == m_nSepNode ? (m_nSepContent - 1) : m_nSepContent);

    IDocumentMarkAccess & rMarksAccess(*rDoc.getIDocumentMarkAccess());
    rMarksAccess.makeFieldBookmark(pam, m_sName, m_sType, &sepPos);
}

void SwHistoryTextFieldmark::ResetInDoc(SwDoc& rDoc)
{
    ::sw::UndoGuard const undoGuard(rDoc.GetIDocumentUndoRedo());

    SwNodes& rNds = rDoc.GetNodes();

    assert(rNds[m_nStartNode]->IsContentNode());
    assert(rNds[m_nEndNode]->IsContentNode());
    assert(rNds[m_nSepNode]->IsContentNode());

    SwPosition const pos(*rNds[m_nStartNode]->GetContentNode(), m_nStartContent);

    IDocumentMarkAccess & rMarksAccess(*rDoc.getIDocumentMarkAccess());
    rMarksAccess.deleteFieldmarkAt(pos);
}

SwHistorySetAttrSet::SwHistorySetAttrSet(
    const SfxItemSet& rSet,
    SwNodeOffset nNodePos,
    const o3tl::sorted_vector<sal_uInt16> &rSetArr)
: SwHistoryHint(HSTRY_SETATTRSET)
, m_OldSet(*rSet.GetPool(), rSet.GetRanges())
, m_ResetArray(0, 4)
, m_nNodeIndex(nNodePos)
{
    // ITEM: Analyzed this one, it originally iterated using two SfxItemIter at the
    // same time to iterate rSet and m_OldSet. m_OldSet was copied from rSet before
    // in the var init above. It then *removed* Items again or manipulated them.
    // The problem with that is that this implies that the two iterators will advance
    // on the same WhichIDs step by step. Even after copying and with the former
    // organization of the Items in ItemSet as fixed array of pointers that is a
    // 'wild' assumption, besides that deleting Items while a iterator is used is
    // bad in general, too.
    // I re-designed this to iterate over the source ItemSet (rSet) and add Items
    // as needed to the target ItemSet m_OldSet. This is tricky since some NonShareable
    // 'special' Items get special treatment.
    for (SfxItemIter aIter(rSet); !aIter.IsAtEnd(); aIter.NextItem())
    {
        // check if Item is intended to be contained
        if (rSetArr.count(aIter.GetCurWhich()))
        {
            // do include item, but take care of some 'special' cases
            switch (aIter.GetCurWhich())
            {
                case RES_PAGEDESC:
                {
                    // SwFormatPageDesc - NonShareable Item
                    SwFormatPageDesc* pNew(new SwFormatPageDesc(*static_cast<const SwFormatPageDesc*>(aIter.GetCurItem())));
                    pNew->ChgDefinedIn(nullptr);
                    m_OldSet.Put(std::unique_ptr<SwFormatPageDesc>(pNew));
                    break;
                }
                case RES_PARATR_DROP:
                {
                    // SwFormatDrop - NonShareable Item
                    SwFormatDrop* pNew(new SwFormatDrop(*static_cast<const SwFormatDrop*>(aIter.GetCurItem())));
                    pNew->ChgDefinedIn(nullptr);
                    m_OldSet.Put(std::unique_ptr<SwFormatDrop>(pNew));
                    break;
                }
                case RES_BOXATR_FORMULA:
                {
                    // When a formula is set, never save the value. It
                    // possibly must be recalculated!
                    // Save formulas always in plain text
                    // RES_BOXATR_FORMULA: SwTableBoxFormula - NonShareable Item
                    // CAUTION: also is connected to RES_BOXATR_VALUE which was hard deleted before
                    SwTableBoxFormula* pNew(static_cast<const SwTableBoxFormula*>(aIter.GetCurItem())->Clone());

                    if (pNew->IsIntrnlName())
                    {
                        const SwTableBoxFormula& rOld(rSet.Get(RES_BOXATR_FORMULA));
                        const SwNode* pNd(rOld.GetNodeOfFormula());

                        if (nullptr != pNd)
                        {
                            const SwTableNode* pTableNode(pNd->FindTableNode());

                            if(nullptr != pTableNode)
                            {
                                auto pCpyTable = const_cast<SwTable*>(&pTableNode->GetTable());
                                pCpyTable->SwitchFormulasToExternalRepresentation();
                                pNew->ChgDefinedIn(rOld.GetDefinedIn());
                                pNew->PtrToBoxNm(pCpyTable);
                            }
                        }
                    }

                    pNew->ChgDefinedIn(nullptr);
                    m_OldSet.Put(std::unique_ptr<SwTableBoxFormula>(pNew));
                    break;
                }
                case RES_BOXATR_VALUE:
                {
                    // If RES_BOXATR_FORMULA is set, do *not* set RES_BOXATR_VALUE - SwTableBoxValue - that IS a Shareable Item (!)
                    // If RES_BOXATR_FORMULA is not set, also no reason to set RES_BOXATR_VALUE
                    // -> so just ignore it here (?)
                    break;
                }
                default:
                {
                    // all other Items: add to m_OldSet
                    m_OldSet.Put(*aIter.GetCurItem());
                    break;
                }
            }
        }
        else
        {
            // do not include item, additionally remember in
            // m_ResetArray
            m_ResetArray.push_back(aIter.GetCurWhich());
        }
    }
}

void SwHistorySetAttrSet::SetInDoc( SwDoc& rDoc, bool )
{
    ::sw::UndoGuard const undoGuard(rDoc.GetIDocumentUndoRedo());

    SwNode * pNode = rDoc.GetNodes()[ m_nNodeIndex ];
    if ( pNode->IsContentNode() )
    {
        static_cast<SwContentNode*>(pNode)->SetAttr( m_OldSet );
        if ( !m_ResetArray.empty() )
        {
            static_cast<SwContentNode*>(pNode)->ResetAttr( m_ResetArray );
        }
    }
    else if ( pNode->IsTableNode() )
    {
        SwFormat& rFormat =
            *static_cast<SwTableNode*>(pNode)->GetTable().GetFrameFormat();
        rFormat.SetFormatAttr( m_OldSet );
        if ( !m_ResetArray.empty() )
        {
            rFormat.ResetFormatAttr( m_ResetArray.front() );
        }
    }
}

SwHistoryChangeFlyAnchor::SwHistoryChangeFlyAnchor(sw::SpzFrameFormat& rFormat)
    : SwHistoryHint( HSTRY_CHGFLYANCHOR )
    , m_rFormat(rFormat)
    , m_nOldNodeIndex( rFormat.GetAnchor().GetAnchorNode()->GetIndex() )
    , m_nOldContentIndex( (RndStdIds::FLY_AT_CHAR == rFormat.GetAnchor().GetAnchorId())
            ?   rFormat.GetAnchor().GetAnchorContentOffset()
            :   COMPLETE_STRING )
{
}

void SwHistoryChangeFlyAnchor::SetInDoc( SwDoc& rDoc, bool )
{
    ::sw::UndoGuard const undoGuard(rDoc.GetIDocumentUndoRedo());

    // One would expect m_rFormat to only contain FlyFormats, given the name of
    // this class, but apparently it is also used for DrawFormats.
    if (!rDoc.GetSpzFrameFormats()->IsAlive(&m_rFormat)) // Format does still exist
        return;

    SwFormatAnchor aTmp( m_rFormat.GetAnchor() );

    SwNode* pNd = rDoc.GetNodes()[ m_nOldNodeIndex ];
    SwContentNode* pCNd = pNd->GetContentNode();
    SwPosition aPos( *pNd );
    if ( COMPLETE_STRING != m_nOldContentIndex )
        aPos.SetContent( m_nOldContentIndex );
    aTmp.SetAnchor( &aPos );

    // so the Layout does not get confused
    if (!pCNd->getLayoutFrame(rDoc.getIDocumentLayoutAccess().GetCurrentLayout(), nullptr, nullptr))
    {
        m_rFormat.DelFrames();
    }

    m_rFormat.SetFormatAttr( aTmp );
}

SwHistoryChangeFlyChain::SwHistoryChangeFlyChain( SwFlyFrameFormat& rFormat,
                                        const SwFormatChain& rAttr )
    : SwHistoryHint( HSTRY_CHGFLYCHAIN )
    , m_pPrevFormat( rAttr.GetPrev() )
    , m_pNextFormat( rAttr.GetNext() )
    , m_pFlyFormat( &rFormat )
{
}

void SwHistoryChangeFlyChain::SetInDoc( SwDoc& rDoc, bool )
{
    if (!rDoc.GetSpzFrameFormats()->IsAlive(m_pFlyFormat))
        return;

    SwFormatChain aChain;

    if (m_pPrevFormat &&
        rDoc.GetSpzFrameFormats()->IsAlive(m_pPrevFormat))
    {
        aChain.SetPrev( m_pPrevFormat );
        SwFormatChain aTmp( m_pPrevFormat->GetChain() );
        aTmp.SetNext( m_pFlyFormat );
        m_pPrevFormat->SetFormatAttr( aTmp );
    }

    if (m_pNextFormat &&
        rDoc.GetSpzFrameFormats()->IsAlive(m_pNextFormat))
    {
        aChain.SetNext( m_pNextFormat );
        SwFormatChain aTmp( m_pNextFormat->GetChain() );
        aTmp.SetPrev( m_pFlyFormat );
        m_pNextFormat->SetFormatAttr( aTmp );
    }

    if ( aChain.GetNext() || aChain.GetPrev() )
    {
        m_pFlyFormat->SetFormatAttr( aChain );
    }
}

// -> #i27615#
SwHistoryChangeCharFormat::SwHistoryChangeCharFormat(SfxItemSet aSet,
                                     UIName sFormat)
    : SwHistoryHint(HSTRY_CHGCHARFMT)
    , m_OldSet(std::move(aSet)), m_Format(std::move(sFormat))
{
}

void SwHistoryChangeCharFormat::SetInDoc(SwDoc& rDoc, bool )
{
    SwCharFormat * pCharFormat = rDoc.FindCharFormatByName(m_Format);

    if (pCharFormat)
    {
        pCharFormat->SetFormatAttr(m_OldSet);
    }
}
// <- #i27615#

SwHistory::SwHistory()
    : m_nEndDiff( 0 )
{
}

SwHistory::~SwHistory()
{
}

void SwHistory::AddPoolItem(
    const SfxPoolItem* pOldValue,
    const SfxPoolItem* pNewValue,
    SwNodeOffset nNodeIdx)
{
    OSL_ENSURE( !m_nEndDiff, "History was not deleted after REDO" );
    const sal_uInt16 nWhich(pNewValue->Which());

    // excluded values
    if(nWhich == RES_TXTATR_FIELD || nWhich == RES_TXTATR_ANNOTATION)
    {
        return;
    }

    // no default Attribute?
    std::unique_ptr<SwHistoryHint> pHt;

    // To be able to include the DrawingLayer FillItems something more
    // general has to be done to check if an Item is default than to check
    // if its pointer equals that in Writer's global PoolDefaults (held in
    // aAttrTab and used to fill the pool defaults in Writer - looks as if
    // Writer is *older* than the SfxItemPool ?). I checked the possibility to
    // get the SfxItemPool here (works), but decided to use the SfxPoolItem's
    // global tooling aka IsDefaultItem(const SfxPoolItem*) for now
    if(pOldValue && !IsDefaultItem(pOldValue))
    {
        pHt.reset( new SwHistorySetFormat( pOldValue, nNodeIdx ) );
    }
    else
    {
        pHt.reset( new SwHistoryResetFormat( pNewValue, nNodeIdx ) );
    }

    m_SwpHstry.push_back( std::move(pHt) );
}

// FIXME: refactor the following "Add" methods (DRY)?
void SwHistory::AddTextAttr(SwTextAttr *const pHint,
        SwNodeOffset const nNodeIdx, bool const bNewAttr)
{
    OSL_ENSURE( !m_nEndDiff, "History was not deleted after REDO" );

    std::unique_ptr<SwHistoryHint> pHt;
    if( !bNewAttr )
    {
        switch ( pHint->Which() )
        {
            case RES_TXTATR_FTN:
                pHt.reset( new SwHistorySetFootnote(
                            static_cast<SwTextFootnote*>(pHint), nNodeIdx ) );
                break;
            case RES_TXTATR_FLYCNT:
                pHt.reset( new SwHistoryTextFlyCnt( static_cast<SwTextFlyCnt*>(pHint)
                            ->GetFlyCnt().GetFrameFormat() ) );
                break;
            case RES_TXTATR_FIELD:
            case RES_TXTATR_ANNOTATION:
                pHt.reset( new SwHistorySetTextField(
                        static_txtattr_cast<SwTextField*>(pHint), nNodeIdx) );
                break;
            case RES_TXTATR_TOXMARK:
                pHt.reset( new SwHistorySetTOXMark(
                        static_txtattr_cast<SwTextTOXMark*>(pHint), nNodeIdx) );
                break;
            case RES_TXTATR_REFMARK:
                pHt.reset( new SwHistorySetRefMark(
                        static_txtattr_cast<SwTextRefMark*>(pHint), nNodeIdx) );
                break;
            default:
                pHt.reset( new SwHistorySetText( pHint, nNodeIdx ) );
        }
    }
    else
    {
        pHt.reset( new SwHistoryResetText( pHint->Which(), pHint->GetStart(),
                                    pHint->GetAnyEnd(), nNodeIdx ) );
    }
    m_SwpHstry.push_back( std::move(pHt) );
}

void SwHistory::AddColl(SwFormatColl *const pColl, SwNodeOffset const nNodeIdx,
        SwNodeType const nWhichNd)
{
    OSL_ENSURE( !m_nEndDiff, "History was not deleted after REDO" );

    std::unique_ptr<SwHistoryHint> pHt(
        new SwHistoryChangeFormatColl( pColl, nNodeIdx, nWhichNd ));
    m_SwpHstry.push_back( std::move(pHt) );
}

void SwHistory::AddIMark(const ::sw::mark::MarkBase& rBkmk,
        bool const bSavePos, bool const bSaveOtherPos)
{
    OSL_ENSURE( !m_nEndDiff, "History was not deleted after REDO" );

    std::unique_ptr<SwHistoryHint> pHt;

    switch (IDocumentMarkAccess::GetType(rBkmk))
    {
        case IDocumentMarkAccess::MarkType::TEXT_FIELDMARK:
        case IDocumentMarkAccess::MarkType::DATE_FIELDMARK:
            assert(bSavePos && bSaveOtherPos); // must be deleted completely!
            pHt.reset(new SwHistoryTextFieldmark(dynamic_cast<sw::mark::Fieldmark const&>(rBkmk)));
            break;
        case IDocumentMarkAccess::MarkType::CHECKBOX_FIELDMARK:
        case IDocumentMarkAccess::MarkType::DROPDOWN_FIELDMARK:
            assert(bSavePos && bSaveOtherPos); // must be deleted completely!
            pHt.reset(new SwHistoryNoTextFieldmark(dynamic_cast<sw::mark::Fieldmark const&>(rBkmk)));
            break;
        default:
            pHt.reset(new SwHistoryBookmark(rBkmk, bSavePos, bSaveOtherPos));
            break;
    }

    assert(pHt);
    m_SwpHstry.push_back( std::move(pHt) );
}

void SwHistory::AddChangeFlyAnchor(sw::SpzFrameFormat& rFormat)
{
    std::unique_ptr<SwHistoryHint> pHt(new SwHistoryChangeFlyAnchor(rFormat));
    m_SwpHstry.push_back( std::move(pHt) );
}

void SwHistory::AddDeleteFly(SwFrameFormat& rFormat, sal_uInt16& rSetPos)
{
    OSL_ENSURE( !m_nEndDiff, "History was not deleted after REDO" );

    const sal_uInt16 nWh = rFormat.Which();
    (void) nWh;
    // only Flys!
    assert((RES_FLYFRMFMT == nWh && dynamic_cast<SwFlyFrameFormat*>(&rFormat))
        || (RES_DRAWFRMFMT == nWh && dynamic_cast<SwDrawFrameFormat*>(&rFormat)));
    {
        std::unique_ptr<SwHistoryHint> pHint(new SwHistoryTextFlyCnt( &rFormat ));
        m_SwpHstry.push_back( std::move(pHint) );

        if( const SwFormatChain* pChainItem = rFormat.GetItemIfSet( RES_CHAIN, false ) )
        {
            assert(RES_FLYFRMFMT == nWh); // not supported on SdrObjects
            if( pChainItem->GetNext() || pChainItem->GetPrev() )
            {
                std::unique_ptr<SwHistoryHint> pHt(
                    new SwHistoryChangeFlyChain(static_cast<SwFlyFrameFormat&>(rFormat), *pChainItem));
                m_SwpHstry.insert( m_SwpHstry.begin() + rSetPos++, std::move(pHt) );
                if ( pChainItem->GetNext() )
                {
                    SwFormatChain aTmp( pChainItem->GetNext()->GetChain() );
                    aTmp.SetPrev( nullptr );
                    pChainItem->GetNext()->SetFormatAttr( aTmp );
                }
                if ( pChainItem->GetPrev() )
                {
                    SwFormatChain aTmp( pChainItem->GetPrev()->GetChain() );
                    aTmp.SetNext( nullptr );
                    pChainItem->GetPrev()->SetFormatAttr( aTmp );
                }
            }
            rFormat.ResetFormatAttr( RES_CHAIN );
        }
    }
}

void SwHistory::AddFootnote(const SwTextFootnote& rFootnote)
{
    std::unique_ptr<SwHistoryHint> pHt(new SwHistorySetFootnote( rFootnote ));
    m_SwpHstry.push_back( std::move(pHt) );
}

// #i27615#
void SwHistory::AddCharFormat(const SfxItemSet & rSet, const SwCharFormat & rFormat)
{
    std::unique_ptr<SwHistoryHint> pHt(new SwHistoryChangeCharFormat(rSet, rFormat.GetName()));
    m_SwpHstry.push_back( std::move(pHt) );
}

bool SwHistory::Rollback( SwDoc& rDoc, sal_uInt16 nStart )
{
    if ( !Count() )
        return false;

    for ( sal_uInt16 i = Count(); i > nStart ; )
    {
        SwHistoryHint * pHHt = m_SwpHstry[ --i ].get();
        pHHt->SetInDoc( rDoc, false );
    }
    m_SwpHstry.erase( m_SwpHstry.begin() + nStart, m_SwpHstry.end() );
    m_nEndDiff = 0;
    return true;
}

bool SwHistory::TmpRollback( SwDoc& rDoc, sal_uInt16 nStart, bool bToFirst )
{
    sal_uInt16 nEnd = Count() - m_nEndDiff;
    if ( !Count() || !nEnd || nStart >= nEnd )
        return false;

    if ( bToFirst )
    {
        for ( ; nEnd > nStart; ++m_nEndDiff )
        {
            SwHistoryHint* pHHt = m_SwpHstry[ --nEnd ].get();
            pHHt->SetInDoc( rDoc, true );
        }
    }
    else
    {
        for ( ; nStart < nEnd; ++m_nEndDiff, ++nStart )
        {
            SwHistoryHint* pHHt = m_SwpHstry[ nStart ].get();
            pHHt->SetInDoc( rDoc, true );
        }
    }
    return true;
}

sal_uInt16 SwHistory::SetTmpEnd( sal_uInt16 nNewTmpEnd )
{
    OSL_ENSURE( nNewTmpEnd <= Count(), "SwHistory::SetTmpEnd: out of bounds" );

    const sal_uInt16 nOld = Count() - m_nEndDiff;
    m_nEndDiff = Count() - nNewTmpEnd;

    // for every SwHistoryFlyCnt, call the Redo of its UndoObject.
    // this saves the formats of the flys!
    for ( sal_uInt16 n = nOld; n < nNewTmpEnd; n++ )
    {
        if ( HSTRY_FLYCNT == (*this)[ n ]->Which() )
        {
            static_cast<SwHistoryTextFlyCnt*>((*this)[ n ])
                ->GetUDelLFormat()->RedoForRollback();
        }
    }

    return nOld;
}

void SwHistory::CopyFormatAttr(
    const SfxItemSet& rSet,
    SwNodeOffset nNodeIdx)
{
    if(!rSet.Count())
        return;

    SfxItemIter aIter(rSet);
    const SfxPoolItem* pItem = aIter.GetCurItem();
    do
    {
        if(!IsInvalidItem(pItem))
        {
            AddPoolItem(pItem, pItem, nNodeIdx);
        }

        pItem = aIter.NextItem();

    } while(pItem);
}

void SwHistory::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SwHistory"));
    (void)xmlTextWriterWriteFormatAttribute(pWriter, BAD_CAST("ptr"), "%p", this);

    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("m_SwpHstry"));
    for (const auto& pHistory : m_SwpHstry)
    {
        pHistory->dumpAsXml(pWriter);
    }
    (void)xmlTextWriterEndElement(pWriter);

    (void)xmlTextWriterEndElement(pWriter);
}

void SwHistory::CopyAttr(
    SwpHints const * pHts,
    const SwNodeOffset nNodeIdx,
    const sal_Int32 nStart,
    const sal_Int32 nEnd,
    const bool bCopyFields )
{
    if( !pHts  )
        return;

    // copy all attributes of the TextNode in the area from nStart to nEnd
    SwTextAttr* pHt;
    for( size_t n = 0; n < pHts->Count(); ++n )
    {
        // nAttrStt must even be set when !pEndIdx
        pHt = pHts->Get(n);
        const sal_Int32 nAttrStt = pHt->GetStart();
        const sal_Int32 * pEndIdx = pHt->GetEnd();
        if( nullptr !=  pEndIdx && nAttrStt > nEnd )
            break;

        // never copy Flys and Footnote !!
        bool bNextAttr = false;
        switch( pHt->Which() )
        {
        case RES_TXTATR_FIELD:
        case RES_TXTATR_ANNOTATION:
        case RES_TXTATR_INPUTFIELD:
            if( !bCopyFields )
                bNextAttr = true;
            break;
        case RES_TXTATR_FLYCNT:
        case RES_TXTATR_FTN:
            bNextAttr = true;
            break;
        }

        if( bNextAttr )
            continue;

        // save all attributes that are somehow in this area
        if ( nStart <= nAttrStt )
        {
            if ( nEnd > nAttrStt )
            {
                AddTextAttr(pHt, nNodeIdx, false);
            }
        }
        else if ( pEndIdx && nStart < *pEndIdx )
        {
            AddTextAttr(pHt, nNodeIdx, false);
        }
    }
}

// Class to register the history at a Node, Format, HintsArray, ...
SwRegHistory::SwRegHistory( SwHistory* pHst )
    : SwClient( nullptr )
    , m_pHistory( pHst )
    , m_nNodeIndex( NODE_OFFSET_MAX )
{
    MakeSetWhichIds();
}

SwRegHistory::SwRegHistory( sw::BroadcastingModify* pRegIn, const SwNode& rNd,
                            SwHistory* pHst )
    : SwClient( pRegIn )
    , m_pHistory( pHst )
    , m_nNodeIndex( rNd.GetIndex() )
{
    MakeSetWhichIds();
}

SwRegHistory::SwRegHistory( const SwNode& rNd, SwHistory* pHst )
    : SwClient( nullptr )
    , m_pHistory( pHst )
    , m_nNodeIndex( rNd.GetIndex() )
{
    MakeSetWhichIds();
}

void SwRegHistory::SwClientNotify(const SwModify&, const SfxHint& rHint)
{
    if (rHint.GetId() == SfxHintId::SwAttrSetChange)
    {
        auto pChangeHint = static_cast<const sw::AttrSetChangeHint*>(&rHint);
        if ( !(m_pHistory && pChangeHint->m_pNew && pChangeHint->m_pOld != pChangeHint->m_pNew) )
            return;
        if (pChangeHint->m_pOld)
        {
            std::unique_ptr<SwHistoryHint> pNewHstr;
            const SfxItemSet& rSet = *pChangeHint->m_pOld->GetChgSet();

            if ( 1 < rSet.Count() )
            {
                pNewHstr.reset( new SwHistorySetAttrSet( rSet, m_nNodeIndex, m_WhichIdSet ) );
            }
            else if (const SfxPoolItem* pItem = SfxItemIter(rSet).GetCurItem())
            {
                if ( m_WhichIdSet.count( pItem->Which() ) )
                {
                    pNewHstr.reset( new SwHistorySetFormat( pItem, m_nNodeIndex ) );
                }
                else
                {
                    pNewHstr.reset( new SwHistoryResetFormat( pItem, m_nNodeIndex ) );
                }
            }

            if (pNewHstr)
                m_pHistory->m_SwpHstry.push_back( std::move(pNewHstr) );
        }
        return;
    }
}

void SwRegHistory::AddHint( SwTextAttr* pHt, const bool bNew )
{
    m_pHistory->AddTextAttr(pHt, m_nNodeIndex, bNew);
}

bool SwRegHistory::InsertItems( const SfxItemSet& rSet,
    sal_Int32 const nStart, sal_Int32 const nEnd, SetAttrMode const nFlags,
    SwTextAttr **ppNewTextAttr )
{
    if( !rSet.Count() )
        return false;

    SwTextNode * const pTextNode =
        dynamic_cast<SwTextNode *>(GetRegisteredIn());

    OSL_ENSURE(pTextNode, "SwRegHistory not registered at text node?");
    if (!pTextNode)
        return false;

    if (m_pHistory)
    {
        pTextNode->GetOrCreateSwpHints().Register(this);
    }

    const bool bInserted = pTextNode->SetAttr( rSet, nStart, nEnd, nFlags, ppNewTextAttr );

    // Caution: The array can be deleted when inserting an attribute!
    // This can happen when the value that should be added first deletes
    // an existing attribute but does not need to be added itself because
    // the paragraph attributes are identical
    // ( -> bForgetAttr in SwpHints::Insert )
    if ( pTextNode->GetpSwpHints() && m_pHistory )
    {
        pTextNode->GetpSwpHints()->DeRegister();
    }

#ifndef NDEBUG
    if ( m_pHistory && bInserted )
    {
        SfxItemIter aIter(rSet);
        for (SfxPoolItem const* pItem = aIter.GetCurItem(); pItem; pItem = aIter.NextItem())
        {   // check that the history recorded a hint to reset every item
            sal_uInt16 const nWhich(pItem->Which());
            sal_uInt16 const nExpected(
                (isCHRATR(nWhich) || RES_TXTATR_UNKNOWN_CONTAINER == nWhich)
                    ? RES_TXTATR_AUTOFMT
                    : nWhich);
            if (RES_TXTATR_AUTOFMT == nExpected)
                continue; // special case, may get set on text node itself
                          // tdf#105077 even worse, node's set could cause
                          // nothing at all to be inserted
            assert(std::any_of(
                m_pHistory->m_SwpHstry.begin(), m_pHistory->m_SwpHstry.end(),
                [nExpected](std::unique_ptr<SwHistoryHint> const& pHint) -> bool {
                    SwHistoryResetText const*const pReset(
                            dynamic_cast<SwHistoryResetText const*>(pHint.get()));
                    return pReset && (pReset->GetWhich() == nExpected);
                }));
        }
    }
#endif

    return bInserted;
}

void SwRegHistory::RegisterInModify( sw::BroadcastingModify* pRegIn, const SwNode& rNd )
{
    if ( m_pHistory && pRegIn )
    {
        pRegIn->Add(*this);
        m_nNodeIndex = rNd.GetIndex();
        MakeSetWhichIds();
    }
    else
    {
        m_WhichIdSet.clear();
    }
}

void SwRegHistory::MakeSetWhichIds()
{
    if (!m_pHistory) return;

    m_WhichIdSet.clear();

    if( !GetRegisteredIn() )
        return;

    const SfxItemSet* pSet = nullptr;
    if( auto pContentNode = dynamic_cast< const SwContentNode *>( GetRegisteredIn() )  )
    {
        pSet = pContentNode->GetpSwAttrSet();
    }
    else if ( auto pSwFormat = dynamic_cast< const SwFormat *>( GetRegisteredIn() )  )
    {
        pSet = &pSwFormat->GetAttrSet();
    }
    if( pSet && pSet->Count() )
    {
        SfxItemIter aIter( *pSet );
        for (const SfxPoolItem* pItem = aIter.GetCurItem(); pItem; pItem = aIter.NextItem())
        {
            sal_uInt16 nW = pItem->Which();
            m_WhichIdSet.insert( nW );
        }
    }
}
/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
