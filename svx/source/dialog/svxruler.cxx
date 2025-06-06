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

#include <cstring>
#include <climits>

#include <vcl/commandevent.hxx>
#include <vcl/event.hxx>
#include <vcl/fieldvalues.hxx>
#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>
#include <vcl/virdev.hxx>
#include <vcl/weldutils.hxx>
#include <svl/eitem.hxx>
#include <svl/rectitem.hxx>
#include <svl/hint.hxx>
#include <sfx2/dispatch.hxx>
#include <svx/strings.hrc>
#include <svx/svxids.hrc>
#include <svx/dialmgr.hxx>
#include <svx/ruler.hxx>
#include <svx/rulritem.hxx>
#include <sfx2/viewsh.hxx>
#include <editeng/editids.hrc>
#include <editeng/tstpitem.hxx>
#include <editeng/lrspitem.hxx>
#include <editeng/protitem.hxx>
#include <osl/diagnose.h>
#include <rtl/math.hxx>
#include <o3tl/string_view.hxx>
#include <svl/itemset.hxx>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <tools/json_writer.hxx>
#include <tools/UnitConversion.hxx>
#include <comphelper/lok.hxx>
#include "rlrcitem.hxx"
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <sfx2/viewfrm.hxx>
#include <memory>

using namespace css;

#define CTRL_ITEM_COUNT 14
#define GAP 10
#define OBJECT_BORDER_COUNT 4
#define TAB_GAP 1
#define INDENT_GAP 2
#define INDENT_FIRST_LINE   2
#define INDENT_LEFT_MARGIN  3
#define INDENT_RIGHT_MARGIN 4
#define INDENT_COUNT        3 //without the first two old values

struct SvxRuler_Impl {
    std::unique_ptr<sal_uInt16[]> pPercBuf;
    std::unique_ptr<sal_uInt16[]> pBlockBuf;
    sal_uInt16 nPercSize;
    tools::Long   nTotalDist;
    tools::Long   lOldWinPos;
    tools::Long   lMaxLeftLogic;
    tools::Long   lMaxRightLogic;
    tools::Long   lLastLMargin;
    tools::Long   lLastRMargin;
    std::unique_ptr<SvxProtectItem> aProtectItem;
    std::unique_ptr<SfxBoolItem> pTextRTLItem;
    sal_uInt16 nControllerItems;
    sal_uInt16 nIdx;
    sal_uInt16 nColLeftPix;
    sal_uInt16 nColRightPix;    // Pixel values for left / right edge
                                // For columns; buffered to prevent
                                // recalculation errors
                                // May be has to be widen for future values
    bool bIsTableRows : 1;  // mxColumnItem contains table rows instead of columns
    //#i24363# tab stops relative to indent
    bool bIsTabsRelativeToIndent : 1; // Tab stops relative to paragraph indent?
                                      // false means relative to SvxRuler::GetLeftFrameMargin()

    SvxRuler_Impl() :
        nPercSize(0), nTotalDist(0),
        lOldWinPos(0), lMaxLeftLogic(0), lMaxRightLogic(0),
        lLastLMargin(0), lLastRMargin(0),
        aProtectItem(std::make_unique<SvxProtectItem>(SID_RULER_PROTECT)),
        nControllerItems(0), nIdx(0),
        nColLeftPix(0), nColRightPix(0),
        bIsTableRows(false),
        bIsTabsRelativeToIndent(true)
    {
    }

    void SetPercSize(sal_uInt16 nSize);

};

static RulerTabData ruler_tab_svx =
{
    0, // DPIScaleFactor to be set
    7, // ruler_tab_width
    6, // ruler_tab_height
    0, // ruler_tab_height2
    0, // ruler_tab_width2
    0, // ruler_tab_cwidth
    0, // ruler_tab_cwidth2
    0, // ruler_tab_cwidth3
    0, // ruler_tab_cwidth4
    0, // ruler_tab_dheight
    0, // ruler_tab_dheight2
    0, // ruler_tab_dwidth
    0, // ruler_tab_dwidth2
    0, // ruler_tab_dwidth3
    0, // ruler_tab_dwidth4
    0  // ruler_tab_textoff
};

void SvxRuler_Impl::SetPercSize(sal_uInt16 nSize)
{
    if(nSize > nPercSize)
    {
        nPercSize = nSize;
        pPercBuf.reset( new sal_uInt16[nPercSize] );
        pBlockBuf.reset( new sal_uInt16[nPercSize] );
    }
    size_t nSize2 = sizeof(sal_uInt16) * nPercSize;
    memset(pPercBuf.get(), 0, nSize2);
    memset(pBlockBuf.get(), 0, nSize2);
}

// Constructor of the ruler

// SID_ATTR_ULSPACE, SID_ATTR_LRSPACE
// expects as parameter SvxULSpaceItem for page edge
// (either left/right or top/bottom)
// Ruler: SetMargin1, SetMargin2

// SID_RULER_PAGE_POS
// expects as parameter the initial value of the page and page width
// Ruler: SetPagePos

// SID_ATTR_TABSTOP
// expects: SvxTabStopItem
// Ruler: SetTabs

// SID_ATTR_PARA_LRSPACE
// left, right paragraph edge in H-ruler
// Ruler: SetIndents

// SID_RULER_BORDERS
// Table borders, columns
// expects: something like SwTabCols
// Ruler: SetBorders

constexpr tools::Long glMinFrame = 5;   // minimal frame width in pixels

SvxRuler::SvxRuler(
            vcl::Window* pParent,        // StarView Parent
            vcl::Window* pWin,           // Output window: is used for conversion
                                         // logical units <-> pixels
            SvxRulerSupportFlags flags,  // Display flags, see ruler.hxx
            SfxBindings &rBindings,      // associated Bindings
            WinBits nWinStyle) :         // StarView WinBits
    Ruler(pParent, nWinStyle),
    pCtrlItems(CTRL_ITEM_COUNT),
    pEditWin(pWin),
    mxRulerImpl(new SvxRuler_Impl),
    bAppSetNullOffset(false),  // Is the 0-offset of the ruler set by the application?
    lLogicNullOffset(0),
    lAppNullOffset(LONG_MAX),
    lInitialDragPos(0),
    nFlags(flags),
    nDragType(SvxRulerDragFlags::NONE),
    nDefTabType(RULER_TAB_LEFT),
    nTabCount(0),
    nTabBufSize(0),
    lDefTabDist(50),
    lTabPos(-1),
    mpBorders(1), // due to one column tables
    pBindings(&rBindings),
    nDragOffset(0),
    nMaxLeft(0),
    nMaxRight(0),
    bValid(false),
    bListening(false),
    bActive(true),
    mbCoarseSnapping(false),
    mbSnapping(true)
{
    /* Constructor; Initialize data buffer; controller items are created */

    rBindings.EnterRegistrations();

    // Create Supported Items
    sal_uInt16 i = 0;

    // Page edges
    pCtrlItems[i++].reset(new SvxRulerItem(SID_RULER_LR_MIN_MAX, *this, rBindings));
    if((nWinStyle & WB_VSCROLL) == WB_VSCROLL)
    {
        bHorz = false;
        pCtrlItems[i++].reset(new SvxRulerItem(SID_ATTR_LONG_ULSPACE, *this, rBindings));
    }
    else
    {
        bHorz = true;
        pCtrlItems[i++].reset(new SvxRulerItem(SID_ATTR_LONG_LRSPACE, *this, rBindings));
    }

    // Page Position
    pCtrlItems[i++].reset(new SvxRulerItem(SID_RULER_PAGE_POS, *this, rBindings));

    if(nFlags & SvxRulerSupportFlags::TABS)
    {
        sal_uInt16 nTabStopId = bHorz ? SID_ATTR_TABSTOP : SID_ATTR_TABSTOP_VERTICAL;
        pCtrlItems[i++].reset(new SvxRulerItem(nTabStopId, *this, rBindings));
        SetExtraType(RulerExtra::Tab, nDefTabType);
    }

    if(nFlags & (SvxRulerSupportFlags::PARAGRAPH_MARGINS |SvxRulerSupportFlags::PARAGRAPH_MARGINS_VERTICAL))
    {
        if(bHorz)
            pCtrlItems[i++].reset(new SvxRulerItem(SID_ATTR_PARA_LRSPACE, *this, rBindings));
        else
            pCtrlItems[i++].reset(new SvxRulerItem(SID_ATTR_PARA_LRSPACE_VERTICAL, *this, rBindings));

        mpIndents.resize(5 + INDENT_GAP);

        for(RulerIndent & rIndent : mpIndents)
        {
            rIndent.nPos = 0;
            rIndent.nStyle = RulerIndentStyle::Top;
        }

        mpIndents[0].nStyle = RulerIndentStyle::Top;
        mpIndents[1].nStyle = RulerIndentStyle::Top;
        mpIndents[INDENT_FIRST_LINE].nStyle = RulerIndentStyle::Top;
        mpIndents[INDENT_LEFT_MARGIN].nStyle = RulerIndentStyle::Bottom;
        mpIndents[INDENT_RIGHT_MARGIN].nStyle = RulerIndentStyle::Bottom;
    }

    if( (nFlags & SvxRulerSupportFlags::BORDERS) ==  SvxRulerSupportFlags::BORDERS )
    {
        pCtrlItems[i++].reset(new SvxRulerItem(bHorz ? SID_RULER_BORDERS : SID_RULER_BORDERS_VERTICAL, *this, rBindings));
        pCtrlItems[i++].reset(new SvxRulerItem(bHorz ? SID_RULER_ROWS : SID_RULER_ROWS_VERTICAL, *this, rBindings));
    }

    pCtrlItems[i++].reset(new SvxRulerItem(SID_RULER_TEXT_RIGHT_TO_LEFT, *this, rBindings));

    if( (nFlags & SvxRulerSupportFlags::OBJECT) == SvxRulerSupportFlags::OBJECT )
    {
        pCtrlItems[i++].reset(new SvxRulerItem(SID_RULER_OBJECT, *this, rBindings));
        mpObjectBorders.resize(OBJECT_BORDER_COUNT);
        for(sal_uInt16 nBorder = 0; nBorder < OBJECT_BORDER_COUNT; ++nBorder)
        {
            mpObjectBorders[nBorder].nPos   = 0;
            mpObjectBorders[nBorder].nWidth = 0;
            mpObjectBorders[nBorder].nStyle = RulerBorderStyle::Moveable;
        }
    }

    pCtrlItems[i++].reset(new SvxRulerItem(SID_RULER_PROTECT, *this, rBindings));
    pCtrlItems[i++].reset(new SvxRulerItem(SID_RULER_BORDER_DISTANCE, *this, rBindings));
    mxRulerImpl->nControllerItems=i;

    if( (nFlags & SvxRulerSupportFlags::SET_NULLOFFSET) == SvxRulerSupportFlags::SET_NULLOFFSET )
        SetExtraType(RulerExtra::NullOffset);

    rBindings.LeaveRegistrations();

    ruler_tab_svx.DPIScaleFactor = pParent->GetDPIScaleFactor();
    ruler_tab_svx.height *= ruler_tab_svx.DPIScaleFactor;
    ruler_tab_svx.width  *= ruler_tab_svx.DPIScaleFactor;
}

SvxRuler::~SvxRuler()
{
    disposeOnce();
}

void SvxRuler::dispose()
{
    /* Destructor ruler; release internal buffer */
    if(bListening)
        EndListening(*pBindings);

    pBindings->EnterRegistrations();

    pCtrlItems.clear();

    pBindings->LeaveRegistrations();

    pEditWin.reset();
    Ruler::dispose();
}

tools::Long SvxRuler::MakePositionSticky(tools::Long aPosition, tools::Long aPointOfReference, bool aSnapToFrameMargin) const
{
    tools::Long aPointOfReferencePixel = ConvertHPosPixel(aPointOfReference);
    tools::Long aLeftFramePosition     = ConvertHPosPixel(GetLeftFrameMargin());
    tools::Long aRightFramePosition    = ConvertHPosPixel(GetRightFrameMargin());

    double aTick = GetCurrentRulerUnit().nTick1;

    if (mbCoarseSnapping)
        aTick = GetCurrentRulerUnit().nTick2;

    tools::Long aTickPixel = pEditWin->LogicToPixel(Size(aTick, 0), GetCurrentMapMode()).Width();

    double aHalfTick = aTick / 2.0;
    double aHalfTickPixel = aTickPixel / 2.0;

    if (aSnapToFrameMargin)
    {
        if (aPosition > aLeftFramePosition - aHalfTickPixel && aPosition < aLeftFramePosition + aHalfTickPixel)
            return aLeftFramePosition;

        if (aPosition > aRightFramePosition - aHalfTickPixel && aPosition < aRightFramePosition + aHalfTickPixel)
            return aRightFramePosition;
    }

    if (!mbSnapping)
        return aPosition;

    // Move "coordinate system" to frame position so ticks are calculated correctly
    tools::Long aTranslatedPosition = aPosition - aPointOfReferencePixel;
    // Convert position to current selected map mode
    tools::Long aPositionLogic = pEditWin->PixelToLogic(Size(aTranslatedPosition, 0), GetCurrentMapMode()).Width();
    // Normalize -- snap to nearest tick
    aPositionLogic = rtl::math::round((aPositionLogic + aHalfTick) / aTick) * aTick;
    // Convert back to pixels
    aPosition = pEditWin->LogicToPixel(Size(aPositionLogic, 0), GetCurrentMapMode()).Width();
    // Move "coordinate system" back to original position
    return aPosition + aPointOfReferencePixel;
}

tools::Long SvxRuler::ConvertHPosPixel(tools::Long nVal) const
{
    return pEditWin->LogicToPixel(Size(nVal, 0)).Width();
}

tools::Long SvxRuler::ConvertVPosPixel(tools::Long nVal) const
{
    return pEditWin->LogicToPixel(Size(0, nVal)).Height();
}

tools::Long SvxRuler::ConvertHSizePixel(tools::Long nVal) const
{
    return pEditWin->LogicToPixel(Size(nVal, 0)).Width();
}

tools::Long SvxRuler::ConvertVSizePixel(tools::Long nVal) const
{
    return pEditWin->LogicToPixel(Size(0, nVal)).Height();
}

tools::Long SvxRuler::ConvertPosPixel(tools::Long nVal) const
{
    return bHorz ? ConvertHPosPixel(nVal): ConvertVPosPixel(nVal);
}

tools::Long SvxRuler::ConvertSizePixel(tools::Long nVal) const
{
    return bHorz? ConvertHSizePixel(nVal): ConvertVSizePixel(nVal);
}

inline tools::Long SvxRuler::ConvertHPosLogic(tools::Long nVal) const
{
    return pEditWin->PixelToLogic(Size(nVal, 0)).Width();
}

inline tools::Long SvxRuler::ConvertVPosLogic(tools::Long nVal) const
{
    return pEditWin->PixelToLogic(Size(0, nVal)).Height();
}

inline tools::Long SvxRuler::ConvertHSizeLogic(tools::Long nVal) const
{
    return pEditWin->PixelToLogic(Size(nVal, 0)).Width();
}

inline tools::Long SvxRuler::ConvertVSizeLogic(tools::Long nVal) const
{
    return pEditWin->PixelToLogic(Size(0, nVal)).Height();
}

inline tools::Long SvxRuler::ConvertPosLogic(tools::Long nVal) const
{
    return bHorz? ConvertHPosLogic(nVal): ConvertVPosLogic(nVal);
}

inline tools::Long SvxRuler::ConvertSizeLogic(tools::Long nVal) const
{
    return bHorz? ConvertHSizeLogic(nVal): ConvertVSizeLogic(nVal);
}

tools::Long SvxRuler::PixelHAdjust(tools::Long nVal, tools::Long nValOld) const
{
    if(ConvertHSizePixel(nVal) != ConvertHSizePixel(nValOld))
        return  nVal;
    else
        return  nValOld;
}

tools::Long SvxRuler::PixelVAdjust(tools::Long nVal, tools::Long nValOld) const
{
    if(ConvertVSizePixel(nVal) != ConvertVSizePixel(nValOld))
        return  nVal;
    else
        return  nValOld;
}

tools::Long SvxRuler::PixelAdjust(tools::Long nVal, tools::Long nValOld) const
{
    if(ConvertSizePixel(nVal) != ConvertSizePixel(nValOld))
        return  nVal;
    else
        return  nValOld;
}

inline sal_uInt16 SvxRuler::GetObjectBordersOff(sal_uInt16 nIdx) const
{
    return bHorz ? nIdx : nIdx + 2;
}

