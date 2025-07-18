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

#include <accessibility/accessiblemenuitemcomponent.hxx>

#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <com/sun/star/accessibility/AccessibleRole.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <unotools/accessiblerelationsethelper.hxx>
#include <comphelper/accessiblecontexthelper.hxx>
#include <comphelper/accessibletexthelper.hxx>
#include <vcl/svapp.hxx>
#include <vcl/unohelp.hxx>
#include <vcl/window.hxx>
#include <vcl/menu.hxx>
#include <vcl/mnemonic.hxx>
#include <vcl/settings.hxx>
#include <i18nlangtag/languagetag.hxx>

using namespace ::com::sun::star::accessibility;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star;
using namespace ::comphelper;




OAccessibleMenuItemComponent::OAccessibleMenuItemComponent( Menu* pParent, sal_uInt16 nItemPos, Menu* pMenu )
    :OAccessibleMenuBaseComponent( pMenu )
    ,m_pParent( pParent )
    ,m_nItemPos( nItemPos )
{
    m_sAccessibleName = GetAccessibleName();
    m_sItemText = GetItemText();
}

OAccessibleMenuItemComponent::~OAccessibleMenuItemComponent()
{
}

bool OAccessibleMenuItemComponent::IsEnabled()
{
    OExternalLockGuard aGuard( this );

    bool bEnabled = false;
    if ( m_pParent )
        bEnabled = m_pParent->IsItemEnabled( m_pParent->GetItemId( m_nItemPos ) );

    return bEnabled;
}


bool OAccessibleMenuItemComponent::IsVisible()
{
    bool bVisible = false;

    if ( m_pParent )
        bVisible = m_pParent->IsItemPosVisible( m_nItemPos );

    return bVisible;
}


void OAccessibleMenuItemComponent::Select()
{
    // open the parent menu
    Reference< XAccessible > xParent( getAccessibleParent() );
    if ( xParent.is() )
    {
        OAccessibleMenuBaseComponent* pComp = static_cast< OAccessibleMenuBaseComponent* >( xParent.get() );
        if ( pComp && pComp->getAccessibleRole() == AccessibleRole::MENU && !pComp->IsPopupMenuOpen() )
            pComp->Click();
    }

    // highlight the menu item
    if ( m_pParent )
        m_pParent->HighlightItem( m_nItemPos );
}


void OAccessibleMenuItemComponent::DeSelect()
{
    if ( m_pParent && IsSelected() )
        m_pParent->DeHighlight();
}


void OAccessibleMenuItemComponent::Click()
{
    // open the parent menu
    Reference< XAccessible > xParent( getAccessibleParent() );
    if ( xParent.is() )
    {
        OAccessibleMenuBaseComponent* pComp = static_cast< OAccessibleMenuBaseComponent* >( xParent.get() );
        if ( pComp && pComp->getAccessibleRole() == AccessibleRole::MENU && !pComp->IsPopupMenuOpen() )
            pComp->Click();
    }

    // click the menu item
    if ( !m_pParent )
        return;

    vcl::Window* pWindow = m_pParent->GetWindow();
    if ( !pWindow )
        return;

    // #102438# Menu items are not selectable
    // Popup menus are executed asynchronously, triggered by a timer.
    // As Menu::SelectItem only works, if the corresponding menu window is
    // already created, we have to set the menu delay to 0, so
    // that the popup menus are executed synchronously.
    AllSettings aSettings = pWindow->GetSettings();
    MouseSettings aMouseSettings = aSettings.GetMouseSettings();
    sal_Int32 nDelay = aMouseSettings.GetMenuDelay();
    aMouseSettings.SetMenuDelay( 0 );
    aSettings.SetMouseSettings( aMouseSettings );
    pWindow->SetSettings( aSettings );

    m_pParent->SelectItem( m_pParent->GetItemId( m_nItemPos ) );

    // meanwhile the window pointer may be invalid
    pWindow = m_pParent->GetWindow();
    if ( pWindow )
    {
        // set the menu delay back to the old value
        aSettings = pWindow->GetSettings();
        aMouseSettings = aSettings.GetMouseSettings();
        aMouseSettings.SetMenuDelay( nDelay );
        aSettings.SetMouseSettings( aMouseSettings );
        pWindow->SetSettings( aSettings );
    }
}


