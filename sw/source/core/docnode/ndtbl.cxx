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

#include <libxml/xmlwriter.h>
#include <config_wasm_strip.h>
#include <memory>
#include <fesh.hxx>
#include <hintids.hxx>
#include <editeng/lrspitem.hxx>
#include <editeng/protitem.hxx>
#include <editeng/boxitem.hxx>
#include <svl/stritem.hxx>
#include <editeng/shaditem.hxx>
#include <fmtfsize.hxx>
#include <fmtornt.hxx>
#include <fmtfordr.hxx>
#include <fmtpdsc.hxx>
#include <fmtanchr.hxx>
#include <fmtlsplt.hxx>
#include <frmatr.hxx>
#include <cellfrm.hxx>
#include <pagefrm.hxx>
#include <tabcol.hxx>
#include <doc.hxx>
#include <IDocumentUndoRedo.hxx>
#include <UndoManager.hxx>
#include <DocumentSettingManager.hxx>
#include <IDocumentChartDataProviderAccess.hxx>
#include <IDocumentRedlineAccess.hxx>
#include <IDocumentStylePoolAccess.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <IDocumentState.hxx>
#include <cntfrm.hxx>
#include <pam.hxx>
#include <swcrsr.hxx>
#include <swmodule.hxx>
#include <swtable.hxx>
#include <swundo.hxx>
#include <tblsel.hxx>
#include <poolfmt.hxx>
#include <tabfrm.hxx>
#include <UndoCore.hxx>
#include <UndoRedline.hxx>
#include <UndoDelete.hxx>
#include <UndoNumbering.hxx>
#include <UndoTable.hxx>
#include <hints.hxx>
#include <tblafmt.hxx>
#include <frminf.hxx>
#include <cellatr.hxx>
#include <swtblfmt.hxx>
#include <swddetbl.hxx>
#include <mvsave.hxx>
#include <docary.hxx>
#include <redline.hxx>
#include <rolbck.hxx>
#include <tblrwcl.hxx>
#include <editsh.hxx>
#include <txtfrm.hxx>
#include <section.hxx>
#include <frmtool.hxx>
#include <node2lay.hxx>
#include <strings.hrc>
#include <docsh.hxx>
#include <unochart.hxx>
#include <node.hxx>
#include <ndtxt.hxx>
#include <cstdlib>
#include <map>
#include <algorithm>
#include <rootfrm.hxx>
#include <fldupde.hxx>
#include <calbck.hxx>
#include <fntcache.hxx>
#include <frameformats.hxx>
#include <officecfg/Office/Writer.hxx>
#include <o3tl/numeric.hxx>
#include <o3tl/string_view.hxx>
#include <svl/numformat.hxx>
#include <tools/datetimeutils.hxx>
#include <sal/log.hxx>
#include <osl/diagnose.h>

#ifdef DBG_UTIL
#define CHECK_TABLE(t) (t).CheckConsistency();
#else
#define CHECK_TABLE(t)
#endif

using ::editeng::SvxBorderLine;
using namespace ::com::sun::star;

const sal_Unicode T2T_PARA = 0x0a;

static void lcl_SetDfltBoxAttr( SwFrameFormat& rFormat, sal_uInt8 nId )
{
    bool bTop = false, bBottom = false, bLeft = false, bRight = false;
    switch ( nId )
    {
    case 0: bTop = bBottom = bLeft = true;          break;
    case 1: bTop = bBottom = bLeft = bRight = true; break;
    case 2: bBottom = bLeft = true;                 break;
    case 3: bBottom = bLeft = bRight = true;        break;
    }

    const bool bHTML = rFormat.getIDocumentSettingAccess().get(DocumentSettingId::HTML_MODE);
    Color aCol( bHTML ? COL_GRAY : COL_BLACK );
    // Default border in Writer: 0.5pt (matching Word)
    SvxBorderLine aLine( &aCol, SvxBorderLineWidth::VeryThin );
    if ( bHTML )
    {
        aLine.SetBorderLineStyle(SvxBorderLineStyle::DOUBLE);
    }
    SvxBoxItem aBox(RES_BOX);
    aBox.SetAllDistances(55);
    if ( bTop )
        aBox.SetLine( &aLine, SvxBoxItemLine::TOP );
    if ( bBottom )
        aBox.SetLine( &aLine, SvxBoxItemLine::BOTTOM );
    if ( bLeft )
        aBox.SetLine( &aLine, SvxBoxItemLine::LEFT );
    if ( bRight )
        aBox.SetLine( &aLine, SvxBoxItemLine::RIGHT );
    rFormat.SetFormatAttr( aBox );
}

typedef std::map<SwFrameFormat *, SwTableBoxFormat *> DfltBoxAttrMap_t;
typedef std::vector<DfltBoxAttrMap_t *> DfltBoxAttrList_t;

static void
lcl_SetDfltBoxAttr(SwTableBox& rBox, DfltBoxAttrList_t & rBoxFormatArr,
        sal_uInt8 const nId, SwTableAutoFormat const*const pAutoFormat = nullptr)
{
    DfltBoxAttrMap_t * pMap = rBoxFormatArr[ nId ];
    if (!pMap)
    {
        pMap = new DfltBoxAttrMap_t;
        rBoxFormatArr[ nId ] = pMap;
    }

    SwTableBoxFormat* pNewTableBoxFormat = nullptr;
    SwFrameFormat* pBoxFrameFormat = rBox.GetFrameFormat();
    DfltBoxAttrMap_t::iterator const iter(pMap->find(pBoxFrameFormat));
    if (pMap->end() != iter)
    {
        pNewTableBoxFormat = iter->second;
    }
    else
    {
        SwDoc& rDoc = pBoxFrameFormat->GetDoc();
        // format does not exist, so create it
        pNewTableBoxFormat = rDoc.MakeTableBoxFormat();
        pNewTableBoxFormat->SetFormatAttr( pBoxFrameFormat->GetAttrSet().Get( RES_FRM_SIZE ) );

        if( pAutoFormat )
            pAutoFormat->UpdateToSet( nId, false, false,
                                    const_cast<SfxItemSet&>(static_cast<SfxItemSet const &>(pNewTableBoxFormat->GetAttrSet())),
                                    SwTableAutoFormatUpdateFlags::Box,
                                    rDoc.GetNumberFormatter() );
        else
            ::lcl_SetDfltBoxAttr( *pNewTableBoxFormat, nId );

        (*pMap)[pBoxFrameFormat] = pNewTableBoxFormat;
    }
    rBox.ChgFrameFormat( pNewTableBoxFormat );
}

static SwTableBoxFormat *lcl_CreateDfltBoxFormat( SwDoc &rDoc, std::vector<SwTableBoxFormat*> &rBoxFormatArr,
                                    sal_uInt16 nCols, sal_uInt8 nId )
{
    if ( !rBoxFormatArr[nId] )
    {
        SwTableBoxFormat* pBoxFormat = rDoc.MakeTableBoxFormat();
        if( USHRT_MAX != nCols )
            pBoxFormat->SetFormatAttr( SwFormatFrameSize( SwFrameSize::Variable,
                                            USHRT_MAX / nCols, 0 ));
        ::lcl_SetDfltBoxAttr( *pBoxFormat, nId );
        rBoxFormatArr[ nId ] = pBoxFormat;
    }
    return rBoxFormatArr[nId];
}

static SwTableBoxFormat *lcl_CreateAFormatBoxFormat( SwDoc &rDoc, std::vector<SwTableBoxFormat*> &rBoxFormatArr,
                                    const SwTableAutoFormat& rAutoFormat,
                                    const sal_uInt16 nRows, const sal_uInt16 nCols, sal_uInt8 nId )
{
    if( !rBoxFormatArr[nId] )
    {
        SwTableBoxFormat* pBoxFormat = rDoc.MakeTableBoxFormat();
        rAutoFormat.UpdateToSet( nId, nRows==1, nCols==1,
                                const_cast<SfxItemSet&>(static_cast<SfxItemSet const &>(pBoxFormat->GetAttrSet())),
                                SwTableAutoFormatUpdateFlags::Box,
                                rDoc.GetNumberFormatter( ) );
        if( USHRT_MAX != nCols )
            pBoxFormat->SetFormatAttr( SwFormatFrameSize( SwFrameSize::Variable,
                                            USHRT_MAX / nCols, 0 ));
        rBoxFormatArr[ nId ] = pBoxFormat;
    }
    return rBoxFormatArr[nId];
}

SwTableNode* SwDoc::IsIdxInTable( const SwNodeIndex& rIdx ) { return IsInTable(rIdx.GetNode()); }

SwTableNode* SwDoc::IsInTable(const SwNode& rIdx)
{
    SwNode* pNd = const_cast<SwNode*>(&rIdx);
    do {
        pNd = pNd->StartOfSectionNode();
        SwTableNode* pTableNd = pNd->GetTableNode();
        if( pTableNd )
            return pTableNd;
    } while ( pNd->GetIndex() );
    return nullptr;
}

/**
 * Insert a new Box before the InsPos
 */
bool SwNodes::InsBoxen( SwTableNode* pTableNd,
                        SwTableLine* pLine,
                        SwTableBoxFormat* pBoxFormat,
                        SwTextFormatColl* pTextColl,
                        const SfxItemSet* pAutoAttr,
                        sal_uInt16 nInsPos,
                        sal_uInt16 nCnt )
{
    if( !nCnt )
        return false;
    OSL_ENSURE( pLine, "No valid Line" );

    // Move Index after the Line's last Box
    SwNodeOffset nIdxPos(0);
    SwTableBox *pPrvBox = nullptr, *pNxtBox = nullptr;
    if( !pLine->GetTabBoxes().empty() )
    {
        if( nInsPos < pLine->GetTabBoxes().size() )
        {
            pPrvBox = pLine->FindPreviousBox( pTableNd->GetTable(),
                            pLine->GetTabBoxes()[ nInsPos ] );
            if( nullptr == pPrvBox )
                pPrvBox = pLine->FindPreviousBox( pTableNd->GetTable() );
        }
        else
        {
            pNxtBox = pLine->FindNextBox( pTableNd->GetTable(),
                            pLine->GetTabBoxes().back() );
            if( nullptr == pNxtBox )
                pNxtBox = pLine->FindNextBox( pTableNd->GetTable() );
        }
    }
    else
    {
        pNxtBox = pLine->FindNextBox( pTableNd->GetTable() );
        if( nullptr == pNxtBox )
            pPrvBox = pLine->FindPreviousBox( pTableNd->GetTable() );
    }

    if( !pPrvBox && !pNxtBox )
    {
        bool bSetIdxPos = true;
        if( !pTableNd->GetTable().GetTabLines().empty() && !nInsPos )
        {
            const SwTableLine* pTableLn = pLine;
            while( pTableLn->GetUpper() )
                pTableLn = pTableLn->GetUpper()->GetUpper();

            if( pTableNd->GetTable().GetTabLines()[ 0 ] == pTableLn )
            {
                // Before the Table's first Box
                while( !( pNxtBox = pLine->GetTabBoxes()[0])->GetTabLines().empty() )
                    pLine = pNxtBox->GetTabLines()[0];
                nIdxPos = pNxtBox->GetSttIdx();
                bSetIdxPos = false;
            }
        }
        if( bSetIdxPos )
            // Tables without content or at the end; move before the End
            nIdxPos = pTableNd->EndOfSectionIndex();
    }
    else if( pNxtBox ) // There is a successor
        nIdxPos = pNxtBox->GetSttIdx();
    else // There is a predecessor
        nIdxPos = pPrvBox->GetSttNd()->EndOfSectionIndex() + 1;

    SwNodeIndex aEndIdx( *this, nIdxPos );
    for( sal_uInt16 n = 0; n < nCnt; ++n )
    {
        SwStartNode* pSttNd = new SwStartNode( aEndIdx.GetNode(), SwNodeType::Start,
                                                SwTableBoxStartNode );
        pSttNd->m_pStartOfSection = pTableNd;
        new SwEndNode( aEndIdx.GetNode(), *pSttNd );

        pPrvBox = new SwTableBox( pBoxFormat, *pSttNd, pLine );

        SwTableBoxes & rTabBoxes = pLine->GetTabBoxes();
        sal_uInt16 nRealInsPos = nInsPos + n;
        if (nRealInsPos > rTabBoxes.size())
            nRealInsPos = rTabBoxes.size();

        rTabBoxes.insert( rTabBoxes.begin() + nRealInsPos, pPrvBox );

        if( ! pTextColl->IsAssignedToListLevelOfOutlineStyle()
            && RES_CONDTXTFMTCOLL != pTextColl->Which()
        )
            new SwTextNode( *pSttNd->EndOfSectionNode(), pTextColl, pAutoAttr );
        else
        {
            // Handle Outline numbering correctly!
            SwTextNode* pTNd = new SwTextNode(
                            *pSttNd->EndOfSectionNode(),
                            GetDoc().GetDfltTextFormatColl(),
                            pAutoAttr );
            pTNd->ChgFormatColl( pTextColl );
        }
    }
    return true;
}

/**
 * Insert a new Table
 */
const SwTable* SwDoc::InsertTable( const SwInsertTableOptions& rInsTableOpts,
                                   const SwPosition& rPos, sal_uInt16 nRows,
                                   sal_uInt16 nCols, sal_Int16 eAdjust,
                                   const SwTableAutoFormat* pTAFormat,
                                   const std::vector<sal_uInt16> *pColArr,
                                   bool bCalledFromShell,
                                   bool bNewModel,
                                   const OUString& rTableName )
{
    assert(nRows && "Table without line?");
    assert(nCols && "Table without rows?");

    {
        // Do not copy into Footnotes!
        if( rPos.GetNode() < GetNodes().GetEndOfInserts() &&
            rPos.GetNode().GetIndex() >= GetNodes().GetEndOfInserts().StartOfSectionIndex() )
            return nullptr;

        // If the ColumnArray has a wrong count, ignore it!
        if( pColArr &&
            static_cast<size_t>(nCols) + ( text::HoriOrientation::NONE == eAdjust ? 2 : 1 ) != pColArr->size() )
            pColArr = nullptr;
    }

    UIName aTableName(rTableName);
    if (aTableName.isEmpty() || FindTableFormatByName(aTableName) != nullptr)
        aTableName = GetUniqueTableName();

    if( GetIDocumentUndoRedo().DoesUndo() )
    {
        GetIDocumentUndoRedo().AppendUndo(
            std::make_unique<SwUndoInsTable>( rPos, nCols, nRows, o3tl::narrowing<sal_uInt16>(eAdjust),
                                      rInsTableOpts, pTAFormat, pColArr,
                                      aTableName));
    }

    // Start with inserting the Nodes and get the AutoFormat for the Table
    SwTextFormatColl *pBodyColl = getIDocumentStylePoolAccess().GetTextCollFromPool( RES_POOLCOLL_TABLE ),
                 *pHeadColl = pBodyColl;

    bool bDfltBorders( rInsTableOpts.mnInsMode & SwInsertTableFlags::DefaultBorder );

    if( (rInsTableOpts.mnInsMode & SwInsertTableFlags::Headline) && (1 != nRows || !bDfltBorders) )
        pHeadColl = getIDocumentStylePoolAccess().GetTextCollFromPool( RES_POOLCOLL_TABLE_HDLN );

    const sal_uInt16 nRowsToRepeat =
            SwInsertTableFlags::Headline == (rInsTableOpts.mnInsMode & SwInsertTableFlags::Headline) ?
            rInsTableOpts.mnRowsToRepeat :
            0;

    /* Save content node to extract FRAMEDIR from. */
    const SwContentNode * pContentNd = rPos.GetNode().GetContentNode();

    /* If we are called from a shell pass the attrset from
        pContentNd (aka the node the table is inserted at) thus causing
        SwNodes::InsertTable to propagate an adjust item if
        necessary. */
    SwTableNode *pTableNd = SwNodes::InsertTable(
        rPos.GetNode(),
        nCols,
        pBodyColl,
        nRows,
        nRowsToRepeat,
        pHeadColl,
        bCalledFromShell ? &pContentNd->GetSwAttrSet() : nullptr );

    // Create the Box/Line/Table construct
    SwTableLineFormat* pLineFormat = MakeTableLineFormat();
    SwTableFormat* pTableFormat = MakeTableFrameFormat( aTableName, GetDfltFrameFormat() );

    /* If the node to insert the table at is a context node and has a
       non-default FRAMEDIR propagate it to the table. */
    if (pContentNd)
    {
        const SwAttrSet & aNdSet = pContentNd->GetSwAttrSet();
        if (const SvxFrameDirectionItem* pItem = aNdSet.GetItemIfSet( RES_FRAMEDIR ))
        {
            pTableFormat->SetFormatAttr( *pItem );
        }
    }

    // Set Orientation at the Table's Format
    pTableFormat->SetFormatAttr( SwFormatHoriOrient( 0, eAdjust ) );
    // All lines use the left-to-right Fill-Order!
    pLineFormat->SetFormatAttr( SwFormatFillOrder( ATT_LEFT_TO_RIGHT ));

    // Set USHRT_MAX as the Table's default SSize
    SwTwips nWidth = USHRT_MAX;
    if( pColArr )
    {
        sal_uInt16 nSttPos = pColArr->front();
        sal_uInt16 nLastPos = pColArr->back();
        if( text::HoriOrientation::NONE == eAdjust )
        {
            sal_uInt16 nFrameWidth = nLastPos;
            nLastPos = (*pColArr)[ pColArr->size()-2 ];
            pTableFormat->SetFormatAttr(SvxLRSpaceItem(
                SvxIndentValue::twips(nSttPos), SvxIndentValue::twips(nFrameWidth - nLastPos),
                SvxIndentValue::zero(), RES_LR_SPACE));
        }
        nWidth = nLastPos - nSttPos;
    }
    else
    {
        nWidth /= nCols;
        nWidth *= nCols; // to avoid rounding problems
    }
    pTableFormat->SetFormatAttr( SwFormatFrameSize( SwFrameSize::Variable, nWidth ));
    if( !(rInsTableOpts.mnInsMode & SwInsertTableFlags::SplitLayout) )
        pTableFormat->SetFormatAttr( SwFormatLayoutSplit( false ));

    // Move the hard PageDesc/PageBreak Attributes if needed
    SwContentNode* pNextNd = GetNodes()[ pTableNd->EndOfSectionIndex()+1 ]
                            ->GetContentNode();
    if( pNextNd && pNextNd->HasSwAttrSet() )
    {
        const SfxItemSet* pNdSet = pNextNd->GetpSwAttrSet();
        if( const SwFormatPageDesc* pItem = pNdSet->GetItemIfSet( RES_PAGEDESC, false ) )
        {
            pTableFormat->SetFormatAttr( *pItem );
            pNextNd->ResetAttr( RES_PAGEDESC );
            pNdSet = pNextNd->GetpSwAttrSet();
        }
        const SvxFormatBreakItem* pItem;
        if( pNdSet && (pItem = pNdSet->GetItemIfSet( RES_BREAK, false )) )
        {
            pTableFormat->SetFormatAttr( *pItem );
            pNextNd->ResetAttr( RES_BREAK );
        }
    }

    SwTable& rNdTable = pTableNd->GetTable();
    rNdTable.RegisterToFormat( *pTableFormat );

    rNdTable.SetRowsToRepeat( nRowsToRepeat );
    rNdTable.SetTableModel( bNewModel );

    std::vector<SwTableBoxFormat*> aBoxFormatArr;
    SwTableBoxFormat* pBoxFormat = nullptr;
    if( !bDfltBorders && !pTAFormat )
    {
        pBoxFormat = MakeTableBoxFormat();
        pBoxFormat->SetFormatAttr( SwFormatFrameSize( SwFrameSize::Variable, USHRT_MAX / nCols, 0 ));
    }
    else
    {
        const sal_uInt16 nBoxArrLen = pTAFormat ? 16 : 4;
        aBoxFormatArr.resize( nBoxArrLen, nullptr );
    }
    SfxItemSet aCharSet(SfxItemSet::makeFixedSfxItemSet<RES_CHRATR_BEGIN, RES_PARATR_LIST_END-1>(GetAttrPool()));

    SwNodeIndex aNdIdx( *pTableNd, 1 ); // Set to StartNode of first Box
    SwTableLines& rLines = rNdTable.GetTabLines();
    for( sal_uInt16 n = 0; n < nRows; ++n )
    {
        SwTableLine* pLine = new SwTableLine( pLineFormat, nCols, nullptr );
        rLines.insert( rLines.begin() + n, pLine );
        SwTableBoxes& rBoxes = pLine->GetTabBoxes();
        for( sal_uInt16 i = 0; i < nCols; ++i )
        {
            SwTableBoxFormat *pBoxF;
            if( pTAFormat )
            {
                sal_uInt8 nId = SwTableAutoFormat::CountPos(i, nCols, n, nRows);
                pBoxF = ::lcl_CreateAFormatBoxFormat( *this, aBoxFormatArr, *pTAFormat,
                                                nRows, nCols, nId );

                // Set the Paragraph/Character Attributes if needed
                if( pTAFormat->IsFont() || pTAFormat->IsJustify() )
                {
                    aCharSet.ClearItem();
                    pTAFormat->UpdateToSet( nId, nRows==1, nCols==1, aCharSet,
                                        SwTableAutoFormatUpdateFlags::Char, nullptr );
                    if( aCharSet.Count() )
                        GetNodes()[ aNdIdx.GetIndex()+1 ]->GetContentNode()->
                            SetAttr( aCharSet );
                }
            }
            else if( bDfltBorders )
            {
                sal_uInt8 nBoxId = (i < nCols - 1 ? 0 : 1) + (n ? 2 : 0 );
                pBoxF = ::lcl_CreateDfltBoxFormat( *this, aBoxFormatArr, nCols, nBoxId);
            }
            else
                pBoxF = pBoxFormat;

            // For AutoFormat on input: the columns are set when inserting the Table
            // The Array contains the columns positions and not their widths!
            if( pColArr )
            {
                nWidth = (*pColArr)[ i + 1 ] - (*pColArr)[ i ];
                if( pBoxF->GetFrameSize().GetWidth() != nWidth )
                {
                    if( pBoxF->HasWriterListeners() ) // Create new Format
                    {
                        SwTableBoxFormat *pNewFormat = MakeTableBoxFormat();
                        *pNewFormat = *pBoxF;
                        pBoxF = pNewFormat;
                    }
                    pBoxF->SetFormatAttr( SwFormatFrameSize( SwFrameSize::Variable, nWidth ));
                }
            }

            SwTableBox *pBox = new SwTableBox( pBoxF, aNdIdx, pLine);
            rBoxes.insert( rBoxes.begin() + i, pBox );
            aNdIdx += SwNodeOffset(3); // StartNode, TextNode, EndNode  == 3 Nodes
        }
    }
    // Insert Frames
    pTableNd->MakeOwnFrames();

    // To-Do - add 'SwExtraRedlineTable' also ?
    if( getIDocumentRedlineAccess().IsRedlineOn() || (!getIDocumentRedlineAccess().IsIgnoreRedline() && !getIDocumentRedlineAccess().GetRedlineTable().empty() ))
    {
        SwPaM aPam( *pTableNd->EndOfSectionNode(), *pTableNd, SwNodeOffset(1) );
        if( getIDocumentRedlineAccess().IsRedlineOn() )
            getIDocumentRedlineAccess().AppendRedline( new SwRangeRedline( RedlineType::Insert, aPam ), true);
        else
            getIDocumentRedlineAccess().SplitRedline( aPam );
    }

    getIDocumentState().SetModified();
    CHECK_TABLE(rNdTable);
    return &rNdTable;
}

