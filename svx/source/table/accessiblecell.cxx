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
#include <sal/log.hxx>

#include <memory>

#include "accessiblecell.hxx"
#include <cell.hxx>

#include <com/sun/star/accessibility/AccessibleRole.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/accessibility/AccessibleEventId.hpp>

#include <editeng/unoedsrc.hxx>
#include <utility>
#include <vcl/svapp.hxx>
#include <vcl/window.hxx>

#include <comphelper/string.hxx>
#include <comphelper/sequence.hxx>
#include <svx/IAccessibleViewForwarder.hxx>
#include <svx/unoshtxt.hxx>
#include <svx/svdotext.hxx>
#include <tools/debug.hxx>

using namespace sdr::table;
using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::accessibility;
using namespace ::com::sun::star::lang;

namespace accessibility {

AccessibleCell::AccessibleCell( const rtl::Reference< AccessibleTableShape>& rxParent, sdr::table::CellRef xCell, sal_Int32 nIndex, const AccessibleShapeTreeInfo& rShapeTreeInfo )
: AccessibleContextBase(rxParent, AccessibleRole::TABLE_CELL)
, maShapeTreeInfo( rShapeTreeInfo )
, mnIndexInParent( nIndex )
, mxCell(std::move( xCell ))
{
    //Init the pAccTable var
    pAccTable = rxParent.get();
}


AccessibleCell::~AccessibleCell()
{
    DBG_ASSERT( mpText == nullptr, "svx::AccessibleCell::~AccessibleCell(), not disposed!?" );
}


void AccessibleCell::Init()
{
    SdrView* pView = maShapeTreeInfo.GetSdrView();
    const vcl::Window* pWindow = maShapeTreeInfo.GetWindow ();
    if( !((pView != nullptr) && (pWindow != nullptr) && mxCell.is()))
        return;

    // create AccessibleTextHelper to handle this shape's text
    if( mxCell->CanCreateEditOutlinerParaObject() || mxCell->GetOutlinerParaObject() != nullptr )
    {
        // non-empty text -> use full-fledged edit source right away

        mpText.reset( new AccessibleTextHelper( std::make_unique<SvxTextEditSource>(mxCell->GetObject(), mxCell.get(), *pView, *pWindow->GetOutDev()) ) );
        if( mxCell.is() && mxCell->IsActiveCell() )
            mpText->SetFocus();
        mpText->SetEventSource(this);
    }
}


bool AccessibleCell::SetState (sal_Int64 aState)
{
    bool bStateHasChanged = false;

    if (aState == AccessibleStateType::FOCUSED && mpText != nullptr)
    {
        // Offer FOCUSED state to edit engine and detect whether the state
        // changes.
        bool bIsFocused = mpText->HaveFocus ();
        mpText->SetFocus();
        bStateHasChanged = (bIsFocused != mpText->HaveFocus ());
    }
    else
        bStateHasChanged = AccessibleContextBase::SetState (aState);

    return bStateHasChanged;
}


bool AccessibleCell::ResetState (sal_Int64 aState)
{
    bool bStateHasChanged = false;

    if (aState == AccessibleStateType::FOCUSED && mpText != nullptr)
    {
        // Try to remove FOCUSED state from the edit engine and detect
        // whether the state changes.
        bool bIsFocused = mpText->HaveFocus ();
        mpText->SetFocus (false);
        bStateHasChanged = (bIsFocused != mpText->HaveFocus ());
    }
    else
        bStateHasChanged = AccessibleContextBase::ResetState (aState);

    return bStateHasChanged;
}

// XAccessibleContext


/** The children of this cell come from the paragraphs of text.
*/
sal_Int64 SAL_CALL AccessibleCell::getAccessibleChildCount()
{
    SolarMutexGuard aSolarGuard;
    ensureAlive();
    return mpText != nullptr ? mpText->GetChildCount () : 0;
}


/** Forward the request to the shape.  Return the requested shape or throw
    an exception for a wrong index.
*/
Reference<XAccessible> SAL_CALL AccessibleCell::getAccessibleChild (sal_Int64 nIndex)
{
    SolarMutexGuard aSolarGuard;
    ensureAlive();

    return mpText->GetChild (nIndex);
}


/** Return a copy of the state set.
    Possible states are:
        ENABLED
        SHOWING
        VISIBLE
*/
sal_Int64 SAL_CALL AccessibleCell::getAccessibleStateSet()
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard (m_aMutex);
    sal_Int64 nStateSet = 0;