void OAccessibleMenuItemComponent::SetItemPos( sal_uInt16 nItemPos )
{
    m_nItemPos = nItemPos;
}


void OAccessibleMenuItemComponent::SetAccessibleName( const OUString& sAccessibleName )
{
    if ( m_sAccessibleName != sAccessibleName )
    {
        Any aOldValue, aNewValue;
        aOldValue <<= m_sAccessibleName;
        aNewValue <<= sAccessibleName;
        m_sAccessibleName = sAccessibleName;
        NotifyAccessibleEvent( AccessibleEventId::NAME_CHANGED, aOldValue, aNewValue );
    }
}


OUString OAccessibleMenuItemComponent::GetAccessibleName()
{
    OUString sName;
    if ( m_pParent )
    {
        sal_uInt16 nItemId = m_pParent->GetItemId( m_nItemPos );
        sName = m_pParent->GetAccessibleName( nItemId );
        if ( sName.isEmpty() )
            sName = m_pParent->GetItemText( nItemId );
        sName = removeMnemonicFromString( sName );
#if defined(_WIN32)
        if ( m_pParent->GetAccelKey( nItemId ).GetName().getLength() )
            sName += "\t" + m_pParent->GetAccelKey(nItemId).GetName();
#endif
    }

    return sName;
}


void OAccessibleMenuItemComponent::SetItemText( const OUString& sItemText )
{
    Any aOldValue, aNewValue;
    if ( OCommonAccessibleText::implInitTextChangedEvent( m_sItemText, sItemText, aOldValue, aNewValue ) )
    {
        m_sItemText = sItemText;
        NotifyAccessibleEvent( AccessibleEventId::TEXT_CHANGED, aOldValue, aNewValue );
    }
}


OUString OAccessibleMenuItemComponent::GetItemText()
{
    OUString sText;
    if ( m_pParent )
        sText = removeMnemonicFromString( m_pParent->GetItemText( m_pParent->GetItemId( m_nItemPos ) ) );

    return sText;
}


void OAccessibleMenuItemComponent::FillAccessibleStateSet( sal_Int64& rStateSet )
{
    bool bEnabled = IsEnabled();
    if ( bEnabled )
    {
        rStateSet |= AccessibleStateType::ENABLED;
        rStateSet |= AccessibleStateType::SENSITIVE;
    }

    if ( IsVisible() )
    {
        rStateSet |= AccessibleStateType::SHOWING;
        if( !IsMenuHideDisabledEntries() || bEnabled )
            rStateSet |= AccessibleStateType::VISIBLE;
    }
    rStateSet |= AccessibleStateType::OPAQUE;
}

// OAccessible

awt::Rectangle OAccessibleMenuItemComponent::implGetBounds()
{
    awt::Rectangle aBounds( 0, 0, 0, 0 );

    if ( m_pParent )
    {
        // get bounding rectangle of the item relative to the containing window
        aBounds = vcl::unohelper::ConvertToAWTRect(m_pParent->GetBoundingRectangle(m_nItemPos));

        // get position of containing window in screen coordinates
        vcl::Window* pWindow = m_pParent->GetWindow();
        if ( pWindow )
        {
            AbsoluteScreenPixelRectangle aRect = pWindow->GetWindowExtentsAbsolute();
            awt::Point aWindowScreenLoc = vcl::unohelper::ConvertToAWTPoint(aRect.TopLeft());

            // get position of accessible parent in screen coordinates
            Reference< XAccessible > xParent = getAccessibleParent();
            if ( xParent.is() )
            {
                Reference< XAccessibleComponent > xParentComponent( xParent->getAccessibleContext(), UNO_QUERY );
                if ( xParentComponent.is() )
                {
                    awt::Point aParentScreenLoc = xParentComponent->getLocationOnScreen();

                    // calculate bounding rectangle of the item relative to the accessible parent
                    aBounds.X += aWindowScreenLoc.X - aParentScreenLoc.X;
                    aBounds.Y += aWindowScreenLoc.Y - aParentScreenLoc.Y;
                }
            }
        }
    }

    return aBounds;
}


