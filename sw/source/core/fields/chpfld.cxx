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

#include <string_view>

#include <com/sun/star/text/ChapterFormat.hpp>
#include <osl/diagnose.h>
#include <doc.hxx>
#include <frame.hxx>
#include <rootfrm.hxx>
#include <txtfrm.hxx>
#include <pam.hxx>
#include <ndtxt.hxx>
#include <chpfld.hxx>
#include <expfld.hxx>
#include <unofldmid.h>
#include <numrule.hxx>

using namespace ::com::sun::star;

namespace
{

OUString removeControlChars(std::u16string_view sIn)
{
    OUStringBuffer aBuf(sIn);
    aBuf = aBuf.replace('\n', ' ').replace('\t', ' ');
    sal_Int32 nLen = aBuf.getLength();
    for (sal_Int32 i = 0; i < nLen; ++i)
    {
        if (aBuf[i] < ' ')
        {
            sal_Int32 j = i+1;
            while (j<nLen && aBuf[j]<' ') ++j;
            aBuf.remove(i, j-i);
            nLen = aBuf.getLength();
        }
    }
    return aBuf.makeStringAndClear();
}

}

SwChapterFieldType::SwChapterFieldType()
    : SwFieldType( SwFieldIds::Chapter )
{
}

std::unique_ptr<SwFieldType> SwChapterFieldType::Copy() const
{
    return std::make_unique<SwChapterFieldType>();
}

// chapter field

SwChapterField::SwChapterField(SwChapterFieldType* pTyp, SwChapterFormat nFormat)
    : SwField(pTyp),
      m_nFormat(nFormat)
{
}

sal_uInt8 SwChapterField::GetLevel(SwRootFrame const*const pLayout) const
{
    State const& rState(pLayout && pLayout->IsHideRedlines() ? m_StateRLHidden : m_State);
    return rState.nLevel;
}

// this is called from UI or from import filters, so override both states
void SwChapterField::SetLevel(sal_uInt8 nLev)
{
    m_State.nLevel = nLev;
    m_StateRLHidden.nLevel = nLev;
}

const OUString& SwChapterField::GetNumber(SwRootFrame const*const pLayout) const
{
    State const& rState(pLayout && pLayout->IsHideRedlines() ? m_StateRLHidden : m_State);
    return rState.sNumber;
}

const OUString& SwChapterField::GetTitle(SwRootFrame const*const pLayout) const
{
    State const& rState(pLayout && pLayout->IsHideRedlines() ? m_StateRLHidden : m_State);
    return rState.sTitle;
}

OUString SwChapterField::ExpandImpl(SwRootFrame const*const pLayout) const
{
    State const& rState(pLayout && pLayout->IsHideRedlines() ? m_StateRLHidden : m_State);
    switch( GetFormat() )
    {
        case SwChapterFormat::Title:
            return rState.sTitle;
        case SwChapterFormat::Number:
            return rState.sPre + rState.sNumber + rState.sPost;
        case SwChapterFormat::NumberAndTitle:
            return rState.sPre + rState.sNumber + rState.sPost + rState.sLabelFollowedBy + rState.sTitle;
        case SwChapterFormat::NumberNoPrePostAndTitle:
            return rState.sNumber + rState.sLabelFollowedBy + rState.sTitle;
        default: break;
    }
    // CF_NUMBER_NOPREPST
    return rState.sNumber;
}

std::unique_ptr<SwField> SwChapterField::Copy() const
{
    std::unique_ptr<SwChapterField> pTmp(
        new SwChapterField(static_cast<SwChapterFieldType*>(GetTyp()), GetFormat()));
    pTmp->m_State = m_State;
    pTmp->m_StateRLHidden = m_StateRLHidden;

    return std::unique_ptr<SwField>(pTmp.release());
}

// #i53420#
void SwChapterField::ChangeExpansion(const SwFrame & rFrame,
                                      const SwContentNode* pContentNode,
                                      bool bSrchNum )
{
    SwDoc& rDoc = const_cast<SwDoc&>(pContentNode->GetDoc());

    const SwTextNode* pTextNode = pContentNode->GetTextNode();
    if (!pTextNode || !rFrame.IsInDocBody())
    {
        SwPosition aDummyPos( rDoc.GetNodes().GetEndOfContent() );
        pTextNode = GetBodyTextNode( rDoc, aDummyPos, rFrame );
    }

    if ( pTextNode )
    {
        ChangeExpansion( *pTextNode, bSrchNum, rFrame.getRootFrame() );
    }
}

