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

#include <hintids.hxx>
#include <svl/imageitm.hxx>
#include <svl/numformat.hxx>
#include <svl/zforlist.hxx>
#include <svl/stritem.hxx>
#include <svl/whiter.hxx>
#include <unotools/moduleoptions.hxx>
#include <editeng/lrspitem.hxx>
#include <editeng/ulspitem.hxx>
#include <editeng/brushitem.hxx>
#include <editeng/boxitem.hxx>
#include <editeng/shaditem.hxx>
#include <editeng/spltitem.hxx>
#include <editeng/keepitem.hxx>
#include <editeng/lineitem.hxx>
#include <editeng/colritem.hxx>
#include <editeng/frmdiritem.hxx>
#include <svx/numinf.hxx>
#include <svx/svddef.hxx>
#include <svx/svxdlg.hxx>
#include <sfx2/bindings.hxx>
#include <vcl/weld.hxx>
#include <sfx2/request.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/objface.hxx>
#include <sfx2/viewfrm.hxx>
#include <vcl/EnumContext.hxx>
#include <o3tl/enumrange.hxx>
#include <comphelper/lok.hxx>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <editeng/itemtype.hxx>
#include <osl/diagnose.h>

#include <fmtornt.hxx>
#include <fmtlsplt.hxx>
#include <fmtrowsplt.hxx>
#include <fmtfsize.hxx>
#include <swmodule.hxx>
#include <wrtsh.hxx>
#include <rootfrm.hxx>
#include <wview.hxx>
#include <frmatr.hxx>
#include <uitool.hxx>
#include <inputwin.hxx>
#include <uiitems.hxx>
#include <tabsh.hxx>
#include <swtablerep.hxx>
#include <tablemgr.hxx>
#include <cellatr.hxx>
#include <frmfmt.hxx>
#include <swundo.hxx>
#include <swtable.hxx>
#include <docsh.hxx>
#include <tblsel.hxx>
#include <viewopt.hxx>
#include <tabfrm.hxx>
#include <frame.hxx>
#include <pagefrm.hxx>
#include <cntfrm.hxx>

#include <strings.hrc>
#include <cmdid.h>
#include <unobaseclass.hxx>

#define ShellClass_SwTableShell
#include <sfx2/msg.hxx>
#include <swslots.hxx>

#include <swabstdlg.hxx>

#include <memory>

using ::editeng::SvxBorderLine;
using namespace ::com::sun::star;

SFX_IMPL_INTERFACE(SwTableShell, SwBaseShell)

void SwTableShell::InitInterface_Impl()
{
    GetStaticInterface()->RegisterPopupMenu(u"table"_ustr);
    GetStaticInterface()->RegisterObjectBar(SFX_OBJECTBAR_OBJECT, SfxVisibilityFlags::Invisible, ToolbarId::Table_Toolbox);
}


const WhichRangesContainer aUITableAttrRange(svl::Items<
    RES_LR_SPACE,                   RES_UL_SPACE,
    RES_PAGEDESC,                   RES_BREAK,
    RES_BACKGROUND,                 RES_BACKGROUND,
    RES_BOX,                        RES_SHADOW,
    RES_KEEP,                       RES_KEEP,
    RES_LAYOUT_SPLIT,               RES_LAYOUT_SPLIT,
    RES_FRAMEDIR,                   RES_FRAMEDIR,
    RES_ROW_SPLIT,                  RES_ROW_SPLIT,
// #i29550#
    RES_COLLAPSING_BORDERS,         RES_COLLAPSING_BORDERS,
// <-- collapsing borders
    XATTR_FILL_FIRST,               XATTR_FILL_LAST,
    SID_ATTR_BORDER_INNER,          SID_ATTR_BORDER_SHADOW,
    SID_RULER_BORDERS,              SID_RULER_BORDERS,
    SID_ATTR_BRUSH_ROW,             SID_ATTR_BRUSH_TABLE, // ??? This is very strange range
//  SID_BACKGRND_DESTINATION,       SID_BACKGRND_DESTINATION, // included into above
//  SID_HTML_MODE,                  SID_HTML_MODE, // included into above
    FN_TABLE_REP,                   FN_TABLE_REP,
    FN_TABLE_SET_VERT_ALIGN,        FN_TABLE_SET_VERT_ALIGN,
    FN_TABLE_BOX_TEXTORIENTATION,   FN_TABLE_BOX_TEXTORIENTATION,
    FN_PARAM_TABLE_NAME,            FN_PARAM_TABLE_NAME,
    FN_PARAM_TABLE_HEADLINE,        FN_PARAM_TABLE_HEADLINE
>);

const WhichRangesContainer& SwuiGetUITableAttrRange()
{
    return aUITableAttrRange;
}

static void lcl_SetAttr( SwWrtShell &rSh, const SfxPoolItem &rItem )
{
    SfxItemSet aSet( rSh.GetView().GetPool(), rItem.Which(), rItem.Which());
    aSet.Put( rItem );
    rSh.SetTableAttr( aSet );
}

static std::shared_ptr<SwTableRep> lcl_TableParamToItemSet( SfxItemSet& rSet, SwWrtShell &rSh )
{
    std::shared_ptr<SwTableRep> pRep;

    SwFrameFormat *pFormat = rSh.GetTableFormat();
    SwTabCols aCols;
    rSh.GetTabCols( aCols );

    //At first get the simple attributes.
    rSet.Put( SfxStringItem( FN_PARAM_TABLE_NAME, pFormat->GetName().toString()));
    rSet.Put( SfxUInt16Item( FN_PARAM_TABLE_HEADLINE, rSh.GetRowsToRepeat() ) );
    rSet.Put( pFormat->GetShadow() );
    rSet.Put(SfxUInt16Item(FN_TABLE_SET_VERT_ALIGN, rSh.GetBoxAlign()));
    rSet.Put( pFormat->GetFrameDir() );

    SvxULSpaceItem aULSpace( pFormat->GetULSpace() );
    rSet.Put( aULSpace );

    const sal_uInt16  nBackgroundDestination = rSh.GetViewOptions()->GetTableDest();
    rSet.Put(SfxUInt16Item(SID_BACKGRND_DESTINATION, nBackgroundDestination ));
    std::unique_ptr<SvxBrushItem> aBrush(std::make_unique<SvxBrushItem>(RES_BACKGROUND));
    if(rSh.GetRowBackground(aBrush))
    {
        aBrush->SetWhich(SID_ATTR_BRUSH_ROW);
        rSet.Put( *aBrush );
    }
    else
        rSet.InvalidateItem(SID_ATTR_BRUSH_ROW);
    rSh.GetTabBackground(aBrush);
    aBrush->SetWhich(SID_ATTR_BRUSH_TABLE);
    rSet.Put( *aBrush );

    // text direction in boxes
    std::unique_ptr<SvxFrameDirectionItem> aBoxDirection(std::make_unique<SvxFrameDirectionItem>(SvxFrameDirection::Environment, RES_FRAMEDIR));
    if(rSh.GetBoxDirection( aBoxDirection ))
    {
        aBoxDirection->SetWhich(FN_TABLE_BOX_TEXTORIENTATION);
        rSet.Put(*aBoxDirection);
    }

    bool bSelectAll = rSh.StartsWith_() == SwCursorShell::StartsWith::Table && rSh.ExtendedSelectedAll();
    bool bTableSel = rSh.IsTableMode() || bSelectAll;
    if(!bTableSel)
    {
        rSh.StartAllAction();
        rSh.Push();
        rSh.GetView().GetViewFrame().GetDispatcher()->Execute( FN_TABLE_SELECT_ALL );
    }
    SvxBoxInfoItem aBoxInfo( SID_ATTR_BORDER_INNER );

    // Table variant: If multiple table cells are selected.
    rSh.GetCursor();                  //Thus GetCursorCnt() returns the right thing
    aBoxInfo.SetTable          ((rSh.IsTableMode() && rSh.GetCursorCnt() > 1) ||
                                    !bTableSel);
    // Always show distance field.
    aBoxInfo.SetDist           (true);
    // Set minimum size in tables and paragraphs.
    aBoxInfo.SetMinDist( !bTableSel || rSh.IsTableMode() ||
                            rSh.GetSelectionType() &
                            (SelectionType::Text | SelectionType::Table));
    // Always set the default spacing.
    aBoxInfo.SetDefDist        (MIN_BORDER_DIST);
    // Individual lines can have DontCare status only in tables.
    aBoxInfo.SetValid( SvxBoxInfoItemValidFlags::DISABLE, !bTableSel || !rSh.IsTableMode() );

    rSet.Put(aBoxInfo);
    rSh.GetTabBorders( rSet );

    //row split
    std::unique_ptr<SwFormatRowSplit> pSplit = rSh.GetRowSplit();
    if(pSplit)
        rSet.Put(std::move(pSplit));

    if(!bTableSel)
    {
        rSh.ClearMark();
        rSh.Pop(SwCursorShell::PopMode::DeleteCurrent);
        rSh.EndAllAction();
    }

    SwTabCols aTabCols;
    rSh.GetTabCols( aTabCols );

    // Pointer will be deleted after the dialogue execution.
    pRep = std::make_shared<SwTableRep>(aTabCols);
    pRep->SetSpace(aCols.GetRightMax());

    sal_uInt16 nPercent = 0;
    auto nWidth = ::GetTableWidth(pFormat, aCols, &nPercent, &rSh );
    // The table width is wrong for relative values.
    if (nPercent)
        nWidth = pRep->GetSpace() * nPercent / 100;
    const sal_uInt16 nAlign = pFormat->GetHoriOrient().GetHoriOrient();
    pRep->SetAlign(nAlign);
    SvxLRSpaceItem aLRSpace( pFormat->GetLRSpace() );
    SwTwips nLeft = aLRSpace.ResolveLeft({});
    SwTwips nRight = aLRSpace.ResolveRight({});
    if(nAlign != text::HoriOrientation::FULL)
    {
        SwTwips nLR = pRep->GetSpace() - nWidth;
        switch ( nAlign )
        {
            case text::HoriOrientation::CENTER:
                nLeft = nRight = nLR / 2;
                break;
            case text::HoriOrientation::LEFT:
                nRight = nLR;
                nLeft = 0;
                break;
            case text::HoriOrientation::RIGHT:
                nLeft = nLR;
                nRight = 0;
                break;
            case text::HoriOrientation::LEFT_AND_WIDTH:
                nRight = nLR - nLeft;
                break;
            case text::HoriOrientation::NONE:
                if(!nPercent)
                    nWidth = pRep->GetSpace() - nLeft - nRight;
                break;
        }
    }
    pRep->SetLeftSpace(nLeft);
    pRep->SetRightSpace(nRight);

    pRep->SetWidth(nWidth);
    pRep->SetWidthPercent(nPercent);
    // Are individual rows / cells are selected, the column processing will be changed.
    pRep->SetLineSelected(bTableSel && ! rSh.HasWholeTabSelection());
    rSet.Put(SwPtrItem(FN_TABLE_REP, pRep.get()));
    return pRep;
}