/*
    Update Upper Left edge.
    Items are translated into the representation of the ruler.
*/
void SvxRuler::UpdateFrame()
{
    const RulerMarginStyle nMarginStyle =
        ( mxRulerImpl->aProtectItem->IsSizeProtected() ||
          mxRulerImpl->aProtectItem->IsPosProtected() ) ?
        RulerMarginStyle::NONE : RulerMarginStyle::Sizeable;

    if(mxLRSpaceItem && mxPagePosItem)
    {
        // if no initialization by default app behavior
        const tools::Long nOld = lLogicNullOffset;
        lLogicNullOffset = mxColumnItem ? mxColumnItem->GetLeft() : mxLRSpaceItem->GetLeft();

        if(bAppSetNullOffset)
        {
            lAppNullOffset += lLogicNullOffset - nOld;
        }

        if(!bAppSetNullOffset || lAppNullOffset == LONG_MAX)
        {
            Ruler::SetNullOffset(ConvertHPosPixel(lLogicNullOffset));
            SetMargin1(0, nMarginStyle);
            lAppNullOffset = 0;
        }
        else
        {
            SetMargin1(ConvertHPosPixel(lAppNullOffset), nMarginStyle);
        }

        tools::Long lRight = 0;

        // evaluate the table right edge of the table
        if(mxColumnItem && mxColumnItem->IsTable())
            lRight = mxColumnItem->GetRight();
        else
            lRight = mxLRSpaceItem->GetRight();

        tools::Long aWidth = mxPagePosItem->GetWidth() - lRight - lLogicNullOffset + lAppNullOffset;
        tools::Long aWidthPixel = ConvertHPosPixel(aWidth);

        SetMargin2(aWidthPixel, nMarginStyle);
    }
    else if(mxULSpaceItem && mxPagePosItem)
    {
        // relative the upper edge of the surrounding frame
        const tools::Long nOld = lLogicNullOffset;
        lLogicNullOffset = mxColumnItem ? mxColumnItem->GetLeft() : mxULSpaceItem->GetUpper();

        if(bAppSetNullOffset)
        {
            lAppNullOffset += lLogicNullOffset - nOld;
        }

        if(!bAppSetNullOffset || lAppNullOffset == LONG_MAX)
        {
            Ruler::SetNullOffset(ConvertVPosPixel(lLogicNullOffset));
            lAppNullOffset = 0;
            SetMargin1(0, nMarginStyle);
        }
        else
        {
            SetMargin1(ConvertVPosPixel(lAppNullOffset), nMarginStyle);
        }

        tools::Long lLower = mxColumnItem ? mxColumnItem->GetRight() : mxULSpaceItem->GetLower();
        tools::Long nMargin2 = mxPagePosItem->GetHeight() - lLower - lLogicNullOffset + lAppNullOffset;
        tools::Long nMargin2Pixel = ConvertVPosPixel(nMargin2);

        SetMargin2(nMargin2Pixel, nMarginStyle);
    }
    else
    {
        // turns off the view
        SetMargin1();
        SetMargin2();
    }

    if (mxColumnItem)
    {
        mxRulerImpl->nColLeftPix = static_cast<sal_uInt16>(ConvertSizePixel(mxColumnItem->GetLeft()));
        mxRulerImpl->nColRightPix = static_cast<sal_uInt16>(ConvertSizePixel(mxColumnItem->GetRight()));
    }
}

void SvxRuler::MouseMove( const MouseEvent& rMEvt )
{
    if( bActive )
    {
        pBindings->Update( SID_RULER_LR_MIN_MAX );
        pBindings->Update( SID_ATTR_LONG_ULSPACE );
        pBindings->Update( SID_ATTR_LONG_LRSPACE );
        pBindings->Update( SID_RULER_PAGE_POS );
        pBindings->Update( bHorz ? SID_ATTR_TABSTOP : SID_ATTR_TABSTOP_VERTICAL);
        pBindings->Update( bHorz ? SID_ATTR_PARA_LRSPACE : SID_ATTR_PARA_LRSPACE_VERTICAL);
        pBindings->Update( bHorz ? SID_RULER_BORDERS : SID_RULER_BORDERS_VERTICAL);
        pBindings->Update( bHorz ? SID_RULER_ROWS : SID_RULER_ROWS_VERTICAL);
        pBindings->Update( SID_RULER_OBJECT );
        pBindings->Update( SID_RULER_PROTECT );
    }

    Ruler::MouseMove( rMEvt );

    RulerSelection aSelection = GetHoverSelection();

    if (aSelection.eType == RulerType::DontKnow)
    {
        SetQuickHelpText(u""_ustr);
        return;
    }

    RulerUnitData aUnitData = GetCurrentRulerUnit();
    double aRoundingFactor = aUnitData.nTickUnit / aUnitData.nTick1;
    sal_Int32 aNoDecimalPlaces = 1 + std::ceil(std::log10(aRoundingFactor));
    OUString sUnit = OUString::createFromAscii(aUnitData.aUnitStr);

    switch (aSelection.eType)
    {
        case RulerType::Indent:
        {
            if (!mxParaItem)
                break;

            tools::Long nIndex = aSelection.nAryPos + INDENT_GAP;

            tools::Long nIndentValue = 0.0;
            if (nIndex == INDENT_LEFT_MARGIN)
                nIndentValue = mxParaItem->ResolveTextLeft({});
            else if (nIndex == INDENT_FIRST_LINE)
                nIndentValue = mxParaItem->ResolveTextFirstLineOffset({});
            else if (nIndex == INDENT_RIGHT_MARGIN)
                nIndentValue = mxParaItem->ResolveRight({});

            double fValue = OutputDevice::LogicToLogic(Size(nIndentValue, 0), pEditWin->GetMapMode(), GetCurrentMapMode()).Width();
            fValue = rtl::math::round(fValue / aUnitData.nTickUnit, aNoDecimalPlaces);

            SetQuickHelpText(OUString::number(fValue) + " " + sUnit);
            break;
        }
        case RulerType::Border:
        {
            if (mxColumnItem == nullptr)
                break;

            SvxColumnItem& aColumnItem = *mxColumnItem;

            if (aSelection.nAryPos + 1 >= aColumnItem.Count())
                break;

            double fStart = OutputDevice::LogicToLogic(Size(aColumnItem[aSelection.nAryPos].nEnd,       0), pEditWin->GetMapMode(), GetCurrentMapMode()).Width();
            fStart = rtl::math::round(fStart / aUnitData.nTickUnit, aNoDecimalPlaces);
            double fEnd   = OutputDevice::LogicToLogic(Size(aColumnItem[aSelection.nAryPos + 1].nStart, 0), pEditWin->GetMapMode(), GetCurrentMapMode()).Width();
            fEnd = rtl::math::round(fEnd / aUnitData.nTickUnit, aNoDecimalPlaces);

            SetQuickHelpText(
                OUString::number(fStart) + " " + sUnit + " - " +
                OUString::number(fEnd)   + " " + sUnit );
            break;
        }
        case RulerType::Margin1:
        {
            tools::Long nLeft = 0.0;
            if (mxLRSpaceItem)
                nLeft = mxLRSpaceItem->GetLeft();
            else if (mxULSpaceItem)
                nLeft = mxULSpaceItem->GetUpper();
            else
                break;

            double fValue = OutputDevice::LogicToLogic(Size(nLeft, 0), pEditWin->GetMapMode(), GetCurrentMapMode()).Width();
            fValue = rtl::math::round(fValue / aUnitData.nTickUnit, aNoDecimalPlaces);
            SetQuickHelpText(OUString::number(fValue) + " " + sUnit);

            break;
        }
        case RulerType::Margin2:
        {
            tools::Long nRight = 0.0;
            if (mxLRSpaceItem)
                nRight = mxLRSpaceItem->GetRight();
            else if (mxULSpaceItem)
                nRight = mxULSpaceItem->GetLower();
            else
                break;

            double fValue = OutputDevice::LogicToLogic(Size(nRight, 0), pEditWin->GetMapMode(), GetCurrentMapMode()).Width();
            fValue = rtl::math::round(fValue / aUnitData.nTickUnit, aNoDecimalPlaces);
            SetQuickHelpText(OUString::number(fValue) + " " + sUnit);

            break;
        }
        default:
        {
            SetQuickHelpText(u""_ustr);
            break;
        }
    }
}

void SvxRuler::StartListening_Impl()
{
    if(!bListening)
    {
        bValid = false;
        StartListening(*pBindings);
        bListening = true;
    }
}

void SvxRuler::UpdateFrame(const SvxLongLRSpaceItem *pItem) // new value LRSpace
{
    /* Store new value LRSpace; delete old ones if possible */
    if(bActive)
    {
        if(pItem)
            mxLRSpaceItem.reset(new SvxLongLRSpaceItem(*pItem));
        else
            mxLRSpaceItem.reset();
        StartListening_Impl();
    }
}

void SvxRuler::UpdateFrameMinMax(const SfxRectangleItem *pItem) // value for MinMax
{
    /* Set new value for MinMax; delete old ones if possible */
    if(bActive)
    {
        if(pItem)
            mxMinMaxItem.reset(new SfxRectangleItem(*pItem));
        else
            mxMinMaxItem.reset();
    }
}


void SvxRuler::UpdateFrame(const SvxLongULSpaceItem *pItem) // new value
{
    /* Update Right/bottom margin */
    if(bActive && !bHorz)
    {
        if(pItem)
            mxULSpaceItem.reset(new SvxLongULSpaceItem(*pItem));
        else
            mxULSpaceItem.reset();
        StartListening_Impl();
    }
}

void SvxRuler::Update( const SvxProtectItem* pItem )
{
    if( pItem )
        mxRulerImpl->aProtectItem.reset(pItem->Clone());
}

void SvxRuler::UpdateTextRTL(const SfxBoolItem* pItem)
{
    if(bActive && bHorz)
    {
        mxRulerImpl->pTextRTLItem.reset();
        if(pItem)
            mxRulerImpl->pTextRTLItem.reset(new SfxBoolItem(*pItem));
        SetTextRTL(mxRulerImpl->pTextRTLItem && mxRulerImpl->pTextRTLItem->GetValue());
        StartListening_Impl();
    }
}

void SvxRuler::Update(
                const SvxColumnItem *pItem,  // new value
                sal_uInt16 nSID) //Slot Id to identify NULL items
{
    /* Set new value for column view */
    if(!bActive)
        return;

    if(pItem)
    {
        mxColumnItem.reset(new SvxColumnItem(*pItem));
        mxRulerImpl->bIsTableRows = (pItem->Which() == SID_RULER_ROWS || pItem->Which() == SID_RULER_ROWS_VERTICAL);
        if(!bHorz && !mxRulerImpl->bIsTableRows)
            mxColumnItem->SetWhich(SID_RULER_BORDERS_VERTICAL);
    }
    else if(mxColumnItem && mxColumnItem->Which() == nSID)
    //there are two groups of column items table/frame columns and table rows
    //both can occur in vertical or horizontal mode
    //the horizontal ruler handles the SID_RULER_BORDERS and SID_RULER_ROWS_VERTICAL
    //and the vertical handles SID_RULER_BORDERS_VERTICAL and SID_RULER_ROWS
    //if mxColumnItem is already set with one of the ids then a NULL pItem argument
    //must not delete it
    {
        mxColumnItem.reset();
        mxRulerImpl->bIsTableRows = false;
    }
    StartListening_Impl();
}


void SvxRuler::UpdateColumns()
{
    /* Update column view */
    if(mxColumnItem && mxColumnItem->Count() > 1)
    {
        mpBorders.resize(mxColumnItem->Count());

        RulerBorderStyle nStyleFlags = RulerBorderStyle::Variable;

        bool bProtectColumns =
                    mxRulerImpl->aProtectItem->IsSizeProtected() ||
                    mxRulerImpl->aProtectItem->IsPosProtected();

        if( !bProtectColumns )
        {
            nStyleFlags |= RulerBorderStyle::Moveable;
            if( !mxColumnItem->IsTable() )
              nStyleFlags |= RulerBorderStyle::Sizeable;
        }

        sal_uInt16 nBorders = mxColumnItem->Count();

        if(!mxRulerImpl->bIsTableRows)
            --nBorders;

        for(sal_uInt16 i = 0; i < nBorders; ++i)
        {
            mpBorders[i].nStyle = nStyleFlags;
            if(!mxColumnItem->At(i).bVisible)
                mpBorders[i].nStyle |= RulerBorderStyle::Invisible;

            mpBorders[i].nPos = ConvertPosPixel(mxColumnItem->At(i).nEnd + lAppNullOffset);

            if(mxColumnItem->Count() == i + 1)
            {
                //with table rows the end of the table is contained in the
                //column item but it has no width!
                mpBorders[i].nWidth = 0;
            }
            else
            {
                mpBorders[i].nWidth = ConvertSizePixel(mxColumnItem->At(i + 1).nStart - mxColumnItem->At(i).nEnd);
            }
            mpBorders[i].nMinPos = ConvertPosPixel(mxColumnItem->At(i).nEndMin + lAppNullOffset);
            mpBorders[i].nMaxPos = ConvertPosPixel(mxColumnItem->At(i).nEndMax + lAppNullOffset);
        }
        SetBorders(mxColumnItem->Count() - 1, mpBorders.data());
    }
    else
    {
        SetBorders();
    }
}

void SvxRuler::UpdateObject()
{
    /* Update view of object representation */
    if (mxObjectItem)
    {
        DBG_ASSERT(!mpObjectBorders.empty(), "no Buffer");
        // !! to the page margin
        tools::Long nMargin = mxLRSpaceItem ? mxLRSpaceItem->GetLeft() : 0;
        mpObjectBorders[0].nPos =
            ConvertPosPixel(mxObjectItem->GetStartX() -
                            nMargin + lAppNullOffset);
        mpObjectBorders[1].nPos =
            ConvertPosPixel(mxObjectItem->GetEndX() - nMargin + lAppNullOffset);
        nMargin = mxULSpaceItem ? mxULSpaceItem->GetUpper() : 0;
        mpObjectBorders[2].nPos =
            ConvertPosPixel(mxObjectItem->GetStartY() -
                            nMargin + lAppNullOffset);
        mpObjectBorders[3].nPos =
            ConvertPosPixel(mxObjectItem->GetEndY() - nMargin + lAppNullOffset);

        const sal_uInt16 nOffset = GetObjectBordersOff(0);
        SetBorders(2, mpObjectBorders.data() + nOffset);
    }
    else
    {
        SetBorders();
    }
}

void SvxRuler::UpdatePara()
{

    /*  Update the view for paragraph indents:
        Left margin, first line indent, right margin paragraph update
        mpIndents[0] = Buffer for old intent
        mpIndents[1] = Buffer for old intent
        mpIndents[INDENT_FIRST_LINE]   = first line indent
        mpIndents[INDENT_LEFT_MARGIN]  = left margin
        mpIndents[INDENT_RIGHT_MARGIN] = right margin
    */

    // Dependence on PagePosItem
    if (mxParaItem && mxPagePosItem && !mxObjectItem)
    {
        bool bRTLText = mxRulerImpl->pTextRTLItem && mxRulerImpl->pTextRTLItem->GetValue();
        // First-line indent is negative to the left paragraph margin
        tools::Long nLeftFrameMargin = GetLeftFrameMargin();
        tools::Long nRightFrameMargin = GetRightFrameMargin();
        SetLeftFrameMargin(ConvertHPosPixel(nLeftFrameMargin));
        SetRightFrameMargin(ConvertHPosPixel(nRightFrameMargin));

        tools::Long leftMargin;
        tools::Long leftFirstLine;
        tools::Long rightMargin;

        if(bRTLText)
        {
            leftMargin = nRightFrameMargin - mxParaItem->ResolveTextLeft({}) + lAppNullOffset;
            leftFirstLine = leftMargin - mxParaItem->ResolveTextFirstLineOffset({});
            rightMargin = nLeftFrameMargin + mxParaItem->ResolveRight({}) + lAppNullOffset;
        }
        else
        {
            leftMargin = nLeftFrameMargin + mxParaItem->ResolveTextLeft({}) + lAppNullOffset;
            leftFirstLine = leftMargin + mxParaItem->ResolveTextFirstLineOffset({});
            rightMargin = nRightFrameMargin - mxParaItem->ResolveRight({}) + lAppNullOffset;
        }

        mpIndents[INDENT_LEFT_MARGIN].nPos  = ConvertHPosPixel(leftMargin);
        mpIndents[INDENT_FIRST_LINE].nPos   = ConvertHPosPixel(leftFirstLine);
        mpIndents[INDENT_RIGHT_MARGIN].nPos = ConvertHPosPixel(rightMargin);

        mpIndents[INDENT_FIRST_LINE].bInvisible = mxParaItem->IsAutoFirst();

        SetIndents(INDENT_COUNT, mpIndents.data() + INDENT_GAP);
    }
    else
    {
        if(!mpIndents.empty())
        {
            mpIndents[INDENT_FIRST_LINE].nPos = 0;
            mpIndents[INDENT_LEFT_MARGIN].nPos = 0;
            mpIndents[INDENT_RIGHT_MARGIN].nPos = 0;
        }
        SetIndents(); // turn off
    }
}

void SvxRuler::UpdatePara(const SvxLRSpaceItem *pItem) // new value of paragraph indents
{
    /* Store new value of paragraph indents */
    if(bActive)
    {
        if(pItem)
            mxParaItem.reset(new SvxLRSpaceItem(*pItem));
        else
            mxParaItem.reset();
        StartListening_Impl();
    }
}

void SvxRuler::UpdateBorder(const SvxLRSpaceItem * pItem)
{
    /* Border distance */
    if(bActive)
    {
        if (pItem)
            mxBorderItem.reset(new SvxLRSpaceItem(*pItem));
        else
            mxBorderItem.reset();

        StartListening_Impl();
    }
}

