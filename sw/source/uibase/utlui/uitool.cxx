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

#include <osl/diagnose.h>
#include <tools/datetime.hxx>
#include <vcl/weld.hxx>
#include <unotools/collatorwrapper.hxx>
#include <svl/stritem.hxx>
#include <svl/grabbagitem.hxx>
#include <unotools/syslocale.hxx>
#include <IDocumentStylePoolAccess.hxx>
#include <editeng/pmdlitem.hxx>
#include <editeng/tstpitem.hxx>
#include <editeng/boxitem.hxx>
#include <editeng/sizeitem.hxx>
#include <editeng/brushitem.hxx>
#include <svx/pageitem.hxx>
#include <editeng/lrspitem.hxx>
#include <svl/style.hxx>
#include <unotools/localedatawrapper.hxx>
#include <com/sun/star/awt/XPopupMenu.hpp>
#include <com/sun/star/frame/XDispatch.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/util/URLTransformer.hpp>
#include <com/sun/star/util/XURLTransformer.hpp>
#include <comphelper/processfactory.hxx>
#include <sfx2/viewfrm.hxx>
#include <sfx2/docfile.hxx>
#include <sfx2/docfilt.hxx>
#include <fmtornt.hxx>
#include <tabcol.hxx>
#include <fmtfsize.hxx>
#include <fmthdft.hxx>
#include <fmtpdsc.hxx>
#include <uiitems.hxx>
#include <docsh.hxx>
#include <wrtsh.hxx>
#include <swmodule.hxx>
#include <view.hxx>
#include <uitool.hxx>
#include <frmatr.hxx>
#include <paratr.hxx>
#include <fmtcol.hxx>
#include <usrpref.hxx>

#include <cmdid.h>
#include <doc.hxx>
#include <charfmt.hxx>
#include <SwStyleNameMapper.hxx>
#include <strings.hrc>
#include <docmodel/color/ComplexColor.hxx>
#include <IDocumentSettingAccess.hxx>

// 50 cm 28350
#define MAXHEIGHT 28350
#define MAXWIDTH  28350

using namespace ::com::sun::star;

// General list of string pointer

// Set boxinfo attribute

void PrepareBoxInfo(SfxItemSet& rSet, const SwWrtShell& rSh)
{
    std::shared_ptr<SvxBoxInfoItem> aBoxInfo(std::make_shared<SvxBoxInfoItem>(SID_ATTR_BORDER_INNER));

    if ( const SvxBoxInfoItem *pBoxInfo = rSet.GetItemIfSet( SID_ATTR_BORDER_INNER ))
    {
        aBoxInfo.reset(pBoxInfo->Clone());
    }

        // Table variant: If more than one table cells are selected
    rSh.GetCursor();                  //So that GetCursorCnt() returns the right thing
    aBoxInfo->SetTable          (rSh.IsTableMode() && rSh.GetCursorCnt() > 1);
        // Always show the distance field
    aBoxInfo->SetDist           (true);
        // Set minimal size in tables and paragraphs
    aBoxInfo->SetMinDist        (rSh.IsTableMode() || rSh.GetSelectionType() & (SelectionType::Text | SelectionType::Table));
        // Set always the default distance
    aBoxInfo->SetDefDist        (MIN_BORDER_DIST);
        // Single lines can have only in tables DontCare-Status
    aBoxInfo->SetValid(SvxBoxInfoItemValidFlags::DISABLE, !rSh.IsTableMode());

    rSet.Put(*aBoxInfo);
}

void ConvertAttrCharToGen(SfxItemSet& rSet, bool bIsPara)
{
    // Background / highlight
    {
        // Always use the visible background
        if( const SvxBrushItem *pTmpBrush = rSet.GetItemIfSet( RES_CHRATR_HIGHLIGHT ) )
        {
            SvxBrushItem aTmpBrush( *pTmpBrush );
            if( aTmpBrush.GetColor() != COL_TRANSPARENT )
            {
                aTmpBrush.SetWhich( RES_CHRATR_BACKGROUND );
                rSet.Put( aTmpBrush );
            }
        }
    }

    if ( bIsPara )
        return;

    // Tell dialogs to use character-specific slots/whichIds
    // tdf#126684: We use RES_PARATR_GRABBAG, because RES_CHRATR_GRABBAG may be overwritten later in
    // SwDocStyleSheet::GetItemSet when applying attributes from char format
    assert(SfxItemState::SET != rSet.GetItemState(RES_PARATR_GRABBAG, false));
    std::map<OUString, css::uno::Any> aGrabBagMap;
    aGrabBagMap[u"DialogUseCharAttr"_ustr] <<= true;
    // Store initial ranges to allow restoring later
    uno::Sequence<sal_uInt16> aOrigRanges(rSet.GetRanges().size() * 2 + 1);
    int i = 0;
    for (const auto& rPair : rSet.GetRanges())
    {
        aOrigRanges.getArray()[i++] = rPair.first;
        aOrigRanges.getArray()[i++] = rPair.second;
    }
    aOrigRanges.getArray()[i++] = 0;
    aGrabBagMap[u"OrigItemSetRanges"_ustr] <<= aOrigRanges;
    rSet.MergeRange(RES_PARATR_GRABBAG, RES_PARATR_GRABBAG);
    rSet.Put(SfxGrabBagItem(RES_PARATR_GRABBAG, std::move(aGrabBagMap)));
}

