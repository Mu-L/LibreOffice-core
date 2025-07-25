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

#include <memory>
#include <scitems.hxx>
#include <editeng/eeitem.hxx>
#include <i18nlangtag/mslangid.hxx>
#include <editeng/borderline.hxx>
#include <editeng/boxitem.hxx>
#include <editeng/brushitem.hxx>
#include <editeng/editeng.hxx>
#include <editeng/editobj.hxx>
#include <editeng/flditem.hxx>
#include <editeng/fontitem.hxx>
#include <svx/pageitem.hxx>
#include <svl/itemset.hxx>
#include <svl/zforlist.hxx>
#include <svl/IndexedStyleSheets.hxx>
#include <unotools/charclass.hxx>
#include <vcl/outdev.hxx>
#include <vcl/svapp.hxx>
#include <vcl/settings.hxx>
#include <osl/diagnose.h>

#include <sc.hrc>
#include <attrib.hxx>
#include <global.hxx>
#include <globstr.hrc>
#include <scresid.hxx>
#include <document.hxx>
#include <docpool.hxx>
#include <stlpool.hxx>
#include <stlsheet.hxx>
#include <editutil.hxx>
#include <stylehelper.hxx>

ScStyleSheetPool::ScStyleSheetPool( const SfxItemPool& rPoolP,
                                    ScDocument*     pDocument )
    :   SfxStyleSheetPool( rPoolP ),
        pActualStyleSheet( nullptr ),
        pDoc( pDocument ),
        bHasStandardStyles( false )
{
}

ScStyleSheetPool::~ScStyleSheetPool()
{
}

void ScStyleSheetPool::SetDocument( ScDocument* pDocument )
{
    pDoc = pDocument;
}

SfxStyleSheetBase& ScStyleSheetPool::Make( const OUString& rName,
                                           SfxStyleFamily eFam, SfxStyleSearchBits mask,
                                           const OUString& rParentStyleSheetName)
{
    if ( rName == STRING_STANDARD && Find( rName, eFam ) != nullptr )
    {
        //  When updating styles from a template, Office 5.1 sometimes created
        //  files with multiple default styles.
        //  Create new styles in that case:

        //TODO: only when loading?

        OSL_FAIL("renaming additional default style");
        sal_uInt32 nCount = GetIndexedStyleSheets().GetNumberOfStyleSheets();
        for ( sal_uInt32 nAdd = 1; nAdd <= nCount; nAdd++ )
        {
            OUString aNewName = ScResId(STR_STYLENAME_STANDARD) + OUString::number( nAdd );
            if ( Find( aNewName, eFam ) == nullptr )
                return SfxStyleSheetPool::Make(aNewName, eFam, mask, rParentStyleSheetName);
        }
    }

    // Core uses translated names for both naming and display.
    // This for all three, loading standard builtin styles from styles.xml
    // configuration, loading documents and updating from templates.
    return SfxStyleSheetPool::Make( ScStyleNameConversion::ProgrammaticToDisplayName( rName, eFam), eFam, mask, rParentStyleSheetName);
}

rtl::Reference<SfxStyleSheetBase> ScStyleSheetPool::Create( const OUString&   rName,
                                             SfxStyleFamily  eFamily,
                                             SfxStyleSearchBits nMaskP,
                                             const OUString& rParentStyleSheetName )
{
    rtl::Reference<ScStyleSheet> pSheet = new ScStyleSheet( rName, *this, eFamily, nMaskP, rParentStyleSheetName );
    if ( eFamily != SfxStyleFamily::Page && ScResId(STR_STYLENAME_STANDARD) != rName )
        pSheet->SetParent( ScResId(STR_STYLENAME_STANDARD) );

    return pSheet;
}

rtl::Reference<SfxStyleSheetBase> ScStyleSheetPool::Create( const SfxStyleSheetBase& rStyle )
{
    OSL_ENSURE( rStyle.isScStyleSheet(), "Invalid StyleSheet-class! :-/" );
    return new ScStyleSheet( static_cast<const ScStyleSheet&>(rStyle) );
}

void ScStyleSheetPool::Rename(SfxStyleSheetBase& rStyle, const OUString& rNewName, SfxStyleFamily eFamily)
{
    if (eFamily == SfxStyleFamily::Para)
    {
        assert(nullptr != pDoc);
        pDoc->getCellAttributeHelper().RenameCellStyle(static_cast<ScStyleSheet&>(rStyle), rNewName);
        return;
    }
    rStyle.SetName(rNewName);
}

