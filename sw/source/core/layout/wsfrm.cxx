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

#include <config_wasm_strip.h>

#include <hints.hxx>
#include <osl/diagnose.h>
#include <o3tl/safeint.hxx>
#include <svl/itemiter.hxx>
#include <editeng/brushitem.hxx>
#include <sfx2/viewsh.hxx>
#include <fmtornt.hxx>
#include <pagefrm.hxx>
#include <section.hxx>
#include <rootfrm.hxx>
#include <anchoreddrawobject.hxx>
#include <fmtanchr.hxx>
#include <viewimp.hxx>
#include <viewopt.hxx>
#include <IDocumentSettingAccess.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentRedlineAccess.hxx>
#include <redline.hxx>
#include <docsh.hxx>
#include <ftninfo.hxx>
#include <ftnidx.hxx>
#include <fmtclbl.hxx>
#include <fmtfsize.hxx>
#include <fmtpdsc.hxx>
#include <txtftn.hxx>
#include <fmtftn.hxx>
#include <fmtsrnd.hxx>
#include <fmtcntnt.hxx>
#include <ftnfrm.hxx>
#include <tabfrm.hxx>
#include <rowfrm.hxx>
#include <flyfrm.hxx>
#include <sectfrm.hxx>
#include <fmtclds.hxx>
#include <txtfrm.hxx>
#include <bodyfrm.hxx>
#include <cellfrm.hxx>
#include <dbg_lay.hxx>
#include <editeng/frmdiritem.hxx>
#include <sortedobjs.hxx>
#include <frmatr.hxx>
#include <frmtool.hxx>
#include <layact.hxx>
#include <ndtxt.hxx>
#include <swtable.hxx>
#include <view.hxx>

// RotateFlyFrame3
#include <basegfx/matrix/b2dhommatrixtools.hxx>

using namespace ::com::sun::star;

SwFrameAreaDefinition::SwFrameAreaDefinition()
:   mnFrameId(SwFrameAreaDefinition::snLastFrameId++),
    mbFrameAreaPositionValid(false),
    mbFrameAreaSizeValid(false),
    mbFramePrintAreaValid(false)
{
}

SwFrameAreaDefinition::~SwFrameAreaDefinition()
{
}

void SwFrameAreaDefinition::setFrameAreaPositionValid(bool bNew)
{
    if(mbFrameAreaPositionValid != bNew)
    {
        mbFrameAreaPositionValid = bNew;
    }
}

void SwFrameAreaDefinition::setFrameAreaSizeValid(bool bNew)
{
    if(mbFrameAreaSizeValid != bNew)
    {
        mbFrameAreaSizeValid = bNew;
    }
}

void SwFrameAreaDefinition::setFramePrintAreaValid(bool bNew)
{
    if(mbFramePrintAreaValid != bNew)
    {
        mbFramePrintAreaValid = bNew;
    }
}

SwFrameAreaDefinition::FrameAreaWriteAccess::~FrameAreaWriteAccess()
{
    if(mrTarget.maFrameArea != *this)
    {
        mrTarget.maFrameArea = *this;
    }
}

SwFrameAreaDefinition::FramePrintAreaWriteAccess::~FramePrintAreaWriteAccess()
{
    if(mrTarget.maFramePrintArea != *this)
    {
        mrTarget.maFramePrintArea = *this;
    }
}

// RotateFlyFrame3 - Support for Transformations
basegfx::B2DHomMatrix SwFrameAreaDefinition::getFrameAreaTransformation() const
{
    // default implementation hands out FrameArea (outer frame)
    const SwRect& rFrameArea(getFrameArea());

    return basegfx::utils::createScaleTranslateB2DHomMatrix(
        rFrameArea.Width(), rFrameArea.Height(),
        rFrameArea.Left(), rFrameArea.Top());
}

basegfx::B2DHomMatrix SwFrameAreaDefinition::getFramePrintAreaTransformation() const
{
    // default implementation hands out FramePrintArea (outer frame)
    // Take into account that FramePrintArea is relative to FrameArea
    const SwRect& rFrameArea(getFrameArea());
    const SwRect& rFramePrintArea(getFramePrintArea());

    return basegfx::utils::createScaleTranslateB2DHomMatrix(
        rFramePrintArea.Width(), rFramePrintArea.Height(),
        rFramePrintArea.Left() + rFrameArea.Left(),
        rFramePrintArea.Top() + rFrameArea.Top());
}

void SwFrameAreaDefinition::transform_translate(const Point& rOffset)
{
    // RotateFlyFrame3: default is to change the FrameArea, FramePrintArea needs no
    // change since it is relative to FrameArea
    SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);

    if (aFrm.Pos().X() != FAR_AWAY)
    {
        aFrm.Pos().AdjustX(rOffset.X() );
    }

    if (aFrm.Pos().Y() != FAR_AWAY)
    {
        aFrm.Pos().AdjustY(rOffset.Y() );
    }
}

SwRect TransformableSwFrame::getUntransformedFrameArea() const
{
    const basegfx::B2DHomMatrix& rSource(getLocalFrameAreaTransformation());

    if(rSource.isIdentity())
    {
        return mrSwFrameAreaDefinition.getFrameArea();
    }
    else
    {
        basegfx::B2DVector aScale, aTranslate;
        double fRotate, fShearX;
        rSource.decompose(aScale, aTranslate, fRotate, fShearX);
        const basegfx::B2DPoint aCenter(rSource * basegfx::B2DPoint(0.5, 0.5));
        const basegfx::B2DVector aAbsScale(basegfx::absolute(aScale));

        return SwRect(
            basegfx::fround<tools::Long>(aCenter.getX() - (0.5 * aAbsScale.getX())),
            basegfx::fround<tools::Long>(aCenter.getY() - (0.5 * aAbsScale.getY())),
            basegfx::fround<tools::Long>(aAbsScale.getX()),
            basegfx::fround<tools::Long>(aAbsScale.getY()));
    }
}

SwRect TransformableSwFrame::getUntransformedFramePrintArea() const
{
    const basegfx::B2DHomMatrix& rSource(getLocalFramePrintAreaTransformation());

    if(rSource.isIdentity())
    {
        return mrSwFrameAreaDefinition.getFramePrintArea();
    }
    else
    {
        basegfx::B2DVector aScale, aTranslate;
        double fRotate, fShearX;
        rSource.decompose(aScale, aTranslate, fRotate, fShearX);
        const basegfx::B2DPoint aCenter(rSource * basegfx::B2DPoint(0.5, 0.5));
        const basegfx::B2DVector aAbsScale(basegfx::absolute(aScale));
        const SwRect aUntransformedFrameArea(getUntransformedFrameArea());

        return SwRect(
            basegfx::fround<tools::Long>(aCenter.getX() - (0.5 * aAbsScale.getX())) - aUntransformedFrameArea.Left(),
            basegfx::fround<tools::Long>(aCenter.getY() - (0.5 * aAbsScale.getY())) - aUntransformedFrameArea.Top(),
            basegfx::fround<tools::Long>(aAbsScale.getX()),
            basegfx::fround<tools::Long>(aAbsScale.getY()));
    }
}

void TransformableSwFrame::createFrameAreaTransformations(
    double fRotation,
    const basegfx::B2DPoint& rCenter)
{
    const basegfx::B2DHomMatrix aRotateAroundCenter(
        basegfx::utils::createRotateAroundPoint(
            rCenter.getX(),
            rCenter.getY(),
            fRotation));
    const SwRect& rFrameArea(mrSwFrameAreaDefinition.getFrameArea());
    const SwRect& rFramePrintArea(mrSwFrameAreaDefinition.getFramePrintArea());

    maFrameAreaTransformation = aRotateAroundCenter * basegfx::utils::createScaleTranslateB2DHomMatrix(
        rFrameArea.Width(), rFrameArea.Height(),
        rFrameArea.Left(), rFrameArea.Top());
    maFramePrintAreaTransformation = aRotateAroundCenter * basegfx::utils::createScaleTranslateB2DHomMatrix(
        rFramePrintArea.Width(), rFramePrintArea.Height(),
        rFramePrintArea.Left() + rFrameArea.Left(), rFramePrintArea.Top() + rFrameArea.Top());
}

void TransformableSwFrame::adaptFrameAreasToTransformations()
{
    if(!getLocalFrameAreaTransformation().isIdentity())
    {
        basegfx::B2DRange aRangeFrameArea(0.0, 0.0, 1.0, 1.0);
        aRangeFrameArea.transform(getLocalFrameAreaTransformation());
        const SwRect aNewFrm(
            basegfx::fround<tools::Long>(aRangeFrameArea.getMinX()), basegfx::fround<tools::Long>(aRangeFrameArea.getMinY()),
            basegfx::fround<tools::Long>(aRangeFrameArea.getWidth()), basegfx::fround<tools::Long>(aRangeFrameArea.getHeight()));

        if(aNewFrm != mrSwFrameAreaDefinition.getFrameArea())
        {
            SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(mrSwFrameAreaDefinition);
            aFrm.setSwRect(aNewFrm);
        }
    }

    if(getLocalFramePrintAreaTransformation().isIdentity())
        return;

    basegfx::B2DRange aRangeFramePrintArea(0.0, 0.0, 1.0, 1.0);
    aRangeFramePrintArea.transform(getLocalFramePrintAreaTransformation());
    const SwRect aNewPrt(
        basegfx::fround<tools::Long>(aRangeFramePrintArea.getMinX()) - mrSwFrameAreaDefinition.getFrameArea().Left(),
        basegfx::fround<tools::Long>(aRangeFramePrintArea.getMinY()) - mrSwFrameAreaDefinition.getFrameArea().Top(),
        basegfx::fround<tools::Long>(aRangeFramePrintArea.getWidth()),
        basegfx::fround<tools::Long>(aRangeFramePrintArea.getHeight()));

    if(aNewPrt != mrSwFrameAreaDefinition.getFramePrintArea())
    {
        SwFrameAreaDefinition::FramePrintAreaWriteAccess aPrt(mrSwFrameAreaDefinition);
        aPrt.setSwRect(aNewPrt);
    }
}

void TransformableSwFrame::restoreFrameAreas()
{
    // This can be done fully based on the Transformations currently
    // set, so use this. Only needed when transformation *is* used
    if(!getLocalFrameAreaTransformation().isIdentity())
    {
        SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(mrSwFrameAreaDefinition);
        aFrm.setSwRect(getUntransformedFrameArea());
    }

    if(!getLocalFramePrintAreaTransformation().isIdentity())
    {
        SwFrameAreaDefinition::FramePrintAreaWriteAccess aPrt(mrSwFrameAreaDefinition);
        aPrt.setSwRect(getUntransformedFramePrintArea());
    }
}

// transform by given B2DHomMatrix
void TransformableSwFrame::transform(const basegfx::B2DHomMatrix& aTransform)
{
    maFrameAreaTransformation *= aTransform;
    maFramePrintAreaTransformation *= aTransform;
}

void SwFrame::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("infos"));
    dumpInfosAsXml(pWriter);
    (void)xmlTextWriterEndElement(pWriter);

    if ( m_pDrawObjs && m_pDrawObjs->size() > 0 )
    {
        (void)xmlTextWriterStartElement( pWriter, BAD_CAST( "anchored" ) );

        for (SwAnchoredObject* pObject : *m_pDrawObjs)
        {
            pObject->dumpAsXml( pWriter );
        }

        (void)xmlTextWriterEndElement( pWriter );
    }

    dumpChildrenAsXml(pWriter);
}

void SwFrame::dumpChildrenAsXml( xmlTextWriterPtr writer ) const
{
    const SwFrame *pFrame = GetLower(  );
    for ( ; pFrame != nullptr; pFrame = pFrame->GetNext(  ) )
    {
        pFrame->dumpAsXml( writer );
    }
}

SwFrame::SwFrame( sw::BroadcastingModify *pMod, SwFrame* pSib )
:   SwClient( pMod ),
    mpRoot( pSib ? pSib->getRootFrame() : nullptr ),
    mpUpper(nullptr),
    mpNext(nullptr),
    mpPrev(nullptr),
    mnFrameType(SwFrameType::None),
    mbInDtor(false),
    mbInvalidR2L(true),
    mbDerivedR2L(false),
    mbRightToLeft(false),
    mbInvalidVert(true),
    mbDerivedVert(false),
    mbVertical(false),
    mbVertLR(false),
    mbVertLRBT(false),
    mbValidLineNum(false),
    mbFixSize(false),
    mbCompletePaint(true),
    mbRetouche(false),
    mbInfInvalid(true),
    mbInfBody( false ),
    mbInfTab ( false ),
    mbInfFly ( false ),
    mbInfFootnote ( false ),
    mbInfSct ( false ),
    mbColLocked(false),
    m_isInDestroy(false),
    mnForbidDelete(0)
{
    OSL_ENSURE( pMod, "No frame format given." );
}

const IDocumentDrawModelAccess& SwFrame::getIDocumentDrawModelAccess()
{
    return GetUpper()->GetFormat()->getIDocumentDrawModelAccess();
}

bool SwFrame::KnowsFormat( const SwFormat& rFormat ) const
{
    return GetRegisteredIn() == &rFormat;
}

void SwFrame::RegisterToFormat( SwFormat& rFormat )
{
    rFormat.Add(*this);
}

void SwFrame::CheckDir( SvxFrameDirection nDir, bool bVert, bool bOnlyBiDi, bool bBrowse )
{
    if( SvxFrameDirection::Environment == nDir || ( bVert && bOnlyBiDi ) )
    {
        mbDerivedVert = true;
        if( SvxFrameDirection::Environment == nDir )
            mbDerivedR2L = true;
        SetDirFlags( bVert );
    }
    else if( bVert )
    {
        mbInvalidVert = false;
        if( SvxFrameDirection::Horizontal_LR_TB == nDir || SvxFrameDirection::Horizontal_RL_TB == nDir
            || bBrowse )
        {
            mbVertical = false;
            mbVertLR = false;
            mbVertLRBT = false;
        }
        else
        {
            mbVertical = true;
            if(SvxFrameDirection::Vertical_RL_TB == nDir)
            {
                mbVertLR = false;
                mbVertLRBT = false;
            }
            else if(SvxFrameDirection::Vertical_LR_TB==nDir)
            {
                mbVertLR = true;
                mbVertLRBT = false;
            }
            else if (nDir == SvxFrameDirection::Vertical_LR_BT)
            {
                mbVertLR = true;
                mbVertLRBT = true;
            }
            else if (nDir == SvxFrameDirection::Vertical_RL_TB90)
            {
                // not yet implemented, render as RL_TB
                mbVertLR = false;
                mbVertLRBT = false;
            }
        }
    }
    else
    {
        mbInvalidR2L = false;
        if( SvxFrameDirection::Horizontal_RL_TB == nDir )
            mbRightToLeft = true;
        else
            mbRightToLeft = false;
    }
}

void SwFrame::CheckDirection( bool bVert )
{
    if( bVert )
    {
        if( !IsHeaderFrame() && !IsFooterFrame() )
        {
            mbDerivedVert = true;
            SetDirFlags( bVert );
        }
    }
    else
    {
        mbDerivedR2L = true;
        SetDirFlags( bVert );
    }
}

void SwSectionFrame::CheckDirection( bool bVert )
{
    const SwFrameFormat* pFormat = GetFormat();
    if( pFormat )
    {
        const SwViewShell *pSh = getRootFrame()->GetCurrShell();
        const bool bBrowseMode = pSh && pSh->GetViewOptions()->getBrowseMode();
        CheckDir(pFormat->GetFormatAttr(RES_FRAMEDIR).GetValue(),
                    bVert, true, bBrowseMode );
    }
    else
        SwFrame::CheckDirection( bVert );
}

void SwFlyFrame::CheckDirection( bool bVert )
{
    const SwFrameFormat* pFormat = GetFormat();
    if( pFormat )
    {
        const SwViewShell *pSh = getRootFrame()->GetCurrShell();
        const bool bBrowseMode = pSh && pSh->GetViewOptions()->getBrowseMode();
        CheckDir(pFormat->GetFormatAttr(RES_FRAMEDIR).GetValue(),
                    bVert, false, bBrowseMode );
    }
    else
        SwFrame::CheckDirection( bVert );
}

void SwTabFrame::CheckDirection( bool bVert )
{
    const SwFrameFormat* pFormat = GetFormat();
    if( pFormat )
    {
        const SwViewShell *pSh = getRootFrame()->GetCurrShell();
        const bool bBrowseMode = pSh && pSh->GetViewOptions()->getBrowseMode();
        CheckDir(pFormat->GetFormatAttr(RES_FRAMEDIR).GetValue(),
                    bVert, true, bBrowseMode );
    }
    else
        SwFrame::CheckDirection( bVert );
}

void SwCellFrame::CheckDirection( bool bVert )
{
    const SwFrameFormat* pFormat = GetFormat();
    const SvxFrameDirectionItem* pFrameDirItem;
    // Check if the item is set, before actually
    // using it. Otherwise the dynamic pool default is used, which may be set
    // to LTR in case of OOo 1.0 documents.
    if( pFormat && (pFrameDirItem = pFormat->GetItemIfSet( RES_FRAMEDIR ) ) )
    {
        const SwViewShell *pSh = getRootFrame()->GetCurrShell();
        const bool bBrowseMode = pSh && pSh->GetViewOptions()->getBrowseMode();
        CheckDir( pFrameDirItem->GetValue(), bVert, false, bBrowseMode );
    }
    else
        SwFrame::CheckDirection( bVert );
}

void SwTextFrame::CheckDirection( bool bVert )
{
    const SwViewShell *pSh = getRootFrame()->GetCurrShell();
    const bool bBrowseMode = pSh && pSh->GetViewOptions()->getBrowseMode();
    CheckDir(GetTextNodeForParaProps()->GetSwAttrSet().GetFrameDir().GetValue(),
             bVert, true, bBrowseMode);
}

void SwFrame::SwClientNotify(const SwModify&, const SfxHint& rHint)
{
    if (rHint.GetId() != SfxHintId::SwLegacyModify && rHint.GetId() != SfxHintId::SwFormatChange
        && rHint.GetId() != SfxHintId::SwAttrSetChange)
        return;

    SwFrameInvFlags eInvFlags = SwFrameInvFlags::NONE;
    if (rHint.GetId() == SfxHintId::SwLegacyModify)
    {
        auto pLegacy = static_cast<const sw::LegacyModifyHint*>(&rHint);
        UpdateAttrFrame(pLegacy->m_pOld, pLegacy->m_pNew, eInvFlags);
    }
    else if (rHint.GetId() == SfxHintId::SwAttrSetChange)
    {
        auto pChangeHint = static_cast<const sw::AttrSetChangeHint*>(&rHint);
        if(pChangeHint->m_pOld && pChangeHint->m_pNew)
        {
            SfxItemIter aNIter(*pChangeHint->m_pNew->GetChgSet());
            SfxItemIter aOIter(*pChangeHint->m_pOld->GetChgSet());
            const SfxPoolItem* pNItem = aNIter.GetCurItem();
            const SfxPoolItem* pOItem = aOIter.GetCurItem();
            do
            {
                UpdateAttrFrame(pOItem, pNItem, eInvFlags);
                pNItem = aNIter.NextItem();
                pOItem = aOIter.NextItem();
            } while (pNItem);
        }
    }
    else // rHint.GetId() == SfxHintId::SwFormatChange
    {
        UpdateAttrFrameForFormatChange(eInvFlags);
    }

    if(eInvFlags == SwFrameInvFlags::NONE)
        return;

    SwPageFrame* pPage = FindPageFrame();
    InvalidatePage(pPage);
    if(eInvFlags & SwFrameInvFlags::InvalidatePrt)
    {
        InvalidatePrt_();
        if(!GetPrev() && IsTabFrame() && IsInSct())
            FindSctFrame()->InvalidatePrt_();
    }
    if(eInvFlags & SwFrameInvFlags::InvalidateSize)
        InvalidateSize_();
    if(eInvFlags & SwFrameInvFlags::InvalidatePos)
        InvalidatePos_();
    if(eInvFlags & SwFrameInvFlags::SetCompletePaint)
        SetCompletePaint();
    SwFrame *pNxt;
    if (eInvFlags & (SwFrameInvFlags::NextInvalidatePos | SwFrameInvFlags::NextSetCompletePaint)
        && nullptr != (pNxt = GetNext()))
    {
        pNxt->InvalidatePage(pPage);
        if(eInvFlags & SwFrameInvFlags::NextInvalidatePos)
            pNxt->InvalidatePos_();
        if(eInvFlags & SwFrameInvFlags::NextSetCompletePaint)
            pNxt->SetCompletePaint();
    }
}

void SwFrame::UpdateAttrFrame( const SfxPoolItem *pOld, const SfxPoolItem *pNew,
                         SwFrameInvFlags &rInvFlags )
{
    sal_uInt16 nWhich = pOld ? pOld->Which() : pNew ? pNew->Which() : 0;
    switch( nWhich )
    {
        case RES_BOX:
        case RES_SHADOW:
            Prepare( PrepareHint::FixSizeChanged );
            [[fallthrough]];
        case RES_MARGIN_FIRSTLINE:
        case RES_MARGIN_TEXTLEFT:
        case RES_MARGIN_RIGHT:
        case RES_LR_SPACE:
        case RES_UL_SPACE:
        case RES_RTL_GUTTER:
            rInvFlags |= SwFrameInvFlags::InvalidatePrt | SwFrameInvFlags::InvalidateSize
                         | SwFrameInvFlags::SetCompletePaint;
            break;

        case RES_HEADER_FOOTER_EAT_SPACING:
            rInvFlags |= SwFrameInvFlags::InvalidatePrt | SwFrameInvFlags::InvalidateSize;
            break;

        case RES_BACKGROUND:
        case RES_BACKGROUND_FULL_SIZE:
            rInvFlags |= SwFrameInvFlags::SetCompletePaint | SwFrameInvFlags::NextSetCompletePaint;
            break;

        case RES_KEEP:
            rInvFlags |= SwFrameInvFlags::InvalidatePos;
            break;

        case RES_FRM_SIZE:
            ReinitializeFrameSizeAttrFlags();
            rInvFlags |= SwFrameInvFlags::InvalidatePrt | SwFrameInvFlags::InvalidateSize
                         | SwFrameInvFlags::NextInvalidatePos;
            break;

        case RES_ROW_SPLIT:
        {
            if ( IsRowFrame() )
            {
                bool bInFollowFlowRow = nullptr != IsInFollowFlowRow();
                if ( bInFollowFlowRow || nullptr != IsInSplitTableRow() )
                {
                    SwTabFrame* pTab = FindTabFrame();
                    if ( bInFollowFlowRow )
                        pTab = pTab->FindMaster();
                    pTab->SetRemoveFollowFlowLinePending( true );
                }
            }
            break;
        }
        case RES_COL:
            OSL_FAIL( "Columns for new FrameType?" );
            break;

        default:
            // the new FillStyle has to do the same as previous RES_BACKGROUND
            if(nWhich >= XATTR_FILL_FIRST && nWhich <= XATTR_FILL_LAST)
            {
                rInvFlags
                    |= SwFrameInvFlags::SetCompletePaint | SwFrameInvFlags::NextSetCompletePaint;
            }
            /* do Nothing */;
    }
}

// static
void SwFrame::UpdateAttrFrameForFormatChange( SwFrameInvFlags &rInvFlags )
{
    rInvFlags |= SwFrameInvFlags::InvalidatePrt | SwFrameInvFlags::InvalidateSize
                 | SwFrameInvFlags::InvalidatePos | SwFrameInvFlags::SetCompletePaint;
}

bool SwFrame::Prepare( const PrepareHint, const void *, bool )
{
    /* Do nothing */
    return false;
}

/**
 * Invalidates the page in which the Frame is currently placed.
 * The page is invalidated depending on the type (Layout, Content, FlyFrame)
 */