SwTableNode* SwNodes::InsertTable( const SwNode& rNd,
                                   sal_uInt16 nBoxes,
                                   SwTextFormatColl* pContentTextColl,
                                   sal_uInt16 nLines,
                                   sal_uInt16 nRepeat,
                                   SwTextFormatColl* pHeadlineTextColl,
                                   const SwAttrSet * pAttrSet)
{
    if( !nBoxes )
        return nullptr;

    // If Lines is given, create the Matrix from Lines and Boxes
    if( !pHeadlineTextColl || !nLines )
        pHeadlineTextColl = pContentTextColl;

    SwTableNode * pTableNd = new SwTableNode( rNd );
    SwEndNode* pEndNd = new SwEndNode( rNd, *pTableNd );

    if( !nLines ) // For the for loop
        ++nLines;

    SwTextFormatColl* pTextColl = pHeadlineTextColl;
    for( sal_uInt16 nL = 0; nL < nLines; ++nL )
    {
        for( sal_uInt16 nB = 0; nB < nBoxes; ++nB )
        {
            SwStartNode* pSttNd = new SwStartNode( *pEndNd, SwNodeType::Start,
                                                    SwTableBoxStartNode );
            pSttNd->m_pStartOfSection = pTableNd;

            SwTextNode * pTmpNd = new SwTextNode( *pEndNd, pTextColl );

            // #i60422# Propagate some more attributes.
            const SfxPoolItem* pItem = nullptr;
            if ( nullptr != pAttrSet )
            {
                static const sal_uInt16 aPropagateItems[] = {
                    RES_PARATR_ADJUST,
                    RES_CHRATR_FONT, RES_CHRATR_FONTSIZE,
                    RES_CHRATR_CJK_FONT, RES_CHRATR_CJK_FONTSIZE,
                    RES_CHRATR_CTL_FONT, RES_CHRATR_CTL_FONTSIZE, 0 };

                const sal_uInt16* pIdx = aPropagateItems;
                while ( *pIdx != 0 )
                {
                    if ( SfxItemState::SET != pTmpNd->GetSwAttrSet().GetItemState( *pIdx ) &&
                         SfxItemState::SET == pAttrSet->GetItemState( *pIdx, true, &pItem ) )
                        static_cast<SwContentNode *>(pTmpNd)->SetAttr(*pItem);
                    ++pIdx;
                }
            }

            new SwEndNode( *pEndNd, *pSttNd );
        }
        if ( nL + 1 >= nRepeat )
            pTextColl = pContentTextColl;
    }
    return pTableNd;
}

/**
 * Text to Table
 */
const SwTable* SwDoc::TextToTable( const SwInsertTableOptions& rInsTableOpts,
                                   const SwPaM& rRange, sal_Unicode cCh,
                                   sal_Int16 eAdjust,
                                   const SwTableAutoFormat* pTAFormat )
{
    // See if the selection contains a Table
    auto [pStart, pEnd] = rRange.StartEnd(); // SwPosition*
    {
        SwNodeOffset nCnt = pStart->GetNodeIndex();
        for( ; nCnt <= pEnd->GetNodeIndex(); ++nCnt )
            if( !GetNodes()[ nCnt ]->IsTextNode() )
                return nullptr;
    }

    if (GetIDocumentUndoRedo().DoesUndo())
    {
        GetIDocumentUndoRedo().StartUndo(SwUndoId::TEXTTOTABLE, nullptr);
    }

    // tdf#153115 first, remove all redlines; splitting them at cell boundaries
    // would be tricky to implement, and it's unclear what the value of
    // existing redlines is once it's been converted to a table
    getIDocumentRedlineAccess().AcceptRedline(rRange, true);

    // Save first node in the selection if it is a context node
    SwContentNode * pSttContentNd = pStart->GetNode().GetContentNode();

    SwPaM aOriginal( *pStart, *pEnd );
    pStart = aOriginal.GetMark();
    pEnd = aOriginal.GetPoint();

    SwUndoTextToTable* pUndo = nullptr;
    if( GetIDocumentUndoRedo().DoesUndo() )
    {
        pUndo = new SwUndoTextToTable( aOriginal, rInsTableOpts, cCh,
                    o3tl::narrowing<sal_uInt16>(eAdjust), pTAFormat );
        GetIDocumentUndoRedo().AppendUndo( std::unique_ptr<SwUndo>(pUndo) );

        // Do not add splitting the TextNode to the Undo history
        GetIDocumentUndoRedo().DoUndo( false );
    }

    ::PaMCorrAbs( aOriginal, *pEnd );

    // Make sure that the range is on Node Edges
    SwNodeRange aRg( pStart->GetNode(), pEnd->GetNode() );
    if( pStart->GetContentIndex() )
        getIDocumentContentOperations().SplitNode( *pStart, false );

    bool bEndContent = 0 != pEnd->GetContentIndex();

    // Do not split at the End of a Line (except at the End of the Doc)
    if( bEndContent )
    {
        if( pEnd->GetNode().GetContentNode()->Len() != pEnd->GetContentIndex()
            || pEnd->GetNodeIndex() >= GetNodes().GetEndOfContent().GetIndex()-1 )
        {
            getIDocumentContentOperations().SplitNode( *pEnd, false );
            const_cast<SwPosition*>(pEnd)->Adjust(SwNodeOffset(-1));
            // A Node and at the End?
            if( pStart->GetNodeIndex() >= pEnd->GetNodeIndex() )
                --aRg.aStart;
        }
        else
            ++aRg.aEnd;
    }

    if( aRg.aEnd.GetIndex() == aRg.aStart.GetIndex() )
    {
        OSL_FAIL( "empty range" );
        ++aRg.aEnd;
    }

    // We always use Upper to insert the Table
    SwNode2LayoutSaveUpperFrames aNode2Layout( aRg.aStart.GetNode() );

    GetIDocumentUndoRedo().DoUndo( nullptr != pUndo );

    // Create the Box/Line/Table construct
    SwTableBoxFormat* pBoxFormat = MakeTableBoxFormat();
    SwTableLineFormat* pLineFormat = MakeTableLineFormat();
    SwTableFormat* pTableFormat = MakeTableFrameFormat( GetUniqueTableName(), GetDfltFrameFormat() );

    // All Lines have a left-to-right Fill Order
    pLineFormat->SetFormatAttr( SwFormatFillOrder( ATT_LEFT_TO_RIGHT ));
    // The Table's SSize is USHRT_MAX
    pTableFormat->SetFormatAttr( SwFormatFrameSize( SwFrameSize::Variable, USHRT_MAX ));
    if( !(rInsTableOpts.mnInsMode & SwInsertTableFlags::SplitLayout) )
        pTableFormat->SetFormatAttr( SwFormatLayoutSplit( false ));

    /* If the first node in the selection is a context node and if it
       has an item FRAMEDIR set (no default) propagate the item to the
       replacing table. */
    if (pSttContentNd)
    {
        const SwAttrSet & aNdSet = pSttContentNd->GetSwAttrSet();
        if (const SvxFrameDirectionItem *pItem = aNdSet.GetItemIfSet( RES_FRAMEDIR ) )
        {
            pTableFormat->SetFormatAttr( *pItem );
        }
    }

    //Resolves: tdf#87977, tdf#78599, disable broadcasting modifications
    //until after RegisterToFormat is completed
    bool bEnableSetModified = getIDocumentState().IsEnableSetModified();
    getIDocumentState().SetEnableSetModified(false);

    SwTableNode* pTableNd = GetNodes().TextToTable(
            aRg, cCh, pTableFormat, pLineFormat, pBoxFormat,
            getIDocumentStylePoolAccess().GetTextCollFromPool( RES_POOLCOLL_STANDARD ), pUndo );

    SwTable& rNdTable = pTableNd->GetTable();

    const sal_uInt16 nRowsToRepeat =
            SwInsertTableFlags::Headline == (rInsTableOpts.mnInsMode & SwInsertTableFlags::Headline) ?
            rInsTableOpts.mnRowsToRepeat :
            0;
    rNdTable.SetRowsToRepeat(nRowsToRepeat);

    bool bUseBoxFormat = false;
    if( !pBoxFormat->HasWriterListeners() )
    {
        // The Box's Formats already have the right size, we must only set
        // the right Border/AutoFormat.
        bUseBoxFormat = true;
        pTableFormat->SetFormatAttr( pBoxFormat->GetFrameSize() );
        delete pBoxFormat;
        eAdjust = text::HoriOrientation::NONE;
    }

    // Set Orientation in the Table's Format
    pTableFormat->SetFormatAttr( SwFormatHoriOrient( 0, eAdjust ) );
    rNdTable.RegisterToFormat(*pTableFormat);

    if( pTAFormat || ( rInsTableOpts.mnInsMode & SwInsertTableFlags::DefaultBorder) )
    {
        sal_uInt8 nBoxArrLen = pTAFormat ? 16 : 4;
        std::unique_ptr< DfltBoxAttrList_t > aBoxFormatArr1;
        std::optional< std::vector<SwTableBoxFormat*> > aBoxFormatArr2;
        if( bUseBoxFormat )
        {
            aBoxFormatArr1.reset(new DfltBoxAttrList_t( nBoxArrLen, nullptr ));
        }
        else
        {
            aBoxFormatArr2 = std::vector<SwTableBoxFormat*>( nBoxArrLen, nullptr );
        }

        SfxItemSet aCharSet(SfxItemSet::makeFixedSfxItemSet<RES_CHRATR_BEGIN, RES_PARATR_LIST_END-1>(GetAttrPool()));

        SwHistory* pHistory = pUndo ? &pUndo->GetHistory() : nullptr;

        SwTableBoxFormat *pBoxF = nullptr;
        SwTableLines& rLines = rNdTable.GetTabLines();
        const SwTableLines::size_type nRows = rLines.size();
        for( SwTableLines::size_type n = 0; n < nRows; ++n )
        {
            SwTableBoxes& rBoxes = rLines[ n ]->GetTabBoxes();
            const SwTableBoxes::size_type nCols = rBoxes.size();
            for( SwTableBoxes::size_type i = 0; i < nCols; ++i )
            {
                SwTableBox* pBox = rBoxes[ i ];
                bool bChgSz = false;

                if( pTAFormat )
                {
                    sal_uInt8 nId = static_cast<sal_uInt8>(!n ? 0 : (( n+1 == nRows )
                                            ? 12 : (4 * (1 + ((n-1) & 1 )))));
                    nId = nId + static_cast<sal_uInt8>(!i ? 0 :
                                ( i+1 == nCols ? 3 : (1 + ((i-1) & 1))));
                    if( bUseBoxFormat )
                        ::lcl_SetDfltBoxAttr( *pBox, *aBoxFormatArr1, nId, pTAFormat );
                    else
                    {
                        bChgSz = nullptr == (*aBoxFormatArr2)[ nId ];
                        pBoxF = ::lcl_CreateAFormatBoxFormat( *this, *aBoxFormatArr2,
                                                *pTAFormat, USHRT_MAX, USHRT_MAX, nId );
                    }

                    // Set Paragraph/Character Attributes if needed
                    if( pTAFormat->IsFont() || pTAFormat->IsJustify() )
                    {
                        aCharSet.ClearItem();
                        pTAFormat->UpdateToSet( nId, nRows==1, nCols==1, aCharSet,
                                            SwTableAutoFormatUpdateFlags::Char, nullptr );
                        if( aCharSet.Count() )
                        {
                            SwNodeOffset nSttNd = pBox->GetSttIdx()+1;
                            SwNodeOffset nEndNd = pBox->GetSttNd()->EndOfSectionIndex();
                            for( ; nSttNd < nEndNd; ++nSttNd )
                            {
                                SwContentNode* pNd = GetNodes()[ nSttNd ]->GetContentNode();
                                if( pNd )
                                {
                                    if( pHistory )
                                    {
                                        SwRegHistory aReg( pNd, *pNd, pHistory );
                                        pNd->SetAttr( aCharSet );
                                    }
                                    else
                                        pNd->SetAttr( aCharSet );
                                }
                            }
                        }
                    }
                }
                else
                {
                    sal_uInt8 nId = (i < nCols - 1 ? 0 : 1) + (n ? 2 : 0 );
                    if( bUseBoxFormat )
                        ::lcl_SetDfltBoxAttr( *pBox, *aBoxFormatArr1, nId );
                    else
                    {
                        bChgSz = nullptr == (*aBoxFormatArr2)[ nId ];
                        pBoxF = ::lcl_CreateDfltBoxFormat( *this, *aBoxFormatArr2,
                                                        USHRT_MAX, nId );
                    }
                }

                if( !bUseBoxFormat )
                {
                    if( bChgSz )
                        pBoxF->SetFormatAttr( pBox->GetFrameFormat()->GetFrameSize() );
                    pBox->ChgFrameFormat( pBoxF );
                }
            }
        }

        if( bUseBoxFormat )
        {
            for( sal_uInt8 i = 0; i < nBoxArrLen; ++i )
            {
                delete (*aBoxFormatArr1)[ i ];
            }
        }
    }

    // Check the boxes for numbers
    if( IsInsTableFormatNum() )
    {
        for (size_t nBoxes = rNdTable.GetTabSortBoxes().size(); nBoxes; )
        {
            ChkBoxNumFormat(*rNdTable.GetTabSortBoxes()[ --nBoxes ], false);
        }
    }

    SwNodeOffset nIdx = pTableNd->GetIndex();
    aNode2Layout.RestoreUpperFrames( GetNodes(), nIdx, nIdx + 1 );

    {
        SwPaM& rTmp = const_cast<SwPaM&>(rRange); // Point always at the Start
        rTmp.DeleteMark();
        rTmp.GetPoint()->Assign( *pTableNd );
        SwNodes::GoNext(rTmp.GetPoint());
    }

    if( pUndo )
    {
        GetIDocumentUndoRedo().EndUndo( SwUndoId::TEXTTOTABLE, nullptr );
    }

    getIDocumentState().SetEnableSetModified(bEnableSetModified);
    getIDocumentState().SetModified();
    getIDocumentFieldsAccess().SetFieldsDirty(true, nullptr, SwNodeOffset(0));
    return &rNdTable;
}

static void lcl_RemoveBreaksTable(SwTableNode & rNode, SwTableFormat *const pTableFormat)
{
    // delete old layout frames, new ones need to be created...
    rNode.DelFrames(nullptr);

    // remove PageBreaks/PageDesc/ColBreak
    SwFrameFormat & rFormat(*rNode.GetTable().GetFrameFormat());

    if (const SvxFormatBreakItem* pItem = rFormat.GetItemIfSet(RES_BREAK, false))
    {
        if (pTableFormat)
        {
            pTableFormat->SetFormatAttr(*pItem);
        }
        rFormat.ResetFormatAttr(RES_BREAK);
    }

    SwFormatPageDesc const*const pPageDescItem(rFormat.GetItemIfSet(RES_PAGEDESC, false));
    if (pPageDescItem && pPageDescItem->GetPageDesc())
    {
        if (pTableFormat)
        {
            pTableFormat->SetFormatAttr(*pPageDescItem);
        }
        rFormat.ResetFormatAttr(RES_PAGEDESC);
    }
}

static void lcl_RemoveBreaks(SwContentNode & rNode, SwTableFormat *const pTableFormat)
{
    // delete old layout frames, new ones need to be created...
    rNode.DelFrames(nullptr);

    if (!rNode.IsTextNode())
    {
        return;
    }

    SwTextNode & rTextNode = *rNode.GetTextNode();
    // remove PageBreaks/PageDesc/ColBreak
    SfxItemSet const* pSet = rTextNode.GetpSwAttrSet();
    if (!pSet)
        return;

    if (const SvxFormatBreakItem* pItem = pSet->GetItemIfSet(RES_BREAK, false))
    {
        if (pTableFormat)
        {
            pTableFormat->SetFormatAttr(*pItem);
        }
        rTextNode.ResetAttr(RES_BREAK);
        pSet = rTextNode.GetpSwAttrSet();
    }

    const SwFormatPageDesc* pPageDescItem;
    if (pSet
        && (pPageDescItem = pSet->GetItemIfSet(RES_PAGEDESC, false))
        && pPageDescItem->GetPageDesc())
    {
        if (pTableFormat)
        {
            pTableFormat->SetFormatAttr(*pPageDescItem);
        }
        rTextNode.ResetAttr(RES_PAGEDESC);
    }
}

/**
 * balance lines in table, insert empty boxes so all lines have the size
 */
static void
lcl_BalanceTable(SwTable & rTable, size_t const nMaxBoxes,
    SwTableNode & rTableNd, SwTableBoxFormat & rBoxFormat, SwTextFormatColl & rTextColl,
    SwUndoTextToTable *const pUndo, std::vector<sal_uInt16> *const pPositions)
{
    for (size_t n = 0; n < rTable.GetTabLines().size(); ++n)
    {
        SwTableLine *const pCurrLine = rTable.GetTabLines()[ n ];
        size_t const nBoxes = pCurrLine->GetTabBoxes().size();
        if (nMaxBoxes != nBoxes)
        {
            rTableNd.GetNodes().InsBoxen(&rTableNd, pCurrLine, &rBoxFormat, &rTextColl,
                    nullptr, nBoxes, nMaxBoxes - nBoxes);

            if (pUndo)
            {
                for (size_t i = nBoxes; i < nMaxBoxes; ++i)
                {
                    pUndo->AddFillBox( *pCurrLine->GetTabBoxes()[i] );
                }
            }

            // if the first line is missing boxes, the width array is useless!
            if (!n && pPositions)
            {
                pPositions->clear();
            }
        }
    }
}

static void
lcl_SetTableBoxWidths(SwTable & rTable, size_t const nMaxBoxes,
        SwTableBoxFormat & rBoxFormat, SwDoc & rDoc,
        std::vector<sal_uInt16> *const pPositions)
{
    if (pPositions && !pPositions->empty())
    {
        SwTableLines& rLns = rTable.GetTabLines();
        sal_uInt16 nLastPos = 0;
        for (size_t n = 0; n < pPositions->size(); ++n)
        {
            SwTableBoxFormat *pNewFormat = rDoc.MakeTableBoxFormat();
            pNewFormat->SetFormatAttr(
                    SwFormatFrameSize(SwFrameSize::Variable, (*pPositions)[n] - nLastPos));
            for (size_t nTmpLine = 0; nTmpLine < rLns.size(); ++nTmpLine)
            {
                // Have to do an Add here, because the BoxFormat
                // is still needed by the caller
                pNewFormat->Add(*rLns[nTmpLine]->GetTabBoxes()[n]);
            }

            nLastPos = (*pPositions)[ n ];
        }

        // propagate size upwards from format, so the table gets the right size
        SAL_WARN_IF(rBoxFormat.HasWriterListeners(), "sw.core",
                "who is still registered in the format?");
        rBoxFormat.SetFormatAttr( SwFormatFrameSize( SwFrameSize::Variable, nLastPos ));
    }
    else
    {
        size_t nWidth = nMaxBoxes ? USHRT_MAX / nMaxBoxes : USHRT_MAX;
        rBoxFormat.SetFormatAttr(SwFormatFrameSize(SwFrameSize::Variable, nWidth));
    }
}

SwTableNode* SwNodes::TextToTable( const SwNodeRange& rRange, sal_Unicode cCh,
                                    SwTableFormat* pTableFormat,
                                    SwTableLineFormat* pLineFormat,
                                    SwTableBoxFormat* pBoxFormat,
                                    SwTextFormatColl* pTextColl,
                                    SwUndoTextToTable* pUndo )
{
    if( rRange.aStart >= rRange.aEnd )
        return nullptr;

    SwTableNode * pTableNd = new SwTableNode( rRange.aStart.GetNode() );
    new SwEndNode( rRange.aEnd.GetNode(), *pTableNd );

    SwDoc& rDoc = GetDoc();
    std::vector<sal_uInt16> aPosArr;
    SwTable& rTable = pTableNd->GetTable();
    SwTableBox* pBox;
    sal_uInt16 nBoxes, nLines, nMaxBoxes = 0;

    SwNodeIndex aSttIdx( *pTableNd, 1 );
    SwNodeIndex aEndIdx( rRange.aEnd, -1 );
    for( nLines = 0, nBoxes = 0;
        aSttIdx.GetIndex() < aEndIdx.GetIndex();
        aSttIdx += SwNodeOffset(2), nLines++, nBoxes = 0 )
    {
        SwTextNode* pTextNd = aSttIdx.GetNode().GetTextNode();
        OSL_ENSURE( pTextNd, "Only add TextNodes to the Table" );

        if( !nLines && 0x0b == cCh )
        {
            cCh = 0x09;

            // Get the separator's position from the first Node, in order for the Boxes to be set accordingly
            SwTextFrameInfo aFInfo( static_cast<SwTextFrame*>(pTextNd->getLayoutFrame( pTextNd->GetDoc().getIDocumentLayoutAccess().GetCurrentLayout() )) );
            if( aFInfo.IsOneLine() ) // only makes sense in this case
            {
                OUString const& rText(pTextNd->GetText());
                for (sal_Int32 nChPos = 0; nChPos < rText.getLength(); ++nChPos)
                {
                    if (rText[nChPos] == cCh)
                    {
                        // sw_redlinehide: no idea if this makes any sense...
                        TextFrameIndex const nPos(aFInfo.GetFrame()->MapModelToView(pTextNd, nChPos));
                        aPosArr.push_back( o3tl::narrowing<sal_uInt16>(
                            aFInfo.GetCharPos(nPos+TextFrameIndex(1), false)) );
                    }
                }

                aPosArr.push_back(
                                o3tl::narrowing<sal_uInt16>(aFInfo.GetFrame()->IsVertical() ?
                                aFInfo.GetFrame()->getFramePrintArea().Bottom() :
                                aFInfo.GetFrame()->getFramePrintArea().Right()) );

            }
        }

        lcl_RemoveBreaks(*pTextNd, (0 == nLines) ? pTableFormat : nullptr);

        // Set the TableNode as StartNode for all TextNodes in the Table
        pTextNd->m_pStartOfSection = pTableNd;

        SwTableLine* pLine = new SwTableLine( pLineFormat, 1, nullptr );
        rTable.GetTabLines().insert(rTable.GetTabLines().begin() + nLines, pLine);

        SwStartNode* pSttNd;
        SwPosition aCntPos( aSttIdx, pTextNd, 0);

        const std::shared_ptr< sw::mark::ContentIdxStore> pContentStore(sw::mark::ContentIdxStore::Create());
        pContentStore->Save(rDoc, aSttIdx.GetIndex(), SAL_MAX_INT32);

        if( T2T_PARA != cCh )
        {
            for (sal_Int32 nChPos = 0; nChPos < pTextNd->GetText().getLength();)
            {
                if (pTextNd->GetText()[nChPos] == cCh)
                {
                    aCntPos.SetContent(nChPos);
                    std::function<void (SwTextNode *, sw::mark::RestoreMode, bool)> restoreFunc(
                        [&](SwTextNode *const pNewNode, sw::mark::RestoreMode const eMode, bool)
                        {
                            if (!pContentStore->Empty())
                            {
                                pContentStore->Restore(*pNewNode, nChPos, nChPos + 1, eMode);
                            }
                        });
                    SwContentNode *const pNewNd =
                        pTextNd->SplitContentNode(aCntPos, &restoreFunc);

                    // Delete separator and correct search string
                    pTextNd->EraseText( aCntPos, 1 );
                    nChPos = 0;

                    // Set the TableNode as StartNode for all TextNodes in the Table
                    const SwNodeIndex aTmpIdx( aCntPos.GetNode(), -1 );
                    pSttNd = new SwStartNode( aTmpIdx.GetNode(), SwNodeType::Start,
                                                SwTableBoxStartNode );
                    new SwEndNode( aCntPos.GetNode(), *pSttNd );
                    pNewNd->m_pStartOfSection = pSttNd;

                    // Assign Section to the Box
                    pBox = new SwTableBox( pBoxFormat, *pSttNd, pLine );
                    pLine->GetTabBoxes().insert( pLine->GetTabBoxes().begin() + nBoxes++, pBox );
                }
                else
                {
                    ++nChPos;
                }
            }
        }

        // Now for the last substring
        if( !pContentStore->Empty())
            pContentStore->Restore( *pTextNd, pTextNd->GetText().getLength(), pTextNd->GetText().getLength()+1 );

        pSttNd = new SwStartNode( aCntPos.GetNode(), SwNodeType::Start, SwTableBoxStartNode );
        const SwNodeIndex aTmpIdx( aCntPos.GetNode(), 1 );
        new SwEndNode( aTmpIdx.GetNode(), *pSttNd  );
        pTextNd->m_pStartOfSection = pSttNd;

        pBox = new SwTableBox( pBoxFormat, *pSttNd, pLine );
        pLine->GetTabBoxes().insert( pLine->GetTabBoxes().begin() + nBoxes++, pBox );
        if( nMaxBoxes < nBoxes )
            nMaxBoxes = nBoxes;
    }

    lcl_BalanceTable(rTable, nMaxBoxes, *pTableNd, *pBoxFormat, *pTextColl,
            pUndo, &aPosArr);
    lcl_SetTableBoxWidths(rTable, nMaxBoxes, *pBoxFormat, rDoc, &aPosArr);

    return pTableNd;
}

