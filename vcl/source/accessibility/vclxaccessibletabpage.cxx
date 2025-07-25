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

#include <accessibility/vclxaccessibletabpage.hxx>

#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <com/sun/star/accessibility/AccessibleRole.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/datatransfer/clipboard/XClipboard.hpp>
#include <com/sun/star/datatransfer/clipboard/XFlushableClipboard.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <comphelper/accessiblecontexthelper.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <unotools/accessiblerelationsethelper.hxx>
#include <vcl/accessibility/characterattributeshelper.hxx>
#include <vcl/mnemonic.hxx>
#include <vcl/svapp.hxx>
#include <vcl/unohelp.hxx>
#include <vcl/unohelp2.hxx>
#include <vcl/tabctrl.hxx>
#include <vcl/tabpage.hxx>
#include <vcl/settings.hxx>
#include <i18nlangtag/languagetag.hxx>

using namespace ::com::sun::star::accessibility;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star;
using namespace ::comphelper;




VCLXAccessibleTabPage::VCLXAccessibleTabPage( TabControl* pTabControl, sal_uInt16 nPageId )
    :m_pTabControl( pTabControl )
    ,m_nPageId( nPageId )
{
    m_bFocused  = IsFocused();
    m_bSelected = IsSelected();
    m_sPageText = GetPageText();
}


VCLXAccessibleTabPage::~VCLXAccessibleTabPage()
{
}


bool VCLXAccessibleTabPage::IsFocused() const
{
    bool bFocused = false;

    if ( m_pTabControl && m_pTabControl->HasFocus() && m_pTabControl->GetCurPageId() == m_nPageId )
        bFocused = true;

    return bFocused;
}


bool VCLXAccessibleTabPage::IsSelected() const
{
    bool bSelected = false;

    if ( m_pTabControl && m_pTabControl->GetCurPageId() == m_nPageId )
        bSelected = true;

    return bSelected;
}


void VCLXAccessibleTabPage::SetFocused( bool bFocused )
{
    if ( m_bFocused != bFocused )
    {
        Any aOldValue, aNewValue;
        if ( m_bFocused )
            aOldValue <<= AccessibleStateType::FOCUSED;
        else
            aNewValue <<= AccessibleStateType::FOCUSED;
        m_bFocused = bFocused;
        NotifyAccessibleEvent( AccessibleEventId::STATE_CHANGED, aOldValue, aNewValue );
    }
}


void VCLXAccessibleTabPage::SetSelected( bool bSelected )
{
    if ( m_bSelected != bSelected )
    {
        Any aOldValue, aNewValue;
        if ( m_bSelected )
            aOldValue <<= AccessibleStateType::SELECTED;
        else
            aNewValue <<= AccessibleStateType::SELECTED;
        m_bSelected = bSelected;
        NotifyAccessibleEvent( AccessibleEventId::STATE_CHANGED, aOldValue, aNewValue );
    }
}


void VCLXAccessibleTabPage::SetPageText( const OUString& sPageText )
{
    Any aOldValue, aNewValue;
    if ( OCommonAccessibleText::implInitTextChangedEvent( m_sPageText, sPageText, aOldValue, aNewValue ) )
    {
        Any aOldName, aNewName;
        aOldName <<= m_sPageText;
        aNewName <<= sPageText;
        m_sPageText = sPageText;
        NotifyAccessibleEvent( AccessibleEventId::NAME_CHANGED, aOldName, aNewName );
        NotifyAccessibleEvent( AccessibleEventId::TEXT_CHANGED, aOldValue, aNewValue );
    }
}


OUString VCLXAccessibleTabPage::GetPageText()
{
    OUString sText;
    if ( m_pTabControl )
        sText = removeMnemonicFromString( m_pTabControl->GetPageText( m_nPageId ) );

    return sText;
}


void VCLXAccessibleTabPage::Update( bool bNew )
{
    if ( !m_pTabControl )
        return;

    TabPage* pTabPage = m_pTabControl->GetTabPage( m_nPageId );
    if ( !pTabPage )
        return;

    Reference< XAccessible > xChild( pTabPage->GetAccessible( bNew ) );
    if ( xChild.is() )
    {
        Any aOldValue, aNewValue;
        if ( bNew )
            aNewValue <<= xChild;
        else
            aOldValue <<= xChild;
        NotifyAccessibleEvent( AccessibleEventId::CHILD, aOldValue, aNewValue );
    }
}


