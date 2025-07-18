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

#include <sal/config.h>

#include <cassert>

#include <basegfx/matrix/b2dhommatrix.hxx>
#include <basegfx/polygon/b2dpolygon.hxx>
#include <basegfx/polygon/b2dpolygontools.hxx>
#include <basegfx/range/b2drange.hxx>
#include <basegfx/tuple/b2dtuple.hxx>
#include <tools/bigint.hxx>
#include <tools/helpers.hxx>

#include <svx/dialmgr.hxx>
#include <svx/strings.hrc>

#include <sdr/contact/viewcontactofsdrcaptionobj.hxx>
#include <sdr/properties/captionproperties.hxx>
#include <svx/sdrhittesthelper.hxx>
#include <svx/sdooitm.hxx>
#include <svx/svddrag.hxx>
#include <svx/svdhdl.hxx>
#include <svx/svdmodel.hxx>
#include <svx/svdocapt.hxx>
#include <svx/svdopath.hxx>
#include <svx/svdogrp.hxx>
#include <svx/svdpage.hxx>
#include <svx/svdtrans.hxx>
#include <svx/svdview.hxx>
#include <svx/sxcecitm.hxx>
#include <svx/sxcgitm.hxx>
#include <svx/sxcllitm.hxx>
#include <svx/sxctitm.hxx>
#include <vcl/canvastools.hxx>
#include <vcl/ptrstyle.hxx>

namespace {

enum EscDir {LKS,RTS,OBN,UNT};

}

class ImpCaptParams
{
public:
    SdrCaptionType              eType;
    tools::Long                        nGap;
    tools::Long                        nEscRel;
    tools::Long                        nEscAbs;
    tools::Long                        nLineLen;
    SdrCaptionEscDir            eEscDir;
    bool                        bFitLineLen;
    bool                        bEscRel;
    bool                        bFixedAngle;

public:
    ImpCaptParams()
      : eType(SdrCaptionType::Type3),
        nGap(0), nEscRel(5000), nEscAbs(0),
        nLineLen(0), eEscDir(SdrCaptionEscDir::Horizontal),
        bFitLineLen(true), bEscRel(true), bFixedAngle(false)
    {
    }
    void CalcEscPos(const Point& rTail, const tools::Rectangle& rRect, Point& rPt, EscDir& rDir) const;
};

void ImpCaptParams::CalcEscPos(const Point& rTailPt, const tools::Rectangle& rRect, Point& rPt, EscDir& rDir) const
{
    Point aTl(rTailPt); // copy locally for performance reasons
    tools::Long nX,nY;
    if (bEscRel) {
        nX=rRect.Right()-rRect.Left();
        nX=BigMulDiv(nX,nEscRel,10000);
        nY=rRect.Bottom()-rRect.Top();
        nY=BigMulDiv(nY,nEscRel,10000);
    } else {
        nX=nEscAbs;
        nY=nEscAbs;
    }
    nX+=rRect.Left();
    nY+=rRect.Top();
    Point  aBestPt;
    EscDir eBestDir=LKS;
    bool bTryH=eEscDir==SdrCaptionEscDir::BestFit;
    if (!bTryH) {
        if (eType!=SdrCaptionType::Type1) {
            bTryH=eEscDir==SdrCaptionEscDir::Horizontal;
        } else {
            bTryH=eEscDir==SdrCaptionEscDir::Vertical;
        }
    }
    bool bTryV=eEscDir==SdrCaptionEscDir::BestFit;
    if (!bTryV) {
        if (eType!=SdrCaptionType::Type1) {
            bTryV=eEscDir==SdrCaptionEscDir::Vertical;
        } else {
            bTryV=eEscDir==SdrCaptionEscDir::Horizontal;
        }
    }

    if (bTryH) {
        Point aLft(rRect.Left()-nGap,nY);
        Point aRgt(rRect.Right()+nGap,nY);
        bool bLft=(aTl.X()-aLft.X()<aRgt.X()-aTl.X());
        if (bLft) {
            eBestDir=LKS;
            aBestPt=aLft;
        } else {
            eBestDir=RTS;
            aBestPt=aRgt;
        }
    }
    if (bTryV) {
        Point aTop(nX,rRect.Top()-nGap);
        Point aBtm(nX,rRect.Bottom()+nGap);
        bool bTop=(aTl.Y()-aTop.Y()<aBtm.Y()-aTl.Y());
        Point aBest2;
        EscDir eBest2;
        if (bTop) {
            eBest2=OBN;
            aBest2=aTop;
        } else {
            eBest2=UNT;
            aBest2=aBtm;
        }
        bool bTakeIt=eEscDir!=SdrCaptionEscDir::BestFit;
        if (!bTakeIt) {
            BigInt aHorX(aBestPt.X()-aTl.X()); aHorX*=aHorX;
            BigInt aHorY(aBestPt.Y()-aTl.Y()); aHorY*=aHorY;
            BigInt aVerX(aBest2.X()-aTl.X());  aVerX*=aVerX;
            BigInt aVerY(aBest2.Y()-aTl.Y());  aVerY*=aVerY;
            if (eType!=SdrCaptionType::Type1) {
                bTakeIt=aVerX+aVerY<aHorX+aHorY;
            } else {
                bTakeIt=aVerX+aVerY>=aHorX+aHorY;
            }
        }
        if (bTakeIt) {
            aBestPt=aBest2;
            eBestDir=eBest2;
        }
    }
    rPt=aBestPt;
    rDir=eBestDir;
}