void SvxRuler::UpdatePage()
{
    /* Update view of position and width of page */
    if (mxPagePosItem)
    {
        // all objects are automatically adjusted
        if(bHorz)
        {
            SetPagePos(
                pEditWin->LogicToPixel(mxPagePosItem->GetPos()).X(),
                pEditWin->LogicToPixel(Size(mxPagePosItem->GetWidth(), 0)).
                Width());
        }
        else
        {
            SetPagePos(
                pEditWin->LogicToPixel(mxPagePosItem->GetPos()).Y(),
                pEditWin->LogicToPixel(Size(0, mxPagePosItem->GetHeight())).
                Height());
        }
        if(bAppSetNullOffset)
            SetNullOffset(ConvertSizePixel(-lAppNullOffset + lLogicNullOffset));
    }
    else
    {
        SetPagePos();
    }

    tools::Long lPos = 0;
    Point aOwnPos = GetPosPixel();
    Point aEdtWinPos = pEditWin->GetPosPixel();
    if( AllSettings::GetLayoutRTL() && bHorz )
    {
        //#i73321# in RTL the window and the ruler is not mirrored but the
        // influence of the vertical ruler is inverted
        Size aOwnSize = GetSizePixel();
        Size aEdtWinSize = pEditWin->GetSizePixel();
        lPos = aOwnSize.Width() - aEdtWinSize.Width();
        lPos -= (aEdtWinPos - aOwnPos).X();
    }
    else
    {
        Point aPos(aEdtWinPos - aOwnPos);
        lPos = bHorz ? aPos.X() : aPos.Y();
    }

    // Unfortunately, we get the offset of the edit window to the ruler never
    // through a status message. So we set it ourselves if necessary.
    if(lPos != mxRulerImpl->lOldWinPos)
    {
        mxRulerImpl->lOldWinPos=lPos;
        SetWinPos(lPos);
    }
}

void SvxRuler::Update(const SvxPagePosSizeItem *pItem) // new value of page attributes
{
    /* Store new value of page attributes */
    if(bActive)
    {
        if(pItem)
            mxPagePosItem.reset(new SvxPagePosSizeItem(*pItem));
        else
            mxPagePosItem.reset();
        StartListening_Impl();
    }
}

void SvxRuler::SetDefTabDist(tools::Long inDefTabDist)  // New distance for DefaultTabs in App-Metrics
{
    if (lAppNullOffset == LONG_MAX)
        UpdateFrame(); // hack: try to get lAppNullOffset initialized
    /* New distance is set for DefaultTabs */
    lDefTabDist = inDefTabDist;
    if( !lDefTabDist )
        lDefTabDist = 1;

    UpdateTabs();
}

static sal_uInt16 ToSvTab_Impl(SvxTabAdjust eAdj)
{
    /* Internal conversion routine between SV-Tab.-Enum and Svx */
    switch(eAdj) {
        case SvxTabAdjust::Left:    return RULER_TAB_LEFT;
        case SvxTabAdjust::Right:   return RULER_TAB_RIGHT;
        case SvxTabAdjust::Decimal: return RULER_TAB_DECIMAL;
        case SvxTabAdjust::Center:  return RULER_TAB_CENTER;
        case SvxTabAdjust::Default: return RULER_TAB_DEFAULT;
        default: ; //prevent warning
    }
    return 0;
}

static SvxTabAdjust ToAttrTab_Impl(sal_uInt16 eAdj)
{
    switch(eAdj) {
        case RULER_TAB_LEFT:    return SvxTabAdjust::Left    ;
        case RULER_TAB_RIGHT:   return SvxTabAdjust::Right   ;
        case RULER_TAB_DECIMAL: return SvxTabAdjust::Decimal ;
        case RULER_TAB_CENTER:  return SvxTabAdjust::Center  ;
        case RULER_TAB_DEFAULT: return SvxTabAdjust::Default ;
    }
    return SvxTabAdjust::Left;
}

void SvxRuler::UpdateTabs()
{
    if(IsDrag())
        return;

    if (mxPagePosItem && mxParaItem && mxTabStopItem && !mxObjectItem)
    {
        // buffer for DefaultTabStop
        // Distance last Tab <-> Right paragraph margin / DefaultTabDist
        bool bRTL = mxRulerImpl->pTextRTLItem && mxRulerImpl->pTextRTLItem->GetValue();

        const tools::Long nLeftFrameMargin = GetLeftFrameMargin();
        const tools::Long nRightFrameMargin = GetRightFrameMargin();

        //#i24363# tab stops relative to indent
        const tools::Long nParaItemTxtLeft = mxParaItem->ResolveTextLeft({});

        const tools::Long lParaIndent = nLeftFrameMargin + nParaItemTxtLeft;
        const tools::Long lRightMargin = nRightFrameMargin - nParaItemTxtLeft;

        const tools::Long lLastTab = mxTabStopItem->Count()
                                ? ConvertHPosPixel(mxTabStopItem->At(mxTabStopItem->Count() - 1).GetTabPos())
                                : 0;
        const tools::Long lPosPixel = ConvertHPosPixel(lParaIndent) + lLastTab;
        const tools::Long lRightIndent
            = ConvertHPosPixel(nRightFrameMargin - mxParaItem->ResolveRight({}));

        tools::Long lCurrentDefTabDist = lDefTabDist;
        if(mxTabStopItem->GetDefaultDistance())
            lCurrentDefTabDist = mxTabStopItem->GetDefaultDistance();
        tools::Long nDefTabDist = ConvertHPosPixel(lCurrentDefTabDist);

        const sal_uInt16 nDefTabBuf = lPosPixel > lRightIndent || lLastTab > lRightIndent || nDefTabDist == 0
                    ? 0
                    : static_cast<sal_uInt16>( (lRightIndent - lPosPixel) / nDefTabDist );

        if(mxTabStopItem->Count() + TAB_GAP + nDefTabBuf > nTabBufSize)
        {
            // 10 (GAP) in stock
            nTabBufSize = mxTabStopItem->Count() + TAB_GAP + nDefTabBuf + GAP;
            mpTabs.resize(nTabBufSize);
        }

        nTabCount = 0;
        sal_uInt16 j;

        const tools::Long lParaIndentPix = ConvertSizePixel(lParaIndent);

        tools::Long lTabStartLogic = (mxRulerImpl->bIsTabsRelativeToIndent ? lParaIndent : nLeftFrameMargin)
            + lAppNullOffset;
        if (bRTL)
        {
            lTabStartLogic = lParaIndent + lRightMargin - lTabStartLogic;
        }
        tools::Long lLastTabOffsetLogic = 0;
        for(j = 0; j < mxTabStopItem->Count(); ++j)
        {
            const SvxTabStop* pTab = &mxTabStopItem->At(j);
            lLastTabOffsetLogic = pTab->GetTabPos();
            tools::Long lPos = lTabStartLogic + (bRTL ? -lLastTabOffsetLogic : lLastTabOffsetLogic);
            mpTabs[nTabCount + TAB_GAP].nPos = ConvertHPosPixel(lPos);
            mpTabs[nTabCount + TAB_GAP].nStyle = ToSvTab_Impl(pTab->GetAdjustment());
            ++nTabCount;
        }

        // Adjust to previous-to-first default tab stop
        lLastTabOffsetLogic -= lLastTabOffsetLogic % lCurrentDefTabDist;

        // fill the rest with default Tabs
        for (j = 0; j < nDefTabBuf; ++j)
        {
            //simply add the default distance to the last position
            lLastTabOffsetLogic += lCurrentDefTabDist;
            if (bRTL)
            {
                mpTabs[nTabCount + TAB_GAP].nPos =
                    ConvertHPosPixel(lTabStartLogic - lLastTabOffsetLogic);
                if (mpTabs[nTabCount + TAB_GAP].nPos <= lParaIndentPix)
                    break;
            }
            else
            {
                mpTabs[nTabCount + TAB_GAP].nPos =
                    ConvertHPosPixel(lTabStartLogic + lLastTabOffsetLogic);
                if (mpTabs[nTabCount + TAB_GAP].nPos >= lRightIndent)
                    break;
            }

            mpTabs[nTabCount + TAB_GAP].nStyle = RULER_TAB_DEFAULT;
            ++nTabCount;
        }
        SetTabs(nTabCount, mpTabs.data() + TAB_GAP);
        DBG_ASSERT(nTabCount + TAB_GAP <= nTabBufSize, "BufferSize too small");
    }
    else
    {
        SetTabs();
    }
}

void SvxRuler::Update(const SvxTabStopItem *pItem) // new value for tabs
{
    /* Store new value for tabs; delete old ones if possible */
    if(!bActive)
        return;

    if(pItem)
    {
        mxTabStopItem.reset(new SvxTabStopItem(*pItem));
        if(!bHorz)
            mxTabStopItem->SetWhich(SID_ATTR_TABSTOP_VERTICAL);
    }
    else
    {
        mxTabStopItem.reset();
    }
    StartListening_Impl();
}

void SvxRuler::Update(const SvxObjectItem *pItem) // new value for objects
{
    /* Store new value for objects */
    if(bActive)
    {
        if(pItem)
            mxObjectItem.reset(new SvxObjectItem(*pItem));
        else
            mxObjectItem.reset();
        StartListening_Impl();
    }
}

void SvxRuler::SetNullOffsetLogic(tools::Long lVal) // Setting of the logic NullOffsets
{
    lAppNullOffset = lLogicNullOffset - lVal;
    bAppSetNullOffset = true;
    Ruler::SetNullOffset(ConvertSizePixel(lVal));
    Update();
}

void SvxRuler::CreateJsonNotification(tools::JsonWriter& rJsonWriter)
{
    tools::Long nMargin1 = 0;
    tools::Long nMargin2 = 0;
    tools::Long nNullOffset = 0;
    tools::Long nPageOffset = 0;
    tools::Long nPageWidthHeight = 0;

    bool bWriter = false;

    // Determine if we are a Ruler for Writer or not
    if (SfxViewFrame* pFrame = SfxViewFrame::Current())
    {
        uno::Reference<frame::XFrame> xFrame = pFrame->GetFrame().GetFrameInterface();
        uno::Reference<frame::XModel> xModel = xFrame->getController()->getModel();
        uno::Reference<lang::XServiceInfo> xSI(xModel, uno::UNO_QUERY);
        if (xSI.is())
        {
            bWriter = xSI->supportsService("com.sun.star.text.TextDocument")
            || xSI->supportsService("com.sun.star.text.WebDocument")
                || xSI->supportsService("com.sun.star.text.GlobalDocument");
        }
    }

    if (bWriter)
    {
        // In Writer the ruler values need to be converted first from pixel to twips (default logical unit) and then to 100thmm
        nMargin1 = convertTwipToMm100(ConvertPosLogic(GetMargin1()));
        nMargin2 = convertTwipToMm100(ConvertPosLogic(GetMargin2()));
        nNullOffset = convertTwipToMm100(ConvertPosLogic(GetNullOffset()));
        nPageOffset = convertTwipToMm100(ConvertPosLogic(GetPageOffset()));
        nPageWidthHeight = convertTwipToMm100(GetPageWidth());
    }
    else
    {
        // Only convert from pixel to default logical unit, which is 100thmm for Impress
        nMargin1 = ConvertPosLogic(GetMargin1());
        nMargin2 = ConvertPosLogic(GetMargin2());
        nPageOffset = ConvertPosLogic(GetPageOffset());

        // In LOKit API we expect the ruler 0,0 coordinate is where the document starts.
        // In Impress and Draw the ruler 0,0 is where the canvas starts, not where the document starts.
        // The margin to the document is 1 document width (on the left and right) and 0.5 document height
        // (on the top and bottom).
        // So the canvas width = 3 * document width, canvas height = 2 * document height
        if (isHorizontal())
        {
            nPageWidthHeight = GetPageWidth() / 3;
            nNullOffset = ConvertPosLogic(GetNullOffset()) - nPageWidthHeight;
        }
        else
        {
            nPageWidthHeight = GetPageWidth() / 2;
            nNullOffset = ConvertPosLogic(GetNullOffset()) - (nPageWidthHeight / 2);
        }
    }

    rJsonWriter.put("margin1", nMargin1);
    rJsonWriter.put("margin2", nMargin2);
    rJsonWriter.put("leftOffset", nNullOffset);
    rJsonWriter.put("pageOffset", nPageOffset);
    rJsonWriter.put("pageWidth", nPageWidthHeight);

    {
        auto tabsNode = rJsonWriter.startNode("tabs");

        // The RulerTab array elements that GetTabs() returns have their nPos field in twips. So these
        // too are actual mm100.
        for (auto const& tab : GetTabs())
        {
            auto tabNode = rJsonWriter.startNode("");
            rJsonWriter.put("position", convertTwipToMm100(tab.nPos));
            rJsonWriter.put("style", tab.nStyle);
        }
    }

    RulerUnitData aUnitData = GetCurrentRulerUnit();
    rJsonWriter.put("unit", aUnitData.aUnitStr);
}

void SvxRuler::NotifyKit()
{
    if (!comphelper::LibreOfficeKit::isActive())
        return;
    SfxViewShell* pViewShell = SfxViewShell::Current();
    if (!pViewShell)
        return;

    tools::JsonWriter aJsonWriter;
    CreateJsonNotification(aJsonWriter);
    OString pJsonData = aJsonWriter.finishAndGetAsOString();
    LibreOfficeKitCallbackType eType = isHorizontal() ? LOK_CALLBACK_RULER_UPDATE : LOK_CALLBACK_VERTICAL_RULER_UPDATE;
    pViewShell->libreOfficeKitViewCallback(eType, pJsonData);
}

void SvxRuler::Update()
{
    /* Perform update of view */
    if(IsDrag())
        return;

    UpdatePage();
    UpdateFrame();
    if(nFlags & SvxRulerSupportFlags::OBJECT)
        UpdateObject();
    else
        UpdateColumns();

    if(nFlags & (SvxRulerSupportFlags::PARAGRAPH_MARGINS | SvxRulerSupportFlags::PARAGRAPH_MARGINS_VERTICAL))
      UpdatePara();

    if(nFlags & SvxRulerSupportFlags::TABS)
      UpdateTabs();

    NotifyKit();
}

tools::Long SvxRuler::GetPageWidth() const
{
    if (!mxPagePosItem)
        return 0;
    return bHorz ? mxPagePosItem->GetWidth() : mxPagePosItem->GetHeight();
}

inline tools::Long SvxRuler::GetFrameLeft() const
{
    /* Get Left margin in Pixels */
    return  bAppSetNullOffset ?
            GetMargin1() + ConvertSizePixel(lLogicNullOffset) :
            Ruler::GetNullOffset();
}

tools::Long SvxRuler::GetFirstLineIndent() const
{
    /* Get First-line indent in pixels */
    return mxParaItem ? mpIndents[INDENT_FIRST_LINE].nPos : GetMargin1();
}

tools::Long SvxRuler::GetLeftIndent() const
{
    /* Get Left paragraph margin in Pixels */
    return mxParaItem ? mpIndents[INDENT_LEFT_MARGIN].nPos : GetMargin1();
}

tools::Long SvxRuler::GetRightIndent() const
{
    /* Get Right paragraph margin in Pixels */
    return mxParaItem ? mpIndents[INDENT_RIGHT_MARGIN].nPos : GetMargin2();
}

tools::Long SvxRuler::GetLogicRightIndent() const
{
    /* Get Right paragraph margin in Logic */
    return mxParaItem ? GetRightFrameMargin() - mxParaItem->ResolveRight({})
                      : GetRightFrameMargin();
}

// Left margin in App values, is either the margin (= 0)  or the left edge of
// the column that is set in the column attribute as current column.
tools::Long SvxRuler::GetLeftFrameMargin() const
{
    // #126721# for some unknown reason the current column is set to 0xffff
    DBG_ASSERT(!mxColumnItem || mxColumnItem->GetActColumn() < mxColumnItem->Count(),
               "issue #126721# - invalid current column!");
    tools::Long nLeft = 0;
    if (mxColumnItem &&
        mxColumnItem->Count() &&
        mxColumnItem->IsConsistent())
    {
        nLeft = mxColumnItem->GetActiveColumnDescription().nStart;
    }

    if (mxBorderItem && (!mxColumnItem || mxColumnItem->IsTable()))
        nLeft += mxBorderItem->ResolveLeft({});

    return nLeft;
}

inline tools::Long SvxRuler::GetLeftMin() const
{
    DBG_ASSERT(mxMinMaxItem, "no MinMax value set");
    if (mxMinMaxItem)
    {
        if (bHorz)
            return mxMinMaxItem->GetValue().Left();
        else
            return mxMinMaxItem->GetValue().Top();
    }
    return 0;
}

inline tools::Long SvxRuler::GetRightMax() const
{
    DBG_ASSERT(mxMinMaxItem, "no MinMax value set");
    if (mxMinMaxItem)
    {
        if (bHorz)
            return mxMinMaxItem->GetValue().Right();
        else
            return mxMinMaxItem->GetValue().Bottom();
    }
    return 0;
}


