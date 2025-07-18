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

#include <com/sun/star/table/TableSortField.hpp>
#include <cppuhelper/exc_hlp.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <svl/itemprop.hxx>
#include <o3tl/any.hxx>
#include <o3tl/safeint.hxx>
#include <unotools/collatorwrapper.hxx>
#include <editeng/memberids.h>

#include <autostyle_helper.hxx>
#include <swtypes.hxx>
#include <hintids.hxx>
#include <cmdid.h>
#include <unomid.h>
#include <hints.hxx>
#include <doc.hxx>
#include <IDocumentUndoRedo.hxx>
#include <istyleaccess.hxx>
#include <ndtxt.hxx>
#include <unocrsr.hxx>
#include <unocrsrhelper.hxx>
#include <unoport.hxx>
#include <swundo.hxx>
#include <rootfrm.hxx>
#include <paratr.hxx>
#include <pam.hxx>
#include <shellio.hxx>
#include <unotbl.hxx>
#include <fmtruby.hxx>
#include <docsh.hxx>
#include <docstyle.hxx>
#include <fmtpdsc.hxx>
#include <pagedesc.hxx>
#include <edimp.hxx>
#include <fchrfmt.hxx>
#include <fmtautofmt.hxx>
#include <unotextrange.hxx>
#include <unotextcursor.hxx>
#include <unomap.hxx>
#include <unoprnms.hxx>
#include <unometa.hxx>
#include <unocontentcontrol.hxx>
#include <unotext.hxx>
#include <com/sun/star/text/TextMarkupType.hpp>
#include <utility>
#include <vcl/svapp.hxx>
#include <unotools/syslocale.hxx>
#include <i18nlangtag/languagetag.hxx>
#include <SwStyleNameMapper.hxx>
#include <sortopt.hxx>
#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/beans/NamedValue.hpp>
#include <com/sun/star/i18n/WordType.hpp>
#include <memory>
#include <unoparaframeenum.hxx>
#include <unoparagraph.hxx>
#include <iodetect.hxx>
#include <comphelper/propertyvalue.hxx>
#include <comphelper/servicehelper.hxx>
#include <comphelper/profilezone.hxx>
#include <comphelper/flagguard.hxx>
#include <swmodule.hxx>
#include <names.hxx>

using namespace ::com::sun::star;

// Helper classes
SwUnoInternalPaM::SwUnoInternalPaM(SwDoc& rDoc) :
    SwPaM(rDoc.GetNodes())
{
}

SwUnoInternalPaM::~SwUnoInternalPaM()
{
    while( GetNext() != this)
    {
        // coverity[deref_arg] - the delete moves a new entry into GetNext()
        delete GetNext();
    }
}

SwUnoInternalPaM&   SwUnoInternalPaM::operator=(const SwPaM& rPaM)
{
    const SwPaM* pTmp = &rPaM;
    *GetPoint() = *rPaM.GetPoint();
    if(rPaM.HasMark())
    {
        SetMark();
        *GetMark() = *rPaM.GetMark();
    }
    else
        DeleteMark();
    while(&rPaM != (pTmp = pTmp->GetNext()))
    {
        if(pTmp->HasMark())
            new SwPaM(*pTmp->GetMark(), *pTmp->GetPoint(), this);
        else
            new SwPaM(*pTmp->GetPoint(), this);
    }
    return *this;
}

void SwUnoCursorHelper::SelectPam(SwPaM & rPam, const bool bExpand)
{
    if (bExpand)
    {
        if (!rPam.HasMark())
        {
            rPam.SetMark();
        }
    }
    else if (rPam.HasMark())
    {
        rPam.DeleteMark();
    }
}

void SwUnoCursorHelper::GetTextFromPam(SwPaM & rPam, OUString & rBuffer,
        SwRootFrame const*const pLayout)
{
    if (!rPam.HasMark())
    {
        return;
    }
    SvMemoryStream aStream;
    aStream.ResetEndianSwap();
    WriterRef xWrt;
    // TODO/MBA: looks like a BaseURL doesn't make sense here
    SwReaderWriter::GetWriter( FILTER_TEXT_DLG, OUString(), xWrt );
    if( !xWrt.is() )
        return;

    SwWriter aWriter( aStream, rPam );
    xWrt->m_bASCII_NoLastLineEnd = true;
    xWrt->m_bExportParagraphNumbering = false;
    SwAsciiOptions aOpt = xWrt->GetAsciiOptions();
    aOpt.SetCharSet( RTL_TEXTENCODING_UNICODE );
    xWrt->SetAsciiOptions( aOpt );
    xWrt->m_bUCS2_WithStartChar = false;
    // #i68522#
    const bool bOldShowProgress = xWrt->m_bShowProgress;
    xWrt->m_bShowProgress = false;
    xWrt->m_bHideDeleteRedlines = pLayout && pLayout->IsHideRedlines();
    // tdf#155951 SwWriter::Write calls EndAllAction, and that
    // called SelectShell(), triggering selection change event, which
    // resulted infinite recursion, if selectionChanged() calls
    // XTextRange::getString() e.g. on the selected range.
    ::comphelper::FlagRestorationGuard g(g_bNoInterrupt, true);

    if( ! aWriter.Write( xWrt ).IsError() )
    {
        const sal_Unicode* p = static_cast<sal_Unicode const*>(aStream.GetData());
        const size_t lUniLen = aStream.GetEndOfData()/sizeof( sal_Unicode );
        if (p && lUniLen < o3tl::make_unsigned(SAL_MAX_INT32-1))
            rBuffer = OUString(p, lUniLen);
    }
    xWrt->m_bShowProgress = bOldShowProgress;

}

/// @throws lang::IllegalArgumentException
/// @throws uno::RuntimeException
static void
lcl_setCharStyle(SwDoc& rDoc, const uno::Any & rValue, SfxItemSet & rSet)
{
    SwDocShell *const pDocSh = rDoc.GetDocShell();
    if(!pDocSh)
        return;

    OUString uStyle;
    if (!(rValue >>= uStyle))
    {
        throw lang::IllegalArgumentException();
    }
    UIName sStyle;
    SwStyleNameMapper::FillUIName(ProgName(uStyle), sStyle,
            SwGetPoolIdFromName::ChrFmt);
    SwDocStyleSheet *const pStyle = static_cast<SwDocStyleSheet*>(
        pDocSh->GetStyleSheetPool()->Find(sStyle.toString(), SfxStyleFamily::Char));
    if (!pStyle)
    {
        throw lang::IllegalArgumentException();
    }
    const SwFormatCharFormat aFormat(pStyle->GetCharFormat());
    rSet.Put(aFormat);
};

/// @throws lang::IllegalArgumentException
static void
lcl_setAutoStyle(IStyleAccess & rStyleAccess, const uno::Any & rValue,
        SfxItemSet & rSet, const bool bPara)
{
    OUString uStyle;
    if (!(rValue >>= uStyle))
    {
         throw lang::IllegalArgumentException();
    }
    std::shared_ptr<SfxItemSet> pStyle = bPara ?
        rStyleAccess.getByName(uStyle, IStyleAccess::AUTO_STYLE_PARA ):
        rStyleAccess.getByName(uStyle, IStyleAccess::AUTO_STYLE_CHAR );
    if(!pStyle)
    {
         throw lang::IllegalArgumentException();
    }

    SwFormatAutoFormat aFormat( bPara
        ? sal::static_int_cast< sal_uInt16 >(RES_AUTO_STYLE)
        : sal::static_int_cast< sal_uInt16 >(RES_TXTATR_AUTOFMT) );
    aFormat.SetStyleHandle( pStyle );
    rSet.Put(aFormat);
};

void
SwUnoCursorHelper::SetTextFormatColl(const uno::Any & rAny, SwPaM & rPaM)
{
    SwDoc& rDoc = rPaM.GetDoc();
    SwDocShell *const pDocSh = rDoc.GetDocShell();
    if(!pDocSh)
        return;
    OUString uStyle;
    rAny >>= uStyle;
    UIName sStyle;
    SwStyleNameMapper::FillUIName(ProgName(uStyle), sStyle,
            SwGetPoolIdFromName::TxtColl );
    SwDocStyleSheet *const pStyle = static_cast<SwDocStyleSheet*>(
            pDocSh->GetStyleSheetPool()->Find(sStyle.toString(), SfxStyleFamily::Para));
    if (!pStyle)
    {
        throw lang::IllegalArgumentException();
    }

    SwTextFormatColl *const pLocal = pStyle->GetCollection();
    UnoActionContext aAction(&rDoc);
    rDoc.GetIDocumentUndoRedo().StartUndo( SwUndoId::START, nullptr );
    SwPaM *pTmpCursor = &rPaM;
    do {
        rDoc.SetTextFormatColl(*pTmpCursor, pLocal);
        pTmpCursor = pTmpCursor->GetNext();
    } while ( pTmpCursor != &rPaM );
    rDoc.GetIDocumentUndoRedo().EndUndo( SwUndoId::END, nullptr );
}

bool
SwUnoCursorHelper::SetPageDesc(
        const uno::Any& rValue, SwDoc & rDoc, SfxItemSet & rSet)
{
    OUString uDescName;
    if (!(rValue >>= uDescName))
    {
        return false;
    }
    std::unique_ptr<SwFormatPageDesc> pNewDesc;
    if(const SwFormatPageDesc* pItem = rSet.GetItemIfSet( RES_PAGEDESC ))
    {
        pNewDesc.reset(new SwFormatPageDesc(*pItem));
    }
    if (!pNewDesc)
    {
        pNewDesc.reset(new SwFormatPageDesc());
    }
    UIName sDescName;
    SwStyleNameMapper::FillUIName(ProgName(uDescName), sDescName,
            SwGetPoolIdFromName::PageDesc);
    if (!pNewDesc->GetPageDesc() ||
        (pNewDesc->GetPageDesc()->GetName() != sDescName))
    {
        bool bPut = false;
        if (!sDescName.isEmpty())
        {
            SwPageDesc *const pPageDesc = SwPageDesc::GetByName(rDoc, sDescName);
            if (!pPageDesc)
            {
                throw lang::IllegalArgumentException();
            }
            pNewDesc->RegisterToPageDesc(*pPageDesc);
            bPut = true;
        }
        if(!bPut)
        {
            rSet.ClearItem(RES_BREAK);
            rSet.Put(SwFormatPageDesc());
        }
        else
        {
            rSet.Put(std::move(pNewDesc));
        }
    }
    return true;
}

static void
lcl_SetNodeNumStart(SwPaM & rCursor, uno::Any const& rValue)
{
    sal_Int16 nTmp = 1;
    rValue >>= nTmp;
    sal_uInt16 nStt = (nTmp < 0 ? USHRT_MAX : o3tl::narrowing<sal_uInt16>(nTmp));
    SwDoc& rDoc = rCursor.GetDoc();
    UnoActionContext aAction(&rDoc);

    if( rCursor.GetNext() != &rCursor )         // MultiSelection?
    {
        rDoc.GetIDocumentUndoRedo().StartUndo( SwUndoId::START, nullptr );
        SwPamRanges aRangeArr( rCursor );
        SwPaM aPam( *rCursor.GetPoint() );
        for( size_t n = 0; n < aRangeArr.Count(); ++n )
        {
          rDoc.SetNumRuleStart(*aRangeArr.SetPam( n, aPam ).GetPoint());
          rDoc.SetNodeNumStart(*aRangeArr.SetPam( n, aPam ).GetPoint(),
                    nStt );
        }
        rDoc.GetIDocumentUndoRedo().EndUndo( SwUndoId::END, nullptr );
    }
    else
    {
        rDoc.SetNumRuleStart( *rCursor.GetPoint());
        rDoc.SetNodeNumStart( *rCursor.GetPoint(), nStt );
    }
}

static bool
lcl_setCharFormatSequence(SwPaM & rPam, uno::Any const& rValue)
{
    uno::Sequence<OUString> aCharStyles;
    if (!(rValue >>= aCharStyles))
    {
        return false;
    }

    for (sal_Int32 nStyle = 0; nStyle < aCharStyles.getLength(); nStyle++)
    {
        uno::Any aStyle;
        rPam.GetDoc().GetIDocumentUndoRedo().StartUndo(SwUndoId::START, nullptr);
        aStyle <<= aCharStyles.getConstArray()[nStyle];
        // create a local set and apply each format directly
        SfxItemSetFixed<RES_TXTATR_CHARFMT, RES_TXTATR_CHARFMT> aSet(rPam.GetDoc().GetAttrPool());
        lcl_setCharStyle(rPam.GetDoc(), aStyle, aSet);
        // the first style should replace the current attributes,
        // all other have to be added
        SwUnoCursorHelper::SetCursorAttr(rPam, aSet, nStyle
                ? SetAttrMode::DONTREPLACE
                : SetAttrMode::DEFAULT);
        rPam.GetDoc().GetIDocumentUndoRedo().EndUndo(SwUndoId::START, nullptr);
    }
    return true;
}

