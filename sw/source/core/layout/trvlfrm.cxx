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

#include <hints.hxx>
#include <comphelper/flagguard.hxx>
#include <tools/line.hxx>
#include <editeng/opaqitem.hxx>
#include <editeng/protitem.hxx>
#include <vcl/settings.hxx>
#include <fmtpdsc.hxx>
#include <fmtsrnd.hxx>
#include <pagedesc.hxx>
#include <pagefrm.hxx>
#include <rootfrm.hxx>
#include <ftnfrm.hxx>
#include <flyfrm.hxx>
#include <tabfrm.hxx>
#include <rowfrm.hxx>
#include <cellfrm.hxx>
#include <txtfrm.hxx>
#include <notxtfrm.hxx>
#include <viewopt.hxx>
#include <DocumentSettingManager.hxx>
#include <viscrs.hxx>
#include <dflyobj.hxx>
#include <crstate.hxx>
#include <dcontact.hxx>
#include <sortedobjs.hxx>
#include <txatbase.hxx>
#include <fmtfld.hxx>
#include <fldbas.hxx>
#include <frmatr.hxx>
#include <frmtool.hxx>
#include "../text/inftxt.hxx"
#include "../text/itrpaint.hxx"
#include <ndtxt.hxx>
#include <undobj.hxx>
#include <flyfrms.hxx>
#include <sectfrm.hxx>

#include <swselectionlist.hxx>
#include <comphelper/lok.hxx>
#include <osl/diagnose.h>

namespace {
    bool lcl_GetModelPositionForViewPoint_Objects( const SwPageFrame* pPageFrame, bool bSearchBackground,
           SwPosition *pPos, Point const & rPoint, SwCursorMoveState* pCMS  )
    {
        bool bRet = false;
        Point aPoint( rPoint );
        SwOrderIter aIter( pPageFrame );
        aIter.Top();
        while ( aIter() )
        {
            const SwVirtFlyDrawObj* pObj =
                                static_cast<const SwVirtFlyDrawObj*>(aIter());
            if (const SwContact* pContact = ::GetUserCall( aIter() ))
            {
                const SwAnchoredObject* pAnchoredObj = pContact->GetAnchoredObj( aIter() );
                const SwFrameFormat* pObjFormat = pAnchoredObj->GetFrameFormat();
                const SwFormatSurround& rSurround = pObjFormat->GetSurround();
                const SvxOpaqueItem& rOpaque = pObjFormat->GetOpaque();
                bool bInBackground = ( rSurround.GetSurround() == css::text::WrapTextMode_THROUGH ) && !rOpaque.GetValue();

                bool bBackgroundMatches = bInBackground == bSearchBackground;

                const SwFlyFrame* pFly = pObj ? pObj->GetFlyFrame() : nullptr;
                if ( pFly && bBackgroundMatches &&
                     ( ( pCMS && pCMS->m_bSetInReadOnly ) ||
                       !pFly->IsProtected() ) &&
                     pFly->GetModelPositionForViewPoint( pPos, aPoint, pCMS ) )
                {
                    bRet = true;
                    break;
                }

                if ( pCMS && pCMS->m_bStop )
                    return false;
            }
            aIter.Prev();
        }
        return bRet;
    }

    double lcl_getDistance( const SwRect& rRect, const Point& rPoint )
    {
        double nDist = 0.0;

        // If the point is inside the rectangle, then distance is 0
        // Otherwise, compute the distance to the center of the rectangle.
        if ( !rRect.Contains( rPoint ) )
        {
            tools::Line aLine( rPoint, rRect.Center( ) );
            nDist = aLine.GetLength( );
        }

        return nDist;
    }
}

namespace {

//For SwFlyFrame::GetModelPositionForViewPoint
class SwCursorOszControl
{
public:
    // So the compiler can initialize the class already. No DTOR and member
    // as public members
    const SwFlyFrame* m_pEntry;
    const SwFlyFrame* m_pStack1;
    const SwFlyFrame* m_pStack2;

    bool ChkOsz( const SwFlyFrame *pFly )
        {
            bool bRet = true;
            if (pFly != m_pStack1 && pFly != m_pStack2)
            {
                m_pStack1 = m_pStack2;
                m_pStack2 = pFly;
                bRet  = false;
            }
            return bRet;
        }

    void Entry( const SwFlyFrame *pFly )
        {
            if (!m_pEntry)
                m_pEntry = m_pStack1 = pFly;
        }

    void Exit( const SwFlyFrame *pFly )
        {
            if (pFly == m_pEntry)
                m_pEntry = m_pStack1 = m_pStack2 = nullptr;
        }
};

}

static SwCursorOszControl g_OszCtrl = { nullptr, nullptr, nullptr };

/** Searches the ContentFrame owning the PrtArea containing the point. */
bool SwLayoutFrame::GetModelPositionForViewPoint( SwPosition *pPos, Point &rPoint,
                               SwCursorMoveState* pCMS, bool ) const
{
    vcl::RenderContext* pRenderContext = getRootFrame()->GetCurrShell()->GetOut();
    bool bRet = false;
    const SwFrame *pFrame = Lower();
    while ( !bRet && pFrame )
    {
        pFrame->Calc(pRenderContext);

        // #i43742# New function
        const bool bContentCheck = pFrame->IsTextFrame() && pCMS && pCMS->m_bContentCheck;
        const SwRect aPaintRect( bContentCheck ?
                                 pFrame->UnionFrame() :
                                 pFrame->GetPaintArea() );

        auto pTextFrame = pFrame->DynCastTextFrame();
        bool bSplitFly = false;
        if (pTextFrame && pTextFrame->HasNonLastSplitFlyDrawObj())
        {
            // Don't consider a non-last anchor of the split fly, so the view point can be corrected
            // to go to the nearest fly, instead of the last anchor on a later page.
            bSplitFly = true;
        }

        if ( aPaintRect.Contains( rPoint ) &&
             ( bContentCheck || pFrame->GetModelPositionForViewPoint( pPos, rPoint, pCMS ) ) && !bSplitFly )
            bRet = true;
        else
            pFrame = pFrame->GetNext();
        if ( pCMS && pCMS->m_bStop )
            return false;
    }
    return bRet;
}

/** Searches for the page containing the searched point. */
bool SwPageFrame::GetModelPositionForViewPoint( SwPosition *pPos, Point &rPoint,
                             SwCursorMoveState* pCMS, bool bTestBackground ) const
{
    Point aPoint(std::clamp(rPoint.X(), getFrameArea().Left(), getFrameArea().Right()),
                 std::clamp(rPoint.Y(), getFrameArea().Top(), getFrameArea().Bottom()));

    bool bRet = false;
    //Could it be a free flying one?
    //If his content should be protected, we can't set the Cursor in it, thus
    //all changes should be impossible.
    if ( GetSortedObjs() )
    {
        bRet = lcl_GetModelPositionForViewPoint_Objects( this, false, pPos, rPoint, pCMS );
    }

    if ( !bRet )
    {
        SwPosition aBackPos( *pPos );
        SwPosition aTextPos( *pPos );

        //We fix the StartPoint if no Content below the page 'answers' and then
        //start all over again one page before the current one.
        //However we can't use Flys in such a case.
        if (!SwLayoutFrame::GetModelPositionForViewPoint(&aTextPos, aPoint, pCMS))
        {
            if ( pCMS && (pCMS->m_bStop || pCMS->m_bExactOnly) )
            {
                pCMS->m_bStop = true;
                return false;
            }

            const SwContentFrame *pCnt = GetContentPos( aPoint, false, false, pCMS, false );

            auto pTextFrame = pCnt ? pCnt->DynCastTextFrame() : nullptr;
            if (pTextFrame)
            {
                SwFlyAtContentFrame* pFly = pTextFrame->HasNonLastSplitFlyDrawObj();
                if (pFly)
                {
                    // No exact match, looking for a nearest doc model position. Consider our fly
                    // frame.
                    pCnt = pFly->GetContentPos( aPoint, false, false, pCMS, false );
                }
            }

            SAL_WARN_IF(!pCnt, "sw.layout", "Cursor is gone to a Black hole");
            if (!pCnt)
                return false;

            // GetContentPos may have modified pCMS
            if ( pCMS && pCMS->m_bStop )
                return false;

            bool bTextRet = false;

            if( pCMS && pCMS->m_pFill && pCnt->IsTextFrame() )
                bTextRet = pCnt->GetModelPositionForViewPoint( &aTextPos, rPoint, pCMS );
            else
                bTextRet = pCnt->GetModelPositionForViewPoint( &aTextPos, aPoint, pCMS );

            if ( !bTextRet )
            {
                // Set point to pCnt, delete mark
                // this may happen, if pCnt is hidden
                if (pCnt->IsTextFrame())
                {
                    aTextPos = static_cast<SwTextFrame const*>(pCnt)->MapViewToModelPos(TextFrameIndex(0));
                }
                else
                {
                    assert(pCnt->IsNoTextFrame());
                    aTextPos.Assign( *static_cast<SwNoTextFrame const*>(pCnt)->GetNode() );
                }
            }
        }

        SwContentNode* pContentNode = aTextPos.GetNode().GetContentNode();
        bool bConsiderBackground = true;
        // If the text position is a clickable field, then that should have priority.
        if (pContentNode && pContentNode->IsTextNode())
        {
            SwTextNode* pTextNd = pContentNode->GetTextNode();
            SwTextAttr* pTextAttr = pTextNd->GetTextAttrForCharAt(aTextPos.GetContentIndex(), RES_TXTATR_FIELD);
            if (pTextAttr)
            {
                const SwField* pField = pTextAttr->GetFormatField().GetField();
                if (pField->IsClickable())
                    bConsiderBackground = false;
            }
        }

        bool bBackRet = false;
        // Check objects in the background if nothing else matched
        if ( GetSortedObjs() )
        {
            bBackRet = lcl_GetModelPositionForViewPoint_Objects( this, true, &aBackPos, rPoint, pCMS );
        }

        if (bConsiderBackground && bTestBackground && bBackRet)
        {
            (*pPos) = std::move(aBackPos);
        }
        else if (!bBackRet)
        {
            (*pPos) = std::move(aTextPos);
        }
        else // bBackRet && !(bConsiderBackground && bTestBackground)
        {
            /* In order to provide a selection as accurate as possible when we have both
             * text and background object, then we compute the distance between both
             * would-be positions and the click point. The shortest distance wins.
             */
            double nTextDistance = 0;
            bool bValidTextDistance = false;
            if (pContentNode)
            {
                SwContentFrame* pTextFrame = pContentNode->getLayoutFrame( getRootFrame( ) );

                // try this again but prefer the "previous" position
                SwCursorMoveState aMoveState;
                SwCursorMoveState *const pState(pCMS ? pCMS : &aMoveState);
                comphelper::FlagRestorationGuard g(
                        pState->m_bPosMatchesBounds, true);
                SwPosition prevTextPos(*pPos);
                if (SwLayoutFrame::GetModelPositionForViewPoint(&prevTextPos, aPoint, pState))
                {
                    SwRect aTextRect;
                    pTextFrame->GetCharRect(aTextRect, prevTextPos);

                    if (prevTextPos.GetContentIndex() < pContentNode->Len())
                    {
                        // aRextRect is just a line on the left edge of the
                        // previous character; to get a better measure from
                        // lcl_getDistance, extend that to a rectangle over
                        // the entire character.
                        SwPosition nextTextPos(std::move(prevTextPos));
                        nextTextPos.AdjustContent(+1);
                        SwRect nextTextRect;
                        pTextFrame->GetCharRect(nextTextRect, nextTextPos);
                        SwRectFnSet aRectFnSet(pTextFrame);
                        if (aRectFnSet.GetTop(aTextRect) ==
                            aRectFnSet.GetTop(nextTextRect)) // same line?
                        {
                            // need to handle mixed RTL/LTR portions somehow
                            if (aRectFnSet.GetLeft(aTextRect) <
                                aRectFnSet.GetLeft(nextTextRect))
                            {
                                aRectFnSet.SetRight( aTextRect,
                                    aRectFnSet.GetLeft(nextTextRect));
                            }
                            else // RTL
                            {
                                aRectFnSet.SetLeft( aTextRect,
                                    aRectFnSet.GetLeft(nextTextRect));
                            }
                        }
                    }

                    nTextDistance = lcl_getDistance(aTextRect, rPoint);
                    bValidTextDistance = true;
                }
            }

            double nBackDistance = 0;
            bool bValidBackDistance = false;
            SwContentNode* pBackNd = aBackPos.GetNode( ).GetContentNode( );
            if ( pBackNd && bConsiderBackground)
            {
                // FIXME There are still cases were we don't have the proper node here.
                SwContentFrame* pBackFrame = pBackNd->getLayoutFrame( getRootFrame( ) );
                if (pBackFrame)
                {
                    SwRect rBackRect;
                    pBackFrame->GetCharRect( rBackRect, aBackPos );

                    nBackDistance = lcl_getDistance( rBackRect, rPoint );
                    bValidBackDistance = true;
                }
            }

            if ( bValidTextDistance && bValidBackDistance && basegfx::fTools::more( nTextDistance, nBackDistance ) )
            {
                (*pPos) = std::move(aBackPos);
            }
            else
            {
                (*pPos) = std::move(aTextPos);
            }
        }
    }

    rPoint = aPoint;
    return true;
}

bool SwLayoutFrame::FillSelection( SwSelectionList& rList, const SwRect& rRect ) const
{
    if( rRect.Overlaps(GetPaintArea()) )
    {
        const SwFrame* pFrame = Lower();
        while( pFrame )
        {
            pFrame->FillSelection( rList, rRect );
            pFrame = pFrame->GetNext();
        }
    }
    return false;
}

