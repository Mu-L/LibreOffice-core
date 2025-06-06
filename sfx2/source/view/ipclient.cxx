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

#include <com/sun/star/awt/XVclWindowPeer.hpp>
#include <com/sun/star/embed/Aspects.hpp>
#include <com/sun/star/embed/EmbedStates.hpp>
#include <com/sun/star/embed/UnreachableStateException.hpp>
#include <com/sun/star/embed/XEmbeddedClient.hpp>
#include <com/sun/star/embed/XInplaceClient.hpp>
#include <com/sun/star/embed/XInplaceObject.hpp>
#include <com/sun/star/embed/XWindowSupplier.hpp>
#include <com/sun/star/embed/EmbedVerbs.hpp>
#include <com/sun/star/embed/XEmbeddedOleObject.hpp>
#include <com/sun/star/embed/XEmbeddedObject.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/embed/XStateChangeListener.hpp>
#include <com/sun/star/embed/StateChangeInProgressException.hpp>
#include <com/sun/star/embed/XLinkageSupport.hpp>
#include <com/sun/star/lang/WrappedTargetRuntimeException.hpp>
#include <com/sun/star/task/ErrorCodeIOException.hpp>
#include <com/sun/star/task/StatusIndicatorFactory.hpp>
#include <com/sun/star/task/XStatusIndicator.hpp>

#include <com/sun/star/embed/EmbedMisc.hpp>
#include <svtools/embedhlp.hxx>
#include <vcl/svapp.hxx>
#include <vcl/unohelp.hxx>

#include <sfx2/ipclient.hxx>
#include <sfx2/viewsh.hxx>
#include <sfx2/viewfrm.hxx>
#include <sfx2/objsh.hxx>
#include <guisaveas.hxx>
#include <cppuhelper/implbase.hxx>
#include <svtools/ehdl.hxx>

#include <vcl/timer.hxx>
#include <vcl/window.hxx>
#include <toolkit/helper/vclunohelper.hxx>
#include <tools/debug.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <tools/fract.hxx>
#include <tools/gen.hxx>
#include <svtools/soerr.hxx>
#include <comphelper/lok.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/propertyvalue.hxx>
#include <cppuhelper/exc_hlp.hxx>

#include <LibreOfficeKit/LibreOfficeKitEnums.h>

#define SFX_CLIENTACTIVATE_TIMEOUT 100

using namespace com::sun::star;

namespace {

// SfxEmbedResizeGuard
class SfxBooleanFlagGuard
{
    bool& m_rFlag;
public:
    explicit SfxBooleanFlagGuard(bool& bFlag)
        : m_rFlag( bFlag )
    {
        m_rFlag = true;
    }

    ~SfxBooleanFlagGuard()
    {
        m_rFlag = false;
    }
};

tools::Rectangle lcl_negateRectX(const tools::Rectangle& rRect)
{
    return tools::Rectangle(
        std::max(static_cast<tools::Long>(0l), -rRect.Right()),
        rRect.Top(),
        std::max(static_cast<tools::Long>(0l), -rRect.Left()),
        rRect.Bottom());
}

}

// SfxInPlaceClient_Impl


class SfxInPlaceClient_Impl : public ::cppu::WeakImplHelper< embed::XEmbeddedClient,
                                                              embed::XInplaceClient,
                                                              document::XEventListener,
                                                              embed::XStateChangeListener,
                                                              embed::XWindowSupplier >
{
public:
    Timer                           m_aTimer { "sfx::SfxInPlaceClient m_xImpl::m_aTimer" }; // activation timeout, starts after object connection
    tools::Rectangle                       m_aObjArea;             // area of object in coordinate system of the container (without scaling)
    Fraction                        m_aScaleWidth;          // scaling that was applied to the object when it was not active
    Fraction                        m_aScaleHeight;
    SfxInPlaceClient*               m_pClient;
    sal_Int64                       m_nAspect;              // ViewAspect that is assigned from the container
    bool                            m_bStoreObject;
    bool                            m_bUIActive;            // set and cleared when notification for UI (de)activation is sent
    bool                            m_bResizeNoScale;
    bool                            m_bNegativeX;

    uno::Reference < embed::XEmbeddedObject > m_xObject;


    SfxInPlaceClient_Impl()
    : m_pClient( nullptr )
    , m_nAspect( 0 )
    , m_bStoreObject( true )
    , m_bUIActive( false )
    , m_bResizeNoScale( false )
    , m_bNegativeX( false )
    {}

    void SizeHasChanged();
    DECL_LINK(TimerHdl, Timer *, void);
    uno::Reference < frame::XFrame > const & GetFrame() const;

    // XEmbeddedClient
    virtual void SAL_CALL saveObject() override;
    virtual void SAL_CALL visibilityChanged( sal_Bool bVisible ) override;

    // XInplaceClient
    virtual sal_Bool SAL_CALL canInplaceActivate() override;
    virtual void SAL_CALL activatingInplace() override;
    virtual void SAL_CALL activatingUI() override;
    virtual void SAL_CALL deactivatedInplace() override;
    virtual void SAL_CALL deactivatedUI() override;
    virtual uno::Reference< css::frame::XLayoutManager > SAL_CALL getLayoutManager() override;
    virtual uno::Reference< frame::XDispatchProvider > SAL_CALL getInplaceDispatchProvider() override;
    virtual awt::Rectangle SAL_CALL getPlacement() override;
    virtual awt::Rectangle SAL_CALL getClipRectangle() override;
    virtual void SAL_CALL translateAccelerators( const uno::Sequence< awt::KeyEvent >& aKeys ) override;
    virtual void SAL_CALL scrollObject( const awt::Size& aOffset ) override;
    virtual void SAL_CALL changedPlacement( const awt::Rectangle& aPosRect ) override;

    // XComponentSupplier
    virtual uno::Reference< util::XCloseable > SAL_CALL getComponent() override;

    // XWindowSupplier
    virtual uno::Reference< awt::XWindow > SAL_CALL getWindow() override;

    // document::XEventListener
    virtual void SAL_CALL       notifyEvent( const document::EventObject& aEvent ) override;

    // XStateChangeListener
    virtual void SAL_CALL changingState( const css::lang::EventObject& aEvent, ::sal_Int32 nOldState, ::sal_Int32 nNewState ) override;
    virtual void SAL_CALL stateChanged( const css::lang::EventObject& aEvent, ::sal_Int32 nOldState, ::sal_Int32 nNewState ) override;
    virtual void SAL_CALL disposing( const css::lang::EventObject& aEvent ) override;
};

