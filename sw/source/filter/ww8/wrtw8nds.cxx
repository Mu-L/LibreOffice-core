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

#include <vector>
#include <utility>
#include <algorithm>
#include <iostream>

#include "docxexport.hxx"

#include <officecfg/Office/Common.hxx>
#include <i18nlangtag/mslangid.hxx>
#include <hintids.hxx>
#include <tools/urlobj.hxx>
#include <editeng/cmapitem.hxx>
#include <editeng/langitem.hxx>
#include <editeng/svxfont.hxx>
#include <editeng/lrspitem.hxx>
#include <editeng/brushitem.hxx>
#include <editeng/charhiddenitem.hxx>
#include <editeng/charreliefitem.hxx>
#include <editeng/contouritem.hxx>
#include <editeng/crossedoutitem.hxx>
#include <editeng/fontitem.hxx>
#include <editeng/keepitem.hxx>
#include <editeng/fhgtitem.hxx>
#include <editeng/ulspitem.hxx>
#include <editeng/formatbreakitem.hxx>
#include <editeng/frmdiritem.hxx>
#include <editeng/postitem.hxx>
#include <editeng/shdditem.hxx>
#include <editeng/tstpitem.hxx>
#include <editeng/wghtitem.hxx>
#include <svl/grabbagitem.hxx>
#include <svl/urihelper.hxx>
#include <svl/whiter.hxx>
#include <unotools/securityoptions.hxx>
#include <fmtpdsc.hxx>
#include <fmtlsplt.hxx>
#include <fmtanchr.hxx>
#include <fmtcntnt.hxx>
#include <frmatr.hxx>
#include <paratr.hxx>
#include <txatbase.hxx>
#include <fmtinfmt.hxx>
#include <fmtrfmrk.hxx>
#include <fchrfmt.hxx>
#include <fmtautofmt.hxx>
#include <charfmt.hxx>
#include <tox.hxx>
#include <ndtxt.hxx>
#include <pam.hxx>
#include <doc.hxx>
#include <IDocumentSettingAccess.hxx>
#include <IDocumentMarkAccess.hxx>
#include <docary.hxx>
#include <swtable.hxx>
#include <swtblfmt.hxx>
#include <section.hxx>
#include <pagedesc.hxx>
#include <swrect.hxx>
#include <reffld.hxx>
#include <redline.hxx>
#include <txttxmrk.hxx>
#include <fmtline.hxx>
#include <fmtruby.hxx>
#include <breakit.hxx>
#include <txtatr.hxx>
#include <cellatr.hxx>
#include <fmtrowsplt.hxx>
#include <com/sun/star/awt/FontRelief.hpp>
#include <com/sun/star/awt/FontStrikeout.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/i18n/BreakIterator.hpp>
#include <com/sun/star/i18n/ScriptType.hpp>
#include <com/sun/star/i18n/WordType.hpp>
#include <com/sun/star/text/RubyPosition.hpp>
#include <com/sun/star/style/CaseMap.hpp>
#include <oox/export/vmlexport.hxx>
#include <sal/log.hxx>
#include <comphelper/propertysequence.hxx>
#include <comphelper/string.hxx>

#include "sprmids.hxx"

#include "writerhelper.hxx"
#include "writerwordglue.hxx"
#include <numrule.hxx>
#include "wrtww8.hxx"
#include "ww8par.hxx"
#include <IMark.hxx>
#include "ww8attributeoutput.hxx"

#include <ndgrf.hxx>
#include <ndole.hxx>
#include <formatflysplit.hxx>
#include <annotationmark.hxx>
#include <poolfmt.hxx>

#include <cstdio>

using namespace ::com::sun::star;
using namespace ::com::sun::star::i18n;
using namespace sw::util;
using namespace sw::types;
using namespace sw::mark;
using namespace ::oox::vml;

static OUString lcl_getFieldCode( const Fieldmark* pFieldmark )
{
    assert(pFieldmark);

    if ( pFieldmark->GetFieldname( ) == ODF_FORMTEXT )
        return u" FORMTEXT "_ustr;
    if ( pFieldmark->GetFieldname( ) == ODF_FORMDROPDOWN )
        return u" FORMDROPDOWN "_ustr;
    if ( pFieldmark->GetFieldname( ) == ODF_FORMCHECKBOX )
        return u" FORMCHECKBOX "_ustr;
    if ( pFieldmark->GetFieldname( ) == ODF_FORMDATE )
        return u" ODFFORMDATE "_ustr;
    if ( pFieldmark->GetFieldname( ) == ODF_TOC )
        return u" TOC "_ustr;
    if ( pFieldmark->GetFieldname( ) == ODF_HYPERLINK )
        return u" HYPERLINK "_ustr;
    if ( pFieldmark->GetFieldname( ) == ODF_PAGEREF )
        return u" PAGEREF "_ustr;
    return pFieldmark->GetFieldname();
}

static ww::eField lcl_getFieldId(const Fieldmark*const pFieldmark)
{
    assert(pFieldmark);

    if ( pFieldmark->GetFieldname( ) == ODF_FORMTEXT )
        return ww::eFORMTEXT;
    if ( pFieldmark->GetFieldname( ) == ODF_FORMDROPDOWN )
        return ww::eFORMDROPDOWN;
    if ( pFieldmark->GetFieldname( ) == ODF_FORMCHECKBOX )
        return ww::eFORMCHECKBOX;
    if ( pFieldmark->GetFieldname( ) == ODF_FORMDATE )
        return ww::eFORMDATE;
    if ( pFieldmark->GetFieldname( ) == ODF_TOC )
        return ww::eTOC;
    if ( pFieldmark->GetFieldname( ) == ODF_HYPERLINK )
        return ww::eHYPERLINK;
    if ( pFieldmark->GetFieldname( ) == ODF_PAGEREF )
        return ww::ePAGEREF;
    return ww::eUNKNOWN;
}

static OUString
lcl_getLinkChainName(const uno::Reference<beans::XPropertySet>& rPropertySet,
                     const uno::Reference<beans::XPropertySetInfo>& rPropertySetInfo)
{
    OUString sLinkChainName;
    if (rPropertySetInfo->hasPropertyByName(u"LinkDisplayName"_ustr))
    {
        rPropertySet->getPropertyValue(u"LinkDisplayName"_ustr) >>= sLinkChainName;
        if (!sLinkChainName.isEmpty())
            return sLinkChainName;
    }
    if (rPropertySetInfo->hasPropertyByName(u"ChainName"_ustr))
        rPropertySet->getPropertyValue(u"ChainName"_ustr) >>= sLinkChainName;
    return sLinkChainName;
}

static bool lcl_IsInlineHeading(const ww8::Frame &rFrame)
{
    const SwFormat* pParent = rFrame.GetFrameFormat().DerivedFrom();
    return pParent && pParent->GetPoolFormatId() == RES_POOLFRM_INLINE_HEADING;
}

MSWordAttrIter::MSWordAttrIter( MSWordExportBase& rExport )
    : m_pOld( rExport.m_pChpIter ), m_rExport( rExport )
{
    m_rExport.m_pChpIter = this;
}

MSWordAttrIter::~MSWordAttrIter()
{
    m_rExport.m_pChpIter = m_pOld;
}

namespace {

class sortswflys
{
public:
    bool operator()(const ww8::Frame &rOne, const ww8::Frame &rTwo) const
    {
        return rOne.GetPosition() < rTwo.GetPosition();
    }
};

}

void SwWW8AttrIter::IterToCurrent()
{
    OSL_ENSURE(maCharRuns.begin() != maCharRuns.end(), "Impossible");
    mnScript = maCharRunIter->mnScript;
    meChrSet = maCharRunIter->meCharSet;
    mbCharIsRTL = maCharRunIter->mbRTL;
}

SwWW8AttrIter::SwWW8AttrIter(MSWordExportBase& rWr, const SwTextNode& rTextNd) :
    MSWordAttrIter(rWr),
    m_rNode(rTextNd),
    maCharRuns(GetPseudoCharRuns(rTextNd)),
    m_pCurRedline(nullptr),
    m_nCurrentSwPos(0),
    m_nCurRedlinePos(SwRedlineTable::npos),
    mrSwFormatDrop(rTextNd.GetSwAttrSet().GetDrop())
{

    SwPosition aPos(rTextNd);
    mbParaIsRTL = SvxFrameDirection::Horizontal_RL_TB == rWr.m_rDoc.GetTextDirection(aPos);

    maCharRunIter = maCharRuns.begin();
    IterToCurrent();

    /*
     #i2916#
     Get list of any graphics which may be anchored from this paragraph.
    */
    maFlyFrames = GetFramesInNode(rWr.m_aFrames, m_rNode);
    std::stable_sort(maFlyFrames.begin(), maFlyFrames.end(), sortswflys());

    /*
     #i18480#
     If we are inside a frame then anything anchored inside this frame can
     only be supported by word anchored inline ("as character"), so force
     this in the supportable case.
    */
    if (rWr.m_bInWriteEscher)
    {
        for ( auto& aFlyFrame : maFlyFrames )
            aFlyFrame.ForceTreatAsInline();
    }

    maFlyIter = maFlyFrames.begin();

    if ( !m_rExport.m_rDoc.getIDocumentRedlineAccess().GetRedlineTable().empty() )
    {
        SwPosition aPosition( m_rNode );
        m_pCurRedline = m_rExport.m_rDoc.getIDocumentRedlineAccess().GetRedline( aPosition, &m_nCurRedlinePos );
    }

    m_nCurrentSwPos = SearchNext(1);
}

sal_Int32 SwWW8AttrIter::SearchNext( sal_Int32 nStartPos )
{
    assert(nStartPos > 0); // it uses nStartPos - 1 in searches
    std::u16string_view aText = m_rNode.GetText();
    auto fieldEndPos = aText.find(CH_TXT_ATR_FIELDEND, nStartPos - 1);
    // HACK: for (so far) mysterious reasons the sdtContent element closes
    // too late in testDateFormField() unless an empty run is exported at
    // the end of the fieldmark; hence find *also* the position after the
    // CH_TXT_ATR_FIELDEND here
    if (fieldEndPos < o3tl::make_unsigned(nStartPos))
    {
        ++fieldEndPos;
    }
    auto fieldSepPos = aText.find(CH_TXT_ATR_FIELDSEP, nStartPos);
    auto fieldStartPos = aText.find(CH_TXT_ATR_FIELDSTART, nStartPos);
    auto formElementPos = aText.find(CH_TXT_ATR_FORMELEMENT, nStartPos - 1);
    if (formElementPos < o3tl::make_unsigned(nStartPos))
    {
        ++formElementPos; // tdf#133604 put this in its own run
    }

    const auto pos = std::min({ fieldEndPos, fieldSepPos, fieldStartPos, formElementPos });

    sal_Int32 nMinPos = (pos != std::u16string_view::npos) ? pos : SAL_MAX_INT32;

    // first the redline, then the attributes
    if( m_pCurRedline )
    {
        const SwPosition* pEnd = m_pCurRedline->End();
        if (pEnd->GetNode() == m_rNode)
        {
            const sal_Int32 i = pEnd->GetContentIndex();
            if ( i >= nStartPos && i < nMinPos )
            {
                nMinPos = i;
            }
        }
    }

    if ( m_nCurRedlinePos < m_rExport.m_rDoc.getIDocumentRedlineAccess().GetRedlineTable().size() )
    {
        // nCurRedlinePos point to the next redline
        SwRedlineTable::size_type nRedLinePos = m_nCurRedlinePos;
        if( m_pCurRedline )
            ++nRedLinePos;

        for ( ; nRedLinePos < m_rExport.m_rDoc.getIDocumentRedlineAccess().GetRedlineTable().size(); ++nRedLinePos )
        {
            const SwRangeRedline* pRedl = m_rExport.m_rDoc.getIDocumentRedlineAccess().GetRedlineTable()[ nRedLinePos ];

            auto [pStart, pEnd] = pRedl->StartEnd(); // SwPosition*

            if( pStart->GetNode() == m_rNode )
            {
                const sal_Int32 i = pStart->GetContentIndex();
                if( i >= nStartPos && i < nMinPos )
                    nMinPos = i;
            }
            else
                break;

            if( pEnd->GetNode() == m_rNode )
            {
                const sal_Int32 i = pEnd->GetContentIndex();
                if( i >= nStartPos && i < nMinPos )
                {
                    nMinPos = i;
                }
            }
        }
    }

    if (mrSwFormatDrop.GetWholeWord() && nStartPos <= m_rNode.GetDropLen(0))
        nMinPos = m_rNode.GetDropLen(0);
    else if(nStartPos <= mrSwFormatDrop.GetChars())
        nMinPos = mrSwFormatDrop.GetChars();

    if(const SwpHints* pTextAttrs = m_rNode.GetpSwpHints())
    {

// can be optimized if we consider that the TextAttrs are sorted by start position.
// but then we'd have to save 2 indices
        for( size_t i = 0; i < pTextAttrs->Count(); ++i )
        {
            const SwTextAttr* pHt = pTextAttrs->Get(i);
            sal_Int32 nPos = pHt->GetStart();    // first Attr characters
            if( nPos >= nStartPos && nPos <= nMinPos )
                nMinPos = nPos;

            if( pHt->End() )         // Attr with end
            {
                nPos = *pHt->End();      // last Attr character + 1
                if( nPos >= nStartPos && nPos <= nMinPos )
                    nMinPos = nPos;
            }
            if (pHt->HasDummyChar())
            {
                // pos + 1 because of CH_TXTATR in Text
                nPos = pHt->GetStart() + 1;
                if( nPos >= nStartPos && nPos <= nMinPos )
                    nMinPos = nPos;
            }
        }
    }

    if (maCharRunIter != maCharRuns.end())
    {
        if (maCharRunIter->mnEndPos < nMinPos)
            nMinPos = maCharRunIter->mnEndPos;
        IterToCurrent();
    }

    // #i2916# Check to see if there are any graphics anchored to characters in this paragraph's text.
    sal_Int32 nNextFlyPos = 0;
    ww8::FrameIter aTmpFlyIter = maFlyIter;
    while (aTmpFlyIter != maFlyFrames.end() && nNextFlyPos < nStartPos)
    {
        const SwPosition &rAnchor = aTmpFlyIter->GetPosition();
        nNextFlyPos = rAnchor.GetContentIndex();

        ++aTmpFlyIter;
    }
    if (nNextFlyPos >= nStartPos && nNextFlyPos < nMinPos)
        nMinPos = nNextFlyPos;

    //nMinPos found and not going to change at this point

    if (maCharRunIter != maCharRuns.end())
    {
        if (maCharRunIter->mnEndPos == nMinPos)
            ++maCharRunIter;
    }

    return nMinPos;
}

void SwWW8AttrIter::OutAttr(sal_Int32 nSwPos, bool bWriteCombChars)
{
    m_rExport.AttrOutput().RTLAndCJKState( mbCharIsRTL, GetScript() );

    /*
     Depending on whether text is in CTL/CJK or Western, get the id of that
     script, the idea is that the font that is actually in use to render this
     range of text ends up in pFont
    */
    TypedWhichId<SvxFontItem> nFontId = GetWhichOfScript( RES_CHRATR_FONT, GetScript() );

    const SvxFontItem &rParentFont =
        static_cast<const SwTextFormatColl&>(m_rNode.GetAnyFormatColl()).GetFormatAttr(nFontId);
    const SvxFontItem *pFont = &rParentFont;
    const SfxPoolItem *pGrabBag = nullptr;

    SfxItemSetFixed<RES_CHRATR_BEGIN, RES_TXTATR_END - 1> aExportSet(*m_rNode.GetSwAttrSet().GetPool());

    //The hard formatting properties that affect the entire paragraph
    if (m_rNode.HasSwAttrSet())
    {
        // only copy hard attributes - bDeep = false
        aExportSet.Set(m_rNode.GetSwAttrSet(), false/*bDeep*/);
        // get the current font item. Use rNd.GetSwAttrSet instead of aExportSet:
        const SvxFontItem &rNdFont = m_rNode.GetSwAttrSet().Get(nFontId);
        pFont = &rNdFont;
        aExportSet.ClearItem(nFontId);
    }

    //The additional hard formatting properties that affect this range in the
    //paragraph
    ww8::PoolItems aRangeItems;
    if (const SwpHints* pTextAttrs = m_rNode.GetpSwpHints())
    {
        for( size_t i = 0; i < pTextAttrs->Count(); ++i )
        {
            const SwTextAttr* pHt = pTextAttrs->Get(i);
            const sal_Int32* pEnd = pHt->End();

            if (pEnd ? ( nSwPos >= pHt->GetStart() && nSwPos < *pEnd)
                        : nSwPos == pHt->GetStart() )
            {
                sal_uInt16 nWhich = pHt->GetAttr().Which();
                if (nWhich == RES_TXTATR_AUTOFMT)
                {
                    const SwFormatAutoFormat& rAutoFormat = static_cast<const SwFormatAutoFormat&>(pHt->GetAttr());
                    const std::shared_ptr<SfxItemSet>& pSet = rAutoFormat.GetStyleHandle();
                    SfxWhichIter aIter( *pSet );
                    const SfxPoolItem* pItem;
                    sal_uInt16 nWhichId = aIter.FirstWhich();
                    while( nWhichId )
                    {
                        if( SfxItemState::SET == aIter.GetItemState( false, &pItem ))
                        {
                            if (nWhichId == nFontId)
                                pFont = &(item_cast<SvxFontItem>(*pItem));
                            else if (nWhichId == RES_CHRATR_GRABBAG)
                                pGrabBag = pItem;
                            else
                                aRangeItems[nWhichId] = pItem;
                        }
                        nWhichId = aIter.NextWhich();
                    }
                }
                else
                    aRangeItems[nWhich] = (&(pHt->GetAttr()));
            }
            else if (nSwPos < pHt->GetStart())
                break;
        }
    }
//    DeduplicateItems(aRangeItems);

    /*
     For #i24291# we need to explicitly remove any properties from the
     aExportSet which a SwCharFormat would override, we can't rely on word doing
     this for us like writer does
    */
    const SwFormatCharFormat *pCharFormatItem =
        HasItem< SwFormatCharFormat >( aRangeItems, RES_TXTATR_CHARFMT );
    if ( pCharFormatItem )
        ClearOverridesFromSet( *pCharFormatItem, aExportSet );

    // check toggle properties in DOCX output
    if (pCharFormatItem)
    {
        handleToggleProperty(aExportSet, *pCharFormatItem);
    }

    // tdf#113790: AutoFormat style overwrites char style, so remove all
    // elements from CHARFMT grab bag which are set in AUTOFMT grab bag
    if (const SfxGrabBagItem *pAutoFmtGrabBag = dynamic_cast<const SfxGrabBagItem*>(pGrabBag))
    {
        if (const SfxGrabBagItem *pCharFmtGrabBag = aExportSet.GetItem<SfxGrabBagItem>(RES_CHRATR_GRABBAG, false))
        {
            std::map<OUString, css::uno::Any> aNewGrabBagMap = pCharFmtGrabBag->GetGrabBag();
            for (auto const & item : pAutoFmtGrabBag->GetGrabBag())
            {
                if (item.second.hasValue())
                    aNewGrabBagMap.erase(item.first);
            }
            aExportSet.Put(std::make_unique<SfxGrabBagItem>(RES_CHRATR_GRABBAG, std::move(aNewGrabBagMap)));
        }
    }

    ww8::PoolItems aExportItems;
    GetPoolItems( aExportSet, aExportItems, false );

    if( m_rNode.GetpSwpHints() == nullptr )
        m_rExport.SetCurItemSet(&aExportSet);

    for ( const auto& aRangeItem : aRangeItems )
    {
        aExportItems[aRangeItem.first] = aRangeItem.second;
    }

    if ( !aExportItems.empty() )
    {
        const sw::BroadcastingModify* pOldMod = m_rExport.m_pOutFormatNode;
        m_rExport.m_pOutFormatNode = &m_rNode;
        m_rExport.m_aCurrentCharPropStarts.push( nSwPos );

        // tdf#38778 Fix output of the font in DOC run for fields
        const SvxFontItem * pFontToOutput = ( rParentFont != *pFont )? pFont : nullptr;

        m_rExport.ExportPoolItemsToCHP( aExportItems, GetScript(), pFontToOutput, bWriteCombChars );

        // HasTextItem only allowed in the above range
        m_rExport.m_aCurrentCharPropStarts.pop();
        m_rExport.m_pOutFormatNode = pOldMod;
    }

    if( m_rNode.GetpSwpHints() == nullptr )
        m_rExport.SetCurItemSet(nullptr);

    OSL_ENSURE( pFont, "must be *some* font associated with this txtnode" );
    if ( pFont )
    {
        SvxFontItem aFont( *pFont );

        if ( rParentFont != aFont )
            m_rExport.AttrOutput().OutputItem( aFont );
    }

    // Output grab bag attributes
    if (pGrabBag)
        m_rExport.AttrOutput().OutputItem( *pGrabBag );
}

