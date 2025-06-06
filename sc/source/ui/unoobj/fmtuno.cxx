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

#include <o3tl/safeint.hxx>
#include <osl/diagnose.h>
#include <svl/style.hxx>
#include <utility>
#include <vcl/svapp.hxx>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <com/sun/star/sheet/ConditionOperator2.hpp>
#include <com/sun/star/sheet/ValidationAlertStyle.hpp>
#include <com/sun/star/sheet/ValidationType.hpp>
#include <com/sun/star/sheet/TableValidationVisibility.hpp>

#include <fmtuno.hxx>
#include <miscuno.hxx>
#include <validat.hxx>
#include <document.hxx>
#include <unonames.hxx>
#include <tokenarray.hxx>
#include <tokenuno.hxx>
#include <stylehelper.hxx>

using namespace ::com::sun::star;
using namespace ::formula;

//  map only for PropertySetInfo

static std::span<const SfxItemPropertyMapEntry> lcl_GetValidatePropertyMap()
{
    static const SfxItemPropertyMapEntry aValidatePropertyMap_Impl[] =
    {
        { SC_UNONAME_ERRALSTY, 0,  cppu::UnoType<sheet::ValidationAlertStyle>::get(),  0, 0},
        { SC_UNONAME_ERRMESS,  0,  cppu::UnoType<OUString>::get(),                0, 0},
        { SC_UNONAME_ERRTITLE, 0,  cppu::UnoType<OUString>::get(),                0, 0},
        { SC_UNONAME_IGNOREBL, 0,  cppu::UnoType<bool>::get(),                          0, 0},
        { SC_UNONAME_ISCASE,   0,  cppu::UnoType<bool>::get(),                          0, 0},
        { SC_UNONAME_INPMESS,  0,  cppu::UnoType<OUString>::get(),                0, 0},
        { SC_UNONAME_INPTITLE, 0,  cppu::UnoType<OUString>::get(),                0, 0},
        { SC_UNONAME_SHOWERR,  0,  cppu::UnoType<bool>::get(),                          0, 0},
        { SC_UNONAME_SHOWINP,  0,  cppu::UnoType<bool>::get(),                          0, 0},
        { SC_UNONAME_SHOWLIST, 0,  cppu::UnoType<sal_Int16>::get(),                    0, 0},
        { SC_UNONAME_TYPE,     0,  cppu::UnoType<sheet::ValidationType>::get(),        0, 0},
    };
    return aValidatePropertyMap_Impl;
}

SC_SIMPLE_SERVICE_INFO( ScTableConditionalEntry, u"ScTableConditionalEntry"_ustr, u"com.sun.star.sheet.TableConditionalEntry"_ustr )
SC_SIMPLE_SERVICE_INFO( ScTableConditionalFormat, u"ScTableConditionalFormat"_ustr, u"com.sun.star.sheet.TableConditionalFormat"_ustr )
SC_SIMPLE_SERVICE_INFO( ScTableValidationObj, u"ScTableValidationObj"_ustr, u"com.sun.star.sheet.TableValidation"_ustr )

static sal_Int32 lcl_ConditionModeToOperatorNew( ScConditionMode eMode )
{
    sal_Int32 eOper = sheet::ConditionOperator2::NONE;
    switch (eMode)
    {
        case ScConditionMode::Equal:         eOper = sheet::ConditionOperator2::EQUAL;           break;
        case ScConditionMode::Less:          eOper = sheet::ConditionOperator2::LESS;            break;
        case ScConditionMode::Greater:       eOper = sheet::ConditionOperator2::GREATER;         break;
        case ScConditionMode::EqLess:        eOper = sheet::ConditionOperator2::LESS_EQUAL;      break;
        case ScConditionMode::EqGreater:     eOper = sheet::ConditionOperator2::GREATER_EQUAL;   break;
        case ScConditionMode::NotEqual:      eOper = sheet::ConditionOperator2::NOT_EQUAL;       break;
        case ScConditionMode::Between:       eOper = sheet::ConditionOperator2::BETWEEN;         break;
        case ScConditionMode::NotBetween:    eOper = sheet::ConditionOperator2::NOT_BETWEEN;     break;
        case ScConditionMode::Direct:        eOper = sheet::ConditionOperator2::FORMULA;         break;
        case ScConditionMode::Duplicate:     eOper = sheet::ConditionOperator2::DUPLICATE;       break;
        default:
        {
            // added to avoid warnings
        }
    }
    return eOper;
}