void SAL_CALL SfxInPlaceClient_Impl::changingState(
    const css::lang::EventObject& /*aEvent*/,
    ::sal_Int32 /*nOldState*/,
    ::sal_Int32 /*nNewState*/ )
{
}

void SAL_CALL SfxInPlaceClient_Impl::stateChanged(
    const css::lang::EventObject& /*aEvent*/,
    ::sal_Int32 nOldState,
    ::sal_Int32 nNewState )
{
    if ( m_pClient && nOldState != embed::EmbedStates::LOADED && nNewState == embed::EmbedStates::RUNNING )
    {
        // deactivation of object
        uno::Reference< frame::XModel > xDocument;
        if ( m_pClient->GetViewShell()->GetObjectShell() )
            xDocument = m_pClient->GetViewShell()->GetObjectShell()->GetModel();
        SfxObjectShell::SetCurrentComponent( xDocument );
    }
}

void SAL_CALL SfxInPlaceClient_Impl::notifyEvent( const document::EventObject& aEvent )
{
    SolarMutexGuard aGuard;

    if ( m_pClient && aEvent.EventName == "OnVisAreaChanged" && m_nAspect != embed::Aspects::MSOLE_ICON )
    {
        m_pClient->FormatChanged(); // for Writer when format of the object is changed with the area
        m_pClient->ViewChanged();
        m_pClient->Invalidate();
    }
}

void SAL_CALL SfxInPlaceClient_Impl::disposing( const css::lang::EventObject& /*aEvent*/ )
{
    delete m_pClient;
    m_pClient = nullptr;
}

// XEmbeddedClient

uno::Reference < frame::XFrame > const & SfxInPlaceClient_Impl::GetFrame() const
{
    if ( !m_pClient )
        throw uno::RuntimeException();
    return m_pClient->GetViewShell()->GetViewFrame().GetFrame().GetFrameInterface();
}

void SAL_CALL SfxInPlaceClient_Impl::saveObject()
{
    if (!m_bStoreObject || (m_pClient && m_pClient->IsProtected()))
        // client wants to discard the object (usually it means the container document is closed while an object is active
        // and the user didn't request saving the changes
        return;

    // the common persistence is supported by objects and links
    uno::Reference< embed::XCommonEmbedPersist > xPersist( m_xObject, uno::UNO_QUERY_THROW );

    uno::Reference< frame::XFrame >              xFrame;
    uno::Reference< task::XStatusIndicator >     xStatusIndicator;
    uno::Reference< frame::XModel >              xModel( m_xObject->getComponent(), uno::UNO_QUERY );
    const uno::Reference< uno::XComponentContext >&     xContext( ::comphelper::getProcessComponentContext() );

    if ( xModel.is() )
    {
        uno::Reference< frame::XController > xController = xModel->getCurrentController();
        if ( xController.is() )
            xFrame = xController->getFrame();
    }

    if ( xFrame.is() )
    {
        // set non-reschedule progress to prevent problems when asynchronous calls are made
        // during storing of the embedded object
        uno::Reference< task::XStatusIndicatorFactory > xStatusIndicatorFactory =
               task::StatusIndicatorFactory::createWithFrame( xContext, xFrame, true/*DisableReschedule*/, false/*AllowParentShow*/ );

        uno::Reference< beans::XPropertySet > xPropSet( xFrame, uno::UNO_QUERY );
        if ( xPropSet.is() )
        {
            try
            {
                xStatusIndicator = xStatusIndicatorFactory->createStatusIndicator();
                xPropSet->setPropertyValue( u"IndicatorInterception"_ustr , uno::Any( xStatusIndicator ));
            }
            catch ( const uno::RuntimeException& )
            {
                throw;
            }
            catch ( uno::Exception& )
            {
            }
        }
    }

    try
    {
        xPersist->storeOwn();
        m_xObject->update();
    }
    catch ( uno::Exception& )
    {
        //TODO/LATER: what should happen if object can't be saved?!
    }

    // reset status indicator interception after storing
    try
    {
        uno::Reference< beans::XPropertySet > xPropSet( xFrame, uno::UNO_QUERY );
        if ( xPropSet.is() )
        {
            xStatusIndicator.clear();
            xPropSet->setPropertyValue( u"IndicatorInterception"_ustr , uno::Any( xStatusIndicator ));
        }
    }
    catch ( const uno::RuntimeException& )
    {
        throw;
    }
    catch ( uno::Exception& )
    {
    }

    // the client can exist only in case there is a view shell
    if ( !m_pClient || !m_pClient->GetViewShell() )
        throw uno::RuntimeException();

    SfxObjectShell* pDocShell = m_pClient->GetViewShell()->GetObjectShell();
    if ( !pDocShell )
        throw uno::RuntimeException();

    pDocShell->SetModified();

    //TODO/LATER: invalidation might be necessary when object was modified, but is not
    //saved through this method
    // m_pClient->Invalidate();
}


