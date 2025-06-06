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

#include <sal/log.hxx>
#include <osl/diagnose.h>
#include <svx/swframetypes.hxx>
#include <pagefrm.hxx>
#include <txtfrm.hxx>
#include <notxtfrm.hxx>
#include <doc.hxx>
#include <pam.hxx>
#include <IDocumentUndoRedo.hxx>
#include <IDocumentSettingAccess.hxx>
#include <IDocumentDrawModelAccess.hxx>
#include <frmtool.hxx>
#include <dflyobj.hxx>
#include <fmtanchr.hxx>
#include <fmtornt.hxx>
#include <fmtfsize.hxx>
#include <fmtsrnd.hxx>
#include <txatbase.hxx>

#include <tabfrm.hxx>
#include <flyfrms.hxx>
#include <crstate.hxx>
#include <sectfrm.hxx>

#include <tocntntanchoredobjectposition.hxx>
#include <sortedobjs.hxx>
#include <layouter.hxx>
#include "objectformattertxtfrm.hxx"
#include <HandleAnchorNodeChg.hxx>
#include <ndtxt.hxx>
#include <textboxhelper.hxx>
#include <fmtfollowtextflow.hxx>
#include <unoprnms.hxx>
#include <rootfrm.hxx>
#include <bodyfrm.hxx>
#include <formatwraptextatflystart.hxx>

using namespace ::com::sun::star;

namespace
{

SwTwips lcl_GetTopForObjPos(const SwContentFrame* pCnt, const bool bVert, const bool bVertL2R)
{
    if ( bVert )
    {
        SwTwips aResult = pCnt->getFrameArea().Left();
        if ( bVertL2R )
            aResult += pCnt->GetUpperSpaceAmountConsideredForPrevFrameAndPageGrid();
        else
            aResult += pCnt->getFrameArea().Width() - pCnt->GetUpperSpaceAmountConsideredForPrevFrameAndPageGrid();
        return aResult;
    }
    else
        return pCnt->getFrameArea().Top() + pCnt->GetUpperSpaceAmountConsideredForPrevFrameAndPageGrid();
}

}

SwFlyAtContentFrame::SwFlyAtContentFrame( SwFlyFrameFormat *pFormat, SwFrame* pSib, SwFrame *pAnch, bool bFollow ) :
    SwFlyFreeFrame( pFormat, pSib, pAnch, bFollow ),
    SwFlowFrame(static_cast<SwFrame&>(*this))
{
    m_bAtCnt = true;
    m_bAutoPosition = (RndStdIds::FLY_AT_CHAR == pFormat->GetAnchor().GetAnchorId());
}

SwFlyAtContentFrame::SwFlyAtContentFrame(SwFlyAtContentFrame& rPrecede)
    : SwFlyAtContentFrame(rPrecede.GetFormat(), const_cast<SwFrame*>(rPrecede.GetAnchorFrame()),
                          const_cast<SwFrame*>(rPrecede.GetAnchorFrame()), /*bFollow=*/true)
{
    SetFollow(rPrecede.GetFollow());
    rPrecede.SetFollow(this);
}

SwFlyAtContentFrame::~SwFlyAtContentFrame()
{
}

// #i28701#

void SwFlyAtContentFrame::SwClientNotify(const SwModify& rMod, const SfxHint& rHint)
{
    if (rHint.GetId() != SfxHintId::SwLegacyModify && rHint.GetId() != SfxHintId::SwAttrSetChange)
    {
        SwFlyFrame::SwClientNotify(rMod, rHint);
        return;
    }
    const SwFormatAnchor* pAnch;
    if (rHint.GetId() == SfxHintId::SwLegacyModify)
    {
        auto pLegacy = static_cast<const sw::LegacyModifyHint*>(&rHint);
        pAnch = pLegacy->m_pNew ? GetAnchorFromPoolItem(*pLegacy->m_pNew) : nullptr;
    }
    else // rHint.GetId() == SfxHintId::SwAttrSetChange
    {
        auto pChangeHint = static_cast<const sw::AttrSetChangeHint*>(&rHint);
        pAnch = pChangeHint->m_pNew ? GetAnchorFromPoolItem(*pChangeHint->m_pNew) : nullptr;
    }
    if(!pAnch)
    {
        SwFlyFrame::SwClientNotify(rMod, rHint);
        return;
    }
    OSL_ENSURE(pAnch->GetAnchorId() == GetFormat()->GetAnchor().GetAnchorId(),
            "Illegal change of anchor type.");

    //Unregister, get hold of a new anchor and attach it
    SwRect aOld(GetObjRectWithSpaces());
    SwPageFrame* pOldPage = FindPageFrame();
    const SwFrame* pOldAnchor = GetAnchorFrame();
    SwContentFrame* pContent = const_cast<SwContentFrame*>(static_cast<const SwContentFrame*>(GetAnchorFrame()));
    AnchorFrame()->RemoveFly(this);

    const bool bBodyFootnote = (pContent->IsInDocBody() || pContent->IsInFootnote());

    // Search the new anchor using the NodeIdx; the relation between old
    // and new NodeIdx determines the search direction
    const SwNodeIndex aNewIdx(*pAnch->GetAnchorNode());
    SwNodeIndex aOldIdx(pContent->IsTextFrame()
            // sw_redlinehide: can pick any node here, the compare with
            // FrameContainsNode should catch it
            ? *static_cast<SwTextFrame *>(pContent)->GetTextNodeFirst()
            : *static_cast<SwNoTextFrame *>(pContent)->GetNode());

    //fix: depending on which index was smaller, searching in the do-while
    //loop previously was done forward or backwards respectively. This however
    //could lead to an infinite loop. To at least avoid the loop, searching
    //is now done in only one direction. Getting hold of a frame from the node
    //is still possible if the new anchor could not be found. Chances are
    //good that this will be the correct one.
    // consider the case that at found anchor frame candidate already a
    // fly frame of the given fly format is registered.
    // consider, that <pContent> is the already
    // the new anchor frame.
    bool bFound(FrameContainsNode(*pContent, aNewIdx.GetIndex()));
    const bool bNext = !bFound && aOldIdx < aNewIdx;
    while(pContent && !bFound)
    {
        do
        {
            if(bNext)
                pContent = pContent->GetNextContentFrame();
            else
                pContent = pContent->GetPrevContentFrame();
        } while(pContent &&
                  (bBodyFootnote != (pContent->IsInDocBody() || pContent->IsInFootnote())));
        if(pContent)
            bFound = FrameContainsNode(*pContent, aNewIdx.GetIndex());

        // check, if at found anchor frame candidate already a fly frame
        // of the given fly frame format is registered.
        if(bFound && pContent && pContent->GetDrawObjs())
        {
            SwFrameFormat* pMyFlyFrameFormat(GetFrameFormat());
            SwSortedObjs &rObjs = *pContent->GetDrawObjs();
            for(SwAnchoredObject* rObj : rObjs)
            {
                SwFlyFrame* pFlyFrame = rObj->DynCastFlyFrame();
                if (pFlyFrame &&
                     pFlyFrame->GetFrameFormat() == pMyFlyFrameFormat)
                {
                    bFound = false;
                    break;
                }
            }
        }
    }
    if(!pContent)
    {
        SwContentNode *pNode = aNewIdx.GetNode().GetContentNode();
        std::pair<Point, bool> const tmp(pOldAnchor->getFrameArea().Pos(), false);
        pContent = pNode->getLayoutFrame(getRootFrame(), nullptr, &tmp);
        OSL_ENSURE(pContent, "New anchor not found");
    }
    //Flys are never attached to a follow, but always on the master which
    //we are going to search now.
    SwContentFrame* pFlow = pContent;
    while(pFlow->IsFollow())
        pFlow = pFlow->FindMaster();
    pContent = pFlow;

    //and *puff* it's attached...
    pContent->AppendFly( this );
    if(pOldPage && pOldPage != FindPageFrame())
        NotifyBackground(pOldPage, aOld, PrepareHint::FlyFrameLeave);

    //Fix(3495)
    InvalidatePos_();
    InvalidatePage();
    SetNotifyBack();
    // #i28701# - reset member <maLastCharRect> and
    // <mnLastTopOfLine> for to-character anchored objects.
    ClearCharRectAndTopOfLine();
}

//We need some helper classes to monitor the oscillation and a few functions
//to not get lost.

namespace {

// #i3317# - re-factoring of the position stack
class SwOszControl
{
    static const SwFlyFrame* s_pStack1;
    static const SwFlyFrame* s_pStack2;
    static const SwFlyFrame* s_pStack3;
    static const SwFlyFrame* s_pStack4;
    static const SwFlyFrame* s_pStack5;

    const SwFlyFrame* m_pFly;
    std::vector<Point> maObjPositions;

public:
    explicit SwOszControl( const SwFlyFrame *pFrame );
    ~SwOszControl();
    bool ChkOsz();
    static bool IsInProgress( const SwFlyFrame *pFly );
};

}