void SwFrame::InvalidatePage( const SwPageFrame *pPage ) const
{
    if ( !pPage )
    {
        pPage = FindPageFrame();
        // #i28701# - for at-character and as-character
        // anchored Writer fly frames additionally invalidate also page frame
        // its 'anchor character' is on.
        if ( pPage && pPage->GetUpper() && IsFlyFrame() )
        {
            const SwFlyFrame* pFlyFrame = static_cast<const SwFlyFrame*>(this);
            if ( pFlyFrame->IsAutoPos() || pFlyFrame->IsFlyInContentFrame() )
            {
                // #i33751#, #i34060# - method <GetPageFrameOfAnchor()>
                // is replaced by method <FindPageFrameOfAnchor()>. It's return value
                // have to be checked.
                SwPageFrame* pPageFrameOfAnchor =
                        const_cast<SwFlyFrame*>(pFlyFrame)->FindPageFrameOfAnchor();
                if ( pPageFrameOfAnchor && pPageFrameOfAnchor != pPage )
                {
                    InvalidatePage( pPageFrameOfAnchor );
                }
            }
        }
    }

    if ( !(pPage && pPage->GetUpper()) )
        return;

    if ( pPage->GetFormat()->GetDoc().IsInDtor() )
        return;

    SwRootFrame *pRoot = const_cast<SwRootFrame*>(static_cast<const SwRootFrame*>(pPage->GetUpper()));
    const SwFlyFrame *pFly = FindFlyFrame();
    if ( IsContentFrame() )
    {
        if ( pRoot->IsTurboAllowed() )
        {
            // If a ContentFrame wants to register for a second time, make it a TurboAction.
            if ( !pRoot->GetTurbo() || this == pRoot->GetTurbo() )
                pRoot->SetTurbo( static_cast<const SwContentFrame*>(this) );
            else
            {
                pRoot->DisallowTurbo();
                //The page of the Turbo could be a different one then mine,
                //therefore we have to invalidate it.
                const SwFrame *pTmp = pRoot->GetTurbo();
                pRoot->ResetTurbo();
                pTmp->InvalidatePage();
            }
        }
        if ( !pRoot->GetTurbo() )
        {
            if ( pFly )
            {   if( !pFly->IsLocked() )
                {
                    if ( pFly->IsFlyInContentFrame() )
                    {   pPage->InvalidateFlyInCnt();
                        pFly->GetAnchorFrame()->InvalidatePage();
                    }
                    else
                        pPage->InvalidateFlyContent();
                }
            }
            else
                pPage->InvalidateContent();
        }
    }
    else
    {
        pRoot->DisallowTurbo();
        if ( pFly )
        {
            if ( !pFly->IsLocked() )
            {
                if ( pFly->IsFlyInContentFrame() )
                {
                    pPage->InvalidateFlyInCnt();
                    pFly->GetAnchorFrame()->InvalidatePage();
                }
                else
                    pPage->InvalidateFlyLayout();
            }
        }
        else
            pPage->InvalidateLayout();

        if ( pRoot->GetTurbo() )
        {   const SwFrame *pTmp = pRoot->GetTurbo();
            pRoot->ResetTurbo();
            pTmp->InvalidatePage();
        }
    }
    pRoot->SetIdleFlags();

    if (!IsTextFrame())
        return;

    SwTextFrame const*const pText(static_cast<SwTextFrame const*>(this));
    if (sw::MergedPara const*const pMergedPara = pText->GetMergedPara())
    {
        SwTextNode const* pNode(nullptr);
        for (auto const& e : pMergedPara->extents)
        {
            if (e.pNode != pNode)
            {
                pNode = e.pNode;
                if (pNode->IsGrammarCheckDirty())
                {
                    pRoot->SetNeedGrammarCheck( true );
                    break;
                }
            }
        }
    }
    else
    {
        if (pText->GetTextNodeFirst()->IsGrammarCheckDirty())
        {
            pRoot->SetNeedGrammarCheck( true );
        }
    }
}

Size SwFrame::ChgSize( const Size& aNewSize )
{
    mbFixSize = true;
    const Size aOldSize( getFrameArea().SSize() );
    if ( aNewSize == aOldSize )
        return aOldSize;

    if ( GetUpper() )
    {
        bool bNeighb = IsNeighbourFrame();
        SwRectFnSet fnRect(IsVertical() != bNeighb, IsVertLR(), IsVertLRBT());
        SwRect aNew( Point(0,0), aNewSize );

        {
            SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
            fnRect.SetWidth(aFrm, fnRect.GetWidth(aNew));
        }

        tools::Long nNew = fnRect.GetHeight(aNew);
        tools::Long nDiff = nNew - fnRect.GetHeight(getFrameArea());

        if( nDiff )
        {
            if ( GetUpper()->IsFootnoteBossFrame() && HasFixSize() &&
                 SwNeighbourAdjust::GrowShrink !=
                 static_cast<SwFootnoteBossFrame*>(GetUpper())->NeighbourhoodAdjustment() )
            {
                {
                    SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
                    fnRect.SetHeight(aFrm, nNew);
                }

                SwTwips nReal = static_cast<SwLayoutFrame*>(this)->AdjustNeighbourhood(nDiff);

                if ( nReal != nDiff )
                {
                    SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
                    fnRect.SetHeight(aFrm, nNew - nDiff + nReal);
                }
            }
            else
            {
                // OD 24.10.2002 #97265# - grow/shrink not for neighbour frames
                // NOTE: neighbour frames are cell and column frames.
                if ( !bNeighb )
                {
                    if ( nDiff > 0 )
                        Grow( nDiff );
                    else
                        Shrink( -nDiff );

                    if (GetUpper() && fnRect.GetHeight(getFrameArea()) != nNew)
                    {
                        GetUpper()->InvalidateSize_();
                    }
                }

                // Even if grow/shrink did not yet set the desired width, for
                // example when called by ChgColumns to set the column width, we
                // set the right width now.
                SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
                fnRect.SetHeight(aFrm, nNew);
            }
        }
    }
    else
    {
        SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
        aFrm.SSize( aNewSize );
    }

    if ( getFrameArea().SSize() != aOldSize )
    {
        SwPageFrame *pPage = FindPageFrame();
        if ( GetNext() )
        {
            GetNext()->InvalidatePos_();
            GetNext()->InvalidatePage( pPage );
        }
        if( IsLayoutFrame() )
        {
            if( IsRightToLeft() )
                InvalidatePos_();
            if( static_cast<SwLayoutFrame*>(this)->Lower() )
                static_cast<SwLayoutFrame*>(this)->Lower()->InvalidateSize_();
        }
        InvalidatePrt_();
        InvalidateSize_();
        InvalidatePage( pPage );
    }

    return getFrameArea().SSize();
}

/** Insert SwFrame into existing structure.
 *
 * Insertion is done below the parent either before pBehind or
 * at the end of the chain if pBehind is empty.
 */
void SwFrame::InsertBefore( SwLayoutFrame* pParent, SwFrame* pBehind )
{
    OSL_ENSURE( pParent, "No parent for insert." );
    OSL_ENSURE( (!pBehind || pParent == pBehind->GetUpper()),
            "Frame tree is inconsistent." );

    mpUpper = pParent;
    mpNext = pBehind;
    if( pBehind )
    {   //Insert before pBehind.
        mpPrev = pBehind->mpPrev;
        if( nullptr != mpPrev )
            mpPrev->mpNext = this;
        else
            mpUpper->m_pLower = this;
        pBehind->mpPrev = this;
    }
    else
    {   //Insert at the end, or as first node in the sub tree
        mpPrev = mpUpper->Lower();
        if ( mpPrev )
        {
            while( mpPrev->mpNext )
                mpPrev = mpPrev->mpNext;
            mpPrev->mpNext = this;
        }
        else
            mpUpper->m_pLower = this;
    }
}

/** Insert SwFrame into existing structure.
 *
 * Insertion is done below the parent either after pBehind or
 * at the beginning of the chain if pBehind is empty.
 */
void SwFrame::InsertBehind( SwLayoutFrame *pParent, SwFrame *pBefore )
{
    OSL_ENSURE( (!pBefore || pParent == pBefore->GetUpper()),
            "Frame tree is inconsistent." );

    mpUpper = pParent;
    mpPrev = pBefore;
    if ( pBefore )
    {
        //Insert after pBefore
        mpNext = pBefore->mpNext;
        if ( nullptr != mpNext )
            mpNext->mpPrev = this;
        pBefore->mpNext = this;
    }
    else
    {
        assert(pParent && "No Parent for Insert.");
        //Insert at the beginning of the chain
        mpNext = pParent->Lower();
        if ( mpNext )
            mpNext->mpPrev = this;
        pParent->m_pLower = this;
    }
}

/** Insert a chain of SwFrames into an existing structure
 *
 * Currently, this method is used to insert a SectionFrame (which may have some siblings) into an
 * existing structure. If the third parameter is NULL, this method is (besides handling the
 * siblings) equal to SwFrame::InsertBefore(..).
 *
 * If the third parameter is passed, the following happens:
 *  - this becomes mpNext of pParent
 *  - pSct becomes mpNext of the last one in the this-chain
 *  - pBehind is reconnected from pParent to pSct
 * The purpose is: a SectionFrame (this) won't become a child of another SectionFrame (pParent), but
 * pParent gets split into two siblings (pParent+pSect) and this is inserted between.
 */
bool SwFrame::InsertGroupBefore( SwFrame* pParent, SwFrame* pBehind, SwFrame* pSct )
{
    OSL_ENSURE( pParent, "No parent for insert." );
    OSL_ENSURE( (!pBehind || ( (pBehind && (pParent == pBehind->GetUpper()))
            || ((pParent->IsSctFrame() && pBehind->GetUpper()->IsColBodyFrame())) ) ),
            "Frame tree inconsistent." );
    if( pSct )
    {
        mpUpper = pParent->GetUpper();
        SwFrame *pLast = this;
        while( pLast->GetNext() )
        {
            pLast = pLast->GetNext();
            pLast->mpUpper = GetUpper();
        }
        if( pBehind )
        {
            pLast->mpNext = pSct;
            pSct->mpPrev = pLast;
            pSct->mpNext = pParent->GetNext();
        }
        else
        {
            pLast->mpNext = pParent->GetNext();
            if( pLast->GetNext() )
                pLast->GetNext()->mpPrev = pLast;
        }
        pParent->mpNext = this;
        mpPrev = pParent;
        if( pSct->GetNext() )
            pSct->GetNext()->mpPrev = pSct;
        while( pLast->GetNext() )
        {
            pLast = pLast->GetNext();
            pLast->mpUpper = GetUpper();
        }
        if( pBehind )
        {   // Insert before pBehind.
            if( pBehind->GetPrev() )
                pBehind->GetPrev()->mpNext = nullptr;
            else
                pBehind->GetUpper()->m_pLower = nullptr;
            pBehind->mpPrev = nullptr;
            SwLayoutFrame* pTmp = static_cast<SwLayoutFrame*>(pSct);
            SwFrame* pLower = pTmp->Lower();
            if( pLower )
            {
                OSL_ENSURE( pLower->IsColumnFrame(), "InsertGrp: Used SectionFrame" );
                pTmp = static_cast<SwLayoutFrame*>(static_cast<SwLayoutFrame*>(pLower)->Lower());
                OSL_ENSURE( pTmp, "InsertGrp: Missing ColBody" );
            }
            pBehind->mpUpper = pTmp;
            pBehind->GetUpper()->m_pLower = pBehind;
            pLast = pBehind->GetNext();
            while ( pLast )
            {
                pLast->mpUpper = pBehind->GetUpper();
                pLast = pLast->GetNext();
            }
        }
        else
        {
            OSL_ENSURE( pSct->IsSctFrame(), "InsertGroup: For SectionFrames only" );
            SwFrame::DestroyFrame(pSct);
            return false;
        }
    }
    else
    {
        mpUpper = static_cast<SwLayoutFrame*>(pParent);
        SwFrame *pLast = this;
        while( pLast->GetNext() )
        {
            pLast = pLast->GetNext();
            pLast->mpUpper = GetUpper();
        }
        pLast->mpNext = pBehind;
        if( pBehind )
        {   // Insert before pBehind.
            mpPrev = pBehind->mpPrev;
            if( nullptr != mpPrev )
                mpPrev->mpNext = this;
            else
                mpUpper->m_pLower = this;
            pBehind->mpPrev = pLast;
        }
        else
        {
            //Insert at the end, or ... the first node in the subtree
            mpPrev = mpUpper->Lower();
            if ( mpPrev )
            {
                while( mpPrev->mpNext )
                    mpPrev = mpPrev->mpNext;
                mpPrev->mpNext = this;
            }
            else
                mpUpper->m_pLower = this;
        }
    }
    return true;
}

void SwFrame::RemoveFromLayout()
{
    OSL_ENSURE( mpUpper, "Remove without upper?" );

    if (mpPrev)
        // one out of the middle is removed
        mpPrev->mpNext = mpNext;
    else if (mpUpper)
    {   // the first in a list is removed //TODO
        OSL_ENSURE( mpUpper->m_pLower == this, "Layout is inconsistent." );
        mpUpper->m_pLower = mpNext;
    }
    if( mpNext )
        mpNext->mpPrev = mpPrev;

    // Remove link
    mpNext  = mpPrev  = nullptr;
    mpUpper = nullptr;
}

void SwContentFrame::Paste( SwFrame* pParent, SwFrame* pSibling)
{
    OSL_ENSURE( pParent, "No parent for pasting." );
    OSL_ENSURE( pParent->IsLayoutFrame(), "Parent is ContentFrame." );
    OSL_ENSURE( pParent != this, "I'm the parent." );
    OSL_ENSURE( pSibling != this, "I'm my own neighbour." );
    OSL_ENSURE( !GetPrev() && !GetNext() && !GetUpper(),
            "I'm still registered somewhere" );
    OSL_ENSURE( !pSibling || pSibling->IsFlowFrame(),
            "<SwContentFrame::Paste(..)> - sibling not of expected type." );

    //Insert in the tree.
    InsertBefore( static_cast<SwLayoutFrame*>(pParent), pSibling );

    SwPageFrame *pPage = FindPageFrame();
    InvalidateAll_();
    InvalidatePage( pPage );

    if( pPage )
    {
        pPage->InvalidateSpelling();
        pPage->InvalidateSmartTags();
        pPage->InvalidateAutoCompleteWords();
        pPage->InvalidateWordCount();
    }

    if ( GetNext() )
    {
        SwFrame* pNxt = GetNext();
        pNxt->InvalidatePrt_();
        pNxt->InvalidatePos_();
        pNxt->InvalidatePage( pPage );
        if( pNxt->IsSctFrame() )
            pNxt = static_cast<SwSectionFrame*>(pNxt)->ContainsContent();
        if( pNxt && pNxt->IsTextFrame() && pNxt->IsInFootnote() )
            pNxt->Prepare( PrepareHint::FootnoteInvalidation, nullptr, false );
    }

    if ( getFrameArea().Height() )
        pParent->Grow( getFrameArea().Height() );

    if ( getFrameArea().Width() != pParent->getFramePrintArea().Width() )
        Prepare( PrepareHint::FixSizeChanged );

    if ( GetPrev() )
    {
        if ( IsFollow() )
            //I'm a direct follower of my master now
            static_cast<SwContentFrame*>(GetPrev())->Prepare( PrepareHint::FollowFollows );
        else
        {
            if ( GetPrev()->getFrameArea().Height() !=
                 GetPrev()->getFramePrintArea().Height() + GetPrev()->getFramePrintArea().Top() )
            {
                // Take the border into account?
                GetPrev()->InvalidatePrt_();
            }
            // OD 18.02.2003 #104989# - force complete paint of previous frame,
            // if frame is inserted at the end of a section frame, in order to
            // get subsidiary lines repainted for the section.
            if ( pParent->IsSctFrame() && !GetNext() )
            {
                // force complete paint of previous frame, if new inserted frame
                // in the section is the last one.
                GetPrev()->SetCompletePaint();
            }
            GetPrev()->InvalidatePage( pPage );
        }
    }
    if ( IsInFootnote() )
    {
        SwFrame* pFrame = GetIndPrev();
        if( pFrame && pFrame->IsSctFrame() )
            pFrame = static_cast<SwSectionFrame*>(pFrame)->ContainsAny();
        if( pFrame )
            pFrame->Prepare( PrepareHint::QuoVadis, nullptr, false );
        if( !GetNext() )
        {
            pFrame = FindFootnoteFrame()->GetNext();
            if( pFrame && nullptr != (pFrame=static_cast<SwLayoutFrame*>(pFrame)->ContainsAny()) )
                pFrame->InvalidatePrt_();
        }
    }

    InvalidateLineNum_();
    SwFrame *pNxt = FindNextCnt();
    if ( !pNxt  )
        return;

    while ( pNxt && pNxt->IsInTab() )
    {
        pNxt = pNxt->FindTabFrame();
        if( nullptr != pNxt )
            pNxt = pNxt->FindNextCnt();
    }
    if ( pNxt )
    {
        pNxt->InvalidateLineNum_();
        if ( pNxt != GetNext() )
            pNxt->InvalidatePage();
    }
}

void SwContentFrame::Cut()
{
    OSL_ENSURE( GetUpper(), "Cut without Upper()." );

    SwPageFrame *pPage = FindPageFrame();
    InvalidatePage( pPage );
    SwFrame *pFrame = GetIndPrev();
    if( pFrame )
    {
        if( pFrame->IsSctFrame() )
            pFrame = static_cast<SwSectionFrame*>(pFrame)->ContainsAny();
        if ( pFrame && pFrame->IsContentFrame() )
        {
            pFrame->InvalidatePrt_();
            if( IsInFootnote() )
                pFrame->Prepare( PrepareHint::QuoVadis, nullptr, false );
        }
        // #i26250# - invalidate printing area of previous
        // table frame.
        else if ( pFrame && pFrame->IsTabFrame() )
        {
            pFrame->InvalidatePrt();
        }
    }

    SwFrame *pNxt = FindNextCnt();
    if ( pNxt )
    {
        while ( pNxt && pNxt->IsInTab() )
        {
            pNxt = pNxt->FindTabFrame();
            if( nullptr != pNxt )
                pNxt = pNxt->FindNextCnt();
        }
        if ( pNxt )
        {
            pNxt->InvalidateLineNum_();
            if ( pNxt != GetNext() )
                pNxt->InvalidatePage();
        }
    }

    SwTabFrame* pMasterTab(nullptr);
    pFrame = GetIndNext();
    if( pFrame )
    {
        // The old follow may have calculated a gap to the predecessor which
        // now becomes obsolete or different as it becomes the first one itself
        pFrame->InvalidatePrt_();
        pFrame->InvalidatePos_();
        pFrame->InvalidatePage( pPage );
        if( pFrame->IsSctFrame() )
        {
            pFrame = static_cast<SwSectionFrame*>(pFrame)->ContainsAny();
            if( pFrame )
            {
                pFrame->InvalidatePrt_();
                pFrame->InvalidatePos_();
                pFrame->InvalidatePage( pPage );
            }
        }
        if( pFrame && IsInFootnote() )
            pFrame->Prepare( PrepareHint::ErgoSum, nullptr, false );
        if( IsInSct() && !GetPrev() )
        {
            SwSectionFrame* pSct = FindSctFrame();
            if( !pSct->IsFollow() )
            {
                pSct->InvalidatePrt_();
                pSct->InvalidatePage( pPage );
            }
        }
    }
    else
    {
        InvalidateNextPos();
        //Someone needs to do the retouching: predecessor or upper
        pFrame = GetPrev();
        if ( nullptr != pFrame )
        {   pFrame->SetRetouche();
            pFrame->Prepare( PrepareHint::WidowsOrphans );
            pFrame->InvalidatePos_();
            pFrame->InvalidatePage( pPage );
        }
        // If I'm (was) the only ContentFrame in my upper, it has to do the
        // retouching. Also, perhaps a page became empty.
        else
        {   SwRootFrame *pRoot = getRootFrame();
            if ( pRoot )
            {
                pRoot->SetSuperfluous();
                // RemoveSuperfluous can only remove empty pages at the end;
                // find if there are pages without content following pPage
                // and if so request a call to CheckPageDescs()
                SwViewShell *pSh = pRoot->GetCurrShell();
                // tdf#152983 pPage is null when called from SwHeadFootFrame ctor
                if (pPage && pSh && pSh->Imp()->IsAction())
                {
                    SwPageFrame const* pNext(pPage);
                    while ((pNext = static_cast<SwPageFrame const*>(pNext->GetNext())))
                    {
                        if (!sw::IsPageFrameEmpty(*pNext) && !pNext->IsFootnotePage())
                        {
                            pSh->Imp()->GetLayAction().SetCheckPageNum(pPage->GetPhyPageNum());
                            break;
                        }
                    }
                }
                GetUpper()->SetCompletePaint();
                GetUpper()->InvalidatePage( pPage );
            }
            if( IsInSct() )
            {
                SwSectionFrame* pSct = FindSctFrame();
                if( !pSct->IsFollow() )
                {
                    pSct->InvalidatePrt_();
                    pSct->InvalidatePage( pPage );
                }
            }
            // #i52253# The master table should take care
            // of removing the follow flow line.
            if ( IsInTab() )
            {
                SwTabFrame* pThisTab = FindTabFrame();
                if (pThisTab && pThisTab->IsFollow())
                {
                    pMasterTab = pThisTab->FindMaster();
                }
            }
        }
    }
    //Remove first, then shrink the upper.
    SwLayoutFrame *pUp = GetUpper();
    RemoveFromLayout();
    if ( !pUp )
    {
        assert(!pMasterTab);
        return;
    }

    if (pMasterTab
        && !pMasterTab->GetFollow()->GetFirstNonHeadlineRow()->ContainsContent())
    {   // only do this if there's no content in other cells of the row!
        pMasterTab->InvalidatePos_();
        pMasterTab->SetRemoveFollowFlowLinePending(true);
    }

    SwSectionFrame *pSct = nullptr;
    if ( !pUp->Lower() &&
         ( ( pUp->IsFootnoteFrame() && !pUp->IsColLocked() ) ||
           ( pUp->IsInSct() &&
             // #i29438#
             // We have to consider the case that the section may be "empty"
             // except from a temporary empty table frame.
             // This can happen due to the new cell split feature.
             !pUp->IsCellFrame() &&
             // #126020# - adjust check for empty section
             // #130797# - correct fix #126020#
             !(pSct = pUp->FindSctFrame())->ContainsContent() &&
             !pSct->ContainsAny( true ) ) ) )
    {
        if ( pUp->GetUpper() )
        {

            // prevent delete of <ColLocked> footnote frame
            if ( pUp->IsFootnoteFrame() && !pUp->IsColLocked())
            {
                if( pUp->GetNext() && !pUp->GetPrev() )
                {
                    SwFrame* pTmp = static_cast<SwLayoutFrame*>(pUp->GetNext())->ContainsAny();
                    if( pTmp )
                        pTmp->InvalidatePrt_();
                }
                if (!pUp->IsDeleteForbidden())
                {
                    pUp->Cut();
                    SwFrame::DestroyFrame(pUp);
                }
            }
            else
            {
                assert(pSct);
                if ( pSct->IsColLocked() || !pSct->IsInFootnote() ||
                     ( pUp->IsFootnoteFrame() && pUp->IsColLocked() ) )
                {
                    pSct->DelEmpty( false );
                    // If a locked section may not be deleted then at least
                    // its size became invalid after removing its last
                    // content.
                    pSct->InvalidateSize_();
                }
                else
                {
                    pSct->DelEmpty( true );
                    SwFrame::DestroyFrame(pSct);
                }
            }
        }
    }
    else
    {
        SwRectFnSet aRectFnSet(this);
        tools::Long nFrameHeight = aRectFnSet.GetHeight(getFrameArea());
        if( nFrameHeight )
            pUp->Shrink( nFrameHeight );
    }
}