const SwTable* SwDoc::TextToTable( const std::vector< std::vector<SwNodeRange> >& rTableNodes )
{
    if (rTableNodes.empty())
        return nullptr;

    const std::vector<SwNodeRange>& rFirstRange = *rTableNodes.begin();

    if (rFirstRange.empty())
        return nullptr;

    const std::vector<SwNodeRange>& rLastRange = *rTableNodes.rbegin();

    if (rLastRange.empty())
        return nullptr;

    /* Save first node in the selection if it is a content node. */
    SwContentNode * pSttContentNd = rFirstRange.begin()->aStart.GetNode().GetContentNode();

    const SwNodeRange& rStartRange = *rFirstRange.begin();
    const SwNodeRange& rEndRange = *rLastRange.rbegin();

    //!!! not necessarily TextNodes !!!
    SwPaM aOriginal( rStartRange.aStart, rEndRange.aEnd );
    const SwPosition *pStart = aOriginal.GetMark();
    SwPosition *pEnd = aOriginal.GetPoint();

    bool const bUndo(GetIDocumentUndoRedo().DoesUndo());
    if (bUndo)
    {
        // Do not add splitting the TextNode to the Undo history
        GetIDocumentUndoRedo().DoUndo(false);
    }

    ::PaMCorrAbs( aOriginal, *pEnd );

    // make sure that the range is on Node Edges
    SwNodeRange aRg( pStart->GetNode(), pEnd->GetNode() );
    if( pStart->GetContentIndex() )
        getIDocumentContentOperations().SplitNode( *pStart, false );

    bool bEndContent = 0 != pEnd->GetContentIndex();

    // Do not split at the End of a Line (except at the End of the Doc)
    if( bEndContent )
    {
        if( pEnd->GetNode().GetContentNode()->Len() != pEnd->GetContentIndex()
            || pEnd->GetNodeIndex() >= GetNodes().GetEndOfContent().GetIndex()-1 )
        {
            getIDocumentContentOperations().SplitNode( *pEnd, false );
            pEnd->Adjust(SwNodeOffset(-1));
            // A Node and at the End?
            if( pStart->GetNodeIndex() >= pEnd->GetNodeIndex() )
                --aRg.aStart;
        }
        else
            ++aRg.aEnd;
    }

    assert(aRg.aEnd.GetNode() == pEnd->GetNode());
    assert(aRg.aStart.GetNode() == pStart->GetNode());
    if( aRg.aEnd.GetIndex() == aRg.aStart.GetIndex() )
    {
        OSL_FAIL( "empty range" );
        ++aRg.aEnd;
    }


    {
        // TODO: this is not Undo-able - only good enough for file import
        IDocumentRedlineAccess & rIDRA(getIDocumentRedlineAccess());
        SwNodeIndex const prev(rTableNodes.begin()->begin()->aStart, -1);
        SwNodeIndex const* pPrev(&prev);
        // pPrev could point to non-textnode now
        for (const auto& rRow : rTableNodes)
        {
            for (const auto& rCell : rRow)
            {
                assert(SwNodeIndex(*pPrev, +1) == rCell.aStart);
                SwPaM pam(rCell.aStart, 0, *pPrev,
                        (pPrev->GetNode().IsContentNode())
                            ? pPrev->GetNode().GetContentNode()->Len() : 0);
                rIDRA.SplitRedline(pam);
                pPrev = &rCell.aEnd;
            }
        }
        // another one to break between last cell and node after table
        SwPaM pam(pPrev->GetNode(), SwNodeOffset(+1), 0,
                  pPrev->GetNode(), SwNodeOffset(0),
                    (pPrev->GetNode().IsContentNode())
                        ? pPrev->GetNode().GetContentNode()->Len() : 0);
        rIDRA.SplitRedline(pam);

        // Paragraph formatting results in overlapping elements, split of redlines then can result
        // in an unsorted redline table, fix it up.
        SwRedlineTable& rRedlineTable = rIDRA.GetRedlineTable();
        if (rRedlineTable.HasOverlappingElements())
        {
            rIDRA.GetRedlineTable().Resort();
        }
    }

    // We always use Upper to insert the Table
    SwNode2LayoutSaveUpperFrames aNode2Layout( aRg.aStart.GetNode() );

    GetIDocumentUndoRedo().DoUndo(bUndo);

    // Create the Box/Line/Table construct
    SwTableBoxFormat* pBoxFormat = MakeTableBoxFormat();
    SwTableLineFormat* pLineFormat = MakeTableLineFormat();
    SwTableFormat* pTableFormat = MakeTableFrameFormat( GetUniqueTableName(), GetDfltFrameFormat() );

    // All Lines have a left-to-right Fill Order
    pLineFormat->SetFormatAttr( SwFormatFillOrder( ATT_LEFT_TO_RIGHT ));
    // The Table's SSize is USHRT_MAX
    pTableFormat->SetFormatAttr( SwFormatFrameSize( SwFrameSize::Variable, USHRT_MAX ));

    /* If the first node in the selection is a context node and if it
       has an item FRAMEDIR set (no default) propagate the item to the
       replacing table. */
    if (pSttContentNd)
    {
        const SwAttrSet & aNdSet = pSttContentNd->GetSwAttrSet();
        if (const SvxFrameDirectionItem* pItem = aNdSet.GetItemIfSet( RES_FRAMEDIR ))
        {
            pTableFormat->SetFormatAttr( *pItem );
        }
    }

    //Resolves: tdf#87977, tdf#78599, disable broadcasting modifications
    //until after RegisterToFormat is completed
    bool bEnableSetModified = getIDocumentState().IsEnableSetModified();
    getIDocumentState().SetEnableSetModified(false);

    SwTableNode* pTableNd = GetNodes().TextToTable(
            rTableNodes, pTableFormat, pLineFormat, pBoxFormat );

    SwTable& rNdTable = pTableNd->GetTable();
    rNdTable.RegisterToFormat(*pTableFormat);

    if( !pBoxFormat->HasWriterListeners() )
    {
        // The Box's Formats already have the right size, we must only set
        // the right Border/AutoFormat.
        pTableFormat->SetFormatAttr( pBoxFormat->GetFrameSize() );
        delete pBoxFormat;
    }

    SwNodeOffset nIdx = pTableNd->GetIndex();
    aNode2Layout.RestoreUpperFrames( GetNodes(), nIdx, nIdx + 1 );

    getIDocumentState().SetEnableSetModified(bEnableSetModified);
    getIDocumentState().SetModified();
    getIDocumentFieldsAccess().SetFieldsDirty( true, nullptr, SwNodeOffset(0) );
    return &rNdTable;
}

void SwNodes::ExpandRangeForTableBox(const SwNodeRange & rRange, std::optional<SwNodeRange>& rExpandedRange)
{
    bool bChanged = false;

    SwNodeIndex aNewStart = rRange.aStart;
    SwNodeIndex aNewEnd = rRange.aEnd;

    SwNodeIndex aEndIndex = rRange.aEnd;
    SwNodeIndex aIndex = rRange.aStart;

    while (aIndex < aEndIndex)
    {
        SwNode& rNode = aIndex.GetNode();

        if (rNode.IsStartNode())
        {
            // advance aIndex to the end node of this start node
            SwNode * pEndNode = rNode.EndOfSectionNode();
            aIndex = *pEndNode;

            if (aIndex > aNewEnd)
            {
                aNewEnd = aIndex;
                bChanged = true;
            }
        }
        else if (rNode.IsEndNode())
        {
            SwNode * pStartNode = rNode.StartOfSectionNode();
            if (pStartNode->GetIndex() < aNewStart.GetIndex())
            {
                aNewStart = *pStartNode;
                bChanged = true;
            }
        }

        if (aIndex < aEndIndex)
            ++aIndex;
    }

    SwNode * pNode = &aIndex.GetNode();
    while (pNode->IsEndNode() && aIndex < Count() - 1)
    {
        SwNode * pStartNode = pNode->StartOfSectionNode();
        aNewStart = *pStartNode;
        aNewEnd = aIndex;
        bChanged = true;

        ++aIndex;
        pNode = &aIndex.GetNode();
    }

    if (bChanged)
        rExpandedRange.emplace(aNewStart, aNewEnd);
}

static void
lcl_SetTableBoxWidths2(SwTable & rTable, size_t const nMaxBoxes,
        SwTableBoxFormat & rBoxFormat, SwDoc & rDoc)
{
    // rhbz#820283, fdo#55462: set default box widths so table width is covered
    SwTableLines & rLines = rTable.GetTabLines();
    assert(nMaxBoxes != 0); // no valid table without boxes
    auto const nTableWidth{USHRT_MAX}; // default
    auto const nWidth{nTableWidth / nMaxBoxes};
    auto const nRest{nTableWidth % nMaxBoxes};
    for (size_t nTmpLine = 0; nTmpLine < rLines.size(); ++nTmpLine)
    {
        SwTableBoxes & rBoxes = rLines[nTmpLine]->GetTabBoxes();
        assert(!rBoxes.empty()); // ensured by convertToTable
        size_t const nMissing = nMaxBoxes - rBoxes.size();
        if (nMissing || nRest != 0)
        {
            // default width for box at the end of an incomplete line
            SwTableBoxFormat *const pNewFormat = rDoc.MakeTableBoxFormat();
            pNewFormat->SetFormatAttr( SwFormatFrameSize(SwFrameSize::Variable,
                        nWidth * (nMissing + 1) + nRest) );
            pNewFormat->Add(*rBoxes.back());
        }
    }
    // default width for all boxes not at the end of an incomplete line
    rBoxFormat.SetFormatAttr(SwFormatFrameSize(SwFrameSize::Variable, nWidth));
}

SwTableNode* SwNodes::TextToTable( const SwNodes::TableRanges_t & rTableNodes,
                                    SwTableFormat* pTableFormat,
                                    SwTableLineFormat* pLineFormat,
                                    SwTableBoxFormat* pBoxFormat  )
{
    if( rTableNodes.empty() )
        return nullptr;

    SwTableNode * pTableNd = new SwTableNode( rTableNodes.begin()->begin()->aStart.GetNode() );
    //insert the end node after the last text node
    SwNodeIndex aInsertIndex( rTableNodes.rbegin()->rbegin()->aEnd );
    ++aInsertIndex;

    //!! ownership will be transferred in c-tor to SwNodes array.
    //!! Thus no real problem here...
    new SwEndNode( aInsertIndex.GetNode(), *pTableNd );

    SwDoc& rDoc = GetDoc();
    SwTable& rTable = pTableNd->GetTable();
    SwTableBox* pBox;
    sal_uInt16 nLines, nMaxBoxes = 0;

    SwNodeIndex aNodeIndex = rTableNodes.begin()->begin()->aStart;
    // delete frames of all contained content nodes
    for( nLines = 0; aNodeIndex <= rTableNodes.rbegin()->rbegin()->aEnd; ++aNodeIndex,++nLines )
    {
        SwNode* pNode(&aNodeIndex.GetNode());
        while (pNode->IsSectionNode()) // could be ToX field in table
        {
            pNode = pNode->GetNodes()[pNode->GetIndex()+1];
        }
        if (pNode->IsTableNode())
        {
            lcl_RemoveBreaksTable(static_cast<SwTableNode&>(*pNode),
                    (0 == nLines) ? pTableFormat : nullptr);
        }
        else if (pNode->IsContentNode())
        {
            lcl_RemoveBreaks(static_cast<SwContentNode&>(*pNode),
                    (0 == nLines) ? pTableFormat : nullptr);
        }
    }

    nLines = 0;
    for( const auto& rRow : rTableNodes )
    {
        sal_uInt16 nBoxes = 0;
        SwTableLine* pLine = new SwTableLine( pLineFormat, 1, nullptr );
        rTable.GetTabLines().insert(rTable.GetTabLines().begin() + nLines, pLine);

        for( const auto& rCell : rRow )
        {
            SwNodeIndex aCellEndIdx(rCell.aEnd);
            ++aCellEndIdx;
            SwStartNode* pSttNd = new SwStartNode( rCell.aStart.GetNode(), SwNodeType::Start,
                                        SwTableBoxStartNode );

            // Quotation of http://nabble.documentfoundation.org/Some-strange-lines-by-taking-a-look-at-the-bt-of-fdo-51916-tp3994561p3994639.html
            // SwNode's constructor adds itself to the same SwNodes array as the other node (pSttNd).
            // So this statement is only executed for the side-effect.
            new SwEndNode( aCellEndIdx.GetNode(), *pSttNd );

            //set the start node on all node of the current cell
            SwNodeIndex aCellNodeIdx = rCell.aStart;
            for(;aCellNodeIdx <= rCell.aEnd; ++aCellNodeIdx )
            {
                aCellNodeIdx.GetNode().m_pStartOfSection = pSttNd;
                //skip start/end node pairs
                if( aCellNodeIdx.GetNode().IsStartNode() )
                    aCellNodeIdx.Assign(*aCellNodeIdx.GetNode().EndOfSectionNode());
            }

            // assign Section to the Box
            pBox = new SwTableBox( pBoxFormat, *pSttNd, pLine );
            pLine->GetTabBoxes().insert( pLine->GetTabBoxes().begin() + nBoxes++, pBox );
        }
        if( nMaxBoxes < nBoxes )
            nMaxBoxes = nBoxes;

        nLines++;
    }

    lcl_SetTableBoxWidths2(rTable, nMaxBoxes, *pBoxFormat, rDoc);

    return pTableNd;
}

/**
 * Table to Text
 */
bool SwDoc::TableToText( const SwTableNode* pTableNd, sal_Unicode cCh )
{
    if( !pTableNd )
        return false;

    // #i34471#
    // If this is triggered by SwUndoTableToText::Repeat() nobody ever deleted
    // the table cursor.
    SwEditShell* pESh = GetEditShell();
    if (pESh && pESh->IsTableMode())
        pESh->ClearMark();

    SwNodeRange aRg( *pTableNd, SwNodeOffset(0), *pTableNd->EndOfSectionNode() );
    std::unique_ptr<SwUndoTableToText> pUndo;
    SwNodeRange* pUndoRg = nullptr;
    if (GetIDocumentUndoRedo().DoesUndo())
    {
        GetIDocumentUndoRedo().ClearRedo();
        pUndoRg = new SwNodeRange( aRg.aStart, SwNodeOffset(-1), aRg.aEnd, SwNodeOffset(+1) );
        pUndo.reset(new SwUndoTableToText( pTableNd->GetTable(), cCh ));
    }

    const_cast<SwTable*>(&pTableNd->GetTable())->SwitchFormulasToExternalRepresentation();

    bool bRet = GetNodes().TableToText( aRg, cCh, pUndo.get() );
    if( pUndoRg )
    {
        ++pUndoRg->aStart;
        --pUndoRg->aEnd;
        pUndo->SetRange( *pUndoRg );
        GetIDocumentUndoRedo().AppendUndo(std::move(pUndo));
        delete pUndoRg;
    }

    if( bRet )
        getIDocumentState().SetModified();

    return bRet;
}

namespace {

/**
 * Use the ForEach method from PtrArray to recreate Text from a Table.
 * The Boxes can also contain Lines!
 */
struct DelTabPara
{
    SwTextNode* pLastNd;
    SwNodes& rNds;
    SwUndoTableToText* pUndo;
    sal_Unicode cCh;

    DelTabPara( SwNodes& rNodes, sal_Unicode cChar, SwUndoTableToText* pU ) :
        pLastNd(nullptr), rNds( rNodes ), pUndo( pU ), cCh( cChar ) {}
};

}

// Forward declare so that the Lines and Boxes can use recursion
static void lcl_DelBox( SwTableBox* pBox, DelTabPara* pDelPara );

static void lcl_DelLine( SwTableLine* pLine, DelTabPara* pPara )
{
    assert(pPara && "The parameters are missing!");
    DelTabPara aPara( *pPara );
    for( auto& rpBox : pLine->GetTabBoxes() )
        lcl_DelBox(rpBox, &aPara );
    if( pLine->GetUpper() ) // Is there a parent Box?
        // Return the last TextNode
        pPara->pLastNd = aPara.pLastNd;
}

static void lcl_DelBox( SwTableBox* pBox, DelTabPara* pDelPara )
{
    assert(pDelPara && "The parameters are missing");

    // Delete the Box's Lines
    if( !pBox->GetTabLines().empty() )
    {
        for( SwTableLine* pLine : pBox->GetTabLines() )
            lcl_DelLine( pLine, pDelPara );
    }
    else
    {
        SwDoc& rDoc = pDelPara->rNds.GetDoc();
        SwNodeRange aDelRg( *pBox->GetSttNd(), SwNodeOffset(0),
                            *pBox->GetSttNd()->EndOfSectionNode() );
        // Delete the Section
        pDelPara->rNds.SectionUp( &aDelRg );
        const SwTextNode* pCurTextNd = nullptr;
        if (T2T_PARA != pDelPara->cCh && pDelPara->pLastNd)
            pCurTextNd = aDelRg.aStart.GetNode().GetTextNode();
        if (nullptr != pCurTextNd)
        {
            // Join the current text node with the last from the previous box if possible
            SwNodeOffset nNdIdx = aDelRg.aStart.GetIndex();
            --aDelRg.aStart;
            if( pDelPara->pLastNd == &aDelRg.aStart.GetNode() )
            {
                // Inserting the separator
                SwContentIndex aCntIdx( pDelPara->pLastNd,
                                 pDelPara->pLastNd->GetText().getLength());
                pDelPara->pLastNd->InsertText( OUString(pDelPara->cCh), aCntIdx,
                    SwInsertFlags::EMPTYEXPAND );
                if( pDelPara->pUndo )
                    pDelPara->pUndo->AddBoxPos( rDoc, nNdIdx, aDelRg.aEnd.GetIndex(),
                                                aCntIdx.GetIndex() );

                const std::shared_ptr<sw::mark::ContentIdxStore> pContentStore(sw::mark::ContentIdxStore::Create());
                const sal_Int32 nOldTextLen = aCntIdx.GetIndex();
                pContentStore->Save(rDoc, nNdIdx, SAL_MAX_INT32);

                pDelPara->pLastNd->JoinNext();

                if( !pContentStore->Empty() )
                    pContentStore->Restore( rDoc, pDelPara->pLastNd->GetIndex(), nOldTextLen );
            }
            else if( pDelPara->pUndo )
            {
                ++aDelRg.aStart;
                pDelPara->pUndo->AddBoxPos( rDoc, nNdIdx, aDelRg.aEnd.GetIndex() );
            }
        }
        else if( pDelPara->pUndo )
            pDelPara->pUndo->AddBoxPos( rDoc, aDelRg.aStart.GetIndex(), aDelRg.aEnd.GetIndex() );
        --aDelRg.aEnd;
        pDelPara->pLastNd = aDelRg.aEnd.GetNode().GetTextNode();

        // Do not take over the NumberFormatting's adjustment
        if( pDelPara->pLastNd && pDelPara->pLastNd->HasSwAttrSet() )
            pDelPara->pLastNd->ResetAttr( RES_PARATR_ADJUST );
    }
}

bool SwNodes::TableToText( const SwNodeRange& rRange, sal_Unicode cCh,
                            SwUndoTableToText* pUndo )
{
    // Is a Table selected?
    if (rRange.aStart.GetIndex() >= rRange.aEnd.GetIndex())
        return false;
    SwTableNode *const pTableNd(rRange.aStart.GetNode().GetTableNode());
    if (nullptr == pTableNd ||
        &rRange.aEnd.GetNode() != pTableNd->EndOfSectionNode() )
        return false;

    // If the Table was alone in a Section, create the Frames via the Table's Upper
    std::optional<SwNode2LayoutSaveUpperFrames> oNode2Layout;
    SwNode* pFrameNd = FindPrvNxtFrameNode( rRange.aStart.GetNode(), &rRange.aEnd.GetNode() );
    SwNodeIndex aFrameIdx( pFrameNd ? *pFrameNd: rRange.aStart.GetNode() );
    if( !pFrameNd )
        // Collect all Uppers
        oNode2Layout.emplace(*pTableNd);

    // Delete the Frames
    pTableNd->DelFrames();

    // "Delete" the Table and merge all Lines/Boxes
    DelTabPara aDelPara( *this, cCh, pUndo );
    for( SwTableLine *pLine : pTableNd->m_pTable->GetTabLines() )
        lcl_DelLine( pLine, &aDelPara );

    // We just created a TextNode with fitting separator for every TableLine.
    // Now we only need to delete the TableSection and create the Frames for the
    // new TextNode.
    SwNodeRange aDelRg( rRange.aStart, rRange.aEnd );

    // If the Table has PageDesc/Break Attributes, carry them over to the
    // first Text Node
    {
        // What about UNDO?
        const SfxItemSet& rTableSet = pTableNd->m_pTable->GetFrameFormat()->GetAttrSet();
        const SvxFormatBreakItem* pBreak = rTableSet.GetItemIfSet( RES_BREAK, false );
        const SwFormatPageDesc* pDesc = rTableSet.GetItemIfSet( RES_PAGEDESC, false );

        if( pBreak || pDesc )
        {
            SwNodeIndex aIdx( *pTableNd  );
            SwContentNode* pCNd = GoNext( &aIdx );
            if( pBreak )
                pCNd->SetAttr( *pBreak );
            if( pDesc )
                pCNd->SetAttr( *pDesc );
        }
    }

    SectionUp( &aDelRg ); // Delete this Section and by that the Table
    // #i28006#
    SwNodeOffset nStt = aDelRg.aStart.GetIndex(), nEnd = aDelRg.aEnd.GetIndex();
    if( !pFrameNd )
    {
        oNode2Layout->RestoreUpperFrames( *this,
                        aDelRg.aStart.GetIndex(), aDelRg.aEnd.GetIndex() );
        oNode2Layout.reset();
    }
    else
    {
        SwContentNode *pCNd;
        SwSectionNode *pSNd;
        while( aDelRg.aStart.GetIndex() < nEnd )
        {
            pCNd = aDelRg.aStart.GetNode().GetContentNode();
            if( nullptr != pCNd )
            {
                if( pFrameNd->IsContentNode() )
                    static_cast<SwContentNode*>(pFrameNd)->MakeFramesForAdjacentContentNode(*pCNd);
                else if( pFrameNd->IsTableNode() )
                    static_cast<SwTableNode*>(pFrameNd)->MakeFramesForAdjacentContentNode(aDelRg.aStart);
                else if( pFrameNd->IsSectionNode() )
                    static_cast<SwSectionNode*>(pFrameNd)->MakeFramesForAdjacentContentNode(aDelRg.aStart);
                pFrameNd = pCNd;
            }
            else
            {
                pSNd = aDelRg.aStart.GetNode().GetSectionNode();
                if( pSNd )
                {
                    if( !pSNd->GetSection().IsHidden() && !pSNd->IsContentHidden() )
                    {
                        pSNd->MakeOwnFrames(&aFrameIdx, &aDelRg.aEnd);
                        break;
                    }
                    aDelRg.aStart = *pSNd->EndOfSectionNode();
                }
            }
            ++aDelRg.aStart;
        }
    }

    // #i28006# Fly frames have to be restored even if the table was
    // #alone in the section
    for(sw::SpzFrameFormat* pFly: *GetDoc().GetSpzFrameFormats())
    {
        SwFrameFormat *const pFormat = pFly;
        const SwFormatAnchor& rAnchor = pFormat->GetAnchor();
        SwNode const*const pAnchorNode = rAnchor.GetAnchorNode();
        if (pAnchorNode &&
            ((RndStdIds::FLY_AT_PARA == rAnchor.GetAnchorId()) ||
             (RndStdIds::FLY_AT_CHAR == rAnchor.GetAnchorId())) &&
            nStt <= pAnchorNode->GetIndex() &&
            pAnchorNode->GetIndex() < nEnd )
        {
            pFormat->MakeFrames();
        }
    }

    return true;
}