bool SwPageFrame::FillSelection( SwSelectionList& rList, const SwRect& rRect ) const
{
    bool bRet = false;
    if( rRect.Overlaps(GetPaintArea()) )
    {
        bRet = SwLayoutFrame::FillSelection( rList, rRect );
        if( GetSortedObjs() )
        {
            const SwSortedObjs &rObjs = *GetSortedObjs();
            for (SwAnchoredObject* pAnchoredObj : rObjs)
            {
                const SwFlyFrame* pFly = pAnchoredObj->DynCastFlyFrame();
                if( !pFly )
                    continue;
                if( pFly->FillSelection( rList, rRect ) )
                    bRet = true;
            }
        }
    }
    return bRet;
}

bool SwRootFrame::FillSelection( SwSelectionList& aSelList, const SwRect& rRect) const
{
    const SwFrame *pPage = Lower();
    const tools::Long nBottom = rRect.Bottom();
    while( pPage )
    {
        if( pPage->getFrameArea().Top() < nBottom )
        {
            if( pPage->getFrameArea().Bottom() > rRect.Top() )
                pPage->FillSelection( aSelList, rRect );
            pPage = pPage->GetNext();
        }
        else
            pPage = nullptr;
    }
    return !aSelList.isEmpty();
}

/** Primary passes the call to the first page.
 *
 *  @return false, if the passed Point gets changed
 */
bool SwRootFrame::GetModelPositionForViewPoint( SwPosition *pPos, Point &rPoint,
                             SwCursorMoveState* pCMS, bool bTestBackground ) const
{
    const bool bOldAction = IsCallbackActionEnabled();
    const_cast<SwRootFrame*>(this)->SetCallbackActionEnabled( false );
    const SwFrame* pLower = Lower();
    OSL_ENSURE( (pLower && pLower->IsPageFrame()), "No PageFrame found." );
    if( pCMS && pCMS->m_pFill )
        pCMS->m_bFillRet = false;
    Point aOldPoint = rPoint;

    // search for page containing rPoint. The borders around the pages are considered
    const SwPageFrame* pPage = GetPageAtPos( rPoint, nullptr, true );

    // #i95626#
    // special handling for <rPoint> beyond root frames area
    if ( !pPage &&
         rPoint.X() > getFrameArea().Right() &&
         rPoint.Y() > getFrameArea().Bottom() )
    {
        pPage = dynamic_cast<const SwPageFrame*>(pLower);
        while ( pPage && pPage->GetNext() )
        {
            pPage = dynamic_cast<const SwPageFrame*>(pPage->GetNext());
        }
    }
    if ( pPage )
    {
        pPage->SwPageFrame::GetModelPositionForViewPoint( pPos, rPoint, pCMS, bTestBackground );
    }

    const_cast<SwRootFrame*>(this)->SetCallbackActionEnabled( bOldAction );
    if( pCMS )
    {
        if( pCMS->m_bStop )
            return false;
        if( pCMS->m_pFill )
            return pCMS->m_bFillRet;
    }
    return aOldPoint == rPoint;
}

/**
 * If this is about a Content-carrying cell the Cursor will be force inserted into one of the ContentFrames
 * if there are no other options.
 *
 * There is no entry for protected cells.
 */
bool SwCellFrame::GetModelPositionForViewPoint( SwPosition *pPos, Point &rPoint,
                             SwCursorMoveState* pCMS, bool ) const
{
    vcl::RenderContext* pRenderContext = getRootFrame()->GetCurrShell()->GetOut();
    // cell frame does not necessarily have a lower (split table cell)
    if ( !Lower() )
        return false;

    if ( !(pCMS && pCMS->m_bSetInReadOnly) &&
         GetFormat()->GetProtect().IsContentProtected() )
        return false;

    if ( pCMS && pCMS->m_eState == CursorMoveState::TableSel )
    {
        const SwTabFrame *pTab = FindTabFrame();
        if ( pTab->IsFollow() && pTab->IsInHeadline( *this ) )
        {
            pCMS->m_bStop = true;
            return false;
        }
    }

    const SwFrame *pFrame = Lower();
    if (pFrame && pFrame->IsLayoutFrame())
        return SwLayoutFrame::GetModelPositionForViewPoint(pPos, rPoint, pCMS);

    Calc(pRenderContext);
    bool bRet = false;

    while (pFrame && !bRet)
    {
        pFrame->Calc(pRenderContext);
        if (pFrame->getFrameArea().Contains(rPoint))
        {
            bRet = pFrame->GetModelPositionForViewPoint(pPos, rPoint, pCMS);
            if (pCMS && pCMS->m_bStop)
                return false;
        }
        pFrame = pFrame->GetNext();
    }
    if (!bRet)
    {
        const bool bFill = pCMS && pCMS->m_pFill;
        Point aPoint(rPoint);
        const SwContentFrame *pCnt = GetContentPos(rPoint, true);
        if (bFill && pCnt->IsTextFrame())
        {
            rPoint = aPoint;
        }
        pCnt->GetModelPositionForViewPoint(pPos, rPoint, pCMS);
    }
    return true;
}

//Problem: If two Flys have the same size and share the same position then
//they end inside each other.
//Because we recursively check if a Point doesn't randomly lie inside another
//fly which lies completely inside the current Fly we could trigger an endless
//loop with the mentioned situation above.
//Using the helper class SwCursorOszControl we prevent the recursion. During
//a recursion GetModelPositionForViewPoint picks the one which lies on top.
bool SwFlyFrame::GetModelPositionForViewPoint( SwPosition *pPos, Point &rPoint,
                            SwCursorMoveState* pCMS, bool ) const
{
    vcl::RenderContext* pRenderContext = getRootFrame()->GetCurrShell()->GetOut();
    g_OszCtrl.Entry( this );

    //If the Points lies inside the Fly, we try hard to set the Cursor inside it.
    //However if the Point sits inside a Fly which is completely located inside
    //the current one, we call GetModelPositionForViewPoint for it.
    Calc(pRenderContext);
    bool bInside = getFrameArea().Contains( rPoint ) && Lower();
    bool bRet = false;

    //If a Frame contains a graphic, but only text was requested, it basically
    //won't accept the Cursor.
    if ( bInside && pCMS && pCMS->m_eState == CursorMoveState::SetOnlyText)
    {
        const SwFrame* pLower = Lower();
        if (!pLower || pLower->IsNoTextFrame())
            bInside = false;
    }

    const SwPageFrame *pPage = FindPageFrame();
    if ( bInside && pPage && pPage->GetSortedObjs() )
    {
        SwOrderIter aIter( pPage );
        aIter.Top();
        while ( aIter() && !bRet )
        {
            const SwVirtFlyDrawObj* pObj = static_cast<const SwVirtFlyDrawObj*>(aIter());
            const SwFlyFrame* pFly = pObj ? pObj->GetFlyFrame() : nullptr;
            if ( pFly && pFly->getFrameArea().Contains( rPoint ) &&
                 getFrameArea().Contains( pFly->getFrameArea() ) )
            {
                if (g_OszCtrl.ChkOsz(pFly))
                    break;
                bRet = pFly->GetModelPositionForViewPoint( pPos, rPoint, pCMS );
                if ( bRet )
                    break;
                if ( pCMS && pCMS->m_bStop )
                    return false;
            }
            aIter.Next();
        }
    }

    while ( bInside && !bRet )
    {
        const SwFrame *pFrame = Lower();
        while ( pFrame && !bRet )
        {
            pFrame->Calc(pRenderContext);
            if ( pFrame->getFrameArea().Contains( rPoint ) )
            {
                bRet = pFrame->GetModelPositionForViewPoint( pPos, rPoint, pCMS );
                if ( pCMS && pCMS->m_bStop )
                    return false;
            }
            pFrame = pFrame->GetNext();
        }
        if ( !bRet )
        {
            const bool bFill = pCMS && pCMS->m_pFill;
            Point aPoint( rPoint );
            const SwContentFrame *pCnt = GetContentPos( rPoint, true, false, pCMS );
            if ( pCMS && pCMS->m_bStop )
                return false;
            if( bFill && pCnt->IsTextFrame() )
            {
                rPoint = aPoint;
            }
            pCnt->GetModelPositionForViewPoint( pPos, rPoint, pCMS );
            bRet = true;
        }
    }
    g_OszCtrl.Exit( this );
    return bRet;
}

/** Layout dependent cursor travelling */
bool SwNoTextFrame::LeftMargin(SwPaM *pPam) const
{
    if( &pPam->GetPointNode() != GetNode() )
        return false;
    pPam->GetPoint()->AssignStartIndex(*GetNode());
    return true;
}

bool SwNoTextFrame::RightMargin(SwPaM *pPam, bool) const
{
    if( &pPam->GetPointNode() != GetNode() )
        return false;
    pPam->GetPoint()->AssignEndIndex(*GetNode());
    return true;
}

static const SwContentFrame *lcl_GetNxtCnt( const SwContentFrame* pCnt )
{
    return pCnt->GetNextContentFrame();
}

static const SwContentFrame *lcl_GetPrvCnt( const SwContentFrame* pCnt )
{
    return pCnt->GetPrevContentFrame();
}

typedef const SwContentFrame *(*GetNxtPrvCnt)( const SwContentFrame* );

/// Frame in repeated headline?
static bool lcl_IsInRepeatedHeadline( const SwFrame *pFrame,
                                    const SwTabFrame** ppTFrame = nullptr )
{
    const SwTabFrame *pTab = pFrame->FindTabFrame();
    if( ppTFrame )
        *ppTFrame = pTab;
    return pTab && pTab->IsFollow() && pTab->IsInHeadline( *pFrame );
}

/// Skip protected table cells. Optionally also skip repeated headlines.
//MA 1998-01-26: Chg also skip other protected areas
//FME: Skip follow flow cells
static const SwContentFrame * lcl_MissProtectedFrames( const SwContentFrame *pCnt,
                                                   GetNxtPrvCnt fnNxtPrv,
                                                   bool bMissHeadline,
                                                   bool bInReadOnly,
                                                   bool bMissFollowFlowLine )
{
    if ( pCnt && pCnt->IsInTab() )
    {
        bool bProtect = true;
        while ( pCnt && bProtect )
        {
            const SwLayoutFrame *pCell = pCnt->GetUpper();
            while ( pCell && !pCell->IsCellFrame() )
                pCell = pCell->GetUpper();
            if ( !pCell ||
                    ( ( bInReadOnly || !pCell->GetFormat()->GetProtect().IsContentProtected() ) &&
                      ( !bMissHeadline || !lcl_IsInRepeatedHeadline( pCell ) ) &&
                      ( !bMissFollowFlowLine || !pCell->IsInFollowFlowRow() ) &&
                        !pCell->IsCoveredCell() ) )
                bProtect = false;
            else
                pCnt = (*fnNxtPrv)( pCnt );
        }
    }
    else if ( !bInReadOnly )
        while ( pCnt && pCnt->IsProtected() )
            pCnt = (*fnNxtPrv)( pCnt );

    return pCnt;
}