const SwFlyFrame* SwOszControl::s_pStack1 = nullptr;
const SwFlyFrame* SwOszControl::s_pStack2 = nullptr;
const SwFlyFrame* SwOszControl::s_pStack3 = nullptr;
const SwFlyFrame* SwOszControl::s_pStack4 = nullptr;
const SwFlyFrame* SwOszControl::s_pStack5 = nullptr;

SwOszControl::SwOszControl(const SwFlyFrame* pFrame)
    : m_pFly(pFrame)
{
    if (!SwOszControl::s_pStack1)
        SwOszControl::s_pStack1 = m_pFly;
    else if (!SwOszControl::s_pStack2)
        SwOszControl::s_pStack2 = m_pFly;
    else if (!SwOszControl::s_pStack3)
        SwOszControl::s_pStack3 = m_pFly;
    else if (!SwOszControl::s_pStack4)
        SwOszControl::s_pStack4 = m_pFly;
    else if (!SwOszControl::s_pStack5)
        SwOszControl::s_pStack5 = m_pFly;
}

SwOszControl::~SwOszControl()
{
    if (SwOszControl::s_pStack1 == m_pFly)
        SwOszControl::s_pStack1 = nullptr;
    else if (SwOszControl::s_pStack2 == m_pFly)
        SwOszControl::s_pStack2 = nullptr;
    else if (SwOszControl::s_pStack3 == m_pFly)
        SwOszControl::s_pStack3 = nullptr;
    else if (SwOszControl::s_pStack4 == m_pFly)
        SwOszControl::s_pStack4 = nullptr;
    else if (SwOszControl::s_pStack5 == m_pFly)
        SwOszControl::s_pStack5 = nullptr;
    // #i3317#
    maObjPositions.clear();
}

bool SwOszControl::IsInProgress( const SwFlyFrame *pFly )
{
    if (SwOszControl::s_pStack1 && !pFly->IsLowerOf(SwOszControl::s_pStack1))
        return true;
    if (SwOszControl::s_pStack2 && !pFly->IsLowerOf(SwOszControl::s_pStack2))
        return true;
    if (SwOszControl::s_pStack3 && !pFly->IsLowerOf(SwOszControl::s_pStack3))
        return true;
    if (SwOszControl::s_pStack4 && !pFly->IsLowerOf(SwOszControl::s_pStack4))
        return true;
    if (SwOszControl::s_pStack5 && !pFly->IsLowerOf(SwOszControl::s_pStack5))
        return true;
    return false;
}

bool SwOszControl::ChkOsz()
{
    bool bOscillationDetected = false;

    if ( maObjPositions.size() == 20 )
    {
        // #i3317# position stack is full -> oscillation
        bOscillationDetected = true;
    }
    else
    {
        Point aNewObjPos = m_pFly->GetObjRect().Pos();
        for ( auto const & pt : maObjPositions )
        {
            if ( aNewObjPos == pt )
            {
                // position already occurred -> oscillation
                bOscillationDetected = true;
                break;
            }
        }
        if ( !bOscillationDetected )
        {
            maObjPositions.push_back( aNewObjPos );
        }
    }

    return bOscillationDetected;
}