/**
 * Inserting Columns/Rows
 */
void SwDoc::InsertCol( const SwCursor& rCursor, sal_uInt16 nCnt, bool bBehind )
{
    if( !::CheckSplitCells( rCursor, nCnt + 1, SwTableSearchType::Col ) )
        return;

    // Find the Boxes via the Layout
    SwSelBoxes aBoxes;
    ::GetTableSel( rCursor, aBoxes, SwTableSearchType::Col );

    if( !aBoxes.empty() )
        InsertCol( aBoxes, nCnt, bBehind );
}

bool SwDoc::InsertCol( const SwSelBoxes& rBoxes, sal_uInt16 nCnt, bool bBehind, bool bInsertDummy )
{
    OSL_ENSURE( !rBoxes.empty(), "No valid Box list" );
    SwTableNode* pTableNd = const_cast<SwTableNode*>(rBoxes[0]->GetSttNd()->FindTableNode());
    if( !pTableNd )
        return false;

    SwTable& rTable = pTableNd->GetTable();
    if( dynamic_cast<const SwDDETable*>( &rTable) !=  nullptr)
        return false;

    SwTableSortBoxes aTmpLst;
    std::unique_ptr<SwUndoTableNdsChg> pUndo;
    if (GetIDocumentUndoRedo().DoesUndo())
    {
        pUndo.reset(new SwUndoTableNdsChg( SwUndoId::TABLE_INSCOL, rBoxes, *pTableNd,
                                     0, 0, nCnt, bBehind, false ));
        aTmpLst.insert( rTable.GetTabSortBoxes() );
    }

    bool bRet(false);
    {
        ::sw::UndoGuard const undoGuard(GetIDocumentUndoRedo());

        rTable.SwitchFormulasToInternalRepresentation();
        bRet = rTable.InsertCol(*this, rBoxes, nCnt, bBehind, bInsertDummy);
        if (bRet)
        {
            getIDocumentState().SetModified();
            ::ClearFEShellTabCols(*this, nullptr);
            getIDocumentFieldsAccess().SetFieldsDirty( true, nullptr, SwNodeOffset(0) );
        }
    }

    if( pUndo && bRet )
    {
        pUndo->SaveNewBoxes( *pTableNd, aTmpLst );
        GetIDocumentUndoRedo().AppendUndo( std::move(pUndo) );
    }
    return bRet;
}

void SwDoc::InsertRow( const SwCursor& rCursor, sal_uInt16 nCnt, bool bBehind )
{
    // Find the Boxes via the Layout
    SwSelBoxes aBoxes;
    GetTableSel( rCursor, aBoxes, SwTableSearchType::Row );

    if( !aBoxes.empty() )
        InsertRow( aBoxes, nCnt, bBehind );
}

bool SwDoc::InsertRow( const SwSelBoxes& rBoxes, sal_uInt16 nCnt, bool bBehind, bool bInsertDummy )
{
    OSL_ENSURE( !rBoxes.empty(), "No valid Box list" );
    SwTableNode* pTableNd = const_cast<SwTableNode*>(rBoxes[0]->GetSttNd()->FindTableNode());
    if( !pTableNd )
        return false;

    SwTable& rTable = pTableNd->GetTable();
    if( dynamic_cast<const SwDDETable*>( &rTable) !=  nullptr)
        return false;

    SwTableSortBoxes aTmpLst;
    std::unique_ptr<SwUndoTableNdsChg> pUndo;
    if (GetIDocumentUndoRedo().DoesUndo())
    {
        pUndo.reset(new SwUndoTableNdsChg( SwUndoId::TABLE_INSROW,rBoxes, *pTableNd,
                                     0, 0, nCnt, bBehind, false ));
        aTmpLst.insert( rTable.GetTabSortBoxes() );
    }

    bool bRet(false);
    {
        ::sw::UndoGuard const undoGuard(GetIDocumentUndoRedo());
        rTable.SwitchFormulasToInternalRepresentation();

        bRet = rTable.InsertRow( *this, rBoxes, nCnt, bBehind, bInsertDummy );
        if (bRet)
        {
            getIDocumentState().SetModified();
            ::ClearFEShellTabCols(*this, nullptr);
            getIDocumentFieldsAccess().SetFieldsDirty( true, nullptr, SwNodeOffset(0) );
        }
    }

    if( pUndo && bRet )
    {
        pUndo->SaveNewBoxes( *pTableNd, aTmpLst );
        GetIDocumentUndoRedo().AppendUndo( std::move(pUndo) );
    }
    return bRet;

}

/**
 * Deleting Columns/Rows
 */
void SwDoc::DeleteRow( const SwCursor& rCursor )
{
    // Find the Boxes via the Layout
    SwSelBoxes aBoxes;
    GetTableSel( rCursor, aBoxes, SwTableSearchType::Row );
    if( ::HasProtectedCells( aBoxes ))
        return;

    // Remove the Cursor from the to-be-deleted Section.
    // The Cursor is placed after the table, except for
    //  - when there's another Line, we place it in that one
    //  - when a Line precedes it, we place it in that one
    {
        SwTableNode* pTableNd = rCursor.GetPointNode().FindTableNode();

        if(dynamic_cast<const SwDDETable*>( & pTableNd->GetTable()) !=  nullptr)
            return;

        // Find all Boxes/Lines
        FndBox_ aFndBox( nullptr, nullptr );
        {
            FndPara aPara( aBoxes, &aFndBox );
            ForEach_FndLineCopyCol( pTableNd->GetTable().GetTabLines(), &aPara );
        }

        if( aFndBox.GetLines().empty() )
            return;

        if (SwEditShell* pESh = GetEditShell())
        {
            pESh->KillPams();
            // FIXME: actually we should be iterating over all Shells!
        }

        FndBox_* pFndBox = &aFndBox;
        while( 1 == pFndBox->GetLines().size() &&
                1 == pFndBox->GetLines().front()->GetBoxes().size() )
        {
            FndBox_ *const pTmp = pFndBox->GetLines().front()->GetBoxes()[0].get();
            if( pTmp->GetBox()->GetSttNd() )
                break; // Else it gets too far
            pFndBox = pTmp;
        }

        SwTableLine* pDelLine = pFndBox->GetLines().back()->GetLine();
        SwTableBox* pDelBox = pDelLine->GetTabBoxes().back();
        while( !pDelBox->GetSttNd() )
        {
            SwTableLine* pLn = pDelBox->GetTabLines()[
                        pDelBox->GetTabLines().size()-1 ];
            pDelBox = pLn->GetTabBoxes().back();
        }
        SwTableBox* pNextBox = pDelLine->FindNextBox( pTableNd->GetTable(),
                                                        pDelBox );
        while( pNextBox &&
                pNextBox->GetFrameFormat()->GetProtect().IsContentProtected() )
            pNextBox = pNextBox->FindNextBox( pTableNd->GetTable(), pNextBox );

        if( !pNextBox ) // No succeeding Boxes? Then take the preceding one
        {
            pDelLine = pFndBox->GetLines().front()->GetLine();
            pDelBox = pDelLine->GetTabBoxes()[ 0 ];
            while( !pDelBox->GetSttNd() )
                pDelBox = pDelBox->GetTabLines()[0]->GetTabBoxes()[0];
            pNextBox = pDelLine->FindPreviousBox( pTableNd->GetTable(),
                                                        pDelBox );
            while( pNextBox &&
                    pNextBox->GetFrameFormat()->GetProtect().IsContentProtected() )
                pNextBox = pNextBox->FindPreviousBox( pTableNd->GetTable(), pNextBox );
        }

        SwNodeOffset nIdx;
        if( pNextBox ) // Place the Cursor here
            nIdx = pNextBox->GetSttIdx() + 1;
        else // Else after the Table
            nIdx = pTableNd->EndOfSectionIndex() + 1;

        SwNodeIndex aIdx( GetNodes(), nIdx );
        SwContentNode* pCNd = aIdx.GetNode().GetContentNode();
        if( !pCNd )
            pCNd = SwNodes::GoNext(&aIdx);

        if( pCNd )
        {
            // Change the Shell's Cursor or the one passed?
            SwPaM* pPam = const_cast<SwPaM*>(static_cast<SwPaM const *>(&rCursor));
            pPam->GetPoint()->Assign(aIdx);
            pPam->SetMark(); // Both want a part of it
            pPam->DeleteMark();
        }
    }

    // Thus delete the Rows
    GetIDocumentUndoRedo().StartUndo(SwUndoId::ROW_DELETE, nullptr);
    DeleteRowCol( aBoxes );
    GetIDocumentUndoRedo().EndUndo(SwUndoId::ROW_DELETE, nullptr);
}

void SwDoc::DeleteCol( const SwCursor& rCursor )
{
    // Find the Boxes via the Layout
    SwSelBoxes aBoxes;
    GetTableSel( rCursor, aBoxes, SwTableSearchType::Col );
    if( ::HasProtectedCells( aBoxes ))
        return;

    // The Cursors need to be removed from the to-be-deleted range.
    // Always place them after/on top of the Table; they are always set
    // to the old position via the document position.
    if (SwEditShell* pESh = GetEditShell())
    {
        const SwNode* pNd = rCursor.GetPointNode().FindTableBoxStartNode();
        pESh->ParkCursor( *pNd );
    }

    // Thus delete the Columns
    GetIDocumentUndoRedo().StartUndo(SwUndoId::COL_DELETE, nullptr);
    DeleteRowCol(aBoxes, SwDoc::RowColMode::DeleteColumn);
    GetIDocumentUndoRedo().EndUndo(SwUndoId::COL_DELETE, nullptr);
}

void SwDoc::DelTable(SwTableNode *const pTableNd)
{
    {
        // tdf#156267 remove DdeBookmarks before deleting nodes
        SwPaM aTmpPaM(*pTableNd, *pTableNd->EndOfSectionNode());
        SwDataChanged aTmp(aTmpPaM);
    }

    bool bNewTextNd = false;
    // Is it alone in a FlyFrame?
    SwNodeIndex aIdx( *pTableNd, -1 );
    const SwStartNode* pSttNd = aIdx.GetNode().GetStartNode();
    if (pSttNd)
    {
        const SwNodeOffset nTableEnd = pTableNd->EndOfSectionIndex() + 1;
        const SwNodeOffset nSectEnd = pSttNd->EndOfSectionIndex();
        if (nTableEnd == nSectEnd)
        {
            if (SwFlyStartNode == pSttNd->GetStartNodeType())
            {
                SwFrameFormat* pFormat = pSttNd->GetFlyFormat();
                if (pFormat)
                {
                    // That's the FlyFormat we're looking for
                    getIDocumentLayoutAccess().DelLayoutFormat( pFormat );
                    return;
                }
            }
            // No Fly? Thus Header or Footer: always leave a TextNode
            // We can forget about Undo then!
            bNewTextNd = true;
        }
    }

    // No Fly? Then it is a Header or Footer, so keep always a TextNode
    ++aIdx;
    if (GetIDocumentUndoRedo().DoesUndo())
    {
        GetIDocumentUndoRedo().ClearRedo();
        SwPaM aPaM( *pTableNd->EndOfSectionNode(), aIdx.GetNode() );

        if (bNewTextNd)
        {
            const SwNodeIndex aTmpIdx( *pTableNd->EndOfSectionNode(), 1 );
            GetNodes().MakeTextNode( aTmpIdx.GetNode(),
                        getIDocumentStylePoolAccess().GetTextCollFromPool( RES_POOLCOLL_STANDARD ) );
        }

        // Save the cursors (UNO and otherwise)
        SwPaM const* pSavePaM(nullptr);
        SwPaM forwardPaM(*pTableNd->EndOfSectionNode());
        if (forwardPaM.Move(fnMoveForward, GoInNode))
        {
            pSavePaM = &forwardPaM;
        }
        SwPaM backwardPaM(*pTableNd);
        if (backwardPaM.Move(fnMoveBackward, GoInNode))
        {
            if (pSavePaM == nullptr
                    // try to stay in the same outer table cell
                || (forwardPaM.GetPoint()->GetNode().FindTableNode() != pTableNd->StartOfSectionNode()->FindTableNode()
                    && forwardPaM.GetPoint()->GetNode().StartOfSectionIndex()
                        < backwardPaM.GetPoint()->GetNode().StartOfSectionIndex()))
            {
                pSavePaM = &backwardPaM;
            }
        }
        assert(pSavePaM); // due to bNewTextNd this must succeed
        {
            SwPaM const tmpPaM(*pTableNd, *pTableNd->EndOfSectionNode());
            ::PaMCorrAbs(tmpPaM, *pSavePaM->GetPoint());
        }

        // Move hard PageBreaks to the succeeding Node
        bool bSavePageBreak = false, bSavePageDesc = false;
        SwNodeOffset nNextNd = pTableNd->EndOfSectionIndex()+1;
        SwContentNode* pNextNd = GetNodes()[ nNextNd ]->GetContentNode();
        if (pNextNd)
        {
            SwFrameFormat* pTableFormat = pTableNd->GetTable().GetFrameFormat();
            const SfxPoolItem *pItem;
            if (SfxItemState::SET == pTableFormat->GetItemState(RES_PAGEDESC,
                    false, &pItem))
            {
                pNextNd->SetAttr( *pItem );
                bSavePageDesc = true;
            }

            if (SfxItemState::SET == pTableFormat->GetItemState(RES_BREAK,
                    false, &pItem))
            {
                pNextNd->SetAttr( *pItem );
                bSavePageBreak = true;
            }
        }
        std::unique_ptr<SwUndoDelete> pUndo(new SwUndoDelete(aPaM, SwDeleteFlags::Default));
        if (bNewTextNd)
            pUndo->SetTableDelLastNd();
        pUndo->SetPgBrkFlags( bSavePageBreak, bSavePageDesc );
        pUndo->SetTableName(pTableNd->GetTable().GetFrameFormat()->GetName());
        GetIDocumentUndoRedo().AppendUndo( std::move(pUndo) );
    }
    else
    {
        if (bNewTextNd)
        {
            const SwNodeIndex aTmpIdx( *pTableNd->EndOfSectionNode(), 1 );
            GetNodes().MakeTextNode( aTmpIdx.GetNode(),
                        getIDocumentStylePoolAccess().GetTextCollFromPool( RES_POOLCOLL_STANDARD ) );
        }

        // Save the cursors (UNO and otherwise)
        SwPaM const* pSavePaM(nullptr);
        SwPaM forwardPaM(*pTableNd->EndOfSectionNode());
        if (forwardPaM.Move(fnMoveForward, GoInNode))
        {
            pSavePaM = &forwardPaM;
        }
        SwPaM backwardPaM(*pTableNd);
        if (backwardPaM.Move(fnMoveBackward, GoInNode))
        {
            if (pSavePaM == nullptr
                    // try to stay in the same outer table cell
                || (forwardPaM.GetPoint()->GetNode().FindTableNode() != pTableNd->StartOfSectionNode()->FindTableNode()
                    && forwardPaM.GetPoint()->GetNode().StartOfSectionIndex()
                        < backwardPaM.GetPoint()->GetNode().StartOfSectionIndex()))
            {
                pSavePaM = &backwardPaM;
            }
        }
        assert(pSavePaM); // due to bNewTextNd this must succeed
        {
            SwPaM const tmpPaM(*pTableNd, *pTableNd->EndOfSectionNode());
            ::PaMCorrAbs(tmpPaM, *pSavePaM->GetPoint());
        }

        // Move hard PageBreaks to the succeeding Node
        SwContentNode* pNextNd = GetNodes()[ pTableNd->EndOfSectionIndex()+1 ]->GetContentNode();
        if (pNextNd)
        {
            SwFrameFormat* pTableFormat = pTableNd->GetTable().GetFrameFormat();
            const SfxPoolItem *pItem;
            if (SfxItemState::SET == pTableFormat->GetItemState(RES_PAGEDESC,
                    false, &pItem))
            {
                pNextNd->SetAttr( *pItem );
            }

            if (SfxItemState::SET == pTableFormat->GetItemState(RES_BREAK,
                    false, &pItem))
            {
                pNextNd->SetAttr( *pItem );
            }
        }

        pTableNd->DelFrames();
        getIDocumentContentOperations().DeleteSection( pTableNd );
    }

    getIDocumentState().SetModified();
    getIDocumentFieldsAccess().SetFieldsDirty( true, nullptr, SwNodeOffset(0) );
}

bool SwDoc::DeleteRowCol(const SwSelBoxes& rBoxes, RowColMode const eMode)
{
    if (!(eMode & SwDoc::RowColMode::DeleteProtected)
        && ::HasProtectedCells(rBoxes))
    {
        return false;
    }

    OSL_ENSURE( !rBoxes.empty(), "No valid Box list" );
    SwTableNode* pTableNd = const_cast<SwTableNode*>(rBoxes[0]->GetSttNd()->FindTableNode());
    if( !pTableNd )
        return false;

    if (!(eMode & SwDoc::RowColMode::DeleteProtected)
        && dynamic_cast<const SwDDETable*>(&pTableNd->GetTable()) != nullptr)
    {
        return false;
    }

    ::ClearFEShellTabCols(*this, nullptr);
    SwSelBoxes aSelBoxes( rBoxes );
    SwTable &rTable = pTableNd->GetTable();
    tools::Long nMin = 0;
    tools::Long nMax = 0;
    if( rTable.IsNewModel() )
    {
        if (eMode & SwDoc::RowColMode::DeleteColumn)
            rTable.ExpandColumnSelection( aSelBoxes, nMin, nMax );
        else
            rTable.FindSuperfluousRows( aSelBoxes );
    }

    // Are we deleting the whole Table?
    const SwNodeOffset nTmpIdx1 = pTableNd->GetIndex();
    const SwNodeOffset nTmpIdx2 = aSelBoxes.back()->GetSttNd()->EndOfSectionIndex() + 1;
    if( pTableNd->GetTable().GetTabSortBoxes().size() == aSelBoxes.size() &&
        aSelBoxes[0]->GetSttIdx()-1 == nTmpIdx1 &&
        nTmpIdx2 == pTableNd->EndOfSectionIndex() )
    {
        DelTable(pTableNd);
        return true;
    }

    std::unique_ptr<SwUndoTableNdsChg> pUndo;
    if (GetIDocumentUndoRedo().DoesUndo())
    {
        pUndo.reset(new SwUndoTableNdsChg( SwUndoId::TABLE_DELBOX, aSelBoxes, *pTableNd,
                                     nMin, nMax, 0, false, false ));
    }

    bool bRet(false);
    {
        ::sw::UndoGuard const undoGuard(GetIDocumentUndoRedo());
        rTable.SwitchFormulasToInternalRepresentation();

        if (rTable.IsNewModel())
        {
            if (eMode & SwDoc::RowColMode::DeleteColumn)
                rTable.PrepareDeleteCol( nMin, nMax );
            rTable.FindSuperfluousRows( aSelBoxes );
            if (pUndo)
                pUndo->ReNewBoxes( aSelBoxes );
        }
        bRet = rTable.DeleteSel( *this, aSelBoxes, nullptr, pUndo.get(), true, true );
        if (bRet)
        {
            if (SwFEShell* pFEShell = GetDocShell()->GetFEShell())
            {
                if (officecfg::Office::Writer::Table::Change::ApplyTableAutoFormat::get())
                {
                    pFEShell->UpdateTableStyleFormatting();
                }
            }

            getIDocumentState().SetModified();
            getIDocumentFieldsAccess().SetFieldsDirty( true, nullptr, SwNodeOffset(0) );
        }
    }

    if( pUndo && bRet )
    {
        GetIDocumentUndoRedo().AppendUndo( std::move(pUndo) );
    }

    return bRet;
}

/**
 * Split up/merge Boxes in the Table
 */
bool SwDoc::SplitTable( const SwSelBoxes& rBoxes, bool bVert, sal_uInt16 nCnt,
                      bool bSameHeight )
{
    OSL_ENSURE( !rBoxes.empty() && nCnt, "No valid Box list" );
    SwTableNode* pTableNd = const_cast<SwTableNode*>(rBoxes[0]->GetSttNd()->FindTableNode());
    if( !pTableNd )
        return false;

    SwTable& rTable = pTableNd->GetTable();
    if( dynamic_cast<const SwDDETable*>( &rTable) !=  nullptr)
        return false;

    std::vector<SwNodeOffset> aNdsCnts;
    SwTableSortBoxes aTmpLst;
    std::unique_ptr<SwUndoTableNdsChg> pUndo;
    if (GetIDocumentUndoRedo().DoesUndo())
    {
        pUndo.reset(new SwUndoTableNdsChg( SwUndoId::TABLE_SPLIT, rBoxes, *pTableNd, 0, 0,
                                     nCnt, bVert, bSameHeight ));

        aTmpLst.insert( rTable.GetTabSortBoxes() );
        if( !bVert )
        {
            for (size_t n = 0; n < rBoxes.size(); ++n)
            {
                const SwStartNode* pSttNd = rBoxes[ n ]->GetSttNd();
                aNdsCnts.push_back( pSttNd->EndOfSectionIndex() -
                                    pSttNd->GetIndex() );
            }
        }
    }

    bool bRet(false);
    {
        ::sw::UndoGuard const undoGuard(GetIDocumentUndoRedo());
        rTable.SwitchFormulasToInternalRepresentation();

        if (bVert)
            bRet = rTable.SplitCol(*this, rBoxes, nCnt);
        else
            bRet = rTable.SplitRow(*this, rBoxes, nCnt, bSameHeight);

        if (bRet)
        {
            if (SwFEShell* pFEShell = GetDocShell()->GetFEShell())
            {
                if (officecfg::Office::Writer::Table::Change::ApplyTableAutoFormat::get())
                {
                    pFEShell->UpdateTableStyleFormatting();
                }
            }

            getIDocumentState().SetModified();
            getIDocumentFieldsAccess().SetFieldsDirty( true, nullptr, SwNodeOffset(0) );
        }
    }

    if( pUndo && bRet )
    {
        if( bVert )
            pUndo->SaveNewBoxes( *pTableNd, aTmpLst );
        else
            pUndo->SaveNewBoxes( *pTableNd, aTmpLst, rBoxes, aNdsCnts );
        GetIDocumentUndoRedo().AppendUndo( std::move(pUndo) );
    }

    return bRet;
}