void ItemSetToTableParam( const SfxItemSet& rSet,
                                SwWrtShell &rSh )
{
    rSh.StartAllAction();
    rSh.StartUndo( SwUndoId::TABLE_ATTR );

    if(const SfxUInt16Item* pDestItem = rSet.GetItemIfSet(SID_BACKGRND_DESTINATION, false))
    {
        SwViewOption aUsrPref( *rSh.GetViewOptions() );
        aUsrPref.SetTableDest(static_cast<sal_uInt8>(pDestItem->GetValue()));
        SwModule::get()->ApplyUsrPref(aUsrPref, &rSh.GetView());
    }
    bool bBorder = ( SfxItemState::SET == rSet.GetItemState( RES_BOX ) ||
            SfxItemState::SET == rSet.GetItemState( SID_ATTR_BORDER_INNER ) );
    const SvxBrushItem* pBackgroundItem = rSet.GetItemIfSet( RES_BACKGROUND, false );
    const SvxBrushItem* pRowItem = rSet.GetItemIfSet( SID_ATTR_BRUSH_ROW, false );
    const SvxBrushItem* pTableItem = rSet.GetItemIfSet( SID_ATTR_BRUSH_TABLE, false );
    bool bBackground = pBackgroundItem || pRowItem || pTableItem;
    const SwFormatRowSplit* pSplit = rSet.GetItemIfSet( RES_ROW_SPLIT, false );
    bool bRowSplit = pSplit != nullptr;
    const SvxFrameDirectionItem* pBoxDirection = rSet.GetItemIfSet( FN_TABLE_BOX_TEXTORIENTATION, false );
    bool bBoxDirection = pBoxDirection != nullptr;
    if( bBackground || bBorder || bRowSplit || bBoxDirection)
    {
        // The border will be applied to the present selection.
        // If there is no selection, the table will be completely selected.
        // The background will always be applied to the current state.
        bool bTableSel = rSh.IsTableMode();
        rSh.StartAllAction();

        if(bBackground)
        {
            if(pBackgroundItem)
                rSh.SetBoxBackground( *pBackgroundItem );
            if(pRowItem)
            {
                std::unique_ptr<SvxBrushItem> aBrush(pRowItem->Clone());
                aBrush->SetWhich(RES_BACKGROUND);
                rSh.SetRowBackground(*aBrush);
            }
            if(pTableItem)
            {
                std::unique_ptr<SvxBrushItem> aBrush(pTableItem->Clone());
                aBrush->SetWhich(RES_BACKGROUND);
                rSh.SetTabBackground( *aBrush );
            }
        }

        if(bBoxDirection)
        {
            SvxFrameDirectionItem aDirection( SvxFrameDirection::Environment, RES_FRAMEDIR );
            aDirection.SetValue(pBoxDirection->GetValue());
            rSh.SetBoxDirection(aDirection);
        }

        if(bBorder || bRowSplit)
        {
            rSh.Push();
            if(!bTableSel)
            {
                rSh.GetView().GetViewFrame().GetDispatcher()->Execute( FN_TABLE_SELECT_ALL );
            }
            if(bBorder)
                rSh.SetTabBorders( rSet );

            if(bRowSplit)
            {
                rSh.SetRowSplit(*pSplit);
            }

            if(!bTableSel)
            {
                rSh.ClearMark();
            }
            rSh.Pop(SwCursorShell::PopMode::DeleteCurrent);
        }

        rSh.EndAllAction();
    }

    SwTabCols aTabCols;
    bool bTabCols = false;
    SwTableRep* pRep = nullptr;
    SwFrameFormat *pFormat = rSh.GetTableFormat();
    SfxItemSetFixed<RES_FRMATR_BEGIN, RES_FRMATR_END-1> aSet( rSh.GetAttrPool() );
    if(const SwPtrItem* pRepItem = rSet.GetItemIfSet( FN_TABLE_REP, false ))
    {
        pRep = static_cast<SwTableRep*>(pRepItem->GetValue());

        const SwTwips nWidth = pRep->GetWidth();
        if ( text::HoriOrientation::FULL == pRep->GetAlign() )
        {
            SwFormatHoriOrient aAttr( pFormat->GetHoriOrient() );
            aAttr.SetHoriOrient( text::HoriOrientation::FULL );
            aSet.Put( aAttr );
        }
        else
        {
            SwFormatFrameSize aSz( SwFrameSize::Variable, nWidth );
            if(pRep->GetWidthPercent())
            {
                aSz.SetWidthPercent( static_cast<sal_uInt8>(pRep->GetWidthPercent()) );
            }
            aSet.Put(aSz);
        }

        SvxLRSpaceItem aLRSpace( RES_LR_SPACE );
        aLRSpace.SetLeft(SvxIndentValue::twips(pRep->GetLeftSpace()));
        aLRSpace.SetRight(SvxIndentValue::twips(pRep->GetRightSpace()));
        aSet.Put( aLRSpace );

        sal_Int16 eOrient = pRep->GetAlign();
        SwFormatHoriOrient aAttr( 0, eOrient );
        aSet.Put( aAttr );
        // The item must only be recorded while manual alignment, so that the
        // alignment is not overwritten by the distances while recording.
        if(eOrient != text::HoriOrientation::NONE)
            const_cast<SfxItemSet&>(rSet).ClearItem( SID_ATTR_LRSPACE );

        if(pRep->HasColsChanged())
        {
            bTabCols = true;
        }
    }

    if( const SfxUInt16Item* pHeadlineItem = rSet.GetItemIfSet( FN_PARAM_TABLE_HEADLINE, false ))
        rSh.SetRowsToRepeat( pHeadlineItem->GetValue() );

    if( const SfxUInt16Item* pAlignItem = rSet.GetItemIfSet( FN_TABLE_SET_VERT_ALIGN, false ))
        rSh.SetBoxAlign(pAlignItem->GetValue());

    if( const SfxStringItem* pNameItem = rSet.GetItemIfSet( FN_PARAM_TABLE_NAME, false ))
        rSh.SetTableName( *pFormat, UIName(pNameItem->GetValue()) );

    // Copy the chosen attributes in the ItemSet.
    static const sal_uInt16 aIds[] =
        {
            RES_PAGEDESC,
            RES_BREAK,
            RES_KEEP,
            RES_LAYOUT_SPLIT,
            RES_UL_SPACE,
            RES_SHADOW,
            RES_FRAMEDIR,
            // #i29550#
            RES_COLLAPSING_BORDERS,
            // <-- collapsing borders
            0
        };
    const SfxPoolItem* pItem = nullptr;
    for( const sal_uInt16* pIds = aIds; *pIds; ++pIds )
        if( SfxItemState::SET == rSet.GetItemState( *pIds, false, &pItem))
            aSet.Put( *pItem );

    if(bTabCols)
    {
        rSh.GetTabCols( aTabCols );
        bool bSingleLine = pRep->FillTabCols( aTabCols );
        rSh.SetTabCols( aTabCols, bSingleLine );
    }

    if( aSet.Count() )
        rSh.SetTableAttr( aSet );

    rSh.EndUndo( SwUndoId::TABLE_ATTR );
    rSh.EndAllAction();
}

static void lcl_TabGetMaxLineWidth(const SvxBorderLine* pBorderLine, SvxBorderLine& rBorderLine)
{
    if(pBorderLine->GetWidth() > rBorderLine.GetWidth())
        rBorderLine.SetWidth(pBorderLine->GetWidth());

    rBorderLine.SetBorderLineStyle(pBorderLine->GetBorderLineStyle());
    rBorderLine.SetColor(pBorderLine->GetColor());
}

static bool lcl_BoxesInTrackedRows(const SwWrtShell &rSh, const SwSelBoxes& rBoxes)
{
    // cursor and selection are there only in tracked rows
    bool bRet = true;
    SwRedlineTable::size_type nRedlinePos = 0;
    if ( rBoxes.empty() )
        bRet = rSh.GetCursor()->GetPointNode().GetTableBox()->GetUpper()->IsTracked(nRedlinePos);
    else
    {
        tools::Long nBoxes = rBoxes.size();
        SwTableLine* pPrevLine = nullptr;
        for ( tools::Long i = 0; i < nBoxes; i++ )
        {
            SwTableLine* pLine = rBoxes[i]->GetUpper();
            if ( pLine != pPrevLine )
                bRet &= pLine->IsTracked(nRedlinePos);
            pPrevLine = pLine;
        }
    }

    return bRet;
}

static bool lcl_CursorInDeletedTable(const SwWrtShell &rSh)
{
    // cursor and selection are there only in deleted table in Show Changes mode
    if ( rSh.GetLayout()->IsHideRedlines() )
        return false;

    SwTableNode* pTableNd = rSh.GetCursor()->GetPoint()->GetNode().FindTableNode();
    return pTableNd && pTableNd->GetTable().IsDeleted();
}