/**
|*      With a paragraph-anchored fly it's absolutely possible that
|*      the anchor reacts to changes of the fly. To this reaction the fly must
|*      certainly react too. Sadly this can lead to oscillations; for example the
|*      fly wants to go down therefore the content can go up - this leads to a
|*      smaller TextFrame thus the fly needs to go up again whereby the text will
|*      get pushed down...
|*      To avoid such oscillations, a small position stack is built. If the fly
|*      reaches a position which it already had once, the action is stopped.
|*      To not run into problems, the stack is designed to hold five positions.
|*      If the stack flows over, the action is stopped too.
|*      Cancellation leads to the situation that the fly has a bad position in
|*      the end. In case of cancellation, the frame is set to automatic top
|*      alignment to not trigger a 'big oscillation' when calling from outside
|*      again.
|*/
void SwFlyAtContentFrame::MakeAll(vcl::RenderContext* pRenderContext)
{
    const SwDoc& rDoc = GetFormat()->GetDoc();
    if (!rDoc.getIDocumentDrawModelAccess().IsVisibleLayerId(GetVirtDrawObj()->GetLayer()))
    {
        return;
    }

    if ( SwOszControl::IsInProgress( this ) || IsLocked() || IsColLocked() )
        return;

    // #i28701# - use new method <GetPageFrame()>
    if( !GetPageFrame() && GetAnchorFrame() && GetAnchorFrame()->IsInFly() )
    {
        SwFlyFrame* pFly = AnchorFrame()->FindFlyFrame();
        SwPageFrame *pTmpPage = pFly ? pFly->FindPageFrame() : nullptr;
        if( pTmpPage )
            pTmpPage->AppendFlyToPage( this );
    }
    // #i28701# - use new method <GetPageFrame()>
    if( !GetPageFrame() )
        return;

    bSetCompletePaintOnInvalidate = true;
    {
        SwFlyFrameFormat *pFormat = GetFormat();
        const SwFormatFrameSize &rFrameSz = GetFormat()->GetFrameSize();
        if( rFrameSz.GetHeightPercent() != SwFormatFrameSize::SYNCED &&
            rFrameSz.GetHeightPercent() >= 100 )
        {
            pFormat->LockModify();
            SwFormatSurround aMain( pFormat->GetSurround() );
            if ( aMain.GetSurround() == css::text::WrapTextMode_NONE )
            {
                aMain.SetSurround( css::text::WrapTextMode_THROUGH );
                pFormat->SetFormatAttr( aMain );
            }
            pFormat->UnlockModify();
        }
    }

    SwOszControl aOszCntrl( this );

    // #i43255#
    // #i50356# - format the anchor frame, which
    // contains the anchor position. E.g., for at-character anchored
    // object this can be the follow frame of the anchor frame.
    const bool bFormatAnchor =
            !static_cast<const SwTextFrame*>( GetAnchorFrameContainingAnchPos() )->IsAnyJoinLocked() &&
            !ConsiderObjWrapInfluenceOnObjPos() &&
            !ConsiderObjWrapInfluenceOfOtherObjs();

    const SwFrame* pFooter = GetAnchorFrame()->FindFooterOrHeader();
    if( pFooter && !pFooter->IsFooterFrame() )
        pFooter = nullptr;
    bool bOsz = false;
    bool bExtra = Lower() && Lower()->IsColumnFrame();
    // #i3317# - boolean, to apply temporarily the
    // 'straightforward positioning process' for the frame due to its
    // overlapping with a previous column.
    bool bConsiderWrapInfluenceDueToOverlapPrevCol( false );
    //  #i35911# - boolean, to apply temporarily the
    // 'straightforward positioning process' for the frame due to fact
    // that it causes the complete content of its layout environment
    // to move forward.
    // #i40444# - extend usage of this boolean:
    // apply temporarily the 'straightforward positioning process' for
    // the frame due to the fact that the frame clears the area for
    // the anchor frame, thus it has to move forward.
    bool bConsiderWrapInfluenceDueToMovedFwdAnchor( false );
    do {
        SwRectFnSet aRectFnSet(this);
        Point aOldPos( aRectFnSet.GetPos(getFrameArea()) );
        SwFlyFreeFrame::MakeAll(pRenderContext);

        const bool bPosChgDueToOwnFormat =
                                aOldPos != aRectFnSet.GetPos(getFrameArea());
        // #i3317#
        if ( !ConsiderObjWrapInfluenceOnObjPos() &&
             OverlapsPrevColumn() )
        {
            bConsiderWrapInfluenceDueToOverlapPrevCol = true;
        }
        // #i28701# - no format of anchor frame, if
        // wrapping style influence is considered on object positioning
        if ( bFormatAnchor )
        {
            SwTextFrame& rAnchPosAnchorFrame =
                    *GetAnchorFrameContainingAnchPos()->DynCastTextFrame();
            // #i58182# - For the usage of new method
            // <SwObjectFormatterTextFrame::CheckMovedFwdCondition(..)>
            // to check move forward of anchor frame due to the object
            // positioning it's needed to know, if the object is anchored
            // at the master frame before the anchor frame is formatted.
            const bool bAnchoredAtMaster(!rAnchPosAnchorFrame.IsFollow());

            // #i56300#
            // perform complete format of anchor text frame and its
            // previous frames, which have become invalid due to the
            // fly frame format.
            SwObjectFormatterTextFrame::FormatAnchorFrameAndItsPrevs( rAnchPosAnchorFrame );
            // #i35911#
            // #i40444#
            // #i58182# - usage of new method
            // <SwObjectFormatterTextFrame::CheckMovedFwdCondition(..)>
            sal_uInt32 nToPageNum( 0 );
            bool bDummy( false );
            bool bPageHasFlysAnchoredBelowThis(false);
            if ( SwObjectFormatterTextFrame::CheckMovedFwdCondition(
// TODO: what if this fly moved bc it's in table? does sth prevent that?
                                *this, *GetPageFrame(),
                                bAnchoredAtMaster, nToPageNum, bDummy,
                                bPageHasFlysAnchoredBelowThis) )
            {
                if (!bPageHasFlysAnchoredBelowThis)
                {
                    bConsiderWrapInfluenceDueToMovedFwdAnchor = true;
                }
                // mark anchor text frame
                // directly, that it is moved forward by object positioning.
                SwTextFrame* pAnchorTextFrame( static_cast<SwTextFrame*>(AnchorFrame()) );
                bool bInsert( true );
                sal_uInt32 nAnchorFrameToPageNum( 0 );
                if ( SwLayouter::FrameMovedFwdByObjPos(
                                        rDoc, *pAnchorTextFrame, nAnchorFrameToPageNum ) )
                {
                    if ( nAnchorFrameToPageNum < nToPageNum )
                    {
                        if (!bPageHasFlysAnchoredBelowThis)
                        {
                            SwLayouter::RemoveMovedFwdFrame(rDoc, *pAnchorTextFrame);
                        }
                    }
                    else
                        bInsert = false;
                }
                if ( bInsert )
                {
                    if (!bPageHasFlysAnchoredBelowThis)
                    {
                        SwLayouter::InsertMovedFwdFrame(rDoc, *pAnchorTextFrame,
                                                        nToPageNum);
                    }
                }
            }
        }

        if ( aOldPos != aRectFnSet.GetPos(getFrameArea()) ||
             ( !isFrameAreaPositionValid() &&
               ( pFooter || bPosChgDueToOwnFormat ) ) )
        {
            bOsz = aOszCntrl.ChkOsz();

            // special loop prevention for dedicated document:
            if ( bOsz &&
                 HasFixSize() && IsClipped() &&
                 GetAnchorFrame()->GetUpper()->IsCellFrame() )
            {
                SwFrameFormat* pFormat = GetFormat();
                const SwFormatFrameSize& rFrameSz = pFormat->GetFrameSize();
                if ( rFrameSz.GetWidthPercent() &&
                     rFrameSz.GetHeightPercent() == SwFormatFrameSize::SYNCED )
                {
                    SwFormatSurround aSurround( pFormat->GetSurround() );
                    if ( aSurround.GetSurround() == css::text::WrapTextMode_NONE )
                    {
                        pFormat->LockModify();
                        aSurround.SetSurround( css::text::WrapTextMode_THROUGH );
                        pFormat->SetFormatAttr( aSurround );
                        pFormat->UnlockModify();
                        bOsz = false;
                        OSL_FAIL( "<SwFlyAtContentFrame::MakeAll()> - special loop prevention for dedicated document of b6403541 applied" );
                    }
                }
            }
        }

        if ( bExtra && Lower() && !Lower()->isFrameAreaPositionValid() )
        {
            // If a multi column frame leaves invalid columns because of
            // a position change, we loop once more and format
            // our content using FormatWidthCols again.
            InvalidateSize_();
            bExtra = false; // Ensure only one additional loop run
        }
    } while ( !isFrameAreaDefinitionValid() && !bOsz &&
              // #i3317#
              !bConsiderWrapInfluenceDueToOverlapPrevCol &&
              // #i40444#
              !bConsiderWrapInfluenceDueToMovedFwdAnchor &&
              rDoc.getIDocumentDrawModelAccess().IsVisibleLayerId( GetVirtDrawObj()->GetLayer() ) );

    // #i3317# - instead of attribute change apply
    // temporarily the 'straightforward positioning process'.
    // #i80924#
    // handle special case during splitting of table rows
    if ( bConsiderWrapInfluenceDueToMovedFwdAnchor &&
         GetAnchorFrame()->IsInTab() &&
         GetAnchorFrame()->IsInFollowFlowRow() )
    {
        const SwFrame* pCellFrame = GetAnchorFrame();
        while ( pCellFrame && !pCellFrame->IsCellFrame() )
        {
            pCellFrame = pCellFrame->GetUpper();
        }
        if ( pCellFrame )
        {
            SwRectFnSet aRectFnSet(pCellFrame);
            if ( aRectFnSet.GetTop(pCellFrame->getFrameArea()) == 0 &&
                 aRectFnSet.GetHeight(pCellFrame->getFrameArea()) == 0 )
            {
                bConsiderWrapInfluenceDueToMovedFwdAnchor = false;
            }
        }
    }
    // tdf#137803: Fix the position of the shape during autoSize
    SwFrameFormat* pShapeFormat
        = SwTextBoxHelper::getOtherTextBoxFormat(GetFormat(), RES_FLYFRMFMT);
    // FIXME: According to tdf37153, ignore FollowTextFlow objs, because
    // wrong position will applied in that case. FollowTextFlow needs fix.
    if (pShapeFormat && !pShapeFormat->GetFollowTextFlow().GetValue() &&
        SwTextBoxHelper::getProperty(pShapeFormat,
            UNO_NAME_FRAME_ISAUTOMATIC_HEIGHT).hasValue() &&
        SwTextBoxHelper::getProperty(pShapeFormat,
            UNO_NAME_FRAME_ISAUTOMATIC_HEIGHT).get<bool>() )
    {
        // get the text area of the shape
        const tools::Rectangle aTextRectangle
            = SwTextBoxHelper::getRelativeTextRectangle(pShapeFormat->FindRealSdrObject());
        // get the original textframe position
        SwFormatHoriOrient aHOri = pShapeFormat->GetHoriOrient();
        SwFormatVertOrient aVOri = pShapeFormat->GetVertOrient();
        // calc the right position of the shape depending on text area
        aHOri.SetPos(aHOri.GetPos() + aTextRectangle.Left());
        aVOri.SetPos(aVOri.GetPos() + aTextRectangle.Top());
        // save the new position for the shape
        auto pFormat = GetFormat();
        const bool bLocked = pFormat->IsModifyLocked();
        if (!bLocked)
            pFormat->LockModify();
        pFormat->SetFormatAttr(aHOri);
        pFormat->SetFormatAttr(aVOri);
        if (!bLocked)
            pFormat->UnlockModify();
    }
    if ( bOsz || bConsiderWrapInfluenceDueToOverlapPrevCol ||
         // #i40444#
         bConsiderWrapInfluenceDueToMovedFwdAnchor )
    {
        SetTmpConsiderWrapInfluence( true );
        SetRestartLayoutProcess( true );
        SetTmpConsiderWrapInfluenceOfOtherObjs();
    }
    bSetCompletePaintOnInvalidate = false;
}

/** method to determine, if a <MakeAll()> on the Writer fly frame is possible

    #i28701#
*/
bool SwFlyAtContentFrame::IsFormatPossible() const
{
    return SwFlyFreeFrame::IsFormatPossible() &&
           !SwOszControl::IsInProgress( this );
}

namespace {

class SwDistance
{
public:
    SwTwips m_nMain, m_nSub;
    SwDistance()
        : m_nMain(0)
        , m_nSub(0)
    {
    }
    bool operator<( const SwDistance& rTwo ) const
        {
            return m_nMain < rTwo.m_nMain
                   || (m_nMain == rTwo.m_nMain && m_nSub && rTwo.m_nSub && m_nSub < rTwo.m_nSub);
        }
    bool operator<=( const SwDistance& rTwo ) const
        {
            return m_nMain < rTwo.m_nMain
                   || (m_nMain == rTwo.m_nMain
                       && (!m_nSub || !rTwo.m_nSub || m_nSub <= rTwo.m_nSub));
        }
};

}