void SwLayoutFrame::Paste( SwFrame* pParent, SwFrame* pSibling)
{
    OSL_ENSURE( pParent, "No parent for pasting." );
    OSL_ENSURE( pParent->IsLayoutFrame(), "Parent is ContentFrame." );
    OSL_ENSURE( pParent != this, "I'm the parent oneself." );
    OSL_ENSURE( pSibling != this, "I'm my own neighbour." );
    OSL_ENSURE( !GetPrev() && !GetNext() && !GetUpper(),
            "I'm still registered somewhere." );

    //Insert in the tree.
    InsertBefore( static_cast<SwLayoutFrame*>(pParent), pSibling );

    // OD 24.10.2002 #103517# - correct setting of variable <fnRect>
    // <fnRect> is used for the following:
    // (1) To invalidate the frame's size, if its size, which has to be the
    //      same as its upper/parent, differs from its upper's/parent's.
    // (2) To adjust/grow the frame's upper/parent, if it has a dimension in its
    //      size, which is not determined by its upper/parent.
    // Which size is which depends on the frame type and the layout direction
    // (vertical or horizontal).
    // There are the following cases:
    // (A) Header and footer frames both in vertical and in horizontal layout
    //      have to size the width to the upper/parent. A dimension in the height
    //      has to cause an adjustment/grow of the upper/parent.
    //      --> <fnRect> = fnRectHori
    // (B) Cell and column frames in vertical layout, the width has to be the
    //          same as upper/parent and a dimension in height causes adjustment/grow
    //          of the upper/parent.
    //          --> <fnRect> = fnRectHori
    //      in horizontal layout the other way around
    //          --> <fnRect> = fnRectVert
    // (C) Other frames in vertical layout, the height has to be the
    //          same as upper/parent and a dimension in width causes adjustment/grow
    //          of the upper/parent.
    //          --> <fnRect> = fnRectVert
    //      in horizontal layout the other way around
    //          --> <fnRect> = fnRectHori
    //SwRectFn fnRect = IsVertical() ? fnRectHori : fnRectVert;
    bool bVert, bVertL2R, bVertL2RB2T;
    if ( IsHeaderFrame() || IsFooterFrame() )
        bVert = bVertL2R = bVertL2RB2T = false;
    else
    {
        bVert = (IsCellFrame() || IsColumnFrame()) ? !GetUpper()->IsVertical() : GetUpper()->IsVertical();
        bVertL2R = GetUpper()->IsVertLR();
        bVertL2RB2T = GetUpper()->IsVertLRBT();
    }
    SwRectFnSet fnRect(bVert, bVertL2R, bVertL2RB2T);

    if (fnRect.GetWidth(getFrameArea()) != fnRect.GetWidth(pParent->getFramePrintArea()))
        InvalidateSize_();
    InvalidatePos_();
    const SwPageFrame *pPage = FindPageFrame();
    InvalidatePage( pPage );
    if( !IsColumnFrame() )
    {
        SwFrame *pFrame = GetIndNext();
        if( nullptr != pFrame )
        {
            pFrame->InvalidatePos_();
            if( IsInFootnote() )
            {
                if( pFrame->IsSctFrame() )
                    pFrame = static_cast<SwSectionFrame*>(pFrame)->ContainsAny();
                if( pFrame )
                    pFrame->Prepare( PrepareHint::ErgoSum, nullptr, false );
            }
        }
        if ( IsInFootnote() && nullptr != ( pFrame = GetIndPrev() ) )
        {
            if( pFrame->IsSctFrame() )
                pFrame = static_cast<SwSectionFrame*>(pFrame)->ContainsAny();
            if( pFrame )
                pFrame->Prepare( PrepareHint::QuoVadis, nullptr, false );
        }
    }

    if (!fnRect.GetHeight(getFrameArea()))
        return;

    // AdjustNeighbourhood is now also called in columns which are not
    // placed inside a frame
    SwNeighbourAdjust nAdjust = GetUpper()->IsFootnoteBossFrame() ?
            static_cast<SwFootnoteBossFrame*>(GetUpper())->NeighbourhoodAdjustment()
            : SwNeighbourAdjust::GrowShrink;
    SwTwips nGrow = fnRect.GetHeight(getFrameArea());
    if( SwNeighbourAdjust::OnlyAdjust == nAdjust )
        AdjustNeighbourhood( nGrow );
    else
    {
        SwTwips nReal = 0;
        if( SwNeighbourAdjust::AdjustGrow == nAdjust )
            nReal = AdjustNeighbourhood( nGrow );
        if( nReal < nGrow )
            nReal += pParent->Grow( nGrow - nReal );
        if( SwNeighbourAdjust::GrowAdjust == nAdjust && nReal < nGrow )
            AdjustNeighbourhood( nGrow - nReal );
    }
}

void SwLayoutFrame::Cut()
{
    if ( GetNext() )
        GetNext()->InvalidatePos_();

    SwRectFnSet aRectFnSet(this);
    SwTwips nShrink = aRectFnSet.GetHeight(getFrameArea());

    // Remove first, then shrink upper.
    SwLayoutFrame *pUp = GetUpper();

    // AdjustNeighbourhood is now also called in columns which are not
    // placed inside a frame.

    // Remove must not be called before an AdjustNeighbourhood, but it has to
    // be called before the upper-shrink-call, if the upper-shrink takes care
    // of its content.
    if ( pUp && nShrink )
    {
        if( pUp->IsFootnoteBossFrame() )
        {
            SwNeighbourAdjust nAdjust= static_cast<SwFootnoteBossFrame*>(pUp)->NeighbourhoodAdjustment();
            if( SwNeighbourAdjust::OnlyAdjust == nAdjust )
                AdjustNeighbourhood( -nShrink );
            else
            {
                SwTwips nReal = 0;
                if( SwNeighbourAdjust::AdjustGrow == nAdjust )
                    nReal = -AdjustNeighbourhood( -nShrink );
                if( nReal < nShrink )
                {
                    const SwTwips nOldHeight = aRectFnSet.GetHeight(getFrameArea());

                    // seems as if this needs to be forwarded to the SwFrame already here,
                    // changing to zero seems temporary anyways
                    {
                        SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
                        aRectFnSet.SetHeight( aFrm, 0 );
                    }

                    nReal += pUp->Shrink( nShrink - nReal );

                    {
                        SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
                        aRectFnSet.SetHeight( aFrm, nOldHeight );
                    }
                }

                if( SwNeighbourAdjust::GrowAdjust == nAdjust && nReal < nShrink )
                    AdjustNeighbourhood( nReal - nShrink );
            }
            RemoveFromLayout();
        }
        else
        {
            RemoveFromLayout();
            pUp->Shrink( nShrink );
        }
    }
    else
        RemoveFromLayout();

    if( pUp && !pUp->Lower() )
    {
        pUp->SetCompletePaint();
        pUp->InvalidatePage();
    }
}

SwTwips SwFrame::Grow(SwTwips nDist, SwResizeLimitReason& reason, bool bTst, bool bInfo)
{
    OSL_ENSURE( nDist >= 0, "Negative growth?" );

    PROTOCOL_ENTER( this, bTst ? PROT::GrowTest : PROT::Grow, DbgAction::NONE, &nDist )

    if ( !nDist )
    {
        reason = SwResizeLimitReason::Unspecified;
        return 0;
    }
    if ( IsFlyFrame() )
        return static_cast<SwFlyFrame*>(this)->Grow_(nDist, reason, bTst);
    if ( IsSctFrame() )
        return static_cast<SwSectionFrame*>(this)->Grow_(nDist, reason, bTst);
    if (IsCellFrame())
    {
        const SwCellFrame* pThisCell = static_cast<const SwCellFrame*>(this);
        const SwTabFrame* pTab = FindTabFrame();

        // NEW TABLES
        if ( pTab->IsVertical() != IsVertical() ||
             pThisCell->GetLayoutRowSpan() < 1 )
        {
            reason = SwResizeLimitReason::FixedSizeFrame;
            return 0;
        }
    }

    SwRectFnSet aRectFnSet(this);

    SwTwips nPrtHeight = aRectFnSet.GetHeight(getFramePrintArea());
    if( nPrtHeight > 0 && nDist > (LONG_MAX - nPrtHeight) )
        nDist = LONG_MAX - nPrtHeight;

    const SwTwips nReal = GrowFrame(nDist, reason, bTst, bInfo);
    if( !bTst )
    {
        nPrtHeight = aRectFnSet.GetHeight(getFramePrintArea());

        SwFrameAreaDefinition::FramePrintAreaWriteAccess aPrt(*this);
        aRectFnSet.SetHeight( aPrt, nPrtHeight + ( IsContentFrame() ? nDist : nReal ) );
    }
    return nReal;
}

SwTwips SwFrame::Shrink( SwTwips nDist, bool bTst, bool bInfo )
{
    OSL_ENSURE( nDist >= 0, "Negative reduction?" );

    PROTOCOL_ENTER( this, bTst ? PROT::ShrinkTest : PROT::Shrink, DbgAction::NONE, &nDist )

    if ( nDist )
    {
        if ( IsFlyFrame() )
            return static_cast<SwFlyFrame*>(this)->Shrink_( nDist, bTst );
        else if( IsSctFrame() )
            return static_cast<SwSectionFrame*>(this)->Shrink_( nDist, bTst );
        else
        {
            if (IsCellFrame())
            {
                const SwCellFrame* pThisCell = static_cast<const SwCellFrame*>(this);
                const SwTabFrame* pTab = FindTabFrame();

                // NEW TABLES
                if ( (pTab && pTab->IsVertical() != IsVertical()) ||
                     pThisCell->GetLayoutRowSpan() < 1 )
                    return 0;
            }
            SwRectFnSet aRectFnSet(this);
            SwTwips nReal = aRectFnSet.GetHeight(getFrameArea());
            ShrinkFrame( nDist, bTst, bInfo );
            nReal -= aRectFnSet.GetHeight(getFrameArea());
            if( !bTst )
            {
                const SwTwips nPrtHeight = aRectFnSet.GetHeight(getFramePrintArea());
                SwFrameAreaDefinition::FramePrintAreaWriteAccess aPrt(*this);
                aRectFnSet.SetHeight( aPrt, nPrtHeight - ( IsContentFrame() ? nDist : nReal ) );
            }
            return nReal;
        }
    }
    return 0;
}

/** Adjust surrounding neighbourhood after insertion
 *
 * A Frame needs "normalization" if it is directly placed below a footnote boss (page/column) and its
 * size changes. There is always a frame that takes the maximum possible space (the frame that
 * contains the Body text) and zero or more frames which only take the space needed (header/footer
 * area, footnote container). If one of these frames changes, the body-text-frame has to grow or
 * shrink accordingly, even though it's fixed.
 *
 * !! Is it possible to do this in a generic way and not restrict it to the page and a distinct
 * frame which takes the maximum space (controlled using the FrameSize attribute)?
 * Problems:
 *   - What if multiple frames taking the maximum space are placed next to each other?
 *   - How is the maximum space calculated?
 *   - How small can those frames become?
 *
 * In any case, only a certain amount of space is allowed, so we never go below a minimum value for
 * the height of the body.
 *
 * @param nDiff the value around which the space has to be allocated
 */
SwTwips SwFrame::AdjustNeighbourhood( SwTwips nDiff, bool bTst )
{
    PROTOCOL_ENTER( this, PROT::AdjustN, DbgAction::NONE, &nDiff );

    if ( !nDiff || !GetUpper()->IsFootnoteBossFrame() ) // only inside pages/columns
        return 0;

    const SwViewShell *pSh = getRootFrame()->GetCurrShell();
    const bool bBrowse = pSh && pSh->GetViewOptions()->getBrowseMode();

    //The (Page-)Body only changes in BrowseMode, but only if it does not
    //contain columns.
    if ( IsPageBodyFrame() && (!bBrowse ||
          (static_cast<SwLayoutFrame*>(this)->Lower() &&
           static_cast<SwLayoutFrame*>(this)->Lower()->IsColumnFrame())) )
        return 0;

    //In BrowseView mode the PageFrame can handle some of the requests.
    tools::Long nBrowseAdd = 0;
    if ( bBrowse && GetUpper()->IsPageFrame() ) // only (Page-)BodyFrames
    {
        SwViewShell *pViewShell = getRootFrame()->GetCurrShell();
        SwLayoutFrame *pUp = GetUpper();
        tools::Long nChg;
        const tools::Long nUpPrtBottom = pUp->getFrameArea().Height() -
                                  pUp->getFramePrintArea().Height() - pUp->getFramePrintArea().Top();
        SwRect aInva( pUp->getFrameArea() );
        if ( pViewShell )
        {
            aInva.Pos().setX( pViewShell->VisArea().Left() );
            aInva.Width( pViewShell->VisArea().Width() );
        }
        if ( nDiff > 0 )
        {
            nChg = BROWSE_HEIGHT - pUp->getFrameArea().Height();
            nChg = std::min( nDiff, SwTwips(nChg) );

            if ( !IsBodyFrame() )
            {
                SetCompletePaint();
                if ( !pViewShell || pViewShell->VisArea().Height() >= pUp->getFrameArea().Height() )
                {
                    //First minimize Body, it will grow again later.
                    SwFrame *pBody = static_cast<SwFootnoteBossFrame*>(pUp)->FindBodyCont();
                    const tools::Long nTmp = nChg - pBody->getFramePrintArea().Height();
                    if ( !bTst )
                    {
                        {
                            SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*pBody);
                            aFrm.Height(std::max( tools::Long(0), aFrm.Height() - nChg ));
                        }

                        pBody->InvalidatePrt_();
                        pBody->InvalidateSize_();
                        if ( pBody->GetNext() )
                            pBody->GetNext()->InvalidatePos_();
                        if ( !IsHeaderFrame() )
                            pBody->SetCompletePaint();
                    }
                    nChg = nTmp <= 0 ? 0 : nTmp;
                }
            }

            const tools::Long nTmp = nUpPrtBottom + 20;
            aInva.Top( aInva.Bottom() - nTmp );
            aInva.Height( nChg + nTmp );
        }
        else
        {
            //The page can shrink to 0. The first page keeps the same size like
            //VisArea.
            nChg = nDiff;
            tools::Long nInvaAdd = 0;
            if ( pViewShell && !pUp->GetPrev() &&
                 pUp->getFrameArea().Height() + nDiff < pViewShell->VisArea().Height() )
            {
                // This means that we have to invalidate adequately.
                nChg = pViewShell->VisArea().Height() - pUp->getFrameArea().Height();
                nInvaAdd = -(nDiff - nChg);
            }

            //Invalidate including bottom border.
            tools::Long nBorder = nUpPrtBottom + 20;
            nBorder -= nChg;
            aInva.Top( aInva.Bottom() - (nBorder+nInvaAdd) );
            if ( !IsBodyFrame() )
            {
                SetCompletePaint();
                if ( !IsHeaderFrame() )
                    static_cast<SwFootnoteBossFrame*>(pUp)->FindBodyCont()->SetCompletePaint();
            }
            //Invalidate the page because of the frames. Thereby the page becomes
            //the right size again if a frame didn't fit. This only works
            //randomly for paragraph bound frames otherwise (NotifyFlys).
            pUp->InvalidateSize();
        }
        if ( !bTst )
        {
            //Independent from nChg
            if ( pViewShell && aInva.HasArea() && pUp->GetUpper() )
                pViewShell->InvalidateWindows( aInva );
        }
        if ( !bTst && nChg )
        {
            {
                SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*pUp);
                aFrm.AddHeight(nChg );
            }

            {
                SwFrameAreaDefinition::FramePrintAreaWriteAccess aPrt(*pUp);
                aPrt.AddHeight(nChg );
            }

            if ( pViewShell )
                pViewShell->Imp()->SetFirstVisPageInvalid();

            if ( GetNext() )
                GetNext()->InvalidatePos_();

            //Trigger a repaint if necessary.
            std::unique_ptr<SvxBrushItem> aBack(pUp->GetFormat()->makeBackgroundBrushItem());
            const SvxGraphicPosition ePos = aBack->GetGraphicPos();
            if ( ePos != GPOS_NONE && ePos != GPOS_TILED )
                pViewShell->InvalidateWindows( pUp->getFrameArea() );

            if ( pUp->GetUpper() )
            {
                if ( pUp->GetNext() )
                    pUp->GetNext()->InvalidatePos();

                //Sad but true: during notify on ViewImp a Calc on the page and
                //its Lower may be called. The values should not be changed
                //because the caller takes care of the adjustment of Frame and
                //Prt.
                const tools::Long nOldFrameHeight = getFrameArea().Height();
                const tools::Long nOldPrtHeight = getFramePrintArea().Height();
                const bool bOldComplete = IsCompletePaint();

                if ( IsBodyFrame() )
                {
                    SwFrameAreaDefinition::FramePrintAreaWriteAccess aPrt(*this);
                    aPrt.Height( nOldFrameHeight );
                }

                if ( pUp->GetUpper() )
                {
                    static_cast<SwRootFrame*>(pUp->GetUpper())->CheckViewLayout( nullptr, nullptr );
                }

                SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
                aFrm.Height( nOldFrameHeight );

                SwFrameAreaDefinition::FramePrintAreaWriteAccess aPrt(*this);
                aPrt.Height( nOldPrtHeight );

                mbCompletePaint = bOldComplete;
            }
            if ( !IsBodyFrame() )
                pUp->InvalidateSize_();
            InvalidatePage( static_cast<SwPageFrame*>(pUp) );
        }
        nDiff -= nChg;
        if ( !nDiff )
            return nChg;
        else
            nBrowseAdd = nChg;
    }

    const SwFootnoteBossFrame *pBoss = static_cast<SwFootnoteBossFrame*>(GetUpper());

    SwTwips nReal = 0,
            nAdd  = 0;
    SwFrame *pFrame = nullptr;
    SwRectFnSet aRectFnSet(this);

    if( IsBodyFrame() )
    {
        if( IsInSct() )
        {
            SwSectionFrame *pSect = FindSctFrame();
            if( nDiff > 0 && pSect->IsEndnAtEnd() && GetNext() &&
                GetNext()->IsFootnoteContFrame() )
            {
                SwFootnoteContFrame* pCont = static_cast<SwFootnoteContFrame*>(GetNext());
                SwTwips nMinH = 0;
                SwFootnoteFrame* pFootnote = static_cast<SwFootnoteFrame*>(pCont->Lower());
                bool bFootnote = false;
                while( pFootnote )
                {
                    if( !pFootnote->GetAttr()->GetFootnote().IsEndNote() )
                    {
                        nMinH += aRectFnSet.GetHeight(pFootnote->getFrameArea());
                        bFootnote = true;
                    }
                    pFootnote = static_cast<SwFootnoteFrame*>(pFootnote->GetNext());
                }
                if( bFootnote )
                    nMinH += aRectFnSet.GetTop(pCont->getFramePrintArea());
                nReal = aRectFnSet.GetHeight(pCont->getFrameArea()) - nMinH;
                if( nReal > nDiff )
                    nReal = nDiff;
                if( nReal > 0 )
                    pFrame = GetNext();
                else
                    nReal = 0;
            }
            if( !bTst && !pSect->IsColLocked() )
                pSect->InvalidateSize();
        }
        if( !pFrame )
            return nBrowseAdd;
    }
    else
    {
        const bool bFootnotePage = pBoss->IsPageFrame() && static_cast<const SwPageFrame*>(pBoss)->IsFootnotePage();
        if ( bFootnotePage && !IsFootnoteContFrame() )
            pFrame = const_cast<SwFrame*>(static_cast<SwFrame const *>(pBoss->FindFootnoteCont()));
        if ( !pFrame )
            pFrame = const_cast<SwFrame*>(static_cast<SwFrame const *>(pBoss->FindBodyCont()));

        if ( !pFrame )
            return 0;

        //If not one is found, everything else is solved.
        nReal = aRectFnSet.GetHeight(pFrame->getFrameArea());
        if( nReal > nDiff )
            nReal = nDiff;
        if( !bFootnotePage )
        {
            //Respect the minimal boundary!
            if( nReal )
            {
                const SwTwips nMax = pBoss->GetVarSpace();
                if ( nReal > nMax )
                    nReal = nMax;
            }
            if( !IsFootnoteContFrame() && nDiff > nReal &&
                pFrame->GetNext() && pFrame->GetNext()->IsFootnoteContFrame()
                && ( pFrame->GetNext()->IsVertical() == IsVertical() )
                )
            {
                //If the Body doesn't return enough, we look for a footnote, if
                //there is one, we steal there accordingly.
                const SwTwips nAddMax = aRectFnSet.GetHeight(pFrame->GetNext()->getFrameArea());
                nAdd = nDiff - nReal;
                if ( nAdd > nAddMax )
                    nAdd = nAddMax;
                if ( !bTst )
                {
                    {
                        SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*pFrame->GetNext());
                        aRectFnSet.SetHeight(aFrm, nAddMax-nAdd);

                        if( aRectFnSet.IsVert() && !aRectFnSet.IsVertL2R() )
                        {
                            aFrm.Pos().AdjustX(nAdd );
                        }
                    }

                    pFrame->GetNext()->InvalidatePrt();

                    if ( pFrame->GetNext()->GetNext() )
                    {
                        pFrame->GetNext()->GetNext()->InvalidatePos_();
                    }
                }
            }
        }
    }

    if ( !bTst && nReal )
    {
        SwTwips nTmp = aRectFnSet.GetHeight(pFrame->getFrameArea());

        {
            SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*pFrame);
            aRectFnSet.SetHeight( aFrm, nTmp - nReal );

            if( aRectFnSet.IsVert() && !aRectFnSet.IsVertL2R() )
            {
                aFrm.Pos().AdjustX(nReal );
            }
        }

        pFrame->InvalidatePrt();

        if ( pFrame->GetNext() )
            pFrame->GetNext()->InvalidatePos_();

        if( nReal < 0 && pFrame->IsInSct() )
        {
            SwLayoutFrame* pUp = pFrame->GetUpper();
            if( pUp && nullptr != ( pUp = pUp->GetUpper() ) && pUp->IsSctFrame() &&
                !pUp->IsColLocked() )
                pUp->InvalidateSize();
        }
        if( ( IsHeaderFrame() || IsFooterFrame() ) && pBoss->GetDrawObjs() )
        {
            const SwSortedObjs &rObjs = *pBoss->GetDrawObjs();
            OSL_ENSURE( pBoss->IsPageFrame(), "Header/Footer out of page?" );
            for (SwAnchoredObject* pAnchoredObj : rObjs)
            {
                if ( auto pFly = pAnchoredObj->DynCastFlyFrame() )
                {
                    OSL_ENSURE( !pFly->IsFlyInContentFrame(), "FlyInCnt at Page?" );
                    const SwFormatVertOrient &rVert =
                                        pFly->GetFormat()->GetVertOrient();
                    // When do we have to invalidate?
                    // If a frame is aligned on a PageTextArea and the header
                    // changes a TOP, MIDDLE or NONE aligned frame needs to
                    // recalculate it's position; if the footer changes a BOTTOM
                    // or MIDDLE aligned frame needs to recalculate it's
                    // position.
                    if( ( rVert.GetRelationOrient() == text::RelOrientation::PRINT_AREA ||
                          rVert.GetRelationOrient() == text::RelOrientation::PAGE_PRINT_AREA )    &&
                        ((IsHeaderFrame() && rVert.GetVertOrient()!=text::VertOrientation::BOTTOM) ||
                         (IsFooterFrame() && rVert.GetVertOrient()!=text::VertOrientation::NONE &&
                          rVert.GetVertOrient() != text::VertOrientation::TOP)) )
                    {
                        pFly->InvalidatePos_();
                        pFly->Invalidate_();
                    }
                }
            }
        }
    }
    return (nBrowseAdd + nReal + nAdd);
}