void ConvertAttrGenToChar(SfxItemSet& rSet, const SfxItemSet& rOrigSet, bool bIsPara)
{
    // Background / highlighting
    if( SfxItemState::SET == rSet.GetItemState( RES_CHRATR_BACKGROUND, false ) )
    {
        // Highlight is an MS specific thing, so remove it at the first time when LO modifies
        // this part of the imported document.
        rSet.Put( SvxBrushItem(RES_CHRATR_HIGHLIGHT) );

        // Remove shading marker
        if (const SfxGrabBagItem* pGrabBagItem = rOrigSet.GetItemIfSet(RES_CHRATR_GRABBAG, false))
        {
            if( pGrabBagItem->GetGrabBag().count(u"CharShadingMarker"_ustr) )
            {
                std::map<OUString, css::uno::Any> aGrabBagMap = pGrabBagItem->GetGrabBag();
                aGrabBagMap[u"CharShadingMarker"_ustr] <<= false;
                rSet.Put( SfxGrabBagItem(RES_CHRATR_GRABBAG, std::move(aGrabBagMap)) );
            }
        }
    }

    if ( bIsPara )
        return;

    rSet.ClearItem( RES_BACKGROUND );

    if (const SfxGrabBagItem* pGrabBagItem = rOrigSet.GetItemIfSet(RES_PARATR_GRABBAG, false))
    {
        const std::map<OUString, css::uno::Any>& rMap = pGrabBagItem->GetGrabBag();
        auto aIterator = rMap.find(u"OrigItemSetRanges"_ustr);
        if (aIterator != rMap.end())
        {
            uno::Sequence<sal_uInt16> aOrigRanges;
            if ( aIterator->second >>= aOrigRanges )
            {
                assert(aOrigRanges.getLength() % 2 == 1);
                int numPairs = (aOrigRanges.getLength()-1)/2;
                std::unique_ptr<WhichPair[]> xPairs(new WhichPair[numPairs]);
                for(int i=0; i<aOrigRanges.getLength()-1; i += 2)
                {
                    xPairs[i/2] = { aOrigRanges[i], aOrigRanges[i+1] };
                }
                rSet.SetRanges(WhichRangesContainer(std::move(xPairs), numPairs));
            }
        }
    }
    assert(SfxItemState::SET != rSet.GetItemState(RES_PARATR_GRABBAG, false));
}

void ApplyCharBackground(Color const& rBackgroundColor, model::ComplexColor const& rComplexColor, SwWrtShell& rShell)
{
    rShell.StartUndo(SwUndoId::INSATTR);

    SfxItemSetFixed<RES_CHRATR_GRABBAG, RES_CHRATR_GRABBAG> aCoreSet(rShell.GetView().GetPool());

    rShell.GetCurAttr(aCoreSet);

    // Set char background
    rShell.SetAttrItem(SvxBrushItem(rBackgroundColor, rComplexColor, RES_CHRATR_BACKGROUND));

    // Highlight is an MS specific thing, so remove it at the first time when LO modifies
    // this part of the imported document.
    rShell.SetAttrItem(SvxBrushItem(RES_CHRATR_HIGHLIGHT));

    // Remove shading marker
    if (const SfxGrabBagItem* pGrabBagItem = aCoreSet.GetItemIfSet(RES_CHRATR_GRABBAG, false))
    {
        if (pGrabBagItem->GetGrabBag().count(u"CharShadingMarker"_ustr))
        {
            std::map<OUString, css::uno::Any> aGrabBagMap = pGrabBagItem->GetGrabBag();
            aGrabBagMap[u"CharShadingMarker"_ustr] <<= false;
            rShell.SetAttrItem(SfxGrabBagItem(RES_CHRATR_GRABBAG, std::move(aGrabBagMap)));
        }
    }

    rShell.EndUndo(SwUndoId::INSATTR);
}

// Fill header footer

static void FillHdFt(SwFrameFormat* pFormat, const  SfxItemSet& rSet)
{
    SwAttrSet aSet(pFormat->GetAttrSet());
    aSet.Put(rSet);

    const SvxSizeItem& rSize = rSet.Get(SID_ATTR_PAGE_SIZE);
    const SfxBoolItem& rDynamic = rSet.Get(SID_ATTR_PAGE_DYNAMIC);

    // Convert size
    SwFormatFrameSize aFrameSize(rDynamic.GetValue() ? SwFrameSize::Minimum : SwFrameSize::Fixed,
                            rSize.GetSize().Width(),
                            rSize.GetSize().Height());
    aSet.Put(aFrameSize);
    pFormat->SetFormatAttr(aSet);
}