static const SwFrame * lcl_CalcDownDist( SwDistance &rRet,
                                         const Point &rPt,
                                         const SwContentFrame *pCnt )
{
    rRet.m_nSub = 0;
    //If the point stays inside the Cnt everything is clear already; the Content
    //automatically has a distance of 0.
    if ( pCnt->getFrameArea().Contains( rPt ) )
    {
        rRet.m_nMain = 0;
        return pCnt;
    }
    else
    {
        const SwLayoutFrame *pUp = pCnt->IsInTab() ? pCnt->FindTabFrame()->GetUpper() : pCnt->GetUpper();
        // single column sections need to interconnect to their upper
        while( pUp->IsSctFrame() )
            pUp = pUp->GetUpper();
        const bool bVert = pUp->IsVertical();

        const bool bVertL2R = pUp->IsVertLR();

        //Follow the text flow.
        // #i70582#
        // --> OD 2009-03-05 - adopted for Support for Classical Mongolian Script
        const SwTwips nTopForObjPos = lcl_GetTopForObjPos(pCnt, bVert, bVertL2R);
        if ( pUp->getFrameArea().Contains( rPt ) )
        {
            // <rPt> point is inside environment of given content frame
            // #i70582#
            if( bVert )
            {
                if ( bVertL2R )
                    rRet.m_nMain = rPt.X() - nTopForObjPos;
                else
                    rRet.m_nMain = nTopForObjPos - rPt.X();
            }
            else
                rRet.m_nMain = rPt.Y() - nTopForObjPos;
            return pCnt;
        }
        else if ( rPt.Y() <= pUp->getFrameArea().Top() )
        {
            // <rPt> point is above environment of given content frame
            // correct for vertical layout?
            rRet.m_nMain = LONG_MAX;
        }
        else if( rPt.X() < pUp->getFrameArea().Left() &&
                 rPt.Y() <= ( bVert ? pUp->getFrameArea().Top() : pUp->getFrameArea().Bottom() ) )
        {
            // <rPt> point is left of environment of given content frame
            // seems not to be correct for vertical layout!?
            const SwFrame *pLay = pUp->GetLeaf( MAKEPAGE_NONE, false, pCnt );
            if( !pLay ||
                (bVert && (pLay->getFrameArea().Top() + pLay->getFramePrintArea().Bottom()) <rPt.Y())||
                (!bVert && (pLay->getFrameArea().Left() + pLay->getFramePrintArea().Right())<rPt.X()) )
            {
                // <rPt> point is in left border of environment
                // #i70582#
                if( bVert )
                {
                    if ( bVertL2R )
                        rRet.m_nMain = rPt.X() - nTopForObjPos;
                    else
                        rRet.m_nMain = nTopForObjPos - rPt.X();
                }
                else
                    rRet.m_nMain = rPt.Y() - nTopForObjPos;
                return pCnt;
            }
            else
                rRet.m_nMain = LONG_MAX;
        }
        else
        {
            rRet.m_nMain
                = bVert ? (bVertL2R
                               ? ((pUp->getFrameArea().Left() + pUp->getFramePrintArea().Right())
                                  - nTopForObjPos)
                               : (nTopForObjPos
                                  - (pUp->getFrameArea().Left() + pUp->getFramePrintArea().Left())))
                        : ((pUp->getFrameArea().Top() + pUp->getFramePrintArea().Bottom())
                           - nTopForObjPos);

            const SwFrame *pPre = pCnt;
            const SwFrame *pLay = pUp->GetLeaf( MAKEPAGE_NONE, true, pCnt );
            SwTwips nFrameTop = 0;
            SwTwips nPrtHeight = 0;
            bool bSct = false;
            const SwSectionFrame *pSect = pUp->FindSctFrame();
            if( pSect )
            {
                rRet.m_nSub = rRet.m_nMain;
                rRet.m_nMain = 0;
            }
            if( pSect && !pSect->IsAnLower( pLay ) )
            {
                bSct = false;
                const SwSectionFrame* pNxtSect = pLay ? pLay->FindSctFrame() : nullptr;
                if (pSect->IsAnFollow(pNxtSect) && pLay)
                {
                    if( pLay->IsVertical() )
                    {
                        if ( pLay->IsVertLR() )
                            nFrameTop = pLay->getFrameArea().Left();
                        else
                            nFrameTop = pLay->getFrameArea().Left() + pLay->getFrameArea().Width();
                        nPrtHeight = pLay->getFramePrintArea().Width();
                    }
                    else
                    {
                        nFrameTop = pLay->getFrameArea().Top();
                        nPrtHeight = pLay->getFramePrintArea().Height();
                    }
                    pSect = pNxtSect;
                }
                else
                {
                    pLay = pSect->GetUpper();
                    if( pLay->IsVertical() )
                    {
                        if ( pLay->IsVertLR() )
                        {
                            nFrameTop = pSect->getFrameArea().Right();
                            nPrtHeight = pLay->getFrameArea().Left() + pLay->getFramePrintArea().Left()
                                         + pLay->getFramePrintArea().Width() - pSect->getFrameArea().Left()
                                         - pSect->getFrameArea().Width();
                        }
                        else
                        {
                            nFrameTop = pSect->getFrameArea().Left();
                            nPrtHeight = pSect->getFrameArea().Left() - pLay->getFrameArea().Left()
                                     - pLay->getFramePrintArea().Left();
                        }
                    }
                    else
                    {
                        nFrameTop = pSect->getFrameArea().Bottom();
                        nPrtHeight = pLay->getFrameArea().Top() + pLay->getFramePrintArea().Top()
                                     + pLay->getFramePrintArea().Height() - pSect->getFrameArea().Top()
                                     - pSect->getFrameArea().Height();
                    }
                    pSect = nullptr;
                }
            }
            else if( pLay )
            {
                if( pLay->IsVertical() )
                {
                    if ( pLay->IsVertLR() )
                    {
                        nFrameTop = pLay->getFrameArea().Left();
                        nPrtHeight = pLay->getFramePrintArea().Width();
                    }
                    else
                    {
                        nFrameTop = pLay->getFrameArea().Left() + pLay->getFrameArea().Width();
                        nPrtHeight = pLay->getFramePrintArea().Width();
                    }
                }
                else
                {
                    nFrameTop = pLay->getFrameArea().Top();
                    nPrtHeight = pLay->getFramePrintArea().Height();
                }
                bSct = nullptr != pSect;
            }
            while ( pLay && !pLay->getFrameArea().Contains( rPt ) &&
                    ( pLay->getFrameArea().Top() <= rPt.Y() || pLay->IsInFly() ||
                      ( pLay->IsInSct() &&
                      pLay->FindSctFrame()->GetUpper()->getFrameArea().Top() <= rPt.Y())) )
            {
                if ( pLay->IsFootnoteContFrame() )
                {
                    if ( !static_cast<const SwLayoutFrame*>(pLay)->Lower() )
                    {
                        SwFrame *pDel = const_cast<SwFrame*>(pLay);
                        pDel->Cut();
                        SwFrame::DestroyFrame(pDel);
                        return pPre;
                    }
                    return nullptr;
                }
                else
                {
                    if( bSct || pSect )
                        rRet.m_nSub += nPrtHeight;
                    else
                        rRet.m_nMain += nPrtHeight;
                    pPre = pLay;
                    pLay = pLay->GetLeaf( MAKEPAGE_NONE, true, pCnt );
                    if( pSect && !pSect->IsAnLower( pLay ) )
                    {   // If we're leaving a SwSectionFrame, the next Leaf-Frame
                        // is the part of the upper below the SectionFrame.
                        const SwSectionFrame* pNxtSect = pLay ?
                            pLay->FindSctFrame() : nullptr;
                        bSct = false;
                        if (pLay && pSect->IsAnFollow(pNxtSect))
                        {
                            pSect = pNxtSect;
                            if( pLay->IsVertical() )
                            {
                                if ( pLay->IsVertLR() )
                                {
                                    nFrameTop = pLay->getFrameArea().Left();
                                    nPrtHeight = pLay->getFramePrintArea().Width();
                                }
                                else
                                {
                                    nFrameTop = pLay->getFrameArea().Left() + pLay->getFrameArea().Width();
                                    nPrtHeight = pLay->getFramePrintArea().Width();
                                }
                            }
                            else
                            {
                                nFrameTop = pLay->getFrameArea().Top();
                                nPrtHeight = pLay->getFramePrintArea().Height();
                            }
                        }
                        else
                        {
                            pLay = pSect->GetUpper();
                            if( pLay->IsVertical() )
                            {
                                if ( pLay->IsVertLR() )
                                {
                                    nFrameTop = pSect->getFrameArea().Right();
                                    nPrtHeight = pLay->getFrameArea().Left()+pLay->getFramePrintArea().Left()
                                             + pLay->getFramePrintArea().Width() - pSect->getFrameArea().Left()
                                             - pSect->getFrameArea().Width();
                                }
                                else
                                {
                                    nFrameTop = pSect->getFrameArea().Left();
                                    nPrtHeight = pSect->getFrameArea().Left() -
                                            pLay->getFrameArea().Left() - pLay->getFramePrintArea().Left();
                                }
                            }
                            else
                            {
                                nFrameTop = pSect->getFrameArea().Bottom();
                                nPrtHeight = pLay->getFrameArea().Top()+pLay->getFramePrintArea().Top()
                                     + pLay->getFramePrintArea().Height() - pSect->getFrameArea().Top()
                                     - pSect->getFrameArea().Height();
                            }
                            pSect = nullptr;
                        }
                    }
                    else if( pLay )
                    {
                        if( pLay->IsVertical() )
                        {
                            if ( pLay->IsVertLR() )
                            {
                                 nFrameTop = pLay->getFrameArea().Left();
                                 nPrtHeight = pLay->getFramePrintArea().Width();
                            }
                            else
                            {
                                 nFrameTop = pLay->getFrameArea().Left() + pLay->getFrameArea().Width();
                                 nPrtHeight = pLay->getFramePrintArea().Width();
                            }
                        }
                        else
                        {
                            nFrameTop = pLay->getFrameArea().Top();
                            nPrtHeight = pLay->getFramePrintArea().Height();
                        }
                        bSct = nullptr != pSect;
                    }
                }
            }
            if ( pLay )
            {
                if ( pLay->getFrameArea().Contains( rPt ) )
                {
                    SwTwips nDiff = pLay->IsVertical() ? ( pLay->IsVertLR() ? ( rPt.X() - nFrameTop ) : ( nFrameTop - rPt.X() ) )
                                                       : ( rPt.Y() - nFrameTop );
                    if( bSct || pSect )
                        rRet.m_nSub += nDiff;
                    else
                        rRet.m_nMain += nDiff;
                }
                if ( pLay->IsFootnoteContFrame() && !static_cast<const SwLayoutFrame*>(pLay)->Lower() )
                {
                    SwFrame *pDel = const_cast<SwFrame*>(pLay);
                    pDel->Cut();
                    SwFrame::DestroyFrame(pDel);
                    return nullptr;
                }
                return pLay;
            }
            else
                rRet.m_nMain = LONG_MAX;
        }
    }
    return nullptr;
}

