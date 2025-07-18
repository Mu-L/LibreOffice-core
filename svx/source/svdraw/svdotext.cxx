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


#include <comphelper/string.hxx>
#include <svl/stritem.hxx>
#include <svx/svdotext.hxx>
#include <svx/svdpage.hxx>
#include <svx/svdoutl.hxx>
#include <svx/svdmodel.hxx>
#include <svx/dialmgr.hxx>
#include <svx/strings.hrc>
#include <editeng/writingmodeitem.hxx>
#include <svx/sdtfchim.hxx>
#include <editeng/editdata.hxx>
#include <editeng/editstat.hxx>
#include <editeng/outlobj.hxx>
#include <editeng/editobj.hxx>
#include <editeng/outliner.hxx>
#include <textchain.hxx>
#include <textchainflow.hxx>
#include <tools/helpers.hxx>
#include <svx/sderitm.hxx>
#include <svx/sdooitm.hxx>
#include <svx/sdshitm.hxx>
#include <svx/sdtagitm.hxx>
#include <svx/sdtfsitm.hxx>
#include <svx/sdtmfitm.hxx>
#include <svx/xtextit0.hxx>
#include <sdr/properties/textproperties.hxx>
#include <sdr/contact/viewcontactoftextobj.hxx>
#include <basegfx/tuple/b2dtuple.hxx>
#include <basegfx/matrix/b2dhommatrix.hxx>
#include <vcl/gdimtf.hxx>
#include <vcl/virdev.hxx>
#include <basegfx/matrix/b2dhommatrixtools.hxx>
#include <sal/log.hxx>
#include <o3tl/unit_conversion.hxx>
#include <o3tl/temporary.hxx>
#include <comphelper/configuration.hxx>
#include <editeng/eeitem.hxx>
#include <editeng/fhgtitem.hxx>

using namespace com::sun::star;

// BaseProperties section
std::unique_ptr<sdr::properties::BaseProperties> SdrTextObj::CreateObjectSpecificProperties()
{
    return std::make_unique<sdr::properties::TextProperties>(*this);
}

// DrawContact section
std::unique_ptr<sdr::contact::ViewContact> SdrTextObj::CreateObjectSpecificViewContact()
{
    return std::make_unique<sdr::contact::ViewContactOfTextObj>(*this);
}

SdrTextObj::SdrTextObj(SdrModel& rSdrModel, const tools::Rectangle& rRectangle, std::optional<SdrObjKind> oeTextKind)
    : SdrAttrObj(rSdrModel)
    , mpEditingOutliner(nullptr)
    , meTextKind(oeTextKind ? *oeTextKind : SdrObjKind::Text)
    , maTextEditOffset(Point())
    , mbTextFrame(false)
    , mbNoShear(false)
    , mbTextSizeDirty(false)
    , mbInEditMode(false)
    , mbDisableAutoWidthOnDragging(false)
    , mbTextAnimationAllowed(true)
    , mbInDownScale(false)
{
    if (!rRectangle.IsEmpty())
    {
        tools::Rectangle aRectangle(rRectangle);
        ImpJustifyRect(aRectangle);
        setRectangle(aRectangle);
    }

    if (oeTextKind)
    {
        mbTextFrame = true;
        mbNoShear = true;
    }

    mbSupportTextIndentingOnLineWidthChange = true;
}

SdrTextObj::SdrTextObj(SdrModel& rSdrModel, SdrTextObj const & rSource)
    : SdrAttrObj(rSdrModel, rSource)
    , mpEditingOutliner(nullptr)
    , meTextKind(rSource.meTextKind)
    , maTextEditOffset(Point(0, 0))
    , mbTextFrame(rSource.mbTextFrame)
    , mbNoShear(rSource.mbNoShear)
    , mbTextSizeDirty(rSource.mbTextSizeDirty)
    , mbInEditMode(false)
    , mbDisableAutoWidthOnDragging(rSource.mbDisableAutoWidthOnDragging)
    , mbTextAnimationAllowed(true)
    , mbInDownScale(false)
{
    // #i25616#
    mbSupportTextIndentingOnLineWidthChange = true;

    maRectangle = rSource.maRectangle;
    maGeo = rSource.maGeo;
    maTextSize = rSource.maTextSize;

    // Not all of the necessary parameters were copied yet.
    SdrText* pText = getActiveText();

    if( pText && rSource.HasText() )
    {
        // before pNewOutlinerParaObject was created the same, but
        // set at mpText (outside this scope), but mpText might be
        // empty (this operator== seems not prepared for MultiText
        // objects). In the current form it makes only sense to
        // create locally and use locally on a known existing SdrText
        const Outliner* pEO = rSource.mpEditingOutliner;
        std::optional<OutlinerParaObject> pNewOutlinerParaObject;

        if (pEO!=nullptr)
        {
            pNewOutlinerParaObject = pEO->CreateParaObject();
        }
        else if (nullptr != rSource.getActiveText()->GetOutlinerParaObject())
        {
            pNewOutlinerParaObject = *rSource.getActiveText()->GetOutlinerParaObject();
        }

        pText->SetOutlinerParaObject( std::move(pNewOutlinerParaObject) );
    }

    ImpSetTextStyleSheetListeners();
}

SdrTextObj::~SdrTextObj()
{
    mxText.clear();
    ImpDeregisterLink();
}

void SdrTextObj::FitFrameToTextSize()
{
    ImpJustifyRect(maRectangle);

    SdrText* pText = getActiveText();
    if(pText==nullptr || !pText->GetOutlinerParaObject())
        return;

    SdrOutliner& rOutliner=ImpGetDrawOutliner();
    rOutliner.SetPaperSize(Size(getRectangle().Right() - getRectangle().Left(), getRectangle().Bottom() - getRectangle().Top()));
    rOutliner.SetUpdateLayout(true);
    rOutliner.SetText(*pText->GetOutlinerParaObject());
    Size aNewSize(rOutliner.CalcTextSize());
    rOutliner.Clear();
    aNewSize.AdjustWidth( 1 ); // because of possible rounding errors
    aNewSize.AdjustWidth(GetTextLeftDistance()+GetTextRightDistance() );
    aNewSize.AdjustHeight(GetTextUpperDistance()+GetTextLowerDistance() );
    tools::Rectangle aNewRect(getRectangle());
    aNewRect.SetSize(aNewSize);
    ImpJustifyRect(aNewRect);

    if (aNewRect != getRectangle())
        SetLogicRect(aNewRect);
}

void SdrTextObj::NbcSetText(const OUString& rStr)
{
    SdrOutliner& rOutliner=ImpGetDrawOutliner();
    rOutliner.SetStyleSheet( 0, GetStyleSheet());
    rOutliner.SetText(rStr,rOutliner.GetParagraph( 0 ));
    std::optional<OutlinerParaObject> pNewText=rOutliner.CreateParaObject();
    NbcSetOutlinerParaObject(std::move(pNewText));
    mbTextSizeDirty=true;
}

void SdrTextObj::SetText(const OUString& rStr)
{
    tools::Rectangle aBoundRect0; if (m_pUserCall!=nullptr) aBoundRect0=GetLastBoundRect();
    NbcSetText(rStr);
    SetChanged();
    BroadcastObjectChange();
    SendUserCall(SdrUserCallType::Resize,aBoundRect0);
}

void SdrTextObj::NbcSetText(SvStream& rInput, const OUString& rBaseURL, EETextFormat eFormat)
{
    SdrOutliner& rOutliner=ImpGetDrawOutliner();
    rOutliner.SetStyleSheet( 0, GetStyleSheet());
    rOutliner.Read(rInput,rBaseURL,eFormat);
    std::optional<OutlinerParaObject> pNewText=rOutliner.CreateParaObject();
    rOutliner.SetUpdateLayout(true);
    Size aSize(rOutliner.CalcTextSize());
    rOutliner.Clear();
    NbcSetOutlinerParaObject(std::move(pNewText));
    maTextSize=aSize;
    mbTextSizeDirty=false;
}

void SdrTextObj::SetText(SvStream& rInput, const OUString& rBaseURL, EETextFormat eFormat)
{
    tools::Rectangle aBoundRect0; if (m_pUserCall!=nullptr) aBoundRect0=GetLastBoundRect();
    NbcSetText(rInput,rBaseURL,eFormat);
    SetChanged();
    BroadcastObjectChange();
    SendUserCall(SdrUserCallType::Resize,aBoundRect0);
}

const Size& SdrTextObj::GetTextSize() const
{
    if (mbTextSizeDirty)
    {
        Size aSiz;
        SdrText* pText = getActiveText();
        if( pText && pText->GetOutlinerParaObject ())
        {
            SdrOutliner& rOutliner=ImpGetDrawOutliner();
            rOutliner.SetText(*pText->GetOutlinerParaObject());
            rOutliner.SetUpdateLayout(true);
            aSiz=rOutliner.CalcTextSize();
            rOutliner.Clear();
        }
        // casting to nonconst twice
        const_cast<SdrTextObj*>(this)->maTextSize = aSiz;
        const_cast<SdrTextObj*>(this)->mbTextSizeDirty = false;
    }
    return maTextSize;
}

bool SdrTextObj::IsAutoGrowHeight() const
{
    if(!mbTextFrame)
        return false; // AutoGrow only together with TextFrames

    const SfxItemSet& rSet = GetObjectItemSet();
    bool bRet = rSet.Get(SDRATTR_TEXT_AUTOGROWHEIGHT).GetValue();

    if(bRet)
    {
        SdrTextAniKind eAniKind = rSet.Get(SDRATTR_TEXT_ANIKIND).GetValue();

        if(eAniKind == SdrTextAniKind::Scroll || eAniKind == SdrTextAniKind::Alternate || eAniKind == SdrTextAniKind::Slide)
        {
            SdrTextAniDirection eDirection = rSet.Get(SDRATTR_TEXT_ANIDIRECTION).GetValue();

            if(eDirection == SdrTextAniDirection::Up || eDirection == SdrTextAniDirection::Down)
            {
                bRet = false;
            }
        }
    }
    return bRet;
}