// BaseProperties section

std::unique_ptr<sdr::properties::BaseProperties> SdrCaptionObj::CreateObjectSpecificProperties()
{
    return std::make_unique<sdr::properties::CaptionProperties>(*this);
}


// DrawContact section

std::unique_ptr<sdr::contact::ViewContact> SdrCaptionObj::CreateObjectSpecificViewContact()
{
    return std::make_unique<sdr::contact::ViewContactOfSdrCaptionObj>(*this);
}


SdrCaptionObj::SdrCaptionObj(SdrModel& rSdrModel)
    : SdrRectObj(rSdrModel, tools::Rectangle(), SdrObjKind::Text)
    , maTailPoly(3)  // default size: 3 points = 2 lines
    , mbSpecialTextBoxShadow(false)
    , mbFixedTail(false)
    , mbSuppressGetBitmap(false)
{
}

SdrCaptionObj::SdrCaptionObj(SdrModel& rSdrModel, const tools::Rectangle& rRect, const Point& rTail)
    : SdrRectObj(rSdrModel, rRect, SdrObjKind::Text)
    , maTailPoly(3)  // default size: 3 points = 2 lines
    , mbSpecialTextBoxShadow(false)
    , mbFixedTail(false)
    , mbSuppressGetBitmap(false)
{
    maTailPoly[0] = maFixedTailPos = rTail;
}

SdrCaptionObj::SdrCaptionObj(SdrModel& rSdrModel, SdrCaptionObj const & rSource)
:   SdrRectObj(rSdrModel, rSource),
    mbSuppressGetBitmap(false)
{
    maTailPoly = rSource.maTailPoly;
    mbSpecialTextBoxShadow = rSource.mbSpecialTextBoxShadow;
    mbFixedTail = rSource.mbFixedTail;
    maFixedTailPos = rSource.maFixedTailPos;
}

SdrCaptionObj::~SdrCaptionObj() = default;