static sal_uInt64 lcl_FindCntDiff( const Point &rPt, const SwLayoutFrame *pLay,
                          const SwContentFrame *& rpCnt,
                          const bool bBody, const bool bFootnote )
{
    // Searches below pLay the nearest Cnt to the point. The reference point of
    //the Contents is always the left upper corner.
    //The Cnt should preferably be above the point.

    rpCnt = nullptr;
    sal_uInt64 nDistance = SAL_MAX_UINT64;
    sal_uInt64 nNearest  = SAL_MAX_UINT64;
    const SwContentFrame *pCnt = pLay ? pLay->ContainsContent() : nullptr;

    while ( pCnt && (bBody != pCnt->IsInDocBody() || bFootnote != pCnt->IsInFootnote()))
    {
        pCnt = pCnt->GetNextContentFrame();
        if ( !pLay->IsAnLower( pCnt ) )
            pCnt = nullptr;
    }
    const SwContentFrame *pNearest = pCnt;
    if ( pCnt )
    {
        do
        {
            //Calculate the distance between those two points.
            //'delta' X^2 + 'delta' Y^2 = 'distance'^2
            sal_uInt64 dX = std::max( pCnt->getFrameArea().Left(), rPt.X() ) -
                       std::min( pCnt->getFrameArea().Left(), rPt.X() ),
                  dY = std::max( pCnt->getFrameArea().Top(), rPt.Y() ) -
                       std::min( pCnt->getFrameArea().Top(), rPt.Y() );
            // square of the difference will do fine here
            const sal_uInt64 nDiff = (dX * dX) + (dY * dY);
            if ( pCnt->getFrameArea().Top() <= rPt.Y() )
            {
                if ( nDiff < nDistance )
                {
                    //This one is the nearer one
                    nDistance = nNearest = nDiff;
                    rpCnt = pNearest = pCnt;
                }
            }
            else if ( nDiff < nNearest )
            {
                nNearest = nDiff;
                pNearest = pCnt;
            }
            pCnt = pCnt->GetNextContentFrame();
            while ( pCnt &&
                    (bBody != pCnt->IsInDocBody() || bFootnote != pCnt->IsInFootnote()))
                pCnt = pCnt->GetNextContentFrame();

        }  while ( pCnt && pLay->IsAnLower( pCnt ) );
    }
    if (nDistance == SAL_MAX_UINT64)
    {   rpCnt = pNearest;
        return nNearest;
    }
    return nDistance;
}

static const SwContentFrame * lcl_FindCnt( const Point &rPt, const SwContentFrame *pCnt,
                                  const bool bBody, const bool bFootnote )
{
    //Starting from pCnt searches the ContentFrame whose left upper corner is the
    //nearest to the point.
    //Always returns a ContentFrame.

    //First the nearest Content inside the page which contains the Content is
    //searched. Starting from this page the pages in both directions need to
    //be considered. If possible a Content is returned whose Y-position is
    //above the point.
    const SwContentFrame  *pRet, *pNew;
    const SwLayoutFrame *pLay = pCnt->FindPageFrame();
    sal_uInt64 nDist; // not sure if a sal_Int32 would be enough?

    nDist = ::lcl_FindCntDiff( rPt, pLay, pNew, bBody, bFootnote );
    if ( pNew )
        pRet = pNew;
    else
    {   pRet  = pCnt;
        nDist = SAL_MAX_UINT64;
    }
    const SwContentFrame *pNearest = pRet;
    sal_uInt64 nNearest = nDist;

    if ( pLay )
    {
        const SwLayoutFrame *pPge = pLay;
        sal_uInt64 nOldNew = SAL_MAX_UINT64;
        for ( int i = 0; pPge->GetPrev() && (i < 3); ++i )
        {
            pPge = static_cast<const SwLayoutFrame*>(pPge->GetPrev());
            const sal_uInt64 nNew = ::lcl_FindCntDiff( rPt, pPge, pNew, bBody, bFootnote );
            if ( nNew < nDist )
            {
                if ( pNew->getFrameArea().Top() <= rPt.Y() )
                {
                    pRet = pNearest = pNew;
                    nDist = nNearest = nNew;
                }
                else if ( nNew < nNearest )
                {
                    pNearest = pNew;
                    nNearest = nNew;
                }
            }
            else if (nOldNew != SAL_MAX_UINT64 && nNew > nOldNew)
                break;
            else
                nOldNew = nNew;

        }
        pPge = pLay;
        nOldNew = SAL_MAX_UINT64;
        for ( int j = 0; pPge->GetNext() && (j < 3); ++j )
        {
            pPge = static_cast<const SwLayoutFrame*>(pPge->GetNext());
            const sal_uInt64 nNew = ::lcl_FindCntDiff( rPt, pPge, pNew, bBody, bFootnote );
            if ( nNew < nDist )
            {
                if ( pNew->getFrameArea().Top() <= rPt.Y() )
                {
                    pRet = pNearest = pNew;
                    nDist = nNearest = nNew;
                }
                else if ( nNew < nNearest )
                {
                    pNearest = pNew;
                    nNearest = nNew;
                }
            }
            else if (nOldNew != SAL_MAX_UINT64 && nNew > nOldNew)
                break;
            else
                nOldNew = nNew;
        }
    }
    if ( pRet->getFrameArea().Top() > rPt.Y() )
        return pNearest;
    else
        return pRet;
}

static void lcl_PointToPrt( Point &rPoint, const SwFrame *pFrame )
{
    SwRect aTmp( pFrame->getFramePrintArea() );
    aTmp += pFrame->getFrameArea().Pos();
    if ( rPoint.getX() < aTmp.Left() )
        rPoint.setX(aTmp.Left());
    else if ( rPoint.getX() > aTmp.Right() )
        rPoint.setX(aTmp.Right());
    if ( rPoint.getY() < aTmp.Top() )
        rPoint.setY(aTmp.Top());
    else if ( rPoint.getY() > aTmp.Bottom() )
        rPoint.setY(aTmp.Bottom());

}

/** Searches an anchor for paragraph bound objects starting from pOldAnch.
 *
 *  This is used to show anchors as well as changing anchors
 *  when dragging paragraph bound objects.
 */