bool SdrTextObj::IsAutoGrowWidth() const
{
    if (!mbTextFrame)
        return false; // AutoGrow only together with TextFrames

    const SfxItemSet& rSet = GetObjectItemSet();
    bool bRet = rSet.Get(SDRATTR_TEXT_AUTOGROWWIDTH).GetValue();

    bool bInEditMOde = IsInEditMode();

    if(!bInEditMOde && bRet)
    {
        SdrTextAniKind eAniKind = rSet.Get(SDRATTR_TEXT_ANIKIND).GetValue();

        if(eAniKind == SdrTextAniKind::Scroll || eAniKind == SdrTextAniKind::Alternate || eAniKind == SdrTextAniKind::Slide)
        {
            SdrTextAniDirection eDirection = rSet.Get(SDRATTR_TEXT_ANIDIRECTION).GetValue();

            if(eDirection == SdrTextAniDirection::Left || eDirection == SdrTextAniDirection::Right)
            {
                bRet = false;
            }
        }
    }
    return bRet;
}

SdrTextHorzAdjust SdrTextObj::GetTextHorizontalAdjust() const
{
    return GetTextHorizontalAdjust(GetObjectItemSet());
}

SdrTextHorzAdjust SdrTextObj::GetTextHorizontalAdjust(const SfxItemSet& rSet) const
{
    if(IsContourTextFrame())
        return SDRTEXTHORZADJUST_BLOCK;

    SdrTextHorzAdjust eRet = rSet.Get(SDRATTR_TEXT_HORZADJUST).GetValue();

    bool bInEditMode = IsInEditMode();

    if(!bInEditMode && eRet == SDRTEXTHORZADJUST_BLOCK)
    {
        SdrTextAniKind eAniKind = rSet.Get(SDRATTR_TEXT_ANIKIND).GetValue();

        if(eAniKind == SdrTextAniKind::Scroll || eAniKind == SdrTextAniKind::Alternate || eAniKind == SdrTextAniKind::Slide)
        {
            SdrTextAniDirection eDirection = rSet.Get(SDRATTR_TEXT_ANIDIRECTION).GetValue();

            if(eDirection == SdrTextAniDirection::Left || eDirection == SdrTextAniDirection::Right)
            {
                eRet = SDRTEXTHORZADJUST_LEFT;
            }
        }
    }

    return eRet;
} // defaults: BLOCK (justify) for text frame, CENTER for captions of drawing objects

SdrTextVertAdjust SdrTextObj::GetTextVerticalAdjust() const
{
    return GetTextVerticalAdjust(GetObjectItemSet());
}

SdrTextVertAdjust SdrTextObj::GetTextVerticalAdjust(const SfxItemSet& rSet) const
{
    if(IsContourTextFrame())
        return SDRTEXTVERTADJUST_TOP;

    // Take care for vertical text animation here
    SdrTextVertAdjust eRet = rSet.Get(SDRATTR_TEXT_VERTADJUST).GetValue();
    bool bInEditMode = IsInEditMode();

    // Take care for vertical text animation here
    if(!bInEditMode && eRet == SDRTEXTVERTADJUST_BLOCK)
    {
        SdrTextAniKind eAniKind = rSet.Get(SDRATTR_TEXT_ANIKIND).GetValue();

        if(eAniKind == SdrTextAniKind::Scroll || eAniKind == SdrTextAniKind::Alternate || eAniKind == SdrTextAniKind::Slide)
        {
            SdrTextAniDirection eDirection = rSet.Get(SDRATTR_TEXT_ANIDIRECTION).GetValue();

            if(eDirection == SdrTextAniDirection::Left || eDirection == SdrTextAniDirection::Right)
            {
                eRet = SDRTEXTVERTADJUST_TOP;
            }
        }
    }

    return eRet;
} // defaults: TOP for text frame, CENTER for captions of drawing objects

void SdrTextObj::ImpJustifyRect(tools::Rectangle& rRect)
{
    if (!rRect.IsEmpty()) {
        rRect.Normalize();
        if (rRect.Left()==rRect.Right()) rRect.AdjustRight( 1 );
        if (rRect.Top()==rRect.Bottom()) rRect.AdjustBottom( 1 );
    }
}

void SdrTextObj::ImpCheckShear()
{
    if (mbNoShear && maGeo.m_nShearAngle)
    {
        maGeo.m_nShearAngle = 0_deg100;
        maGeo.mfTanShearAngle = 0;
    }
}

void SdrTextObj::TakeObjInfo(SdrObjTransformInfoRec& rInfo) const
{
    bool bNoTextFrame=!IsTextFrame();
    rInfo.bResizeFreeAllowed=bNoTextFrame || ((maGeo.m_nRotationAngle.get() % 9000) == 0);
    rInfo.bResizePropAllowed=true;
    rInfo.bRotateFreeAllowed=true;
    rInfo.bRotate90Allowed  =true;
    rInfo.bMirrorFreeAllowed=bNoTextFrame;
    rInfo.bMirror45Allowed  =bNoTextFrame;
    rInfo.bMirror90Allowed  =bNoTextFrame;

    // allow transparency
    rInfo.bTransparenceAllowed = true;

    rInfo.bShearAllowed     =bNoTextFrame;
    rInfo.bEdgeRadiusAllowed=true;
    bool bCanConv=ImpCanConvTextToCurve();
    rInfo.bCanConvToPath    =bCanConv;
    rInfo.bCanConvToPoly    =bCanConv;
    rInfo.bCanConvToPathLineToArea=bCanConv;
    rInfo.bCanConvToPolyLineToArea=bCanConv;
    rInfo.bCanConvToContour = (rInfo.bCanConvToPoly || LineGeometryUsageIsNecessary());
}

SdrObjKind SdrTextObj::GetObjIdentifier() const
{
    return meTextKind;
}

bool SdrTextObj::HasTextImpl( SdrOutliner const * pOutliner )
{
    bool bRet=false;
    if(pOutliner)
    {
        Paragraph* p1stPara=pOutliner->GetParagraph( 0 );
        sal_Int32 nParaCount=pOutliner->GetParagraphCount();
        if(p1stPara==nullptr)
            nParaCount=0;

        if(nParaCount==1)
        {
            // if it is only one paragraph, check if that paragraph is empty
            if( pOutliner->GetText(p1stPara).isEmpty() )
                nParaCount = 0;
        }

        bRet= nParaCount!=0;
    }
    return bRet;
}

void SdrTextObj::handlePageChange(SdrPage* pOldPage, SdrPage* pNewPage)
{
    const bool bRemove(pNewPage == nullptr && pOldPage != nullptr);
    const bool bInsert(pNewPage != nullptr && pOldPage == nullptr);
    const bool bLinked(IsLinkedText());

    if (bLinked && bRemove)
    {
        ImpDeregisterLink();
    }

    // call parent
    SdrAttrObj::handlePageChange(pOldPage, pNewPage);

    if (bLinked && bInsert)
    {
        ImpRegisterLink();
    }
}

void SdrTextObj::NbcSetEckenradius(tools::Long nRad)
{
    SetObjectItem(makeSdrEckenradiusItem(nRad));
}

// #115391# This implementation is based on the object size (aRect) and the
// states of IsAutoGrowWidth/Height to correctly set TextMinFrameWidth/Height
void SdrTextObj::AdaptTextMinSize()
{
    if (!mbTextFrame)
        // Only do this for text frame.
        return;

    if (getSdrModelFromSdrObject().IsPasteResize())
        // Don't do this during paste resize.
        return;

    const bool bW = IsAutoGrowWidth();
    const bool bH = IsAutoGrowHeight();

    if (!bW && !bH)
        // No auto grow requested.  Bail out.
        return;

    SfxItemSet aSet(SfxItemSet::makeFixedSfxItemSet<SDRATTR_TEXT_MINFRAMEHEIGHT, SDRATTR_TEXT_AUTOGROWHEIGHT,
                                                    // contains SDRATTR_TEXT_MAXFRAMEWIDTH
                                                    SDRATTR_TEXT_MINFRAMEWIDTH, SDRATTR_TEXT_AUTOGROWWIDTH>(*GetObjectItemSet().GetPool()));

    if(bW)
    {
        // Set minimum width.
        const tools::Long nDist = GetTextLeftDistance() + GetTextRightDistance();
        const tools::Long nW = std::max<tools::Long>(0, getRectangle().GetWidth() - 1 - nDist); // text width without margins

        aSet.Put(makeSdrTextMinFrameWidthItem(nW));

        if(!IsVerticalWriting() && mbDisableAutoWidthOnDragging)
        {
            mbDisableAutoWidthOnDragging = true;
            aSet.Put(makeSdrTextAutoGrowWidthItem(false));
        }
    }

    if(bH)
    {
        // Set Minimum height.
        const tools::Long nDist = GetTextUpperDistance() + GetTextLowerDistance();
        const tools::Long nH = std::max<tools::Long>(0, getRectangle().GetHeight() - 1 - nDist); // text height without margins

        aSet.Put(makeSdrTextMinFrameHeightItem(nH));

        if(IsVerticalWriting() && mbDisableAutoWidthOnDragging)
        {
            mbDisableAutoWidthOnDragging = false;
            aSet.Put(makeSdrTextAutoGrowHeightItem(false));
        }
    }

    SetObjectItemSet(aSet);
}