static void
lcl_setDropcapCharStyle(SwPaM const & rPam, SfxItemSet & rItemSet,
        uno::Any const& rValue)
{
    OUString uStyle;
    if (!(rValue >>= uStyle))
    {
        throw lang::IllegalArgumentException();
    }
    UIName sStyle;
    SwStyleNameMapper::FillUIName(ProgName(uStyle), sStyle,
            SwGetPoolIdFromName::ChrFmt);
    SwDoc& rDoc = rPam.GetDoc();
    //default character style must not be set as default format
    SwDocStyleSheet *const pStyle = static_cast<SwDocStyleSheet*>(
            rDoc.GetDocShell()
            ->GetStyleSheetPool()->Find(sStyle.toString(), SfxStyleFamily::Char));
    if (!pStyle || pStyle->GetCharFormat() == rDoc.GetDfltCharFormat())
    {
        throw lang::IllegalArgumentException();
    }
    std::unique_ptr<SwFormatDrop> pDrop;
    if (const SwFormatDrop* pItem = rItemSet.GetItemIfSet(RES_PARATR_DROP))
    {
        pDrop.reset(new SwFormatDrop(*pItem));
    }
    if (!pDrop)
    {
        pDrop.reset(new SwFormatDrop);
    }
    const rtl::Reference<SwDocStyleSheet> xStyle(new SwDocStyleSheet(*pStyle));
    pDrop->SetCharFormat(xStyle->GetCharFormat());
    rItemSet.Put(std::move(pDrop));
}

static void
lcl_setRubyCharstyle(SfxItemSet & rItemSet, uno::Any const& rValue)
{
    OUString sTmp;
    if (!(rValue >>= sTmp))
    {
        throw lang::IllegalArgumentException();
    }

    std::unique_ptr<SwFormatRuby> pRuby;
    if (const SwFormatRuby* pItem = rItemSet.GetItemIfSet(RES_TXTATR_CJK_RUBY))
    {
        pRuby.reset(new SwFormatRuby(*pItem));
    }
    if (!pRuby)
    {
        pRuby.reset(new SwFormatRuby(OUString()));
    }
    UIName sStyle;
    SwStyleNameMapper::FillUIName(ProgName(sTmp), sStyle,
            SwGetPoolIdFromName::ChrFmt);
    pRuby->SetCharFormatName(sStyle);
    pRuby->SetCharFormatId(0);
    if (!sStyle.isEmpty())
    {
        const sal_uInt16 nId = SwStyleNameMapper::GetPoolIdFromUIName(
                sStyle, SwGetPoolIdFromName::ChrFmt);
        pRuby->SetCharFormatId(nId);
    }
    rItemSet.Put(std::move(pRuby));
}

bool
SwUnoCursorHelper::SetCursorPropertyValue(
        SfxItemPropertyMapEntry const& rEntry, const uno::Any& rValue,
        SwPaM & rPam, SfxItemSet & rItemSet)
{
    if (!(rEntry.nFlags & beans::PropertyAttribute::MAYBEVOID) &&
        (rValue.getValueType() == cppu::UnoType<void>::get()))
    {
        return false;
    }
    bool bRet = true;
    switch (rEntry.nWID)
    {
        case RES_TXTATR_CHARFMT:
            lcl_setCharStyle(rPam.GetDoc(), rValue, rItemSet);
        break;
        case RES_TXTATR_AUTOFMT:
            lcl_setAutoStyle(rPam.GetDoc().GetIStyleAccess(),
                    rValue, rItemSet, false);
        break;
        case FN_UNO_CHARFMT_SEQUENCE:
            lcl_setCharFormatSequence(rPam, rValue);
        break;
        case FN_UNO_PARA_STYLE :
            SwUnoCursorHelper::SetTextFormatColl(rValue, rPam);
        break;
        case RES_AUTO_STYLE:
            lcl_setAutoStyle(rPam.GetDoc().GetIStyleAccess(),
                    rValue, rItemSet, true);
        break;
        case FN_UNO_PAGE_STYLE:
            //FIXME nothing here?
        break;
        case FN_UNO_NUM_START_VALUE:
            lcl_SetNodeNumStart( rPam, rValue );
        break;
        case FN_UNO_NUM_LEVEL:
        // #i91601#
        case FN_UNO_LIST_ID:
        case FN_UNO_IS_NUMBER:
        case FN_UNO_PARA_NUM_AUTO_FORMAT:
        {
            // multi selection is not considered
            SwTextNode *const pTextNd = rPam.GetPointNode().GetTextNode();
            if (!pTextNd)
            {
                throw lang::IllegalArgumentException();
            }
            if (FN_UNO_NUM_LEVEL == rEntry.nWID)
            {
                sal_Int16 nLevel = 0;
                if (rValue >>= nLevel)
                {
                    if (nLevel < 0 || MAXLEVEL <= nLevel)
                    {
                        throw lang::IllegalArgumentException(
                            u"invalid NumberingLevel"_ustr, nullptr, 0);
                    }
                    pTextNd->SetAttrListLevel(nLevel);
                }
            }
            // #i91601#
            else if (FN_UNO_LIST_ID == rEntry.nWID)
            {
                OUString sListId;
                if (rValue >>= sListId)
                {
                    pTextNd->SetListId( sListId );
                }
            }
            else if (FN_UNO_IS_NUMBER == rEntry.nWID)
            {
                bool bIsNumber(false);
                if ((rValue >>= bIsNumber) && !bIsNumber)
                {
                    pTextNd->SetCountedInList( false );
                }
            }
            else if (FN_UNO_PARA_NUM_AUTO_FORMAT == rEntry.nWID)
            {
                std::shared_ptr<SfxItemSet> pAutoStyle;
                if (uno::Sequence<beans::NamedValue> props; rValue >>= props)
                {
                    // TODO create own map for this, it contains UNO_NAME_DISPLAY_NAME? or make property readable so ODF export can map it to a automatic style?
                    SfxItemPropertySet const& rPropSet(*aSwMapProvider.GetPropertySet(PROPERTY_MAP_CHAR_AUTO_STYLE));
                    SfxItemPropertyMap const& rMap(rPropSet.getPropertyMap());
                    SfxItemSetFixed
                        <RES_CHRATR_BEGIN, RES_CHRATR_END-1,
                            RES_TXTATR_CHARFMT, RES_TXTATR_CHARFMT,
                            RES_UNKNOWNATR_BEGIN, RES_UNKNOWNATR_END-1>
                         items( rPam.GetDoc().GetAttrPool() );

                    for (beans::NamedValue const& prop : props)
                    {
                        SfxItemPropertyMapEntry const*const pEntry =
                            rMap.getByName(prop.Name);
                        if (!pEntry)
                        {
                            throw beans::UnknownPropertyException(
                                "Unknown property: " + prop.Name);
                        }
                        if (pEntry->nFlags & beans::PropertyAttribute::READONLY)
                        {
                            throw beans::PropertyVetoException(
                                "Property is read-only: " + prop.Name);
                        }
                        if (prop.Name == "CharStyleName")
                        {
                            lcl_setCharStyle(rPam.GetDoc(), prop.Value, items);
                        }
                        else
                        {
                            SfxItemPropertySet::setPropertyValue(*pEntry, prop.Value, items);
                        }
                    }

                    IStyleAccess& rStyleAccess = rPam.GetDoc().GetIStyleAccess();
                    // Add it to the autostyle pool, needed by the ODT export.
                    pAutoStyle = rStyleAccess.getAutomaticStyle(items, IStyleAccess::AUTO_STYLE_CHAR);
                }
                else if (OUString styleName; rValue >>= styleName)
                {
                    IStyleAccess& rStyleAccess = rPam.GetDoc().GetIStyleAccess();
                    pAutoStyle = rStyleAccess.getByName(styleName, IStyleAccess::AUTO_STYLE_CHAR);
                }
                if (pAutoStyle)
                {
                    SwFormatAutoFormat item(RES_PARATR_LIST_AUTOFMT);
                    // note: paragraph auto styles have ParaStyleName property for the parent style; character auto styles currently do not because there's a separate hint, but for this it would be a good way to add it in order to export it as style:parent-style-name, see XMLTextParagraphExport::Add()
                    item.SetStyleHandle(pAutoStyle);
                    pTextNd->SetAttr(item);
                }
            }
            //PROPERTY_MAYBEVOID!
        }
        break;
        case FN_NUMBER_NEWSTART:
        {
            bool bVal = false;
            if (!(rValue >>= bVal))
            {
                throw lang::IllegalArgumentException();
            }
            rPam.GetDoc().SetNumRuleStart(*rPam.GetPoint(), bVal);
        }
        break;
        case FN_UNO_NUM_RULES:
            SwUnoCursorHelper::setNumberingProperty(rValue, rPam);
        break;
        case RES_PARATR_DROP:
        {
            if (MID_DROPCAP_CHAR_STYLE_NAME == rEntry.nMemberId)
            {
                lcl_setDropcapCharStyle(rPam, rItemSet, rValue);
            }
            else
            {
                bRet = false;
            }
        }
        break;
        case RES_TXTATR_CJK_RUBY:
        {
            if (MID_RUBY_CHARSTYLE == rEntry.nMemberId)
            {
                lcl_setRubyCharstyle(rItemSet, rValue);
            }
            else
            {
                bRet = false;
            }
        }
        break;
        case RES_PAGEDESC:
        {
            if (MID_PAGEDESC_PAGEDESCNAME == rEntry.nMemberId)
            {
                SwUnoCursorHelper::SetPageDesc(
                        rValue, rPam.GetDoc(), rItemSet);
            }
            else
            {
                bRet = false;
            }
        }
        break;
        default:
            bRet = false;
    }
    return bRet;
}

SwFormatColl *
SwUnoCursorHelper::GetCurTextFormatColl(SwPaM & rPaM, const bool bConditional)
{
    static constexpr sal_Int32 nMaxLookup = 1000;
    SwFormatColl *pFormat = nullptr;
    bool bError = false;
    SwPaM *pTmpCursor = &rPaM;
    do
    {
        const SwNodeOffset nSttNd = pTmpCursor->Start()->GetNodeIndex();
        const SwNodeOffset nEndNd = pTmpCursor->End()->GetNodeIndex();

        if( nEndNd - nSttNd >= SwNodeOffset(nMaxLookup) )
        {
            pFormat = nullptr;
            break;
        }

        const SwNodes& rNds = rPaM.GetDoc().GetNodes();
        for( SwNodeOffset n = nSttNd; n <= nEndNd; ++n )
        {
            SwTextNode const*const pNd = rNds[ n ]->GetTextNode();
            if( pNd )
            {
                SwFormatColl *const pNdFormat = bConditional
                    ? pNd->GetFormatColl() : &pNd->GetAnyFormatColl();
                if( !pFormat )
                {
                    pFormat = pNdFormat;
                }
                else if( pFormat != pNdFormat )
                {
                    bError = true;
                    break;
                }
            }
        }

        pTmpCursor = pTmpCursor->GetNext();
    } while ( pTmpCursor != &rPaM );
    return bError ? nullptr : pFormat;
}

SwUnoCursor& SwXTextCursor::GetCursor()
    { return *m_pUnoCursor; }

SwPaM const* SwXTextCursor::GetPaM() const
    { return m_pUnoCursor.get(); }

SwPaM* SwXTextCursor::GetPaM()
    { return m_pUnoCursor.get(); }

SwDoc const* SwXTextCursor::GetDoc() const
    { return m_pUnoCursor ? &m_pUnoCursor->GetDoc() : nullptr; }

SwDoc* SwXTextCursor::GetDoc()
    { return m_pUnoCursor ? &m_pUnoCursor->GetDoc() : nullptr; }

SwXTextCursor::SwXTextCursor(
        SwDoc & rDoc,
        uno::Reference< text::XText > xParent,
        const CursorType eType,
        const SwPosition& rPos,
        SwPosition const*const pMark)
    : m_rPropSet(*aSwMapProvider.GetPropertySet(PROPERTY_MAP_TEXT_CURSOR))
    , m_eType(eType)
    , m_xParentText(std::move(xParent))
    , m_pUnoCursor(rDoc.CreateUnoCursor(rPos))
{
    if (pMark)
    {
        m_pUnoCursor->SetMark();
        *m_pUnoCursor->GetMark() = *pMark;
    }
}

SwXTextCursor::SwXTextCursor(uno::Reference< text::XText > xParent,
        SwPaM const& rSourceCursor, const CursorType eType)
    : m_rPropSet(*aSwMapProvider.GetPropertySet(PROPERTY_MAP_TEXT_CURSOR))
    , m_eType(eType)
    , m_xParentText(std::move(xParent))
    , m_pUnoCursor(rSourceCursor.GetDoc().CreateUnoCursor(*rSourceCursor.GetPoint()))
{
    if (rSourceCursor.HasMark())
    {
        m_pUnoCursor->SetMark();
        *m_pUnoCursor->GetMark() = *rSourceCursor.GetMark();
    }
}

SwXTextCursor::~SwXTextCursor()
{
    SolarMutexGuard g; // #i105557#: call dtor with locked solar mutex
    m_pUnoCursor.reset(nullptr); // need to delete this with SolarMutex held
}