void SAL_CALL SfxInPlaceClient_Impl::visibilityChanged( sal_Bool bVisible )
{
    SolarMutexGuard aGuard;

    if ( !m_pClient || !m_pClient->GetViewShell() )
        throw uno::RuntimeException();

    m_pClient->GetViewShell()->OutplaceActivated( bVisible );
    if (m_pClient) // it can change in the above code
        m_pClient->Invalidate();
}


// XInplaceClient

sal_Bool SAL_CALL SfxInPlaceClient_Impl::canInplaceActivate()
{
    if ( !m_xObject.is() )
        throw uno::RuntimeException();

    // we don't want to switch directly from outplace to inplace mode
    if ( m_xObject->getCurrentState() == embed::EmbedStates::ACTIVE || m_nAspect == embed::Aspects::MSOLE_ICON )
        return false;

    return true;
}


void SAL_CALL SfxInPlaceClient_Impl::activatingInplace()
{
    if ( !m_pClient || !m_pClient->GetViewShell() )
        throw uno::RuntimeException();

    if ( !comphelper::LibreOfficeKit::isActive() )
        return;

    if ( SfxViewShell* pViewShell = m_pClient->GetViewShell() )
    {
        tools::Rectangle aRect(m_pClient->GetObjArea());

        if (m_pClient->GetEditWin())
        {
            if (m_pClient->GetEditWin()->GetMapMode().GetMapUnit() == MapUnit::Map100thMM)
                aRect = o3tl::convert(aRect, o3tl::Length::mm100, o3tl::Length::twip);
        }

        OString str = (m_bNegativeX ? lcl_negateRectX(aRect) : aRect).toString() + ", \"INPLACE\"";
        pViewShell->libreOfficeKitViewCallback( LOK_CALLBACK_GRAPHIC_SELECTION, str );
    }

}


void SAL_CALL SfxInPlaceClient_Impl::activatingUI()
{
    if ( !m_pClient || !m_pClient->GetViewShell() )
        throw uno::RuntimeException();

    m_pClient->GetViewShell()->ResetAllClients_Impl(m_pClient);
    m_bUIActive = true;
    m_pClient->GetViewShell()->UIActivating( m_pClient );
}


void SAL_CALL SfxInPlaceClient_Impl::deactivatedInplace()
{
    if ( !m_pClient || !m_pClient->GetViewShell() )
        throw uno::RuntimeException();

    if ( comphelper::LibreOfficeKit::isActive() )
    {
        if ( SfxViewShell* pViewShell = m_pClient->GetViewShell() ) {
            pViewShell->libreOfficeKitViewCallback( LOK_CALLBACK_GRAPHIC_SELECTION, "INPLACE EXIT"_ostr );
        }
    }
}


void SAL_CALL SfxInPlaceClient_Impl::deactivatedUI()
{
    if ( !m_pClient || !m_pClient->GetViewShell() )
        throw uno::RuntimeException();

    m_pClient->GetViewShell()->UIDeactivated( m_pClient );
    m_bUIActive = false;
}


uno::Reference< css::frame::XLayoutManager > SAL_CALL SfxInPlaceClient_Impl::getLayoutManager()
{
    uno::Reference < beans::XPropertySet > xFrame( GetFrame(), uno::UNO_QUERY_THROW );

    uno::Reference< css::frame::XLayoutManager > xMan;
    try
    {
        uno::Any aAny = xFrame->getPropertyValue( u"LayoutManager"_ustr );
        aAny >>= xMan;
    }
    catch ( uno::Exception& ex )
    {
        css::uno::Any anyEx = cppu::getCaughtException();
        throw css::lang::WrappedTargetRuntimeException( ex.Message,
                        nullptr, anyEx );
    }

    return xMan;
}


uno::Reference< frame::XDispatchProvider > SAL_CALL SfxInPlaceClient_Impl::getInplaceDispatchProvider()
{
    return uno::Reference < frame::XDispatchProvider >( GetFrame(), uno::UNO_QUERY_THROW );
}