void SdrTextObj::ImpSetContourPolygon( SdrOutliner& rOutliner, tools::Rectangle const & rAnchorRect, bool bLineWidth ) const
{
    basegfx::B2DPolyPolygon aXorPolyPolygon(TakeXorPoly());
    std::optional<basegfx::B2DPolyPolygon> pContourPolyPolygon;
    basegfx::B2DHomMatrix aMatrix(basegfx::utils::createTranslateB2DHomMatrix(
        -rAnchorRect.Left(), -rAnchorRect.Top()));

    if(maGeo.m_nRotationAngle)
    {
        // Unrotate!
        aMatrix.rotate(-toRadians(maGeo.m_nRotationAngle));
    }

    aXorPolyPolygon.transform(aMatrix);

    if( bLineWidth )
    {
        // Take line width into account.
        // When doing the hit test, avoid this. (Performance!)
        pContourPolyPolygon.emplace();

        // test if shadow needs to be avoided for TakeContour()
        const SfxItemSet& rSet = GetObjectItemSet();
        bool bShadowOn = rSet.Get(SDRATTR_SHADOW).GetValue();

        // #i33696#
        // Remember TextObject currently set at the DrawOutliner, it WILL be
        // replaced during calculating the outline since it uses an own paint
        // and that one uses the DrawOutliner, too.
        const SdrTextObj* pLastTextObject = rOutliner.GetTextObj();

        if(bShadowOn)
        {
            // force shadow off
            rtl::Reference<SdrTextObj> pCopy = SdrObject::Clone(*this, getSdrModelFromSdrObject());
            pCopy->SetMergedItem(makeSdrShadowItem(false));
            *pContourPolyPolygon = pCopy->TakeContour();
        }
        else
        {
            *pContourPolyPolygon = TakeContour();
        }

        // #i33696#
        // restore remembered text object
        if(pLastTextObject != rOutliner.GetTextObj())
        {
            rOutliner.SetTextObj(pLastTextObject);
        }

        pContourPolyPolygon->transform(aMatrix);
    }

    rOutliner.SetPolygon(aXorPolyPolygon, pContourPolyPolygon ? &*pContourPolyPolygon : nullptr);
}

void SdrTextObj::TakeUnrotatedSnapRect(tools::Rectangle& rRect) const
{
    rRect = getRectangle();
}

// See also: <unnamed>::getTextAnchorRange in svx/source/sdr/primitive2d/sdrdecompositiontools.cxx
void SdrTextObj::AdjustRectToTextDistance(tools::Rectangle& rAnchorRect, double fExtraRot) const
{
    const tools::Long nLeftDist = GetTextLeftDistance();
    const tools::Long nRightDist = GetTextRightDistance();
    const tools::Long nUpperDist = GetTextUpperDistance();
    const tools::Long nLowerDist = GetTextLowerDistance();
    if (!IsVerticalWriting())
    {
        if (fExtraRot == 180.0)
        {
            rAnchorRect.AdjustLeft(nLeftDist);
            rAnchorRect.AdjustTop(-nUpperDist);
            rAnchorRect.AdjustRight(-nRightDist);
            rAnchorRect.AdjustBottom(nLowerDist);
        }
        else
        {
            rAnchorRect.AdjustLeft(nLeftDist);
            rAnchorRect.AdjustTop(nUpperDist);
            rAnchorRect.AdjustRight(-nRightDist);
            rAnchorRect.AdjustBottom(-nLowerDist);
        }
    }
    else if (IsTopToBottom())
    {
        rAnchorRect.AdjustLeft(nLowerDist);
        rAnchorRect.AdjustTop(nLeftDist);
        rAnchorRect.AdjustRight(-nUpperDist);
        rAnchorRect.AdjustBottom(-nRightDist);
    }
    else
    {
        rAnchorRect.AdjustLeft(nUpperDist);
        rAnchorRect.AdjustTop(nRightDist);
        rAnchorRect.AdjustRight(-nLowerDist);
        rAnchorRect.AdjustBottom(-nLeftDist);
    }

    // Since sizes may be bigger than the object bounds it is necessary to
    // justify the rect now.
    ImpJustifyRect(rAnchorRect);
}

void SdrTextObj::TakeTextAnchorRect(tools::Rectangle& rAnchorRect) const
{
    tools::Rectangle aAnkRect(getRectangle()); // the rectangle in which we anchor
    bool bFrame=IsTextFrame();
    if (!bFrame) {
        TakeUnrotatedSnapRect(aAnkRect);
    }
    Point aRotateRef(aAnkRect.TopLeft());
    AdjustRectToTextDistance(aAnkRect);

    if (bFrame) {
        // TODO: Optimize this.
        if (aAnkRect.GetWidth()<2) aAnkRect.SetRight(aAnkRect.Left()+1 ); // minimum size h and v: 2 px
        if (aAnkRect.GetHeight()<2) aAnkRect.SetBottom(aAnkRect.Top()+1 );
    }
    if (maGeo.m_nRotationAngle) {
        Point aTmpPt(aAnkRect.TopLeft());
        RotatePoint(aTmpPt,aRotateRef,maGeo.mfSinRotationAngle,maGeo.mfCosRotationAngle);
        aTmpPt-=aAnkRect.TopLeft();
        aAnkRect.Move(aTmpPt.X(),aTmpPt.Y());
    }
    rAnchorRect=aAnkRect;
}

void SdrTextObj::TakeTextRect( SdrOutliner& rOutliner, tools::Rectangle& rTextRect, bool bNoEditText,
                               tools::Rectangle* pAnchorRect, bool bLineWidth ) const
{
    tools::Rectangle aAnkRect; // the rectangle in which we anchor
    TakeTextAnchorRect(aAnkRect);
    SdrTextVertAdjust eVAdj=GetTextVerticalAdjust();
    SdrTextHorzAdjust eHAdj=GetTextHorizontalAdjust();
    SdrTextAniKind      eAniKind=GetTextAniKind();
    SdrTextAniDirection eAniDirection=GetTextAniDirection();

    bool bFitToSize(IsFitToSize());
    bool bContourFrame=IsContourTextFrame();

    bool bFrame=IsTextFrame();
    EEControlBits nStat0=rOutliner.GetControlWord();
    Size aNullSize;
    if (!bContourFrame)
    {
        rOutliner.SetControlWord(nStat0|EEControlBits::AUTOPAGESIZE);
        rOutliner.SetMinAutoPaperSize(aNullSize);
        rOutliner.SetMaxAutoPaperSize(Size(1000000,1000000));
    }

    if (!bFitToSize && !bContourFrame)
    {
        tools::Long nAnkWdt=aAnkRect.GetWidth();
        tools::Long nAnkHgt=aAnkRect.GetHeight();
        if (bFrame)
        {
            tools::Long nWdt=nAnkWdt;
            tools::Long nHgt=nAnkHgt;

            bool bInEditMode = IsInEditMode();

            if (!bInEditMode && (eAniKind==SdrTextAniKind::Scroll || eAniKind==SdrTextAniKind::Alternate || eAniKind==SdrTextAniKind::Slide))
            {
                // unlimited paper size for ticker text
                if (eAniDirection==SdrTextAniDirection::Left || eAniDirection==SdrTextAniDirection::Right) nWdt=1000000;
                if (eAniDirection==SdrTextAniDirection::Up || eAniDirection==SdrTextAniDirection::Down) nHgt=1000000;
            }

            bool bChainedFrame = IsChainable();
            // Might be required for overflow check working: do limit height to frame if box is chainable.
            if (!bChainedFrame) {
                // #i119885# Do not limit/force height to geometrical frame (vice versa for vertical writing)

                if(IsVerticalWriting())
                {
                    nWdt = 1000000;
                }
                else
                {
                    nHgt = 1000000;
                }
            }

            rOutliner.SetMaxAutoPaperSize(Size(nWdt,nHgt));
        }

        // New try with _BLOCK for hor and ver after completely
        // supporting full width for vertical text.
        if(SDRTEXTHORZADJUST_BLOCK == eHAdj && !IsVerticalWriting())
        {
            rOutliner.SetMinAutoPaperSize(Size(nAnkWdt, 0));
            rOutliner.SetMinColumnWrapHeight(nAnkHgt);
        }

        if(SDRTEXTVERTADJUST_BLOCK == eVAdj && IsVerticalWriting())
        {
            rOutliner.SetMinAutoPaperSize(Size(0, nAnkHgt));
            rOutliner.SetMinColumnWrapHeight(nAnkWdt);
        }
    }

    rOutliner.SetPaperSize(aNullSize);
    if (bContourFrame)
        ImpSetContourPolygon( rOutliner, aAnkRect, bLineWidth );

    // put text into the outliner, if available from the edit outliner
    SdrText* pText = getActiveText();
    OutlinerParaObject* pOutlinerParaObject = pText ? pText->GetOutlinerParaObject() : nullptr;
    std::optional<OutlinerParaObject> pPara;
    if (mpEditingOutliner && !bNoEditText)
        pPara = mpEditingOutliner->CreateParaObject();
    else if (pOutlinerParaObject)
        pPara = *pOutlinerParaObject;

    if (pPara)
    {
        const bool bHitTest(&getSdrModelFromSdrObject().GetHitTestOutliner() == &rOutliner);
        const SdrTextObj* pTestObj = rOutliner.GetTextObj();

        if( !pTestObj || !bHitTest || pTestObj != this ||
            pTestObj->GetOutlinerParaObject() != pOutlinerParaObject )
        {
            if( bHitTest ) // #i33696# take back fix #i27510#
            {
                rOutliner.SetFixedCellHeight(GetMergedItem(SDRATTR_TEXT_USEFIXEDCELLHEIGHT).GetValue());
                rOutliner.SetTextObj( this );
            }

            rOutliner.SetText(*pPara);
        }
    }
    else
    {
        rOutliner.SetTextObj( nullptr );
    }

    rOutliner.SetUpdateLayout(true);
    rOutliner.SetControlWord(nStat0);

    if( pText )
        pText->CheckPortionInfo(rOutliner);

    Point aTextPos(aAnkRect.TopLeft());
    Size aTextSiz(rOutliner.GetPaperSize()); // GetPaperSize() adds a little tolerance, right?

    // For draw objects containing text correct hor/ver alignment if text is bigger
    // than the object itself. Without that correction, the text would always be
    // formatted to the left edge (or top edge when vertical) of the draw object.
    if(!IsTextFrame())
    {
        if(aAnkRect.GetWidth() < aTextSiz.Width() && !IsVerticalWriting())
        {
            // Horizontal case here. Correct only if eHAdj == SDRTEXTHORZADJUST_BLOCK,
            // else the alignment is wanted.
            if(SDRTEXTHORZADJUST_BLOCK == eHAdj)
            {
                eHAdj = SDRTEXTHORZADJUST_CENTER;
            }
        }

        if(aAnkRect.GetHeight() < aTextSiz.Height() && IsVerticalWriting())
        {
            // Vertical case here. Correct only if eHAdj == SDRTEXTVERTADJUST_BLOCK,
            // else the alignment is wanted.
            if(SDRTEXTVERTADJUST_BLOCK == eVAdj)
            {
                eVAdj = SDRTEXTVERTADJUST_CENTER;
            }
        }
    }

    if (eHAdj==SDRTEXTHORZADJUST_CENTER || eHAdj==SDRTEXTHORZADJUST_RIGHT)
    {
        tools::Long nFreeWdt=aAnkRect.GetWidth()-aTextSiz.Width();
        if (eHAdj==SDRTEXTHORZADJUST_CENTER)
            aTextPos.AdjustX(nFreeWdt/2 );
        if (eHAdj==SDRTEXTHORZADJUST_RIGHT)
            aTextPos.AdjustX(nFreeWdt );
    }
    if (eVAdj==SDRTEXTVERTADJUST_CENTER || eVAdj==SDRTEXTVERTADJUST_BOTTOM)
    {
        tools::Long nFreeHgt=aAnkRect.GetHeight()-aTextSiz.Height();
        if (eVAdj==SDRTEXTVERTADJUST_CENTER)
            aTextPos.AdjustY(nFreeHgt/2 );
        if (eVAdj==SDRTEXTVERTADJUST_BOTTOM)
            aTextPos.AdjustY(nFreeHgt );
    }
    if (maGeo.m_nRotationAngle)
        RotatePoint(aTextPos,aAnkRect.TopLeft(),maGeo.mfSinRotationAngle,maGeo.mfCosRotationAngle);

    if (pAnchorRect)
        *pAnchorRect=aAnkRect;

    // rTextRect might not be correct in some cases at ContourFrame
    rTextRect=tools::Rectangle(aTextPos,aTextSiz);
    if (bContourFrame)
        rTextRect=aAnkRect;
}