void SdrCaptionObj::TakeObjInfo(SdrObjTransformInfoRec& rInfo) const
{
    rInfo.bRotateFreeAllowed=false;
    rInfo.bRotate90Allowed  =false;
    rInfo.bMirrorFreeAllowed=false;
    rInfo.bMirror45Allowed  =false;
    rInfo.bMirror90Allowed  =false;
    rInfo.bTransparenceAllowed = false;
    rInfo.bShearAllowed     =false;
    rInfo.bEdgeRadiusAllowed=false;
    rInfo.bCanConvToPath    =true;
    rInfo.bCanConvToPoly    =true;
    rInfo.bCanConvToPathLineToArea=false;
    rInfo.bCanConvToPolyLineToArea=false;
    rInfo.bCanConvToContour = (rInfo.bCanConvToPoly || LineGeometryUsageIsNecessary());
}

SdrObjKind SdrCaptionObj::GetObjIdentifier() const
{
    return SdrObjKind::Caption;
}

rtl::Reference<SdrObject> SdrCaptionObj::CloneSdrObject(SdrModel& rTargetModel) const
{
    return new SdrCaptionObj(rTargetModel, *this);
}

OUString SdrCaptionObj::TakeObjNameSingul() const
{
    OUString sName(SvxResId(STR_ObjNameSingulCAPTION));

    OUString aName(GetName());
    if (!aName.isEmpty())
        sName += " '" + aName + "'";

    return sName;
}

OUString SdrCaptionObj::TakeObjNamePlural() const
{
    return SvxResId(STR_ObjNamePluralCAPTION);
}

basegfx::B2DPolyPolygon SdrCaptionObj::TakeXorPoly() const
{
    basegfx::B2DPolyPolygon aPolyPoly(SdrRectObj::TakeXorPoly());
    aPolyPoly.append(maTailPoly.getB2DPolygon());

    return aPolyPoly;
}

sal_uInt32 SdrCaptionObj::GetHdlCount() const
{
    sal_uInt32 nCount1(SdrRectObj::GetHdlCount());
    // Currently only dragging the tail's end is implemented.
    return nCount1 + 1;
}

void SdrCaptionObj::AddToHdlList(SdrHdlList& rHdlList) const
{
    SdrRectObj::AddToHdlList(rHdlList);
    // Currently only dragging the tail's end is implemented.
    std::unique_ptr<SdrHdl> pHdl(new SdrHdl(maTailPoly.GetPoint(0), SdrHdlKind::Poly));
    pHdl->SetPolyNum(1);
    pHdl->SetPointNum(0);
    rHdlList.AddHdl(std::move(pHdl));
}

bool SdrCaptionObj::hasSpecialDrag() const
{
    return true;
}

bool SdrCaptionObj::beginSpecialDrag(SdrDragStat& rDrag) const
{
    const SdrHdl* pHdl = rDrag.GetHdl();
    rDrag.SetEndDragChangesAttributes(true);
    rDrag.SetEndDragChangesGeoAndAttributes(true);

    if(pHdl && 0 == pHdl->GetPolyNum())
    {
        return SdrRectObj::beginSpecialDrag(rDrag);
    }
    else
    {
        rDrag.SetOrtho8Possible();

        if(!pHdl)
        {
            if (m_bMovProt)
                return false;

            rDrag.SetNoSnap();
            rDrag.SetActionRect(getRectangle());

            Point aHit(rDrag.GetStart());

            if(rDrag.GetPageView() && SdrObjectPrimitiveHit(*this, aHit, {0, 0}, *rDrag.GetPageView(), nullptr, false))
            {
                return true;
            }
        }
        else
        {
            if((1 == pHdl->GetPolyNum()) && (0 == pHdl->GetPointNum()))
                return true;
        }
    }

    return false;
}

bool SdrCaptionObj::applySpecialDrag(SdrDragStat& rDrag)
{
    const SdrHdl* pHdl = rDrag.GetHdl();

    if(pHdl && 0 == pHdl->GetPolyNum())
    {
        const bool bRet(SdrRectObj::applySpecialDrag(rDrag));
        ImpRecalcTail();
        ActionChanged();

        return bRet;
    }
    else
    {
        Point aDelta(rDrag.GetNow()-rDrag.GetStart());

        if(!pHdl)
        {
            moveRectangle(aDelta.X(), aDelta.Y());
        }
        else
        {
            maTailPoly[0] += aDelta;
        }

        ImpRecalcTail();
        ActionChanged();

        return true;
    }
}