static sheet::ConditionOperator lcl_ConditionModeToOperator( ScConditionMode eMode )
{
    sheet::ConditionOperator eOper = sheet::ConditionOperator_NONE;
    switch (eMode)
    {
        case ScConditionMode::Equal:         eOper = sheet::ConditionOperator_EQUAL;         break;
        case ScConditionMode::Less:          eOper = sheet::ConditionOperator_LESS;          break;
        case ScConditionMode::Greater:       eOper = sheet::ConditionOperator_GREATER;       break;
        case ScConditionMode::EqLess:        eOper = sheet::ConditionOperator_LESS_EQUAL;    break;
        case ScConditionMode::EqGreater:     eOper = sheet::ConditionOperator_GREATER_EQUAL; break;
        case ScConditionMode::NotEqual:      eOper = sheet::ConditionOperator_NOT_EQUAL;     break;
        case ScConditionMode::Between:       eOper = sheet::ConditionOperator_BETWEEN;       break;
        case ScConditionMode::NotBetween:    eOper = sheet::ConditionOperator_NOT_BETWEEN;   break;
        case ScConditionMode::Direct:        eOper = sheet::ConditionOperator_FORMULA;       break;
        default:
        {
            // added to avoid warnings
        }
    }
    return eOper;
}

static ScConditionMode lcl_ConditionOperatorToMode( sheet::ConditionOperator eOper )
{
    ScConditionMode eMode = ScConditionMode::NONE;
    switch (eOper)
    {
        case sheet::ConditionOperator_EQUAL:            eMode = ScConditionMode::Equal;      break;
        case sheet::ConditionOperator_LESS:             eMode = ScConditionMode::Less;       break;
        case sheet::ConditionOperator_GREATER:          eMode = ScConditionMode::Greater;    break;
        case sheet::ConditionOperator_LESS_EQUAL:       eMode = ScConditionMode::EqLess;     break;
        case sheet::ConditionOperator_GREATER_EQUAL:    eMode = ScConditionMode::EqGreater;  break;
        case sheet::ConditionOperator_NOT_EQUAL:        eMode = ScConditionMode::NotEqual;   break;
        case sheet::ConditionOperator_BETWEEN:          eMode = ScConditionMode::Between;    break;
        case sheet::ConditionOperator_NOT_BETWEEN:      eMode = ScConditionMode::NotBetween; break;
        case sheet::ConditionOperator_FORMULA:          eMode = ScConditionMode::Direct;     break;
        default:
        {
            // added to avoid warnings
        }
    }
    return eMode;
}

ScCondFormatEntryItem::ScCondFormatEntryItem() :
    meGrammar1( FormulaGrammar::GRAM_UNSPECIFIED ),
    meGrammar2( FormulaGrammar::GRAM_UNSPECIFIED ),
    meMode( ScConditionMode::NONE )
{
}

ScTableConditionalFormat::ScTableConditionalFormat(
        const ScDocument& rDoc, sal_uLong nKey, SCTAB nTab, FormulaGrammar::Grammar eGrammar)
{
    //  read the entry from the document...

    if ( !nKey )
        return;

    ScConditionalFormatList* pList = rDoc.GetCondFormList(nTab);
    if (!pList)
        return;

    const ScConditionalFormat* pFormat = pList->GetFormat( nKey );
    if (!pFormat)
        return;

    // During save to XML.
    if (rDoc.IsInExternalReferenceMarking())
        pFormat->MarkUsedExternalReferences();

    size_t nEntryCount = pFormat->size();
    for (size_t i=0; i<nEntryCount; i++)
    {
        ScCondFormatEntryItem aItem;
        const ScFormatEntry* pFrmtEntry = pFormat->GetEntry(i);
        if(pFrmtEntry->GetType() != ScFormatEntry::Type::Condition &&
           pFrmtEntry->GetType() != ScFormatEntry::Type::ExtCondition)
            continue;

        const ScCondFormatEntry* pFormatEntry = static_cast<const ScCondFormatEntry*>(pFrmtEntry);
        aItem.meMode = pFormatEntry->GetOperation();
        aItem.maPos = pFormatEntry->GetValidSrcPos();
        aItem.maExpr1 = pFormatEntry->GetExpression(aItem.maPos, 0, 0, eGrammar);
        aItem.maExpr2 = pFormatEntry->GetExpression(aItem.maPos, 1, 0, eGrammar);
        aItem.meGrammar1 = aItem.meGrammar2 = eGrammar;
        aItem.maStyle = pFormatEntry->GetStyle();

        AddEntry_Impl(aItem);
    }
}

namespace {

FormulaGrammar::Grammar lclResolveGrammar( FormulaGrammar::Grammar eExtGrammar, FormulaGrammar::Grammar eIntGrammar )
{
    if( eExtGrammar != FormulaGrammar::GRAM_UNSPECIFIED )
        return eExtGrammar;
    OSL_ENSURE( eIntGrammar != FormulaGrammar::GRAM_UNSPECIFIED, "lclResolveGrammar - unspecified grammar, using GRAM_API" );
    return (eIntGrammar == FormulaGrammar::GRAM_UNSPECIFIED) ? FormulaGrammar::GRAM_API : eIntGrammar;
}

} // namespace