void SwXTextCursor::DeleteAndInsert(std::u16string_view aText,
        ::sw::DeleteAndInsertMode const eMode)
{
    auto pUnoCursor = static_cast<SwCursor*>(m_pUnoCursor.get());
    if (!pUnoCursor)
        return;

    // Start/EndAction
    SwDoc& rDoc = pUnoCursor->GetDoc();
    UnoActionContext aAction(&rDoc);
    const sal_Int32 nTextLen = aText.size();
    rDoc.GetIDocumentUndoRedo().StartUndo(SwUndoId::INSERT, nullptr);
    auto pCurrent = pUnoCursor;
    do
    {
        if (pCurrent->HasMark())
        {
            rDoc.getIDocumentContentOperations().DeleteAndJoin(*pCurrent,
                // is it "delete" or "replace"?
                (nTextLen != 0 || eMode & ::sw::DeleteAndInsertMode::ForceReplace) ? SwDeleteFlags::ArtificialSelection : SwDeleteFlags::Default);
        }
        if(nTextLen)
        {
            // Store node and content indexes prior to insertion: to select the inserted text,
            // we need to account for possible surrogate pairs, combining characters, etc.; it
            // is easier to just restore the correct position from the indexes.
            const auto start = pCurrent->Start();
            const auto nodeIndex = start->GetNodeIndex();
            const auto contentIndex = start->GetContentIndex();
            const bool bSuccess(
                SwUnoCursorHelper::DocInsertStringSplitCR(
                    rDoc, SwPaM(*start, pCurrent), aText, bool(eMode & ::sw::DeleteAndInsertMode::ForceExpandHints)));
            OSL_ENSURE( bSuccess, "Doc->Insert(Str) failed." );

            pCurrent->SetMark();
            pCurrent->GetPoint()->Assign(nodeIndex, contentIndex);
        }
        pCurrent = pCurrent->GetNext();
    } while (pCurrent != pUnoCursor);
    rDoc.GetIDocumentUndoRedo().EndUndo(SwUndoId::INSERT, nullptr);
}

namespace {

enum ForceIntoMetaMode { META_CHECK_BOTH, META_INIT_START, META_INIT_END };

enum ForceIntoContentControlMode
{
    CONTENT_CONTROL_CHECK_BOTH,
    CONTENT_CONTROL_INIT_START,
    CONTENT_CONTROL_INIT_END
};
}

static bool
lcl_ForceIntoMeta(SwPaM & rCursor,
        uno::Reference<text::XText> const & xParentText,
        const enum ForceIntoMetaMode eMode)
{
    bool bRet( true ); // means not forced in META_CHECK_BOTH
    SwXMeta const * const pXMeta( dynamic_cast<SwXMeta*>(xParentText.get()) );
    OSL_ENSURE(pXMeta, "no parent?");
    if (!pXMeta)
        throw uno::RuntimeException();
    SwTextNode * pTextNode;
    sal_Int32 nStart;
    sal_Int32 nEnd;
    const bool bSuccess( pXMeta->SetContentRange(pTextNode, nStart, nEnd) );
    OSL_ENSURE(bSuccess, "no pam?");
    if (!bSuccess)
        throw uno::RuntimeException();
    // force the cursor back into the meta if it has moved outside
    SwPosition start(*pTextNode, nStart);
    SwPosition end(*pTextNode, nEnd);
    switch (eMode)
    {
        case META_INIT_START:
            *rCursor.GetPoint() = start;
            break;
        case META_INIT_END:
            *rCursor.GetPoint() = end;
            break;
        case META_CHECK_BOTH:
            if (*rCursor.Start() < start)
            {
                *rCursor.Start() = std::move(start);
                bRet = false;
            }
            if (*rCursor.End() > end)
            {
                *rCursor.End() = std::move(end);
                bRet = false;
            }
            break;
    }
    return bRet;
}

namespace
{
bool lcl_ForceIntoContentControl(SwPaM& rCursor, const uno::Reference<text::XText>& xParentText,
                                 ForceIntoContentControlMode eMode)
{
    bool bRet = true; // means not forced in CONTENT_CONTROL_CHECK_BOTH
    auto pXContentControl = dynamic_cast<SwXContentControl*>(xParentText.get());
    if (!pXContentControl)
    {
        SAL_WARN("sw.core", "lcl_ForceIntoContentControl: no parent text");
        throw uno::RuntimeException();
    }

    SwTextNode* pTextNode;
    sal_Int32 nStart;
    sal_Int32 nEnd;
    bool bSuccess = pXContentControl->SetContentRange(pTextNode, nStart, nEnd);
    if (!bSuccess)
    {
        SAL_WARN("sw.core", "lcl_ForceIntoContentControl: SetContentRange() failed");
        throw uno::RuntimeException();
    }

    // Force the cursor back into the content control if it has moved outside.
    SwPosition aStart(*pTextNode, nStart);
    SwPosition aEnd(*pTextNode, nEnd);
    switch (eMode)
    {
        case CONTENT_CONTROL_INIT_START:
            *rCursor.GetPoint() = aStart;
            break;

        case CONTENT_CONTROL_INIT_END:
            *rCursor.GetPoint() = aEnd;
            break;

        case CONTENT_CONTROL_CHECK_BOTH:
            if (*rCursor.Start() < aStart)
            {
                *rCursor.Start() = std::move(aStart);
                bRet = false;
            }

            if (*rCursor.End() > aEnd)
            {
                *rCursor.End() = std::move(aEnd);
                bRet = false;
            }
            break;
    }

    return bRet;
}
}

bool SwXTextCursor::IsAtEndOfMeta() const
{
    if (CursorType::Meta == m_eType)
    {
        sw::UnoCursorPointer pCursor( m_pUnoCursor );
        SwXMeta const*const pXMeta(
                dynamic_cast<SwXMeta*>(m_xParentText.get()) );
        OSL_ENSURE(pXMeta, "no meta?");
        if (pCursor && pXMeta)
        {
            SwTextNode * pTextNode;
            sal_Int32 nStart;
            sal_Int32 nEnd;
            const bool bSuccess(
                    pXMeta->SetContentRange(pTextNode, nStart, nEnd) );
            OSL_ENSURE(bSuccess, "no pam?");
            if (bSuccess)
            {
                const SwPosition end(*pTextNode, nEnd);
                if (   (*pCursor->GetPoint() == end)
                    || (*pCursor->GetMark()  == end))
                {
                    return true;
                }
            }
        }
    }
    return false;
}

bool SwXTextCursor::IsAtEndOfContentControl() const
{
    if (CursorType::ContentControl == m_eType)
    {
        sw::UnoCursorPointer pCursor( m_pUnoCursor );
        auto pXContentControl(
                dynamic_cast<SwXContentControl*>(m_xParentText.get()) );
        if (!pXContentControl)
        {
            SAL_WARN("sw.core", "SwXTextCursor::IsAtEndOfContentControl: no content control");
        }
        if (pCursor && pXContentControl)
        {
            SwTextNode * pTextNode;
            sal_Int32 nStart;
            sal_Int32 nEnd;
            const bool bSuccess(
                    pXContentControl->SetContentRange(pTextNode, nStart, nEnd) );
            if (!bSuccess)
            {
                SAL_WARN("sw.core", "SwXTextCursor::IsAtEndOfContentControl: no pam");
            }
            else
            {
                const SwPosition end(*pTextNode, nEnd);
                if (   (*pCursor->GetPoint() == end)
                    || (*pCursor->GetMark()  == end))
                {
                    return true;
                }
            }
        }
    }
    return false;
}

OUString SwXTextCursor::getImplementationName()
{
    return u"SwXTextCursor"_ustr;
}

sal_Bool SAL_CALL SwXTextCursor::supportsService(const OUString& rServiceName)
{
    return cppu::supportsService(this, rServiceName);
}

uno::Sequence< OUString > SAL_CALL
SwXTextCursor::getSupportedServiceNames()
{
    return {
        u"com.sun.star.text.TextCursor"_ustr,
        u"com.sun.star.style.CharacterProperties"_ustr,
        u"com.sun.star.style.CharacterPropertiesAsian"_ustr,
        u"com.sun.star.style.CharacterPropertiesComplex"_ustr,
        u"com.sun.star.style.ParagraphProperties"_ustr,
        u"com.sun.star.style.ParagraphPropertiesAsian"_ustr,
        u"com.sun.star.style.ParagraphPropertiesComplex"_ustr,
        u"com.sun.star.text.TextSortable"_ustr
    };
}

void SAL_CALL SwXTextCursor::collapseToStart()
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    if (rUnoCursor.HasMark())
    {
        if (*rUnoCursor.GetPoint() > *rUnoCursor.GetMark())
        {
            rUnoCursor.Exchange();
        }
        rUnoCursor.DeleteMark();
    }
}

void SAL_CALL SwXTextCursor::collapseToEnd()
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    if (rUnoCursor.HasMark())
    {
        if (*rUnoCursor.GetPoint() < *rUnoCursor.GetMark())
        {
            rUnoCursor.Exchange();
        }
        rUnoCursor.DeleteMark();
    }
}