void ScStyleSheetPool::Remove( SfxStyleSheetBase* pStyle )
{
    if ( pStyle )
    {
        OSL_ENSURE( SfxStyleSearchBits::UserDefined & pStyle->GetMask(),
                    "SfxStyleSearchBits::UserDefined not set!" );

        assert(nullptr != pDoc);
        pDoc->getCellAttributeHelper().CellStyleDeleted(static_cast<ScStyleSheet&>(*pStyle));
        SfxStyleSheetPool::Remove(pStyle);
    }
}

void ScStyleSheetPool::CopyStyleFrom( SfxStyleSheetBasePool* pSrcPool,
                                      const OUString& rName, SfxStyleFamily eFamily,
                                      bool bNewStyleHierarchy )
{
    //  this is the Dest-Pool

    SfxStyleSheetBase* pStyleSheet = pSrcPool->Find( rName, eFamily );
    if (!pStyleSheet)
        return;

    const SfxItemSet& rSourceSet = pStyleSheet->GetItemSet();
    SfxStyleSheetBase* pDestSheet = Find( rName, eFamily );
    if (pDestSheet && bNewStyleHierarchy)
        return;
    if (!pDestSheet)
        pDestSheet = &Make( rName, eFamily, pStyleSheet->GetMask() );
    SfxItemSet& rDestSet = pDestSheet->GetItemSet();
    rDestSet.PutExtended( rSourceSet, SfxItemState::INVALID, SfxItemState::DEFAULT );

    if ( eFamily == SfxStyleFamily::Page )
    {
        //  Set-Items

        if ( const SvxSetItem* pSetItem = rSourceSet.GetItemIfSet( ATTR_PAGE_HEADERSET, false ) )
        {
            const SfxItemSet& rSrcSub = pSetItem->GetItemSet();
            SfxItemSet aDestSub( *rDestSet.GetPool(), rSrcSub.GetRanges() );
            aDestSub.PutExtended( rSrcSub, SfxItemState::INVALID, SfxItemState::DEFAULT );
        }
        if ( const SvxSetItem* pSetItem = rSourceSet.GetItemIfSet( ATTR_PAGE_FOOTERSET, false ) )
        {
            const SfxItemSet& rSrcSub = pSetItem->GetItemSet();
            SfxItemSet aDestSub( *rDestSet.GetPool(), rSrcSub.GetRanges() );
            aDestSub.PutExtended( rSrcSub, SfxItemState::INVALID, SfxItemState::DEFAULT );
            rDestSet.Put( SvxSetItem( ATTR_PAGE_FOOTERSET, aDestSub ) );
        }
    }
    else if ( eFamily == SfxStyleFamily::Para )
    {
        // number format exchange list has to be handled here, too

        const SfxUInt32Item* pItem;
        if ( pDoc && pDoc->GetFormatExchangeList() &&
             (pItem = rSourceSet.GetItemIfSet( ATTR_VALUE_FORMAT, false )) )
        {
            sal_uInt32 nOldFormat = pItem->GetValue();
            SvNumberFormatterIndexTable::const_iterator it = pDoc->GetFormatExchangeList()->find(nOldFormat);
            if (it != pDoc->GetFormatExchangeList()->end())
            {
                sal_uInt32 nNewFormat = it->second;
                rDestSet.Put( SfxUInt32Item( ATTR_VALUE_FORMAT, nNewFormat ) );
            }
        }
    }

    const OUString aParentName = pStyleSheet->GetParent();
    if (!bNewStyleHierarchy || aParentName.isEmpty())
        return;

    CopyStyleFrom(pSrcPool, aParentName, eFamily, bNewStyleHierarchy);
    pDestSheet->SetParent(aParentName);
}

void ScStyleSheetPool::CopyUsedGraphicStylesFrom(SfxStyleSheetBasePool* pSrcPool)
{
    //  this is the Dest-Pool

    std::vector<std::pair<SfxStyleSheetBase*, OUString>> aNewStyles;

    auto pSrcSheet = pSrcPool->First(SfxStyleFamily::Frame);
    while (pSrcSheet)
    {
        if (pSrcSheet->IsUsed() && !Find(pSrcSheet->GetName(), pSrcSheet->GetFamily()))
        {
            auto pDestSheet = &Make(pSrcSheet->GetName(), pSrcSheet->GetFamily(), pSrcSheet->GetMask());
            aNewStyles.emplace_back(pDestSheet, pSrcSheet->GetParent());

            SfxItemSet& rDestSet = pDestSheet->GetItemSet();
            rDestSet.Put(pSrcSheet->GetItemSet());
        }

        pSrcSheet = pSrcPool->Next();
    }

    for (const auto& style : aNewStyles)
        style.first->SetParent(style.second);
}