void SwChapterField::ChangeExpansion(const SwTextNode &rTextNd, bool bSrchNum,
        SwRootFrame const*const pLayout)
{
    State & rState(pLayout && pLayout->IsHideRedlines() ? m_StateRLHidden : m_State);
    rState.sNumber.clear();
    rState.sLabelFollowedBy.clear();
    rState.sTitle.clear();
    rState.sPost.clear();
    rState.sPre.clear();

    SwDoc& rDoc = const_cast<SwDoc&>(rTextNd.GetDoc());
    const SwTextNode *pTextNd = rTextNd.FindOutlineNodeOfLevel(rState.nLevel, pLayout);
    if( !pTextNd )
        return;

    if( bSrchNum )
    {
        const SwTextNode* pONd = pTextNd;
        do {
            if( pONd && pONd->GetTextColl() )
            {
                sal_uInt8 nPrevLvl = rState.nLevel;

                OSL_ENSURE( pONd->GetAttrOutlineLevel() >= 0 && pONd->GetAttrOutlineLevel() <= MAXLEVEL,
                        "<SwChapterField::ChangeExpansion(..)> - outline node with inconsistent outline level. Serious defect." );
                rState.nLevel = static_cast<sal_uInt8>(pONd->GetAttrOutlineLevel());

                if (nPrevLvl < rState.nLevel)
                    rState.nLevel = nPrevLvl;
                else if( SVX_NUM_NUMBER_NONE != rDoc.GetOutlineNumRule()
                        ->Get( rState.nLevel ).GetNumberingType() )
                {
                    pTextNd = pONd;
                    break;
                }

                if (!rState.nLevel--)
                    break;
                pONd = pTextNd->FindOutlineNodeOfLevel(rState.nLevel, pLayout);
            }
            else
                break;
        } while( true );
    }

    // get the number without Pre-/Post-fixstrings

    if ( pTextNd->IsOutline() )
    {
        // correction of refactoring done by cws swnumtree:
        // retrieve numbering string without prefix and suffix strings
        // as stated in the above german comment.
        rState.sNumber = pTextNd->GetNumString(false, MAXLEVEL, pLayout);

        SwNumRule* pRule( pTextNd->GetNumRule() );
        if ( pTextNd->IsCountedInList() && pRule )
        {
            int nListLevel = pTextNd->GetActualListLevel();
            if (nListLevel < 0)
                nListLevel = 0;
            if (nListLevel >= MAXLEVEL)
                nListLevel = MAXLEVEL - 1;

            const SwNumFormat& rNFormat = pRule->Get(nListLevel);
            rState.sPost = rNFormat.GetSuffix();
            rState.sPre = rNFormat.GetPrefix();
            rState.sLabelFollowedBy = removeControlChars(rNFormat.GetLabelFollowedByAsString());
        }
    }
    else
    {
        rState.sNumber = "??";
    }

    rState.sTitle = removeControlChars(sw::GetExpandTextMerged(pLayout,
                *pTextNd, false, false, ExpandMode(0)));
}

bool SwChapterField::QueryValue( uno::Any& rAny, sal_uInt16 nWhichId ) const
{
    switch( nWhichId )
    {
    case FIELD_PROP_BYTE1:
        rAny <<= static_cast<sal_Int8>(m_State.nLevel);
        break;

    case FIELD_PROP_USHORT1:
        {
            sal_Int16 nRet;
            switch( GetFormat() )
            {
                case SwChapterFormat::Number:
                    nRet = text::ChapterFormat::NUMBER;
                    break;
                case SwChapterFormat::Title:
                    nRet = text::ChapterFormat::NAME;
                    break;
                case SwChapterFormat::NumberNoPrePost:
                    nRet = text::ChapterFormat::DIGIT;
                    break;
                case SwChapterFormat::NumberNoPrePostAndTitle:
                    nRet = text::ChapterFormat::NO_PREFIX_SUFFIX;
                    break;
                case SwChapterFormat::NumberAndTitle:
                default:
                    nRet = text::ChapterFormat::NAME_NUMBER;
            }
            rAny <<= nRet;
        }
        break;

    default:
        assert(false);
    }
    return true;
}

bool SwChapterField::PutValue( const uno::Any& rAny, sal_uInt16 nWhichId )
{
    bool bRet = true;
    switch( nWhichId )
    {
    case FIELD_PROP_BYTE1:
    {
        sal_Int8 nTmp = 0;
        rAny >>= nTmp;
        if(nTmp >= 0 && nTmp < MAXLEVEL)
        {
            m_State.nLevel = nTmp;
            m_StateRLHidden.nLevel = nTmp;
        }
        else
            bRet = false;
        break;
    }

    case FIELD_PROP_USHORT1:
        {
            sal_Int16 nVal = 0;
            rAny >>= nVal;
            switch( nVal )
            {
                case text::ChapterFormat::NAME:
                    m_nFormat = SwChapterFormat::Title;
                    break;
                case text::ChapterFormat::NUMBER:
                    m_nFormat = SwChapterFormat::Number;
                    break;
                case text::ChapterFormat::NO_PREFIX_SUFFIX:
                    m_nFormat = SwChapterFormat::NumberNoPrePostAndTitle;
                    break;
                case text::ChapterFormat::DIGIT:
                    m_nFormat = SwChapterFormat::NumberNoPrePost;
                    break;
                default:
                    m_nFormat = SwChapterFormat::NumberAndTitle;
            }
        }
        break;

    default:
        assert(false);
    }
    return bRet;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