void VCLXAccessibleTabPage::FillAccessibleStateSet( sal_Int64& rStateSet )
{
    rStateSet |= AccessibleStateType::ENABLED;
    rStateSet |= AccessibleStateType::SENSITIVE;

    rStateSet |= AccessibleStateType::FOCUSABLE;

    if ( IsFocused() )
        rStateSet |= AccessibleStateType::FOCUSED;

    rStateSet |= AccessibleStateType::VISIBLE;

    rStateSet |= AccessibleStateType::SHOWING;

    rStateSet |= AccessibleStateType::SELECTABLE;

    if ( IsSelected() )
        rStateSet |= AccessibleStateType::SELECTED;
}

// OAccessible

awt::Rectangle VCLXAccessibleTabPage::implGetBounds()
{
    awt::Rectangle aBounds( 0, 0, 0, 0 );

    if ( m_pTabControl )
        aBounds = vcl::unohelper::ConvertToAWTRect(m_pTabControl->GetTabBounds(m_nPageId));

    return aBounds;
}


// OCommonAccessibleText


OUString VCLXAccessibleTabPage::implGetText()
{
    return GetPageText();
}


lang::Locale VCLXAccessibleTabPage::implGetLocale()
{
    return Application::GetSettings().GetLanguageTag().getLocale();
}


void VCLXAccessibleTabPage::implGetSelection( sal_Int32& nStartIndex, sal_Int32& nEndIndex )
{
    nStartIndex = 0;
    nEndIndex = 0;
}


// XComponent


void VCLXAccessibleTabPage::disposing()
{
    comphelper::OAccessibleTextHelper::disposing();

    m_pTabControl = nullptr;
    m_sPageText.clear();
}


// XServiceInfo


OUString VCLXAccessibleTabPage::getImplementationName()
{
    return u"com.sun.star.comp.toolkit.AccessibleTabPage"_ustr;
}


sal_Bool VCLXAccessibleTabPage::supportsService( const OUString& rServiceName )
{
    return cppu::supportsService(this, rServiceName);
}


Sequence< OUString > VCLXAccessibleTabPage::getSupportedServiceNames()
{
    return { u"com.sun.star.awt.AccessibleTabPage"_ustr };
}

// XAccessibleContext


sal_Int64 VCLXAccessibleTabPage::getAccessibleChildCount()
{
    OExternalLockGuard aGuard( this );
    return implGetAccessibleChildCount();
}

sal_Int64 VCLXAccessibleTabPage::implGetAccessibleChildCount()
{
    sal_Int64 nCount = 0;
    if ( m_pTabControl )
    {
        TabPage* pTabPage = m_pTabControl->GetTabPage( m_nPageId );
        if ( pTabPage && pTabPage->IsVisible() )
            nCount = 1;
    }

    return nCount;
}


Reference< XAccessible > VCLXAccessibleTabPage::getAccessibleChild( sal_Int64 i )
{
    OExternalLockGuard aGuard( this );

    if ( i < 0 || i >= implGetAccessibleChildCount() )
        throw IndexOutOfBoundsException();

    rtl::Reference<comphelper::OAccessible> pChild;
    if ( m_pTabControl )
    {
        TabPage* pTabPage = m_pTabControl->GetTabPage( m_nPageId );
        if ( pTabPage && pTabPage->IsVisible() )
            pChild = pTabPage->GetAccessible();
    }

    return pChild;
}


Reference< XAccessible > VCLXAccessibleTabPage::getAccessibleParent(  )
{
    OExternalLockGuard aGuard( this );

    Reference< XAccessible > xParent;
    if ( m_pTabControl )
        xParent = m_pTabControl->GetAccessible();

    return xParent;
}


sal_Int64 VCLXAccessibleTabPage::getAccessibleIndexInParent(  )
{
    OExternalLockGuard aGuard( this );

    sal_Int64 nIndexInParent = -1;
    if ( m_pTabControl )
        nIndexInParent = m_pTabControl->GetPagePos( m_nPageId );

    return nIndexInParent;
}

sal_Int16 VCLXAccessibleTabPage::getAccessibleRole()
{
    OExternalLockGuard aGuard( this );

    return AccessibleRole::PAGE_TAB;
}