/// Convert from UseOnPage to SvxPageUsage.
static SvxPageUsage lcl_convertUseToSvx(UseOnPage nUse)
{
    SvxPageUsage nRet = SvxPageUsage::NONE;
    if (nUse & UseOnPage::Left)
        nRet = SvxPageUsage::Left;
    if (nUse & UseOnPage::Right)
        nRet = SvxPageUsage::Right;
    if ((nUse & UseOnPage::All) == UseOnPage::All)
        nRet = SvxPageUsage::All;
    if ((nUse & UseOnPage::Mirror) == UseOnPage::Mirror)
        nRet = SvxPageUsage::Mirror;
    return nRet;
}

/// Convert from SvxPageUsage to UseOnPage.
static UseOnPage lcl_convertUseFromSvx(SvxPageUsage nUse)
{
    UseOnPage nRet = UseOnPage::NONE;
    if (nUse == SvxPageUsage::Left)
        nRet = UseOnPage::Left;
    else if (nUse == SvxPageUsage::Right)
        nRet = UseOnPage::Right;
    else if (nUse == SvxPageUsage::All)
        nRet = UseOnPage::All;
    else if (nUse == SvxPageUsage::Mirror)
        nRet = UseOnPage::Mirror;
    return nRet;
}

// PageDesc <-> convert into sets and back