tools::Long SvxRuler::GetRightFrameMargin() const
{
    /* Get right frame margin (in logical units) */
    if (mxColumnItem)
    {
        if (!IsActLastColumn(true))
        {
            return mxColumnItem->At(GetActRightColumn(true)).nEnd;
        }
    }

    tools::Long lResult = lLogicNullOffset;

    // If possible deduct right table entry
    if(mxColumnItem && mxColumnItem->IsTable())
        lResult += mxColumnItem->GetRight();
    else if(bHorz && mxLRSpaceItem)
        lResult += mxLRSpaceItem->GetRight();
    else if(!bHorz && mxULSpaceItem)
        lResult += mxULSpaceItem->GetLower();

    if (bHorz && mxBorderItem && (!mxColumnItem || mxColumnItem->IsTable()))
        lResult += mxBorderItem->ResolveRight({});

    if(bHorz)
        lResult = mxPagePosItem->GetWidth() - lResult;
    else
        lResult = mxPagePosItem->GetHeight() - lResult;

    return lResult;
}

#define NEG_FLAG ( (nFlags & SvxRulerSupportFlags::NEGATIVE_MARGINS) == \
                   SvxRulerSupportFlags::NEGATIVE_MARGINS )
#define TAB_FLAG ( mxColumnItem && mxColumnItem->IsTable() )

tools::Long SvxRuler::GetCorrectedDragPos( bool bLeft, bool bRight )
{
    /*
        Corrects the position within the calculated limits. The limit values are in
        pixels relative to the page edge.
    */

    const tools::Long lNullPix = Ruler::GetNullOffset();
    tools::Long lDragPos = GetDragPos() + lNullPix;
    bool bHoriRows = bHorz && mxRulerImpl->bIsTableRows;
    if((bLeft || bHoriRows) && lDragPos < nMaxLeft)
        lDragPos = nMaxLeft;
    else if((bRight||bHoriRows) && lDragPos > nMaxRight)
        lDragPos = nMaxRight;
    return lDragPos - lNullPix;
}

static void ModifyTabs_Impl( sal_uInt16 nCount, // Number of Tabs
                      RulerTab* pTabs,   // Tab buffer
                      tools::Long lDiff)        // difference to be added
{
    /* Helper function, move all the tabs by a fixed value */
    if( pTabs )
    {
        for(sal_uInt16 i = 0; i < nCount; ++i)
        {
            pTabs[i].nPos += lDiff;
        }
    }
}

void SvxRuler::DragMargin1()
{
    /* Dragging the left edge of frame */
    tools::Long aDragPosition = GetCorrectedDragPos( !TAB_FLAG || !NEG_FLAG );

    aDragPosition = MakePositionSticky(aDragPosition, GetRightFrameMargin(), false);

    // Check if position changed
    if (aDragPosition == 0)
        return;

    DrawLine_Impl(lTabPos, ( TAB_FLAG && NEG_FLAG ) ? 3 : 7, bHorz);
    if (mxColumnItem && (nDragType & SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL))
        DragBorders();
    AdjustMargin1(aDragPosition);
}

void SvxRuler::AdjustMargin1(tools::Long lInputDiff)
{
    const tools::Long nOld = bAppSetNullOffset? GetMargin1(): GetNullOffset();
    const tools::Long lDragPos = lInputDiff;

    bool bProtectColumns =
        mxRulerImpl->aProtectItem->IsSizeProtected() ||
        mxRulerImpl->aProtectItem->IsPosProtected();

    const RulerMarginStyle nMarginStyle =
        bProtectColumns ? RulerMarginStyle::NONE : RulerMarginStyle::Sizeable;

    if(!bAppSetNullOffset)
    {
        tools::Long lDiff = lDragPos;
        SetNullOffset(nOld + lDiff);
        if (!mxColumnItem || !(nDragType & SvxRulerDragFlags::OBJECT_SIZE_LINEAR))
        {
            SetMargin2( GetMargin2() - lDiff, nMarginStyle );

            if (!mxColumnItem && !mxObjectItem && mxParaItem)
            {
                // Right indent of the old position
                mpIndents[INDENT_RIGHT_MARGIN].nPos -= lDiff;
                SetIndents(INDENT_COUNT, mpIndents.data() + INDENT_GAP);
            }
            if (mxObjectItem)
            {
                mpObjectBorders[GetObjectBordersOff(0)].nPos -= lDiff;
                mpObjectBorders[GetObjectBordersOff(1)].nPos -= lDiff;
                SetBorders(2, mpObjectBorders.data() + GetObjectBordersOff(0));
            }
            if (mxColumnItem)
            {
                for(sal_uInt16 i = 0; i < mxColumnItem->Count()-1; ++i)
                    mpBorders[i].nPos -= lDiff;
                SetBorders(mxColumnItem->Count()-1, mpBorders.data());
                if(mxColumnItem->IsFirstAct())
                {
                    // Right indent of the old position
                    if (mxParaItem)
                    {
                        mpIndents[INDENT_RIGHT_MARGIN].nPos -= lDiff;
                        SetIndents(INDENT_COUNT, mpIndents.data() + INDENT_GAP);
                    }
                }
                else
                {
                    if (mxParaItem)
                    {
                        mpIndents[INDENT_FIRST_LINE].nPos -= lDiff;
                        mpIndents[INDENT_LEFT_MARGIN].nPos -= lDiff;
                        mpIndents[INDENT_RIGHT_MARGIN].nPos -= lDiff;
                        SetIndents(INDENT_COUNT, mpIndents.data() + INDENT_GAP);
                    }
                }
                if(mxTabStopItem && (nDragType & SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL)
                   &&!IsActFirstColumn())
                {
                    ModifyTabs_Impl(nTabCount + TAB_GAP, mpTabs.data(), -lDiff);
                    SetTabs(nTabCount, mpTabs.data() + TAB_GAP);
                }
            }
        }
    }
    else
    {
        tools::Long lDiff = lDragPos - nOld;
        SetMargin1(nOld + lDiff, nMarginStyle);

        if (!mxColumnItem
            || !(nDragType
                 & (SvxRulerDragFlags::OBJECT_SIZE_LINEAR
                    | SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL)))
        {
            if (!mxColumnItem && !mxObjectItem && mxParaItem)
            {
                // Left indent of the old position
                mpIndents[INDENT_FIRST_LINE].nPos += lDiff;
                mpIndents[INDENT_LEFT_MARGIN].nPos += lDiff;
                SetIndents(INDENT_COUNT, mpIndents.data() + INDENT_GAP);
            }

            if (mxColumnItem)
            {
                for(sal_uInt16 i = 0; i < mxColumnItem->Count() - 1; ++i)
                    mpBorders[i].nPos += lDiff;
                SetBorders(mxColumnItem->Count() - 1, mpBorders.data());
                if (mxColumnItem->IsFirstAct())
                {
                    // Left indent of the old position
                    if (mxParaItem)
                    {
                        mpIndents[INDENT_FIRST_LINE].nPos += lDiff;
                        mpIndents[INDENT_LEFT_MARGIN].nPos += lDiff;
                        SetIndents(INDENT_COUNT, mpIndents.data() + INDENT_GAP);
                    }
                }
                else
                {
                    if (mxParaItem)
                    {
                        mpIndents[INDENT_FIRST_LINE].nPos += lDiff;
                        mpIndents[INDENT_LEFT_MARGIN].nPos += lDiff;
                        mpIndents[INDENT_RIGHT_MARGIN].nPos += lDiff;
                        SetIndents(INDENT_COUNT, mpIndents.data() + INDENT_GAP);
                    }
                }
            }
            if (mxTabStopItem)
            {
                ModifyTabs_Impl(nTabCount + TAB_GAP, mpTabs.data(), lDiff);
                SetTabs(nTabCount, mpTabs.data() + TAB_GAP);
            }
        }
    }
}

void SvxRuler::DragMargin2()
{
    /* Dragging the right edge of frame */
    tools::Long aDragPosition = GetCorrectedDragPos( true, !TAB_FLAG || !NEG_FLAG);
    aDragPosition = MakePositionSticky(aDragPosition, GetLeftFrameMargin(), false);
    tools::Long lDiff = aDragPosition - GetMargin2();

    // Check if position changed
    if (lDiff == 0)
        return;

    if( mxRulerImpl->bIsTableRows &&
        !bHorz &&
        mxColumnItem &&
        (nDragType & SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL))
    {
        DragBorders();
    }

    bool bProtectColumns =
        mxRulerImpl->aProtectItem->IsSizeProtected() ||
        mxRulerImpl->aProtectItem->IsPosProtected();

    const RulerMarginStyle nMarginStyle = bProtectColumns ? RulerMarginStyle::NONE : RulerMarginStyle::Sizeable;

    SetMargin2( aDragPosition, nMarginStyle );

    // Right indent of the old position
    if ((!mxColumnItem || IsActLastColumn()) && mxParaItem)
    {
        mpIndents[INDENT_FIRST_LINE].nPos += lDiff;
        SetIndents(INDENT_COUNT, mpIndents.data() + INDENT_GAP);
    }

    DrawLine_Impl(lTabPos, ( TAB_FLAG && NEG_FLAG ) ? 5 : 7, bHorz);
}

void SvxRuler::DragIndents()
{
    /* Dragging the paragraph indents */
    tools::Long aDragPosition = NEG_FLAG ? GetDragPos() : GetCorrectedDragPos();
    const sal_uInt16 nIndex = GetDragAryPos() + INDENT_GAP;

    bool bRTL = mxRulerImpl->pTextRTLItem && mxRulerImpl->pTextRTLItem->GetValue();

    if(nIndex == INDENT_RIGHT_MARGIN)
        aDragPosition = MakePositionSticky(aDragPosition, bRTL ? GetLeftFrameMargin() : GetRightFrameMargin());
    else
        aDragPosition = MakePositionSticky(aDragPosition, bRTL ? GetRightFrameMargin() : GetLeftFrameMargin());

    const tools::Long lDiff = mpIndents[nIndex].nPos - aDragPosition;

    // Check if position changed
    if (lDiff == 0)
        return;

    if((nIndex == INDENT_FIRST_LINE || nIndex == INDENT_LEFT_MARGIN )  &&
        !(nDragType & SvxRulerDragFlags::OBJECT_LEFT_INDENT_ONLY))
    {
        mpIndents[INDENT_FIRST_LINE].nPos -= lDiff;
    }

    mpIndents[nIndex].nPos = aDragPosition;

    SetIndents(INDENT_COUNT, mpIndents.data() + INDENT_GAP);
    DrawLine_Impl(lTabPos, 1, bHorz);
}

void SvxRuler::DrawLine_Impl(tools::Long& lTabPosition, int nNew, bool bHorizontal)
{
    /*
       Output routine for the ledger line when moving tabs, tables and other
       columns
    */
    if(bHorizontal)
    {
        const tools::Long nHeight = pEditWin->GetOutDev()->GetOutputSize().Height();
        Point aZero = pEditWin->GetMapMode().GetOrigin();
        if(lTabPosition != -1)
        {
            pEditWin->InvertTracking(
                tools::Rectangle( Point(lTabPosition, -aZero.Y()),
                           Point(lTabPosition, -aZero.Y() + nHeight)),
                ShowTrackFlags::Split | ShowTrackFlags::Clip );
        }
        if( nNew & 1 )
        {
            tools::Long nDrapPosition = GetCorrectedDragPos( ( nNew & 4 ) != 0, ( nNew & 2 ) != 0 );
            nDrapPosition = MakePositionSticky(nDrapPosition, GetLeftFrameMargin());
            lTabPosition = ConvertHSizeLogic( nDrapPosition + GetNullOffset() );
            if (mxPagePosItem)
                lTabPosition += mxPagePosItem->GetPos().X();
            pEditWin->InvertTracking(
                tools::Rectangle( Point(lTabPosition, -aZero.Y()),
                           Point(lTabPosition, -aZero.Y() + nHeight) ),
                ShowTrackFlags::Clip | ShowTrackFlags::Split );
        }
    }
    else
    {
        const tools::Long nWidth = pEditWin->GetOutDev()->GetOutputSize().Width();
        Point aZero = pEditWin->GetMapMode().GetOrigin();
        if(lTabPosition != -1)
        {
            pEditWin->InvertTracking(
                tools::Rectangle( Point(-aZero.X(),          lTabPosition),
                           Point(-aZero.X() + nWidth, lTabPosition)),
                ShowTrackFlags::Split | ShowTrackFlags::Clip );
        }

        if(nNew & 1)
        {
            tools::Long nDrapPosition = GetCorrectedDragPos();
            nDrapPosition = MakePositionSticky(nDrapPosition, GetLeftFrameMargin());
            lTabPosition = ConvertVSizeLogic(nDrapPosition + GetNullOffset());
            if (mxPagePosItem)
                lTabPosition += mxPagePosItem->GetPos().Y();
            pEditWin->InvertTracking(
                tools::Rectangle( Point(-aZero.X(),        lTabPosition),
                           Point(-aZero.X()+nWidth, lTabPosition)),
                ShowTrackFlags::Clip | ShowTrackFlags::Split );
        }
    }
}

void SvxRuler::DragTabs()
{
    /* Dragging of Tabs */
    tools::Long aDragPosition = GetCorrectedDragPos(true, false);
    aDragPosition = MakePositionSticky(aDragPosition, GetLeftFrameMargin());

    sal_uInt16 nIdx = GetDragAryPos() + TAB_GAP;
    tools::Long nDiff = aDragPosition - mpTabs[nIdx].nPos;
    if (nDiff == 0)
        return;

    DrawLine_Impl(lTabPos, 7, bHorz);

    if(nDragType & SvxRulerDragFlags::OBJECT_SIZE_LINEAR)
    {

        for(sal_uInt16 i = nIdx; i < nTabCount; ++i)
        {
            mpTabs[i].nPos += nDiff;
            // limit on maximum
            if(mpTabs[i].nPos > GetMargin2())
                mpTabs[nIdx].nStyle |= RULER_STYLE_INVISIBLE;
            else
                mpTabs[nIdx].nStyle &= ~RULER_STYLE_INVISIBLE;
        }
    }
    else if(nDragType & SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL)
    {
        mxRulerImpl->nTotalDist -= nDiff;
        mpTabs[nIdx].nPos = aDragPosition;
        for(sal_uInt16 i = nIdx+1; i < nTabCount; ++i)
        {
            if(mpTabs[i].nStyle & RULER_TAB_DEFAULT)
                // can be canceled at the DefaultTabs
                break;
            tools::Long nDelta = mxRulerImpl->nTotalDist * mxRulerImpl->pPercBuf[i];
            nDelta /= 1000;
            mpTabs[i].nPos = mpTabs[nIdx].nPos + nDelta;
            if(mpTabs[i].nPos + GetNullOffset() > nMaxRight)
                mpTabs[i].nStyle |= RULER_STYLE_INVISIBLE;
            else
                mpTabs[i].nStyle &= ~RULER_STYLE_INVISIBLE;
        }
    }
    else
    {
        mpTabs[nIdx].nPos = aDragPosition;
    }

    if(IsDragDelete())
        mpTabs[nIdx].nStyle |= RULER_STYLE_INVISIBLE;
    else
        mpTabs[nIdx].nStyle &= ~RULER_STYLE_INVISIBLE;
    SetTabs(nTabCount, mpTabs.data() + TAB_GAP);
}

void SvxRuler::SetActive(bool bOn)
{
    if(bOn)
    {
        Activate();
    }
    else
        Deactivate();
    if(bActive!=bOn)
    {
        pBindings->EnterRegistrations();
        if(bOn)
            for(sal_uInt16 i=0;i<mxRulerImpl->nControllerItems;i++)
                pCtrlItems[i]->ReBind();
        else
            for(sal_uInt16 j=0;j<mxRulerImpl->nControllerItems;j++)
                pCtrlItems[j]->UnBind();
        pBindings->LeaveRegistrations();
    }
    bActive = bOn;
}

void SvxRuler::UpdateParaContents_Impl(
                            tools::Long lDifference,
                            UpdateType eType)  // Art (all, left or right)
{
    /* Helper function; carry Tabs and Paragraph Margins */
    switch(eType)
    {
        case UpdateType::MoveRight:
            mpIndents[INDENT_RIGHT_MARGIN].nPos += lDifference;
            break;
        case UpdateType::MoveLeft:
        {
            mpIndents[INDENT_FIRST_LINE].nPos += lDifference;
            mpIndents[INDENT_LEFT_MARGIN].nPos += lDifference;
            if (!mpTabs.empty())
            {
                for(sal_uInt16 i = 0; i < nTabCount+TAB_GAP; ++i)
                {
                    mpTabs[i].nPos += lDifference;
                }
                SetTabs(nTabCount, mpTabs.data() + TAB_GAP);
            }
            break;
        }
    }
    SetIndents(INDENT_COUNT, mpIndents.data() + INDENT_GAP);
}