sal_Bool SAL_CALL SwXTextCursor::isCollapsed()
{
    SolarMutexGuard aGuard;

    bool bRet = true;
    sw::UnoCursorPointer pUnoCursor(m_pUnoCursor);
    if(pUnoCursor && pUnoCursor->GetMark())
    {
        bRet = (*pUnoCursor->GetPoint() == *pUnoCursor->GetMark());
    }
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::goLeft(sal_Int16 nCount, sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    bool bRet = rUnoCursor.Left( nCount);
    if (CursorType::Meta == m_eType)
    {
        bRet = lcl_ForceIntoMeta(rUnoCursor, m_xParentText,
                    META_CHECK_BOTH)
            && bRet;
    }
    else if (m_eType == CursorType::ContentControl)
    {
        bRet = lcl_ForceIntoContentControl(rUnoCursor, m_xParentText, CONTENT_CONTROL_CHECK_BOTH)
               && bRet;
    }
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::goRight(sal_Int16 nCount, sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    bool bRet = rUnoCursor.Right(nCount);
    if (CursorType::Meta == m_eType)
    {
        bRet = lcl_ForceIntoMeta(rUnoCursor, m_xParentText,
                    META_CHECK_BOTH)
            && bRet;
    }
    else if (m_eType == CursorType::ContentControl)
    {
        bRet = lcl_ForceIntoContentControl(rUnoCursor, m_xParentText, CONTENT_CONTROL_CHECK_BOTH)
               && bRet;
    }
    return bRet;
}

void SAL_CALL
SwXTextCursor::gotoStart(sal_Bool Expand)
{
    SolarMutexGuard aGuard;
    comphelper::ProfileZone aZone("gotoStart");

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    if (CursorType::Body == m_eType)
    {
        rUnoCursor.Move( fnMoveBackward, GoInDoc );
        //check, that the cursor is not in a table
        SwTableNode * pTableNode = rUnoCursor.GetPointNode().FindTableNode();
        while (pTableNode)
        {
            rUnoCursor.GetPoint()->Assign( *pTableNode->EndOfSectionNode() );
            SwContentNode* pCNode = SwNodes::GoNext(rUnoCursor.GetPoint());
            pTableNode = pCNode ? pCNode->FindTableNode() : nullptr;
        }
        SwStartNode const*const pTmp =
            rUnoCursor.GetPointNode().StartOfSectionNode();
        if (pTmp->IsSectionNode())
        {
            SwSectionNode const*const pSectionStartNode =
                static_cast<SwSectionNode const*>(pTmp);
            if (pSectionStartNode->GetSection().IsHiddenFlag())
            {
                SwNodes::GoNextSection(
                        rUnoCursor.GetPoint(), true, false);
            }
        }
    }
    else if (   (CursorType::Frame   == m_eType)
            ||  (CursorType::TableText == m_eType)
            ||  (CursorType::Header  == m_eType)
            ||  (CursorType::Footer  == m_eType)
            ||  (CursorType::Footnote== m_eType)
            ||  (CursorType::Redline == m_eType))
    {
        rUnoCursor.MoveSection(GoCurrSection, fnSectionStart);
    }
    else if (CursorType::Meta == m_eType)
    {
        lcl_ForceIntoMeta(rUnoCursor, m_xParentText, META_INIT_START);
    }
    else if (m_eType == CursorType::ContentControl)
    {
        lcl_ForceIntoContentControl(rUnoCursor, m_xParentText, CONTENT_CONTROL_INIT_START);
    }
}

void SAL_CALL
SwXTextCursor::gotoEnd(sal_Bool Expand)
{
    SolarMutexGuard aGuard;
    comphelper::ProfileZone aZone("gotoEnd");

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    if (CursorType::Body == m_eType)
    {
        rUnoCursor.Move( fnMoveForward, GoInDoc );
    }
    else if (   (CursorType::Frame   == m_eType)
            ||  (CursorType::TableText == m_eType)
            ||  (CursorType::Header  == m_eType)
            ||  (CursorType::Footer  == m_eType)
            ||  (CursorType::Footnote== m_eType)
            ||  (CursorType::Redline == m_eType))
    {
        rUnoCursor.MoveSection( GoCurrSection, fnSectionEnd);
    }
    else if (CursorType::Meta == m_eType)
    {
        lcl_ForceIntoMeta(rUnoCursor, m_xParentText, META_INIT_END);
    }
    else if (m_eType == CursorType::ContentControl)
    {
        lcl_ForceIntoContentControl(rUnoCursor, m_xParentText, CONTENT_CONTROL_INIT_END);
    }
}

void SAL_CALL
SwXTextCursor::gotoRange(
    const uno::Reference< text::XTextRange > & xRange, sal_Bool bExpand)
{
    SolarMutexGuard aGuard;
    if (!xRange.is())
    {
        throw uno::RuntimeException();
    }
    SwXTextRange* pRange = dynamic_cast<SwXTextRange*>(xRange.get());
    OTextCursorHelper* pCursor = dynamic_cast<OTextCursorHelper*>(xRange.get());
    if (!pRange && !pCursor)
    {
        throw uno::RuntimeException();
    }

    gotoRangeImpl(pRange, pCursor, bExpand);
}

void
SwXTextCursor::gotoRangeImpl(
    const SwXTextRange* pRange,
    OTextCursorHelper* pCursor,
    bool bExpand)
{
    DBG_TESTSOLARMUTEX();
    assert((pRange || pCursor) && "one of these parameters must be non-null");

    SwUnoCursor & rOwnCursor( GetCursorOrThrow() );

    SwPaM aPam(GetDoc()->GetNodes());
    const SwPaM * pPam(nullptr);
    if (pCursor)
    {
        pPam = pCursor->GetPaM();
    }
    else
    {
        if (pRange->GetPositions(aPam))
        {
            pPam = & aPam;
        }
    }

    if (!pPam)
    {
        throw uno::RuntimeException();
    }

    {
        SwStartNodeType eSearchNodeType = SwNormalStartNode;
        switch (m_eType)
        {
        case CursorType::Frame:      eSearchNodeType = SwFlyStartNode;       break;
        case CursorType::TableText:    eSearchNodeType = SwTableBoxStartNode;  break;
        case CursorType::Footnote:   eSearchNodeType = SwFootnoteStartNode;  break;
        case CursorType::Header:     eSearchNodeType = SwHeaderStartNode;    break;
        case CursorType::Footer:     eSearchNodeType = SwFooterStartNode;    break;
            //case CURSOR_INVALID:
            //case CursorType::Body:
        default:
            ;
        }

        const SwStartNode* pOwnStartNode = rOwnCursor.GetPointNode().FindStartNodeByType(eSearchNodeType);
        while ( pOwnStartNode != nullptr
                && pOwnStartNode->IsSectionNode())
        {
            pOwnStartNode = pOwnStartNode->StartOfSectionNode();
        }

        const SwStartNode* pTmp =
            pPam->GetPointNode().FindStartNodeByType(eSearchNodeType);
        while ( pTmp != nullptr
                && pTmp->IsSectionNode() )
        {
            pTmp = pTmp->StartOfSectionNode();
        }

        if ( eSearchNodeType == SwTableBoxStartNode )
        {
            if (!pOwnStartNode || !pTmp)
            {
                throw uno::RuntimeException();
            }

            if ( pOwnStartNode->FindTableNode() != pTmp->FindTableNode() )
            {
                throw uno::RuntimeException();
            }
        }
        else
        {
            if ( pOwnStartNode != pTmp )
            {
                throw uno::RuntimeException();
            }
        }
    }

    if (CursorType::Meta == m_eType)
    {
        SwPaM CopyPam(*pPam->GetMark(), *pPam->GetPoint());
        const bool bNotForced( lcl_ForceIntoMeta(
                    CopyPam, m_xParentText, META_CHECK_BOTH) );
        if (!bNotForced)
        {
            throw uno::RuntimeException(
                u"gotoRange: parameter range not contained in nesting"
                    " text content for which this cursor was created"_ustr,
                static_cast<text::XWordCursor*>(this));
        }
    }
    else if (m_eType == CursorType::ContentControl)
    {
        SwPaM aPaM(*pPam->GetMark(), *pPam->GetPoint());
        if (!lcl_ForceIntoContentControl(aPaM, m_xParentText, CONTENT_CONTROL_CHECK_BOTH))
        {
            throw uno::RuntimeException(u"gotoRange: xRange is out of bounds of the content control"_ustr,
                                        static_cast<text::XWordCursor*>(this));
        }
    }

    // selection has to be expanded here
    if(bExpand)
    {
        // cursor should include its previous range plus the given range
        const SwPosition aOwnLeft(*rOwnCursor.Start());
        const SwPosition aOwnRight(*rOwnCursor.End());
        SwPosition const& rParamLeft  = *pPam->Start();
        SwPosition const& rParamRight = *pPam->End();

        // now there are four SwPositions,
        // two of them are going to be used, but which ones?
        if (aOwnRight > rParamRight)
            *rOwnCursor.GetPoint() = aOwnRight;
        else
            *rOwnCursor.GetPoint() = rParamRight;
        rOwnCursor.SetMark();
        if (aOwnLeft < rParamLeft)
            *rOwnCursor.GetMark() = aOwnLeft;
        else
            *rOwnCursor.GetMark() = rParamLeft;
    }
    else
    {
        // cursor should be the given range
        *rOwnCursor.GetPoint() = *pPam->GetPoint();
        if (pPam->HasMark())
        {
            rOwnCursor.SetMark();
            *rOwnCursor.GetMark() = *pPam->GetMark();
        }
        else
        {
            rOwnCursor.DeleteMark();
        }
    }
}

sal_Bool SAL_CALL SwXTextCursor::isStartOfWord()
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    const bool bRet =
        rUnoCursor.IsStartWordWT( i18n::WordType::DICTIONARY_WORD );
    return bRet;
}

sal_Bool SAL_CALL SwXTextCursor::isEndOfWord()
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    const bool bRet =
        rUnoCursor.IsEndWordWT( i18n::WordType::DICTIONARY_WORD );
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::gotoNextWord(sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    // problems arise when a paragraph starts with something other than a word
    bool bRet = false;
    // remember old position to check if cursor has moved
    // since the called functions are sometimes a bit unreliable
    // in specific cases...
    SwPosition  *const pPoint     = rUnoCursor.GetPoint();
    SwNode      *const pOldNode   = &pPoint->GetNode();
    sal_Int32 const nOldIndex  = pPoint->GetContentIndex();

    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    // end of paragraph
    if (rUnoCursor.GetPointContentNode() &&
            (pPoint->GetContentIndex() == rUnoCursor.GetPointContentNode()->Len()))
    {
        rUnoCursor.Right(1);
    }
    else
    {
        const bool bTmp =
            rUnoCursor.GoNextWordWT( i18n::WordType::DICTIONARY_WORD );
        // if there is no next word within the current paragraph
        // try to go to the start of the next paragraph
        if (!bTmp)
        {
            rUnoCursor.MovePara(GoNextPara, fnParaStart);
        }
    }

    // return true if cursor has moved
    bRet =  (&pPoint->GetNode() != pOldNode)  ||
            (pPoint->GetContentIndex() != nOldIndex);
    if (bRet && (CursorType::Meta == m_eType))
    {
        bRet = lcl_ForceIntoMeta(rUnoCursor, m_xParentText,
                    META_CHECK_BOTH);
    }
    else if (bRet && m_eType == CursorType::ContentControl)
    {
        bRet = lcl_ForceIntoContentControl(rUnoCursor, m_xParentText, CONTENT_CONTROL_CHECK_BOTH);
    }

    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::gotoPreviousWord(sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    // white spaces create problems on the paragraph start
    bool bRet = false;
    SwPosition  *const pPoint     = rUnoCursor.GetPoint();
    SwNode      *const pOldNode   = &pPoint->GetNode();
    sal_Int32 const nOldIndex  = pPoint->GetContentIndex();

    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    // start of paragraph?
    if (pPoint->GetContentIndex() == 0)
    {
        rUnoCursor.Left(1);
    }
    else
    {
        rUnoCursor.GoPrevWordWT( i18n::WordType::DICTIONARY_WORD );
        if (pPoint->GetContentIndex() == 0)
        {
            rUnoCursor.Left(1);
        }
    }

    // return true if cursor has moved
    bRet =  (&pPoint->GetNode() != pOldNode)  ||
            (pPoint->GetContentIndex() != nOldIndex);
    if (bRet && (CursorType::Meta == m_eType))
    {
        bRet = lcl_ForceIntoMeta(rUnoCursor, m_xParentText,
                    META_CHECK_BOTH);
    }
    else if (bRet && m_eType == CursorType::ContentControl)
    {
        bRet = lcl_ForceIntoContentControl(rUnoCursor, m_xParentText, CONTENT_CONTROL_CHECK_BOTH);
    }

    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::gotoEndOfWord(sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    bool bRet = false;
    SwPosition  *const pPoint     = rUnoCursor.GetPoint();
    SwNode      &      rOldNode   = pPoint->GetNode();
    sal_Int32 const nOldIndex  = pPoint->GetContentIndex();

    const sal_Int16 nWordType = i18n::WordType::DICTIONARY_WORD;
    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    if (!rUnoCursor.IsEndWordWT( nWordType ))
    {
        rUnoCursor.GoEndWordWT( nWordType );
    }

    // restore old cursor if we are not at the end of a word by now
    // otherwise use current one
    bRet = rUnoCursor.IsEndWordWT( nWordType );
    if (!bRet)
    {
        pPoint->Assign(rOldNode, nOldIndex);
    }
    else if (CursorType::Meta == m_eType)
    {
        bRet = lcl_ForceIntoMeta(rUnoCursor, m_xParentText,
                    META_CHECK_BOTH);
    }
    else if (m_eType == CursorType::ContentControl)
    {
        bRet = lcl_ForceIntoContentControl(rUnoCursor, m_xParentText, CONTENT_CONTROL_CHECK_BOTH);
    }

    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::gotoStartOfWord(sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    bool bRet = false;
    SwPosition  *const pPoint     = rUnoCursor.GetPoint();
    SwNode      &      rOldNode   = pPoint->GetNode();
    sal_Int32 const nOldIndex  = pPoint->GetContentIndex();

    const sal_Int16 nWordType = i18n::WordType::DICTIONARY_WORD;
    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    if (!rUnoCursor.IsStartWordWT( nWordType ))
    {
        rUnoCursor.GoStartWordWT( nWordType );
    }

    // restore old cursor if we are not at the start of a word by now
    // otherwise use current one
    bRet = rUnoCursor.IsStartWordWT( nWordType );
    if (!bRet)
    {
        pPoint->Assign(rOldNode, nOldIndex);
    }
    else if (CursorType::Meta == m_eType)
    {
        bRet = lcl_ForceIntoMeta(rUnoCursor, m_xParentText,
                    META_CHECK_BOTH);
    }
    else if (m_eType == CursorType::ContentControl)
    {
        bRet = lcl_ForceIntoContentControl(rUnoCursor, m_xParentText, CONTENT_CONTROL_CHECK_BOTH);
    }

    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::isStartOfSentence()
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    // start of paragraph?
    bool bRet = rUnoCursor.GetPoint()->GetContentIndex() == 0;
    // with mark ->no sentence start
    // (check if cursor is no selection, i.e. it does not have
    // a mark or else point and mark are identical)
    if (!bRet && (!rUnoCursor.HasMark() ||
                    *rUnoCursor.GetPoint() == *rUnoCursor.GetMark()))
    {
        SwCursor aCursor(*rUnoCursor.GetPoint(),nullptr);
        SwPosition aOrigPos = *aCursor.GetPoint();
        aCursor.GoSentence(SwCursor::START_SENT );
        bRet = aOrigPos == *aCursor.GetPoint();
    }
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::isEndOfSentence()
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    // end of paragraph?
    bool bRet = rUnoCursor.GetPointContentNode() &&
        (rUnoCursor.GetPoint()->GetContentIndex() == rUnoCursor.GetPointContentNode()->Len());
    // with mark->no sentence end
    // (check if cursor is no selection, i.e. it does not have
    // a mark or else point and mark are identical)
    if (!bRet && (!rUnoCursor.HasMark() ||
                    *rUnoCursor.GetPoint() == *rUnoCursor.GetMark()))
    {
        SwCursor aCursor(*rUnoCursor.GetPoint(), nullptr);
        SwPosition aOrigPos = *aCursor.GetPoint();
        aCursor.GoSentence(SwCursor::END_SENT);
        bRet = aOrigPos == *aCursor.GetPoint();
    }
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::gotoNextSentence(sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    const bool bWasEOS = isEndOfSentence();
    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    bool bRet = rUnoCursor.GoSentence(SwCursor::NEXT_SENT);
    if (!bRet)
    {
        bRet = rUnoCursor.MovePara(GoNextPara, fnParaStart);
    }

    // if at the end of the sentence (i.e. at the space after the '.')
    // advance to next word in order for GoSentence to work properly
    // next time and have isStartOfSentence return true after this call
    if (!rUnoCursor.IsStartWordWT(css::i18n::WordType::ANYWORD_IGNOREWHITESPACES))
    {
        const bool bNextWord = rUnoCursor.GoNextWordWT(i18n::WordType::ANYWORD_IGNOREWHITESPACES);
        if (bWasEOS && !bNextWord)
        {
            bRet = false;
        }
    }
    if (CursorType::Meta == m_eType)
    {
        bRet = lcl_ForceIntoMeta(rUnoCursor, m_xParentText,
                    META_CHECK_BOTH)
            && bRet;
    }
    else if (m_eType == CursorType::ContentControl)
    {
        bRet = lcl_ForceIntoContentControl(rUnoCursor, m_xParentText, CONTENT_CONTROL_CHECK_BOTH)
               && bRet;
    }
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::gotoPreviousSentence(sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    bool bRet = rUnoCursor.GoSentence(SwCursor::PREV_SENT);
    if (!bRet)
    {
        bRet = rUnoCursor.MovePara(GoPrevPara, fnParaStart);
        if (bRet)
        {
            rUnoCursor.MovePara(GoCurrPara, fnParaEnd);
            // at the end of a paragraph move to the sentence end again
            rUnoCursor.GoSentence(SwCursor::PREV_SENT);
        }
    }
    if (CursorType::Meta == m_eType)
    {
        bRet = lcl_ForceIntoMeta(rUnoCursor, m_xParentText,
                    META_CHECK_BOTH)
            && bRet;
    }
    else if (m_eType == CursorType::ContentControl)
    {
        bRet = lcl_ForceIntoContentControl(rUnoCursor, m_xParentText, CONTENT_CONTROL_CHECK_BOTH)
               && bRet;
    }
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::gotoStartOfSentence(sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    // if we're at the para start then we won't move
    // but bRet is also true if GoSentence failed but
    // the start of the sentence is reached
    bool bRet = SwUnoCursorHelper::IsStartOfPara(rUnoCursor)
        || rUnoCursor.GoSentence(SwCursor::START_SENT)
        || SwUnoCursorHelper::IsStartOfPara(rUnoCursor);
    if (CursorType::Meta == m_eType)
    {
        bRet = lcl_ForceIntoMeta(rUnoCursor, m_xParentText,
                    META_CHECK_BOTH)
            && bRet;
    }
    else if (m_eType == CursorType::ContentControl)
    {
        bRet = lcl_ForceIntoContentControl(rUnoCursor, m_xParentText, CONTENT_CONTROL_CHECK_BOTH)
               && bRet;
    }
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::gotoEndOfSentence(sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    // bRet is true if GoSentence() succeeded or if the
    // MovePara() succeeded while the end of the para is
    // not reached already
    bool bAlreadyParaEnd = SwUnoCursorHelper::IsEndOfPara(rUnoCursor);
    bool bRet = !bAlreadyParaEnd
            &&  (rUnoCursor.GoSentence(SwCursor::END_SENT)
                 || rUnoCursor.MovePara(GoCurrPara, fnParaEnd));
    if (CursorType::Meta == m_eType)
    {
        bRet = lcl_ForceIntoMeta(rUnoCursor, m_xParentText,
                    META_CHECK_BOTH)
            && bRet;
    }
    else if (m_eType == CursorType::ContentControl)
    {
        bRet = lcl_ForceIntoContentControl(rUnoCursor, m_xParentText, CONTENT_CONTROL_CHECK_BOTH)
               && bRet;
    }
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::isStartOfParagraph()
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    const bool bRet = SwUnoCursorHelper::IsStartOfPara(rUnoCursor);
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::isEndOfParagraph()
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    const bool bRet = SwUnoCursorHelper::IsEndOfPara(rUnoCursor);
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::gotoStartOfParagraph(sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    if (CursorType::Meta == m_eType)
    {
        return false;
    }
    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    bool bRet = SwUnoCursorHelper::IsStartOfPara(rUnoCursor);
    if (!bRet)
    {
        bRet = rUnoCursor.MovePara(GoCurrPara, fnParaStart);
    }

    // since MovePara(GoCurrPara, fnParaStart) only returns false
    // if we were already at the start of the paragraph this function
    // should always complete successfully.
    OSL_ENSURE( bRet, "gotoStartOfParagraph failed" );
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::gotoEndOfParagraph(sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    if (CursorType::Meta == m_eType)
    {
        return false;
    }
    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    bool bRet = SwUnoCursorHelper::IsEndOfPara(rUnoCursor);
    if (!bRet)
    {
        bRet = rUnoCursor.MovePara(GoCurrPara, fnParaEnd);
    }

    // since MovePara(GoCurrPara, fnParaEnd) only returns false
    // if we were already at the end of the paragraph this function
    // should always complete successfully.
    OSL_ENSURE( bRet, "gotoEndOfParagraph failed" );
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::gotoNextParagraph(sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    if (CursorType::Meta == m_eType)
    {
        return false;
    }
    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    const bool bRet = rUnoCursor.MovePara(GoNextPara, fnParaStart);
    return bRet;
}

sal_Bool SAL_CALL
SwXTextCursor::gotoPreviousParagraph(sal_Bool Expand)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    if (CursorType::Meta == m_eType)
    {
        return false;
    }
    SwUnoCursorHelper::SelectPam(rUnoCursor, Expand);
    const bool bRet = rUnoCursor.MovePara(GoPrevPara, fnParaStart);
    return bRet;
}

uno::Reference< text::XText > SAL_CALL
SwXTextCursor::getText()
{
    SolarMutexGuard g;

    return m_xParentText;
}

uno::Reference< text::XTextRange > SAL_CALL
SwXTextCursor::getStart()
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    uno::Reference< text::XTextRange > xRet;
    SwPaM aPam(*rUnoCursor.Start());
    const uno::Reference< text::XText >  xParent = getText();
    if (CursorType::Meta == m_eType)
    {
        // return cursor to prevent modifying SwXTextRange for META
        rtl::Reference<SwXTextCursor> pXCursor(
            new SwXTextCursor(rUnoCursor.GetDoc(), xParent, CursorType::Meta,
                *rUnoCursor.GetPoint()) );
        pXCursor->gotoStart(false);
        xRet = static_cast<text::XWordCursor*>(pXCursor.get());
    }
    else
    {
        xRet = new SwXTextRange(aPam, xParent);
    }
    return xRet;
}

uno::Reference< text::XTextRange > SAL_CALL
SwXTextCursor::getEnd()
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    uno::Reference< text::XTextRange >  xRet;
    SwPaM aPam(*rUnoCursor.End());
    const uno::Reference< text::XText >  xParent = getText();
    if (CursorType::Meta == m_eType)
    {
        // return cursor to prevent modifying SwXTextRange for META
        rtl::Reference<SwXTextCursor> pXCursor(
            new SwXTextCursor(rUnoCursor.GetDoc(), xParent, CursorType::Meta,
                *rUnoCursor.GetPoint()) );
        pXCursor->gotoEnd(false);
        xRet = static_cast<text::XWordCursor*>(pXCursor.get());
    }
    else
    {
        xRet = new SwXTextRange(aPam, xParent);
    }
    return xRet;
}

OUString SAL_CALL SwXTextCursor::getString()
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    OUString aText;
    SwUnoCursorHelper::GetTextFromPam(rUnoCursor, aText);
    return aText;
}

void SAL_CALL
SwXTextCursor::setString(const OUString& aString)
{
    SolarMutexGuard aGuard;

    GetCursorOrThrow(); // just to check if valid

    const bool bForceExpandHints( (CursorType::Meta == m_eType)
        && dynamic_cast<SwXMeta&>(*m_xParentText)
                .CheckForOwnMemberMeta(*GetPaM(), true) );
    DeleteAndInsert(aString, bForceExpandHints ? ::sw::DeleteAndInsertMode::ForceExpandHints : ::sw::DeleteAndInsertMode::Default);
}

uno::Any SwUnoCursorHelper::GetPropertyValue(
    SwPaM& rPaM, const SfxItemPropertySet& rPropSet,
    std::u16string_view rPropertyName)
{
    uno::Any aAny;
    SfxItemPropertyMapEntry const*const pEntry =
        rPropSet.getPropertyMap().getByName(rPropertyName);

    if (!pEntry)
    {
        throw beans::UnknownPropertyException(
            OUString::Concat("Unknown property: ") + rPropertyName);
    }

    beans::PropertyState eTemp;
    const bool bDone = SwUnoCursorHelper::getCursorPropertyValue(
            *pEntry, rPaM, &aAny, eTemp );

    if (!bDone)
    {
        SfxItemSetFixed<
                RES_CHRATR_BEGIN, RES_FRMATR_END - 1,
                RES_UNKNOWNATR_CONTAINER, RES_UNKNOWNATR_CONTAINER>
            aSet(rPaM.GetDoc().GetAttrPool());

        SwUnoCursorHelper::GetCursorAttr(rPaM, aSet);

        SfxItemPropertySet::getPropertyValue(*pEntry, aSet, aAny);
    }

    return aAny;
}

void SwUnoCursorHelper::SetPropertyValue(
    SwPaM& rPaM, const SfxItemPropertySet& rPropSet,
    const OUString& rPropertyName,
    const uno::Any& rValue,
    const SetAttrMode nAttrMode)
{
    beans::PropertyValue aVal { comphelper::makePropertyValue(rPropertyName, rValue) };
    SetPropertyValues(rPaM, rPropSet, std::span<beans::PropertyValue>(&aVal, 1), nAttrMode);
}

// FN_UNO_PARA_STYLE is known to set attributes for nodes, inside
// SwUnoCursorHelper::SetTextFormatColl, instead of extending item set.
// We need to get them from nodes in next call to GetCursorAttr.
// The rest could cause similar problems in theory, so we just list them here.
static bool propertyCausesSideEffectsInNodes(sal_uInt16 nWID)
{
    return nWID == FN_UNO_PARA_STYLE ||
           nWID == FN_UNO_CHARFMT_SEQUENCE ||
           nWID == FN_UNO_NUM_START_VALUE ||
           nWID == FN_UNO_NUM_RULES;
}

void SwUnoCursorHelper::SetPropertyValues(
    SwPaM& rPaM, const SfxItemPropertySet& rPropSet,
    const uno::Sequence< beans::PropertyValue > &rPropertyValues,
    const SetAttrMode nAttrMode)
{
    SetPropertyValues(rPaM, rPropSet,
        std::span<const beans::PropertyValue>(rPropertyValues.getConstArray(), rPropertyValues.getLength()),
        nAttrMode);
}

void SwUnoCursorHelper::SetPropertyValues(
    SwPaM& rPaM, const SfxItemPropertySet& rPropSet,
    std::span< const beans::PropertyValue > aPropertyValues,
    const SetAttrMode nAttrMode)
{
    if (aPropertyValues.empty())
        return;

    SwDoc& rDoc = rPaM.GetDoc();
    OUString aUnknownExMsg, aPropertyVetoExMsg;

    // Build set of attributes we want to fetch
    WhichRangesContainer aRanges;
    std::vector<std::pair<const SfxItemPropertyMapEntry*, const uno::Any&>> aSideEffectsEntries;
    std::vector<std::pair<const SfxItemPropertyMapEntry*, const uno::Any&>> aEntries;
    aEntries.reserve(aPropertyValues.size());
    for (const auto& rPropVal : aPropertyValues)
    {
        const OUString &rPropertyName = rPropVal.Name;

        SfxItemPropertyMapEntry const* pEntry =
            rPropSet.getPropertyMap().getByName(rPropertyName);

        // Queue up any exceptions until the end ...
        if (!pEntry)
        {
            aUnknownExMsg += "Unknown property: '" + rPropertyName + "' ";
            continue;
        }
        else if (pEntry->nFlags & beans::PropertyAttribute::READONLY)
        {
            aPropertyVetoExMsg += "Property is read-only: '" + rPropertyName + "' ";
            continue;
        }
        if (propertyCausesSideEffectsInNodes(pEntry->nWID))
        {
            aSideEffectsEntries.emplace_back(pEntry, rPropVal.Value);
        }
        else
        {
            aRanges = aRanges.MergeRange(pEntry->nWID, pEntry->nWID);
            aEntries.emplace_back(pEntry, rPropVal.Value);
        }
    }

    // Entries with side effects first, using dedicated one-element SfxItemSet for each
    for (const auto& [pEntry, rValue] : aSideEffectsEntries)
    {
        SfxItemSet aItemSet(rDoc.GetAttrPool(), pEntry->nWID, pEntry->nWID);
        // we need to get up-to-date item set from nodes
        SwUnoCursorHelper::GetCursorAttr(rPaM, aItemSet);
        // this can set some attributes in nodes' mpAttrSet
        if (!SwUnoCursorHelper::SetCursorPropertyValue(*pEntry, rValue, rPaM, aItemSet))
            SfxItemPropertySet::setPropertyValue(*pEntry, rValue, aItemSet);
        SwUnoCursorHelper::SetCursorAttr(rPaM, aItemSet, nAttrMode, false /*bTableMode*/);
    }

    if (!aEntries.empty())
    {
        // Fetch, overwrite, and re-set the attributes from the core
        SfxItemSet aItemSet(rDoc.GetAttrPool(), std::move(aRanges));
        // we need to get up-to-date item set from nodes
        SwUnoCursorHelper::GetCursorAttr(rPaM, aItemSet);

        for (const auto& [pEntry, rValue] : aEntries)
        {
            // this can set some attributes in nodes' mpAttrSet
            if (!SwUnoCursorHelper::SetCursorPropertyValue(*pEntry, rValue, rPaM, aItemSet))
                SfxItemPropertySet::setPropertyValue(*pEntry, rValue, aItemSet);
        }

        SwUnoCursorHelper::SetCursorAttr(rPaM, aItemSet, nAttrMode, false /*bTableMode*/);
    }

    if (!aUnknownExMsg.isEmpty())
        throw beans::UnknownPropertyException(aUnknownExMsg);
    if (!aPropertyVetoExMsg.isEmpty())
        throw beans::PropertyVetoException(aPropertyVetoExMsg);
}

namespace
{
    bool NotInRange(sal_uInt16 nWID, sal_uInt16 nStart, sal_uInt16 nEnd)
    {
        return nWID < nStart || nWID > nEnd;
    }
}

uno::Sequence< beans::PropertyState >
SwUnoCursorHelper::GetPropertyStates(
            SwPaM& rPaM, const SfxItemPropertySet& rPropSet,
            const uno::Sequence< OUString >& rPropertyNames,
            const SwGetPropertyStatesCaller eCaller)
{
    const OUString* pNames = rPropertyNames.getConstArray();
    uno::Sequence< beans::PropertyState > aRet(rPropertyNames.getLength());
    beans::PropertyState* pStates = aRet.getArray();
    const SfxItemPropertyMap &rMap = rPropSet.getPropertyMap();
    std::optional<SfxItemSet> oSet;
    std::optional<SfxItemSet> oSetParent;

    for (sal_Int32 i = 0, nEnd = rPropertyNames.getLength(); i < nEnd; i++)
    {
        SfxItemPropertyMapEntry const*const pEntry =
                rMap.getByName( pNames[i] );
        if(!pEntry)
        {
            if (pNames[i] == UNO_NAME_IS_SKIP_HIDDEN_TEXT ||
                pNames[i] == UNO_NAME_IS_SKIP_PROTECTED_TEXT ||
                pNames[i] == UNO_NAME_NO_FORMAT_ATTR)
            {
                pStates[i] = beans::PropertyState_DEFAULT_VALUE;
                continue;
            }
            else if (SW_PROPERTY_STATE_CALLER_SWX_TEXT_PORTION_TOLERANT ==
                        eCaller)
            {
                //this values marks the element as unknown property
                pStates[i] = beans::PropertyState::PropertyState_MAKE_FIXED_SIZE;
                continue;
            }
            else
            {
                throw beans::UnknownPropertyException(
                    "Unknown property: " + pNames[i]);
            }
        }
        if (((SW_PROPERTY_STATE_CALLER_SWX_TEXT_PORTION == eCaller)  ||
             (SW_PROPERTY_STATE_CALLER_SWX_TEXT_PORTION_TOLERANT == eCaller)) &&
            NotInRange(pEntry->nWID, FN_UNO_RANGE_BEGIN, FN_UNO_RANGE_END) &&
            NotInRange(pEntry->nWID, RES_CHRATR_BEGIN, RES_TXTATR_END) )
        {
            pStates[i] = beans::PropertyState_DEFAULT_VALUE;
        }
        else
        {
            if ( pEntry->nWID >= FN_UNO_RANGE_BEGIN &&
                 pEntry->nWID <= FN_UNO_RANGE_END )
            {
                (void)SwUnoCursorHelper::getCursorPropertyValue(
                    *pEntry, rPaM, nullptr, pStates[i] );
            }
            else
            {
                if (!oSet)
                {
                    switch ( eCaller )
                    {
                        case SW_PROPERTY_STATE_CALLER_SWX_TEXT_PORTION_TOLERANT:
                        case SW_PROPERTY_STATE_CALLER_SWX_TEXT_PORTION:
                            oSet.emplace( rPaM.GetDoc().GetAttrPool(),
                                    svl::Items<RES_CHRATR_BEGIN,   RES_TXTATR_END> );
                        break;
                        case SW_PROPERTY_STATE_CALLER_SINGLE_VALUE_ONLY:
                            oSet.emplace( rPaM.GetDoc().GetAttrPool(),
                                    pEntry->nWID, pEntry->nWID );
                        break;
                        default:
                            oSet.emplace(
                                rPaM.GetDoc().GetAttrPool(),
                                svl::Items<
                                    RES_CHRATR_BEGIN, RES_FRMATR_END - 1,
                                    RES_UNKNOWNATR_CONTAINER,
                                        RES_UNKNOWNATR_CONTAINER>);
                    }
                    // #i63870#
                    SwUnoCursorHelper::GetCursorAttr( rPaM, *oSet );
                }

                pStates[i] = ( oSet->Count() )
                    ? SfxItemPropertySet::getPropertyState( *pEntry, *oSet )
                    : beans::PropertyState_DEFAULT_VALUE;

                //try again to find out if a value has been inherited
                if( beans::PropertyState_DIRECT_VALUE == pStates[i] )
                {
                    if (!oSetParent)
                    {
                        oSetParent.emplace(oSet->CloneAsValue( false ));
                        // #i63870#
                        SwUnoCursorHelper::GetCursorAttr(
                                rPaM, *oSetParent, true, false );
                    }

                    pStates[i] = ( oSetParent->Count() )
                        ? SfxItemPropertySet::getPropertyState( *pEntry, *oSetParent )
                        : beans::PropertyState_DEFAULT_VALUE;
                }
            }
        }
    }
    return aRet;
}

beans::PropertyState SwUnoCursorHelper::GetPropertyState(
    SwPaM& rPaM, const SfxItemPropertySet& rPropSet,
    const OUString& rPropertyName)
{
    uno::Sequence< OUString > aStrings { rPropertyName };
    uno::Sequence< beans::PropertyState > aSeq =
        GetPropertyStates(rPaM, rPropSet, aStrings,
                SW_PROPERTY_STATE_CALLER_SINGLE_VALUE_ONLY );
    return aSeq[0];
}

static void
lcl_SelectParaAndReset( SwPaM &rPaM, SwDoc & rDoc,
                        o3tl::sorted_vector<sal_uInt16> const &rWhichIds )
{
    // if we are resetting paragraph attributes, we need to select the full paragraph first
    SwPosition aStart = *rPaM.Start();
    SwPosition aEnd = *rPaM.End();
    auto pTemp ( rDoc.CreateUnoCursor(aStart) );
    if(!SwUnoCursorHelper::IsStartOfPara(*pTemp))
    {
        pTemp->MovePara(GoCurrPara, fnParaStart);
    }
    pTemp->SetMark();
    *pTemp->GetPoint() = std::move(aEnd);
    SwUnoCursorHelper::SelectPam(*pTemp, true);
    if(!SwUnoCursorHelper::IsEndOfPara(*pTemp))
    {
        pTemp->MovePara(GoCurrPara, fnParaEnd);
    }
    rDoc.ResetAttrs(*pTemp, true, rWhichIds);
}

void SwUnoCursorHelper::SetPropertyToDefault(
    SwPaM& rPaM, const SfxItemPropertySet& rPropSet,
    std::u16string_view rPropertyName)
{
    SwDoc& rDoc = rPaM.GetDoc();
    SfxItemPropertyMapEntry const*const pEntry =
        rPropSet.getPropertyMap().getByName(rPropertyName);
    if (!pEntry)
    {
        throw beans::UnknownPropertyException(
            OUString::Concat("Unknown property: ") + rPropertyName);
    }

    if (pEntry->nFlags & beans::PropertyAttribute::READONLY)
    {
        throw uno::RuntimeException(
                OUString::Concat("setPropertyToDefault: property is read-only: ")
                + rPropertyName, nullptr);
    }

    if (pEntry->nWID < RES_FRMATR_END)
    {
        const o3tl::sorted_vector<sal_uInt16> aWhichIds{ pEntry->nWID };
        if (pEntry->nWID < RES_PARATR_BEGIN)
        {
            rDoc.ResetAttrs(rPaM, true, aWhichIds);
        }
        else
        {
            lcl_SelectParaAndReset ( rPaM, rDoc, aWhichIds );
        }
    }
    else
    {
        SwUnoCursorHelper::resetCursorPropertyValue(*pEntry, rPaM);
    }
}

uno::Any SwUnoCursorHelper::GetPropertyDefault(
    SwPaM const & rPaM, const SfxItemPropertySet& rPropSet,
    std::u16string_view rPropertyName)
{
    SfxItemPropertyMapEntry const*const pEntry =
        rPropSet.getPropertyMap().getByName(rPropertyName);
    if (!pEntry)
    {
        throw beans::UnknownPropertyException(
            OUString::Concat("Unknown property: ") + rPropertyName);
    }

    uno::Any aRet;
    if (pEntry->nWID < RES_FRMATR_END)
    {
        SwDoc& rDoc = rPaM.GetDoc();
        const SfxPoolItem& rDefItem =
            rDoc.GetAttrPool().GetUserOrPoolDefaultItem(pEntry->nWID);
        rDefItem.QueryValue(aRet, pEntry->nMemberId);
    }
    return aRet;
}

uno::Reference< beans::XPropertySetInfo > SAL_CALL
SwXTextCursor::getPropertySetInfo()
{
    SolarMutexGuard g;

    static uno::Reference< beans::XPropertySetInfo >  xRef = [&]()
    {
        static SfxItemPropertyMapEntry const aCursorExtMap_Impl[] =
        {
            { UNO_NAME_IS_SKIP_HIDDEN_TEXT, FN_SKIP_HIDDEN_TEXT, cppu::UnoType<bool>::get(), PROPERTY_NONE,     0},
            { UNO_NAME_IS_SKIP_PROTECTED_TEXT, FN_SKIP_PROTECTED_TEXT, cppu::UnoType<bool>::get(), PROPERTY_NONE,     0},
            { UNO_NAME_NO_FORMAT_ATTR, 0, cppu::UnoType<bool>::get(), PROPERTY_NONE,     0},
        };
        const rtl::Reference< SfxItemPropertySetInfo >  xInfo =
            m_rPropSet.getPropertySetInfo();
        // extend PropertySetInfo!
        const uno::Sequence<beans::Property> aPropSeq = xInfo->getProperties();
        return rtl::Reference<SfxExtItemPropertySetInfo>(new SfxExtItemPropertySetInfo(
            aCursorExtMap_Impl,
            aPropSeq ));
    }();
    return xRef;
}

void SAL_CALL
SwXTextCursor::setPropertyValue(
        const OUString& rPropertyName, const uno::Any& rValue)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    if (rPropertyName == UNO_NAME_IS_SKIP_HIDDEN_TEXT)
    {
        bool bSet(false);
        if (!(rValue >>= bSet))
        {
            throw lang::IllegalArgumentException();
        }
        rUnoCursor.SetSkipOverHiddenSections(bSet);
    }
    else if (rPropertyName == UNO_NAME_IS_SKIP_PROTECTED_TEXT)
    {
        bool bSet(false);
        if (!(rValue >>= bSet))
        {
            throw lang::IllegalArgumentException();
        }
        rUnoCursor.SetSkipOverProtectSections(bSet);
    }
    else if (rPropertyName == UNO_NAME_NO_FORMAT_ATTR)
    {
        bool bSet(false);
        if (!(rValue >>= bSet))
        {
            throw lang::IllegalArgumentException();
        }
        if (bSet)
        {
            m_nAttrMode = SetAttrMode::NOFORMATATTR;
        }
        else
        {
            m_nAttrMode = SetAttrMode::DEFAULT;
        }
    }
    else if (rPropertyName == "ParaAutoStyleDef")
    {
        // Create an autostyle from passed definition (sequence of PropertyValue, same
        // as in XAutoStyleFamily::insertStyle), using the currently applied properties
        // from the paragraph to not lose their values when creating complex properties
        // like SvxULSpaceItem, when only part of the properties stored there is passed;
        // and apply it to the paragraph.
        uno::Sequence<beans::PropertyValue> def;
        if (!(rValue >>= def))
            throw lang::IllegalArgumentException();

        // See SwUnoCursorHelper::SetPropertyValues

        auto pPropSet = aSwMapProvider.GetPropertySet(PROPERTY_MAP_PARA_AUTO_STYLE);

        // Build set of attributes we want to fetch
        WhichRangesContainer aRanges;
        for (auto& rPropVal : def)
        {
            SfxItemPropertyMapEntry const* pEntry =
                pPropSet->getPropertyMap().getByName(rPropVal.Name);
            if (!pEntry)
                continue; // PropValuesToAutoStyleItemSet ignores invalid names

            aRanges = aRanges.MergeRange(pEntry->nWID, pEntry->nWID);
        }

        if (!aRanges.empty())
        {
            SwAttrSet aAutoStyleItemSet(rUnoCursor.GetDoc().GetAttrPool(), std::move(aRanges));
            // we need to get up-to-date item set: this makes sure that the complex properties,
            // that are only partially defined by passed definition, do not lose the rest of
            // their already present data (which will become part of the autostyle, too).
            SwUnoCursorHelper::GetCursorAttr(rUnoCursor, aAutoStyleItemSet);
            // Set normal set ranges before putting into autostyle, to the same ranges
            // that are used for paragraph autostyle in SwXAutoStyleFamily::insertStyle
            aAutoStyleItemSet.SetRanges(aTextNodeSetRange);

            // Fill the prepared item set, containing current paragraph property values,
            // with the passed definition, and create the autostyle.
            auto pStyle = PropValuesToAutoStyleItemSet(
                rUnoCursor.GetDoc(), IStyleAccess::AUTO_STYLE_PARA, def, aAutoStyleItemSet);

            SwFormatAutoFormat aFormat(RES_AUTO_STYLE);
            aFormat.SetStyleHandle(pStyle);
            SfxItemSet rSet(rUnoCursor.GetDoc().GetAttrPool(), RES_AUTO_STYLE, RES_AUTO_STYLE);
            rSet.Put(aFormat);
            SwUnoCursorHelper::SetCursorAttr(rUnoCursor, rSet, m_nAttrMode);
        }
    }
    else
    {
        SwUnoCursorHelper::SetPropertyValue(rUnoCursor,
                m_rPropSet, rPropertyName, rValue, m_nAttrMode);
    }
}

uno::Any SAL_CALL
SwXTextCursor::getPropertyValue(const OUString& rPropertyName)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    uno::Any aAny;
    if (rPropertyName == UNO_NAME_IS_SKIP_HIDDEN_TEXT)
    {
        const bool bSet = rUnoCursor.IsSkipOverHiddenSections();
        aAny <<= bSet;
    }
    else if (rPropertyName == UNO_NAME_IS_SKIP_PROTECTED_TEXT)
    {
        const bool bSet = rUnoCursor.IsSkipOverProtectSections();
        aAny <<= bSet;
    }
    else
    {
        aAny = SwUnoCursorHelper::GetPropertyValue(rUnoCursor,
                m_rPropSet, rPropertyName);
    }
    return aAny;
}

void SAL_CALL
SwXTextCursor::addPropertyChangeListener(
        const OUString& /*rPropertyName*/,
        const uno::Reference< beans::XPropertyChangeListener >& /*xListener*/)
{
    OSL_FAIL("SwXTextCursor::addPropertyChangeListener(): not implemented");
}

void SAL_CALL
SwXTextCursor::removePropertyChangeListener(
        const OUString& /*rPropertyName*/,
        const uno::Reference< beans::XPropertyChangeListener >& /*xListener*/)
{
    OSL_FAIL("SwXTextCursor::removePropertyChangeListener(): not implemented");
}

void SAL_CALL
SwXTextCursor::addVetoableChangeListener(
        const OUString& /*rPropertyName*/,
        const uno::Reference< beans::XVetoableChangeListener >& /*xListener*/)
{
    OSL_FAIL("SwXTextCursor::addVetoableChangeListener(): not implemented");
}

void SAL_CALL
SwXTextCursor::removeVetoableChangeListener(
        const OUString& /*rPropertyName*/,
        const uno::Reference< beans::XVetoableChangeListener >& /*xListener*/)
{
    OSL_FAIL("SwXTextCursor::removeVetoableChangeListener(): not implemented");
}

beans::PropertyState SAL_CALL
SwXTextCursor::getPropertyState(const OUString& rPropertyName)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    const beans::PropertyState eRet = SwUnoCursorHelper::GetPropertyState(
            rUnoCursor, m_rPropSet, rPropertyName);
    return eRet;
}

uno::Sequence< beans::PropertyState > SAL_CALL
SwXTextCursor::getPropertyStates(
        const uno::Sequence< OUString >& rPropertyNames)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    return SwUnoCursorHelper::GetPropertyStates(
            rUnoCursor, m_rPropSet, rPropertyNames);
}

void SAL_CALL
SwXTextCursor::setPropertyToDefault(const OUString& rPropertyName)
{
    // forward: need no solar mutex here
    uno::Sequence < OUString > aSequence ( &rPropertyName, 1 );
    setPropertiesToDefault ( aSequence );
}

uno::Any SAL_CALL
SwXTextCursor::getPropertyDefault(const OUString& rPropertyName)
{
    // forward: need no solar mutex here
    const uno::Sequence < OUString > aSequence ( &rPropertyName, 1 );
    return getPropertyDefaults ( aSequence ).getConstArray()[0];
}

void SAL_CALL SwXTextCursor::setPropertyValues(
    const uno::Sequence< OUString >& aPropertyNames,
    const uno::Sequence< uno::Any >& aValues )
{
    if( aValues.getLength() != aPropertyNames.getLength() )
    {
        OSL_FAIL( "mis-matched property value sequences" );
        throw lang::IllegalArgumentException();
    }

    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    // a little lame to have to copy into this.
    uno::Sequence< beans::PropertyValue > aPropertyValues( aValues.getLength() );
    auto aPropertyValuesRange = asNonConstRange(aPropertyValues);
    for ( sal_Int32 i = 0; i < aPropertyNames.getLength(); i++ )
    {
        if ( aPropertyNames[ i ] == UNO_NAME_IS_SKIP_HIDDEN_TEXT ||
             aPropertyNames[ i ] == UNO_NAME_IS_SKIP_PROTECTED_TEXT )
        {
            // the behaviour of these is hard to model in a group
            OSL_FAIL("invalid property name for batch setting");
            throw lang::IllegalArgumentException();
        }
        aPropertyValuesRange[ i ].Name = aPropertyNames[ i ];
        aPropertyValuesRange[ i ].Value = aValues[ i ];
    }
    try
    {
        SwUnoCursorHelper::SetPropertyValues( rUnoCursor, m_rPropSet, aPropertyValues );
    }
    catch (const css::beans::UnknownPropertyException& e)
    {
        uno::Any a(cppu::getCaughtException());
        throw lang::WrappedTargetException(
            "wrapped Exception " + e.Message,
            uno::Reference<uno::XInterface>(), a);
    }
}

uno::Sequence< uno::Any > SAL_CALL
SwXTextCursor::getPropertyValues( const uno::Sequence< OUString >& aPropertyNames )
{
    // a banal implementation for now
    uno::Sequence< uno::Any > aValues( aPropertyNames.getLength() );
    std::transform(aPropertyNames.begin(), aPropertyNames.end(), aValues.getArray(),
        [this](const OUString& rName) -> uno::Any { return getPropertyValue( rName ); });
    return aValues;
}

void SAL_CALL SwXTextCursor::addPropertiesChangeListener(
        const uno::Sequence< OUString >& /* aPropertyNames */,
        const uno::Reference< css::beans::XPropertiesChangeListener >& /* xListener */ )
{
    OSL_FAIL("SwXTextCursor::addPropertiesChangeListener(): not implemented");
}
void SAL_CALL SwXTextCursor::removePropertiesChangeListener(
        const uno::Reference< css::beans::XPropertiesChangeListener >& /* xListener */ )
{
    OSL_FAIL("SwXTextCursor::removePropertiesChangeListener(): not implemented");
}

void SAL_CALL SwXTextCursor::firePropertiesChangeEvent(
        const uno::Sequence< OUString >& /* aPropertyNames */,
        const uno::Reference< css::beans::XPropertiesChangeListener >& /* xListener */ )
{
    OSL_FAIL("SwXTextCursor::firePropertiesChangeEvent(): not implemented");
}

// para specific attribute ranges
const sal_uInt16 g_ParaResetableSetRange[] = {
    RES_FRMATR_BEGIN, RES_FRMATR_END-1,
    RES_PARATR_BEGIN, RES_PARATR_END-1,
    RES_PARATR_LIST_BEGIN, RES_PARATR_LIST_END-1,
    RES_UNKNOWNATR_BEGIN, RES_UNKNOWNATR_END-1,
    0
};

// selection specific attribute ranges
const sal_uInt16 g_ResetableSetRange[] = {
    RES_CHRATR_BEGIN, RES_CHRATR_END-1,
    RES_TXTATR_INETFMT, RES_TXTATR_INETFMT,
    RES_TXTATR_CHARFMT, RES_TXTATR_CHARFMT,
    RES_TXTATR_CJK_RUBY, RES_TXTATR_CJK_RUBY,
    RES_TXTATR_UNKNOWN_CONTAINER, RES_TXTATR_UNKNOWN_CONTAINER,
    0
};

static void
lcl_EnumerateIds(sal_uInt16 const* pIdRange, o3tl::sorted_vector<sal_uInt16> &rWhichIds)
{
    while (*pIdRange)
    {
        const sal_uInt16 nStart = *pIdRange++;
        const sal_uInt16 nEnd   = *pIdRange++;
        for (sal_uInt16 nId = nStart + 1;  nId <= nEnd;  ++nId)
        {
            rWhichIds.insert( nId );
        }
    }
}

void SAL_CALL
SwXTextCursor::setAllPropertiesToDefault()
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    o3tl::sorted_vector<sal_uInt16> aParaWhichIds;
    o3tl::sorted_vector<sal_uInt16> aWhichIds;
    lcl_EnumerateIds(g_ParaResetableSetRange, aParaWhichIds);
    lcl_EnumerateIds(g_ResetableSetRange, aWhichIds);
    if (!aParaWhichIds.empty())
    {
        lcl_SelectParaAndReset(rUnoCursor, rUnoCursor.GetDoc(),
            aParaWhichIds);
    }
    if (!aWhichIds.empty())
    {
        rUnoCursor.GetDoc().ResetAttrs(rUnoCursor, true, aWhichIds);
    }
}

void SAL_CALL
SwXTextCursor::setPropertiesToDefault(
        const uno::Sequence< OUString >& rPropertyNames)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    if ( !rPropertyNames.hasElements() )
        return;

    SwDoc& rDoc = rUnoCursor.GetDoc();
    o3tl::sorted_vector<sal_uInt16> aWhichIds;
    o3tl::sorted_vector<sal_uInt16> aParaWhichIds;
    for (const OUString& rName : rPropertyNames)
    {
        SfxItemPropertyMapEntry const*const  pEntry =
            m_rPropSet.getPropertyMap().getByName( rName );
        if (!pEntry)
        {
            if (rName == UNO_NAME_IS_SKIP_HIDDEN_TEXT ||
                rName == UNO_NAME_IS_SKIP_PROTECTED_TEXT)
            {
                continue;
            }
            throw beans::UnknownPropertyException(
                "Unknown property: " + rName,
                getXWeak());
        }
        if (pEntry->nFlags & beans::PropertyAttribute::READONLY)
        {
            throw uno::RuntimeException(
                "setPropertiesToDefault: property is read-only: " + rName,
                getXWeak());
        }

        if (pEntry->nWID < RES_FRMATR_END)
        {
            if (pEntry->nWID < RES_PARATR_BEGIN)
            {
                aWhichIds.insert( pEntry->nWID );
            }
            else
            {
                aParaWhichIds.insert( pEntry->nWID );
            }
        }
        else if (pEntry->nWID == FN_UNO_NUM_START_VALUE)
        {
            SwUnoCursorHelper::resetCursorPropertyValue(*pEntry, rUnoCursor);
        }
    }

    if (!aParaWhichIds.empty())
    {
        lcl_SelectParaAndReset(rUnoCursor, rDoc, aParaWhichIds);
    }
    if (!aWhichIds.empty())
    {
        rDoc.ResetAttrs(rUnoCursor, true, aWhichIds);
    }
}

uno::Sequence< uno::Any > SAL_CALL
SwXTextCursor::getPropertyDefaults(
        const uno::Sequence< OUString >& rPropertyNames)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    const sal_Int32 nCount = rPropertyNames.getLength();
    uno::Sequence< uno::Any > aRet(nCount);
    if ( nCount )
    {
        SwDoc& rDoc = rUnoCursor.GetDoc();
        const OUString *pNames = rPropertyNames.getConstArray();
        uno::Any *pAny = aRet.getArray();
        for (sal_Int32 i = 0; i < nCount; i++)
        {
            SfxItemPropertyMapEntry const*const pEntry =
                m_rPropSet.getPropertyMap().getByName( pNames[i] );
            if (!pEntry)
            {
                if (pNames[i] == UNO_NAME_IS_SKIP_HIDDEN_TEXT ||
                    pNames[i] == UNO_NAME_IS_SKIP_PROTECTED_TEXT ||
                    pNames[i] == UNO_NAME_NO_FORMAT_ATTR)
                {
                    continue;
                }
                throw beans::UnknownPropertyException(
                    "Unknown property: " + pNames[i]);
            }
            if (pEntry->nWID < RES_FRMATR_END)
            {
                const SfxPoolItem& rDefItem =
                    rDoc.GetAttrPool().GetUserOrPoolDefaultItem(pEntry->nWID);
                rDefItem.QueryValue(pAny[i], pEntry->nMemberId);
            }
        }
    }
    return aRet;
}