bool SdrTextObj::CanCreateEditOutlinerParaObject() const
{
    if( HasTextImpl( mpEditingOutliner ) )
    {
        return mpEditingOutliner->GetParagraphCount() > 0;
    }
    return false;
}

std::optional<OutlinerParaObject> SdrTextObj::CreateEditOutlinerParaObject() const
{
    std::optional<OutlinerParaObject> pPara;
    if( HasTextImpl( mpEditingOutliner ) )
    {
        sal_Int32 nParaCount = mpEditingOutliner->GetParagraphCount();
        pPara = mpEditingOutliner->CreateParaObject(0, nParaCount);
    }
    return pPara;
}

void SdrTextObj::ImpSetCharStretching(SdrOutliner& rOutliner, const Size& rTextSize, const Size& rShapeSize, Fraction& rFitXCorrection)
{
    OutputDevice* pOut = rOutliner.GetRefDevice();
    bool bNoStretching(false);

    if(pOut && pOut->GetOutDevType() == OUTDEV_PRINTER)
    {
        // check whether CharStretching is possible at all
        GDIMetaFile* pMtf = pOut->GetConnectMetaFile();
        OUString aTestString(u'J');

        if(pMtf && (!pMtf->IsRecord() || pMtf->IsPause()))
            pMtf = nullptr;

        if(pMtf)
            pMtf->Pause(true);

        vcl::Font aOriginalFont(pOut->GetFont());
        vcl::Font aTmpFont( OutputDevice::GetDefaultFont( DefaultFontType::SERIF, LANGUAGE_SYSTEM, GetDefaultFontFlags::OnlyOne ) );

        aTmpFont.SetFontSize(Size(0,100));
        pOut->SetFont(aTmpFont);
        Size aSize1(pOut->GetTextWidth(aTestString), pOut->GetTextHeight());
        aTmpFont.SetFontSize(Size(800,100));
        pOut->SetFont(aTmpFont);
        Size aSize2(pOut->GetTextWidth(aTestString), pOut->GetTextHeight());
        pOut->SetFont(aOriginalFont);

        if(pMtf)
            pMtf->Pause(false);

        bNoStretching = (aSize1 == aSize2);

#ifdef _WIN32
        // Windows zooms the font proportionally when using Size(100,500),
        // we don't like that.
        if(aSize2.Height() >= aSize1.Height() * 2)
        {
            bNoStretching = true;
        }
#endif
    }

    rOutliner.setRoundFontSizeToPt(false);

    unsigned nLoopCount=0;
    bool bNoMoreLoop = false;
    tools::Long nXDiff0=0x7FFFFFFF;
    tools::Long nWantWdt=rShapeSize.Width();
    tools::Long nIsWdt=rTextSize.Width();
    if (nIsWdt==0) nIsWdt=1;

    tools::Long nWantHgt=rShapeSize.Height();
    tools::Long nIsHgt=rTextSize.Height();
    if (nIsHgt==0) nIsHgt=1;

    tools::Long nXTolPl=nWantWdt/100; // tolerance: +1%
    tools::Long nXTolMi=nWantWdt/25;  // tolerance: -4%
    tools::Long nXCorr =nWantWdt/20;  // correction scale: 5%

    double nX = nWantWdt / double(nIsWdt); // calculate X stretching
    double nY = nWantHgt / double(nIsHgt); // calculate Y stretching
    bool bChkX = true;
    if (bNoStretching)
    { // might only be possible proportionally
        if (nX > nY)
        {
            nX = nY;
            bChkX = false;
        }
        else
        {
            nY = nX;
        }
    }

    while (nLoopCount<5 && !bNoMoreLoop)
    {
        if (nX < 0.0)
            nX = -nX;
        if (nX < 0.01)
        {
            nX = 0.01;
            bNoMoreLoop = true;
        }
        if (nX > 655.35)
        {
            nX = 655.35;
            bNoMoreLoop = true;
        }

        if (nY < 0.0)
        {
            nY = -nY;
        }
        if (nY < 0.01)
        {
            nY = 0.01;
            bNoMoreLoop = true;
        }
        if (nY > 655.35)
        {
            nY = 655.35;
            bNoMoreLoop = true;
        }

        // exception, there is no text yet (horizontal case)
        if (nIsWdt <= 1)
        {
            nX = nY;
            bNoMoreLoop = true;
        }

        // exception, there is no text yet (vertical case)
        if (nIsHgt <= 1)
        {
            nY = nX;
            bNoMoreLoop = true;
        }
        rOutliner.setScalingParameters({nX, nY});
        nLoopCount++;
        Size aSiz(rOutliner.CalcTextSize());
        tools::Long nXDiff = aSiz.Width() - nWantWdt;
        rFitXCorrection=Fraction(nWantWdt,aSiz.Width());
        if (((nXDiff>=nXTolMi || !bChkX) && nXDiff<=nXTolPl) || nXDiff==nXDiff0) {
            bNoMoreLoop = true;
        } else {
            // correct stretching factors
            tools::Long nMul = nWantWdt;
            tools::Long nDiv = aSiz.Width();
            if (std::abs(nXDiff) <= 2 * nXCorr)
            {
                if (nMul > nDiv)
                    nDiv += (nMul - nDiv) / 2.0; // but only add half of what we calculated,
                else
                    nMul += (nDiv - nMul) / 2.0;// because the EditEngine calculates wrongly later on
            }
            nX = nX * double(nMul) / double(nDiv);
            if (bNoStretching)
                nY = nX;
        }
        nXDiff0 = nXDiff;
    }
}

OUString SdrTextObj::TakeObjNameSingul() const
{
    OUString aStr;

    switch(meTextKind)
    {
        case SdrObjKind::OutlineText:
        {
            aStr = SvxResId(STR_ObjNameSingulOUTLINETEXT);
            break;
        }

        case SdrObjKind::TitleText  :
        {
            aStr = SvxResId(STR_ObjNameSingulTITLETEXT);
            break;
        }

        default:
        {
            if(IsLinkedText())
                aStr = SvxResId(STR_ObjNameSingulTEXTLNK);
            else
                aStr = SvxResId(STR_ObjNameSingulTEXT);
            break;
        }
    }

    OutlinerParaObject* pOutlinerParaObject = GetOutlinerParaObject();
    if(pOutlinerParaObject && meTextKind != SdrObjKind::OutlineText)
    {
        // shouldn't currently cause any problems at OUTLINETEXT
        OUString aStr2(comphelper::string::stripStart(pOutlinerParaObject->GetTextObject().GetText(0), ' '));

        // avoid non expanded text portions in object name
        // (second condition is new)
        if(!aStr2.isEmpty() && aStr2.indexOf(u'\x00FF') == -1)
        {
            // space between ResStr and content text
            aStr += " \'";

            if(aStr2.getLength() > 10)
            {
                aStr2 = OUString::Concat(aStr2.subView(0, 8)) + "...";
            }

            aStr += aStr2 + "\'";
        }
    }

    OUString sName(aStr);

    OUString aName(GetName());
    if (!aName.isEmpty())
        sName += " '" + aName + "'";

    return sName;
}

OUString SdrTextObj::TakeObjNamePlural() const
{
    OUString sName;
    switch (meTextKind)
    {
        case SdrObjKind::OutlineText: sName=SvxResId(STR_ObjNamePluralOUTLINETEXT); break;
        case SdrObjKind::TitleText  : sName=SvxResId(STR_ObjNamePluralTITLETEXT);   break;
        default: {
            if (IsLinkedText()) {
                sName=SvxResId(STR_ObjNamePluralTEXTLNK);
            } else {
                sName=SvxResId(STR_ObjNamePluralTEXT);
            }
        } break;
    } // switch
    return sName;
}

rtl::Reference<SdrObject> SdrTextObj::CloneSdrObject(SdrModel& rTargetModel) const
{
    return new SdrTextObj(rTargetModel, *this);
}

basegfx::B2DPolyPolygon SdrTextObj::TakeXorPoly() const
{
    tools::Polygon aPol(getRectangle());
    if (maGeo.m_nShearAngle)
        ShearPoly(aPol, getRectangle().TopLeft(), maGeo.mfTanShearAngle);
    if (maGeo.m_nRotationAngle)
        RotatePoly(aPol, getRectangle().TopLeft(), maGeo.mfSinRotationAngle, maGeo.mfCosRotationAngle);

    basegfx::B2DPolyPolygon aRetval;
    aRetval.append(aPol.getB2DPolygon());
    return aRetval;
}