void ItemSetToPageDesc( const SfxItemSet& rSet, SwPageDesc& rPageDesc )
{
    SwFrameFormat& rMaster = rPageDesc.GetMaster();
    bool bFirstShare = false;

    // before SetFormatAttr() in case it contains RES_BACKGROUND_FULL_SIZE
    // itself, as it does when called from SwXPageStyle
    if (const SfxGrabBagItem* pGrabBag = rSet.GetItemIfSet(SID_ATTR_CHAR_GRABBAG))
    {
        bool bValue;
        const auto& rGrabBagInner = pGrabBag->GetGrabBag();
        const auto iter = rGrabBagInner.find(u"BackgroundFullSize"_ustr);
        assert(iter != rGrabBagInner.end());
        if (iter->second >>= bValue)
        {
            rMaster.SetFormatAttr(SfxBoolItem(RES_BACKGROUND_FULL_SIZE, bValue));
        }
        auto it = rGrabBagInner.find(u"RtlGutter"_ustr);
        if (it != rGrabBagInner.end() && (it->second >>= bValue))
        {
            rMaster.SetFormatAttr(SfxBoolItem(RES_RTL_GUTTER, bValue));
        }
    }

    // Transfer all general frame attributes
    rMaster.SetFormatAttr(rSet);

    // PageData
    if(rSet.GetItemState(SID_ATTR_PAGE) == SfxItemState::SET)
    {
        const SvxPageItem& rPageItem = rSet.Get(SID_ATTR_PAGE);

        const SvxPageUsage nUse = rPageItem.GetPageUsage();
        if(nUse != SvxPageUsage::NONE)
            rPageDesc.SetUseOn( lcl_convertUseFromSvx(nUse) );
        rPageDesc.SetLandscape(rPageItem.IsLandscape());
        SvxNumberType aNumType;
        aNumType.SetNumberingType( rPageItem.GetNumType() );
        rPageDesc.SetNumType(aNumType);
    }
    // Size
    if(rSet.GetItemState(SID_ATTR_PAGE_SIZE) == SfxItemState::SET)
    {
        const SvxSizeItem& rSizeItem = rSet.Get(SID_ATTR_PAGE_SIZE);
        SwFormatFrameSize aSize(SwFrameSize::Fixed);
        aSize.SetSize(rSizeItem.GetSize());
        rMaster.SetFormatAttr(aSize);
    }
    // Evaluate header attributes
    if( const SvxSetItem* pHeaderSetItem = rSet.GetItemIfSet( SID_ATTR_PAGE_HEADERSET,
            false ) )
    {
        const SfxItemSet& rHeaderSet = pHeaderSetItem->GetItemSet();
        const SfxBoolItem& rHeaderOn = rHeaderSet.Get(SID_ATTR_PAGE_ON);

        if(rHeaderOn.GetValue())
        {
            // Take over values
            if(!rMaster.GetHeader().IsActive())
                rMaster.SetFormatAttr(SwFormatHeader(true));

            // Pick out everything and adapt the header format
            SwFormatHeader aHeaderFormat(rMaster.GetHeader());
            SwFrameFormat *pHeaderFormat = aHeaderFormat.GetHeaderFormat();
            OSL_ENSURE(pHeaderFormat != nullptr, "no header format");

            ::FillHdFt(pHeaderFormat, rHeaderSet);

            rPageDesc.ChgHeaderShare(rHeaderSet.Get(SID_ATTR_PAGE_SHARED).GetValue());
            rPageDesc.ChgFirstShare(static_cast<const SfxBoolItem&>(
                            rHeaderSet.Get(SID_ATTR_PAGE_SHARED_FIRST)).GetValue());
            bFirstShare = true;
        }
        else
        {
            // Disable header
            if(rMaster.GetHeader().IsActive())
            {
                rMaster.SetFormatAttr(SwFormatHeader(false));
                rPageDesc.ChgHeaderShare(false);
            }
        }
    }

    // Evaluate footer attributes
    if( const SvxSetItem* pFooterSetItem = rSet.GetItemIfSet( SID_ATTR_PAGE_FOOTERSET,
            false ) )
    {
        const SfxItemSet& rFooterSet = pFooterSetItem->GetItemSet();
        const SfxBoolItem& rFooterOn = rFooterSet.Get(SID_ATTR_PAGE_ON);

        if(rFooterOn.GetValue())
        {
            // Take over values
            if(!rMaster.GetFooter().IsActive())
                rMaster.SetFormatAttr(SwFormatFooter(true));

            // Pick out everything and adapt the footer format
            SwFormatFooter aFooterFormat(rMaster.GetFooter());
            SwFrameFormat *pFooterFormat = aFooterFormat.GetFooterFormat();
            OSL_ENSURE(pFooterFormat != nullptr, "no footer format");

            ::FillHdFt(pFooterFormat, rFooterSet);

            rPageDesc.ChgFooterShare(rFooterSet.Get(SID_ATTR_PAGE_SHARED).GetValue());
            if (!bFirstShare)
            {
                rPageDesc.ChgFirstShare(static_cast<const SfxBoolItem&>(
                            rFooterSet.Get(SID_ATTR_PAGE_SHARED_FIRST)).GetValue());
            }
        }
        else
        {
            // Disable footer
            if(rMaster.GetFooter().IsActive())
            {
                rMaster.SetFormatAttr(SwFormatFooter(false));
                // why reset this? but not doing it causes testTdf112694 to fail
                rPageDesc.ChgFooterShare(false);
            }
        }
    }

    // Footnotes

    if( const SwPageFootnoteInfoItem* pFootnoteItem = rSet.GetItemIfSet( FN_PARAM_FTN_INFO,
            false ) )
        rPageDesc.SetFootnoteInfo( pFootnoteItem->GetPageFootnoteInfo() );

    // Columns

    // Register compliant

    const SfxBoolItem* pRegisterModeItem = rSet.GetItemIfSet(
                            SID_SWREGISTER_MODE, false);
    if(!pRegisterModeItem)
        return;

    bool bSet = pRegisterModeItem->GetValue();
    if(!bSet)
        rPageDesc.SetRegisterFormatColl(nullptr);
    else if(const SfxStringItem* pCollectionItem = rSet.GetItemIfSet(
                            SID_SWREGISTER_COLLECTION, false))
    {
        const OUString& rColl = pCollectionItem->GetValue();
        SwDoc& rDoc = rMaster.GetDoc();
        SwTextFormatColl* pColl = rDoc.FindTextFormatCollByName( UIName(rColl) );
        if( !pColl )
        {
            const sal_uInt16 nId = SwStyleNameMapper::GetPoolIdFromUIName(
                UIName(rColl), SwGetPoolIdFromName::TxtColl );
            if( USHRT_MAX != nId )
                pColl = rDoc.getIDocumentStylePoolAccess().GetTextCollFromPool( nId );
            else
                pColl = rDoc.MakeTextFormatColl( UIName(rColl),
                            rDoc.GetDfltTextFormatColl() );
        }
        if( pColl )
            pColl->SetFormatAttr( SwRegisterItem ( true ));
        rPageDesc.SetRegisterFormatColl( pColl );
    }
}

namespace
{
bool IsOwnFormat(const SwDoc& rDoc)
{
    const SwDocShell* pDocShell = rDoc.GetDocShell();
    if (!pDocShell)
        return false;

    SfxMedium* pMedium = pDocShell->GetMedium();
    if (!pMedium)
    {
        return false;
    }

    std::shared_ptr<const SfxFilter> pFilter = pMedium->GetFilter();
    if (!pFilter)
    {
        return false;
    }

    return pFilter->IsOwnFormat();
}
}