OUString VCLXAccessibleTabPage::getAccessibleDescription()
{
    OExternalLockGuard aGuard( this );

    OUString sDescription;
    if ( m_pTabControl )
        sDescription = m_pTabControl->GetAccessibleDescription( m_nPageId );

    return sDescription;
}

OUString VCLXAccessibleTabPage::getAccessibleName()
{
    OExternalLockGuard aGuard( this );

    OUString sName;
    if ( m_pTabControl )
        sName = m_pTabControl->GetAccessibleName( m_nPageId );

    return sName;
}

Reference< XAccessibleRelationSet > VCLXAccessibleTabPage::getAccessibleRelationSet(  )
{
    OExternalLockGuard aGuard( this );

    return new utl::AccessibleRelationSetHelper;
}


sal_Int64 VCLXAccessibleTabPage::getAccessibleStateSet(  )
{
    OExternalLockGuard aGuard( this );

    sal_Int64 nStateSet = 0;

    if (isAlive())
        FillAccessibleStateSet( nStateSet );
    else
        nStateSet |= AccessibleStateType::DEFUNC;

    return nStateSet;
}


Locale VCLXAccessibleTabPage::getLocale(  )
{
    OExternalLockGuard aGuard( this );

    return Application::GetSettings().GetLanguageTag().getLocale();
}


// XAccessibleComponent


Reference< XAccessible > VCLXAccessibleTabPage::getAccessibleAtPoint( const awt::Point& rPoint )
{
    OExternalLockGuard aGuard( this );

    for ( sal_Int64 i = 0, nCount = getAccessibleChildCount(); i < nCount; ++i )
    {
        Reference< XAccessible > xAcc = getAccessibleChild( i );
        if ( xAcc.is() )
        {
            Reference< XAccessibleComponent > xComp( xAcc->getAccessibleContext(), UNO_QUERY );
            if ( xComp.is() )
            {
                tools::Rectangle aRect = vcl::unohelper::ConvertToVCLRect(xComp->getBounds());
                Point aPos = vcl::unohelper::ConvertToVCLPoint(rPoint);
                if ( aRect.Contains( aPos ) )
                {
                    return xAcc;
                }
            }
        }
    }

    return nullptr;
}


void VCLXAccessibleTabPage::grabFocus(  )
{
    OExternalLockGuard aGuard( this );

    if ( m_pTabControl )
    {
        m_pTabControl->SelectTabPage( m_nPageId );
        m_pTabControl->GrabFocus();
    }
}


sal_Int32 VCLXAccessibleTabPage::getForeground( )
{
    OExternalLockGuard aGuard( this );

    sal_Int32 nColor = 0;
    Reference< XAccessible > xParent = getAccessibleParent();
    if ( xParent.is() )
    {
        Reference< XAccessibleComponent > xParentComp( xParent->getAccessibleContext(), UNO_QUERY );
        if ( xParentComp.is() )
            nColor = xParentComp->getForeground();
    }

    return nColor;
}


sal_Int32 VCLXAccessibleTabPage::getBackground(  )
{
    OExternalLockGuard aGuard( this );

    sal_Int32 nColor = 0;
    Reference< XAccessible > xParent = getAccessibleParent();
    if ( xParent.is() )
    {
        Reference< XAccessibleComponent > xParentComp( xParent->getAccessibleContext(), UNO_QUERY );
        if ( xParentComp.is() )
            nColor = xParentComp->getBackground();
    }

    return nColor;
}

// XAccessibleText

OUString VCLXAccessibleTabPage::getText()
{
    OExternalLockGuard aGuard( this );

    return GetPageText();
}

OUString VCLXAccessibleTabPage::getTextRange(sal_Int32 nStartIndex, sal_Int32 nEndIndex)
{
    OExternalLockGuard aGuard( this );

    return OCommonAccessibleText::implGetTextRange(GetPageText(), nStartIndex, nEndIndex);
}

sal_Unicode VCLXAccessibleTabPage::getCharacter( sal_Int32 nIndex )
{
     OExternalLockGuard aGuard( this );

     return OCommonAccessibleText::implGetCharacter( GetPageText(), nIndex );
}

sal_Int32 VCLXAccessibleTabPage::getCharacterCount()
{
    return GetPageText().getLength();
}