void ScTableConditionalFormat::FillFormat( ScConditionalFormat& rFormat,
        ScDocument& rDoc, FormulaGrammar::Grammar eGrammar) const
{
    //  ScConditionalFormat = Core-Struktur, has to be empty

    OSL_ENSURE( rFormat.IsEmpty(), "FillFormat: format not empty" );

    for (const auto & i : maEntries)
    {
        ScCondFormatEntryItem aData;
        i->GetData(aData);

        FormulaGrammar::Grammar eGrammar1 = lclResolveGrammar( eGrammar, aData.meGrammar1 );
        FormulaGrammar::Grammar eGrammar2 = lclResolveGrammar( eGrammar, aData.meGrammar2 );

        ScCondFormatEntry* pCoreEntry = new ScCondFormatEntry( aData.meMode, aData.maExpr1, aData.maExpr2,
            rDoc, aData.maPos, aData.maStyle, aData.maExprNmsp1, aData.maExprNmsp2, eGrammar1, eGrammar2 );

        if ( !aData.maPosStr.isEmpty() )
            pCoreEntry->SetSrcString( aData.maPosStr );

        if ( aData.maTokens1.hasElements() )
        {
            ScTokenArray aTokenArray(rDoc);
            if ( ScTokenConversion::ConvertToTokenArray(rDoc, aTokenArray, aData.maTokens1) )
                pCoreEntry->SetFormula1(aTokenArray);
        }

        if ( aData.maTokens2.hasElements() )
        {
            ScTokenArray aTokenArray(rDoc);
            if ( ScTokenConversion::ConvertToTokenArray(rDoc, aTokenArray, aData.maTokens2) )
                pCoreEntry->SetFormula2(aTokenArray);
        }
        rFormat.AddEntry( pCoreEntry );
    }
}

ScTableConditionalFormat::~ScTableConditionalFormat()
{
}

void ScTableConditionalFormat::AddEntry_Impl(const ScCondFormatEntryItem& aEntry)
{
    rtl::Reference<ScTableConditionalEntry> pNew = new ScTableConditionalEntry(aEntry);
    maEntries.emplace_back(pNew);
}

// XSheetConditionalFormat

ScTableConditionalEntry* ScTableConditionalFormat::GetObjectByIndex_Impl(sal_uInt16 nIndex) const
{
    return nIndex < maEntries.size() ? maEntries[nIndex].get() : nullptr;
}

void SAL_CALL ScTableConditionalFormat::addNew(
                    const uno::Sequence<beans::PropertyValue >& aConditionalEntry )
{
    SolarMutexGuard aGuard;
    ScCondFormatEntryItem aEntry;
    aEntry.meMode = ScConditionMode::NONE;

    for (const beans::PropertyValue& rProp : aConditionalEntry)
    {
        if ( rProp.Name == SC_UNONAME_OPERATOR )
        {
            sal_Int32 eOper = ScUnoHelpFunctions::GetEnumFromAny( rProp.Value );
            aEntry.meMode = ScConditionEntry::GetModeFromApi( static_cast<sheet::ConditionOperator>(eOper) );
        }
        else if ( rProp.Name == SC_UNONAME_FORMULA1 )
        {
            OUString aStrVal;
            uno::Sequence<sheet::FormulaToken> aTokens;
            if ( rProp.Value >>= aStrVal )
                aEntry.maExpr1 = aStrVal;
            else if ( rProp.Value >>= aTokens )
            {
                aEntry.maExpr1.clear();
                aEntry.maTokens1 = std::move(aTokens);
            }
        }
        else if ( rProp.Name == SC_UNONAME_FORMULA2 )
        {
            OUString aStrVal;
            uno::Sequence<sheet::FormulaToken> aTokens;
            if ( rProp.Value >>= aStrVal )
                aEntry.maExpr2 = aStrVal;
            else if ( rProp.Value >>= aTokens )
            {
                aEntry.maExpr2.clear();
                aEntry.maTokens2 = std::move(aTokens);
            }
        }
        else if ( rProp.Name == SC_UNONAME_SOURCEPOS )
        {
            table::CellAddress aAddress;
            if ( rProp.Value >>= aAddress )
                aEntry.maPos = ScAddress( static_cast<SCCOL>(aAddress.Column), static_cast<SCROW>(aAddress.Row), aAddress.Sheet );
        }
        else if ( rProp.Name == SC_UNONAME_SOURCESTR )
        {
            OUString aStrVal;
            if ( rProp.Value >>= aStrVal )
                aEntry.maPosStr = aStrVal;
        }
        else if ( rProp.Name == SC_UNONAME_STYLENAME )
        {
            OUString aStrVal;
            if ( rProp.Value >>= aStrVal )
                aEntry.maStyle = ScStyleNameConversion::ProgrammaticToDisplayName(
                                                aStrVal, SfxStyleFamily::Para );
        }
        else if ( rProp.Name == SC_UNONAME_FORMULANMSP1 )
        {
            OUString aStrVal;
            if ( rProp.Value >>= aStrVal )
                aEntry.maExprNmsp1 = aStrVal;
        }
        else if ( rProp.Name == SC_UNONAME_FORMULANMSP2 )
        {
            OUString aStrVal;
            if ( rProp.Value >>= aStrVal )
                aEntry.maExprNmsp2 = aStrVal;
        }
        else if ( rProp.Name == SC_UNONAME_GRAMMAR1 )
        {
            sal_Int32 nVal = 0;
            if ( rProp.Value >>= nVal )
                aEntry.meGrammar1 = static_cast< FormulaGrammar::Grammar >( nVal );
        }
        else if ( rProp.Name == SC_UNONAME_GRAMMAR2 )
        {
            sal_Int32 nVal = 0;
            if ( rProp.Value >>= nVal )
                aEntry.meGrammar2 = static_cast< FormulaGrammar::Grammar >( nVal );
        }
        else
        {
            OSL_FAIL("wrong property");
            //! Exception...
        }
    }

    AddEntry_Impl(aEntry);
}