awt::Rectangle SAL_CALL SfxInPlaceClient_Impl::getPlacement()
{
    if ( !m_pClient || !m_pClient->GetViewShell() )
        throw uno::RuntimeException();

    // apply scaling to object area and convert to pixels
    tools::Rectangle aRealObjArea( m_aObjArea );
    aRealObjArea.SetSize( Size( tools::Long( aRealObjArea.GetWidth() * m_aScaleWidth),
                                tools::Long( aRealObjArea.GetHeight() * m_aScaleHeight) ) );

    vcl::Window* pEditWin = m_pClient->GetEditWin();
    // In Writer and Impress the map mode is disabled. So when a chart is
    // activated (for in place editing) we get the chart win size in 100th mm
    // and any method that should return pixels returns 100th mm and the chart
    // window map mode has a ~26.485 scale factor.
    // All that does not fit with current implementation for handling chart
    // editing in LOK.
    if (comphelper::LibreOfficeKit::isActive())
    {
        bool bMapModeEnabled = pEditWin->IsMapModeEnabled();
        if (!bMapModeEnabled)
            pEditWin->EnableMapMode();
        aRealObjArea = pEditWin->LogicToPixel(aRealObjArea);
        if (!bMapModeEnabled && pEditWin->IsMapModeEnabled())
            pEditWin->EnableMapMode(false);
    }
    else
    {
        aRealObjArea = pEditWin->LogicToPixel(aRealObjArea);
    }

    return vcl::unohelper::ConvertToAWTRect(aRealObjArea);
}


awt::Rectangle SAL_CALL SfxInPlaceClient_Impl::getClipRectangle()
{
    if ( !m_pClient || !m_pClient->GetViewShell() )
        throw uno::RuntimeException();

    // currently(?) same as placement
    tools::Rectangle aRealObjArea( m_aObjArea );
    aRealObjArea.SetSize( Size( tools::Long( aRealObjArea.GetWidth() * m_aScaleWidth),
                                tools::Long( aRealObjArea.GetHeight() * m_aScaleHeight) ) );

    vcl::Window* pEditWin = m_pClient->GetEditWin();
    // See comment for SfxInPlaceClient_Impl::getPlacement.
    if (comphelper::LibreOfficeKit::isActive())
    {
        bool bMapModeEnabled = pEditWin->IsMapModeEnabled();
        if (!bMapModeEnabled)
            pEditWin->EnableMapMode();
        aRealObjArea = pEditWin->LogicToPixel(aRealObjArea);
        if (!bMapModeEnabled && pEditWin->IsMapModeEnabled())
            pEditWin->EnableMapMode(false);
    }
    else
    {
        aRealObjArea = pEditWin->LogicToPixel(aRealObjArea);
    }

    return vcl::unohelper::ConvertToAWTRect(aRealObjArea);
}


void SAL_CALL SfxInPlaceClient_Impl::translateAccelerators( const uno::Sequence< awt::KeyEvent >& /*aKeys*/ )
{
    if ( !m_pClient || !m_pClient->GetViewShell() )
        throw uno::RuntimeException();

    // TODO/MBA: keyboard accelerators
}


void SAL_CALL SfxInPlaceClient_Impl::scrollObject( const awt::Size& /*aOffset*/ )
{
    if ( !m_pClient || !m_pClient->GetViewShell() )
        throw uno::RuntimeException();
}


void SAL_CALL SfxInPlaceClient_Impl::changedPlacement( const awt::Rectangle& aPosRect )
{
    uno::Reference< embed::XInplaceObject > xInplace( m_xObject, uno::UNO_QUERY_THROW );
    if ( !m_pClient || !m_pClient->GetEditWin() || !m_pClient->GetViewShell() )
        throw uno::RuntimeException();

    // check if the change is at least one pixel in size
    awt::Rectangle aOldRect = getPlacement();
    tools::Rectangle aNewPixelRect = vcl::unohelper::ConvertToVCLRect(aPosRect);
    tools::Rectangle aOldPixelRect = vcl::unohelper::ConvertToVCLRect(aOldRect);
    if ( aOldPixelRect == aNewPixelRect )
        // nothing has changed
        return;

    // new scaled object area
    tools::Rectangle aNewLogicRect = m_pClient->GetEditWin()->PixelToLogic( aNewPixelRect );

    // all the size changes in this method should happen without scaling
    // SfxBooleanFlagGuard aGuard( m_bResizeNoScale, sal_True );

    // allow container to apply restrictions on the requested new area;
    // the container might change the object view during size calculation;
    // currently only writer does it
    m_pClient->RequestNewObjectArea( aNewLogicRect);

    if ( aNewLogicRect != m_pClient->GetScaledObjArea() )
    {
        // the calculation of the object area has not changed the object size
        // it should be done here then
        SfxBooleanFlagGuard aGuard( m_bResizeNoScale );

        // new size of the object area without scaling
        Size aNewObjSize( tools::Long( aNewLogicRect.GetWidth()  / m_aScaleWidth ),
                          tools::Long( aNewLogicRect.GetHeight() / m_aScaleHeight ) );

        // now remove scaling from new placement and keep this at the new object area
        aNewLogicRect.SetSize( aNewObjSize );
        m_aObjArea = aNewLogicRect;

        // let the window size be recalculated
        SizeHasChanged();
    }

    // notify container view about changes
    m_pClient->ObjectAreaChanged();
}

// XComponentSupplier

uno::Reference< util::XCloseable > SAL_CALL SfxInPlaceClient_Impl::getComponent()
{
    if ( !m_pClient || !m_pClient->GetViewShell() )
        throw uno::RuntimeException();

    SfxObjectShell* pDocShell = m_pClient->GetViewShell()->GetObjectShell();
    if ( !pDocShell )
        throw uno::RuntimeException();

    // all the components must implement XCloseable
    uno::Reference< util::XCloseable > xComp( pDocShell->GetModel(), uno::UNO_QUERY_THROW );
    return xComp;
}