/** method to perform additional actions on an invalidation (2004-05-19 #i28701#) */
void SwFrame::ActionOnInvalidation( const InvalidationType )
{
    // default behaviour is to perform no additional action
}

/** method to determine, if an invalidation is allowed (2004-05-19 #i28701#) */
bool SwFrame::InvalidationAllowed( const InvalidationType ) const
{
    // default behaviour is to allow invalidation
    return true;
}

void SwFrame::ImplInvalidateSize()
{
    if ( InvalidationAllowed( INVALID_SIZE ) )
    {
        setFrameAreaSizeValid(false);

        if ( IsFlyFrame() )
            static_cast<SwFlyFrame*>(this)->Invalidate_();
        else
            InvalidatePage();

        // OD 2004-05-19 #i28701#
        ActionOnInvalidation( INVALID_SIZE );
    }
}

void SwFrame::ImplInvalidatePrt()
{
    if ( InvalidationAllowed( INVALID_PRTAREA ) )
    {
        setFramePrintAreaValid(false);

        if ( IsFlyFrame() )
            static_cast<SwFlyFrame*>(this)->Invalidate_();
        else
            InvalidatePage();

        // OD 2004-05-19 #i28701#
        ActionOnInvalidation( INVALID_PRTAREA );
    }
}

void SwFrame::ImplInvalidatePos()
{
    if ( !InvalidationAllowed( INVALID_POS ) )
        return;

    setFrameAreaPositionValid(false);

    if ( IsFlyFrame() )
    {
        static_cast<SwFlyFrame*>(this)->Invalidate_();
    }
    else
    {
        InvalidatePage();
    }

    // OD 2004-05-19 #i28701#
    ActionOnInvalidation( INVALID_POS );
}

void SwFrame::ImplInvalidateLineNum()
{
    if ( InvalidationAllowed( INVALID_LINENUM ) )
    {
        mbValidLineNum = false;
        OSL_ENSURE( IsTextFrame(), "line numbers are implemented for text only" );
        InvalidatePage();

        // OD 2004-05-19 #i28701#
        ActionOnInvalidation( INVALID_LINENUM );
    }
}

void SwFrame::ReinitializeFrameSizeAttrFlags()
{
    const SwFormatFrameSize &rFormatSize = GetAttrSet()->GetFrameSize();
    if ( SwFrameSize::Variable == rFormatSize.GetHeightSizeType() ||
         SwFrameSize::Minimum == rFormatSize.GetHeightSizeType())
    {
        mbFixSize = false;
        if ( GetType() & (SwFrameType::Header | SwFrameType::Footer | SwFrameType::Row) )
        {
            SwFrame *pFrame = static_cast<SwLayoutFrame*>(this)->Lower();
            while ( pFrame )
            {   pFrame->InvalidateSize_();
                pFrame->InvalidatePrt_();
                pFrame = pFrame->GetNext();
            }
            SwContentFrame *pCnt = static_cast<SwLayoutFrame*>(this)->ContainsContent();
            // #i36991# - be save.
            // E.g., a row can contain *no* content.
            if ( pCnt )
            {
                pCnt->InvalidatePage();
                do
                {
                    pCnt->Prepare( PrepareHint::AdjustSizeWithoutFormatting );
                    pCnt->InvalidateSize_();
                    pCnt = pCnt->GetNextContentFrame();
                } while ( static_cast<SwLayoutFrame*>(this)->IsAnLower( pCnt ) );
            }
        }
    }
    else if ( rFormatSize.GetHeightSizeType() == SwFrameSize::Fixed )
    {
        if( IsVertical() )
            ChgSize( Size( rFormatSize.GetWidth(), getFrameArea().Height()));
        else
            ChgSize( Size( getFrameArea().Width(), rFormatSize.GetHeight()));
    }
}

void SwFrame::ValidateThisAndAllLowers( const sal_uInt16 nStage )
{
    // Stage 0: Only validate frames. Do not process any objects.
    // Stage 1: Only validate fly frames and all of their contents.
    // Stage 2: Validate all.

    const bool bOnlyObject = 1 == nStage;
    const bool bIncludeObjects = 1 <= nStage;

    if ( !bOnlyObject || IsFlyFrame() )
    {
        setFrameAreaSizeValid(true);
        setFramePrintAreaValid(true);
        setFrameAreaPositionValid(true);
    }

    if ( bIncludeObjects )
    {
        const SwSortedObjs* pObjs = GetDrawObjs();
        if ( pObjs )
        {
            const size_t nCnt = pObjs->size();
            for ( size_t i = 0; i < nCnt; ++i )
            {
                SwAnchoredObject* pAnchObj = (*pObjs)[i];
                if ( auto pFlyFrame = pAnchObj->DynCastFlyFrame() )
                    pFlyFrame->ValidateThisAndAllLowers( 2 );
                else if ( auto pAnchoredDrawObj = dynamic_cast<SwAnchoredDrawObject *>( pAnchObj ) )
                    pAnchoredDrawObj->ValidateThis();
            }
        }
    }

    if ( IsLayoutFrame() )
    {
        SwFrame* pLower = static_cast<SwLayoutFrame*>(this)->Lower();
        while ( pLower )
        {
            pLower->ValidateThisAndAllLowers( nStage );
            pLower = pLower->GetNext();
        }
    }
}

SwTwips SwContentFrame::GrowFrame(SwTwips nDist, SwResizeLimitReason& reason, bool bTst, bool bInfo)
{
    SwRectFnSet aRectFnSet(this);

    SwTwips nFrameHeight = aRectFnSet.GetHeight(getFrameArea());
    if( nFrameHeight > 0 &&
         nDist > (LONG_MAX - nFrameHeight ) )
        nDist = LONG_MAX - nFrameHeight;

    const SwViewShell *pSh = getRootFrame()->GetCurrShell();
    const bool bBrowse = pSh && pSh->GetViewOptions()->getBrowseMode();
    SwFrameType nTmpType = SwFrameType::Cell | SwFrameType::Column;
    if (bBrowse)
        nTmpType |= SwFrameType::Body;
    if( !(GetUpper()->GetType() & nTmpType) && GetUpper()->HasFixSize() )
    {
        if ( !bTst )
        {
            {
                SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
                aRectFnSet.SetHeight( aFrm, nFrameHeight + nDist );

                if( IsVertical() && !IsVertLR() )
                {
                    aFrm.Pos().AdjustX( -nDist );
                }
            }

            if ( GetNext() )
            {
                GetNext()->InvalidatePos();
            }
            // #i28701# - Due to the new object positioning the
            // frame on the next page/column can flow backward (e.g. it was moved forward
            // due to the positioning of its objects ). Thus, invalivate this next frame,
            // if document compatibility option 'Consider wrapping style influence on
            // object positioning' is ON.
            else if ( GetUpper()->GetFormat()->getIDocumentSettingAccess().get(DocumentSettingId::CONSIDER_WRAP_ON_OBJECT_POSITION) )
            {
                InvalidateNextPos();
            }
        }
        if (!nDist)
            reason = SwResizeLimitReason::Unspecified;
        else if (GetUpper()->IsBodyFrame() // Page / column body
                 || (GetUpper()->IsFlyFrame()
                     && static_cast<SwFlyFrame*>(GetUpper())->GetNextLink()))
            reason = SwResizeLimitReason::FlowToFollow;
        else
            reason = SwResizeLimitReason::FixedSizeFrame;
        return 0;
    }

    reason = SwResizeLimitReason::Unspecified;
    SwTwips nReal = aRectFnSet.GetHeight(GetUpper()->getFramePrintArea());
    for (SwFrame* pFrame = GetUpper()->Lower(); pFrame && nReal > 0; pFrame = pFrame->GetNext())
        nReal -= aRectFnSet.GetHeight(pFrame->getFrameArea());

    if ( !bTst )
    {
        //Contents are always resized to the wished value.
        tools::Long nOld = aRectFnSet.GetHeight(getFrameArea());

        {
            SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);

            aRectFnSet.SetHeight( aFrm, nOld + nDist );

            if( IsVertical()&& !IsVertLR() )
            {
                aFrm.Pos().AdjustX( -nDist );
            }
        }

        SwTabFrame *pTab = (nOld && IsInTab()) ? FindTabFrame() : nullptr;
        if (pTab)
        {
            SwDocShell* pShell = pTab->GetFormat()->GetDoc().GetDocShell();
            if ( pTab->GetTable()->GetHTMLTableLayout() &&
                 !pTab->IsJoinLocked() &&
                 pShell && !pShell->IsReadOnly() )
            {
                pTab->InvalidatePos();
                pTab->SetResizeHTMLTable();
            }
        }
    }

    //Only grow Upper if necessary.
    if ( nReal < nDist )
    {
        if( GetUpper() )
        {
            if( bTst || !GetUpper()->IsFooterFrame() )
                nReal = GetUpper()->Grow(nDist - std::max(nReal, SwTwips(0)), reason, bTst, bInfo);
            else
            {
                nReal = 0;
                GetUpper()->InvalidateSize();
            }
        }
        else
            nReal = 0;
    }
    else
        nReal = nDist;

    // #i28701# - Due to the new object positioning the
    // frame on the next page/column can flow backward (e.g. it was moved forward
    // due to the positioning of its objects ). Thus, invalivate this next frame,
    // if document compatibility option 'Consider wrapping style influence on
    // object positioning' is ON.
    if ( !bTst )
    {
        if ( GetNext() )
        {
            GetNext()->InvalidatePos();
        }
        else if ( GetUpper()->GetFormat()->getIDocumentSettingAccess().get(DocumentSettingId::CONSIDER_WRAP_ON_OBJECT_POSITION) )
        {
            InvalidateNextPos();
        }
    }

    return nReal;
}

SwTwips SwContentFrame::ShrinkFrame( SwTwips nDist, bool bTst, bool bInfo )
{
    SwRectFnSet aRectFnSet(this);
    OSL_ENSURE( nDist >= 0, "nDist < 0" );
    OSL_ENSURE( nDist <= aRectFnSet.GetHeight(getFrameArea()),
            "nDist > than current size." );

    if ( !bTst )
    {
        SwTwips nRstHeight;
        if( GetUpper() )
            nRstHeight = aRectFnSet.BottomDist( getFrameArea(), aRectFnSet.GetPrtBottom(*GetUpper()) );
        else
            nRstHeight = 0;
        if( nRstHeight < 0 )
        {
            SwTwips nNextHeight = 0;
            // i#94666 if WIDOW_MAGIC was set as height, nDist is wrong, need
            // to take into account all the frames in the section.
            if (GetUpper()->IsSctFrame()
                && sw::WIDOW_MAGIC - 20000 - getFrameArea().Top() < nDist)
            {
                SwFrame *pNxt = GetNext();
                while( pNxt )
                {
                    nNextHeight += aRectFnSet.GetHeight(pNxt->getFrameArea());
                    pNxt = pNxt->GetNext();
                }
            }
            nRstHeight = nDist + nRstHeight - nNextHeight;
        }
        else
        {
            nRstHeight = nDist;
        }

        {
            SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
            aRectFnSet.SetHeight( aFrm, aRectFnSet.GetHeight(aFrm) - nDist );

            if( IsVertical() && !IsVertLR() )
            {
                aFrm.Pos().AdjustX(nDist );
            }
        }

        nDist = nRstHeight;
        SwTabFrame *pTab = IsInTab() ? FindTabFrame() : nullptr;
        if (pTab)
        {
            SwDocShell* pShell = pTab->GetFormat()->GetDoc().GetDocShell();
            if ( pTab->GetTable()->GetHTMLTableLayout() &&
                 !pTab->IsJoinLocked() &&
                 pShell && !pShell->IsReadOnly() )
            {
                pTab->InvalidatePos();
                pTab->SetResizeHTMLTable();
            }
        }
    }

    SwTwips nReal;
    if( GetUpper() && nDist > 0 )
    {
        if( bTst || !GetUpper()->IsFooterFrame() )
            nReal = GetUpper()->Shrink( nDist, bTst, bInfo );
        else
        {
            nReal = 0;

            // #108745# Sorry, dear old footer friend, I'm not gonna invalidate you,
            // if there are any objects anchored inside your content, which
            // overlap with the shrinking frame.
            // This may lead to a footer frame that is too big, but this is better
            // than looping.
            // #109722# : The fix for #108745# was too strict.

            bool bInvalidate = true;
            const SwRect aRect( getFrameArea() );
            const SwPageFrame* pPage = FindPageFrame();
            const SwSortedObjs* pSorted = pPage ? pPage->GetSortedObjs() : nullptr;
            if( pSorted )
            {
                for (SwAnchoredObject* pAnchoredObj : *pSorted)
                {
                    const SwRect aBound( pAnchoredObj->GetObjRectWithSpaces() );

                    if( aBound.Left() > aRect.Right() )
                        continue;

                    if( aBound.Overlaps( aRect ) )
                    {
                        const SwFrameFormat* pFormat = pAnchoredObj->GetFrameFormat();
                        if( css::text::WrapTextMode_THROUGH != pFormat->GetSurround().GetSurround() )
                        {
                            const SwFrame* pAnchor = pAnchoredObj->GetAnchorFrame();
                            if ( pAnchor && pAnchor->FindFooterOrHeader() == GetUpper() )
                            {
                                bInvalidate = false;
                                break;
                            }
                        }
                    }
                }
            }

            if ( bInvalidate )
                GetUpper()->InvalidateSize();
        }
    }
    else
        nReal = 0;

    if ( !bTst )
    {
        //The position of the next Frame changes for sure.
        InvalidateNextPos();

        //If I don't have a successor I have to do the retouch by myself.
        if ( !GetNext() )
            SetRetouche();
    }
    return nReal;
}

void SwContentFrame::SwClientNotify(const SwModify& rMod, const SfxHint& rHint)
{
    if (rHint.GetId() != SfxHintId::SwLegacyModify && rHint.GetId() != SfxHintId::SwFormatChange
        && rHint.GetId() != SfxHintId::SwAttrSetChange)
        return;

    SwContentFrameInvFlags eInvFlags = SwContentFrameInvFlags::NONE;
    if (rHint.GetId() == SfxHintId::SwLegacyModify)
    {
        auto pLegacy = static_cast<const sw::LegacyModifyHint*>(&rHint);
        UpdateAttr_(pLegacy->m_pOld, pLegacy->m_pNew, eInvFlags);
    }
    else if (rHint.GetId() == SfxHintId::SwAttrSetChange)
    {
        auto pChangeHint = static_cast<const sw::AttrSetChangeHint*>(&rHint);
        if(pChangeHint->m_pNew && pChangeHint->m_pOld)
        {
            const SwAttrSetChg& rOldSetChg = *pChangeHint->m_pOld;
            const SwAttrSetChg& rNewSetChg = *pChangeHint->m_pNew;
            SfxItemIter aOIter(*rOldSetChg.GetChgSet());
            SfxItemIter aNIter(*rNewSetChg.GetChgSet());
            const SfxPoolItem* pNItem = aNIter.GetCurItem();
            const SfxPoolItem* pOItem = aOIter.GetCurItem();
            SwAttrSetChg aOldSet(rOldSetChg);
            SwAttrSetChg aNewSet(rNewSetChg);
            do
            {
                UpdateAttr_(pOItem, pNItem, eInvFlags, &aOldSet, &aNewSet);
                pNItem = aNIter.NextItem();
                pOItem = aOIter.NextItem();
            } while(pNItem);
            if(aOldSet.Count() || aNewSet.Count())
                SwFrame::SwClientNotify(rMod, sw::AttrSetChangeHint(&aOldSet, &aNewSet));
        }
    }
    else // rHint.GetId() == SfxHintId::SwFormatChange
    {
        auto pChangeHint = static_cast<const SwFormatChangeHint*>(&rHint);
        UpdateAttrForFormatChange(pChangeHint->m_pOldFormat, pChangeHint->m_pNewFormat, eInvFlags);
    }

    if(eInvFlags == SwContentFrameInvFlags::NONE)
        return;

    SwPageFrame* pPage = FindPageFrame();
    InvalidatePage(pPage);
    if(eInvFlags & SwContentFrameInvFlags::SetCompletePaint)
        SetCompletePaint();
    if(eInvFlags & SwContentFrameInvFlags::InvalidatePos)
        InvalidatePos_();
    if(eInvFlags & SwContentFrameInvFlags::InvalidateSize)
        InvalidateSize_();
    if(eInvFlags & (SwContentFrameInvFlags::InvalidateSectPrt | SwContentFrameInvFlags::SetNextCompletePaint))
    {
        if(IsInSct() && !GetPrev())
        {
            SwSectionFrame* pSect = FindSctFrame();
            if(pSect->ContainsAny() == this)
            {
                pSect->InvalidatePrt_();
                pSect->InvalidatePage(pPage);
            }
        }
        InvalidatePrt_();
    }
    SwFrame* pNextFrame = GetIndNext();
    if(pNextFrame && eInvFlags & SwContentFrameInvFlags::InvalidateNextPrt)
    {
        pNextFrame->InvalidatePrt_();
        pNextFrame->InvalidatePage(pPage);
    }
    if(pNextFrame && eInvFlags & SwContentFrameInvFlags::SetNextCompletePaint)
    {
        pNextFrame->SetCompletePaint();
    }
    if(eInvFlags & SwContentFrameInvFlags::InvalidatePrevPrt)
    {
        SwFrame* pPrevFrame = GetPrev();
        if(pPrevFrame)
        {
            pPrevFrame->InvalidatePrt_();
            pPrevFrame->InvalidatePage(pPage);
        }
    }
    if(eInvFlags & SwContentFrameInvFlags::InvalidateNextPos)
        InvalidateNextPos();
}

void SwContentFrame::UpdateAttr_( const SfxPoolItem* pOld, const SfxPoolItem* pNew,
                              SwContentFrameInvFlags &rInvFlags,
                            SwAttrSetChg *pOldSet, SwAttrSetChg *pNewSet )
{
    bool bClear = true;
    sal_uInt16 nWhich = pOld ? pOld->Which() : pNew ? pNew->Which() : 0;
    switch ( nWhich )
    {
        case RES_PAGEDESC:                      //attribute changes (on/off)
            if ( IsInDocBody() && !IsInTab() )
            {
                rInvFlags |= SwContentFrameInvFlags::InvalidatePos;
                SwPageFrame *pPage = FindPageFrame();
                if ( !GetPrev() )
                    CheckPageDescs( pPage );
                if (GetPageDescItem().GetNumOffset())
                    static_cast<SwRootFrame*>(pPage->GetUpper())->SetVirtPageNum( true );
                pPage->GetFormat()->GetDoc().getIDocumentFieldsAccess().UpdatePageFields(pPage->getFrameArea().Top());
            }
            break;

        case RES_UL_SPACE:
            {
                // OD 2004-02-18 #106629# - correction
                // Invalidation of the printing area of next frame, not only
                // for footnote content.
                if ( !GetIndNext() )
                {
                    SwFrame* pNxt = FindNext();
                    if ( pNxt )
                    {
                        SwPageFrame* pPg = pNxt->FindPageFrame();
                        pNxt->InvalidatePage( pPg );
                        pNxt->InvalidatePrt_();
                        if( pNxt->IsSctFrame() )
                        {
                            SwFrame* pCnt = static_cast<SwSectionFrame*>(pNxt)->ContainsAny();
                            if( pCnt )
                            {
                                pCnt->InvalidatePrt_();
                                pCnt->InvalidatePage( pPg );
                            }
                        }
                        pNxt->SetCompletePaint();
                    }
                }
                // OD 2004-03-17 #i11860#
                if ( GetIndNext() &&
                     !GetUpper()->GetFormat()->getIDocumentSettingAccess().get(DocumentSettingId::USE_FORMER_OBJECT_POS) )
                {
                    // OD 2004-07-01 #i28701# - use new method <InvalidateObjs(..)>
                    GetIndNext()->InvalidateObjs();
                }
                Prepare( PrepareHint::ULSpaceChanged );   //TextFrame has to correct line spacing.
                rInvFlags |= SwContentFrameInvFlags::SetNextCompletePaint;
                [[fallthrough]];
            }
        case RES_MARGIN_FIRSTLINE:
        case RES_MARGIN_TEXTLEFT:
        case RES_MARGIN_RIGHT:
        case RES_LR_SPACE:
        case RES_BOX:
        case RES_SHADOW:
            {
                Prepare( PrepareHint::FixSizeChanged );
                SwModify aMod;
                SwFrame::SwClientNotify(aMod, sw::LegacyModifyHint(pOld, pNew));
                rInvFlags |= SwContentFrameInvFlags::InvalidateNextPrt | SwContentFrameInvFlags::InvalidatePrevPrt;
                break;
            }
        case RES_BREAK:
            {
                rInvFlags |= SwContentFrameInvFlags::InvalidatePos | SwContentFrameInvFlags::InvalidateNextPos;
                const IDocumentSettingAccess& rIDSA = GetUpper()->GetFormat()->getIDocumentSettingAccess();
                if( rIDSA.get(DocumentSettingId::PARA_SPACE_MAX) ||
                    rIDSA.get(DocumentSettingId::PARA_SPACE_MAX_AT_PAGES) )
                {
                    rInvFlags |= SwContentFrameInvFlags::SetCompletePaint;
                    SwFrame* pNxt = FindNext();
                    if( pNxt )
                    {
                        SwPageFrame* pPg = pNxt->FindPageFrame();
                        pNxt->InvalidatePage( pPg );
                        pNxt->InvalidatePrt_();
                        if( pNxt->IsSctFrame() )
                        {
                            SwFrame* pCnt = static_cast<SwSectionFrame*>(pNxt)->ContainsAny();
                            if( pCnt )
                            {
                                pCnt->InvalidatePrt_();
                                pCnt->InvalidatePage( pPg );
                            }
                        }
                        pNxt->SetCompletePaint();
                    }
                }
            }
            break;

        // OD 2004-02-26 #i25029#
        case RES_PARATR_CONNECT_BORDER:
        {
            rInvFlags |= SwContentFrameInvFlags::SetCompletePaint;
            if ( IsTextFrame() )
            {
                InvalidateNextPrtArea();
            }
            if ( !GetIndNext() && IsInTab() && IsInSplitTableRow() )
            {
                FindTabFrame()->InvalidateSize();
            }
        }
        break;

        case RES_PARATR_TABSTOP:
        case RES_CHRATR_SHADOWED:
        case RES_CHRATR_AUTOKERN:
        case RES_CHRATR_UNDERLINE:
        case RES_CHRATR_OVERLINE:
        case RES_CHRATR_KERNING:
        case RES_CHRATR_FONT:
        case RES_CHRATR_FONTSIZE:
        case RES_CHRATR_ESCAPEMENT:
        case RES_CHRATR_CONTOUR:
        case RES_CHRATR_NOHYPHEN:
        case RES_PARATR_NUMRULE:
            rInvFlags |= SwContentFrameInvFlags::SetCompletePaint;
            break;

        case RES_FRM_SIZE:
            rInvFlags |= SwContentFrameInvFlags::SetCompletePaint;
            [[fallthrough]];

        default:
            bClear = false;
    }
    if ( !bClear )
        return;

    if ( pOldSet || pNewSet )
    {
        if ( pOldSet )
            pOldSet->ClearItem( nWhich );
        if ( pNewSet )
            pNewSet->ClearItem( nWhich );
    }
    else
    {
        SwModify aMod;
        SwFrame::SwClientNotify(aMod, sw::LegacyModifyHint(pOld, pNew));
    }
}