const SwContentFrame *FindAnchor( const SwFrame *pOldAnch, const Point &rNew,
                              const bool bBodyOnly )
{
    //Search the nearest Cnt around the given document position in the text
    //flow. The given anchor is the starting Frame.
    const SwContentFrame* pCnt;
    if ( pOldAnch->IsContentFrame() )
    {
        pCnt = static_cast<const SwContentFrame*>(pOldAnch);
    }
    else
    {
        Point aTmp( rNew );
        const SwLayoutFrame *pTmpLay = static_cast<const SwLayoutFrame*>(pOldAnch);
        if( pTmpLay->IsRootFrame() )
        {
            SwRect aTmpRect( aTmp, Size(0,0) );
            pTmpLay = static_cast<const SwLayoutFrame*>(::FindPage( aTmpRect, pTmpLay->Lower() ));
        }
        pCnt = pTmpLay->GetContentPos( aTmp, false, bBodyOnly );
    }

    //Take care to use meaningful ranges during search. This means to not enter
    //or leave header/footer in this case.
    const bool bBody = pCnt->IsInDocBody() || bBodyOnly;
    const bool bFootnote  = !bBodyOnly && pCnt->IsInFootnote();

    Point aNew( rNew );
    if ( bBody )
    {
        //#38848 drag from page margin into the body.
        const SwFrame *pPage = pCnt->FindPageFrame();
        ::lcl_PointToPrt( aNew, pPage->GetUpper() );
        SwRect aTmp( aNew, Size( 0, 0 ) );
        pPage = ::FindPage( aTmp, pPage );
        ::lcl_PointToPrt( aNew, pPage );
    }

    if ( pCnt->IsInDocBody() == bBody && pCnt->getFrameArea().Contains( aNew ) )
        return pCnt;
    else if ( pOldAnch->IsInDocBody() || pOldAnch->IsPageFrame() )
    {
        // Maybe the selected anchor is on the same page as the current anchor.
        // With this we won't run into problems with the columns.
        Point aTmp( aNew );
        const SwContentFrame *pTmp = pCnt->FindPageFrame()->
                                        GetContentPos( aTmp, false, true );
        if ( pTmp && pTmp->getFrameArea().Contains( aNew ) )
            return pTmp;
    }

    //Starting from the anchor we now search in both directions until we found
    //the nearest one respectively.
    //Not the direct distance is relevant but the distance which needs to be
    //traveled through the text flow.
    const SwContentFrame *pUpLst;
    const SwContentFrame *pUpFrame = pCnt;
    SwDistance nUp, nUpLst;
    ::lcl_CalcDownDist( nUp, aNew, pUpFrame );
    SwDistance nDown = nUp;
    bool bNegAllowed = true;// Make it possible to leave the negative section once.
    do
    {
        pUpLst = pUpFrame; nUpLst = nUp;
        pUpFrame = pUpLst->GetPrevContentFrame();
        while ( pUpFrame &&
                (bBody != pUpFrame->IsInDocBody() || bFootnote != pUpFrame->IsInFootnote()))
            pUpFrame = pUpFrame->GetPrevContentFrame();
        if ( pUpFrame )
        {
            ::lcl_CalcDownDist( nUp, aNew, pUpFrame );
            //It makes sense to search further, if the distance grows inside
            //a table.
            if ( pUpLst->IsInTab() && pUpFrame->IsInTab() )
            {
                while ( pUpFrame && ((nUpLst < nUp && pUpFrame->IsInTab()) ||
                        bBody != pUpFrame->IsInDocBody()) )
                {
                    pUpFrame = pUpFrame->GetPrevContentFrame();
                    if ( pUpFrame )
                        ::lcl_CalcDownDist( nUp, aNew, pUpFrame );
                }
            }
        }
        if ( !pUpFrame )
            nUp.m_nMain = LONG_MAX;
        if (nUp.m_nMain >= 0 && LONG_MAX != nUp.m_nMain)
        {
            bNegAllowed = false;
            if (nUpLst.m_nMain < 0) //don't take the wrong one, if the value
                //just changed from negative to positive.
            {   pUpLst = pUpFrame;
                nUpLst = nUp;
            }
        }
    } while (pUpFrame && ((bNegAllowed && nUp.m_nMain < 0) || (nUp <= nUpLst)));

    const SwContentFrame *pDownLst;
    const SwContentFrame *pDownFrame = pCnt;
    SwDistance nDownLst;
    if (nDown.m_nMain < 0)
        nDown.m_nMain = LONG_MAX;
    do
    {
        pDownLst = pDownFrame; nDownLst = nDown;
        pDownFrame = pDownLst->GetNextContentFrame();
        while ( pDownFrame &&
                (bBody != pDownFrame->IsInDocBody() || bFootnote != pDownFrame->IsInFootnote()))
            pDownFrame = pDownFrame->GetNextContentFrame();
        if ( pDownFrame )
        {
            ::lcl_CalcDownDist( nDown, aNew, pDownFrame );
            if (nDown.m_nMain < 0)
                nDown.m_nMain = LONG_MAX;
            //It makes sense to search further, if the distance grows inside
            //a table.
            if ( pDownLst->IsInTab() && pDownFrame->IsInTab() )
            {
                while (pDownFrame
                       && ((nDown.m_nMain != LONG_MAX && pDownFrame->IsInTab())
                           || bBody != pDownFrame->IsInDocBody()))
                {
                    pDownFrame = pDownFrame->GetNextContentFrame();
                    if ( pDownFrame )
                        ::lcl_CalcDownDist( nDown, aNew, pDownFrame );
                    if (nDown.m_nMain < 0)
                        nDown.m_nMain = LONG_MAX;
                }
            }
        }
        if ( !pDownFrame )
            nDown.m_nMain = LONG_MAX;

    } while (pDownFrame && nDown <= nDownLst && nDown.m_nMain != LONG_MAX
             && nDownLst.m_nMain != LONG_MAX);

    //If we couldn't find one in both directions, we'll search the Content whose
    //left upper corner is the nearest to the point. Such a situation may
    //happen, if the point doesn't lay in the text flow but in any margin.
    if (nDownLst.m_nMain == LONG_MAX && nUpLst.m_nMain == LONG_MAX)
    {
        // If an OLE objects, which is contained in a fly frame
        // is resized in inplace mode and the new Position is outside the
        // fly frame, we do not want to leave our fly frame.
        if ( pCnt->IsInFly() )
            return pCnt;

        return ::lcl_FindCnt( aNew, pCnt, bBody, bFootnote );
    }
    else
        return nDownLst < nUpLst ? pDownLst : pUpLst;
}

void SwFlyAtContentFrame::SetAbsPos( const Point &rNew )
{
    SwPageFrame *pOldPage = FindPageFrame();
    const SwRect aOld( GetObjRectWithSpaces() );
    Point aNew( rNew );

    if( ( GetAnchorFrame()->IsVertical() && !GetAnchorFrame()->IsVertLR() ) || GetAnchorFrame()->IsRightToLeft() )
        aNew.setX(aNew.getX() + getFrameArea().Width());
    SwContentFrame *pCnt = const_cast<SwContentFrame*>(::FindAnchor( GetAnchorFrame(), aNew ));
    if( pCnt->IsProtected() )
        pCnt = const_cast<SwContentFrame*>(static_cast<const SwContentFrame*>(GetAnchorFrame()));

    SwPageFrame *pTmpPage = nullptr;
    const bool bVert = pCnt->IsVertical();

    const bool bVertL2R = pCnt->IsVertLR();
    const bool bRTL = pCnt->IsRightToLeft();

    if( ( !bVert != !GetAnchorFrame()->IsVertical() ) ||
        ( !bRTL !=  !GetAnchorFrame()->IsRightToLeft() ) )
    {
        if( bVert || bRTL )
            aNew.setX(aNew.getX() + getFrameArea().Width());
        else
            aNew.setX(aNew.getX() - getFrameArea().Width());
    }

    if ( pCnt->IsInDocBody() )
    {
        //#38848 drag from page margin into the body.
        pTmpPage = pCnt->FindPageFrame();
        ::lcl_PointToPrt( aNew, pTmpPage->GetUpper() );
        SwRect aTmp( aNew, Size( 0, 0 ) );
        pTmpPage = const_cast<SwPageFrame*>(static_cast<const SwPageFrame*>(::FindPage( aTmp, pTmpPage )));
        ::lcl_PointToPrt( aNew, pTmpPage );
    }

    //Setup RelPos, only invalidate if requested.
    //rNew is an absolute position. We need to calculate the distance from rNew
    //to the anchor inside the text flow to correctly set RelPos.
//!!!!!We can optimize here: FindAnchor could also return RelPos!
    const SwFrame *pFrame = nullptr;
    SwTwips nY;
    if ( pCnt->getFrameArea().Contains( aNew ) )
    {
        // #i70582#
        if ( bVert )
        {
            nY = pCnt->getFrameArea().Left() - rNew.X();
            if ( bVertL2R )
                nY = -nY;
            else
                nY += pCnt->getFrameArea().Width() - getFrameArea().Width();
            nY -= pCnt->GetUpperSpaceAmountConsideredForPrevFrameAndPageGrid();
        }
        else
            nY = rNew.Y() - pCnt->getFrameArea().Top() - pCnt->GetUpperSpaceAmountConsideredForPrevFrameAndPageGrid();
    }
    else
    {
        SwDistance aDist;
        pFrame = ::lcl_CalcDownDist( aDist, aNew, pCnt );
        nY = aDist.m_nMain + aDist.m_nSub;
    }

    SwTwips nX = 0;

    if ( pCnt->IsFollow() )
    {
        // Flys are never attached to the follow but always to the master,
        // which we're going to search now.
        const SwContentFrame *pOriginal = pCnt;
        const SwContentFrame *pFollow = pCnt;
        while ( pCnt->IsFollow() )
        {
            do
            {
                SwContentFrame* pPrev = pCnt->GetPrevContentFrame();
                if (!pPrev)
                {
                    SAL_WARN("sw.core", "very unexpected missing PrevContentFrame");
                    break;
                }
                pCnt = pPrev;
            }
            while ( pCnt->GetFollow() != pFollow );
            pFollow = pCnt;
        }
        SwTwips nDiff = 0;
        do
        {   const SwFrame *pUp = pFollow->GetUpper();
            if( pUp->IsVertical() )
            {
                if ( pUp->IsVertLR()  )
                    nDiff += pUp->getFramePrintArea().Width() - pFollow->GetRelPos().getX();
                else
                       nDiff += pFollow->getFrameArea().Left() + pFollow->getFrameArea().Width()
                             - pUp->getFrameArea().Left() - pUp->getFramePrintArea().Left();
            }
            else
                nDiff += pUp->getFramePrintArea().Height() - pFollow->GetRelPos().Y();
            pFollow = pFollow->GetFollow();
        } while ( pFollow != pOriginal );
        nY += nDiff;
        if( bVert )
            nX = pCnt->getFrameArea().Top() - pOriginal->getFrameArea().Top();
        else
            nX = pCnt->getFrameArea().Left() - pOriginal->getFrameArea().Left();
    }

    if ( nY == LONG_MAX )
    {
        // #i70582#
        const SwTwips nTopForObjPos = lcl_GetTopForObjPos(pCnt, bVert, bVertL2R);
        if( bVert )
        {
            if ( bVertL2R )
                nY = rNew.X() - nTopForObjPos;
            else
                nY = nTopForObjPos - rNew.X();
        }
        else
        {
            nY = rNew.Y() - nTopForObjPos;
        }
    }

    SwFlyFrameFormat *pFormat = GetFormat();

    if( bVert )
    {
        if( !pFrame )
            nX += rNew.Y() - pCnt->getFrameArea().Top();
        else
            nX = rNew.Y() - pFrame->getFrameArea().Top();
    }
    else
    {
        if( !pFrame )
        {
            if ( pCnt->IsRightToLeft() )
                nX += pCnt->getFrameArea().Right() - rNew.X() - getFrameArea().Width();
            else
                nX += rNew.X() - pCnt->getFrameArea().Left();
        }
        else
        {
            if ( pFrame->IsRightToLeft() )
                nX += pFrame->getFrameArea().Right() - rNew.X() - getFrameArea().Width();
            else
                nX = rNew.X() - pFrame->getFrameArea().Left();
        }
    }
    GetFormat()->GetDoc().GetIDocumentUndoRedo().StartUndo( SwUndoId::START, nullptr );

    if( pCnt != GetAnchorFrame() || ( IsAutoPos() && pCnt->IsTextFrame() &&
                                  GetFormat()->getIDocumentSettingAccess().get(DocumentSettingId::HTML_MODE)) )
    {
        //Set the anchor attribute according to the new Cnt.
        SwFormatAnchor aAnch( pFormat->GetAnchor() );
        SwPosition pos = *aAnch.GetContentAnchor();
        if( IsAutoPos() && pCnt->IsTextFrame() )
        {
            SwTextFrame const*const pTextFrame(static_cast<SwTextFrame const*>(pCnt));
            SwCursorMoveState eTmpState( CursorMoveState::SetOnlyText );
            Point aPt( rNew );
            if( pCnt->GetModelPositionForViewPoint( &pos, aPt, &eTmpState )
                && FrameContainsNode(*pTextFrame, pos.GetNodeIndex()))
            {
                const SwTextAttr *const pTextInputField =
                    pos.GetNode().GetTextNode()->GetTextAttrAt(
                        pos.GetContentIndex(), RES_TXTATR_INPUTFIELD, ::sw::GetTextAttrMode::Parent);
                if (pTextInputField != nullptr)
                {
                    pos.SetContent( pTextInputField->GetStart() );
                }
                ResetLastCharRectHeight();
                if( text::RelOrientation::CHAR == pFormat->GetVertOrient().GetRelationOrient() )
                    nY = LONG_MAX;
                if( text::RelOrientation::CHAR == pFormat->GetHoriOrient().GetRelationOrient() )
                    nX = LONG_MAX;
            }
            else
            {
                pos = pTextFrame->MapViewToModelPos(TextFrameIndex(0));
            }
        }
        else if (pCnt->IsTextFrame())
        {
            pos = static_cast<SwTextFrame const*>(pCnt)->MapViewToModelPos(TextFrameIndex(0));
        }
        else // is that even possible? maybe if there was a change of anchor type from AT_FLY or something?
        {
            assert(pCnt->IsNoTextFrame());
            pos.Assign(*static_cast<SwNoTextFrame*>(pCnt)->GetNode());
        }
        aAnch.SetAnchor( &pos );

        // handle change of anchor node:
        // if count of the anchor frame also change, the fly frames have to be
        // re-created. Thus, delete all fly frames except the <this> before the
        // anchor attribute is change and re-create them afterwards.
        {
            SwHandleAnchorNodeChg aHandleAnchorNodeChg( *pFormat, aAnch, this );
            pFormat->GetDoc().SetAttr( aAnch, *pFormat );
        }
    }
    else if ( pTmpPage && pTmpPage != GetPageFrame() )
        GetPageFrame()->MoveFly( this, pTmpPage );

    const Point aRelPos = bVert ? Point( -nY, nX ) : Point( nX, nY );
    ChgRelPos( aRelPos );
    GetFormat()->GetDoc().GetIDocumentUndoRedo().EndUndo( SwUndoId::END, nullptr );

    if ( pOldPage != FindPageFrame() )
        ::Notify_Background( GetVirtDrawObj(), pOldPage, aOld, PrepareHint::FlyFrameLeave, false );
}