basegfx::B2DPolyPolygon SdrTextObj::TakeContour() const
{
    basegfx::B2DPolyPolygon aRetval(SdrAttrObj::TakeContour());

    // and now add the BoundRect of the text, if necessary
    if ( GetOutlinerParaObject() && !IsFontwork() && !IsContourTextFrame() )
    {
        // using Clone()-Paint() strategy inside TakeContour() leaves a destroyed
        // SdrObject as pointer in DrawOutliner. Set *this again in fetching the outliner
        // in every case
        SdrOutliner& rOutliner=ImpGetDrawOutliner();

        tools::Rectangle aAnchor2;
        tools::Rectangle aR;
        TakeTextRect(rOutliner,aR,false,&aAnchor2);
        rOutliner.Clear();
        bool bFitToSize(IsFitToSize());
        if (bFitToSize) aR=aAnchor2;
        tools::Polygon aPol(aR);
        if (maGeo.m_nRotationAngle) RotatePoly(aPol,aR.TopLeft(), maGeo.mfSinRotationAngle, maGeo.mfCosRotationAngle);

        aRetval.append(aPol.getB2DPolygon());
    }

    return aRetval;
}

void SdrTextObj::RecalcSnapRect()
{
    if (maGeo.m_nRotationAngle || maGeo.m_nShearAngle)
    {
        maSnapRect = Rect2Poly(getRectangle(), maGeo).GetBoundRect();
    } else {
        maSnapRect = getRectangle();
    }
}

sal_uInt32 SdrTextObj::GetSnapPointCount() const
{
    return 4;
}

Point SdrTextObj::GetSnapPoint(sal_uInt32 i) const
{
    Point aP;
    const auto& rRectangle = getRectangle();
    switch (i) {
        case 0: aP = rRectangle.TopLeft(); break;
        case 1: aP = rRectangle.TopRight(); break;
        case 2: aP = rRectangle.BottomLeft(); break;
        case 3: aP = rRectangle.BottomRight(); break;
        default: aP = rRectangle.Center(); break;
    }
    if (maGeo.m_nShearAngle)
        ShearPoint(aP, rRectangle.TopLeft(), maGeo.mfTanShearAngle);
    if (maGeo.m_nRotationAngle)
        RotatePoint(aP, rRectangle.TopLeft(), maGeo.mfSinRotationAngle, maGeo.mfCosRotationAngle);
    return aP;
}

// Extracted from ImpGetDrawOutliner()
void SdrTextObj::ImpInitDrawOutliner( SdrOutliner& rOutl ) const
{
    rOutl.SetUpdateLayout(false);
    OutlinerMode nOutlinerMode = OutlinerMode::OutlineObject;
    if ( !IsOutlText() )
        nOutlinerMode = OutlinerMode::TextObject;
    rOutl.Init( nOutlinerMode );

    rOutl.resetScalingParameters();

    EEControlBits nStat=rOutl.GetControlWord();
    nStat &= ~EEControlBits(EEControlBits::STRETCHING|EEControlBits::AUTOPAGESIZE);
    rOutl.SetControlWord(nStat);
    Size aMaxSize(100000,100000);
    rOutl.SetMinAutoPaperSize(Size());
    rOutl.SetMaxAutoPaperSize(aMaxSize);
    rOutl.SetPaperSize(aMaxSize);
    rOutl.ClearPolygon();
}

SdrOutliner& SdrTextObj::ImpGetDrawOutliner() const
{
    SdrOutliner& rOutl(getSdrModelFromSdrObject().GetDrawOutliner(this));

    // Code extracted to ImpInitDrawOutliner()
    ImpInitDrawOutliner( rOutl );

    return rOutl;
}

// Extracted from Paint()
void SdrTextObj::ImpSetupDrawOutlinerForPaint( bool             bContourFrame,
                                               SdrOutliner&     rOutliner,
                                               tools::Rectangle&       rTextRect,
                                               tools::Rectangle&       rAnchorRect,
                                               tools::Rectangle&       rPaintRect,
                                               Fraction&        rFitXCorrection ) const
{
    if (!bContourFrame)
    {
        // FitToSize can't be used together with ContourFrame for now
        if (IsFitToSize() || IsAutoFit())
        {
            EEControlBits nStat=rOutliner.GetControlWord();
            nStat|=EEControlBits::STRETCHING|EEControlBits::AUTOPAGESIZE;
            rOutliner.SetControlWord(nStat);
        }
    }
    rOutliner.SetFixedCellHeight(GetMergedItem(SDRATTR_TEXT_USEFIXEDCELLHEIGHT).GetValue());
    TakeTextRect(rOutliner, rTextRect, false, &rAnchorRect);
    rPaintRect = rTextRect;

    if (bContourFrame)
        return;

    // FitToSize can't be used together with ContourFrame for now
    if (IsFitToSize())
    {
        ImpSetCharStretching(rOutliner,rTextRect.GetSize(),rAnchorRect.GetSize(),rFitXCorrection);
        rPaintRect=rAnchorRect;
    }
    else if (IsAutoFit())
    {
        setupAutoFitText(rOutliner);
    }
}

double SdrTextObj::GetFontScale() const
{
    SdrOutliner& rOutliner = ImpGetDrawOutliner();
    // This eventually calls setupAutoFitText
    UpdateOutlinerFormatting(rOutliner, o3tl::temporary(tools::Rectangle()));

    return rOutliner.getScalingParameters().fFontY;
}

double SdrTextObj::GetSpacingScale() const
{
    SdrOutliner& rOutliner = ImpGetDrawOutliner();
    // This eventually calls setupAutoFitText
    UpdateOutlinerFormatting(rOutliner, o3tl::temporary(tools::Rectangle()));

    return rOutliner.getScalingParameters().fSpacingY;
}

void SdrTextObj::setupAutoFitText(SdrOutliner& rOutliner) const
{
    const Size aShapeSize = GetSnapRect().GetSize();
    Size aSize(aShapeSize.Width() - GetTextLeftDistance() - GetTextRightDistance(),
               aShapeSize.Height() - GetTextUpperDistance() - GetTextLowerDistance());

    setupAutoFitText(rOutliner, aSize);
}
void SdrTextObj::setupAutoFitText(SdrOutliner& rOutliner, const Size& rTextBoxSize) const
{
    rOutliner.setRoundFontSizeToPt(true); // We need to round the font size nearest integer pt size
    rOutliner.SetMaxAutoPaperSize(rTextBoxSize);
    rOutliner.SetPaperSize(rTextBoxSize);

    const SdrTextFitToSizeTypeItem& rItem = GetObjectItem(SDRATTR_TEXT_FITTOSIZE);

    double fFontScale = rItem.getFontScale();
    double fSpacingScale = rItem.getSpacingScale();

    if (fFontScale > 0.0 && fSpacingScale > 0.0 && !mbInEditMode)
    {
        rOutliner.setScalingParameters({ fFontScale, fFontScale, 1.0, fSpacingScale });
    }
    else
    {
        rOutliner.resetScalingParameters();
    }

    rOutliner.QuickFormatDoc();
}

void SdrTextObj::SetupOutlinerFormatting( SdrOutliner& rOutl, tools::Rectangle& rPaintRect ) const
{
    ImpInitDrawOutliner( rOutl );
    UpdateOutlinerFormatting( rOutl, rPaintRect );
}

void SdrTextObj::UpdateOutlinerFormatting( SdrOutliner& rOutl, tools::Rectangle& rPaintRect ) const
{
    tools::Rectangle aTextRect;
    tools::Rectangle aAnchorRect;
    Fraction aFitXCorrection(1,1);

    const bool bContourFrame(IsContourTextFrame());
    const MapMode aMapMode(getSdrModelFromSdrObject().GetScaleUnit());

    rOutl.SetRefMapMode(aMapMode);
    ImpSetupDrawOutlinerForPaint(
        bContourFrame,
        rOutl,
        aTextRect,
        aAnchorRect,
        rPaintRect,
        aFitXCorrection);
}


OutlinerParaObject* SdrTextObj::GetOutlinerParaObject() const
{
    SdrText* pText = getActiveText();
    if( pText )
        return pText->GetOutlinerParaObject();
    else
        return nullptr;
}

void SdrTextObj::NbcSetOutlinerParaObject(std::optional<OutlinerParaObject> pTextObject, bool bAdjustTextFrameWidthAndHeight)
{
    NbcSetOutlinerParaObjectForText( std::move(pTextObject), getActiveText(), bAdjustTextFrameWidthAndHeight );
}

namespace
{
    bool IsAutoGrow(const SdrTextObj& rObj)
    {
        bool bAutoGrow = rObj.IsAutoGrowHeight() || rObj.IsAutoGrowWidth();
        return bAutoGrow && !comphelper::IsFuzzing();
    }
}

void SdrTextObj::NbcSetOutlinerParaObjectForText( std::optional<OutlinerParaObject> pTextObject, SdrText* pText, bool bAdjustTextFrameWidthAndHeight )
{
    if( pText )
        pText->SetOutlinerParaObject( std::move(pTextObject) );

    if (pText && pText->GetOutlinerParaObject())
    {
        SvxWritingModeItem aWritingMode(pText->GetOutlinerParaObject()->IsEffectivelyVertical() && pText->GetOutlinerParaObject()->IsTopToBottom()
            ? css::text::WritingMode_TB_RL
            : css::text::WritingMode_LR_TB,
            SDRATTR_TEXTDIRECTION);
        GetProperties().SetObjectItemDirect(aWritingMode);
    }

    SetTextSizeDirty();
    if (IsTextFrame() && IsAutoGrow(*this) && bAdjustTextFrameWidthAndHeight)
    { // adapt text frame!
        NbcAdjustTextFrameWidthAndHeight();
    }
    if (!IsTextFrame())
    {
        // the SnapRect keeps its size
        SetBoundAndSnapRectsDirty(true);
    }

    // always invalidate BoundRect on change
    SetBoundRectDirty();
    ActionChanged();

    ImpSetTextStyleSheetListeners();
}