void SwContentFrame::UpdateAttrForFormatChange( SwFormat* pOldFormat, SwFormat* pNewFormat,
                              SwContentFrameInvFlags &rInvFlags )
{
    rInvFlags = SwContentFrameInvFlags::SetCompletePaint
                | SwContentFrameInvFlags::InvalidatePos
                | SwContentFrameInvFlags::InvalidateSize
                | SwContentFrameInvFlags::InvalidateSectPrt
                | SwContentFrameInvFlags::InvalidateNextPrt
                | SwContentFrameInvFlags::InvalidatePrevPrt
                | SwContentFrameInvFlags::InvalidateNextPos
                | SwContentFrameInvFlags::SetNextCompletePaint;

    if ( IsInDocBody() && !IsInTab() )
    {
        rInvFlags |= SwContentFrameInvFlags::InvalidatePos;
        SwPageFrame *pPage = FindPageFrame();
        if ( !GetPrev() )
            CheckPageDescs( pPage );
        if (GetPageDescItem().GetNumOffset())
            static_cast<SwRootFrame*>(pPage->GetUpper())->SetVirtPageNum( true );
        pPage->GetFormat()->GetDoc().getIDocumentFieldsAccess().UpdatePageFields(pPage->getFrameArea().Top());
    }

    SwModify aMod;
    SwFrame::SwClientNotify(aMod, SwFormatChangeHint(pOldFormat, pNewFormat));
}

SwLayoutFrame::SwLayoutFrame(SwFrameFormat *const pFormat, SwFrame *const pSib)
    : SwFrame(pFormat, pSib)
    , m_pLower(nullptr)
{
    const SwFormatFrameSize &rFormatSize = pFormat->GetFrameSize();
    if ( rFormatSize.GetHeightSizeType() == SwFrameSize::Fixed )
        mbFixSize = true;
}

// #i28701#

SwTwips SwLayoutFrame::InnerHeight() const
{
    const SwFrame* pCnt = Lower();
    if (!pCnt)
        return 0;

    SwRectFnSet aRectFnSet(this);
    SwTwips nRet = 0;
    if( pCnt->IsColumnFrame() || pCnt->IsCellFrame() )
    {
        do
        {
            SwTwips nTmp = static_cast<const SwLayoutFrame*>(pCnt)->InnerHeight();
            if( pCnt->isFramePrintAreaValid() )
                nTmp += aRectFnSet.GetHeight(pCnt->getFrameArea()) -
                        aRectFnSet.GetHeight(pCnt->getFramePrintArea());
            if( nRet < nTmp )
                nRet = nTmp;
            pCnt = pCnt->GetNext();
        } while ( pCnt );
    }
    else
    {
        do
        {
            nRet += aRectFnSet.GetHeight(pCnt->getFrameArea());
            if( pCnt->IsContentFrame() && static_cast<const SwTextFrame*>(pCnt)->IsUndersized() )
                nRet += static_cast<const SwTextFrame*>(pCnt)->GetParHeight() -
                        aRectFnSet.GetHeight(pCnt->getFramePrintArea());
            if( pCnt->IsLayoutFrame() && !pCnt->IsTabFrame() )
                nRet += static_cast<const SwLayoutFrame*>(pCnt)->InnerHeight() -
                        aRectFnSet.GetHeight(pCnt->getFramePrintArea());
            pCnt = pCnt->GetNext();
        } while( pCnt );

    }
    return nRet;
}

SwTwips SwLayoutFrame::GrowFrame(SwTwips nDist, SwResizeLimitReason& reason, bool bTst, bool bInfo)
{
    const SwViewShell *pSh = getRootFrame()->GetCurrShell();
    const bool bBrowse = pSh && pSh->GetViewOptions()->getBrowseMode();
    SwFrameType nTmpType = SwFrameType::Cell | SwFrameType::Column;
    if (bBrowse)
        nTmpType |= SwFrameType::Body;
    if( !(GetType() & nTmpType) && HasFixSize() )
    {
        if (nDist <= 0)
            reason = SwResizeLimitReason::Unspecified;
        else
            reason = IsBodyFrame() ? SwResizeLimitReason::FlowToFollow // Page / column body
                                   : SwResizeLimitReason::FixedSizeFrame;
        return 0;
    }

    SwRectFnSet aRectFnSet(this);
    const SwTwips nFrameHeight = aRectFnSet.GetHeight(getFrameArea());
    const SwTwips nFramePos = getFrameArea().Pos().X();

    if ( nFrameHeight > 0 && nDist > (LONG_MAX - nFrameHeight) )
        nDist = LONG_MAX - nFrameHeight;

    SwTwips nMin = 0;
    if ( GetUpper() && !IsCellFrame() )
    {
        for (SwFrame* pFrame = GetUpper()->Lower(); pFrame; pFrame = pFrame->GetNext())
            nMin += aRectFnSet.GetHeight(pFrame->getFrameArea());
        nMin = aRectFnSet.GetHeight(GetUpper()->getFramePrintArea()) - nMin;
        if ( nMin < 0 )
            nMin = 0;
    }

    SwRect aOldFrame( getFrameArea() );
    bool bMoveAccFrame = false;

    bool bChgPos = IsVertical();
    if ( !bTst )
    {
        SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
        aRectFnSet.SetHeight( aFrm, nFrameHeight + nDist );

        if( bChgPos && !IsVertLR() )
        {
            aFrm.Pos().AdjustX( -nDist );
        }

        bMoveAccFrame = true;
    }

    reason = SwResizeLimitReason::Unspecified;
    SwTwips nReal = nDist - nMin;
    if ( nReal > 0 )
    {
        if ( GetUpper() )
        {   // AdjustNeighbourhood now only for the columns (but not in frames)
            SwNeighbourAdjust nAdjust = GetUpper()->IsFootnoteBossFrame() ?
                static_cast<SwFootnoteBossFrame*>(GetUpper())->NeighbourhoodAdjustment()
                : SwNeighbourAdjust::GrowShrink;
            if( SwNeighbourAdjust::OnlyAdjust == nAdjust )
                nReal = AdjustNeighbourhood( nReal, bTst );
            else
            {
                if( SwNeighbourAdjust::AdjustGrow == nAdjust )
                    nReal += AdjustNeighbourhood( nReal, bTst );

                SwTwips nGrow = 0;
                if( nReal > 0 )
                {
                    SwFrame* pToGrow = GetUpper();
                    // NEW TABLES
                    // A cell with a row span of > 1 is allowed to grow the
                    // line containing the end of the row span if it is
                    // located in the same table frame:
                    if (IsCellFrame())
                    {
                        const SwCellFrame* pThisCell = static_cast<const SwCellFrame*>(this);
                        if ( pThisCell->GetLayoutRowSpan() > 1 )
                        {
                            SwCellFrame& rEndCell = const_cast<SwCellFrame&>(pThisCell->FindStartEndOfRowSpanCell( false ));
                            if ( -1 == rEndCell.GetTabBox()->getRowSpan() )
                                pToGrow = rEndCell.GetUpper();
                            else
                            {
                                pToGrow = nullptr;
                                reason = SwResizeLimitReason::FlowToFollow;
                            }
                        }
                    }
                    nGrow = pToGrow ? pToGrow->Grow(nReal, reason, bTst, bInfo) : 0;
                }

                if( SwNeighbourAdjust::GrowAdjust == nAdjust && nGrow < nReal )
                    nReal = o3tl::saturating_add(nReal, AdjustNeighbourhood( nReal - nGrow, bTst ));

                if ( IsFootnoteFrame() && (nGrow != nReal) && GetNext() )
                {
                    //Footnotes can replace their successor.
                    SwTwips nSpace = bTst ? 0 : -nDist;
                    if (const SwFrame *pFrame = GetUpper()->Lower())
                    {
                        do
                        {   nSpace += aRectFnSet.GetHeight(pFrame->getFrameArea());
                            pFrame = pFrame->GetNext();
                        } while ( pFrame != GetNext() );
                    }
                    nSpace = aRectFnSet.GetHeight(GetUpper()->getFramePrintArea()) -nSpace;
                    if ( nSpace < 0 )
                        nSpace = 0;
                    nSpace += nGrow;
                    if ( nReal > nSpace )
                        nReal = nSpace;
                    if ( nReal && !bTst )
                        static_cast<SwFootnoteFrame*>(this)->InvalidateNxtFootnoteCnts( FindPageFrame() );
                }
                else
                    nReal = nGrow;
            }
        }
        else
            nReal = 0;

        nReal += nMin;
    }
    else
        nReal = nDist;

    if ( !bTst )
    {
        if( nReal != nDist &&
            // NEW TABLES
            ( !IsCellFrame() || static_cast<SwCellFrame*>(this)->GetLayoutRowSpan() > 1 ) )
        {
            SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
            aRectFnSet.SetHeight( aFrm, nFrameHeight + nReal );

            if( bChgPos && !IsVertLR() )
            {
                aFrm.Pos().setX( nFramePos - nReal );
            }

            bMoveAccFrame = true;
        }

        if ( nReal )
        {
            SwPageFrame *pPage = FindPageFrame();
            if ( GetNext() )
            {
                SwFrame * pNext = GetNext();
                do
                {
                    pNext->InvalidatePos_();
                    if (pNext->IsRowFrame())
                    {   // also invalidate first cell
                        static_cast<SwLayoutFrame*>(pNext)->Lower()->InvalidatePos_();
                    }
                    else if (pNext->IsContentFrame())
                    {
                        pNext->InvalidatePage(pPage);
                    }
                    if (pNext->HasFixSize())
                    {   // continue to invalidate because growing pNext won't do it!
                        pNext = pNext->GetNext();
                    }
                    else
                    {
                        break;
                    }
                }
                while (pNext);
            }
            if ( !IsPageBodyFrame() )
            {
                InvalidateAll_();
                InvalidatePage( pPage );
            }
            if (!(GetType() & (SwFrameType::Row|SwFrameType::Tab|SwFrameType::FootnoteContainer|SwFrameType::Page|SwFrameType::Root)))
                NotifyLowerObjs();

            if( IsCellFrame() )
                InvaPercentLowers( nReal );

            std::unique_ptr<SvxBrushItem> aBack(GetFormat()->makeBackgroundBrushItem());
            const SvxGraphicPosition ePos = aBack->GetGraphicPos();
            if ( GPOS_NONE != ePos && GPOS_TILED != ePos )
                SetCompletePaint();
        }
    }

#if !ENABLE_WASM_STRIP_ACCESSIBILITY
    if( bMoveAccFrame && IsAccessibleFrame() )
    {
        SwRootFrame *pRootFrame = getRootFrame();
        if( pRootFrame && pRootFrame->IsAnyShellAccessible() &&
            pRootFrame->GetCurrShell() )
        {
            pRootFrame->GetCurrShell()->Imp()->MoveAccessibleFrame( this, aOldFrame );
        }
    }
#else
    (void)bMoveAccFrame;
    (void)aOldFrame;
#endif

    if (reason == SwResizeLimitReason::Unspecified && nReal < nDist && IsBodyFrame()) // Page / column body
        reason = SwResizeLimitReason::FlowToFollow;

    return nReal;
}

SwTwips SwLayoutFrame::ShrinkFrame( SwTwips nDist, bool bTst, bool bInfo )
{
    const SwViewShell *pSh = getRootFrame()->GetCurrShell();
    const bool bBrowse = pSh && pSh->GetViewOptions()->getBrowseMode();
    SwFrameType nTmpType = SwFrameType::Cell | SwFrameType::Column;
    if (bBrowse)
        nTmpType |= SwFrameType::Body;

    if (pSh && pSh->GetViewOptions()->IsWhitespaceHidden())
    {
        if (IsBodyFrame())
        {
            // Whitespace is hidden and this body frame will not shrink, as it
            // has a fix size.
            // Invalidate the page frame size, so in case the reason for the
            // shrink was that there is more whitespace on this page, the size
            // without whitespace will be recalculated correctly.
            SwPageFrame* pPageFrame = FindPageFrame();
            pPageFrame->InvalidateSize();
        }
    }

    if( !(GetType() & nTmpType) && HasFixSize() )
        return 0;

    OSL_ENSURE( nDist >= 0, "nDist < 0" );
    SwRectFnSet aRectFnSet(this);
    SwTwips nFrameHeight = aRectFnSet.GetHeight(getFrameArea());
    if ( nDist > nFrameHeight )
        nDist = nFrameHeight;

    SwTwips nMin = 0;
    bool bChgPos = IsVertical();
    if (const SwFrame *pFrame = Lower())
    {
        if( !pFrame->IsNeighbourFrame() )
        {
            const tools::Long nTmp = aRectFnSet.GetHeight(getFramePrintArea());
            while( pFrame && nMin < nTmp )
            {   nMin += aRectFnSet.GetHeight(pFrame->getFrameArea());
                pFrame = pFrame->GetNext();
            }
        }
    }
    SwTwips nReal = nDist;
    SwTwips nMinDiff = aRectFnSet.GetHeight(getFramePrintArea()) - nMin;
    if( nReal > nMinDiff )
        nReal = nMinDiff;
    if( nReal <= 0 )
        return nDist;

    SwRect aOldFrame( getFrameArea() );
    bool bMoveAccFrame = false;

    SwTwips nRealDist = nReal;
    if ( !bTst )
    {
        SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
        aRectFnSet.SetHeight( aFrm, nFrameHeight - nReal );

        if( bChgPos && !IsVertLR() )
        {
            aFrm.Pos().AdjustX(nReal );
        }

        bMoveAccFrame = true;
    }

    SwNeighbourAdjust nAdjust = GetUpper() && GetUpper()->IsFootnoteBossFrame() ?
                   static_cast<SwFootnoteBossFrame*>(GetUpper())->NeighbourhoodAdjustment()
                   : SwNeighbourAdjust::GrowShrink;

    // AdjustNeighbourhood also in columns (but not in frames)
    if( SwNeighbourAdjust::OnlyAdjust == nAdjust )
    {
        if ( IsPageBodyFrame() && !bBrowse )
            nReal = nDist;
        else
        {   nReal = AdjustNeighbourhood( -nReal, bTst );
            nReal *= -1;
            if ( !bTst && IsBodyFrame() && nReal < nRealDist )
            {
                SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
                aRectFnSet.SetHeight( aFrm, aRectFnSet.GetHeight(aFrm) + nRealDist - nReal );

                if( bChgPos && !IsVertLR() )
                {
                    aFrm.Pos().AdjustX(nRealDist - nReal );
                }

                OSL_ENSURE( !IsAccessibleFrame(), "bMoveAccFrame has to be set!" );
            }
        }
    }
    else if( IsColumnFrame() || IsColBodyFrame() )
    {
        SwTwips nTmp = GetUpper()->Shrink( nReal, bTst, bInfo );
        if ( nTmp != nReal )
        {
            SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
            aRectFnSet.SetHeight( aFrm, aRectFnSet.GetHeight(aFrm) + nReal - nTmp );

            if( bChgPos && !IsVertLR() )
            {
                aFrm.Pos().AdjustX(nTmp - nReal );
            }

            OSL_ENSURE( !IsAccessibleFrame(), "bMoveAccFrame has to be set!" );
            nReal = nTmp;
        }
    }
    else
    {
        SwTwips nShrink = nReal;
        SwFrame* pToShrink = GetUpper();
        // NEW TABLES
        if ( IsCellFrame() )
        {
            const SwCellFrame* pThisCell = static_cast<const SwCellFrame*>(this);
            if ( pThisCell->GetLayoutRowSpan() > 1 )
            {
                SwCellFrame& rEndCell = const_cast<SwCellFrame&>(pThisCell->FindStartEndOfRowSpanCell( false ));
                pToShrink = rEndCell.GetUpper();
            }
        }

        // A table frame may have grown beyond its parent frame after
        // RemoveFollowFlowLine(), which is a problem in case the parent is a
        // section: prevent shrinking the section smaller than the contained
        // table.
        if (IsTabFrame()
            && static_cast<SwTabFrame*>(this)->IsRebuildLastLine()
            && pToShrink == GetUpper()
            && pToShrink->IsSctFrame()) // not required for page body, unsure about others
        {
            SwTwips nUpperMin{0};
            for (SwFrame const* pFrame = pToShrink->GetLower();
                 pFrame != GetNext(); pFrame = pFrame->GetNext())
            {
                nUpperMin += aRectFnSet.GetHeight(pFrame->getFrameArea());
            }
            if (aRectFnSet.GetHeight(pToShrink->getFramePrintArea()) - nShrink < nUpperMin)
            {
                nShrink = aRectFnSet.GetHeight(pToShrink->getFramePrintArea()) - nUpperMin;
                if (nShrink <= 0)
                {
                    return 0; // nothing to do
                }
            }
        }
        nReal = pToShrink ? pToShrink->Shrink( nShrink, bTst, bInfo ) : 0;
        if( ( SwNeighbourAdjust::GrowAdjust == nAdjust || SwNeighbourAdjust::AdjustGrow == nAdjust )
            && nReal < nShrink )
            AdjustNeighbourhood( nReal - nShrink );
    }

#if !ENABLE_WASM_STRIP_ACCESSIBILITY
    if( bMoveAccFrame && IsAccessibleFrame() )
    {
        SwRootFrame *pRootFrame = getRootFrame();
        if( pRootFrame && pRootFrame->IsAnyShellAccessible() &&
            pRootFrame->GetCurrShell() )
        {
            pRootFrame->GetCurrShell()->Imp()->MoveAccessibleFrame( this, aOldFrame );
        }
    }
#else
    (void)aOldFrame;
    (void)bMoveAccFrame;
#endif

    if ( !bTst && (IsCellFrame() || IsColumnFrame() ? nReal : nRealDist) )
    {
        SwPageFrame *pPage = FindPageFrame();
        if ( GetNext() )
        {
            GetNext()->InvalidatePos_();
            if ( GetNext()->IsContentFrame() )
                GetNext()->InvalidatePage( pPage );
            if ( IsTabFrame() )
                static_cast<SwTabFrame*>(this)->SetComplete();
        }
        else
        {   if ( IsRetoucheFrame() )
                SetRetouche();
            if ( IsTabFrame() )
            {
                static_cast<SwTabFrame*>(this)->SetComplete();
                if ( Lower() )  // Can also be in the Join and be empty!
                    InvalidateNextPos();
            }
        }
        if ( !IsBodyFrame() )
        {
            InvalidateAll_();
            InvalidatePage( pPage );
            bool bCompletePaint = true;
            const SwFrameFormat* pFormat = GetFormat();
            if (pFormat)
            {
                std::unique_ptr<SvxBrushItem> aBack(pFormat->makeBackgroundBrushItem());
                const SvxGraphicPosition ePos = aBack->GetGraphicPos();
                if ( GPOS_NONE == ePos || GPOS_TILED == ePos )
                    bCompletePaint = false;
            }
            if (bCompletePaint)
                SetCompletePaint();
        }

        if (!(GetType() & (SwFrameType::Row|SwFrameType::Tab|SwFrameType::FootnoteContainer|SwFrameType::Page|SwFrameType::Root)))
            NotifyLowerObjs();

        if( IsCellFrame() )
            InvaPercentLowers( nReal );

        SwContentFrame *pCnt;
        if( IsFootnoteFrame() && !static_cast<SwFootnoteFrame*>(this)->GetAttr()->GetFootnote().IsEndNote() &&
            ( GetFormat()->GetDoc().GetFootnoteInfo().m_ePos != FTNPOS_CHAPTER ||
              ( IsInSct() && FindSctFrame()->IsFootnoteAtEnd() ) ) &&
              nullptr != (pCnt = static_cast<SwFootnoteFrame*>(this)->GetRefFromAttr() ) )
        {
            if ( pCnt->IsFollow() )
            {   // If we are in another column/page than the frame with the
                // reference, we don't need to invalidate its master.
                SwFrame *pTmp = pCnt->FindFootnoteBossFrame(true) == FindFootnoteBossFrame(true)
                              ?  &pCnt->FindMaster()->GetFrame() : pCnt;
                pTmp->Prepare( PrepareHint::AdjustSizeWithoutFormatting );
                pTmp->InvalidateSize();
            }
            else
            {
                if (pCnt->FindPageFrame() == FindPageFrame())
                {
                    pCnt->InvalidatePos();
                }
                else
                {
                    SAL_WARN("sw.layout", "footnote frame on different page than ref frame?");
                }
            }
        }
    }
    return nReal;
}

/**
 * Changes the size of the directly subsidiary Frame's that have a fixed size, proportionally to the
 * size change of the PrtArea of the Frame's.
 *
 * The variable Frames are also proportionally adapted; they will grow/shrink again by themselves.
 */