void SvxRuler::DragBorders()
{
    /* Dragging of Borders (Tables and other columns) */
    bool bLeftIndentsCorrected  = false;
    bool bRightIndentsCorrected = false;
    int nIndex;

    if(GetDragType() == RulerType::Border)
    {
        DrawLine_Impl(lTabPos, 7, bHorz);
        nIndex = GetDragAryPos();
    }
    else
    {
        nIndex = 0;
    }

    RulerDragSize nDragSize = GetDragSize();
    tools::Long lDiff = 0;

    // the drag position has to be corrected to be able to prevent borders from passing each other
    tools::Long lPos = MakePositionSticky(GetCorrectedDragPos(), GetLeftFrameMargin());

    switch(nDragSize)
    {
        case RulerDragSize::Move:
        {
            if(GetDragType() == RulerType::Border)
                lDiff = lPos - nDragOffset - mpBorders[nIndex].nPos;
            else
                lDiff = GetDragType() == RulerType::Margin1 ? lPos - mxRulerImpl->lLastLMargin : lPos - mxRulerImpl->lLastRMargin;

            if(nDragType & SvxRulerDragFlags::OBJECT_SIZE_LINEAR)
            {
                tools::Long nRight = GetMargin2() - glMinFrame; // Right limiters
                for(int i = mpBorders.size() - 2; i >= nIndex; --i)
                {
                    tools::Long l = mpBorders[i].nPos;
                    mpBorders[i].nPos += lDiff;
                    mpBorders[i].nPos = std::min(mpBorders[i].nPos, nRight - mpBorders[i].nWidth);
                    nRight = mpBorders[i].nPos - glMinFrame;
                    // RR update the column
                    if(i == GetActRightColumn())
                    {
                        UpdateParaContents_Impl(mpBorders[i].nPos - l, UpdateType::MoveRight);
                        bRightIndentsCorrected = true;
                    }
                    // LAR, EZE update the column
                    else if(i == GetActLeftColumn())
                    {
                        UpdateParaContents_Impl(mpBorders[i].nPos - l, UpdateType::MoveLeft);
                        bLeftIndentsCorrected = true;
                    }
                }
            }
            else if(nDragType & SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL)
            {
                int nLimit;
                tools::Long lLeft;
                int nStartLimit = mpBorders.size() - 2;
                switch(GetDragType())
                {
                default: ;//prevent warning
                    OSL_FAIL("svx::SvxRuler::DragBorders(), unknown drag type!" );
                    [[fallthrough]];
                case RulerType::Border:
                    if(mxRulerImpl->bIsTableRows)
                    {
                        mpBorders[nIndex].nPos += lDiff;
                        if(bHorz)
                        {
                            lLeft = mpBorders[nIndex].nPos;
                            mxRulerImpl->nTotalDist -= lDiff;
                            nLimit = nIndex + 1;
                        }
                        else
                        {
                            lLeft = 0;
                            nStartLimit = nIndex - 1;
                            mxRulerImpl->nTotalDist += lDiff;
                            nLimit = 0;
                        }
                    }
                    else
                    {
                        nLimit = nIndex + 1;
                        mpBorders[nIndex].nPos += lDiff;
                        lLeft = mpBorders[nIndex].nPos;
                        mxRulerImpl->nTotalDist -= lDiff;
                    }
                break;
                case RulerType::Margin1:
                    nLimit = 0;
                    lLeft = mxRulerImpl->lLastLMargin + lDiff;
                    mxRulerImpl->nTotalDist -= lDiff;
                break;
                case RulerType::Margin2:
                    nLimit = 0;
                    lLeft= 0;
                    nStartLimit = mpBorders.size() - 2;
                    mxRulerImpl->nTotalDist += lDiff;
                break;
                }

                for(int i  = nStartLimit; i >= nLimit; --i)
                {

                    tools::Long l = mpBorders[i].nPos;
                    mpBorders[i].nPos =
                        lLeft +
                        (mxRulerImpl->nTotalDist * mxRulerImpl->pPercBuf[i]) / 1000 +
                        mxRulerImpl->pBlockBuf[i];

                    // RR update the column
                    if(!mxRulerImpl->bIsTableRows)
                    {
                        if(i == GetActRightColumn())
                        {
                            UpdateParaContents_Impl(mpBorders[i].nPos - l, UpdateType::MoveRight);
                            bRightIndentsCorrected = true;
                        }
                        // LAR, EZE update the column
                        else if(i == GetActLeftColumn())
                        {
                            UpdateParaContents_Impl(mpBorders[i].nPos - l, UpdateType::MoveLeft);
                            bLeftIndentsCorrected = true;
                        }
                    }
                }
                if(mxRulerImpl->bIsTableRows)
                {
                    //in vertical tables the left borders have to be moved
                    if(bHorz)
                    {
                        for(int i  = 0; i < nIndex; ++i)
                            mpBorders[i].nPos += lDiff;
                        AdjustMargin1(lDiff);
                    }
                    else
                    {
                        //otherwise the right borders are moved
                        for(int i  = mxColumnItem->Count() - 1; i > nIndex; --i)
                            mpBorders[i].nPos += lDiff;
                        SetMargin2( GetMargin2() + lDiff, RulerMarginStyle::NONE );
                    }
                }
            }
            else if(mxRulerImpl->bIsTableRows)
            {
                //moving rows: if a row is resized all following rows
                //have to be moved by the same amount.
                //This includes the left border when the table is not limited
                //to a lower frame border.
                int nLimit;
                if(GetDragType()==RulerType::Border)
                {
                    nLimit = nIndex + 1;
                    mpBorders[nIndex].nPos += lDiff;
                }
                else
                {
                    nLimit=0;
                }
                //in vertical tables the left borders have to be moved
                if(bHorz)
                {
                    for(int i  = 0; i < nIndex; ++i)
                    {
                        mpBorders[i].nPos += lDiff;
                    }
                    AdjustMargin1(lDiff);
                }
                else
                {
                    //otherwise the right borders are moved
                    for(int i  = mpBorders.size() - 2; i >= nLimit; --i)
                    {
                        mpBorders[i].nPos += lDiff;
                    }
                    SetMargin2( GetMargin2() + lDiff, RulerMarginStyle::NONE );
                }
            }
            else
                mpBorders[nIndex].nPos += lDiff;
            break;
        }
      case RulerDragSize::N1:
        {
            lDiff = lPos - mpBorders[nIndex].nPos;
            mpBorders[nIndex].nWidth += mpBorders[nIndex].nPos - lPos;
            mpBorders[nIndex].nPos = lPos;
            break;
        }
      case RulerDragSize::N2:
        {
            const tools::Long nOld = mpBorders[nIndex].nWidth;
            mpBorders[nIndex].nWidth = lPos - mpBorders[nIndex].nPos;
            lDiff = mpBorders[nIndex].nWidth - nOld;
            break;
        }
    }
    if(!bRightIndentsCorrected &&
       GetActRightColumn() == nIndex &&
       nDragSize != RulerDragSize::N2 &&
       !mpIndents.empty() &&
       !mxRulerImpl->bIsTableRows)
    {
        UpdateParaContents_Impl(lDiff, UpdateType::MoveRight);
    }
    else if(!bLeftIndentsCorrected &&
            GetActLeftColumn() == nIndex &&
            nDragSize != RulerDragSize::N1 &&
            !mpIndents.empty())
    {
        UpdateParaContents_Impl(lDiff, UpdateType::MoveLeft);
    }
    SetBorders(mxColumnItem->Count() - 1, mpBorders.data());
}

void SvxRuler::DragObjectBorder()
{
    /* Dragging of object edges */
    if(RulerDragSize::Move == GetDragSize())
    {
        const tools::Long lPosition = MakePositionSticky(GetCorrectedDragPos(), GetLeftFrameMargin());

        const sal_uInt16 nIdx = GetDragAryPos();
        mpObjectBorders[GetObjectBordersOff(nIdx)].nPos = lPosition;
        SetBorders(2, mpObjectBorders.data() + GetObjectBordersOff(0));
        DrawLine_Impl(lTabPos, 7, bHorz);

    }
}

void SvxRuler::ApplyMargins()
{
    /* Applying margins; changed by dragging. */
    const SfxPoolItem* pItem = nullptr;
    sal_uInt16 nId = SID_ATTR_LONG_LRSPACE;

    if(bHorz)
    {
        const tools::Long lOldNull = lLogicNullOffset;
        if(mxRulerImpl->lMaxLeftLogic != -1 && nMaxLeft == GetMargin1() + Ruler::GetNullOffset())
        {
            lLogicNullOffset = mxRulerImpl->lMaxLeftLogic;
            mxLRSpaceItem->SetLeft(lLogicNullOffset);
        }
        else
        {
            lLogicNullOffset = ConvertHPosLogic(GetFrameLeft()) - lAppNullOffset;
            mxLRSpaceItem->SetLeft(PixelHAdjust(lLogicNullOffset, mxLRSpaceItem->GetLeft()));
        }

        if(bAppSetNullOffset)
        {
            lAppNullOffset += lLogicNullOffset - lOldNull;
        }

        tools::Long nRight;
        if(mxRulerImpl->lMaxRightLogic != -1
           && nMaxRight == GetMargin2() + Ruler::GetNullOffset())
        {
            nRight = GetPageWidth() - mxRulerImpl->lMaxRightLogic;
        }
        else
        {
            nRight = std::max(tools::Long(0),
                            mxPagePosItem->GetWidth() - mxLRSpaceItem->GetLeft() -
                                (ConvertHPosLogic(GetMargin2()) - lAppNullOffset));

            nRight = PixelHAdjust( nRight, mxLRSpaceItem->GetRight());
        }
        mxLRSpaceItem->SetRight(nRight);

        pItem = mxLRSpaceItem.get();

#ifdef DEBUGLIN
        Debug_Impl(pEditWin, *mxLRSpaceItem);
#endif // DEBUGLIN

    }
    else
    {
        const tools::Long lOldNull = lLogicNullOffset;
        lLogicNullOffset =
                ConvertVPosLogic(GetFrameLeft()) -
                lAppNullOffset;
        mxULSpaceItem->SetUpper(
            PixelVAdjust(lLogicNullOffset, mxULSpaceItem->GetUpper()));
        if(bAppSetNullOffset)
        {
            lAppNullOffset += lLogicNullOffset - lOldNull;
        }
        mxULSpaceItem->SetLower(
            PixelVAdjust(
                std::max(tools::Long(0), mxPagePosItem->GetHeight() -
                    mxULSpaceItem->GetUpper() -
                    (ConvertVPosLogic(GetMargin2()) -
                     lAppNullOffset)), mxULSpaceItem->GetLower()));
        pItem = mxULSpaceItem.get();
        nId = SID_ATTR_LONG_ULSPACE;

#ifdef DEBUGLIN
        Debug_Impl(pEditWin,*mxULSpaceItem);
#endif // DEBUGLIN

    }
    pBindings->GetDispatcher()->ExecuteList(nId, SfxCallMode::RECORD, { pItem });
    if (mxTabStopItem)
        UpdateTabs();
}

tools::Long SvxRuler::RoundToCurrentMapMode(tools::Long lValue) const
{
    RulerUnitData aUnitData = GetCurrentRulerUnit();
    double aRoundingFactor = aUnitData.nTickUnit / aUnitData.nTick1;

    tools::Long lNewValue = OutputDevice::LogicToLogic(Size(lValue, 0), pEditWin->GetMapMode(), GetCurrentMapMode()).Width();
    lNewValue = (rtl::math::round(lNewValue / static_cast<double>(aUnitData.nTickUnit) * aRoundingFactor) / aRoundingFactor) * aUnitData.nTickUnit;
    return OutputDevice::LogicToLogic(Size(lNewValue, 0), GetCurrentMapMode(), pEditWin->GetMapMode()).Width();
}

void SvxRuler::ApplyIndents()
{
    /* Applying paragraph settings; changed by dragging. */

    tools::Long nLeftFrameMargin  = GetLeftFrameMargin();

    bool bRTL = mxRulerImpl->pTextRTLItem && mxRulerImpl->pTextRTLItem->GetValue();

    tools::Long nNewTxtLeft;
    tools::Long nNewFirstLineOffset;
    tools::Long nNewRight;

    tools::Long nFirstLine    = ConvertPosLogic(mpIndents[INDENT_FIRST_LINE].nPos);
    tools::Long nLeftMargin   = ConvertPosLogic(mpIndents[INDENT_LEFT_MARGIN].nPos);
    tools::Long nRightMargin  = ConvertPosLogic(mpIndents[INDENT_RIGHT_MARGIN].nPos);

    if(mxColumnItem && ((bRTL && !IsActLastColumn(true)) || (!bRTL && !IsActFirstColumn(true))))
    {
        if(bRTL)
        {
            tools::Long nRightColumn  = GetActRightColumn(true);
            tools::Long nRightBorder  = ConvertPosLogic(mpBorders[nRightColumn].nPos);
            nNewTxtLeft = nRightBorder - nLeftMargin - lAppNullOffset;
        }
        else
        {
            tools::Long nLeftColumn = GetActLeftColumn(true);
            tools::Long nLeftBorder = ConvertPosLogic(mpBorders[nLeftColumn].nPos + mpBorders[nLeftColumn].nWidth);
            nNewTxtLeft = nLeftMargin - nLeftBorder - lAppNullOffset;
        }
    }
    else
    {
        if(bRTL)
        {
            tools::Long nRightBorder = ConvertPosLogic(GetMargin2());
            nNewTxtLeft = nRightBorder - nLeftMargin - lAppNullOffset;
        }
        else
        {
            tools::Long nLeftBorder = ConvertPosLogic(GetMargin1());
            nNewTxtLeft = nLeftBorder + nLeftMargin - nLeftFrameMargin - lAppNullOffset;
        }
    }

    if(bRTL)
        nNewFirstLineOffset = nLeftMargin - nFirstLine - lAppNullOffset;
    else
        nNewFirstLineOffset = nFirstLine - nLeftMargin - lAppNullOffset;

    if(mxColumnItem && ((!bRTL && !IsActLastColumn(true)) || (bRTL && !IsActFirstColumn(true))))
    {
        if(bRTL)
        {
            tools::Long nLeftColumn = GetActLeftColumn(true);
            tools::Long nLeftBorder = ConvertPosLogic(mpBorders[nLeftColumn].nPos + mpBorders[nLeftColumn].nWidth);
            nNewRight = nRightMargin - nLeftBorder - lAppNullOffset;
        }
        else
        {
            tools::Long nRightColumn  = GetActRightColumn(true);
            tools::Long nRightBorder  = ConvertPosLogic(mpBorders[nRightColumn].nPos);
            nNewRight = nRightBorder - nRightMargin - lAppNullOffset;
        }
    }
    else
    {
        if(bRTL)
        {
            tools::Long nLeftBorder = ConvertPosLogic(GetMargin1());
            nNewRight = nLeftBorder + nRightMargin - nLeftFrameMargin - lAppNullOffset;
        }
        else
        {
            tools::Long nRightBorder = ConvertPosLogic(GetMargin2());
            nNewRight = nRightBorder - nRightMargin - lAppNullOffset;
        }
    }

    if (mbSnapping)
    {
        nNewTxtLeft         = RoundToCurrentMapMode(nNewTxtLeft);
        nNewFirstLineOffset = RoundToCurrentMapMode(nNewFirstLineOffset);
        nNewRight           = RoundToCurrentMapMode(nNewRight);
    }

    mxParaItem->SetTextFirstLineOffset(SvxIndentValue::twips(nNewFirstLineOffset));
    mxParaItem->SetTextLeft(SvxIndentValue::twips(nNewTxtLeft));
    mxParaItem->SetRight(SvxIndentValue::twips(nNewRight));

    sal_uInt16 nParagraphId  = bHorz ? SID_ATTR_PARA_LRSPACE : SID_ATTR_PARA_LRSPACE_VERTICAL;
    pBindings->GetDispatcher()->ExecuteList(nParagraphId, SfxCallMode::RECORD,
            { mxParaItem.get() });
    UpdateTabs();
}