sal_Int32 VCLXAccessibleTabPage::getCaretPosition()
{
    OExternalLockGuard aGuard( this );

    return -1;
}


sal_Bool VCLXAccessibleTabPage::setCaretPosition( sal_Int32 nIndex )
{
    OExternalLockGuard aGuard( this );

    if ( !implIsValidRange( nIndex, nIndex, GetPageText().getLength() ) )
        throw IndexOutOfBoundsException();

    return false;
}


Sequence< PropertyValue > VCLXAccessibleTabPage::getCharacterAttributes( sal_Int32 nIndex, const Sequence< OUString >& aRequestedAttributes )
{
    OExternalLockGuard aGuard( this );

    Sequence< PropertyValue > aValues;
    OUString sText( GetPageText() );

    if ( !implIsValidIndex( nIndex, sText.getLength() ) )
        throw IndexOutOfBoundsException();

    if ( m_pTabControl )
    {
        vcl::Font aFont = m_pTabControl->GetFont();
        sal_Int32 nBackColor = getBackground();
        sal_Int32 nColor = getForeground();
        aValues = CharacterAttributesHelper( aFont, nBackColor, nColor )
            .GetCharacterAttributes( aRequestedAttributes );
    }

    return aValues;
}


awt::Rectangle VCLXAccessibleTabPage::getCharacterBounds( sal_Int32 nIndex )
{
    OExternalLockGuard aGuard( this );

    if ( !implIsValidIndex( nIndex, GetPageText().getLength() ) )
        throw IndexOutOfBoundsException();

    awt::Rectangle aBounds( 0, 0, 0, 0 );
    if ( m_pTabControl )
    {
        tools::Rectangle aPageRect = m_pTabControl->GetTabBounds( m_nPageId );
        tools::Rectangle aCharRect; // m_pTabControl->GetCharacterBounds( m_nPageId, nIndex );
        aCharRect.Move( -aPageRect.Left(), -aPageRect.Top() );
        aBounds = vcl::unohelper::ConvertToAWTRect(aCharRect);
    }

    return aBounds;
}


sal_Int32 VCLXAccessibleTabPage::getIndexAtPoint( const awt::Point& /*aPoint*/ )
{
    OExternalLockGuard aGuard( this );

    sal_Int32 nIndex = -1;
//    if ( m_pTabControl )
//    {
//        sal_uInt16 nPageId = 0;
//        tools::Rectangle aPageRect = m_pTabControl->GetTabBounds( m_nPageId );
//        Point aPnt( vcl::unohelper::ConvertToVCLPoint( aPoint ) );
//        aPnt += aPageRect.TopLeft();
//        sal_Int32 nI = m_pTabControl->GetIndexForPoint( aPnt, nPageId );
//        if ( nI != -1 && m_nPageId == nPageId )
//            nIndex = nI;
//    }

    return nIndex;
}


sal_Bool VCLXAccessibleTabPage::setSelection( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    OExternalLockGuard aGuard( this );

    if ( !implIsValidRange( nStartIndex, nEndIndex, GetPageText().getLength() ) )
        throw IndexOutOfBoundsException();

    return false;
}


sal_Bool VCLXAccessibleTabPage::copyText( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    OExternalLockGuard aGuard( this );

    bool bReturn = false;

    if ( m_pTabControl )
    {
        Reference< datatransfer::clipboard::XClipboard > xClipboard = m_pTabControl->GetClipboard();
        if ( xClipboard.is() )
        {
            OUString sText( implGetTextRange( GetPageText(), nStartIndex, nEndIndex ) );

            rtl::Reference<vcl::unohelper::TextDataObject> pDataObj = new vcl::unohelper::TextDataObject( sText );

            SolarMutexReleaser aReleaser;
            xClipboard->setContents( pDataObj, nullptr );

            Reference< datatransfer::clipboard::XFlushableClipboard > xFlushableClipboard( xClipboard, uno::UNO_QUERY );
            if( xFlushableClipboard.is() )
                xFlushableClipboard->flushClipboard();

            bReturn = true;
        }
    }

    return bReturn;
}

sal_Bool VCLXAccessibleTabPage::scrollSubstringTo( sal_Int32, sal_Int32, AccessibleScrollType )
{
    return false;
}


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