// Toggle Properties
//
// If the value of the toggle property appears at multiple levels of the style hierarchy (17.7.2), their
// effective values shall be combined as follows:
//
//     value_{effective} = val_{table} XOR val_{paragraph} XOR val_{character}
//
// If the value specified by the document defaults is true, the effective value is true.
// Otherwise, the values are combined by a Boolean XOR as follows:
// i.e., the effective value to be applied to the content shall be true if its effective value is true for
// an odd number of levels of the style hierarchy.
//
// To prevent such logic inside output, it is required to write inline attribute tokens on content level.
void SwWW8AttrIter::handleToggleProperty(SfxItemSet& rExportSet, const SwFormatCharFormat& rCharFormatItem)
{
    if (rExportSet.HasItem(RES_CHRATR_WEIGHT) || rExportSet.HasItem(RES_CHRATR_POSTURE)  ||
        rExportSet.HasItem(RES_CHRATR_CTL_WEIGHT) || rExportSet.HasItem(RES_CHRATR_CTL_POSTURE)  ||
        rExportSet.HasItem(RES_CHRATR_CONTOUR) || rExportSet.HasItem(RES_CHRATR_CASEMAP) ||
        rExportSet.HasItem(RES_CHRATR_RELIEF) || rExportSet.HasItem(RES_CHRATR_SHADOWED) ||
        rExportSet.HasItem(RES_CHRATR_CROSSEDOUT) || rExportSet.HasItem(RES_CHRATR_HIDDEN))
        return;

    SvxWeightItem aBoldProperty(WEIGHT_BOLD, RES_CHRATR_WEIGHT);
    SvxPostureItem aPostureProperty(ITALIC_NORMAL, RES_CHRATR_POSTURE);
    SvxContourItem aContouredProperty(true, RES_CHRATR_CONTOUR);
    SvxCaseMapItem aCaseMapCapsProperty(SvxCaseMap::Uppercase, RES_CHRATR_CASEMAP);
    SvxCaseMapItem aCaseMapSmallProperty(SvxCaseMap::SmallCaps, RES_CHRATR_CASEMAP);
    SvxCharReliefItem aEmbossedProperty(FontRelief::Embossed, RES_CHRATR_RELIEF);
    SvxCharReliefItem aImprintProperty(FontRelief::Engraved, RES_CHRATR_RELIEF);
    SvxShadowedItem aShadowedProperty(true, RES_CHRATR_SHADOWED);
    SvxCrossedOutItem aStrikeoutProperty(STRIKEOUT_SINGLE, RES_CHRATR_CROSSEDOUT);
    SvxCharHiddenItem aHiddenProperty(true, RES_CHRATR_HIDDEN);

    bool hasWeightPropertyInCharStyle = false;
    bool hasWeightComplexPropertyInCharStyle = false;
    bool hasPosturePropertyInCharStyle = false;
    bool hasPostureComplexPropertyInCharStyle = false;
    bool bHasCapsPropertyInCharStyle = false;
    bool bHasSmallCapsPropertyInCharStyle = false;
    bool bHasEmbossedPropertyInCharStyle = false;
    bool bHasImprintPropertyInCharStyle = false;
    bool hasContouredPropertyInCharStyle = false;
    bool hasShadowedPropertyInCharStyle = false;
    bool hasStrikeoutPropertyInCharStyle = false;
    bool hasHiddenPropertyInCharStyle = false;


    // get attribute flags from specified character style
    if (const SwCharFormat* pCharFormat = rCharFormatItem.GetCharFormat())
    {
        if (const SfxPoolItem* pWeightItem = pCharFormat->GetAttrSet().GetItem(RES_CHRATR_WEIGHT))
            hasWeightPropertyInCharStyle = (*pWeightItem == aBoldProperty);

        if (const SfxPoolItem* pWeightComplexItem = pCharFormat->GetAttrSet().GetItem(RES_CHRATR_CTL_WEIGHT))
            hasWeightComplexPropertyInCharStyle = (*pWeightComplexItem == aBoldProperty);

        if (const SfxPoolItem* pPostureItem = pCharFormat->GetAttrSet().GetItem(RES_CHRATR_POSTURE))
            hasPosturePropertyInCharStyle = (*pPostureItem == aPostureProperty);

        if (const SfxPoolItem* pPostureComplexItem = pCharFormat->GetAttrSet().GetItem(RES_CHRATR_CTL_POSTURE))
            hasPostureComplexPropertyInCharStyle = (*pPostureComplexItem == aPostureProperty);

        if (const SfxPoolItem* pContouredItem = pCharFormat->GetAttrSet().GetItem(RES_CHRATR_CONTOUR))
            hasContouredPropertyInCharStyle = (*pContouredItem == aContouredProperty);

        if (const SfxPoolItem* pShadowedItem = pCharFormat->GetAttrSet().GetItem(RES_CHRATR_SHADOWED))
            hasShadowedPropertyInCharStyle = (*pShadowedItem == aShadowedProperty);

        if (const SfxPoolItem* pStrikeoutItem = pCharFormat->GetAttrSet().GetItem(RES_CHRATR_CROSSEDOUT))
            hasStrikeoutPropertyInCharStyle = (*pStrikeoutItem == aStrikeoutProperty);

        if (const SfxPoolItem* pHiddenItem = pCharFormat->GetAttrSet().GetItem(RES_CHRATR_HIDDEN))
            hasHiddenPropertyInCharStyle = (*pHiddenItem == aHiddenProperty);

        if (const SfxPoolItem* pCaseMapItem  = pCharFormat->GetAttrSet().GetItem(RES_CHRATR_CASEMAP))
        {
            bHasCapsPropertyInCharStyle = (*pCaseMapItem == aCaseMapCapsProperty);
            bHasSmallCapsPropertyInCharStyle = (*pCaseMapItem == aCaseMapSmallProperty);
        }

        if (const SfxPoolItem* pReliefItem  = pCharFormat->GetAttrSet().GetItem(RES_CHRATR_RELIEF))
        {
            bHasEmbossedPropertyInCharStyle = (*pReliefItem == aEmbossedProperty);
            bHasImprintPropertyInCharStyle = (*pReliefItem == aImprintProperty);
        }
    }

    // get attribute flags from specified paragraph style and apply properties if they are set in character and paragraph style
    {
        SwTextFormatColl& rTextColl = static_cast<SwTextFormatColl&>( m_rNode.GetAnyFormatColl() );
        sal_uInt16 nStyle = m_rExport.m_pStyles->GetSlot( &rTextColl );
        nStyle = ( nStyle != 0xfff ) ? nStyle : 0;
        const SwFormat* pFormat = m_rExport.m_pStyles->GetSwFormat(nStyle);
        if (pFormat)
        {
            const SfxPoolItem* pItem;
            if (hasWeightPropertyInCharStyle && (pItem = pFormat->GetAttrSet().GetItem(RES_CHRATR_WEIGHT)) &&
                (*pItem == aBoldProperty))
                rExportSet.Put(aBoldProperty);

            if (hasWeightComplexPropertyInCharStyle && (pItem = pFormat->GetAttrSet().GetItem(RES_CHRATR_CTL_WEIGHT)) &&
                *pItem == aBoldProperty)
            {
                rExportSet.PutAsTargetWhich(aBoldProperty, RES_CHRATR_CTL_WEIGHT);
            }

            if (hasPosturePropertyInCharStyle && (pItem = pFormat->GetAttrSet().GetItem(RES_CHRATR_POSTURE)) &&
                *pItem == aPostureProperty)
                rExportSet.Put(aPostureProperty);

            if (hasPostureComplexPropertyInCharStyle && (pItem = pFormat->GetAttrSet().GetItem(RES_CHRATR_CTL_POSTURE)) &&
                *pItem == aPostureProperty)
            {
                rExportSet.PutAsTargetWhich(aPostureProperty, RES_CHRATR_CTL_POSTURE);
            }

            if (hasContouredPropertyInCharStyle && (pItem = pFormat->GetAttrSet().GetItem(RES_CHRATR_CONTOUR)) && *pItem == aContouredProperty)
                rExportSet.Put(aContouredProperty);

            if (hasShadowedPropertyInCharStyle && (pItem = pFormat->GetAttrSet().GetItem(RES_CHRATR_SHADOWED)) &&
                *pItem == aShadowedProperty)
                rExportSet.Put(aShadowedProperty);

            if (hasStrikeoutPropertyInCharStyle && (pItem = pFormat->GetAttrSet().GetItem(RES_CHRATR_CROSSEDOUT)) &&
                *pItem == aStrikeoutProperty)
                rExportSet.Put(aStrikeoutProperty);

            if (hasHiddenPropertyInCharStyle && (pItem = pFormat->GetAttrSet().GetItem(RES_CHRATR_HIDDEN)) &&
                (*pItem == aHiddenProperty))
                rExportSet.Put(aHiddenProperty);

            if ((bHasCapsPropertyInCharStyle||bHasSmallCapsPropertyInCharStyle) && (pItem = pFormat->GetAttrSet().GetItem(RES_CHRATR_CASEMAP)))
            {
                if (bHasCapsPropertyInCharStyle && *pItem == aCaseMapCapsProperty)
                    rExportSet.Put(aCaseMapCapsProperty);
                else if (bHasSmallCapsPropertyInCharStyle && *pItem == aCaseMapSmallProperty)
                    rExportSet.Put(aCaseMapSmallProperty);
            }

            if ((bHasEmbossedPropertyInCharStyle||bHasImprintPropertyInCharStyle) && (pItem = pFormat->GetAttrSet().GetItem(RES_CHRATR_RELIEF)))
            {
                if (bHasEmbossedPropertyInCharStyle && *pItem == aEmbossedProperty)
                    rExportSet.Put(aEmbossedProperty);
                else if (bHasImprintPropertyInCharStyle && *pItem == aImprintProperty)
                    rExportSet.Put(aImprintProperty);
            }
        }

    }
}

bool SwWW8AttrIter::IsWatermarkFrame()
{
    if (maFlyFrames.size() != 1)
        return false;

    while ( maFlyIter != maFlyFrames.end() )
    {
        const SdrObject* pSdrObj = maFlyIter->GetFrameFormat().FindRealSdrObject();

        if (pSdrObj)
        {
            if (VMLExport::IsWaterMarkShape(pSdrObj->GetName()))
                  return true;
        }
        ++maFlyIter;
    }

    return false;
}

bool SwWW8AttrIter::IsAnchorLinkedToThisNode( SwNodeOffset nNodePos )
{
    if ( maFlyIter == maFlyFrames.end() )
        return false;

    /* if current node position and the anchor position are the same
        then the frame anchor is linked to this node
    */
    return nNodePos == maFlyIter->GetPosition().GetNodeIndex();
}

bool SwWW8AttrIter::HasFlysAt(sal_Int32 nSwPos, const ww8::Frame** pInlineHeading) const
{
    for (const auto& rFly : maFlyFrames)
    {
        const SwPosition& rAnchor = rFly.GetPosition();
        const sal_Int32 nPos = rAnchor.GetContentIndex();
        if (nPos == nSwPos)
        {
            if ( pInlineHeading )
            {
                if ( lcl_IsInlineHeading(rFly) )
                {
                    *pInlineHeading = &rFly;
                    return true;
                }
            }
            else
                return true;
        }
    }

    return false;
}

FlyProcessingState SwWW8AttrIter::OutFlys(sal_Int32 nSwPos)
{
    // collection point to first gather info about all of the potentially linked textboxes: to be analyzed later.
    ww8::FrameIter linkedTextboxesIter = maFlyIter;
    while ( linkedTextboxesIter != maFlyFrames.end() )
    {
        uno::Reference< drawing::XShape > xShape;
        ww8::Frame aFrame = *linkedTextboxesIter;
        const SdrObject* pSdrObj = aFrame.GetFrameFormat().FindRealSdrObject();
        if( pSdrObj )
            xShape.set(const_cast<SdrObject*>(pSdrObj)->getUnoShape(), uno::UNO_QUERY);
        uno::Reference< beans::XPropertySet > xPropertySet(xShape, uno::UNO_QUERY);
        uno::Reference< beans::XPropertySetInfo > xPropertySetInfo;
        if( xPropertySet.is() )
            xPropertySetInfo = xPropertySet->getPropertySetInfo();
        if( xPropertySetInfo.is() )
        {
            MSWordExportBase::LinkedTextboxInfo aLinkedTextboxInfo;

            const OUString sLinkChainName = lcl_getLinkChainName(xPropertySet, xPropertySetInfo);

            if( xPropertySetInfo->hasPropertyByName(u"ChainNextName"_ustr) )
                xPropertySet->getPropertyValue(u"ChainNextName"_ustr) >>= aLinkedTextboxInfo.sNextChain;
            if( xPropertySetInfo->hasPropertyByName(u"ChainPrevName"_ustr) )
                xPropertySet->getPropertyValue(u"ChainPrevName"_ustr) >>= aLinkedTextboxInfo.sPrevChain;

            //collect a list of linked textboxes: those with a NEXT or PREVIOUS link
            if( !aLinkedTextboxInfo.sNextChain.isEmpty() || !aLinkedTextboxInfo.sPrevChain.isEmpty() )
            {
                assert( !sLinkChainName.isEmpty() );

                //there are many discarded duplicates in documents - no duplicates allowed in the list, so try to find the real one.
                //if this LinkDisplayName/ChainName already exists on a different shape...
                //  the earlier processed duplicates are thrown out unless this one can be proved as bad. (last processed duplicate usually is stored)
                auto linkFinder = m_rExport.m_aLinkedTextboxesHelper.find(sLinkChainName);
                if( linkFinder != m_rExport.m_aLinkedTextboxesHelper.end() )
                {
                    //If my NEXT/PREV targets have already been discovered, but don't match me, then assume I'm an abandoned remnant
                    //    (this logic fails if both me and one of my links are duplicated, and the remnants were added first.)
                    linkFinder = m_rExport.m_aLinkedTextboxesHelper.find(aLinkedTextboxInfo.sNextChain);
                    if( (linkFinder != m_rExport.m_aLinkedTextboxesHelper.end()) && (linkFinder->second.sPrevChain != sLinkChainName) )
                    {
                        ++linkedTextboxesIter;
                        break;
                    }

                    linkFinder = m_rExport.m_aLinkedTextboxesHelper.find(aLinkedTextboxInfo.sPrevChain);
                    if( (linkFinder != m_rExport.m_aLinkedTextboxesHelper.end()) && (linkFinder->second.sNextChain != sLinkChainName) )
                    {
                        ++linkedTextboxesIter;
                        break;
                    }
                }
                m_rExport.m_bLinkedTextboxesHelperInitialized = false;
                m_rExport.m_aLinkedTextboxesHelper[sLinkChainName] = std::move(aLinkedTextboxInfo);
            }
        }
        ++linkedTextboxesIter;
    }

    if (maFlyIter == maFlyFrames.end())
    {
        // tdf#143039 postponed prevents fly duplication at end of paragraph
        return m_rExport.AttrOutput().IsFlyProcessingPostponed() ? FLY_POSTPONED : FLY_NONE;
    }

    /*
     #i2916#
     May have an anchored graphic to be placed, loop through sorted array
     and output all at this position
    */
    while ( maFlyIter != maFlyFrames.end() )
    {
        const SwPosition &rAnchor = maFlyIter->GetPosition();
        const sal_Int32 nPos = rAnchor.GetContentIndex();

        assert(nPos >= nSwPos && "a fly must get flagged as a nextAttr/CurrentPos");
        if ( nPos != nSwPos )
            return FLY_NOT_PROCESSED ; // We haven't processed the fly

        const SdrObject* pSdrObj = maFlyIter->GetFrameFormat().FindRealSdrObject();

        if (pSdrObj)
        {
            if (VMLExport::IsWaterMarkShape(pSdrObj->GetName()))
            {
                 // This is a watermark object. Should be written ONLY in the header
                 if(m_rExport.m_nTextTyp == TXT_HDFT)
                 {
                       // Should write a watermark in the header
                       m_rExport.AttrOutput().OutputFlyFrame( *maFlyIter );
                 }
                 else
                 {
                       // Should not write watermark object in the main body text
                 }
            }
            else if ( lcl_IsInlineHeading(*maFlyIter) )
            {
                 // Should not write inline heading again
            }
            else
            {
                 // This is not a watermark object - write normally
                 m_rExport.AttrOutput().OutputFlyFrame( *maFlyIter );
            }
        }
        else
        {
            // This is not a watermark object - write normally
            m_rExport.AttrOutput().OutputFlyFrame( *maFlyIter );
        }
        ++maFlyIter;
    }
    return ( m_rExport.AttrOutput().IsFlyProcessingPostponed() ? FLY_POSTPONED : FLY_PROCESSED ) ;
}

bool SwWW8AttrIter::IsTextAttr( sal_Int32 nSwPos ) const
{
    // search for attrs with dummy character or content
    if (const SwpHints* pTextAttrs = m_rNode.GetpSwpHints())
    {
        for (size_t i = 0; i < pTextAttrs->Count(); ++i)
        {
            const SwTextAttr* pHt = pTextAttrs->Get(i);
            if (nSwPos == pHt->GetStart())
            {
                if (pHt->HasDummyChar() || pHt->HasContent() )
                {
                    return true;
                }
            }
            else if (nSwPos < pHt->GetStart())
            {
                break; // sorted by start
            }
        }
    }

    return false;
}

bool SwWW8AttrIter::IsExportableAttr(sal_Int32 nSwPos) const
{
    if (const SwpHints* pTextAttrs = m_rNode.GetpSwpHints())
    {
        for (size_t i = 0; i < pTextAttrs->Count(); ++i)
        {
            const SwTextAttr* pHt = pTextAttrs->GetSortedByEnd(i);
            const sal_Int32 nStart = pHt->GetStart();
            const sal_Int32 nEnd = pHt->End() ? *pHt->End() : INT_MAX;
            if (nSwPos >= nStart && nSwPos < nEnd)
            {
                switch (pHt->GetAttr().Which())
                {
                    // Metadata fields should be dynamically generated, not dumped as text.
                case RES_TXTATR_METAFIELD:
                    return false;
                }
            }
        }
    }

    return true;
}

bool SwWW8AttrIter::IsDropCap( int nSwPos )
{
    // see if the current position falls on a DropCap
    int nDropChars = mrSwFormatDrop.GetChars();
    bool bWholeWord = mrSwFormatDrop.GetWholeWord();
    if (bWholeWord)
    {
        const sal_Int32 nWordLen = m_rNode.GetDropLen(0);
        if(nSwPos == nWordLen && nSwPos != 0)
            return true;
    }
    else
    {
        if (nSwPos == nDropChars && nSwPos != 0)
            return true;
    }
    return false;
}

bool SwWW8AttrIter::RequiresImplicitBookmark()
{
    return std::any_of(m_rExport.m_aImplicitBookmarks.begin(), m_rExport.m_aImplicitBookmarks.end(),
        [this](const aBookmarkPair& rBookmarkPair) { return rBookmarkPair.second == m_rNode.GetIndex(); });
}