//                      Standard templates

void ScStyleSheetPool::CopyStdStylesFrom( ScStyleSheetPool* pSrcPool )
{
    //  Copy Default styles

    CopyStyleFrom( pSrcPool, ScResId(STR_STYLENAME_STANDARD),     SfxStyleFamily::Para );
    CopyStyleFrom( pSrcPool, ScResId(STR_STYLENAME_STANDARD),     SfxStyleFamily::Frame );
    CopyStyleFrom( pSrcPool, ScResId(STR_STYLENAME_STANDARD),     SfxStyleFamily::Page );
    CopyStyleFrom( pSrcPool, ScResId(STR_STYLENAME_REPORT),       SfxStyleFamily::Page );
}

static void lcl_CheckFont( SfxItemSet& rSet, LanguageType eLang, DefaultFontType nFontType, sal_uInt16 nItemId )
{
    if ( eLang != LANGUAGE_NONE && eLang != LANGUAGE_DONTKNOW && eLang != LANGUAGE_SYSTEM )
    {
        vcl::Font aDefFont = OutputDevice::GetDefaultFont( nFontType, eLang, GetDefaultFontFlags::OnlyOne );
        SvxFontItem aNewItem( aDefFont.GetFamilyTypeMaybeAskConfig(), aDefFont.GetFamilyName(), aDefFont.GetStyleName(),
                              aDefFont.GetPitchMaybeAskConfig(), aDefFont.GetCharSet(), nItemId );
        if ( aNewItem != rSet.Get( nItemId ) )
        {
            // put item into style's ItemSet only if different from (static) default
            rSet.Put( aNewItem );
        }
    }
}