static bool lcl_UpDown( SwPaM *pPam, const SwContentFrame *pStart,
                    GetNxtPrvCnt fnNxtPrv, bool bInReadOnly )
{
    OSL_ENSURE( FrameContainsNode(*pStart, pPam->GetPointNode().GetIndex()),
            "lcl_UpDown doesn't work for others." );

    const SwContentFrame *pCnt = nullptr;

    //We have to cheat a little bit during a table selection: Go to the
    //beginning of the cell while going up and go to the end of the cell while
    //going down.
    bool bTableSel = false;
    if ( pStart->IsInTab() &&
        pPam->GetPointNode().StartOfSectionNode() !=
        pPam->GetMarkNode().StartOfSectionNode() )
    {
        bTableSel = true;
        const SwLayoutFrame  *pCell = pStart->GetUpper();
        while ( !pCell->IsCellFrame() )
            pCell = pCell->GetUpper();

        // Check, if cell has a Prev/Follow cell:
        const bool bFwd = ( fnNxtPrv == lcl_GetNxtCnt );
        const SwLayoutFrame* pTmpCell = bFwd ?
            static_cast<const SwCellFrame*>(pCell)->GetFollowCell() :
            static_cast<const SwCellFrame*>(pCell)->GetPreviousCell();

        const SwContentFrame* pTmpStart = pStart;
        while ( pTmpCell && nullptr != ( pTmpStart = pTmpCell->ContainsContent() ) )
        {
            pCell = pTmpCell;
            pTmpCell = bFwd ?
                static_cast<const SwCellFrame*>(pCell)->GetFollowCell() :
                static_cast<const SwCellFrame*>(pCell)->GetPreviousCell();
        }
        const SwContentFrame *pNxt = pCnt = pTmpStart;

        while ( pCell->IsAnLower( pNxt ) )
        {
            pCnt = pNxt;
            pNxt = (*fnNxtPrv)( pNxt );
        }
    }

    pCnt = (*fnNxtPrv)( pCnt ? pCnt : pStart );
    pCnt = ::lcl_MissProtectedFrames( pCnt, fnNxtPrv, true, bInReadOnly, bTableSel );

    const SwTabFrame *pStTab = pStart->FindTabFrame();
    const SwTabFrame *pTable = nullptr;
    const bool bTab = pStTab || (pCnt && pCnt->IsInTab());
    bool bEnd = !bTab;

    const SwFrame* pVertRefFrame = pStart;
    if ( bTableSel && pStTab )
        pVertRefFrame = pStTab;
    SwRectFnSet aRectFnSet(pVertRefFrame);

    SwTwips nX = 0;
    if ( bTab )
    {
        // pStart or pCnt is inside a table. nX will be used for travelling:
        SwRect aRect( pStart->getFrameArea() );
        pStart->GetCharRect( aRect, *pPam->GetPoint() );
        Point aCenter = aRect.Center();
        nX = aRectFnSet.IsVert() ? aCenter.Y() : aCenter.X();

        pTable = pCnt ? pCnt->FindTabFrame() : nullptr;
        if ( !pTable )
            pTable = pStTab;

        if ( pStTab &&
            !pStTab->GetUpper()->IsInTab() &&
            !pTable->GetUpper()->IsInTab() )
        {
            const SwFrame *pCell = pStart->GetUpper();
            while ( pCell && !pCell->IsCellFrame() )
                pCell = pCell->GetUpper();
            OSL_ENSURE( pCell, "could not find the cell" );
            nX = aRectFnSet.XInc(aRectFnSet.GetLeft(pCell->getFrameArea()),
                                 aRectFnSet.GetWidth(pCell->getFrameArea()) / 2);

            //The flow leads from one table to the next. The X-value needs to be
            //corrected based on the middle of the starting cell by the amount
            //of the offset of the tables.
            if ( pStTab != pTable )
            {
                nX += aRectFnSet.GetLeft(pTable->getFrameArea()) -
                      aRectFnSet.GetLeft(pStTab->getFrameArea());
            }
        }

        // Restrict nX to the left and right borders of pTab:
        // (is this really necessary?)
        if (pTable && !pTable->GetUpper()->IsInTab())
        {
            const bool bRTL = pTable->IsRightToLeft();
            const tools::Long nPrtLeft = bRTL ?
                                aRectFnSet.GetPrtRight(*pTable) :
                                aRectFnSet.GetPrtLeft(*pTable);
            if (bRTL != (aRectFnSet.XDiff(nPrtLeft, nX) > 0))
                nX = nPrtLeft;
            else
            {
                const tools::Long nPrtRight = bRTL ?
                                    aRectFnSet.GetPrtLeft(*pTable) :
                                    aRectFnSet.GetPrtRight(*pTable);
                if (bRTL != (aRectFnSet.XDiff(nX, nPrtRight) > 0))
                    nX = nPrtRight;
            }
        }
    }

    do
    {
        //If I'm in the DocumentBody, I want to stay there.
        if ( pStart->IsInDocBody() )
        {
            while (pCnt && (!pCnt->IsInDocBody() || pCnt->IsHiddenNow()))
            {
                pCnt = (*fnNxtPrv)( pCnt );
                pCnt = ::lcl_MissProtectedFrames( pCnt, fnNxtPrv, true, bInReadOnly, bTableSel );
            }
        }

        //If I'm in the FootNoteArea, I try to reach the next FootNoteArea in
        //case of necessity.
        else if ( pStart->IsInFootnote() )
        {
            while (pCnt && (!pCnt->IsInFootnote() || pCnt->IsHiddenNow()))
            {
                pCnt = (*fnNxtPrv)( pCnt );
                pCnt = ::lcl_MissProtectedFrames( pCnt, fnNxtPrv, true, bInReadOnly, bTableSel );
            }
        }

        //In Flys we can go ahead blindly as long as we find a Content.
        else if ( pStart->IsInFly() )
        {
            if (pCnt && pCnt->IsHiddenNow())
            {
                pCnt = (*fnNxtPrv)( pCnt );
                pCnt = ::lcl_MissProtectedFrames( pCnt, fnNxtPrv, true, bInReadOnly, bTableSel );
            }
        }

        //Otherwise I'll just refuse to leave to current area.
        else if ( pCnt )
        {
            const SwFrame *pUp = pStart->GetUpper();
            while (pUp && pUp->GetUpper() && !(pUp->GetType() & FRM_HEADFOOT))
                pUp = pUp->GetUpper();
            bool bSame = false;
            const SwFrame *pCntUp = pCnt->GetUpper();
            while ( pCntUp && !bSame )
            {
                if ( pUp == pCntUp )
                    bSame = true;
                else
                    pCntUp = pCntUp->GetUpper();
            }
            if ( !bSame )
                pCnt = nullptr;
            else if (pCnt->IsHiddenNow()) // i73332
            {
                pCnt = (*fnNxtPrv)( pCnt );
                pCnt = ::lcl_MissProtectedFrames( pCnt, fnNxtPrv, true, bInReadOnly, bTableSel );
            }
        }

        if ( bTab )
        {
            if ( !pCnt )
                bEnd = true;
            else
            {
                const SwTabFrame *pTab = pCnt->FindTabFrame();
                if( !pTab )
                    bEnd = true;
                else
                {
                    if ( pTab != pTable )
                    {
                        //The flow leads from one table to the next. The X-value
                        //needs to be corrected by the amount of the offset of
                        //the tables
                        if ( pTable &&
                              !pTab->GetUpper()->IsInTab() &&
                            !pTable->GetUpper()->IsInTab() )
                            nX += pTab->getFrameArea().Left() - pTable->getFrameArea().Left();
                        pTable = pTab;
                    }
                    const SwLayoutFrame *pCell = pCnt->GetUpper();
                    while ( pCell && !pCell->IsCellFrame() )
                        pCell = pCell->GetUpper();

                    Point aInsideCell;
                    Point aInsideCnt;
                    if ( pCell )
                    {
                        tools::Long nTmpTop = aRectFnSet.GetTop(pCell->getFrameArea());
                        if ( aRectFnSet.IsVert() )
                        {
                            if ( nTmpTop )
                                nTmpTop = aRectFnSet.XInc(nTmpTop, -1);

                            aInsideCell = Point( nTmpTop, nX );
                        }
                        else
                            aInsideCell = Point( nX, nTmpTop );
                    }

                    tools::Long nTmpTop = aRectFnSet.GetTop(pCnt->getFrameArea());
                    if ( aRectFnSet.IsVert() )
                    {
                        if ( nTmpTop )
                            nTmpTop = aRectFnSet.XInc(nTmpTop, -1);

                        aInsideCnt = Point( nTmpTop, nX );
                    }
                    else
                        aInsideCnt = Point( nX, nTmpTop );

                    if ( pCell && pCell->getFrameArea().Contains( aInsideCell ) )
                    {
                        bEnd = true;
                        //Get the right Content out of the cell.
                        if ( !pCnt->getFrameArea().Contains( aInsideCnt ) )
                        {
                            pCnt = pCell->ContainsContent();
                            if ( fnNxtPrv == lcl_GetPrvCnt )
                                while ( pCell->IsAnLower(pCnt->GetNextContentFrame()) )
                                    pCnt = pCnt->GetNextContentFrame();
                        }
                    }
                    else if ( pCnt->getFrameArea().Contains( aInsideCnt ) )
                        bEnd = true;
                }
            }
            if ( !bEnd )
            {
                pCnt = (*fnNxtPrv)( pCnt );
                pCnt = ::lcl_MissProtectedFrames( pCnt, fnNxtPrv, true, bInReadOnly, bTableSel );
            }
        }

    } while (!bEnd || (pCnt && pCnt->IsHiddenNow()));

    if (pCnt == nullptr)
    {
        return false;
    }
    if (pCnt->IsTextFrame())
    {
        SwTextFrame const*const pFrame(static_cast<SwTextFrame const*>(pCnt));
        *pPam->GetPoint() = pFrame->MapViewToModelPos(TextFrameIndex(
                fnNxtPrv == lcl_GetPrvCnt
                    ? pFrame->GetText().getLength()
                    : 0));
    }
    else
    {   // set the Point on the Content-Node
        assert(pCnt->IsNoTextFrame());
        const SwContentNode* const pCNd = static_cast<SwNoTextFrame const*>(pCnt)->GetNode();
        if ( fnNxtPrv == lcl_GetPrvCnt )
            pPam->GetPoint()->AssignEndIndex(*pCNd);
        else
            pPam->GetPoint()->AssignStartIndex(*pCNd);
    }
    return true;
}

bool SwContentFrame::UnitUp( SwPaM* pPam, const SwTwips, bool bInReadOnly ) const
{
    return ::lcl_UpDown( pPam, this, lcl_GetPrvCnt, bInReadOnly );
}

bool SwContentFrame::UnitDown( SwPaM* pPam, const SwTwips, bool bInReadOnly ) const
{
    return ::lcl_UpDown( pPam, this, lcl_GetNxtCnt, bInReadOnly );
}

/** Returns the number of the current page.
 *
 * If the method gets a PaM then the current page is the one in which the PaM sits. Otherwise the
 * current page is the first one inside the VisibleArea. We only work on available pages!
 */
sal_uInt16 SwRootFrame::GetCurrPage( const SwPaM *pActualCursor ) const
{
    assert(pActualCursor && "got no page cursor");
    SwFrame const*const pActFrame = pActualCursor->GetPoint()->GetNode().
                                    GetContentNode()->getLayoutFrame(this,
                                                    pActualCursor->GetPoint());
    return pActFrame->FindPageFrame()->GetPhyPageNum();
}

/** Returns a PaM which sits at the beginning of the requested page.
 *
 * Formatting is done as far as necessary.
 * The PaM sits on the last page, if the page number was chosen too big.
 *
 * @return Null, if the operation was not possible.
 */
sal_uInt16 SwRootFrame::SetCurrPage( SwCursor* pToSet, sal_uInt16 nPageNum )
{
    vcl::RenderContext* pRenderContext = GetCurrShell() ? GetCurrShell()->GetOut() : nullptr;
    SwFrame* pLower = Lower();
    assert( (pLower && pLower->IsPageFrame()) && "No page available." );

    SwPageFrame *pPage = static_cast<SwPageFrame*>(pLower);
    bool bEnd =false;
    while ( !bEnd && pPage->GetPhyPageNum() != nPageNum )
    {
        if ( pPage->GetNext() )
            pPage = static_cast<SwPageFrame*>(pPage->GetNext());
        else
        {   //Search the first ContentFrame and format until a new page is started
            //or until the ContentFrame are all done.
            const SwContentFrame *pContent = pPage->ContainsContent();
            while ( pContent && pPage->IsAnLower( pContent ) )
            {
                pContent->Calc(pRenderContext);
                pContent = pContent->GetNextContentFrame();
            }
            //Either this is a new page or we found the last page.
            if ( pPage->GetNext() )
                pPage = static_cast<SwPageFrame*>(pPage->GetNext());
            else
                bEnd = true;
        }
    }
    //pPage now points to the 'requested' page. Now we have to create the PaM
    //on the beginning of the first ContentFrame in the body-text.
    //If this is a footnote-page, the PaM will be set in the first footnote.
    const SwContentFrame *pContent = pPage->ContainsContent();
    if ( pPage->IsFootnotePage() )
        while ( pContent && !pContent->IsInFootnote() )
            pContent = pContent->GetNextContentFrame();
    else
        while ( pContent && !pContent->IsInDocBody() )
            pContent = pContent->GetNextContentFrame();
    if ( pContent )
    {
        assert(pContent->IsTextFrame());
        SwTextFrame const*const pFrame(static_cast<const SwTextFrame*>(pContent));
        *pToSet->GetPoint() = pFrame->MapViewToModelPos(pFrame->GetOffset());

        SwShellCursor* pSCursor = dynamic_cast<SwShellCursor*>(pToSet);
        if( pSCursor )
        {
            Point &rPt = pSCursor->GetPtPos();
            rPt = pContent->getFrameArea().Pos();
            rPt += pContent->getFramePrintArea().Pos();
        }
        return pPage->GetPhyPageNum();
    }
    return 0;
}

SwContentFrame *GetFirstSub( const SwLayoutFrame *pLayout )
{
    return const_cast<SwPageFrame*>(static_cast<const SwPageFrame*>(pLayout))->FindFirstBodyContent();
}

SwContentFrame *GetLastSub( const SwLayoutFrame *pLayout )
{
    return const_cast<SwPageFrame*>(static_cast<const SwPageFrame*>(pLayout))->FindLastBodyContent();
}

SwLayoutFrame *GetNextFrame( const SwLayoutFrame *pFrame )
{
    SwLayoutFrame *pNext =
        (pFrame->GetNext() && pFrame->GetNext()->IsLayoutFrame()) ?
                                            const_cast<SwLayoutFrame*>(static_cast<const SwLayoutFrame*>(pFrame->GetNext())) : nullptr;
    // #i39402# in case of an empty page
    if(pNext && !pNext->ContainsContent())
        pNext = (pNext->GetNext() && pNext->GetNext()->IsLayoutFrame()) ?
                                            static_cast<SwLayoutFrame*>(pNext->GetNext()) : nullptr;
    return pNext;
}

SwLayoutFrame *GetThisFrame( const SwLayoutFrame *pFrame )
{
    return const_cast<SwLayoutFrame*>(pFrame);
}

SwLayoutFrame *GetPrevFrame( const SwLayoutFrame *pFrame )
{
    SwLayoutFrame *pPrev =
        (pFrame->GetPrev() && pFrame->GetPrev()->IsLayoutFrame()) ?
                                            const_cast<SwLayoutFrame*>(static_cast<const SwLayoutFrame*>(pFrame->GetPrev())) : nullptr;
    // #i39402# in case of an empty page
    if(pPrev && !pPrev->ContainsContent())
        pPrev = (pPrev->GetPrev() && pPrev->GetPrev()->IsLayoutFrame()) ?
                                            static_cast<SwLayoutFrame*>(pPrev->GetPrev()) : nullptr;
    return pPrev;
}

/**
 * Returns the first/last Contentframe (controlled using the parameter fnPosPage)
 * of the current/previous/next page (controlled using the parameter fnWhichPage).
 */