    if (rBHelper.bDisposed || mpText == nullptr)
    {
        // Return a minimal state set that only contains the DEFUNC state.
        nStateSet = AccessibleContextBase::getAccessibleStateSet ();
    }
    else
    {
        // Merge current FOCUSED state from edit engine.
        if (mpText != nullptr)
        {
            if (mpText->HaveFocus())
                mnStateSet |= AccessibleStateType::FOCUSED;
            else
                mnStateSet &= ~AccessibleStateType::FOCUSED;
        }
        // Set the invisible state for merged cell
        if (mxCell.is() && mxCell->isMerged())
            mnStateSet &= ~AccessibleStateType::VISIBLE;
        else
            mnStateSet |= AccessibleStateType::VISIBLE;


        //Just when the parent table is not read-only,set states EDITABLE,RESIZABLE,MOVEABLE
        css::uno::Reference<XAccessible> xTempAcc = getAccessibleParent();
        if( xTempAcc.is() )
        {
            css::uno::Reference<XAccessibleContext>
                                    xTempAccContext = xTempAcc->getAccessibleContext();
            if( xTempAccContext.is() )
            {
                if (xTempAccContext->getAccessibleStateSet() & AccessibleStateType::EDITABLE)
                {
                    mnStateSet |= AccessibleStateType::EDITABLE;
                    mnStateSet |= AccessibleStateType::RESIZABLE;
                    mnStateSet |= AccessibleStateType::MOVEABLE;
                }
            }
        }
        nStateSet = mnStateSet;
    }

    return nStateSet;
}


// XAccessibleComponent

/** The implementation below is at the moment straightforward.  It iterates
    over all children (and thereby instances all children which have not
    been already instantiated) until a child covering the specified point is
    found.
    This leaves room for improvement.  For instance, first iterate only over
    the already instantiated children and only if no match is found
    instantiate the remaining ones.
*/
Reference<XAccessible > SAL_CALL  AccessibleCell::getAccessibleAtPoint ( const css::awt::Point& aPoint)
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard (m_aMutex);

    sal_Int64 nChildCount = getAccessibleChildCount ();
    for (sal_Int64 i = 0; i < nChildCount; ++i)
    {
        Reference<XAccessible> xChild (getAccessibleChild (i));
        if (xChild.is())
        {
            Reference<XAccessibleComponent> xChildComponent (xChild->getAccessibleContext(), uno::UNO_QUERY);
            if (xChildComponent.is())
            {
                awt::Rectangle aBBox (xChildComponent->getBounds());
                if ( (aPoint.X >= aBBox.X)
                    && (aPoint.Y >= aBBox.Y)
                    && (aPoint.X < aBBox.X+aBBox.Width)
                    && (aPoint.Y < aBBox.Y+aBBox.Height) )
                    return xChild;
            }
        }
    }

    // Have not found a child under the given point.  Returning empty
    // reference to indicate this.
    return uno::Reference<XAccessible>();
}


css::awt::Rectangle AccessibleCell::implGetBounds()
{
    css::awt::Rectangle aBoundingBox;
    if( mxCell.is() )
    {
        // Get the cell's bounding box in internal coordinates (in 100th of mm)
        const ::tools::Rectangle aCellRect( mxCell->getCellRect() );

        // Transform coordinates from internal to pixel.
        if (maShapeTreeInfo.GetViewForwarder() == nullptr)
            throw uno::RuntimeException (u"AccessibleCell has no valid view forwarder"_ustr, getXWeak());

        ::Size aPixelSize( maShapeTreeInfo.GetViewForwarder()->LogicToPixel(::Size(aCellRect.GetWidth(), aCellRect.GetHeight())) );
        ::Point aPixelPosition( maShapeTreeInfo.GetViewForwarder()->LogicToPixel( aCellRect.TopLeft() ));

        // Clip the shape's bounding box with the bounding box of its parent.
        Reference<XAccessibleComponent> xParentComponent ( getAccessibleParent(), uno::UNO_QUERY);
        if (xParentComponent.is())
        {
            // Make the coordinates relative to the parent.
            awt::Point aParentLocation (xParentComponent->getLocationOnScreen());
            int x = aPixelPosition.getX() - aParentLocation.X;
            int y = aPixelPosition.getY() - aParentLocation.Y;

            // Clip with parent (with coordinates relative to itself).
            ::tools::Rectangle aBBox ( x, y, x + aPixelSize.getWidth(), y + aPixelSize.getHeight());
            awt::Size aParentSize (xParentComponent->getSize());
            ::tools::Rectangle aParentBBox (0,0, aParentSize.Width, aParentSize.Height);
            aBBox = aBBox.GetIntersection (aParentBBox);
            aBoundingBox = awt::Rectangle ( aBBox.Left(), aBBox.Top(), aBBox.getOpenWidth(), aBBox.getOpenHeight());
        }
        else
        {
            SAL_INFO("svx", "parent does not support component");
            aBoundingBox = awt::Rectangle (aPixelPosition.getX(), aPixelPosition.getY(),aPixelSize.getWidth(), aPixelSize.getHeight());
        }
    }

    return aBoundingBox;
}

sal_Int32 SAL_CALL AccessibleCell::getForeground()
{
    ensureAlive();

    // todo
    return sal_Int32(0x0ffffffL);
}