void ScStyleSheetPool::CreateStandardStyles()
{
    //  Add new entries even for CopyStdStylesFrom

    Color           aColBlack   ( COL_BLACK );
    OUString        aStr;
    sal_Int32       nStrLen;
    const OUString  aHelpFile;//which text???
    SfxItemSet*     pSet            = nullptr;
    SfxItemSet*     pHFSet          = nullptr;
    ScEditEngineDefaulter aEdEngine( pDoc->GetEditEnginePool(), true );
    aEdEngine.SetUpdateLayout( false );
    std::unique_ptr<EditTextObject> pEmptyTxtObj = aEdEngine.CreateTextObject();
    std::unique_ptr<EditTextObject> pTxtObj;
    ScPageHFItem aHeaderItem( ATTR_PAGE_HEADERRIGHT );
    ScPageHFItem aFooterItem( ATTR_PAGE_FOOTERRIGHT );
    ScStyleSheet*   pSheet          = nullptr;
    ::editeng::SvxBorderLine    aBorderLine     ( &aColBlack, SvxBorderLineWidth::Medium );
    SvxBoxItem      aBoxItem        ( ATTR_BORDER );
    SvxBoxInfoItem  aBoxInfoItem    ( ATTR_BORDER_INNER );

    OUString  aStrStandard = ScResId(STR_STYLENAME_STANDARD);

    // Cell format templates:

    // 1. Standard

    pSheet = static_cast<ScStyleSheet*>( &Make( aStrStandard, SfxStyleFamily::Para, SfxStyleSearchBits::ScStandard ) );
    pSheet->SetHelpId( aHelpFile, HID_SC_SHEET_CELL_STD );

    //  if default fonts for the document's languages are different from the pool default,
    //  put them into the default style
    //  (not as pool defaults, because pool defaults can't be changed by the user)
    //  the document languages must be set before creating the default styles!

    pSet = &pSheet->GetItemSet();
    LanguageType eLatin, eCjk, eCtl;
    pDoc->GetLanguage( eLatin, eCjk, eCtl );

    //  If the UI language is Korean, the default Latin font has to
    //  be queried for Korean, too (the Latin language from the document can't be Korean).
    //  This is the same logic as in SwDocShell::InitNew.
    LanguageType eUiLanguage = Application::GetSettings().GetUILanguageTag().getLanguageType();
    if (MsLangId::isKorean(eUiLanguage))
        eLatin = eUiLanguage;

    lcl_CheckFont( *pSet, eLatin, DefaultFontType::LATIN_SPREADSHEET, ATTR_FONT );
    lcl_CheckFont( *pSet, eCjk, DefaultFontType::CJK_SPREADSHEET, ATTR_CJK_FONT );
    lcl_CheckFont( *pSet, eCtl, DefaultFontType::CTL_SPREADSHEET, ATTR_CTL_FONT );

    // #i55300# default CTL font size for Thai has to be larger
    // #i59408# The 15 point size causes problems with row heights, so no different
    // size is used for Thai in Calc for now.
//    if ( eCtl == LANGUAGE_THAI )
//        pSet->Put( SvxFontHeightItem( 300, 100, ATTR_CTL_FONT_HEIGHT ) );   // 15 pt

    // Page format template:

    // 1. Standard

    pSheet = static_cast<ScStyleSheet*>( &Make( aStrStandard,
                                    SfxStyleFamily::Page,
                                    SfxStyleSearchBits::ScStandard ) );

    pSet = &pSheet->GetItemSet();
    pSheet->SetHelpId( aHelpFile, HID_SC_SHEET_PAGE_STD );

    // distance to header/footer for the sheet
    SvxSetItem aHFSetItem = pSet->Get( ATTR_PAGE_HEADERSET );
    aHFSetItem.SetWhich(ATTR_PAGE_HEADERSET);
    pSet->Put( aHFSetItem );
    aHFSetItem.SetWhich(ATTR_PAGE_FOOTERSET);
    pSet->Put( aHFSetItem );

    // Header:
    // [empty][\sheet\][empty]

    aEdEngine.SetTextCurrentDefaults(OUString());
    aEdEngine.QuickInsertField( SvxFieldItem(SvxTableField(), EE_FEATURE_FIELD), ESelection() );
    pTxtObj = aEdEngine.CreateTextObject();
    aHeaderItem.SetLeftArea  ( *pEmptyTxtObj );
    aHeaderItem.SetCenterArea( *pTxtObj );
    aHeaderItem.SetRightArea ( *pEmptyTxtObj );
    pSet->Put( aHeaderItem );

    // Footer:
    // [empty][Page \STR_PAGE\][empty]

    aStr = ScResId( STR_PAGE ) + " ";
    aEdEngine.SetTextCurrentDefaults( aStr );
    nStrLen = aStr.getLength();
    aEdEngine.QuickInsertField( SvxFieldItem(SvxPageField(), EE_FEATURE_FIELD), ESelection(0,nStrLen) );
    pTxtObj = aEdEngine.CreateTextObject();
    aFooterItem.SetLeftArea  ( *pEmptyTxtObj );
    aFooterItem.SetCenterArea( *pTxtObj );
    aFooterItem.SetRightArea ( *pEmptyTxtObj );
    pSet->Put( aFooterItem );

    // 2. Report

    pSheet = static_cast<ScStyleSheet*>( &Make( ScResId( STR_STYLENAME_REPORT ),
                                    SfxStyleFamily::Page,
                                    SfxStyleSearchBits::ScStandard ) );
    pSet = &pSheet->GetItemSet();
    pSheet->SetHelpId( aHelpFile, HID_SC_SHEET_PAGE_REP );

    // Background and border
    aBoxItem.SetLine( &aBorderLine, SvxBoxItemLine::TOP );
    aBoxItem.SetLine( &aBorderLine, SvxBoxItemLine::BOTTOM );
    aBoxItem.SetLine( &aBorderLine, SvxBoxItemLine::LEFT );
    aBoxItem.SetLine( &aBorderLine, SvxBoxItemLine::RIGHT );
    aBoxItem.SetAllDistances( 10 ); // 0.2mm
    aBoxInfoItem.SetValid( SvxBoxInfoItemValidFlags::TOP );
    aBoxInfoItem.SetValid( SvxBoxInfoItemValidFlags::BOTTOM );
    aBoxInfoItem.SetValid( SvxBoxInfoItemValidFlags::LEFT );
    aBoxInfoItem.SetValid( SvxBoxInfoItemValidFlags::RIGHT );
    aBoxInfoItem.SetValid( SvxBoxInfoItemValidFlags::DISTANCE );
    aBoxInfoItem.SetTable( false );
    aBoxInfoItem.SetDist ( true );

    SvxSetItem aHFSetItem2 = pSet->Get( ATTR_PAGE_HEADERSET );
    pHFSet = &(aHFSetItem2.GetItemSet());

    pHFSet->Put( SvxBrushItem( COL_LIGHTGRAY, ATTR_BACKGROUND ) );
    pHFSet->Put( aBoxItem );
    pHFSet->Put( aBoxInfoItem );
    aHFSetItem2.SetWhich(ATTR_PAGE_HEADERSET);
    pSet->Put( aHFSetItem2 );
    aHFSetItem2.SetWhich(ATTR_PAGE_FOOTERSET);
    pSet->Put( aHFSetItem2 );

    // Footer:
    // [\TABLE\ (\DATA\)][empty][\DATE\, \TIME\]

    aStr = " ()";
    aEdEngine.SetTextCurrentDefaults( aStr );
    aEdEngine.QuickInsertField( SvxFieldItem(SvxFileField(), EE_FEATURE_FIELD), ESelection(0,2) );
    aEdEngine.QuickInsertField( SvxFieldItem(SvxTableField(), EE_FEATURE_FIELD), ESelection() );
    pTxtObj = aEdEngine.CreateTextObject();
    aHeaderItem.SetLeftArea( *pTxtObj );
    aHeaderItem.SetCenterArea( *pEmptyTxtObj );
    aStr = ", ";
    aEdEngine.SetTextCurrentDefaults( aStr );
    aEdEngine.QuickInsertField( SvxFieldItem(SvxTimeField(), EE_FEATURE_FIELD), ESelection(0,2) );
    aEdEngine.QuickInsertField( SvxFieldItem(SvxDateField(Date( Date::SYSTEM ),SvxDateType::Var), EE_FEATURE_FIELD),
                                    ESelection() );
    pTxtObj = aEdEngine.CreateTextObject();
    aHeaderItem.SetRightArea( *pTxtObj );
    pSet->Put( aHeaderItem );

    // Footer:
    // [empty][Page: \PAGE\ / \PAGE\][empty]

    aStr = ScResId( STR_PAGE ) + " ";
    nStrLen = aStr.getLength();
    aStr += " / ";
    sal_Int32 nStrLen2 = aStr.getLength();
    aEdEngine.SetTextCurrentDefaults( aStr );
    aEdEngine.QuickInsertField( SvxFieldItem(SvxPagesField(), EE_FEATURE_FIELD), ESelection(0,nStrLen2) );
    aEdEngine.QuickInsertField( SvxFieldItem(SvxPageField(), EE_FEATURE_FIELD), ESelection(0,nStrLen) );
    pTxtObj = aEdEngine.CreateTextObject();
    aFooterItem.SetLeftArea  ( *pEmptyTxtObj );
    aFooterItem.SetCenterArea( *pTxtObj );
    aFooterItem.SetRightArea ( *pEmptyTxtObj );
    pSet->Put( aFooterItem );

    bHasStandardStyles = true;
}