/** method to assure that anchored object is registered at the correct
    page frame

    #i28701#
    takes over functionality of deleted method <SwFlyAtContentFrame::AssertPage()>
*/
void SwFlyAtContentFrame::RegisterAtCorrectPage()
{
    SwPageFrame* pPageFrame( nullptr );
    if ( GetVertPosOrientFrame() )
    {
        pPageFrame = const_cast<SwPageFrame*>(GetVertPosOrientFrame()->FindPageFrame());
    }
    if ( pPageFrame && GetPageFrame() != pPageFrame )
    {
        RegisterAtPage(*pPageFrame);
    }
}

void SwFlyAtContentFrame::RegisterAtPage(SwPageFrame & rPageFrame)
{
    if (GetPageFrame())
    {
        GetPageFrame()->MoveFly( this, &rPageFrame );
    }
    else
    {
        rPageFrame.AppendFlyToPage( this );
    }
}

// #i26791#
void SwFlyAtContentFrame::MakeObjPos()
{
    // if fly frame position is valid, nothing is to do. Thus, return
    if ( isFrameAreaPositionValid() )
    {
        return;
    }

    // #i26791# - validate position flag here.
    setFrameAreaPositionValid(true);

    // #i35911# - no calculation of new position, if
    // anchored object is marked that it clears its environment and its
    // environment is already cleared.
    // before checking for cleared environment
    // check, if member <mpVertPosOrientFrame> is set.
    if ( GetVertPosOrientFrame() &&
         ClearedEnvironment() && HasClearedEnvironment() )
    {
        return;
    }

    // use new class to position object
    objectpositioning::SwToContentAnchoredObjectPosition
            aObjPositioning( *GetVirtDrawObj() );
    aObjPositioning.CalcPosition();

    SetVertPosOrientFrame ( aObjPositioning.GetVertPosOrientFrame() );
}

// #i28701#
bool SwFlyAtContentFrame::InvalidationAllowed( const InvalidationType _nInvalid ) const
{
    bool bAllowed( SwFlyFreeFrame::InvalidationAllowed( _nInvalid ) );

    // forbiddance of base instance can't be over ruled.
    if ( bAllowed )
    {
        if ( _nInvalid == INVALID_POS ||
             _nInvalid == INVALID_ALL )
        {
            bAllowed = InvalidationOfPosAllowed();
        }
    }

    return bAllowed;
}

bool SwFlyAtContentFrame::ShouldBwdMoved(SwLayoutFrame* /*pNewUpper*/, bool& /*rReformat*/)
{
    return false;
}

const SwFlyAtContentFrame* SwFlyAtContentFrame::GetFollow() const
{
    return static_cast<const SwFlyAtContentFrame*>(SwFlowFrame::GetFollow());
}

SwFlyAtContentFrame* SwFlyAtContentFrame::GetFollow()
{
    return static_cast<SwFlyAtContentFrame*>(SwFlowFrame::GetFollow());
}