//HasItem is for the summary of the double attributes: Underline and WordlineMode as TextItems.
// OutAttr () calls the output function, which can call HasItem() for other items at the attribute's start position.
// Only attributes with end can be queried.
// It searches with bDeep
const SfxPoolItem* SwWW8AttrIter::HasTextItem( sal_uInt16 nWhich ) const
{
    const SfxPoolItem* pRet = nullptr;
    const SwpHints* pTextAttrs = m_rNode.GetpSwpHints();
    if (pTextAttrs && !m_rExport.m_aCurrentCharPropStarts.empty())
    {
        const sal_Int32 nTmpSwPos = m_rExport.m_aCurrentCharPropStarts.top();
        for (size_t i = 0; i < pTextAttrs->Count(); ++i)
        {
            const SwTextAttr* pHt = pTextAttrs->Get(i);
            const SfxPoolItem* pItem = &pHt->GetAttr();
            const sal_Int32 * pAtrEnd = nullptr;
            if( nullptr != ( pAtrEnd = pHt->End() ) &&        // only Attr with an end
                nTmpSwPos >= pHt->GetStart() && nTmpSwPos < *pAtrEnd )
            {
                if ( nWhich == pItem->Which() )
                {
                    pRet = pItem;       // found it
                    break;
                }
                else if( RES_TXTATR_INETFMT == pHt->Which() ||
                         RES_TXTATR_CHARFMT == pHt->Which() ||
                         RES_TXTATR_AUTOFMT == pHt->Which() )
                {
                    const SfxItemSet* pSet = CharFormat::GetItemSet( pHt->GetAttr() );
                    const SfxPoolItem* pCharItem;
                    if ( pSet &&
                         SfxItemState::SET == pSet->GetItemState( nWhich, pHt->Which() != RES_TXTATR_AUTOFMT, &pCharItem ) )
                    {
                        pRet = pCharItem;       // found it
                        break;
                    }
                }
            }
            else if (nTmpSwPos < pHt->GetStart())
                break;              // nothing more to come
        }
    }
    return pRet;
}

void WW8Export::GetCurrentItems(ww::bytes &rItems) const
{
    rItems.insert(rItems.end(), m_pO->begin(), m_pO->end());
}

const SfxPoolItem& SwWW8AttrIter::GetItem(sal_uInt16 nWhich) const
{
    const SfxPoolItem* pRet = HasTextItem(nWhich);
    return pRet ? *pRet : m_rNode.SwContentNode::GetAttr(nWhich);
}

void WW8AttributeOutput::StartRuby( const SwTextNode& rNode, sal_Int32 /*nPos*/, const SwFormatRuby& rRuby )
{
    WW8Ruby aWW8Ruby(rNode, rRuby, GetExport());
    OUString aStr =
        FieldString( ww::eEQ )
        + "\\* jc"
        + OUString::number(aWW8Ruby.GetJC())
        + " \\* \"Font:"
        + aWW8Ruby.GetFontFamily()
        + "\" \\* hps"
        + OUString::number((aWW8Ruby.GetRubyHeight() + 5) / 10)
        + " \\o";
    if (aWW8Ruby.GetDirective())
    {
        aStr += OUString::Concat(u"\\a") + OUStringChar(aWW8Ruby.GetDirective());
    }
    aStr += "(\\s\\up " + OUString::number((aWW8Ruby.GetBaseHeight() + 10) / 20 - 1) + "(";
    aStr += rRuby.GetText() + ")";

    // The parameter separator depends on the FIB.lid
    if ( m_rWW8Export.m_pFib->getNumDecimalSep() == '.' )
        aStr += ",";
    else
        aStr += ";";

    m_rWW8Export.OutputField( nullptr, ww::eEQ, aStr,
            FieldFlags::Start | FieldFlags::CmdStart );
}

void WW8AttributeOutput::EndRuby(const SwTextNode& /*rNode*/, sal_Int32 /*nPos*/, bool /*bEmptyBaseText*/)
{
    m_rWW8Export.WriteChar( ')' );
    m_rWW8Export.OutputField( nullptr, ww::eEQ, OUString(), FieldFlags::End | FieldFlags::Close );
}

OUString AttributeOutputBase::ConvertURL( const OUString& rUrl, bool bAbsoluteOut )
{
    OUString sURL = rUrl;

    INetURLObject anAbsoluteParent(m_sBaseURL);
    OUString sConvertedParent = INetURLObject::GetScheme( anAbsoluteParent.GetProtocol() ) + anAbsoluteParent.GetURLPath();
    OUString sParentPath = sConvertedParent.isEmpty() ? m_sBaseURL : sConvertedParent;

    if ( bAbsoluteOut )
    {
        INetURLObject anAbsoluteNew;

        if ( anAbsoluteParent.GetNewAbsURL( rUrl, &anAbsoluteNew ) )
            sURL = anAbsoluteNew.GetMainURL( INetURLObject::DecodeMechanism::NONE );
    }
    else
    {
        OUString sToConvert = rUrl.replaceAll( "\\", "/" );
        INetURLObject aURL( sToConvert );
        sToConvert = INetURLObject::GetScheme( aURL.GetProtocol() ) + aURL.GetURLPath();
        OUString sRelative = INetURLObject::GetRelURL( sParentPath, sToConvert, INetURLObject::EncodeMechanism::WasEncoded, INetURLObject::DecodeMechanism::NONE );
        if ( !sRelative.isEmpty() )
            sURL = sRelative;
    }

    return sURL;
}

bool AttributeOutputBase::AnalyzeURL( const OUString& rUrl, const OUString& /*rTarget*/, OUString* pLinkURL, OUString* pMark )
{
    bool bBookMarkOnly = false;

    OUString sMark;
    OUString sURL;

    if ( rUrl.getLength() > 1 && rUrl[0] == '#' )
    {
        sMark = BookmarkToWriter( rUrl.subView(1) );

        const sal_Int32 nPos = sMark.lastIndexOf( cMarkSeparator );

        const OUString sRefType(nPos>=0 && nPos+1<sMark.getLength() ?
                                sMark.copy(nPos+1).replaceAll(" ", "") :
                                OUString());

        // #i21465# Only interested in outline references
        if ( !sRefType.isEmpty() &&
            (sRefType == "outline" || sRefType == "graphic" || sRefType == "frame" || sRefType == "ole" || sRefType == "region" || sRefType == "table") )
        {
            for ( const auto& rBookmarkPair : GetExport().m_aImplicitBookmarks )
            {
                if ( rBookmarkPair.first == sMark )
                {
                    sMark = "_toc" + OUString::number( sal_Int32(rBookmarkPair.second) );
                    break;
                }
            }
        }
    }
    else
    {
        INetURLObject aURL( rUrl, INetProtocol::NotValid );
        sURL = aURL.GetURLNoMark( INetURLObject::DecodeMechanism::NONE );
        sMark = aURL.GetMark( INetURLObject::DecodeMechanism::NONE );
        INetProtocol aProtocol = aURL.GetProtocol();

        if ( aProtocol == INetProtocol::File || aProtocol == INetProtocol::NotValid )
        {
            // INetProtocol::NotValid - may be a relative link
            bool bExportRelative = officecfg::Office::Common::Save::URL::FileSystem::get();
            sURL = ConvertURL( rUrl, !bExportRelative );
        }
    }

    if ( !sMark.isEmpty() && sURL.isEmpty() )
        bBookMarkOnly = true;

    *pMark = sMark;
    *pLinkURL = sURL;
    return bBookMarkOnly;
}

bool WW8AttributeOutput::AnalyzeURL( const OUString& rUrl, const OUString& rTarget, OUString* pLinkURL, OUString* pMark )
{
    bool bBookMarkOnly = AttributeOutputBase::AnalyzeURL( rUrl, rTarget, pLinkURL, pMark );

    OUString sURL = *pLinkURL;

    if ( !sURL.isEmpty() )
        sURL = URIHelper::simpleNormalizedMakeRelative( m_rWW8Export.GetWriter().GetBaseURL(), sURL );

    if (bBookMarkOnly)
    {
        sURL = FieldString(ww::eHYPERLINK);
        *pMark = GetExport().BookmarkToWord(*pMark);
    }
    else
        sURL = FieldString( ww::eHYPERLINK ) + "\"" + sURL + "\"";

    if ( !pMark->isEmpty() )
        sURL += " \\l \"" + *pMark + "\"";

    if ( !rTarget.isEmpty() )
        sURL += " \\n " + rTarget;

    *pLinkURL = sURL;

    return bBookMarkOnly;
}

void WW8AttributeOutput::WriteBookmarkInActParagraph( const OUString& rName, sal_Int32 nFirstRunPos, sal_Int32 nLastRunPos )
{
    m_aBookmarksOfParagraphStart.insert(std::pair<sal_Int32, OUString>(nFirstRunPos, rName));
    m_aBookmarksOfParagraphEnd.insert(std::pair<sal_Int32, OUString>(nLastRunPos, rName));
}

bool WW8AttributeOutput::StartURL(const OUString& rUrl, const OUString& rTarget, const OUString& /*rName*/)
{
    INetURLObject aURL( rUrl );
    OUString sURL;
    OUString sMark;

    bool bBookMarkOnly = AnalyzeURL( rUrl, rTarget, &sURL, &sMark );

    m_rWW8Export.OutputField( nullptr, ww::eHYPERLINK, sURL, FieldFlags::Start | FieldFlags::CmdStart );

    // write the reference to the "picture" structure
    sal_uInt64 nDataStt = m_rWW8Export.m_pDataStrm->Tell();
    m_rWW8Export.m_pChpPlc->AppendFkpEntry( m_rWW8Export.Strm().Tell() );

    // WinWord 2000 doesn't write this - so it's a temp solution by W97 ?
    m_rWW8Export.WriteChar( 0x01 );

    sal_uInt8 aArr1[] = {
        0x03, 0x6a, 0,0,0,0,    // sprmCPicLocation

        0x06, 0x08, 0x01,       // sprmCFData
        0x55, 0x08, 0x01,       // sprmCFSpec
        0x02, 0x08, 0x01        // sprmCFFieldVanish
    };
    sal_uInt8* pDataAdr = aArr1 + 2;
    Set_UInt32( pDataAdr, nDataStt );

    m_rWW8Export.m_pChpPlc->AppendFkpEntry( m_rWW8Export.Strm().Tell(), sizeof( aArr1 ), aArr1 );

    m_rWW8Export.OutputField( nullptr, ww::eHYPERLINK, sURL, FieldFlags::CmdEnd );

    // now write the picture structure
    sURL = aURL.GetURLNoMark();

    // Compare the URL written by AnalyzeURL with the original one to see if
    // the output URL is absolute or relative.
    OUString sRelativeURL;
    if ( !rUrl.isEmpty() )
        sRelativeURL = URIHelper::simpleNormalizedMakeRelative( m_rWW8Export.GetWriter().GetBaseURL(), rUrl );
    bool bAbsolute = sRelativeURL == rUrl;

    static const sal_uInt8 aURLData1[] = {
        0,0,0,0,        // len of struct
        0x44,0,         // the start of "next" data
        0,0,0,0,0,0,0,0,0,0,                // PIC-Structure!
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,    //  |
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,    //  |
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,    //  |
        0,0,0,0,                            // /
    };
    static const sal_uInt8 MAGIC_A[] = {
        // start of "next" data
        0xD0,0xC9,0xEA,0x79,0xF9,0xBA,0xCE,0x11,
        0x8C,0x82,0x00,0xAA,0x00,0x4B,0xA9,0x0B
    };

    m_rWW8Export.m_pDataStrm->WriteBytes(aURLData1, sizeof(aURLData1));
    /* Write HFD Structure */
    sal_uInt8 nAnchor = 0x00;
    if ( !sMark.isEmpty() )
        nAnchor = 0x08;
    m_rWW8Export.m_pDataStrm->WriteUChar(nAnchor); // HFDBits
    m_rWW8Export.m_pDataStrm->WriteBytes(MAGIC_A, sizeof(MAGIC_A)); //clsid

    /* Write Hyperlink Object see [MS-OSHARED] spec*/
    SwWW8Writer::WriteLong( *m_rWW8Export.m_pDataStrm, 0x00000002);
    sal_uInt32 nFlag = bBookMarkOnly ? 0 : 0x01;
    if ( bAbsolute )
        nFlag |= 0x02;
    if ( !sMark.isEmpty() )
        nFlag |= 0x08;
    SwWW8Writer::WriteLong( *m_rWW8Export.m_pDataStrm, nFlag );

    INetProtocol eProto = aURL.GetProtocol();
    if ( eProto == INetProtocol::File || eProto == INetProtocol::Smb )
    {
        // version 1 (for a document)

        static const sal_uInt8 MAGIC_C[] = {
            0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
            0x00, 0x00
        };

        static const sal_uInt8 MAGIC_D[] = {
            0xFF, 0xFF, 0xAD, 0xDE, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };

        // save the links to files as relative
        sURL = URIHelper::simpleNormalizedMakeRelative( m_rWW8Export.GetWriter().GetBaseURL(), sURL );
        if ( eProto == INetProtocol::File && sURL.startsWith( "/" ) )
            sURL = aURL.PathToFileName();

        // special case for the absolute windows names
        // (convert '/c:/foo/bar.doc' into 'c:\foo\bar.doc')
        if (sURL.getLength()>=3)
        {
            const sal_Unicode aDrive = sURL[1];
            if ( sURL[0]=='/' && sURL[2]==':' &&
                 ( (aDrive>='A' && aDrive<='Z' ) || (aDrive>='a' && aDrive<='z') ) )
            {
                sURL = sURL.copy(1).replaceAll("/", "\\");
            }
        }

        // n#261623 convert smb notation to '\\'
        const char pSmb[] = "smb://";
        if ( eProto == INetProtocol::Smb && sURL.startsWith( pSmb ) )
        {
            sURL = sURL.copy( sizeof(pSmb)-3 ).replaceAll( "/", "\\" );
        }

        m_rWW8Export.m_pDataStrm->WriteBytes(MAGIC_C, sizeof(MAGIC_C));
        SwWW8Writer::WriteLong( *m_rWW8Export.m_pDataStrm, sURL.getLength()+1 );
        SwWW8Writer::WriteString8( *m_rWW8Export.m_pDataStrm, sURL, true,
                                    RTL_TEXTENCODING_MS_1252 );
        m_rWW8Export.m_pDataStrm->WriteBytes(MAGIC_D, sizeof(MAGIC_D));

        SwWW8Writer::WriteLong( *m_rWW8Export.m_pDataStrm, 2*sURL.getLength() + 6 );
        SwWW8Writer::WriteLong( *m_rWW8Export.m_pDataStrm, 2*sURL.getLength() );
        SwWW8Writer::WriteShort( *m_rWW8Export.m_pDataStrm, 3 );
        SwWW8Writer::WriteString16( *m_rWW8Export.m_pDataStrm, sURL, false );
    }
    else if ( eProto != INetProtocol::NotValid )
    {
        // version 2 (simple url)
        // and write some data to the data stream, but don't ask
        // what the data mean, except for the URL.
        // The First piece is the WW8_PIC structure.
        static const sal_uInt8 MAGIC_B[] = {
            0xE0,0xC9,0xEA,0x79,0xF9,0xBA,0xCE,0x11,
            0x8C,0x82,0x00,0xAA,0x00,0x4B,0xA9,0x0B
        };

        m_rWW8Export.m_pDataStrm->WriteBytes(MAGIC_B, sizeof(MAGIC_B));
        SwWW8Writer::WriteLong( *m_rWW8Export.m_pDataStrm, 2 * ( sURL.getLength() + 1 ) );
        SwWW8Writer::WriteString16( *m_rWW8Export.m_pDataStrm, sURL, true );
    }

    if ( !sMark.isEmpty() )
    {
        SwWW8Writer::WriteLong( *m_rWW8Export.m_pDataStrm, sMark.getLength()+1 );
        SwWW8Writer::WriteString16( *m_rWW8Export.m_pDataStrm, sMark, true );
    }
    SwWW8Writer::WriteLong( *m_rWW8Export.m_pDataStrm, nDataStt,
        m_rWW8Export.m_pDataStrm->Tell() - nDataStt );

    return true;
}

bool WW8AttributeOutput::EndURL(bool const)
{
    m_rWW8Export.OutputField( nullptr, ww::eHYPERLINK, OUString(), FieldFlags::Close );

    return true;
}

OUString BookmarkToWriter(std::u16string_view rBookmark)
{
    return INetURLObject::decode(rBookmark,
        INetURLObject::DecodeMechanism::Unambiguous, RTL_TEXTENCODING_ASCII_US);
}

void SwWW8AttrIter::OutSwFormatRefMark(const SwFormatRefMark& rAttr)
{
    if(m_rExport.HasRefToAttr(rAttr.GetRefName().toString()))
        m_rExport.AppendBookmark( m_rExport.GetBookmarkName( ReferencesSubtype::SetRefAttr,
                                            &rAttr.GetRefName().toString(), 0 ));
}

void SwWW8AttrIter::SplitRun( sal_Int32 nSplitEndPos )
{
    auto aIter = std::find_if(maCharRuns.begin(), maCharRuns.end(),
        [nSplitEndPos](const CharRunEntry& rCharRun) { return rCharRun.mnEndPos >= nSplitEndPos; });
    if (aIter == maCharRuns.end() || aIter->mnEndPos == nSplitEndPos)
        return;

    CharRunEntry aNewEntry = *aIter;
    aIter->mnEndPos = nSplitEndPos;
    maCharRuns.insert( ++aIter, aNewEntry);
    maCharRunIter = maCharRuns.begin();
    IterToCurrent();
    m_nCurrentSwPos = SearchNext(1);
}

void WW8AttributeOutput::FieldVanish(const OUString& rText, ww::eField /*eType*/, OUString const*const /*pBookmarkName*/)
{
    ww::bytes aItems;
    m_rWW8Export.GetCurrentItems( aItems );

    // sprmCFFieldVanish
    SwWW8Writer::InsUInt16( aItems, NS_sprm::CFFldVanish::val );
    aItems.push_back( 1 );

    sal_uInt16 nStt_sprmCFSpec = aItems.size();

    // sprmCFSpec --  fSpec-Attribute true
    SwWW8Writer::InsUInt16( aItems, 0x855 );
    aItems.push_back( 1 );

    m_rWW8Export.WriteChar( '\x13' );
    m_rWW8Export.m_pChpPlc->AppendFkpEntry( m_rWW8Export.Strm().Tell(), aItems.size(),
                                    aItems.data() );
    m_rWW8Export.OutSwString(rText, 0, rText.getLength());
    m_rWW8Export.m_pChpPlc->AppendFkpEntry( m_rWW8Export.Strm().Tell(), nStt_sprmCFSpec,
                                    aItems.data() );
    m_rWW8Export.WriteChar( '\x15' );
    m_rWW8Export.m_pChpPlc->AppendFkpEntry( m_rWW8Export.Strm().Tell(), aItems.size(),
                                    aItems.data() );
}

void AttributeOutputBase::TOXMark( const SwTextNode& rNode, const SwTOXMark& rAttr )
{
    // it's a field; so get the Text from the Node and build the field
    OUString sText;
    ww::eField eType = ww::eNONE;

    const SwTextTOXMark& rTextTOXMark = *rAttr.GetTextTOXMark();
    const sal_Int32* pTextEnd = rTextTOXMark.End();
    if ( pTextEnd ) // has range?
    {
        sText = rNode.GetExpandText(nullptr, rTextTOXMark.GetStart(),
                                   *pTextEnd - rTextTOXMark.GetStart() );
    }
    else
        sText = rAttr.GetAlternativeText();

    OUString sUserTypeName;
    auto aType = rAttr.GetTOXType()->GetType();
    // user index mark, it needs XE with \f
    if ( TOX_USER == aType )
    {
        sUserTypeName = rAttr.GetTOXType()->GetTypeName();
        if ( !sUserTypeName.isEmpty() )
            aType = TOX_INDEX;
    }

    switch ( aType )
    {
        case TOX_INDEX:
            eType = ww::eXE;
            if ( !rAttr.GetPrimaryKey().isEmpty() )
            {
                if ( !rAttr.GetSecondaryKey().isEmpty() )
                {
                    sText = rAttr.GetSecondaryKey() + ":" + sText;
                }

                sText = rAttr.GetPrimaryKey() + ":" + sText;
            }
            sText = " XE \"" + sText + "\" ";

            if (!sUserTypeName.isEmpty())
            {
                sText += "\\f \"" + sUserTypeName + "\" ";
            }
            break;

        case TOX_USER:
            sText += "\" \\f \"" + OUStringChar(static_cast<char>( 'A' + GetExport( ).GetId( *rAttr.GetTOXType() ) ));
            [[fallthrough]];
        case TOX_CONTENT:
            {
                eType = ww::eTC;
                sText = " TC \"" + sText;
                sal_uInt16 nLvl = rAttr.GetLevel();
                if (nLvl > WW8ListManager::nMaxLevel)
                    nLvl = WW8ListManager::nMaxLevel;

                sText += "\" \\l " + OUString::number(nLvl) + " ";
            }
            break;
        default:
            OSL_ENSURE( false, "Unhandled option for toc export" );
            break;
    }

    if (!sText.isEmpty())
    {
        OUString const* pBookmarkName(nullptr);
        if (auto const it = GetExport().m_TOXMarkBookmarksByTOXMark.find(&rAttr);
            it != GetExport().m_TOXMarkBookmarksByTOXMark.end())
        {
            pBookmarkName = &it->second;
        }
        FieldVanish(sText, eType, pBookmarkName);
    }
}