TableMergeErr SwDoc::MergeTable( SwPaM& rPam )
{
    // Check if the current cursor's Point/Mark are inside a Table
    SwTableNode* pTableNd = rPam.GetPointNode().FindTableNode();
    if( !pTableNd )
        return TableMergeErr::NoSelection;
    SwTable& rTable = pTableNd->GetTable();
    if( dynamic_cast<const SwDDETable*>( &rTable) !=  nullptr )
        return TableMergeErr::NoSelection;
    TableMergeErr nRet = TableMergeErr::NoSelection;
    if( !rTable.IsNewModel() )
    {
        nRet =::CheckMergeSel( rPam );
        if( TableMergeErr::Ok != nRet )
            return nRet;
        nRet = TableMergeErr::NoSelection;
    }

    // #i33394#
    GetIDocumentUndoRedo().StartUndo( SwUndoId::TABLE_MERGE, nullptr );

    RedlineFlags eOld = getIDocumentRedlineAccess().GetRedlineFlags();
    getIDocumentRedlineAccess().SetRedlineFlags_intern(eOld | RedlineFlags::Ignore);

    std::unique_ptr<SwUndoTableMerge> pUndo;
    if (GetIDocumentUndoRedo().DoesUndo())
        pUndo.reset(new SwUndoTableMerge( rPam ));

    // Find the Boxes via the Layout
    SwSelBoxes aBoxes;
    SwSelBoxes aMerged;
    SwTableBox* pMergeBox;

    if( !rTable.PrepareMerge( rPam, aBoxes, aMerged, &pMergeBox, pUndo.get() ) )
    {   // No cells found to merge
        getIDocumentRedlineAccess().SetRedlineFlags_intern( eOld );
        if( pUndo )
        {
            pUndo.reset();
            SwUndoId nLastUndoId(SwUndoId::EMPTY);
            if (GetIDocumentUndoRedo().GetLastUndoInfo(nullptr, & nLastUndoId)
                && (SwUndoId::REDLINE == nLastUndoId))
            {
                // FIXME: why is this horrible cleanup necessary?
                SwUndoRedline *const pU = dynamic_cast<SwUndoRedline*>(
                        GetUndoManager().RemoveLastUndo());
                if (pU && pU->GetRedlSaveCount())
                {
                    SwEditShell *const pEditShell(GetEditShell());
                    assert(pEditShell);
                    ::sw::UndoRedoContext context(*this, *pEditShell);
                    static_cast<SfxUndoAction *>(pU)->UndoWithContext(context);
                }
                delete pU;
            }
        }
    }
    else
    {
        // The PaMs need to be removed from the to-be-deleted range. Thus always place
        // them at the end of/on top of the Table; it's always set to the old position via
        // the Document Position.
        // For a start remember an index for the temporary position, because we cannot
        // access it after GetMergeSel
        {
            rPam.DeleteMark();
            rPam.GetPoint()->Assign(*pMergeBox->GetSttNd());
            rPam.SetMark();
            rPam.DeleteMark();

            SwPaM* pTmp = &rPam;
            while( &rPam != ( pTmp = pTmp->GetNext() ))
                for( int i = 0; i < 2; ++i )
                    pTmp->GetBound( static_cast<bool>(i) ) = *rPam.GetPoint();

            if (SwTableCursor* pTableCursor = dynamic_cast<SwTableCursor*>(&rPam))
            {
                // tdf#135098 update selection so rPam's m_SelectedBoxes is updated
                // to not contain the soon to-be-deleted SwTableBox so if the rPam
                // is queried via a11y it doesn't claim the deleted cell still
                // exists
                pTableCursor->NewTableSelection();
            }
        }

        // Merge them
        pTableNd->GetTable().SwitchFormulasToInternalRepresentation();

        if( pTableNd->GetTable().Merge( *this, aBoxes, aMerged, pMergeBox, pUndo.get() ))
        {
            nRet = TableMergeErr::Ok;

            getIDocumentState().SetModified();
            getIDocumentFieldsAccess().SetFieldsDirty( true, nullptr, SwNodeOffset(0) );
            if( pUndo )
            {
                GetIDocumentUndoRedo().AppendUndo( std::move(pUndo) );
            }
        }

        rPam.GetPoint()->Assign( *pMergeBox->GetSttNd() );
        rPam.Move();

        ::ClearFEShellTabCols(*this, nullptr);
        getIDocumentRedlineAccess().SetRedlineFlags_intern( eOld );
    }
    GetIDocumentUndoRedo().EndUndo( SwUndoId::TABLE_MERGE, nullptr );
    return nRet;
}

SwTableNode::SwTableNode( const SwNode& rWhere )
    : SwStartNode( rWhere, SwNodeType::Table )
{
    m_pTable.reset(new SwTable);
}

SwTableNode::~SwTableNode()
{
    // Notify UNO wrappers
    GetTable().GetFrameFormat()->GetNotifier().Broadcast(SfxHint(SfxHintId::Dying));
    DelFrames();
    m_pTable->SetTableNode(this); // set this so that ~SwDDETable can read it!
    m_pTable.reset();
}

SwTabFrame *SwTableNode::MakeFrame( SwFrame* pSib )
{
    return new SwTabFrame( *m_pTable, pSib );
}

/**
 * Creates all Views from the Document for the preceding Node. The resulting ContentFrames
 * are added to the corresponding Layout.
 */
void SwTableNode::MakeFramesForAdjacentContentNode(const SwNodeIndex & rIdx)
{
    if( !GetTable().GetFrameFormat()->HasWriterListeners()) // Do we actually have Frame?
        return;

    SwFrame *pFrame;
    SwContentNode * pNode = rIdx.GetNode().GetContentNode();

    assert(pNode && "No ContentNode or CopyNode and new Node is identical");

    bool bBefore = rIdx < GetIndex();

    SwNode2Layout aNode2Layout( *this, rIdx.GetIndex() );

    while( nullptr != (pFrame = aNode2Layout.NextFrame()) )
    {
        if ( ( pFrame->getRootFrame()->HasMergedParas() &&
                                !pNode->IsCreateFrameWhenHidingRedlines() ) ||
              // tdf#153819 table deletion with change tracking:
              // table node without frames in Hide Changes mode
              !pFrame->GetUpper() )
        {
            continue;
        }
        SwFrame *pNew = pNode->MakeFrame( pFrame );
        // Will the Node receive Frames before or after?
        if ( bBefore )
            // The new one precedes me
            pNew->Paste( pFrame->GetUpper(), pFrame );
        else
            // The new one succeeds me
            pNew->Paste( pFrame->GetUpper(), pFrame->GetNext() );
    }
}

/**
 * Create a TableFrame for every Shell and insert before the corresponding ContentFrame.
 */
void SwTableNode::MakeOwnFrames(SwPosition* pIdxBehind)
{
    SwNode *pNd = GetNodes().FindPrvNxtFrameNode( *this, EndOfSectionNode() );
    if( !pNd )
    {
        if (pIdxBehind)
            pIdxBehind->Assign(*this);
        return;
    }
    if (pIdxBehind)
        pIdxBehind->Assign(*pNd);

    SwFrame *pFrame( nullptr );
    SwLayoutFrame *pUpper( nullptr );
    SwNode2Layout aNode2Layout( *pNd, GetIndex() );
    while( nullptr != (pUpper = aNode2Layout.UpperFrame( pFrame, *this )) )
    {
        if (pUpper->getRootFrame()->HasMergedParas()
            && !IsCreateFrameWhenHidingRedlines())
        {
            continue;
        }
        SwTabFrame* pNew = MakeFrame( pUpper );
        pNew->Paste( pUpper, pFrame );
        // #i27138#
        // notify accessibility paragraphs objects about changed
        // CONTENT_FLOWS_FROM/_TO relation.
        // Relation CONTENT_FLOWS_FROM for next paragraph will change
        // and relation CONTENT_FLOWS_TO for previous paragraph will change.
#if !ENABLE_WASM_STRIP_ACCESSIBILITY
        {
            SwViewShell* pViewShell( pNew->getRootFrame()->GetCurrShell() );
            if ( pViewShell && pViewShell->GetLayout() &&
                 pViewShell->GetLayout()->IsAnyShellAccessible() )
            {
                auto pNext = pNew->FindNextCnt( true );
                auto pPrev = pNew->FindPrevCnt();
                pViewShell->InvalidateAccessibleParaFlowRelation(
                            pNext ? pNext->DynCastTextFrame() : nullptr,
                            pPrev ? pPrev->DynCastTextFrame() : nullptr );
            }
        }
#endif
        pNew->RegistFlys();
    }
}

void SwTableNode::DelFrames(SwRootFrame const*const pLayout)
{
    /* For a start, cut out and delete the TabFrames (which will also delete the Columns and Rows)
       The TabFrames are attached to the FrameFormat of the SwTable.
       We need to delete them in a more cumbersome way, for the Master to also delete the Follows. */

    SwIterator<SwTabFrame,SwFormat> aIter( *(m_pTable->GetFrameFormat()) );
    SwTabFrame *pFrame = aIter.First();
    while ( pFrame )
    {
        bool bAgain = false;
        {
            if (!pFrame->IsFollow() && (!pLayout || pLayout == pFrame->getRootFrame()))
            {
                while ( pFrame->HasFollow() )
                    pFrame->JoinAndDelFollows();
                // #i27138#
                // notify accessibility paragraphs objects about changed
                // CONTENT_FLOWS_FROM/_TO relation.
                // Relation CONTENT_FLOWS_FROM for current next paragraph will change
                // and relation CONTENT_FLOWS_TO for current previous paragraph will change.
#if !ENABLE_WASM_STRIP_ACCESSIBILITY
                if (!GetDoc().IsInDtor())
                {
                    SwViewShell* pViewShell( pFrame->getRootFrame()->GetCurrShell() );
                    if ( pViewShell && pViewShell->GetLayout() &&
                         pViewShell->GetLayout()->IsAnyShellAccessible() )
                    {
                        auto pNext = pFrame->FindNextCnt( true );
                        auto pPrev = pFrame->FindPrevCnt();
                        pViewShell->InvalidateAccessibleParaFlowRelation(
                            pNext ? pNext->DynCastTextFrame() : nullptr,
                            pPrev ? pPrev->DynCastTextFrame() : nullptr );
                    }
                }
#endif
                if (pFrame->GetUpper())
                    pFrame->Cut();
                SwFrame::DestroyFrame(pFrame);
                bAgain = true;
            }
        }
        pFrame = bAgain ? aIter.First() : aIter.Next();
    }
}

void SwTableNode::SetNewTable( std::unique_ptr<SwTable> pNewTable, bool bNewFrames )
{
    DelFrames();
    m_pTable->SetTableNode(this);
    m_pTable = std::move(pNewTable);
    if( bNewFrames )
    {
        MakeOwnFrames();
    }
}

void SwTableNode::RemoveRedlines()
{
    SwDoc& rDoc = GetDoc();
    SwTable& rTable = GetTable();
    rDoc.getIDocumentRedlineAccess().GetExtraRedlineTable().DeleteAllTableRedlines(rDoc, rTable, true, RedlineType::Any);
}

void SwTableNode::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SwTableNode"));
    (void)xmlTextWriterWriteFormatAttribute(pWriter, BAD_CAST("ptr"), "%p", this);
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("index"), BAD_CAST(OString::number(sal_Int32(GetIndex())).getStr()));

    if (m_pTable)
    {
        m_pTable->dumpAsXml(pWriter);
    }

    // (void)xmlTextWriterEndElement(pWriter); - it is a start node, so don't end, will make xml better nested
}

void SwDoc::GetTabCols( SwTabCols &rFill, const SwCellFrame* pBoxFrame )
{
    OSL_ENSURE( pBoxFrame, "pBoxFrame needs to be specified!" );
    if( !pBoxFrame )
        return;

    SwTabFrame *pTab = const_cast<SwFrame*>(static_cast<SwFrame const *>(pBoxFrame))->ImplFindTabFrame();
    const SwTableBox* pBox = pBoxFrame->GetTabBox();

    // Set fixed points, LeftMin in Document coordinates, all others relative
    SwRectFnSet aRectFnSet(pTab);
    const SwPageFrame* pPage = pTab->FindPageFrame();
    const sal_uLong nLeftMin = aRectFnSet.GetLeft(pTab->getFrameArea()) -
                           aRectFnSet.GetLeft(pPage->getFrameArea());
    const sal_uLong nRightMax = aRectFnSet.GetRight(pTab->getFrameArea()) -
                            aRectFnSet.GetLeft(pPage->getFrameArea());

    rFill.SetLeftMin ( nLeftMin );
    rFill.SetLeft    ( aRectFnSet.GetLeft(pTab->getFramePrintArea()) );
    rFill.SetRight   ( aRectFnSet.GetRight(pTab->getFramePrintArea()));
    rFill.SetRightMax( nRightMax - nLeftMin );

    pTab->GetTable()->GetTabCols( rFill, pBox );
}

// Here are some little helpers used in SwDoc::GetTabRows

#define ROWFUZZY 25

namespace {

struct FuzzyCompare
{
    bool operator() ( tools::Long s1, tools::Long s2 ) const;
};

}

bool FuzzyCompare::operator() ( tools::Long s1, tools::Long s2 ) const
{
    return ( s1 < s2 && std::abs( s1 - s2 ) > ROWFUZZY );
}

static bool lcl_IsFrameInColumn( const SwCellFrame& rFrame, SwSelBoxes const & rBoxes )
{
    for (size_t i = 0; i < rBoxes.size(); ++i)
    {
        if ( rFrame.GetTabBox() == rBoxes[ i ] )
            return true;
    }

    return false;
}

void SwDoc::GetTabRows( SwTabCols &rFill, const SwCellFrame* pBoxFrame )
{
    OSL_ENSURE( pBoxFrame, "GetTabRows called without pBoxFrame" );

    // Make code robust:
    if ( !pBoxFrame )
        return;

    // #i39552# Collection of the boxes of the current
    // column has to be done at the beginning of this function, because
    // the table may be formatted in ::GetTableSel.
    SwDeletionChecker aDelCheck( pBoxFrame );

    SwSelBoxes aBoxes;
    const SwContentFrame* pContent = ::GetCellContent( *pBoxFrame );
    if ( pContent && pContent->IsTextFrame() )
    {
        const SwPosition aPos(*static_cast<const SwTextFrame*>(pContent)->GetTextNodeFirst());
        const SwCursor aTmpCursor( aPos, nullptr );
        ::GetTableSel( aTmpCursor, aBoxes, SwTableSearchType::Col );
    }

    // Make code robust:
    if ( aDelCheck.HasBeenDeleted() )
    {
        OSL_FAIL( "Current box has been deleted during GetTabRows()" );
        return;
    }

    // Make code robust:
    const SwTabFrame* pTab = pBoxFrame->FindTabFrame();
    OSL_ENSURE( pTab, "GetTabRows called without a table" );
    if ( !pTab )
        return;

    const SwFrame* pFrame = pTab->GetNextLayoutLeaf();

    // Set fixed points, LeftMin in Document coordinates, all others relative
    SwRectFnSet aRectFnSet(pTab);
    const SwPageFrame* pPage = pTab->FindPageFrame();
    const tools::Long nLeftMin  = ( aRectFnSet.IsVert() ?
                             pTab->GetPrtLeft() - pPage->getFrameArea().Left() :
                             pTab->GetPrtTop() - pPage->getFrameArea().Top() );
    const tools::Long nLeft     = aRectFnSet.IsVert() ? LONG_MAX : 0;
    const tools::Long nRight    = aRectFnSet.GetHeight(pTab->getFramePrintArea());
    const tools::Long nRightMax = aRectFnSet.IsVert() ? nRight : LONG_MAX;

    rFill.SetLeftMin( nLeftMin );
    rFill.SetLeft( nLeft );
    rFill.SetRight( nRight );
    rFill.SetRightMax( nRightMax );

    typedef std::map< tools::Long, std::pair< tools::Long, long >, FuzzyCompare > BoundaryMap;
    BoundaryMap aBoundaries;
    BoundaryMap::iterator aIter;
    std::pair< tools::Long, long > aPair;

    typedef std::map< tools::Long, bool > HiddenMap;
    HiddenMap aHidden;
    HiddenMap::iterator aHiddenIter;

    while ( pFrame && pTab->IsAnLower( pFrame ) )
    {
        if ( pFrame->IsCellFrame() && pFrame->FindTabFrame() == pTab )
        {
            // upper and lower borders of current cell frame:
            tools::Long nUpperBorder = aRectFnSet.GetTop(pFrame->getFrameArea());
            tools::Long nLowerBorder = aRectFnSet.GetBottom(pFrame->getFrameArea());

            // get boundaries for nUpperBorder:
            aIter = aBoundaries.find( nUpperBorder );
            if ( aIter == aBoundaries.end() )
            {
                aPair.first = nUpperBorder; aPair.second = LONG_MAX;
                aBoundaries[ nUpperBorder ] = aPair;
            }

            // get boundaries for nLowerBorder:
            aIter = aBoundaries.find( nLowerBorder );
            if ( aIter == aBoundaries.end() )
            {
                aPair.first = nUpperBorder; aPair.second = LONG_MAX;
            }
            else
            {
                nLowerBorder = (*aIter).first;
                tools::Long nNewLowerBorderUpperBoundary = std::max( (*aIter).second.first, nUpperBorder );
                aPair.first = nNewLowerBorderUpperBoundary; aPair.second = LONG_MAX;
            }
            aBoundaries[ nLowerBorder ] = aPair;

            // calculate hidden flags for entry nUpperBorder/nLowerBorder:
            tools::Long nTmpVal = nUpperBorder;
            for ( sal_uInt8 i = 0; i < 2; ++i )
            {
                aHiddenIter = aHidden.find( nTmpVal );
                if ( aHiddenIter == aHidden.end() )
                    aHidden[ nTmpVal ] = !lcl_IsFrameInColumn( *static_cast<const SwCellFrame*>(pFrame), aBoxes );
                else
                {
                    if ( aHidden[ nTmpVal ] &&
                         lcl_IsFrameInColumn( *static_cast<const SwCellFrame*>(pFrame), aBoxes ) )
                        aHidden[ nTmpVal ] = false;
                }
                nTmpVal = nLowerBorder;
            }
        }

        pFrame = pFrame->GetNextLayoutLeaf();
    }

    // transfer calculated values from BoundaryMap and HiddenMap into rFill:
    size_t nIdx = 0;
    for ( const auto& rEntry : aBoundaries )
    {
        const tools::Long nTabTop = aRectFnSet.GetPrtTop(*pTab);
        const tools::Long nKey = aRectFnSet.YDiff( rEntry.first, nTabTop );
        const std::pair< tools::Long, long > aTmpPair = rEntry.second;
        const tools::Long nFirst = aRectFnSet.YDiff( aTmpPair.first, nTabTop );
        const tools::Long nSecond = aTmpPair.second;

        aHiddenIter = aHidden.find( rEntry.first );
        const bool bHidden = aHiddenIter != aHidden.end() && (*aHiddenIter).second;
        rFill.Insert( nKey, nFirst, nSecond, bHidden, nIdx++ );
    }

    // delete first and last entry
    OSL_ENSURE( rFill.Count(), "Deleting from empty vector. Fasten your seatbelts!" );
    // #i60818# There may be only one entry in rFill. Make
    // code robust by checking count of rFill.
    if ( rFill.Count() ) rFill.Remove( 0 );
    if ( rFill.Count() ) rFill.Remove( rFill.Count() - 1 );
    rFill.SetLastRowAllowedToChange( !pTab->HasFollowFlowLine() );
}

void SwDoc::SetTabCols( const SwTabCols &rNew, bool bCurRowOnly,
                        const SwCellFrame* pBoxFrame )
{
    const SwTableBox* pBox = nullptr;
    SwTabFrame *pTab = nullptr;

    if( pBoxFrame )
    {
        pTab = const_cast<SwFrame*>(static_cast<SwFrame const *>(pBoxFrame))->ImplFindTabFrame();
        pBox = pBoxFrame->GetTabBox();
    }
    else
    {
        OSL_ENSURE( false, "must specify pBoxFrame" );
        return ;
    }

    // If the Table is still using relative values (USHRT_MAX)
    // we need to switch to absolute ones.
    SwTable& rTab = *pTab->GetTable();
    const SwFormatFrameSize& rTableFrameSz = rTab.GetFrameFormat()->GetFrameSize();
    SwRectFnSet aRectFnSet(pTab);
    // #i17174# - With fix for #i9040# the shadow size is taken
    // from the table width. Thus, add its left and right size to current table
    // printing area width in order to get the correct table size attribute.
    SwTwips nPrtWidth = aRectFnSet.GetWidth(pTab->getFramePrintArea());
    {
        SvxShadowItem aShadow( rTab.GetFrameFormat()->GetShadow() );
        nPrtWidth += aShadow.CalcShadowSpace( SvxShadowItemSide::LEFT ) +
                     aShadow.CalcShadowSpace( SvxShadowItemSide::RIGHT );
    }
    if( nPrtWidth != rTableFrameSz.GetWidth() )
    {
        SwFormatFrameSize aSz( rTableFrameSz );
        aSz.SetWidth( nPrtWidth );
        rTab.GetFrameFormat()->SetFormatAttr( aSz );
    }

    SwTabCols aOld( rNew.Count() );

    const SwPageFrame* pPage = pTab->FindPageFrame();
    const sal_uLong nLeftMin = aRectFnSet.GetLeft(pTab->getFrameArea()) -
                           aRectFnSet.GetLeft(pPage->getFrameArea());
    const sal_uLong nRightMax = aRectFnSet.GetRight(pTab->getFrameArea()) -
                            aRectFnSet.GetLeft(pPage->getFrameArea());

    // Set fixed points, LeftMin in Document coordinates, all others relative
    aOld.SetLeftMin ( nLeftMin );
    aOld.SetLeft    ( aRectFnSet.GetLeft(pTab->getFramePrintArea()) );
    aOld.SetRight   ( aRectFnSet.GetRight(pTab->getFramePrintArea()));
    aOld.SetRightMax( nRightMax - nLeftMin );

    rTab.GetTabCols( aOld, pBox );
    SetTabCols(rTab, rNew, aOld, pBox, bCurRowOnly );
}

void SwDoc::SetTabRows( const SwTabCols &rNew, bool bCurColOnly,
                        const SwCellFrame* pBoxFrame )
{
    SwTabFrame *pTab = nullptr;

    if( pBoxFrame )
    {
        pTab = const_cast<SwFrame*>(static_cast<SwFrame const *>(pBoxFrame))->ImplFindTabFrame();
    }
    else
    {
        OSL_ENSURE( false, "must specify pBoxFrame" );
        return ;
    }

    // If the Table is still using relative values (USHRT_MAX)
    // we need to switch to absolute ones.
    SwRectFnSet aRectFnSet(pTab);
    SwTabCols aOld( rNew.Count() );

    // Set fixed points, LeftMin in Document coordinates, all others relative
    const SwPageFrame* pPage = pTab->FindPageFrame();

    aOld.SetRight( aRectFnSet.GetHeight(pTab->getFramePrintArea()) );
    tools::Long nLeftMin;
    if ( aRectFnSet.IsVert() )
    {
        nLeftMin = pTab->GetPrtLeft() - pPage->getFrameArea().Left();
        aOld.SetLeft    ( LONG_MAX );
        aOld.SetRightMax( aOld.GetRight() );

    }
    else
    {
        nLeftMin = pTab->GetPrtTop() - pPage->getFrameArea().Top();
        aOld.SetLeft    ( 0 );
        aOld.SetRightMax( LONG_MAX );
    }
    aOld.SetLeftMin ( nLeftMin );

    GetTabRows( aOld, pBoxFrame );

    GetIDocumentUndoRedo().StartUndo( SwUndoId::TABLE_ATTR, nullptr );

    // check for differences between aOld and rNew:
    const size_t nCount = rNew.Count();
    const SwTable* pTable = pTab->GetTable();
    OSL_ENSURE( pTable, "My colleague told me, this couldn't happen" );

    for ( size_t i = 0; i <= nCount; ++i )
    {
        const size_t nIdxStt = aRectFnSet.IsVert() ? nCount - i : i - 1;
        const size_t nIdxEnd = aRectFnSet.IsVert() ? nCount - i - 1 : i;

        const tools::Long nOldRowStart = i == 0  ? 0 : aOld[ nIdxStt ];
        const tools::Long nOldRowEnd =   i == nCount ? aOld.GetRight() : aOld[ nIdxEnd ];
        const tools::Long nOldRowHeight = nOldRowEnd - nOldRowStart;

        const tools::Long nNewRowStart = i == 0  ? 0 : rNew[ nIdxStt ];
        const tools::Long nNewRowEnd =   i == nCount ? rNew.GetRight() : rNew[ nIdxEnd ];
        const tools::Long nNewRowHeight = nNewRowEnd - nNewRowStart;

        const tools::Long nDiff = nNewRowHeight - nOldRowHeight;
        if ( std::abs( nDiff ) >= ROWFUZZY )
        {
            // For the old table model pTextFrame and pLine will be set for every box.
            // For the new table model pTextFrame will be set if the box is not covered,
            // but the pLine will be set if the box is not an overlapping box
            // In the new table model the row height can be adjusted,
            // when both variables are set.
            const SwTextFrame* pTextFrame = nullptr;
            const SwTableLine* pLine = nullptr;

            // Iterate over all SwCellFrames with Bottom = nOldPos
            const SwFrame* pFrame = pTab->GetNextLayoutLeaf();
            while ( pFrame && pTab->IsAnLower( pFrame ) )
            {
                if ( pFrame->IsCellFrame() && pFrame->FindTabFrame() == pTab )
                {
                    const tools::Long nLowerBorder = aRectFnSet.GetBottom(pFrame->getFrameArea());
                    const sal_uLong nTabTop = aRectFnSet.GetPrtTop(*pTab);
                    if ( std::abs( aRectFnSet.YInc( nTabTop, nOldRowEnd ) - nLowerBorder ) <= ROWFUZZY )
                    {
                        if ( !bCurColOnly || pFrame == pBoxFrame )
                        {
                            const SwFrame* pContent = ::GetCellContent( static_cast<const SwCellFrame&>(*pFrame) );

                            if ( pContent && pContent->IsTextFrame() )
                            {
                                const SwTableBox* pBox = static_cast<const SwCellFrame*>(pFrame)->GetTabBox();
                                const sal_Int32 nRowSpan = pBox->getRowSpan();
                                if( nRowSpan > 0 ) // Not overlapped
                                    pTextFrame = static_cast<const SwTextFrame*>(pContent);
                                if( nRowSpan < 2 ) // Not overlapping for row height
                                    pLine = pBox->GetUpper();
                                if( pLine && pTextFrame ) // always for old table model
                                {
                                    // The new row height must not to be calculated from an overlapping box
                                    SwFormatFrameSize aNew( pLine->GetFrameFormat()->GetFrameSize() );
                                    const tools::Long nNewSize = aRectFnSet.GetHeight(pFrame->getFrameArea()) + nDiff;
                                    if( nNewSize != aNew.GetHeight() )
                                    {
                                        aNew.SetHeight( nNewSize );
                                        if ( SwFrameSize::Variable == aNew.GetHeightSizeType() )
                                            aNew.SetHeightSizeType( SwFrameSize::Minimum );
                                        // This position must not be in an overlapped box
                                        const SwPosition aPos(*static_cast<const SwTextFrame*>(pContent)->GetTextNodeFirst());
                                        const SwCursor aTmpCursor( aPos, nullptr );
                                        SetRowHeight( aTmpCursor, aNew );
                                        // For the new table model we're done, for the old one
                                        // there might be another (sub)row to adjust...
                                        if( pTable->IsNewModel() )
                                            break;
                                    }
                                    pLine = nullptr;
                                }
                            }
                        }
                    }
                }
                pFrame = pFrame->GetNextLayoutLeaf();
            }
        }
    }

    GetIDocumentUndoRedo().EndUndo( SwUndoId::TABLE_ATTR, nullptr );

    ::ClearFEShellTabCols(*this, nullptr);
}