void SAL_CALL ScTableConditionalFormat::removeByIndex( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;

    if (nIndex >= 0 && o3tl::make_unsigned(nIndex) < maEntries.size())
    {
        maEntries.erase(maEntries.begin()+nIndex);
    }
}

void SAL_CALL ScTableConditionalFormat::clear()
{
    SolarMutexGuard aGuard;
    maEntries.clear();
}

// XEnumerationAccess

uno::Reference<container::XEnumeration> SAL_CALL ScTableConditionalFormat::createEnumeration()
{
    SolarMutexGuard aGuard;
    return new ScIndexEnumeration(this, u"com.sun.star.sheet.TableConditionalEntryEnumeration"_ustr);
}

// XIndexAccess

sal_Int32 SAL_CALL ScTableConditionalFormat::getCount()
{
    SolarMutexGuard aGuard;
    return maEntries.size();
}

uno::Any SAL_CALL ScTableConditionalFormat::getByIndex( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;
    uno::Reference<sheet::XSheetConditionalEntry> xEntry(GetObjectByIndex_Impl(static_cast<sal_uInt16>(nIndex)));
    if (!xEntry.is())
        throw lang::IndexOutOfBoundsException();

    return uno::Any(xEntry);
}

uno::Type SAL_CALL ScTableConditionalFormat::getElementType()
{
    return cppu::UnoType<sheet::XSheetConditionalEntry>::get();
}

sal_Bool SAL_CALL ScTableConditionalFormat::hasElements()
{
    SolarMutexGuard aGuard;
    return ( getCount() != 0 );
}

//  conditional format entries have no real names
//  -> generate name from index

static OUString lcl_GetEntryNameFromIndex( sal_Int32 nIndex )
{
    OUString aRet = "Entry" + OUString::number( nIndex );
    return aRet;
}

uno::Any SAL_CALL ScTableConditionalFormat::getByName( const OUString& aName )
{
    SolarMutexGuard aGuard;

    uno::Reference<sheet::XSheetConditionalEntry> xEntry;
    tools::Long nCount = maEntries.size();
    for (tools::Long i=0; i<nCount; i++)
        if ( aName == lcl_GetEntryNameFromIndex(i) )
        {
            xEntry.set(GetObjectByIndex_Impl(static_cast<sal_uInt16>(i)));
            break;
        }

    if (!xEntry.is())
        throw container::NoSuchElementException();

    return uno::Any(xEntry);
}

uno::Sequence<OUString> SAL_CALL ScTableConditionalFormat::getElementNames()
{
    SolarMutexGuard aGuard;

    tools::Long nCount = maEntries.size();
    uno::Sequence<OUString> aNames(nCount);
    OUString* pArray = aNames.getArray();
    for (tools::Long i=0; i<nCount; i++)
        pArray[i] = lcl_GetEntryNameFromIndex(i);

    return aNames;
}

sal_Bool SAL_CALL ScTableConditionalFormat::hasByName( const OUString& aName )
{
    SolarMutexGuard aGuard;

    tools::Long nCount = maEntries.size();
    for (tools::Long i=0; i<nCount; i++)
        if ( aName == lcl_GetEntryNameFromIndex(i) )
            return true;

    return false;
}

ScTableConditionalEntry::ScTableConditionalEntry(ScCondFormatEntryItem  aItem) :
    aData(std::move( aItem ))
{
    // #i113668# only store the settings, keep no reference to parent object
}

ScTableConditionalEntry::~ScTableConditionalEntry()
{
}