// XWindowSupplier

uno::Reference< awt::XWindow > SAL_CALL SfxInPlaceClient_Impl::getWindow()
{
    if ( !m_pClient || !m_pClient->GetEditWin() )
        throw uno::RuntimeException();

    uno::Reference< awt::XWindow > xWin( m_pClient->GetEditWin()->GetComponentInterface(), uno::UNO_QUERY );
    return xWin;
}


// notification to the client implementation that either the object area or the scaling has been changed
// as a result the logical size of the window has changed also
void SfxInPlaceClient_Impl::SizeHasChanged()
{
    if ( !m_pClient || !m_pClient->GetViewShell() )
        throw uno::RuntimeException();

    try {
        if ( m_xObject.is()
          && ( m_xObject->getCurrentState() == embed::EmbedStates::INPLACE_ACTIVE
                || m_xObject->getCurrentState() == embed::EmbedStates::UI_ACTIVE ) )
        {
            // only possible in active states
            uno::Reference< embed::XInplaceObject > xInplace( m_xObject, uno::UNO_QUERY_THROW );

            if ( m_bResizeNoScale )
            {
                // the resizing should be done without scaling
                // set the correct size to the object to avoid the scaling
                MapMode aObjectMap( VCLUnoHelper::UnoEmbed2VCLMapUnit( m_xObject->getMapUnit( m_nAspect ) ) );
                MapMode aClientMap( m_pClient->GetEditWin()->GetMapMode().GetMapUnit() );

                // convert to logical coordinates of the embedded object
                Size aNewSize = m_pClient->GetEditWin()->LogicToLogic( m_aObjArea.GetSize(), &aClientMap, &aObjectMap );
                m_xObject->setVisualAreaSize( m_nAspect, awt::Size( aNewSize.Width(), aNewSize.Height() ) );
            }

            xInplace->setObjectRectangles( getPlacement(), getClipRectangle() );
        }
    }
    catch( uno::Exception& )
    {
        // TODO/LATER: handle error
    }
}


IMPL_LINK_NOARG(SfxInPlaceClient_Impl, TimerHdl, Timer *, void)
{
    if ( m_pClient && m_xObject.is() )
    {
        m_pClient->GetViewShell()->CheckIPClient_Impl(m_pClient,
                m_pClient->GetViewShell()->GetObjectShell()->GetVisArea());
    }
}


// SfxInPlaceClient


SfxInPlaceClient::SfxInPlaceClient( SfxViewShell* pViewShell, vcl::Window *pDraw, sal_Int64 nAspect ) :
    m_xImp( new SfxInPlaceClient_Impl ),
    m_pViewSh( pViewShell ),
    m_pEditWin( pDraw )
{
    m_xImp->m_pClient = this;
    m_xImp->m_nAspect = nAspect;
    m_xImp->m_aScaleWidth = m_xImp->m_aScaleHeight = Fraction(1,1);
    pViewShell->NewIPClient_Impl(this);
    m_xImp->m_aTimer.SetTimeout( SFX_CLIENTACTIVATE_TIMEOUT );
    m_xImp->m_aTimer.SetInvokeHandler( LINK( m_xImp.get(), SfxInPlaceClient_Impl, TimerHdl ) );
}


SfxInPlaceClient::~SfxInPlaceClient()
{
    m_pViewSh->IPClientGone_Impl(this);

    // deleting the client before storing the object means discarding all changes
    m_xImp->m_bStoreObject = false;
    SetObject(nullptr);

    m_xImp->m_pClient = nullptr;

    // the next call will destroy m_xImp if no other reference to it exists
    m_xImp.clear();

    // TODO/LATER:
    // the class is not intended to be used in multithreaded environment;
    // if it will this disconnection and all the parts that use the m_pClient
    // must be guarded with mutex
}


void SfxInPlaceClient::SetObjectState( sal_Int32 nState )
{
    if ( !GetObject().is() )
        return;

    if ( m_xImp->m_nAspect == embed::Aspects::MSOLE_ICON
      && ( nState == embed::EmbedStates::UI_ACTIVE || nState == embed::EmbedStates::INPLACE_ACTIVE ) )
    {
        OSL_FAIL( "Iconified object should not be activated inplace!" );
        return;
    }

    try
    {
        GetObject()->changeState( nState );
    }
    catch ( uno::Exception& )
    {}
}


sal_Int64 SfxInPlaceClient::GetObjectMiscStatus() const
{
    if ( GetObject().is() )
        return GetObject()->getStatus( m_xImp->m_nAspect );
    return 0;
}


const uno::Reference < embed::XEmbeddedObject >& SfxInPlaceClient::GetObject() const
{
    return m_xImp->m_xObject;
}