void SAL_CALL SwXTextCursor::invalidateMarkings(::sal_Int32 nType)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    SwNode& node = rUnoCursor.GetPointNode();

    SwTextNode* txtNode = node.GetTextNode();

    if (txtNode == nullptr) return;

    if ( text::TextMarkupType::SPELLCHECK == nType )
    {
        txtNode->SetWrongDirty(sw::WrongState::TODO);
        txtNode->ClearWrong();
    }
    else if( text::TextMarkupType::PROOFREADING == nType )
    {
        txtNode->SetGrammarCheckDirty(true);
        txtNode->ClearGrammarCheck();
    }
    else if ( text::TextMarkupType::SMARTTAG == nType )
    {
        txtNode->SetSmartTagDirty(true);
        txtNode->ClearSmartTags();
    }
    else return;

    SwFormatColl* fmtColl=txtNode->GetFormatColl();

    if (fmtColl == nullptr) return;

    txtNode->CallSwClientNotify(SwFormatChangeHint(nullptr, fmtColl));
}

void SAL_CALL
SwXTextCursor::makeRedline(
    const OUString& rRedlineType,
    const uno::Sequence< beans::PropertyValue >& rRedlineProperties)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    SwUnoCursorHelper::makeRedline(rUnoCursor, rRedlineType, rRedlineProperties);
}