int SwWW8AttrIter::OutAttrWithRange(const SwTextNode& rNode, sal_Int32 nPos)
{
    int nRet = 0;
    if ( const SwpHints* pTextAttrs = m_rNode.GetpSwpHints() )
    {
        m_rExport.m_aCurrentCharPropStarts.push( nPos );
        const sal_Int32* pEnd;
        // first process ends of attributes with extent
        for (size_t i = 0; i < pTextAttrs->Count(); ++i)
        {
            const SwTextAttr* pHt = pTextAttrs->GetSortedByEnd(i);
            const SfxPoolItem* pItem = &pHt->GetAttr();
            switch ( pItem->Which() )
            {
                case RES_TXTATR_INETFMT:
                    pEnd = pHt->End();
                    if (nPos == *pEnd && nPos != pHt->GetStart())
                    {
                        if (m_rExport.AttrOutput().EndURL(nPos == m_rNode.Len()))
                            --nRet;
                    }
                    break;
                case RES_TXTATR_REFMARK:
                    pEnd = pHt->End();
                    if (nullptr != pEnd && nPos == *pEnd && nPos != pHt->GetStart())
                    {
                        OutSwFormatRefMark(*static_cast<const SwFormatRefMark*>(pItem));
                        --nRet;
                    }
                    break;
                case RES_TXTATR_CJK_RUBY:
                    pEnd = pHt->End();
                    if (nPos == *pEnd && nPos != pHt->GetStart())
                    {
                        m_rExport.AttrOutput().EndRuby(rNode, nPos, false);
                        --nRet;
                    }
                    break;
            }
            if (nPos < pHt->GetAnyEnd())
                break; // sorted by end
        }
        for ( size_t i = 0; i < pTextAttrs->Count(); ++i )
        {
            const SwTextAttr* pHt = pTextAttrs->Get(i);
            const SfxPoolItem* pItem = &pHt->GetAttr();
            switch ( pItem->Which() )
            {
                case RES_TXTATR_INETFMT:
                    if ( nPos == pHt->GetStart() )
                    {
                        const SwFormatINetFormat *rINet = static_cast< const SwFormatINetFormat* >( pItem );
                        if (m_rExport.AttrOutput().StartURL(rINet->GetValue(), rINet->GetTargetFrame(), rINet->GetName()))
                            ++nRet;
                    }
                    pEnd = pHt->End();
                    if (nPos == *pEnd && nPos == pHt->GetStart())
                    {   // special case: empty must be handled here
                        if (m_rExport.AttrOutput().EndURL(nPos == m_rNode.Len()))
                            --nRet;
                    }
                    break;
                case RES_TXTATR_REFMARK:
                    if ( nPos == pHt->GetStart() )
                    {
                        OutSwFormatRefMark( *static_cast< const SwFormatRefMark* >( pItem ) );
                        ++nRet;
                    }
                    pEnd = pHt->End();
                    if (nullptr != pEnd && nPos == *pEnd && nPos == pHt->GetStart())
                    {   // special case: empty TODO: is this possible or would empty one have pEnd null?
                        OutSwFormatRefMark( *static_cast< const SwFormatRefMark* >( pItem ) );
                        --nRet;
                    }
                    break;
                case RES_TXTATR_TOXMARK:
                    if ( nPos == pHt->GetStart() )
                        m_rExport.AttrOutput().TOXMark( m_rNode, *static_cast< const SwTOXMark* >( pItem ) );
                    break;
                case RES_TXTATR_CJK_RUBY:
                    if ( nPos == pHt->GetStart() )
                    {
                        m_rExport.AttrOutput().StartRuby( m_rNode, nPos, *static_cast< const SwFormatRuby* >( pItem ) );
                        ++nRet;
                    }
                    pEnd = pHt->End();
                    if (nPos == *pEnd && nPos == pHt->GetStart())
                    {   // special case: empty must be handled here
                        m_rExport.AttrOutput().EndRuby(m_rNode, nPos, true);
                        --nRet;
                    }
                    break;
            }
            if (nPos < pHt->GetStart())
                break; // sorted by start
        }
        m_rExport.m_aCurrentCharPropStarts.pop(); // HasTextItem only allowed in the above range
    }
    return nRet;
}

bool SwWW8AttrIter::IncludeEndOfParaCRInRedlineProperties( sal_Int32 nEnd ) const
{
    // search next Redline
    for( SwRedlineTable::size_type nPos = m_nCurRedlinePos;
        nPos < m_rExport.m_rDoc.getIDocumentRedlineAccess().GetRedlineTable().size(); ++nPos )
    {
        const SwRangeRedline *pRange = m_rExport.m_rDoc.getIDocumentRedlineAccess().GetRedlineTable()[nPos];
        const SwPosition* pEnd = pRange->End();
        const SwPosition* pStart = pRange->Start();
        bool bBreak = true;
        // In word the paragraph end marker is a real character, in writer it is not.
        // Here we find out if the para end marker we will emit is affected by
        // redlining, in which case it must be included by the range of character
        // attributes that contains the redlining information.
        if (pEnd->GetNode() == m_rNode)
        {
            if (pEnd->GetContentIndex() == nEnd)
            {
                // This condition detects if the pseudo-char we will export
                // should be explicitly included by the redlining char
                // properties on this node because the redlining ends right
                // after it
                return true;
            }
            bBreak = false;
        }
        if (pStart->GetNode() == m_rNode)
        {
            if (pStart->GetContentIndex() == nEnd)
            {
                // This condition detects if the pseudo-char we will export
                // should be explicitly included by the redlining char
                // properties on this node because the redlining starts right
                // before it
                return true;
            }
            bBreak = false;
        }
        if (pStart->GetNodeIndex()-1 == m_rNode.GetIndex())
        {
            if (pStart->GetContentIndex() == 0)
            {
                // This condition detects if the pseudo-char we will export
                // should be implicitly excluded by the redlining char
                // properties starting on the next node.
                return true;
            }
            bBreak = false;
        }

        if (bBreak)
            break;
    }
    return false;
}

const SwRedlineData* SwWW8AttrIter::GetParagraphLevelRedline( )
{
    m_pCurRedline = nullptr;

    // ToDo : this is not the most ideal ... should start maybe from 'nCurRedlinePos'
    for(SwRangeRedline* pRedl : m_rExport.m_rDoc.getIDocumentRedlineAccess().GetRedlineTable())
    {
        const SwPosition* pCheckedStt = pRedl->Start();

        if( pCheckedStt->GetNode() == m_rNode )
        {
            // Maybe add here a check that also the start & end of the redline is the entire paragraph

            // Only return if this is a paragraph formatting redline
            if (pRedl->GetType() == RedlineType::ParagraphFormat)
            {
                // write data of this redline
                m_pCurRedline = pRedl;
                return &( m_pCurRedline->GetRedlineData() );
            }
        }
    }
    return nullptr;
}

const SwRedlineData* SwWW8AttrIter::GetRunLevelRedline( sal_Int32 nPos )
{
    if( m_pCurRedline )
    {
        const SwPosition* pEnd = m_pCurRedline->End();
        if (pEnd->GetNode() != m_rNode || pEnd->GetContentIndex() > nPos)
        {
            switch( m_pCurRedline->GetType() )
            {
                case RedlineType::Insert:
                case RedlineType::Delete:
                case RedlineType::Format:
                    // write data of this redline
                    return &( m_pCurRedline->GetRedlineData() );
                default:
                    break;
            }
        }
        m_pCurRedline = nullptr;
        ++m_nCurRedlinePos;
    }

    assert(!m_pCurRedline);
    // search next Redline
    for( ; m_nCurRedlinePos < m_rExport.m_rDoc.getIDocumentRedlineAccess().GetRedlineTable().size();
            ++m_nCurRedlinePos )
    {
        const SwRangeRedline* pRedl = m_rExport.m_rDoc.getIDocumentRedlineAccess().GetRedlineTable()[ m_nCurRedlinePos ];

        auto [pStart, pEnd] = pRedl->StartEnd(); // SwPosition*

        if( pStart->GetNode() == m_rNode )
        {
            if( pStart->GetContentIndex() >= nPos )
            {
                if( pStart->GetContentIndex() == nPos )
                {
                        switch( pRedl->GetType() )
                        {
                            case RedlineType::Insert:
                            case RedlineType::Delete:
                            case RedlineType::Format:
                                // write data of this redline
                                m_pCurRedline = pRedl;
                                return &( m_pCurRedline->GetRedlineData() );
                            default:
                                break;
                        }
                }
                break;
            }
        }
        else
        {
            break;
        }

        if( pEnd->GetNode() == m_rNode &&
            pEnd->GetContentIndex() < nPos )
        {
            m_pCurRedline = pRedl;
            break;
        }
    }
    return nullptr;
}

SvxFrameDirection MSWordExportBase::GetCurrentPageDirection() const
{
    const SwFrameFormat &rFormat = m_pCurrentPageDesc
                    ? m_pCurrentPageDesc->GetMaster()
                    : m_rDoc.GetPageDesc( 0 ).GetMaster();
    return rFormat.GetFrameDir().GetValue();
}

SvxFrameDirection MSWordExportBase::GetDefaultFrameDirection( ) const
{
    SvxFrameDirection nDir = SvxFrameDirection::Environment;

    if ( m_bOutPageDescs )
        nDir = GetCurrentPageDirection(  );
    else if ( m_pOutFormatNode )
    {
        if ( m_bOutFlyFrameAttrs ) //frame
        {
            nDir = TrueFrameDirection( *static_cast< const SwFrameFormat * >(m_pOutFormatNode) );
        }
        else if ( auto pNd = dynamic_cast< const SwContentNode *>( m_pOutFormatNode ) )    //paragraph
        {
            SwPosition aPos( *pNd );
            nDir = m_rDoc.GetTextDirection( aPos );
        }
        else if ( dynamic_cast< const SwTextFormatColl *>( m_pOutFormatNode ) !=  nullptr )
        {
            if ( MsLangId::isRightToLeft( GetAppLanguage()) )
                nDir = SvxFrameDirection::Horizontal_RL_TB;
            else
                nDir = SvxFrameDirection::Horizontal_LR_TB;    //what else can we do :-(
        }
    }

    if ( nDir == SvxFrameDirection::Environment )
    {
        // fdo#44029 put direction right when the locale are RTL.
        if( MsLangId::isRightToLeft( GetAppLanguage()) )
            nDir = SvxFrameDirection::Horizontal_RL_TB;
        else
            nDir = SvxFrameDirection::Horizontal_LR_TB;        //Set something
    }

    return nDir;
}

SvxFrameDirection MSWordExportBase::TrueFrameDirection( const SwFrameFormat &rFlyFormat ) const
{
    const SwFrameFormat *pFlyFormat = &rFlyFormat;
    const SvxFrameDirectionItem* pItem = nullptr;
    while ( pFlyFormat )
    {
        pItem = &pFlyFormat->GetFrameDir();
        if ( SvxFrameDirection::Environment == pItem->GetValue() )
        {
            pItem = nullptr;
            const SwFormatAnchor* pAnchor = &pFlyFormat->GetAnchor();
            if ((RndStdIds::FLY_AT_PAGE != pAnchor->GetAnchorId()) &&
                pAnchor->GetAnchorNode() )
            {
                pFlyFormat = pAnchor->GetAnchorNode()->GetFlyFormat();
            }
            else
                pFlyFormat = nullptr;
        }
        else
            pFlyFormat = nullptr;
    }

    SvxFrameDirection nRet;
    if ( pItem )
        nRet = pItem->GetValue();
    else
        nRet = GetCurrentPageDirection();

    OSL_ENSURE( nRet != SvxFrameDirection::Environment, "leaving with environment direction" );
    return nRet;
}

const SvxBrushItem* WW8Export::GetCurrentPageBgBrush() const
{
    const SwFrameFormat  &rFormat = m_pCurrentPageDesc
                    ? m_pCurrentPageDesc->GetMaster()
                    : m_rDoc.GetPageDesc(0).GetMaster();

    //If not set, or "no fill", get real bg
    const SvxBrushItem* pRet = rFormat.GetItemIfSet(RES_BACKGROUND);

    if (!pRet ||
        (!pRet->GetGraphic() && pRet->GetColor() == COL_TRANSPARENT))
    {
        pRet = &m_rDoc.GetAttrPool().GetUserOrPoolDefaultItem(RES_BACKGROUND);
    }
    return pRet;
}

std::shared_ptr<SvxBrushItem> WW8Export::TrueFrameBgBrush(const SwFrameFormat &rFlyFormat) const
{
    const SwFrameFormat *pFlyFormat = &rFlyFormat;
    const SvxBrushItem* pRet = nullptr;

    while (pFlyFormat)
    {
        //If not set, or "no fill", get real bg
        pRet = pFlyFormat->GetItemIfSet(RES_BACKGROUND);
        if (!pRet || (!pRet->GetGraphic() &&
            pRet->GetColor() == COL_TRANSPARENT))
        {
            pRet = nullptr;
            const SwFormatAnchor* pAnchor = &pFlyFormat->GetAnchor();
            if ((RndStdIds::FLY_AT_PAGE != pAnchor->GetAnchorId()) &&
                pAnchor->GetAnchorNode())
            {
                pFlyFormat =
                    pAnchor->GetAnchorNode()->GetFlyFormat();
            }
            else
                pFlyFormat = nullptr;
        }
        else
            pFlyFormat = nullptr;
    }

    if (!pRet)
        pRet = GetCurrentPageBgBrush();

    const Color aTmpColor( COL_WHITE );
    std::shared_ptr<SvxBrushItem> aRet(std::make_shared<SvxBrushItem>(aTmpColor, RES_BACKGROUND));

    if (pRet && (pRet->GetGraphic() ||( pRet->GetColor() != COL_TRANSPARENT)))
    {
        aRet.reset(pRet->Clone());
    }

    return aRet;
}

/*
Convert characters that need to be converted, the basic replacements and the
ridiculously complicated title case attribute mapping to hardcoded upper case
because word doesn't have the feature
*/
OUString SwWW8AttrIter::GetSnippet(const OUString &rStr, sal_Int32 nCurrentPos,
    sal_Int32 nLen) const
{
    if (!nLen)
        return OUString();

    OUString aSnippet(rStr.copy(nCurrentPos, nLen));
    // 0x0a     ( Hard Line Break ) -> 0x0b
    // 0xad     ( soft hyphen )     -> 0x1f
    // 0x2011   ( hard hyphen )     -> 0x1e
    aSnippet = aSnippet.replace(0x0A, 0x0B);
    aSnippet = aSnippet.replace(CHAR_HARDHYPHEN, 0x1e);
    aSnippet = aSnippet.replace(CHAR_SOFTHYPHEN, 0x1f);
    // Ignore the dummy character at the end of content controls.
    static sal_Unicode const aForbidden[] = {
        CH_TXTATR_BREAKWORD,
        0
    };
    aSnippet = comphelper::string::removeAny(aSnippet, aForbidden);

    m_rExport.m_aCurrentCharPropStarts.push( nCurrentPos );
    const SfxPoolItem &rItem = GetItem(RES_CHRATR_CASEMAP);

    if (SvxCaseMap::Capitalize == static_cast<const SvxCaseMapItem&>(rItem).GetValue())
    {
        assert(g_pBreakIt && g_pBreakIt->GetBreakIter().is());
        sal_uInt16 nScriptType = g_pBreakIt->GetBreakIter()->getScriptType(aSnippet, 0);

        LanguageType nLanguage;
        switch (nScriptType)
        {
        case i18n::ScriptType::ASIAN:
                nLanguage = static_cast<const SvxLanguageItem&>(GetItem(RES_CHRATR_CJK_LANGUAGE)).GetLanguage();
                break;
        case i18n::ScriptType::COMPLEX:
                nLanguage = static_cast<const SvxLanguageItem&>(GetItem(RES_CHRATR_CTL_LANGUAGE)).GetLanguage();
                break;
        case i18n::ScriptType::LATIN:
            default:
                nLanguage = static_cast<const SvxLanguageItem&>(GetItem(RES_CHRATR_LANGUAGE)).GetLanguage();
                break;
        }

        SvxFont aFontHelper;
        aFontHelper.SetCaseMap(SvxCaseMap::Capitalize);
        aFontHelper.SetLanguage(nLanguage);
        aSnippet = aFontHelper.CalcCaseMap(aSnippet);

        //If we weren't at the begin of a word undo the case change.
        //not done before doing the casemap because the sequence might start
        //with whitespace
        if (!g_pBreakIt->GetBreakIter()->isBeginWord(
            rStr, nCurrentPos, g_pBreakIt->GetLocale(nLanguage),
            i18n::WordType::ANYWORD_IGNOREWHITESPACES ) )
        {
            aSnippet = OUStringChar(rStr[nCurrentPos]) + aSnippet.subView(1);
        }
    }
    m_rExport.m_aCurrentCharPropStarts.pop();

    return aSnippet;
}

/** Delivers the right paragraph style

    Because of the different style handling for delete operations,
    the track changes have to be analysed. A deletion, starting in paragraph A
    with style A, ending in paragraph B with style B, needs a hack.
    If paragraph B is not deleted completely, we should preserve the style here.
*/
static SwTextFormatColl& lcl_getFormatCollection( MSWordExportBase& rExport, const SwTextNode* pTextNode )
{
    SwRedlineTable::size_type nPos = 0;
    SwRedlineTable::size_type nMax = rExport.m_rDoc.getIDocumentRedlineAccess().GetRedlineTable().size();
    while( nPos < nMax )
    {
        const SwRangeRedline* pRedl = rExport.m_rDoc.getIDocumentRedlineAccess().GetRedlineTable()[ nPos++ ];
        auto [pStart, pEnd] = pRedl->StartEnd(); // SwPosition*
        // Looking for deletions, which ends in current pTextNode
        if( RedlineType::Delete == pRedl->GetRedlineData().GetType() &&
            pEnd->GetNode() == *pTextNode && pStart->GetNode() != *pTextNode &&
            pStart->GetNode().IsTextNode() &&
            pEnd->GetContentIndex() == pEnd->GetNode().GetTextNode()->GetText().getLength())
        {
            pTextNode = pStart->GetNode().GetTextNode();
            nMax = nPos;
            nPos = 0;
        }
    }
    return static_cast<SwTextFormatColl&>( pTextNode->GetAnyFormatColl() );
}