void SwLayoutFrame::ChgLowersProp( const Size& rOldSize )
{
    // no change of lower properties for root frame or if no lower exists.
    if ( IsRootFrame() || !Lower() )
        return;

    // declare and init <SwFrame* pLowerFrame> with first lower
    SwFrame *pLowerFrame = Lower();

    // declare and init const booleans <bHeightChgd> and <bWidthChg>
    const bool bHeightChgd = rOldSize.Height() != getFramePrintArea().Height();
    const bool bWidthChgd  = rOldSize.Width()  != getFramePrintArea().Width();

    SwRectFnSet aRectFnSet(this);

    // This shortcut basically tries to handle only lower frames that
    // are affected by the size change. Otherwise much more lower frames
    // are invalidated.
    if ( !( aRectFnSet.IsVert() ? bHeightChgd : bWidthChgd ) &&
         ! Lower()->IsColumnFrame() &&
           ( ( IsBodyFrame() && IsInDocBody() && ( !IsInSct() || !FindSctFrame()->IsColLocked() ) ) ||
                // #i10826# Section frames without columns should not
                // invalidate all lowers!
               IsSctFrame() ) )
    {
        // Determine page frame the body frame resp. the section frame belongs to.
        SwPageFrame *pPage = FindPageFrame();
        // Determine last lower by traveling through them using <GetNext()>.
        // During travel check each section frame, if it will be sized to
        // maximum. If Yes, invalidate size of section frame and set
        // corresponding flags at the page.
        do
        {
            if( pLowerFrame->IsSctFrame() && static_cast<SwSectionFrame*>(pLowerFrame)->ToMaximize_() )
            {
                pLowerFrame->InvalidateSize_();
                pLowerFrame->InvalidatePage( pPage );
            }
            if( pLowerFrame->GetNext() )
                pLowerFrame = pLowerFrame->GetNext();
            else
                break;
        } while( true );
        // If found last lower is a section frame containing no section
        // (section frame isn't valid and will be deleted in the future),
        // travel backwards.
        while( pLowerFrame->IsSctFrame() && !static_cast<SwSectionFrame*>(pLowerFrame)->GetSection() &&
               pLowerFrame->GetPrev() )
            pLowerFrame = pLowerFrame->GetPrev();
        // If found last lower is a section frame, set <pLowerFrame> to its last
        // content, if the section frame is valid and is not sized to maximum.
        // Otherwise set <pLowerFrame> to NULL - In this case body frame only
        //      contains invalid section frames.
        if( pLowerFrame->IsSctFrame() )
            pLowerFrame = static_cast<SwSectionFrame*>(pLowerFrame)->GetSection() &&
                   !static_cast<SwSectionFrame*>(pLowerFrame)->ToMaximize( false ) ?
                   static_cast<SwSectionFrame*>(pLowerFrame)->FindLastContent() : nullptr;

        // continue with found last lower, probably the last content of a section
        if ( pLowerFrame )
        {
            // If <pLowerFrame> is in a table frame, set <pLowerFrame> to this table
            // frame and continue.
            if ( pLowerFrame->IsInTab() )
            {
                // OD 28.10.2002 #97265# - safeguard for setting <pLowerFrame> to
                // its table frame - check, if the table frame is also a lower
                // of the body frame, in order to assure that <pLowerFrame> is not
                // set to a frame, which is an *upper* of the body frame.
                SwFrame* pTableFrame = pLowerFrame->FindTabFrame();
                if ( IsAnLower( pTableFrame ) )
                {
                    pLowerFrame = pTableFrame;
                }
            }
            // Check, if variable size of body frame resp. section frame has grown
            // OD 28.10.2002 #97265# - correct check, if variable size has grown.
            SwTwips nOldHeight = aRectFnSet.IsVert() ? rOldSize.Width() : rOldSize.Height();
            if( nOldHeight < aRectFnSet.GetHeight(getFramePrintArea()) )
            {
                // If variable size of body|section frame has grown, only found
                // last lower and the position of the its next have to be invalidated.
                pLowerFrame->InvalidateAll_();
                pLowerFrame->InvalidatePage( pPage );
                if( !pLowerFrame->IsFlowFrame() ||
                    !SwFlowFrame::CastFlowFrame( pLowerFrame )->HasFollow() )
                    pLowerFrame->InvalidateNextPos( true );
                if ( pLowerFrame->IsTextFrame() )
                    static_cast<SwContentFrame*>(pLowerFrame)->Prepare( PrepareHint::AdjustSizeWithoutFormatting );
            }
            else
            {
                SwFrame const* pFirstInvalid(nullptr);
                for (SwFrame const* pLow = Lower();
                     pLow && pLow != pLowerFrame; pLow = pLow->GetNext())
                {
                    if (!pLow->isFrameAreaDefinitionValid())
                    {
                        pFirstInvalid = pLow;
                        break;
                    }
                }
                // variable size of body|section frame has shrunk. Thus,
                // invalidate all lowers not matching the new body|section size
                // and the dedicated new last lower.
                if( aRectFnSet.IsVert() )
                {
                    SwTwips nBot = getFrameArea().Left() + getFramePrintArea().Left();
                    while (pLowerFrame && pLowerFrame->GetPrev()
                        && (pFirstInvalid != nullptr // tdf#152307 trust nothing after invalid frame
                            || pLowerFrame->getFrameArea().Left() < nBot))
                    {
                        pLowerFrame->InvalidateAll_();
                        pLowerFrame->InvalidatePage( pPage );
                        if (pLowerFrame == pFirstInvalid)
                        {
                            pFirstInvalid = nullptr; // continue checking nBot
                        }
                        pLowerFrame = pLowerFrame->GetPrev();
                    }
                }
                else
                {
                    SwTwips nBot = getFrameArea().Top() + getFramePrintArea().Bottom();
                    while (pLowerFrame && pLowerFrame->GetPrev()
                        && (pFirstInvalid != nullptr // tdf#152307 trust nothing after invalid frame
                            || nBot < pLowerFrame->getFrameArea().Top()))
                    {
                        pLowerFrame->InvalidateAll_();
                        pLowerFrame->InvalidatePage( pPage );
                        if (pLowerFrame == pFirstInvalid)
                        {
                            pFirstInvalid = nullptr; // continue checking nBot
                        }
                        pLowerFrame = pLowerFrame->GetPrev();
                    }
                }
                if ( pLowerFrame )
                {
                    pLowerFrame->InvalidateSize_();
                    pLowerFrame->InvalidatePage( pPage );
                    if ( pLowerFrame->IsTextFrame() )
                        static_cast<SwContentFrame*>(pLowerFrame)->Prepare( PrepareHint::AdjustSizeWithoutFormatting );
                }
            }
            // #i41694# - improvement by removing duplicates
            if ( pLowerFrame )
            {
                if ( pLowerFrame->IsInSct() )
                {
                    // #i41694# - follow-up of issue #i10826#
                    // No invalidation of section frame, if it's the this.
                    SwFrame* pSectFrame = pLowerFrame->FindSctFrame();
                    if( pSectFrame != this && IsAnLower( pSectFrame ) )
                    {
                        pSectFrame->InvalidateSize_();
                        pSectFrame->InvalidatePage( pPage );
                    }
                }
            }
        }
        return;
    } // end of { special case }

    // Invalidate page for content only once.
    bool bInvaPageForContent = true;

    // Declare booleans <bFixChgd> and <bVarChgd>, indicating for text frame
    // adjustment, if fixed/variable size has changed.
    bool bFixChgd, bVarChgd;
    if( aRectFnSet.IsVert() == pLowerFrame->IsNeighbourFrame() )
    {
        bFixChgd = bWidthChgd;
        bVarChgd = bHeightChgd;
    }
    else
    {
        bFixChgd = bHeightChgd;
        bVarChgd = bWidthChgd;
    }

    // Declare const unsigned short <nFixWidth> and init it this frame types
    // which has fixed width in vertical respectively horizontal layout.
    // In vertical layout these are neighbour frames (cell and column frames),
    //      header frames and footer frames.
    // In horizontal layout these are all frames, which aren't neighbour frames.
    const SwFrameType nFixWidth = aRectFnSet.IsVert() ? (FRM_NEIGHBOUR | FRM_HEADFOOT)
                                   : ~FRM_NEIGHBOUR;

    // Declare const unsigned short <nFixHeight> and init it this frame types
    // which has fixed height in vertical respectively horizontal layout.
    // In vertical layout these are all frames, which aren't neighbour frames,
    //      header frames, footer frames, body frames or foot note container frames.
    // In horizontal layout these are neighbour frames.
    const SwFrameType nFixHeight = aRectFnSet.IsVert() ? ~SwFrameType(FRM_NEIGHBOUR | FRM_HEADFOOT | FRM_BODYFTNC)
                                   : FRM_NEIGHBOUR;

    // Travel through all lowers using <GetNext()>
    while ( pLowerFrame )
    {
        if ( pLowerFrame->IsTextFrame() )
        {
            // Text frames will only be invalidated - prepare invalidation
            if ( bFixChgd )
                static_cast<SwContentFrame*>(pLowerFrame)->Prepare( PrepareHint::FixSizeChanged );
            if ( bVarChgd )
                static_cast<SwContentFrame*>(pLowerFrame)->Prepare( PrepareHint::AdjustSizeWithoutFormatting );
        }
        else
        {
            // If lower isn't a table, row, cell or section frame, adjust its
            // frame size.
            const SwFrameType nLowerType = pLowerFrame->GetType();
            if ( !(nLowerType & (SwFrameType::Tab|SwFrameType::Row|SwFrameType::Cell|SwFrameType::Section)) )
            {
                if ( bWidthChgd )
                {
                    if( nLowerType & nFixWidth )
                    {
                        // Considering previous conditions:
                        // In vertical layout set width of column, header and
                        // footer frames to its upper width.
                        // In horizontal layout set width of header, footer,
                        // foot note container, foot note, body and no-text
                        // frames to its upper width.
                        SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*pLowerFrame);
                        aFrm.Width( getFramePrintArea().Width() );
                    }
                    else if( rOldSize.Width() && !pLowerFrame->IsFootnoteFrame() )
                    {
                        // Adjust frame width proportional, if lower isn't a
                        // foot note frame and condition <nLowerType & nFixWidth>
                        // isn't true.
                        // Considering previous conditions:
                        // In vertical layout these are foot note container,
                        // body and no-text frames.
                        // In horizontal layout these are column and no-text frames.
                        // OD 24.10.2002 #97265# - <double> calculation
                        // Perform <double> calculation of new width, if
                        // one of the coefficients is greater than 50000
                        SwTwips nNewWidth;
                        if ( (pLowerFrame->getFrameArea().Width() > 50000) ||
                             (getFramePrintArea().Width() > 50000) )
                        {
                            double nNewWidthTmp =
                                ( double(pLowerFrame->getFrameArea().Width())
                                  * double(getFramePrintArea().Width()) )
                                / double(rOldSize.Width());
                            nNewWidth = SwTwips(nNewWidthTmp);
                        }
                        else
                        {
                            nNewWidth =
                                (pLowerFrame->getFrameArea().Width() * getFramePrintArea().Width()) / rOldSize.Width();
                        }

                        SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*pLowerFrame);
                        aFrm.Width( nNewWidth );
                    }
                }
                if ( bHeightChgd )
                {
                    if( nLowerType & nFixHeight )
                    {
                        // Considering previous conditions:
                        // In vertical layout set height of foot note and
                        // no-text frames to its upper height.
                        // In horizontal layout set height of column frames
                        // to its upper height.
                        SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*pLowerFrame);
                        aFrm.Height( getFramePrintArea().Height() );
                    }
                    // OD 01.10.2002 #102211#
                    // add conditions <!pLowerFrame->IsHeaderFrame()> and
                    // <!pLowerFrame->IsFooterFrame()> in order to avoid that
                    // the <Grow> of header or footer are overwritten.
                    // NOTE: Height of header/footer frame is determined by contents.
                    else if ( rOldSize.Height() &&
                              !pLowerFrame->IsFootnoteFrame() &&
                              !pLowerFrame->IsHeaderFrame() &&
                              !pLowerFrame->IsFooterFrame()
                            )
                    {
                        // Adjust frame height proportional, if lower isn't a
                        // foot note, a header or a footer frame and
                        // condition <nLowerType & nFixHeight> isn't true.
                        // Considering previous conditions:
                        // In vertical layout these are column, foot note container,
                        // body and no-text frames.
                        // In horizontal layout these are column, foot note
                        // container, body and no-text frames.

                        // OD 29.10.2002 #97265# - special case for page lowers
                        // The page lowers that have to be adjusted on page height
                        // change are the body frame and the foot note container
                        // frame.
                        // In vertical layout the height of both is directly
                        // adjusted to the page height change.
                        // In horizontal layout the height of the body frame is
                        // directly adjusted to the page height change and the
                        // foot note frame height isn't touched, because its
                        // determined by its content.
                        // OD 31.03.2003 #108446# - apply special case for page
                        // lowers - see description above - also for section columns.
                        if ( IsPageFrame() ||
                             ( IsColumnFrame() && IsInSct() )
                           )
                        {
                            OSL_ENSURE( pLowerFrame->IsBodyFrame() || pLowerFrame->IsFootnoteContFrame(),
                                    "ChgLowersProp - only for body or foot note container" );
                            if ( pLowerFrame->IsBodyFrame() || pLowerFrame->IsFootnoteContFrame() )
                            {
                                if ( IsVertical() || pLowerFrame->IsBodyFrame() )
                                {
                                    SwTwips nNewHeight =
                                            pLowerFrame->getFrameArea().Height() +
                                            ( getFramePrintArea().Height() - rOldSize.Height() );
                                    if ( nNewHeight < 0)
                                    {
                                        // OD 01.04.2003 #108446# - adjust assertion condition and text
                                        OSL_ENSURE( !( IsPageFrame() &&
                                                   (pLowerFrame->getFrameArea().Height()>0) &&
                                                   (pLowerFrame->isFrameAreaDefinitionValid()) ),
                                                    "ChgLowersProg - negative height for lower.");
                                        nNewHeight = 0;
                                    }

                                    SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*pLowerFrame);
                                    aFrm.Height( nNewHeight );
                                }
                            }
                        }
                        else
                        {
                            SwTwips nNewHeight;
                            // OD 24.10.2002 #97265# - <double> calculation
                            // Perform <double> calculation of new height, if
                            // one of the coefficients is greater than 50000
                            if ( (pLowerFrame->getFrameArea().Height() > 50000) ||
                                 (getFramePrintArea().Height() > 50000) )
                            {
                                double nNewHeightTmp =
                                    ( double(pLowerFrame->getFrameArea().Height())
                                      * double(getFramePrintArea().Height()) )
                                    / double(rOldSize.Height());
                                nNewHeight = SwTwips(nNewHeightTmp);
                            }
                            else
                            {
                                nNewHeight = ( pLowerFrame->getFrameArea().Height()
                                             * getFramePrintArea().Height() ) / rOldSize.Height();
                            }
                            if( !pLowerFrame->GetNext() )
                            {
                                SwTwips nSum = getFramePrintArea().Height();
                                SwFrame* pTmp = Lower();
                                while( pTmp->GetNext() )
                                {
                                    if( !pTmp->IsFootnoteContFrame() || !pTmp->IsVertical() )
                                        nSum -= pTmp->getFrameArea().Height();
                                    pTmp = pTmp->GetNext();
                                }
                                if( nSum - nNewHeight == 1 &&
                                    nSum == pLowerFrame->getFrameArea().Height() )
                                    nNewHeight = nSum;
                            }

                            SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*pLowerFrame);
                            aFrm.Height( nNewHeight );
                        }
                    }
                }
            }
        } // end of else { NOT text frame }

        pLowerFrame->InvalidateAll_();
        if ( bInvaPageForContent && pLowerFrame->IsContentFrame() )
        {
            pLowerFrame->InvalidatePage();
            bInvaPageForContent = false;
        }

        if ( !pLowerFrame->GetNext() && pLowerFrame->IsRetoucheFrame() )
        {
            //If a growth took place and the subordinate elements can retouch
            //itself (currently Tabs, Sections and Content) we trigger it.
            if ( rOldSize.Height() < getFramePrintArea().SSize().Height() ||
                 rOldSize.Width() < getFramePrintArea().SSize().Width() )
                pLowerFrame->SetRetouche();
        }
        pLowerFrame = pLowerFrame->GetNext();
    }

    // Finally adjust the columns if width is set to auto
    // Possible optimization: execute this code earlier in this function and
    // return???
    if ( !(( (aRectFnSet.IsVert() && bHeightChgd) || (! aRectFnSet.IsVert() && bWidthChgd) ) &&
           Lower()->IsColumnFrame()) )
        return;

    // get column attribute
    const SwFormatCol* pColAttr = nullptr;
    if ( IsPageBodyFrame() )
    {
        OSL_ENSURE( GetUpper()->IsPageFrame(), "Upper is not page frame" );
        pColAttr = &GetUpper()->GetFormat()->GetCol();
    }
    else
    {
        OSL_ENSURE( IsFlyFrame() || IsSctFrame(), "Columns not in fly or section" );
        pColAttr = &GetFormat()->GetCol();
    }

    if ( pColAttr->IsOrtho() && pColAttr->GetNumCols() > 1 )
        AdjustColumns( pColAttr, false );
}

/** "Formats" the Frame; Frame and PrtArea.
 *
 * The Fixsize is not set here.
 */
void SwLayoutFrame::Format( vcl::RenderContext* /*pRenderContext*/, const SwBorderAttrs *pAttrs )
{
    OSL_ENSURE( pAttrs, "LayoutFrame::Format, pAttrs is 0." );

    if ( isFramePrintAreaValid() && isFrameAreaSizeValid() )
        return;

    bool bHideWhitespace = false;
    if (IsPageFrame())
    {
        SwViewShell* pShell = getRootFrame()->GetCurrShell();
        if (pShell && pShell->GetViewOptions()->IsWhitespaceHidden())
        {
            // This is needed so that no space is reserved for the margin on
            // the last page of the document. Other pages would have no margin
            // set even without this, as their frame height is the content
            // height already.
            bHideWhitespace = true;
        }
    }

    const sal_uInt16 nLeft = o3tl::narrowing<sal_uInt16>(pAttrs->CalcLeft(this));
    const sal_uInt16 nUpper = bHideWhitespace ? 0 : pAttrs->CalcTop();

    const sal_uInt16 nRight = o3tl::narrowing<sal_uInt16>(pAttrs->CalcRight(this));
    const sal_uInt16 nLower = bHideWhitespace ? 0 : pAttrs->CalcBottom();

    SwRectFnSet fnRect(IsVertical() && !IsPageFrame(), IsVertLR(), IsVertLRBT());
    if ( !isFramePrintAreaValid() )
    {
        setFramePrintAreaValid(true);
        fnRect.SetXMargins(*this, nLeft, nRight);
        fnRect.SetYMargins(*this, nUpper, nLower);
    }

    if ( isFrameAreaSizeValid() )
        return;

    if ( !HasFixSize() )
    {
        const SwTwips nBorder = nUpper + nLower;
        const SwFormatFrameSize &rSz = GetFormat()->GetFrameSize();
        SwTwips nMinHeight = rSz.GetHeightSizeType() == SwFrameSize::Minimum ? rSz.GetHeight() : 0;
        do
        {
            setFrameAreaSizeValid(true);

            //The size in VarSize is calculated using the content plus the
            // borders.
            SwTwips nRemaining = 0;
            SwFrame *pFrame = Lower();
            while ( pFrame )
            {   nRemaining += fnRect.GetHeight(pFrame->getFrameArea());
                if( pFrame->IsTextFrame() && static_cast<SwTextFrame*>(pFrame)->IsUndersized() )
                // This TextFrame would like to be a bit bigger
                    nRemaining += static_cast<SwTextFrame*>(pFrame)->GetParHeight()
                                  - fnRect.GetHeight(pFrame->getFramePrintArea());
                else if( pFrame->IsSctFrame() && static_cast<SwSectionFrame*>(pFrame)->IsUndersized() )
                    nRemaining += static_cast<SwSectionFrame*>(pFrame)->Undersize();
                pFrame = pFrame->GetNext();
            }
            nRemaining += nBorder;
            nRemaining = std::max( nRemaining, nMinHeight );
            const SwTwips nDiff = nRemaining - fnRect.GetHeight(getFrameArea());
            const tools::Long nOldLeft = fnRect.GetLeft(getFrameArea());
            const tools::Long nOldTop = fnRect.GetTop(getFrameArea());
            if ( nDiff )
            {
                if ( nDiff > 0 )
                    Grow( nDiff );
                else
                    Shrink( -nDiff );
                //Updates the positions using the fast channel.
                MakePos();
            }
            //Don't exceed the bottom edge of the Upper.
            if (GetUpper() && fnRect.GetHeight(getFrameArea()))
            {
                const SwTwips nLimit = fnRect.GetPrtBottom(*GetUpper());
                if( fnRect.SetLimit(*this, nLimit) &&
                    nOldLeft == fnRect.GetLeft(getFrameArea()) &&
                    nOldTop  == fnRect.GetTop(getFrameArea()) )
                {
                    setFrameAreaSizeValid(true);
                    setFramePrintAreaValid(true);
                }
            }
        } while ( !isFrameAreaSizeValid() );
    }
    else if (GetType() & FRM_HEADFOOT)
    {
        do
        {   if ( getFrameArea().Height() != pAttrs->GetSize().Height() )
            {
                ChgSize( Size( getFrameArea().Width(), pAttrs->GetSize().Height()));
            }

            setFrameAreaSizeValid(true);
            MakePos();
        } while ( !isFrameAreaSizeValid() );
    }
    else
    {
        setFrameAreaSizeValid(true);
    }

    // While updating the size, PrtArea might be invalidated.
    if (!isFramePrintAreaValid())
    {
        setFramePrintAreaValid(true);
        fnRect.SetXMargins(*this, nLeft, nRight);
        fnRect.SetYMargins(*this, nUpper, nLower);
    }
}

static void InvaPercentFlys( SwFrame *pFrame, SwTwips nDiff )
{
    OSL_ENSURE( pFrame->GetDrawObjs(), "Can't find any Objects" );
    for (SwAnchoredObject* pAnchoredObj : *pFrame->GetDrawObjs())
    {
        if ( auto pFly = pAnchoredObj->DynCastFlyFrame() )
        {
            const SwFormatFrameSize &rSz = pFly->GetFormat()->GetFrameSize();
            if ( rSz.GetWidthPercent() || rSz.GetHeightPercent() )
            {
                bool bNotify = true;
                // If we've a fly with more than 90% relative height...
                if( rSz.GetHeightPercent() > 90 && pFly->GetAnchorFrame() &&
                    rSz.GetHeightPercent() != SwFormatFrameSize::SYNCED && nDiff )
                {
                    const SwFrame *pRel = pFly->IsFlyLayFrame() ? pFly->GetAnchorFrame():
                                        pFly->GetAnchorFrame()->GetUpper();
                    // ... and we have already more than 90% height and we
                    // not allow the text to go through...
                    // then a notification could cause an endless loop, e.g.
                    // 100% height and no text wrap inside a cell of a table.
                    if( pFly->getFrameArea().Height()*10 >
                        ( nDiff + pRel->getFramePrintArea().Height() )*9 &&
                        pFly->GetFormat()->GetSurround().GetSurround() !=
                        css::text::WrapTextMode_THROUGH )
                       bNotify = false;
                }
                if( bNotify )
                    pFly->InvalidateSize();
            }
        }
    }
}

void SwLayoutFrame::InvaPercentLowers( SwTwips nDiff )
{
    if ( GetDrawObjs() )
        ::InvaPercentFlys( this, nDiff );

    SwFrame *pFrame = ContainsContent();
    if ( !pFrame )
        return;

    do
    {
        if ( pFrame->IsInTab() && !IsTabFrame() )
        {
            SwFrame *pTmp = pFrame->FindTabFrame();
            OSL_ENSURE( pTmp, "Where's my TabFrame?" );
            if( IsAnLower( pTmp ) )
                pFrame = pTmp;
        }

        if ( pFrame->IsTabFrame() )
        {
            const SwFormatFrameSize &rSz = static_cast<SwLayoutFrame*>(pFrame)->GetFormat()->GetFrameSize();
            if ( rSz.GetWidthPercent() || rSz.GetHeightPercent() )
                pFrame->InvalidatePrt();
        }
        else if ( pFrame->GetDrawObjs() )
            ::InvaPercentFlys( pFrame, nDiff );
        pFrame = pFrame->FindNextCnt();
    } while ( pFrame && IsAnLower( pFrame ) ) ;
}

tools::Long SwLayoutFrame::CalcRel( const SwFormatFrameSize &rSz ) const
{
    tools::Long nRet     = rSz.GetWidth(),
         nPercent = rSz.GetWidthPercent();

    if ( nPercent )
    {
        const SwFrame *pRel = GetUpper();
        tools::Long nRel = LONG_MAX;
        const SwViewShell *pSh = getRootFrame()->GetCurrShell();
        const bool bBrowseMode = pSh && pSh->GetViewOptions()->getBrowseMode();
        if( pRel->IsPageBodyFrame() && pSh && bBrowseMode && pSh->VisArea().Width() )
        {
            nRel = pSh->GetBrowseWidth();
            tools::Long nDiff = nRel - pRel->getFramePrintArea().Width();
            if ( nDiff > 0 )
                nRel -= nDiff;
        }
        nRel = std::min( nRel, pRel->getFramePrintArea().Width() );
        nRet = nRel * nPercent / 100;
    }
    return nRet;
}

// Local helpers for SwLayoutFrame::FormatWidthCols()

static tools::Long lcl_CalcMinColDiff( SwLayoutFrame *pLayFrame )
{
    tools::Long nDiff = 0, nFirstDiff = 0;
    SwLayoutFrame *pCol = static_cast<SwLayoutFrame*>(pLayFrame->Lower());
    OSL_ENSURE( pCol, "Where's the columnframe?" );
    SwFrame *pFrame = pCol->Lower();
    do
    {
        if( pFrame && pFrame->IsBodyFrame() )
            pFrame = static_cast<SwBodyFrame*>(pFrame)->Lower();
        if ( pFrame && pFrame->IsTextFrame() )
        {
            const tools::Long nTmp = static_cast<SwTextFrame*>(pFrame)->FirstLineHeight();
            if (nTmp != std::numeric_limits<SwTwips>::max())
            {
                if ( pCol == pLayFrame->Lower() )
                    nFirstDiff = nTmp;
                else
                    nDiff = nDiff ? std::min( nDiff, nTmp ) : nTmp;
            }
        }
        //Skip empty columns!
        pCol = static_cast<SwLayoutFrame*>(pCol->GetNext());
        while ( pCol && nullptr == (pFrame = pCol->Lower()) )
            pCol = static_cast<SwLayoutFrame*>(pCol->GetNext());

    } while ( pFrame && pCol );

    return nDiff ? nDiff : nFirstDiff ? nFirstDiff : 240;
}