void SfxInPlaceClient::SetObject( const uno::Reference < embed::XEmbeddedObject >& rObject )
{
    if ( m_xImp->m_xObject.is() && rObject != m_xImp->m_xObject )
    {
        DBG_ASSERT( GetObject()->getClientSite() == getXWeak(m_xImp.get()), "Wrong ClientSite!" );
        if ( GetObject()->getClientSite() == getXWeak(m_xImp.get()) )
        {
            if ( GetObject()->getCurrentState() != embed::EmbedStates::LOADED )
                SetObjectState( embed::EmbedStates::RUNNING );
            m_xImp->m_xObject->removeEventListener( m_xImp );
            m_xImp->m_xObject->removeStateChangeListener( m_xImp );
            try
            {
                m_xImp->m_xObject->setClientSite( nullptr );
            }
            catch( uno::Exception& )
            {
                OSL_FAIL( "Can not clean the client site!" );
            }
        }
    }

    if ( m_pViewSh->GetViewFrame().GetFrame().IsClosing_Impl() )
        // sometimes applications reconnect clients on shutting down because it happens in their Paint methods
        return;

    m_xImp->m_xObject = rObject;

    if ( rObject.is() )
    {
        // as soon as an object was connected to a client it has to be checked whether the object wants
        // to be activated
        rObject->addStateChangeListener( m_xImp );
        rObject->addEventListener( m_xImp );

        try
        {
            rObject->setClientSite( m_xImp );
        }
        catch( uno::Exception& )
        {
            OSL_FAIL( "Can not set the client site!" );
        }

        m_xImp->m_aTimer.Start();
    }
    else
        m_xImp->m_aTimer.Stop();
}


bool SfxInPlaceClient::SetObjArea( const tools::Rectangle& rArea )
{
    if( rArea != m_xImp->m_aObjArea )
    {
        m_xImp->m_aObjArea = rArea;
        m_xImp->SizeHasChanged();

        Invalidate();
        return true;
    }

    return false;
}


const tools::Rectangle& SfxInPlaceClient::GetObjArea() const
{
    return m_xImp->m_aObjArea;
}

tools::Rectangle SfxInPlaceClient::GetScaledObjArea() const
{
    tools::Rectangle aRealObjArea( m_xImp->m_aObjArea );
    aRealObjArea.SetSize( Size( tools::Long( aRealObjArea.GetWidth()  * m_xImp->m_aScaleWidth ),
                                tools::Long( aRealObjArea.GetHeight() * m_xImp->m_aScaleHeight ) ) );
    return aRealObjArea;
}


void SfxInPlaceClient::SetSizeScale( const Fraction & rScaleWidth, const Fraction & rScaleHeight )
{
    if ( m_xImp->m_aScaleWidth != rScaleWidth || m_xImp->m_aScaleHeight != rScaleHeight )
    {
        m_xImp->m_aScaleWidth = rScaleWidth;
        m_xImp->m_aScaleHeight = rScaleHeight;

        m_xImp->SizeHasChanged();

        // TODO/LATER: Invalidate seems to trigger (wrong) recalculations of the ObjArea, so it's better
        // not to call it here, but maybe it sounds reasonable to do so.
        //Invalidate();
    }
}


void SfxInPlaceClient::SetObjAreaAndScale( const tools::Rectangle& rArea, const Fraction& rScaleWidth, const Fraction& rScaleHeight )
{
    if( rArea != m_xImp->m_aObjArea || m_xImp->m_aScaleWidth != rScaleWidth || m_xImp->m_aScaleHeight != rScaleHeight )
    {
        m_xImp->m_aObjArea = rArea;
        m_xImp->m_aScaleWidth = rScaleWidth;
        m_xImp->m_aScaleHeight = rScaleHeight;

        m_xImp->SizeHasChanged();

        Invalidate();
    }
}


const Fraction& SfxInPlaceClient::GetScaleWidth() const
{
    return m_xImp->m_aScaleWidth;
}


const Fraction& SfxInPlaceClient::GetScaleHeight() const
{
    return m_xImp->m_aScaleHeight;
}


void SfxInPlaceClient::Invalidate()
{
    // TODO/LATER: do we need both?

    // the object area is provided in logical coordinates of the window but without scaling applied
    tools::Rectangle aRealObjArea( m_xImp->m_aObjArea );
    aRealObjArea.SetSize( Size( tools::Long( aRealObjArea.GetWidth()  * m_xImp->m_aScaleWidth ),
                                tools::Long( aRealObjArea.GetHeight() * m_xImp->m_aScaleHeight ) ) );

    m_pEditWin->Invalidate( IsNegativeX() ? lcl_negateRectX(aRealObjArea) : aRealObjArea );

    ViewChanged();
}


bool SfxInPlaceClient::IsObjectUIActive() const
{
    try {
        return ( m_xImp->m_xObject.is() && ( m_xImp->m_xObject->getCurrentState() == embed::EmbedStates::UI_ACTIVE ) );
    }
    catch( uno::Exception& )
    {}

    return false;
}


bool SfxInPlaceClient::IsObjectInPlaceActive() const
{
    try {
        return(
               (
                m_xImp->m_xObject.is() &&
                (m_xImp->m_xObject->getCurrentState() == embed::EmbedStates::INPLACE_ACTIVE)
               ) ||
               (
                m_xImp->m_xObject.is() &&
                (m_xImp->m_xObject->getCurrentState() == embed::EmbedStates::UI_ACTIVE)
               )
              );
    }
    catch( uno::Exception& )
    {}

    return false;
}


SfxInPlaceClient* SfxInPlaceClient::GetClient( SfxObjectShell const * pDoc, const css::uno::Reference < css::embed::XEmbeddedObject >& xObject )
{
    for ( SfxViewFrame* pFrame = SfxViewFrame::GetFirst(pDoc); pFrame; pFrame=SfxViewFrame::GetNext(*pFrame,pDoc) )
    {
        if( pFrame->GetViewShell() )
        {
            SfxInPlaceClient* pClient = pFrame->GetViewShell()->FindIPClient( xObject, nullptr );
            if ( pClient )
                return pClient;
        }
    }

    return nullptr;
}