/**
 * Direct access for UNO
 */
void SwDoc::SetTabCols(SwTable& rTab, const SwTabCols &rNew, const SwTabCols &rOld,
                                const SwTableBox *pStart, bool bCurRowOnly )
{
    if (GetIDocumentUndoRedo().DoesUndo())
    {
        GetIDocumentUndoRedo().AppendUndo(
            std::make_unique<SwUndoAttrTable>( *rTab.GetTableNode(), true ));
    }
    rTab.SetTabCols( rNew, rOld, pStart, bCurRowOnly );
    ::ClearFEShellTabCols(*this, nullptr);
    getIDocumentState().SetModified();
}

void SwDoc::SetRowsToRepeat( SwTable &rTable, sal_uInt16 nSet )
{
    if( nSet == rTable.GetRowsToRepeat() )
        return;

    if (GetIDocumentUndoRedo().DoesUndo())
    {
        GetIDocumentUndoRedo().AppendUndo(
            std::make_unique<SwUndoTableHeadline>(rTable, rTable.GetRowsToRepeat(), nSet) );
    }

    rTable.SetRowsToRepeat(nSet);
    rTable.GetFrameFormat()->CallSwClientNotify(sw::TableHeadingChange());
    getIDocumentState().SetModified();
}

void SwCollectTableLineBoxes::AddToUndoHistory( const SwContentNode& rNd )
{
    if( m_pHistory )
        m_pHistory->AddColl(rNd.GetFormatColl(), rNd.GetIndex(), SwNodeType::Text);
}

void SwCollectTableLineBoxes::AddBox( const SwTableBox& rBox )
{
    m_aPositionArr.push_back(m_nWidth);
    SwTableBox* p = const_cast<SwTableBox*>(&rBox);
    m_Boxes.push_back(p);
    m_nWidth = m_nWidth + o3tl::narrowing<sal_uInt16>(rBox.GetFrameFormat()->GetFrameSize().GetWidth());
}

const SwTableBox* SwCollectTableLineBoxes::GetBoxOfPos( const SwTableBox& rBox )
{
    size_t nCount = m_aPositionArr.size();
    if (nCount == 0)
        return nullptr;

    std::vector<sal_uInt16>::size_type n;
    for (n = 0; n < nCount; ++n)
    {
        if( m_aPositionArr[ n ] == m_nWidth )
            break;
        else if( m_aPositionArr[ n ] > m_nWidth )
        {
            if( n )
                --n;
            break;
        }
    }

    if (n >= nCount)
        --n;

    m_nWidth = m_nWidth + o3tl::narrowing<sal_uInt16>(rBox.GetFrameFormat()->GetFrameSize().GetWidth());
    return m_Boxes[ n ];
}

bool SwCollectTableLineBoxes::Resize( sal_uInt16 nOffset, sal_uInt16 nOldWidth )
{
    if( !m_aPositionArr.empty() )
    {
        std::vector<sal_uInt16>::size_type n;
        for( n = 0; n < m_aPositionArr.size(); ++n )
        {
            if( m_aPositionArr[ n ] == nOffset )
                break;
            else if( m_aPositionArr[ n ] > nOffset )
            {
                if( n )
                    --n;
                break;
            }
        }

        m_aPositionArr.erase( m_aPositionArr.begin(), m_aPositionArr.begin() + n );
        m_Boxes.erase(m_Boxes.begin(), m_Boxes.begin() + n);

        size_t nArrSize = m_aPositionArr.size();
        if (nArrSize)
        {
            if (nOldWidth == 0)
                throw o3tl::divide_by_zero();

            // Adapt the positions to the new Size
            for( n = 0; n < nArrSize; ++n )
            {
                sal_uLong nSize = m_nWidth;
                nSize *= ( m_aPositionArr[ n ] - nOffset );
                nSize /= nOldWidth;
                m_aPositionArr[ n ] = sal_uInt16( nSize );
            }
        }
    }
    return !m_aPositionArr.empty();
}

bool sw_Line_CollectBox( const SwTableLine*& rpLine, void* pPara )
{
    SwCollectTableLineBoxes* pSplPara = static_cast<SwCollectTableLineBoxes*>(pPara);
    if( pSplPara->IsGetValues() )
        for( const auto& rpBox : const_cast<SwTableLine*>(rpLine)->GetTabBoxes() )
            sw_Box_CollectBox(rpBox, pSplPara );
    else
        for( auto& rpBox : const_cast<SwTableLine*>(rpLine)->GetTabBoxes() )
            sw_BoxSetSplitBoxFormats(rpBox, pSplPara );
    return true;
}

void sw_Box_CollectBox( const SwTableBox* pBox, SwCollectTableLineBoxes* pSplPara )
{
    auto nLen = pBox->GetTabLines().size();
    if( nLen )
    {
        // Continue with the actual Line
        if( pSplPara->IsGetFromTop() )
            nLen = 0;
        else
            --nLen;

        const SwTableLine* pLn = pBox->GetTabLines()[ nLen ];
        sw_Line_CollectBox( pLn, pSplPara );
    }
    else
        pSplPara->AddBox( *pBox );
}

void sw_BoxSetSplitBoxFormats( SwTableBox* pBox, SwCollectTableLineBoxes* pSplPara )
{
    auto nLen = pBox->GetTabLines().size();
    if( nLen )
    {
        // Continue with the actual Line
        if( pSplPara->IsGetFromTop() )
            nLen = 0;
        else
            --nLen;

        const SwTableLine* pLn = pBox->GetTabLines()[ nLen ];
        sw_Line_CollectBox( pLn, pSplPara );
    }
    else
    {
        const SwTableBox* pSrcBox = pSplPara->GetBoxOfPos( *pBox );
        SwFrameFormat* pFormat = pSrcBox->GetFrameFormat();

        if( SplitTable_HeadlineOption::BorderCopy == pSplPara->GetMode() )
        {
            const SvxBoxItem& rBoxItem = pBox->GetFrameFormat()->GetBox();
            if( !rBoxItem.GetTop() )
            {
                SvxBoxItem aNew( rBoxItem );
                aNew.SetLine( pFormat->GetBox().GetBottom(), SvxBoxItemLine::TOP );
                if( aNew != rBoxItem )
                    pBox->ClaimFrameFormat()->SetFormatAttr( aNew );
            }
        }
        else
        {
            SfxItemSet aTmpSet(SfxItemSet::makeFixedSfxItemSet<
                        RES_LR_SPACE, RES_UL_SPACE, RES_PROTECT, RES_PROTECT,
                        RES_VERT_ORIENT, RES_VERT_ORIENT, RES_BACKGROUND,
                        RES_SHADOW>(pFormat->GetDoc().GetAttrPool()));
            aTmpSet.Put( pFormat->GetAttrSet() );
            if( aTmpSet.Count() )
                pBox->ClaimFrameFormat()->SetFormatAttr( aTmpSet );

            if( SplitTable_HeadlineOption::BoxAttrAllCopy == pSplPara->GetMode() )
            {
                SwNodeIndex aIdx( *pSrcBox->GetSttNd(), 1 );
                SwContentNode* pCNd = aIdx.GetNode().GetContentNode();
                if( !pCNd )
                    pCNd = SwNodes::GoNext(&aIdx);
                aIdx = *pBox->GetSttNd();
                SwContentNode* pDNd = SwNodes::GoNext(&aIdx);

                // If the Node is alone in the Section
                if( SwNodeOffset(2) == pDNd->EndOfSectionIndex() -
                        pDNd->StartOfSectionIndex() )
                {
                    pSplPara->AddToUndoHistory( *pDNd );
                    pDNd->ChgFormatColl( pCNd->GetFormatColl() );
                }
            }

            // note conditional template
            pBox->GetSttNd()->CheckSectionCondColl();
        }
    }
}

/**
 * Splits a Table in the top-level Line which contains the Index.
 * All succeeding top-level Lines go into a new Table/Node.
 *
 * @param bCalcNewSize true
 *                     Calculate the new Size for both from the
 *                     Boxes' Max; but only if Size is using absolute
 *                     values (USHRT_MAX)
 */
void SwDoc::SplitTable( const SwPosition& rPos, SplitTable_HeadlineOption eHdlnMode,
                        bool bCalcNewSize )
{
    SwNode* pNd = &rPos.GetNode();
    SwTableNode* pTNd = pNd->FindTableNode();
    if( !pTNd || pNd->IsTableNode() )
        return;

    if( dynamic_cast<const SwDDETable*>( &pTNd->GetTable() ) !=  nullptr)
        return;

    SwTable& rTable = pTNd->GetTable();
    rTable.SetHTMLTableLayout(std::shared_ptr<SwHTMLTableLayout>()); // Delete HTML Layout

    SwHistory aHistory;
    {
        SwNodeOffset nSttIdx = pNd->FindTableBoxStartNode()->GetIndex();
        // Find top-level Line
        SwTableBox* pBox = rTable.GetTableBox(nSttIdx);
        sal_uInt16 nSplitLine = 0;
        if(pBox)
        {
            SwTableLine* pLine = pBox->GetUpper();
            while(pLine->GetUpper())
                pLine = pLine->GetUpper()->GetUpper();

            // pLine contains the top-level Line now
            nSplitLine = rTable.GetTabLines().GetPos(pLine);
        }
        rTable.Split(GetUniqueTableName(), nSplitLine, GetIDocumentUndoRedo().DoesUndo() ? &aHistory : nullptr);
    }

    // Find Lines for the Layout update
    FndBox_ aFndBox( nullptr, nullptr );
    aFndBox.SetTableLines( rTable );
    aFndBox.DelFrames( rTable );

    SwTableNode* pNew = GetNodes().SplitTable( rPos.GetNode(), false, bCalcNewSize );

    if( pNew )
    {
        std::unique_ptr<SwSaveRowSpan> pSaveRowSp = pNew->GetTable().CleanUpTopRowSpan( rTable.GetTabLines().size() );
        SwUndoSplitTable* pUndo = nullptr;
        if (GetIDocumentUndoRedo().DoesUndo())
        {
            pUndo = new SwUndoSplitTable(
                        *pNew, std::move(pSaveRowSp), eHdlnMode, bCalcNewSize);
            GetIDocumentUndoRedo().AppendUndo(std::unique_ptr<SwUndo>(pUndo));
            if( aHistory.Count() )
                pUndo->SaveFormula( aHistory );
        }

        switch( eHdlnMode )
        {
        // Set the lower Border of the preceding Line to
        // the upper Border of the current one
        case SplitTable_HeadlineOption::BorderCopy:
            {
                SwCollectTableLineBoxes aPara( false, eHdlnMode );
                SwTableLine* pLn = rTable.GetTabLines()[
                            rTable.GetTabLines().size() - 1 ];
                for( const auto& rpBox : pLn->GetTabBoxes() )
                    sw_Box_CollectBox(rpBox, &aPara );

                aPara.SetValues( true );
                pLn = pNew->GetTable().GetTabLines()[ 0 ];
                for( auto& rpBox : pLn->GetTabBoxes() )
                    sw_BoxSetSplitBoxFormats(rpBox, &aPara );

                // Switch off repeating Header
                pNew->GetTable().SetRowsToRepeat( 0 );
            }
            break;

        // Take over the Attributes of the first Line to the new one
        case SplitTable_HeadlineOption::BoxAttrCopy:
        case SplitTable_HeadlineOption::BoxAttrAllCopy:
            {
                SwHistory* pHst = nullptr;
                if( SplitTable_HeadlineOption::BoxAttrAllCopy == eHdlnMode && pUndo )
                    pHst = pUndo->GetHistory();

                SwCollectTableLineBoxes aPara( true, eHdlnMode, pHst );
                SwTableLine* pLn = rTable.GetTabLines()[ 0 ];
                for( const auto& rpBox : pLn->GetTabBoxes() )
                    sw_Box_CollectBox(rpBox, &aPara );

                aPara.SetValues( true );
                pLn = pNew->GetTable().GetTabLines()[ 0 ];
                for( auto& rpBox : pLn->GetTabBoxes() )
                    sw_BoxSetSplitBoxFormats(rpBox, &aPara );
            }
            break;

        case SplitTable_HeadlineOption::ContentCopy:
            rTable.CopyHeadlineIntoTable( *pNew );
            if( pUndo )
                pUndo->SetTableNodeOffset( pNew->GetIndex() );
            break;

        case SplitTable_HeadlineOption::NONE:
            // Switch off repeating the Header
            pNew->GetTable().SetRowsToRepeat( 0 );
            break;
        }

        // And insert Frames
        pNew->MakeOwnFrames();

        // Insert a paragraph between the Table
        GetNodes().MakeTextNode( *pNew,
                                getIDocumentStylePoolAccess().GetTextCollFromPool( RES_POOLCOLL_TEXT ) );
    }

    // Update Layout
    aFndBox.MakeFrames( rTable );

    // TL_CHART2: need to inform chart of probably changed cell names
    UpdateCharts( rTable.GetFrameFormat()->GetName() );

    // update table style formatting of both the tables
    if (SwFEShell* pFEShell = GetDocShell()->GetFEShell())
    {
        if (officecfg::Office::Writer::Table::Change::ApplyTableAutoFormat::get())
        {
            pFEShell->UpdateTableStyleFormatting(pTNd);
            pFEShell->UpdateTableStyleFormatting(pNew);
        }
    }

    getIDocumentFieldsAccess().SetFieldsDirty( true, nullptr, SwNodeOffset(0) );
}

static bool lcl_ChgTableSize( SwTable& rTable )
{
    // The Attribute must not be set via the Modify or else all Boxes are
    // set back to 0.
    // So lock the Format.
    SwFrameFormat* pFormat = rTable.GetFrameFormat();
    SwFormatFrameSize aTableMaxSz( pFormat->GetFrameSize() );

    if( USHRT_MAX == aTableMaxSz.GetWidth() )
        return false;

    bool bLocked = pFormat->IsModifyLocked();
    pFormat->LockModify();

    aTableMaxSz.SetWidth( 0 );

    SwTableLines& rLns = rTable.GetTabLines();
    for( auto pLn : rLns )
    {
        SwTwips nMaxLnWidth = 0;
        SwTableBoxes& rBoxes = pLn->GetTabBoxes();
        for( auto pBox : rBoxes )
            nMaxLnWidth += pBox->GetFrameFormat()->GetFrameSize().GetWidth();

        if( nMaxLnWidth > aTableMaxSz.GetWidth() )
            aTableMaxSz.SetWidth( nMaxLnWidth );
    }
    pFormat->SetFormatAttr( aTableMaxSz );
    if( !bLocked ) // Release the Lock if appropriate
        pFormat->UnlockModify();

    return true;
}

namespace {

class SplitTable_Para
{
    std::map<SwFrameFormat const*, SwFrameFormat*> m_aSrcDestMap;
    SwTableNode* m_pNewTableNode;
    SwTable& m_rOldTable;

public:
    SplitTable_Para(SwTableNode* pNew, SwTable& rOld)
        : m_pNewTableNode(pNew)
        , m_rOldTable(rOld)
    {}
    SwFrameFormat* GetDestFormat( SwFrameFormat* pSrcFormat ) const
    {
        auto it = m_aSrcDestMap.find(pSrcFormat);
        return it == m_aSrcDestMap.end() ? nullptr : it->second;
    }

    void InsertSrcDest( SwFrameFormat const * pSrcFormat, SwFrameFormat* pDestFormat )
            {
                m_aSrcDestMap[pSrcFormat] = pDestFormat;
            }

    void ChgBox( SwTableBox* pBox )
    {
        m_rOldTable.GetTabSortBoxes().erase(pBox);
        m_pNewTableNode->GetTable().GetTabSortBoxes().insert(pBox);
    }
};

}

static void lcl_SplitTable_CpyBox( SwTableBox* pBox, SplitTable_Para* pPara );

static void lcl_SplitTable_CpyLine( SwTableLine* pLn, SplitTable_Para* pPara )
{
    SwFrameFormat *pSrcFormat = pLn->GetFrameFormat();
    SwTableLineFormat* pDestFormat = static_cast<SwTableLineFormat*>( pPara->GetDestFormat( pSrcFormat ) );
    if( pDestFormat == nullptr )
    {
        pPara->InsertSrcDest( pSrcFormat, pLn->ClaimFrameFormat() );
    }
    else
        pLn->ChgFrameFormat( pDestFormat );

    for( auto& rpBox : pLn->GetTabBoxes() )
        lcl_SplitTable_CpyBox(rpBox, pPara );
}

static void lcl_SplitTable_CpyBox( SwTableBox* pBox, SplitTable_Para* pPara )
{
    SwFrameFormat *pSrcFormat = pBox->GetFrameFormat();
    SwTableBoxFormat* pDestFormat = static_cast<SwTableBoxFormat*>(pPara->GetDestFormat( pSrcFormat ));
    if( pDestFormat == nullptr )
    {
        pPara->InsertSrcDest( pSrcFormat, pBox->ClaimFrameFormat() );
    }
    else
        pBox->ChgFrameFormat( pDestFormat );

    if( pBox->GetSttNd() )
        pPara->ChgBox( pBox );
    else
        for( SwTableLine* pLine : pBox->GetTabLines() )
            lcl_SplitTable_CpyLine( pLine, pPara );
}

SwTableNode* SwNodes::SplitTable( SwNode& rPos, bool bAfter,
                                    bool bCalcNewSize )
{
    SwNode* pNd = &rPos;
    SwTableNode* pTNd = pNd->FindTableNode();
    if( !pTNd || pNd->IsTableNode() )
        return nullptr;

    SwNodeOffset nSttIdx = pNd->FindTableBoxStartNode()->GetIndex();

    // Find this Box/top-level line
    SwTable& rTable = pTNd->GetTable();
    SwTableBox* pBox = rTable.GetTableBox( nSttIdx );
    if( !pBox )
        return nullptr;

    SwTableLine* pLine = pBox->GetUpper();
    while( pLine->GetUpper() )
        pLine = pLine->GetUpper()->GetUpper();

    // pLine now contains the top-level line
    sal_uInt16 nLinePos = rTable.GetTabLines().GetPos( pLine );
    if( USHRT_MAX == nLinePos ||
        ( bAfter ? ++nLinePos >= rTable.GetTabLines().size() : !nLinePos ))
        return nullptr; // Not found or last Line!

    // Find the first Box of the succeeding Line
    SwTableLine* pNextLine = rTable.GetTabLines()[ nLinePos ];
    pBox = pNextLine->GetTabBoxes()[0];
    while( !pBox->GetSttNd() )
        pBox = pBox->GetTabLines()[0]->GetTabBoxes()[0];

    // Insert an EndNode and TableNode into the Nodes Array
    SwTableNode * pNewTableNd;
    {
        SwEndNode* pOldTableEndNd = pTNd->EndOfSectionNode()->GetEndNode();
        assert(pOldTableEndNd && "Where is the EndNode?");

        new SwEndNode( *pBox->GetSttNd(), *pTNd );
        pNewTableNd = new SwTableNode( *pBox->GetSttNd() );
        pNewTableNd->GetTable().SetTableModel( rTable.IsNewModel() );

        pOldTableEndNd->m_pStartOfSection = pNewTableNd;
        pNewTableNd->m_pEndOfSection = pOldTableEndNd;

        SwNode* pBoxNd = const_cast<SwStartNode*>(pBox->GetSttNd()->GetStartNode());
        do {
            OSL_ENSURE( pBoxNd->IsStartNode(), "This needs to be a StartNode!" );
            pBoxNd->m_pStartOfSection = pNewTableNd;
            pBoxNd = (*this)[ pBoxNd->EndOfSectionIndex() + 1 ];
        } while( pBoxNd != pOldTableEndNd );
    }

    {
        // Move the Lines
        SwTable& rNewTable = pNewTableNd->GetTable();
        rNewTable.GetTabLines().insert( rNewTable.GetTabLines().begin(),
                      rTable.GetTabLines().begin() + nLinePos, rTable.GetTabLines().end() );

        /* From the back (bottom right) to the front (top left) deregister all Boxes from the
           Chart Data Provider. The Modify event is triggered in the calling function.
         TL_CHART2: */
        SwChartDataProvider *pPCD = rTable.GetFrameFormat()->getIDocumentChartDataProviderAccess().GetChartDataProvider();
        if( pPCD )
        {
            for (SwTableLines::size_type k = nLinePos;  k < rTable.GetTabLines().size(); ++k)
            {
                const SwTableLines::size_type nLineIdx = (rTable.GetTabLines().size() - 1) - k + nLinePos;
                const SwTableBoxes::size_type nBoxCnt = rTable.GetTabLines()[ nLineIdx ]->GetTabBoxes().size();
                for (SwTableBoxes::size_type j = 0;  j < nBoxCnt;  ++j)
                {
                    const SwTableBoxes::size_type nIdx = nBoxCnt - 1 - j;
                    pPCD->DeleteBox( &rTable, *rTable.GetTabLines()[ nLineIdx ]->GetTabBoxes()[nIdx] );
                }
            }
        }

        // Delete
        sal_uInt16 nDeleted = rTable.GetTabLines().size() - nLinePos;
        rTable.GetTabLines().erase( rTable.GetTabLines().begin() + nLinePos, rTable.GetTabLines().end() );

        // Move the affected Boxes. Make the Formats unique and correct the StartNodes
        SplitTable_Para aPara( pNewTableNd, rTable );
        for( SwTableLine* pNewLine : rNewTable.GetTabLines() )
            lcl_SplitTable_CpyLine( pNewLine, &aPara );
        rTable.CleanUpBottomRowSpan( nDeleted );
    }

    {
        // Copy the Table FrameFormat
        SwFrameFormat* pOldTableFormat = rTable.GetFrameFormat();
        SwFrameFormat* pNewTableFormat = pOldTableFormat->GetDoc().MakeTableFrameFormat(
                                pOldTableFormat->GetDoc().GetUniqueTableName(),
                                pOldTableFormat->GetDoc().GetDfltFrameFormat() );

        *pNewTableFormat = *pOldTableFormat;
        pNewTableNd->GetTable().RegisterToFormat( *pNewTableFormat );

        pNewTableNd->GetTable().SetTableStyleName(rTable.GetTableStyleName());

        // Calculate a new Size?
        // lcl_ChgTableSize: Only execute the second call if the first call was
        // successful, thus has an absolute Size
        if( bCalcNewSize && lcl_ChgTableSize( rTable ) )
            lcl_ChgTableSize( pNewTableNd->GetTable() );
    }

    // TL_CHART2: need to inform chart of probably changed cell names
    rTable.UpdateCharts();

    return pNewTableNd; // That's it!
}