static bool lcl_IsFlyHeightClipped( SwLayoutFrame *pLay )
{
    SwFrame *pFrame = pLay->ContainsContent();
    while ( pFrame )
    {
        if ( pFrame->IsInTab() )
            pFrame = pFrame->FindTabFrame();

        if ( pFrame->GetDrawObjs() )
        {
            const size_t nCnt = pFrame->GetDrawObjs()->size();
            for ( size_t i = 0; i < nCnt; ++i )
            {
                SwAnchoredObject* pAnchoredObj = (*pFrame->GetDrawObjs())[i];
                if ( auto pFly = pAnchoredObj->DynCastFlyFrame() )
                {
                    if ( pFly->IsHeightClipped() &&
                         ( !pFly->IsFlyFreeFrame() || pFly->GetPageFrame() ) )
                        return true;
                }
            }
        }
        pFrame = pFrame->FindNextCnt();
    }
    return false;
}

void SwLayoutFrame::FormatWidthCols( const SwBorderAttrs &rAttrs,
                                   const SwTwips nBorder, const SwTwips nMinHeight )
{
    //If there are columns involved, the size is adjusted using the last column.
    //1. Format content.
    //2. Calculate height of the last column: if it's too big, the Fly has to
    //   grow. The amount by which the Fly grows is not the amount of the
    //   overhang because we have to act on the assumption that some text flows
    //   back which will generate some more space.
    //   The amount which we grow by equals the overhang
    //   divided by the amount of columns or the overhang itself if it's smaller
    //   than the amount of columns.
    //3. Go back to 1. until everything is stable.

    const SwFormatCol &rCol = rAttrs.GetAttrSet().GetCol();
    const sal_uInt16 nNumCols = rCol.GetNumCols();

    bool bEnd = false;
    bool bBackLock = false;
    SwViewShell *pSh = getRootFrame()->GetCurrShell();
    SwViewShellImp *pImp = pSh ? pSh->Imp() : nullptr;
    vcl::RenderContext* pRenderContext = pSh ? pSh->GetOut() : nullptr;
    {
        // Underlying algorithm
        // We try to find the optimal height for the column.
        // nMinimum starts with the passed minimum height and is then maintained
        // as the maximum height on which column content did not fit into a
        // column.
        // nMaximum starts with LONG_MAX and is then maintained as the minimum
        // height on which the content fit into the columns.
        // In column based sections nMaximum starts at the maximum value which
        // the environment defines, this can certainly be a value on which
        // content doesn't fit.
        // The columns are formatted. If content still juts out, nMinimum is
        // adjusted accordingly, then we grow, at least by nMinDiff but not
        // over a certain nMaximum. If no content juts out but there is still
        // some space left in a column, shrinking is done accordingly, at
        // least by nMinDiff but not below the nMinimum.
        // Cancel as soon as no content juts out and the difference from minimum
        // to maximum is less than nMinDiff or the maximum which was defined by
        // the environment is reached and some content still doesn't fit.

        // Criticism of this implementation
        // 1. Theoretically situations are possible in which the content fits in
        // a lower height but not in a higher height. To ensure that the code
        // handles such situations the code contains a few checks concerning
        // minimum and maximum which probably are never triggered.
        // 2. We use the same nMinDiff for shrinking and growing, but nMinDiff
        // is more or less the smallest first line height and doesn't seem ideal
        // as minimum value for shrinking.

        tools::Long nMinimum = nMinHeight;
        tools::Long nMaximum;
        bool bNoBalance = false;
        SwRectFnSet aRectFnSet(this);
        if( IsSctFrame() )
        {
            nMaximum = aRectFnSet.GetHeight(getFrameArea()) - nBorder +
                       aRectFnSet.BottomDist(getFrameArea(), aRectFnSet.GetPrtBottom(*GetUpper()));
            nMaximum += GetUpper()->Grow( LONG_MAX, true );
            if( nMaximum < nMinimum )
            {
                if( nMaximum < 0 )
                    nMinimum = nMaximum = 0;
                else
                    nMinimum = nMaximum;
            }
            if( nMaximum > BROWSE_HEIGHT )
                nMaximum = BROWSE_HEIGHT;

            bNoBalance = static_cast<SwSectionFrame*>(this)->GetSection()->GetFormat()->
                         GetBalancedColumns().GetValue();
            SwFrame* pAny = ContainsAny();
            if( bNoBalance ||
                ( !aRectFnSet.GetHeight(getFrameArea()) && pAny ) )
            {
                tools::Long nTop = aRectFnSet.GetTopMargin(*this);
                // #i23129# - correction
                // to the calculated maximum height.
                {
                    SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
                    aRectFnSet.AddBottom( aFrm, nMaximum - aRectFnSet.GetHeight(getFrameArea()) );
                }

                if( nTop > nMaximum )
                    nTop = nMaximum;
                aRectFnSet.SetYMargins( *this, nTop, 0 );
            }
            if( !pAny && !static_cast<SwSectionFrame*>(this)->IsFootnoteLock() )
            {
                SwFootnoteContFrame* pFootnoteCont = static_cast<SwSectionFrame*>(this)->ContainsFootnoteCont();
                if( pFootnoteCont )
                {
                    SwFrame* pFootnoteAny = pFootnoteCont->ContainsAny();
                    if( pFootnoteAny && pFootnoteAny->isFrameAreaDefinitionValid() )
                    {
                        bBackLock = true;
                        static_cast<SwSectionFrame*>(this)->SetFootnoteLock( true );
                    }
                }
            }
        }
        else
            nMaximum = LONG_MAX;

        // #i3317# - reset temporarily consideration
        // of wrapping style influence
        SwPageFrame* pPageFrame = FindPageFrame();
        SwSortedObjs* pObjs = pPageFrame ? pPageFrame->GetSortedObjs() : nullptr;
        if ( pObjs )
        {
            for (SwAnchoredObject* pAnchoredObj : *pObjs)
            {
                if ( IsAnLower( pAnchoredObj->GetAnchorFrame() ) )
                {
                    pAnchoredObj->SetTmpConsiderWrapInfluence( false );
                }
            }
        }
        do
        {
            //Could take a while therefore check for Waitcrsr here.
            if ( pImp )
                pImp->CheckWaitCursor();

            setFrameAreaSizeValid(true);
            //First format the column, before using lots of stack space here.
            //Also set width and height of the column (if they are wrong)
            //while we are at it.
            SwLayoutFrame *pCol = static_cast<SwLayoutFrame*>(Lower());

            // #i27399#
            // Simply setting the column width based on the values returned by
            // CalcColWidth does not work for automatic column width.
            AdjustColumns( &rCol, false );

            for ( sal_uInt16 i = 0; i < nNumCols; ++i )
            {
                pCol->Calc(pRenderContext);
                // ColumnFrames have a BodyFrame now, which needs to be calculated
                pCol->Lower()->Calc(pRenderContext);
                if( pCol->Lower()->GetNext() )
                    pCol->Lower()->GetNext()->Calc(pRenderContext);  // SwFootnoteCont
                pCol = static_cast<SwLayoutFrame*>(pCol->GetNext());
            }

            ::CalcContent( this );

            pCol = static_cast<SwLayoutFrame*>(Lower());
            OSL_ENSURE( pCol && pCol->GetNext(), ":-( column making holidays?");
            // set bMinDiff if no empty columns exist
            bool bMinDiff = true;
            // OD 28.03.2003 #108446# - check for all column content and all columns
            while ( bMinDiff && pCol )
            {
                bMinDiff = nullptr != pCol->ContainsContent();
                pCol = static_cast<SwLayoutFrame*>(pCol->GetNext());
            }
            pCol = static_cast<SwLayoutFrame*>(Lower());

            SwTwips nDiff = 0;
            SwTwips nMaxFree = 0;
            SwTwips nAllFree = LONG_MAX;
            // set bFoundLower if there is at least one non-empty column
            bool bFoundLower = false;
            while( pCol )
            {
                SwLayoutFrame* pLay = static_cast<SwLayoutFrame*>(pCol->Lower());
                SwTwips nInnerHeight = aRectFnSet.GetHeight(pLay->getFrameArea()) -
                                       aRectFnSet.GetHeight(pLay->getFramePrintArea());
                if( pLay->Lower() )
                {
                    bFoundLower = true;
                    nInnerHeight += pLay->InnerHeight();
                }
                else if( nInnerHeight < 0 )
                    nInnerHeight = 0;

                if( pLay->GetNext() )
                {
                    bFoundLower = true;
                    pLay = static_cast<SwLayoutFrame*>(pLay->GetNext());
                    OSL_ENSURE( pLay->IsFootnoteContFrame(),"FootnoteContainer expected" );
                    nInnerHeight += pLay->InnerHeight();
                    nInnerHeight += aRectFnSet.GetHeight(pLay->getFrameArea()) -
                                    aRectFnSet.GetHeight(pLay->getFramePrintArea());
                }
                nInnerHeight -= aRectFnSet.GetHeight(pCol->getFramePrintArea());
                if( nInnerHeight > nDiff )
                {
                    nDiff = nInnerHeight;
                    nAllFree = 0;
                }
                else
                {
                    if( nMaxFree < -nInnerHeight )
                        nMaxFree = -nInnerHeight;
                    if( nAllFree > -nInnerHeight )
                        nAllFree = -nInnerHeight;
                }
                pCol = static_cast<SwLayoutFrame*>(pCol->GetNext());
            }

            if ( bFoundLower || ( IsSctFrame() && static_cast<SwSectionFrame*>(this)->HasFollow() ) )
            {
                SwTwips nMinDiff = ::lcl_CalcMinColDiff( this );
                // Here we decide if growing is needed - this is the case, if
                // column content (nDiff) or a Fly juts over.
                // In sections with columns we take into account to set the size
                // when having a non-empty Follow.
                if ( nDiff || ::lcl_IsFlyHeightClipped( this ) ||
                     ( IsSctFrame() && static_cast<SwSectionFrame*>(this)->CalcMinDiff( nMinDiff ) ) )
                {
                    tools::Long nPrtHeight = aRectFnSet.GetHeight(getFramePrintArea());
                    // The minimum must not be smaller than our PrtHeight as
                    // long as something juts over.
                    if( nMinimum < nPrtHeight )
                        nMinimum = nPrtHeight;
                    // The maximum must not be smaller than PrtHeight if
                    // something still juts over.
                    if( nMaximum < nPrtHeight )
                        nMaximum = nPrtHeight;  // Robust, but will this ever happen?
                    if( !nDiff ) // If only Flys jut over, we grow by nMinDiff
                        nDiff = nMinDiff;
                    // If we should grow more than by nMinDiff we split it over
                    // the columns
                    if ( std::abs(nDiff - nMinDiff) > nNumCols && nDiff > static_cast<tools::Long>(nNumCols) )
                        nDiff /= nNumCols;

                    if ( bMinDiff )
                    {   // If no empty column exists, we want to grow at least
                        // by nMinDiff. Special case: If we are smaller than the
                        // minimal FrameHeight and PrtHeight is smaller than
                        // nMindiff we grow in a way that PrtHeight is exactly
                        // nMinDiff afterwards.
                        tools::Long nFrameHeight = aRectFnSet.GetHeight(getFrameArea());
                        if ( nFrameHeight > nMinHeight || nPrtHeight >= nMinDiff )
                            nDiff = std::max( nDiff, nMinDiff );
                        else if( nDiff < nMinDiff )
                            nDiff = nMinDiff - nPrtHeight + 1;
                    }
                    // nMaximum is a size in which the content fit or the
                    // requested value from the environment, therefore we don't
                    // should not exceed this value.
                    if( nDiff + nPrtHeight > nMaximum )
                        nDiff = nMaximum - nPrtHeight;
                }
                else if (nMaximum > nMinimum) // We fit, do we still have some opportunity to shrink?
                {
                    tools::Long nPrtHeight = aRectFnSet.GetHeight(getFramePrintArea());
                    if ( nMaximum < nPrtHeight )
                        nDiff = nMaximum - nPrtHeight; // We grew over a working
                        // height and shrink back to it, but will this ever
                        // happen?
                    else
                    {   // We have a new maximum, a size where the content fits.
                        nMaximum = nPrtHeight;
                        // If the extra space in the column is bigger than
                        // nMinDiff, we shrink a bit, unless size would drop
                        // below the minimum.
                        if ( !bNoBalance &&
                             // #i23129# - <nMinDiff> can be
                             // big, because of an object at the beginning of
                             // a column. Thus, decrease optimization here.
                             //nMaxFree >= nMinDiff &&
                             nMaxFree > 0 &&
                             ( !nAllFree ||
                               nMinimum < nPrtHeight - nMinDiff ) )
                        {
                            nMaxFree /= nNumCols; // disperse over the columns
                            nDiff = nMaxFree < nMinDiff ? -nMinDiff : -nMaxFree; // min nMinDiff
                            if( nPrtHeight + nDiff <= nMinimum ) // below the minimum?
                                nDiff = ( nMinimum - nMaximum ) / 2; // Take the center
                        }
                        else if( nAllFree )
                        {
                            nDiff = -nAllFree;
                            if( nPrtHeight + nDiff <= nMinimum ) // Less than minimum?
                                nDiff = ( nMinimum - nMaximum ) / 2; // Take the center
                        }
                    }
                }
                if( nDiff ) // now we shrink or grow...
                {
                    Size aOldSz( getFramePrintArea().SSize() );
                    tools::Long nTop = aRectFnSet.GetTopMargin(*this);
                    nDiff = aRectFnSet.GetHeight(getFramePrintArea()) + nDiff + nBorder - aRectFnSet.GetHeight(getFrameArea());

                    {
                        SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
                        aRectFnSet.AddBottom( aFrm, nDiff );
                    }

                    // #i68520#
                    SwFlyFrame *pFlyFrame = IsFlyFrame() ? static_cast<SwFlyFrame*>(this) : nullptr;
                    if (pFlyFrame)
                    {
                        pFlyFrame->InvalidateObjRectWithSpaces();
                    }
                    aRectFnSet.SetYMargins( *this, nTop, nBorder - nTop );
                    ChgLowersProp( aOldSz );
                    NotifyLowerObjs();

                    // #i3317# - reset temporarily consideration
                    // of wrapping style influence
                    SwPageFrame* pTmpPageFrame = FindPageFrame();
                    SwSortedObjs* pTmpObjs = pTmpPageFrame ? pTmpPageFrame->GetSortedObjs() : nullptr;
                    if ( pTmpObjs )
                    {
                        for (SwAnchoredObject* pAnchoredObj : *pTmpObjs)
                        {
                            if ( IsAnLower( pAnchoredObj->GetAnchorFrame() ) )
                            {
                                pAnchoredObj->SetTmpConsiderWrapInfluence( false );
                            }
                        }
                    }
                    //Invalidate suitable to nicely balance the Frames.
                    //- Every first lower starting from the second column gets a
                    //  InvalidatePos();
                    pCol = static_cast<SwLayoutFrame*>(Lower()->GetNext());
                    while ( pCol )
                    {
                        SwFrame *pLow = pCol->Lower();
                        if ( pLow )
                            pLow->InvalidatePos_();
                        pCol = static_cast<SwLayoutFrame*>(pCol->GetNext());
                    }
                    if( IsSctFrame() && static_cast<SwSectionFrame*>(this)->HasFollow() )
                    {
                        // If we created a Follow, we need to give its content
                        // the opportunity to flow back inside the CalcContent
                        SwContentFrame* pTmpContent =
                            static_cast<SwSectionFrame*>(this)->GetFollow()->ContainsContent();
                        if( pTmpContent )
                            pTmpContent->InvalidatePos_();
                    }
                }
                else
                    bEnd = true;
            }
            else
                bEnd = true;

        } while ( !bEnd || !isFrameAreaSizeValid() );
    }
    // OD 01.04.2003 #108446# - Don't collect endnotes for sections. Thus, set
    // 2nd parameter to <true>.
    ::CalcContent( this, true );
    if( IsSctFrame() )
    {
        // OD 14.03.2003 #i11760# - adjust 2nd parameter - sal_True --> true
        setFrameAreaSizeValid(true);
        ::CalcContent( this, true );
        if( bBackLock )
            static_cast<SwSectionFrame*>(this)->SetFootnoteLock( false );
    }
}

void SwLayoutFrame::dumpAsXmlAttributes(xmlTextWriterPtr writer) const
{
    SwFrame::dumpAsXmlAttributes(writer);

    const SwFrameFormat* pFormat = GetFormat();
    if (pFormat)
    {
        (void)xmlTextWriterWriteFormatAttribute( writer, BAD_CAST( "format" ), "%p", pFormat);
        (void)xmlTextWriterWriteFormatAttribute( writer, BAD_CAST( "formatName" ), "%s", BAD_CAST(pFormat->GetName().toString().toUtf8().getStr()));
    }
}

static SwContentFrame* lcl_InvalidateSection( SwFrame *pCnt, SwInvalidateFlags nInv )
{
    SwSectionFrame* pSect = pCnt->FindSctFrame();
    // If our ContentFrame is placed inside a table or a footnote, only sections
    // which are also placed inside are meant.
    // Exception: If a table is directly passed.
    if( ( ( pCnt->IsInTab() && !pSect->IsInTab() ) ||
        ( pCnt->IsInFootnote() && !pSect->IsInFootnote() ) ) && !pCnt->IsTabFrame() )
        return nullptr;
    if( nInv & SwInvalidateFlags::Size )
        pSect->InvalidateSize_();
    if( nInv & SwInvalidateFlags::Pos )
        pSect->InvalidatePos_();
    if( nInv & SwInvalidateFlags::PrtArea )
        pSect->InvalidatePrt_();
    SwFlowFrame *pFoll = pSect->GetFollow();
    // Temporary separation from follow
    pSect->SetFollow( nullptr );
    SwContentFrame* pRet = pSect->FindLastContent();
    pSect->SetFollow( pFoll );
    return pRet;
}

static SwContentFrame* lcl_InvalidateTable( SwTabFrame *pTable, SwInvalidateFlags nInv )
{
    if( ( nInv & SwInvalidateFlags::Section ) && pTable->IsInSct() )
        lcl_InvalidateSection( pTable, nInv );
    if( nInv & SwInvalidateFlags::Size )
        pTable->InvalidateSize_();
    if( nInv & SwInvalidateFlags::Pos )
        pTable->InvalidatePos_();
    if( nInv & SwInvalidateFlags::PrtArea )
        pTable->InvalidatePrt_();
    return pTable->FindLastContent();
}

static void lcl_InvalidateAllContent( SwContentFrame *pCnt, SwInvalidateFlags nInv );

static void lcl_InvalidateContent( SwContentFrame *pCnt, SwInvalidateFlags nInv )
{
    SwContentFrame *pLastTabCnt = nullptr;
    SwContentFrame *pLastSctCnt = nullptr;
    while ( pCnt )
    {
        if( nInv & SwInvalidateFlags::Section )
        {
            if( pCnt->IsInSct() )
            {
                // See above at tables
                if( !pLastSctCnt )
                    pLastSctCnt = lcl_InvalidateSection( pCnt, nInv );
                if( pLastSctCnt == pCnt )
                    pLastSctCnt = nullptr;
            }
#if OSL_DEBUG_LEVEL > 0
            else
                OSL_ENSURE( !pLastSctCnt, "Where's the last SctContent?" );
#endif
        }
        if( nInv & SwInvalidateFlags::Table )
        {
            if( pCnt->IsInTab() )
            {
                // To not call FindTabFrame() for each ContentFrame of a table and
                // then invalidate the table, we remember the last ContentFrame of
                // the table and ignore IsInTab() until we are past it.
                // When entering the table, LastSctCnt is set to null, so
                // sections inside the table are correctly invalidated.
                // If the table itself is in a section the
                // invalidation is done three times, which is acceptable.
                if( !pLastTabCnt )
                {
                    pLastTabCnt = lcl_InvalidateTable( pCnt->FindTabFrame(), nInv );
                    pLastSctCnt = nullptr;
                }
                if( pLastTabCnt == pCnt )
                {
                    pLastTabCnt = nullptr;
                    pLastSctCnt = nullptr;
                }
            }
#if OSL_DEBUG_LEVEL > 0
            else
                OSL_ENSURE( !pLastTabCnt, "Where's the last TabContent?" );
#endif
        }

        if( nInv & SwInvalidateFlags::Size )
            pCnt->Prepare( PrepareHint::Clear, nullptr, false );
        if( nInv & SwInvalidateFlags::Pos )
            pCnt->InvalidatePos_();
        if( nInv & SwInvalidateFlags::PrtArea )
            pCnt->InvalidatePrt_();
        if ( nInv & SwInvalidateFlags::LineNum )
            pCnt->InvalidateLineNum();
        if ( pCnt->GetDrawObjs() )
            lcl_InvalidateAllContent( pCnt, nInv );
        pCnt = pCnt->GetNextContentFrame();
    }
}

static void lcl_InvalidateAllContent( SwContentFrame *pCnt, SwInvalidateFlags nInv )
{
    SwSortedObjs &rObjs = *pCnt->GetDrawObjs();
    for (SwAnchoredObject* pAnchoredObj : rObjs)
    {
        if ( auto pFly = pAnchoredObj->DynCastFlyFrame() )
        {
            if ( pFly->IsFlyInContentFrame() )
            {
                ::lcl_InvalidateContent( pFly->ContainsContent(), nInv );
                if( nInv & SwInvalidateFlags::Direction )
                    pFly->CheckDirChange();
            }
        }
    }
}

void SwRootFrame::InvalidateAllContent( SwInvalidateFlags nInv )
{
    // First process all page bound FlyFrames.
    SwPageFrame *pPage = static_cast<SwPageFrame*>(Lower());
    while( pPage )
    {
        pPage->InvalidateFlyLayout();
        pPage->InvalidateFlyContent();
        pPage->InvalidateFlyInCnt();
        pPage->InvalidateLayout();
        pPage->InvalidateContent();
        pPage->InvalidatePage( pPage ); // So even the Turbo disappears if applicable

        if ( pPage->GetSortedObjs() )
        {
            const SwSortedObjs &rObjs = *pPage->GetSortedObjs();
            for (SwAnchoredObject* pAnchoredObj : rObjs)
            {
                if ( auto pFly = pAnchoredObj->DynCastFlyFrame() )
                {
                    ::lcl_InvalidateContent( pFly->ContainsContent(), nInv );
                    if ( nInv & SwInvalidateFlags::Direction )
                        pFly->CheckDirChange();
                }
            }
        }
        if( nInv & SwInvalidateFlags::Direction )
            pPage->CheckDirChange();
        pPage = static_cast<SwPageFrame*>(pPage->GetNext());
    }

    //Invalidate the whole document content and the character bound Flys here.
    ::lcl_InvalidateContent( ContainsContent(), nInv );

    if( nInv & SwInvalidateFlags::PrtArea )
    {
        SwViewShell *pSh  = getRootFrame()->GetCurrShell();
        if( pSh )
            pSh->InvalidateWindows( getFrameArea() );
    }
}

/**
 * Invalidate/re-calculate the position of all floating screen objects (Writer fly frames and
 * drawing objects), that are anchored to paragraph or to character. (2004-03-16 #i11860#)
 */
void SwRootFrame::InvalidateAllObjPos()
{
    const SwPageFrame* pPageFrame = static_cast<const SwPageFrame*>(Lower());
    while( pPageFrame )
    {
        pPageFrame->InvalidateFlyLayout();

        if ( pPageFrame->GetSortedObjs() )
        {
            const SwSortedObjs& rObjs = *(pPageFrame->GetSortedObjs());
            for (SwAnchoredObject* pAnchoredObj : rObjs)
            {
                const SwFormatAnchor& rAnch = pAnchoredObj->GetFrameFormat()->GetAnchor();
                if ((rAnch.GetAnchorId() != RndStdIds::FLY_AT_PARA) &&
                    (rAnch.GetAnchorId() != RndStdIds::FLY_AT_CHAR))
                {
                    // only to paragraph and to character anchored objects are considered.
                    continue;
                }
                // #i28701# - special invalidation for anchored
                // objects, whose wrapping style influence has to be considered.
                if ( pAnchoredObj->ConsiderObjWrapInfluenceOnObjPos() )
                    pAnchoredObj->InvalidateObjPosForConsiderWrapInfluence();
                else
                    pAnchoredObj->InvalidateObjPos();
            }
        }

        pPageFrame = static_cast<const SwPageFrame*>(pPageFrame->GetNext());
    }
}