void SwTableShell::Execute(SfxRequest &rReq)
{
    const SfxItemSet* pArgs = rReq.GetArgs();
    SwWrtShell &rSh = GetShell();

    // At first the slots which doesn't need a FrameMgr.
    bool bMore = false;
    const SfxPoolItem* pItem = nullptr;
    sal_uInt16 nSlot = rReq.GetSlot();
    if(pArgs)
        pArgs->GetItemState(GetPool().GetWhichIDFromSlotID(nSlot), false, &pItem);
    bool bCallDone = false;
    switch ( nSlot )
    {
        case SID_ATTR_BORDER:
        {
            if(!pArgs)
                break;
            // Create items, because we have to rework anyway.
            std::shared_ptr<SvxBoxItem> aBox(std::make_shared<SvxBoxItem>(RES_BOX));
            SfxItemSetFixed<RES_BOX, RES_BOX,
                            SID_ATTR_BORDER_INNER, SID_ATTR_BORDER_INNER>
                aCoreSet( GetPool() );
            SvxBoxInfoItem aCoreInfo( SID_ATTR_BORDER_INNER );
            aCoreSet.Put(aCoreInfo);
            rSh.GetTabBorders( aCoreSet );
            const SvxBoxItem& rCoreBox = aCoreSet.Get(RES_BOX);
            const SvxBoxItem *pBoxItem = pArgs->GetItemIfSet(RES_BOX);
            if ( pBoxItem )
            {
                aBox.reset(pBoxItem->Clone());
                sal_Int16 nDefValue = MIN_BORDER_DIST;
                if ( !rReq.IsAPI() )
                    nDefValue = 55;
                if (!rReq.IsAPI() || aBox->GetSmallestDistance() < MIN_BORDER_DIST)
                {
                    for( SvxBoxItemLine k : o3tl::enumrange<SvxBoxItemLine>() )
                        aBox->SetDistance( std::max(rCoreBox.GetDistance(k), nDefValue) , k );
                }
            }
            else
                OSL_ENSURE( false, "where is BoxItem?" );

            //since the drawing layer also supports borders the which id might be a different one
            std::shared_ptr<SvxBoxInfoItem> aInfo(std::make_shared<SvxBoxInfoItem>(SID_ATTR_BORDER_INNER));
            if (const SvxBoxInfoItem* pBoxInfoItem = pArgs->GetItemIfSet(SID_ATTR_BORDER_INNER))
            {
                aInfo.reset(pBoxInfoItem->Clone());
            }
            else if( const SvxBoxInfoItem* pBoxInfoInnerItem = pArgs->GetItemIfSet(SDRATTR_TABLE_BORDER_INNER))
            {
                aInfo.reset(pBoxInfoInnerItem->Clone());
                aInfo->SetWhich(SID_ATTR_BORDER_INNER);
            }

            aInfo->SetTable( true );
            aInfo->SetValid( SvxBoxInfoItemValidFlags::DISABLE, false );

// The attributes of all lines will be read and the strongest wins.
            const SvxBorderLine* pBorderLine;
            SvxBorderLine aBorderLine;
            if ((pBorderLine = rCoreBox.GetTop()) != nullptr)
                lcl_TabGetMaxLineWidth(pBorderLine, aBorderLine);
            if ((pBorderLine = rCoreBox.GetBottom()) != nullptr)
                lcl_TabGetMaxLineWidth(pBorderLine, aBorderLine);
            if ((pBorderLine = rCoreBox.GetLeft()) != nullptr)
                lcl_TabGetMaxLineWidth(pBorderLine, aBorderLine);
            if ((pBorderLine = rCoreBox.GetRight()) != nullptr)
                lcl_TabGetMaxLineWidth(pBorderLine, aBorderLine);
            if ((pBorderLine = aCoreInfo.GetHori()) != nullptr)
                lcl_TabGetMaxLineWidth(pBorderLine, aBorderLine);
            if ((pBorderLine = aCoreInfo.GetVert()) != nullptr)
                lcl_TabGetMaxLineWidth(pBorderLine, aBorderLine);

            if(aBorderLine.GetOutWidth() == 0)
            {
                aBorderLine.SetBorderLineStyle(SvxBorderLineStyle::SOLID);
                aBorderLine.SetWidth( SvxBorderLineWidth::VeryThin );
            }

            if( aBox->GetTop() != nullptr )
            {
                aBox->SetLine(&aBorderLine, SvxBoxItemLine::TOP);
            }
            if( aBox->GetBottom() != nullptr )
            {
                aBox->SetLine(&aBorderLine, SvxBoxItemLine::BOTTOM);
            }
            if( aBox->GetLeft() != nullptr )
            {
                aBox->SetLine(&aBorderLine, SvxBoxItemLine::LEFT);
            }
            if( aBox->GetRight() != nullptr )
            {
                aBox->SetLine(&aBorderLine, SvxBoxItemLine::RIGHT);
            }
            if( aInfo->GetHori() != nullptr )
            {
                aInfo->SetLine(&aBorderLine, SvxBoxInfoItemLine::HORI);
            }
            if( aInfo->GetVert() != nullptr )
            {
                aInfo->SetLine(&aBorderLine, SvxBoxInfoItemLine::VERT);
            }

            aCoreSet.Put( *aBox  );
            aCoreSet.Put( *aInfo );
            rSh.SetTabBorders( aCoreSet );

            // we must record the "real" values because otherwise the lines can't be reconstructed on playtime
            // the coding style of the controller (setting lines with width 0) is not transportable via Query/PutValue in
            // the SvxBoxItem
            rReq.AppendItem( *aBox );
            rReq.AppendItem( *aInfo );
            bCallDone = true;
            break;
        }
        case FN_INSERT_TABLE:
            InsertTable( rReq );
            break;
        case FN_BREAK_ABOVE_TABLE:
        {
            rSh.MoveTable( GotoCurrTable, fnTableStart );
            rSh.SplitNode( false );
            break;
        }
        case FN_FORMAT_TABLE_DLG:
        {
            //#127012# get the bindings before the dialog is called
            // it might happen that this shell is removed after closing the dialog
            SfxBindings& rBindings = GetView().GetViewFrame().GetBindings();
            SfxItemSet aCoreSet( GetPool(), aUITableAttrRange);

            FieldUnit eMetric = ::GetDfltMetric(dynamic_cast<SwWebView*>( &rSh.GetView()) != nullptr );
            SwModule::get()->PutItem(SfxUInt16Item(SID_ATTR_METRIC, static_cast< sal_uInt16 >(eMetric)));
            std::shared_ptr<SwTableRep> xTableRep(::lcl_TableParamToItemSet(aCoreSet, rSh));

            aCoreSet.Put(SfxUInt16Item(SID_HTML_MODE, ::GetHtmlMode(GetView().GetDocShell())));
            rSh.GetTableAttr(aCoreSet);
            // GetTableAttr overwrites the background!
            std::unique_ptr<SvxBrushItem> aBrush(std::make_unique<SvxBrushItem>(RES_BACKGROUND));
            if(rSh.GetBoxBackground(aBrush))
                aCoreSet.Put( *aBrush );
            else
                aCoreSet.InvalidateItem( RES_BACKGROUND );

            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            VclPtr<SfxAbstractTabDialog> pDlg(pFact->CreateSwTableTabDlg(GetView().GetFrameWeld(), &aCoreSet, &rSh));

            if (pDlg)
            {
                if (pItem)
                    pDlg->SetCurPageId(static_cast<const SfxStringItem *>(pItem)->GetValue());

                auto xRequest = std::make_shared<SfxRequest>(rReq);
                rReq.Ignore(); // the 'old' request is not relevant any more

                const bool bTableMode = rSh.IsTableMode();
                SwPaM* pCursor = bTableMode ? rSh.GetTableCrs() : rSh.GetCursor(); // tdf#142165 use table cursor if in table mode
                auto vCursors = CopyPaMRing(*pCursor); // tdf#135636 make a copy to use at later apply
                pDlg->StartExecuteAsync([pDlg, xRequest=std::move(xRequest), xTableRep=std::move(xTableRep),
                                         &rBindings, &rSh, vCursors=std::move(vCursors), bTableMode](sal_Int32 nResult){
                    if (RET_OK == nResult)
                    {
                        if (!bTableMode && rSh.IsTableMode()) // tdf#140977 drop current table-cursor if setting a replacement
                            rSh.TableCursorToCursor();        // non-table one

                        // tdf#135636 set the selection at dialog launch as current selection
                        rSh.SetSelection(*vCursors->front()); // UpdateCursor() will be called which in the case
                                                              // of a table selection should recreate a
                                                              // SwShellTableCursor if the selection is more than a single cell

                        if (bTableMode && !rSh.IsTableMode()) // tdf#142721 ensure the new selection is a SwShellTableCursor in
                            rSh.SelTableBox();                // the case of a single cell

                        const SfxItemSet* pOutSet = pDlg->GetOutputItemSet();

                        //to record FN_INSERT_TABLE correctly
                        xRequest->SetSlot(FN_FORMAT_TABLE_DLG);
                        xRequest->Done(*pOutSet);

                        ItemSetToTableParam(*pOutSet, rSh);
                    }

                    rBindings.Update(SID_RULER_BORDERS);
                    rBindings.Update(SID_ATTR_TABSTOP);
                    rBindings.Update(SID_RULER_BORDERS_VERTICAL);
                    rBindings.Update(SID_ATTR_TABSTOP_VERTICAL);

                    pDlg->disposeOnce();
                });
            }
            else
            {
                if (rReq.GetArgs())
                    ItemSetToTableParam(*rReq.GetArgs(), rSh);

                rBindings.Update(SID_RULER_BORDERS);
                rBindings.Update(SID_ATTR_TABSTOP);
                rBindings.Update(SID_RULER_BORDERS_VERTICAL);
                rBindings.Update(SID_ATTR_TABSTOP_VERTICAL);
            }

            break;
        }
        case SID_ATTR_BRUSH:
        case SID_ATTR_BRUSH_ROW :
        case SID_ATTR_BRUSH_TABLE :
            if(rReq.GetArgs())
                ItemSetToTableParam(*rReq.GetArgs(), rSh);
            break;
        case FN_NUM_FORMAT_TABLE_DLG:
        {
            if (SwView* pView = GetActiveView())
            {
                FieldUnit eMetric = ::GetDfltMetric(dynamic_cast<SwWebView*>( pView) !=  nullptr );
                SwModule::get()->PutItem(SfxUInt16Item(SID_ATTR_METRIC, static_cast< sal_uInt16 >(eMetric)));
                SvNumberFormatter* pFormatter = rSh.GetNumberFormatter();
                auto pCoreSet = std::make_shared<SfxItemSetFixed<SID_ATTR_NUMBERFORMAT_VALUE, SID_ATTR_NUMBERFORMAT_INFO>>( GetPool() );

                SfxItemSetFixed<RES_BOXATR_FORMAT, RES_BOXATR_FORMAT,
                                RES_BOXATR_VALUE, RES_BOXATR_VALUE>
                     aBoxSet( *pCoreSet->GetPool() );
                rSh.GetTableBoxFormulaAttrs( aBoxSet );

                // tdf#132111: if RES_BOXATR_FORMAT state is DEFAULT (no number format set to cell
                // explicitly), it's not equal to any specific format (the rules are special, e.g.
                // it's considered numeric for empty or number text in SwTableBox::HasNumContent).
                // For multiselection, it's INVALID, also not equal to any single format.
                if (auto pFormat = aBoxSet.GetItemIfSet(RES_BOXATR_FORMAT))
                    pCoreSet->Put(SfxUInt32Item(SID_ATTR_NUMBERFORMAT_VALUE, pFormat->GetValue()));

                pCoreSet->Put( SvxNumberInfoItem( pFormatter,
                                    aBoxSet.Get(
                                        RES_BOXATR_VALUE).GetValue(),
                                    rSh.GetTableBoxText(), SID_ATTR_NUMBERFORMAT_INFO ));

                SwWrtShell* pSh = &rSh;
                SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
                VclPtr<SfxAbstractDialog> pDlg(pFact->CreateNumFormatDialog(GetView().GetFrameWeld(), *pCoreSet));

                pDlg->StartExecuteAsync([pDlg, pCoreSet=std::move(pCoreSet), pSh](sal_uInt32 nResult){
                    if (RET_OK == nResult)
                    {
                        const SvxNumberInfoItem* pNumberFormatItem
                            = pSh->GetView().GetDocShell()->GetItem( SID_ATTR_NUMBERFORMAT_INFO );

                        if( pNumberFormatItem )
                        {
                            for ( sal_uInt32 key : pNumberFormatItem->GetDelFormats() )
                                pNumberFormatItem->GetNumberFormatter()->DeleteEntry( key );
                        }

                        const SfxPoolItem* pNumberFormatValueItem =
                            pDlg->GetOutputItemSet()->GetItemIfSet(
                                SID_ATTR_NUMBERFORMAT_VALUE, false);
                        if( pNumberFormatValueItem )
                        {
                            SfxItemSetFixed<RES_BOXATR_FORMAT, RES_BOXATR_FORMAT>
                                    aBoxFormatSet( *pCoreSet->GetPool() );
                            aBoxFormatSet.Put( SwTableBoxNumFormat(
                                    static_cast<const SfxUInt32Item*>(pNumberFormatValueItem)->GetValue() ));
                            pSh->SetTableBoxFormulaAttrs( aBoxFormatSet );

                        }
                    }

                    pDlg->disposeOnce();
                });
            }
            break;
        }
        case FN_CALC_TABLE:
            rSh.UpdateTable();
            bCallDone = true;
            break;
        case FN_TABLE_DELETE_COL:
            if ( rSh.DeleteCol() && rSh.HasSelection() )
                rSh.EnterStdMode();
            bCallDone = true;
            break;
        case FN_END_TABLE:
            rSh.MoveTable( GotoCurrTable, fnTableEnd );
            bCallDone = true;
            break;
        case FN_START_TABLE:
            rSh.MoveTable( GotoCurrTable, fnTableStart );
            bCallDone = true;
            break;
        case FN_GOTO_NEXT_CELL:
        {
            bool bAppendLine = true;
            if( pItem )
                bAppendLine = static_cast<const SfxBoolItem*>(pItem)->GetValue();
            rReq.SetReturnValue( SfxBoolItem( nSlot,
                                    rSh.GoNextCell( bAppendLine ) ) );
            bCallDone = true;
            break;
        }
        case FN_GOTO_PREV_CELL:
            rReq.SetReturnValue( SfxBoolItem( nSlot, rSh.GoPrevCell() ) );
            bCallDone = true;
            break;
        case FN_TABLE_DELETE_ROW:
            if ( rSh.DeleteRow() && rSh.HasSelection() )
                rSh.EnterStdMode();
            bCallDone = true;
            break;
        case FN_TABLE_MERGE_CELLS:
            if ( rSh.IsTableMode() )
                switch ( rSh.MergeTab() )
                {
                    case TableMergeErr::Ok:
                         bCallDone = true;
                         [[fallthrough]];
                    case TableMergeErr::NoSelection:
                        break;
                    case TableMergeErr::TooComplex:
                    {
                        std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(GetView().GetFrameWeld(),
                                                                      VclMessageType::Info, VclButtonsType::Ok,
                                                                      SwResId(STR_ERR_TABLE_MERGE)));
                        xInfoBox->run();
                        break;
                    }
                    default:
                        OSL_ENSURE( false, "unknown return value MergeTab.");
                        break;
                }
            break;
        case SID_TABLE_MINIMAL_COLUMN_WIDTH:
        case FN_TABLE_ADJUST_CELLS:
        case FN_TABLE_BALANCE_CELLS:
        {
            bool bBalance = (FN_TABLE_BALANCE_CELLS == nSlot);
            const bool bNoShrink = FN_TABLE_ADJUST_CELLS == nSlot;
            if ( rSh.IsAdjustCellWidthAllowed(bBalance) )
            {
                {
                    // remove actions to make a valid table selection
                    UnoActionRemoveContext aRemoveContext(rSh.GetDoc());
                }
                rSh.AdjustCellWidth(bBalance, bNoShrink);
            }
            bCallDone = true;
            break;
        }
        case SID_TABLE_MINIMAL_ROW_HEIGHT:
        {
            const SwFormatFrameSize aSz;
            rSh.SetRowHeight( aSz );
            bCallDone = true;
            break;
        }
        case FN_TABLE_OPTIMAL_HEIGHT:
        {
            rSh.BalanceRowHeight(/*bTstOnly=*/false, /*bOptimize=*/true);
            rSh.BalanceRowHeight(/*bTstOnly=*/false, /*bOptimize=*/false);
            bCallDone = true;
            break;
        }
        case FN_TABLE_BALANCE_ROWS:
            if ( rSh.BalanceRowHeight(true) )
                rSh.BalanceRowHeight(false);
            bCallDone = true;
            break;
        case FN_TABLE_SELECT_ALL:
            rSh.EnterStdMode();
            rSh.MoveTable( GotoCurrTable, fnTableStart );
            rSh.SttSelect();
            rSh.MoveTable( GotoCurrTable, fnTableEnd );
            rSh.EndSelect();
            bCallDone = true;
            break;
        case FN_TABLE_SELECT_COL:
            rSh.EnterStdMode();
            rSh.SelectTableCol();
            bCallDone = true;
            break;
        case FN_TABLE_SELECT_ROW:
            rSh.EnterStdMode();
            rSh.SelectTableRow();
            bCallDone = true;
            break;
        case FN_TABLE_SET_READ_ONLY_CELLS:
            rSh.ProtectCells();
            rSh.ResetSelect( nullptr, false, ScrollSizeMode::ScrollSizeDefault );
            bCallDone = true;
            break;
        case FN_TABLE_UNSET_READ_ONLY_CELLS:
            rSh.UnProtectCells();
            bCallDone = true;
            break;
        case SID_AUTOFORMAT:
        {
            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            VclPtr<AbstractSwAutoFormatDlg> pDlg(pFact->CreateSwAutoFormatDlg(GetView().GetFrameWeld(), &rSh));
            pDlg->StartExecuteAsync(
                [pDlg] (sal_Int32 nResult)->void
                {
                    if (nResult == RET_OK)
                        pDlg->Apply();
                    pDlg->disposeOnce();
                }
            );
            break;
        }
        case FN_TABLE_SET_ROW_HEIGHT:
        {
            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            VclPtr<AbstractSwTableHeightDlg> pDlg(pFact->CreateSwTableHeightDialog(GetView().GetFrameWeld(), rSh));
            pDlg->StartExecuteAsync(
                [pDlg] (sal_Int32 nResult)->void
                {
                    if (nResult == RET_OK)
                        pDlg->Apply();
                    pDlg->disposeOnce();
                }
            );
            break;
        }
        case FN_NUMBER_BULLETS:
        case FN_NUM_BULLET_ON:
            OSL_ENSURE( false, "function may not be called now." );
            break;


        // 2015/06 The following two are deprecated but kept for ascending
        // compatibility
        case FN_TABLE_INSERT_COL:
        case FN_TABLE_INSERT_ROW:
            // fallback
        case FN_TABLE_INSERT_COL_BEFORE:
        case FN_TABLE_INSERT_ROW_BEFORE:
        case FN_TABLE_INSERT_COL_AFTER:
        case FN_TABLE_INSERT_ROW_AFTER:
        {
            bool bColumn = rReq.GetSlot() == FN_TABLE_INSERT_COL_BEFORE
                           || rReq.GetSlot() == FN_TABLE_INSERT_COL_AFTER
                           || rReq.GetSlot() == FN_TABLE_INSERT_COL;
            sal_uInt16 nCount = 0;
            bool bAfter = true;
            if (pItem)
            {
                nCount = static_cast<const SfxInt16Item* >(pItem)->GetValue();
                if(const SfxBoolItem* pAfterItem = pArgs->GetItemIfSet(FN_PARAM_INSERT_AFTER))
                    bAfter = pAfterItem->GetValue();
            }
            else if( !rReq.IsAPI() )
            {
                SwSelBoxes aBoxes;
                ::GetTableSel( rSh, aBoxes );
                if ( !aBoxes.empty() )
                {
                    tools::Long maxX = 0;
                    tools::Long maxY = 0;
                    tools::Long minX = std::numeric_limits<tools::Long>::max();
                    tools::Long minY = std::numeric_limits<tools::Long>::max();
                    tools::Long nbBoxes = aBoxes.size();
                    for ( tools::Long i = 0; i < nbBoxes; i++ )
                    {
                        Point aCoord ( aBoxes[i]->GetCoordinates() );
                        if ( aCoord.X() < minX ) minX = aCoord.X();
                        if ( aCoord.X() > maxX ) maxX = aCoord.X();
                        if ( aCoord.Y() < minY ) minY = aCoord.Y();
                        if ( aCoord.Y() > maxY ) maxY = aCoord.Y();
                    }
                    if (bColumn)
                        nCount = maxX - minX + 1;
                    else
                        nCount = maxY - minY + 1;
                }
                bAfter = rReq.GetSlot() == FN_TABLE_INSERT_COL_AFTER
                         || rReq.GetSlot() == FN_TABLE_INSERT_ROW_AFTER
                         || rReq.GetSlot() == FN_TABLE_INSERT_ROW
                         || rReq.GetSlot() == FN_TABLE_INSERT_COL;
            }

            if( nCount )
            {
                // i74180: Table border patch submitted by chensuchun:
                // -->get the SvxBoxInfoItem of the table before insert
                SfxItemSet aCoreSet( GetPool(), aUITableAttrRange);
                ::lcl_TableParamToItemSet( aCoreSet, rSh );
                bool bSetInnerBorders = false;
                SwUndoId nUndoId = SwUndoId::EMPTY;
                // <--End

                if( bColumn )
                {
                    rSh.StartUndo( SwUndoId::TABLE_INSCOL );
                    rSh.InsertCol( nCount, bAfter );
                    bSetInnerBorders = true;
                    nUndoId = SwUndoId::TABLE_INSCOL;
                }
                else if ( !rSh.IsInRepeatedHeadline() )
                {
                    rSh.StartUndo( SwUndoId::TABLE_INSROW );
                    rSh.InsertRow( nCount, bAfter );
                    bSetInnerBorders = true;
                    nUndoId = SwUndoId::TABLE_INSROW;
                }

                // -->after inserting,reset the inner table borders
                if ( bSetInnerBorders )
                {
                    const SvxBoxInfoItem& aBoxInfo(aCoreSet.Get(SID_ATTR_BORDER_INNER));
                    SfxItemSetFixed<SID_ATTR_BORDER_INNER, SID_ATTR_BORDER_INNER> aSet( GetPool() );
                    aSet.Put( aBoxInfo );
                    ItemSetToTableParam( aSet, rSh );
                    rSh.EndUndo( nUndoId );
                }

                bCallDone = true;
                break;
            }

            nSlot = bColumn ? FN_TABLE_INSERT_COL_DLG : FN_TABLE_INSERT_ROW_DLG;

            [[fallthrough]]; // on Count = 0 appears the dialog
        }
        case FN_TABLE_INSERT_COL_DLG:
        case FN_TABLE_INSERT_ROW_DLG:
        {
            const SfxSlot* pSlot = GetStaticInterface()->GetSlot(nSlot);
            if ( FN_TABLE_INSERT_ROW_DLG != nSlot || !rSh.IsInRepeatedHeadline())
            {
                auto xRequest = std::make_shared<SfxRequest>(rReq);
                rReq.Ignore(); // the 'old' request is not relevant any more
                SvxAbstractDialogFactory* pFact = SvxAbstractDialogFactory::Create();
                VclPtr<SvxAbstractInsRowColDlg> pDlg(pFact->CreateSvxInsRowColDlg(GetView().GetFrameWeld(),
                                                                                        nSlot == FN_TABLE_INSERT_COL_DLG, pSlot->GetCommand()));
                pDlg->StartExecuteAsync(
                    [this, pDlg, xRequest=std::move(xRequest), nSlot] (sal_Int32 nResult)->void
                    {
                        if (nResult == RET_OK)
                        {
                            const TypedWhichId<SfxUInt16Item> nDispatchSlot = (nSlot == FN_TABLE_INSERT_COL_DLG)
                                ? FN_TABLE_INSERT_COL_AFTER : FN_TABLE_INSERT_ROW_AFTER;
                            SfxUInt16Item aCountItem( nDispatchSlot, pDlg->getInsertCount() );
                            SfxBoolItem  aAfter( FN_PARAM_INSERT_AFTER, !pDlg->isInsertBefore() );
                            SfxViewFrame& rVFrame = GetView().GetViewFrame();
                            rVFrame.GetDispatcher()->ExecuteList(nDispatchSlot,
                                SfxCallMode::SYNCHRON|SfxCallMode::RECORD,
                                { &aCountItem, &aAfter });
                        }
                        pDlg->disposeOnce();
                        xRequest->Done();
                    }
                );
            }
            break;
        }
        case FN_TABLE_SPLIT_CELLS:
        {
            tools::Long nCount=0;
            bool bHorizontal=true;
            bool bProportional = false;
            const SfxInt32Item* pSplit = rReq.GetArg<SfxInt32Item>(FN_TABLE_SPLIT_CELLS);
            const SfxBoolItem* pHor = rReq.GetArg<SfxBoolItem>(FN_PARAM_1);
            const SfxBoolItem* pProp = rReq.GetArg<SfxBoolItem>(FN_PARAM_2);
            if ( pSplit )
            {
                nCount = pSplit->GetValue();
                if ( pHor )
                    bHorizontal = pHor->GetValue();
                if ( pProp )
                    bProportional = pProp->GetValue();
            }
            else
            {
                SvxAbstractDialogFactory* pFact = SvxAbstractDialogFactory::Create();
                SwWrtShell* pSh = &rSh;
                const tools::Long nMaxVert = rSh.GetAnyCurRect( CurRectType::Frame ).Width() / MINLAY;
                VclPtr<SvxAbstractSplitTableDialog> pDlg(pFact->CreateSvxSplitTableDialog(GetView().GetFrameWeld(), rSh.IsTableVertical(), nMaxVert));
                if(rSh.IsSplitVerticalByDefault())
                    pDlg->SetSplitVerticalByDefault();
                pDlg->StartExecuteAsync([pDlg, pSh](int nResult) {
                    if (nResult == RET_OK)
                    {
                        tools::Long nCount2 = pDlg->GetCount();
                        bool bHorizontal2 = pDlg->IsHorizontal();
                        bool bProportional2 = pDlg->IsProportional();

                        // tdf#60242: remember choice for next time
                        bool bVerticalWasChecked = !pDlg->IsHorizontal();
                        pSh->SetSplitVerticalByDefault(bVerticalWasChecked);

                        if ( nCount2 > 1 )
                            pSh->SplitTab(!bHorizontal2, static_cast< sal_uInt16 >( nCount2-1 ), bProportional2 );
                    }

                    pDlg->disposeOnce();
                });
            }

            if ( nCount>1 )
            {
                rSh.SplitTab(!bHorizontal, static_cast< sal_uInt16 >( nCount-1 ), bProportional );
                bCallDone = true;
            }
            else
                rReq.Ignore();
            break;
        }

        case FN_TABLE_SPLIT_TABLE:
        {
            const SfxUInt16Item* pType = rReq.GetArg<SfxUInt16Item>(FN_PARAM_1);
            if( pType )
            {
                switch( static_cast<SplitTable_HeadlineOption>(pType->GetValue()) )
                {
                    case SplitTable_HeadlineOption::NONE    :
                    case SplitTable_HeadlineOption::BorderCopy:
                    case SplitTable_HeadlineOption::ContentCopy:
                    case SplitTable_HeadlineOption::BoxAttrCopy:
                    case SplitTable_HeadlineOption::BoxAttrAllCopy:
                        rSh.SplitTable(static_cast<SplitTable_HeadlineOption>(pType->GetValue())) ;
                        break;
                    default: ;//wrong parameter, do nothing
                }
            }
            else
            {
                SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
                VclPtr<AbstractSplitTableDialog> pDlg(pFact->CreateSplitTableDialog(GetView().GetFrameWeld(), rSh));

                SwWrtShell* pSh = &rSh;

                pDlg->StartExecuteAsync([pDlg, pSh](int nResult) {
                    if (nResult == RET_OK)
                    {
                        const auto aSplitMode = pDlg->GetSplitMode();
                        pSh->SplitTable( aSplitMode );
                    }

                    pDlg->disposeOnce();
                });
                rReq.Ignore(); // We're already handling the request in our async bit
            }
            break;
        }

        case FN_TABLE_MERGE_TABLE:
        {
            bool bPrev = rSh.CanMergeTable();
            bool bNext = rSh.CanMergeTable( false );

            if( bPrev && bNext )
            {
                SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
                ScopedVclPtr<VclAbstractDialog> pDlg(pFact->CreateTableMergeDialog(GetView().GetFrameWeld(), bPrev));
                if( RET_OK != pDlg->Execute())
                    bPrev = bNext = false;
            }

            if( bPrev || bNext )
                rSh.MergeTable( bPrev );
            break;
        }

        case FN_TABLE_MODE_FIX       :
        case FN_TABLE_MODE_FIX_PROP  :
        case FN_TABLE_MODE_VARIABLE  :
        {
            rSh.SetTableChgMode( FN_TABLE_MODE_FIX == nSlot
                                    ? TableChgMode::FixedWidthChangeAbs
                                    : FN_TABLE_MODE_FIX_PROP == nSlot
                                        ? TableChgMode::FixedWidthChangeProp
                                        : TableChgMode::VarWidthChangeAbs );

            SfxBindings& rBind = GetView().GetViewFrame().GetBindings();
            static const sal_uInt16 aInva[] =
                            {   FN_TABLE_MODE_FIX,
                                FN_TABLE_MODE_FIX_PROP,
                                FN_TABLE_MODE_VARIABLE,
                                0
                            };
            rBind.Invalidate( aInva );
            bCallDone = true;
            break;
        }
        case FN_TABLE_AUTOSUM:
        {
            SfxViewFrame& rVFrame = GetView().GetViewFrame();
            rVFrame.GetDispatcher()->Execute(FN_EDIT_FORMULA, SfxCallMode::SYNCHRON);
            const sal_uInt16 nId = SwInputChild::GetChildWindowId();
            SwInputChild* pChildWin = static_cast<SwInputChild*>(rVFrame.
                                                GetChildWindow( nId ));
            OUString sSum;
            GetShell().GetAutoSum(sSum);
            if( pChildWin )
                pChildWin->SetFormula( sSum );

            break;
        }
        case FN_TABLE_HEADLINE_REPEAT:
            if(0 != rSh.GetRowsToRepeat())
                rSh.SetRowsToRepeat( 0 );
            else
                rSh.SetRowsToRepeat(rSh.GetRowSelectionFromTop());
            break;
        case FN_TABLE_SELECT_CELL   :
            rSh.SelectTableCell();
            break;
        case FN_TABLE_DELETE_TABLE  :
        {
            rSh.StartAction();
            rSh.StartUndo();
            rSh.GetView().GetViewFrame().GetDispatcher()->Execute(FN_TABLE_SELECT_ALL);
            rSh.DeleteTable();
            rSh.EndUndo();
            rSh.EndAction();
            //'this' is already destroyed
            return;
        }
        case SID_ATTR_TABLE_ROW_HEIGHT:
        {
            const SfxUInt32Item* pItem2 = rReq.GetArg<SfxUInt32Item>(SID_ATTR_TABLE_ROW_HEIGHT);
            if (pItem2)
            {
                tools::Long nNewHeight = pItem2->GetValue();
                std::unique_ptr<SwFormatFrameSize> pHeight = rSh.GetRowHeight();
                if ( pHeight )
                {
                    if (pHeight->GetHeightSizeType() == SwFrameSize::Variable)
                        pHeight->SetHeightSizeType(SwFrameSize::Minimum);
                    pHeight->SetHeight(nNewHeight);
                    rSh.SetRowHeight(*pHeight);
                }
            }
            return;
        }
        case SID_ATTR_TABLE_COLUMN_WIDTH:
        {
            const SfxUInt32Item* pItem2 = rReq.GetArg<SfxUInt32Item>(SID_ATTR_TABLE_COLUMN_WIDTH);
            if (pItem2)
            {
                tools::Long nNewWidth = pItem2->GetValue();
                SwTableFUNC aFunc( &rSh );
                aFunc.InitTabCols();
                aFunc.SetColWidth(aFunc.GetCurColNum(), nNewWidth);
            }
            return;
        }
        case SID_ATTR_TABLE_ALIGNMENT:
        {
            const SfxUInt16Item* pAlignItem = rReq.GetArg<SfxUInt16Item>(SID_ATTR_TABLE_ALIGNMENT);
            if (pAlignItem && pAlignItem->GetValue() <= text::HoriOrientation::LEFT_AND_WIDTH)
            {
                SfxItemSetFixed<RES_FRMATR_BEGIN, RES_FRMATR_END - 1> aSet( GetPool());
                rSh.StartUndo(SwUndoId::TABLE_ATTR);
                SwFormatHoriOrient aAttr( 0, pAlignItem->GetValue());

                const SfxInt32Item* pLeftItem = rReq.GetArg<SfxInt32Item>(SID_ATTR_TABLE_LEFT_SPACE);
                const SfxInt32Item* pRightItem = rReq.GetArg<SfxInt32Item>(SID_ATTR_TABLE_RIGHT_SPACE);

                SvxLRSpaceItem aLRSpace( RES_LR_SPACE );
                SwTwips nLeft = pLeftItem ? pLeftItem->GetValue() : 0;
                SwTwips nRight = pRightItem ? pRightItem->GetValue() : 0;
                SwTabCols aTabCols;
                rSh.GetTabCols(aTabCols);
                tools::Long nSpace = aTabCols.GetRightMax();
                SwTwips nWidth = nSpace;
                switch (pAlignItem->GetValue())
                {
                    case text::HoriOrientation::LEFT:
                        if (MINLAY < nSpace - nRight)
                            nWidth = nSpace - nRight;
                        else
                        {
                            nWidth = MINLAY;
                            nRight = nSpace - MINLAY;
                        }
                        nLeft = 0;
                        break;
                    case text::HoriOrientation::RIGHT:
                        if (MINLAY < nSpace - nLeft)
                            nWidth = nSpace - nLeft;
                        else
                        {
                            nWidth = MINLAY;
                            nLeft = nSpace - MINLAY;
                        }
                        nRight = 0;
                        break;
                    case text::HoriOrientation::LEFT_AND_WIDTH:
                        // width doesn't change
                        nRight = 0;
                        nLeft = std::min(nLeft, nSpace);
                        break;
                    case text::HoriOrientation::FULL:
                        nLeft = nRight = 0;
                        break;
                    case text::HoriOrientation::CENTER:
                        if (MINLAY < nSpace - 2 * nLeft)
                            nWidth = nSpace - 2 * nLeft;
                        else
                        {
                            nWidth = MINLAY;
                            nLeft = nRight = (nSpace - MINLAY) / 2;
                        }
                        break;
                    case text::HoriOrientation::NONE:
                        if (MINLAY < nSpace - nLeft - nRight)
                            nWidth = nSpace - nLeft - nRight;
                        else
                        {
                            nWidth = MINLAY;
                            //TODO: keep the previous value - if possible and reduce the 'new one' only
                            nLeft = nRight = (nSpace - MINLAY) / 2;
                        }
                        break;
                    default:
                        break;
                }
                SwFormatFrameSize aSz( SwFrameSize::Variable, nWidth );
                aSet.Put(aSz);

                aLRSpace.SetLeft(SvxIndentValue::twips(nLeft));
                aLRSpace.SetRight(SvxIndentValue::twips(nRight));
                aSet.Put( aLRSpace );

                aSet.Put( aAttr );
                rSh.SetTableAttr( aSet );
                rSh.EndUndo(SwUndoId::TABLE_ATTR);
                static const sal_uInt16 aInva[] =
                                {   SID_ATTR_TABLE_LEFT_SPACE,
                                    SID_ATTR_TABLE_RIGHT_SPACE,
                                    0
                                };
                GetView().GetViewFrame().GetBindings().Invalidate( aInva );
            }
            return;
        }
        default:
            bMore = true;
    }

    if ( !bMore )
    {
        if(bCallDone)
            rReq.Done();
        return;
    }

    // Now the slots which are working directly on the TableFormat.
    switch ( nSlot )
    {
        case SID_ATTR_ULSPACE:
            if(pItem)
            {
                SvxULSpaceItem aULSpace( *static_cast<const SvxULSpaceItem*>(pItem) );
                aULSpace.SetWhich( RES_UL_SPACE );
                ::lcl_SetAttr( rSh, aULSpace );
            }
            break;

        case SID_ATTR_LRSPACE:
            if(pItem)
            {
                SfxItemSetFixed<RES_LR_SPACE, RES_LR_SPACE,
                                RES_HORI_ORIENT, RES_HORI_ORIENT>  aSet( GetPool() );
                SvxLRSpaceItem aLRSpace( *static_cast<const SvxLRSpaceItem*>(pItem) );
                aLRSpace.SetWhich( RES_LR_SPACE );
                aSet.Put( aLRSpace );
                rSh.SetTableAttr( aSet );
            }
            break;
        // The last case branch which needs a table manager!!
        case FN_TABLE_SET_COL_WIDTH:
        {
            // Adjust line height (dialogue)
            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            VclPtr<AbstractSwTableWidthDlg> pDlg(pFact->CreateSwTableWidthDlg(GetView().GetFrameWeld(), &rSh));
            pDlg->StartExecuteAsync(
                [pDlg] (sal_Int32 nResult)->void
                {
                    if (nResult == RET_OK)
                        pDlg->Apply();
                    pDlg->disposeOnce();
                }
            );
            break;
        }
        case SID_TABLE_VERT_NONE:
        case SID_TABLE_VERT_CENTER:
        case SID_TABLE_VERT_BOTTOM:
        {
            const sal_uInt16 nAlign = nSlot == SID_TABLE_VERT_NONE ?
                                text::VertOrientation::NONE :
                                    nSlot == SID_TABLE_VERT_CENTER ?
                                        text::VertOrientation::CENTER : text::VertOrientation::BOTTOM;
            rSh.SetBoxAlign(nAlign);
            bCallDone = true;
            break;
        }

        case SID_ATTR_PARA_SPLIT:
            if ( pItem )
            {
                SwFormatLayoutSplit aSplit( static_cast<const SvxFormatSplitItem*>(pItem)->GetValue());
                SfxItemSetFixed<RES_LAYOUT_SPLIT, RES_LAYOUT_SPLIT> aSet(GetPool());
                aSet.Put(aSplit);
                rSh.SetTableAttr(aSet);
            }
            break;

        case SID_ATTR_PARA_KEEP:
            if ( pItem )
            {
                SvxFormatKeepItem aKeep( *static_cast<const SvxFormatKeepItem*>(pItem) );
                aKeep.SetWhich( RES_KEEP );
                SfxItemSetFixed<RES_KEEP, RES_KEEP> aSet(GetPool());
                aSet.Put(aKeep);
                rSh.SetTableAttr(aSet);
            }
            break;
        case FN_TABLE_ROW_SPLIT :
        {
            const SfxBoolItem* pBool = static_cast<const SfxBoolItem*>(pItem);
            std::unique_ptr<SwFormatRowSplit> pSplit;
            if(!pBool)
            {
                pSplit = rSh.GetRowSplit();
                if(pSplit)
                    pSplit->SetValue(!pSplit->GetValue());
                else
                   pSplit.reset(new SwFormatRowSplit(true));
            }
            else
            {
                pSplit.reset(new SwFormatRowSplit(pBool->GetValue()));
            }
            rSh.SetRowSplit( *pSplit );
            break;
        }

        default:
            OSL_ENSURE( false, "wrong Dispatcher" );
            return;
    }
    if(bCallDone)
        rReq.Done();
}