bool GetFrameInPage( const SwContentFrame *pCnt, SwWhichPage fnWhichPage,
                   SwPosPage fnPosPage, SwPaM *pPam )
{
    //First find the requested page, at first the current, then the one which
    //was requests through fnWichPage.
    const SwLayoutFrame *pLayoutFrame = pCnt->FindPageFrame();
    if ( !pLayoutFrame || (nullptr == (pLayoutFrame = (*fnWhichPage)(pLayoutFrame))) )
        return false;

    //Now the desired ContentFrame below the page
    pCnt = (*fnPosPage)(pLayoutFrame);
    if( nullptr == pCnt )
        return false;
    else
    {
        // repeated headlines in tables
        if ( pCnt->IsInTab() && fnPosPage == GetFirstSub )
        {
            const SwTabFrame* pTab = pCnt->FindTabFrame();
            if ( pTab->IsFollow() )
            {
                if ( pTab->IsInHeadline( *pCnt ) )
                {
                    SwLayoutFrame* pRow = pTab->GetFirstNonHeadlineRow();
                    if ( pRow )
                    {
                        // We are in the first line of a follow table
                        // with repeated headings.
                        // To actually make a "real" move we take the first content
                        // of the next row
                        pCnt = pRow->ContainsContent();
                        if ( ! pCnt )
                            return false;
                    }
                }
            }
        }

        assert(pCnt->IsTextFrame());
        SwTextFrame const*const pFrame(static_cast<const SwTextFrame*>(pCnt));
        TextFrameIndex const nIdx((fnPosPage == GetFirstSub)
            ? pFrame->GetOffset()
            : (pFrame->GetFollow())
                ? pFrame->GetFollow()->GetOffset() - TextFrameIndex(1)
                : TextFrameIndex(pFrame->GetText().getLength()));
        *pPam->GetPoint() = pFrame->MapViewToModelPos(nIdx);
        return true;
    }
}

static sal_uInt64 CalcDiff(const Point &rPt1, const Point &rPt2)
{
    //Calculate the distance between the two points.
    //'delta' X^2 + 'delta'Y^2 = 'distance'^2
    sal_uInt64 dX = std::max( rPt1.X(), rPt2.X() ) -
               std::min( rPt1.X(), rPt2.X() ),
          dY = std::max( rPt1.Y(), rPt2.Y() ) -
               std::min( rPt1.Y(), rPt2.Y() );
    return (dX * dX) + (dY * dY);
}

/** Check if the point lies inside the page part in which also the ContentFrame lies.
 *
 * In this context header, page body, footer and footnote-container count as page part.
 * This will suit the purpose that the ContentFrame which lies in the "right" page part will be
 * accepted instead of one which doesn't lie there although his distance to the point is shorter.
 */
static const SwLayoutFrame* lcl_Inside( const SwContentFrame *pCnt, Point const & rPt )
{
    const SwLayoutFrame* pUp = pCnt->GetUpper();
    while( pUp )
    {
        if( pUp->IsPageBodyFrame() || pUp->IsFooterFrame() || pUp->IsHeaderFrame() )
        {
            if( rPt.Y() >= pUp->getFrameArea().Top() && rPt.Y() <= pUp->getFrameArea().Bottom() )
                return pUp;
            return nullptr;
        }
        if( pUp->IsFootnoteContFrame() )
            return pUp->getFrameArea().Contains( rPt ) ? pUp : nullptr;
        pUp = pUp->GetUpper();
    }
    return nullptr;
}

/** Search for the nearest Content to pass.
 *
 * Considers the previous, the current and the next page.
 * If no content is found, the area gets expanded until one is found.
 *
 * @return The 'semantically correct' position inside the PrtArea of the found ContentFrame.
 */
const SwContentFrame *SwLayoutFrame::GetContentPos( Point& rPoint,
                                            const bool bDontLeave,
                                            const bool bBodyOnly,
                                            SwCursorMoveState *pCMS,
                                            const bool bDefaultExpand ) const
{
    //Determine the first ContentFrame.
    const SwLayoutFrame *pStart = (!bDontLeave && bDefaultExpand && GetPrev()) ?
                                    static_cast<const SwLayoutFrame*>(GetPrev()) : this;
    const SwContentFrame *pContent = pStart->ContainsContent();

    if ( !pContent && (GetPrev() && !bDontLeave) )
        pContent = ContainsContent();

    if ( bBodyOnly )
        while ( pContent && !pContent->IsInDocBody() )
            pContent = pContent->GetNextContentFrame();

    const SwContentFrame *pActual= pContent;
    const SwLayoutFrame *pInside = nullptr;
    sal_uInt16 nMaxPage = GetPhyPageNum() + (bDefaultExpand ? 1 : 0);
    Point aPoint = rPoint;
    sal_uInt64 nDistance = SAL_MAX_UINT64;

    while ( true )  //A loop to be sure we always find one.
    {
        while ( pContent &&
                ((!bDontLeave || IsAnLower( pContent )) &&
                (pContent->GetPhyPageNum() <= nMaxPage)) )
        {
            if ( pContent->getFrameArea().Width() &&
                 ( !bBodyOnly || pContent->IsInDocBody() ) )
            {
                //If the Content lies in a protected area (cell, Footnote, section),
                //we search the next Content which is not protected.
                const SwContentFrame *pComp = pContent;
                pContent = ::lcl_MissProtectedFrames( pContent, lcl_GetNxtCnt, false,
                                        pCMS && pCMS->m_bSetInReadOnly, false );
                if ( pComp != pContent )
                    continue;

                if (!pContent->IsHiddenNow())
                {
                    SwRect aContentFrame( pContent->UnionFrame() );
                    if ( aContentFrame.Contains( rPoint ) )
                    {
                        pActual = pContent;
                        aPoint = rPoint;
                        break;
                    }
                    //The distance from rPoint to the nearest Point of pContent
                    //will now be calculated.
                    Point aContentPoint( rPoint );

                    //First set the vertical position
                    if ( aContentFrame.Top() > aContentPoint.Y() )
                        aContentPoint.setY( aContentFrame.Top() );
                    else if ( aContentFrame.Bottom() < aContentPoint.Y() )
                        aContentPoint.setY( aContentFrame.Bottom() );

                    //Now the horizontal position
                    if ( aContentFrame.Left() > aContentPoint.X() )
                        aContentPoint.setX( aContentFrame.Left() );
                    else if ( aContentFrame.Right() < aContentPoint.X() )
                        aContentPoint.setX( aContentFrame.Right() );

                    // pInside is a page area in which the point lies. As soon
                    // as pInside != 0 only frames are accepted which are
                    // placed inside.
                    if( !pInside || ( pInside->IsAnLower( pContent ) &&
                        ( !pContent->IsInFootnote() || pInside->IsFootnoteContFrame() ) ) )
                    {
                        const sal_uInt64 nDiff = ::CalcDiff(aContentPoint, rPoint);
                        bool bBetter = nDiff < nDistance;  // This one is nearer
                        if( !pInside )
                        {
                            pInside = lcl_Inside( pContent, rPoint );
                            if( pInside )  // In the "right" page area
                                bBetter = true;
                        }
                        if( bBetter )
                        {
                            aPoint = aContentPoint;
                            nDistance = nDiff;
                            pActual = pContent;
                        }
                    }
                }
            }
            pContent = pContent->GetNextContentFrame();
            if ( bBodyOnly )
                while ( pContent && !pContent->IsInDocBody() )
                    pContent = pContent->GetNextContentFrame();
        }
        if ( !pActual )
        {   //If we not yet found one we have to expand the searched
            //area, sometime we will find one!
            //MA 1997-01-09: Opt for many empty pages - if we only search inside
            //the body, we can expand the searched area sufficiently in one step.
            if ( bBodyOnly )
            {
                while ( !pContent && pStart->GetPrev() )
                {
                    ++nMaxPage;
                    if( !pStart->GetPrev()->IsLayoutFrame() )
                        return nullptr;
                    pStart = static_cast<const SwLayoutFrame*>(pStart->GetPrev());
                    if( pStart->IsInDocBody() )
                        pContent = pStart->ContainsContent();
                    else
                    {
                        const SwPageFrame *pPage = pStart->FindPageFrame();
                        if( !pPage )
                            return nullptr;
                        pContent = pPage->FindFirstBodyContent();
                    }
                }
                if ( !pContent )  // Somewhere down the road we have to start with one!
                {
                    const SwPageFrame *pPage = pStart->FindPageFrame();
                    if( !pPage )
                        return nullptr;
                    pContent = pPage->GetUpper()->ContainsContent();
                    while ( pContent && !pContent->IsInDocBody() )
                        pContent = pContent->GetNextContentFrame();
                    if ( !pContent )
                        return nullptr;   // There is no document content yet!
                }
            }
            else
            {
                ++nMaxPage;
                if ( pStart->GetPrev() )
                {
                    if( !pStart->GetPrev()->IsLayoutFrame() )
                        return nullptr;
                    pStart = static_cast<const SwLayoutFrame*>(pStart->GetPrev());
                    pContent = pStart->ContainsContent();
                }
                else // Somewhere down the road we have to start with one!
                {
                    const SwPageFrame *pPage = pStart->FindPageFrame();
                    if( !pPage )
                        return nullptr;
                    pContent = pPage->GetUpper()->ContainsContent();
                }
            }
            pActual = pContent;
        }
        else
            break;
    }

    OSL_ENSURE( pActual, "no Content found." );
    OSL_ENSURE( !bBodyOnly || pActual->IsInDocBody(), "Content not in Body." );

    //Special case for selecting tables not in repeated TableHeadlines.
    if ( pActual->IsInTab() && pCMS && pCMS->m_eState == CursorMoveState::TableSel )
    {
        const SwTabFrame *pTab = pActual->FindTabFrame();
        if ( pTab->IsFollow() && pTab->IsInHeadline( *pActual ) )
        {
            pCMS->m_bStop = true;
            return nullptr;
        }
    }

    //A small correction at the first/last
    Size aActualSize( pActual->getFramePrintArea().SSize() );
    if ( aActualSize.Height() > pActual->GetUpper()->getFramePrintArea().Height() )
        aActualSize.setHeight( pActual->GetUpper()->getFramePrintArea().Height() );

    SwRectFnSet aRectFnSet(pActual);
    if ( !pActual->GetPrev() &&
         aRectFnSet.YDiff( aRectFnSet.GetPrtTop(*pActual),
                              aRectFnSet.IsVert() ? rPoint.X() : rPoint.Y() ) > 0 )
    {
        aPoint.setY( pActual->getFrameArea().Top() + pActual->getFramePrintArea().Top() );
        aPoint.setX( pActual->getFrameArea().Left() +
                        ( pActual->IsRightToLeft() || aRectFnSet.IsVert() ?
                          pActual->getFramePrintArea().Right() :
                          pActual->getFramePrintArea().Left() ) );
    }
    else if ( !pActual->GetNext() &&
              aRectFnSet.YDiff( aRectFnSet.GetPrtBottom(*pActual),
                                   aRectFnSet.IsVert() ? rPoint.X() : rPoint.Y() ) < 0 )
    {
        aPoint.setY( pActual->getFrameArea().Top() + pActual->getFramePrintArea().Bottom() );
        aPoint.setX( pActual->getFrameArea().Left() +
                        ( pActual->IsRightToLeft() || aRectFnSet.IsVert() ?
                          pActual->getFramePrintArea().Left() :
                          pActual->getFramePrintArea().Right() ) );
    }

    //Bring the Point into the PrtArea
    const SwRect aRect( pActual->getFrameArea().Pos() + pActual->getFramePrintArea().Pos(),
                        aActualSize );
    if ( aPoint.Y() < aRect.Top() )
        aPoint.setY( aRect.Top() );
    else if ( aPoint.Y() > aRect.Bottom() )
        aPoint.setY( aRect.Bottom() );
    if ( aPoint.X() < aRect.Left() )
        aPoint.setX( aRect.Left() );
    else if ( aPoint.X() > aRect.Right() )
        aPoint.setX( aRect.Right() );
    rPoint = aPoint;
    return pActual;
}

/** Same as SwLayoutFrame::GetContentPos(). Specialized for fields and border. */
void SwPageFrame::GetContentPosition( const Point &rPt, SwPosition &rPos ) const
{
    //Determine the first ContentFrame.
    const SwContentFrame *pContent = ContainsContent();
    if ( pContent )
    {
        //Look back one more (if possible).
        const SwContentFrame *pTmp = pContent->GetPrevContentFrame();
        while ( pTmp && !pTmp->IsInDocBody() )
            pTmp = pTmp->GetPrevContentFrame();
        if ( pTmp )
            pContent = pTmp;
    }
    else
        pContent = GetUpper()->ContainsContent();

    const SwContentFrame *pAct = pContent;
    Point aAct       = rPt;
    sal_uInt64 nDist = SAL_MAX_UINT64;

    while ( pContent )
    {
        SwRect aContentFrame( pContent->UnionFrame() );
        if ( aContentFrame.Contains( rPt ) )
        {
            //This is the nearest one.
            pAct = pContent;
            break;
        }

        //Calculate the distance from rPt to the nearest point of pContent.
        Point aPoint( rPt );

        //Calculate the vertical position first
        if ( aContentFrame.Top() > rPt.Y() )
            aPoint.setY( aContentFrame.Top() );
        else if ( aContentFrame.Bottom() < rPt.Y() )
            aPoint.setY( aContentFrame.Bottom() );

        //And now the horizontal position
        if ( aContentFrame.Left() > rPt.X() )
            aPoint.setX( aContentFrame.Left() );
        else if ( aContentFrame.Right() < rPt.X() )
            aPoint.setX( aContentFrame.Right() );

        const sal_uInt64 nDiff = ::CalcDiff( aPoint, rPt );
        if ( nDiff < nDist )
        {
            aAct    = aPoint;
            nDist   = nDiff;
            pAct    = pContent;
        }
        else if ( aContentFrame.Top() > getFrameArea().Bottom() )
            //In terms of fields, it's not possible to be closer any more!
            break;

        pContent = pContent->GetNextContentFrame();
        while ( pContent && !pContent->IsInDocBody() )
            pContent = pContent->GetNextContentFrame();
    }

    //Bring the point into the PrtArea.
    assert(pAct);
    const SwRect aRect( pAct->getFrameArea().Pos() + pAct->getFramePrintArea().Pos(), pAct->getFramePrintArea().SSize() );
    if ( aAct.Y() < aRect.Top() )
        aAct.setY( aRect.Top() );
    else if ( aAct.Y() > aRect.Bottom() )
        aAct.setY( aRect.Bottom() );
    if ( aAct.X() < aRect.Left() )
        aAct.setX( aRect.Left() );
    else if ( aAct.X() > aRect.Right() )
        aAct.setX( aRect.Right() );

    if (!pAct->isFrameAreaDefinitionValid() ||
        (pAct->IsTextFrame() && !static_cast<SwTextFrame const*>(pAct)->HasPara()))
    {
        // ContentFrame not formatted -> always on node-beginning
        // tdf#100635 also if the SwTextFrame would require reformatting,
        // which is unwanted in case this is called from text formatting code
        rPos = static_cast<SwTextFrame const*>(pAct)->MapViewToModelPos(TextFrameIndex(0));
    }
    else
    {
        SwCursorMoveState aTmpState( CursorMoveState::SetOnlyText );
        pAct->GetModelPositionForViewPoint( &rPos, aAct, &aTmpState );
    }
}