static void AddRemoveFlysForNode(
        SwTextFrame & rFrame, SwTextNode & rTextNode,
        std::set<SwNodeOffset> *const pSkipped,
        const sw::FrameFormats<sw::SpzFrameFormat*>& rTable,
        SwPageFrame *const pPage,
        SwTextNode const*const pNode,
        std::vector<sw::Extent>::const_iterator const& rIterFirst,
        std::vector<sw::Extent>::const_iterator const& rIterEnd,
        SwTextNode const*const pFirstNode, SwTextNode const*const pLastNode)
{
    if (pNode == &rTextNode)
    {   // remove existing hidden at-char anchored flys
        RemoveHiddenObjsOfNode(rTextNode, &rIterFirst, &rIterEnd, pFirstNode, pLastNode);
    }
    else if (rTextNode.GetIndex() < pNode->GetIndex())
    {
        // pNode's frame has been deleted by CheckParaRedlineMerge()
        AppendObjsOfNode(&rTable,
            pNode->GetIndex(), &rFrame, pPage, rTextNode.GetDoc(),
            &rIterFirst, &rIterEnd, pFirstNode, pLastNode);
        if (pSkipped)
        {
            // if a fly has been added by AppendObjsOfNode, it must be skipped; if not, then it doesn't matter if it's skipped or not because it has no frames and because of that it would be skipped anyway
            for (auto const pFly : pNode->GetAnchoredFlys())
            {
                if (pFly->Which() != RES_DRAWFRMFMT)
                {
                    pSkipped->insert(pFly->GetContent().GetContentIdx()->GetIndex());
                }
            }
        }
    }
}

namespace sw {

/// rTextNode is the first one of the "new" merge - if rTextNode isn't the same
/// as MergedPara::pFirstNode, then nodes before rTextNode have their flys
/// already properly attached, so only the other nodes need handling here.
void AddRemoveFlysAnchoredToFrameStartingAtNode(
        SwTextFrame & rFrame, SwTextNode & rTextNode,
        std::set<SwNodeOffset> *const pSkipped)
{
    auto const pMerged(rFrame.GetMergedPara());
    if (!pMerged
        // do this only *once*, for the *last* frame
        // otherwise AppendObj would create multiple frames for fly-frames!
        || rFrame.GetFollow())
        return;

    assert(pMerged->pFirstNode->GetIndex() <= rTextNode.GetIndex()
        && rTextNode.GetIndex() <= pMerged->pLastNode->GetIndex());
    // add visible flys in non-first node to merged frame
    // (hidden flys remain and are deleted via DelFrames())
    sw::SpzFrameFormats& rTable(*rTextNode.GetDoc().GetSpzFrameFormats());
    SwPageFrame *const pPage(rFrame.FindPageFrame());
    std::vector<sw::Extent>::const_iterator iterFirst(pMerged->extents.begin());
    std::vector<sw::Extent>::const_iterator iter(iterFirst);
    SwTextNode const* pNode(pMerged->pFirstNode);
    for ( ; ; ++iter)
    {
        if (iter == pMerged->extents.end()
            || iter->pNode != pNode)
        {
            AddRemoveFlysForNode(rFrame, rTextNode, pSkipped, rTable, pPage,
                    pNode, iterFirst, iter,
                    pMerged->pFirstNode, pMerged->pLastNode);
            SwNodeOffset const until = iter == pMerged->extents.end()
                ? pMerged->pLastNode->GetIndex() + 1
                : iter->pNode->GetIndex();
            for (SwNodeOffset i = pNode->GetIndex() + 1; i < until; ++i)
            {
                // let's show at-para flys on nodes that contain start/end of
                // redline too, even if there's no text there
                SwNode const*const pTmp(pNode->GetNodes()[i]);
                if (pTmp->GetRedlineMergeFlag() == SwNode::Merge::NonFirst)
                {
                    AddRemoveFlysForNode(rFrame, rTextNode, pSkipped,
                        rTable, pPage, pTmp->GetTextNode(), iter, iter,
                        pMerged->pFirstNode, pMerged->pLastNode);
                }
            }
            if (iter == pMerged->extents.end())
            {
                break;
            }
            pNode = iter->pNode;
            iterFirst = iter;
        }
    }
}

} // namespace sw

static void UnHideRedlines(SwRootFrame & rLayout,
        const SwNodes & rNodes, SwNode const& rEndOfSectionNode,
        std::set<SwNodeOffset> *const pSkipped)
{
    assert(rEndOfSectionNode.IsEndNode());
    assert(rNodes[rEndOfSectionNode.StartOfSectionNode()->GetIndex() + 1]->IsCreateFrameWhenHidingRedlines()); // first node is never hidden
    for (SwNodeOffset i = rEndOfSectionNode.StartOfSectionNode()->GetIndex() + 1;
         i < rEndOfSectionNode.GetIndex(); ++i)
    {
        SwNode & rNode(*rNodes[i]);
        if (rNode.IsTextNode()) // only text nodes are 1st node of a merge
        {
            SwTextNode & rTextNode(*rNode.GetTextNode());
            SwIterator<SwTextFrame, SwTextNode, sw::IteratorMode::UnwrapMulti> aIter(rTextNode);
            std::vector<SwTextFrame*> frames;
            for (SwTextFrame * pFrame = aIter.First(); pFrame; pFrame = aIter.Next())
            {
                if (pFrame->getRootFrame() == &rLayout)
                {
                    if (pFrame->IsFollow())
                    {
                        frames.push_back(pFrame);
                    }    // when hiding, the loop must remove the anchored flys
                    else // *before* resetting SetMergedPara anywhere - else
                    {    // the fly deletion code will access multiple of the
                         // frames with inconsistent MergedPara and assert
                        frames.insert(frames.begin(), pFrame);
                    }
                }
            }
            // this messes with pRegisteredIn so do it outside SwIterator
            auto eMode(sw::FrameMode::Existing);
            for (SwTextFrame * pFrame : frames)
            {
                if (rLayout.HasMergedParas())
                {
                    assert(!pFrame->GetMergedPara() ||
                        !rNode.IsCreateFrameWhenHidingRedlines() ||
                        // FIXME: skip this assert in tables with deleted rows
                        pFrame->IsInTab());
                    if (rNode.IsCreateFrameWhenHidingRedlines())
                    {
                        {
                            auto pMerged(CheckParaRedlineMerge(*pFrame,
                                    rTextNode, eMode));
                            pFrame->SetMergedPara(std::move(pMerged));
                        }
                        auto const pMerged(pFrame->GetMergedPara());
                        if (pMerged)
                        {
                            // invalidate SwInvalidateFlags::Size
                            pFrame->Prepare(PrepareHint::Clear, nullptr, false);
                            pFrame->InvalidatePage();
                            if (auto const pObjs = pFrame->GetDrawObjs())
                            {   // also invalidate position of existing flys
                                // because they may need to be moved
                                for (auto const pObject : *pObjs)
                                {
                                    pObject->InvalidateObjPos();
                                }
                            }
                        }
                        sw::AddRemoveFlysAnchoredToFrameStartingAtNode(*pFrame, rTextNode, pSkipped);
                        // only *first* frame of node gets Existing because it
                        eMode = sw::FrameMode::New; // is not idempotent!
                    }
                }
                else
                {
                    if (auto const pMergedPara = pFrame->GetMergedPara())
                    {
                        // invalidate SwInvalidateFlags::Size
                        pFrame->Prepare(PrepareHint::Clear, nullptr, false);
                        pFrame->InvalidatePage();
                        if (auto const pObjs = pFrame->GetDrawObjs())
                        {   // also invalidate position of existing flys
                            for (auto const pObject : *pObjs)
                            {
                                pObject->InvalidateObjPos();
                            }
                        }
                        // SwFlyAtContentFrame::SwClientNotify() always appends to
                        // the master frame, so do the same here.
                        // (RemoveFootnotesForNode must be called at least once)
                        if (!pFrame->IsFollow())
                        {
                            // the new text frames don't exist yet, so at this point
                            // we can only delete the footnote frames so they don't
                            // point to the merged SwTextFrame any more...
                            assert(&rTextNode == pMergedPara->pFirstNode);
                            // iterate over nodes, not extents: if a node has
                            // no extents now but did have extents initially,
                            // its flys need their frames deleted too!
                            for (SwNodeOffset j = rTextNode.GetIndex() + 1;
                                 j <= pMergedPara->pLastNode->GetIndex(); ++j)
                            {
                                SwNode *const pNode(rTextNode.GetNodes()[j]);
                                assert(!pNode->IsEndNode());
                                if (pNode->IsStartNode())
                                {
                                    j = pNode->EndOfSectionIndex();
                                }
                                else if (pNode->IsTextNode())
                                {
                                    sw::RemoveFootnotesForNode(rLayout, *pNode->GetTextNode(), nullptr);
                                    // similarly, remove the anchored flys
                                    for (SwFrameFormat * pFormat : pNode->GetAnchoredFlys())
                                    {
                                        pFormat->DelFrames(/*&rLayout*/);
                                    }
                                }
                            }
                            // rely on AppendAllObjs call at the end to add
                            // all flys in first node that are hidden
                        }
                        pFrame->SetMergedPara(nullptr);
                    }
                }
                pFrame->Broadcast(SfxHint()); // notify SwAccessibleParagraph
            }
            // all nodes, not just merged ones! it may be in the same list as
            if (rTextNode.IsNumbered(nullptr)) // a preceding merged one...
            {   // notify frames so they reformat numbering portions
                rTextNode.NumRuleChgd();
            }
        }
        else if (rNode.IsTableNode() && rLayout.IsHideRedlines())
        {
            SwTableNode * pTableNd = rNode.GetTableNode();
            SwPosition const tmp(rNode);
            SwRangeRedline const*const pRedline(
                rLayout.GetFormat()->GetDoc().getIDocumentRedlineAccess().GetRedline(tmp, nullptr));
            // pathology: redline that starts on a TableNode; cannot
            // be created in UI but by import filters...
            if (pRedline
                && pRedline->GetType() == RedlineType::Delete
                && &pRedline->Start()->GetNode() == &rNode)
            {
                for (SwNodeOffset j = rNode.GetIndex(); j <= rNode.EndOfSectionIndex(); ++j)
                {
                    rNode.GetNodes()[j]->SetRedlineMergeFlag(SwNode::Merge::Hidden);
                }
                pTableNd->DelFrames(&rLayout);
            }
            else if ( pTableNd->GetTable().HasDeletedRowOrCell() )
            {
                pTableNd->DelFrames(&rLayout);
                if ( !pTableNd->GetTable().IsDeleted() )
                {
                    pTableNd->MakeOwnFrames();
                }
            }
        }
        else if (rNode.IsTableNode() && !rLayout.IsHideRedlines() &&
            rNode.GetTableNode()->GetTable().HasDeletedRowOrCell() )
        {
            SwTableNode * pTableNd = rNode.GetTableNode();
            pTableNd->DelFrames(&rLayout);
            pTableNd->MakeOwnFrames();
        }

        if (!rNode.IsCreateFrameWhenHidingRedlines())
        {
            if (rLayout.HasMergedParas())
            {
                if (rNode.IsContentNode())
                {
                    // note: nothing to do here, already done
#ifndef NDEBUG
                    auto const pFrame(static_cast<SwContentNode&>(rNode).getLayoutFrame(&rLayout));
                    assert(!pFrame || static_cast<SwTextFrame*>(pFrame)->GetMergedPara()->pFirstNode != &rNode);
#endif
                }
            }
            else
            {
                assert(!rNode.IsContentNode() || !rNode.GetContentNode()->getLayoutFrame(&rLayout) ||
                    // FIXME: skip this assert in tables with deleted rows
                    rNode.GetContentNode()->getLayoutFrame(&rLayout)->IsInTab());
                SwNodeOffset j = i + 1;
                for ( ; j < rEndOfSectionNode.GetIndex(); ++j)
                {
                    if (rNodes[j]->IsCreateFrameWhenHidingRedlines())
                    {
                        break;
                    }
                }
                // call MakeFrames once, because sections/tables
                // InsertCnt_ also checks for hidden sections
                {
                    sw::FlyCreationSuppressor aSuppressor(false);
                    ::MakeFrames(rLayout.GetFormat()->GetDoc(), *rNodes[i], *rNodes[j]);
                }
                i = j - 1; // will be incremented again
            }
        }
    }
}

static void UnHideRedlinesExtras(SwRootFrame & rLayout,
        const SwNodes & rNodes, SwNode const& rEndOfExtraSectionNode,
        std::set<SwNodeOffset> *const pSkipped)
{
    assert(rEndOfExtraSectionNode.IsEndNode());
    for (SwNodeOffset i = rEndOfExtraSectionNode.StartOfSectionNode()->GetIndex()
            + 1; i < rEndOfExtraSectionNode.GetIndex(); ++i)
    {
        SwNode const& rStartNode(*rNodes[i]);
        assert(rStartNode.IsStartNode());
        assert(rStartNode.GetRedlineMergeFlag() == SwNode::Merge::None);
        SwNode const& rEndNode(*rStartNode.EndOfSectionNode());
        bool bSkip(pSkipped && pSkipped->find(i) != pSkipped->end());
        i = rEndNode.GetIndex();
        for (SwNodeOffset j = rStartNode.GetIndex() + 1; j < i; ++j)
        {
            // note: SwStartNode has no way to access the frames, so check
            // whether the first content-node inside the section has frames
            SwNode const& rNode(*rNodes[j]);
            if (rNode.IsSectionNode() &&
                static_cast<SwSectionNode const&>(rNode).GetSection().IsHiddenFlag())
            {   // skip hidden sections - they can be inserted in fly-frames :(
                j = rNode.EndOfSectionNode()->GetIndex();
                continue;
            }
            if (rNode.IsContentNode())
            {
                SwContentNode const& rCNode(static_cast<SwContentNode const&>(rNode));
                if (!rCNode.getLayoutFrame(&rLayout))
                {   // ignore footnote/fly/header/footer with no layout frame
                    bSkip = true; // they will be created from scratch later if needed
                }
                break;
            }
        }
        if (!bSkip)
        {
            UnHideRedlines(rLayout, rNodes, rEndNode, pSkipped);
        }
    }
}

static void UnHide(SwRootFrame & rLayout)
{
    assert(rLayout.GetCurrShell()->ActionPend()); // tdf#125754 avoid recursive layout
    SwDoc & rDoc(rLayout.GetFormat()->GetDoc());
    // don't do early return if there are no redlines:
    // Show->Hide must init hidden number trees
    // Hide->Show may be called after all redlines have been deleted but there
    //            may still be MergedParas because those aren't deleted yet...
#if 0
    if (!bHideRedlines
        && rDoc.getIDocumentRedlineAccess().GetRedlineTable().empty())
    {
        return;
    }
#endif
    // Hide->Show: clear MergedPara, create frames
    // Show->Hide: call CheckParaRedlineMerge, delete frames
    // Traverse the document via the nodes-array; traversing via the layout
    // wouldn't find the nodes that don't have frames in the ->Show case.
    // In-order traversal of each nodes array section should init the flags
    // in nodes before they are iterated.
    // Actual creation of frames should be done with existing functions
    // if possible, particularly InsertCnt_() or its wrapper ::MakeFrames().
    SwNodes /*const*/& rNodes(rDoc.GetNodes());
    // Flys/footnotes: must iterate and find all the ones that already exist
    // with frames and have redlines inside them; if any don't have frames at
    // all, they will be created (if necessary) from scratch and completely by
    // MakeFrames().
    //
    // Flys before footnotes: because footnotes may contain flys but not
    // vice-versa; alas flys may contain flys, so we skip some of them
    // if they have already been created from scratch via their anchor flys.
    std::set<SwNodeOffset> skippedFlys;
    UnHideRedlinesExtras(rLayout, rNodes, rNodes.GetEndOfAutotext(),
        // when un-hiding, delay all fly frame creation to AppendAllObjs below
                         rLayout.HasMergedParas() ? &skippedFlys : nullptr);
    // Footnotes are created automatically (after invalidation etc.) by
    // ConnectFootnote(), but need to be deleted manually. Footnotes do not
    // occur in flys or headers/footers.
    UnHideRedlinesExtras(rLayout, rNodes, rNodes.GetEndOfInserts(), nullptr);
    UnHideRedlines(rLayout, rNodes, rNodes.GetEndOfContent(), nullptr);

    if (!rLayout.HasMergedParas())
    {   // create all previously hidden flys at once:
        // * Flys on first node of pre-existing merged frames that are hidden
        //   (in delete redline), to be added to the existing frame
        // * Flys on non-first (hidden/merged) nodes of pre-existing merged
        //   frames, to be added to the new frame of their node
        // * Flys anchored in other flys that are hidden
        AppendAllObjs(rDoc.GetSpzFrameFormats(), &rLayout);
    }

    const bool bIsShowChangesInMargin = rLayout.GetCurrShell()->GetViewOptions()->IsShowChangesInMargin();
    for (auto const pRedline : rDoc.getIDocumentRedlineAccess().GetRedlineTable())
    {   // DELETE are handled by the code above; for other types, need to
        // trigger repaint of text frames to add/remove the redline color font
        // (handle deletions showed in margin also here)
        if (bIsShowChangesInMargin || pRedline->GetType() != RedlineType::Delete)
        {
            pRedline->InvalidateRange(SwRangeRedline::Invalidation::Add);
        }
    }

    SwFootnoteIdxs & rFootnotes(rDoc.GetFootnoteIdxs());
    if (rDoc.GetFootnoteInfo().m_eNum == FTNNUM_CHAPTER)
    {
        // sadly determining which node is outline node requires hidden layout
        rFootnotes.UpdateAllFootnote();
    }
    // invalidate all footnotes to reformat their numbers
    for (SwTextFootnote *const pFootnote : rFootnotes)
    {
        SwFormatFootnote const& rFootnote(pFootnote->GetFootnote());
        if (rFootnote.GetNumber() != rFootnote.GetNumberRLHidden()
            && rFootnote.GetNumStr().isEmpty())
        {
            pFootnote->InvalidateNumberInLayout();
        }
    }
    // update various fields to re-expand them with the new layout
    IDocumentFieldsAccess & rIDFA(rDoc.getIDocumentFieldsAccess());
    auto const pAuthType(rIDFA.GetFieldType(
        SwFieldIds::TableOfAuthorities, OUString(), false));
    if (pAuthType) // created on demand...
    {   // calling DelSequenceArray() should be unnecessary here since the
        // sequence doesn't depend on frames
        pAuthType->UpdateFields();
    }
    rIDFA.GetFieldType(SwFieldIds::RefPageGet, OUString(), false)->UpdateFields();
    rIDFA.GetSysFieldType(SwFieldIds::Chapter)->UpdateFields();
    rIDFA.UpdateExpFields(nullptr, false);
    rIDFA.UpdateRefFields();

    // update SwPostItMgr / notes in the margin
    // note: as long as all shells share layout, broadcast to all shells!
    rDoc.GetDocShell()->Broadcast( SwFormatFieldHint(nullptr, rLayout.HasMergedParas()
            ? SwFormatFieldHintWhich::REMOVED
            : SwFormatFieldHintWhich::INSERTED) );


//    InvalidateAllContent(SwInvalidateFlags::Size); // ??? TODO what to invalidate?  this is the big hammer
}

void SwRootFrame::SetHideRedlines(bool const bHideRedlines)
{
    if (bHideRedlines == mbHideRedlines)
    {
        return;
    }
    // TODO: remove temporary ShowBoth
    sw::FieldmarkMode const eMode(m_FieldmarkMode);
    sw::ParagraphBreakMode const ePBMode(m_ParagraphBreakMode);
    if (HasMergedParas())
    {
        m_FieldmarkMode = sw::FieldmarkMode::ShowBoth;
        m_ParagraphBreakMode = sw::ParagraphBreakMode::Shown;
        mbHideRedlines = false;
        UnHide(*this);
    }
    if (bHideRedlines || eMode != m_FieldmarkMode || ePBMode != m_ParagraphBreakMode)
    {
        m_FieldmarkMode = eMode;
        m_ParagraphBreakMode = ePBMode;
        mbHideRedlines = bHideRedlines;
        UnHide(*this);
    }
}

void SwRootFrame::SetFieldmarkMode(sw::FieldmarkMode const eFMMode, sw::ParagraphBreakMode const ePBMode)
{
    if (eFMMode == m_FieldmarkMode && ePBMode == m_ParagraphBreakMode)
    {
        return;
    }
    // TODO: remove temporary ShowBoth
    bool const isHideRedlines(mbHideRedlines);
    if (HasMergedParas())
    {
        mbHideRedlines = false;
        m_FieldmarkMode = sw::FieldmarkMode::ShowBoth;
        m_ParagraphBreakMode = sw::ParagraphBreakMode::Shown;
        UnHide(*this);
    }
    if (isHideRedlines || eFMMode != sw::FieldmarkMode::ShowBoth || ePBMode == sw::ParagraphBreakMode::Hidden)
    {
        mbHideRedlines = isHideRedlines;
        m_FieldmarkMode = eFMMode;
        m_ParagraphBreakMode = ePBMode;
        UnHide(*this);
    }
}

bool SwRootFrame::HasMergedParas() const
{
    return IsHideRedlines()
        || GetFieldmarkMode() != sw::FieldmarkMode::ShowBoth
        || GetParagraphBreakMode() == sw::ParagraphBreakMode::Hidden;
}

namespace {
    xmlTextWriterPtr lcl_createDefaultWriter()
    {
        xmlTextWriterPtr writer = xmlNewTextWriterFilename( "layout.xml", 0 );
        xmlTextWriterSetIndent(writer,1);
        (void)xmlTextWriterSetIndentString(writer, BAD_CAST("  "));
        (void)xmlTextWriterStartDocument( writer, nullptr, nullptr, nullptr );
        return writer;
    }

    void lcl_freeWriter( xmlTextWriterPtr writer )
    {
        (void)xmlTextWriterEndDocument( writer );
        xmlFreeTextWriter( writer );
    }
}

void SwRootFrame::dumpAsXml(xmlTextWriterPtr writer) const
{
    bool bCreateWriter = (nullptr == writer);
    if (bCreateWriter)
        writer = lcl_createDefaultWriter();

    (void)xmlTextWriterStartElement(writer, reinterpret_cast<const xmlChar*>("root"));
    dumpAsXmlAttributes(writer);

    (void)xmlTextWriterStartElement(writer, BAD_CAST("sfxViewShells"));
    SwView* pView = static_cast<SwView*>(SfxViewShell::GetFirst(true, checkSfxViewShell<SwView>));
    while (pView)
    {
        if (GetCurrShell()->GetSfxViewShell() && pView->GetObjectShell() == GetCurrShell()->GetSfxViewShell()->GetObjectShell())
            pView->dumpAsXml(writer);
        pView = static_cast<SwView*>(SfxViewShell::GetNext(*pView, true, checkSfxViewShell<SwView>));
    }
    (void)xmlTextWriterEndElement(writer);

    (void)xmlTextWriterStartElement(writer, BAD_CAST("infos"));
    dumpInfosAsXml(writer);
    (void)xmlTextWriterEndElement(writer);
    dumpChildrenAsXml(writer);
    (void)xmlTextWriterEndElement(writer);

    if (bCreateWriter)
        lcl_freeWriter(writer);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