void SwTableShell::GetState(SfxItemSet &rSet)
{
    SfxWhichIter aIter( rSet );
    SwWrtShell &rSh = GetShell();
    SwFrameFormat *pFormat = rSh.GetTableFormat();
    // os #124829# crash report: in case of an invalid shell selection return immediately
    if(!pFormat)
        return;
    sal_uInt16 nSlot = aIter.FirstWhich();
    while ( nSlot )
    {
        switch ( nSlot )
        {
            case FN_TABLE_MERGE_CELLS:
                if ( !rSh.IsTableMode() )
                    rSet.DisableItem(FN_TABLE_MERGE_CELLS);
                break;
            case SID_TABLE_MINIMAL_COLUMN_WIDTH:
            case FN_TABLE_ADJUST_CELLS:
                if ( !rSh.IsAdjustCellWidthAllowed() )
                    rSet.DisableItem(nSlot);
                break;

            case FN_TABLE_BALANCE_CELLS:
                if ( !rSh.IsAdjustCellWidthAllowed(true) )
                    rSet.DisableItem(FN_TABLE_BALANCE_CELLS);
                break;

            case FN_TABLE_OPTIMAL_HEIGHT:
            case FN_TABLE_BALANCE_ROWS:
                if ( !rSh.BalanceRowHeight(true) )
                    rSet.DisableItem(nSlot);
                break;
            case FN_OPTIMIZE_TABLE:
                if ( !rSh.IsTableMode() &&
                        !rSh.IsAdjustCellWidthAllowed() &&
                        !rSh.IsAdjustCellWidthAllowed(true) &&
                        !rSh.BalanceRowHeight(true) )
                    rSet.DisableItem(FN_OPTIMIZE_TABLE);
            break;
            case SID_INSERT_DIAGRAM:
                {
                    SvtModuleOptions aMOpt;
                    if ( !aMOpt.IsMathInstalled() || rSh.IsTableComplexForChart() )
                        rSet.DisableItem(nSlot);
                }
                break;

            case FN_INSERT_TABLE:
                if ( rSh.CursorInsideInputField() )
                {
                    rSet.DisableItem( nSlot );
                }
                break;

            case SID_TABLE_MINIMAL_ROW_HEIGHT:
            {
                // Disable if auto height already is enabled.
                std::unique_ptr<SwFormatFrameSize> pSz = rSh.GetRowHeight();
                if ( pSz )
                {
                    if ( SwFrameSize::Variable == pSz->GetHeightSizeType() )
                        rSet.DisableItem( nSlot );
                }
                break;
            }
            case FN_TABLE_INSERT_COL_BEFORE:
            case FN_TABLE_INSERT_COL_AFTER:
            {
                SfxImageItem aImageItem(nSlot);
                if (pFormat->GetFrameDir().GetValue() == SvxFrameDirection::Environment)
                {
                    // Inherited from superordinate object (page or frame).
                    // If the table spans multiple pages, direction is set by the first page.
                    SwIterator<SwTabFrame, SwFrameFormat> aIterT(*pFormat);
                    for (SwTabFrame* pFrame = aIterT.First(); pFrame;
                        pFrame = static_cast<SwTabFrame*>(pFrame->GetPrecede()))
                        aImageItem.SetMirrored(pFrame->IsRightToLeft());
                }
                else
                    aImageItem.SetMirrored(pFormat->GetFrameDir().GetValue() == SvxFrameDirection::Horizontal_RL_TB);
                rSet.Put(aImageItem);
                break;
            }
            case FN_TABLE_INSERT_ROW:
            case FN_TABLE_INSERT_ROW_AFTER:
            case FN_TABLE_INSERT_ROW_DLG:
                if ( rSh.IsInRepeatedHeadline() )
                    rSet.DisableItem( nSlot );
                break;
            case RES_LR_SPACE:
                rSet.Put(pFormat->GetLRSpace());
                break;
            case RES_UL_SPACE:
                rSet.Put(pFormat->GetULSpace());
                break;

            case SID_TABLE_VERT_NONE:
            case SID_TABLE_VERT_CENTER:
            case SID_TABLE_VERT_BOTTOM:
            {
                const sal_uInt16 nAlign = rSh.GetBoxAlign();
                bool bSet = (nSlot == SID_TABLE_VERT_NONE && nAlign == text::VertOrientation::NONE) ||
                            (nSlot == SID_TABLE_VERT_CENTER && nAlign == text::VertOrientation::CENTER) ||
                            (nSlot == SID_TABLE_VERT_BOTTOM && nAlign == text::VertOrientation::BOTTOM);
                rSet.Put(SfxBoolItem(nSlot, bSet));
                break;
            }

            case FN_TABLE_MODE_FIX       :
            case FN_TABLE_MODE_FIX_PROP  :
            case FN_TABLE_MODE_VARIABLE  :
                {
                    TableChgMode nMode = rSh.GetTableChgMode();
                    bool bSet = (nSlot == FN_TABLE_MODE_FIX && nMode == TableChgMode::FixedWidthChangeAbs) ||
                            (nSlot == FN_TABLE_MODE_FIX_PROP && nMode == TableChgMode::FixedWidthChangeProp) ||
                            (nSlot == FN_TABLE_MODE_VARIABLE && nMode == TableChgMode::VarWidthChangeAbs);
                    rSet.Put(SfxBoolItem(nSlot, bSet));
                }
                break;
            case FN_BREAK_ABOVE_TABLE:
                {
                    // exec just moves on top and adds the break, which however makes only sense if the table
                    // is the very first item of the document; the command should be hidden otherwise
                    SwContentFrame* curFrame = rSh.GetCurrFrame();
                    SwPageFrame* pageFrame = curFrame->FindPageFrame();
                    SwFrame* frame = pageFrame->Lower();

                    while(!frame->IsContentFrame())
                    {
                        frame = frame->GetLower();
                    }

                    if(frame->FindTabFrame() != curFrame->FindTabFrame())
                    {
                        rSet.DisableItem(nSlot);
                    }
                }
                break;
            case SID_ATTR_PARA_SPLIT:
                rSet.Put( pFormat->GetKeep() );
                break;

            case SID_ATTR_PARA_KEEP:
                rSet.Put( pFormat->GetLayoutSplit() );
                break;
            case FN_TABLE_SPLIT_TABLE:
                if ( rSh.IsInHeadline() )
                    rSet.DisableItem( nSlot );
                break;
            case FN_TABLE_MERGE_TABLE:
            {
                bool bAsk;
                if( !rSh.CanMergeTable( true, &bAsk ))
                    rSet.DisableItem( nSlot );
                break;
            }

            case FN_TABLE_DELETE_ROW:
                {
                    SwSelBoxes aBoxes;
                    ::GetTableSel( rSh, aBoxes, SwTableSearchType::Row );
                    if( ::HasProtectedCells( aBoxes ) || lcl_BoxesInTrackedRows( rSh, aBoxes ) )
                        rSet.DisableItem( nSlot );
                }
                break;
            case FN_TABLE_DELETE_COL:
                {
                    SwSelBoxes aBoxes;
                    ::GetTableSel( rSh, aBoxes, SwTableSearchType::Col );
                    if( ::HasProtectedCells( aBoxes ) || lcl_CursorInDeletedTable( rSh ) )
                        rSet.DisableItem( nSlot );
                }
                break;
            case FN_TABLE_DELETE_TABLE:
                if( lcl_CursorInDeletedTable( rSh ) )
                    rSet.DisableItem( nSlot );
                break;

            case FN_TABLE_UNSET_READ_ONLY_CELLS:
                // disable in readonly sections, but enable in protected cells
                if( !rSh.CanUnProtectCells() )
                    rSet.DisableItem( nSlot );
                break;
            case RES_ROW_SPLIT:
            {
                const SwFormatLayoutSplit& rTabSplit = pFormat->GetLayoutSplit();
                if ( !rTabSplit.GetValue() )
                {
                    rSet.DisableItem( nSlot );
                }
                else
                {
                    std::unique_ptr<SwFormatRowSplit> pSplit = rSh.GetRowSplit();
                    if(pSplit)
                        rSet.Put(std::move(pSplit));
                    else
                        rSet.InvalidateItem( nSlot );
                }
                break;
            }
            case FN_TABLE_HEADLINE_REPEAT:
                if(0 != rSh.GetRowsToRepeat())
                    rSet.Put(SfxBoolItem(nSlot, true));
                else if(!rSh.GetRowSelectionFromTop())
                    rSet.DisableItem( nSlot );
                else
                    rSet.Put(SfxBoolItem(nSlot, false));
                break;
            case FN_TABLE_SELECT_CELL   :
                if(rSh.HasBoxSelection())
                    rSet.DisableItem( nSlot );
                break;
            case SID_ATTR_TABLE_ROW_HEIGHT:
            {
                SfxUInt32Item aRowHeight(SID_ATTR_TABLE_ROW_HEIGHT);
                std::unique_ptr<SwFormatFrameSize> pHeight = rSh.GetRowHeight();
                if (pHeight)
                {
                    tools::Long nHeight = pHeight->GetHeight();
                    aRowHeight.SetValue(nHeight);
                    rSet.Put(aRowHeight);

                    if (comphelper::LibreOfficeKit::isActive())
                    {
                        // TODO: set correct unit
                        MapUnit eTargetUnit = MapUnit::MapInch;
                        OUString sHeight = GetMetricText(nHeight,
                                            MapUnit::MapTwip, eTargetUnit, nullptr);

                        OUString sPayload = ".uno:TableRowHeight=" + sHeight;

                        GetViewShell()->libreOfficeKitViewCallback(LOK_CALLBACK_STATE_CHANGED,
                            OUStringToOString(sPayload, RTL_TEXTENCODING_ASCII_US));
                    }
                }
                break;
            }
            case SID_ATTR_TABLE_COLUMN_WIDTH:
            {
                SfxUInt32Item aColumnWidth(SID_ATTR_TABLE_COLUMN_WIDTH);
                SwTableFUNC aFunc( &rSh );
                aFunc.InitTabCols();
                SwTwips nWidth = aFunc.GetColWidth(aFunc.GetCurColNum());
                aColumnWidth.SetValue(nWidth);
                rSet.Put(aColumnWidth);

                if (comphelper::LibreOfficeKit::isActive())
                {
                    // TODO: set correct unit
                    MapUnit eTargetUnit = MapUnit::MapInch;
                    OUString sWidth = GetMetricText(nWidth,
                                        MapUnit::MapTwip, eTargetUnit, nullptr);

                    OUString sPayload = ".uno:TableColumWidth=" + sWidth;

                    GetViewShell()->libreOfficeKitViewCallback(LOK_CALLBACK_STATE_CHANGED,
                        OUStringToOString(sPayload, RTL_TEXTENCODING_ASCII_US));
                }

                break;
            }
            case SID_ATTR_TABLE_ALIGNMENT:
            {
                const sal_uInt16 nAlign = pFormat->GetHoriOrient().GetHoriOrient();
                rSet.Put(SfxUInt16Item(nSlot, nAlign));
                break;
            }
            case SID_ATTR_TABLE_LEFT_SPACE:
            case SID_ATTR_TABLE_RIGHT_SPACE:
            {
                SwTabCols aTabCols;
                rSh.GetTabCols(aTabCols);
                tools::Long nSpace = aTabCols.GetRightMax();
                SvxLRSpaceItem aLRSpace(pFormat->GetLRSpace());
                SwTwips nLeft = aLRSpace.ResolveLeft({});
                SwTwips nRight = aLRSpace.ResolveRight({});

                sal_uInt16 nPercent = 0;
                auto nWidth = ::GetTableWidth(pFormat, aTabCols, &nPercent, &rSh );
                // The table width is wrong for relative values.
                if (nPercent)
                    nWidth = nSpace * nPercent / 100;
                const sal_uInt16 nAlign = pFormat->GetHoriOrient().GetHoriOrient();
                if(nAlign != text::HoriOrientation::FULL )
                {
                    SwTwips nLR = nSpace - nWidth;
                    switch ( nAlign )
                    {
                        case text::HoriOrientation::CENTER:
                            nLeft = nRight = nLR / 2;
                            break;
                        case text::HoriOrientation::LEFT:
                            nRight = nLR;
                            nLeft = 0;
                            break;
                        case text::HoriOrientation::RIGHT:
                            nLeft = nLR;
                            nRight = 0;
                            break;
                        case text::HoriOrientation::LEFT_AND_WIDTH:
                            nRight = nLR - nLeft;
                            break;
                        case text::HoriOrientation::NONE:
                            if(!nPercent)
                                nWidth = nSpace - nLeft - nRight;
                            break;
                    }
                }
                rSet.Put(SfxInt32Item(SID_ATTR_TABLE_LEFT_SPACE, nLeft));
                rSet.Put(SfxInt32Item(SID_ATTR_TABLE_RIGHT_SPACE, nRight));
                break;
            }
        }
        nSlot = aIter.NextWhich();
    }
}