void SdrTextObj::NbcReformatText()
{
    SdrText* pText = getActiveText();
    if( !(pText && pText->GetOutlinerParaObject()) )
        return;

    pText->ReformatText();
    if (mbTextFrame)
    {
        NbcAdjustTextFrameWidthAndHeight();
    }
    else
    {
        // the SnapRect keeps its size
        SetBoundRectDirty();
        SetBoundAndSnapRectsDirty(/*bNotMyself*/true);
    }
    SetTextSizeDirty();
    ActionChanged();
    // i22396
    // Necessary here since we have no compare operator at the outliner
    // para object which may detect changes regarding the combination
    // of outliner para data and configuration (e.g., change of
    // formatting of text numerals)
    GetViewContact().flushViewObjectContacts(false);
}

std::unique_ptr<SdrObjGeoData> SdrTextObj::NewGeoData() const
{
    return std::make_unique<SdrTextObjGeoData>();
}

void SdrTextObj::SaveGeoData(SdrObjGeoData& rGeo) const
{
    SdrAttrObj::SaveGeoData(rGeo);
    SdrTextObjGeoData& rTGeo=static_cast<SdrTextObjGeoData&>(rGeo);
    rTGeo.maRect = getRectangle();
    rTGeo.maGeo = maGeo;
}

void SdrTextObj::RestoreGeoData(const SdrObjGeoData& rGeo)
{ // RectsDirty is called by SdrObject
    SdrAttrObj::RestoreGeoData(rGeo);
    const SdrTextObjGeoData& rTGeo=static_cast<const SdrTextObjGeoData&>(rGeo);
    NbcSetLogicRect(rTGeo.maRect);
    maGeo = rTGeo.maGeo;
    SetTextSizeDirty();
}

drawing::TextFitToSizeType SdrTextObj::GetFitToSize() const
{
    drawing::TextFitToSizeType eType = drawing::TextFitToSizeType_NONE;

    if(!IsAutoGrowWidth())
        eType = GetObjectItem(SDRATTR_TEXT_FITTOSIZE).GetValue();

    return eType;
}

const tools::Rectangle& SdrTextObj::GetGeoRect() const
{
    return getRectangle();
}

void SdrTextObj::ForceOutlinerParaObject()
{
    SdrText* pText = getActiveText();
    if( pText && (pText->GetOutlinerParaObject() == nullptr) )
    {
        OutlinerMode nOutlMode = OutlinerMode::TextObject;
        if( IsTextFrame() && meTextKind == SdrObjKind::OutlineText )
            nOutlMode = OutlinerMode::OutlineObject;

        pText->ForceOutlinerParaObject( nOutlMode );
    }
}

TextChain *SdrTextObj::GetTextChain() const
{
    //if (!IsChainable())
    //    return NULL;

    return getSdrModelFromSdrObject().GetTextChain();
}

bool SdrTextObj::IsVerticalWriting() const
{
    if(mpEditingOutliner)
    {
        return mpEditingOutliner->IsVertical();
    }

    OutlinerParaObject* pOutlinerParaObject = GetOutlinerParaObject();
    if(pOutlinerParaObject)
    {
        return pOutlinerParaObject->IsEffectivelyVertical();
    }

    return false;
}

void SdrTextObj::SetVerticalWriting(bool bVertical)
{
    OutlinerParaObject* pOutlinerParaObject = GetOutlinerParaObject();

    if( !pOutlinerParaObject && bVertical )
    {
        // we only need to force an outliner para object if the default of
        // horizontal text is changed
        ForceOutlinerParaObject();
        pOutlinerParaObject = GetOutlinerParaObject();
    }

    if (!pOutlinerParaObject ||
        (pOutlinerParaObject->IsEffectivelyVertical() == bVertical))
        return;

    // get item settings
    const SfxItemSet& rSet = GetObjectItemSet();
    bool bAutoGrowWidth = rSet.Get(SDRATTR_TEXT_AUTOGROWWIDTH).GetValue();
    bool bAutoGrowHeight = rSet.Get(SDRATTR_TEXT_AUTOGROWHEIGHT).GetValue();

    // Also exchange hor/ver adjust items
    SdrTextHorzAdjust eHorz = rSet.Get(SDRATTR_TEXT_HORZADJUST).GetValue();
    SdrTextVertAdjust eVert = rSet.Get(SDRATTR_TEXT_VERTADJUST).GetValue();

    // rescue object size
    tools::Rectangle aObjectRect = GetSnapRect();

    // prepare ItemSet to set exchanged width and height items
    SfxItemSet aNewSet(SfxItemSet::makeFixedSfxItemSet<SDRATTR_TEXT_AUTOGROWHEIGHT, SDRATTR_TEXT_AUTOGROWHEIGHT,
                                                       // Expanded item ranges to also support hor and ver adjust.
                                                       SDRATTR_TEXT_VERTADJUST, SDRATTR_TEXT_VERTADJUST,
                                                       SDRATTR_TEXT_AUTOGROWWIDTH, SDRATTR_TEXT_HORZADJUST>(*rSet.GetPool()));

    aNewSet.Put(rSet);
    aNewSet.Put(makeSdrTextAutoGrowWidthItem(bAutoGrowHeight));
    aNewSet.Put(makeSdrTextAutoGrowHeightItem(bAutoGrowWidth));

    // Exchange horz and vert adjusts
    switch (eVert)
    {
        case SDRTEXTVERTADJUST_TOP: aNewSet.Put(SdrTextHorzAdjustItem(SDRTEXTHORZADJUST_RIGHT)); break;
        case SDRTEXTVERTADJUST_CENTER: aNewSet.Put(SdrTextHorzAdjustItem(SDRTEXTHORZADJUST_CENTER)); break;
        case SDRTEXTVERTADJUST_BOTTOM: aNewSet.Put(SdrTextHorzAdjustItem(SDRTEXTHORZADJUST_LEFT)); break;
        case SDRTEXTVERTADJUST_BLOCK: aNewSet.Put(SdrTextHorzAdjustItem(SDRTEXTHORZADJUST_BLOCK)); break;
    }
    switch (eHorz)
    {
        case SDRTEXTHORZADJUST_LEFT: aNewSet.Put(SdrTextVertAdjustItem(SDRTEXTVERTADJUST_BOTTOM)); break;
        case SDRTEXTHORZADJUST_CENTER: aNewSet.Put(SdrTextVertAdjustItem(SDRTEXTVERTADJUST_CENTER)); break;
        case SDRTEXTHORZADJUST_RIGHT: aNewSet.Put(SdrTextVertAdjustItem(SDRTEXTVERTADJUST_TOP)); break;
        case SDRTEXTHORZADJUST_BLOCK: aNewSet.Put(SdrTextVertAdjustItem(SDRTEXTVERTADJUST_BLOCK)); break;
    }

    SetObjectItemSet(aNewSet);

    pOutlinerParaObject = GetOutlinerParaObject();
    if (pOutlinerParaObject)
    {
        // set ParaObject orientation accordingly
        pOutlinerParaObject->SetVertical(bVertical);
    }

    // restore object size
    SetSnapRect(aObjectRect);
}

bool SdrTextObj::IsTopToBottom() const
{
    if (mpEditingOutliner)
        return mpEditingOutliner->IsTopToBottom();

    if (OutlinerParaObject* pOutlinerParaObject = GetOutlinerParaObject())
        return pOutlinerParaObject->IsTopToBottom();

    return false;
}

// transformation interface for StarOfficeAPI. This implements support for
// homogeneous 3x3 matrices containing the transformation of the SdrObject. At the
// moment it contains a shearX, rotation and translation, but for setting all linear
// transforms like Scale, ShearX, ShearY, Rotate and Translate are supported.


// gets base transformation and rectangle of object. If it's an SdrPathObj it fills the PolyPolygon
// with the base geometry and returns TRUE. Otherwise it returns FALSE.
bool SdrTextObj::TRGetBaseGeometry(basegfx::B2DHomMatrix& rMatrix, basegfx::B2DPolyPolygon& /*rPolyPolygon*/) const
{
    // get turn and shear
    double fRotate = toRadians(maGeo.m_nRotationAngle);
    double fShearX = toRadians(maGeo.m_nShearAngle);

    // get aRect, this is the unrotated snaprect
    tools::Rectangle aRectangle(getRectangle());

    // fill other values
    basegfx::B2DTuple aScale(aRectangle.GetWidth(), aRectangle.GetHeight());
    basegfx::B2DTuple aTranslate(aRectangle.Left(), aRectangle.Top());

    // position maybe relative to anchorpos, convert
    if( getSdrModelFromSdrObject().IsWriter() )
    {
        if(GetAnchorPos().X() || GetAnchorPos().Y())
        {
            aTranslate -= basegfx::B2DTuple(GetAnchorPos().X(), GetAnchorPos().Y());
        }
    }

    // build matrix
    rMatrix = basegfx::utils::createScaleShearXRotateTranslateB2DHomMatrix(
        aScale,
        basegfx::fTools::equalZero(fShearX) ? 0.0 : tan(fShearX),
        -fRotate,
        aTranslate);

    return false;
}