void WW8AttributeOutput::FormatDrop( const SwTextNode& rNode, const SwFormatDrop &rSwFormatDrop, sal_uInt16 nStyle,
        ww8::WW8TableNodeInfo::Pointer_t pTextNodeInfo, ww8::WW8TableNodeInfoInner::Pointer_t pTextNodeInfoInner )
{
    short nDropLines = rSwFormatDrop.GetLines();
    short nDistance = rSwFormatDrop.GetDistance();
    int rFontHeight, rDropHeight, rDropDescent;

    SVBT16 nSty;
    ShortToSVBT16( nStyle, nSty );
    m_rWW8Export.m_pO->insert( m_rWW8Export.m_pO->end(), nSty, nSty+2 );     // Style #

    m_rWW8Export.InsUInt16( NS_sprm::PPc::val );            // Alignment (sprmPPc)
    m_rWW8Export.m_pO->push_back( 0x20 );

    m_rWW8Export.InsUInt16( NS_sprm::PWr::val );            // Wrapping (sprmPWr)
    m_rWW8Export.m_pO->push_back( 0x02 );

    m_rWW8Export.InsUInt16( NS_sprm::PDcs::val );            // Dropcap (sprmPDcs)
    int nDCS = ( nDropLines << 3 ) | 0x01;
    m_rWW8Export.InsUInt16( static_cast< sal_uInt16 >( nDCS ) );

    m_rWW8Export.InsUInt16( NS_sprm::PDxaFromText::val );            // Distance from text (sprmPDxaFromText)
    m_rWW8Export.InsUInt16( nDistance );

    if ( rNode.GetDropSize( rFontHeight, rDropHeight, rDropDescent ) )
    {
        m_rWW8Export.InsUInt16( NS_sprm::PDyaLine::val );            // Line spacing
        m_rWW8Export.InsUInt16( static_cast< sal_uInt16 >( -rDropHeight ) );
        m_rWW8Export.InsUInt16( 0 );
    }

    m_rWW8Export.WriteCR( pTextNodeInfoInner );

    if ( pTextNodeInfo )
    {
#ifdef DBG_UTIL
        SAL_INFO( "sw.ww8", pTextNodeInfo->toString());
#endif
        TableInfoCell( pTextNodeInfoInner );
    }

    m_rWW8Export.m_pPapPlc->AppendFkpEntry( m_rWW8Export.Strm().Tell(), m_rWW8Export.m_pO->size(), m_rWW8Export.m_pO->data() );
    m_rWW8Export.m_pO->clear();

    if ( rNode.GetDropSize( rFontHeight, rDropHeight, rDropDescent ) )
    {
        const SwCharFormat *pSwCharFormat = rSwFormatDrop.GetCharFormat();
        if ( pSwCharFormat )
        {
            m_rWW8Export.InsUInt16( NS_sprm::CIstd::val );
            m_rWW8Export.InsUInt16( m_rWW8Export.GetId( pSwCharFormat ) );
        }

        m_rWW8Export.InsUInt16( NS_sprm::CHpsPos::val );            // Lower the chars
        m_rWW8Export.InsUInt16( static_cast< sal_uInt16 >( -((nDropLines - 1)*rDropDescent) / 10 ) );

        m_rWW8Export.InsUInt16( NS_sprm::CHps::val );            // Font Size
        m_rWW8Export.InsUInt16( static_cast< sal_uInt16 >( rFontHeight / 10 ) );
    }

    m_rWW8Export.m_pChpPlc->AppendFkpEntry( m_rWW8Export.Strm().Tell(), m_rWW8Export.m_pO->size(), m_rWW8Export.m_pO->data() );
    m_rWW8Export.m_pO->clear();
}

sal_Int32 MSWordExportBase::GetNextPos( SwWW8AttrIter const * aAttrIter, const SwTextNode& rNode, sal_Int32 nCurrentPos )
{
    // Get the bookmarks for the normal run
    const sal_Int32 nNextPos = aAttrIter->WhereNext();
    sal_Int32 nNextBookmark = nNextPos;
    sal_Int32 nNextAnnotationMark = nNextPos;

    if( nNextBookmark > nCurrentPos ) //no need to search for bookmarks otherwise (checked in UpdatePosition())
    {
        GetSortedBookmarks( rNode, nCurrentPos, nNextBookmark - nCurrentPos );
        NearestBookmark( nNextBookmark, nCurrentPos, false );
        GetSortedAnnotationMarks(*aAttrIter, nCurrentPos, nNextAnnotationMark - nCurrentPos);
        NearestAnnotationMark( nNextAnnotationMark, nCurrentPos, false );
    }
    return std::min( nNextPos, std::min( nNextBookmark, nNextAnnotationMark ) );
}

void MSWordExportBase::UpdatePosition( SwWW8AttrIter* aAttrIter, sal_Int32 nCurrentPos )
{
    sal_Int32 nNextPos;

    // go to next attribute if no bookmark is found or if the bookmark is after the next attribute position
    // It may happened that the WhereNext() wasn't used in the previous increment because there was a
    // bookmark before it. Use that position before trying to find another one.
    bool bNextBookmark = NearestBookmark( nNextPos, nCurrentPos, true );
    if( nCurrentPos == aAttrIter->WhereNext() && ( !bNextBookmark || nNextPos > aAttrIter->WhereNext() ) )
        aAttrIter->NextPos();
}

bool MSWordExportBase::GetBookmarks( const SwTextNode& rNd, sal_Int32 nStt,
                    sal_Int32 nEnd, IMarkVector& rArr )
{
    IDocumentMarkAccess* const pMarkAccess = m_rDoc.getIDocumentMarkAccess();

    const sal_Int32 nMarks = pMarkAccess->getAllMarksCount();
    for ( sal_Int32 i = 0; i < nMarks; i++ )
    {
        MarkBase* pMark = pMarkAccess->getAllMarksBegin()[i];

        switch (IDocumentMarkAccess::GetType( *pMark ))
        {
            case IDocumentMarkAccess::MarkType::UNO_BOOKMARK:
            case IDocumentMarkAccess::MarkType::DDE_BOOKMARK:
            case IDocumentMarkAccess::MarkType::ANNOTATIONMARK:
            case IDocumentMarkAccess::MarkType::TEXT_FIELDMARK:
            case IDocumentMarkAccess::MarkType::CHECKBOX_FIELDMARK:
            case IDocumentMarkAccess::MarkType::DROPDOWN_FIELDMARK:
            case IDocumentMarkAccess::MarkType::DATE_FIELDMARK:
            case IDocumentMarkAccess::MarkType::NAVIGATOR_REMINDER:
                continue; // ignore irrelevant marks
            case IDocumentMarkAccess::MarkType::BOOKMARK:
            case IDocumentMarkAccess::MarkType::CROSSREF_HEADING_BOOKMARK:
            case IDocumentMarkAccess::MarkType::CROSSREF_NUMITEM_BOOKMARK:
                break;
        }

        // Only keep the bookmarks starting or ending in this node
        if ( pMark->GetMarkStart().GetNode() == rNd ||
             pMark->GetMarkEnd().GetNode() == rNd )
        {
            const sal_Int32 nBStart = pMark->GetMarkStart().GetContentIndex();
            const sal_Int32 nBEnd = pMark->GetMarkEnd().GetContentIndex();

            // Keep only the bookmarks starting or ending in the snippet
            bool bIsStartOk = ( pMark->GetMarkStart().GetNode() == rNd ) && ( nBStart >= nStt ) && ( nBStart <= nEnd );
            bool bIsEndOk = ( pMark->GetMarkEnd().GetNode() == rNd ) && ( nBEnd >= nStt ) && ( nBEnd <= nEnd );

            if ( bIsStartOk || bIsEndOk )
            {
                rArr.push_back( pMark );
            }
        }
    }
    return ( !rArr.empty() );
}

bool MSWordExportBase::GetAnnotationMarks( const SwWW8AttrIter& rAttrs, sal_Int32 nStt,
                    sal_Int32 nEnd, IMarkVector& rArr )
{
    IDocumentMarkAccess* const pMarkAccess = m_rDoc.getIDocumentMarkAccess();
    const SwNode& rNd = rAttrs.GetNode();

    const sal_Int32 nMarks = pMarkAccess->getAnnotationMarksCount();
    for ( sal_Int32 i = 0; i < nMarks; i++ )
    {
        AnnotationMark* pMark = pMarkAccess->getAnnotationMarksBegin()[i];

        // Only keep the bookmarks starting or ending in this node
        if ( pMark->GetMarkStart().GetNode() == rNd ||
             pMark->GetMarkEnd().GetNode() == rNd )
        {
            const sal_Int32 nBStart = pMark->GetMarkStart().GetContentIndex();
            const sal_Int32 nBEnd = pMark->GetMarkEnd().GetContentIndex();

            // Keep only the bookmarks starting or ending in the snippet
            bool bIsStartOk = ( pMark->GetMarkStart().GetNode() == rNd ) && ( nBStart >= nStt ) && ( nBStart <= nEnd );
            bool bIsEndOk = ( pMark->GetMarkEnd().GetNode() == rNd ) && ( nBEnd >= nStt ) && ( nBEnd <= nEnd );

            // Annotation marks always have at least one character: the anchor
            // point of the comment field. In this case Word wants only the
            // comment field, so ignore the annotation mark itself.
            bool bSingleChar = pMark->GetMarkStart().GetNode() == pMark->GetMarkEnd().GetNode() && nBStart + 1 == nBEnd;

            if (bSingleChar)
            {
                if (rAttrs.HasFlysAt(nBStart))
                {
                    // There is content (an at-char anchored frame) between the annotation mark
                    // start/end, so still emit range start/end.
                    bSingleChar = false;
                }
            }

            if ( ( bIsStartOk || bIsEndOk ) && !bSingleChar )
            {
                rArr.push_back( pMark );
            }
        }
    }
    return ( !rArr.empty() );
}

namespace {

class CompareMarksEnd
{
public:
    bool operator() ( const MarkBase * pOneB, const MarkBase * pTwoB ) const
    {
        const sal_Int32 nOEnd = pOneB->GetMarkEnd().GetContentIndex();
        const sal_Int32 nTEnd = pTwoB->GetMarkEnd().GetContentIndex();

        return nOEnd < nTEnd;
    }
};

}

bool MSWordExportBase::NearestBookmark( sal_Int32& rNearest, const sal_Int32 nCurrentPos, bool bNextPositionOnly )
{
    bool bHasBookmark = false;

    if ( !m_rSortedBookmarksStart.empty() )
    {
        MarkBase* pMarkStart = m_rSortedBookmarksStart.front();
        const sal_Int32 nNext = pMarkStart->GetMarkStart().GetContentIndex();
        if( !bNextPositionOnly || (nNext > nCurrentPos ))
        {
            rNearest = nNext;
            bHasBookmark = true;
        }
    }

    if ( !m_rSortedBookmarksEnd.empty() )
    {
        MarkBase* pMarkEnd = m_rSortedBookmarksEnd[0];
        const sal_Int32 nNext = pMarkEnd->GetMarkEnd().GetContentIndex();
        if( !bNextPositionOnly || nNext > nCurrentPos )
        {
            if ( !bHasBookmark )
                rNearest = nNext;
            else
                rNearest = std::min( rNearest, nNext );
            bHasBookmark = true;
        }
    }

    return bHasBookmark;
}

void MSWordExportBase::NearestAnnotationMark( sal_Int32& rNearest, const sal_Int32 nCurrentPos, bool bNextPositionOnly )
{
    bool bHasAnnotationMark = false;

    if ( !m_rSortedAnnotationMarksStart.empty() )
    {
        MarkBase* pMarkStart = m_rSortedAnnotationMarksStart.front();
        const sal_Int32 nNext = pMarkStart->GetMarkStart().GetContentIndex();
        if( !bNextPositionOnly || (nNext > nCurrentPos ))
        {
            rNearest = nNext;
            bHasAnnotationMark = true;
        }
    }

    if ( !m_rSortedAnnotationMarksEnd.empty() )
    {
        MarkBase* pMarkEnd = m_rSortedAnnotationMarksEnd[0];
        const sal_Int32 nNext = pMarkEnd->GetMarkEnd().GetContentIndex();
        if( !bNextPositionOnly || nNext > nCurrentPos )
        {
            if ( !bHasAnnotationMark )
                rNearest = nNext;
            else
                rNearest = std::min( rNearest, nNext );
        }
    }
}

void MSWordExportBase::GetSortedAnnotationMarks( const SwWW8AttrIter& rAttrs, sal_Int32 nCurrentPos, sal_Int32 nLen )
{
    IMarkVector aMarksStart;
    if (GetAnnotationMarks(rAttrs, nCurrentPos, nCurrentPos + nLen, aMarksStart))
    {
        IMarkVector aSortedEnd;
        IMarkVector aSortedStart;
        for ( MarkBase* pMark : aMarksStart )
        {
            // Remove the positions equal to the current pos
            const sal_Int32 nStart = pMark->GetMarkStart().GetContentIndex();
            const sal_Int32 nEnd = pMark->GetMarkEnd().GetContentIndex();

            const SwTextNode& rNode = rAttrs.GetNode();
            if ( nStart > nCurrentPos && ( pMark->GetMarkStart().GetNode() == rNode) )
                aSortedStart.push_back( pMark );

            if ( nEnd > nCurrentPos && nEnd <= ( nCurrentPos + nLen ) && (pMark->GetMarkEnd().GetNode() == rNode) )
                aSortedEnd.push_back( pMark );
        }

        // Sort the bookmarks by end position
        std::sort( aSortedEnd.begin(), aSortedEnd.end(), CompareMarksEnd() );

        m_rSortedAnnotationMarksStart.swap( aSortedStart );
        m_rSortedAnnotationMarksEnd.swap( aSortedEnd );
    }
    else
    {
        m_rSortedAnnotationMarksStart.clear( );
        m_rSortedAnnotationMarksEnd.clear( );
    }
}

void MSWordExportBase::GetSortedBookmarks( const SwTextNode& rNode, sal_Int32 nCurrentPos, sal_Int32 nLen )
{
    IMarkVector aMarksStart;
    if ( GetBookmarks( rNode, nCurrentPos, nCurrentPos + nLen, aMarksStart ) )
    {
        IMarkVector aSortedEnd;
        IMarkVector aSortedStart;
        for ( MarkBase* pMark : aMarksStart )
        {
            // Remove the positions equal to the current pos
            const sal_Int32 nStart = pMark->GetMarkStart().GetContentIndex();
            const sal_Int32 nEnd = pMark->GetMarkEnd().GetContentIndex();

            if ( nStart > nCurrentPos && (pMark->GetMarkStart().GetNode() == rNode) )
                aSortedStart.push_back( pMark );

            if ( nEnd > nCurrentPos && nEnd <= ( nCurrentPos + nLen ) && (pMark->GetMarkEnd().GetNode() == rNode) )
                aSortedEnd.push_back( pMark );
        }

        // Sort the bookmarks by end position
        std::sort( aSortedEnd.begin(), aSortedEnd.end(), CompareMarksEnd() );

        m_rSortedBookmarksStart.swap( aSortedStart );
        m_rSortedBookmarksEnd.swap( aSortedEnd );
    }
    else
    {
        m_rSortedBookmarksStart.clear( );
        m_rSortedBookmarksEnd.clear( );
    }
}

bool MSWordExportBase::NeedSectionBreak( const SwNode& rNd ) const
{
    if ( m_bStyDef || m_bOutKF || m_bInWriteEscher || m_bOutPageDescs || m_pCurrentPageDesc == nullptr )
        return false;

    const SwPageDesc * pPageDesc = rNd.FindPageDesc()->GetFollow();

    if (m_pCurrentPageDesc != pPageDesc)
    {
        if (!sw::util::IsPlausableSingleWordSection(m_pCurrentPageDesc->GetFirstMaster(), pPageDesc->GetMaster()))
        {
            return true;
        }
    }

    return false;
}

bool MSWordExportBase::NeedTextNodeSplit( const SwTextNode& rNd, SwSoftPageBreakList& pList ) const
{
    SwSoftPageBreakList tmp;
    rNd.fillSoftPageBreakList(tmp);
    // hack: move the break behind any field marks; currently we can't hide the
    // field mark instruction so the layout position is quite meaningless
    IDocumentMarkAccess const& rIDMA(*rNd.GetDoc().getIDocumentMarkAccess());
    sal_Int32 pos(-1);
    for (auto const& it : tmp)
    {
        if (pos < it) // previous one might have skipped over it
        {
            pos = it;
            while (auto const*const pMark = rIDMA.getInnerFieldmarkFor(SwPosition(rNd, pos)))
            {
                if (pMark->GetMarkEnd().GetNode() != rNd)
                {
                    pos = rNd.Len(); // skip everything
                    break;
                }
                pos = pMark->GetMarkEnd().GetContentIndex(); // no +1, it's behind the char
            }
            pList.insert(pos);
        }
    }
    pList.insert(0);
    pList.insert( rNd.GetText().getLength() );
    return pList.size() > 2 && NeedSectionBreak( rNd );
}

namespace {
OUString lcl_GetSymbolFont(SwAttrPool& rPool, const SwTextNode* pTextNode, int nStart, int nEnd)
{
    SfxItemSetFixed<RES_CHRATR_FONT, RES_CHRATR_FONT> aSet( rPool );
    if ( pTextNode && pTextNode->GetParaAttr(aSet, nStart, nEnd) )
    {
        SfxPoolItem const* pPoolItem = aSet.GetItem(RES_CHRATR_FONT);
        if (pPoolItem)
        {
            const SvxFontItem* pFontItem = static_cast<const SvxFontItem*>(pPoolItem);
            if (pFontItem->GetCharSet() == RTL_TEXTENCODING_SYMBOL)
                return pFontItem->GetFamilyName();
        }
    }

    return OUString();
}
}