void SvxRuler::ApplyTabs()
{
    /* Apply tab settings, changed by dragging. */
    bool bRTL = mxRulerImpl->pTextRTLItem && mxRulerImpl->pTextRTLItem->GetValue();
    const sal_uInt16 nCoreIdx = GetDragAryPos();
    if(IsDragDelete())
    {
        mxTabStopItem->Remove(nCoreIdx);
    }
    else if(SvxRulerDragFlags::OBJECT_SIZE_LINEAR & nDragType ||
            SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL & nDragType)
    {
        SvxTabStopItem *pItem = new SvxTabStopItem(mxTabStopItem->Which());
        //remove default tab stops
        for ( sal_uInt16 i = 0; i < pItem->Count(); )
        {
            if ( SvxTabAdjust::Default == (*pItem)[i].GetAdjustment() )
            {
                pItem->Remove(i);
                continue;
            }
            ++i;
        }

        sal_uInt16 j;
        for(j = 0; j < nCoreIdx; ++j)
        {
            pItem->Insert(mxTabStopItem->At(j));
        }
        for(; j < mxTabStopItem->Count(); ++j)
        {
            SvxTabStop aTabStop = mxTabStopItem->At(j);
            aTabStop.GetTabPos() = PixelHAdjust(
                ConvertHPosLogic(
                    mpTabs[j + TAB_GAP].nPos - GetLeftIndent()) - lAppNullOffset,
                aTabStop.GetTabPos());
            pItem->Insert(aTabStop);
        }
        mxTabStopItem.reset(pItem);
    }
    else if( mxTabStopItem->Count() == 0 )
        return;
    else
    {
        SvxTabStop aTabStop = mxTabStopItem->At(nCoreIdx);
        if( mxRulerImpl->lMaxRightLogic != -1 &&
            mpTabs[nCoreIdx + TAB_GAP].nPos + Ruler::GetNullOffset() == nMaxRight )
        {
            // Set tab pos exactly at the right indent
            tools::Long nTmpLeftIndentLogic
                = lAppNullOffset + (bRTL ? GetRightFrameMargin() : GetLeftFrameMargin());
            if (mxRulerImpl->bIsTabsRelativeToIndent && mxParaItem)
            {
                nTmpLeftIndentLogic
                    += bRTL ? mxParaItem->ResolveRight({}) : mxParaItem->ResolveTextLeft({});
            }
            aTabStop.GetTabPos()
                = mxRulerImpl->lMaxRightLogic - lLogicNullOffset - nTmpLeftIndentLogic;
        }
        else
        {
            if(bRTL)
            {
                //#i24363# tab stops relative to indent
                const tools::Long nTmpLeftIndent = mxRulerImpl->bIsTabsRelativeToIndent ?
                                            GetLeftIndent() :
                                            ConvertHPosPixel( GetRightFrameMargin() + lAppNullOffset );

                tools::Long nNewPosition = ConvertHPosLogic(nTmpLeftIndent - mpTabs[nCoreIdx + TAB_GAP].nPos);
                aTabStop.GetTabPos() = PixelHAdjust(nNewPosition - lAppNullOffset, aTabStop.GetTabPos());
            }
            else
            {
                //#i24363# tab stops relative to indent
                const tools::Long nTmpLeftIndent = mxRulerImpl->bIsTabsRelativeToIndent ?
                                            GetLeftIndent() :
                                            ConvertHPosPixel( GetLeftFrameMargin() + lAppNullOffset );

                tools::Long nNewPosition = ConvertHPosLogic(mpTabs[nCoreIdx + TAB_GAP].nPos - nTmpLeftIndent);
                aTabStop.GetTabPos() = PixelHAdjust(nNewPosition - lAppNullOffset, aTabStop.GetTabPos());
            }
        }
        mxTabStopItem->Remove(nCoreIdx);
        mxTabStopItem->Insert(aTabStop);
    }
    sal_uInt16 nTabStopId = bHorz ? SID_ATTR_TABSTOP : SID_ATTR_TABSTOP_VERTICAL;
    pBindings->GetDispatcher()->ExecuteList(nTabStopId, SfxCallMode::RECORD,
            { mxTabStopItem.get() });
    UpdateTabs();
}

void SvxRuler::ApplyBorders()
{
    /* Applying (table) column settings; changed by dragging. */
    if(mxColumnItem->IsTable())
    {
        tools::Long lValue = GetFrameLeft();
        if(lValue != mxRulerImpl->nColLeftPix)
        {
            tools::Long nLeft = PixelHAdjust(
                            ConvertHPosLogic(lValue) -
                                lAppNullOffset,
                            mxColumnItem->GetLeft());
            mxColumnItem->SetLeft(nLeft);
        }

        lValue = GetMargin2();

        if(lValue != mxRulerImpl->nColRightPix)
        {
            tools::Long nWidthOrHeight = bHorz ? mxPagePosItem->GetWidth() : mxPagePosItem->GetHeight();
            tools::Long nRight = PixelHAdjust(
                            nWidthOrHeight -
                                mxColumnItem->GetLeft() -
                                ConvertHPosLogic(lValue) -
                                lAppNullOffset,
                            mxColumnItem->GetRight() );
            mxColumnItem->SetRight(nRight);
        }
    }

    for(sal_uInt16 i = 0; i < mxColumnItem->Count() - 1; ++i)
    {
        tools::Long& nEnd = mxColumnItem->At(i).nEnd;
        nEnd = PixelHAdjust(
                ConvertPosLogic(mpBorders[i].nPos),
                mxColumnItem->At(i).nEnd);
        tools::Long& nStart = mxColumnItem->At(i + 1).nStart;
        nStart = PixelHAdjust(
                    ConvertSizeLogic(mpBorders[i].nPos +
                        mpBorders[i].nWidth) -
                        lAppNullOffset,
                    mxColumnItem->At(i + 1).nStart);
        // It may be that, due to the PixelHAdjust readjustment to old values,
        // the width becomes  < 0. This we readjust.
        if( nEnd > nStart )
            nStart = nEnd;
    }

#ifdef DEBUGLIN
        Debug_Impl(pEditWin,*mxColumnItem);
#endif // DEBUGLIN

    SfxBoolItem aFlag(SID_RULER_ACT_LINE_ONLY,
                      bool(nDragType & SvxRulerDragFlags::OBJECT_ACTLINE_ONLY));

    sal_uInt16 nColId = mxRulerImpl->bIsTableRows ? (bHorz ? SID_RULER_ROWS : SID_RULER_ROWS_VERTICAL) :
                            (bHorz ? SID_RULER_BORDERS : SID_RULER_BORDERS_VERTICAL);

    pBindings->GetDispatcher()->ExecuteList(nColId, SfxCallMode::RECORD,
            { mxColumnItem.get(), &aFlag });
}

void SvxRuler::ApplyObject()
{
    /* Applying object settings, changed by dragging. */

    // to the page margin
    tools::Long nMargin = mxLRSpaceItem ? mxLRSpaceItem->GetLeft() : 0;
    tools::Long nStartX = PixelAdjust(
                    ConvertPosLogic(mpObjectBorders[0].nPos) +
                        nMargin -
                        lAppNullOffset,
                    mxObjectItem->GetStartX());
    mxObjectItem->SetStartX(nStartX);

    tools::Long nEndX = PixelAdjust(
                    ConvertPosLogic(mpObjectBorders[1].nPos) +
                        nMargin -
                        lAppNullOffset,
                    mxObjectItem->GetEndX());
    mxObjectItem->SetEndX(nEndX);

    nMargin = mxULSpaceItem ? mxULSpaceItem->GetUpper() : 0;
    tools::Long nStartY = PixelAdjust(
                    ConvertPosLogic(mpObjectBorders[2].nPos) +
                        nMargin -
                        lAppNullOffset,
                    mxObjectItem->GetStartY());
    mxObjectItem->SetStartY(nStartY);

    tools::Long nEndY = PixelAdjust(
                    ConvertPosLogic(mpObjectBorders[3].nPos) +
                        nMargin -
                        lAppNullOffset,
                    mxObjectItem->GetEndY());
    mxObjectItem->SetEndY(nEndY);

    pBindings->GetDispatcher()->ExecuteList(SID_RULER_OBJECT,
            SfxCallMode::RECORD, { mxObjectItem.get() });
}

void SvxRuler::PrepareProportional_Impl(RulerType eType)
{
    /*
       Preparation proportional dragging, and it is calculated based on the
       proportional share of the total width in parts per thousand.
    */
    mxRulerImpl->nTotalDist = GetMargin2();
    switch(eType)
    {
      case RulerType::Margin2:
      case RulerType::Margin1:
      case RulerType::Border:
        {
            DBG_ASSERT(mxColumnItem, "no ColumnItem");

            mxRulerImpl->SetPercSize(mxColumnItem->Count());

            tools::Long lPos;
            tools::Long lWidth=0;
            sal_uInt16 nStart;
            sal_uInt16 nIdx=GetDragAryPos();
            tools::Long lActWidth=0;
            tools::Long lActBorderSum;
            tools::Long lOrigLPos;

            if(eType != RulerType::Border)
            {
                lOrigLPos = GetMargin1();
                nStart = 0;
                lActBorderSum = 0;
            }
            else
            {
                if(mxRulerImpl->bIsTableRows &&!bHorz)
                {
                    lOrigLPos = GetMargin1();
                    nStart = 0;
                }
                else
                {
                    lOrigLPos = mpBorders[nIdx].nPos + mpBorders[nIdx].nWidth;
                    nStart = 1;
                }
                lActBorderSum = mpBorders[nIdx].nWidth;
            }

            //in horizontal mode the percentage value has to be
            //calculated on a "current change" position base
            //because the height of the table changes while dragging
            if(mxRulerImpl->bIsTableRows && RulerType::Border == eType)
            {
                sal_uInt16 nStartBorder;
                sal_uInt16 nEndBorder;
                if(bHorz)
                {
                    nStartBorder = nIdx + 1;
                    nEndBorder = mxColumnItem->Count() - 1;
                }
                else
                {
                    nStartBorder = 0;
                    nEndBorder = nIdx;
                }

                lWidth = mpBorders[nIdx].nPos;
                if(bHorz)
                    lWidth = GetMargin2() - lWidth;
                mxRulerImpl->nTotalDist = lWidth;
                lPos = mpBorders[nIdx].nPos;

                for(sal_uInt16 i = nStartBorder; i < nEndBorder; ++i)
                {
                    if(bHorz)
                    {
                        lActWidth += mpBorders[i].nPos - lPos;
                        lPos = mpBorders[i].nPos + mpBorders[i].nWidth;
                    }
                    else
                        lActWidth = mpBorders[i].nPos;
                    mxRulerImpl->pPercBuf[i] = static_cast<sal_uInt16>((lActWidth * 1000)
                                                    / mxRulerImpl->nTotalDist);
                    mxRulerImpl->pBlockBuf[i] = static_cast<sal_uInt16>(lActBorderSum);
                    lActBorderSum += mpBorders[i].nWidth;
                }
            }
            else
            {
                lPos = lOrigLPos;
                for(sal_uInt16 ii = nStart; ii < mxColumnItem->Count() - 1; ++ii)
                {
                    lWidth += mpBorders[ii].nPos - lPos;
                    lPos = mpBorders[ii].nPos + mpBorders[ii].nWidth;
                }

                lWidth += GetMargin2() - lPos;
                mxRulerImpl->nTotalDist = lWidth;
                lPos = lOrigLPos;

                for(sal_uInt16 i = nStart; i < mxColumnItem->Count() - 1; ++i)
                {
                    lActWidth += mpBorders[i].nPos - lPos;
                    lPos = mpBorders[i].nPos + mpBorders[i].nWidth;
                    mxRulerImpl->pPercBuf[i] = static_cast<sal_uInt16>((lActWidth * 1000)
                                                    / mxRulerImpl->nTotalDist);
                    mxRulerImpl->pBlockBuf[i] = static_cast<sal_uInt16>(lActBorderSum);
                    lActBorderSum += mpBorders[i].nWidth;
                }
            }
        }
        break;
        case RulerType::Tab:
        {
            const sal_uInt16 nIdx = GetDragAryPos()+TAB_GAP;
            mxRulerImpl->nTotalDist -= mpTabs[nIdx].nPos;
            mxRulerImpl->SetPercSize(nTabCount);
            for(sal_uInt16 n=0;n<=nIdx;mxRulerImpl->pPercBuf[n++]=0) ;
            for(sal_uInt16 i = nIdx+1; i < nTabCount; ++i)
            {
                const tools::Long nDelta = mpTabs[i].nPos - mpTabs[nIdx].nPos;
                mxRulerImpl->pPercBuf[i] = static_cast<sal_uInt16>((nDelta * 1000) / mxRulerImpl->nTotalDist);
            }
            break;
        }
        default: break;
    }
}

void SvxRuler::EvalModifier()
{
    /*
    Eval Drag Modifier
    Shift: move linear
    Control: move proportional
    Shift + Control: Table: only current line
    Alt: disable snapping
    Alt + Shift: coarse snapping
    */

    sal_uInt16 nModifier = GetDragModifier();
    if(mxRulerImpl->bIsTableRows)
    {
        //rows can only be moved in one way, additionally current column is possible
        if(nModifier == KEY_SHIFT)
            nModifier = 0;
    }

    switch(nModifier)
    {
        case KEY_SHIFT:
            nDragType = SvxRulerDragFlags::OBJECT_SIZE_LINEAR;
        break;
        case KEY_MOD2 | KEY_SHIFT:
            mbCoarseSnapping = true;
        break;
        case KEY_MOD2:
            mbSnapping = false;
        break;
        case KEY_MOD1:
        {
            const RulerType eType = GetDragType();
            nDragType = SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL;
            if( RulerType::Tab == eType ||
                ( ( RulerType::Border == eType  ||
                    RulerType::Margin1 == eType ||
                    RulerType::Margin2 == eType ) &&
                mxColumnItem ) )
            {
                PrepareProportional_Impl(eType);
            }
        }
        break;
        case KEY_MOD1 | KEY_SHIFT:
            if( GetDragType() != RulerType::Margin1 &&
                GetDragType() != RulerType::Margin2 )
            {
                nDragType = SvxRulerDragFlags::OBJECT_ACTLINE_ONLY;
            }
        break;
    }
}

void SvxRuler::Click()
{
    /* Override handler SV; sets Tab per dispatcher call */
    Ruler::Click();
    if( bActive )
    {
        pBindings->Update( SID_RULER_LR_MIN_MAX );
        pBindings->Update( SID_ATTR_LONG_ULSPACE );
        pBindings->Update( SID_ATTR_LONG_LRSPACE );
        pBindings->Update( SID_RULER_PAGE_POS );
        pBindings->Update( bHorz ? SID_ATTR_TABSTOP : SID_ATTR_TABSTOP_VERTICAL);
        pBindings->Update( bHorz ? SID_ATTR_PARA_LRSPACE : SID_ATTR_PARA_LRSPACE_VERTICAL);
        pBindings->Update( bHorz ? SID_RULER_BORDERS : SID_RULER_BORDERS_VERTICAL);
        pBindings->Update( bHorz ? SID_RULER_ROWS : SID_RULER_ROWS_VERTICAL);
        pBindings->Update( SID_RULER_OBJECT );
        pBindings->Update( SID_RULER_PROTECT );
        pBindings->Update( SID_ATTR_PARA_LRSPACE_VERTICAL );
    }
    bool bRTL = mxRulerImpl->pTextRTLItem && mxRulerImpl->pTextRTLItem->GetValue();
    if(!(mxTabStopItem &&
       (nFlags & SvxRulerSupportFlags::TABS) == SvxRulerSupportFlags::TABS))
        return;

    bool bContentProtected = mxRulerImpl->aProtectItem->IsContentProtected();
    if( bContentProtected ) return;
    const tools::Long lPos = GetClickPos();
    if(!((bRTL && lPos < std::min(GetFirstLineIndent(), GetLeftIndent()) && lPos > GetRightIndent()) ||
        (!bRTL && lPos > std::min(GetFirstLineIndent(), GetLeftIndent()) && lPos < GetRightIndent())))
        return;

    //convert position in left-to-right text
    tools::Long nTabPos;
//#i24363# tab stops relative to indent
    if(bRTL)
        nTabPos = ( mxRulerImpl->bIsTabsRelativeToIndent ?
                    GetLeftIndent() :
                    ConvertHPosPixel( GetRightFrameMargin() + lAppNullOffset ) ) -
                  lPos;
    else
        nTabPos = lPos -
                  ( mxRulerImpl->bIsTabsRelativeToIndent ?
                    GetLeftIndent() :
                    ConvertHPosPixel( GetLeftFrameMargin() + lAppNullOffset ));

    SvxTabStop aTabStop(ConvertHPosLogic(nTabPos),
                        ToAttrTab_Impl(nDefTabType));
    mxTabStopItem->Insert(aTabStop);
    UpdateTabs();
}