void PageDescToItemSet( const SwPageDesc& rPageDesc, SfxItemSet& rSet)
{
    const SwFrameFormat& rMaster = rPageDesc.GetMaster();

    // Page data
    SvxPageItem aPageItem(SID_ATTR_PAGE);
    aPageItem.SetDescName(rPageDesc.GetName().toString());
    aPageItem.SetPageUsage(lcl_convertUseToSvx(rPageDesc.GetUseOn()));
    aPageItem.SetLandscape(rPageDesc.GetLandscape());
    aPageItem.SetNumType(rPageDesc.GetNumType().GetNumberingType());
    rSet.Put(aPageItem);

    // Size
    SvxSizeItem aSizeItem(SID_ATTR_PAGE_SIZE, rMaster.GetFrameSize().GetSize());
    rSet.Put(aSizeItem);

    // Maximum size
    SvxSizeItem aMaxSizeItem(SID_ATTR_PAGE_MAXSIZE, Size(MAXWIDTH, MAXHEIGHT));
    rSet.Put(aMaxSizeItem);

    // Margins, border and the other stuff.
    rSet.Put(rMaster.GetAttrSet());

    std::shared_ptr<SvxBoxInfoItem> aBoxInfo(std::make_shared<SvxBoxInfoItem>(SID_ATTR_BORDER_INNER));

    if ( const SvxBoxInfoItem *pBoxInfo = rSet.GetItemIfSet( SID_ATTR_BORDER_INNER ) )
    {
        aBoxInfo.reset(pBoxInfo->Clone());
    }

    aBoxInfo->SetTable( false );
        // Show always the distance field
    aBoxInfo->SetDist( true);
        // Set minimal size in tables and paragraphs
    aBoxInfo->SetMinDist( false );
        // Set always the default distance
    aBoxInfo->SetDefDist( MIN_BORDER_DIST );
        // Single lines can have only in tables DontCare-Status
    aBoxInfo->SetValid( SvxBoxInfoItemValidFlags::DISABLE );
    rSet.Put( *aBoxInfo );

    SfxStringItem aFollow(SID_ATTR_PAGE_EXT1, OUString());
    if(rPageDesc.GetFollow())
        aFollow.SetValue(rPageDesc.GetFollow()->GetName().toString());
    rSet.Put(aFollow);

    // Header
    if(rMaster.GetHeader().IsActive())
    {
        const SwFormatHeader &rHeaderFormat = rMaster.GetHeader();
        const SwFrameFormat *pHeaderFormat = rHeaderFormat.GetHeaderFormat();
        assert(pHeaderFormat && "no header format");

        // HeaderInfo, margins, background, border
        SfxItemSetFixed<RES_FRMATR_BEGIN,RES_FRMATR_END - 1,            // [82

            // FillAttribute support
            XATTR_FILL_FIRST, XATTR_FILL_LAST,              // [1014

            SID_ATTR_BORDER_INNER,SID_ATTR_BORDER_INNER,    // [10023
            SID_ATTR_PAGE_SIZE,SID_ATTR_PAGE_SIZE,          // [10051
            SID_ATTR_PAGE_ON,SID_ATTR_PAGE_SHARED,          // [10060
            SID_ATTR_PAGE_SHARED_FIRST,SID_ATTR_PAGE_SHARED_FIRST>  aHeaderSet(*rSet.GetPool());

        // set correct parent to get the XFILL_NONE FillStyle as needed
        aHeaderSet.SetParent(&rMaster.GetDoc().GetDfltFrameFormat()->GetAttrSet());

        // Dynamic or fixed height
        SfxBoolItem aOn(SID_ATTR_PAGE_ON, true);
        aHeaderSet.Put(aOn);

        const SwFormatFrameSize &rFrameSize = pHeaderFormat->GetFrameSize();
        const SwFrameSize eSizeType = rFrameSize.GetHeightSizeType();
        SfxBoolItem aDynamic(SID_ATTR_PAGE_DYNAMIC, eSizeType != SwFrameSize::Fixed);
        aHeaderSet.Put(aDynamic);

        // Left equal right
        SfxBoolItem aShared(SID_ATTR_PAGE_SHARED, rPageDesc.IsHeaderShared());
        aHeaderSet.Put(aShared);
        SfxBoolItem aFirstShared(SID_ATTR_PAGE_SHARED_FIRST, rPageDesc.IsFirstShared());
        aHeaderSet.Put(aFirstShared);

        // Size
        SvxSizeItem aSize(SID_ATTR_PAGE_SIZE, rFrameSize.GetSize());
        aHeaderSet.Put(aSize);

        // Shifting frame attributes
        aHeaderSet.Put(pHeaderFormat->GetAttrSet());
        aHeaderSet.Put( *aBoxInfo );

        // Create SetItem
        SvxSetItem aSetItem(SID_ATTR_PAGE_HEADERSET, aHeaderSet);
        rSet.Put(aSetItem);
    }

    // Footer
    if(rMaster.GetFooter().IsActive())
    {
        const SwFormatFooter &rFooterFormat = rMaster.GetFooter();
        const SwFrameFormat *pFooterFormat = rFooterFormat.GetFooterFormat();
        assert(pFooterFormat && "no footer format");

        // FooterInfo, margins, background, border
        SfxItemSetFixed<RES_FRMATR_BEGIN,RES_FRMATR_END - 1,            // [82

            // FillAttribute support
            XATTR_FILL_FIRST, XATTR_FILL_LAST,              // [1014

            SID_ATTR_BORDER_INNER,SID_ATTR_BORDER_INNER,    // [10023
            SID_ATTR_PAGE_SIZE,SID_ATTR_PAGE_SIZE,          // [10051
            SID_ATTR_PAGE_ON,SID_ATTR_PAGE_SHARED,          // [10060
            SID_ATTR_PAGE_SHARED_FIRST,SID_ATTR_PAGE_SHARED_FIRST>  aFooterSet(*rSet.GetPool());

        // set correct parent to get the XFILL_NONE FillStyle as needed
        aFooterSet.SetParent(&rMaster.GetDoc().GetDfltFrameFormat()->GetAttrSet());

        // Dynamic or fixed height
        SfxBoolItem aOn(SID_ATTR_PAGE_ON, true);
        aFooterSet.Put(aOn);

        const SwFormatFrameSize &rFrameSize = pFooterFormat->GetFrameSize();
        const SwFrameSize eSizeType = rFrameSize.GetHeightSizeType();
        SfxBoolItem aDynamic(SID_ATTR_PAGE_DYNAMIC, eSizeType != SwFrameSize::Fixed);
        aFooterSet.Put(aDynamic);

        // Left equal right
        SfxBoolItem aShared(SID_ATTR_PAGE_SHARED, rPageDesc.IsFooterShared());
        aFooterSet.Put(aShared);
        SfxBoolItem aFirstShared(SID_ATTR_PAGE_SHARED_FIRST, rPageDesc.IsFirstShared());
        aFooterSet.Put(aFirstShared);

        // Size
        SvxSizeItem aSize(SID_ATTR_PAGE_SIZE, rFrameSize.GetSize());
        aFooterSet.Put(aSize);

        // Shifting Frame attributes
        aFooterSet.Put(pFooterFormat->GetAttrSet());
        aFooterSet.Put( *aBoxInfo );

        // Create SetItem
        SvxSetItem aSetItem(SID_ATTR_PAGE_FOOTERSET, aFooterSet);
        rSet.Put(aSetItem);
    }

    // Integrate footnotes
    SwPageFootnoteInfo& rInfo = const_cast<SwPageFootnoteInfo&>(rPageDesc.GetFootnoteInfo());
    SwPageFootnoteInfoItem aFootnoteItem(rInfo);
    rSet.Put(aFootnoteItem);

    // Register compliant
    const SwTextFormatColl* pCol = rPageDesc.GetRegisterFormatColl();
    SwRegisterItem aReg(pCol != nullptr);
    aReg.SetWhich(SID_SWREGISTER_MODE);
    rSet.Put(aReg);
    if(pCol)
        rSet.Put(SfxStringItem(SID_SWREGISTER_COLLECTION, pCol->GetName().toString()));

    std::map<OUString, css::uno::Any> aGrabBagMap;
    if (SfxGrabBagItem const* pItem = rSet.GetItemIfSet(SID_ATTR_CHAR_GRABBAG))
        aGrabBagMap = pItem->GetGrabBag();
    aGrabBagMap[u"BackgroundFullSize"_ustr] <<=
        rMaster.GetAttrSet().GetItem<SfxBoolItem>(RES_BACKGROUND_FULL_SIZE)->GetValue();

    if (IsOwnFormat(rMaster.GetDoc()))
    {
        aGrabBagMap[u"RtlGutter"_ustr]
            <<= rMaster.GetAttrSet().GetItem<SfxBoolItem>(RES_RTL_GUTTER)->GetValue();
    }

    const IDocumentSettingAccess& rIDSA = rMaster.getIDocumentSettingAccess();
    if (rIDSA.get(DocumentSettingId::CONTINUOUS_ENDNOTES))
    {
        aGrabBagMap[u"ContinuousEndnotes"_ustr] <<= true;
    }

    rSet.Put(SfxGrabBagItem(SID_ATTR_CHAR_GRABBAG, std::move(aGrabBagMap)));
}

