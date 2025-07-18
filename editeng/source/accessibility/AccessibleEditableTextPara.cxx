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


// Global header


#include <algorithm>
#include <utility>
#include <vcl/svapp.hxx>
#include <tools/debug.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <sal/log.hxx>
#include <editeng/flditem.hxx>
#include <com/sun/star/uno/Any.hxx>
#include <com/sun/star/uno/Reference.hxx>
#include <com/sun/star/awt/Point.hpp>
#include <com/sun/star/awt/Rectangle.hpp>
#include <com/sun/star/container/XNameContainer.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <com/sun/star/i18n/Boundary.hpp>
#include <com/sun/star/accessibility/AccessibleRole.hpp>
#include <com/sun/star/accessibility/AccessibleTextType.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <comphelper/accessibleeventnotifier.hxx>
#include <comphelper/sequenceashashmap.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <unotools/accessiblerelationsethelper.hxx>
#include <com/sun/star/accessibility/AccessibleRelationType.hpp>
#include <vcl/unohelp.hxx>
#include <vcl/settings.hxx>
#include <i18nlangtag/languagetag.hxx>

#include <editeng/AccessibleParaManager.hxx>
#include <editeng/editeng.hxx>
#include <editeng/unoprnms.hxx>
#include <editeng/unoipset.hxx>
#include <editeng/outliner.hxx>
#include <editeng/unoedprx.hxx>
#include <editeng/unoedsrc.hxx>
#include <svl/eitem.hxx>


// Project-local header


#include <com/sun/star/beans/PropertyState.hpp>

#include <unopracc.hxx>
#include <editeng/AccessibleEditableTextPara.hxx>
#include "AccessibleHyperlink.hxx"
#include "AccessibleImageBullet.hxx"

#include <svtools/colorcfg.hxx>
#include <editeng/editrids.hrc>
#include <editeng/eerdll.hxx>
#include <editeng/numitem.hxx>
#include <memory>

using namespace ::com::sun::star;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::accessibility;


// AccessibleEditableTextPara implementation