void SAL_CALL SwXTextCursor::insertDocumentFromURL(const OUString& rURL,
    const uno::Sequence< beans::PropertyValue >& rOptions)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    SwUnoCursorHelper::InsertFile(&rUnoCursor, rURL, rOptions);
}

uno::Sequence< beans::PropertyValue >
SwUnoCursorHelper::CreateSortDescriptor(const bool bFromTable)
{
    uno::Sequence< beans::PropertyValue > aRet(5);
    beans::PropertyValue* pArray = aRet.getArray();

    uno::Any aVal;
    aVal <<= bFromTable;
    pArray[0] = beans::PropertyValue(u"IsSortInTable"_ustr, -1, aVal,
                    beans::PropertyState_DIRECT_VALUE);

    aVal <<= u' ';
    pArray[1] = beans::PropertyValue(u"Delimiter"_ustr, -1, aVal,
                    beans::PropertyState_DIRECT_VALUE);

    aVal <<= false;
    pArray[2] = beans::PropertyValue(u"IsSortColumns"_ustr, -1, aVal,
                    beans::PropertyState_DIRECT_VALUE);

    aVal <<= sal_Int32(3);
    pArray[3] = beans::PropertyValue(u"MaxSortFieldsCount"_ustr, -1, aVal,
                    beans::PropertyState_DIRECT_VALUE);

    lang::Locale aLang( SvtSysLocale().GetLanguageTag().getLocale());
    // get collator algorithm to be used for the locale
    uno::Sequence< OUString > aSeq(
            GetAppCollator().listCollatorAlgorithms( aLang ) );
    const bool bHasElements = aSeq.hasElements();
    OSL_ENSURE( bHasElements, "list of collator algorithms is empty!");
    OUString aCollAlg;
    if (bHasElements)
    {
        aCollAlg = aSeq.getConstArray()[0];
    }

    uno::Sequence< table::TableSortField > aFields
    {
        // Field, IsAscending, IsCaseSensitive, FieldType, CollatorLocale, CollatorAlgorithm
        { 1, true, false, table::TableSortFieldType_ALPHANUMERIC, aLang, aCollAlg },
        { 1, true, false, table::TableSortFieldType_ALPHANUMERIC, aLang, aCollAlg },
        { 1, true, false, table::TableSortFieldType_ALPHANUMERIC, aLang, aCollAlg }
    };

    aVal <<= aFields;
    pArray[4] = beans::PropertyValue(u"SortFields"_ustr, -1, aVal,
                    beans::PropertyState_DIRECT_VALUE);

    return aRet;
}