// Set DefaultTabs

void MakeDefTabs(SwTwips nDefDist, SvxTabStopItem& rTabs)
{
    if( rTabs.Count() )
        return;
    {
        SvxTabStop aSwTabStop( nDefDist, SvxTabAdjust::Default );
        rTabs.Insert( aSwTabStop );
    }
}

// Distance between two tabs

SwTwips GetTabDist(const SvxTabStopItem& rTabs)
{
    return rTabs.Count() ? rTabs[0].GetTabPos() : 1134; // 1134 = 2 cm
}

// Inquire if in the set is a Sfx-PageDesc combination present and return it.
void SfxToSwPageDescAttr( const SwWrtShell& rShell, SfxItemSet& rSet )
{
    const SfxPoolItem* pItem;
    SwFormatPageDesc aPgDesc;

    bool bChanged = false;
    bool bRemoveNumOffset = false;
    // Page number
    switch (rSet.GetItemState(SID_ATTR_PARA_PAGENUM, false, &pItem))
    {
        case SfxItemState::SET:
        {
            aPgDesc.SetNumOffset(static_cast<const SfxUInt16Item*>(pItem)->GetValue());
            bChanged = true;
            break;
        }
        case SfxItemState::DISABLED:
        {
            bChanged = true; // default initialised aPgDesc clears the number
            break;
        }
        case SfxItemState::UNKNOWN:
        case SfxItemState::DEFAULT:
            bRemoveNumOffset = true;
            break;
        default:
            assert(false); // unexpected
            break;
    }
    if( const SvxPageModelItem* pModelItem = rSet.GetItemIfSet( SID_ATTR_PARA_MODEL, false ))
    {
        const OUString& rDescName = pModelItem->GetValue();
        if( !rDescName.isEmpty() )   // No name -> disable PageDesc!
        {
            // Delete only, if PageDesc will be enabled!
            rSet.ClearItem( RES_BREAK );
            SwPageDesc* pDesc = const_cast<SwWrtShell&>(rShell).FindPageDescByName(
                                                    UIName(rDescName), true );
            if( pDesc )
                aPgDesc.RegisterToPageDesc( *pDesc );
        }
        rSet.ClearItem( SID_ATTR_PARA_MODEL );
        bChanged = true;
    }
    else
    {
        SfxItemSetFixed<RES_PAGEDESC, RES_PAGEDESC> aCoreSet(rShell.GetView().GetPool());
        rShell.GetCurAttr( aCoreSet );
        if(const SwFormatPageDesc* pPageDescItem = aCoreSet.GetItemIfSet( RES_PAGEDESC ) )
        {
            const SwPageDesc* pPageDesc = pPageDescItem->GetPageDesc();
            if (bRemoveNumOffset && pPageDescItem->GetNumOffset())
                bChanged = true;
            if( pPageDesc )
            {
                aPgDesc.RegisterToPageDesc( *const_cast<SwPageDesc*>(pPageDesc) );
            }
        }
    }
    if(bChanged)
        rSet.Put( aPgDesc );
}