void ScTableConditionalEntry::GetData(ScCondFormatEntryItem& rData) const
{
    rData = aData;
}

// XSheetCondition

sheet::ConditionOperator SAL_CALL ScTableConditionalEntry::getOperator()
{
    SolarMutexGuard aGuard;
    return lcl_ConditionModeToOperator( aData.meMode );
}

void SAL_CALL ScTableConditionalEntry::setOperator( sheet::ConditionOperator nOperator )
{
    SolarMutexGuard aGuard;
    aData.meMode = lcl_ConditionOperatorToMode( nOperator );
}

sal_Int32 SAL_CALL ScTableConditionalEntry::getConditionOperator()
{
    SolarMutexGuard aGuard;
    return lcl_ConditionModeToOperatorNew( aData.meMode );
}

void SAL_CALL ScTableConditionalEntry::setConditionOperator( sal_Int32 nOperator )
{
    SolarMutexGuard aGuard;
    aData.meMode = ScConditionEntry::GetModeFromApi( static_cast<sheet::ConditionOperator>(nOperator) );
}

OUString SAL_CALL ScTableConditionalEntry::getFormula1()
{
    SolarMutexGuard aGuard;
    return aData.maExpr1;
}

void SAL_CALL ScTableConditionalEntry::setFormula1( const OUString& aFormula1 )
{
    SolarMutexGuard aGuard;
    aData.maExpr1 = aFormula1;
}

OUString SAL_CALL ScTableConditionalEntry::getFormula2()
{
    SolarMutexGuard aGuard;
    return aData.maExpr2;
}

void SAL_CALL ScTableConditionalEntry::setFormula2( const OUString& aFormula2 )
{
    SolarMutexGuard aGuard;
    aData.maExpr2 = aFormula2;
}

table::CellAddress SAL_CALL ScTableConditionalEntry::getSourcePosition()
{
    SolarMutexGuard aGuard;
    table::CellAddress aRet;
    aRet.Column = aData.maPos.Col();
    aRet.Row    = aData.maPos.Row();
    aRet.Sheet  = aData.maPos.Tab();
    return aRet;
}

void SAL_CALL ScTableConditionalEntry::setSourcePosition( const table::CellAddress& aSourcePosition )
{
    SolarMutexGuard aGuard;
    aData.maPos.Set( static_cast<SCCOL>(aSourcePosition.Column), static_cast<SCROW>(aSourcePosition.Row), aSourcePosition.Sheet );
}

// XSheetConditionalEntry

OUString SAL_CALL ScTableConditionalEntry::getStyleName()
{
    SolarMutexGuard aGuard;
    return ScStyleNameConversion::DisplayToProgrammaticName( aData.maStyle, SfxStyleFamily::Para );
}

void SAL_CALL ScTableConditionalEntry::setStyleName( const OUString& aStyleName )
{
    SolarMutexGuard aGuard;
    aData.maStyle = ScStyleNameConversion::ProgrammaticToDisplayName( aStyleName, SfxStyleFamily::Para );
}

ScTableValidationObj::ScTableValidationObj(const ScDocument& rDoc, sal_uInt32 nKey,
                                           const formula::FormulaGrammar::Grammar eGrammar) :
    aPropSet( lcl_GetValidatePropertyMap() )
{
    //  read the entry from the document...

    bool bFound = false;
    if (nKey)
    {
        const ScValidationData* pData = rDoc.GetValidationEntry( nKey );
        if (pData)
        {
            nMode = pData->GetOperation();
            aSrcPos = pData->GetValidSrcPos();  // valid pos for expressions
            aExpr1 = pData->GetExpression( aSrcPos, 0, 0, eGrammar );
            aExpr2 = pData->GetExpression( aSrcPos, 1, 0, eGrammar );
            meGrammar1 = meGrammar2 = eGrammar;
            nValMode = sal::static_int_cast<sal_uInt16>( pData->GetDataMode() );
            bIgnoreBlank = pData->IsIgnoreBlank();
            bCaseSensitive = pData->IsCaseSensitive();
            nShowList = pData->GetListType();
            bShowInput = pData->GetInput( aInputTitle, aInputMessage );
            ScValidErrorStyle eStyle;
            bShowError = pData->GetErrMsg( aErrorTitle, aErrorMessage, eStyle );
            nErrorStyle = sal::static_int_cast<sal_uInt16>( eStyle );

            // During save to XML, sheet::ValidationType_ANY formulas are not
            // saved, even if in the list, see
            // ScMyValidationsContainer::GetCondition(), so shall not mark
            // anything in use.
            if (nValMode != SC_VALID_ANY && rDoc.IsInExternalReferenceMarking())
                pData->MarkUsedExternalReferences();

            bFound = true;
        }
    }
    if (!bFound)
        ClearData_Impl();       // Defaults
}