// sets the base geometry of the object using infos contained in the homogeneous 3x3 matrix.
// If it's an SdrPathObj it will use the provided geometry information. The Polygon has
// to use (0,0) as upper left and will be scaled to the given size in the matrix.
void SdrTextObj::TRSetBaseGeometry(const basegfx::B2DHomMatrix& rMatrix, const basegfx::B2DPolyPolygon& /*rPolyPolygon*/)
{
    // break up matrix
    basegfx::B2DTuple aScale;
    basegfx::B2DTuple aTranslate;
    double fRotate(0.0);
    double fShearX(0.0);
    rMatrix.decompose(aScale, aTranslate, fRotate, fShearX);

    // flip?
    bool bFlipX = aScale.getX() < 0.0,
         bFlipY = aScale.getY() < 0.0;
    if (bFlipX)
    {
        aScale.setX(fabs(aScale.getX()));
    }
    if (bFlipY)
    {
        aScale.setY(fabs(aScale.getY()));
    }

    // reset object shear and rotations
    maGeo.m_nRotationAngle = 0_deg100;
    maGeo.RecalcSinCos();
    maGeo.m_nShearAngle = 0_deg100;
    maGeo.RecalcTan();

    // if anchor is used, make position relative to it
    if( getSdrModelFromSdrObject().IsWriter() )
    {
        if(GetAnchorPos().X() || GetAnchorPos().Y())
        {
            aTranslate += basegfx::B2DTuple(GetAnchorPos().X(), GetAnchorPos().Y());
        }
    }

    // build and set BaseRect (use scale)
    Size aSize(basegfx::fround<tools::Long>(aScale.getX()),
               basegfx::fround<tools::Long>(aScale.getY()));
    tools::Rectangle aBaseRect(Point(), aSize);
    SetSnapRect(aBaseRect);

    // flip?
    if (bFlipX)
    {
        Mirror(Point(), Point(0, 1));
    }
    if (bFlipY)
    {
        Mirror(Point(), Point(1, 0));
    }

    // shear?
    if(!basegfx::fTools::equalZero(fShearX))
    {
        GeoStat aGeoStat;
        aGeoStat.m_nShearAngle = Degree100(basegfx::fround(basegfx::rad2deg<100>(atan(fShearX))));
        aGeoStat.RecalcTan();
        Shear(Point(), aGeoStat.m_nShearAngle, aGeoStat.mfTanShearAngle, false);
    }

    // rotation?
    if(!basegfx::fTools::equalZero(fRotate))
    {
        GeoStat aGeoStat;

        // #i78696#
        // fRotate is matematically correct, but aGeoStat.nRotationAngle is
        // mirrored -> mirror value here
        aGeoStat.m_nRotationAngle = NormAngle36000(Degree100(basegfx::fround(-basegfx::rad2deg<100>(fRotate))));
        aGeoStat.RecalcSinCos();
        Rotate(Point(), aGeoStat.m_nRotationAngle, aGeoStat.mfSinRotationAngle, aGeoStat.mfCosRotationAngle);
    }

    // translate?
    if(!aTranslate.equalZero())
    {
        Move(Size(basegfx::fround<tools::Long>(aTranslate.getX()),
                  basegfx::fround<tools::Long>(aTranslate.getY())));
    }
}

bool SdrTextObj::IsReallyEdited() const
{
    return mpEditingOutliner && mpEditingOutliner->IsModified();
}

// moved inlines here form hxx

tools::Long SdrTextObj::GetEckenradius() const
{
    return GetObjectItemSet().Get(SDRATTR_CORNER_RADIUS).GetValue();
}

tools::Long SdrTextObj::GetMinTextFrameHeight() const
{
    return GetObjectItemSet().Get(SDRATTR_TEXT_MINFRAMEHEIGHT).GetValue();
}

tools::Long SdrTextObj::GetMaxTextFrameHeight() const
{
    return GetObjectItemSet().Get(SDRATTR_TEXT_MAXFRAMEHEIGHT).GetValue();
}

tools::Long SdrTextObj::GetMinTextFrameWidth() const
{
    return GetObjectItemSet().Get(SDRATTR_TEXT_MINFRAMEWIDTH).GetValue();
}

tools::Long SdrTextObj::GetMaxTextFrameWidth() const
{
    return GetObjectItemSet().Get(SDRATTR_TEXT_MAXFRAMEWIDTH).GetValue();
}

bool SdrTextObj::IsFontwork() const
{
    return !mbTextFrame // Default is FALSE
        && GetObjectItemSet().Get(XATTR_FORMTXTSTYLE).GetValue() != XFormTextStyle::NONE;
}

bool SdrTextObj::IsHideContour() const
{
    return !mbTextFrame // Default is: no, don't HideContour; HideContour not together with TextFrames
        && GetObjectItemSet().Get(XATTR_FORMTXTHIDEFORM).GetValue();
}

bool SdrTextObj::IsContourTextFrame() const
{
    return !mbTextFrame // ContourFrame not together with normal TextFrames
        && GetObjectItemSet().Get(SDRATTR_TEXT_CONTOURFRAME).GetValue();
}

tools::Long SdrTextObj::GetTextLeftDistance() const
{
    return GetObjectItemSet().Get(SDRATTR_TEXT_LEFTDIST).GetValue();
}

tools::Long SdrTextObj::GetTextRightDistance() const
{
    return GetObjectItemSet().Get(SDRATTR_TEXT_RIGHTDIST).GetValue();
}

tools::Long SdrTextObj::GetTextUpperDistance() const
{
    return GetObjectItemSet().Get(SDRATTR_TEXT_UPPERDIST).GetValue();
}

tools::Long SdrTextObj::GetTextLowerDistance() const
{
    return GetObjectItemSet().Get(SDRATTR_TEXT_LOWERDIST).GetValue();
}

SdrTextAniKind SdrTextObj::GetTextAniKind() const
{
    return GetObjectItemSet().Get(SDRATTR_TEXT_ANIKIND).GetValue();
}

SdrTextAniDirection SdrTextObj::GetTextAniDirection() const
{
    return GetObjectItemSet().Get(SDRATTR_TEXT_ANIDIRECTION).GetValue();
}

bool SdrTextObj::HasTextColumnsNumber() const
{
    return GetObjectItemSet().HasItem(SDRATTR_TEXTCOLUMNS_NUMBER);
}

sal_Int16 SdrTextObj::GetTextColumnsNumber() const
{
    return GetObjectItemSet().Get(SDRATTR_TEXTCOLUMNS_NUMBER).GetValue();
}

void SdrTextObj::SetTextColumnsNumber(sal_Int16 nColumns)
{
    SetObjectItem(SfxInt16Item(SDRATTR_TEXTCOLUMNS_NUMBER, nColumns));
}

bool SdrTextObj::HasTextColumnsSpacing() const
{
    return GetObjectItemSet().HasItem(SDRATTR_TEXTCOLUMNS_SPACING);
}

sal_Int32 SdrTextObj::GetTextColumnsSpacing() const
{
    return GetObjectItemSet().Get(SDRATTR_TEXTCOLUMNS_SPACING).GetValue();
}

void SdrTextObj::SetTextColumnsSpacing(sal_Int32 nSpacing)
{
    SetObjectItem(SdrMetricItem(SDRATTR_TEXTCOLUMNS_SPACING, nSpacing));
}

// Get necessary data for text scroll animation. ATM base it on a Text-Metafile and a
// painting rectangle. Rotation is excluded from the returned values.
GDIMetaFile* SdrTextObj::GetTextScrollMetaFileAndRectangle(
    tools::Rectangle& rScrollRectangle, tools::Rectangle& rPaintRectangle)
{
    GDIMetaFile* pRetval = nullptr;
    SdrOutliner& rOutliner = ImpGetDrawOutliner();
    tools::Rectangle aTextRect;
    tools::Rectangle aAnchorRect;
    tools::Rectangle aPaintRect;
    Fraction aFitXCorrection(1,1);
    bool bContourFrame(IsContourTextFrame());

    // get outliner set up. To avoid getting a somehow rotated MetaFile,
    // temporarily disable object rotation.
    Degree100 nAngle(maGeo.m_nRotationAngle);
    maGeo.m_nRotationAngle = 0_deg100;
    ImpSetupDrawOutlinerForPaint( bContourFrame, rOutliner, aTextRect, aAnchorRect, aPaintRect, aFitXCorrection );
    maGeo.m_nRotationAngle = nAngle;

    tools::Rectangle aScrollFrameRect(aPaintRect);
    const SfxItemSet& rSet = GetObjectItemSet();
    SdrTextAniDirection eDirection = rSet.Get(SDRATTR_TEXT_ANIDIRECTION).GetValue();

    if(SdrTextAniDirection::Left == eDirection || SdrTextAniDirection::Right == eDirection)
    {
        aScrollFrameRect.SetLeft( aAnchorRect.Left() );
        aScrollFrameRect.SetRight( aAnchorRect.Right() );
    }

    if(SdrTextAniDirection::Up == eDirection || SdrTextAniDirection::Down == eDirection)
    {
        aScrollFrameRect.SetTop( aAnchorRect.Top() );
        aScrollFrameRect.SetBottom( aAnchorRect.Bottom() );
    }

    // create the MetaFile
    pRetval = new GDIMetaFile;
    ScopedVclPtrInstance< VirtualDevice > pBlackHole;
    pBlackHole->EnableOutput(false);
    pRetval->Record(pBlackHole);
    Point aPaintPos = aPaintRect.TopLeft();

    rOutliner.DrawText_ToPosition(*pBlackHole, aPaintPos);

    pRetval->Stop();
    pRetval->WindStart();

    // return PaintRectanglePixel and pRetval;
    rScrollRectangle = aScrollFrameRect;
    rPaintRectangle = aPaintRect;

    return pRetval;
}

// Access to TextAnimationAllowed flag
bool SdrTextObj::IsAutoFit() const
{
    return GetFitToSize() == drawing::TextFitToSizeType_AUTOFIT;
}

bool SdrTextObj::IsFitToSize() const
{
    const drawing::TextFitToSizeType eFit = GetFitToSize();
    return (eFit == drawing::TextFitToSizeType_PROPORTIONAL
         || eFit == drawing::TextFitToSizeType_ALLLINES);
}

void SdrTextObj::SetTextAnimationAllowed(bool bNew)
{
    if(mbTextAnimationAllowed != bNew)
    {
        mbTextAnimationAllowed = bNew;
        ActionChanged();
    }
}

/** called from the SdrObjEditView during text edit when the status of the edit outliner changes */
void SdrTextObj::onEditOutlinerStatusEvent( EditStatus* pEditStatus )
{
    const EditStatusFlags nStat = pEditStatus->GetStatusWord();
    const bool bGrowX = bool(nStat & EditStatusFlags::TEXTWIDTHCHANGED);
    const bool bGrowY = bool(nStat & EditStatusFlags::TextHeightChanged);
    if(!(mbTextFrame && (bGrowX || bGrowY)))
        return;

    if ((bGrowX && IsAutoGrowWidth()) || (bGrowY && IsAutoGrowHeight()))
    {
        AdjustTextFrameWidthAndHeight();
    }
    else if ( (IsAutoFit() || IsFitToSize()) && !mbInDownScale)
    {
        assert(mpEditingOutliner);
        mbInDownScale = true;

        // Need to reset scaling so it searches for the fitting size again
        mpEditingOutliner->resetScalingParameters();

        // sucks that we cannot disable paints via
        // mpEditingOutliner->SetUpdateMode(FALSE) - but EditEngine skips
        // formatting as well, then.
        setupAutoFitText(*mpEditingOutliner);
        mbInDownScale = false;
    }
}