SwTableShell::SwTableShell(SwView &_rView) :
    SwBaseShell(_rView)
{
    SetName(u"Table"_ustr);
    SfxShell::SetContextName(vcl::EnumContext::GetContextName(vcl::EnumContext::Context::Table));
}

void SwTableShell::GetFrameBorderState(SfxItemSet &rSet)
{
    SfxItemSetFixed<RES_BOX, RES_BOX,
                 SID_ATTR_BORDER_INNER, SID_ATTR_BORDER_INNER>  aCoreSet( GetPool() );
    SvxBoxInfoItem aBoxInfo( SID_ATTR_BORDER_INNER );
    aCoreSet.Put( aBoxInfo );
    GetShell().GetTabBorders( aCoreSet );
    rSet.Put( aCoreSet );
}

void SwTableShell::ExecTableStyle(SfxRequest& rReq)
{
    SwWrtShell &rSh = GetShell();
    const SfxItemSet *pArgs = rReq.GetArgs();
    if(!pArgs)
        return;

    switch ( rReq.GetSlot() )
    {
        case SID_FRAME_LINESTYLE:
        case SID_FRAME_LINECOLOR:
            if ( rReq.GetSlot() == SID_FRAME_LINESTYLE )
            {
                const SvxLineItem &rLineItem = pArgs->Get( SID_FRAME_LINESTYLE );
                const SvxBorderLine* pBorderLine = rLineItem.GetLine();
                rSh.SetTabLineStyle( nullptr, true, pBorderLine);
            }
            else
            {
                const SvxColorItem &rNewColorItem = pArgs->Get( SID_FRAME_LINECOLOR );
                rSh.SetTabLineStyle( &rNewColorItem.GetValue() );
            }

            rReq.Done();

            break;
    }
}