OUString SdrCaptionObj::getSpecialDragComment(const SdrDragStat& rDrag) const
{
    const bool bCreateComment(rDrag.GetView() && this == rDrag.GetView()->GetCreateObj());

    if(bCreateComment)
    {
        return OUString();
    }
    else
    {
        const SdrHdl* pHdl = rDrag.GetHdl();

        if(pHdl && 0 == pHdl->GetPolyNum())
        {
            return SdrRectObj::getSpecialDragComment(rDrag);
        }
        else
        {
            if(!pHdl)
            {
                return ImpGetDescriptionStr(STR_DragCaptFram);
            }
            else
            {
                return ImpGetDescriptionStr(STR_DragCaptTail);
            }
        }
    }
}


void SdrCaptionObj::ImpGetCaptParams(ImpCaptParams& rPara) const
{
    const SfxItemSet& rSet = GetObjectItemSet();
    rPara.eType      =rSet.Get(SDRATTR_CAPTIONTYPE      ).GetValue();
    rPara.bFixedAngle=rSet.Get(SDRATTR_CAPTIONFIXEDANGLE).GetValue();
    rPara.nGap       =static_cast<const SdrCaptionGapItem&>       (rSet.Get(SDRATTR_CAPTIONGAP       )).GetValue();
    rPara.eEscDir    =rSet.Get(SDRATTR_CAPTIONESCDIR    ).GetValue();
    rPara.bEscRel    =rSet.Get(SDRATTR_CAPTIONESCISREL  ).GetValue();
    rPara.nEscRel    =rSet.Get(SDRATTR_CAPTIONESCREL    ).GetValue();
    rPara.nEscAbs    =rSet.Get(SDRATTR_CAPTIONESCABS    ).GetValue();
    rPara.nLineLen   =rSet.Get(SDRATTR_CAPTIONLINELEN   ).GetValue();
    rPara.bFitLineLen=rSet.Get(SDRATTR_CAPTIONFITLINELEN).GetValue();
}

void SdrCaptionObj::ImpRecalcTail()
{
    ImpCaptParams aPara;
    ImpGetCaptParams(aPara);
    ImpCalcTail(aPara, maTailPoly, getRectangle());
    SetBoundAndSnapRectsDirty();
    SetXPolyDirty();
}

// #i35971#
// SdrCaptionObj::ImpCalcTail1 does move the object(!). What a hack.
// I really wonder why this had not triggered problems before. I am
// sure there are some places where SetTailPos() is called at least
// twice or SetSnapRect after it again just to work around this.
// Changed this method to not do that.
// Also found why this has been done: For interactive dragging of the
// tail end pos for SdrCaptionType::Type1. This sure was the simplest method
// to achieve this, at the cost of making a whole group of const methods
// of this object implicitly change the object's position.
void SdrCaptionObj::ImpCalcTail1(const ImpCaptParams& rPara, tools::Polygon& rPoly, tools::Rectangle const & rRect)
{
    tools::Polygon aPol(2);
    Point aTl(rPoly[0]);

    aPol[0] = aTl;
    aPol[1] = aTl;

    EscDir eEscDir;
    Point aEscPos;

    rPara.CalcEscPos(aTl, rRect, aEscPos, eEscDir);
    aPol[1] = aEscPos;

    if(eEscDir==LKS || eEscDir==RTS)
    {
        aPol[0].setX( aEscPos.X() );
    }
    else
    {
        aPol[0].setY( aEscPos.Y() );
    }

    rPoly = std::move(aPol);
}