void MSWordExportBase::OutputTextNode( SwTextNode& rNode )
{
    SAL_INFO( "sw.ww8", "<OutWW8_SwTextNode>" );

    if (IsDummyFloattableAnchor(rNode))
    {
        // Emit their floating tables
        AttrOutput().CheckAndWriteFloatingTables(rNode);
        return;
    }

    SwWW8AttrIter aWatermarkAttrIter( *this, rNode );

    // export inline heading
    const ww8::Frame* pInlineHeading;
    if (aWatermarkAttrIter.HasFlysAt(0, &pInlineHeading))
    {
        if (pInlineHeading->GetContent()->IsTextNode())
        {
            SwTextNode *pTextNode =
                const_cast<SwTextNode*>(pInlineHeading->GetContent()->GetTextNode());
            if (pTextNode)
            {
                m_bParaInlineHeading = true;
                OutputTextNode(*pTextNode);
                m_bParaInlineHeading = false;
            }
        }
    }

    ww8::WW8TableNodeInfo::Pointer_t pTextNodeInfo( m_pTableInfo->getTableNodeInfo( &rNode ) );

    //For i120928,identify the last node
    bool bLastCR = false;
    bool bExported = false;
    {
        SwNodeIndex aNextIdx(rNode,1);
        SwNodeIndex aLastIdx(rNode.GetNodes().GetEndOfContent());
        if (aNextIdx == aLastIdx)
            bLastCR = true;
    }

    // In order to make sure watermark is stored in 'header.xml', check nTextTyp.
    // if it is document.xml, don't write the tags (watermark should be only in the 'header')
    if (( TXT_HDFT != m_nTextTyp) && aWatermarkAttrIter.IsWatermarkFrame())
    {
        return;
    }

    bool bFlyInTable = m_pParentFrame && IsInTable();

    SwTextFormatColl& rTextColl = lcl_getFormatCollection( *this, &rNode );
    if ( !bFlyInTable )
        m_nStyleBeforeFly = GetId( rTextColl );

    // nStyleBeforeFly may change when we recurse into another node, so we
    // have to remember it in nStyle
    sal_uInt16 nStyle = m_nStyleBeforeFly;

    SwWW8AttrIter aAttrIter( *this, rNode );
    rtl_TextEncoding eChrSet = aAttrIter.GetCharSet();

    if ( m_bStartTOX )
    {
        // ignore TOX header section
        const SwSectionNode* pSectNd = rNode.FindSectionNode();
        if ( pSectNd && SectionType::ToxContent == pSectNd->GetSection().GetType() )
        {
            AttrOutput().StartTOX( pSectNd->GetSection() );
            m_aCurrentCharPropStarts.push( 0 );
        }
    }

    // Emulate: If 1-row table is marked as don't split, then set the row as don't split.
    if ( IsInTable() )
    {
        const SwTableNode* pTableNode = rNode.FindTableNode();
        if ( pTableNode )
        {
            const SwTable& rTable = pTableNode->GetTable();
            const SwTableBox* pBox = rNode.GetTableBox();

            // export formula cell as formula field instead of only its cell content in DOCX
            if ( pBox->IsFormulaOrValueBox() == RES_BOXATR_FORMULA &&
                 GetExportFormat() == MSWordExportBase::ExportFormat::DOCX )
            {
                std::unique_ptr<SwTableBoxFormula> pFormula(pBox->GetFrameFormat()->GetTableBoxFormula().Clone());
                pFormula->PtrToBoxNm( &pTableNode->GetTable() );
                OutputField( nullptr, ww::eEquals, " =" + pFormula->GetFormula(),
                    FieldFlags::Start | FieldFlags::CmdStart | FieldFlags::CmdEnd | FieldFlags::Close );
            }

            const bool bKeep = rTable.GetFrameFormat()->GetKeep().GetValue();
            const bool bDontSplit = !rTable.GetFrameFormat()->GetLayoutSplit().GetValue();
            // bKeep handles this a different way later on, so ignore now
            if ( !bKeep && bDontSplit && rTable.GetTabLines().size() == 1 )
            {
                // bDontSplit : set don't split once for the row
                // but only for non-complex tables
                const SwTableLine* pLine = pBox ? pBox->GetUpper() : nullptr;
                if ( pLine && !pLine->GetUpper() )
                {
                    // check if box is first in that line:
                    if ( 0 == pLine->GetBoxPos( pBox ) && pBox->GetSttNd() )
                    {
                        // check if paragraph is first in that line:
                        if ( SwNodeOffset(1) == ( rNode.GetIndex() - pBox->GetSttNd()->GetIndex() ) )
                            pLine->GetFrameFormat()->SetFormatAttr(SwFormatRowSplit(!bDontSplit));
                    }
                }
            }
        }
    }

    SwSoftPageBreakList softBreakList;
    // Let's decide if we need to split the paragraph because of a section break
    bool bNeedParaSplit = NeedTextNodeSplit( rNode, softBreakList )
                        && !IsInTable();
    const SwPageDesc* pNextSplitParaPageDesc = m_pCurrentPageDesc;

    auto aBreakIt = softBreakList.begin();
    // iterate through portions on different pages
    do
    {
        sal_Int32 nCurrentPos = *aBreakIt;

        if( softBreakList.size() > 1 ) // not for empty paragraph
        {
            // no need to split again if the page style won't change anymore
            if ( pNextSplitParaPageDesc == pNextSplitParaPageDesc->GetFollow() )
                aBreakIt = --softBreakList.end();
            else
                ++aBreakIt;
        }

        AttrOutput().StartParagraph(pTextNodeInfo, false);

        const SwSection* pTOXSect = nullptr;
        if( m_bInWriteTOX )
        {
            // check for end of TOX
            SwNodeIndex aIdx( rNode, 1 );
            if( !aIdx.GetNode().IsTextNode() )
            {
                const SwSectionNode* pTOXSectNd = rNode.FindSectionNode();
                if ( pTOXSectNd )
                {
                    pTOXSect = &pTOXSectNd->GetSection();

                    const SwNode* pNxt = SwNodes::GoNext(&aIdx);
                    if( pNxt && pNxt->FindSectionNode() == pTOXSectNd )
                        pTOXSect = nullptr;
                }
            }
        }

        if ( aAttrIter.RequiresImplicitBookmark() )
        {
            OUString sBkmkName =  "_toc" + OUString::number( sal_Int32(rNode.GetIndex()) );
            // Add a bookmark converted to a Word name.
            AppendBookmark( BookmarkToWord( sBkmkName ) );
        }

        // Call this before write out fields and runs
        AttrOutput().GenerateBookmarksForSequenceField(rNode, aAttrIter);

        const OUString& aStr( rNode.GetText() );

        sal_Int32 const nEnd = bNeedParaSplit ? *aBreakIt : aStr.getLength();
        bool bIsEndOfCell = false;
        bool bIncludeEndOfParaCRInRedlineProperties = false;
        sal_Int32 nOpenAttrWithRange = 0;

        ww8::WW8TableNodeInfoInner::Pointer_t pTextNodeInfoInner;
        if ( pTextNodeInfo )
        {
            pTextNodeInfoInner = pTextNodeInfo->getFirstInner();
            if (pTextNodeInfoInner && pTextNodeInfoInner->isEndOfCell())
                bIsEndOfCell = true;
        }

        do {

            const SwRedlineData* pRedlineData = aAttrIter.GetRunLevelRedline( nCurrentPos );
            bool bPostponeWritingText    = false ;
            bool bStartedPostponedRunProperties = false;
            OUString aSavedSnippet ;

            // Don't redline content-controls--Word doesn't do them.
            SwTextAttr* pAttr = rNode.GetTextAttrAt(nCurrentPos, RES_TXTATR_CONTENTCONTROL,
                                                    sw::GetTextAttrMode::Default);
            if (pAttr && pAttr->GetStart() == nCurrentPos)
            {
                pRedlineData = nullptr;
            }

            sal_Int32 nNextAttr = GetNextPos( &aAttrIter, rNode, nCurrentPos );

            // Skip un-exportable attributes.
            if (!aAttrIter.IsExportableAttr(nCurrentPos))
            {
                nCurrentPos = nNextAttr;
                UpdatePosition(&aAttrIter, nCurrentPos);
                eChrSet = aAttrIter.GetCharSet();
                continue;
            }

            // Is this the only run in this paragraph and it's empty?
            bool bSingleEmptyRun = nCurrentPos == 0 && nNextAttr == 0;
            AttrOutput().StartRun( pRedlineData, nCurrentPos, bSingleEmptyRun );

            if( nNextAttr > nEnd )
                nNextAttr = nEnd;

            if( m_nTextTyp == TXT_FTN || m_nTextTyp == TXT_EDN )
            {
                if( AttrOutput().FootnoteEndnoteRefTag() )
                {
                    AttrOutput().EndRun( &rNode, nCurrentPos, -1, nNextAttr == nEnd );
                    AttrOutput().StartRun( pRedlineData, nCurrentPos, bSingleEmptyRun );
                }
            }

            /*
               1) If there is a text node and an overlapping anchor, then write them in two different
               runs and not as part of the same run.
               2) Ensure that it is a text node and not in a fly.
               3) If the anchor is associated with a text node with empty text then we ignore.
               */
            if( rNode.IsTextNode()
                && GetExportFormat() == MSWordExportBase::ExportFormat::DOCX
                && aStr != OUStringChar(CH_TXTATR_BREAKWORD) && !aStr.isEmpty()
                    && !rNode.GetFlyFormat()
                    && aAttrIter.IsAnchorLinkedToThisNode(rNode.GetIndex()) )
            {
                bPostponeWritingText = true ;
            }

            FlyProcessingState nStateOfFlyFrame = aAttrIter.OutFlys( nCurrentPos );
            AttrOutput().SetStateOfFlyFrame( nStateOfFlyFrame );
            AttrOutput().SetAnchorIsLinkedToNode( bPostponeWritingText && (FLY_POSTPONED != nStateOfFlyFrame) );
            // Append bookmarks in this range after flys, exclusive of final
            // position of this range
            AppendBookmarks( rNode, nCurrentPos, nNextAttr - nCurrentPos, pRedlineData );
            // Sadly only possible for main or glossary document parts: ECMA-376 Part 1 sect. 11.3.2
            if ( m_nTextTyp == TXT_MAINTEXT )
                AppendAnnotationMarks(aAttrIter, nCurrentPos, nNextAttr - nCurrentPos);

            // At the moment smarttags are only written for paragraphs, at the
            // beginning of the paragraph.
            if (nCurrentPos == 0 && m_bHasBailsMetaData)
                AppendSmartTags(rNode);

            bool bTextAtr = aAttrIter.IsTextAttr( nCurrentPos );
            nOpenAttrWithRange += aAttrIter.OutAttrWithRange( rNode, nCurrentPos );

            OUString aSymbolFont;
            sal_Int32 nLen = nNextAttr - nCurrentPos;
            if ( !bTextAtr && nLen )
            {
                sal_Unicode ch = aStr[nCurrentPos];

                const sal_Int32 ofs = (ch == CH_TXT_ATR_FIELDSTART
                                    || ch == CH_TXT_ATR_FIELDSEP
                                    || ch == CH_TXT_ATR_FIELDEND
                                    || ch == CH_TXT_ATR_FORMELEMENT)
                                ? 1 : 0;
                if (ofs == 1
                    && GetExportFormat() == MSWordExportBase::ExportFormat::DOCX
                    // FLY_PROCESSED: there's at least 1 fly already written
                    && nStateOfFlyFrame == FLY_PROCESSED)
                {
                    // write flys in a separate run before field character
                    AttrOutput().EndRun(&rNode, nCurrentPos, -1, nNextAttr == nEnd);
                    AttrOutput().StartRun(pRedlineData, nCurrentPos, bSingleEmptyRun);
                }

                IDocumentMarkAccess* const pMarkAccess = m_rDoc.getIDocumentMarkAccess();
                if ( ch == CH_TXT_ATR_FIELDSTART )
                {
                    SwPosition aPosition( rNode, nCurrentPos );
                    ::sw::mark::Fieldmark const*const pFieldmark = pMarkAccess->getFieldmarkAt(aPosition);
                    assert(pFieldmark);

                    // Date field is exported as content control, not as a simple field
                    if (pFieldmark->GetFieldname() == ODF_FORMDATE)
                    {
                        if(GetExportFormat() == MSWordExportBase::ExportFormat::DOCX) // supported by DOCX only
                        {
                            OutputField( nullptr, lcl_getFieldId( pFieldmark ),
                                    lcl_getFieldCode( pFieldmark ),
                                    FieldFlags::Start | FieldFlags::CmdStart );
                            WriteFormData( *pFieldmark );
                        }
                    }
                    else
                    {

                        if (pFieldmark->GetFieldname() == ODF_FORMTEXT
                             && GetExportFormat() != MSWordExportBase::ExportFormat::DOCX )
                        {
                           AppendBookmark( pFieldmark->GetName().toString() );
                        }
                        ww::eField eFieldId = lcl_getFieldId( pFieldmark );
                        OUString sCode = lcl_getFieldCode( pFieldmark );
                        if (pFieldmark->GetFieldname() == ODF_UNHANDLED )
                        {
                            Fieldmark::parameter_map_t::const_iterator it = pFieldmark->GetParameters()->find( ODF_ID_PARAM );
                            if ( it != pFieldmark->GetParameters()->end() )
                            {
                                OUString sFieldId;
                                it->second >>= sFieldId;
                                eFieldId = static_cast<ww::eField>(sFieldId.toInt32());
                            }

                            it = pFieldmark->GetParameters()->find( ODF_CODE_PARAM );
                            if ( it != pFieldmark->GetParameters()->end() )
                            {
                                it->second >>= sCode;
                            }
                        }

                        OutputField( nullptr, eFieldId, sCode, FieldFlags::Start | FieldFlags::CmdStart );

                        if (pFieldmark->GetFieldname() == ODF_FORMTEXT)
                            WriteFormData( *pFieldmark );
                        else if (pFieldmark->GetFieldname() == ODF_HYPERLINK)
                            WriteHyperlinkData( *pFieldmark );
                    }
                }
                else if (ch == CH_TXT_ATR_FIELDSEP)
                {
                    SwPosition aPosition(rNode, nCurrentPos);
                    // the innermost field is the correct one
                    sw::mark::Fieldmark const*const pFieldmark = pMarkAccess->getInnerFieldmarkFor(aPosition);
                    assert(pFieldmark);
                    // DateFieldmark / ODF_FORMDATE is not a field...
                    if (pFieldmark->GetFieldname() != ODF_FORMDATE)
                    {
                        FieldFlags nFlags = FieldFlags::CmdEnd;
                        // send hint that fldrslt is empty, to avoid spamming RTF CharProp reset.
                        // ::End does nothing when sending rFieldCmd=OUString(), so safe to do.
                        if (pFieldmark->GetContent().isEmpty())
                            nFlags |= FieldFlags::End;
                        OutputField(nullptr, lcl_getFieldId(pFieldmark), OUString(), nFlags);

                        if (pFieldmark->GetFieldname() == ODF_UNHANDLED)
                        {
                            // Check for the presence of a linked OLE object
                            Fieldmark::parameter_map_t::const_iterator it = pFieldmark->GetParameters()->find( ODF_OLE_PARAM );
                            if ( it != pFieldmark->GetParameters()->end() )
                            {
                                OUString sOleId;
                                uno::Any aValue = it->second;
                                aValue >>= sOleId;
                                if ( !sOleId.isEmpty() )
                                    OutputLinkedOLE( sOleId );
                            }
                        }
                    }
                }
                else if ( ch == CH_TXT_ATR_FIELDEND )
                {
                    SwPosition aPosition( rNode, nCurrentPos );
                    ::sw::mark::Fieldmark const*const pFieldmark = pMarkAccess->getFieldmarkAt(aPosition);

                    assert(pFieldmark);

                    if (pFieldmark)
                    {
                        if (pFieldmark->GetFieldname() == ODF_FORMDATE)
                        {
                            if(GetExportFormat() == MSWordExportBase::ExportFormat::DOCX) // supported by DOCX only
                            {
                                OutputField( nullptr, ww::eFORMDATE, OUString(), FieldFlags::Close );
                            }
                        }
                        else
                        {
                            ww::eField eFieldId = lcl_getFieldId( pFieldmark );
                            if (pFieldmark->GetFieldname() == ODF_UNHANDLED)
                            {
                                Fieldmark::parameter_map_t::const_iterator it = pFieldmark->GetParameters()->find( ODF_ID_PARAM );
                                if ( it != pFieldmark->GetParameters()->end() )
                                {
                                    OUString sFieldId;
                                    it->second >>= sFieldId;
                                    eFieldId = static_cast<ww::eField>(sFieldId.toInt32());
                                }
                            }

                            OutputField( nullptr, eFieldId, OUString(), FieldFlags::Close );

                            if (pFieldmark->GetFieldname() == ODF_FORMTEXT
                                    && GetExportFormat() != MSWordExportBase::ExportFormat::DOCX )
                            {
                                AppendBookmark( pFieldmark->GetName().toString() );
                            }
                        }
                    }
                }
                else if ( ch == CH_TXT_ATR_FORMELEMENT )
                {
                    SwPosition aPosition( rNode, nCurrentPos );
                    ::sw::mark::Fieldmark const*const pFieldmark = pMarkAccess->getFieldmarkAt(aPosition);
                    assert(pFieldmark);

                    bool const isDropdownOrCheckbox(pFieldmark->GetFieldname() == ODF_FORMDROPDOWN ||
                                                    pFieldmark->GetFieldname() == ODF_FORMCHECKBOX);
                    if ( isDropdownOrCheckbox )
                        AppendBookmark( pFieldmark->GetName().toString() );
                    OutputField( nullptr, lcl_getFieldId( pFieldmark ),
                            lcl_getFieldCode( pFieldmark ),
                            FieldFlags::Start | FieldFlags::CmdStart );
                    if ( isDropdownOrCheckbox )
                        WriteFormData( *pFieldmark );
                    // tdf#129514 need CmdEnd for docx
                    OutputField(nullptr, lcl_getFieldId(pFieldmark), OUString(),
                            FieldFlags::CmdEnd | FieldFlags::Close);
                    if ( isDropdownOrCheckbox )
                        AppendBookmark( pFieldmark->GetName().toString() );
                }
                nLen -= ofs;

                // if paragraph needs to be split, write only until split position
                assert(!bNeedParaSplit || nCurrentPos <= *aBreakIt);
                if( bNeedParaSplit && nCurrentPos + ofs + nLen > *aBreakIt)
                    nLen = *aBreakIt - nCurrentPos - ofs;
                assert(0 <= nLen);

                OUString aSnippet( aAttrIter.GetSnippet( aStr, nCurrentPos + ofs, nLen ) );
                const SwTextNode* pTextNode( rNode.GetTextNode() );
                if (m_bAddFootnoteTab && (m_nTextTyp == TXT_EDN || m_nTextTyp == TXT_FTN)
                    && nCurrentPos == 0 && nLen > 0 && aSnippet[0] != 0x09)
                {
                    // Allow MSO to emulate LO footnote text starting at left margin - only meaningful with hanging indent
                    sal_Int32 nFirstLineIndent=0;
                    SfxItemSetFixed<RES_MARGIN_FIRSTLINE, RES_MARGIN_FIRSTLINE> aSet( m_rDoc.GetAttrPool() );

                    if ( pTextNode && pTextNode->GetAttr(aSet) )
                    {
                        const SvxFirstLineIndentItem *const pFirstLine(aSet.GetItem<SvxFirstLineIndentItem>(RES_MARGIN_FIRSTLINE));
                        if (pFirstLine)
                            nFirstLineIndent = pFirstLine->ResolveTextFirstLineOffset({});
                    }

                    // Insert tab for aesthetic purposes #i24762#
                    if (nFirstLineIndent < 0)
                        aSnippet = "\x09" + aSnippet;
                    m_bAddFootnoteTab = false;
                }

                aSymbolFont = lcl_GetSymbolFont(m_rDoc.GetAttrPool(), pTextNode, nCurrentPos + ofs, nCurrentPos + ofs + nLen);

                if ( bPostponeWritingText && ( FLY_POSTPONED != nStateOfFlyFrame ) )
                {
                    aSavedSnippet = aSnippet ;
                }
                else
                {
                    bPostponeWritingText = false ;
                    AttrOutput().RunText( aSnippet, eChrSet, aSymbolFont );
                }

                if (ofs == 1 && nNextAttr == nEnd)
                {
                    // tdf#152200: There could be flys anchored after the last position; make sure
                    // to provide a separate run after field character to write them
                    AttrOutput().EndRun(&rNode, nCurrentPos, -1, nNextAttr == nEnd);
                    AttrOutput().StartRun(pRedlineData, nCurrentPos, bSingleEmptyRun);
                }
            }

            if ( aAttrIter.IsDropCap( nNextAttr ) )
                AttrOutput().FormatDrop( rNode, aAttrIter.GetSwFormatDrop(), nStyle, pTextNodeInfo, pTextNodeInfoInner );

            // Only output character attributes if this is not a postponed text run.
            if (0 != nEnd && !(bPostponeWritingText
                    && (FLY_PROCESSED == nStateOfFlyFrame || FLY_NONE == nStateOfFlyFrame)))
            {
                // Output the character attributes
                // #i51277# do this before writing flys at end of paragraph
                bStartedPostponedRunProperties = true;
                AttrOutput().StartRunProperties();
                aAttrIter.OutAttr(nCurrentPos, false);
                AttrOutput().EndRunProperties( pRedlineData );
            }

            // At the end of line, output the attributes until the CR.
            // Exception: footnotes at the end of line
            if ( nNextAttr == nEnd )
            {
                OSL_ENSURE( nOpenAttrWithRange >= 0, "odd to see this happening, expected >= 0" );
                if ( !bTextAtr && nOpenAttrWithRange <= 0 )
                {
                    if ( aAttrIter.IncludeEndOfParaCRInRedlineProperties( nEnd ) )
                        bIncludeEndOfParaCRInRedlineProperties = true;
                    else
                    {
                        // insert final graphic anchors if any before CR
                        nStateOfFlyFrame = aAttrIter.OutFlys( nEnd );
                        // insert final bookmarks if any before CR and after flys
                        AppendBookmarks( rNode, nEnd, 1 );
                        AppendAnnotationMarks(aAttrIter, nEnd, 1);
                        if ( pTOXSect )
                        {
                            m_aCurrentCharPropStarts.pop();
                            AttrOutput().EndTOX( *pTOXSect ,false);
                        }
                        //For i120928,the position of the bullet's graphic is at end of doc
                        if (bLastCR && (!bExported))
                        {
                            ExportGrfBullet(rNode);
                            bExported = true;
                        }

                        WriteCR( pTextNodeInfoInner );

                        if (0 != nEnd && bIsEndOfCell)
                            AttrOutput().OutputFKP(/*bforce=*/true);
                    }
                }
            }

            if (0 == nEnd)
            {
                // Output the character attributes
                // do it after WriteCR for an empty paragraph (otherwise
                // WW8_WrFkp::Append throws SPRMs away...)
                AttrOutput().StartRunProperties();
                aAttrIter.OutAttr( nCurrentPos, false );
                AttrOutput().EndRunProperties( pRedlineData );
            }

            // Exception: footnotes at the end of line
            if ( nNextAttr == nEnd )
            {
                OSL_ENSURE(nOpenAttrWithRange >= 0,
                        "odd to see this happening, expected >= 0");
                bool bAttrWithRange = (nOpenAttrWithRange > 0);
                if ( nCurrentPos != nEnd )
                {
                    nOpenAttrWithRange += aAttrIter.OutAttrWithRange( rNode, nEnd );
                    OSL_ENSURE(nOpenAttrWithRange == 0,
                            "odd to see this happening, expected 0");
                }

                AttrOutput().OutputFKP(/*bForce=*/false);

                if (bTextAtr || bAttrWithRange || bIncludeEndOfParaCRInRedlineProperties)
                {
                    AttrOutput().WritePostitFieldReference();

                    // insert final graphic anchors if any before CR
                    nStateOfFlyFrame = aAttrIter.OutFlys( nEnd );
                    // insert final bookmarks if any before CR and after flys
                    AppendBookmarks( rNode, nEnd, 1 );
                    AppendAnnotationMarks(aAttrIter, nEnd, 1);
                    WriteCR( pTextNodeInfoInner );
                    // #i120928 - position of the bullet's graphic is at end of doc
                    if (bLastCR && (!bExported))
                    {
                        ExportGrfBullet(rNode);
                        bExported = true;
                    }

                    if ( pTOXSect )
                    {
                        m_aCurrentCharPropStarts.pop();
                        AttrOutput().EndTOX( *pTOXSect );
                    }

                    if (bIncludeEndOfParaCRInRedlineProperties)
                    {
                        AttrOutput().Redline( aAttrIter.GetRunLevelRedline( nEnd ) );
                        //If there was no redline property emitted, force adding
                        //another entry for the CR so that in the case that this
                        //has no redline, but the next para does, then this one is
                        //not merged with the next
                        AttrOutput().OutputFKP(true);
                    }
                }
            }

            AttrOutput().WritePostitFieldReference();

            aSymbolFont = lcl_GetSymbolFont(m_rDoc.GetAttrPool(), &rNode, nCurrentPos, nCurrentPos + nLen);

            if (bPostponeWritingText)
            {
                if (FLY_PROCESSED == nStateOfFlyFrame || FLY_NONE == nStateOfFlyFrame)
                {
                    AttrOutput().EndRun(&rNode, nCurrentPos, -1, /*bLastRun=*/false);
                    if (!aSavedSnippet.isEmpty())
                        bStartedPostponedRunProperties = false;

                    AttrOutput().StartRun( pRedlineData, nCurrentPos, bSingleEmptyRun );
                    AttrOutput().SetAnchorIsLinkedToNode( false );
                    AttrOutput().ResetFlyProcessingFlag();
                }
                if (0 != nEnd && !bStartedPostponedRunProperties)
                {
                    AttrOutput().StartRunProperties();
                    aAttrIter.OutAttr( nCurrentPos, false );
                    AttrOutput().EndRunProperties( pRedlineData );

                    // OutAttr may have introduced new comments, so write them out now
                    AttrOutput().WritePostitFieldReference();
                }
                AttrOutput().RunText( aSavedSnippet, eChrSet, aSymbolFont );
                AttrOutput().EndRun(&rNode, nCurrentPos, nLen, nNextAttr == nEnd);
            }
            else
                AttrOutput().EndRun(&rNode, nCurrentPos, nLen, nNextAttr == nEnd);

            nCurrentPos = nNextAttr;
            UpdatePosition( &aAttrIter, nCurrentPos );
            eChrSet = aAttrIter.GetCharSet();
        }
        while ( nCurrentPos < nEnd );

        // if paragraph is split, put the section break between the parts
        if( bNeedParaSplit && *aBreakIt != rNode.GetText().getLength() )
        {
            pNextSplitParaPageDesc = pNextSplitParaPageDesc->GetFollow();
            assert(pNextSplitParaPageDesc);
            PrepareNewPageDesc( rNode.GetpSwAttrSet(), rNode, nullptr , pNextSplitParaPageDesc);
        }
        else
        {
            // else check if section break needed after the paragraph
            bool bCheckSectionBreak = true;
            // only try to sectionBreak after a split para if the next node specifies a break
            if ( bNeedParaSplit )
            {
                m_pCurrentPageDesc = pNextSplitParaPageDesc;
                SwNodeIndex aNextIndex( rNode, 1 );
                const SwTextNode* pNextNode = aNextIndex.GetNode().GetTextNode();
                bCheckSectionBreak = pNextNode && !NoPageBreakSection( pNextNode->GetpSwAttrSet() );

                if ( !bCheckSectionBreak )
                {
                    const SvxFormatBreakItem& rBreak = rNode.GetSwAttrSet().Get(RES_BREAK);
                    if ( rBreak.GetBreak() == SvxBreak::PageAfter )
                    {
                        if ( pNextNode && pNextNode->FindPageDesc() != pNextSplitParaPageDesc )
                            bCheckSectionBreak = true;
                        else
                            AttrOutput().SectionBreak(msword::PageBreak, /*bBreakAfter=*/true);
                    }
                }
            }

            if ( bCheckSectionBreak )
                AttrOutput().SectionBreaks(rNode);
        }

        AttrOutput().StartParagraphProperties();

        AttrOutput().ParagraphStyle( nStyle );

        if ( m_pParentFrame && IsInTable() )    // Fly-Attrs
            OutputFormat( m_pParentFrame->GetFrameFormat(), false, false, true );

        if ( pTextNodeInfo )
        {
#ifdef DBG_UTIL
            SAL_INFO( "sw.ww8", pTextNodeInfo->toString());
#endif

            AttrOutput().TableInfoCell( pTextNodeInfoInner );
            if (pTextNodeInfoInner && pTextNodeInfoInner->isFirstInTable())
            {
                const SwTable * pTable = pTextNodeInfoInner->getTable();

                const SwTableFormat* pTabFormat = pTable->GetFrameFormat();
                if (pTabFormat != nullptr)
                {
                    if (pTabFormat->GetBreak().GetBreak() == SvxBreak::PageBefore)
                        AttrOutput().PageBreakBefore(true);
                }
            }
        }

        if ( !bFlyInTable )
        {
            std::optional<SfxItemSet> oTmpSet;
            const sal_uInt8 nPrvNxtNd = rNode.HasPrevNextLayNode();

            if( (ND_HAS_PREV_LAYNODE|ND_HAS_NEXT_LAYNODE ) != nPrvNxtNd )
            {
                const SvxULSpaceItem* pSpaceItem = rNode.GetSwAttrSet().GetItemIfSet(
                        RES_UL_SPACE );
                if( pSpaceItem &&
                    ( ( !( ND_HAS_PREV_LAYNODE & nPrvNxtNd ) && pSpaceItem->GetUpper()) ||
                      ( !( ND_HAS_NEXT_LAYNODE & nPrvNxtNd ) && pSpaceItem->GetLower()) ))
                {
                    oTmpSet.emplace( rNode.GetSwAttrSet() );
                    SvxULSpaceItem aUL( *pSpaceItem );
                    // #i25901#- consider compatibility option
                    if (!m_rDoc.getIDocumentSettingAccess().get(DocumentSettingId::PARA_SPACE_MAX_AT_PAGES))
                    {
                        if( !(ND_HAS_PREV_LAYNODE & nPrvNxtNd ))
                            aUL.SetUpper( 0 );
                    }
                    // #i25901# - consider compatibility option
                    if (!m_rDoc.getIDocumentSettingAccess().get(DocumentSettingId::ADD_PARA_SPACING_TO_TABLE_CELLS))
                    {
                        if( !(ND_HAS_NEXT_LAYNODE & nPrvNxtNd ))
                            aUL.SetLower( 0 );
                    }
                    oTmpSet->Put( aUL );
                }
            }

            const bool bParaRTL = aAttrIter.IsParaRTL();

            int nNumberLevel = -1;
            if (rNode.IsNumbered())
                nNumberLevel = rNode.GetActualListLevel();
            if (nNumberLevel >= 0 && nNumberLevel < MAXLEVEL)
            {
                const SwNumRule* pRule = rNode.GetNumRule();
                sal_uInt8 nLvl = static_cast< sal_uInt8 >(nNumberLevel);
                const SwNumFormat* pFormat = pRule->GetNumFormat( nLvl );
                if( !pFormat )
                    pFormat = &pRule->Get( nLvl );

                if( !oTmpSet )
                    oTmpSet.emplace( rNode.GetSwAttrSet() );

                SvxFirstLineIndentItem firstLine(oTmpSet->Get(RES_MARGIN_FIRSTLINE));
                SvxTextLeftMarginItem leftMargin(oTmpSet->Get(RES_MARGIN_TEXTLEFT));
                // #i86652#
                if ( pFormat->GetPositionAndSpaceMode() ==
                                        SvxNumberFormat::LABEL_WIDTH_AND_POSITION )
                {
                    leftMargin.SetTextLeft(SvxIndentValue::twips(leftMargin.ResolveTextLeft({})
                                                                 + pFormat->GetAbsLSpace()));
                }

                if( rNode.IsNumbered() && rNode.IsCountedInList() )
                {
                    // #i86652#
                    if ( pFormat->GetPositionAndSpaceMode() ==
                                            SvxNumberFormat::LABEL_WIDTH_AND_POSITION )
                    {
                        if (bParaRTL)
                        {
                            firstLine.SetTextFirstLineOffset(SvxIndentValue::twips(
                                firstLine.ResolveTextFirstLineOffset({}) + pFormat->GetAbsLSpace()
                                - pFormat->GetFirstLineOffset()));
                        }
                        else
                        {
                            firstLine.SetTextFirstLineOffset(
                                SvxIndentValue::twips(firstLine.ResolveTextFirstLineOffset({})
                                                      + GetWordFirstLineOffset(*pFormat)));
                        }
                    }

                    // correct fix for issue i94187
                    if (SfxItemState::SET !=
                        oTmpSet->GetItemState(RES_PARATR_NUMRULE, false) )
                    {
                        // List style set via paragraph style - then put it into the itemset.
                        // This is needed to get list level and list id exported for
                        // the paragraph.
                        oTmpSet->Put( SwNumRuleItem( pRule->GetName() ));

                        // Put indent values into the itemset in case that the list
                        // style is applied via paragraph style and the list level
                        // indent values are not applicable.
                        if ( pFormat->GetPositionAndSpaceMode() ==
                                    SvxNumberFormat::LABEL_ALIGNMENT)
                        {
                            ::sw::ListLevelIndents const indents(rNode.AreListLevelIndentsApplicable());
                            if (indents & ::sw::ListLevelIndents::FirstLine)
                            {
                                oTmpSet->Put(firstLine);
                            }
                            if (indents & ::sw::ListLevelIndents::LeftMargin)
                            {
                                oTmpSet->Put(leftMargin);
                            }
                        }
                    }
                }
                else
                    oTmpSet->ClearItem(RES_PARATR_NUMRULE);

                // #i86652#
                if ( pFormat->GetPositionAndSpaceMode() ==
                                        SvxNumberFormat::LABEL_WIDTH_AND_POSITION )
                {
                    oTmpSet->Put(firstLine);
                    oTmpSet->Put(leftMargin);

                    //#i21847#
                    SvxTabStopItem aItem(oTmpSet->Get(RES_PARATR_TABSTOP));
                    SvxTabStop aTabStop(pFormat->GetAbsLSpace());
                    aItem.Insert(aTabStop);
                    oTmpSet->Put(aItem);

                    MSWordExportBase::CorrectTabStopInSet(*oTmpSet, pFormat->GetAbsLSpace());
                }
            }

            /*
            If a given para is using the SvxFrameDirection::Environment direction we
            cannot export that, if it's ltr then that's ok as that is word's
            default. Otherwise we must add a RTL attribute to our export list
            Only necessary if the ParaStyle doesn't define the direction.
            */
            const SvxFrameDirectionItem* pItem =
                rNode.GetSwAttrSet().GetItem(RES_FRAMEDIR);
            if (
                (!pItem || pItem->GetValue() == SvxFrameDirection::Environment) &&
                rTextColl.GetFrameDir().GetValue() == SvxFrameDirection::Environment
               )
            {
                if ( !oTmpSet )
                    oTmpSet.emplace(rNode.GetSwAttrSet());

                if ( bParaRTL )
                    oTmpSet->Put(SvxFrameDirectionItem(SvxFrameDirection::Horizontal_RL_TB, RES_FRAMEDIR));
                else
                    oTmpSet->Put(SvxFrameDirectionItem(SvxFrameDirection::Horizontal_LR_TB, RES_FRAMEDIR));

                const SvxAdjustItem* pAdjust = rNode.GetSwAttrSet().GetItem(RES_PARATR_ADJUST);
                if ( pAdjust && (pAdjust->GetAdjust() == SvxAdjust::Left || pAdjust->GetAdjust() == SvxAdjust::Right ) )
                    oTmpSet->Put( *pAdjust );
            }
            // move code for handling of numbered,
            // but not counted paragraphs to this place. Otherwise, the paragraph
            // isn't exported as numbered, but not counted, if no other attribute
            // is found in <pTmpSet>
            // #i44815# adjust numbering/indents for numbered paragraphs
            //          without number
            // #i47013# need to check rNode.GetNumRule()!=NULL as well.
            if ( ! rNode.IsCountedInList() && rNode.GetNumRule()!=nullptr )
            {
                // WW8 does not know numbered paragraphs without number
                // In WW8AttributeOutput::ParaNumRule(), we will export
                // the RES_PARATR_NUMRULE as list-id 0, which in WW8 means
                // no numbering. Here, we will adjust the indents to match
                // visually.

                if ( !oTmpSet )
                    oTmpSet.emplace(rNode.GetSwAttrSet());

                // create new LRSpace item, based on the current (if present)
                const SvxFirstLineIndentItem *const pFirstLineIndent(oTmpSet->GetItemIfSet(RES_MARGIN_FIRSTLINE));
                const SvxTextLeftMarginItem *const pTextLeftMargin(oTmpSet->GetItemIfSet(RES_MARGIN_TEXTLEFT));
                SvxFirstLineIndentItem firstLine(pFirstLineIndent
                        ? *pFirstLineIndent
                        : SvxFirstLineIndentItem(RES_MARGIN_FIRSTLINE));
                SvxTextLeftMarginItem leftMargin(
                    pTextLeftMargin
                        ? *pTextLeftMargin
                        : SvxTextLeftMarginItem(SvxIndentValue::zero(), RES_MARGIN_TEXTLEFT));

                // new left margin = old left + label space
                const SwNumRule* pRule = rNode.GetNumRule();
                int nLevel = rNode.GetActualListLevel();

                if (nLevel < 0)
                    nLevel = 0;

                if (nLevel >= MAXLEVEL)
                    nLevel = MAXLEVEL - 1;

                const SwNumFormat& rNumFormat = pRule->Get( static_cast< sal_uInt16 >(nLevel) );

                // #i86652#
                if ( rNumFormat.GetPositionAndSpaceMode() ==
                                        SvxNumberFormat::LABEL_WIDTH_AND_POSITION )
                {
                    leftMargin.SetTextLeft(
                        SvxIndentValue::twips(leftMargin.ResolveLeft(firstLine, /*metrics*/ {})
                                              + rNumFormat.GetAbsLSpace()));
                }
                else
                {
                    leftMargin.SetTextLeft(
                        SvxIndentValue::twips(leftMargin.ResolveLeft(firstLine, /*metrics*/ {})
                                              + rNumFormat.GetIndentAt()));
                }

                // new first line indent = 0
                // (first line indent is ignored)
                if (!bParaRTL)
                {
                    firstLine.SetTextFirstLineOffset(SvxIndentValue::zero());
                }

                // put back the new item
                oTmpSet->Put(firstLine);
                oTmpSet->Put(leftMargin);

                // assure that numbering rule is in <oTmpSet>
                if (SfxItemState::SET != oTmpSet->GetItemState(RES_PARATR_NUMRULE, false) )
                {
                    oTmpSet->Put( SwNumRuleItem( pRule->GetName() ));
                }
            }

            // #i75457#
            // Export page break after attribute from paragraph style.
            // If page break attribute at the text node exist, an existing page
            // break after at the paragraph style hasn't got to be considered.
            if ( !rNode.GetpSwAttrSet() ||
                 SfxItemState::SET != rNode.GetpSwAttrSet()->GetItemState(RES_BREAK, false) )
            {
                const SvxFormatBreakItem& rBreakAtParaStyle
                    = rNode.GetSwAttrSet().Get(RES_BREAK);
                if (rBreakAtParaStyle.GetBreak() == SvxBreak::PageAfter)
                {
                    if ( !oTmpSet )
                        oTmpSet.emplace(rNode.GetSwAttrSet());
                    oTmpSet->Put(rBreakAtParaStyle);
                }
                else if( oTmpSet )
                {   // Even a pagedesc item is set, the break item can be set 'NONE',
                    // this has to be overruled.
                    const SwFormatPageDesc& rPageDescAtParaStyle =
                        rNode.GetAttr( RES_PAGEDESC );
                    if( rPageDescAtParaStyle.KnowsPageDesc() )
                        oTmpSet->ClearItem( RES_BREAK );
                }
            }

            // #i76520# Emulate non-splitting tables
            if ( IsInTable() )
            {
                const SwTableNode* pTableNode = rNode.FindTableNode();

                if ( pTableNode )
                {
                    const SwTable& rTable = pTableNode->GetTable();
                    const SvxFormatKeepItem& rKeep = rTable.GetFrameFormat()->GetKeep();
                    const bool bKeep = rKeep.GetValue();
                    const bool bDontSplit = !(bKeep ||
                                              rTable.GetFrameFormat()->GetLayoutSplit().GetValue());

                    if ( bKeep || bDontSplit )
                    {
                        // bKeep: set keep at first paragraphs in all lines
                        // bDontSplit : set keep at first paragraphs in all lines except from last line
                        // but only for non-complex tables
                        const SwTableBox* pBox = rNode.GetTableBox();
                        const SwTableLine* pLine = pBox ? pBox->GetUpper() : nullptr;

                        if ( pLine && !pLine->GetUpper() )
                        {
                            // check if box is first in that line:
                            if ( 0 == pLine->GetBoxPos( pBox ) && pBox->GetSttNd() )
                            {
                                // check if paragraph is first in that line:
                                if ( SwNodeOffset(1) == ( rNode.GetIndex() - pBox->GetSttNd()->GetIndex() ) )
                                {
                                    bool bSetAtPara = false;
                                    if ( bKeep )
                                        bSetAtPara = true;
                                    else if ( bDontSplit )
                                    {
                                        // check if pLine isn't last line in table
                                        if ( rTable.GetTabLines().size() - rTable.GetTabLines().GetPos( pLine ) != 1 )
                                            bSetAtPara = true;
                                    }

                                    if ( bSetAtPara )
                                    {
                                        if ( !oTmpSet )
                                            oTmpSet.emplace(rNode.GetSwAttrSet());

                                        const SvxFormatKeepItem aKeepItem( true, RES_KEEP );
                                        oTmpSet->Put( aKeepItem );
                                    }
                                }
                            }
                        }
                    }
                }
            }

            const SfxItemSet* pNewSet = oTmpSet ? &*oTmpSet : rNode.GetpSwAttrSet();
            if( pNewSet )
            {                                               // Para-Attrs
                m_pStyAttr = &rNode.GetAnyFormatColl().GetAttrSet();

                const sw::BroadcastingModify* pOldMod = m_pOutFormatNode;
                m_pOutFormatNode = &rNode;

                // Pap-Attrs, so script is not necessary
                OutputItemSet( *pNewSet, true, false, i18n::ScriptType::LATIN, false);

                m_pStyAttr = nullptr;
                m_pOutFormatNode = pOldMod;
            }
        }

        // The formatting of the paragraph marker has two sources:
        // 0) If there is a RES_PARATR_LIST_AUTOFMT, then use that.
        // 1) If there are hints at the end of the paragraph, then use that.
        // 2) Else use the RES_CHRATR_BEGIN..RES_TXTATR_END range of the paragraph
        // properties.
        //
        // Exception: if there is a character style hint at the end of the
        // paragraph only, then still go with 2), as RES_TXTATR_CHARFMT is always
        // set as a hint.
        SfxItemSetFixed<RES_CHRATR_BEGIN, RES_TXTATR_END> aParagraphMarkerProperties(m_rDoc.GetAttrPool());
        bool bCharFormatOnly = true;

        SwFormatAutoFormat const& rListAutoFormat(rNode.GetAttr(RES_PARATR_LIST_AUTOFMT));
        if (std::shared_ptr<SfxItemSet> const& pSet = rListAutoFormat.GetStyleHandle())
        {
            aParagraphMarkerProperties.Put(*pSet);
            bCharFormatOnly = false;
        }
        else if (const SwpHints* pTextAttrs = rNode.GetpSwpHints())
        {
            for( size_t i = 0; i < pTextAttrs->Count(); ++i )
            {
                const SwTextAttr* pHt = pTextAttrs->Get(i);
                const sal_Int32 startPos = pHt->GetStart();    // first Attr characters
                const sal_Int32* endPos = pHt->End();    // end Attr characters
                // Check if these attributes are for the last character in the paragraph
                // - which means the paragraph marker. If a paragraph has 7 characters,
                // then properties on character 8 are for the paragraph marker
                if( endPos && (startPos == *endPos ) && (*endPos == rNode.GetText().getLength()) )
                {
                    SAL_INFO( "sw.ww8", startPos << "startPos == endPos" << *endPos);
                    sal_uInt16 nWhich = pHt->GetAttr().Which();
                    SAL_INFO( "sw.ww8", "nWhich" << nWhich);
                    if ((nWhich == RES_TXTATR_AUTOFMT && bCharFormatOnly)
                        || nWhich == RES_TXTATR_CHARFMT)
                    {
                        aParagraphMarkerProperties.Put(pHt->GetAttr());
                    }
                    if (nWhich != RES_TXTATR_CHARFMT)
                        bCharFormatOnly = false;
                }
            }
        }
        if (rNode.GetpSwAttrSet() && bCharFormatOnly)
        {
            aParagraphMarkerProperties.Put(*rNode.GetpSwAttrSet());
        }
        const SwRedlineData* pRedlineParagraphMarkerDelete = AttrOutput().GetParagraphMarkerRedline( rNode, RedlineType::Delete );
        const SwRedlineData* pRedlineParagraphMarkerInsert = AttrOutput().GetParagraphMarkerRedline( rNode, RedlineType::Insert );
        const SwRedlineData* pParagraphRedlineData = aAttrIter.GetParagraphLevelRedline( );
        AttrOutput().EndParagraphProperties(aParagraphMarkerProperties, pParagraphRedlineData, pRedlineParagraphMarkerDelete, pRedlineParagraphMarkerInsert);

        AttrOutput().EndParagraph( pTextNodeInfoInner );
    }while(*aBreakIt != rNode.GetText().getLength() && bNeedParaSplit );

    SAL_INFO( "sw.ww8", "</OutWW8_SwTextNode>" );
}