ScValidationData* ScTableValidationObj::CreateValidationData( ScDocument& rDoc,
                                            formula::FormulaGrammar::Grammar eGrammar ) const
{
    //  ScValidationData = Core-Struktur

    FormulaGrammar::Grammar eGrammar1 = lclResolveGrammar( eGrammar, meGrammar1 );
    FormulaGrammar::Grammar eGrammar2 = lclResolveGrammar( eGrammar, meGrammar2 );

    ScValidationData* pRet = new ScValidationData( static_cast<ScValidationMode>(nValMode),
                                                   nMode,
                                                   aExpr1, aExpr2, rDoc, aSrcPos,
                                                   maExprNmsp1, maExprNmsp2,
                                                   eGrammar1, eGrammar2 );
    pRet->SetIgnoreBlank(bIgnoreBlank);
    pRet->SetCaseSensitive(bCaseSensitive);
    pRet->SetListType(nShowList);

    if ( aTokens1.hasElements() )
    {
        ScTokenArray aTokenArray(rDoc);
        if ( ScTokenConversion::ConvertToTokenArray(rDoc, aTokenArray, aTokens1) )
            pRet->SetFormula1(aTokenArray);
    }

    if ( aTokens2.hasElements() )
    {
        ScTokenArray aTokenArray(rDoc);
        if ( ScTokenConversion::ConvertToTokenArray(rDoc, aTokenArray, aTokens2) )
            pRet->SetFormula2(aTokenArray);
    }

    // set strings for error / input even if disabled (and disable afterwards)
    pRet->SetInput( aInputTitle, aInputMessage );
    if (!bShowInput)
        pRet->ResetInput();
    pRet->SetError( aErrorTitle, aErrorMessage, static_cast<ScValidErrorStyle>(nErrorStyle) );
    if (!bShowError)
        pRet->ResetError();

    if ( !aPosString.isEmpty() )
        pRet->SetSrcString( aPosString );

    return pRet;
}

void ScTableValidationObj::ClearData_Impl()
{
    nMode        = ScConditionMode::NONE;
    nValMode     = SC_VALID_ANY;
    bIgnoreBlank = true;
    bCaseSensitive = false;
    nShowList    = sheet::TableValidationVisibility::UNSORTED;
    bShowInput   = false;
    bShowError   = false;
    nErrorStyle  = SC_VALERR_STOP;
    aSrcPos.Set(0,0,0);
    aExpr1.clear();
    aExpr2.clear();
    maExprNmsp1.clear();
    maExprNmsp2.clear();
    meGrammar1 = meGrammar2 = FormulaGrammar::GRAM_UNSPECIFIED;  // will be overridden when needed
    aInputTitle.clear();
    aInputMessage.clear();
    aErrorTitle.clear();
    aErrorMessage.clear();
}

ScTableValidationObj::~ScTableValidationObj()
{
}

// XSheetCondition

sheet::ConditionOperator SAL_CALL ScTableValidationObj::getOperator()
{
    SolarMutexGuard aGuard;
    return lcl_ConditionModeToOperator( nMode );
}

void SAL_CALL ScTableValidationObj::setOperator( sheet::ConditionOperator nOperator )
{
    SolarMutexGuard aGuard;
    nMode = lcl_ConditionOperatorToMode( nOperator );
}

sal_Int32 SAL_CALL ScTableValidationObj::getConditionOperator()
{
    SolarMutexGuard aGuard;
    return lcl_ConditionModeToOperatorNew( nMode );
}

void SAL_CALL ScTableValidationObj::setConditionOperator( sal_Int32 nOperator )
{
    SolarMutexGuard aGuard;
    nMode = ScConditionEntry::GetModeFromApi( static_cast<css::sheet::ConditionOperator>(nOperator) );
}

OUString SAL_CALL ScTableValidationObj::getFormula1()
{
    SolarMutexGuard aGuard;
    return aExpr1;
}

void SAL_CALL ScTableValidationObj::setFormula1( const OUString& aFormula1 )
{
    SolarMutexGuard aGuard;
    aExpr1 = aFormula1;
}

OUString SAL_CALL ScTableValidationObj::getFormula2()
{
    SolarMutexGuard aGuard;
    return aExpr2;
}

void SAL_CALL ScTableValidationObj::setFormula2( const OUString& aFormula2 )
{
    SolarMutexGuard aGuard;
    aExpr2 = aFormula2;
}

table::CellAddress SAL_CALL ScTableValidationObj::getSourcePosition()
{
    SolarMutexGuard aGuard;
    table::CellAddress aRet;
    aRet.Column = aSrcPos.Col();
    aRet.Row    = aSrcPos.Row();
    aRet.Sheet  = aSrcPos.Tab();
    return aRet;
}