sal_Int64 SfxInPlaceClient::GetAspect() const
{
    return m_xImp->m_nAspect;
}

ErrCodeMsg SfxInPlaceClient::DoVerb(sal_Int32 nVerb)
{
    SfxErrorContext aEc(ERRCTX_SO_DOVERB, m_pViewSh->GetFrameWeld(), RID_SO_ERRCTX);
    ErrCodeMsg nError = ERRCODE_NONE;

    if ( m_xImp->m_xObject.is() )
    {
        bool bSaveCopyAs = false;
        if ( nVerb == -8 ) // "Save Copy as..."
        {
            svt::EmbeddedObjectRef::TryRunningState( m_xImp->m_xObject );
            // TODO/LATER: this special verb should disappear when outplace activation is completely available
            uno::Reference< frame::XModel2 > xEmbModel( m_xImp->m_xObject->getComponent(), uno::UNO_QUERY );
            if ( xEmbModel.is() )
            {
                bSaveCopyAs = true;

                try
                {
                    SfxStoringHelper aHelper;
                    uno::Sequence< beans::PropertyValue > aDispatchArgs{
                        comphelper::makePropertyValue(u"SaveTo"_ustr, true)
                    };

                    aHelper.GUIStoreModel( xEmbModel,
                                            u"SaveAs",
                                            aDispatchArgs,
                                            false,
                                            SignatureState::NOSIGNATURES,
                                            SignatureState::NOSIGNATURES,
                                            false );
                }
                catch( const task::ErrorCodeIOException& aErrorEx )
                {
                    nError = ErrCode(aErrorEx.ErrCode);
                }
                catch( uno::Exception& )
                {
                    nError = ERRCODE_IO_GENERAL;
                    // TODO/LATER: better error handling
                }
            }
        }

        if ( !bSaveCopyAs )
        {
            if ( m_xImp->m_nAspect == embed::Aspects::MSOLE_ICON )
            {
                // the common persistence is supported by objects and links

                uno::Reference< embed::XEmbeddedOleObject > xEmbeddedOleObject( m_xImp->m_xObject, uno::UNO_QUERY );

                if ( xEmbeddedOleObject.is() && (nVerb == embed::EmbedVerbs::MS_OLEVERB_PRIMARY || nVerb == embed::EmbedVerbs::MS_OLEVERB_OPEN || nVerb == embed::EmbedVerbs::MS_OLEVERB_SHOW ))
                    nVerb = embed::EmbedVerbs::MS_OLEVERB_SHOW;
                else if ( nVerb == embed::EmbedVerbs::MS_OLEVERB_PRIMARY || nVerb == embed::EmbedVerbs::MS_OLEVERB_SHOW )
                    nVerb = embed::EmbedVerbs::MS_OLEVERB_OPEN; // outplace activation
                else if ( nVerb == embed::EmbedVerbs::MS_OLEVERB_UIACTIVATE
                       || nVerb == embed::EmbedVerbs::MS_OLEVERB_IPACTIVATE )
                    nError = ERRCODE_SO_GENERALERROR;
            }

            if ( !nError )
            {
                // See comment for SfxInPlaceClient_Impl::getPlacement.
                vcl::Window* pEditWin = GetEditWin();
                bool bMapModeEnabled = pEditWin->IsMapModeEnabled();
                if (comphelper::LibreOfficeKit::isActive() && !bMapModeEnabled)
                {
                    pEditWin->EnableMapMode();
                }
                m_pViewSh->GetViewFrame().GetFrame().LockResize_Impl(true);
                try
                {
                    m_xImp->m_xObject->setClientSite( m_xImp );

                    m_xImp->m_xObject->doVerb( nVerb );
                }
                catch ( embed::UnreachableStateException& e )
                {
                    if (nVerb == embed::EmbedVerbs::MS_OLEVERB_PRIMARY || nVerb == embed::EmbedVerbs::MS_OLEVERB_OPEN || nVerb == embed::EmbedVerbs::MS_OLEVERB_SHOW)
                    {
                        // a workaround for the default verb, usually makes sense for alien objects
                        try
                        {
                            m_xImp->m_xObject->doVerb( -9 ); // open own view, a workaround verb that is not visible

                            if ( m_xImp->m_xObject->getCurrentState() == embed::EmbedStates::UI_ACTIVE )
                            {
                                // the object was converted to OOo object
                                awt::Size aSize = m_xImp->m_xObject->getVisualAreaSize( m_xImp->m_nAspect );
                                MapMode aObjectMap( VCLUnoHelper::UnoEmbed2VCLMapUnit( m_xImp->m_xObject->getMapUnit( m_xImp->m_nAspect ) ) );
                                MapMode aClientMap( GetEditWin()->GetMapMode().GetMapUnit() );
                                Size aNewSize = GetEditWin()->LogicToLogic( Size( aSize.Width, aSize.Height ), &aObjectMap, &aClientMap );

                                tools::Rectangle aScaledArea = GetScaledObjArea();
                                m_xImp->m_aObjArea.SetSize( aNewSize );
                                m_xImp->m_aScaleWidth = Fraction( aScaledArea.GetWidth(), aNewSize.Width() );
                                m_xImp->m_aScaleHeight = Fraction( aScaledArea.GetHeight(), aNewSize.Height() );
                            }
                        }
                        catch (uno::Exception const&)
                        {
                            TOOLS_WARN_EXCEPTION("embeddedobj", "SfxInPlaceClient::DoVerb: -9 fallback path");
                            nError = ErrCodeMsg(ERRCODE_SO_GENERALERROR, e.Message);
                        }
                    }
                }
                catch ( embed::StateChangeInProgressException& )
                {
                    // TODO/LATER: it would be nice to be able to provide the current target state outside
                    nError = ERRCODE_SO_CANNOT_DOVERB_NOW;
                }
                catch (uno::Exception const&)
                {
                    TOOLS_WARN_EXCEPTION("embeddedobj", "SfxInPlaceClient::DoVerb");
                    nError = ERRCODE_SO_GENERALERROR;
                    //TODO/LATER: better error handling

                }
                if (comphelper::LibreOfficeKit::isActive() && !bMapModeEnabled
                        && pEditWin->IsMapModeEnabled())
                {
                    pEditWin->EnableMapMode(false);
                }
                SfxViewFrame& rFrame = m_pViewSh->GetViewFrame();
                rFrame.GetFrame().LockResize_Impl(false);
                rFrame.GetFrame().Resize();
            }
        }
    }

    if( nError )
        ErrorHandler::HandleError( nError );

    return nError;
}