SwLayoutFrame *SwFrame::GetNextFlyLeaf( MakePageType eMakePage )
{
    auto pFly = dynamic_cast<SwFlyAtContentFrame*>(FindFlyFrame());
    assert(pFly && "GetNextFlyLeaf: missing fly frame");
    assert(pFly->IsFlySplitAllowed() && "GetNextFlyLeaf: fly split not allowed");

    if (pFly->HasFollow())
    {
        // If we already have a follow, then no need to create a new one, just use it.
        return pFly->GetFollow();
    }

    SwTextFrame* pFlyAnchor = pFly->FindAnchorCharFrame();

    if (!pFlyAnchor)
    {
        // In case our fly frame is split already, but not yet moved, then FindAnchorCharFrame()
        // won't find the anchor, since it wants a follow anchor, but there is no follow anchor yet.
        // In this case work with a plain anchor, so FindSctFrame() works to find out we're in a
        // section.
        auto pAnchorFrame = const_cast<SwFrame*>(pFly->GetAnchorFrame());
        if (pAnchorFrame && pAnchorFrame->IsTextFrame())
        {
            pFlyAnchor = static_cast<SwTextFrame*>(pAnchorFrame);
        }
    }

    bool bBody = pFlyAnchor && pFlyAnchor->IsInDocBody();
    SwLayoutFrame *pLayLeaf = nullptr;
    // Look up the first candidate.
    SwSectionFrame* pFlyAnchorSection = pFlyAnchor ? pFlyAnchor->FindSctFrame() : nullptr;
    bool bNesting = false;
    if (pFlyAnchorSection)
    {
        // The anchor is in a section.
        if (pFlyAnchor)
        {
            SwTabFrame* pFlyAnchorTab = pFlyAnchor->FindTabFrame();
            if (pFlyAnchorTab)
            {
                // The anchor is in table as well.
                if (pFlyAnchorTab->FindSctFrame() == pFlyAnchorSection)
                {
                    // We're in a table-in-section, no anchor move in this case, because that would
                    // mean we're not in a table anymore.
                    bNesting = true;
                }
            }
        }
        if (!bNesting)
        {
            // We can't just move the split anchor to the next page, that would be outside the section.
            // Rather split that section as well.
            pLayLeaf = pFlyAnchorSection->GetNextSctLeaf(eMakePage);
        }
    }
    else if (IsTabFrame())
    {
        // If we're in a table, try to find the next frame of the table's last content.
        SwFrame* pContent = static_cast<SwTabFrame*>(this)->FindLastContentOrTable();
        pLayLeaf = pContent ? pContent->GetUpper() : nullptr;
    }
    else
    {
        pLayLeaf = GetNextLayoutLeaf();
    }

    SwLayoutFrame* pOldLayLeaf = nullptr;
    while (true)
    {
        if (pLayLeaf)
        {
            // If we're anchored in a body frame, the candidate has to be in a body frame as well.
            bool bLeftBody = bBody && !pLayLeaf->IsInDocBody();
            // If the candidate is in a fly, make sure that the candidate is a child of our follow.
            bool bLeftFly = pLayLeaf->IsInFly() && pLayLeaf->FindFlyFrame() != pFly->GetFollow();
            bool bSameBody = false;
            if (bBody && pLayLeaf->IsInDocBody())
            {
                // Make sure the candidate is not inside the same body frame, that would prevent
                // inserting a new page.
                SwBodyFrame const* pAnchorBody(pFlyAnchor->FindBodyFrame());
                while (!pAnchorBody->IsPageBodyFrame())
                {
                    pAnchorBody = pAnchorBody->GetUpper()->FindBodyFrame();
                };
                SwBodyFrame const* pLeafBody(pLayLeaf->FindBodyFrame());
                while (!pLeafBody->IsPageBodyFrame())
                {
                    pLeafBody = pLeafBody->GetUpper()->FindBodyFrame();
                };
                if (pAnchorBody == pLeafBody)
                {
                    bSameBody = true;
                }
            }

            if (bLeftFly && pFlyAnchor && pFlyAnchor->IsInTab()
                && FindTabFrame() == pLayLeaf->FindTabFrame())
            {
                // This is an inner fly (parent is an inline or a floating table), then the follow
                // anchor will be just next to us.
                SwLayoutFrame* pFlyAnchorUpper = pFlyAnchor->GetUpper();
                pOldLayLeaf = pLayLeaf;
                pLayLeaf = pFlyAnchorUpper;
                bLeftFly = false;
                bNesting = true;
            }

            if (bLeftBody || bLeftFly || bSameBody)
            {
                // The above conditions are not held, reject.
                pOldLayLeaf = pLayLeaf;
                do
                {
                    pLayLeaf = pLayLeaf->GetNextLayoutLeaf();
                }
                // skip deleted section frames - do not move into these
                while (pLayLeaf && pLayLeaf->FindSctFrame()
                    && !pLayLeaf->FindSctFrame()->GetSection());

                if (pLayLeaf && pLayLeaf->IsInDocBody() && !bSameBody && !pLayLeaf->IsInFly() && pLayLeaf->IsInTab())
                {
                    // We found a next leaf in a next body frame, which is in an inline table. Make
                    // sure we won't insert the follow anchor inside the table, but before it.
                    SwTabFrame* pTabFrame = pLayLeaf->FindTabFrame();
                    pLayLeaf = pTabFrame->GetUpper();
                }

                continue;
            }
        }
        else
        {
            // No candidate: insert a page and try again.
            if (eMakePage == MAKEPAGE_INSERT)
            {
                InsertPage(FindPageFrame(), false);
                // If we already had a candidate, continue trying with that instead of starting from
                // scratch.
                pLayLeaf = pOldLayLeaf ? pOldLayLeaf : GetNextLayoutLeaf();
                continue;
            }
        }
        break;
    }

    if( pLayLeaf )
    {
        SwFlyAtContentFrame* pNew = nullptr;
        // Find the anchor frame to split.
        if (pFlyAnchor)
        {
            // Split the anchor at char 0: all the content goes to the follow of the anchor.
            pFlyAnchor->SplitFrame(TextFrameIndex(0));
            auto pNext = static_cast<SwTextFrame*>(pFlyAnchor->GetNext());
            // The nesting case just splits the inner fly; the outer fly will split and move.
            if (!bNesting)
            {
                // Move the new anchor frame, before the first child of pLayLeaf.
                pNext->MoveSubTree(pLayLeaf, pLayLeaf->Lower());
            }

            // Now create the follow of the fly and anchor it in the master of the anchor.
            pNew = new SwFlyAtContentFrame(*pFly);
            while (pFlyAnchor->IsFollow())
            {
                pFlyAnchor = pFlyAnchor->FindMaster();
            }
            pFlyAnchor->AppendFly(pNew);
        }
        pLayLeaf = pNew;
    }
    return pLayLeaf;
}

void SwRootFrame::DeleteEmptyFlys_()
{
    assert(mpFlyDestroy);

    while (!mpFlyDestroy->empty())
    {
        SwFlyFrame* pFly = *mpFlyDestroy->begin();
        mpFlyDestroy->erase( mpFlyDestroy->begin() );
        // Allow deletion of non-empty flys: a fly with no content is still formatted to have a
        // height of MINLAY.
        if (!pFly->ContainsContent() && !pFly->IsDeleteForbidden())
        {
            SwFrame::DestroyFrame(pFly);
        }
    }
}

bool SwRootFrame::IsInFlyDelList( SwFlyFrame* pFly ) const
{
    if (!mpFlyDestroy)
    {
        return false;
    }

    return mpFlyDestroy->find(pFly) != mpFlyDestroy->end();
}

const SwFlyAtContentFrame* SwFlyAtContentFrame::GetPrecede() const
{
    return static_cast<const SwFlyAtContentFrame*>(SwFlowFrame::GetPrecede());
}

SwFlyAtContentFrame* SwFlyAtContentFrame::GetPrecede()
{
    return static_cast<SwFlyAtContentFrame*>(SwFlowFrame::GetPrecede());
}

void SwFlyAtContentFrame::DelEmpty()
{
    SwTextFrame* pAnchor = FindAnchorCharFrame();
    if (pAnchor)
    {
        if (SwFlowFrame* pAnchorPrecede = pAnchor->GetPrecede())
        {
            // The anchor has a precede: invalidate it so that JoinFrame() is called on it.
            pAnchorPrecede->GetFrame().InvalidateSize();
        }
    }

    SwFlyAtContentFrame* pMaster = IsFollow() ? GetPrecede() : nullptr;
    if (pMaster)
    {
        pMaster->SetFollow(GetFollow());
    }

    SwFlyAtContentFrame* pFollow = GetFollow();
    if (pFollow)
    {
        // I'll be deleted, so invalidate the position of my follow, so it can move up.
        pFollow->InvalidatePos();
    }

    SetFollow(nullptr);

    {
        SwFrameAreaDefinition::FrameAreaWriteAccess aFrm(*this);
        aFrm.Height(0);
    }
    InvalidateObjRectWithSpaces();

    if(getRootFrame())
    {
        getRootFrame()->InsertEmptyFly(this);
    }
}

bool SwFlyAtContentFrame::IsWrapOnAllPages() const
{
    const SwFormatWrapTextAtFlyStart& rWrapTextAtFlyStart = GetFormat()->GetWrapTextAtFlyStart();
    if (rWrapTextAtFlyStart.GetValue())
    {
        return true;
    }

    const SwRootFrame* pRootFrame = getRootFrame();
    if (!pRootFrame)
    {
        return false;
    }

    const SwFrameFormat* pFormat = pRootFrame->GetFormat();
    if (!pFormat)
    {
        return false;
    }

    const IDocumentSettingAccess& rIDSA = pFormat->getIDocumentSettingAccess();
    return rIDSA.get(DocumentSettingId::ALLOW_TEXT_AFTER_FLOATING_TABLE_BREAK);
}

void SwFlyAtContentFrame::dumpAsXmlAttributes(xmlTextWriterPtr pWriter) const
{
    SwFlyFreeFrame::dumpAsXmlAttributes(pWriter);

    if (m_pFollow != nullptr)
    {
        (void)xmlTextWriterWriteAttribute(
            pWriter, BAD_CAST("follow"),
            BAD_CAST(OString::number(m_pFollow->GetFrame().GetFrameId()).getStr()));
    }
    if (m_pPrecede != nullptr)
    {
        (void)xmlTextWriterWriteAttribute(
            pWriter, BAD_CAST("precede"),
            BAD_CAST(OString::number(m_pPrecede->GetFrame().GetFrameId()).getStr()));
    }
}

void SwRootFrame::InsertEmptyFly(SwFlyFrame* pDel)
{
    if (!mpFlyDestroy)
    {
        mpFlyDestroy.reset(new SwFlyDestroyList);
    }

    mpFlyDestroy->insert(pDel);
}

SwLayoutFrame* SwFrame::GetPrevFlyLeaf()
{
    auto pFly = dynamic_cast<SwFlyAtContentFrame*>(FindFlyFrame());
    assert(pFly && "GetPrevFlyLeaf: missing fly frame");
    if (!pFly->IsFlySplitAllowed())
    {
        return nullptr;
    }

    return pFly->GetPrecede();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