/* Begin chaining code */

// XXX: Make it a method somewhere?
static SdrObject *ImpGetObjByName(SdrObjList const *pObjList, std::u16string_view aObjName)
{
    // scan the whole list
    for (const rtl::Reference<SdrObject>& pCurObj : *pObjList)
        if (pCurObj->GetName() == aObjName)
            return pCurObj.get();
    // not found
    return nullptr;
}

// XXX: Make it a (private) method of SdrTextObj
static void ImpUpdateChainLinks(SdrTextObj *pTextObj, std::u16string_view aNextLinkName)
{
    // XXX: Current implementation constraints text boxes to be on the same page

    // No next link
    if (aNextLinkName.empty()) {
        pTextObj->SetNextLinkInChain(nullptr);
        return;
    }

    SdrPage *pPage(pTextObj->getSdrPageFromSdrObject());
    assert(pPage);
    SdrTextObj *pNextTextObj = DynCastSdrTextObj
                                (ImpGetObjByName(pPage, aNextLinkName));
    if (!pNextTextObj) {
        SAL_INFO("svx.chaining", "[CHAINING] Can't find object as next link.");
        return;
    }

    pTextObj->SetNextLinkInChain(pNextTextObj);
}

bool SdrTextObj::IsChainable() const
{
    // Read it as item
    const SfxItemSet& rSet = GetObjectItemSet();
    OUString aNextLinkName = rSet.Get(SDRATTR_TEXT_CHAINNEXTNAME).GetValue();

    // Update links if any inconsistency is found
    bool bNextLinkUnsetYet = !aNextLinkName.isEmpty() && !mpNextInChain;
    bool bInconsistentNextLink = mpNextInChain && mpNextInChain->GetName() != aNextLinkName;
    // if the link is not set despite there should be one OR if it has changed
    if (bNextLinkUnsetYet || bInconsistentNextLink) {
        ImpUpdateChainLinks(const_cast<SdrTextObj *>(this), aNextLinkName);
    }

    return !aNextLinkName.isEmpty(); // XXX: Should we also check for GetNilChainingEvent? (see old code below)

/*
    // Check that no overflow is going on
    if (!GetTextChain() || GetTextChain()->GetNilChainingEvent(this))
        return false;
*/
}

void SdrTextObj::onChainingEvent()
{
    if (!mpEditingOutliner)
        return;

    // Outliner for text transfer
    SdrOutliner &aDrawOutliner = ImpGetDrawOutliner();

    EditingTextChainFlow aTxtChainFlow(this);
    aTxtChainFlow.CheckForFlowEvents(mpEditingOutliner);

    if (aTxtChainFlow.IsOverflow()) {
        SAL_INFO("svx.chaining", "[CHAINING] Overflow going on");
        // One outliner is for non-overflowing text, the other for overflowing text
        // We remove text directly from the editing outliner
        aTxtChainFlow.ExecuteOverflow(mpEditingOutliner, &aDrawOutliner);
    } else if (aTxtChainFlow.IsUnderflow()) {
        SAL_INFO("svx.chaining", "[CHAINING] Underflow going on");
        // underflow-induced overflow
        aTxtChainFlow.ExecuteUnderflow(&aDrawOutliner);
        bool bIsOverflowFromUnderflow = aTxtChainFlow.IsOverflow();
        // handle overflow
        if (bIsOverflowFromUnderflow) {
            SAL_INFO("svx.chaining", "[CHAINING] Overflow going on (underflow induced)");
            // prevents infinite loops when setting text for editing outliner
            aTxtChainFlow.ExecuteOverflow(&aDrawOutliner, &aDrawOutliner);
        }
    }
}

SdrTextObj* SdrTextObj::GetNextLinkInChain() const
{
    /*
    if (GetTextChain())
        return GetTextChain()->GetNextLink(this);

    return NULL;
    */

    return mpNextInChain;
}

void SdrTextObj::SetNextLinkInChain(SdrTextObj *pNextObj)
{
    // Basically a doubly linked list implementation

    SdrTextObj *pOldNextObj = mpNextInChain;

    // Replace next link
    mpNextInChain = pNextObj;
    // Deal with old next link's prev link
    if (pOldNextObj) {
        pOldNextObj->mpPrevInChain = nullptr;
    }

    // Deal with new next link's prev link
    if (mpNextInChain) {
        // If there is a prev already at all and this is not already the current object
        if (mpNextInChain->mpPrevInChain &&
            mpNextInChain->mpPrevInChain != this)
            mpNextInChain->mpPrevInChain->mpNextInChain = nullptr;
        mpNextInChain->mpPrevInChain = this;
    }

    // TODO: Introduce check for circular chains

}

SdrTextObj* SdrTextObj::GetPrevLinkInChain() const
{
    /*
    if (GetTextChain())
        return GetTextChain()->GetPrevLink(this);

    return NULL;
    */

    return mpPrevInChain;
}

bool SdrTextObj::GetPreventChainable() const
{
    // Prevent chaining it 1) during dragging && 2) when we are editing next link
    return mbIsUnchainableClone || (GetNextLinkInChain() && GetNextLinkInChain()->IsInEditMode());
}

rtl::Reference<SdrObject> SdrTextObj::getFullDragClone() const
{
    rtl::Reference<SdrObject> pClone = SdrAttrObj::getFullDragClone();
    SdrTextObj *pTextObjClone = DynCastSdrTextObj(pClone.get());
    if (pTextObjClone != nullptr) {
        // Avoid transferring of text for chainable object during dragging
        pTextObjClone->mbIsUnchainableClone = true;
    }

    return pClone;
 }

/* End chaining code */

/** returns the currently active text. */
SdrText* SdrTextObj::getActiveText() const
{
    if( !mxText )
        return getText( 0 );
    else
        return mxText.get();
}

/** returns the nth available text. */
SdrText* SdrTextObj::getText( sal_Int32 nIndex ) const
{
    if( nIndex == 0 )
    {
        if( !mxText )
            const_cast< SdrTextObj* >(this)->mxText = new SdrText( *const_cast< SdrTextObj* >(this) );
        return mxText.get();
    }
    else
    {
        return nullptr;
    }
}

/** returns the number of texts available for this object. */
sal_Int32 SdrTextObj::getTextCount() const
{
    return 1;
}

/** changes the current active text */
void SdrTextObj::setActiveText( sal_Int32 /*nIndex*/ )
{
}

/** returns the index of the text that contains the given point or -1 */
sal_Int32 SdrTextObj::CheckTextHit(const Point& /*rPnt*/) const
{
    return 0;
}

void SdrTextObj::SetObjectItemNoBroadcast(const SfxPoolItem& rItem)
{
    static_cast< sdr::properties::TextProperties& >(GetProperties()).SetObjectItemNoBroadcast(rItem);
}


// The concept of the text object:
// ~~~~~~~~~~~~~~~~~~~~~~~~
// Attributes/Variations:
// - bool text frame / graphics object with caption
// - bool FontWork                 (if it is not a text frame and not a ContourTextFrame)
// - bool ContourTextFrame         (if it is not a text frame and not Fontwork)
// - long rotation angle               (if it is not FontWork)
// - long text frame margins           (if it is not FontWork)
// - bool FitToSize                (if it is not FontWork)
// - bool AutoGrowingWidth/Height  (if it is not FitToSize and not FontWork)
// - long Min/MaxFrameWidth/Height     (if AutoGrowingWidth/Height)
// - enum horizontal text anchoring left,center,right,justify/block,Stretch(ni)
// - enum vertical text anchoring top, middle, bottom, block, stretch(ni)
// - enum ticker text                  (if it is not FontWork)

// Every derived object is either a text frame (mbTextFrame=true)
// or a drawing object with a caption (mbTextFrame=false).

// Default anchoring for text frames:
//   SDRTEXTHORZADJUST_BLOCK, SDRTEXTVERTADJUST_TOP
//   = static Pool defaults
// Default anchoring for drawing objects with a caption:
//   SDRTEXTHORZADJUST_CENTER, SDRTEXTVERTADJUST_CENTER
//   via "hard" attribution of SdrAttrObj

// Every object derived from SdrTextObj must return an "UnrotatedSnapRect"
// (->TakeUnrotatedSnapRect()) (the reference point for the rotation is the top
// left of the rectangle (maGeo.nRotationAngle)) which is the basis for anchoring
// text. We then subtract the text frame margins from this rectangle, as a re-
// sult we get the anchoring area (->TakeTextAnchorRect()). Within this area, we
// calculate the anchoring point and the painting area, depending on the hori-
// zontal and vertical adjustment of the text (SdrTextVertAdjust,
// SdrTextHorzAdjust).
// In the case of drawing objects with a caption the painting area might well
// be larger than the anchoring area, for text frames on the other hand, it is
// always of the same or a smaller size (except when there are negative text
// frame margins).

// FitToSize takes priority over text anchoring and AutoGrowHeight/Width. When
// FitToSize is turned on, the painting area is always equal to the anchoring
// area. Additionally, FitToSize doesn't allow automatic line breaks.

// ContourTextFrame:
// - long rotation angle
// - long text frame margins (maybe later)
// - bool FitToSize (maybe later)
// - bool AutoGrowingWidth/Height (maybe much later)
// - long Min/MaxFrameWidth/Height (maybe much later)
// - enum horizontal text anchoring (maybe later, for now: left, centered)
// - enum vertical text anchoring (maybe later, for now: top)
// - enum ticker text (maybe later, maybe even with correct clipping)

// When making changes, check these:
// - Paint
// - HitTest
// - ConvertToPoly
// - Edit
// - Printing, Saving, Painting in neighboring View while editing
// - ModelChanged (e. g. through a neighboring View or rulers) while editing
// - FillColorChanged while editing
// - and many more...


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