/**
 * rPos needs to be in the Table that remains
 *
 * @param bWithPrev  merge the current Table with the preceding
 *                   or succeeding one
 */
bool SwDoc::MergeTable( const SwPosition& rPos, bool bWithPrev )
{
    SwTableNode* pTableNd = rPos.GetNode().FindTableNode(), *pDelTableNd;
    if( !pTableNd )
        return false;

    SwNodes& rNds = GetNodes();
    if( bWithPrev )
        pDelTableNd = rNds[ pTableNd->GetIndex() - 1 ]->FindTableNode();
    else
        pDelTableNd = rNds[ pTableNd->EndOfSectionIndex() + 1 ]->GetTableNode();
    if( !pDelTableNd )
        return false;

    if( dynamic_cast<const SwDDETable*>( &pTableNd->GetTable() ) !=  nullptr ||
        dynamic_cast<const SwDDETable*>( &pDelTableNd->GetTable() ) !=  nullptr)
        return false;

    // Delete HTML Layout
    pTableNd->GetTable().SetHTMLTableLayout(std::shared_ptr<SwHTMLTableLayout>());
    pDelTableNd->GetTable().SetHTMLTableLayout(std::shared_ptr<SwHTMLTableLayout>());

    // Both Tables are present; we can start
    SwUndoMergeTable* pUndo = nullptr;
    std::unique_ptr<SwHistory> pHistory;
    if (GetIDocumentUndoRedo().DoesUndo())
    {
        pUndo = new SwUndoMergeTable( *pTableNd, *pDelTableNd, bWithPrev );
        GetIDocumentUndoRedo().AppendUndo(std::unique_ptr<SwUndo>(pUndo));
        pHistory.reset(new SwHistory);
    }

    // Adapt all "TableFormulas"
    pTableNd->GetTable().Merge(pDelTableNd->GetTable(), pHistory.get());

    // The actual merge
    bool bRet = rNds.MergeTable( bWithPrev ? *pTableNd : *pDelTableNd, !bWithPrev );

    if( pHistory )
    {
        if( pHistory->Count() )
            pUndo->SaveFormula( *pHistory );
        pHistory.reset();
    }
    if( bRet )
    {
        if (SwFEShell* pFEShell = GetDocShell()->GetFEShell())
        {
            if (officecfg::Office::Writer::Table::Change::ApplyTableAutoFormat::get())
            {
                pFEShell->UpdateTableStyleFormatting();
            }
        }

        getIDocumentState().SetModified();
        getIDocumentFieldsAccess().SetFieldsDirty( true, nullptr, SwNodeOffset(0) );
    }
    return bRet;
}

bool SwNodes::MergeTable( SwNode& rPos, bool bWithPrev )
{
    SwTableNode* pDelTableNd = rPos.GetTableNode();
    OSL_ENSURE( pDelTableNd, "Where did the TableNode go?" );

    SwTableNode* pTableNd = (*this)[ rPos.GetIndex() - 1]->FindTableNode();
    OSL_ENSURE( pTableNd, "Where did the TableNode go?" );

    if( !pDelTableNd || !pTableNd )
        return false;

    pDelTableNd->DelFrames();

    SwTable& rDelTable = pDelTableNd->GetTable();
    SwTable& rTable = pTableNd->GetTable();

    // Find Lines for the Layout update
    FndBox_ aFndBox( nullptr, nullptr );
    aFndBox.SetTableLines( rTable );
    aFndBox.DelFrames( rTable );

    // TL_CHART2:
    // tell the charts about the table to be deleted and have them use their own data
    GetDoc().getIDocumentChartDataProviderAccess().CreateChartInternalDataProviders( &rDelTable );

    // Sync the TableFormat's Width
    {
        const SwFormatFrameSize& rTableSz = rTable.GetFrameFormat()->GetFrameSize();
        const SwFormatFrameSize& rDelTableSz = rDelTable.GetFrameFormat()->GetFrameSize();
        if( rTableSz != rDelTableSz )
        {
            // The needs correction
            if( bWithPrev )
                rDelTable.GetFrameFormat()->SetFormatAttr( rTableSz );
            else
                rTable.GetFrameFormat()->SetFormatAttr( rDelTableSz );
        }
    }

    if( !bWithPrev )
    {
        // Transfer all Attributes of the succeeding Table to the preceding one
        // We do this, because the succeeding one is deleted when deleting the Node
        rTable.SetRowsToRepeat( rDelTable.GetRowsToRepeat() );
        rTable.SetTableChgMode( rDelTable.GetTableChgMode() );

        rTable.GetFrameFormat()->LockModify();
        *rTable.GetFrameFormat() = *rDelTable.GetFrameFormat();
        // Also switch the Name
        rTable.GetFrameFormat()->SetFormatName( rDelTable.GetFrameFormat()->GetName() );
        rTable.GetFrameFormat()->UnlockModify();
    }

    // Move the Lines and Boxes
    SwTableLines::size_type nOldSize = rTable.GetTabLines().size();
    rTable.GetTabLines().insert( rTable.GetTabLines().begin() + nOldSize,
                               rDelTable.GetTabLines().begin(), rDelTable.GetTabLines().end() );
    rDelTable.GetTabLines().clear();

    rTable.GetTabSortBoxes().insert( rDelTable.GetTabSortBoxes() );
    rDelTable.GetTabSortBoxes().clear();

    // The preceding Table always remains, while the succeeding one is deleted
    SwEndNode* pTableEndNd = pDelTableNd->EndOfSectionNode();
    pTableNd->m_pEndOfSection = pTableEndNd;

    SwNodeIndex aIdx( *pDelTableNd, 1 );

    SwNode* pBoxNd = aIdx.GetNode().GetStartNode();
    do {
        OSL_ENSURE( pBoxNd->IsStartNode(), "This needs to be a StartNode!" );
        pBoxNd->m_pStartOfSection = pTableNd;
        pBoxNd = (*this)[ pBoxNd->EndOfSectionIndex() + 1 ];
    } while( pBoxNd != pTableEndNd );
    pBoxNd->m_pStartOfSection = pTableNd;

    aIdx -= SwNodeOffset(2);
    DelNodes( aIdx, SwNodeOffset(2) );

    // tweak the conditional styles at the first inserted Line
    const SwTableLine* pFirstLn = rTable.GetTabLines()[ nOldSize ];
    sw_LineSetHeadCondColl( pFirstLn );

    // Clean up the Borders
    if( nOldSize )
    {
        SwGCLineBorder aPara( rTable );
        aPara.nLinePos = --nOldSize;
        pFirstLn = rTable.GetTabLines()[ nOldSize ];
        sw_GC_Line_Border( pFirstLn, &aPara );
    }

    // Update Layout
    aFndBox.MakeFrames( rTable );

    return true;
}

namespace {

// Use the PtrArray's ForEach method
struct SetAFormatTabPara
{
    SwTableAutoFormat& rTableFormat;
    SwUndoTableAutoFormat* pUndo;
    sal_uInt16 nEndBox, nCurBox;
    sal_uInt8 nAFormatLine, nAFormatBox;
    bool bSingleRowTable;

    explicit SetAFormatTabPara( const SwTableAutoFormat& rNew )
        : rTableFormat( const_cast<SwTableAutoFormat&>(rNew) ), pUndo( nullptr ),
        nEndBox( 0 ), nCurBox( 0 ), nAFormatLine( 0 ), nAFormatBox( 0 ), bSingleRowTable(false)
    {}
};

}

// Forward declare so that the Lines and Boxes can use recursion
static bool lcl_SetAFormatBox(FndBox_ &, SetAFormatTabPara *pSetPara, bool bResetDirect);
static bool lcl_SetAFormatLine(FndLine_ &, SetAFormatTabPara *pPara, bool bResetDirect);

static bool lcl_SetAFormatLine(FndLine_ & rLine, SetAFormatTabPara *pPara, bool bResetDirect)
{
    for (auto const& it : rLine.GetBoxes())
    {
        lcl_SetAFormatBox(*it, pPara, bResetDirect);
    }
    return true;
}

static bool lcl_SetAFormatBox(FndBox_ & rBox, SetAFormatTabPara *pSetPara, bool bResetDirect)
{
    if (!rBox.GetUpper()->GetUpper()) // Box on first level?
    {
        if( !pSetPara->nCurBox )
            pSetPara->nAFormatBox = 0;
        else if( pSetPara->nCurBox == pSetPara->nEndBox )
            pSetPara->nAFormatBox = 3;
        else //Even column(1) or Odd column(2)
            pSetPara->nAFormatBox = static_cast<sal_uInt8>(1 + ((pSetPara->nCurBox-1) & 1));
    }

    if (rBox.GetBox()->GetSttNd())
    {
        SwTableBox* pSetBox = rBox.GetBox();
        if (!pSetBox->HasDirectFormatting() || bResetDirect)
        {
            if (bResetDirect)
                pSetBox->SetDirectFormatting(false);

            SwDoc& rDoc = pSetBox->GetFrameFormat()->GetDoc();
            SfxItemSet aCharSet(SfxItemSet::makeFixedSfxItemSet<RES_CHRATR_BEGIN, RES_PARATR_LIST_END-1>(rDoc.GetAttrPool()));
            SfxItemSet aBoxSet(rDoc.GetAttrPool(), aTableBoxSetRange);
            sal_uInt8 nPos = pSetPara->nAFormatLine * 4 + pSetPara->nAFormatBox;
            const bool bSingleRowTable = pSetPara->bSingleRowTable;
            const bool bSingleColTable = pSetPara->nEndBox == 0;
            pSetPara->rTableFormat.UpdateToSet(nPos, bSingleRowTable, bSingleColTable, aCharSet, SwTableAutoFormatUpdateFlags::Char, nullptr);
            pSetPara->rTableFormat.UpdateToSet(nPos, bSingleRowTable, bSingleColTable, aBoxSet, SwTableAutoFormatUpdateFlags::Box, rDoc.GetNumberFormatter());

            if (aCharSet.Count())
            {
                SwNodeOffset nSttNd = pSetBox->GetSttIdx()+1;
                SwNodeOffset nEndNd = pSetBox->GetSttNd()->EndOfSectionIndex();
                for (; nSttNd < nEndNd; ++nSttNd)
                {
                    SwContentNode* pNd = rDoc.GetNodes()[ nSttNd ]->GetContentNode();
                    if (pNd)
                        pNd->SetAttr(aCharSet);
                }
            }

            if (aBoxSet.Count())
            {
                if (pSetPara->pUndo && SfxItemState::SET == aBoxSet.GetItemState(RES_BOXATR_FORMAT))
                    pSetPara->pUndo->SaveBoxContent( *pSetBox );

                pSetBox->ClaimFrameFormat()->SetFormatAttr(aBoxSet);
            }
        }
    }
    else
    {
        // Not sure how this situation can occur, but apparently we have some kind of table in table.
        // I am guessing at how to best handle singlerow in this situation.
        const bool bOrigSingleRowTable = pSetPara->bSingleRowTable;
        pSetPara->bSingleRowTable = rBox.GetLines().size() == 1;
        for (auto const& rpFndLine : rBox.GetLines())
        {
            lcl_SetAFormatLine(*rpFndLine, pSetPara, bResetDirect);
        }
        pSetPara->bSingleRowTable = bOrigSingleRowTable;
    }

    if (!rBox.GetUpper()->GetUpper()) // a BaseLine
        ++pSetPara->nCurBox;
    return true;
}

bool SwDoc::SetTableAutoFormat(const SwSelBoxes& rBoxes,
        const SwTableAutoFormat& rNew, bool bResetDirect,
        TableStyleName const*const pStyleNameToSet)
{
    OSL_ENSURE( !rBoxes.empty(), "No valid Box list" );
    SwTableNode* pTableNd = const_cast<SwTableNode*>(rBoxes[0]->GetSttNd()->FindTableNode());
    if( !pTableNd )
        return false;

    // Find all Boxes/Lines
    FndBox_ aFndBox( nullptr, nullptr );
    {
        FndPara aPara( rBoxes, &aFndBox );
        ForEach_FndLineCopyCol( pTableNd->GetTable().GetTabLines(), &aPara );
    }
    if( aFndBox.GetLines().empty() )
        return false;

    SwTable &table = pTableNd->GetTable();
    table.SetHTMLTableLayout(std::shared_ptr<SwHTMLTableLayout>());

    FndBox_* pFndBox = &aFndBox;
    while( 1 == pFndBox->GetLines().size() &&
            1 == pFndBox->GetLines().front()->GetBoxes().size())
    {
        pFndBox = pFndBox->GetLines().front()->GetBoxes()[0].get();
    }

    if( pFndBox->GetLines().empty() ) // One too far? (only one sel. Box)
        pFndBox = pFndBox->GetUpper()->GetUpper();

    // Disable Undo, but first store parameters
    SwUndoTableAutoFormat* pUndo = nullptr;
    bool const bUndo(GetIDocumentUndoRedo().DoesUndo());
    if (bUndo)
    {
        pUndo = new SwUndoTableAutoFormat( *pTableNd, rNew );
        GetIDocumentUndoRedo().AppendUndo(std::unique_ptr<SwUndo>(pUndo));
        GetIDocumentUndoRedo().DoUndo(false);
    }

    if (pStyleNameToSet)
    {   // tdf#98226 do this here where undo can record it
        pTableNd->GetTable().SetTableStyleName(*pStyleNameToSet);
    }

    rNew.RestoreTableProperties(table);

    SetAFormatTabPara aPara( rNew );
    FndLines_t& rFLns = pFndBox->GetLines();
    aPara.bSingleRowTable = rFLns.size() == 1;

    for (FndLines_t::size_type n = 0; n < rFLns.size(); ++n)
    {
        FndLine_* pLine = rFLns[n].get();

        // Set Upper to 0 (thus simulate BaseLine)
        FndBox_* pSaveBox = pLine->GetUpper();
        pLine->SetUpper( nullptr );

        if( !n )
            aPara.nAFormatLine = 0;
        else if (static_cast<size_t>(n+1) == rFLns.size())
            aPara.nAFormatLine = 3;
        else
            aPara.nAFormatLine = static_cast<sal_uInt8>(1 + ((n-1) & 1 ));

        aPara.nAFormatBox = 0;
        aPara.nCurBox = 0;
        aPara.nEndBox = pLine->GetBoxes().size()-1;
        aPara.pUndo = pUndo;
        for (auto const& it : pLine->GetBoxes())
        {
            lcl_SetAFormatBox(*it, &aPara, bResetDirect);
        }

        pLine->SetUpper( pSaveBox );
    }

    if( pUndo )
    {
        GetIDocumentUndoRedo().DoUndo(bUndo);
    }

    getIDocumentState().SetModified();
    getIDocumentFieldsAccess().SetFieldsDirty( true, nullptr, SwNodeOffset(0) );

    return true;
}

/**
 * Find out who has the Attributes
 */
bool SwDoc::GetTableAutoFormat( const SwSelBoxes& rBoxes, SwTableAutoFormat& rGet )
{
    OSL_ENSURE( !rBoxes.empty(), "No valid Box list" );
    SwTableNode* pTableNd = const_cast<SwTableNode*>(rBoxes[0]->GetSttNd()->FindTableNode());
    if( !pTableNd )
        return false;

    // Find all Boxes/Lines
    FndBox_ aFndBox( nullptr, nullptr );
    {
        FndPara aPara( rBoxes, &aFndBox );
        ForEach_FndLineCopyCol( pTableNd->GetTable().GetTabLines(), &aPara );
    }
    if( aFndBox.GetLines().empty() )
        return false;

    // Store table properties
    SwTable &table = pTableNd->GetTable();
    rGet.StoreTableProperties(table);

    FndBox_* pFndBox = &aFndBox;
    while( 1 == pFndBox->GetLines().size() &&
            1 == pFndBox->GetLines().front()->GetBoxes().size())
    {
        pFndBox = pFndBox->GetLines().front()->GetBoxes()[0].get();
    }

    if( pFndBox->GetLines().empty() ) // One too far? (only one sel. Box)
        pFndBox = pFndBox->GetUpper()->GetUpper();

    FndLines_t& rFLns = pFndBox->GetLines();

    sal_uInt16 aLnArr[4];
    aLnArr[0] = 0;
    aLnArr[1] = 1 < rFLns.size() ? 1 : 0;
    aLnArr[2] = 2 < rFLns.size() ? 2 : aLnArr[1];
    aLnArr[3] = rFLns.size() - 1;

    for( sal_uInt8 nLine = 0; nLine < 4; ++nLine )
    {
        FndLine_& rLine = *rFLns[ aLnArr[ nLine ] ];

        sal_uInt16 aBoxArr[4];
        aBoxArr[0] = 0;
        aBoxArr[1] = 1 < rLine.GetBoxes().size() ? 1 : 0;
        aBoxArr[2] = 2 < rLine.GetBoxes().size() ? 2 : aBoxArr[1];
        aBoxArr[3] = rLine.GetBoxes().size() - 1;

        for( sal_uInt8 nBox = 0; nBox < 4; ++nBox )
        {
            SwTableBox* pFBox = rLine.GetBoxes()[ aBoxArr[ nBox ] ]->GetBox();
            // Always apply to the first ones
            while( !pFBox->GetSttNd() )
                pFBox = pFBox->GetTabLines()[0]->GetTabBoxes()[0];

            sal_uInt8 nPos = nLine * 4 + nBox;
            SwNodeIndex aIdx( *pFBox->GetSttNd(), 1 );
            SwContentNode* pCNd = aIdx.GetNode().GetContentNode();
            if( !pCNd )
                pCNd = SwNodes::GoNext(&aIdx);

            if( pCNd )
                rGet.UpdateFromSet( nPos, pCNd->GetSwAttrSet(),
                                    SwTableAutoFormatUpdateFlags::Char, nullptr );
            rGet.UpdateFromSet( nPos, pFBox->GetFrameFormat()->GetAttrSet(),
                                SwTableAutoFormatUpdateFlags::Box,
                                GetNumberFormatter() );
        }
    }

    return true;
}

SwTableAutoFormatTable& SwDoc::GetTableStyles()
{
    if (!m_pTableStyles)
        m_pTableStyles.reset(new SwTableAutoFormatTable(SwModule::get()->GetAutoFormatTable()));
    return *m_pTableStyles;
}

UIName SwDoc::GetUniqueTableName() const
{
    if( IsInMailMerge())
    {
        UIName newName( "MailMergeTable"
            + DateTimeToOUString( DateTime( DateTime::SYSTEM ) )
            + OUString::number( mpTableFrameFormatTable->size() + 1 ) );
        return newName;
    }

    const OUString aName(SwResId(STR_TABLE_DEFNAME));

    const size_t nFlagSize = ( mpTableFrameFormatTable->size() / 8 ) + 2;

    std::unique_ptr<sal_uInt8[]> pSetFlags( new sal_uInt8[ nFlagSize ] );
    memset( pSetFlags.get(), 0, nFlagSize );

    for( size_t n = 0; n < mpTableFrameFormatTable->size(); ++n )
    {
        const SwTableFormat* pFormat = (*mpTableFrameFormatTable)[ n ];
        if( !pFormat->IsDefault() && IsUsed( *pFormat ) &&
            pFormat->GetName().toString().startsWith( aName ) )
        {
            // Get number and set the Flag
            const sal_Int32 nNmLen = aName.getLength();
            size_t nNum = o3tl::toInt32(pFormat->GetName().toString().subView( nNmLen ));
            if( nNum-- && nNum < mpTableFrameFormatTable->size() )
                pSetFlags[ nNum / 8 ] |= (0x01 << ( nNum & 0x07 ));
        }
    }

    // All numbers are flagged properly, thus calculate the right number
    size_t nNum = mpTableFrameFormatTable->size();
    for( size_t n = 0; n < nFlagSize; ++n )
    {
        auto nTmp = pSetFlags[ n ];
        if( nTmp != 0xFF )
        {
            // Calculate the number
            nNum = n * 8;
            while( nTmp & 1 )
            {
                ++nNum;
                nTmp >>= 1;
            }
            break;
        }
    }

    return UIName(aName + OUString::number( ++nNum ));
}

SwTableFormat* SwDoc::FindTableFormatByName( const UIName& rName, bool bAll ) const
{
    const SwFormat* pRet = nullptr;
    if( bAll )
        pRet = mpTableFrameFormatTable->FindFormatByName( rName );
    else
    {
        auto [it, itEnd] = mpTableFrameFormatTable->findRangeByName(rName);
        // Only the ones set in the Doc
        for( ; it != itEnd; ++it )
        {
            const SwFrameFormat* pFormat = *it;
            if( !pFormat->IsDefault() && IsUsed( *pFormat ) &&
                pFormat->GetName() == rName )
            {
                pRet = pFormat;
                break;
            }
        }
    }
    return const_cast<SwTableFormat*>(static_cast<const SwTableFormat*>(pRet));
}

void SwDoc::SetColRowWidthHeight( SwTableBox& rCurrentBox, TableChgWidthHeightType eType,
                                    SwTwips nAbsDiff, SwTwips nRelDiff )
{
    SwTableNode* pTableNd = const_cast<SwTableNode*>(rCurrentBox.GetSttNd()->FindTableNode());
    std::unique_ptr<SwUndo> pUndo;

    pTableNd->GetTable().SwitchFormulasToInternalRepresentation();
    bool const bUndo(GetIDocumentUndoRedo().DoesUndo());
    bool bRet = false;
    switch( extractPosition(eType) )
    {
    case TableChgWidthHeightType::ColLeft:
    case TableChgWidthHeightType::ColRight:
    case TableChgWidthHeightType::CellLeft:
    case TableChgWidthHeightType::CellRight:
        {
             bRet = pTableNd->GetTable().SetColWidth( rCurrentBox,
                                eType, nAbsDiff, nRelDiff,
                                bUndo ? &pUndo : nullptr );
        }
        break;
    case TableChgWidthHeightType::RowBottom:
    case TableChgWidthHeightType::CellTop:
    case TableChgWidthHeightType::CellBottom:
        bRet = pTableNd->GetTable().SetRowHeight( rCurrentBox,
                            eType, nAbsDiff, nRelDiff,
                            bUndo ? &pUndo : nullptr );
        break;
    default: break;
    }

    GetIDocumentUndoRedo().DoUndo(bUndo); // SetColWidth can turn it off
    if( pUndo )
    {
        GetIDocumentUndoRedo().AppendUndo( std::move(pUndo) );
    }

    if( bRet )
    {
        getIDocumentState().SetModified();
    }
}

bool SwDoc::IsNumberFormat( const OUString& aString, sal_uInt32& F_Index, double& fOutNumber )
{
    if( aString.getLength() > 308 ) // optimization matches svl:IsNumberFormat arbitrary value
        return false;

    // remove any comment anchor marks
    return GetNumberFormatter()->IsNumberFormat(
        aString.replaceAll(OUStringChar(CH_TXTATR_INWORD), u""), F_Index, fOutNumber);
}