void SvxRuler::CalcMinMax()
{
    /*
       Calculates the limits for dragging; which are in pixels relative to the
       page edge
    */
    bool bRTL = mxRulerImpl->pTextRTLItem && mxRulerImpl->pTextRTLItem->GetValue();
    const tools::Long lNullPix = ConvertPosPixel(lLogicNullOffset);
    mxRulerImpl->lMaxLeftLogic=mxRulerImpl->lMaxRightLogic=-1;
    switch(GetDragType())
    {
        case RulerType::Margin1:
        {        // left edge of the surrounding Frame
            // DragPos - NOf between left - right
            mxRulerImpl->lMaxLeftLogic = GetLeftMin();
            nMaxLeft=ConvertSizePixel(mxRulerImpl->lMaxLeftLogic);

            if (!mxColumnItem || mxColumnItem->Count() == 1)
            {
                if(bRTL)
                {
                    nMaxRight = lNullPix - GetRightIndent() +
                        std::max(GetFirstLineIndent(), GetLeftIndent()) -
                        glMinFrame;
                }
                else
                {
                    nMaxRight = lNullPix + GetRightIndent() -
                        std::max(GetFirstLineIndent(), GetLeftIndent()) -
                        glMinFrame;
                }
            }
            else if(mxRulerImpl->bIsTableRows)
            {
                //top border is not moveable when table rows are displayed
                // protection of content means the margin is not moveable
                if(bHorz && !mxRulerImpl->aProtectItem->IsContentProtected())
                {
                    nMaxLeft = mpBorders[0].nMinPos + lNullPix;
                    if(nDragType & SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL)
                        nMaxRight = GetRightIndent() + lNullPix -
                                (mxColumnItem->Count() - 1 ) * glMinFrame;
                    else
                        nMaxRight = mpBorders[0].nPos - glMinFrame + lNullPix;
                }
                else
                    nMaxLeft = nMaxRight = lNullPix;
            }
            else
            {
                if (nDragType & SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL)
                {
                    nMaxRight=lNullPix+CalcPropMaxRight();
                }
                else if (nDragType & SvxRulerDragFlags::OBJECT_SIZE_LINEAR)
                {
                    nMaxRight = ConvertPosPixel(
                        GetPageWidth() - (
                            (mxColumnItem->IsTable() && mxLRSpaceItem)
                            ? mxLRSpaceItem->GetRight() : 0))
                            - GetMargin2() + GetMargin1();
                }
                else
                {
                    nMaxRight = lNullPix - glMinFrame;
                    if (mxColumnItem->IsFirstAct())
                    {
                        if(bRTL)
                        {
                            nMaxRight += std::min(
                                mpBorders[0].nPos,
                                std::max(GetFirstLineIndent(), GetLeftIndent()) - GetRightIndent());
                        }
                        else
                        {
                            nMaxRight += std::min(
                                mpBorders[0].nPos, GetRightIndent() -
                                std::max(GetFirstLineIndent(), GetLeftIndent()));
                        }
                    }
                    else if ( mxColumnItem->Count() > 1 )
                    {
                        nMaxRight += mpBorders[0].nPos;
                    }
                    else
                    {
                        nMaxRight += GetRightIndent() - std::max(GetFirstLineIndent(), GetLeftIndent());
                    }
                    // Do not drag the left table edge over the edge of the page
                    if(mxLRSpaceItem && mxColumnItem->IsTable())
                    {
                        tools::Long nTmp=ConvertSizePixel(mxLRSpaceItem->GetLeft());
                        if(nTmp>nMaxLeft)
                            nMaxLeft=nTmp;
                    }
                }
            }
            break;
        }
        case RulerType::Margin2:
        {        // right edge of the surrounding Frame
            mxRulerImpl->lMaxRightLogic
                = mxMinMaxItem ? GetPageWidth() - GetRightMax() : GetPageWidth();
            nMaxRight = ConvertSizePixel(mxRulerImpl->lMaxRightLogic);

            if (!mxColumnItem)
            {
                if(bRTL)
                {
                    nMaxLeft =  GetMargin2() + GetRightIndent() -
                        std::max(GetFirstLineIndent(),GetLeftIndent())  - GetMargin1()+
                            glMinFrame + lNullPix;
                }
                else
                {
                    nMaxLeft =  GetMargin2() - GetRightIndent() +
                        std::max(GetFirstLineIndent(),GetLeftIndent())  - GetMargin1()+
                            glMinFrame + lNullPix;
                }
            }
            else if(mxRulerImpl->bIsTableRows)
            {
                // get the bottom move range from the last border position - only available for rows!
                // protection of content means the margin is not moveable
                if(bHorz || mxRulerImpl->aProtectItem->IsContentProtected())
                {
                    nMaxLeft = nMaxRight = mpBorders[mxColumnItem->Count() - 1].nMaxPos + lNullPix;
                }
                else
                {
                    if(nDragType & SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL)
                    {
                        nMaxLeft = (mxColumnItem->Count()) * glMinFrame + lNullPix;
                    }
                    else
                    {
                        if(mxColumnItem->Count() > 1)
                            nMaxLeft = mpBorders[mxColumnItem->Count() - 2].nPos + glMinFrame + lNullPix;
                        else
                            nMaxLeft = glMinFrame + lNullPix;
                    }
                    if(mxColumnItem->Count() > 1)
                        nMaxRight = mpBorders[mxColumnItem->Count() - 2].nMaxPos + lNullPix;
                    else
                        nMaxRight -= GetRightIndent() - lNullPix;
                }
            }
            else
            {
                nMaxLeft = glMinFrame + lNullPix;
                if(IsActLastColumn() || mxColumnItem->Count() < 2 ) //If last active column
                {
                    if(bRTL)
                    {
                        nMaxLeft = glMinFrame + lNullPix + GetMargin2() +
                            GetRightIndent() - std::max(GetFirstLineIndent(),
                                                   GetLeftIndent());
                    }
                    else
                    {
                        nMaxLeft = glMinFrame + lNullPix + GetMargin2() -
                            GetRightIndent() + std::max(GetFirstLineIndent(),
                                                   GetLeftIndent());
                    }
                }
                if( mxColumnItem->Count() >= 2 )
                {
                    tools::Long nNewMaxLeft =
                        glMinFrame + lNullPix +
                        mpBorders[mxColumnItem->Count() - 2].nPos +
                        mpBorders[mxColumnItem->Count() - 2].nWidth;
                    nMaxLeft = std::max(nMaxLeft, nNewMaxLeft);
                }

            }
            break;
        }
        case RulerType::Border:
        {                // Table, column (Modifier)
        const sal_uInt16 nIdx = GetDragAryPos();
        switch(GetDragSize())
        {
          case RulerDragSize::N1 :
            {
                nMaxRight = mpBorders[nIdx].nPos +
                    mpBorders[nIdx].nWidth + lNullPix;

                if(0 == nIdx)
                    nMaxLeft = lNullPix;
                else
                    nMaxLeft = mpBorders[nIdx - 1].nPos + mpBorders[nIdx - 1].nWidth + lNullPix;
                if (mxColumnItem && nIdx == mxColumnItem->GetActColumn())
                {
                    if(bRTL)
                    {
                        nMaxLeft += mpBorders[nIdx].nPos +
                            GetRightIndent() - std::max(GetFirstLineIndent(),
                                                   GetLeftIndent());
                    }
                    else
                    {
                        nMaxLeft += mpBorders[nIdx].nPos -
                            GetRightIndent() + std::max(GetFirstLineIndent(),
                                                   GetLeftIndent());
                    }
                    if(0 != nIdx)
                        nMaxLeft -= mpBorders[nIdx-1].nPos +
                            mpBorders[nIdx-1].nWidth;
                }
                nMaxLeft += glMinFrame;
                nMaxLeft += nDragOffset;
                break;
            }
          case RulerDragSize::Move:
            {
                if (mxColumnItem)
                {
                    //nIdx contains the position of the currently moved item
                    //next visible separator on the left
                    sal_uInt16 nLeftCol=GetActLeftColumn(false, nIdx);
                    //next visible separator on the right
                    sal_uInt16 nRightCol=GetActRightColumn(false, nIdx);
                    //next separator on the left - regardless if visible or not
                    sal_uInt16 nActLeftCol=GetActLeftColumn();
                    //next separator on the right - regardless if visible or not
                    sal_uInt16 nActRightCol=GetActRightColumn();
                    if(mxColumnItem->IsTable())
                    {
                        if(nDragType & SvxRulerDragFlags::OBJECT_ACTLINE_ONLY)
                        {
                            //the current row/column should be modified only
                            //then the next/previous visible border position
                            //marks the min/max positions
                            nMaxLeft = nLeftCol == USHRT_MAX ?
                                0 :
                                mpBorders[nLeftCol].nPos;
                            //rows can always be increased without a limit
                            if(mxRulerImpl->bIsTableRows)
                                nMaxRight = mpBorders[nIdx].nMaxPos;
                            else
                                nMaxRight = nRightCol == USHRT_MAX ?
                                    GetMargin2():
                                    mpBorders[nRightCol].nPos;
                            nMaxLeft += lNullPix;
                            nMaxRight += lNullPix;
                        }
                        else
                        {
                            if(SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL & nDragType && !bHorz && mxRulerImpl->bIsTableRows)
                                nMaxLeft = (nIdx + 1) * glMinFrame + lNullPix;
                            else
                                nMaxLeft = mpBorders[nIdx].nMinPos + lNullPix;
                            if((SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL & nDragType) ||
                               (SvxRulerDragFlags::OBJECT_SIZE_LINEAR & nDragType) )
                            {
                                if(mxRulerImpl->bIsTableRows)
                                {
                                    if(bHorz)
                                        nMaxRight = GetRightIndent() + lNullPix -
                                                (mxColumnItem->Count() - nIdx - 1) * glMinFrame;
                                    else
                                        nMaxRight = mpBorders[nIdx].nMaxPos + lNullPix;
                                }
                                else
                                    nMaxRight=lNullPix+CalcPropMaxRight(nIdx);
                            }
                            else
                                nMaxRight = mpBorders[nIdx].nMaxPos + lNullPix;
                        }
                        nMaxLeft += glMinFrame;
                        nMaxRight -= glMinFrame;

                    }
                    else
                    {
                        if(nLeftCol==USHRT_MAX)
                            nMaxLeft=lNullPix;
                        else
                            nMaxLeft = mpBorders[nLeftCol].nPos +
                                mpBorders[nLeftCol].nWidth + lNullPix;

                        if(nActRightCol == nIdx)
                        {
                            if(bRTL)
                            {
                                nMaxLeft += mpBorders[nIdx].nPos +
                                    GetRightIndent() - std::max(GetFirstLineIndent(),
                                                           GetLeftIndent());
                                if(nActLeftCol!=USHRT_MAX)
                                    nMaxLeft -= mpBorders[nActLeftCol].nPos +
                                        mpBorders[nActLeftCol].nWidth;
                            }
                            else
                            {
                                nMaxLeft += mpBorders[nIdx].nPos -
                                    GetRightIndent() + std::max(GetFirstLineIndent(),
                                                           GetLeftIndent());
                                if(nActLeftCol!=USHRT_MAX)
                                    nMaxLeft -= mpBorders[nActLeftCol].nPos +
                                        mpBorders[nActLeftCol].nWidth;
                            }
                        }
                        nMaxLeft += glMinFrame;
                        nMaxLeft += nDragOffset;

                        // nMaxRight
                        // linear / proportional move
                        if((SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL & nDragType) ||
                           (SvxRulerDragFlags::OBJECT_SIZE_LINEAR & nDragType) )
                        {
                            nMaxRight=lNullPix+CalcPropMaxRight(nIdx);
                        }
                        else if(SvxRulerDragFlags::OBJECT_SIZE_LINEAR & nDragType)
                        {
                            nMaxRight = lNullPix + GetMargin2() - GetMargin1() +
                                (mpBorders.size() - nIdx - 1) * glMinFrame;
                        }
                        else
                        {
                            if(nRightCol==USHRT_MAX)
                            { // last column
                                nMaxRight = GetMargin2() + lNullPix;
                                if(IsActLastColumn())
                                {
                                    if(bRTL)
                                    {
                                        nMaxRight -=
                                            GetMargin2() + GetRightIndent() -
                                                std::max(GetFirstLineIndent(),
                                                    GetLeftIndent());
                                    }
                                    else
                                    {
                                        nMaxRight -=
                                            GetMargin2() - GetRightIndent() +
                                                std::max(GetFirstLineIndent(),
                                                    GetLeftIndent());
                                    }
                                    nMaxRight += mpBorders[nIdx].nPos +
                                        mpBorders[nIdx].nWidth;
                                }
                            }
                            else
                            {
                                nMaxRight = lNullPix + mpBorders[nRightCol].nPos;
                                sal_uInt16 nNotHiddenRightCol =
                                    GetActRightColumn(true, nIdx);

                                if( nActLeftCol == nIdx )
                                {
                                    tools::Long nBorder = nNotHiddenRightCol ==
                                        USHRT_MAX ?
                                        GetMargin2() :
                                        mpBorders[nNotHiddenRightCol].nPos;
                                    if(bRTL)
                                    {
                                        nMaxRight -= nBorder + GetRightIndent() -
                                            std::max(GetFirstLineIndent(),
                                                GetLeftIndent());
                                    }
                                    else
                                    {
                                        nMaxRight -= nBorder - GetRightIndent() +
                                            std::max(GetFirstLineIndent(),
                                                GetLeftIndent());
                                    }
                                    nMaxRight += mpBorders[nIdx].nPos +
                                        mpBorders[nIdx].nWidth;
                                }
                            }
                            nMaxRight -= glMinFrame;
                            nMaxRight -= mpBorders[nIdx].nWidth;
                        }
                    }
                }
                // ObjectItem
                else
                {
                    nMaxLeft = LONG_MIN;
                    nMaxRight = LONG_MAX;
                }
                break;
            }
          case RulerDragSize::N2:
            if (mxColumnItem)
            {
                nMaxLeft = lNullPix + mpBorders[nIdx].nPos;
                if(nIdx == mxColumnItem->Count()-2) { // last column
                    nMaxRight = GetMargin2() + lNullPix;
                    if(mxColumnItem->IsLastAct()) {
                        nMaxRight -=
                            GetMargin2() - GetRightIndent() +
                                std::max(GetFirstLineIndent(),
                                    GetLeftIndent());
                        nMaxRight += mpBorders[nIdx].nPos +
                            mpBorders[nIdx].nWidth;
                    }
                }
                else {
                    nMaxRight = lNullPix + mpBorders[nIdx+1].nPos;
                    if(mxColumnItem->GetActColumn()-1 == nIdx) {
                        nMaxRight -= mpBorders[nIdx+1].nPos  - GetRightIndent() +
                            std::max(GetFirstLineIndent(),
                                GetLeftIndent());
                        nMaxRight += mpBorders[nIdx].nPos +
                            mpBorders[nIdx].nWidth;
                    }
                }
                nMaxRight -= glMinFrame;
                nMaxRight -= mpBorders[nIdx].nWidth;
                break;
            }
        }
        nMaxRight += nDragOffset;
        break;
    }
      case RulerType::Indent:
        {
        const sal_uInt16 nIdx = GetDragAryPos();
        switch(nIdx) {
        case INDENT_FIRST_LINE - INDENT_GAP:
        case INDENT_LEFT_MARGIN - INDENT_GAP:
            {
                if(bRTL)
                {
                    nMaxLeft = lNullPix + GetRightIndent();

                    if(mxColumnItem && !mxColumnItem->IsFirstAct())
                        nMaxLeft += mpBorders[mxColumnItem->GetActColumn()-1].nPos +
                            mpBorders[mxColumnItem->GetActColumn()-1].nWidth;
                    nMaxRight = lNullPix + GetMargin2();

                    // Dragging along
                    if((INDENT_FIRST_LINE - INDENT_GAP) != nIdx &&
                       !(nDragType & SvxRulerDragFlags::OBJECT_LEFT_INDENT_ONLY))
                    {
                        if(GetLeftIndent() > GetFirstLineIndent())
                            nMaxLeft += GetLeftIndent() - GetFirstLineIndent();
                        else
                            nMaxRight -= GetFirstLineIndent() - GetLeftIndent();
                    }
                }
                else
                {
                    nMaxLeft = lNullPix;

                    if(mxColumnItem && !mxColumnItem->IsFirstAct())
                        nMaxLeft += mpBorders[mxColumnItem->GetActColumn()-1].nPos +
                            mpBorders[mxColumnItem->GetActColumn()-1].nWidth;
                    nMaxRight = lNullPix + GetRightIndent() - glMinFrame;

                    // Dragging along
                    if((INDENT_FIRST_LINE - INDENT_GAP) != nIdx &&
                       !(nDragType & SvxRulerDragFlags::OBJECT_LEFT_INDENT_ONLY))
                    {
                        if(GetLeftIndent() > GetFirstLineIndent())
                            nMaxLeft += GetLeftIndent() - GetFirstLineIndent();
                        else
                            nMaxRight -= GetFirstLineIndent() - GetLeftIndent();
                    }
                }
            }
          break;
          case INDENT_RIGHT_MARGIN - INDENT_GAP:
            {
                if(bRTL)
                {
                    nMaxLeft = lNullPix;
                    nMaxRight = lNullPix + std::min(GetFirstLineIndent(), GetLeftIndent()) - glMinFrame;
                    if (mxColumnItem)
                    {
                        sal_uInt16 nRightCol=GetActRightColumn( true );
                        if(!IsActLastColumn( true ))
                            nMaxRight += mpBorders[nRightCol].nPos;
                        else
                            nMaxRight += GetMargin2();
                    }
                    else
                    {
                        nMaxLeft += GetMargin1();
                    }
                    nMaxLeft += glMinFrame;
                }
                else
                {
                    nMaxLeft = lNullPix +
                        std::max(GetFirstLineIndent(), GetLeftIndent());
                    nMaxRight = lNullPix;
                    if (mxColumnItem)
                    {
                        sal_uInt16 nRightCol=GetActRightColumn( true );
                        if(!IsActLastColumn( true ))
                            nMaxRight += mpBorders[nRightCol].nPos;
                        else
                            nMaxRight += GetMargin2();
                    }
                    else
                        nMaxRight += GetMargin2();
                    nMaxLeft += glMinFrame;
                }
            }
            break;
        }
        break;
    }
    case RulerType::Tab:                // Tabs (Modifier)
        /* left = NOf + Max(LAR, EZ)
           right = NOf + RAR */

        if (bRTL)
            nMaxLeft = lNullPix + GetRightIndent();
        else
            nMaxLeft = lNullPix + std::min(GetFirstLineIndent(), GetLeftIndent());

        mxRulerImpl->lMaxRightLogic = GetLogicRightIndent() + lLogicNullOffset;
        nMaxRight = ConvertSizePixel(mxRulerImpl->lMaxRightLogic);
        break;
    default: ; //prevent warning
    }
}