// XComponent


void SAL_CALL OAccessibleMenuItemComponent::disposing()
{
    OAccessibleMenuBaseComponent::disposing();

    m_pParent = nullptr;
    m_sAccessibleName.clear();
    m_sItemText.clear();
}


// XAccessibleContext


sal_Int64 OAccessibleMenuItemComponent::getAccessibleChildCount()
{
    OExternalLockGuard aGuard( this );

    return 0;
}


Reference< XAccessible > OAccessibleMenuItemComponent::getAccessibleChild( sal_Int64 i )
{
    OExternalLockGuard aGuard( this );

    if ( i < 0 || i >= getAccessibleChildCount() )
        throw IndexOutOfBoundsException();

    return Reference< XAccessible >();
}


Reference< XAccessible > OAccessibleMenuItemComponent::getAccessibleParent(  )
{
    OExternalLockGuard aGuard( this );

    return m_pParent->GetAccessible();
}


sal_Int64 OAccessibleMenuItemComponent::getAccessibleIndexInParent(  )
{
    OExternalLockGuard aGuard( this );

    return m_nItemPos;
}


sal_Int16 OAccessibleMenuItemComponent::getAccessibleRole(  )
{
    OExternalLockGuard aGuard( this );

    return AccessibleRole::UNKNOWN;
}


OUString OAccessibleMenuItemComponent::getAccessibleDescription( )
{
    OExternalLockGuard aGuard( this );

    OUString sDescription;
    if ( m_pParent )
        sDescription = m_pParent->GetAccessibleDescription( m_pParent->GetItemId( m_nItemPos ) );

    return sDescription;
}


OUString OAccessibleMenuItemComponent::getAccessibleName(  )
{
    OExternalLockGuard aGuard( this );

    return m_sAccessibleName;
}


Reference< XAccessibleRelationSet > OAccessibleMenuItemComponent::getAccessibleRelationSet(  )
{
    OExternalLockGuard aGuard( this );

    return new utl::AccessibleRelationSetHelper;
}


Locale OAccessibleMenuItemComponent::getLocale(  )
{
    OExternalLockGuard aGuard( this );

    return Application::GetSettings().GetLanguageTag().getLocale();
}


// XAccessibleComponent


Reference< XAccessible > OAccessibleMenuItemComponent::getAccessibleAtPoint( const awt::Point& )
{
    OExternalLockGuard aGuard( this );

    return Reference< XAccessible >();
}


void OAccessibleMenuItemComponent::grabFocus(  )
{
    // no focus for items
}


sal_Int32 OAccessibleMenuItemComponent::getForeground(  )
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


sal_Int32 OAccessibleMenuItemComponent::getBackground(  )
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


// XAccessibleExtendedComponent

OUString OAccessibleMenuItemComponent::getToolTipText(  )
{
    OExternalLockGuard aGuard( this );

    OUString sRet;
    if ( m_pParent )
        sRet = m_pParent->GetTipHelpText( m_pParent->GetItemId( m_nItemPos ) );

    return sRet;
}


bool OAccessibleMenuItemComponent::IsMenuHideDisabledEntries()
{
    if (m_pParent )
    {
        if( m_pParent->GetMenuFlags() & MenuFlags::HideDisabledEntries)
        {
            return true;
        }
    }
    return false;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