void SdrCaptionObj::ImpCalcTail2(const ImpCaptParams& rPara, tools::Polygon& rPoly, tools::Rectangle const & rRect)
{ // Gap/EscDir/EscPos/Angle
    tools::Polygon aPol(2);
    Point aTl(rPoly[0]);
    aPol[0]=aTl;

    EscDir eEscDir;
    Point aEscPos;
    rPara.CalcEscPos(aTl,rRect,aEscPos,eEscDir);
    aPol[1]=aEscPos;

    if (!rPara.bFixedAngle) {
        // TODO: Implementation missing.
    }
    rPoly = std::move(aPol);
}

void SdrCaptionObj::ImpCalcTail3(const ImpCaptParams& rPara, tools::Polygon& rPoly, tools::Rectangle const & rRect)
{ // Gap/EscDir/EscPos/Angle/LineLen
    tools::Polygon aPol(3);
    Point aTl(rPoly[0]);
    aPol[0]=aTl;

    EscDir eEscDir;
    Point aEscPos;
    rPara.CalcEscPos(aTl,rRect,aEscPos,eEscDir);
    aPol[1]=aEscPos;
    aPol[2]=aEscPos;

    if (eEscDir==LKS || eEscDir==RTS) {
        if (rPara.bFitLineLen) {
            aPol[1].setX((aTl.X()+aEscPos.X())/2 );
        } else {
            if (eEscDir==LKS) aPol[1].AdjustX( -(rPara.nLineLen) );
            else aPol[1].AdjustX(rPara.nLineLen );
        }
    } else {
        if (rPara.bFitLineLen) {
            aPol[1].setY((aTl.Y()+aEscPos.Y())/2 );
        } else {
            if (eEscDir==OBN) aPol[1].AdjustY( -(rPara.nLineLen) );
            else aPol[1].AdjustY(rPara.nLineLen );
        }
    }
    if (!rPara.bFixedAngle) {
        // TODO: Implementation missing.
    }
    rPoly = std::move(aPol);
}

void SdrCaptionObj::ImpCalcTail(const ImpCaptParams& rPara, tools::Polygon& rPoly, tools::Rectangle const & rRect)
{
    switch (rPara.eType) {
        case SdrCaptionType::Type1: ImpCalcTail1(rPara,rPoly,rRect); break;
        case SdrCaptionType::Type2: ImpCalcTail2(rPara,rPoly,rRect); break;
        case SdrCaptionType::Type3: ImpCalcTail3(rPara,rPoly,rRect); break;
        case SdrCaptionType::Type4: ImpCalcTail3(rPara,rPoly,rRect); break;
    }
}

bool SdrCaptionObj::BegCreate(SdrDragStat& rStat)
{
    if (getRectangle().IsEmpty())
        return false; // Create currently only works with the given Rect

    ImpCaptParams aPara;
    ImpGetCaptParams(aPara);
    moveRectanglePosition(rStat.GetNow().X(), rStat.GetNow().Y());
    maTailPoly[0]=rStat.GetStart();
    ImpCalcTail(aPara,maTailPoly, getRectangle());
    rStat.SetActionRect(getRectangle());
    return true;
}

bool SdrCaptionObj::MovCreate(SdrDragStat& rStat)
{
    ImpCaptParams aPara;
    ImpGetCaptParams(aPara);
    moveRectanglePosition(rStat.GetNow().X(), rStat.GetNow().Y());
    ImpCalcTail(aPara,maTailPoly, getRectangle());
    rStat.SetActionRect(getRectangle());
    SetBoundRectDirty();
    m_bSnapRectDirty=true;
    return true;
}

bool SdrCaptionObj::EndCreate(SdrDragStat& rStat, SdrCreateCmd eCmd)
{
    ImpCaptParams aPara;
    ImpGetCaptParams(aPara);
    moveRectanglePosition(rStat.GetNow().X(), rStat.GetNow().Y());
    ImpCalcTail(aPara,maTailPoly, getRectangle());
    SetBoundAndSnapRectsDirty();
    return (eCmd==SdrCreateCmd::ForceEnd || rStat.GetPointCount()>=2);
}

bool SdrCaptionObj::BckCreate(SdrDragStat& /*rStat*/)
{
    return false;
}