void SwTableShell::GetLineStyleState(SfxItemSet &rSet)
{
    SfxItemSetFixed<RES_BOX, RES_BOX,
                    SID_ATTR_BORDER_INNER, SID_ATTR_BORDER_INNER>  aCoreSet( GetPool() );
    SvxBoxInfoItem aCoreInfo( SID_ATTR_BORDER_INNER );
    aCoreSet.Put(aCoreInfo);
    GetShell().GetTabBorders( aCoreSet );

    const SvxBoxItem& rBoxItem = aCoreSet.Get( RES_BOX );
    const SvxBorderLine* pLine = rBoxItem.GetTop();

    rSet.Put( SvxColorItem( pLine ? pLine->GetColor() : Color(), SID_FRAME_LINECOLOR ) );
    SvxLineItem aLine( SID_FRAME_LINESTYLE );
    aLine.SetLine(pLine);
    rSet.Put( aLine );
}

void SwTableShell::ExecNumberFormat(SfxRequest const & rReq)
{
    const SfxItemSet* pArgs = rReq.GetArgs();
    SwWrtShell &rSh = GetShell();

    // At first the slots, which doesn't need a FrameMgr.
    const SfxPoolItem* pItem = nullptr;
    const sal_uInt16 nSlot = rReq.GetSlot();
    if(pArgs)
        pArgs->GetItemState(GetPool().GetWhichIDFromSlotID(nSlot), false, &pItem);

    // Always acquire the language from the current cursor position.
    LanguageType eLang = rSh.GetCurLang();
    SvNumberFormatter* pFormatter = rSh.GetNumberFormatter();
    sal_uInt32 nNumberFormat = NUMBERFORMAT_ENTRY_NOT_FOUND;
    SvNumFormatType nFormatType = SvNumFormatType::ALL;
    sal_uInt16 nOffset = 0;

    switch ( nSlot )
    {
    case FN_NUMBER_FORMAT:
        if( pItem )
        {
            // Determine index for string.
            OUString aCode( static_cast<const SfxStringItem*>(pItem)->GetValue() );
            nNumberFormat = pFormatter->GetEntryKey( aCode, eLang );
            if( NUMBERFORMAT_ENTRY_NOT_FOUND == nNumberFormat )
            {
                // Re-enter
                sal_Int32 nErrPos;
                SvNumFormatType nType;
                if( !pFormatter->PutEntry( aCode, nErrPos, nType,
                                            nNumberFormat, eLang ))
                    nNumberFormat = NUMBERFORMAT_ENTRY_NOT_FOUND;
            }
        }
        break;
    case FN_NUMBER_STANDARD:        nFormatType = SvNumFormatType::NUMBER; break;
    case FN_NUMBER_SCIENTIFIC:      nFormatType = SvNumFormatType::SCIENTIFIC; break;
    case FN_NUMBER_DATE:            nFormatType = SvNumFormatType::DATE; break;
    case FN_NUMBER_TIME:            nFormatType = SvNumFormatType::TIME; break;
    case FN_NUMBER_CURRENCY:        nFormatType = SvNumFormatType::CURRENCY; break;
    case FN_NUMBER_PERCENT:         nFormatType = SvNumFormatType::PERCENT; break;

    case FN_NUMBER_TWODEC:          // #.##0,00
        nFormatType = SvNumFormatType::NUMBER;
        nOffset = NF_NUMBER_1000DEC2;
        break;

    default:
        OSL_FAIL("wrong dispatcher");
        return;
    }

    if( nFormatType != SvNumFormatType::ALL )
        nNumberFormat = pFormatter->GetStandardFormat( nFormatType, eLang ) + nOffset;

    if( NUMBERFORMAT_ENTRY_NOT_FOUND != nNumberFormat )
    {
        SfxItemSetFixed<RES_BOXATR_FORMAT, RES_BOXATR_FORMAT> aBoxSet( GetPool() );
        aBoxSet.Put( SwTableBoxNumFormat( nNumberFormat ));
        rSh.SetTableBoxFormulaAttrs( aBoxSet );
    }

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