void SAL_CALL ScTableValidationObj::setSourcePosition( const table::CellAddress& aSourcePosition )
{
    SolarMutexGuard aGuard;
    aSrcPos.Set( static_cast<SCCOL>(aSourcePosition.Column), static_cast<SCROW>(aSourcePosition.Row), aSourcePosition.Sheet );
}

uno::Sequence<sheet::FormulaToken> SAL_CALL ScTableValidationObj::getTokens( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;
    if (nIndex >= 2 || nIndex < 0)
        throw lang::IndexOutOfBoundsException();

    return nIndex == 0 ? aTokens1 : aTokens2;
}

void SAL_CALL ScTableValidationObj::setTokens( sal_Int32 nIndex, const uno::Sequence<sheet::FormulaToken>& aTokens )
{
    SolarMutexGuard aGuard;
    if (nIndex >= 2 || nIndex < 0)
        throw lang::IndexOutOfBoundsException();

    if (nIndex == 0)
    {
        aTokens1 = aTokens;
        aExpr1.clear();
    }
    else if (nIndex == 1)
    {
        aTokens2 = aTokens;
        aExpr2.clear();
    }
}

sal_Int32 SAL_CALL ScTableValidationObj::getCount()
{
    return 2;
}

uno::Reference<beans::XPropertySetInfo> SAL_CALL ScTableValidationObj::getPropertySetInfo()
{
    SolarMutexGuard aGuard;
    static uno::Reference<beans::XPropertySetInfo> aRef(
        new SfxItemPropertySetInfo( aPropSet.getPropertyMap() ));
    return aRef;
}

void SAL_CALL ScTableValidationObj::setPropertyValue(
                        const OUString& aPropertyName, const uno::Any& aValue )
{
    SolarMutexGuard aGuard;

    if ( aPropertyName == SC_UNONAME_SHOWINP )       bShowInput = ScUnoHelpFunctions::GetBoolFromAny( aValue );
    else if ( aPropertyName == SC_UNONAME_SHOWERR )  bShowError = ScUnoHelpFunctions::GetBoolFromAny( aValue );
    else if ( aPropertyName == SC_UNONAME_IGNOREBL ) bIgnoreBlank = ScUnoHelpFunctions::GetBoolFromAny( aValue );
    else if ( aPropertyName == SC_UNONAME_ISCASE )   bCaseSensitive = ScUnoHelpFunctions::GetBoolFromAny( aValue );
    else if ( aPropertyName == SC_UNONAME_SHOWLIST ) aValue >>= nShowList;
    else if ( aPropertyName == SC_UNONAME_INPTITLE )
    {
        OUString aStrVal;
        if ( aValue >>= aStrVal )
            aInputTitle = aStrVal;
    }
    else if ( aPropertyName == SC_UNONAME_INPMESS )
    {
        OUString aStrVal;
        if ( aValue >>= aStrVal )
            aInputMessage = aStrVal;
    }
    else if ( aPropertyName == SC_UNONAME_ERRTITLE )
    {
        OUString aStrVal;
        if ( aValue >>= aStrVal )
            aErrorTitle = aStrVal;
    }
    else if ( aPropertyName == SC_UNONAME_ERRMESS )
    {
        OUString aStrVal;
        if ( aValue >>= aStrVal )
            aErrorMessage = aStrVal;
    }
    else if ( aPropertyName == SC_UNONAME_TYPE )
    {
        sheet::ValidationType eType = static_cast<sheet::ValidationType>(ScUnoHelpFunctions::GetEnumFromAny( aValue ));
        switch (eType)
        {
            case sheet::ValidationType_ANY:      nValMode = SC_VALID_ANY;     break;
            case sheet::ValidationType_WHOLE:    nValMode = SC_VALID_WHOLE;   break;
            case sheet::ValidationType_DECIMAL:  nValMode = SC_VALID_DECIMAL; break;
            case sheet::ValidationType_DATE:     nValMode = SC_VALID_DATE;    break;
            case sheet::ValidationType_TIME:     nValMode = SC_VALID_TIME;    break;
            case sheet::ValidationType_TEXT_LEN: nValMode = SC_VALID_TEXTLEN; break;
            case sheet::ValidationType_LIST:     nValMode = SC_VALID_LIST;    break;
            case sheet::ValidationType_CUSTOM:   nValMode = SC_VALID_CUSTOM;  break;
            default:
            {
                // added to avoid warnings
            }
        }
    }
    else if ( aPropertyName == SC_UNONAME_ERRALSTY )
    {
        sheet::ValidationAlertStyle eStyle = static_cast<sheet::ValidationAlertStyle>(ScUnoHelpFunctions::GetEnumFromAny( aValue ));
        switch (eStyle)
        {
            case sheet::ValidationAlertStyle_STOP:    nErrorStyle = SC_VALERR_STOP;    break;
            case sheet::ValidationAlertStyle_WARNING: nErrorStyle = SC_VALERR_WARNING; break;
            case sheet::ValidationAlertStyle_INFO:    nErrorStyle = SC_VALERR_INFO;    break;
            case sheet::ValidationAlertStyle_MACRO:   nErrorStyle = SC_VALERR_MACRO;   break;
            default:
            {
                // added to avoid warnings
            }
        }
    }
    else if ( aPropertyName == SC_UNONAME_SOURCESTR )
    {
        // internal - only for XML filter, not in PropertySetInfo, only set

        OUString aStrVal;
        if ( aValue >>= aStrVal )
            aPosString =  aStrVal;
    }
    else if ( aPropertyName == SC_UNONAME_FORMULANMSP1 )
    {
        // internal - only for XML filter, not in PropertySetInfo, only set

        OUString aStrVal;
        if ( aValue >>= aStrVal )
            maExprNmsp1 = aStrVal;
    }
    else if ( aPropertyName == SC_UNONAME_FORMULANMSP2 )
    {
        // internal - only for XML filter, not in PropertySetInfo, only set

        OUString aStrVal;
        if ( aValue >>= aStrVal )
            maExprNmsp2 = aStrVal;
    }
    else if ( aPropertyName == SC_UNONAME_GRAMMAR1 )
    {
        // internal - only for XML filter, not in PropertySetInfo, only set

        sal_Int32 nVal = 0;
        if ( aValue >>= nVal )
            meGrammar1 = static_cast< FormulaGrammar::Grammar >(nVal);
    }
    else if ( aPropertyName == SC_UNONAME_GRAMMAR2 )
    {
        // internal - only for XML filter, not in PropertySetInfo, only set

        sal_Int32 nVal = 0;
        if ( aValue >>= nVal )
            meGrammar2 = static_cast< FormulaGrammar::Grammar >(nVal);
    }
}