sal_Int32 SAL_CALL AccessibleCell::getBackground()
{
    ensureAlive();

    // todo
    return 0;
}

// XAccessibleEventBroadcaster


void SAL_CALL AccessibleCell::addAccessibleEventListener( const Reference<XAccessibleEventListener >& rxListener)
{
    AccessibleContextBase::addAccessibleEventListener(rxListener);

    if (isAlive() && mpText)
        mpText->AddEventListener (rxListener);
}


void SAL_CALL AccessibleCell::removeAccessibleEventListener( const Reference<XAccessibleEventListener >& rxListener)
{
    SolarMutexGuard aSolarGuard;
    AccessibleContextBase::removeAccessibleEventListener(rxListener);
    if (mpText != nullptr)
        mpText->RemoveEventListener (rxListener);
}


// XServiceInfo


OUString SAL_CALL AccessibleCell::getImplementationName()
{
    return u"AccessibleCell"_ustr;
}


Sequence<OUString> SAL_CALL AccessibleCell::getSupportedServiceNames()
{
    ensureAlive();
    const css::uno::Sequence<OUString> vals { u"com.sun.star.drawing.AccessibleCell"_ustr };
    return comphelper::concatSequences(AccessibleContextBase::getSupportedServiceNames(), vals);
}


// IAccessibleViewForwarderListener


void AccessibleCell::ViewForwarderChanged()
{
    // Inform all listeners that the graphical representation (i.e. size
    // and/or position) of the shape has changed.
    CommitChange(AccessibleEventId::VISIBLE_DATA_CHANGED, Any(), Any(), -1);

    // update our children that our screen position might have changed
    if( mpText )
        mpText->UpdateChildren();
}


// protected


void AccessibleCell::disposing()
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard (m_aMutex);

    // Make sure to send an event that this object loses the focus in the
    // case that it has the focus.
    mnStateSet &= ~AccessibleStateType::FOCUSED;

    if (mpText != nullptr)
    {
        mpText->Dispose();
        mpText.reset();
    }

    // Cleanup.  Remove references to objects to allow them to be
    // destroyed.
    mxCell.clear();
    maShapeTreeInfo.dispose();

    // Call base classes.
    AccessibleContextBase::dispose ();
}

sal_Int64 SAL_CALL AccessibleCell::getAccessibleIndexInParent()
{
    ensureAlive();
    return mnIndexInParent;
}


OUString AccessibleCell::getCellName( sal_Int32 nCol, sal_Int32 nRow )
{
    OUStringBuffer aBuf;

    if (nCol < 26*26)
    {
        if (nCol < 26)
            aBuf.append( static_cast<sal_Unicode>( 'A' +
                        static_cast<sal_uInt16>(nCol)));
        else
        {
            aBuf.append(
                OUStringChar(static_cast<sal_Unicode>( 'A' +
                         (static_cast<sal_uInt16>(nCol) / 26) - 1))
                + OUStringChar( static_cast<sal_Unicode>( 'A' +
                                (static_cast<sal_uInt16>(nCol) % 26))) );
        }
    }
    else
    {
        OUStringBuffer aStr;
        while (nCol >= 26)
        {
            sal_Int32 nC = nCol % 26;
            aStr.append(static_cast<sal_Unicode>( 'A' +
                    static_cast<sal_uInt16>(nC)));
            nCol = nCol - nC;
            nCol = nCol / 26 - 1;
        }
        aStr.append(static_cast<sal_Unicode>( 'A' +
                static_cast<sal_uInt16>(nCol)));
        aBuf.append(comphelper::string::reverseString(aStr));
    }
    aBuf.append(nRow+1);
    return aBuf.makeStringAndClear();
}

OUString SAL_CALL AccessibleCell::getAccessibleName()
{
    ensureAlive();
    SolarMutexGuard aSolarGuard;

    if( pAccTable )
    {
        try
        {
            sal_Int32 nRow = 0, nCol = 0;
            pAccTable->getColumnAndRow(mnIndexInParent, nCol, nRow);
            return getCellName( nCol, nRow );
        }
        catch(const Exception&)
        {
        }
    }

    return AccessibleContextBase::getAccessibleName();
}

void AccessibleCell::UpdateChildren()
{
    if (mpText)
        mpText->UpdateChildren();
}

/* MT: Above getAccessibleName was introduced with IA2 CWS, while below was introduce in 3.3 meanwhile. Check which one is correct
+If this is correct, we also don't need  sdr::table::CellRef getCellRef(), UpdateChildren(), getCellName( sal_Int32 nCol, sal_Int32 nRow ) above
+

OUString SAL_CALL AccessibleCell::getAccessibleName() throw (css::uno::RuntimeException)
{
    ThrowIfDisposed ();
    SolarMutexGuard aSolarGuard;

    if( mxCell.is() )
        return mxCell->getName();

    return AccessibleContextBase::getAccessibleName();
}
*/

} // end of namespace accessibility

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