void SfxInPlaceClient::VisAreaChanged()
{
    uno::Reference < embed::XInplaceObject > xObj( m_xImp->m_xObject, uno::UNO_QUERY );
    if ( xObj.is() )
        m_xImp->SizeHasChanged();
}

void SfxInPlaceClient::ObjectAreaChanged()
{
    // dummy implementation
}

void SfxInPlaceClient::RequestNewObjectArea( tools::Rectangle& )
{
    // dummy implementation
}

void SfxInPlaceClient::ViewChanged()
{
    // dummy implementation
}

void SfxInPlaceClient::FormatChanged()
{
    // dummy implementation
}

bool SfxInPlaceClient::IsProtected() const { return false; }

void SfxInPlaceClient::DeactivateObject()
{
    if ( !GetObject().is() )
        return;

    try
    {
        m_xImp->m_bUIActive = false;
        bool bHasFocus = false;
        uno::Reference< frame::XModel > xModel( m_xImp->m_xObject->getComponent(), uno::UNO_QUERY );
        if ( xModel.is() )
        {
            uno::Reference< frame::XController > xController = xModel->getCurrentController();
            if ( xController.is() )
            {
                VclPtr<vcl::Window> pWindow = VCLUnoHelper::GetWindow( xController->getFrame()->getContainerWindow() );
                bHasFocus = pWindow->HasChildPathFocus( true );
            }
        }

        m_pViewSh->GetViewFrame().GetFrame().LockResize_Impl(true);

        if ( m_xImp->m_xObject->getStatus( m_xImp->m_nAspect ) & embed::EmbedMisc::MS_EMBED_ACTIVATEWHENVISIBLE )
        {
            m_xImp->m_xObject->changeState( embed::EmbedStates::INPLACE_ACTIVE );
            if (bHasFocus)
                m_pViewSh->GetWindow()->GrabFocus();
        }
        else
        {
            // the links should not stay in running state for long time because of locking
            uno::Reference< embed::XLinkageSupport > xLink( m_xImp->m_xObject, uno::UNO_QUERY );
            if ( xLink.is() && xLink->isLink() )
                m_xImp->m_xObject->changeState( embed::EmbedStates::LOADED );
            else
                m_xImp->m_xObject->changeState( embed::EmbedStates::RUNNING );
        }

        SfxViewFrame& rFrame = m_pViewSh->GetViewFrame();
        SfxViewFrame::SetViewFrame( &rFrame );
        rFrame.GetFrame().LockResize_Impl(false);
        rFrame.GetFrame().Resize();
    }
    catch (css::uno::Exception& )
    {}
}

void SfxInPlaceClient::ResetObject()
{
    if ( !GetObject().is() )
        return;

    try
    {
        m_xImp->m_bUIActive = false;
        if ( m_xImp->m_xObject->getStatus( m_xImp->m_nAspect ) & embed::EmbedMisc::MS_EMBED_ACTIVATEWHENVISIBLE )
            m_xImp->m_xObject->changeState( embed::EmbedStates::INPLACE_ACTIVE );
        else
        {
            // the links should not stay in running state for long time because of locking
            uno::Reference< embed::XLinkageSupport > xLink( m_xImp->m_xObject, uno::UNO_QUERY );
            if ( xLink.is() && xLink->isLink() )
                m_xImp->m_xObject->changeState( embed::EmbedStates::LOADED );
            else
                m_xImp->m_xObject->changeState( embed::EmbedStates::RUNNING );
        }
    }
    catch (css::uno::Exception& )
    {}
}

bool SfxInPlaceClient::IsUIActive() const
{
    return m_xImp->m_bUIActive;
}

void SfxInPlaceClient::SetNegativeX(bool bSet)
{
    m_xImp->m_bNegativeX = bSet;
}

bool SfxInPlaceClient::IsNegativeX() const
{
    return m_xImp->m_bNegativeX;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