/** Search the nearest Content to the passed point.
 *
 * Only search inside the BodyText.
 * @note Only the nearest vertically one will be searched.
 * @note JP 11.10.2001: only in tables we try to find the right column - Bug 72294
 */
Point SwRootFrame::GetNextPrevContentPos( const Point& rPoint, bool bNext ) const
{
    vcl::RenderContext* pRenderContext = GetCurrShell() ? GetCurrShell()->GetOut() : nullptr;
    // #123110# - disable creation of an action by a callback
    // event during processing of this method. Needed because formatting is
    // triggered by this method.
    DisableCallbackAction aDisableCallbackAction(const_cast<SwRootFrame&>(*this));
    //Search the first ContentFrame and his successor in the body area.
    //To be efficient (and not formatting too much) we'll start at the correct
    //page.
    const SwLayoutFrame *pPage = static_cast<const SwLayoutFrame*>(Lower());
    if( pPage )
        while( pPage->GetNext() && pPage->getFrameArea().Bottom() < rPoint.Y() )
            pPage = static_cast<const SwLayoutFrame*>(pPage->GetNext());

    const SwContentFrame *pCnt = pPage ? pPage->ContainsContent() : ContainsContent();
    while ( pCnt && !pCnt->IsInDocBody() )
        pCnt = pCnt->GetNextContentFrame();

    if ( !pCnt )
        return Point( 0, 0 );

    pCnt->Calc(pRenderContext);
    if( !bNext )
    {
        // As long as the point lies before the first ContentFrame and there are
        // still precedent pages I'll go to the next page.
        assert(pPage);
        while ( rPoint.Y() < pCnt->getFrameArea().Top() && pPage->GetPrev() )
        {
            pPage = static_cast<const SwLayoutFrame*>(pPage->GetPrev());
            pCnt = pPage->ContainsContent();
            while ( !pCnt )
            {
                pPage = static_cast<const SwLayoutFrame*>(pPage->GetPrev());
                if ( pPage )
                    pCnt = pPage->ContainsContent();
                else
                {
                    pCnt = ContainsContent();
                    return pCnt ? pCnt->UnionFrame().Pos() : Point();
                }
            }
            pCnt->Calc(pRenderContext);
        }
    }

    //Does the point lie above the first ContentFrame?
    if ( rPoint.Y() < pCnt->getFrameArea().Top() && !lcl_IsInRepeatedHeadline( pCnt ) )
        return pCnt->UnionFrame().Pos();

    Point aRet(0, 0);
    do
    {
        //Does the point lie in the current ContentFrame?
        SwRect aContentFrame( pCnt->UnionFrame() );
        if ( aContentFrame.Contains( rPoint ) && !lcl_IsInRepeatedHeadline( pCnt ))
        {
            aRet = rPoint;
            break;
        }

        //Is the current one the last ContentFrame?
        //If the next ContentFrame lies behind the point, then the current on is the
        //one we searched.
        const SwContentFrame *pNxt = pCnt->GetNextContentFrame();
        while ( pNxt && !pNxt->IsInDocBody() )
            pNxt = pNxt->GetNextContentFrame();

        //Does the point lie behind the last ContentFrame?
        if ( !pNxt )
        {
            aRet = Point( aContentFrame.Right(), aContentFrame.Bottom() );
            break;
        }

        //If the next ContentFrame lies behind the point then it is the one we
        //searched.
        const SwTabFrame* pTFrame;
        pNxt->Calc(pRenderContext);
        if( pNxt->getFrameArea().Top() > rPoint.Y() &&
            !lcl_IsInRepeatedHeadline( pCnt, &pTFrame ) &&
            ( !pTFrame || pNxt->getFrameArea().Left() > rPoint.X() ))
        {
            if (bNext)
                aRet = pNxt->getFrameArea().Pos();
            else
                aRet = Point( aContentFrame.Right(), aContentFrame.Bottom() );
            break;
        }
        pCnt = pNxt;
    }
    while (pCnt);
    return aRet;
}

/** Returns the absolute document position of the desired page.
 *
 * Formatting is done only as far as needed and only if bFormat=true.
 * Pos is set to the one of the last page, if the page number was chosen too big.
 *
 * @return Null, if the operation failed.
 */
Point SwRootFrame::GetPagePos( sal_uInt16 nPageNum ) const
{
    const SwFrame* pLower = Lower();
    assert( (pLower && pLower->IsPageFrame()) && "No page available." );

    const SwPageFrame *pPage = static_cast<const SwPageFrame*>(pLower);
    while ( true )
    {
        if ( pPage->GetPhyPageNum() >= nPageNum || !pPage->GetNext() )
            break;
        pPage = static_cast<const SwPageFrame*>(pPage->GetNext());
    }
    return pPage->getFrameArea().Pos();
}

/** get page frame by physical page number
 *
 * @return pointer to the page frame with the given physical page number
 */
SwPageFrame* SwRootFrame::GetPageByPageNum( sal_uInt16 _nPageNum ) const
{
    const SwPageFrame* pPageFrame = static_cast<const SwPageFrame*>( Lower() );
    while ( pPageFrame && pPageFrame->GetPhyPageNum() < _nPageNum )
    {
          pPageFrame = static_cast<const SwPageFrame*>( pPageFrame->GetNext() );
    }

    if ( pPageFrame && pPageFrame->GetPhyPageNum() == _nPageNum )
    {
        return const_cast<SwPageFrame*>( pPageFrame );
    }
    else
    {
        return nullptr;
    }
}

/**
 * @return true, when the given physical pagenumber doesn't exist or this page is an empty page.
 */
bool SwRootFrame::IsDummyPage( sal_uInt16 nPageNum ) const
{
    if( !Lower() || !nPageNum || nPageNum > GetPageNum() )
        return true;

    const SwPageFrame *pPage = static_cast<const SwPageFrame*>(Lower());
    while( pPage && nPageNum < pPage->GetPhyPageNum() )
        pPage = static_cast<const SwPageFrame*>(pPage->GetNext());
    return !pPage || pPage->IsEmptyPage();
}

/** Is the Frame or rather the Section in which it lies protected?
 *
 * Also Fly in Fly in ... and Footnotes
 */
bool SwFrame::IsProtected() const
{
    if (IsTextFrame())
    {
        const SwDoc *pDoc = &static_cast<const SwTextFrame*>(this)->GetDoc();
        bool isFormProtected=pDoc->GetDocumentSettingManager().get(DocumentSettingId::PROTECT_FORM );
        if (isFormProtected)
        {
            return false; // TODO a hack for now, well deal with it later, I we return true here we have a "double" locking
        }
    }
    //The Frame can be protected in borders, cells or sections.
    //Also goes up FlyFrames recursive and from footnote to anchor.
    const SwFrame *pFrame = this;
    do
    {
        if (pFrame->IsTextFrame())
        {   // sw_redlinehide: redlines can't overlap section nodes, so any node will do
            if (static_cast<SwTextFrame const*>(pFrame)->GetTextNodeFirst()->IsInProtectSect())
            {
                return true;
            }
        }
        else if ( pFrame->IsContentFrame() )
        {
            assert(pFrame->IsNoTextFrame());
            if (static_cast<const SwNoTextFrame*>(pFrame)->GetNode() &&
                static_cast<const SwNoTextFrame*>(pFrame)->GetNode()->IsInProtectSect())
            {
                return true;
            }
        }
        else
        {
            if ( static_cast<const SwLayoutFrame*>(pFrame)->GetFormat() &&
                 static_cast<const SwLayoutFrame*>(pFrame)->GetFormat()->
                 GetProtect().IsContentProtected() )
                return true;
            if ( pFrame->IsCoveredCell() )
                return true;
        }
        if ( pFrame->IsFlyFrame() )
        {
            //In a chain the protection of the content can be specified by the
            //master of the chain.
            if ( static_cast<const SwFlyFrame*>(pFrame)->GetPrevLink() )
            {
                const SwFlyFrame *pMaster = static_cast<const SwFlyFrame*>(pFrame);
                do
                {   pMaster = pMaster->GetPrevLink();
                } while ( pMaster->GetPrevLink() );
                if ( pMaster->IsProtected() )
                    return true;
            }
            pFrame = static_cast<const SwFlyFrame*>(pFrame)->GetAnchorFrame();
        }
        else if ( pFrame->IsFootnoteFrame() )
            pFrame = static_cast<const SwFootnoteFrame*>(pFrame)->GetRef();
        else
            pFrame = pFrame->GetUpper();

    } while ( pFrame );

    return false;
}

// virtual
bool SwFrame::IsHiddenNow() const
{
    if (const auto* pSectFrame = FindSctFrame())
        return pSectFrame->IsHiddenNow();

    return false;
}

void SwFrame::MakeValidZeroHeight()
{
    SwRectFnSet aRectFnSet(this);
    {
        SwFrameAreaDefinition::FramePrintAreaWriteAccess area(*this);
        aRectFnSet.SetHeight(area, 0);
    }
    ShrinkFrame(aRectFnSet.GetHeight(getFrameArea()));
    if (IsLayoutFrame() && aRectFnSet.GetHeight(getFrameArea())) // ShrinkFrame might do nothing!
    {
        SwFrameAreaDefinition::FrameAreaWriteAccess area(*this);
        aRectFnSet.SetHeight(area, 0);
        if (GetUpper())
            GetUpper()->InvalidateSize();
    }
    setFrameAreaSizeValid(true);
    setFramePrintAreaValid(true);
}

/** @return the physical page number */
sal_uInt16 SwFrame::GetPhyPageNum() const
{
    const SwPageFrame *pPage = FindPageFrame();
    return pPage ? pPage->GetPhyPageNum() : 0;
}

/** Decides if the page want to be a right page or not.
 *
 * If the first content of the page has a page descriptor, we take the follow
 * of the page descriptor of the last not empty page. If this descriptor allows
 * only right(left) pages and the page isn't an empty page then it wants to be
 * such right(left) page. If the descriptor allows right and left pages, we
 * look for a number offset in the first content. If there is one, odd number
 * results right pages (or left pages if document starts with even number),
 * even number results left pages (or right pages if document starts with even
 * number).
 * If there is no number offset, we take the physical page number instead,
 * but a previous empty page doesn't count.
 */
bool SwFrame::WannaRightPage() const
{
    const SwPageFrame *pPage = FindPageFrame();
    if ( !pPage || !pPage->GetUpper() )
        return true;

    const SwFrame *pFlow = pPage->FindFirstBodyContent();
    const SwPageDesc *pDesc = nullptr;
    ::std::optional<sal_uInt16> oPgNum;
    if ( pFlow )
    {
        if ( pFlow->IsInTab() )
            pFlow = pFlow->FindTabFrame();
        const SwFlowFrame *pTmp = SwFlowFrame::CastFlowFrame( pFlow );
        if ( !pTmp->IsFollow() )
        {
            const SwFormatPageDesc& rPgDesc = pFlow->GetPageDescItem();
            pDesc = rPgDesc.GetPageDesc();
            oPgNum = rPgDesc.GetNumOffset();
        }
    }
    if ( !pDesc )
    {
        const SwPageFrame* pPrv = static_cast<const SwPageFrame*>(pPage->GetPrev());
        if( pPrv && pPrv->IsEmptyPage() )
            pPrv = static_cast<const SwPageFrame*>(pPrv->GetPrev());
        if( pPrv )
            pDesc = pPrv->GetPageDesc()->GetFollow();
        else
        {
            const SwDoc& rDoc = pPage->GetFormat()->GetDoc();
            pDesc = &rDoc.GetPageDesc( 0 );
        }
    }
    OSL_ENSURE( pDesc, "No pagedescriptor" );
    bool isRightPage;
    if( oPgNum )
        isRightPage = sw::IsRightPageByNumber(*mpRoot, *oPgNum);
    else
    {
        isRightPage = pPage->OnRightPage();
        if( pPage->GetPrev() && static_cast<const SwPageFrame*>(pPage->GetPrev())->IsEmptyPage() )
            isRightPage = !isRightPage;
    }
    if( !pPage->IsEmptyPage() )
    {
        assert(pDesc && "No pagedescriptor");
        if( !pDesc->GetRightFormat() )
            isRightPage = false;
        else if( !pDesc->GetLeftFormat() )
            isRightPage = true;
    }
    return isRightPage;
}

bool SwFrame::OnFirstPage() const
{
    bool bRet = false;
    const SwPageFrame *pPage = FindPageFrame();

    if (pPage)
    {
        const SwPageFrame* pPrevFrame = dynamic_cast<const SwPageFrame*>(pPage->GetPrev());
        if (pPrevFrame)
        {
            // first page of layout may be empty page, but only if it starts with "Left Page" style
            const SwPageDesc* pDesc = pPage->GetPageDesc();
            bRet = pPrevFrame->GetPageDesc() != pDesc;
        }
        else
            bRet = true;
    }
    return bRet;
}