namespace {

struct CaseInsensitiveNamePredicate : svl::StyleSheetPredicate
{
    CaseInsensitiveNamePredicate(const OUString& rName, SfxStyleFamily eFam)
    : mUppercaseName(ScGlobal::getCharClass().uppercase(rName)), mFamily(eFam)
    {
    }

    bool
    Check(const SfxStyleSheetBase& rStyleSheet) override
    {
        if (rStyleSheet.GetFamily() == mFamily)
        {
            OUString aUpName = ScGlobal::getCharClass().uppercase(rStyleSheet.GetName());
            if (mUppercaseName == aUpName)
            {
                return true;
            }
        }
        return false;
    }

    OUString mUppercaseName;
    SfxStyleFamily mFamily;
};

}

// Functor object to find all style sheets of a family which match a given name caseinsensitively
ScStyleSheet* ScStyleSheetPool::FindCaseIns( const OUString& rName, SfxStyleFamily eFam )
{
    CaseInsensitiveNamePredicate aPredicate(rName, eFam);
    std::vector<sal_Int32> aFoundPositions = GetIndexedStyleSheets().FindPositionsByPredicate(aPredicate);

    ScStyleSheet* first = nullptr; // first case insensitive match found
    for (const auto& rPos : aFoundPositions)
    {
        SfxStyleSheetBase *pFound = GetStyleSheetByPositionInIndex(rPos);
        // we do not know what kind of sheets we have.
        if (pFound->isScStyleSheet())
        {
            if (pFound->GetName() == rName) // exact case sensitive match
                return static_cast<ScStyleSheet*>(pFound);
            if (!first)
                first = static_cast<ScStyleSheet*>(pFound);
        }
    }
    return first;
}

ScStyleSheet* ScStyleSheetPool::FindAutoStyle(const OUString& rName)
{
    ScStyleSheet* pStyleSheet = FindCaseIns(rName, SfxStyleFamily::Para);
    if (!pStyleSheet)
        if (auto pFound = Find(ScResId(STR_STYLENAME_STANDARD), SfxStyleFamily::Para))
            if (pFound->isScStyleSheet()) // we do not know what kind of sheets we have
                pStyleSheet = static_cast<ScStyleSheet*>(pFound);
    return pStyleSheet;
}

void ScStyleSheetPool::setAllParaStandard()
{
    SfxStyleSheetBase* pSheet = First(SfxStyleFamily::Para);
    while (pSheet)
    {
        pSheet->SetMask(SfxStyleSearchBits::ScStandard);
        pSheet = Next();
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