uno::Any SAL_CALL ScTableValidationObj::getPropertyValue( const OUString& aPropertyName )
{
    SolarMutexGuard aGuard;
    uno::Any aRet;

    if ( aPropertyName == SC_UNONAME_SHOWINP )       aRet <<= bShowInput;
    else if ( aPropertyName == SC_UNONAME_SHOWERR )  aRet <<= bShowError;
    else if ( aPropertyName == SC_UNONAME_IGNOREBL ) aRet <<= bIgnoreBlank;
    else if ( aPropertyName == SC_UNONAME_ISCASE )   aRet <<= bCaseSensitive;
    else if ( aPropertyName == SC_UNONAME_SHOWLIST ) aRet <<= nShowList;
    else if ( aPropertyName == SC_UNONAME_INPTITLE ) aRet <<= aInputTitle;
    else if ( aPropertyName == SC_UNONAME_INPMESS )  aRet <<= aInputMessage;
    else if ( aPropertyName == SC_UNONAME_ERRTITLE ) aRet <<= aErrorTitle;
    else if ( aPropertyName == SC_UNONAME_ERRMESS )  aRet <<= aErrorMessage;
    else if ( aPropertyName == SC_UNONAME_TYPE )
    {
        sheet::ValidationType eType = sheet::ValidationType_ANY;
        switch (nValMode)
        {
            case SC_VALID_ANY:      eType = sheet::ValidationType_ANY;      break;
            case SC_VALID_WHOLE:    eType = sheet::ValidationType_WHOLE;    break;
            case SC_VALID_DECIMAL:  eType = sheet::ValidationType_DECIMAL;  break;
            case SC_VALID_DATE:     eType = sheet::ValidationType_DATE;     break;
            case SC_VALID_TIME:     eType = sheet::ValidationType_TIME;     break;
            case SC_VALID_TEXTLEN:  eType = sheet::ValidationType_TEXT_LEN; break;
            case SC_VALID_LIST:     eType = sheet::ValidationType_LIST;     break;
            case SC_VALID_CUSTOM:   eType = sheet::ValidationType_CUSTOM;   break;
        }
        aRet <<= eType;
    }
    else if ( aPropertyName == SC_UNONAME_ERRALSTY )
    {
        sheet::ValidationAlertStyle eStyle = sheet::ValidationAlertStyle_STOP;
        switch (nErrorStyle)
        {
            case SC_VALERR_STOP:    eStyle = sheet::ValidationAlertStyle_STOP;    break;
            case SC_VALERR_WARNING: eStyle = sheet::ValidationAlertStyle_WARNING; break;
            case SC_VALERR_INFO:    eStyle = sheet::ValidationAlertStyle_INFO;    break;
            case SC_VALERR_MACRO:   eStyle = sheet::ValidationAlertStyle_MACRO;   break;
        }
        aRet <<= eStyle;
    }

    return aRet;
}

SC_IMPL_DUMMY_PROPERTY_LISTENER( ScTableValidationObj )

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