void SwFrame::Calc(vcl::RenderContext* pRenderContext) const
{
    if ( !isFrameAreaPositionValid() || !isFramePrintAreaValid() || !isFrameAreaSizeValid() )
    {
        const_cast<SwFrame*>(this)->PrepareMake(pRenderContext);
    }
}

Point SwFrame::GetRelPos() const
{
    Point aRet( getFrameArea().Pos() );
    // here we cast since SwLayoutFrame is declared only as forwarded
    aRet -= GetUpper()->getFramePrintArea().Pos();
    aRet -= GetUpper()->getFrameArea().Pos();
    return aRet;
}

static const SwFrame* lcl_FindStartOfVirtualPages(const SwPageFrame *pPage)
{
    const SwPageFrame* pPageFrameIter = pPage;
    while (pPageFrameIter)
    {
        const SwContentFrame* pContentFrame = pPageFrameIter->FindFirstBodyContent();
        if (pContentFrame)
        {
            const SwFormatPageDesc& rFormatPageDesc = pContentFrame->GetPageDescItem();

            if ( rFormatPageDesc.GetNumOffset() && rFormatPageDesc.GetDefinedIn() )
            {
                const sw::BroadcastingModify* pMod = rFormatPageDesc.GetDefinedIn();
                sw::VirtPageNumHint aHint(pPage);
                pMod->CallSwClientNotify(aHint);
                if(aHint.GetPage())
                {
                    return aHint.GetFrame();
                }
            }
        }
        pPageFrameIter = static_cast<const SwPageFrame*>(pPageFrameIter->GetPrev());
    }
    return nullptr;
}
/** @return the virtual page number with the offset. */
sal_uInt16 SwFrame::GetVirtPageNum() const
{
    const SwPageFrame *pPage = FindPageFrame();
    if ( !pPage || !pPage->GetUpper() )
        return 0;

    sal_uInt16 nPhyPage = pPage->GetPhyPageNum();
    const SwRootFrame* pRootFrame = static_cast<const SwRootFrame*>(pPage->GetUpper());
    if ( !pRootFrame->IsVirtPageNum() )
        return nPhyPage;

    //Search the nearest section using the virtual page number.
    const SwFrame *pFoundFrame = lcl_FindStartOfVirtualPages(pPage);
    if ( pFoundFrame )
    {
        ::std::optional<sal_uInt16> oNumOffset = pFoundFrame->GetPageDescItem().GetNumOffset();
        if (oNumOffset)
        {
            return nPhyPage - pFoundFrame->GetPhyPageNum() + *oNumOffset;
        }
        else
        {
            return nPhyPage - pFoundFrame->GetPhyPageNum();
        }
    }
    return nPhyPage;
}

sal_uInt16 SwFrame::GetVirtPageCount() const
{
    const SwPageFrame *pPage = FindPageFrame();
    if ( !pPage || !pPage->GetUpper() )
        return 0;

    const SwRootFrame* pRootFrame = static_cast<const SwRootFrame*>(pPage->GetUpper());
    if ( !pRootFrame->IsVirtPageNum() )
        return pRootFrame->GetPageNum();

    //Search the nearest section using the virtual page number and the nearest
    //follower without a virtual page number.
    const SwFrame *pFoundFrame = lcl_FindStartOfVirtualPages(pPage);
    const SwPageFrame* pEndPage = pPage;
    const SwPageFrame* pPageFrameIter = static_cast<const SwPageFrame*>(pPage->GetNext());;
    while (pPageFrameIter)
    {
        const SwContentFrame* pContentFrame = pPageFrameIter->FindFirstBodyContent();
        if (pContentFrame)
        {
            const SwFormatPageDesc& rFormatPageDesc = pContentFrame->GetPageDescItem();

            if ( rFormatPageDesc.GetNumOffset() && rFormatPageDesc.GetDefinedIn() )
                break;
            else
                pEndPage = pPageFrameIter;
        }
        pPageFrameIter = static_cast<const SwPageFrame*>(pPageFrameIter->GetNext());
    }
    sal_uInt16 nStartPage = pFoundFrame ? pFoundFrame->GetPhyPageNum() : 1;
    return pEndPage->GetPhyPageNum() - nStartPage + 1;
}

/** Determines and sets those cells which are enclosed by the selection. */
bool SwRootFrame::MakeTableCursors( SwTableCursor& rTableCursor )
{
    //Find Union-Rects and tables (Follows) of the selection.
    OSL_ENSURE( rTableCursor.GetPointContentNode() && rTableCursor.GetMarkContentNode(),
            "Tabselection not on Cnt." );

    bool bRet = false;

    // For new table models there's no need to ask the layout...
    if( rTableCursor.NewTableSelection() )
        return true;

    Point aPtPt, aMkPt;
    {
        SwShellCursor* pShCursor = dynamic_cast<SwShellCursor*>(&rTableCursor);

        if( pShCursor )
        {
            aPtPt = pShCursor->GetPtPos();
            aMkPt = pShCursor->GetMkPos();
        }
    }

    // #151012# Made code robust here
    const SwContentNode* pTmpStartNode = rTableCursor.GetPointContentNode();
    const SwContentNode* pTmpEndNode   = rTableCursor.GetMarkContentNode();

    std::pair<Point, bool> tmp(aPtPt, false);
    const SwFrame *const pTmpStartFrame = pTmpStartNode ? pTmpStartNode->getLayoutFrame(this, nullptr, &tmp) : nullptr;
    tmp.first = aMkPt;
    const SwFrame *const pTmpEndFrame = pTmpEndNode ? pTmpEndNode->getLayoutFrame(this, nullptr, &tmp) : nullptr;

    const SwLayoutFrame* pStart = pTmpStartFrame ? pTmpStartFrame->GetUpper() : nullptr;
    const SwLayoutFrame* pEnd   = pTmpEndFrame   ? pTmpEndFrame->GetUpper() : nullptr;

    OSL_ENSURE( pStart && pEnd, "MakeTableCursors: Good to have the code robust here!" );

    /* #109590# Only change table boxes if the frames are
        valid. Needed because otherwise the table cursor after moving
        table cells by dnd resulted in an empty tables cursor.  */
    if ( pStart && pEnd && pStart->isFrameAreaDefinitionValid() && pEnd->isFrameAreaDefinitionValid())
    {
        SwSelUnions aUnions;
        ::MakeSelUnions( aUnions, pStart, pEnd );

        SwSelBoxes aNew;

        const bool bReadOnlyAvailable = rTableCursor.IsReadOnlyAvailable();

        for (SwSelUnion & rUnion : aUnions)
        {
            const SwTabFrame *pTable = rUnion.GetTable();

            // Skip any repeated headlines in the follow:
            const SwLayoutFrame* pRow = pTable->IsFollow() ?
                                pTable->GetFirstNonHeadlineRow() :
                                static_cast<const SwLayoutFrame*>(pTable->Lower());

            while ( pRow )
            {
                if ( pRow->getFrameArea().Overlaps( rUnion.GetUnion() ) )
                {
                    const SwLayoutFrame *pCell = pRow->FirstCell();

                    while ( pCell && pRow->IsAnLower( pCell ) )
                    {
                        OSL_ENSURE( pCell->IsCellFrame(), "Frame without cell" );
                        if( IsFrameInTableSel( rUnion.GetUnion(), pCell ) &&
                            (bReadOnlyAvailable ||
                             !pCell->GetFormat()->GetProtect().IsContentProtected()))
                        {
                            SwTableBox* pInsBox = const_cast<SwTableBox*>(
                                static_cast<const SwCellFrame*>(pCell)->GetTabBox());
                            aNew.insert( pInsBox );
                        }
                        if ( pCell->GetNext() )
                        {
                            pCell = static_cast<const SwLayoutFrame*>(pCell->GetNext());
                            const SwFrame* pLower = pCell->Lower();
                            if ( pLower && pLower->IsRowFrame() )
                                pCell = pCell->FirstCell();
                        }
                        else
                        {
                            const SwLayoutFrame* pLastCell = pCell;
                            do
                            {
                                pCell = pCell->GetNextLayoutLeaf();
                            } while ( pCell && pLastCell->IsAnLower( pCell ) );
                            // For sections with columns
                            if( pCell && pCell->IsInTab() )
                            {
                                while( !pCell->IsCellFrame() )
                                {
                                    pCell = pCell->GetUpper();
                                    assert(pCell && "Where's my cell?");
                                }
                            }
                        }
                    }
                }
                pRow = static_cast<const SwLayoutFrame*>(pRow->GetNext());
            }
        }

        rTableCursor.ActualizeSelection( aNew );
        bRet = true;
    }

    return bRet;
}

static void Sub( SwRegionRects& rRegion, const SwRect& rRect )
{
    if( rRect.Width() > 1 && rRect.Height() > 1 &&
        rRect.Overlaps( rRegion.GetOrigin() ))
        rRegion -= rRect;
}

static void Add( SwRegionRects& rRegion, const SwRect& rRect )
{
    if( rRect.Width() > 1 && rRect.Height() > 1 )
        rRegion += rRect;
}

/*
 * The following situations can happen (simplified version, before
 * CJK/CTL features were added):
 *  1. Start and end in same line of text and in the same frame
 *     -> one rectangle out of start and end; and we're okay
 *  2. Start and end in same frame
 *     -> expand start to the right, end to the left and if more than two
 *        lines of text are involved - calculate the in-between area
 *  3. Start and end in different frames
 *     -> expand start to the right until frame-end, calculate Rect
 *        expand end to the left until frame-start, calculate Rect
 *        and if more than two frames are involved add the PrtArea of all
 *        frames which lie in between
 *
 * Big reorganization because of FlyFrame - those need to be excluded.
 * Exceptions:  - The Fly in which the selection took place (if it took place
 *                 in a Fly)
 *              - The Flys which are below the text (in z-order)
 *              - The Flys which are anchored to somewhere inside the selection.
 * Functioning: First a SwRegion with a root gets initialized.
 *              Out of the region the selected areas are cut out. The
 *              section gets compressed and finally inverted and thereby the
 *              rectangles are available for highlighting.
 *              In the end the Flys are cut out of the region.
 */