// Tables

void WW8AttributeOutput::EmptyParagraph()
{
    m_rWW8Export.WriteStringAsPara( OUString() );
}

bool MSWordExportBase::NoPageBreakSection( const SfxItemSet* pSet )
{
    bool bRet = false;
    if( pSet)
    {
        bool bNoPageBreak = false;
        const SwFormatPageDesc* pDescItem = pSet->GetItemIfSet(RES_PAGEDESC);
        if ( !pDescItem || nullptr == pDescItem->GetPageDesc() )
        {
            bNoPageBreak = true;
        }

        if (bNoPageBreak)
        {
            if (const SvxFormatBreakItem* pBreakItem = pSet->GetItemIfSet(RES_BREAK))
            {
                SvxBreak eBreak = pBreakItem->GetBreak();
                switch (eBreak)
                {
                    case SvxBreak::PageBefore:
                    case SvxBreak::PageAfter:
                        bNoPageBreak = false;
                        break;
                    default:
                        break;
                }
            }
        }
        bRet = bNoPageBreak;
    }
    return bRet;
}

void MSWordExportBase::OutputSectionNode( const SwSectionNode& rSectionNode )
{
    const SwSection& rSection = rSectionNode.GetSection();

    SwNodeIndex aIdx( rSectionNode, 1 );
    const SwNode& rNd = aIdx.GetNode();
    if ( !rNd.IsSectionNode() && !IsInTable() ) //No sections in table
    {
        // if the first Node inside the section has an own
        // PageDesc or PageBreak attribute, then don't write
        // here the section break
        sal_uLong nRstLnNum = 0;
        const SfxItemSet* pSet;
        if ( rNd.IsContentNode() )
        {
            pSet = &rNd.GetContentNode()->GetSwAttrSet();
            nRstLnNum = pSet->Get( RES_LINENUMBER ).GetStartValue();
        }
        else
            pSet = nullptr;

        if ( pSet && NoPageBreakSection( pSet ) )
            pSet = nullptr;
        else
            AttrOutput().SectionBreaks( rSectionNode );

        const bool bInTOX = rSection.GetType() == SectionType::ToxContent || rSection.GetType() == SectionType::ToxHeader;
        if ( !pSet && !bInTOX )
        {
            // new Section with no own PageDesc/-Break
            //  -> write follow section break;
            const SwSectionFormat* pFormat = rSection.GetFormat();
            ReplaceCr( msword::PageBreak ); // Indicator for Page/Section-Break

            // Get the page in use at the top of this section
            const SwPageDesc *pCurrent = SwPageDesc::GetPageDescOfNode(rNd);
            if (!pCurrent)
                pCurrent = m_pCurrentPageDesc;

            AppendSection( pCurrent, pFormat, nRstLnNum );
        }
    }
    if ( SectionType::ToxContent == rSection.GetType() )
    {
        m_bStartTOX = true;
        UpdateTocSectionNodeProperties(rSectionNode);
    }
}