// Inquire if in the set is a Sfx-PageDesc combination present and return it.
void SwToSfxPageDescAttr( SfxItemSet& rCoreSet )
{
    const SwFormatPageDesc* pPageDescItem = nullptr;
    UIName aName;
    ::std::optional<sal_uInt16> oNumOffset;
    bool bPut = true;
    switch( rCoreSet.GetItemState( RES_PAGEDESC, true, &pPageDescItem ) )
    {
    case SfxItemState::SET:
        {
            if( pPageDescItem->GetPageDesc() )
            {
                aName = pPageDescItem->GetPageDesc()->GetName();
                oNumOffset = pPageDescItem->GetNumOffset();
            }
            rCoreSet.ClearItem( RES_PAGEDESC );
            // Page number
        }
        break;

    case SfxItemState::DEFAULT:
        break;

    default:
        bPut = false;
    }

    if (oNumOffset)
    {
        SfxUInt16Item aPageNum( SID_ATTR_PARA_PAGENUM, *oNumOffset );
        rCoreSet.Put( aPageNum );
    }

    if(bPut)
        rCoreSet.Put( SvxPageModelItem( aName.toString(), true, SID_ATTR_PARA_MODEL ) );
}

// Determine metric

FieldUnit   GetDfltMetric(bool bWeb)
{
    return SwModule::get()->GetUsrPref(bWeb)->GetMetric();
}

// Determine metric

void    SetDfltMetric( FieldUnit eMetric, bool bWeb )
{
    SwModule::get()->ApplyUserMetric(eMetric, bWeb);
}

void InsertStringSorted(const OUString& rId, const OUString& rEntry, weld::ComboBox& rToFill, int nOffset)
{
    CollatorWrapper& rCaseColl = ::GetAppCaseCollator();
    const int nCount = rToFill.get_count();
    while (nOffset < nCount)
    {
        if (0 < rCaseColl.compareString(rToFill.get_text(nOffset), rEntry))
            break;
        ++nOffset;
    }
    rToFill.insert(nOffset, rEntry, &rId, nullptr, nullptr);
}