void SwRootFrame::CalcFrameRects(SwShellCursor const& rCursor, SwRects & rRects, RectsMode const eMode)
{
    auto [pStartPos, pEndPos] = rCursor.StartEnd(); // SwPosition*

    SwViewShell *pSh = GetCurrShell();

    bool bIgnoreVisArea = true;
    if (pSh)
        bIgnoreVisArea = pSh->GetViewOptions()->IsPDFExport() || comphelper::LibreOfficeKit::isActive();

    SwRegionRects aRegion( !bIgnoreVisArea ?
                           pSh->VisArea() :
                           getFrameArea() );
    if( !pStartPos->GetNode().IsContentNode() ||
        !pStartPos->GetNode().GetContentNode()->getLayoutFrame(this) ||
        ( pStartPos->GetNode() != pEndPos->GetNode() &&
          ( !pEndPos->GetNode().IsContentNode() ||
            !pEndPos->GetNode().GetContentNode()->getLayoutFrame(this) ) ) )
    {
        return;
    }

    DisableCallbackAction a(*this); // the GetCharRect below may format

    //First obtain the ContentFrames for the start and the end - those are needed
    //anyway.
    std::pair<Point, bool> tmp(rCursor.GetSttPos(), true);
    SwContentFrame* pStartFrame = pStartPos->GetNode().
        GetContentNode()->getLayoutFrame(this, pStartPos, &tmp);

    tmp.first = rCursor.GetEndPos();
    SwContentFrame* pEndFrame   = pEndPos->GetNode().
        GetContentNode()->getLayoutFrame(this, pEndPos, &tmp);

    assert(pStartFrame && pEndFrame && "No ContentFrames found.");
    //tdf#119224 start and end are expected to exist for the scope of this function
    SwFrameDeleteGuard aStartFrameGuard(pStartFrame), aEndFrameGuard(pEndFrame);

    // Do not subtract FlyFrames that contain selected Frames.
    SwSortedObjs aSortObjs;
    if ( pStartFrame->IsInFly() )
    {
        SwAnchoredObject* pObj = pStartFrame->FindFlyFrame();
        OSL_ENSURE( pObj, "No Start Object." );
        if (pObj) aSortObjs.Insert( *pObj );
        SwAnchoredObject* pObj2 = pEndFrame->FindFlyFrame();
        OSL_ENSURE( pObj2, "SwRootFrame::CalcFrameRects(..) - FlyFrame missing - looks like an invalid selection" );
        if ( pObj2 != nullptr && pObj2 != pObj )
        {
            aSortObjs.Insert(*pObj2);
        }
    }

    // if a selection which is not allowed exists, we correct what is not
    // allowed (header/footer/table-headline start/end on different pages).
    do {    // middle check loop
        const SwLayoutFrame* pSttLFrame = pStartFrame->GetUpper();
        const SwFrameType cHdFtTableHd = SwFrameType::Header | SwFrameType::Footer | SwFrameType::Tab;
        while( pSttLFrame &&
            ! (cHdFtTableHd & pSttLFrame->GetType() ))
            pSttLFrame = pSttLFrame->GetUpper();
        if( !pSttLFrame )
            break;
        const SwLayoutFrame* pEndLFrame = pEndFrame->GetUpper();
        while( pEndLFrame &&
            ! (cHdFtTableHd & pEndLFrame->GetType() ))
            pEndLFrame = pEndLFrame->GetUpper();
        if( !pEndLFrame )
            break;

        OSL_ENSURE( pEndLFrame->GetType() == pSttLFrame->GetType(),
            "Selection over different content" );
        switch( pSttLFrame->GetType() )
        {
        case SwFrameType::Header:
        case SwFrameType::Footer:
            // On different pages? Then always on the start-page
            if( pEndLFrame->FindPageFrame() != pSttLFrame->FindPageFrame() )
            {
                // Set end- to the start-ContentFrame
                if( pStartPos == rCursor.GetPoint() )
                    pEndFrame = pStartFrame;
                else
                    pStartFrame = pEndFrame;
            }
            break;
        case SwFrameType::Tab:
            // On different pages? Then check for table-headline
            {
                const SwTabFrame* pTabFrame = static_cast<const SwTabFrame*>(pSttLFrame);
                if( ( pTabFrame->GetFollow() ||
                    static_cast<const SwTabFrame*>(pEndLFrame)->GetFollow() ) &&
                    pTabFrame->GetTable()->GetRowsToRepeat() > 0 &&
                    pTabFrame->GetLower() != static_cast<const SwTabFrame*>(pEndLFrame)->GetLower() &&
                    ( lcl_IsInRepeatedHeadline( pStartFrame ) ||
                    lcl_IsInRepeatedHeadline( pEndFrame ) ) )
                {
                    // Set end- to the start-ContentFrame
                    if( pStartPos == rCursor.GetPoint() )
                        pEndFrame = pStartFrame;
                    else
                        pStartFrame = pEndFrame;
                }
            }
            break;
        default: break;
        }
    } while( false );

    SwCursorMoveState aTmpState( CursorMoveState::NONE );
    aTmpState.m_b2Lines = true;
    aTmpState.m_bNoScroll = true;
    aTmpState.m_nCursorBidiLevel = pStartFrame->IsRightToLeft() ? 1 : 0;

    //ContentRects to Start- and EndFrames.
    SwRect aStRect, aEndRect;
    pStartFrame->GetCharRect( aStRect, *pStartPos, &aTmpState );
    std::unique_ptr<Sw2LinesPos> pSt2Pos = std::move(aTmpState.m_p2Lines);
    aTmpState.m_nCursorBidiLevel = pEndFrame->IsRightToLeft() ? 1 : 0;

    pEndFrame->GetCharRect( aEndRect, *pEndPos, &aTmpState );
    std::unique_ptr<Sw2LinesPos> pEnd2Pos = std::move(aTmpState.m_p2Lines);

    SwRect aStFrame ( pStartFrame->UnionFrame( true ) );
    aStFrame.Intersection( pStartFrame->GetPaintArea() );
    SwRect aEndFrame( pStartFrame == pEndFrame ? aStFrame : pEndFrame->UnionFrame( true ) );
    if( pStartFrame != pEndFrame )
    {
        aEndFrame.Intersection( pEndFrame->GetPaintArea() );
    }
    SwRectFnSet aRectFnSet(pStartFrame);
    const bool bR2L = pStartFrame->IsRightToLeft();
    const bool bEndR2L = pEndFrame->IsRightToLeft();
    const bool bB2T = pStartFrame->IsVertLRBT();

    // If there's no doubleline portion involved or start and end are both
    // in the same doubleline portion, all works fine, but otherwise
    // we need the following...
    if( pSt2Pos != pEnd2Pos && ( !pSt2Pos || !pEnd2Pos ||
        pSt2Pos->aPortion != pEnd2Pos->aPortion ) )
    {
        // If we have a start(end) position inside a doubleline portion
        // the surrounded part of the doubleline portion is subtracted
        // from the region and the aStRect(aEndRect) is set to the
        // end(start) of the doubleline portion.
        if( pSt2Pos )
        {
            SwRect aTmp( aStRect );

            // BiDi-Portions are swimming against the current.
            const bool bPorR2L = ( MultiPortionType::BIDI == pSt2Pos->nMultiType ) ?
                ! bR2L :
            bR2L;

            if( MultiPortionType::BIDI == pSt2Pos->nMultiType &&
                aRectFnSet.GetWidth(pSt2Pos->aPortion2) )
            {
                // nested bidi portion
                tools::Long nRightAbs = aRectFnSet.GetRight(pSt2Pos->aPortion);
                nRightAbs -= aRectFnSet.GetLeft(pSt2Pos->aPortion2);
                tools::Long nLeftAbs = nRightAbs - aRectFnSet.GetWidth(pSt2Pos->aPortion2);

                aRectFnSet.SetRight( aTmp, nRightAbs );

                if ( ! pEnd2Pos || pEnd2Pos->aPortion != pSt2Pos->aPortion )
                {
                    SwRect aTmp2( pSt2Pos->aPortion );
                    aRectFnSet.SetRight( aTmp2, nLeftAbs );
                    aTmp2.Intersection( aEndFrame );
                    Sub( aRegion, aTmp2 );
                }
            }
            else
            {
                if( bPorR2L )
                    aRectFnSet.SetLeft( aTmp, aRectFnSet.GetLeft(pSt2Pos->aPortion) );
                else
                    aRectFnSet.SetRight( aTmp, aRectFnSet.GetRight(pSt2Pos->aPortion) );
            }

            if( MultiPortionType::ROT_90 == pSt2Pos->nMultiType ||
                aRectFnSet.GetTop(pSt2Pos->aPortion) ==
                aRectFnSet.GetTop(aTmp) )
            {
                aRectFnSet.SetTop( aTmp, aRectFnSet.GetTop(pSt2Pos->aLine) );
            }

            aTmp.Intersection( aStFrame );
            Sub( aRegion, aTmp );

            SwTwips nTmp = aRectFnSet.GetBottom(pSt2Pos->aLine);
            if( MultiPortionType::ROT_90 != pSt2Pos->nMultiType &&
                aRectFnSet.BottomDist( aStRect, nTmp ) > 0 )
            {
                aRectFnSet.SetTop( aTmp, aRectFnSet.GetBottom(aTmp) );
                aRectFnSet.SetBottom( aTmp, nTmp );
                if( aRectFnSet.BottomDist( aStRect, aRectFnSet.GetBottom(pSt2Pos->aPortion) ) > 0 )
                {
                    if( bPorR2L )
                        aRectFnSet.SetRight( aTmp, aRectFnSet.GetRight(pSt2Pos->aPortion) );
                    else
                        aRectFnSet.SetLeft( aTmp, aRectFnSet.GetLeft(pSt2Pos->aPortion) );
                }
                aTmp.Intersection( aStFrame );
                Sub( aRegion, aTmp );
            }

            aStRect = pSt2Pos->aLine;
            aRectFnSet.SetLeft( aStRect, bR2L ?
                    aRectFnSet.GetLeft(pSt2Pos->aPortion) :
                    aRectFnSet.GetRight(pSt2Pos->aPortion) );
            aRectFnSet.SetWidth( aStRect, 1 );
        }

        if( pEnd2Pos )
        {
            SwRectFnSet fnRectX(pEndFrame);
            SwRect aTmp( aEndRect );

            // BiDi-Portions are swimming against the current.
            const bool bPorR2L = ( MultiPortionType::BIDI == pEnd2Pos->nMultiType ) ?
                                       ! bEndR2L :
                                         bEndR2L;

            if( MultiPortionType::BIDI == pEnd2Pos->nMultiType &&
                fnRectX.GetWidth(pEnd2Pos->aPortion2) )
            {
                // nested bidi portion
                tools::Long nRightAbs = fnRectX.GetRight(pEnd2Pos->aPortion);
                nRightAbs = nRightAbs - fnRectX.GetLeft(pEnd2Pos->aPortion2);
                tools::Long nLeftAbs = nRightAbs - fnRectX.GetWidth(pEnd2Pos->aPortion2);

                fnRectX.SetLeft( aTmp, nLeftAbs );

                if ( ! pSt2Pos || pSt2Pos->aPortion != pEnd2Pos->aPortion )
                {
                    SwRect aTmp2( pEnd2Pos->aPortion );
                    fnRectX.SetLeft( aTmp2, nRightAbs );
                    aTmp2.Intersection( aEndFrame );
                    Sub( aRegion, aTmp2 );
                }
            }
            else
            {
                if ( bPorR2L )
                    fnRectX.SetRight( aTmp, fnRectX.GetRight(pEnd2Pos->aPortion) );
                else
                    fnRectX.SetLeft( aTmp, fnRectX.GetLeft(pEnd2Pos->aPortion) );
            }

            if( MultiPortionType::ROT_90 == pEnd2Pos->nMultiType ||
                fnRectX.GetBottom(pEnd2Pos->aPortion) ==
                fnRectX.GetBottom(aEndRect) )
            {
                fnRectX.SetBottom( aTmp, fnRectX.GetBottom(pEnd2Pos->aLine) );
            }

            aTmp.Intersection( aEndFrame );
            Sub( aRegion, aTmp );

            // The next statement means neither ruby nor rotate(90):
            if( MultiPortionType::RUBY != pEnd2Pos->nMultiType && MultiPortionType::ROT_90 != pEnd2Pos->nMultiType )
            {
                SwTwips nTmp = fnRectX.GetTop(pEnd2Pos->aLine);
                if( fnRectX.GetTop(aEndRect) != nTmp )
                {
                    fnRectX.SetBottom( aTmp, fnRectX.GetTop(aTmp) );
                    fnRectX.SetTop( aTmp, nTmp );
                    if( fnRectX.GetTop(aEndRect) !=
                        fnRectX.GetTop(pEnd2Pos->aPortion) )
                    {
                        if( bPorR2L )
                            fnRectX.SetLeft( aTmp, fnRectX.GetLeft(pEnd2Pos->aPortion) );
                        else
                            fnRectX.SetRight( aTmp, fnRectX.GetRight(pEnd2Pos->aPortion) );
                    }
                    aTmp.Intersection( aEndFrame );
                    Sub( aRegion, aTmp );
                }
            }

            aEndRect = pEnd2Pos->aLine;
            fnRectX.SetLeft( aEndRect, bEndR2L ?
                    fnRectX.GetRight(pEnd2Pos->aPortion) :
                    fnRectX.GetLeft(pEnd2Pos->aPortion) );
            fnRectX.SetWidth( aEndRect, 1 );
        }
    }
    else if( pSt2Pos && pEnd2Pos &&
             MultiPortionType::BIDI == pSt2Pos->nMultiType &&
             MultiPortionType::BIDI == pEnd2Pos->nMultiType &&
             pSt2Pos->aPortion == pEnd2Pos->aPortion &&
             pSt2Pos->aPortion2 != pEnd2Pos->aPortion2 )
    {
        // This is the ugly special case, where the selection starts and
        // ends in the same bidi portion but one start or end is inside a
        // nested bidi portion.

        if ( aRectFnSet.GetWidth(pSt2Pos->aPortion2) )
        {
            SwRect aTmp( aStRect );
            tools::Long nRightAbs = aRectFnSet.GetRight(pSt2Pos->aPortion);
            nRightAbs -= aRectFnSet.GetLeft(pSt2Pos->aPortion2);
            tools::Long nLeftAbs = nRightAbs - aRectFnSet.GetWidth(pSt2Pos->aPortion2);

            aRectFnSet.SetRight( aTmp, nRightAbs );
            aTmp.Intersection( aStFrame );
            Sub( aRegion, aTmp );

            aStRect = pSt2Pos->aLine;
            aRectFnSet.SetLeft( aStRect, bR2L ? nRightAbs : nLeftAbs );
            aRectFnSet.SetWidth( aStRect, 1 );
        }

        SwRectFnSet fnRectX(pEndFrame);
        if ( fnRectX.GetWidth(pEnd2Pos->aPortion2) )
        {
            SwRect aTmp( aEndRect );
            tools::Long nRightAbs = fnRectX.GetRight(pEnd2Pos->aPortion);
            nRightAbs -= fnRectX.GetLeft(pEnd2Pos->aPortion2);
            tools::Long nLeftAbs = nRightAbs - fnRectX.GetWidth(pEnd2Pos->aPortion2);

            fnRectX.SetLeft( aTmp, nLeftAbs );
            aTmp.Intersection( aEndFrame );
            Sub( aRegion, aTmp );

            aEndRect = pEnd2Pos->aLine;
            fnRectX.SetLeft( aEndRect, bEndR2L ? nLeftAbs : nRightAbs );
            fnRectX.SetWidth( aEndRect, 1 );
        }
    }

    // The charrect may be outside the paintarea (for cursortravelling)
    // but the selection has to be restricted to the paintarea
    if( aStRect.Left() < aStFrame.Left() )
        aStRect.Left( aStFrame.Left() );
    else if( aStRect.Left() > aStFrame.Right() )
        aStRect.Left( aStFrame.Right() );
    SwTwips nTmp = aStRect.Right();
    if( nTmp < aStFrame.Left() )
        aStRect.Right( aStFrame.Left() );
    else if( nTmp > aStFrame.Right() )
        aStRect.Right( aStFrame.Right() );
    if( aEndRect.Left() < aEndFrame.Left() )
        aEndRect.Left( aEndFrame.Left() );
    else if( aEndRect.Left() > aEndFrame.Right() )
        aEndRect.Left( aEndFrame.Right() );
    nTmp = aEndRect.Right();
    if( nTmp < aEndFrame.Left() )
        aEndRect.Right( aEndFrame.Left() );
    else if( nTmp > aEndFrame.Right() )
        aEndRect.Right( aEndFrame.Right() );

    if( pStartFrame == pEndFrame )
    {
        bool bSameRotatedOrBidi = pSt2Pos && pEnd2Pos &&
            ( MultiPortionType::BIDI == pSt2Pos->nMultiType  ||
              MultiPortionType::ROT_270 == pSt2Pos->nMultiType ||
              MultiPortionType::ROT_90  == pSt2Pos->nMultiType ) &&
            pSt2Pos->aPortion == pEnd2Pos->aPortion;
        // case 1: (Same frame and same line)
        if( bSameRotatedOrBidi ||
            aRectFnSet.GetTop(aStRect) == aRectFnSet.GetTop(aEndRect) )
        {
            Point aTmpSt( aStRect.Pos() );
            Point aTmpEnd( aEndRect.Right(), aEndRect.Bottom() );
            if (bSameRotatedOrBidi || bR2L || bB2T)
            {
                if( aTmpSt.Y() > aTmpEnd.Y() )
                {
                    tools::Long nTmpY = aTmpEnd.Y();
                    aTmpEnd.setY( aTmpSt.Y() );
                    aTmpSt.setY( nTmpY );
                }
                if( aTmpSt.X() > aTmpEnd.X() )
                {
                    tools::Long nTmpX = aTmpEnd.X();
                    aTmpEnd.setX( aTmpSt.X() );
                    aTmpSt.setX( nTmpX );
                }
            }

            SwRect aTmp( aTmpSt, aTmpEnd );
            // If content is selected which doesn't take space (e.g. PostIts,
            // RefMarks, TOXMarks), then at least set the width of the Cursor.
            if( 1 == aRectFnSet.GetWidth(aTmp) &&
                pStartPos->GetContentIndex() !=
                pEndPos->GetContentIndex() )
            {
                OutputDevice* pOut = pSh->GetOut();
                tools::Long nCursorWidth = pOut->GetSettings().GetStyleSettings().
                    GetCursorSize();
                aRectFnSet.SetWidth( aTmp, pOut->PixelToLogic(
                    Size( nCursorWidth, 0 ) ).Width() );
            }
            aTmp.Intersection( aStFrame );
            Sub( aRegion, aTmp );
        }
        //case 2: (Same frame, but not the same line)
        else
        {
            SwTwips lLeft, lRight;
            if( pSt2Pos && pEnd2Pos && pSt2Pos->aPortion == pEnd2Pos->aPortion )
            {
                lLeft = aRectFnSet.GetLeft(pSt2Pos->aPortion);
                lRight = aRectFnSet.GetRight(pSt2Pos->aPortion);
            }
            else
            {
                lLeft = aRectFnSet.GetLeft(pStartFrame->getFrameArea()) +
                    aRectFnSet.GetLeft(pStartFrame->getFramePrintArea());
                lRight = aRectFnSet.GetRight(aEndFrame);
            }
            if( lLeft < aRectFnSet.GetLeft(aStFrame) )
                lLeft = aRectFnSet.GetLeft(aStFrame);
            if( lRight > aRectFnSet.GetRight(aStFrame) )
                lRight = aRectFnSet.GetRight(aStFrame);
            SwRect aSubRect( aStRect );
            //First line
            if( bR2L )
                aRectFnSet.SetLeft( aSubRect, lLeft );
            else
                aRectFnSet.SetRight( aSubRect, lRight );
            Sub( aRegion, aSubRect );

            //If there's at least a twips between start- and endline,
            //the whole area between will be added.
            SwTwips aTmpBottom = aRectFnSet.GetBottom(aStRect);
            SwTwips aTmpTop = aRectFnSet.GetTop(aEndRect);
            if( aTmpBottom != aTmpTop )
            {
                aRectFnSet.SetLeft( aSubRect, lLeft );
                aRectFnSet.SetRight( aSubRect, lRight );
                aRectFnSet.SetTop( aSubRect, aTmpBottom );
                aRectFnSet.SetBottom( aSubRect, aTmpTop );
                Sub( aRegion, aSubRect );
            }
            //and the last line
            aSubRect = aEndRect;
            if( bR2L )
                aRectFnSet.SetRight( aSubRect, lRight );
            else
                aRectFnSet.SetLeft( aSubRect, lLeft );
            Sub( aRegion, aSubRect );
        }
    }
    //case 3: (Different frames, maybe with other frames between)
    else
    {
        //The startframe first...
        SwRect aSubRect( aStRect );
        if( bR2L )
            aRectFnSet.SetLeft( aSubRect, aRectFnSet.GetLeft(aStFrame));
        else
            aRectFnSet.SetRight( aSubRect, aRectFnSet.GetRight(aStFrame));
        Sub( aRegion, aSubRect );
        SwTwips nTmpTwips = aRectFnSet.GetBottom(aStRect);
        if( aRectFnSet.GetBottom(aStFrame) != nTmpTwips )
        {
            aSubRect = aStFrame;
            aRectFnSet.SetTop( aSubRect, nTmpTwips );
            Sub( aRegion, aSubRect );
        }

        //Now the frames between, if there are any
        bool const bBody = pStartFrame->IsInDocBody();
        const SwTableBox* pCellBox = pStartFrame->GetUpper()->IsCellFrame() ?
            static_cast<const SwCellFrame*>(pStartFrame->GetUpper())->GetTabBox() : nullptr;
        assert(pSh);
        if (pSh->IsSelectAll())
            pCellBox = nullptr;

        const SwContentFrame *pContent = pStartFrame->GetNextContentFrame();
        SwRect aPrvRect;

        OSL_ENSURE( pContent,
            "<SwRootFrame::CalcFrameRects(..)> - no content frame. This is a serious defect" );
        while ( pContent && pContent != pEndFrame )
        {
            if ( pContent->IsInFly() )
            {
                const SwAnchoredObject* pObj = pContent->FindFlyFrame();
                if (!aSortObjs.Contains(*pObj))
                {   // is this even possible, assuming valid cursor pos.?
                    aSortObjs.Insert( *const_cast<SwAnchoredObject*>(pObj) );
                }
            }

            // Consider only frames which have the same IsInDocBody value like pStartFrame
            // If pStartFrame is inside a SwCellFrame, consider only frames which are inside the
            // same cell frame (or its follow cell)
            const SwTableBox* pTmpCellBox = pContent->GetUpper()->IsCellFrame() ?
                static_cast<const SwCellFrame*>(pContent->GetUpper())->GetTabBox() : nullptr;
            if (pSh->IsSelectAll())
                pTmpCellBox = nullptr;
            if ( bBody == pContent->IsInDocBody() &&
                ( !pCellBox || pCellBox == pTmpCellBox ) )
            {
                SwRect aCRect( pContent->UnionFrame( true ) );
                aCRect.Intersection( pContent->GetPaintArea() );
                if( aCRect.Overlaps( aRegion.GetOrigin() ))
                {
                    SwRect aTmp( aPrvRect );
                    aTmp.Union( aCRect );
                    if ( (aPrvRect.Height() * aPrvRect.Width() +
                        aCRect.Height()   * aCRect.Width()) ==
                        (aTmp.Height() * aTmp.Width()) )
                    {
                        aPrvRect.Union( aCRect );
                    }
                    else
                    {
                        if ( aPrvRect.HasArea() )
                            Sub( aRegion, aPrvRect );
                        aPrvRect = aCRect;
                    }
                }
            }
            pContent = pContent->GetNextContentFrame();
            OSL_ENSURE( pContent,
                "<SwRootFrame::CalcFrameRects(..)> - no content frame. This is a serious defect!" );
        }
        if ( aPrvRect.HasArea() )
            Sub( aRegion, aPrvRect );

        // At last the endframe...
        aRectFnSet.Refresh(pEndFrame);
        nTmpTwips = aRectFnSet.GetTop(aEndRect);
        if( aRectFnSet.GetTop(aEndFrame) != nTmpTwips )
        {
            aSubRect = aEndFrame;
            aRectFnSet.SetBottom( aSubRect, nTmpTwips );
            Sub( aRegion, aSubRect );
        }
        aSubRect = aEndRect;
        if( bEndR2L )
            aRectFnSet.SetRight(aSubRect, aRectFnSet.GetRight(aEndFrame));
        else
            aRectFnSet.SetLeft( aSubRect, aRectFnSet.GetLeft(aEndFrame) );
        Sub( aRegion, aSubRect );
    }

    aRegion.Invert();
    pSt2Pos.reset();
    pEnd2Pos.reset();

    // Cut out Flys in the foreground. We don't cut out a Fly when:
    // - it's a Lower of StartFrame/EndFrame (FLY_AS_CHAR and all other Flys
    //   which sit in it recursively)
    // - it's lower in the Z-order than the fly that contains the StartFrame
    //   (i.e. the one with the StartFrame is painted on top of it)
    // - it's anchored to inside the selection and thus part of it
    const SwPageFrame *pPage      = pStartFrame->FindPageFrame();
    const SwPageFrame *pEndPage   = pEndFrame->FindPageFrame();

    // for link rectangles: just remove all the fly portions - this prevents
    // splitting of portions vertically (causes spurious extra PDF annotations)
    if (eMode == RectsMode::NoAnchoredFlys)
    {
        for (SwContentFrame * pFrame = pStartFrame; ; pFrame = pFrame->GetFollow())
        {
            assert(pFrame->IsTextFrame());
            SwTextGridItem const*const pGrid(GetGridItem(pFrame->FindPageFrame()));
            SwTextPaintInfo info(static_cast<SwTextFrame*>(pFrame), pFrame->FindPageFrame()->getFrameArea());
            SwTextPainter painter(static_cast<SwTextFrame*>(pFrame), &info);
            // because nothing outside the start/end has been added, it doesn't
            // matter to match exactly the start/end, subtracting outside is no-op
            if (pFrame == pStartFrame)
            {
                painter.CharToLine(static_cast<SwTextFrame*>(pFrame)->MapModelToViewPos(*pStartPos));
            }
            do
            {
                info.SetPos(painter.GetTopLeft());
                bool const bAdjustBaseLine(
                    painter.GetLineInfo().HasSpecialAlign(pFrame->IsVertical())
                    || nullptr != pGrid || painter.GetCurr()->GetHangingBaseline());
                SwTwips nAscent, nHeight;
                painter.CalcAscentAndHeight(nAscent, nHeight);
                SwTwips const nOldY(info.Y());
                for (SwLinePortion const* pLP = painter.GetCurr()->GetFirstPortion();
                        pLP; pLP = pLP->GetNextPortion())
                {
                    if (pLP->IsFlyPortion())
                    {
                        info.Y(info.Y() + (bAdjustBaseLine
                                ? painter.AdjustBaseLine(*painter.GetCurr(), pLP)
                                : nAscent));
                        SwRect flyPortion;
                        info.CalcRect(*pLP, &flyPortion);
                        Sub(aRegion, flyPortion);
                        info.Y(nOldY);
                    }
                    pLP->Move(info);
                }
            }
            while (painter.Next());
            if (pFrame == pEndFrame)
            {
                break;
            }
        }
    }
    else while (pPage)
    {
        if ( pPage->GetSortedObjs() )
        {
            const SwSortedObjs &rObjs = *pPage->GetSortedObjs();
            for (SwAnchoredObject* pAnchoredObj : rObjs)
            {
                const SwFlyFrame* pFly = pAnchoredObj->DynCastFlyFrame();
                SdrObject const*const pObj(pAnchoredObj->GetDrawObj());
                SwFormatSurround const& rSur(pAnchoredObj->GetFrameFormat()->GetSurround());
                SwFormatAnchor const& rAnchor(pAnchoredObj->GetFrameFormat()->GetAnchor());
                const SwPosition* anchoredAt = rAnchor.GetContentAnchor();
                bool inSelection = (
                            anchoredAt != nullptr
                        && (   (rAnchor.GetAnchorId() == RndStdIds::FLY_AT_CHAR
                                && IsDestroyFrameAnchoredAtChar(*anchoredAt, *pStartPos, *pEndPos))
                            || (rAnchor.GetAnchorId() == RndStdIds::FLY_AT_PARA
                                && IsSelectFrameAnchoredAtPara(*anchoredAt, *pStartPos, *pEndPos))));
                if (inSelection)
                {
                    Add(aRegion, pAnchoredObj->GetObjRect());
                }
                else if (pFly && !pFly->IsAnLower(pStartFrame) &&
                    (rSur.GetSurround() != css::text::WrapTextMode_THROUGH &&
                    !rSur.IsContour()) )
                {
                    if ( aSortObjs.Contains( *pAnchoredObj ) )
                        continue;

                    bool bSub = true;
                    const sal_uInt32 nPos = pObj->GetOrdNum();
                    for ( size_t k = 0; bSub && k < aSortObjs.size(); ++k )
                    {
                        assert( dynamic_cast< const SwFlyFrame *>( aSortObjs[k] ) &&
                            "<SwRootFrame::CalcFrameRects(..)> - object in <aSortObjs> of unexpected type" );
                        const SwFlyFrame* pTmp = static_cast<SwFlyFrame*>(aSortObjs[k]);
                        do
                        {
                            if ( nPos < pTmp->GetVirtDrawObj()->GetOrdNumDirect() )
                            {
                                bSub = false;
                            }
                            else
                            {
                                pTmp = pTmp->GetAnchorFrame()->FindFlyFrame();
                            }
                        } while ( bSub && pTmp );
                    }
                    if ( bSub )
                        Sub( aRegion, pFly->getFrameArea() );
                }
            }
        }
        if ( pPage == pEndPage )
            break;
        else
            pPage = static_cast<const SwPageFrame*>(pPage->GetNext());
    }

    // Because it looks better, we cut out the DropCaps.
    SwRect aDropRect;
    if ( pStartFrame->IsTextFrame() )
    {
        if ( static_cast<const SwTextFrame*>(pStartFrame)->GetDropRect( aDropRect ) )
            Sub( aRegion, aDropRect );
    }
    if ( pEndFrame != pStartFrame && pEndFrame->IsTextFrame() )
    {
        if ( static_cast<const SwTextFrame*>(pEndFrame)->GetDropRect( aDropRect ) )
            Sub( aRegion, aDropRect );
    }

    rRects.assign( aRegion.begin(), aRegion.end() );
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