void SdrCaptionObj::BrkCreate(SdrDragStat& /*rStat*/)
{
}

basegfx::B2DPolyPolygon SdrCaptionObj::TakeCreatePoly(const SdrDragStat& /*rDrag*/) const
{
    basegfx::B2DPolyPolygon aRetval;
    const basegfx::B2DRange aRange =vcl::unotools::b2DRectangleFromRectangle(getRectangle());
    aRetval.append(basegfx::utils::createPolygonFromRect(aRange));
    aRetval.append(maTailPoly.getB2DPolygon());
    return aRetval;
}

PointerStyle SdrCaptionObj::GetCreatePointer() const
{
    return PointerStyle::DrawCaption;
}

void SdrCaptionObj::NbcMove(const Size& rSiz)
{
    SdrRectObj::NbcMove(rSiz);
    MovePoly(maTailPoly,rSiz);
    if(mbFixedTail)
        SetTailPos(GetFixedTailPos());
}

void SdrCaptionObj::NbcResize(const Point& rRef, const Fraction& xFact, const Fraction& yFact)
{
    SdrRectObj::NbcResize(rRef,xFact,yFact);
    ResizePoly(maTailPoly,rRef,xFact,yFact);
    ImpRecalcTail();
    if(mbFixedTail)
        SetTailPos(GetFixedTailPos());
}

void SdrCaptionObj::NbcSetRelativePos(const Point& rPnt)
{
    Point aRelPos0(maTailPoly.GetPoint(0)-m_aAnchor);
    Size aSiz(rPnt.X()-aRelPos0.X(),rPnt.Y()-aRelPos0.Y());
    NbcMove(aSiz); // This also calls SetRectsDirty()
}

Point SdrCaptionObj::GetRelativePos() const
{
    return maTailPoly.GetPoint(0)-m_aAnchor;
}

const tools::Rectangle& SdrCaptionObj::GetLogicRect() const
{
    return getRectangle();
}

void SdrCaptionObj::NbcSetLogicRect(const tools::Rectangle& rRect, bool bAdaptTextMinSize)
{
    SdrRectObj::NbcSetLogicRect(rRect, bAdaptTextMinSize);
    ImpRecalcTail();
}

const Point& SdrCaptionObj::GetTailPos() const
{
    return maTailPoly[0];
}

void SdrCaptionObj::SetTailPos(const Point& rPos)
{
    if (maTailPoly.GetSize()==0 || maTailPoly[0]!=rPos) {
        tools::Rectangle aBoundRect0; if (m_pUserCall!=nullptr) aBoundRect0=GetLastBoundRect();
        NbcSetTailPos(rPos);
        SetChanged();
        BroadcastObjectChange();
        SendUserCall(SdrUserCallType::Resize,aBoundRect0);
    }
}

void SdrCaptionObj::NbcSetTailPos(const Point& rPos)
{
    maTailPoly[0]=rPos;
    ImpRecalcTail();
}

sal_uInt32 SdrCaptionObj::GetSnapPointCount() const
{
    // TODO: Implementation missing.
    return 0;
}

Point SdrCaptionObj::GetSnapPoint(sal_uInt32 /*i*/) const
{
    // TODO: Implementation missing.
    return Point(0,0);
}

void SdrCaptionObj::Notify(SfxBroadcaster& rBC, const SfxHint& rHint)
{
    SdrRectObj::Notify(rBC,rHint);
    ImpRecalcTail();
}

std::unique_ptr<SdrObjGeoData> SdrCaptionObj::NewGeoData() const
{
    return std::make_unique<SdrCaptObjGeoData>();
}

void SdrCaptionObj::SaveGeoData(SdrObjGeoData& rGeo) const
{
    SdrRectObj::SaveGeoData(rGeo);
    SdrCaptObjGeoData& rCGeo=static_cast<SdrCaptObjGeoData&>(rGeo);
    rCGeo.aTailPoly=maTailPoly;
}