void FillCharStyleListBox(weld::ComboBox& rToFill, SwDocShell* pDocSh, bool bSorted, bool bWithDefault)
{
    const int nOffset = rToFill.get_count() > 0 ? 1 : 0;
    rToFill.freeze();
    SfxStyleSheetBasePool* pPool = pDocSh->GetStyleSheetPool();
    SwDoc* pDoc = pDocSh->GetDoc();
    const SfxStyleSheetBase* pBase = pPool->First(SfxStyleFamily::Char);
    const OUString sStandard(SwResId(STR_POOLCHR_STANDARD));
    while(pBase)
    {
        if(bWithDefault || pBase->GetName() !=  sStandard)
        {
            sal_IntPtr nPoolId = SwStyleNameMapper::GetPoolIdFromUIName( UIName(pBase->GetName()), SwGetPoolIdFromName::ChrFmt );
            OUString sId(OUString::number(nPoolId));
            if (bSorted)
                InsertStringSorted(sId, pBase->GetName(), rToFill, nOffset);
            else
                rToFill.append(sId, pBase->GetName());
        }
        pBase = pPool->Next();
    }
    // non-pool styles
    for(const auto pFormat : *pDoc->GetCharFormats())
    {
        if(pFormat->IsDefault())
            continue;
        const UIName& rName = pFormat->GetName();
        if (rToFill.find_text(rName.toString()) == -1)
        {
            OUString sId(OUString::number(USHRT_MAX));
            if (bSorted)
                InsertStringSorted(sId, rName.toString(), rToFill, nOffset);
            else
                rToFill.append(sId, rName.toString());
        }
    }
    rToFill.thaw();
};

SwTwips GetTableWidth( SwFrameFormat const * pFormat, SwTabCols const & rCols, sal_uInt16 *pPercent,
            SwWrtShell* pSh )
{
    // To get the width is slightly more complicated.
    SwTwips nWidth = 0;
    const sal_Int16 eOri = pFormat->GetHoriOrient().GetHoriOrient();
    switch(eOri)
    {
        case text::HoriOrientation::FULL: nWidth = rCols.GetRight(); break;
        case text::HoriOrientation::LEFT_AND_WIDTH:
        case text::HoriOrientation::LEFT:
        case text::HoriOrientation::RIGHT:
        case text::HoriOrientation::CENTER:
            nWidth = pFormat->GetFrameSize().GetWidth();
        break;
        default:
        {
            if(pSh)
            {
                if ( nullptr == pSh->GetFlyFrameFormat() )
                {
                    nWidth = pSh->GetAnyCurRect(CurRectType::PagePrt).Width();
                }
                else
                {
                    nWidth = pSh->GetAnyCurRect(CurRectType::FlyEmbeddedPrt).Width();
                }
            }
            else
            {
                OSL_FAIL("where to get the actual width from?");
            }
            const SvxLRSpaceItem& rLRSpace = pFormat->GetLRSpace();
            nWidth -= (rLRSpace.ResolveRight({}) + rLRSpace.ResolveLeft({}));
        }
    }
    if (pPercent)
        *pPercent = pFormat->GetFrameSize().GetWidthPercent();
    return nWidth;
}

OUString GetAppLangDateTimeString( const DateTime& rDT )
{
    const SvtSysLocale aSysLocale;
    const LocaleDataWrapper& rAppLclData = aSysLocale.GetLocaleData();
    OUString sRet = rAppLclData.getDate( rDT ) + " " + rAppLclData.getTime( rDT );
    return sRet;
}

// Add a new function which can get and set the current "SID_ATTR_APPLYCHARUNIT" value

bool HasCharUnit( bool bWeb)
{
    return SwModule::get()->GetUsrPref(bWeb)->IsApplyCharUnit();
}

void SetApplyCharUnit(bool bApplyChar, bool bWeb)
{
    SwModule::get()->ApplyUserCharUnit(bApplyChar, bWeb);
}

bool ExecuteMenuCommand(const css::uno::Reference<css::awt::XPopupMenu>& rMenu, const SfxViewFrame& rViewFrame, sal_uInt16 nId)
{
    bool bRet = false;
    const sal_uInt16 nItemCount = rMenu->getItemCount();
    OUString sCommand;
    for (sal_uInt16 nItem = 0; nItem < nItemCount; ++nItem)
    {
        sal_Int16 nItemId = rMenu->getItemId(nItem);
        css::uno::Reference<css::awt::XPopupMenu> xPopup = rMenu->getPopupMenu(nItemId);
        if (xPopup.is())
        {
            sCommand = xPopup->getCommand(nId);
            if(!sCommand.isEmpty())
                break;
        }
    }
    if(!sCommand.isEmpty())
    {
        uno::Reference< frame::XFrame >  xFrame = rViewFrame.GetFrame().GetFrameInterface();
        uno::Reference < frame::XDispatchProvider > xProv( xFrame, uno::UNO_QUERY );
        util::URL aURL;
        aURL.Complete = sCommand;
        uno::Reference < util::XURLTransformer > xTrans( util::URLTransformer::create(::comphelper::getProcessComponentContext() ) );
        xTrans->parseStrict( aURL );
        uno::Reference< frame::XDispatch >  xDisp = xProv->queryDispatch( aURL, OUString(), 0 );
        if( xDisp.is() )
        {
            uno::Sequence< beans::PropertyValue > aSeq;
            xDisp->dispatch( aURL, aSeq );
            bRet = true;
        }
    }
    return bRet;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