bool SvxRuler::StartDrag()
{
    /*
       Beginning of a drag operation (SV-handler) evaluates modifier and
       calculated values

       [Cross-reference]

       <SvxRuler::EvalModifier()>
       <SvxRuler::CalcMinMax()>
       <SvxRuler::EndDrag()>
    */
    bool bContentProtected = mxRulerImpl->aProtectItem->IsContentProtected();

    if(!bValid)
        return false;

    mxRulerImpl->lLastLMargin = GetMargin1();
    mxRulerImpl->lLastRMargin = GetMargin2();

    bool bOk = true;

    lInitialDragPos = GetDragPos();
    switch(GetDragType())
    {
        case RulerType::Margin1:        // left edge of the surrounding Frame
        case RulerType::Margin2:        // right edge of the surrounding Frame
            if((bHorz && mxLRSpaceItem) || (!bHorz && mxULSpaceItem))
            {
                if (!mxColumnItem)
                    EvalModifier();
                else
                    nDragType = SvxRulerDragFlags::OBJECT;
            }
            else
            {
                bOk = false;
            }
            break;
        case RulerType::Border: // Table, column (Modifier)
            if (mxColumnItem)
            {
                nDragOffset = 0;
                if (!mxColumnItem->IsTable())
                    nDragOffset = GetDragPos() - mpBorders[GetDragAryPos()].nPos;
                EvalModifier();
            }
            else
                nDragOffset = 0;
            break;
        case RulerType::Indent: // Paragraph indents (Modifier)
        {
            if( bContentProtected )
                return false;
            if(INDENT_LEFT_MARGIN == GetDragAryPos() + INDENT_GAP) {  // Left paragraph indent
                mpIndents[0] = mpIndents[INDENT_FIRST_LINE];
                EvalModifier();
            }
            else
            {
                nDragType = SvxRulerDragFlags::OBJECT;
            }
            mpIndents[1] = mpIndents[GetDragAryPos() + INDENT_GAP];
            break;
        }
        case RulerType::Tab: // Tabs (Modifier)
            if( bContentProtected )
                return false;
            EvalModifier();
            mpTabs[0] = mpTabs[GetDragAryPos() + 1];
            mpTabs[0].nStyle |= RULER_STYLE_DONTKNOW;
            break;
        default:
            nDragType = SvxRulerDragFlags::NONE;
    }

    if(bOk)
        CalcMinMax();

    return bOk;
}

void  SvxRuler::Drag()
{
    /* SV-Draghandler */
    if(IsDragCanceled())
    {
        Ruler::Drag();
        return;
    }
    switch(GetDragType()) {
        case RulerType::Margin1: // left edge of the surrounding Frame
            DragMargin1();
            mxRulerImpl->lLastLMargin = GetMargin1();
            break;
        case RulerType::Margin2: // right edge of the surrounding Frame
            DragMargin2();
            mxRulerImpl->lLastRMargin = GetMargin2();
            break;
        case RulerType::Indent: // Paragraph indents
            DragIndents();
            break;
        case RulerType::Border: // Table, columns
            if (mxColumnItem)
                DragBorders();
            else if (mxObjectItem)
                DragObjectBorder();
            break;
        case RulerType::Tab: // Tabs
            DragTabs();
            break;
        default:
            break; //prevent warning
    }
    Ruler::Drag();
}

void SvxRuler::EndDrag()
{
    /*
       SV-handler; is called when ending the dragging. Triggers the updating of data
       on the application, by calling the respective Apply...() methods to send the
       data to the application.
    */
    const bool bUndo = IsDragCanceled();
    const tools::Long lPos = GetDragPos();
    DrawLine_Impl(lTabPos, 6, bHorz);
    lTabPos = -1;

    if(!bUndo)
    {
        switch(GetDragType())
        {
            case RulerType::Margin1: // upper left edge of the surrounding Frame
            case RulerType::Margin2: // lower right edge of the surrounding Frame
                {
                    if (!mxColumnItem || !mxColumnItem->IsTable())
                        ApplyMargins();

                    if(mxColumnItem &&
                       (mxColumnItem->IsTable() ||
                        (nDragType & SvxRulerDragFlags::OBJECT_SIZE_PROPORTIONAL)))
                        ApplyBorders();

                }
                break;
            case RulerType::Border: // Table, columns
                if(lInitialDragPos != lPos ||
                    (mxRulerImpl->bIsTableRows && bHorz)) //special case - the null offset is changed here
                {
                    if (mxColumnItem)
                    {
                        ApplyBorders();
                        if(bHorz)
                            UpdateTabs();
                    }
                    else if (mxObjectItem)
                        ApplyObject();
                }
                break;
            case RulerType::Indent: // Paragraph indents
                if(lInitialDragPos != lPos)
                    ApplyIndents();
                SetIndents(INDENT_COUNT, mpIndents.data() + INDENT_GAP);
                break;
            case RulerType::Tab: // Tabs
                {
                    ApplyTabs();
                    mpTabs[GetDragAryPos()].nStyle &= ~RULER_STYLE_INVISIBLE;
                    SetTabs(nTabCount, mpTabs.data() + TAB_GAP);
                }
                break;
            default:
                break; //prevent warning
        }
    }
    nDragType = SvxRulerDragFlags::NONE;

    mbCoarseSnapping = false;
    mbSnapping = true;

    Ruler::EndDrag();
    if(bUndo)
    {
        for(sal_uInt16 i = 0; i < mxRulerImpl->nControllerItems; i++)
        {
            pCtrlItems[i]->ClearCache();
            pCtrlItems[i]->GetBindings().Invalidate(pCtrlItems[i]->GetId());
        }
    }
}

void SvxRuler::ExtraDown()
{
    /* Override SV method, sets the new type for the Default tab. */

    // Switch Tab Type
    if(mxTabStopItem &&
        (nFlags & SvxRulerSupportFlags::TABS) == SvxRulerSupportFlags::TABS)
    {
        ++nDefTabType;
        if(RULER_TAB_DEFAULT == nDefTabType)
            nDefTabType = RULER_TAB_LEFT;
        SetExtraType(RulerExtra::Tab, nDefTabType);
    }
    Ruler::ExtraDown();
}

void SvxRuler::Notify(SfxBroadcaster&, const SfxHint& rHint)
{
    /*
       Report through the bindings that the status update is completed. The ruler
       updates its appearance and gets registered again in the bindings.
    */

    // start update
    if (bActive && rHint.GetId() == SfxHintId::UpdateDone)
    {
        Update();
        EndListening(*pBindings);
        bValid = true;
        bListening = false;
    }
}

void SvxRuler::MenuSelect(std::u16string_view ident)
{
    if (ident.empty())
        return;
    /* Handler of the context menus for switching the unit of measurement */
    SetUnit(vcl::EnglishStringToMetric(ident));
}

void SvxRuler::TabMenuSelect(std::u16string_view rIdent)
{
    if (rIdent.empty())
        return;
    sal_Int32 nId = o3tl::toInt32(rIdent);
    /* Handler of the tab menu for setting the type */
    if (mxTabStopItem && mxTabStopItem->Count() > mxRulerImpl->nIdx)
    {
        SvxTabStop aTabStop = mxTabStopItem->At(mxRulerImpl->nIdx);
        aTabStop.GetAdjustment() = ToAttrTab_Impl(nId - 1);
        mxTabStopItem->Remove(mxRulerImpl->nIdx);
        mxTabStopItem->Insert(aTabStop);
        sal_uInt16 nTabStopId = bHorz ? SID_ATTR_TABSTOP : SID_ATTR_TABSTOP_VERTICAL;
        pBindings->GetDispatcher()->ExecuteList(nTabStopId,
                SfxCallMode::RECORD, { mxTabStopItem.get() });
        UpdateTabs();
        mxRulerImpl->nIdx = 0;
    }
}

const TranslateId RID_SVXSTR_RULER_TAB[] =
{
    RID_SVXSTR_RULER_TAB_LEFT,
    RID_SVXSTR_RULER_TAB_RIGHT,
    RID_SVXSTR_RULER_TAB_CENTER,
    RID_SVXSTR_RULER_TAB_DECIMAL
};

void SvxRuler::Command( const CommandEvent& rCommandEvent )
{
    /* Mouse context menu for switching the unit of measurement */
    if ( CommandEventId::ContextMenu == rCommandEvent.GetCommand() )
    {
        CancelDrag();

        tools::Rectangle aRect(rCommandEvent.GetMousePosPixel(), Size(1, 1));
        weld::Window* pPopupParent = weld::GetPopupParent(*this, aRect);
        std::unique_ptr<weld::Builder> xBuilder(Application::CreateBuilder(pPopupParent, u"svx/ui/rulermenu.ui"_ustr));
        std::unique_ptr<weld::Menu> xMenu(xBuilder->weld_menu(u"menu"_ustr));

        bool bRTL = mxRulerImpl->pTextRTLItem && mxRulerImpl->pTextRTLItem->GetValue();
        if ( !mpTabs.empty() &&
             RulerType::Tab ==
             GetRulerType( rCommandEvent.GetMousePosPixel(), &mxRulerImpl->nIdx ) &&
             mpTabs[mxRulerImpl->nIdx + TAB_GAP].nStyle < RULER_TAB_DEFAULT )
        {
            xMenu->clear();

            const Size aSz(ruler_tab_svx.width + 2, ruler_tab_svx.height + 2);
            const Point aPt(aSz.Width() / 2, aSz.Height() / 2);

            for ( sal_uInt16 i = RULER_TAB_LEFT; i < RULER_TAB_DEFAULT; ++i )
            {
                ScopedVclPtr<VirtualDevice> xDev(pPopupParent->create_virtual_device());
                xDev->SetOutputSize(aSz);

                sal_uInt16 nStyle = bRTL ? i|RULER_TAB_RTL : i;
                nStyle |= static_cast<sal_uInt16>(bHorz ? WB_HORZ : WB_VERT);

                Color aFillColor(xDev->GetSettings().GetStyleSettings().GetShadowColor());
                DrawTab(*xDev, aFillColor, aPt, nStyle);

                OUString sId(OUString::number(i + 1));
                xMenu->insert(-1, sId, SvxResId(RID_SVXSTR_RULER_TAB[i]),
                              nullptr, xDev.get(), nullptr, TRISTATE_TRUE);
                xMenu->set_active(sId, i == mpTabs[mxRulerImpl->nIdx + TAB_GAP].nStyle);
            }
            TabMenuSelect(xMenu->popup_at_rect(pPopupParent, aRect));
        }
        else
        {
            FieldUnit eUnit = GetUnit();
            const int nCount = xMenu->n_children();

            bool bReduceMetric = bool(nFlags & SvxRulerSupportFlags::REDUCED_METRIC);
            for ( sal_uInt16 i = nCount; i; --i )
            {
                OUString sIdent = xMenu->get_id(i - 1);
                FieldUnit eMenuUnit = vcl::EnglishStringToMetric(sIdent);
                xMenu->set_active(sIdent, eMenuUnit == eUnit);
                if( bReduceMetric )
                {
                    if (eMenuUnit == FieldUnit::M    ||
                        eMenuUnit == FieldUnit::KM   ||
                        eMenuUnit == FieldUnit::FOOT ||
                        eMenuUnit == FieldUnit::MILE)
                    {
                        xMenu->remove(sIdent);
                    }
                    else if (( eMenuUnit == FieldUnit::CHAR ) && !bHorz )
                    {
                        xMenu->remove(sIdent);
                    }
                    else if (( eMenuUnit == FieldUnit::LINE ) && bHorz )
                    {
                        xMenu->remove(sIdent);
                    }
                }
            }
            MenuSelect(xMenu->popup_at_rect(pPopupParent, aRect));
        }
    }
    else
    {
        Ruler::Command( rCommandEvent );
    }
}

sal_uInt16 SvxRuler::GetActRightColumn(
                        bool bForceDontConsiderHidden,
                        sal_uInt16 nAct ) const
{
    if( nAct == USHRT_MAX )
        nAct = mxColumnItem->GetActColumn();
    else
        nAct++; //To be able to pass on the ActDrag

    bool bConsiderHidden = !bForceDontConsiderHidden &&
                               !(nDragType & SvxRulerDragFlags::OBJECT_ACTLINE_ONLY);

    while( nAct < mxColumnItem->Count() - 1 )
    {
        if (mxColumnItem->At(nAct).bVisible || bConsiderHidden)
            return nAct;
        else
            nAct++;
    }
    return USHRT_MAX;
}

sal_uInt16 SvxRuler::GetActLeftColumn(
                        bool bForceDontConsiderHidden,
                        sal_uInt16 nAct ) const
{
    if(nAct == USHRT_MAX)
        nAct = mxColumnItem->GetActColumn();

    sal_uInt16 nLeftOffset = 1;

    bool bConsiderHidden = !bForceDontConsiderHidden &&
                               !(nDragType & SvxRulerDragFlags::OBJECT_ACTLINE_ONLY);

    while(nAct >= nLeftOffset)
    {
        if (mxColumnItem->At(nAct - nLeftOffset).bVisible || bConsiderHidden)
            return nAct - nLeftOffset;
        else
            nLeftOffset++;
    }
    return USHRT_MAX;
}

bool SvxRuler::IsActLastColumn(
                        bool bForceDontConsiderHidden,
                        sal_uInt16 nAct) const
{
    return GetActRightColumn(bForceDontConsiderHidden, nAct) == USHRT_MAX;
}

bool SvxRuler::IsActFirstColumn(
                        bool bForceDontConsiderHidden,
                        sal_uInt16 nAct) const
{
    return GetActLeftColumn(bForceDontConsiderHidden, nAct) == USHRT_MAX;
}

tools::Long SvxRuler::CalcPropMaxRight(sal_uInt16 nCol) const
{

    if(!(nDragType & SvxRulerDragFlags::OBJECT_SIZE_LINEAR))
    {
        // Remove the minimum width for all affected columns
        // starting from the right edge
        tools::Long _nMaxRight = GetMargin2() - GetMargin1();

        tools::Long lFences = 0;
        tools::Long lMinSpace = USHRT_MAX;
        tools::Long lOldPos;
        tools::Long lColumns = 0;

        sal_uInt16 nStart;
        if(!mxColumnItem->IsTable())
        {
            if(nCol == USHRT_MAX)
            {
                lOldPos = GetMargin1();
                nStart = 0;
            }
            else
            {
                lOldPos = mpBorders[nCol].nPos + mpBorders[nCol].nWidth;
                nStart = nCol + 1;
                lFences = mpBorders[nCol].nWidth;
            }

            for(size_t i = nStart; i < mpBorders.size() - 1; ++i)
            {
                tools::Long lWidth = mpBorders[i].nPos - lOldPos;
                lColumns += lWidth;
                if(lWidth < lMinSpace)
                    lMinSpace = lWidth;
                lOldPos = mpBorders[i].nPos + mpBorders[i].nWidth;
                lFences += mpBorders[i].nWidth;
            }
            tools::Long lWidth = GetMargin2() - lOldPos;
            lColumns += lWidth;
            if(lWidth < lMinSpace)
                lMinSpace = lWidth;
        }
        else
        {
            sal_uInt16 nActCol;
            if(nCol == USHRT_MAX) //CalcMinMax for LeftMargin
            {
                lOldPos = GetMargin1();
            }
            else
            {
                lOldPos = mpBorders[nCol].nPos;
            }
            lColumns = GetMargin2()-lOldPos;
            nActCol = nCol;
            lFences = 0;
            while(nActCol < mpBorders.size() || nActCol == USHRT_MAX)
            {
                sal_uInt16 nRight;
                if(nActCol == USHRT_MAX)
                {
                    nRight = 0;
                    while (!(*mxColumnItem)[nRight].bVisible)
                    {
                        nRight++;
                    }
                }
                else
                {
                    nRight = GetActRightColumn(false, nActCol);
                }

                tools::Long lWidth;
                if(nRight != USHRT_MAX)
                {
                    lWidth = mpBorders[nRight].nPos - lOldPos;
                    lOldPos = mpBorders[nRight].nPos;
                }
                else
                {
                    lWidth=GetMargin2() - lOldPos;
                }
                nActCol = nRight;
                if(lWidth < lMinSpace)
                    lMinSpace = lWidth;
                if(nActCol == USHRT_MAX)
                    break;
            }
        }

        _nMaxRight -= static_cast<tools::Long>(lFences + glMinFrame / static_cast<float>(lMinSpace) * lColumns);
        return _nMaxRight;
    }
    else
    {
        if(mxColumnItem->IsTable())
        {
            sal_uInt16 nVisCols = 0;
            for(size_t i = GetActRightColumn(false, nCol); i < mpBorders.size();)
            {
                if ((*mxColumnItem)[i].bVisible)
                    nVisCols++;
                i = GetActRightColumn(false, i);
            }
            return GetMargin2() - GetMargin1() - (nVisCols + 1) * glMinFrame;
        }
        else
        {
            tools::Long lWidth = 0;
            for(size_t i = nCol; i < mpBorders.size() - 1; i++)
            {
                lWidth += glMinFrame + mpBorders[i].nWidth;
            }
            return GetMargin2() - GetMargin1() - lWidth;
        }
    }
}

// Tab stops relative to indent (#i24363#)
void SvxRuler::SetTabsRelativeToIndent( bool bRel )
{
    mxRulerImpl->bIsTabsRelativeToIndent = bRel;
}

void SvxRuler::SetValues(RulerChangeType type, tools::Long diffValue)
{
    if (diffValue == 0)
        return;

    if (type == RulerChangeType::MARGIN1)
        AdjustMargin1(diffValue);
    else if (type == RulerChangeType::MARGIN2)
        SetMargin2( GetMargin2() - diffValue);
    ApplyMargins();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