void SwDoc::ChkBoxNumFormat( SwTableBox& rBox, bool bCallUpdate )
{
    // Optimization: If the Box says it's Text, it remains Text
    const SwTableBoxNumFormat* pNumFormatItem = rBox.GetFrameFormat()->GetItemIfSet( RES_BOXATR_FORMAT,
        false );
    if (pNumFormatItem && GetNumberFormatter()->IsTextFormat(pNumFormatItem->GetValue()))
        return ;

    std::unique_ptr<SwUndoTableNumFormat> pUndo;

    bool bIsEmptyTextNd;
    bool bChgd = true;
    sal_uInt32 nFormatIdx;
    double fNumber;
    if( rBox.HasNumContent( fNumber, nFormatIdx, bIsEmptyTextNd ) )
    {
        if( !rBox.IsNumberChanged() )
            bChgd = false;
        else
        {
            if (GetIDocumentUndoRedo().DoesUndo())
            {
                GetIDocumentUndoRedo().StartUndo( SwUndoId::TABLE_AUTOFMT, nullptr );
                pUndo.reset(new SwUndoTableNumFormat( rBox ));
                pUndo->SetNumFormat( nFormatIdx, fNumber );
            }

            SwTableBoxFormat* pBoxFormat = rBox.GetFrameFormat();
            SfxItemSet aBoxSet(SfxItemSet::makeFixedSfxItemSet<RES_BOXATR_FORMAT, RES_BOXATR_VALUE>(GetAttrPool()));

            bool bLockModify = true;
            bool bSetNumberFormat = IsInsTableFormatNum();
            const bool bForceNumberFormat = IsInsTableFormatNum() && IsInsTableChangeNumFormat();

            // if the user forced a number format in this cell previously,
            // keep it, unless the user set that she wants the full number
            // format recognition
            if( pNumFormatItem && !bForceNumberFormat )
            {
                sal_uLong nOldNumFormat = pNumFormatItem->GetValue();
                SvNumberFormatter* pNumFormatr = GetNumberFormatter();

                SvNumFormatType nFormatType = pNumFormatr->GetType( nFormatIdx );
                if( nFormatType == pNumFormatr->GetType( nOldNumFormat ) || SvNumFormatType::NUMBER == nFormatType )
                {
                    // Current and specified NumFormat match
                    // -> keep old Format
                    nFormatIdx = nOldNumFormat;
                    bSetNumberFormat = true;
                }
                else
                {
                    // Current and specified NumFormat do not match
                    // -> insert as Text
                    bLockModify = bSetNumberFormat = false;
                }
            }

            if( bSetNumberFormat || bForceNumberFormat )
            {
                pBoxFormat = rBox.ClaimFrameFormat();

                aBoxSet.Put( SwTableBoxValue( fNumber ));
                aBoxSet.Put( SwTableBoxNumFormat( nFormatIdx ));
            }

            // It's not enough to only reset the Formula.
            // Make sure that the Text is formatted accordingly
            if( !bSetNumberFormat && !bIsEmptyTextNd && pNumFormatItem )
            {
                // Just resetting Attributes is not enough
                // Make sure that the Text is formatted accordingly
                pBoxFormat->SetFormatAttr( *GetDfltAttr( RES_BOXATR_FORMAT ));
            }

            if( bLockModify ) pBoxFormat->LockModify();
            pBoxFormat->ResetFormatAttr( RES_BOXATR_FORMAT, RES_BOXATR_VALUE );
            if( bLockModify ) pBoxFormat->UnlockModify();

            if( bSetNumberFormat )
                pBoxFormat->SetFormatAttr( aBoxSet );
        }
    }
    else
    {
        // It's not a number
        SwTableBoxFormat* pBoxFormat = rBox.GetFrameFormat();
        if( SfxItemState::SET == pBoxFormat->GetItemState( RES_BOXATR_FORMAT, false ) ||
            SfxItemState::SET == pBoxFormat->GetItemState( RES_BOXATR_VALUE, false ) )
        {
            if (GetIDocumentUndoRedo().DoesUndo())
            {
                GetIDocumentUndoRedo().StartUndo( SwUndoId::TABLE_AUTOFMT, nullptr );
                pUndo.reset(new SwUndoTableNumFormat( rBox ));
            }

            pBoxFormat = rBox.ClaimFrameFormat();

            // Remove all number formats
            sal_uInt16 nWhich1 = RES_BOXATR_FORMULA;
            if( !bIsEmptyTextNd )
            {
                nWhich1 = RES_BOXATR_FORMAT;

                // Just resetting Attributes is not enough
                // Make sure that the Text is formatted accordingly
                pBoxFormat->SetFormatAttr( *GetDfltAttr( nWhich1 ));
            }
            pBoxFormat->ResetFormatAttr( nWhich1, RES_BOXATR_VALUE );
        }
        else
            bChgd = false;
    }

    if( bChgd && pUndo )
    {
        pUndo->SetBox( rBox );
        GetIDocumentUndoRedo().AppendUndo(std::move(pUndo));
        GetIDocumentUndoRedo().EndUndo( SwUndoId::END, nullptr );
    }

    const SwTableNode* pTableNd = rBox.GetSttNd()->FindTableNode();
    if( bCallUpdate )
    {
        getIDocumentFieldsAccess().UpdateTableFields(&pTableNd->GetTable());

        // TL_CHART2: update charts (when cursor leaves cell and
        // automatic update is enabled)
        if (AUTOUPD_FIELD_AND_CHARTS == GetDocumentSettingManager().getFieldUpdateFlags(true))
            pTableNd->GetTable().UpdateCharts();
    }
    if( bChgd )
        getIDocumentState().SetModified();
}

void SwDoc::SetTableBoxFormulaAttrs( SwTableBox& rBox, const SfxItemSet& rSet )
{
    if (GetIDocumentUndoRedo().DoesUndo())
    {
        GetIDocumentUndoRedo().AppendUndo( std::make_unique<SwUndoTableNumFormat>(rBox, &rSet) );
    }

    SwFrameFormat* pBoxFormat = rBox.ClaimFrameFormat();
    if( SfxItemState::SET == rSet.GetItemState( RES_BOXATR_FORMULA ))
    {
        pBoxFormat->LockModify();
        pBoxFormat->ResetFormatAttr( RES_BOXATR_VALUE );
        pBoxFormat->UnlockModify();
    }
    else if( SfxItemState::SET == rSet.GetItemState( RES_BOXATR_VALUE ))
    {
        pBoxFormat->LockModify();
        pBoxFormat->ResetFormatAttr( RES_BOXATR_FORMULA );
        pBoxFormat->UnlockModify();
    }
    pBoxFormat->SetFormatAttr( rSet );
    getIDocumentState().SetModified();
}

void SwDoc::ClearLineNumAttrs( SwPosition const & rPos )
{
    SwPaM aPam(rPos);
    aPam.Move(fnMoveBackward);
    SwContentNode *pNode = aPam.GetPointContentNode();
    if ( nullptr == pNode )
        return ;
    if( !pNode->IsTextNode() )
        return;

    SwTextNode * pTextNode = pNode->GetTextNode();
    if (!(pTextNode && pTextNode->IsNumbered()
        && pTextNode->GetText().isEmpty()))
        return;

    SfxItemSet rSet(SfxItemSet::makeFixedSfxItemSet<RES_PARATR_BEGIN, RES_PARATR_END - 1>(pTextNode->GetDoc().GetAttrPool()));
    pTextNode->SwContentNode::GetAttr( rSet );
    const SfxStringItem* pFormatItem = rSet.GetItemIfSet( RES_PARATR_NUMRULE, false );
    if ( !pFormatItem )
        return;

    SwUndoDelNum * pUndo;
    if( GetIDocumentUndoRedo().DoesUndo() )
    {
        GetIDocumentUndoRedo().ClearRedo();
        pUndo = new SwUndoDelNum( aPam );
        GetIDocumentUndoRedo().AppendUndo( std::unique_ptr<SwUndo>(pUndo) );
    }
    else
        pUndo = nullptr;
    SwRegHistory aRegH( pUndo ? pUndo->GetHistory() : nullptr );
    aRegH.RegisterInModify( pTextNode , *pTextNode );
    if ( pUndo )
        pUndo->AddNode( *pTextNode );
    std::unique_ptr<SfxStringItem> pNewItem(pFormatItem->Clone());
    pNewItem->SetValue(OUString());
    rSet.Put( std::move(pNewItem) );
    pTextNode->SetAttr( rSet );
}

void SwDoc::ClearBoxNumAttrs( SwNode& rNode )
{
    SwStartNode* pSttNd = rNode.FindStartNodeByType( SwTableBoxStartNode );
    if( nullptr == pSttNd ||
        SwNodeOffset(2) != pSttNd->EndOfSectionIndex() - pSttNd->GetIndex())
        return;

    SwTableBox* pBox = pSttNd->FindTableNode()->GetTable().
                        GetTableBox( pSttNd->GetIndex() );

    const SfxItemSet& rSet = pBox->GetFrameFormat()->GetAttrSet();
    const SwTableBoxNumFormat* pFormatItem = rSet.GetItemIfSet( RES_BOXATR_FORMAT, false );
    if( !pFormatItem ||
        SfxItemState::SET == rSet.GetItemState( RES_BOXATR_FORMULA, false ) ||
        SfxItemState::SET == rSet.GetItemState( RES_BOXATR_VALUE, false ))
        return;

    if (GetIDocumentUndoRedo().DoesUndo())
    {
        GetIDocumentUndoRedo().AppendUndo(std::make_unique<SwUndoTableNumFormat>(*pBox));
    }

    SwFrameFormat* pBoxFormat = pBox->ClaimFrameFormat();

    // Keep TextFormats!
    sal_uInt16 nWhich1 = RES_BOXATR_FORMAT;
    if( GetNumberFormatter()->IsTextFormat(
            pFormatItem->GetValue() ))
        nWhich1 = RES_BOXATR_FORMULA;
    else
        // Just resetting Attributes is not enough
        // Make sure that the Text is formatted accordingly
        pBoxFormat->SetFormatAttr( *GetDfltAttr( RES_BOXATR_FORMAT ));

    pBoxFormat->ResetFormatAttr( nWhich1, RES_BOXATR_VALUE );
    getIDocumentState().SetModified();
}

/**
 * Copies a Table from the same or another Doc into itself
 * We create a new Table or an existing one is filled with the Content.
 * We either fill in the Content from a certain Box or a certain TableSelection
 *
 * This method is called by edglss.cxx/fecopy.cxx
 */
bool SwDoc::InsCopyOfTable( SwPosition& rInsPos, const SwSelBoxes& rBoxes,
                        const SwTable* pCpyTable, bool bCpyName, bool bCorrPos, const TableStyleName& rStyleName )
{
    bool bRet;

    const SwTableNode* pSrcTableNd = pCpyTable
            ? pCpyTable->GetTableNode()
            : rBoxes[ 0 ]->GetSttNd()->FindTableNode();

    SwTableNode * pInsTableNd = rInsPos.GetNode().FindTableNode();

    bool const bUndo( GetIDocumentUndoRedo().DoesUndo() );
    if( !pCpyTable && !pInsTableNd )
    {
        std::unique_ptr<SwUndoCpyTable> pUndo;
        if (bUndo)
        {
            GetIDocumentUndoRedo().ClearRedo();
            pUndo.reset(new SwUndoCpyTable(*this));
        }

        {
            ::sw::UndoGuard const undoGuard(GetIDocumentUndoRedo());
            bRet = pSrcTableNd->GetTable().MakeCopy( *this, rInsPos, rBoxes,
                                                bCpyName, rStyleName );
        }

        if( pUndo && bRet )
        {
            pInsTableNd = GetNodes()[ rInsPos.GetNodeIndex() - 1 ]->FindTableNode();

            pUndo->SetTableSttIdx( pInsTableNd->GetIndex() );
            GetIDocumentUndoRedo().AppendUndo( std::move(pUndo) );
        }
    }
    else
    {
        RedlineFlags eOld = getIDocumentRedlineAccess().GetRedlineFlags();
        if( getIDocumentRedlineAccess().IsRedlineOn() )
            getIDocumentRedlineAccess().SetRedlineFlags( RedlineFlags::On |
                                  RedlineFlags::ShowInsert |
                                  RedlineFlags::ShowDelete );

        std::unique_ptr<SwUndoTableCpyTable> pUndo;
        if (bUndo)
        {
            GetIDocumentUndoRedo().ClearRedo();
            pUndo.reset(new SwUndoTableCpyTable(*this));
            GetIDocumentUndoRedo().DoUndo(false);
        }

        rtl::Reference<SwDoc> xCpyDoc(&const_cast<SwDoc&>(pSrcTableNd->GetDoc()));
        bool bDelCpyDoc = xCpyDoc == this;

        if( bDelCpyDoc )
        {
            // Copy the Table into a temporary Doc
            xCpyDoc = new SwDoc;

            SwPosition aPos( xCpyDoc->GetNodes().GetEndOfContent() );
            if( !pSrcTableNd->GetTable().MakeCopy( *xCpyDoc, aPos, rBoxes, true ))
            {
                xCpyDoc.clear();

                if( pUndo )
                {
                    GetIDocumentUndoRedo().DoUndo(bUndo);
                }
                return false;
            }
            aPos.Adjust(SwNodeOffset(-1)); // Set to the Table's EndNode
            pSrcTableNd = aPos.GetNode().FindTableNode();
        }

        const SwStartNode* pSttNd = rInsPos.GetNode().FindTableBoxStartNode();

        rInsPos.nContent.Assign( nullptr, 0 );

        // no complex into complex, but copy into or from new model is welcome
        if( ( !pSrcTableNd->GetTable().IsTableComplex() || pInsTableNd->GetTable().IsNewModel() )
            && ( bDelCpyDoc || !rBoxes.empty() ) )
        {
            // Copy the Table "relatively"
            const SwSelBoxes* pBoxes;
            SwSelBoxes aBoxes;

            if( bDelCpyDoc )
            {
                SwTableBox* pBox = pInsTableNd->GetTable().GetTableBox(
                                        pSttNd->GetIndex() );
                OSL_ENSURE( pBox, "Box is not in this Table" );
                aBoxes.insert( pBox );
                pBoxes = &aBoxes;
            }
            else
                pBoxes = &rBoxes;

            // Copy Table to the selected Lines
            bRet = pInsTableNd->GetTable().InsTable( pSrcTableNd->GetTable(),
                                                        *pBoxes, pUndo.get() );
        }
        else
        {
            SwNodeIndex aNdIdx( *pSttNd, 1 );
            bRet = pInsTableNd->GetTable().InsTable( pSrcTableNd->GetTable(),
                                                    aNdIdx, pUndo.get() );
        }

        xCpyDoc.clear();

        if( pUndo )
        {
            // If the Table could not be copied, delete the Undo object
            GetIDocumentUndoRedo().DoUndo(bUndo);
            if( bRet || !pUndo->IsEmpty() )
            {
                GetIDocumentUndoRedo().AppendUndo(std::move(pUndo));
            }
        }

        if( bCorrPos )
        {
            rInsPos.Assign( *pSttNd );
            SwNodes::GoNext(&rInsPos);
        }
        getIDocumentRedlineAccess().SetRedlineFlags( eOld );
    }

    if( bRet )
    {
        getIDocumentState().SetModified();
        getIDocumentFieldsAccess().SetFieldsDirty( true, nullptr, SwNodeOffset(0) );
    }
    return bRet;
}

bool SwDoc::UnProtectTableCells( SwTable& rTable )
{
    bool bChgd = false;
    std::unique_ptr<SwUndoAttrTable> pUndo;
    if (GetIDocumentUndoRedo().DoesUndo())
        pUndo.reset(new SwUndoAttrTable( *rTable.GetTableNode() ));

    SwTableSortBoxes& rSrtBox = rTable.GetTabSortBoxes();
    for (size_t i = rSrtBox.size(); i; )
    {
        SwFrameFormat *pBoxFormat = rSrtBox[ --i ]->GetFrameFormat();
        if( pBoxFormat->GetProtect().IsContentProtected() )
        {
            pBoxFormat->ResetFormatAttr( RES_PROTECT );
            bChgd = true;
        }
    }

    if( pUndo && bChgd )
        GetIDocumentUndoRedo().AppendUndo( std::move(pUndo) );
    return bChgd;
}

void SwDoc::UnProtectCells( const UIName& rName )
{
    SwTableFormat* pFormat = FindTableFormatByName( rName );
    if( pFormat )
    {
        bool bChgd = UnProtectTableCells( *SwTable::FindTable( pFormat ) );
        if( bChgd )
            getIDocumentState().SetModified();
    }
}

bool SwDoc::UnProtectCells( const SwSelBoxes& rBoxes )
{
    bool bChgd = false;
    if( !rBoxes.empty() )
    {
        std::unique_ptr<SwUndoAttrTable> pUndo;
        if (GetIDocumentUndoRedo().DoesUndo())
            pUndo.reset(new SwUndoAttrTable( *rBoxes[0]->GetSttNd()->FindTableNode() ));

        std::map<SwFrameFormat*, SwTableBoxFormat*> aFormatsMap;
        for (size_t i = rBoxes.size(); i; )
        {
            SwTableBox* pBox = rBoxes[ --i ];
            SwFrameFormat* pBoxFormat = pBox->GetFrameFormat();
            if( pBoxFormat->GetProtect().IsContentProtected() )
            {
                std::map<SwFrameFormat*, SwTableBoxFormat*>::const_iterator const it =
                    aFormatsMap.find(pBoxFormat);
                if (aFormatsMap.end() != it)
                    pBox->ChgFrameFormat(it->second);
                else
                {
                    SwTableBoxFormat *const pNewBoxFormat(pBox->ClaimFrameFormat());
                    pNewBoxFormat->ResetFormatAttr( RES_PROTECT );
                    aFormatsMap.insert(std::make_pair(pBoxFormat, pNewBoxFormat));
                }
                bChgd = true;
            }
        }

        if( pUndo && bChgd )
            GetIDocumentUndoRedo().AppendUndo( std::move(pUndo) );
    }
    return bChgd;
}

void SwDoc::UnProtectTables( const SwPaM& rPam )
{
    GetIDocumentUndoRedo().StartUndo(SwUndoId::EMPTY, nullptr);

    bool bChgd = false, bHasSel = rPam.HasMark() ||
                                    rPam.GetNext() != &rPam;
    sw::TableFrameFormats& rFormats = *GetTableFrameFormats();
    SwTable* pTable;
    const SwTableNode* pTableNd;
    for( auto n = rFormats.size(); n ; )
        if( nullptr != (pTable = SwTable::FindTable( rFormats[ --n ])) &&
            nullptr != (pTableNd = pTable->GetTableNode() ) &&
            pTableNd->GetNodes().IsDocNodes() )
        {
            SwNodeOffset nTableIdx = pTableNd->GetIndex();

            // Check whether the Table is within the Selection
            if( bHasSel )
            {
                bool bFound = false;
                SwPaM* pTmp = const_cast<SwPaM*>(&rPam);
                do {
                    auto [pStart, pEnd] = pTmp->StartEnd(); // SwPosition*
                    bFound = pStart->GetNodeIndex() < nTableIdx &&
                            nTableIdx < pEnd->GetNodeIndex();

                } while( !bFound && &rPam != ( pTmp = pTmp->GetNext() ) );
                if( !bFound )
                    continue; // Continue searching
            }

            // Lift the protection
            bChgd |= UnProtectTableCells( *pTable );
        }

    GetIDocumentUndoRedo().EndUndo(SwUndoId::EMPTY, nullptr);
    if( bChgd )
        getIDocumentState().SetModified();
}

bool SwDoc::HasTableAnyProtection( const SwPosition* pPos,
                                 const UIName* pTableName,
                                 bool* pFullTableProtection )
{
    bool bHasProtection = false;
    SwTable* pTable = nullptr;
    if( pTableName )
        pTable = SwTable::FindTable( FindTableFormatByName( *pTableName ) );
    else if( pPos )
    {
        SwTableNode* pTableNd = pPos->GetNode().FindTableNode();
        if( pTableNd )
            pTable = &pTableNd->GetTable();
    }

    if( pTable )
    {
        SwTableSortBoxes& rSrtBox = pTable->GetTabSortBoxes();
        for (size_t i = rSrtBox.size(); i; )
        {
            SwFrameFormat *pBoxFormat = rSrtBox[ --i ]->GetFrameFormat();
            if( pBoxFormat->GetProtect().IsContentProtected() )
            {
                if( !bHasProtection )
                {
                    bHasProtection = true;
                    if( !pFullTableProtection )
                        break;
                    *pFullTableProtection = true;
                }
            }
            else if( bHasProtection && pFullTableProtection )
            {
                *pFullTableProtection = false;
                break;
            }
        }
    }
    return bHasProtection;
}

SwTableAutoFormat* SwDoc::MakeTableStyle(const TableStyleName& rName)
{
    SwTableAutoFormat aTableFormat(rName);
    GetTableStyles().AddAutoFormat(aTableFormat);
    SwTableAutoFormat* pTableFormat = GetTableStyles().FindAutoFormat(rName);

    getIDocumentState().SetModified();

    if (GetIDocumentUndoRedo().DoesUndo())
    {
        GetIDocumentUndoRedo().AppendUndo(
            std::make_unique<SwUndoTableStyleMake>(rName, *this));
    }

    return pTableFormat;
}

std::unique_ptr<SwTableAutoFormat> SwDoc::DelTableStyle(const TableStyleName& rName, bool bBroadcast)
{
    if (bBroadcast)
        BroadcastStyleOperation(UIName(rName.toString()), SfxStyleFamily::Table, SfxHintId::StyleSheetErased);

    std::unique_ptr<SwTableAutoFormat> pReleasedFormat = GetTableStyles().ReleaseAutoFormat(rName);

    std::vector<SwTable*> vAffectedTables;
    if (pReleasedFormat)
    {
        size_t nTableCount = GetTableFrameFormatCount(true);
        for (size_t i=0; i < nTableCount; ++i)
        {
            SwFrameFormat* pFrameFormat = &GetTableFrameFormat(i, true);
            SwTable* pTable = SwTable::FindTable(pFrameFormat);
            if (pTable->GetTableStyleName() == pReleasedFormat->GetName())
            {
                pTable->SetTableStyleName(TableStyleName());
                vAffectedTables.push_back(pTable);
            }
        }

        getIDocumentState().SetModified();

        if (GetIDocumentUndoRedo().DoesUndo())
        {
            GetIDocumentUndoRedo().AppendUndo(
                std::make_unique<SwUndoTableStyleDelete>(std::move(pReleasedFormat), std::move(vAffectedTables), *this));
        }
    }

    return pReleasedFormat;
}

void SwDoc::ChgTableStyle(const TableStyleName& rName, const SwTableAutoFormat& rNewFormat)
{
    SwTableAutoFormat* pFormat = GetTableStyles().FindAutoFormat(rName);
    if (!pFormat)
        return;

    SwTableAutoFormat aOldFormat = *pFormat;
    *pFormat = rNewFormat;
    pFormat->SetName(rName);

    size_t nTableCount = GetTableFrameFormatCount(true);
    for (size_t i=0; i < nTableCount; ++i)
    {
        SwFrameFormat* pFrameFormat = &GetTableFrameFormat(i, true);
        SwTable* pTable = SwTable::FindTable(pFrameFormat);
        if (pTable->GetTableStyleName() == rName)
            if (SwFEShell* pFEShell = GetDocShell()->GetFEShell())
                pFEShell->UpdateTableStyleFormatting(pTable->GetTableNode());
    }

    getIDocumentState().SetModified();

    if (GetIDocumentUndoRedo().DoesUndo())
    {
        GetIDocumentUndoRedo().AppendUndo(
            std::make_unique<SwUndoTableStyleUpdate>(*pFormat, aOldFormat, *this));
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