namespace accessibility
{

static const SvxItemPropertySet* ImplGetSvxCharAndParaPropertiesSet()
{
    // PropertyMap for character and paragraph properties
    static const SfxItemPropertyMapEntry aPropMap[] =
    {
        SVX_UNOEDIT_OUTLINER_PROPERTIES,
        SVX_UNOEDIT_CHAR_PROPERTIES,
        SVX_UNOEDIT_PARA_PROPERTIES,
        SVX_UNOEDIT_NUMBERING_PROPERTY,
        { u"TextUserDefinedAttributes"_ustr,     EE_CHAR_XMLATTRIBS,     cppu::UnoType<css::container::XNameContainer>::get(),        0,     0},
        { u"ParaUserDefinedAttributes"_ustr,     EE_PARA_XMLATTRIBS,     cppu::UnoType<css::container::XNameContainer>::get(),        0,     0},
    };
    static SvxItemPropertySet aPropSet( aPropMap, EditEngine::GetGlobalItemPool() );
    return &aPropSet;
}

// #i27138# - add parameter <_pParaManager>
AccessibleEditableTextPara::AccessibleEditableTextPara(
                            uno::Reference< XAccessible > xParent,
                            const AccessibleParaManager* _pParaManager )
    : mnParagraphIndex( 0 ),
      mnIndexInParent( 0 ),
      mpEditSource( nullptr ),
      maEEOffset( 0, 0 ),
      mxParent(std::move( xParent )),
      mpParaManager( _pParaManager )
{

    // Create the state set.
    mnStateSet  = 0;

    // these are always on
    mnStateSet |= AccessibleStateType::MULTI_LINE;
    mnStateSet |= AccessibleStateType::FOCUSABLE;
    mnStateSet |= AccessibleStateType::VISIBLE;
    mnStateSet |= AccessibleStateType::SHOWING;
    mnStateSet |= AccessibleStateType::ENABLED;
    mnStateSet |= AccessibleStateType::SENSITIVE;
}

OUString AccessibleEditableTextPara::implGetText()
{
    return GetTextRange( 0, GetTextLen() );
}

css::lang::Locale AccessibleEditableTextPara::implGetLocale()
{
    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getLocale: paragraph index value overflow");

    // return locale of first character in the paragraph
    return LanguageTag(GetTextForwarder().GetLanguage( GetParagraphIndex(), 0 )).getLocale();
}

void AccessibleEditableTextPara::implGetSelection( sal_Int32& nStartIndex, sal_Int32& nEndIndex )
{
    sal_Int32 nStart, nEnd;

    if( GetSelection( nStart, nEnd ) )
    {
        nStartIndex = nStart;
        nEndIndex = nEnd;
    }
    else
    {
        // #102234# No exception, just set to 'invalid'
        nStartIndex = -1;
        nEndIndex = -1;
    }
}

void AccessibleEditableTextPara::implGetParagraphBoundary( const OUString& rText, css::i18n::Boundary& rBoundary, sal_Int32 /*nIndex*/ )
{
    SAL_INFO( "editeng", "AccessibleEditableTextPara::implGetParagraphBoundary: only a base implementation, ignoring the index" );

    rBoundary.startPos = 0;
    rBoundary.endPos = rText.getLength();
}

void AccessibleEditableTextPara::implGetLineBoundary( const OUString&, css::i18n::Boundary& rBoundary, sal_Int32 nIndex )
{
    SvxTextForwarder&   rCacheTF = GetTextForwarder();
    const sal_Int32     nParaIndex = GetParagraphIndex();

    DBG_ASSERT(nParaIndex >= 0,
               "AccessibleEditableTextPara::implGetLineBoundary: paragraph index value overflow");

    const sal_Int32 nTextLen = rCacheTF.GetTextLen( nParaIndex );

    CheckPosition(nIndex);

    rBoundary.startPos = rBoundary.endPos = -1;

    const sal_Int32 nLineCount=rCacheTF.GetLineCount( nParaIndex );

    if( nIndex == nTextLen )
    {
        // #i17014# Special-casing one-behind-the-end character
        if( nLineCount <= 1 )
            rBoundary.startPos = 0;
        else
            rBoundary.startPos = nTextLen - rCacheTF.GetLineLen( nParaIndex,
                                                                 nLineCount-1 );

        rBoundary.endPos = nTextLen;
    }
    else
    {
        // normal line search
        sal_Int32 nLine;
        sal_Int32 nCurIndex;
        for( nLine=0, nCurIndex=0; nLine<nLineCount; ++nLine )
        {
            nCurIndex += rCacheTF.GetLineLen( nParaIndex, nLine);

            if( nCurIndex > nIndex )
            {
                rBoundary.startPos = nCurIndex - rCacheTF.GetLineLen( nParaIndex, nLine);
                rBoundary.endPos = nCurIndex;
                break;
            }
        }
    }
}


void AccessibleEditableTextPara::SetIndexInParent( sal_Int32 nIndex )
{
    mnIndexInParent = nIndex;
}


void AccessibleEditableTextPara::SetParagraphIndex( sal_Int32 nIndex )
{
    sal_Int32 nOldIndex = mnParagraphIndex;

    mnParagraphIndex = nIndex;

    auto aChild( maImageBullet.get() );
    if( aChild.is() )
        aChild->SetParagraphIndex(mnParagraphIndex);

    try
    {
        if( nOldIndex != nIndex )
        {
            uno::Any aOldDesc;
            uno::Any aOldName;

            try
            {
                aOldDesc <<= getAccessibleDescription();
                aOldName <<= getAccessibleName();
            }
            catch (const uno::Exception&) // optional behaviour
            {
            }
            // index and therefore description changed
            FireEvent( AccessibleEventId::DESCRIPTION_CHANGED, uno::Any( getAccessibleDescription() ), aOldDesc );
            FireEvent( AccessibleEventId::NAME_CHANGED, uno::Any( getAccessibleName() ), aOldName );
        }
    }
    catch (const uno::Exception&) // optional behaviour
    {
    }
}


void SAL_CALL AccessibleEditableTextPara::dispose()
{
    rtl::Reference<AccessibleImageBullet> xBullet = maImageBullet.get();
    if (xBullet.is())
        xBullet->dispose();
    maImageBullet.clear();

    mxParent = nullptr;
    mpEditSource = nullptr;

    comphelper::OAccessible::dispose();
}

void AccessibleEditableTextPara::SetEditSource( SvxEditSourceAdapter* pEditSource )
{
    auto aChild( maImageBullet.get() );
    if( aChild.is() )
        aChild->SetEditSource(pEditSource);

    if( !pEditSource )
    {
        // going defunc
        UnSetState( AccessibleStateType::SHOWING );
        UnSetState( AccessibleStateType::VISIBLE );
        SetState( AccessibleStateType::INVALID );
        SetState( AccessibleStateType::DEFUNC );
    }
    mpEditSource = pEditSource;
    // #108900# Init last text content
    try
    {
        TextChanged();
    }
    catch (const uno::RuntimeException&)
    {
    }
}

ESelection AccessibleEditableTextPara::MakeSelection( sal_Int32 nStartEEIndex, sal_Int32 nEndEEIndex )
{
    // check overflow
    DBG_ASSERT(nStartEEIndex >= 0 &&
               nEndEEIndex >= 0 &&
               GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::MakeSelection: index value overflow");

    sal_Int32 nParaIndex = GetParagraphIndex();
    return ESelection(nParaIndex, nStartEEIndex, nParaIndex, nEndEEIndex);
}

ESelection AccessibleEditableTextPara::MakeSelection( sal_Int32 nEEIndex )
{
    return MakeSelection( nEEIndex, nEEIndex+1 );
}

ESelection AccessibleEditableTextPara::MakeCursor( sal_Int32 nEEIndex )
{
    return MakeSelection( nEEIndex, nEEIndex );
}

void AccessibleEditableTextPara::CheckIndex( sal_Int32 nIndex )
{
    if( nIndex < 0 || nIndex >= getCharacterCount() )
        throw lang::IndexOutOfBoundsException(u"AccessibleEditableTextPara: character index out of bounds"_ustr,
                                              getXWeak() );
}

void AccessibleEditableTextPara::CheckPosition( sal_Int32 nIndex )
{
    if( nIndex < 0 || nIndex > getCharacterCount() )
        throw lang::IndexOutOfBoundsException(u"AccessibleEditableTextPara: character position out of bounds"_ustr,
                                              getXWeak() );
}

void AccessibleEditableTextPara::CheckRange( sal_Int32 nStart, sal_Int32 nEnd )
{
    CheckPosition( nStart );
    CheckPosition( nEnd );
}

bool AccessibleEditableTextPara::GetSelection(sal_Int32 &nStartPos, sal_Int32 &nEndPos)
{
    ESelection aSelection;
    sal_Int32 nPara = GetParagraphIndex();
    if( !GetEditViewForwarder().GetSelection( aSelection ) )
        return false;

    if( aSelection.start.nPara < aSelection.end.nPara )
    {
        if( aSelection.start.nPara > nPara ||
            aSelection.end.nPara < nPara )
            return false;

        if( nPara == aSelection.start.nPara )
            nStartPos = aSelection.start.nIndex;
        else
            nStartPos = 0;

        if( nPara == aSelection.end.nPara )
            nEndPos = aSelection.end.nIndex;
        else
            nEndPos = GetTextLen();
    }
    else
    {
        if( aSelection.start.nPara < nPara ||
            aSelection.end.nPara > nPara )
            return false;

        if( nPara == aSelection.start.nPara )
            nStartPos = aSelection.start.nIndex;
        else
            nStartPos = GetTextLen();

        if( nPara == aSelection.end.nPara )
            nEndPos = aSelection.end.nIndex;
        else
            nEndPos = 0;
    }

    return true;
}

OUString AccessibleEditableTextPara::GetTextRange( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    return GetTextForwarder().GetText( MakeSelection(nStartIndex, nEndIndex) );
}

sal_Int32 AccessibleEditableTextPara::GetTextLen() const
{
    return GetTextForwarder().GetTextLen(GetParagraphIndex());
}

SvxEditSourceAdapter& AccessibleEditableTextPara::GetEditSource() const
{
    if( !mpEditSource )
        throw uno::RuntimeException(u"No edit source, object is defunct"_ustr,
                                     const_cast< AccessibleEditableTextPara* > (this)->getXWeak() );
    return *mpEditSource;
}

SvxAccessibleTextAdapter& AccessibleEditableTextPara::GetTextForwarder() const
{
    SvxEditSourceAdapter& rEditSource = GetEditSource();
    SvxAccessibleTextAdapter* pTextForwarder = rEditSource.GetTextForwarderAdapter();

    if( !pTextForwarder )
        throw uno::RuntimeException(u"Unable to fetch text forwarder, object is defunct"_ustr,
                                    const_cast< AccessibleEditableTextPara* > (this)->getXWeak() );

    if( !pTextForwarder->IsValid() )
        throw uno::RuntimeException(u"Text forwarder is invalid, object is defunct"_ustr,
                                    const_cast< AccessibleEditableTextPara* > (this)->getXWeak() );
    return *pTextForwarder;
}

SvxViewForwarder& AccessibleEditableTextPara::GetViewForwarder() const
{
    SvxEditSource& rEditSource = GetEditSource();
    SvxViewForwarder* pViewForwarder = rEditSource.GetViewForwarder();

    if( !pViewForwarder )
    {
        throw uno::RuntimeException(u"Unable to fetch view forwarder, object is defunct"_ustr,
                                    const_cast< AccessibleEditableTextPara* > (this)->getXWeak() );
    }

    if( !pViewForwarder->IsValid() )
        throw uno::RuntimeException(u"View forwarder is invalid, object is defunct"_ustr,
                                    const_cast< AccessibleEditableTextPara* > (this)->getXWeak() );
    return *pViewForwarder;
}

SvxAccessibleTextEditViewAdapter& AccessibleEditableTextPara::GetEditViewForwarder( bool bCreate ) const
{
    SvxEditSourceAdapter& rEditSource = GetEditSource();
    SvxAccessibleTextEditViewAdapter* pTextEditViewForwarder = rEditSource.GetEditViewForwarderAdapter( bCreate );

    if( !pTextEditViewForwarder )
    {
        if( bCreate )
            throw uno::RuntimeException(u"Unable to fetch view forwarder, object is defunct"_ustr,
                                        const_cast< AccessibleEditableTextPara* > (this)->getXWeak() );
        else
            throw uno::RuntimeException(u"No view forwarder, object not in edit mode"_ustr,
                                        const_cast< AccessibleEditableTextPara* > (this)->getXWeak() );
    }

    if( pTextEditViewForwarder->IsValid() )
        return *pTextEditViewForwarder;
    else
    {
        if( bCreate )
            throw uno::RuntimeException(u"View forwarder is invalid, object is defunct"_ustr,
                                        const_cast< AccessibleEditableTextPara* > (this)->getXWeak() );
        else
            throw uno::RuntimeException(u"View forwarder is invalid, object not in edit mode"_ustr,
                                        const_cast< AccessibleEditableTextPara* > (this)->getXWeak() );
    }
}

bool AccessibleEditableTextPara::HaveEditView() const
{
    SvxEditSource& rEditSource = GetEditSource();
    SvxEditViewForwarder* pViewForwarder = rEditSource.GetEditViewForwarder();

    if( !pViewForwarder )
        return false;

    if( !pViewForwarder->IsValid() )
        return false;

    return true;
}

bool AccessibleEditableTextPara::HaveChildren()
{
    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::HaveChildren: paragraph index value overflow");

    return GetTextForwarder().HaveImageBullet( GetParagraphIndex() );
}

sal_Int32 AccessibleEditableTextPara::GetBulletTextLength() const
{
    sal_Int32 nBulletLen = 0;
    EBulletInfo aBulletInfo = GetTextForwarder().GetBulletInfo(GetParagraphIndex());
    if (aBulletInfo.nParagraph != EE_PARA_MAX && aBulletInfo.bVisible)
        nBulletLen = aBulletInfo.aText.getLength();
    return nBulletLen;
}

tools::Rectangle AccessibleEditableTextPara::LogicToPixel( const tools::Rectangle& rRect, const MapMode& rMapMode, SvxViewForwarder const & rForwarder )
{
    // convert to screen coordinates
    return tools::Rectangle( rForwarder.LogicToPixel( rRect.TopLeft(), rMapMode ),
                      rForwarder.LogicToPixel( rRect.BottomRight(), rMapMode ) );
}


void AccessibleEditableTextPara::SetEEOffset( const Point& rOffset )
{
    auto aChild( maImageBullet.get() );
    if( aChild.is() )
        aChild->SetEEOffset(rOffset);

    maEEOffset = rOffset;
}

void AccessibleEditableTextPara::FireEvent(const sal_Int16 nEventId, const uno::Any& rNewValue, const uno::Any& rOldValue)
{
    NotifyAccessibleEvent(nEventId, rOldValue, rNewValue);
}

void AccessibleEditableTextPara::SetState( const sal_Int64 nStateId )
{
    if( !(mnStateSet & nStateId) )
    {
        mnStateSet |= nStateId;
        FireEvent( AccessibleEventId::STATE_CHANGED, uno::Any( nStateId ) );
    }
}

void AccessibleEditableTextPara::UnSetState( const sal_Int64 nStateId )
{
    if( mnStateSet & nStateId )
    {
        mnStateSet &= ~nStateId;
        FireEvent( AccessibleEventId::STATE_CHANGED, uno::Any(), uno::Any( nStateId ) );
    }
}

void AccessibleEditableTextPara::TextChanged()
{
    OUString aCurrentString( implGetText() );
    uno::Any aDeleted;
    uno::Any aInserted;
    if( OCommonAccessibleText::implInitTextChangedEvent( maLastTextString, aCurrentString,
                                                         aDeleted, aInserted) )
    {
        FireEvent( AccessibleEventId::TEXT_CHANGED, aInserted, aDeleted );
        maLastTextString = aCurrentString;
    }
}

bool AccessibleEditableTextPara::GetAttributeRun( sal_Int32& nStartIndex, sal_Int32& nEndIndex, sal_Int32 nIndex )
{
    DBG_ASSERT(nIndex >= 0,
               "AccessibleEditableTextPara::GetAttributeRun: index value overflow");

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getLocale: paragraph index value overflow");

    return GetTextForwarder().GetAttributeRun( nStartIndex,
                                               nEndIndex,
                                               GetParagraphIndex(),
                                               nIndex );
}

// XAccessibleContext
sal_Int64 SAL_CALL AccessibleEditableTextPara::getAccessibleChildCount()
{
    SolarMutexGuard aGuard;

    return HaveChildren() ? 1 : 0;
}

uno::Reference< XAccessible > SAL_CALL AccessibleEditableTextPara::getAccessibleChild( sal_Int64 i )
{
    SolarMutexGuard aGuard;

    if( !HaveChildren() )
        throw lang::IndexOutOfBoundsException(u"No children available"_ustr,
                                              getXWeak() );

    if( i != 0 )
        throw lang::IndexOutOfBoundsException(u"Invalid child index"_ustr,
                                              getXWeak() );

    auto aChild( maImageBullet.get() );

    if( !aChild.is() )
    {
        // there is no hard reference available, create object then
        aChild = new AccessibleImageBullet(this, i);

        aChild->SetEditSource( &GetEditSource() );
        aChild->SetParagraphIndex( GetParagraphIndex() );

        maImageBullet = aChild.get();
    }

    return aChild;
}

uno::Reference< XAccessible > SAL_CALL AccessibleEditableTextPara::getAccessibleParent()
{
    SAL_WARN_IF(!mxParent.is(), "editeng", "AccessibleEditableTextPara::getAccessibleParent: no frontend set, did somebody forgot to call AccessibleTextHelper::SetEventSource()?");

    return mxParent;
}

sal_Int64 SAL_CALL AccessibleEditableTextPara::getAccessibleIndexInParent()
{
    return mnIndexInParent;
}

sal_Int16 SAL_CALL AccessibleEditableTextPara::getAccessibleRole()
{
    return AccessibleRole::PARAGRAPH;
}

OUString SAL_CALL AccessibleEditableTextPara::getAccessibleDescription()
{
    return OUString();
}

OUString SAL_CALL AccessibleEditableTextPara::getAccessibleName()
{
    //See tdf#101003 before implementing a body
    return OUString();
}

uno::Reference< XAccessibleRelationSet > SAL_CALL AccessibleEditableTextPara::getAccessibleRelationSet()
{
    // #i27138# - provide relations CONTENT_FLOWS_FROM
    // and CONTENT_FLOWS_TO
    if ( mpParaManager )
    {
        rtl::Reference<utl::AccessibleRelationSetHelper> pAccRelSetHelper =
                                    new utl::AccessibleRelationSetHelper();
        sal_Int32 nMyParaIndex( GetParagraphIndex() );
        // relation CONTENT_FLOWS_FROM
        if ( nMyParaIndex > 0 &&
             mpParaManager->IsReferencable( nMyParaIndex - 1 ) )
        {
            uno::Sequence<uno::Reference<XAccessible>> aSequence
                { mpParaManager->GetChild( nMyParaIndex - 1 ).first.get() };
            AccessibleRelation aAccRel(AccessibleRelationType_CONTENT_FLOWS_FROM,
                                       aSequence );
            pAccRelSetHelper->AddRelation( aAccRel );
        }

        // relation CONTENT_FLOWS_TO
        if ( (nMyParaIndex + 1) < mpParaManager->GetNum() &&
             mpParaManager->IsReferencable( nMyParaIndex + 1 ) )
        {
            uno::Sequence<uno::Reference<XAccessible>> aSequence
                { mpParaManager->GetChild( nMyParaIndex + 1 ).first.get() };
            AccessibleRelation aAccRel(AccessibleRelationType_CONTENT_FLOWS_TO,
                                       aSequence );
            pAccRelSetHelper->AddRelation( aAccRel );
        }

        return pAccRelSetHelper;
    }
    else
    {
        // no relations, therefore empty
        return uno::Reference< XAccessibleRelationSet >();
    }
}

static uno::Sequence< OUString > const & getAttributeNames()
{
    static const uno::Sequence<OUString> aNames{
        u"CharColor"_ustr,
        u"CharContoured"_ustr,
        u"CharEmphasis"_ustr,
        u"CharEscapement"_ustr,
        u"CharFontName"_ustr,
        u"CharHeight"_ustr,
        u"CharPosture"_ustr,
        u"CharShadowed"_ustr,
        u"CharStrikeout"_ustr,
        u"CharCaseMap"_ustr,
        u"CharUnderline"_ustr,
        u"CharUnderlineColor"_ustr,
        u"CharWeight"_ustr,
        u"NumberingLevel"_ustr,
        u"NumberingRules"_ustr,
        u"ParaAdjust"_ustr,
        u"ParaBottomMargin"_ustr,
        u"ParaFirstLineIndent"_ustr,
        u"ParaLeftMargin"_ustr,
        u"ParaLineSpacing"_ustr,
        u"ParaRightMargin"_ustr,
        u"ParaTabStops"_ustr};

    return aNames;
}

namespace {

struct IndexCompare
{
    const uno::Sequence<beans::PropertyValue>& m_rValues;
    explicit IndexCompare(const uno::Sequence<beans::PropertyValue>& rValues)
        : m_rValues(rValues)
    {
    }
    bool operator() ( sal_Int32 a, sal_Int32 b ) const
    {
        return m_rValues[a].Name < m_rValues[b].Name;
    }
};

}
}

namespace
{
OUString GetFieldTypeNameFromField(EFieldInfo const &ree)
{
    OUString strFldType;
    sal_Int32 nFieldType = -1;
    if (ree.pFieldItem)
    {
        // So we get a field, check its type now.
        nFieldType = ree.pFieldItem->GetField()->GetClassId() ;
    }
    switch (nFieldType)
    {
        case text::textfield::Type::DATE:
        {
            const SvxDateField* pDateField = static_cast< const SvxDateField* >(ree.pFieldItem->GetField());
            if (pDateField)
            {
                if (pDateField->GetType() == SvxDateType::Fix)
                    strFldType = "date (fixed)";
                else if (pDateField->GetType() == SvxDateType::Var)
                    strFldType = "date (variable)";
            }
            break;
        }
        case text::textfield::Type::PAGE:
            strFldType = "page-number";
            break;
        //support the sheet name & pages fields
        case text::textfield::Type::PAGES:
            strFldType = "page-count";
            break;
        case text::textfield::Type::TABLE:
            strFldType = "sheet-name";
            break;
        //End
        case text::textfield::Type::TIME:
            strFldType = "time";
            break;
        case text::textfield::Type::EXTENDED_TIME:
        {
            const SvxExtTimeField* pTimeField = static_cast< const SvxExtTimeField* >(ree.pFieldItem->GetField());
            if (pTimeField)
            {
                if (pTimeField->GetType() == SvxTimeType::Fix)
                    strFldType = "time (fixed)";
                else if (pTimeField->GetType() == SvxTimeType::Var)
                    strFldType = "time (variable)";
            }
            break;
        }
        case text::textfield::Type::AUTHOR:
            strFldType = "author";
            break;
        case text::textfield::Type::EXTENDED_FILE:
        case text::textfield::Type::DOCINFO_TITLE:
            strFldType = "file name";
            break;
        case text::textfield::Type::DOCINFO_CUSTOM:
            strFldType = "custom document property";
            break;
        default:
            break;
    }
    return strFldType;
}
}

namespace accessibility
{
OUString AccessibleEditableTextPara::GetFieldTypeNameAtIndex(sal_Int32 nIndex)
{
    SvxAccessibleTextAdapter& rCacheTF = GetTextForwarder();
    //For field object info
    sal_Int32 nParaIndex = GetParagraphIndex();
    sal_Int32 nAllFieldLen = 0;
    std::vector<EFieldInfo> aFieldInfos = rCacheTF.GetFieldInfo(nParaIndex);
    for (const EFieldInfo& ree : aFieldInfos)
    {
        sal_Int32 reeBegin = ree.aPosition.nIndex + nAllFieldLen;
        sal_Int32 reeEnd = reeBegin + ree.aCurrentText.getLength();
        nAllFieldLen += (ree.aCurrentText.getLength() - 1);
        if (nIndex < reeBegin)
            break;
        if (nIndex < reeEnd)
            return GetFieldTypeNameFromField(ree);
    }
    return OUString();
}

sal_Int64 SAL_CALL AccessibleEditableTextPara::getAccessibleStateSet()
{
    SolarMutexGuard aGuard;

    // Create a copy of the state set and return it.

    sal_Int64 nParentStates = 0;
    if (getAccessibleParent().is())
    {
        uno::Reference<XAccessibleContext> xParentContext = getAccessibleParent()->getAccessibleContext();
        nParentStates = xParentContext->getAccessibleStateSet();
    }
    if (nParentStates & AccessibleStateType::EDITABLE)
    {
        mnStateSet |= AccessibleStateType::EDITABLE;
    }
    return mnStateSet;
}

lang::Locale SAL_CALL AccessibleEditableTextPara::getLocale()
{
    SolarMutexGuard aGuard;

    return implGetLocale();
}

// XAccessibleComponent

uno::Reference< XAccessible > SAL_CALL AccessibleEditableTextPara::getAccessibleAtPoint( const awt::Point& _aPoint )
{
    SolarMutexGuard aGuard;

    if( HaveChildren() )
    {
        // #103862# No longer need to make given position relative
        Point aPoint( _aPoint.X, _aPoint.Y );

        // respect EditEngine offset to surrounding shape/cell
        aPoint -= GetEEOffset();

        // convert to EditEngine coordinate system
        SvxTextForwarder& rCacheTF = GetTextForwarder();
        Point aLogPoint( GetViewForwarder().PixelToLogic( aPoint, rCacheTF.GetMapMode() ) );

        EBulletInfo aBulletInfo = rCacheTF.GetBulletInfo(GetParagraphIndex());

        if( aBulletInfo.nParagraph != EE_PARA_MAX &&
            aBulletInfo.bVisible &&
            aBulletInfo.nType == SVX_NUM_BITMAP )
        {
            tools::Rectangle aRect = aBulletInfo.aBounds;

            if( aRect.Contains( aLogPoint ) )
                return getAccessibleChild(0);
        }
    }

    // no children at all, or none at given position
    return uno::Reference< XAccessible >();
}

awt::Rectangle AccessibleEditableTextPara::implGetBounds()
{
    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::implGetBounds: index value overflow");

    SvxTextForwarder& rCacheTF = GetTextForwarder();
    tools::Rectangle aRect = rCacheTF.GetParaBounds( GetParagraphIndex() );

    // convert to screen coordinates
    tools::Rectangle aScreenRect = AccessibleEditableTextPara::LogicToPixel( aRect,
                                                                      rCacheTF.GetMapMode(),
                                                                      GetViewForwarder() );

    // offset from shape/cell
    Point aOffset = GetEEOffset();

    return awt::Rectangle( aScreenRect.Left() + aOffset.X(),
                           aScreenRect.Top() + aOffset.Y(),
                           aScreenRect.GetSize().Width(),
                           aScreenRect.GetSize().Height() );
}

void SAL_CALL AccessibleEditableTextPara::grabFocus(  )
{
    // set cursor to this paragraph
    setSelection(0,0);
}

sal_Int32 SAL_CALL AccessibleEditableTextPara::getForeground(  )
{
    // #104444# Added to XAccessibleComponent interface
    svtools::ColorConfig aColorConfig;
    Color nColor = aColorConfig.GetColorValue( svtools::FONTCOLOR ).nColor;
    return static_cast<sal_Int32>(nColor);
}

sal_Int32 SAL_CALL AccessibleEditableTextPara::getBackground(  )
{
    // #104444# Added to XAccessibleComponent interface
    Color aColor( Application::GetSettings().GetStyleSettings().GetWindowColor() );

    // the background is transparent
    aColor.SetAlpha(0);

    return static_cast<sal_Int32>( aColor );
}

// XAccessibleText
sal_Int32 SAL_CALL AccessibleEditableTextPara::getCaretPosition()
{
    SolarMutexGuard aGuard;

    if( !HaveEditView() )
        return -1;

    ESelection aSelection;
    if( GetEditViewForwarder().GetSelection( aSelection ) &&
        GetParagraphIndex() == aSelection.end.nPara )
    {
        // caret is always nEndPara,nEndPos
        EBulletInfo aBulletInfo = GetTextForwarder().GetBulletInfo(GetParagraphIndex());
        if( aBulletInfo.nParagraph != EE_PARA_MAX &&
            aBulletInfo.bVisible &&
            aBulletInfo.nType != SVX_NUM_BITMAP )
        {
            sal_Int32 nBulletLen = aBulletInfo.aText.getLength();
            if( aSelection.end.nIndex - nBulletLen >= 0 )
                return aSelection.end.nIndex - nBulletLen;
        }
        return aSelection.end.nIndex;
    }

    // not within this paragraph
    return -1;
}

sal_Bool SAL_CALL AccessibleEditableTextPara::setCaretPosition( sal_Int32 nIndex )
{
    return setSelection(nIndex, nIndex);
}

sal_Unicode SAL_CALL AccessibleEditableTextPara::getCharacter( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getCharacter: index value overflow");

    return OCommonAccessibleText::implGetCharacter( implGetText(), nIndex );
}

uno::Sequence< beans::PropertyValue > SAL_CALL AccessibleEditableTextPara::getCharacterAttributes( sal_Int32 nIndex, const css::uno::Sequence< OUString >& rRequestedAttributes )
{
    SolarMutexGuard aGuard;

    //Skip the bullet range to ignore the bullet text
    SvxTextForwarder& rCacheTF = GetTextForwarder();
    EBulletInfo aBulletInfo = rCacheTF.GetBulletInfo(GetParagraphIndex());
    if (aBulletInfo.bVisible)
        nIndex += aBulletInfo.aText.getLength();
    CheckIndex(nIndex); // may throw IndexOutOfBoundsException

    bool bSupplementalMode = false;
    uno::Sequence< OUString > aPropertyNames = rRequestedAttributes;
    if (!aPropertyNames.hasElements())
    {
        bSupplementalMode = true;
        aPropertyNames = getAttributeNames();
    }

    // get default attributes...
    ::comphelper::SequenceAsHashMap aPropHashMap( getDefaultAttributes( aPropertyNames ) );

    // ... and override them with the direct attributes from the specific position
    const uno::Sequence< beans::PropertyValue > aRunAttribs( getRunAttributes( nIndex, aPropertyNames ) );
    for (auto const& rRunAttrib : aRunAttribs)
    {
        aPropHashMap[ rRunAttrib.Name ] = rRunAttrib.Value; //!! should not only be the value !!
    }

    // get resulting sequence
    uno::Sequence< beans::PropertyValue > aRes;
    aPropHashMap >> aRes;

    // since SequenceAsHashMap ignores property handles and property state
    // we have to restore the property state here (property handles are
    // of no use to the accessibility API).
    for (beans::PropertyValue & rRes : asNonConstRange(aRes))
    {
        bool bIsDirectVal = false;
        for (auto const& rRunAttrib : aRunAttribs)
        {
            bIsDirectVal = rRes.Name == rRunAttrib.Name;
            if (bIsDirectVal)
                break;
        }
        rRes.Handle = -1;
        rRes.State  = bIsDirectVal ? PropertyState_DIRECT_VALUE : PropertyState_DEFAULT_VALUE;
    }
    if( bSupplementalMode )
    {
        _correctValues( aRes );
        // NumberingPrefix
        sal_Int32 nRes = aRes.getLength();
        aRes.realloc( nRes + 1 );
        beans::PropertyValue &rRes = aRes.getArray()[nRes];
        rRes.Name = "NumberingPrefix";
        OUString numStr;
        if (aBulletInfo.nType != SVX_NUM_CHAR_SPECIAL && aBulletInfo.nType != SVX_NUM_BITMAP)
            numStr = aBulletInfo.aText;
        rRes.Value <<= numStr;
        rRes.Handle = -1;
        rRes.State = PropertyState_DIRECT_VALUE;
        //For field object.
        OUString strFieldType = GetFieldTypeNameAtIndex(nIndex);
        if (!strFieldType.isEmpty())
        {
            nRes = aRes.getLength();
            aRes.realloc( nRes + 1 );
            beans::PropertyValue &rResField = aRes.getArray()[nRes];
            rResField.Name = "FieldType";
            rResField.Value <<= strFieldType.toAsciiLowerCase();
            rResField.Handle = -1;
            rResField.State = PropertyState_DIRECT_VALUE;
        }
        //sort property values
        // build sorted index array
        sal_Int32 nLength = aRes.getLength();
        std::vector<sal_Int32> indices(nLength);
        std::iota(indices.begin(), indices.end(), 0);
        std::sort(indices.begin(), indices.end(), IndexCompare(aRes));
        // create sorted sequences according to index array
        uno::Sequence<beans::PropertyValue> aNewValues( nLength );
        std::transform(indices.begin(), indices.end(), aNewValues.getArray(),
                       [&aRes](sal_Int32 index) -> const beans::PropertyValue& { return aRes[index]; });

        return aNewValues;
    }
    return aRes;
}

awt::Rectangle SAL_CALL AccessibleEditableTextPara::getCharacterBounds( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getCharacterBounds: index value overflow");

    // #108900# Have position semantics now for nIndex, as
    // one-past-the-end values are legal, too.
    CheckPosition( nIndex );

    SvxTextForwarder& rCacheTF = GetTextForwarder();
    tools::Rectangle aRect = rCacheTF.GetCharBounds(GetParagraphIndex(), nIndex);

    // convert to screen
    tools::Rectangle aScreenRect = AccessibleEditableTextPara::LogicToPixel( aRect,
                                                                      rCacheTF.GetMapMode(),
                                                                      GetViewForwarder() );
    // #109864# offset from parent (paragraph), but in screen
    // coordinates. This makes sure the internal text offset in
    // the outline view forwarder gets cancelled out here
    awt::Rectangle aParaRect( getBounds() );
    aScreenRect.Move( -aParaRect.X, -aParaRect.Y );

    // offset from shape/cell
    Point aOffset = GetEEOffset();

    return awt::Rectangle( aScreenRect.Left() + aOffset.X(),
                           aScreenRect.Top() + aOffset.Y(),
                           aScreenRect.GetSize().Width(),
                           aScreenRect.GetSize().Height() );
}

sal_Int32 SAL_CALL AccessibleEditableTextPara::getCharacterCount()
{
    SolarMutexGuard aGuard;

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getCharacterCount: index value overflow");

    return implGetText().getLength();
}

sal_Int32 SAL_CALL AccessibleEditableTextPara::getIndexAtPoint( const awt::Point& rPoint )
{
    SolarMutexGuard aGuard;

    sal_Int32 nPara;
    sal_Int32 nIndex;

    // offset from surrounding cell/shape
    Point aOffset( GetEEOffset() );
    Point aPoint( rPoint.X - aOffset.X(), rPoint.Y - aOffset.Y() );

    // convert to logical coordinates
    SvxTextForwarder& rCacheTF = GetTextForwarder();
    Point aLogPoint( GetViewForwarder().PixelToLogic( aPoint, rCacheTF.GetMapMode() ) );

    // re-offset to parent (paragraph)
    tools::Rectangle aParaRect = rCacheTF.GetParaBounds( GetParagraphIndex() );
    aLogPoint.Move( aParaRect.Left(), aParaRect.Top() );

    if( rCacheTF.GetIndexAtPoint( aLogPoint, nPara, nIndex ) &&
        GetParagraphIndex() == nPara )
    {
        // #102259# Double-check if we're _really_ on the given character
        try
        {
            awt::Rectangle aRect1( getCharacterBounds(nIndex) );
            tools::Rectangle aRect2( aRect1.X, aRect1.Y,
                              aRect1.Width + aRect1.X, aRect1.Height + aRect1.Y );
            if( aRect2.Contains( Point( rPoint.X, rPoint.Y ) ) )
                return nIndex;
            else
                return -1;
        }
        catch (const lang::IndexOutOfBoundsException&)
        {
            // #103927# Don't throw for invalid nIndex values
            return -1;
        }
    }
    else
    {
        // not within our paragraph
        return -1;
    }
}

OUString SAL_CALL AccessibleEditableTextPara::getSelectedText()
{
    SolarMutexGuard aGuard;

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getSelectedText: index value overflow");

    if( !HaveEditView() )
        return OUString();

    return OCommonAccessibleText::getSelectedText();
}

sal_Int32 SAL_CALL AccessibleEditableTextPara::getSelectionStart()
{
    SolarMutexGuard aGuard;

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getSelectionStart: index value overflow");

    if( !HaveEditView() )
        return -1;

    return OCommonAccessibleText::getSelectionStart();
}

sal_Int32 SAL_CALL AccessibleEditableTextPara::getSelectionEnd()
{
    SolarMutexGuard aGuard;

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getSelectionEnd: index value overflow");

    if( !HaveEditView() )
        return -1;

    return OCommonAccessibleText::getSelectionEnd();
}

sal_Bool SAL_CALL AccessibleEditableTextPara::setSelection( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    SolarMutexGuard aGuard;

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::setSelection: paragraph index value overflow");

    CheckRange(nStartIndex, nEndIndex);

    try
    {
        SvxEditViewForwarder& rCacheVF = GetEditViewForwarder( true );
        return rCacheVF.SetSelection( MakeSelection(nStartIndex, nEndIndex) );
    }
    catch (const uno::RuntimeException&)
    {
        return false;
    }
}

OUString SAL_CALL AccessibleEditableTextPara::getText()
{
    SolarMutexGuard aGuard;

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getText: paragraph index value overflow");

    return implGetText();
}

OUString SAL_CALL AccessibleEditableTextPara::getTextRange( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    SolarMutexGuard aGuard;

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getTextRange: paragraph index value overflow");

    return OCommonAccessibleText::implGetTextRange(implGetText(), nStartIndex, nEndIndex);
}

void AccessibleEditableTextPara::_correctValues( uno::Sequence< PropertyValue >& rValues)
{
    SvxTextForwarder& rCacheTF = GetTextForwarder();
    sal_Int32 nRes = rValues.getLength();
    beans::PropertyValue *pRes = rValues.getArray();
    for (sal_Int32 i = 0;  i < nRes;  ++i)
    {
        beans::PropertyValue &rRes = pRes[i];
        // Char color
        if (rRes.Name == "CharColor")
        {
            uno::Any &anyChar = rRes.Value;
            Color crChar;
            anyChar >>= crChar;
            if (COL_AUTO == crChar )
            {
                uno::Reference< css::accessibility::XAccessibleComponent > xComponent(mxParent,uno::UNO_QUERY);
                if (xComponent.is())
                {
                    uno::Reference< css::accessibility::XAccessibleContext > xContext(xComponent,uno::UNO_QUERY);
                    if (xContext->getAccessibleRole() == AccessibleRole::SHAPE
                        || xContext->getAccessibleRole() == AccessibleRole::TABLE_CELL)
                    {
                        anyChar <<= COL_BLACK;
                    }
                    else
                    {
                        Color cr(ColorTransparency, xComponent->getBackground());
                        crChar = cr.IsDark() ? COL_WHITE : COL_BLACK;
                        anyChar <<= crChar;
                    }
                }
            }
            continue;
        }
        // Underline
        if (rRes.Name == "CharUnderline")
        {
            continue;
        }
        // Underline color && Mis-spell
        if (rRes.Name == "CharUnderlineColor")
        {
            uno::Any &anyCharUnderLine = rRes.Value;
            Color crCharUnderLine;
            anyCharUnderLine >>= crCharUnderLine;
            if (COL_AUTO == crCharUnderLine )
            {
                uno::Reference< css::accessibility::XAccessibleComponent > xComponent(mxParent,uno::UNO_QUERY);
                if (xComponent.is())
                {
                    uno::Reference< css::accessibility::XAccessibleContext > xContext(xComponent,uno::UNO_QUERY);
                    if (xContext->getAccessibleRole() == AccessibleRole::SHAPE
                        || xContext->getAccessibleRole() == AccessibleRole::TABLE_CELL)
                    {
                        anyCharUnderLine <<= COL_BLACK;
                    }
                    else
                    {
                        Color cr(ColorTransparency, xComponent->getBackground());
                        crCharUnderLine = cr.IsDark() ? COL_WHITE : COL_BLACK;
                        anyCharUnderLine <<= crCharUnderLine;
                    }
                }
            }
            continue;
        }
        // NumberingLevel
        if (rRes.Name == "NumberingLevel")
        {
            if(rCacheTF.GetParaAttribs(GetParagraphIndex()).Get(EE_PARA_NUMBULLET).GetNumRule().GetLevelCount()==0)
            {
                rRes.Value <<= sal_Int16(-1);
                rRes.Handle = -1;
                rRes.State = PropertyState_DIRECT_VALUE;
            }
            else
            {
//                  SvxAccessibleTextPropertySet aPropSet( &GetEditSource(),
//                      ImplGetSvxCharAndParaPropertiesMap() );
                // MT IA2 TODO: Check if this is the correct replacement for ImplGetSvxCharAndParaPropertiesMap
                rtl::Reference< SvxAccessibleTextPropertySet > xPropSet( new SvxAccessibleTextPropertySet( &GetEditSource(), ImplGetSvxTextPortionSvxPropertySet() ) );

                xPropSet->SetSelection( MakeSelection( 0, GetTextLen() ) );
                rRes.Value = xPropSet->_getPropertyValue( rRes.Name, mnParagraphIndex );
                rRes.State = xPropSet->_getPropertyState( rRes.Name, mnParagraphIndex );
                rRes.Handle = -1;
            }
            continue;
        }
        // NumberingRules
        if (rRes.Name == "NumberingRules")
        {
            SfxItemSet aAttribs = rCacheTF.GetParaAttribs(GetParagraphIndex());
            bool bVis = aAttribs.Get( EE_PARA_BULLETSTATE ).GetValue();
            if(bVis)
            {
                rRes.Value <<= sal_Int16(-1);
                rRes.Handle = -1;
                rRes.State = PropertyState_DIRECT_VALUE;
            }
            else
            {
                // MT IA2 TODO: Check if this is the correct replacement for ImplGetSvxCharAndParaPropertiesMap
                rtl::Reference< SvxAccessibleTextPropertySet > xPropSet( new SvxAccessibleTextPropertySet( &GetEditSource(), ImplGetSvxTextPortionSvxPropertySet() ) );
                xPropSet->SetSelection( MakeSelection( 0, GetTextLen() ) );
                rRes.Value = xPropSet->_getPropertyValue( rRes.Name, mnParagraphIndex );
                rRes.State = xPropSet->_getPropertyState( rRes.Name, mnParagraphIndex );
                rRes.Handle = -1;
            }
            continue;
        }
    }
}
sal_Int32 AccessibleEditableTextPara::SkipField(sal_Int32 nIndex, bool bForward)
{
    sal_Int32 nParaIndex = GetParagraphIndex();
    SvxAccessibleTextAdapter& rCacheTF = GetTextForwarder();
    sal_Int32 nAllFieldLen = 0;
    sal_Int32 nFoundFieldIndex = -1;
    std::vector<EFieldInfo> aFieldInfos = rCacheTF.GetFieldInfo(nParaIndex);
    sal_Int32  reeBegin=0, reeEnd=0;
    sal_Int32 j = 0;
    for (const EFieldInfo& ree : aFieldInfos)
    {
        reeBegin = ree.aPosition.nIndex + nAllFieldLen;
        reeEnd = reeBegin + ree.aCurrentText.getLength();
        nAllFieldLen += (ree.aCurrentText.getLength() - 1);
        if (nIndex < reeBegin)
            break;
        if (!ree.pFieldItem)
            continue;
        if (nIndex < reeEnd)
        {
            if (ree.pFieldItem->GetField()->GetClassId() != text::textfield::Type::URL)
            {
                nFoundFieldIndex = j;
                break;
            }
        }
        j++;
    }
    if( nFoundFieldIndex >= 0  )
    {
        if( bForward )
            return reeEnd - 1;
        else
            return reeBegin;
    }
    return nIndex;
}
void AccessibleEditableTextPara::ExtendByField( css::accessibility::TextSegment& Segment )
{
    sal_Int32 nParaIndex = GetParagraphIndex();
    SvxAccessibleTextAdapter& rCacheTF = GetTextForwarder();
    std::vector<EFieldInfo> aFieldInfos = rCacheTF.GetFieldInfo(nParaIndex);
    sal_Int32 nAllFieldLen = 0;
    sal_Int32 nField = aFieldInfos.size(), nFoundFieldIndex = -1;
    sal_Int32  reeBegin=0, reeEnd=0;
    for (sal_Int32 j = 0; j < nField; ++j)
    {
        const EFieldInfo& ree = aFieldInfos[j];
        reeBegin  = ree.aPosition.nIndex + nAllFieldLen;
        reeEnd = reeBegin + ree.aCurrentText.getLength();
        nAllFieldLen += (ree.aCurrentText.getLength() - 1);
        if( reeBegin > Segment.SegmentEnd )
        {
            break;
        }
        if (!ree.pFieldItem)
            continue;
        if(  (Segment.SegmentEnd > reeBegin && Segment.SegmentEnd <= reeEnd) ||
              (Segment.SegmentStart >= reeBegin && Segment.SegmentStart < reeEnd)  )
        {
            if(ree.pFieldItem->GetField()->GetClassId() != text::textfield::Type::URL)
            {
                nFoundFieldIndex = j;
                break;
            }
        }
    }
    if( nFoundFieldIndex < 0 )
        return;

    bool bExtend = false;
    if( Segment.SegmentEnd < reeEnd )
    {
        Segment.SegmentEnd  = reeEnd;
        bExtend = true;
    }
    if( Segment.SegmentStart > reeBegin )
    {
        Segment.SegmentStart = reeBegin;
        bExtend = true;
    }
    if( !bExtend )
        return;

    //If there is a bullet before the field, should add the bullet length into the segment.
    EBulletInfo aBulletInfo = rCacheTF.GetBulletInfo(nParaIndex);
    sal_Int32 nBulletLen = aBulletInfo.aText.getLength();
    if (nBulletLen > 0)
    {
        Segment.SegmentEnd += nBulletLen;
        if (nFoundFieldIndex > 0)
            Segment.SegmentStart += nBulletLen;
        Segment.SegmentText = GetTextRange(Segment.SegmentStart, Segment.SegmentEnd);
        //After get the correct field name, should restore the offset value which don't contain the bullet.
        Segment.SegmentEnd -= nBulletLen;
        if (nFoundFieldIndex > 0)
            Segment.SegmentStart -= nBulletLen;
    }
    else
        Segment.SegmentText = GetTextRange(Segment.SegmentStart, Segment.SegmentEnd);
}

css::accessibility::TextSegment SAL_CALL AccessibleEditableTextPara::getTextAtIndex( sal_Int32 nIndex, sal_Int16 aTextType )
{
    SolarMutexGuard aGuard;

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getTextAtIndex: paragraph index value overflow");

    css::accessibility::TextSegment aResult;
    aResult.SegmentStart = -1;
    aResult.SegmentEnd = -1;

    switch( aTextType )
    {
        case AccessibleTextType::CHARACTER:
        case AccessibleTextType::WORD:
        {
            aResult = OCommonAccessibleText::getTextAtIndex( nIndex, aTextType );
            ExtendByField( aResult );
            break;
        }
        // Not yet handled by OCommonAccessibleText. Missing
        // implGetAttributeRunBoundary() method there
        case AccessibleTextType::ATTRIBUTE_RUN:
        {
            const sal_Int32 nTextLen = GetTextForwarder().GetTextLen( GetParagraphIndex() );

            if( nIndex == nTextLen )
            {
                // #i17014# Special-casing one-behind-the-end character
                aResult.SegmentStart = aResult.SegmentEnd = nTextLen;
            }
            else
            {
                sal_Int32 nStartIndex, nEndIndex;
                //For the bullet paragraph, the bullet string is ignored for IAText::attributes() function.
                SvxTextForwarder&   rCacheTF = GetTextForwarder();
                // MT IA2: Not used? sal_Int32 nBulletLen = 0;
                EBulletInfo aBulletInfo = rCacheTF.GetBulletInfo(GetParagraphIndex());
                if (aBulletInfo.bVisible)
                    nIndex += aBulletInfo.aText.getLength();
                if (nIndex != 0  && nIndex >= getCharacterCount())
                    nIndex = getCharacterCount()-1;
                CheckPosition(nIndex);
                if( GetAttributeRun(nStartIndex, nEndIndex, nIndex) )
                {
                    aResult.SegmentText = GetTextRange(nStartIndex, nEndIndex);
                    if (aBulletInfo.bVisible)
                    {
                        nStartIndex -= aBulletInfo.aText.getLength();
                        nEndIndex -= aBulletInfo.aText.getLength();
                    }
                    aResult.SegmentStart = nStartIndex;
                    aResult.SegmentEnd = nEndIndex;
                }
            }
            break;
        }
        case AccessibleTextType::LINE:
        {
            SvxTextForwarder&   rCacheTF = GetTextForwarder();
            sal_Int32           nParaIndex = GetParagraphIndex();
            CheckPosition(nIndex);
            if (nIndex != 0  && nIndex == getCharacterCount())
                --nIndex;
            sal_Int32 nLine, nLineCount=rCacheTF.GetLineCount( nParaIndex );
            sal_Int32 nCurIndex;
            //the problem is that rCacheTF.GetLineLen() will include the bullet length. But for the bullet line,
            //the text value doesn't contain the bullet characters. all of the bullet and numbering info are exposed
            //by the IAText::attributes(). So here must do special support for bullet line.
            sal_Int32 nBulletLen = 0;
            for( nLine=0, nCurIndex=0; nLine<nLineCount; ++nLine )
            {
                if (nLine == 0)
                {
                    EBulletInfo aBulletInfo = rCacheTF.GetBulletInfo( nParaIndex );
                    if (aBulletInfo.bVisible)
                    {
                        //in bullet or numbering;
                        nBulletLen = aBulletInfo.aText.getLength();
                    }
                }
                sal_Int32 nLineLen = rCacheTF.GetLineLen(nParaIndex, nLine);
                if (nLine == 0)
                    nCurIndex += nLineLen - nBulletLen;
                else
                    nCurIndex += nLineLen;
                if( nCurIndex > nIndex )
                {
                    if (nLine ==0)
                    {
                        aResult.SegmentStart = 0;
                        aResult.SegmentEnd = nCurIndex;
                        aResult.SegmentText = GetTextRange( aResult.SegmentStart, aResult.SegmentEnd + nBulletLen);
                        break;
                    }
                    else
                    {
                        aResult.SegmentStart = nCurIndex - nLineLen;
                        aResult.SegmentEnd = nCurIndex;
                        //aResult.SegmentText = GetTextRange( aResult.SegmentStart, aResult.SegmentEnd );
                        aResult.SegmentText = GetTextRange( aResult.SegmentStart + nBulletLen, aResult.SegmentEnd + nBulletLen);
                        break;
                    }
                }
            }
            break;
        }
        default:
            aResult = OCommonAccessibleText::getTextAtIndex( nIndex, aTextType );
            break;
    } /* end of switch( aTextType ) */

    return aResult;
}

css::accessibility::TextSegment SAL_CALL AccessibleEditableTextPara::getTextBeforeIndex( sal_Int32 nIndex, sal_Int16 aTextType )
{
    SolarMutexGuard aGuard;

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getTextBeforeIndex: paragraph index value overflow");

    css::accessibility::TextSegment aResult;
    aResult.SegmentStart = -1;
    aResult.SegmentEnd = -1;
    i18n::Boundary aBoundary;
    switch( aTextType )
    {
        // Not yet handled by OCommonAccessibleText. Missing
        // implGetAttributeRunBoundary() method there
        case AccessibleTextType::ATTRIBUTE_RUN:
        {
            const sal_Int32 nTextLen = GetTextForwarder().GetTextLen( GetParagraphIndex() );
            sal_Int32 nStartIndex, nEndIndex;

            if( nIndex == nTextLen )
            {
                // #i17014# Special-casing one-behind-the-end character
                if( nIndex > 0 &&
                    GetAttributeRun(nStartIndex, nEndIndex, nIndex-1) )
                {
                    aResult.SegmentText = GetTextRange(nStartIndex, nEndIndex);
                    aResult.SegmentStart = nStartIndex;
                    aResult.SegmentEnd = nEndIndex;
                }
            }
            else
            {
                if( GetAttributeRun(nStartIndex, nEndIndex, nIndex) )
                {
                    // already at the left border? If not, query
                    // one index further left
                    if( nStartIndex > 0 &&
                        GetAttributeRun(nStartIndex, nEndIndex, nStartIndex-1) )
                    {
                        aResult.SegmentText = GetTextRange(nStartIndex, nEndIndex);
                        aResult.SegmentStart = nStartIndex;
                        aResult.SegmentEnd = nEndIndex;
                    }
                }
            }
            break;
        }
        case AccessibleTextType::LINE:
        {
            SvxTextForwarder&   rCacheTF = GetTextForwarder();
            sal_Int32           nParaIndex = GetParagraphIndex();

            CheckPosition(nIndex);

            sal_Int32 nLine, nLineCount=rCacheTF.GetLineCount( nParaIndex );
            //the problem is that rCacheTF.GetLineLen() will include the bullet length. But for the bullet line,
            //the text value doesn't contain the bullet characters. all of the bullet and numbering info are exposed
            //by the IAText::attributes(). So here must do special support for bullet line.
            sal_Int32 nCurIndex=0, nLastIndex=0, nCurLineLen=0;
            sal_Int32 nLastLineLen = 0, nBulletLen = 0;
            // get the line before the line the index points into
            for( nLine=0, nCurIndex=0; nLine<nLineCount; ++nLine )
            {
                nLastIndex = nCurIndex;
                if (nLine == 0)
                {
                    EBulletInfo aBulletInfo = rCacheTF.GetBulletInfo(nParaIndex);
                    if (aBulletInfo.bVisible)
                    {
                        //in bullet or numbering;
                        nBulletLen = aBulletInfo.aText.getLength();
                    }
                }
                if (nLine == 1)
                    nLastLineLen = nCurLineLen - nBulletLen;
                else
                    nLastLineLen = nCurLineLen;
                nCurLineLen = rCacheTF.GetLineLen( nParaIndex, nLine);
                //nCurIndex += nCurLineLen;
                if (nLine == 0)
                    nCurIndex += nCurLineLen - nBulletLen;
                else
                    nCurIndex += nCurLineLen;

                //if( nCurIndex > nIndex &&
                //nLastIndex > nCurLineLen )
                if (nCurIndex > nIndex)
                {
                    if (nLine == 0)
                    {
                        break;
                    }
                    else if (nLine == 1)
                    {
                        aResult.SegmentStart = 0;
                        aResult.SegmentEnd = nLastIndex;
                        aResult.SegmentText = GetTextRange( aResult.SegmentStart, aResult.SegmentEnd + nBulletLen);
                        break;
                    }
                    else
                    {
                        //aResult.SegmentStart = nLastIndex - nCurLineLen;
                        aResult.SegmentStart = nLastIndex - nLastLineLen;
                        aResult.SegmentEnd = nLastIndex;
                        aResult.SegmentText = GetTextRange( aResult.SegmentStart + nBulletLen, aResult.SegmentEnd + nBulletLen);
                        break;
                    }
                }
            }

            break;
        }
        case AccessibleTextType::WORD:
        {
            nIndex = SkipField( nIndex, false);
            OUString sText( implGetText() );
            sal_Int32 nLength = sText.getLength();

            // get word at index
            implGetWordBoundary( sText, aBoundary, nIndex );


            //sal_Int32 curWordStart = aBoundary.startPos;
            //sal_Int32 preWordStart = curWordStart;
            sal_Int32 curWordStart , preWordStart;
            if( aBoundary.startPos == -1 || aBoundary.startPos > nIndex)
                curWordStart = preWordStart = nIndex;
            else
                curWordStart = preWordStart = aBoundary.startPos;

            // get previous word

            bool bWord = false;

            //while ( preWordStart > 0 && aBoundary.startPos == curWordStart)
            while ( (preWordStart >= 0 && !bWord ) || ( aBoundary.endPos > curWordStart ) )
            {
                preWordStart--;
                bWord = implGetWordBoundary( sText, aBoundary, preWordStart );
            }
            if ( bWord && implIsValidBoundary( aBoundary, nLength ) )
            {
                aResult.SegmentText = sText.copy( aBoundary.startPos, aBoundary.endPos - aBoundary.startPos );
                aResult.SegmentStart = aBoundary.startPos;
                aResult.SegmentEnd = aBoundary.endPos;
                ExtendByField( aResult );
            }
        }
        break;
        case AccessibleTextType::CHARACTER:
        {
            nIndex = SkipField( nIndex, false);
            aResult = OCommonAccessibleText::getTextBeforeIndex( nIndex, aTextType );
            ExtendByField( aResult );
            break;
        }
        default:
            aResult = OCommonAccessibleText::getTextBeforeIndex( nIndex, aTextType );
            break;
    } /* end of switch( aTextType ) */

    return aResult;
}

css::accessibility::TextSegment SAL_CALL AccessibleEditableTextPara::getTextBehindIndex( sal_Int32 nIndex, sal_Int16 aTextType )
{
    SolarMutexGuard aGuard;

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getTextBehindIndex: paragraph index value overflow");

    css::accessibility::TextSegment aResult;
    aResult.SegmentStart = -1;
    aResult.SegmentEnd = -1;
    i18n::Boundary aBoundary;
    switch( aTextType )
    {
        case AccessibleTextType::ATTRIBUTE_RUN:
        {
            sal_Int32 nStartIndex, nEndIndex;

            if( GetAttributeRun(nStartIndex, nEndIndex, nIndex) )
            {
                // already at the right border?
                if( nEndIndex < GetTextLen() )
                {
                    if( GetAttributeRun(nStartIndex, nEndIndex, nEndIndex) )
                    {
                        aResult.SegmentText = GetTextRange(nStartIndex, nEndIndex);
                        aResult.SegmentStart = nStartIndex;
                        aResult.SegmentEnd = nEndIndex;
                    }
                }
            }
            break;
        }

        case AccessibleTextType::LINE:
        {
            SvxTextForwarder&   rCacheTF = GetTextForwarder();
            sal_Int32           nParaIndex = GetParagraphIndex();

            CheckPosition(nIndex);

            sal_Int32 nLine, nLineCount = rCacheTF.GetLineCount( nParaIndex );
            sal_Int32 nCurIndex;
            //the problem is that rCacheTF.GetLineLen() will include the bullet length. But for the bullet line,
            //the text value doesn't contain the bullet characters. all of the bullet and numbering info are exposed
            //by the IAText::attributes(). So here must do special support for bullet line.
            sal_Int32 nBulletLen = 0;
            // get the line after the line the index points into
            for( nLine=0, nCurIndex=0; nLine<nLineCount; ++nLine )
            {
                if (nLine == 0)
                {
                    EBulletInfo aBulletInfo = rCacheTF.GetBulletInfo(nParaIndex);
                    if (aBulletInfo.bVisible)
                    {
                        //in bullet or numbering;
                        nBulletLen = aBulletInfo.aText.getLength();
                    }
                }
                sal_Int32 nLineLen = rCacheTF.GetLineLen( nParaIndex, nLine);

                if (nLine == 0)
                    nCurIndex += nLineLen - nBulletLen;
                else
                    nCurIndex += nLineLen;

                if( nCurIndex > nIndex &&
                    nLine < nLineCount-1 )
                {
                    aResult.SegmentStart = nCurIndex;
                    aResult.SegmentEnd = nCurIndex + rCacheTF.GetLineLen( nParaIndex, nLine+1);
                    aResult.SegmentText = GetTextRange( aResult.SegmentStart + nBulletLen, aResult.SegmentEnd + nBulletLen);
                    break;
                }
            }

            break;
        }
        case AccessibleTextType::WORD:
        {
            nIndex = SkipField( nIndex, true);
            OUString sText( implGetText() );
            sal_Int32 nLength = sText.getLength();

            // get word at index
            bool bWord = implGetWordBoundary( sText, aBoundary, nIndex );

            // real current world
            sal_Int32 nextWord = nIndex;
            //if( nIndex >= aBoundary.startPos && nIndex <= aBoundary.endPos )
            if( nIndex <= aBoundary.endPos )
            {
                nextWord =  aBoundary.endPos;
                if (nextWord < sText.getLength() && sText[nextWord] == u' ') nextWord++;
                bWord = implGetWordBoundary( sText, aBoundary, nextWord );
            }

            if ( bWord && implIsValidBoundary( aBoundary, nLength ) )
            {
                aResult.SegmentText = sText.copy( aBoundary.startPos, aBoundary.endPos - aBoundary.startPos );
                aResult.SegmentStart = aBoundary.startPos;
                aResult.SegmentEnd = aBoundary.endPos;

                // If the end position of aBoundary is inside a field, extend the result to the end of the field

                ExtendByField( aResult );
            }
        }
        break;

        case AccessibleTextType::CHARACTER:
        {
            nIndex = SkipField( nIndex, true);
            aResult = OCommonAccessibleText::getTextBehindIndex( nIndex, aTextType );
            ExtendByField( aResult );
            break;
        }
        default:
            aResult = OCommonAccessibleText::getTextBehindIndex( nIndex, aTextType );
            break;
    } /* end of switch( aTextType ) */

    return aResult;
}

sal_Bool SAL_CALL AccessibleEditableTextPara::copyText( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    SolarMutexGuard aGuard;

    try
    {
        SvxEditViewForwarder& rCacheVF = GetEditViewForwarder( true );
        GetTextForwarder();                                         // MUST be after GetEditViewForwarder(), see method docs

        bool aRetVal;

        DBG_ASSERT(GetParagraphIndex() >= 0,
                   "AccessibleEditableTextPara::copyText: index value overflow");

        CheckRange(nStartIndex, nEndIndex);

        //Because bullet may occupy one or more characters, the TextAdapter will include bullet to calculate the selection. Add offset to handle bullet
        const sal_Int32 nBulletLen = GetBulletTextLength();
        // save current selection
        ESelection aOldSelection;

        rCacheVF.GetSelection( aOldSelection );
        //rCacheVF.SetSelection( MakeSelection(nStartIndex, nEndIndex) );
        rCacheVF.SetSelection( MakeSelection(nStartIndex + nBulletLen, nEndIndex + nBulletLen) );
        aRetVal = rCacheVF.Copy();
        rCacheVF.SetSelection( aOldSelection ); // restore

        return aRetVal;
    }
    catch (const uno::RuntimeException&)
    {
        return false;
    }
}

sal_Bool SAL_CALL AccessibleEditableTextPara::scrollSubstringTo( sal_Int32, sal_Int32, AccessibleScrollType )
{
    return false;
}

// XAccessibleEditableText
sal_Bool SAL_CALL AccessibleEditableTextPara::cutText( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{

    SolarMutexGuard aGuard;

    try
    {
        SvxEditViewForwarder& rCacheVF = GetEditViewForwarder( true );
        SvxAccessibleTextAdapter& rCacheTF = GetTextForwarder();    // MUST be after GetEditViewForwarder(), see method docs

        DBG_ASSERT(GetParagraphIndex() >= 0,
                   "AccessibleEditableTextPara::cutText: index value overflow");

        CheckRange(nStartIndex, nEndIndex);

        // Because bullet may occupy one or more characters, the TextAdapter will include bullet to calculate the selection. Add offset to handle bullet
        const sal_Int32 nBulletLen = GetBulletTextLength();
        ESelection aSelection = MakeSelection (nStartIndex + nBulletLen, nEndIndex + nBulletLen);
        //if( !rCacheTF.IsEditable( MakeSelection(nStartIndex, nEndIndex) ) )
        if( !rCacheTF.IsEditable( aSelection ) )
            return false; // non-editable area selected

        // don't save selection, might become invalid after cut!
        //rCacheVF.SetSelection( MakeSelection(nStartIndex, nEndIndex) );
        rCacheVF.SetSelection( aSelection );

        return rCacheVF.Cut();
    }
    catch (const uno::RuntimeException&)
    {
        return false;
    }
}

sal_Bool SAL_CALL AccessibleEditableTextPara::pasteText( sal_Int32 nIndex )
{

    SolarMutexGuard aGuard;

    try
    {
        SvxEditViewForwarder& rCacheVF = GetEditViewForwarder( true );
        SvxAccessibleTextAdapter& rCacheTF = GetTextForwarder();    // MUST be after GetEditViewForwarder(), see method docs

        DBG_ASSERT(GetParagraphIndex() >= 0,
                   "AccessibleEditableTextPara::pasteText: index value overflow");

        CheckPosition(nIndex);

        // Because bullet may occupy one or more characters, the TextAdapter will include bullet to calculate the selection. Add offset to handle bullet
        const sal_Int32 nBulletLen = GetBulletTextLength();
        if( !rCacheTF.IsEditable( MakeSelection(nIndex + nBulletLen) ) )
            return false; // non-editable area selected

        // #104400# set empty selection (=> cursor) to given index
        //rCacheVF.SetSelection( MakeCursor(nIndex) );
        rCacheVF.SetSelection( MakeCursor(nIndex + nBulletLen) );

        return rCacheVF.Paste();
    }
    catch (const uno::RuntimeException&)
    {
        return false;
    }
}

sal_Bool SAL_CALL AccessibleEditableTextPara::deleteText( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{

    SolarMutexGuard aGuard;

    try
    {
        // #102710# Request edit view when doing changes
        // AccessibleEmptyEditSource relies on this behaviour
        GetEditViewForwarder( true );
        SvxAccessibleTextAdapter& rCacheTF = GetTextForwarder();    // MUST be after GetEditViewForwarder(), see method docs

        DBG_ASSERT(GetParagraphIndex() >= 0,
                   "AccessibleEditableTextPara::deleteText: index value overflow");

        CheckRange(nStartIndex, nEndIndex);

        // Because bullet may occupy one or more characters, the TextAdapter will include bullet to calculate the selection. Add offset to handle bullet
        const sal_Int32 nBulletLen = GetBulletTextLength();
        ESelection aSelection = MakeSelection (nStartIndex + nBulletLen, nEndIndex + nBulletLen);

        //if( !rCacheTF.IsEditable( MakeSelection(nStartIndex, nEndIndex) ) )
        if( !rCacheTF.IsEditable( aSelection ) )
            return false; // non-editable area selected

        //sal_Bool bRet = rCacheTF.Delete( MakeSelection(nStartIndex, nEndIndex) );
        bool bRet = rCacheTF.Delete( aSelection );

        GetEditSource().UpdateData();

        return bRet;
    }
    catch (const uno::RuntimeException&)
    {
        return false;
    }
}

sal_Bool SAL_CALL AccessibleEditableTextPara::insertText( const OUString& sText, sal_Int32 nIndex )
{

    SolarMutexGuard aGuard;

    try
    {
        // #102710# Request edit view when doing changes
        // AccessibleEmptyEditSource relies on this behaviour
        GetEditViewForwarder( true );
        SvxAccessibleTextAdapter& rCacheTF = GetTextForwarder();    // MUST be after GetEditViewForwarder(), see method docs

        DBG_ASSERT(GetParagraphIndex() >= 0,
                   "AccessibleEditableTextPara::insertText: index value overflow");

        CheckPosition(nIndex);

        // Because bullet may occupy one or more characters, the TextAdapter will include bullet to calculate the selection. Add offset to handle bullet
        const sal_Int32 nBulletLen = GetBulletTextLength();
        if( !rCacheTF.IsEditable( MakeSelection(nIndex + nBulletLen) ) )
            return false; // non-editable area selected

        // #104400# insert given text at empty selection (=> cursor)
        bool bRet = rCacheTF.InsertText( sText, MakeCursor(nIndex + nBulletLen) );

        rCacheTF.QuickFormatDoc();
        GetEditSource().UpdateData();

        return bRet;
    }
    catch (const uno::RuntimeException&)
    {
        return false;
    }
}

sal_Bool SAL_CALL AccessibleEditableTextPara::replaceText( sal_Int32 nStartIndex, sal_Int32 nEndIndex, const OUString& sReplacement )
{

    SolarMutexGuard aGuard;

    try
    {
        // #102710# Request edit view when doing changes
        // AccessibleEmptyEditSource relies on this behaviour
        GetEditViewForwarder( true );
        SvxAccessibleTextAdapter& rCacheTF = GetTextForwarder();    // MUST be after GetEditViewForwarder(), see method docs

        DBG_ASSERT(GetParagraphIndex() >= 0,
                   "AccessibleEditableTextPara::replaceText: index value overflow");

        CheckRange(nStartIndex, nEndIndex);

        // Because bullet may occupy one or more characters, the TextAdapter will include bullet to calculate the selection. Add offset to handle bullet
        const sal_Int32 nBulletLen = GetBulletTextLength();
        ESelection aSelection = MakeSelection (nStartIndex + nBulletLen, nEndIndex + nBulletLen);

        //if( !rCacheTF.IsEditable( MakeSelection(nStartIndex, nEndIndex) ) )
        if( !rCacheTF.IsEditable( aSelection ) )
            return false; // non-editable area selected

        // insert given text into given range => replace
        //sal_Bool bRet = rCacheTF.InsertText( sReplacement, MakeSelection(nStartIndex, nEndIndex) );
        bool bRet = rCacheTF.InsertText( sReplacement, aSelection );

        rCacheTF.QuickFormatDoc();
        GetEditSource().UpdateData();

        return bRet;
    }
    catch (const uno::RuntimeException&)
    {
        return false;
    }
}

sal_Bool SAL_CALL AccessibleEditableTextPara::setAttributes( sal_Int32 nStartIndex, sal_Int32 nEndIndex, const uno::Sequence< beans::PropertyValue >& aAttributeSet )
{

    SolarMutexGuard aGuard;

    try
    {
        // #102710# Request edit view when doing changes
        // AccessibleEmptyEditSource relies on this behaviour
        GetEditViewForwarder( true );
        SvxAccessibleTextAdapter& rCacheTF = GetTextForwarder();    // MUST be after GetEditViewForwarder(), see method docs
        sal_Int32 nPara = GetParagraphIndex();

        DBG_ASSERT(GetParagraphIndex() >= 0,
                   "AccessibleEditableTextPara::setAttributes: index value overflow");

        CheckRange(nStartIndex, nEndIndex);

        if( !rCacheTF.IsEditable( MakeSelection(nStartIndex, nEndIndex) ) )
            return false; // non-editable area selected

        // do the indices span the whole paragraph? Then use the outliner map
        // TODO: hold it as a member?
        rtl::Reference< SvxAccessibleTextPropertySet > xPropSet( new SvxAccessibleTextPropertySet( &GetEditSource(),
                                               0 == nStartIndex &&
                                               rCacheTF.GetTextLen(nPara) == nEndIndex ?
                                               ImplGetSvxUnoOutlinerTextCursorSvxPropertySet() :
                                               ImplGetSvxTextPortionSvxPropertySet() ) );

        xPropSet->SetSelection( MakeSelection(nStartIndex, nEndIndex) );

        // convert from PropertyValue to Any
        for(const beans::PropertyValue& rProp : aAttributeSet)
        {
            try
            {
                xPropSet->setPropertyValue(rProp.Name, rProp.Value);
            }
            catch (const uno::Exception&)
            {
                TOOLS_WARN_EXCEPTION( "dbaccess", "AccessibleEditableTextPara::setAttributes exception in setPropertyValue");
            }
        }

        rCacheTF.QuickFormatDoc();
        GetEditSource().UpdateData();

        return true;
    }
    catch (const uno::RuntimeException&)
    {
        return false;
    }
}

sal_Bool SAL_CALL AccessibleEditableTextPara::setText( const OUString& sText )
{

    SolarMutexGuard aGuard;

    return replaceText(0, getCharacterCount(), sText);
}

// XAccessibleTextAttributes
uno::Sequence< beans::PropertyValue > SAL_CALL AccessibleEditableTextPara::getDefaultAttributes(
        const uno::Sequence< OUString >& rRequestedAttributes )
{
    SolarMutexGuard aGuard;

    GetTextForwarder();

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getCharacterAttributes: index value overflow");

    // get XPropertySetInfo for paragraph attributes and
    // character attributes that span all the paragraphs text.
    rtl::Reference< SvxAccessibleTextPropertySet > xPropSet( new SvxAccessibleTextPropertySet( &GetEditSource(),
            ImplGetSvxCharAndParaPropertiesSet() ) );
    xPropSet->SetSelection( MakeSelection( 0, GetTextLen() ) );
    uno::Reference< beans::XPropertySetInfo > xPropSetInfo = xPropSet->getPropertySetInfo();
    if (!xPropSetInfo.is())
        throw uno::RuntimeException(u"Cannot query XPropertySetInfo"_ustr,
                    uno::Reference< uno::XInterface >
                    ( static_cast< XAccessible* > (this) ) );   // disambiguate hierarchy

    // build sequence of available properties to check
    uno::Sequence< beans::Property > aProperties;
    if (const sal_Int32 nLenReqAttr = rRequestedAttributes.getLength())
    {
        aProperties.realloc( nLenReqAttr );
        beans::Property *pProperties = aProperties.getArray();
        sal_Int32 nCurLen = 0;
        for (const OUString& rRequestedAttribute : rRequestedAttributes)
        {
            beans::Property aProp;
            try
            {
                aProp = xPropSetInfo->getPropertyByName( rRequestedAttribute );
            }
            catch (const beans::UnknownPropertyException&)
            {
                continue;
            }
            pProperties[nCurLen++] = std::move(aProp);
        }
        aProperties.realloc( nCurLen );
    }
    else
        aProperties = xPropSetInfo->getProperties();

    // build resulting sequence
    uno::Sequence< beans::PropertyValue > aOutSequence( aProperties.getLength() );
    beans::PropertyValue* pOutSequence = aOutSequence.getArray();
    sal_Int32 nOutLen = 0;
    for (const beans::Property& rProperty : aProperties)
    {
        // calling implementation functions:
        // _getPropertyState and _getPropertyValue (see below) to provide
        // the proper paragraph number when retrieving paragraph attributes
        PropertyState eState = xPropSet->_getPropertyState( rProperty.Name, mnParagraphIndex );
        if ( eState == PropertyState_AMBIGUOUS_VALUE )
        {
            OSL_FAIL( "ambiguous property value encountered" );
        }

        //if (eState == PropertyState_DIRECT_VALUE)
        // per definition all paragraph properties and all character
        // properties spanning the whole paragraph should be returned
        // and declared as default value
        {
            pOutSequence->Name      = rProperty.Name;
            pOutSequence->Handle    = rProperty.Handle;
            pOutSequence->Value     = xPropSet->_getPropertyValue( rProperty.Name, mnParagraphIndex );
            pOutSequence->State     = PropertyState_DEFAULT_VALUE;

            ++pOutSequence;
            ++nOutLen;
        }
    }
    aOutSequence.realloc( nOutLen );

    return aOutSequence;
}


uno::Sequence< beans::PropertyValue > SAL_CALL AccessibleEditableTextPara::getRunAttributes(
        sal_Int32 nIndex,
        const uno::Sequence< OUString >& rRequestedAttributes )
{

    SolarMutexGuard aGuard;

    GetTextForwarder();

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::getCharacterAttributes: index value overflow");

    if( getCharacterCount() > 0 )
        CheckIndex(nIndex);
    else
        CheckPosition(nIndex);

    rtl::Reference< SvxAccessibleTextPropertySet > xPropSet( new SvxAccessibleTextPropertySet( &GetEditSource(),
                                           ImplGetSvxCharAndParaPropertiesSet() ) );
    xPropSet->SetSelection( MakeSelection( nIndex ) );
    uno::Reference< beans::XPropertySetInfo > xPropSetInfo = xPropSet->getPropertySetInfo();
    if (!xPropSetInfo.is())
        throw uno::RuntimeException(u"Cannot query XPropertySetInfo"_ustr,
                                    uno::Reference< uno::XInterface >
                                    ( static_cast< XAccessible* > (this) ) );   // disambiguate hierarchy

    // build sequence of available properties to check
    uno::Sequence< beans::Property > aProperties;
    if (const sal_Int32 nLenReqAttr = rRequestedAttributes.getLength())
    {
        aProperties.realloc( nLenReqAttr );
        beans::Property *pProperties = aProperties.getArray();
        sal_Int32 nCurLen = 0;
        for (const OUString& rRequestedAttribute : rRequestedAttributes)
        {
            beans::Property aProp;
            try
            {
                aProp = xPropSetInfo->getPropertyByName( rRequestedAttribute );
            }
            catch (const beans::UnknownPropertyException&)
            {
                continue;
            }
            pProperties[ nCurLen++ ] = std::move(aProp);
        }
        aProperties.realloc( nCurLen );
    }
    else
        aProperties = xPropSetInfo->getProperties();

    // build resulting sequence
    uno::Sequence< beans::PropertyValue > aOutSequence( aProperties.getLength() );
    beans::PropertyValue* pOutSequence = aOutSequence.getArray();
    sal_Int32 nOutLen = 0;
    for (const beans::Property& rProperty : aProperties)
    {
        // calling 'regular' functions that will operate on the selection
        PropertyState eState = xPropSet->getPropertyState( rProperty.Name );
        if (eState == PropertyState_DIRECT_VALUE)
        {
            pOutSequence->Name      = rProperty.Name;
            pOutSequence->Handle    = rProperty.Handle;
            pOutSequence->Value     = xPropSet->getPropertyValue( rProperty.Name );
            pOutSequence->State     = eState;

            ++pOutSequence;
            ++nOutLen;
        }
    }
    aOutSequence.realloc( nOutLen );

    return aOutSequence;
}

// XAccessibleHypertext
::sal_Int32 SAL_CALL AccessibleEditableTextPara::getHyperLinkCount(  )
{
    SvxAccessibleTextAdapter& rT = GetTextForwarder();
    const sal_Int32 nPara = GetParagraphIndex();

    std::vector<EFieldInfo> aFieldInfos = rT.GetFieldInfo( nPara );
    sal_Int32 nHyperLinks = 0;
    sal_Int32 nFields = aFieldInfos.size();
    for (sal_Int32 n = 0; n < nFields; ++n)
    {
        if ( dynamic_cast<const SvxURLField* >(aFieldInfos[n].pFieldItem->GetField() ) != nullptr)
            nHyperLinks++;
    }
    return nHyperLinks;
}

css::uno::Reference< css::accessibility::XAccessibleHyperlink > SAL_CALL AccessibleEditableTextPara::getHyperLink( ::sal_Int32 nLinkIndex )
{
    rtl::Reference< AccessibleHyperlink > xRef;

    SvxAccessibleTextAdapter& rT = GetTextForwarder();
    const sal_Int32 nPara = GetParagraphIndex();

    sal_Int32 nHyperLink = 0;
    for (const EFieldInfo& rField : rT.GetFieldInfo( nPara ))
    {
        if ( dynamic_cast<const SvxURLField* >(rField.pFieldItem->GetField()) != nullptr )
        {
            if ( nHyperLink == nLinkIndex )
            {
                sal_Int32 nEEStart = rField.aPosition.nIndex;

                // Translate EE Index to accessible index
                sal_Int32 nStart = rT.CalcEditEngineIndex( nPara, nEEStart );
                sal_Int32 nEnd = nStart + rField.aCurrentText.getLength();
                xRef = new AccessibleHyperlink( rT, new SvxFieldItem( *rField.pFieldItem ), nStart, nEnd, rField.aCurrentText );
                break;
            }
            nHyperLink++;
        }
    }

    return xRef;
}

::sal_Int32 SAL_CALL AccessibleEditableTextPara::getHyperLinkIndex( ::sal_Int32 nCharIndex )
{
    const sal_Int32 nPara = GetParagraphIndex();
    SvxAccessibleTextAdapter& rT = GetTextForwarder();

    const sal_Int32 nEEIndex = rT.CalcEditEngineIndex( nPara, nCharIndex );
    sal_Int32 nHLIndex = -1; //i123620
    sal_Int32 nHyperLink = 0;
    for (const EFieldInfo & rField : rT.GetFieldInfo( nPara ))
    {
        if ( dynamic_cast<const SvxURLField* >( rField.pFieldItem->GetField() ) != nullptr)
        {
            if ( rField.aPosition.nIndex == nEEIndex )
            {
                nHLIndex = nHyperLink;
                break;
            }
            nHyperLink++;
        }
    }

    return nHLIndex;
}

// XAccessibleMultiLineText
sal_Int32 SAL_CALL AccessibleEditableTextPara::getLineNumberAtIndex( sal_Int32 nIndex )
{

    sal_Int32 nRes = -1;
    sal_Int32 nPara = GetParagraphIndex();

    SvxTextForwarder &rCacheTF = GetTextForwarder();
    const bool bValidPara = 0 <= nPara && nPara < rCacheTF.GetParagraphCount();
    DBG_ASSERT( bValidPara, "getLineNumberAtIndex: current paragraph index out of range" );
    if (bValidPara)
    {
        // we explicitly allow for the index to point at the character right behind the text
        if (0 > nIndex || nIndex > rCacheTF.GetTextLen( nPara ))
            throw lang::IndexOutOfBoundsException();
        nRes = rCacheTF.GetLineNumberAtIndex( nPara, nIndex );
    }
    return nRes;
}

// XAccessibleMultiLineText
css::accessibility::TextSegment SAL_CALL AccessibleEditableTextPara::getTextAtLineNumber( sal_Int32 nLineNo )
{

    css::accessibility::TextSegment aResult;
    sal_Int32 nPara = GetParagraphIndex();
    SvxTextForwarder &rCacheTF = GetTextForwarder();
    const bool bValidPara = 0 <= nPara && nPara < rCacheTF.GetParagraphCount();
    DBG_ASSERT( bValidPara, "getTextAtLineNumber: current paragraph index out of range" );
    if (bValidPara)
    {
        if (0 > nLineNo || nLineNo >= rCacheTF.GetLineCount( nPara ))
            throw lang::IndexOutOfBoundsException();
        sal_Int32 nStart = 0, nEnd = 0;
        rCacheTF.GetLineBoundaries( nStart, nEnd, nPara, nLineNo );
        if (nStart >= 0 && nEnd >=  0)
        {
            try
            {
                aResult.SegmentText     = getTextRange( nStart, nEnd );
                aResult.SegmentStart    = nStart;
                aResult.SegmentEnd      = nEnd;
            }
            catch (const lang::IndexOutOfBoundsException&)
            {
                // this is not the exception that should be raised in this function ...
                DBG_UNHANDLED_EXCEPTION("editeng");
            }
        }
    }
    return aResult;
}

// XAccessibleMultiLineText
css::accessibility::TextSegment SAL_CALL AccessibleEditableTextPara::getTextAtLineWithCaret(  )
{

    css::accessibility::TextSegment aResult;
    try
    {
        aResult = getTextAtLineNumber( getNumberOfLineWithCaret() );
    }
    catch (const lang::IndexOutOfBoundsException&)
    {
        // this one needs to be caught since this interface does not allow for it.
    }
    return aResult;
}

// XAccessibleMultiLineText
sal_Int32 SAL_CALL AccessibleEditableTextPara::getNumberOfLineWithCaret(  )
{

    sal_Int32 nRes = -1;
    try
    {
        nRes = getLineNumberAtIndex( getCaretPosition() );
    }
    catch (const lang::IndexOutOfBoundsException&)
    {
        // this one needs to be caught since this interface does not allow for it.
    }
    return nRes;
}

}  // end of namespace accessibility


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