// don't need to broadcast modification
static void SetAttrNoBroadcast(SwContentNode& rNode, const SfxPoolItem& rItem)
{
    const bool bModifyNotifyDisabled = rNode.IsModifyLocked();
    if (!bModifyNotifyDisabled)
        rNode.LockModify();
    rNode.SetAttr(rItem);
    if (!bModifyNotifyDisabled)
        rNode.UnlockModify();
}

// tdf#121561: During export of the ODT file with TOC inside into DOCX format,
// the TOC title is being exported as regular paragraph. We should surround it
// with <w:sdt><w:sdtPr><w:sdtContent> to make it (TOC title) recognizable
// by MS Word as part of the TOC.
void MSWordExportBase::UpdateTocSectionNodeProperties(const SwSectionNode& rSectionNode)
{
    // check section type
    {
        const SwSection& rSection = rSectionNode.GetSection();
        if (SectionType::ToxContent != rSection.GetType())
            return;

        const SwTOXBase* pTOX = rSection.GetTOXBase();
        if (pTOX)
        {
            TOXTypes type = pTOX->GetType();
            if (type != TOXTypes::TOX_CONTENT)
                return;
        }
    }

    // get section node, skip toc-header node
    const SwSectionNode* pSectNd = &rSectionNode;
    {
        SwNodeIndex aIdxNext( *pSectNd, 1 );
        const SwNode& rNdNext = aIdxNext.GetNode();

        if (rNdNext.IsSectionNode())
        {
            const SwSectionNode* pSectNdNext = static_cast<const SwSectionNode*>(&rNdNext);
            if (SectionType::ToxHeader == pSectNdNext->GetSection().GetType() &&
                pSectNdNext->StartOfSectionNode()->IsSectionNode())
            {
                pSectNd = pSectNdNext;
            }
        }
    }

    // get node of the first paragraph inside TOC
    SwNodeIndex aIdxNext( *pSectNd, 1 );
    const SwNode& rNdTocPara = aIdxNext.GetNode();
    const SwContentNode* pNode = rNdTocPara.GetContentNode();
    if (!pNode)
        return;

    // put required flags into grab bag of the first node in TOC
    {
        uno::Sequence<beans::PropertyValue> aDocPropertyValues(comphelper::InitPropertySequence(
        {
            {"ooxml:CT_SdtDocPart_docPartGallery", uno::Any(u"Table of Contents"_ustr)},
            {"ooxml:CT_SdtDocPart_docPartUnique",  uno::Any(u"true"_ustr)},
        }));

        uno::Sequence<beans::PropertyValue> aSdtPrPropertyValues(comphelper::InitPropertySequence(
        {
            {"ooxml:CT_SdtPr_docPartObj", uno::Any(aDocPropertyValues)},
        }));

        SfxGrabBagItem aGrabBag(RES_PARATR_GRABBAG,
            std::map<OUString, css::uno::Any>{{ u"SdtPr"_ustr, uno::Any(aSdtPrPropertyValues) }});

        // set new attr to node
        SetAttrNoBroadcast(*const_cast<SwContentNode*>(pNode), aGrabBag);
    }

    // set flag for the next node after TOC
    // in order to indicate that std area has been finished
    // see, DomainMapper::lcl_startParagraphGroup() for the same functionality during load
    {
        SwNodeIndex aEndTocNext( *rSectionNode.EndOfSectionNode(), 1 );
        const SwNode& rEndTocNextNode = aEndTocNext.GetNode();
        const SwContentNode* pNodeAfterToc = rEndTocNextNode.GetContentNode();
        if (pNodeAfterToc)
        {
            SfxGrabBagItem aGrabBag(RES_PARATR_GRABBAG,
                std::map<OUString, css::uno::Any>{{u"ParaSdtEndBefore"_ustr, uno::Any(true)}});

            // set new attr to node
            SetAttrNoBroadcast(*const_cast<SwContentNode*>(pNodeAfterToc), aGrabBag);
        }
    }
}

void WW8Export::AppendSection( const SwPageDesc *pPageDesc, const SwSectionFormat* pFormat, sal_uLong nLnNum )
{
    m_pSepx->AppendSep(Fc2Cp(Strm().Tell()), pPageDesc, pFormat, nLnNum);
}

// Flys

void WW8AttributeOutput::OutputFlyFrame_Impl( const ww8::Frame& rFormat, const Point& rNdTopLeft )
{
    const SwFrameFormat &rFrameFormat = rFormat.GetFrameFormat();
    const SwFormatAnchor& rAnch = rFrameFormat.GetAnchor();

    bool bUseEscher = true;

    if (rFormat.IsInline())
    {
        ww8::Frame::WriterSource eType = rFormat.GetWriterType();
        bUseEscher = eType != ww8::Frame::eGraphic && eType != ww8::Frame::eOle;

        /*
         A special case for converting some inline form controls to form fields
         when in winword 8+ mode
        */
        if (bUseEscher && (eType == ww8::Frame::eFormControl))
        {
            if ( m_rWW8Export.MiserableFormFieldExportHack( rFrameFormat ) )
                return ;
        }
    }

    if (bUseEscher)
    {
        // write as escher
        if (rFrameFormat.GetFlySplit().GetValue())
        {
            // The frame can split: this was originally from a floating table, write it back as
            // such.
            const SwNodeIndex* pNodeIndex = rFrameFormat.GetContent().GetContentIdx();
            SwNodeOffset nStt = pNodeIndex ? pNodeIndex->GetIndex() + 1 : SwNodeOffset(0);
            SwNodeOffset nEnd = pNodeIndex ? pNodeIndex->GetNode().EndOfSectionIndex() : SwNodeOffset(0);
            m_rWW8Export.SaveData(nStt, nEnd);
            GetExport().WriteText();
            m_rWW8Export.RestoreData();
        }
        else
        {
            m_rWW8Export.AppendFlyInFlys(rFormat, rNdTopLeft);
        }
    }
    else
    {
        bool bDone = false;

        // Fetch from node and last node the position in the section
        const SwNodeIndex* pNodeIndex = rFrameFormat.GetContent().GetContentIdx();

        SwNodeOffset nStt = pNodeIndex ? pNodeIndex->GetIndex()+1                  : SwNodeOffset(0);
        SwNodeOffset nEnd = pNodeIndex ? pNodeIndex->GetNode().EndOfSectionIndex() : SwNodeOffset(0);

        if( nStt >= nEnd )      // no range, hence no valid node
            return;

        if ( !m_rWW8Export.IsInTable() && rFormat.IsInline() )
        {
            //Test to see if this textbox contains only a single graphic/ole
            SwTextNode* pParTextNode = rAnch.GetAnchorNode()->GetTextNode();
            if ( pParTextNode && !m_rWW8Export.m_rDoc.GetNodes()[ nStt ]->IsNoTextNode() )
                bDone = true;
        }
        if( !bDone )
        {

            m_rWW8Export.SaveData( nStt, nEnd );

            Point aOffset;
            if ( m_rWW8Export.m_pParentFrame )
            {
                /* Munge flys in fly into absolutely positioned elements for word 6 */
                const SwTextNode* pParTextNode = rAnch.GetAnchorNode()->GetTextNode();
                const SwRect aPageRect = pParTextNode->FindPageFrameRect();

                aOffset = rFrameFormat.FindLayoutRect().Pos();
                aOffset -= aPageRect.Pos();

                m_rWW8Export.m_pFlyOffset = &aOffset;
                m_rWW8Export.m_eNewAnchorType = RndStdIds::FLY_AT_PAGE;
            }

            m_rWW8Export.m_pParentFrame = &rFormat;
            if (
                m_rWW8Export.IsInTable() &&
                 (RndStdIds::FLY_AT_PAGE != rAnch.GetAnchorId()) &&
                 !m_rWW8Export.m_rDoc.GetNodes()[ nStt ]->IsNoTextNode()
               )
            {
                // note: set Flag  bOutTable again,
                // because we deliver the normal content of the table cell, and no border
                // ( Flag was deleted above in aSaveData() )
                m_rWW8Export.m_bOutTable = true;
                const UIName& aName = rFrameFormat.GetName();
                m_rWW8Export.StartCommentOutput(aName.toString());
                m_rWW8Export.WriteText();
                m_rWW8Export.EndCommentOutput(aName.toString());
            }
            else
                m_rWW8Export.WriteText();

            m_rWW8Export.RestoreData();
        }
    }
}

void AttributeOutputBase::OutputFlyFrame( const ww8::Frame& rFormat )
{
    if ( !rFormat.GetContentNode() )
        return;

    const SwContentNode &rNode = *rFormat.GetContentNode();
    Point aLayPos;

    // get the Layout Node-Position
    if (RndStdIds::FLY_AT_PAGE == rFormat.GetFrameFormat().GetAnchor().GetAnchorId())
        aLayPos = rNode.FindPageFrameRect().Pos();
    else
        aLayPos = rNode.FindLayoutRect().Pos();

    OutputFlyFrame_Impl( rFormat, aLayPos );
}

// write data of any redline
void WW8AttributeOutput::Redline( const SwRedlineData* pRedline )
{
    if ( !pRedline )
        return;

    if ( pRedline->Next() )
        Redline( pRedline->Next() );

    static const sal_uInt16 insSprmIds[ 3 ] =
    {
        // Ids for insert // for WW8
        NS_sprm::CFRMarkIns::val, NS_sprm::CIbstRMark::val, NS_sprm::CDttmRMark::val,
    };
    static const sal_uInt16 delSprmIds[ 3 ] =
    {
        // Ids for delete // for WW8
        NS_sprm::CFRMarkDel::val, NS_sprm::CIbstRMarkDel::val, NS_sprm::CDttmRMarkDel::val,
    };

    bool bRemovePersonalInfo
        = SvtSecurityOptions::IsOptionSet(SvtSecurityOptions::EOption::DocWarnRemovePersonalInfo)
          && !SvtSecurityOptions::IsOptionSet(
                 SvtSecurityOptions::EOption::DocWarnKeepRedlineInfo);

    const sal_uInt16* pSprmIds = nullptr;
    switch( pRedline->GetType() )
    {
    case RedlineType::Insert:
        pSprmIds = insSprmIds;
        break;

    case RedlineType::Delete:
        pSprmIds = delSprmIds;
        break;

    case RedlineType::Format:
        m_rWW8Export.InsUInt16( NS_sprm::CPropRMark90::val );
        m_rWW8Export.m_pO->push_back( 7 );       // len
        m_rWW8Export.m_pO->push_back( 1 );
        m_rWW8Export.InsUInt16( m_rWW8Export.AddRedlineAuthor( pRedline->GetAuthor() ) );
        m_rWW8Export.InsUInt32(sw::ms::DateTime2DTTM(
            bRemovePersonalInfo ? DateTime(DateTime::EMPTY) : pRedline->GetTimeStamp()));
        break;
    default:
        OSL_ENSURE(false, "Unhandled redline type for export");
        break;
    }

    if ( pSprmIds )
    {
        m_rWW8Export.InsUInt16( pSprmIds[0] );
        m_rWW8Export.m_pO->push_back( 1 );

        m_rWW8Export.InsUInt16( pSprmIds[1] );
        m_rWW8Export.InsUInt16( m_rWW8Export.AddRedlineAuthor( pRedline->GetAuthor() ) );

        m_rWW8Export.InsUInt16( pSprmIds[2] );
        m_rWW8Export.InsUInt32(sw::ms::DateTime2DTTM(
            bRemovePersonalInfo ? DateTime(DateTime::EMPTY) : pRedline->GetTimeStamp()));
    }
}

void MSWordExportBase::OutputContentNode( SwContentNode& rNode )
{
    switch ( rNode.GetNodeType() )
    {
        case SwNodeType::Text:
            OutputTextNode(*rNode.GetTextNode());
            break;
        case SwNodeType::Grf:
            OutputGrfNode( *rNode.GetGrfNode() );
            break;
        case SwNodeType::Ole:
            OutputOLENode( *rNode.GetOLENode() );
            break;
        default:
            SAL_WARN("sw.ww8", "Unhandled node, type == " << static_cast<int>(rNode.GetNodeType()) );
            break;
    }
}


WW8Ruby::WW8Ruby(const SwTextNode& rNode, const SwFormatRuby& rRuby, const MSWordExportBase& rExport ):
    m_nJC(0),
    m_cDirective(0),
    m_nRubyHeight(0),
    m_nBaseHeight(0)
{
    switch ( rRuby.GetAdjustment() )
    {
        case css::text::RubyAdjust_LEFT:
            m_nJC = 3;
            m_cDirective = 'l';
            break;
        case css::text::RubyAdjust_CENTER:
            //defaults to 0
            break;
        case css::text::RubyAdjust_RIGHT:
            m_nJC = 4;
            m_cDirective = 'r';
            break;
        case css::text::RubyAdjust_BLOCK:
            m_nJC = 1;
            m_cDirective = 'd';
            break;
        case css::text::RubyAdjust_INDENT_BLOCK:
            m_nJC = 2;
            m_cDirective = 'd';
            break;
        default:
            OSL_ENSURE( false,"Unhandled Ruby justification code" );
            break;
    }

    if ( rRuby.GetPosition() == css::text::RubyPosition::INTER_CHARACTER )
    {
        m_nJC = 5;
        m_cDirective = 0;
    }

    /*
     MS needs to know the name and size of the font used in the ruby item,
     but we could have written it in a mixture of asian and western
     scripts, and each of these can be a different font and size than the
     other, so we make a guess based upon the first character of the text,
     defaulting to asian.
     */
    assert(g_pBreakIt && g_pBreakIt->GetBreakIter().is());
    sal_uInt16 nRubyScript = g_pBreakIt->GetBreakIter()->getScriptType(rRuby.GetText(), 0);

    const SwTextRuby* pRubyText = rRuby.GetTextRuby();
    const SwCharFormat* pFormat = pRubyText ? pRubyText->GetCharFormat() : nullptr;

    if (pFormat)
    {
        const auto& rFont
            = pFormat->GetFormatAttr( GetWhichOfScript(RES_CHRATR_FONT, nRubyScript) );
        m_sFontFamily = rFont.GetFamilyName();

        const auto& rHeight =
            pFormat->GetFormatAttr( GetWhichOfScript(RES_CHRATR_FONTSIZE, nRubyScript) );
        m_nRubyHeight = rHeight.GetHeight();
    }
    else
    {
        /*Get defaults if no formatting on ruby text*/

        const SfxItemPool* pPool = rNode.GetSwAttrSet().GetPool();
        pPool = pPool ? pPool : &rExport.m_rDoc.GetAttrPool();


        const SvxFontItem& rFont
            = pPool->GetUserOrPoolDefaultItem( GetWhichOfScript(RES_CHRATR_FONT, nRubyScript) );
        m_sFontFamily = rFont.GetFamilyName();

        const SvxFontHeightItem& rHeight =
            pPool->GetUserOrPoolDefaultItem( GetWhichOfScript(RES_CHRATR_FONTSIZE, nRubyScript));
        m_nRubyHeight = rHeight.GetHeight();
    }

    const OUString &rText = rNode.GetText();
    sal_uInt16 nScript = i18n::ScriptType::LATIN;

    if (!rText.isEmpty())
        nScript = g_pBreakIt->GetBreakIter()->getScriptType(rText, 0);

    TypedWhichId<SvxFontHeightItem> nWhich = GetWhichOfScript(RES_CHRATR_FONTSIZE, nScript);
    const SvxFontHeightItem& rHeightItem = rExport.GetItem(nWhich);
    m_nBaseHeight = rHeightItem.GetHeight();
}
/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