uno::Sequence< beans::PropertyValue > SAL_CALL
SwXTextCursor::createSortDescriptor()
{
    SolarMutexGuard aGuard;

    return SwUnoCursorHelper::CreateSortDescriptor(false);
}

bool SwUnoCursorHelper::ConvertSortProperties(
    const uno::Sequence< beans::PropertyValue >& rDescriptor,
    SwSortOptions& rSortOpt)
{
    bool bRet = true;

    rSortOpt.bTable = false;
    rSortOpt.cDeli = ' ';
    rSortOpt.eDirection = SwSortDirection::Columns;  //!! UI text may be contrary though !!

    SwSortKey aKey1;
    aKey1.nColumnId = USHRT_MAX;
    aKey1.bIsNumeric = true;
    aKey1.eSortOrder = SwSortOrder::Ascending;

    SwSortKey aKey2;
    aKey2.nColumnId = USHRT_MAX;
    aKey2.bIsNumeric = true;
    aKey2.eSortOrder = SwSortOrder::Ascending;

    SwSortKey aKey3;
    aKey3.nColumnId = USHRT_MAX;
    aKey3.bIsNumeric = true;
    aKey3.eSortOrder = SwSortOrder::Ascending;
    SwSortKey* aKeys[3] = {&aKey1, &aKey2, &aKey3};

    bool bOldSortdescriptor(false);
    bool bNewSortdescriptor(false);

    for (const beans::PropertyValue& rProperty : rDescriptor)
    {
        uno::Any aValue( rProperty.Value );
        const OUString& rPropName = rProperty.Name;

        // old and new sortdescriptor
        if ( rPropName == "IsSortInTable" )
        {
            if (auto b = o3tl::tryAccess<bool>(aValue))
            {
                rSortOpt.bTable = *b;
            }
            else
            {
                bRet = false;
            }
        }
        else if ( rPropName == "Delimiter" )
        {
            sal_Unicode uChar;
            sal_uInt16 nChar;
            if (aValue >>= uChar)
            {
                rSortOpt.cDeli = uChar;
            }
            else if (aValue >>= nChar)
            {
                // For compatibility with BASIC, also accept an ANY containing
                // an UNSIGNED SHORT:
                rSortOpt.cDeli = nChar;
            }
            else
            {
                bRet = false;
            }
        }
        // old sortdescriptor
        else if ( rPropName == "SortColumns" )
        {
            bOldSortdescriptor = true;
            bool bTemp(false);
            if (aValue >>= bTemp)
            {
                rSortOpt.eDirection = bTemp ? SwSortDirection::Columns : SwSortDirection::Rows;
            }
            else
            {
                bRet = false;
            }
        }
        else if ( rPropName == "IsCaseSensitive" )
        {
            bOldSortdescriptor = true;
            bool bTemp(false);
            if (aValue >>= bTemp)
            {
                rSortOpt.bIgnoreCase = !bTemp;
            }
            else
            {
                bRet = false;
            }
        }
        else if ( rPropName == "CollatorLocale" )
        {
            bOldSortdescriptor = true;
            lang::Locale aLocale;
            if (aValue >>= aLocale)
            {
                rSortOpt.nLanguage = LanguageTag::convertToLanguageType( aLocale);
            }
            else
            {
                bRet = false;
            }
        }
        else if (rPropName.startsWith("CollatorAlgorithm") &&
            rPropName.getLength() == 18 &&
            (rPropName[17] >= '0' && rPropName[17] <= '9'))
        {
            bOldSortdescriptor = true;
            sal_uInt16 nIndex = rPropName[17];
            nIndex -= '0';
            OUString aText;
            if ((aValue >>= aText) && nIndex < 3)
            {
                aKeys[nIndex]->sSortType = aText;
            }
            else
            {
                bRet = false;
            }
        }
        else if (rPropName.startsWith("SortRowOrColumnNo") &&
            rPropName.getLength() == 18 &&
            (rPropName[17] >= '0' && rPropName[17] <= '9'))
        {
            bOldSortdescriptor = true;
            sal_uInt16 nIndex = rPropName[17];
            nIndex -= '0';
            sal_Int16 nCol = -1;
            if (aValue.getValueType() == ::cppu::UnoType<sal_Int16>::get()
                && nIndex < 3)
            {
                aValue >>= nCol;
            }
            if (nCol >= 0)
            {
                aKeys[nIndex]->nColumnId = nCol;
            }
            else
            {
                bRet = false;
            }
        }
        else if (rPropName.startsWith("IsSortNumeric") &&
            rPropName.getLength() == 14 &&
            (rPropName[13] >= '0' && rPropName[13] <= '9'))
        {
            bOldSortdescriptor = true;
            sal_uInt16 nIndex = rPropName[13];
            nIndex = nIndex - '0';
            std::optional<const bool> bTemp = o3tl::tryAccess<bool>(aValue);
            if (bTemp.has_value() && nIndex < 3)
            {
                aKeys[nIndex]->bIsNumeric = *bTemp;
            }
            else
            {
                bRet = false;
            }
        }
        else if (rPropName.startsWith("IsSortAscending") &&
            rPropName.getLength() == 16 &&
            (rPropName[15] >= '0' && rPropName[15] <= '9'))
        {
            bOldSortdescriptor = true;
            sal_uInt16 nIndex = rPropName[15];
            nIndex -= '0';
            std::optional<const bool> bTemp = o3tl::tryAccess<bool>(aValue);
            if (bTemp.has_value() && nIndex < 3)
            {
                aKeys[nIndex]->eSortOrder = (*bTemp)
                    ? SwSortOrder::Ascending : SwSortOrder::Descending;
            }
            else
            {
                bRet = false;
            }
        }
        // new sortdescriptor
        else if ( rPropName == "IsSortColumns" )
        {
            bNewSortdescriptor = true;
            if (auto bTemp = o3tl::tryAccess<bool>(aValue))
            {
                rSortOpt.eDirection = *bTemp ? SwSortDirection::Columns : SwSortDirection::Rows;
            }
            else
            {
                bRet = false;
            }
        }
        else if ( rPropName == "SortFields" )
        {
            bNewSortdescriptor = true;
            uno::Sequence < table::TableSortField > aFields;
            if (aValue >>= aFields)
            {
                sal_Int32 nCount(aFields.getLength());
                if (nCount <= 3)
                {
                    table::TableSortField* pFields = aFields.getArray();
                    for (sal_Int32 i = 0; i < nCount; ++i)
                    {
                        rSortOpt.bIgnoreCase = !pFields[i].IsCaseSensitive;
                        rSortOpt.nLanguage =
                            LanguageTag::convertToLanguageType( pFields[i].CollatorLocale );
                        aKeys[i]->sSortType = pFields[i].CollatorAlgorithm;
                        aKeys[i]->nColumnId =
                            o3tl::narrowing<sal_uInt16>(pFields[i].Field);
                        aKeys[i]->bIsNumeric = (pFields[i].FieldType ==
                                table::TableSortFieldType_NUMERIC);
                        aKeys[i]->eSortOrder = (pFields[i].IsAscending)
                            ? SwSortOrder::Ascending : SwSortOrder::Descending;
                    }
                }
                else
                {
                    bRet = false;
                }
            }
            else
            {
                bRet = false;
            }
        }
    }

    if (bNewSortdescriptor && bOldSortdescriptor)
    {
        OSL_FAIL("someone tried to set the old deprecated and "
            "the new sortdescriptor");
        bRet = false;
    }

    if (aKey1.nColumnId != USHRT_MAX)
    {
        rSortOpt.aKeys.push_back(aKey1);
    }
    if (aKey2.nColumnId != USHRT_MAX)
    {
        rSortOpt.aKeys.push_back(aKey2);
    }
    if (aKey3.nColumnId != USHRT_MAX)
    {
        rSortOpt.aKeys.push_back(aKey3);
    }

    return bRet && !rSortOpt.aKeys.empty();
}