void SdrCaptionObj::RestoreGeoData(const SdrObjGeoData& rGeo)
{
    SdrRectObj::RestoreGeoData(rGeo);
    const SdrCaptObjGeoData& rCGeo=static_cast<const SdrCaptObjGeoData&>(rGeo);
    maTailPoly=rCGeo.aTailPoly;
}

rtl::Reference<SdrObject> SdrCaptionObj::DoConvertToPolyObj(bool bBezier, bool bAddText) const
{
    rtl::Reference<SdrObject> pRect = SdrRectObj::DoConvertToPolyObj(bBezier, bAddText);
    rtl::Reference<SdrObject> pTail = ImpConvertMakeObj(basegfx::B2DPolyPolygon(maTailPoly.getB2DPolygon()), false, bBezier);
    rtl::Reference<SdrObject> pRet;
    if (pTail && !pRect)
        pRet = std::move(pTail);
    else if (pRect && !pTail)
        pRet = std::move(pRect);
    else if (pTail && pRect)
    {
        if (pTail->GetSubList())
        {
            pTail->GetSubList()->NbcInsertObject(pRect.get());
            pRet = std::move(pTail);
        }
        else if (pRect->GetSubList())
        {
            pRect->GetSubList()->NbcInsertObject(pTail.get(),0);
            pRet = std::move(pRect);
        }
        else
        {
            rtl::Reference<SdrObjGroup> pGrp = new SdrObjGroup(getSdrModelFromSdrObject());
            pGrp->GetSubList()->NbcInsertObject(pRect.get());
            pGrp->GetSubList()->NbcInsertObject(pTail.get(),0);
            pRet = pGrp;
        }
    }
    return pRet;
}

namespace {

void handleNegativeScale(basegfx::B2DTuple & scale, double * rotate) {
    assert(rotate != nullptr);

    // #i75086# Old DrawingLayer (GeoStat and geometry) does not support holding negative scalings
    // in X and Y which equal a 180 degree rotation. Recognize it and react accordingly
    if(scale.getX() < 0.0 && scale.getY() < 0.0)
    {
        scale.setX(fabs(scale.getX()));
        scale.setY(fabs(scale.getY()));
        *rotate = fmod(*rotate + M_PI, 2 * M_PI);
    }
}

}

// #i32599#
// Add own implementation for TRSetBaseGeometry to handle TailPos over changes.
void SdrCaptionObj::TRSetBaseGeometry(const basegfx::B2DHomMatrix& rMatrix, const basegfx::B2DPolyPolygon& /*rPolyPolygon*/)
{
    // break up matrix
    basegfx::B2DTuple aScale;
    basegfx::B2DTuple aTranslate;
    double fRotate, fShearX;
    rMatrix.decompose(aScale, aTranslate, fRotate, fShearX);

    handleNegativeScale(aScale, &fRotate);

    // if anchor is used, make position relative to it
    if(getSdrModelFromSdrObject().IsWriter())
    {
        if(GetAnchorPos().X() || GetAnchorPos().Y())
        {
            aTranslate += basegfx::B2DTuple(GetAnchorPos().X(), GetAnchorPos().Y());
        }
    }

    // build BaseRect
    Point aPoint(basegfx::fround<tools::Long>(aTranslate.getX()),
                 basegfx::fround<tools::Long>(aTranslate.getY()));
    tools::Rectangle aBaseRect(aPoint, Size(basegfx::fround<tools::Long>(aScale.getX()),
                                            basegfx::fround<tools::Long>(aScale.getY())));

    // set BaseRect, but rescue TailPos over this call
    const Point aTailPoint = GetTailPos();
    SetSnapRect(aBaseRect);
    SetTailPos(aTailPoint);
    ImpRecalcTail();
}

// geometry access
basegfx::B2DPolygon SdrCaptionObj::getTailPolygon() const
{
    return maTailPoly.getB2DPolygon();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