void SAL_CALL
SwXTextCursor::sort(const uno::Sequence< beans::PropertyValue >& rDescriptor)
{
    SolarMutexGuard aGuard;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    if (!rUnoCursor.HasMark())
        return;

    SwSortOptions aSortOpt;
    if (!SwUnoCursorHelper::ConvertSortProperties(rDescriptor, aSortOpt))
    {
        throw uno::RuntimeException(u"Bad sort properties"_ustr);
    }
    UnoActionContext aContext( &rUnoCursor.GetDoc() );

    SwPosition & rStart = *rUnoCursor.Start();
    SwPosition & rEnd   = *rUnoCursor.End();

    SwNodeIndex aPrevIdx( rStart.GetNode(), -1 );
    const SwNodeOffset nOffset = rEnd.GetNodeIndex() - rStart.GetNodeIndex();
    const sal_Int32 nCntStt  = rStart.GetContentIndex();

    rUnoCursor.GetDoc().SortText(rUnoCursor, aSortOpt);

    // update selection
    rUnoCursor.DeleteMark();
    rUnoCursor.GetPoint()->Assign( aPrevIdx.GetNode(), SwNodeOffset(1) );
    SwContentNode *const pCNd = rUnoCursor.GetPointContentNode();
    sal_Int32 nLen = pCNd->Len();
    if (nLen > nCntStt)
    {
        nLen = nCntStt;
    }
    rUnoCursor.GetPoint()->SetContent( nLen );
    rUnoCursor.SetMark();

    rUnoCursor.GetPoint()->Adjust(nOffset);
    SwContentNode *const pCNd2 = rUnoCursor.GetPointContentNode();
    rUnoCursor.GetPoint()->SetContent( pCNd2->Len() );

}

uno::Reference< container::XEnumeration > SAL_CALL
SwXTextCursor::createContentEnumeration(const OUString& rServiceName)
{
    SolarMutexGuard g;
    if (rServiceName != "com.sun.star.text.TextContent")
        throw uno::RuntimeException();
    SwUnoCursor& rUnoCursor( GetCursorOrThrow() );
    return SwXParaFrameEnumeration::Create(rUnoCursor, PARAFRAME_PORTION_TEXTRANGE);
}

uno::Reference< container::XEnumeration > SAL_CALL
SwXTextCursor::createEnumeration()
{
    SolarMutexGuard g;

    SwUnoCursor & rUnoCursor( GetCursorOrThrow() );

    SwXText* pParentText = dynamic_cast<SwXText*>(m_xParentText.get());
    OSL_ENSURE(pParentText, "parent is not a SwXText");
    if (!pParentText)
    {
        throw uno::RuntimeException();
    }

    auto pNewCursor(rUnoCursor.GetDoc().CreateUnoCursor(*rUnoCursor.GetPoint()) );
    if (rUnoCursor.HasMark())
    {
        pNewCursor->SetMark();
        *pNewCursor->GetMark() = *rUnoCursor.GetMark();
    }
    const CursorType eSetType = (CursorType::TableText == m_eType)
            ? CursorType::SelectionInTable : CursorType::Selection;
    return SwXParagraphEnumeration::Create(pParentText, pNewCursor, eSetType);
}

uno::Type SAL_CALL
SwXTextCursor::getElementType()
{
    return cppu::UnoType<text::XTextRange>::get();
}

sal_Bool SAL_CALL SwXTextCursor::hasElements()
{
    return true;
}

uno::Sequence< OUString > SAL_CALL
SwXTextCursor::getAvailableServiceNames()
{
    uno::Sequence<OUString> aRet { u"com.sun.star.text.TextContent"_ustr };
    return aRet;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
