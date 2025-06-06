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

#include <utility>

#include <dispatch/dispatchprovider.hxx>
#include <dispatch/interceptionhelper.hxx>
#include <dispatch/windowcommanddispatch.hxx>
#include <loadenv/loadenv.hxx>
#include <helper/oframes.hxx>
#include <framework/framecontainer.hxx>
#include <framework/titlehelper.hxx>
#include <svtools/openfiledroptargetlistener.hxx>
#include <classes/taskcreator.hxx>
#include <loadenv/targethelper.hxx>
#include <framework/framelistanalyzer.hxx>
#include <helper/dockingareadefaultacceptor.hxx>
#include <dispatch/dispatchinformationprovider.hxx>

#include <pattern/window.hxx>
#include <properties.h>
#include <targets.h>

#include <com/sun/star/awt/Toolkit.hpp>
#include <com/sun/star/awt/XDevice.hpp>
#include <com/sun/star/awt/XTopWindow.hpp>
#include <com/sun/star/awt/PosSize.hpp>
#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/beans/PropertyExistException.hpp>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/datatransfer/dnd/XDropTarget.hpp>
#include <com/sun/star/frame/XFrame2.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/frame/XTitleChangeBroadcaster.hpp>
#include <com/sun/star/frame/LayoutManager.hpp>
#include <com/sun/star/frame/XDesktop.hpp>
#include <com/sun/star/frame/FrameSearchFlag.hpp>
#include <com/sun/star/lang/DisposedException.hpp>
#include <com/sun/star/task/StatusIndicatorFactory.hpp>
#include <com/sun/star/task/theJobExecutor.hpp>
#include <com/sun/star/task/XJobExecutor.hpp>
#include <com/sun/star/util/CloseVetoException.hpp>
#include <com/sun/star/util/URLTransformer.hpp>
#include <com/sun/star/util/XURLTransformer.hpp>
#include <com/sun/star/util/XCloseable.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>

#include <cppuhelper/basemutex.hxx>
#include <cppuhelper/compbase.hxx>
#include <comphelper/multiinterfacecontainer3.hxx>
#include <comphelper/multicontainer2.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <cppuhelper/weak.hxx>
#include <rtl/ref.hxx>
#include <sal/log.hxx>
#include <vcl/window.hxx>
#include <vcl/wrkwin.hxx>
#include <vcl/svapp.hxx>

#include <toolkit/helper/vclunohelper.hxx>
#include <unotools/moduleoptions.hxx>
#include <unotools/weakref.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <unotools/cmdoptions.hxx>
#include <vcl/threadex.hxx>
#include <mutex>

using namespace framework;

namespace {

// This enum can be used to set different active states of frames
enum EActiveState
{
    E_INACTIVE,   // I am not a member of active path in tree and I don't have the focus.
    E_ACTIVE,     // I am in the middle of an active path in tree and I don't have the focus.
    E_FOCUS       // I have the focus now. I must be a member of an active path!
};

/*-************************************************************************************************************
    @short      implements a normal frame of hierarchy
    @descr      An instance of this class can be a normal node in a frame tree. A frame supports influencing its
                subtree, finding of subframes, activate- and deactivate-mechanisms as well as
                set/get of a frame window, component or controller.
*//*-*************************************************************************************************************/
class XFrameImpl:
    private cppu::BaseMutex,
    public cppu::PartialWeakComponentImplHelper<
        css::lang::XServiceInfo, css::frame::XFrame2, css::awt::XWindowListener,
        css::awt::XTopWindowListener, css::awt::XFocusListener,
        css::document::XActionLockable, css::util::XCloseable,
        css::frame::XComponentLoader, css::frame::XTitle,
        css::frame::XTitleChangeBroadcaster, css::beans::XPropertySet,
        css::beans::XPropertySetInfo>
{
public:

    explicit XFrameImpl(css::uno::Reference< css::uno::XComponentContext >  xContext);

    /// Initialization function after having acquire()'d.
    void initListeners();

    virtual OUString SAL_CALL getImplementationName() override
    {
        return u"com.sun.star.comp.framework.Frame"_ustr;
    }

    virtual sal_Bool SAL_CALL supportsService(OUString const & ServiceName) override
    {
        return cppu::supportsService(this, ServiceName);
    }

    virtual css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override
    {
        return {u"com.sun.star.frame.Frame"_ustr};
    }

    //  XComponentLoader

    virtual css::uno::Reference< css::lang::XComponent > SAL_CALL loadComponentFromURL(
            const OUString& sURL,
            const OUString& sTargetFrameName,
            sal_Int32 nSearchFlags,
            const css::uno::Sequence< css::beans::PropertyValue >& lArguments ) override;

    //  XFramesSupplier

    virtual css::uno::Reference < css::frame::XFrames > SAL_CALL getFrames() override;
    virtual css::uno::Reference < css::frame::XFrame > SAL_CALL getActiveFrame() override;
    virtual void SAL_CALL setActiveFrame(const css::uno::Reference < css::frame::XFrame > & xFrame) override;

    //  XFrame

    virtual void SAL_CALL initialize(const css::uno::Reference < css::awt::XWindow > & xWindow) override;
    virtual css::uno::Reference < css::awt::XWindow > SAL_CALL getContainerWindow() override;
    virtual void SAL_CALL setCreator(const css::uno::Reference < css::frame::XFramesSupplier > & xCreator) override;
    virtual css::uno::Reference < css::frame::XFramesSupplier > SAL_CALL getCreator() override;
    virtual OUString SAL_CALL getName() override;
    virtual void SAL_CALL setName(const OUString & sName) override;
    virtual css::uno::Reference < css::frame::XFrame > SAL_CALL findFrame(
            const OUString & sTargetFrameName,
            sal_Int32 nSearchFlags) override;
    virtual sal_Bool SAL_CALL isTop() override;
    virtual void SAL_CALL activate() override;
    virtual void SAL_CALL deactivate() override;
    virtual sal_Bool SAL_CALL isActive() override;
    virtual void SAL_CALL contextChanged() override;
    virtual sal_Bool SAL_CALL setComponent(
            const css::uno::Reference < css::awt::XWindow > & xComponentWindow,
            const css::uno::Reference < css::frame::XController > & xController) override;
    virtual css::uno::Reference < css::awt::XWindow > SAL_CALL getComponentWindow() override;
    virtual css::uno::Reference < css::frame::XController > SAL_CALL getController() override;
    virtual void SAL_CALL addFrameActionListener(const css::uno::Reference < css::frame::XFrameActionListener > & xListener) override;
    virtual void SAL_CALL removeFrameActionListener(const css::uno::Reference < css::frame::XFrameActionListener > & xListener) override;

    //  XComponent

    virtual void SAL_CALL disposing() override;
    virtual void SAL_CALL addEventListener(const css::uno::Reference < css::lang::XEventListener > & xListener) override;
    virtual void SAL_CALL removeEventListener(const css::uno::Reference < css::lang::XEventListener > & xListener) override;

    //  XStatusIndicatorFactory

    virtual css::uno::Reference < css::task::XStatusIndicator > SAL_CALL createStatusIndicator() override;

    //  XDispatchProvider

    virtual css::uno::Reference < css::frame::XDispatch > SAL_CALL queryDispatch(const css::util::URL & aURL,
            const OUString & sTargetFrameName,
            sal_Int32 nSearchFlags) override;
    virtual css::uno::Sequence < css::uno::Reference < css::frame::XDispatch > > SAL_CALL queryDispatches(
            const css::uno::Sequence < css::frame::DispatchDescriptor > & lDescriptor) override;

    //  XDispatchProviderInterception

    virtual void SAL_CALL registerDispatchProviderInterceptor(
            const css::uno::Reference < css::frame::XDispatchProviderInterceptor > & xInterceptor) override;
    virtual void SAL_CALL releaseDispatchProviderInterceptor(
            const css::uno::Reference < css::frame::XDispatchProviderInterceptor > & xInterceptor) override;

    //  XDispatchInformationProvider

    virtual css::uno::Sequence < sal_Int16 > SAL_CALL getSupportedCommandGroups() override;
    virtual css::uno::Sequence < css::frame::DispatchInformation > SAL_CALL getConfigurableDispatchInformation(sal_Int16 nCommandGroup) override;

    //  XWindowListener
    //  Attention: only windowResized() and windowShown() are implemented! All others are empty!

    virtual void SAL_CALL windowResized(const css::awt::WindowEvent & aEvent) override;
    virtual void SAL_CALL windowMoved(const css::awt::WindowEvent & /*aEvent*/ ) override {};
    virtual void SAL_CALL windowShown(const css::lang::EventObject & aEvent) override;
    virtual void SAL_CALL windowHidden(const css::lang::EventObject & aEvent) override;

    //  XFocusListener
    //  Attention: focusLost() not implemented yet!

    virtual void SAL_CALL focusGained(const css::awt::FocusEvent & aEvent) override;
    virtual void SAL_CALL focusLost(const css::awt::FocusEvent & /*aEvent*/ ) override {};

    //  XTopWindowListener
    //  Attention: only windowActivated(), windowDeactivated() and windowClosing() are implemented! All others are empty!

    virtual void SAL_CALL windowActivated(const css::lang::EventObject & aEvent) override;
    virtual void SAL_CALL windowDeactivated(const css::lang::EventObject & aEvent) override;
    virtual void SAL_CALL windowOpened(const css::lang::EventObject & /*aEvent*/ ) override {};
    virtual void SAL_CALL windowClosing(const css::lang::EventObject & aEvent) override;
    virtual void SAL_CALL windowClosed(const css::lang::EventObject & /*aEvent*/ ) override {};
    virtual void SAL_CALL windowMinimized(const css::lang::EventObject & /*aEvent*/ ) override {};
    virtual void SAL_CALL windowNormalized(const css::lang::EventObject & /*aEvent*/ ) override {};

    //  XEventListener

    virtual void SAL_CALL disposing(const css::lang::EventObject & aEvent) override;

    //  XActionLockable

    virtual sal_Bool SAL_CALL isActionLocked() override;
    virtual void SAL_CALL addActionLock() override;
    virtual void SAL_CALL removeActionLock() override;
    virtual void SAL_CALL setActionLocks(sal_Int16 nLock) override;
    virtual sal_Int16 SAL_CALL resetActionLocks() override;

    //  XCloseable

    virtual void SAL_CALL close(sal_Bool bDeliverOwnership) override;

    //  XCloseBroadcaster

    virtual void SAL_CALL addCloseListener(const css::uno::Reference < css::util::XCloseListener > & xListener) override;
    virtual void SAL_CALL removeCloseListener(const css::uno::Reference < css::util::XCloseListener > & xListener) override;

    //  XTitle

    virtual OUString SAL_CALL getTitle() override;
    virtual void SAL_CALL setTitle(const OUString & sTitle) override;

    //  XTitleChangeBroadcaster

    virtual void SAL_CALL addTitleChangeListener(const css::uno::Reference < css::frame::XTitleChangeListener > & xListener) override;
    virtual void SAL_CALL removeTitleChangeListener(const css::uno::Reference < css::frame::XTitleChangeListener > & xListenr) override;

    //  XFrame2 attributes

    virtual css::uno::Reference < css::container::XNameContainer > SAL_CALL getUserDefinedAttributes() override;

    virtual css::uno::Reference < css::frame::XDispatchRecorderSupplier > SAL_CALL getDispatchRecorderSupplier() override;
    virtual void SAL_CALL setDispatchRecorderSupplier(const css::uno::Reference < css::frame::XDispatchRecorderSupplier > & ) override;

    virtual css::uno::Reference < css::uno::XInterface > SAL_CALL getLayoutManager() override;
    virtual void SAL_CALL setLayoutManager(const css::uno::Reference < css::uno::XInterface > & ) override;

    // XPropertySet
    virtual css::uno::Reference < css::beans::XPropertySetInfo > SAL_CALL getPropertySetInfo() override;

    virtual void SAL_CALL setPropertyValue(const OUString & sProperty, const css::uno::Any & aValue) override;

    virtual css::uno::Any SAL_CALL getPropertyValue(const OUString & sProperty) override;

    virtual void SAL_CALL addPropertyChangeListener(
            const OUString & sProperty,
            const css::uno::Reference < css::beans::XPropertyChangeListener > & xListener) override;

    virtual void SAL_CALL removePropertyChangeListener(
            const OUString & sProperty,
            const css::uno::Reference < css::beans::XPropertyChangeListener > & xListener) override;

    virtual void SAL_CALL addVetoableChangeListener(
            const OUString & sProperty,
            const css::uno::Reference < css::beans::XVetoableChangeListener > & xListener) override;

    virtual void SAL_CALL removeVetoableChangeListener(
            const OUString & sProperty,
            const css::uno::Reference < css::beans::XVetoableChangeListener > & xListener) override;

    // XPropertySetInfo
    virtual css::uno::Sequence < css::beans::Property > SAL_CALL getProperties() override;

    virtual css::beans::Property SAL_CALL getPropertyByName(const OUString & sName) override;

    virtual sal_Bool SAL_CALL hasPropertyByName(const OUString & sName) override;


private:

    void impl_setPropertyValue(sal_Int32 nHandle,
                                        const css::uno::Any& aValue);

    css::uno::Any impl_getPropertyValue(sal_Int32 nHandle);

    /** set a new owner for this helper.
     *
     *  This owner is used as source for all broadcasted events.
     *  Further we hold it weak, because we don't wish to be disposed() .-)
     */
    void impl_setPropertyChangeBroadcaster(XFrameImpl& rBroadcaster);

    /** add a new property info to the set of supported ones.
     *
     *  @param  aProperty
     *          describes the new property.
     *
     *  @throw  [css::beans::PropertyExistException]
     *          if a property with the same name already exists.
     *
     *  Note:   The consistence of the whole set of properties is not checked here.
     *          Meaning e.g. a handle which exists more than once is not detected.
     *          The owner of this class has to be sure, that no new property
     *          clashes with an existing one.
     */
    void impl_addPropertyInfo(const css::beans::Property& aProperty);

    /** mark the object as "dead".
     */
    void impl_disablePropertySet();

    bool impl_existsVeto(const css::beans::PropertyChangeEvent& aEvent);

    void impl_notifyChangeListener(const css::beans::PropertyChangeEvent& aEvent);

    /*-****************************************************************************************************
        @short      helper methods
        @descr      The following methods are needed at different points in our code (more than once!).

        @attention  Threadsafe methods are signed by "implts_..."!
    *//*-*****************************************************************************************************/

    // threadsafe
    void implts_sendFrameActionEvent     ( const css::frame::FrameAction&                        aAction          );
    void implts_resizeComponentWindow    (                                                                        );
    void implts_setIconOnWindow          (                                                                        );
    void implts_startWindowListening     (                                                                        );
    void implts_stopWindowListening      (                                                                        );
    void implts_checkSuicide             (                                                                        );
    void implts_forgetSubFrames          (                                                                        );

    // non threadsafe
    void impl_checkMenuCloser            (                                                                        );
    static void impl_setCloser           ( const css::uno::Reference< css::frame::XFrame2 >& xFrame , bool bState );

    void disableLayoutManager(const css::uno::Reference< css::frame::XLayoutManager2 >& xLayoutManager);

    void checkDisposed() {
        osl::MutexGuard g(rBHelper.rMutex);
        if (rBHelper.bInDispose || rBHelper.bDisposed) {
            throw css::lang::DisposedException(u"Frame disposed"_ustr);
        }
    }

//  variables
//  -threadsafe by SolarMutex

    /// reference to factory, which has created this instance
    css::uno::Reference< css::uno::XComponentContext >                      m_xContext;
    /// reference to factory helper to create status indicator objects
    css::uno::Reference< css::task::XStatusIndicatorFactory >               m_xIndicatorFactoryHelper;
    /// points to an external set progress, which should be used instead of the internal one.
    css::uno::WeakReference< css::task::XStatusIndicator >                  m_xIndicatorInterception;
    /// helper for XDispatch/Provider and interception interfaces
    rtl::Reference< InterceptionHelper >                                    m_xDispatchHelper;
    /// helper for XFrames, XIndexAccess and XElementAccess interfaces
    rtl::Reference< OFrames >                                               m_xFramesHelper;
    /// container for ALL Listeners
    comphelper::OMultiTypeInterfaceContainerHelper2                         m_aListenerContainer;
    /// parent of this frame
    css::uno::Reference< css::frame::XFramesSupplier >                      m_xParent;
    /// containerwindow of this frame for embedded components
    css::uno::Reference< css::awt::XWindow >                                m_xContainerWindow;
    /// window of the actual component
    css::uno::Reference< css::awt::XWindow >                                m_xComponentWindow;
    /// controller of the actual frame
    css::uno::Reference< css::frame::XController >                          m_xController;
    /// listen to drag & drop
    rtl::Reference< OpenFileDropTargetListener >                            m_xDropTargetListener;
    /// state, if I am a member of an active path in the tree or I have the focus or...
    EActiveState                                                            m_eActiveState;
    /// name of this frame
    OUString                                                                m_sName;
    /// frame has no parent or the parent is a task or the desktop
    bool                                                                    m_bIsFrameTop;
    /// due to FrameActionEvent
    bool                                                                    m_bConnected;
    sal_Int16                                                               m_nExternalLockCount;
    /// is used for dispatch recording and will be set/get from outside. Only the frame provides it!
    css::uno::Reference< css::frame::XDispatchRecorderSupplier >            m_xDispatchRecorderSupplier;
    /// ref counted class to support disabling commands defined by configuration file
    SvtCommandOptions                                                       m_aCommandOptions;
    /// in case of CloseVetoException on method close() was thrown by ourselves, we must close ourselves later if no internal processes are running
    bool                                                                    m_bSelfClose;
    /// indicates, if this frame is used in hidden mode or not
    bool                                                                    m_bIsHidden;
    /// The container window has WindowExtendedStyle::DocHidden set.
    bool                                                                    m_bDocHidden = false;
    /// Is used to layout the child windows of the frame.
    css::uno::Reference< css::frame::XLayoutManager2 >                      m_xLayoutManager;
    rtl::Reference< DispatchInformationProvider >                           m_xDispatchInfoHelper;
    rtl::Reference< TitleHelper >                                           m_xTitleHelper;

    std::unique_ptr<WindowCommandDispatch>                                  m_pWindowCommandDispatch;

    typedef std::unordered_map<OUString, css::beans::Property> TPropInfoHash;
    TPropInfoHash m_lProps;

    comphelper::OMultiTypeInterfaceContainerHelperVar3<css::beans::XPropertyChangeListener, OUString> m_lSimpleChangeListener;
    comphelper::OMultiTypeInterfaceContainerHelperVar3<css::beans::XVetoableChangeListener, OUString> m_lVetoChangeListener;

    // hold it weak ... otherwise this helper has to be "killed" explicitly .-)
    unotools::WeakReference< XFrameImpl > m_xBroadcaster;

    FrameContainer                                                          m_aChildFrameContainer;   /// array of child frames
    /**
     * URL of the file that is being loaded. During the loading we don't have a controller yet.
     */
    OUString m_aURL;
};


/*-****************************************************************************************************
    @short      standard constructor to create instance by factory
    @descr      This constructor initializes a new instance of this class by valid factory,
                and will be set valid values on its member and base classes.

    @attention  a)  Don't use your own reference during a UNO-Service-ctor! There is no guarantee, that you
                    will succeed (e.g. using your reference as parameter to initialize some member).
                    Do such things in the DEFINE_INIT_SERVICE() method, which is called automatically after your ctor!!!
                b)  Base class OBroadcastHelper is a typedef in namespace cppu!
                    The Microsoft compiler has some problems to handle it right by using namespace explicitly ::cppu::OBroadcastHelper.
                    If we write it without a namespace or expand the typedef to OBroadcastHelperVar<...> -> it will be OK!?
                    I don't know why! (other compilers not tested, but it works!)

    @seealso    method DEFINE_INIT_SERVICE()

    @param      xContext is the multi service manager, which creates this instance.
                    The value must be different from NULL!
    @onerror    ASSERT in debug version or nothing in release version.
*//*-*****************************************************************************************************/
XFrameImpl::XFrameImpl( css::uno::Reference< css::uno::XComponentContext >  xContext )
        : PartialWeakComponentImplHelper(m_aMutex)
        //  init member
        , m_xContext                  (std::move( xContext ))
        , m_aListenerContainer        ( m_aMutex )
        , m_eActiveState              ( E_INACTIVE )
        , m_bIsFrameTop               ( true ) // I think we are top without a parent and there is no parent yet!
        , m_bConnected                ( false ) // There exists no component inside of us => sal_False, we are not connected!
        , m_nExternalLockCount        ( 0 )
        , m_bSelfClose                ( false ) // Important!
        , m_bIsHidden                 ( true )
        , m_lSimpleChangeListener     ( m_aMutex )
        , m_lVetoChangeListener       ( m_aMutex )
{
}

void XFrameImpl::initListeners()
{
    css::uno::Reference< css::uno::XInterface > xThis(static_cast< ::cppu::OWeakObject* >(this), css::uno::UNO_QUERY_THROW);

    // Initialize a new DispatchHelper-object to handle dispatches.
    // We use this helper as a slave for our interceptor helper, not directly!
    // But the helper is an event listener in THIS instance!
    rtl::Reference<DispatchProvider> xDispatchProvider = new DispatchProvider( m_xContext, this );

    m_xDispatchInfoHelper = new DispatchInformationProvider(m_xContext, this);

    // Initialize a new interception helper object to handle dispatches and implement an interceptor mechanism.
    // Set created dispatch provider as slowest slave of it.
    // Hold interception helper by reference only - not by pointer!
    // So it's easier to destroy it.
    m_xDispatchHelper = new InterceptionHelper( this, xDispatchProvider );

    // Initialize a new XFrames-helper-object to handle XIndexAccess and XElementAccess.
    // We only hold the member as reference and not as pointer!
    // Attention: we share our frame container with this helper. Container itself is threadsafe ... So I think we can do that.
    // But look at dispose() for the right order of deinitialization.
    m_xFramesHelper = new OFrames( this, &m_aChildFrameContainer );

    // Initialize the drop target listener.
    // We only hold the member as reference and not as pointer!
    m_xDropTargetListener = new OpenFileDropTargetListener( m_xContext, this );

    // Safe impossible cases
    // We can't work without these helpers!
    SAL_WARN_IF( !xDispatchProvider.is(), "fwk.frame", "XFrameImpl::XFrameImpl(): Slowest slave for dispatch- and interception helper "
        "is not valid. XDispatchProvider, XDispatch, XDispatchProviderInterception are not full supported!" );
    SAL_WARN_IF( !m_xDispatchHelper.is(), "fwk.frame", "XFrameImpl::XFrameImpl(): Interception helper is not valid. XDispatchProvider, "
        "XDispatch, XDispatchProviderInterception are not full supported!" );
    SAL_WARN_IF( !m_xFramesHelper.is(), "fwk.frame", "XFrameImpl::XFrameImpl(): Frames helper is not valid. XFrames, "
        "XIndexAccess and XElementAccess are not supported!" );
    SAL_WARN_IF( !m_xDropTargetListener.is(), "fwk.frame", "XFrameImpl::XFrameImpl(): DropTarget helper is not valid. "
        "Drag and drop without functionality!" );

    // establish notifications for changing of "disabled commands" configuration during runtime
    m_aCommandOptions.EstablishFrameCallback(this);

    // Create an initial layout manager
    // Create layout manager and connect it to the newly created frame
    m_xLayoutManager = css::frame::LayoutManager::create(m_xContext);

    // set information about all supported properties
    impl_setPropertyChangeBroadcaster(*this);
    impl_addPropertyInfo(
        css::beans::Property(
            FRAME_PROPNAME_ASCII_DISPATCHRECORDERSUPPLIER,
            FRAME_PROPHANDLE_DISPATCHRECORDERSUPPLIER,
            cppu::UnoType<css::frame::XDispatchRecorderSupplier>::get(),
            css::beans::PropertyAttribute::TRANSIENT));
    impl_addPropertyInfo(
        css::beans::Property(
            FRAME_PROPNAME_ASCII_INDICATORINTERCEPTION,
            FRAME_PROPHANDLE_INDICATORINTERCEPTION,
            cppu::UnoType<css::task::XStatusIndicator>::get(),
            css::beans::PropertyAttribute::TRANSIENT));
    impl_addPropertyInfo(
        css::beans::Property(
            FRAME_PROPNAME_ASCII_ISHIDDEN,
            FRAME_PROPHANDLE_ISHIDDEN,
            cppu::UnoType<bool>::get(),
            css::beans::PropertyAttribute::TRANSIENT | css::beans::PropertyAttribute::READONLY));
    impl_addPropertyInfo(
        css::beans::Property(
            FRAME_PROPNAME_ASCII_LAYOUTMANAGER,
            FRAME_PROPHANDLE_LAYOUTMANAGER,
            cppu::UnoType<css::frame::XLayoutManager>::get(),
            css::beans::PropertyAttribute::TRANSIENT));
    impl_addPropertyInfo(
        css::beans::Property(
            FRAME_PROPNAME_ASCII_TITLE,
            FRAME_PROPHANDLE_TITLE,
            cppu::UnoType<OUString>::get(),
            css::beans::PropertyAttribute::TRANSIENT));
    impl_addPropertyInfo(css::beans::Property(FRAME_PROPNAME_ASCII_URL, FRAME_PROPHANDLE_URL,
                                              cppu::UnoType<OUString>::get(),
                                              css::beans::PropertyAttribute::TRANSIENT));
}

/*-************************************************************************************************************
    @interface  XComponentLoader
    @short      try to load given URL into a task
    @descr      You can give us some information about the content, which you will load into a frame.
                We search or create this target for you, make a type detection of the given URL and try to load it.
                As result of this operation we return the new created component or nothing, if loading failed.
    @param      "sURL"              , URL, which represents the content
    @param      "sTargetFrameName"  , name of target frame or special value like "_self", "_blank" ...
    @param      "nSearchFlags"      , optional arguments for frame search, if target is not a special one
    @param      "lArguments"        , optional arguments for loading
    @return     A valid component reference, if loading was successful.
                A null reference otherwise.

    @onerror    We return a null reference.
    @threadsafe yes
*//*-*************************************************************************************************************/
css::uno::Reference< css::lang::XComponent > SAL_CALL XFrameImpl::loadComponentFromURL(
        const OUString& sURL,
        const OUString& sTargetFrameName,
        sal_Int32 nSearchFlags,
        const css::uno::Sequence< css::beans::PropertyValue >& lArguments )
{
    checkDisposed();

    css::uno::Reference< css::frame::XComponentLoader > xThis(this);

    utl::MediaDescriptor aDescriptor(lArguments);
    bool bOnMainThread = aDescriptor.getUnpackedValueOrDefault(u"OnMainThread"_ustr, false);

    if (bOnMainThread)
    {
        // Make sure that we own the solar mutex, otherwise later
        // vcl::SolarThreadExecutor::execute() will release the solar mutex, even if it's owned by
        // another thread, leading to an std::abort() at the end.
        SolarMutexGuard g;

        return vcl::solarthread::syncExecute([this, xThis, sURL, sTargetFrameName, nSearchFlags, lArguments] {
            return LoadEnv::loadComponentFromURL(xThis, m_xContext, sURL, sTargetFrameName,
                                                 nSearchFlags, lArguments);
        });
    }
    else
        return LoadEnv::loadComponentFromURL(xThis, m_xContext, sURL, sTargetFrameName,
                                             nSearchFlags, lArguments);
}

/*-****************************************************************************************************
    @short      return access to append or remove children on desktop
    @descr      We don't implement this interface directly. We use a helper class to do this.
                If you wish to add or delete children to/from the container, call this method to get
                a reference to the helper.

    @seealso    class OFrames
    @return     A reference to the helper which answers your queries.

    @onerror    A null reference is returned.
*//*-*****************************************************************************************************/
css::uno::Reference< css::frame::XFrames > SAL_CALL XFrameImpl::getFrames()
{
    checkDisposed();

    SolarMutexGuard g;
    // Return access to all child frames to caller.
    // Our childframe container is implemented in helper class OFrames and used as a reference m_xFramesHelper!
    return m_xFramesHelper;
}

/*-****************************************************************************************************
    @short      get the current active child frame
    @descr      It must be a frame, too. Direct children of a frame are frames only! No task or desktop is accepted.
                We don't save this information directly in this class. We use our container-helper
                to do that.

    @seealso    class OFrameContainer
    @seealso    method setActiveFrame()
    @return     A reference to our current active childframe, if any exist.
    @return     A null reference, if nobody is active.

    @onerror    A null reference is returned.
*//*-*****************************************************************************************************/
css::uno::Reference< css::frame::XFrame > SAL_CALL XFrameImpl::getActiveFrame()
{
    checkDisposed();

    SolarMutexGuard g;
    // Return current active frame.
    // This information is available via the container.
    return m_aChildFrameContainer.getActive();
}

/*-****************************************************************************************************
    @short      set the new active direct child frame
    @descr      It must be a frame, too. Direct children of frame are frames only! No task or desktop is accepted.
                We don't save this information directly in this class. We use our container-helper
                to do that.

    @seealso    class OFrameContainer
    @seealso    method getActiveFrame()

    @param      "xFrame", reference to new active child. It must be an already existing child!
    @onerror    An assertion is thrown and element is ignored, if the given frame isn't already a child of us.
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::setActiveFrame( const css::uno::Reference< css::frame::XFrame >& xFrame )
{
    checkDisposed();

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    SolarMutexResettableGuard aWriteLock;

    // Copy necessary member for threadsafe access!
    // m_aChildFrameContainer itself is threadsafe and it lives if we live!!!
    css::uno::Reference< css::frame::XFrame > xActiveChild = m_aChildFrameContainer.getActive();
    EActiveState                              eActiveState = m_eActiveState;

    aWriteLock.clear();
    /* UNSAFE AREA --------------------------------------------------------------------------------------------- */

    // Don't work, if "new" active frame isn't different from current one!
    // (xFrame==NULL is allowed to UNSET it!)
    if( xActiveChild != xFrame )
    {
        // ... otherwise set new and deactivate old one.
        m_aChildFrameContainer.setActive( xFrame );
        if  (
                ( eActiveState      !=  E_INACTIVE  )   &&
                xActiveChild.is()
            )
        {
            xActiveChild->deactivate();
        }
    }

    if( xFrame.is() )
    {
        // If last active frame had focus ...
        // ... reset state to ACTIVE and send right FrameActionEvent for focus lost.
        if( eActiveState == E_FOCUS )
        {
            aWriteLock.reset();
            eActiveState   = E_ACTIVE;
            m_eActiveState = eActiveState;
            aWriteLock.clear();
            implts_sendFrameActionEvent( css::frame::FrameAction_FRAME_UI_DEACTIVATING );
        }

        // If last active frame was active ...
        // but new one is not it ...
        // ... set it as the active one.
        if ( eActiveState == E_ACTIVE && !xFrame->isActive() )
        {
            xFrame->activate();
        }
    }
    else
    // If this frame is active and has no active subframe anymore it is UI active too
    if( eActiveState == E_ACTIVE )
    {
        aWriteLock.reset();
        eActiveState   = E_FOCUS;
        m_eActiveState = eActiveState;
        aWriteLock.clear();
        implts_sendFrameActionEvent( css::frame::FrameAction_FRAME_UI_ACTIVATED );
    }
}

/*-****************************************************************************************************
   initialize new created layout manager
**/
void lcl_enableLayoutManager(const css::uno::Reference< css::frame::XLayoutManager2 >& xLayoutManager,
                             const css::uno::Reference< css::frame::XFrame >&         xFrame        )
{
    // Provide container window to our layout manager implementation
    xLayoutManager->attachFrame(xFrame);

    xFrame->addFrameActionListener(xLayoutManager);

    rtl::Reference<DockingAreaDefaultAcceptor> xDockingAreaAcceptor = new DockingAreaDefaultAcceptor(xFrame);
    xLayoutManager->setDockingAreaAcceptor(xDockingAreaAcceptor);
}

/*-****************************************************************************************************
   deinitialize layout manager
**/
void XFrameImpl::disableLayoutManager(const css::uno::Reference< css::frame::XLayoutManager2 >& xLayoutManager)
{
    removeFrameActionListener(xLayoutManager);
    xLayoutManager->setDockingAreaAcceptor(css::uno::Reference< css::ui::XDockingAreaAcceptor >());
    xLayoutManager->attachFrame(css::uno::Reference< css::frame::XFrame >());
}

/*-****************************************************************************************************
    @short      initialize frame instance
    @descr      A frame needs a window. This method sets a new one, but should only be called once!
                We use this window to listen for window events and to forward them to our set component.
                It's used as a parent of component window, too.

    @seealso    method getContainerWindow()
    @seealso    method setComponent()
    @seealso    member m_xContainerWindow

    @param      "xWindow", reference to a new container window - must be valid!
    @onerror    We do nothing.
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::initialize( const css::uno::Reference< css::awt::XWindow >& xWindow )
{
    /* UNSAFE AREA --------------------------------------------------------------------------------------------- */
    if (!xWindow.is())
        throw css::uno::RuntimeException(
                    u"XFrameImpl::initialize() called without a valid container window reference."_ustr,
                    static_cast< css::frame::XFrame* >(this));

    checkDisposed();

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    SolarMutexResettableGuard aWriteLock;

    // This must be the first call of this method!
    // We should initialize our object and open it for working.
    if ( m_xContainerWindow.is() )
        throw css::uno::RuntimeException(
                u"XFrameImpl::initialized() is called more than once, which is not useful nor allowed."_ustr,
                static_cast< css::frame::XFrame* >(this));

    // Set the new window.
    m_xContainerWindow = xWindow;

    // if window is initially visible, we will never get a windowShowing event
    VclPtr<vcl::Window> pWindow = VCLUnoHelper::GetWindow(xWindow);
    if (pWindow)
    {
        if (pWindow->IsVisible())
            m_bIsHidden = false;
        m_bDocHidden
            = static_cast<bool>(pWindow->GetExtendedStyle() & WindowExtendedStyle::DocHidden);
    }

    css::uno::Reference< css::frame::XLayoutManager2 >  xLayoutManager = m_xLayoutManager;

    // Release lock, because we call some impl methods, which are threadsafe by themselves.
    // If we hold this lock - we will produce our own deadlock!
    aWriteLock.clear();
    /* UNSAFE AREA --------------------------------------------------------------------------------------------- */

    // Avoid enabling the layout manager for hidden frames: it's expensive and
    // provides little value.
    if (xLayoutManager.is() && !m_bDocHidden)
        lcl_enableLayoutManager(xLayoutManager, this);

    // create progress helper
    css::uno::Reference< css::frame::XFrame > xThis (this);
    {
        css::uno::Reference< css::task::XStatusIndicatorFactory > xIndicatorFactory =
            css::task::StatusIndicatorFactory::createWithFrame(m_xContext, xThis,
                                                               false/*DisableReschedule*/, true/*AllowParentShow*/ );

        // SAFE -> ----------------------------------
        aWriteLock.reset();
        m_xIndicatorFactoryHelper = std::move(xIndicatorFactory);
        aWriteLock.clear();
        // <- SAFE ----------------------------------
    }

    // Start listening for events after setting it on helper class.
    // So superfluous messages are filtered to NULL :-)
    implts_startWindowListening();

    m_pWindowCommandDispatch.reset(new WindowCommandDispatch(m_xContext, this));

    // Initialize title functionality
    m_xTitleHelper = new TitleHelper( m_xContext, xThis, nullptr );
}

/*-****************************************************************************************************
    @short      returns currently set container window
    @descr      The ContainerWindow property is used as a container for the component
                in this frame. So this object implements a container interface too.
                The instantiation of the container window is done by the user of this class.
                The frame is the owner of its container window.

    @seealso    method initialize()
    @return     A reference to the currently set containerwindow.

    @onerror    A null reference is returned.
*//*-*****************************************************************************************************/
css::uno::Reference< css::awt::XWindow > SAL_CALL XFrameImpl::getContainerWindow()
{
    SolarMutexGuard g;
    return m_xContainerWindow;
}

/*-****************************************************************************************************
    @short      set parent frame
    @descr      We need a parent to support some functionality! e.g. findFrame()
                By the way, we use the chance to set internal information about our top state.
                So we must not check this information during every isTop() call.
                We are top, if our parent is the desktop instance or we have no parent.

    @seealso    getCreator()
    @seealso    findFrame()
    @seealso    isTop()
    @seealso    m_bIsFrameTop

    @param      xCreator
                    valid reference to our new owner frame, which should implement a supplier interface

    @threadsafe yes
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::setCreator( const css::uno::Reference< css::frame::XFramesSupplier >& xCreator )
{
    checkDisposed();

    /* SAFE { */
    {
        SolarMutexGuard aWriteLock;
        m_xParent = xCreator;
    }
    /* } SAFE */

    css::uno::Reference< css::frame::XDesktop > xIsDesktop( xCreator, css::uno::UNO_QUERY );
    m_bIsFrameTop = ( xIsDesktop.is() || ! xCreator.is() );
}

/*-****************************************************************************************************
    @short      returns current parent frame
    @descr      The Creator is the parent frame container. If it is NULL, the frame is the topmost one.

    @seealso    method setCreator()
    @return     A reference to currently set parent frame container.

    @onerror    A null reference is returned.
*//*-*****************************************************************************************************/
css::uno::Reference< css::frame::XFramesSupplier > SAL_CALL XFrameImpl::getCreator()
{
    checkDisposed();
    SolarMutexGuard g;
    return m_xParent;
}

/*-****************************************************************************************************
    @short      returns currently set name of frame
    @descr      This name is used to find the target of findFrame() or queryDispatch() calls.

    @seealso    method setName()
    @return     Current set name of frame.

    @onerror    An empty string is returned.
*//*-*****************************************************************************************************/
OUString SAL_CALL XFrameImpl::getName()
{
    SolarMutexGuard g;
    return m_sName;
}

/*-****************************************************************************************************
    @short      set new name for frame
    @descr      This name is used to find the target of findFrame() or queryDispatch() calls.

    @attention  Special names like "_blank", "_self" aren't allowed...
                "_beamer" is an exception to this rule!

    @seealso    method getName()

    @param      "sName", new frame name.
    @onerror    We do nothing.
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::setName( const OUString& sName )
{
    SolarMutexGuard g;
    // Set new name... but look for invalid special target names!
    // They are not allowed to set.
    if (TargetHelper::isValidNameForFrame(sName))
        m_sName = sName;
}

/*-****************************************************************************************************
    @short      search for frames
    @descr      This method searches for a frame with the specified name.
                Frames may contain other frames (e.g. a frameset) and may
                be contained in other frames. This hierarchy is searched by
                this method.
                First some special names are taken into account, i.e. "",
                "_self", "_top", "_blank" etc. The nSearchFlags are ignored
                when comparing these names with sTargetFrameName, further steps are
                controlled by the search flags. If allowed, the name of the frame
                itself is compared with the desired one, then ( again if allowed )
                the method findFrame() is called for all children, for siblings
                and as last for the parent frame.
                If no frame with the given name is found until the top frames container,
                a new top one is created, if this is allowed by a special
                flag. The new frame also gets the desired name.

    @param      sTargetFrameName
                    special names (_blank, _self) or real name of target frame
    @param      nSearchFlags
                    optional flags which regulate search for non special target frames

    @return     A reference to found or maybe new created frame.
    @threadsafe yes
*//*-*****************************************************************************************************/
css::uno::Reference< css::frame::XFrame > SAL_CALL XFrameImpl::findFrame( const OUString& sTargetFrameName,
                                                                     sal_Int32 nSearchFlags )
{
    css::uno::Reference< css::frame::XFrame > xTarget;

    // 0) Ignore wrong parameters!
    //    We don't support searching for the following special targets.
    //    If we reject these requests, we don't have to keep checking for such names
    //    in the code that follows. If we do not reject them, very wrong
    //    search results may occur!

    if ( sTargetFrameName == SPECIALTARGET_DEFAULT ) // valid for dispatches - not for findFrame()!
    {
        return nullptr;
    }

    // I) Check for special defined targets first which must be handled exclusive.
    //    Force using of "if() else if() ..."

    // get threadsafe some members which are necessary for the functionality that follows
    /* SAFE { */
    SolarMutexResettableGuard aReadLock;
    css::uno::Reference< css::frame::XFrame > xParent = m_xParent;
    bool bIsTopFrame  = m_bIsFrameTop;
    bool bIsTopWindow = WindowHelper::isTopWindow(m_xContainerWindow);
    aReadLock.clear();
    /* } SAFE */

    // I.I) "_blank"
    //  Not allowed for a normal frame, but for the desktop.
    //  Use helper class to do so. It uses the desktop automatically.

    if ( sTargetFrameName==SPECIALTARGET_BLANK )
    {
        TaskCreator aCreator(m_xContext);
        xTarget = aCreator.createTask(sTargetFrameName, utl::MediaDescriptor());
    }

    // I.II) "_parent"
    //  It doesn't matter if we have a valid parent or not. User asks for it and gets it.
    //  An empty result is a valid result too.

    else if ( sTargetFrameName==SPECIALTARGET_PARENT )
    {
        xTarget = std::move(xParent);
    }

    // I.III) "_top"
    //  If we are not the top frame in this hierarchy, we must forward the request to our parent.
    //  Otherwise we must return ourselves.

    else if ( sTargetFrameName==SPECIALTARGET_TOP )
    {
        if (bIsTopFrame)
            xTarget = this;
        else if (xParent.is()) // If we are not top - the parent MUST exist. But may it's better to check it again .-)
            xTarget = xParent->findFrame(SPECIALTARGET_TOP,0);
    }

    // I.IV) "_self", ""
    //  This means this frame in every case.

    else if (
        ( sTargetFrameName==SPECIALTARGET_SELF ) ||
        ( sTargetFrameName.isEmpty()           )
       )
    {
        xTarget = this;
    }

    // I.V) "_beamer"
    //  This is a special sub frame of any task. We must return it if we found it among our direct children
    //  or create it there if it does not already exist.
    //  Note: such a beamer exists for task(top) frames only!

    else if ( sTargetFrameName==SPECIALTARGET_BEAMER )
    {
        // We are a task => search or create the beamer
        if (bIsTopWindow)
        {
            xTarget = m_aChildFrameContainer.searchOnDirectChildrens(SPECIALTARGET_BEAMER);
            if ( ! xTarget.is() )
            {
                /* TODO
                    Creation not supported yet!
                    Wait for new layout manager service because we can't plug it
                    inside already opened document of this frame.
                */
            }
        }
        // We aren't a task => forward the request to our parent or ignore it.
        else if (xParent.is())
            xTarget = xParent->findFrame(SPECIALTARGET_BEAMER,0);
    }

    else
    {

        // II) Otherwise use optional given search flags.
        //  Force using of combinations of such flags. It means there is no "else" part in the used if() statements.
        //  But we must break further searches if target was already found.
        //  Order of using flags is fixed: SELF - CHILDREN - SIBLINGS - PARENT
        //  TASK and CREATE are handled as special cases.

        // get threadsafe some members which are necessary for the functionality that follows
        /* SAFE { */
        aReadLock.reset();
        OUString sOwnName = m_sName;
        aReadLock.clear();
        /* } SAFE */

        // II.I) SELF
        //  Check for the right name. If it's the searched one return ourselves, otherwise
        //  ignore this flag.

        if (
            (nSearchFlags &  css::frame::FrameSearchFlag::SELF)  &&
            (sOwnName     == sTargetFrameName                 )
           )
        {
            xTarget = this;
        }

        // II.II) CHILDREN
        //  Search among all children for the given target name.
        //  An empty name value can't occur here - because it must be already handled as "_self"
        //  before. The used helper function of the container doesn't create any frame.
        //  It only makes a deep search.

        if (
            ( ! xTarget.is()                                     ) &&
            (nSearchFlags & css::frame::FrameSearchFlag::CHILDREN)
           )
        {
            xTarget = m_aChildFrameContainer.searchOnAllChildrens(sTargetFrameName);
        }

        // II.III) TASKS
        //  This is a special flag. It regulates search only on this task tree or allows searching among
        //  all the other ones (which are sibling trees of us) as well.
        //  Upper search must stop at this frame if we are the topmost one and the TASK flag is not set
        //  or we can ignore it if we have no valid parent.

        if (
            (   bIsTopFrame && (nSearchFlags & css::frame::FrameSearchFlag::TASKS) )   ||
            ( ! bIsTopFrame                                                        )
           )
        {

            // II.III.I) SIBLINGS
            //  Search among all our direct siblings, meaning all the children of our parent.
            //  Use this flag in combination with TASK. We must suppress such an upper search if
            //  the user has not set it and if we are a top frame.
            //  Attention: don't forward this request to our parent as a findFrame() call.
            //  In such cases we must protect ourselves from recursive calls.
            //  Use a snapshot of our parent. But don't use queryFrames() of XFrames interface.
            //  Because it returns all siblings and all their children including our children,
            //  if we call it with the CHILDREN flag. We don't need that - we only need the direct container
            //  items of our parent to start searches there. So we must use the container interface
            //  XIndexAccess instead of XFrames.

            if (
                ( ! xTarget.is()                                      ) &&
                (nSearchFlags &  css::frame::FrameSearchFlag::SIBLINGS) &&
                (   xParent.is()                                      ) // search among siblings is impossible without a parent
               )
            {
                css::uno::Reference< css::frame::XFramesSupplier > xSupplier( xParent, css::uno::UNO_QUERY );
                if (xSupplier.is())
                {
                    css::uno::Reference< css::container::XIndexAccess > xContainer = xSupplier->getFrames();
                    if (xContainer.is())
                    {
                        sal_Int32 nCount = xContainer->getCount();
                        for( sal_Int32 i=0; i<nCount; ++i )
                        {
                            css::uno::Reference< css::frame::XFrame > xSibling;
                            if (
                                // control unpacking
                                ( !(xContainer->getByIndex(i)>>=xSibling) ) ||
                                // check for valid items
                                ( ! xSibling.is() ) ||
                                // ignore ourselves! (We are a part of this container too, but search among our children was already done.)
                                ( xSibling==static_cast< ::cppu::OWeakObject* >(this) )
                            )
                            {
                                continue;
                            }

                            // Don't allow upper search here! Use the appropriate flags to regulate it.
                            // And only allow deep search among children, if it was allowed for us as well.
                            sal_Int32 nRightFlags = css::frame::FrameSearchFlag::SELF;
                            if (nSearchFlags & css::frame::FrameSearchFlag::CHILDREN)
                                nRightFlags |= css::frame::FrameSearchFlag::CHILDREN;
                            xTarget = xSibling->findFrame(sTargetFrameName, nRightFlags );
                            // perform search and break further searching if a result exists
                            if (xTarget.is())
                                break;
                        }
                    }
                }
            }

            // II.III.II) PARENT
            //  Forward search to our parent (if it exists).
            //  To avoid recursive and superfluous calls (which can occur if we allow it
            //  to search among its children too) we must change the used search flags.

            if (
                ( ! xTarget.is()                                    ) &&
                (nSearchFlags &  css::frame::FrameSearchFlag::PARENT) &&
                (   xParent.is()                                    )
               )
            {
                if (xParent->getName() == sTargetFrameName)
                    xTarget = std::move(xParent);
                else
                {
                    sal_Int32 nRightFlags  = nSearchFlags & ~css::frame::FrameSearchFlag::CHILDREN;
                    xTarget = xParent->findFrame(sTargetFrameName, nRightFlags);
                }
            }
        }

        // II.IV) CREATE
        //  If we haven't found any valid target frame by using normal flags, but the user allowed us to create
        //  a new one, we should do that. The used TaskCreator uses Desktop instance automatically as parent!

        if (
            ( ! xTarget.is()                                   )    &&
            (nSearchFlags & css::frame::FrameSearchFlag::CREATE)
           )
        {
            TaskCreator aCreator(m_xContext);
            xTarget = aCreator.createTask(sTargetFrameName, utl::MediaDescriptor());
        }
    }

    return xTarget;
}

/*-****************************************************************************************************
    @descr      Returns sal_True, if this frame is a "top frame", otherwise sal_False.
                The "m_bIsFrameTop" member must be set in the ctor or setCreator() method.
                A top frame is a member of the top frame container or a member of the
                task frame container. Both containers can create new frames if the findFrame()
                method of their css::frame::XFrame interface is called with a frame name not yet known.

    @seealso    ctor
    @seealso    method setCreator()
    @seealso    method findFrame()
    @return     true, if is it a top frame ... false otherwise.

    @onerror    No error should occur!
*//*-*****************************************************************************************************/
sal_Bool SAL_CALL XFrameImpl::isTop()
{
    checkDisposed();
    SolarMutexGuard g;
    // This information is set in setCreator().
    // We are top, if our parent is a task or the desktop or if no parent exists!
    return m_bIsFrameTop;
}

/*-****************************************************************************************************
    @short      activate frame in hierarchy
    @descr      This feature is used to mark active paths in our frame hierarchy.
                You can be a listener for this event to react to it and change some internal states or something else.

    @seealso    method deactivate()
    @seealso    method isActivate()
    @seealso    enum EActiveState
    @seealso    listener mechanism
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::activate()
{
    checkDisposed();

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    SolarMutexResettableGuard aWriteLock;

    // Copy necessary member and free the lock.
    // It's not necessary for m_aChildFrameContainer, because
    // it is threadsafe by itself and lives if we live.
    css::uno::Reference< css::frame::XFrame >           xActiveChild    = m_aChildFrameContainer.getActive();
    css::uno::Reference< css::frame::XFramesSupplier >  xParent         = m_xParent;
    css::uno::Reference< css::frame::XFrame >           xThis(this);
    EActiveState                                        eState          = m_eActiveState;

    aWriteLock.clear();
    /* UNSAFE AREA --------------------------------------------------------------------------------------------- */

    //  1)  If I am not active before...
    if( eState == E_INACTIVE )
    {
        // ... do it then.
        aWriteLock.reset();
        eState         = E_ACTIVE;
        m_eActiveState = eState;
        aWriteLock.clear();
        // Deactivate sibling path and forward activation to parent, if any parent exists!
        if( xParent.is() )
        {
            // Set THIS frame as active child of parent every time and activate it.
            // We MUST have a valid path from bottom to top as active path!
            // But we must deactivate the old active sibling path first.

            // Attention: deactivation of an active path, deactivates the whole path from bottom to top!
            // But we only wish to deactivate the found sibling-tree.
            // [ see deactivate() / step 4) for further information! ]

            xParent->setActiveFrame( xThis );

            // Then we can activate from here to top.
            // Attention: we are ACTIVE now. And the parent will call activate() at us!
            // But we do nothing then! We are already activated.
            xParent->activate();
        }
        // It's necessary to send event NOW, not before.
        // Activation goes from bottom to top!
        // That's the reason to activate parent first and send event now.
        implts_sendFrameActionEvent( css::frame::FrameAction_FRAME_ACTIVATED );
    }

    //  2)  I was active before or currently activated and there is a path from here to bottom, who CAN be active.
    //      But our direct child of path is not active yet.
    //      (It can be, if activation occurs in the middle of a current path!)
    //      In this case we activate path to bottom to set focus on the correct frame!
    if ( eState == E_ACTIVE && xActiveChild.is() && !xActiveChild->isActive() )
    {
        xActiveChild->activate();
    }

    //  3)  I was active before or currently activated. But if I have no active child => I will get the focus!
    if ( eState == E_ACTIVE && !xActiveChild.is() )
    {
        aWriteLock.reset();
        eState         = E_FOCUS;
        m_eActiveState = eState;
        aWriteLock.clear();
        implts_sendFrameActionEvent( css::frame::FrameAction_FRAME_UI_ACTIVATED );
    }
}

/*-****************************************************************************************************
    @short      deactivate frame in hierarchy
    @descr      This feature is used to deactivate paths in our frame hierarchy.
                You can be a listener for this event to react to it and change some internal states or something else.

    @seealso    method activate()
    @seealso    method isActivate()
    @seealso    enum EActiveState
    @seealso    listener mechanism
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::deactivate()
{
    checkDisposed();

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    SolarMutexResettableGuard aWriteLock;

    // Copy necessary member and free the lock.
    css::uno::Reference< css::frame::XFrame > xActiveChild = m_aChildFrameContainer.getActive();
    css::uno::Reference< css::frame::XFramesSupplier > xParent = m_xParent;
    css::uno::Reference< css::frame::XFrame > xThis(this);
    EActiveState eState = m_eActiveState;

    aWriteLock.clear();
    /* UNSAFE AREA --------------------------------------------------------------------------------------------- */

    // Work only, if there is something to do!
    if( eState == E_INACTIVE )
        return;


    //  1)  Deactivate all active children.
    if ( xActiveChild.is() && xActiveChild->isActive() )
    {
        xActiveChild->deactivate();
    }

    //  2)  If I have the focus, I will lose it now.
    if( eState == E_FOCUS )
    {
        // Set new state INACTIVE(!) and send message to all listeners.
        // Don't set ACTIVE as new state. This frame is deactivated the next time due to activate().
        aWriteLock.reset();
        eState          = E_ACTIVE;
        m_eActiveState  = eState;
        aWriteLock.clear();
        implts_sendFrameActionEvent( css::frame::FrameAction_FRAME_UI_DEACTIVATING );
    }

    //  3)  If I am active, I will be deactivated now.
    if( eState == E_ACTIVE )
    {
        // Set new state and send message to all listeners.
        aWriteLock.reset();
        eState          = E_INACTIVE;
        m_eActiveState  = eState;
        aWriteLock.clear();
        implts_sendFrameActionEvent( css::frame::FrameAction_FRAME_DEACTIVATING );
    }

    //  4)  If there is a path from here to my parent,
    //      I am at the top or in the middle of a deactivated subtree and the action was started here.
    //      I must deactivate all frames from here to top, which are members of the current path.
    //      Stop, if THIS frame is not the active frame of our parent!
    if ( xParent.is() && xParent->getActiveFrame() == xThis )
    {
        // We MUST break the path - otherwise we will get the focus instead of our parent!
        // Attention: our parent will not call us again - WE ARE NOT ACTIVE YET!
        // [ see step 3 and condition "if ( m_eActiveState!=INACTIVE ) ..." in this method! ]
        xParent->deactivate();
    }
}

/*-****************************************************************************************************
    @short      returns active state
    @descr      Call it to get information about current active state of this frame.

    @seealso    method activate()
    @seealso    method deactivate()
    @seealso    enum EActiveState
    @return     true if active, false otherwise.

    @onerror    No error should occur.
*//*-*****************************************************************************************************/
sal_Bool SAL_CALL XFrameImpl::isActive()
{
    checkDisposed();
    SolarMutexGuard g;
    return m_eActiveState == E_ACTIVE || m_eActiveState == E_FOCUS;
}

/*-****************************************************************************************************
    @short      ???
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::contextChanged()
{
    // Sometimes called during closing object...
    // Impl-method itself is threadsafe!
    // Send event to all listeners for frame actions.
    implts_sendFrameActionEvent( css::frame::FrameAction_CONTEXT_CHANGED );
}

/*-****************************************************************************************************
    @short      set new component inside the frame
    @descr      A frame is a container for a component. Use this method to set, change or release it!
                We accept null references! The xComponentWindow will be a child of our container window
                and get all window events from us.

    @attention  (a) A currently set component can disagree with the suspend() request!
                    We don't set the new one and return with false then.
                (b) It's possible to set:
                        (b1) a simple component here which supports the window only - no controller;
                        (b2) a full featured component which supports window and controller;
                        (b3) or both to NULL if this component should be forgotten.

    @seealso    method getComponentWindow()
    @seealso    method getController()

    @param      xComponentWindow
                    valid reference to new component window which will be a child of internal container window
                    <NULL/> may be used for releasing.
    @param      xController
                    reference to new component controller
                    (<NULL/> may be used for releasing or setting of a simple component)

    @return     <TRUE/> if operation was successful, <FALSE/> otherwise.

    @onerror    We return <FALSE/>.
    @threadsafe yes
*//*-*****************************************************************************************************/
sal_Bool SAL_CALL XFrameImpl::setComponent(const css::uno::Reference< css::awt::XWindow >& xComponentWindow,
                                      const css::uno::Reference< css::frame::XController >& xController )
{

    // Ignore this HACK of sfx2!
    // It calls us with a valid controller without a valid window - that's not allowed!
    if  ( xController.is() && ! xComponentWindow.is() )
        return true;

    checkDisposed();

    // Get threadsafe some copies of used members.
    /* SAFE { */
    SolarMutexClearableGuard aReadLock;
    css::uno::Reference< css::awt::XWindow > xContainerWindow = m_xContainerWindow;
    css::uno::Reference< css::awt::XWindow > xOldComponentWindow = m_xComponentWindow;
    css::uno::Reference< css::frame::XController > xOldController = m_xController;
    VclPtr<vcl::Window> pOwnWindow = VCLUnoHelper::GetWindow( xContainerWindow );
    bool bHadFocus = pOwnWindow != nullptr && pOwnWindow->HasChildPathFocus();
    bool bWasConnected = m_bConnected;
    aReadLock.clear();
    /* } SAFE */

    // Stop listening on old window.
    // It may cause some trouble.
    // But don't forget to listen on new window again or reactivate listening,
    // if we reject this setComponent() request and leave this method without changing the old window.
    implts_stopWindowListening();

    // Notify all listeners, that this component (if current one exists) will be unloaded.
    if (bWasConnected)
        implts_sendFrameActionEvent( css::frame::FrameAction_COMPONENT_DETACHING );

    // Otherwise release old component first.
    // Always release controller before releasing window,
    // because controller may want to access its window!
    // But check for real changes - the new controller might be the old one.
    if (
        (xOldController.is()          )   &&
        (xOldController != xController)
       )
    {
        /* ATTENTION
            Don't suspend the old controller here. Because the outside caller must do that
            by definition. We only have to dispose it here.
         */

        // Before we dispose this controller we should hide it inside this frame instance.
        // We hold it alive for the subsequent calls by using xOldController!
        /* SAFE {*/
        {
            SolarMutexGuard aWriteLock;
            m_xController = nullptr;

            if (m_xDispatchHelper)
            {
                rtl::Reference<DispatchProvider> pDispatchProvider = m_xDispatchHelper->GetSlave();
                if (pDispatchProvider)
                {
                    pDispatchProvider->ClearProtocolHandlers();
                }
            }
        }
        /* } SAFE */

        if (xOldController.is())
        {
            try
            {
                xOldController->dispose();
            }
            catch(const css::lang::DisposedException&)
                {}
        }
        xOldController = nullptr;
    }

    // Now it's time to release the component window.
    // If the controller wasn't released successfully, this code block shouldn't be reached.
    // Because in case of "suspend()==false" we return immediately with false, see before.
    // Check for real changes, too.
    if (
        (xOldComponentWindow.is()               )   &&
        (xOldComponentWindow != xComponentWindow)
       )
    {
        /* SAFE { */
        {
            SolarMutexGuard aWriteLock;
            m_xComponentWindow = nullptr;
        }
        /* } SAFE */

        if (xOldComponentWindow.is())
        {
            try
            {
                xOldComponentWindow->dispose();
            }
            catch(const css::lang::DisposedException&)
            {
            }
        }
        xOldComponentWindow = nullptr;
    }

    // Now it's time to set the new component.
    // By the way, find out our new "load state", meaning if we have a valid component inside.
    /* SAFE { */
    SolarMutexResettableGuard aWriteLock;
    m_xComponentWindow = xComponentWindow;
    m_xController      = xController;

    // Clear the URL on the frame itself, now that the controller has it.
    m_aURL.clear();

    m_bConnected       = (m_xComponentWindow.is() || m_xController.is());
    bool bIsConnected       = m_bConnected;
    aWriteLock.clear();
    /* } SAFE */

    // notifies all interested listeners, that current component was changed or a new one was loaded
    if (bIsConnected && bWasConnected)
        implts_sendFrameActionEvent( css::frame::FrameAction_COMPONENT_REATTACHED );
    else if (bIsConnected && !bWasConnected)
        implts_sendFrameActionEvent( css::frame::FrameAction_COMPONENT_ATTACHED   );

    // A new component window doesn't know anything about current active/focus states.
    // Set this information on it!
    if ( bHadFocus && xComponentWindow.is() )
    {
        xComponentWindow->setFocus();
    }

    // If it was a new component window, we must resize it to fill out
    // our container window.
    implts_resizeComponentWindow();
    // New component should change our current icon.
    implts_setIconOnWindow();
    // OK - start listening on new window again or do nothing if it is an empty one.
    implts_startWindowListening();

    /* SAFE { */
    aWriteLock.reset();
    impl_checkMenuCloser();
    aWriteLock.clear();
    /* } SAFE */

    return true;
}

/*-****************************************************************************************************
    @short      returns currently set component window
    @descr      Frames are used to display components. The actual displayed component is
                held by the m_xComponentWindow property. If the component implements only an
                XComponent interface, the communication between the frame and the
                component is very restricted. Better integration is achievable through an
                XController interface.
                If the component wants other objects to be able to get information about its
                ResourceDescriptor it has to implement an XModel interface.
                This frame is the owner of the component window.

    @seealso    method setComponent()
    @return     css::uno::Reference to currently set component window.

    @onerror    A null reference is returned.
*//*-*****************************************************************************************************/
css::uno::Reference< css::awt::XWindow > SAL_CALL XFrameImpl::getComponentWindow()
{
    checkDisposed();
    SolarMutexGuard g;
    return m_xComponentWindow;
}

/*-****************************************************************************************************
    @short      returns currently set controller
    @descr      Frames are used to display components. The actual displayed component is
                held by the m_xComponentWindow property. If the component implements only an
                XComponent interface, the communication between the frame and the
                component is very restricted. Better integration is achievable through an
                XController interface.
                If the component wants other objects to be able to get information about its
                ResourceDescriptor it has to implement an XModel interface.
                This frame is the owner of the component window.

    @seealso    method setComponent()
    @return     css::uno::Reference to currently set controller.

    @onerror    A null reference is returned.
*//*-*****************************************************************************************************/
css::uno::Reference< css::frame::XController > SAL_CALL XFrameImpl::getController()
{
    SolarMutexGuard g;
    return m_xController;
}

/*-****************************************************************************************************
    @short      add/remove listener for activate/deactivate/contextChanged events
    @seealso    method activate()
    @seealso    method deactivate()
    @seealso    method contextChanged()

    @param      "xListener" reference to your listener object
    @onerror    Listener is ignored.
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::addFrameActionListener( const css::uno::Reference< css::frame::XFrameActionListener >& xListener )
{
    checkDisposed();
    m_aListenerContainer.addInterface( cppu::UnoType<css::frame::XFrameActionListener>::get(), xListener );
}

void SAL_CALL XFrameImpl::removeFrameActionListener( const css::uno::Reference< css::frame::XFrameActionListener >& xListener )
{
    m_aListenerContainer.removeInterface( cppu::UnoType<css::frame::XFrameActionListener>::get(), xListener );
}

/*-****************************************************************************************************
    @short      support two way mechanism to release a frame
    @descr      This method asks an internal component (controller) if it accepts this close request.
                In case of <TRUE/> nothing will happen (from point of the caller of this close method).
                In case of <FALSE/> a CloseVetoException is thrown. After such an exception, the given parameter
                <var>bDeliverOwnership</var> regulates what will be the new owner of this instance.

    @attention  It's the replacement for XTask::close() which is marked as an obsolete method.

    @param      bDeliverOwnership
                    If parameter is set to <FALSE/> the original caller will be the owner after thrown
                    veto exception and must try to close this frame again at a later time. Otherwise the
                    source of the thrown exception is the correct one. It may be the frame itself.

    @throws     CloseVetoException
                    if any internal things will not be closed

    @threadsafe yes
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::close( sal_Bool bDeliverOwnership )
{
    checkDisposed();

    // At the end of this method we might have to dispose ourselves
    // without anybody from outside holding a reference to us.
    // Thus, it's a good idea to do that ourselves.
    css::uno::Reference< css::uno::XInterface > xSelfHold( static_cast< ::cppu::OWeakObject* >(this) );

    // Try to close any listeners before we look for currently running internal processes.
    // Because if a listener disagrees with this close() request, we have time to finish these
    // internal operations too.
    // Note: container itself is threadsafe.
    css::lang::EventObject             aSource    (static_cast< ::cppu::OWeakObject*>(this));
    comphelper::OInterfaceContainerHelper2* pContainer = m_aListenerContainer.getContainer( cppu::UnoType<css::util::XCloseListener>::get());
    if (pContainer!=nullptr)
    {
        comphelper::OInterfaceIteratorHelper2 pIterator(*pContainer);
        while (pIterator.hasMoreElements())
        {
            try
            {
                static_cast<css::util::XCloseListener*>(pIterator.next())->queryClosing( aSource, bDeliverOwnership );
            }
            catch( const css::uno::RuntimeException& )
            {
                pIterator.remove();
            }
        }
    }

    // Ok - no listener disagreed with this close() request.
    // Check if this frame is used for any load process currently.
    if (isActionLocked())
    {
        if (bDeliverOwnership)
        {
            SolarMutexGuard g;
            m_bSelfClose = true;
        }

        throw css::util::CloseVetoException(u"Frame in use for loading document..."_ustr,static_cast< ::cppu::OWeakObject*>(this));
    }

    if ( ! setComponent(nullptr,nullptr) )
        throw css::util::CloseVetoException(u"Component couldn't be detached..."_ustr,static_cast< ::cppu::OWeakObject*>(this));

    // If closing is allowed, inform all listeners and dispose this frame!
    pContainer = m_aListenerContainer.getContainer( cppu::UnoType<css::util::XCloseListener>::get());
    if (pContainer!=nullptr)
    {
        comphelper::OInterfaceIteratorHelper2 pIterator(*pContainer);
        while (pIterator.hasMoreElements())
        {
            try
            {
                static_cast<css::util::XCloseListener*>(pIterator.next())->notifyClosing( aSource );
            }
            catch( const css::uno::RuntimeException& )
            {
                pIterator.remove();
            }
        }
    }

    /* SAFE { */
    {
        SolarMutexGuard aWriteLock;
        m_bIsHidden = true;
    }
    /* } SAFE */
    impl_checkMenuCloser();

    dispose();
}

/*-****************************************************************************************************
    @short      be a listener for close events!
    @descr      Adds/remove a CloseListener at this frame instance. If the close() method is called on
                this object, any such listeners are informed and can disagree with that by throwing
                a CloseVetoException.

    @seealso    XFrameImpl::close()

    @param      xListener
                    reference to your listener object

    @onerror    Listener is ignored.

    @threadsafe yes
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::addCloseListener( const css::uno::Reference< css::util::XCloseListener >& xListener )
{
    checkDisposed();
    m_aListenerContainer.addInterface( cppu::UnoType<css::util::XCloseListener>::get(), xListener );
}

void SAL_CALL XFrameImpl::removeCloseListener( const css::uno::Reference< css::util::XCloseListener >& xListener )
{
    m_aListenerContainer.removeInterface( cppu::UnoType<css::util::XCloseListener>::get(), xListener );
}

OUString SAL_CALL XFrameImpl::getTitle()
{
    checkDisposed();

    // SAFE ->
    SolarMutexClearableGuard aReadLock;
    css::uno::Reference< css::frame::XTitle > xTitle(m_xTitleHelper, css::uno::UNO_SET_THROW);
    aReadLock.clear();
    // <- SAFE

    return xTitle->getTitle();
}

void SAL_CALL XFrameImpl::setTitle( const OUString& sTitle )
{
    checkDisposed();

    // SAFE ->
    SolarMutexClearableGuard aReadLock;
    css::uno::Reference< css::frame::XTitle > xTitle(m_xTitleHelper, css::uno::UNO_SET_THROW);
    aReadLock.clear();
    // <- SAFE

    xTitle->setTitle(sTitle);
}

void SAL_CALL XFrameImpl::addTitleChangeListener( const css::uno::Reference< css::frame::XTitleChangeListener >& xListener)
{
    checkDisposed();

    // SAFE ->
    SolarMutexClearableGuard aReadLock;
    rtl::Reference< TitleHelper > xTitle(m_xTitleHelper);
    aReadLock.clear();
    // <- SAFE

    xTitle->addTitleChangeListener(xListener);
}

void SAL_CALL XFrameImpl::removeTitleChangeListener( const css::uno::Reference< css::frame::XTitleChangeListener >& xListener )
{
    checkDisposed();

    // SAFE ->
    SolarMutexClearableGuard aReadLock;
    rtl::Reference< TitleHelper > xTitle(m_xTitleHelper);
    aReadLock.clear();
    // <- SAFE

    xTitle->removeTitleChangeListener(xListener);
}

css::uno::Reference<css::container::XNameContainer> SAL_CALL XFrameImpl::getUserDefinedAttributes()
{
    // optional attribute
    return nullptr;
}

css::uno::Reference<css::frame::XDispatchRecorderSupplier> SAL_CALL XFrameImpl::getDispatchRecorderSupplier()
{
    SolarMutexGuard g;
    return m_xDispatchRecorderSupplier;
}

void SAL_CALL XFrameImpl::setDispatchRecorderSupplier(const css::uno::Reference<css::frame::XDispatchRecorderSupplier>& p)
{
    checkDisposed();
    SolarMutexGuard g;
    m_xDispatchRecorderSupplier.set(p);
}

css::uno::Reference<css::uno::XInterface> SAL_CALL XFrameImpl::getLayoutManager()
{
    SolarMutexGuard g;
    return m_xLayoutManager;
}

void SAL_CALL XFrameImpl::setLayoutManager(const css::uno::Reference<css::uno::XInterface>& p1)
{
    checkDisposed();
    SolarMutexGuard g;

    css::uno::Reference<css::frame::XLayoutManager2> xOldLayoutManager = m_xLayoutManager;
    css::uno::Reference<css::frame::XLayoutManager2> xNewLayoutManager(p1, css::uno::UNO_QUERY);

    if (xOldLayoutManager != xNewLayoutManager)
    {
        m_xLayoutManager = xNewLayoutManager;
        if (xOldLayoutManager.is())
            disableLayoutManager(xOldLayoutManager);
        if (xNewLayoutManager.is() && !m_bDocHidden)
            lcl_enableLayoutManager(xNewLayoutManager, this);
    }
}

css::uno::Reference< css::beans::XPropertySetInfo > SAL_CALL XFrameImpl::getPropertySetInfo()
{
    checkDisposed();
    return css::uno::Reference< css::beans::XPropertySetInfo >(this);
}

void SAL_CALL XFrameImpl::setPropertyValue(const OUString& sProperty,
                                           const css::uno::Any& rValue)
{
    // TODO look for e.g. readonly props and reject setProp() call!

    checkDisposed();

    // SAFE ->
    SolarMutexGuard g;

    TPropInfoHash::const_iterator pIt = m_lProps.find(sProperty);
    if (pIt == m_lProps.end())
        throw css::beans::UnknownPropertyException(sProperty);

    css::beans::Property aPropInfo = pIt->second;

    css::uno::Any aCurrentValue = impl_getPropertyValue(aPropInfo.Handle);

    bool bWillBeChanged = (aCurrentValue != rValue);
    if (! bWillBeChanged)
        return;

    css::beans::PropertyChangeEvent aEvent;
    aEvent.PropertyName   = aPropInfo.Name;
    aEvent.Further        = false;
    aEvent.PropertyHandle = aPropInfo.Handle;
    aEvent.OldValue       = std::move(aCurrentValue);
    aEvent.NewValue       = rValue;
    aEvent.Source = m_xBroadcaster;

    if (impl_existsVeto(aEvent))
        throw css::beans::PropertyVetoException();

    impl_setPropertyValue(aPropInfo.Handle, rValue);

    impl_notifyChangeListener(aEvent);
}

css::uno::Any SAL_CALL XFrameImpl::getPropertyValue(const OUString& sProperty)
{
    checkDisposed();

    // SAFE ->
    SolarMutexGuard aReadLock;

    TPropInfoHash::const_iterator pIt = m_lProps.find(sProperty);
    if (pIt == m_lProps.end())
        throw css::beans::UnknownPropertyException(sProperty);

    css::beans::Property aPropInfo = pIt->second;

    return impl_getPropertyValue(aPropInfo.Handle);
}

void SAL_CALL XFrameImpl::addPropertyChangeListener(
        const OUString& sProperty,
        const css::uno::Reference< css::beans::XPropertyChangeListener >& xListener)
{
    checkDisposed();

    // SAFE ->
    {
        SolarMutexGuard aReadLock;

        TPropInfoHash::const_iterator pIt = m_lProps.find(sProperty);
        if (pIt == m_lProps.end())
            throw css::beans::UnknownPropertyException(sProperty);
    }
    // <- SAFE

    m_lSimpleChangeListener.addInterface(sProperty, xListener);
}

void SAL_CALL XFrameImpl::removePropertyChangeListener(
        const OUString& sProperty,
        const css::uno::Reference< css::beans::XPropertyChangeListener >& xListener)
{
    // SAFE ->
    {
        SolarMutexGuard aReadLock;

        TPropInfoHash::const_iterator pIt = m_lProps.find(sProperty);
        if (pIt == m_lProps.end())
            throw css::beans::UnknownPropertyException(sProperty);
    }
    // <- SAFE

    m_lSimpleChangeListener.removeInterface(sProperty, xListener);
}

void SAL_CALL XFrameImpl::addVetoableChangeListener(
        const OUString& sProperty,
        const css::uno::Reference< css::beans::XVetoableChangeListener >& xListener)
{
    checkDisposed();

    // SAFE ->
    {
        SolarMutexGuard aReadLock;

        TPropInfoHash::const_iterator pIt = m_lProps.find(sProperty);
        if (pIt == m_lProps.end())
            throw css::beans::UnknownPropertyException(sProperty);
    }
    // <- SAFE

    m_lVetoChangeListener.addInterface(sProperty, xListener);
}

void SAL_CALL XFrameImpl::removeVetoableChangeListener(
        const OUString& sProperty,
        const css::uno::Reference< css::beans::XVetoableChangeListener >& xListener)
{
    // SAFE ->
    {
        SolarMutexGuard aReadLock;

        TPropInfoHash::const_iterator pIt = m_lProps.find(sProperty);
        if (pIt == m_lProps.end())
            throw css::beans::UnknownPropertyException(sProperty);
    }
    // <- SAFE

    m_lVetoChangeListener.removeInterface(sProperty, xListener);
}

css::uno::Sequence< css::beans::Property > SAL_CALL XFrameImpl::getProperties()
{
    checkDisposed();

    SolarMutexGuard g;

    sal_Int32 c = static_cast<sal_Int32>(m_lProps.size());
    css::uno::Sequence< css::beans::Property > lProps(c);
    auto lPropsRange = asNonConstRange(lProps);
    for (auto const& elem : m_lProps)
    {
        lPropsRange[--c] = elem.second;
    }

    return lProps;
}

css::beans::Property SAL_CALL XFrameImpl::getPropertyByName(const OUString& sName)
{
    checkDisposed();

    SolarMutexGuard g;

    TPropInfoHash::const_iterator pIt = m_lProps.find(sName);
    if (pIt == m_lProps.end())
        throw css::beans::UnknownPropertyException(sName);

    return pIt->second;
}

sal_Bool SAL_CALL XFrameImpl::hasPropertyByName(const OUString& sName)
{
    checkDisposed();

    SolarMutexGuard g;

    TPropInfoHash::iterator pIt    = m_lProps.find(sName);
    bool                                   bExist = (pIt != m_lProps.end());

    return bExist;
}

/*-****************************************************************************************************/
void XFrameImpl::implts_forgetSubFrames()
{
    // SAFE ->
    SolarMutexClearableGuard aReadLock;
    rtl::Reference< OFrames > xContainer(m_xFramesHelper);
    aReadLock.clear();
    // <- SAFE

    sal_Int32 c = xContainer->getCount();
    sal_Int32 i = 0;

    for (i=0; i<c; ++i)
    {
        try
        {
            css::uno::Reference< css::frame::XFrame > xFrame;
            xContainer->getByIndex(i) >>= xFrame;
            if (xFrame.is())
                xFrame->setCreator(css::uno::Reference< css::frame::XFramesSupplier >());
        }
        catch(const css::uno::Exception&)
        {
            // Ignore errors here.
            // Nobody can guarantee a stable index in multi threaded environments .-)
        }
    }

    SolarMutexGuard g;
    m_xFramesHelper.clear(); // clear uno reference
    m_aChildFrameContainer.clear(); // clear container content
}

/*-****************************************************************************************************
    @short      destroy instance
    @descr      The owner of this object calls the dispose method if the object
                should be destroyed. All other objects and components, that are registered
                as an EventListener are forced to release their references to this object.
                Furthermore this frame is removed from its parent frame container to release
                this reference. The reference attributes are disposed and released also.

    @attention  Look for global description at the beginning of the file, too!
                (DisposedException, FairRWLock ..., initialize, dispose)

    @seealso    method initialize()
    @seealso    base class FairRWLockBase!
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::disposing()
{
    // We should hold a reference to ourselves,
    // because our owner disposes us and releases our reference.
    // We might die before finishing this method.
    css::uno::Reference< css::frame::XFrame > xThis(this);

    SAL_INFO("fwk.frame", "[Frame] " << m_sName << " send dispose event to listener");

    // First operation should be "stop all listening for window events on our container window".
    // These events are superfluous but can cause trouble!
    // We will die, die and die...
    implts_stopWindowListening();

    css::uno::Reference<css::frame::XLayoutManager2> layoutMgr;
    {
        SolarMutexGuard g;
        layoutMgr = m_xLayoutManager;
    }
    if (layoutMgr.is()) {
        disableLayoutManager(layoutMgr);
    }

    std::unique_ptr<WindowCommandDispatch> disp;
    {
        SolarMutexGuard g;
        std::swap(disp, m_pWindowCommandDispatch);
    }
    disp.reset();

    // Send message to all listeners and forget their references.
    css::lang::EventObject aEvent( xThis );
    m_aListenerContainer.disposeAndClear( aEvent );

    // set "end of life" for our property set helper
    impl_disablePropertySet();

    // Interception/dispatch chain must be destroyed explicitly.
    // Otherwise some dispatches and/or interception objects won't die.
    rtl::Reference< InterceptionHelper > xDispatchHelper;
    {
        SolarMutexGuard g;
        xDispatchHelper = m_xDispatchHelper;
    }
    xDispatchHelper->disposing(aEvent);
    xDispatchHelper.clear();

    // Don't show any dialogs, errors or anything else anymore!
    // If dispose() without a preceding close() was called somewhere, normally no dialogs
    // should exist. Otherwise it's the problem of the outside caller.
    // Note:
    //      (a) Do it after implts_stopWindowListening(). If the order is not followed,
    //          some activate/deactivate notifications may appear, which can be fatal.
    //      (b) Don't forget to save the old value of IsDialogCancelEnabled() to
    //          restore it afterwards (to not kill headless mode).
    DialogCancelMode old = Application::GetDialogCancelMode();
    Application::SetDialogCancelMode( DialogCancelMode::Silent );

    // We should be alone forever and any further dispose calls are rejected by the preceding lines,
    // I hope :-)

    // Free references of our frame tree.
    // Force parent container to forget this frame, too
    // ( It's contained in m_xParent and so no css::lang::XEventListener for m_xParent! )
    // It's important to do that before we free some other internal structures.
    // Because if our parent gets an activate and found us as last possible active frame
    // it tries to deactivate us and we run into some trouble (DisposedExceptions!).
    css::uno::Reference<css::frame::XFramesSupplier> parent;
    {
        SolarMutexGuard g;
        std::swap(parent, m_xParent);
    }
    if( parent.is() )
    {
        parent->getFrames()->remove( xThis );
    }

    /* } SAFE */
    // Forget our internal component and its window first.
    // So we can release our container window later without problems.
    // Because this container window is the parent of the component window.
    // Note: dispose it forcefully, because suspending must be done inside close() call!
    // But try to dispose the controller first before you destroy the window.
    // Because the window is used by the controller too.
    css::uno::Reference< css::lang::XComponent > xDisposableCtrl;
    css::uno::Reference< css::lang::XComponent > xDisposableComp;
    {
        SolarMutexGuard g;
        xDisposableCtrl = m_xController;
        xDisposableComp = m_xComponentWindow;
    }
    if (xDisposableCtrl.is())
        xDisposableCtrl->dispose();
    if (xDisposableComp.is())
        xDisposableComp->dispose();

    impl_checkMenuCloser();

    css::uno::Reference<css::awt::XWindow> contWin;
    {
        SolarMutexGuard g;
        std::swap(contWin, m_xContainerWindow);
    }
    if( contWin.is() )
    {
        contWin->setVisible( false );
        // All VclComponents are XComponents; so call dispose before discarding
        // a css::uno::Reference< XVclComponent >, because this frame is the owner of the window
        contWin->dispose();
    }

    /*ATTENTION
        Clear container after successfully removing from parent container,
        because our parent could be the desktop which stands to be disposed as well!
        If we have already cleared our own container we lost our child before this could be
        removed itself at this instance.
        Release m_xFramesHelper after that - it's the same problem between parent and child!
        "m_xParent->getFrames()->remove( xThis );" needs this helper.
        Otherwise we get a null reference and could finish removing successfully.
        => You see: order of calling operations is important!!!
     */
    implts_forgetSubFrames();

    {
        SolarMutexGuard g;

        // Release some other references.
        // These calls should be easy, I hope :-)
        m_xDispatchHelper.clear();
        m_xDropTargetListener.clear();
        m_xDispatchRecorderSupplier.clear();
        m_xLayoutManager.clear();
        m_xIndicatorFactoryHelper.clear();

        // It's important to set default values here!
        // If maybe later the disposed-behaviour of this implementation is changed somewhere
        // and doesn't throw any DisposedExceptions, we must guarantee best matching default values.
        m_eActiveState       = E_INACTIVE;
        m_sName.clear();
        m_bIsFrameTop        = false;
        m_bConnected         = false;
        m_nExternalLockCount = 0;
        m_bSelfClose         = false;
        m_bIsHidden          = true;
    }

    // Don't forget to restore old value.
    // Otherwise no dialogs can be shown anymore in other frames.
    Application::SetDialogCancelMode( old );
}

/*-****************************************************************************************************
    @short      Be a listener for dispose events!
    @descr      Adds/remove an EventListener to this object. If the dispose method is called on
                this object, the disposing method of the listener is called.
    @param      "xListener" reference to your listener object.
    @onerror    Listener is ignored.
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::addEventListener( const css::uno::Reference< css::lang::XEventListener >& xListener )
{
    checkDisposed();
    m_aListenerContainer.addInterface( cppu::UnoType<css::lang::XEventListener>::get(), xListener );
}

void SAL_CALL XFrameImpl::removeEventListener( const css::uno::Reference< css::lang::XEventListener >& xListener )
{
    m_aListenerContainer.removeInterface( cppu::UnoType<css::lang::XEventListener>::get(), xListener );
}

/*-****************************************************************************************************
    @short      create new status indicator
    @descr      Use returned status indicator to show progresses and some text information.
                All created objects share the same dialog! Only the last one can show its information.

    @seealso    class StatusIndicatorFactory
    @seealso    class StatusIndicator
    @return     A reference to created object.

    @onerror    We return a null reference.
*//*-*****************************************************************************************************/
css::uno::Reference< css::task::XStatusIndicator > SAL_CALL XFrameImpl::createStatusIndicator()
{
    checkDisposed();

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    SolarMutexClearableGuard aReadLock;

    // Make snapshot of necessary members and define default return value.
    css::uno::Reference< css::task::XStatusIndicator >        xExternal(m_xIndicatorInterception.get(), css::uno::UNO_QUERY);
    css::uno::Reference< css::task::XStatusIndicatorFactory > xFactory = m_xIndicatorFactoryHelper;

    aReadLock.clear();
    /* UNSAFE AREA ----------------------------------------------------------------------------------------- */

    // Was set from outside to intercept any progress activities!
    if (xExternal.is())
        return xExternal;

    // Or use our own factory as fallback, to create such progress.
    if (xFactory.is())
        return xFactory->createStatusIndicator();

    return css::uno::Reference< css::task::XStatusIndicator >();
}

/*-****************************************************************************************************
    @short      search for target to load URL
    @descr      This method searches for a dispatch for the specified DispatchDescriptor.
                The FrameSearchFlags and the FrameName of the DispatchDescriptor are
                treated as described for findFrame.

    @seealso    method findFrame()
    @seealso    method queryDispatches()
    @seealso    method set/getName()
    @seealso    class TargetFinder

    @param      "aURL"              , URL for loading
    @param      "sTargetFrameName"  , name of target frame
    @param      "nSearchFlags"      , additional flags to regulate search if sTargetFrameName is not clear
    @return     css::uno::Reference to dispatch handler.

    @onerror    A null reference is returned.
*//*-*****************************************************************************************************/
css::uno::Reference< css::frame::XDispatch > SAL_CALL XFrameImpl::queryDispatch( const css::util::URL& aURL,
                                                                            const OUString& sTargetFrameName,
                                                                            sal_Int32 nSearchFlags)
{
    // Don't check incoming parameters here! Our helper does it for us and it is not a good idea to do it more than once!

    checkDisposed();

    // Remove uno and cmd protocol part as we want to support both of them. We store only the command part
    // in our hash map. All other protocols are stored with the protocol part.
    OUString aCommand( aURL.Main );
    if ( aURL.Protocol.equalsIgnoreAsciiCase(".uno:") )
        aCommand = aURL.Path;

    // Make std::unordered_map lookup if the current URL is in the disabled list
    if ( m_aCommandOptions.LookupDisabled( aCommand ) )
        return css::uno::Reference< css::frame::XDispatch >();
    else
    {
        // We use a helper to support this interface and an interceptor mechanism.
        rtl::Reference<InterceptionHelper> disp;
        {
            SolarMutexGuard g;
            disp = m_xDispatchHelper;
        }
        if (!disp.is()) {
            throw css::lang::DisposedException(u"Frame disposed"_ustr);
        }
        return disp->queryDispatch( aURL, sTargetFrameName, nSearchFlags );
    }
}

/*-****************************************************************************************************
    @short      handle more than one dispatch in the same call
    @descr      Returns a sequence of dispatches. For details see the queryDispatch method.
                For failed dispatches we return empty items in list!

    @seealso    method queryDispatch()

    @param      "lDescriptor" list of dispatch arguments for queryDispatch()!
    @return     List of dispatch references. Some elements can be NULL!

    @onerror    An empty list is returned.
*//*-*****************************************************************************************************/
css::uno::Sequence< css::uno::Reference< css::frame::XDispatch > > SAL_CALL XFrameImpl::queryDispatches(
        const css::uno::Sequence< css::frame::DispatchDescriptor >& lDescriptor )
{
    // Don't check incoming parameters here! Our helper does it for us and it is not a good idea to do it more than once!

    checkDisposed();

    // We use a helper to support this interface and an interceptor mechanism.
    rtl::Reference<InterceptionHelper> disp;
    {
        SolarMutexGuard g;
        disp = m_xDispatchHelper;
    }
    if (!disp.is()) {
        throw css::lang::DisposedException(u"Frame disposed"_ustr);
    }
    return disp->queryDispatches( lDescriptor );
}

/*-****************************************************************************************************
    @short      register/unregister interceptor for dispatch calls
    @descr      If you wish to handle some dispatches by yourselves, you should be
                an interceptor for it. Please see class OInterceptionHelper for further information.

    @seealso    class OInterceptionHelper

    @param      "xInterceptor", reference to your interceptor implementation.
    @onerror    Interceptor is ignored.
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::registerDispatchProviderInterceptor(
        const css::uno::Reference< css::frame::XDispatchProviderInterceptor >& xInterceptor )
{
    // We use a helper to support this interface and an interceptor mechanism.
    // This helper itself is threadsafe and checks incoming parameters, too.
    // I think we don't need any lock here!

    checkDisposed();

    rtl::Reference< InterceptionHelper > xInterceptionHelper;
    {
        SolarMutexGuard g;
        xInterceptionHelper = m_xDispatchHelper;
    }
    if (xInterceptionHelper.is()) {
        xInterceptionHelper->registerDispatchProviderInterceptor( xInterceptor );
    }
}

void SAL_CALL XFrameImpl::releaseDispatchProviderInterceptor(
        const css::uno::Reference< css::frame::XDispatchProviderInterceptor >& xInterceptor )
{
    // We use a helper to support this interface and an interceptor mechanism.
    // This helper itself is threadsafe and checks incoming parameters, too.
    // I think we don't need any lock here!

    // Sometimes we are called during our dispose() method

    rtl::Reference< InterceptionHelper > xInterceptionHelper;
    {
        SolarMutexGuard g;
        xInterceptionHelper = m_xDispatchHelper;
    }
    if (xInterceptionHelper.is()) {
        xInterceptionHelper->releaseDispatchProviderInterceptor( xInterceptor );
    }
}

/*-****************************************************************************************************
    @short      provides information about all possible dispatch functions
                inside the current frame environment
*//*-*****************************************************************************************************/
css::uno::Sequence< sal_Int16 > SAL_CALL XFrameImpl::getSupportedCommandGroups()
{
    return m_xDispatchInfoHelper->getSupportedCommandGroups();
}

css::uno::Sequence< css::frame::DispatchInformation > SAL_CALL XFrameImpl::getConfigurableDispatchInformation(
        sal_Int16 nCommandGroup)
{
    return m_xDispatchInfoHelper->getConfigurableDispatchInformation(nCommandGroup);
}

/*-****************************************************************************************************
    @short      notifications for window events
    @descr      We are a listener on our container window to forward it to our component window.

    @seealso    method setComponent()
    @seealso    member m_xContainerWindow
    @seealso    member m_xComponentWindow

    @param      "aEvent" describe source of detected event
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::windowResized( const css::awt::WindowEvent& )
{
    // Part of dispose-mechanism

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    // Impl-method is threadsafe!
    // If we have a current component window, we must resize it!
    implts_resizeComponentWindow();
}

void SAL_CALL XFrameImpl::focusGained( const css::awt::FocusEvent& )
{
    // Part of dispose() mechanism

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    SolarMutexClearableGuard aReadLock;
    // Make snapshot of member!
    css::uno::Reference< css::awt::XWindow > xComponentWindow = m_xComponentWindow;
    aReadLock.clear();
    /* UNSAFE AREA --------------------------------------------------------------------------------------------- */

    if( xComponentWindow.is() )
    {
        xComponentWindow->setFocus();
    }
}

/*-****************************************************************************************************
    @short      notifications for window events
    @descr      We are a listener on our container window to forward it to our component window,
                but we are an XTopWindowListener only if we are a top frame!

    @seealso    method setComponent()
    @seealso    member m_xContainerWindow
    @seealso    member m_xComponentWindow

    @param      "aEvent" describe source of detected event
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::windowActivated( const css::lang::EventObject& )
{
    checkDisposed();

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    SolarMutexClearableGuard aReadLock;
    // Make snapshot of member!
    EActiveState eState = m_eActiveState;
    aReadLock.clear();
    /* UNSAFE AREA --------------------------------------------------------------------------------------------- */
    // Activate the new active path from here to top.
    if( eState == E_INACTIVE )
    {
        setActiveFrame( css::uno::Reference< css::frame::XFrame >() );
        activate();
    }
}

void SAL_CALL XFrameImpl::windowDeactivated( const css::lang::EventObject& )
{
    // Sometimes called during dispose()

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    SolarMutexClearableGuard aReadLock;

    css::uno::Reference< css::frame::XFrame > xParent          = m_xParent;
    css::uno::Reference< css::awt::XWindow >  xContainerWindow = m_xContainerWindow;
    EActiveState                              eActiveState     = m_eActiveState;

    aReadLock.clear();

    if( eActiveState == E_INACTIVE )
        return;

    // Deactivation is always done implicitly by activation of another frame.
    // Only if no activation is done, deactivations have to be processed if the activated window
    // is a parent window of the last active Window!
    SolarMutexClearableGuard aSolarGuard;
    vcl::Window* pFocusWindow = Application::GetFocusWindow();
    if  ( !xContainerWindow.is() || !xParent.is() ||
          css::uno::Reference< css::frame::XDesktop >( xParent, css::uno::UNO_QUERY ).is()
        )
        return;

    css::uno::Reference< css::awt::XWindow >  xParentWindow   = xParent->getContainerWindow();
    VclPtr<vcl::Window>                       pParentWindow   = VCLUnoHelper::GetWindow( xParentWindow    );
    //#i70261#: dialogs opened from an OLE object will cause a deactivate on the frame of the OLE object
    // On Solaris/Linux at that time pFocusWindow is still NULL because the focus handling is different; right after
    // the deactivation the focus will be set into the dialog!
    // Currently I see no case where a sub frame could get a deactivate with pFocusWindow being NULL permanently
    // so for now this case is omitted from handled deactivations
    if( pFocusWindow && pParentWindow->IsChild( pFocusWindow ) )
    {
        css::uno::Reference< css::frame::XFramesSupplier > xSupplier( xParent, css::uno::UNO_QUERY );
        if( xSupplier.is() )
        {
            aSolarGuard.clear();
            xSupplier->setActiveFrame( css::uno::Reference< css::frame::XFrame >() );
        }
    }
}

void SAL_CALL XFrameImpl::windowClosing( const css::lang::EventObject& )
{
    checkDisposed();

    // deactivate this frame ...
    deactivate();

    // and try to close it
    // But do it asynchronously inside the main thread.
    // VCL doesn't like to do such things outside its main thread :-(
    // Note: the used dispatch makes it asynchronous for us .-)

    /*ATTENTION!
        Don't try to suspend the controller here! Because it's done inside used dispatch().
        Otherwise the dialog "would you save your changes?" will be shown more than once.
     */

    css::util::URL aURL;
    aURL.Complete = ".uno:CloseFrame";
    css::uno::Reference< css::util::XURLTransformer > xParser(css::util::URLTransformer::create(m_xContext));
    xParser->parseStrict(aURL);

    css::uno::Reference< css::frame::XDispatch > xCloser = queryDispatch(aURL, SPECIALTARGET_SELF, 0);
    if (xCloser.is())
        xCloser->dispatch(aURL, css::uno::Sequence< css::beans::PropertyValue >());

    // Attention: if this dispatch works synchronously and fulfills its job,
    // this line of code will never be reached.
    // Or if it will be reached it will be for sure that all your member are gone .-)
}

/*-****************************************************************************************************
    @short      react for a show event for the internal container window
    @descr      Normally we don't really need this information. But we can use it to
                implement the special feature "trigger first visible task".

                Algorithm: - first we have to check if we are a top (task) frame
                             It's not enough to be a top frame! Because we MUST have the desktop as parent.
                             But frames without a parent are top too. So it's not possible to check isTop() here!
                             We have to look for the type of our parent.
                           - if we are a task frame, then we have to check if we are the first one.
                             We use a static variable to do so. They will be reset in order to be sure
                             that further calls of this method don't do anything.
                           - Then we have to trigger the right event string on the global job executor.

    @seealso    css::task::JobExecutor

    @param      aEvent
                    describes the source of this event
                    We are not interested in this information. We are interested in the visible state only.

    @threadsafe yes
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::windowShown( const css::lang::EventObject& )
{
    static std::mutex aFirstVisibleLock;

    /* SAFE { */
    SolarMutexClearableGuard aReadLock;
    css::uno::Reference< css::frame::XDesktop > xDesktopCheck( m_xParent, css::uno::UNO_QUERY );
    m_bIsHidden = false;
    aReadLock.clear();
    /* } SAFE */

    impl_checkMenuCloser();

    if (!xDesktopCheck.is())
        return;

    static bool bFirstVisibleTask = true;
    std::unique_lock aGuard(aFirstVisibleLock);
    bool bMustBeTriggered = bFirstVisibleTask;
    bFirstVisibleTask = false;
    aGuard.unlock();

    if (bMustBeTriggered)
    {
        css::uno::Reference< css::task::XJobExecutor > xExecutor
            = css::task::theJobExecutor::get( m_xContext );
        xExecutor->trigger( u"onFirstVisibleTask"_ustr );
    }
}

void SAL_CALL XFrameImpl::windowHidden( const css::lang::EventObject& )
{
    /* SAFE { */
    {
        SolarMutexGuard aReadLock;
        m_bIsHidden = true;
    }
    /* } SAFE */

    impl_checkMenuCloser();
}

/*-****************************************************************************************************
    @short      called by dispose of our windows!
    @descr      This object is forced to release all references to the interfaces given
                by the parameter source. We are a listener at our container window and
                should listen for its disposing.

    @seealso    XWindowListener
    @seealso    XTopWindowListener
    @seealso    XFocusListener
*//*-*****************************************************************************************************/
void SAL_CALL XFrameImpl::disposing( const css::lang::EventObject& aEvent )
{
    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    SolarMutexResettableGuard aWriteLock;

    if( aEvent.Source == m_xContainerWindow )
    {
        // NECESSARY: Impl-method itself is threadsafe!
        aWriteLock.clear();
        implts_stopWindowListening();
        aWriteLock.reset();
        m_xContainerWindow.clear();
    }
}

/*-************************************************************************************************************
    @interface  com.sun.star.document.XActionLockable
    @short      implement locking of frame/task from outside
    @descr      Sometimes we have problems in deciding if closing of a task is allowed. Because frame/task
                could be used for pending loading jobs. So you can lock this object from outside and
                prevent the instance from closing it during use! But don't do it in a wrong or expensive manner.
                Otherwise the task can't die anymore!!!

    @seealso    interface XActionLockable
    @seealso    method BaseDispatcher::implts_loadIt()
    @seealso    method Desktop::loadComponentFromURL()
    @return     true if frame/task is locked
                false otherwise
    @threadsafe yes
*//*-*************************************************************************************************************/
sal_Bool SAL_CALL XFrameImpl::isActionLocked()
{
    SolarMutexGuard g;
    return( m_nExternalLockCount!=0);
}

void SAL_CALL XFrameImpl::addActionLock()
{
    SolarMutexGuard g;
    ++m_nExternalLockCount;
}

void SAL_CALL XFrameImpl::removeActionLock()
{
    {
        SolarMutexGuard g;
        SAL_WARN_IF( m_nExternalLockCount<=0, "fwk.frame", "XFrameImpl::removeActionLock(): Frame is not locked! "
            "Possible multithreading problem detected." );
        --m_nExternalLockCount;
    }

    implts_checkSuicide();
}

void SAL_CALL XFrameImpl::setActionLocks( sal_Int16 nLock )
{
    SolarMutexGuard g;
    // Attention: if resetActionLocks() is called somewhere before and gets e.g. 5 locks
    //            and tried to set these 5 here after its operations,
    //            we can't ignore set requests during these two calls!
    //            So we must add(!) these 5 locks here.
    m_nExternalLockCount = m_nExternalLockCount + nLock;
}

sal_Int16 SAL_CALL XFrameImpl::resetActionLocks()
{
    sal_Int16 nCurrentLocks = 0;
    {
        SolarMutexGuard g;
        nCurrentLocks = m_nExternalLockCount;
        m_nExternalLockCount = 0;
    }

    // Attention:
    // external lock count is 0 here every time, but if
    // member m_bSelfClose is set to true as well, we call our own close()/dispose().
    // See close() for further information
    implts_checkSuicide();

    return nCurrentLocks;
}

void XFrameImpl::impl_setPropertyValue(sal_Int32 nHandle,
                                           const css::uno::Any& aValue)

{
    /* There is no need to lock any mutex here. Because we share the
       solar mutex with our base class. And we said to our base class: "don't release it on calling us" .-)
    */

    /* Attention: you can use nHandle only, if you are sure that all supported
                  properties have a unique handle. That must be guaranteed
                  inside method initListeners()!
    */
    switch (nHandle)
    {
        case FRAME_PROPHANDLE_TITLE :
                {
                    OUString sExternalTitle;
                    aValue >>= sExternalTitle;
                    setTitle (sExternalTitle);
                }
                break;

        case FRAME_PROPHANDLE_DISPATCHRECORDERSUPPLIER :
                aValue >>= m_xDispatchRecorderSupplier;
                break;

        case FRAME_PROPHANDLE_LAYOUTMANAGER :
                {
                    css::uno::Reference< css::frame::XLayoutManager2 > xOldLayoutManager = m_xLayoutManager;
                    css::uno::Reference< css::frame::XLayoutManager2 > xNewLayoutManager;
                    aValue >>= xNewLayoutManager;

                    if (xOldLayoutManager != xNewLayoutManager)
                    {
                        m_xLayoutManager = xNewLayoutManager;
                        if (xOldLayoutManager.is())
                            disableLayoutManager(xOldLayoutManager);
                        if (xNewLayoutManager.is() && !m_bDocHidden)
                            lcl_enableLayoutManager(xNewLayoutManager, this);
                    }
                }
                break;

        case FRAME_PROPHANDLE_INDICATORINTERCEPTION :
                {
                    css::uno::Reference< css::task::XStatusIndicator > xProgress;
                    aValue >>= xProgress;
                    m_xIndicatorInterception = xProgress;
                }
                break;

        case FRAME_PROPHANDLE_URL:
        {
            aValue >>= m_aURL;
        }
        break;
        default :
                SAL_INFO("fwk.frame",  "XFrameImpl::setFastPropertyValue_NoBroadcast(): Invalid handle detected!" );
                break;
    }
}

css::uno::Any XFrameImpl::impl_getPropertyValue(sal_Int32 nHandle)
{
    /* There is no need to lock any mutex here. Because we share the
       solar mutex with our base class. And we said to our base class: "don't release it on calling us" .-)
    */

    /* Attention: you can use nHandle only, if you are sure that all supported
                  properties have a unique handle. That must be guaranteed
                  inside method initListeners()!
    */
    css::uno::Any aValue;
    switch (nHandle)
    {
        case FRAME_PROPHANDLE_TITLE :
                aValue <<= getTitle ();
                break;

        case FRAME_PROPHANDLE_DISPATCHRECORDERSUPPLIER :
                aValue <<= m_xDispatchRecorderSupplier;
                break;

        case FRAME_PROPHANDLE_ISHIDDEN :
                aValue <<= m_bIsHidden;
                break;

        case FRAME_PROPHANDLE_LAYOUTMANAGER :
                aValue <<= m_xLayoutManager;
                break;

        case FRAME_PROPHANDLE_INDICATORINTERCEPTION :
                {
                    css::uno::Reference< css::task::XStatusIndicator > xProgress(m_xIndicatorInterception.get(),
                                                                                 css::uno::UNO_QUERY);
                    aValue <<= xProgress;
                }
                break;

        case FRAME_PROPHANDLE_URL:
        {
            aValue <<= m_aURL;
        }
        break;
        default :
                SAL_INFO("fwk.frame", "XFrameImpl::getFastPropertyValue(): Invalid handle detected!" );
                break;
    }

    return aValue;
}

void XFrameImpl::impl_setPropertyChangeBroadcaster(XFrameImpl& xBroadcaster)
{
    SolarMutexGuard g;
    m_xBroadcaster = &xBroadcaster;
}

void XFrameImpl::impl_addPropertyInfo(const css::beans::Property& aProperty)
{
    SolarMutexGuard g;

    TPropInfoHash::const_iterator pIt = m_lProps.find(aProperty.Name);
    if (pIt != m_lProps.end())
        throw css::beans::PropertyExistException();

    m_lProps[aProperty.Name] = aProperty;
}

void XFrameImpl::impl_disablePropertySet()
{
    SolarMutexGuard g;

    css::uno::Reference< css::uno::XInterface > xThis(static_cast< css::beans::XPropertySet* >(this), css::uno::UNO_QUERY);
    css::lang::EventObject aEvent(xThis);

    m_lSimpleChangeListener.disposeAndClear(aEvent);
    m_lVetoChangeListener.disposeAndClear(aEvent);
    m_lProps.clear();
}

bool XFrameImpl::impl_existsVeto(const css::beans::PropertyChangeEvent& aEvent)
{
    /*  Don't use the lock here!
        The used helper is threadsafe and it lives for the whole lifetime of
        our own object.
    */
    ::comphelper::OInterfaceContainerHelper3<css::beans::XVetoableChangeListener>* pVetoListener = m_lVetoChangeListener.getContainer(aEvent.PropertyName);
    if (! pVetoListener)
        return false;

    ::comphelper::OInterfaceIteratorHelper3 pListener(*pVetoListener);
    while (pListener.hasMoreElements())
    {
        try
        {
            pListener.next()->vetoableChange(aEvent);
        }
        catch(const css::uno::RuntimeException&)
            { pListener.remove(); }
        catch(const css::beans::PropertyVetoException&)
            { return true; }
    }

    return false;
}

void XFrameImpl::impl_notifyChangeListener(const css::beans::PropertyChangeEvent& aEvent)
{
    /*  Don't use the lock here!
        The used helper is threadsafe and it lives for the whole lifetime of
        our own object.
    */
    ::comphelper::OInterfaceContainerHelper3<css::beans::XPropertyChangeListener>* pSimpleListener = m_lSimpleChangeListener.getContainer(aEvent.PropertyName);
    if (! pSimpleListener)
        return;

    ::comphelper::OInterfaceIteratorHelper3 pListener(*pSimpleListener);
    while (pListener.hasMoreElements())
    {
        try
        {
            pListener.next()->propertyChange(aEvent);
        }
        catch(const css::uno::RuntimeException&)
            { pListener.remove(); }
    }
}

/*-****************************************************************************************************
    @short      send frame action event to our listener
    @descr      This method is threadsafe AND can be called by our dispose method too!
    @param      "aAction", describe the event for sending
*//*-*****************************************************************************************************/
void XFrameImpl::implts_sendFrameActionEvent( const css::frame::FrameAction& aAction )
{
    // Sometimes used by dispose()

    // Log information about order of events to file!
    // (only activated in debug version!)
    SAL_INFO( "fwk.frame",
              "[Frame] " << m_sName << " send event " <<
              (aAction == css::frame::FrameAction_COMPONENT_ATTACHED ? u"COMPONENT ATTACHED"_ustr :
               (aAction == css::frame::FrameAction_COMPONENT_DETACHING ? u"COMPONENT DETACHING"_ustr :
                (aAction == css::frame::FrameAction_COMPONENT_REATTACHED ? u"COMPONENT REATTACHED"_ustr :
                 (aAction == css::frame::FrameAction_FRAME_ACTIVATED ? u"FRAME ACTIVATED"_ustr :
                  (aAction == css::frame::FrameAction_FRAME_DEACTIVATING ? u"FRAME DEACTIVATING"_ustr :
                   (aAction == css::frame::FrameAction_CONTEXT_CHANGED ? u"CONTEXT CHANGED"_ustr :
                    (aAction == css::frame::FrameAction_FRAME_UI_ACTIVATED ? u"FRAME UI ACTIVATED"_ustr :
                     (aAction == css::frame::FrameAction_FRAME_UI_DEACTIVATING ? u"FRAME UI DEACTIVATING"_ustr :
                      (aAction == css::frame::FrameAction::FrameAction_MAKE_FIXED_SIZE ? u"MAKE_FIXED_SIZE"_ustr :
                       u"*invalid*"_ustr))))))))));

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    // Send css::frame::FrameAction event to all listeners.
    // Get container for right listener.
    // THE FOLLOWING LINES ARE THREADSAFE!!!
    // ( OInterfaceContainerHelper2 is synchronized with m_aListenerContainer! )
    comphelper::OInterfaceContainerHelper2* pContainer = m_aListenerContainer.getContainer(
        cppu::UnoType<css::frame::XFrameActionListener>::get());

    if( pContainer == nullptr )
        return;

    // Build action event.
    css::frame::FrameActionEvent aFrameActionEvent( static_cast< ::cppu::OWeakObject* >(this), this, aAction );

    // Get iterator for access to listener.
    comphelper::OInterfaceIteratorHelper2 aIterator( *pContainer );
    // Send message to all listeners.
    while( aIterator.hasMoreElements() )
    {
        try
        {
            static_cast<css::frame::XFrameActionListener*>(aIterator.next())->frameAction( aFrameActionEvent );
        }
        catch( const css::uno::RuntimeException& )
        {
            aIterator.remove();
        }
    }
}

/*-****************************************************************************************************
    @short      helper to resize our component window
    @descr      A frame contains 2 windows: a container and a component window.
                This method resizes the inner component window to the full size of the outer container window.
                This method is threadsafe AND can be called by our dispose method too!
*//*-*****************************************************************************************************/
void XFrameImpl::implts_resizeComponentWindow()
{
    // usually the LayoutManager does the resizing
    // in case there is no LayoutManager resizing has to be done here
    if ( m_xLayoutManager.is() )
        return;

    css::uno::Reference< css::awt::XWindow > xComponentWindow( getComponentWindow() );
    if( !xComponentWindow.is() )
        return;

    css::uno::Reference< css::awt::XDevice > xDevice( getContainerWindow(), css::uno::UNO_QUERY );

    // Convert relative size to output size.
    css::awt::Rectangle  aRectangle  = getContainerWindow()->getPosSize();
    css::awt::DeviceInfo aInfo = xDevice->getInfo();
    css::awt::Size aSize( aRectangle.Width - aInfo.LeftInset - aInfo.RightInset,
                          aRectangle.Height - aInfo.TopInset - aInfo.BottomInset );

    // Resize our component window.
    xComponentWindow->setPosSize( 0, 0, aSize.Width, aSize.Height, css::awt::PosSize::POSSIZE );
}

/*-****************************************************************************************************
    @short      helper to set icon on our container window (if it is a system window!)
    @descr      We use our internal set controller (if it exists) to specify which factory it represents.
                This information can be used to find the right icon. But our controller can tell it to us directly,
                too. We should ask its optional property set first.

    @seealso    method Window::SetIcon()
    @onerror    We do nothing.
*//*-*****************************************************************************************************/
void XFrameImpl::implts_setIconOnWindow()
{
    checkDisposed();

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    // Make snapshot of necessary members and release lock.
    SolarMutexClearableGuard aReadLock;
    css::uno::Reference< css::awt::XWindow > xContainerWindow = m_xContainerWindow;
    css::uno::Reference< css::frame::XController > xController = m_xController;
    aReadLock.clear();
    /* UNSAFE AREA --------------------------------------------------------------------------------------------- */

    if( !(xContainerWindow.is() && xController.is()) )
        return;


    // a) set default value to an invalid one. So we can start further searches for right icon id, if
    //    first steps failed!
    //    We must reset it to any fallback value - if no search step returns a valid result.
    sal_Int32 nIcon = -1;

    // b) try to find information on controller propertyset directly
    //    Don't forget to catch possible exceptions, because this property is an optional one!
    css::uno::Reference< css::beans::XPropertySet > xSet( xController, css::uno::UNO_QUERY );
    if( xSet.is() )
    {
        try
        {
            css::uno::Reference< css::beans::XPropertySetInfo > const xPSI( xSet->getPropertySetInfo(),
                                                                            css::uno::UNO_SET_THROW );
            if ( xPSI->hasPropertyByName( u"IconId"_ustr ) )
                xSet->getPropertyValue( u"IconId"_ustr ) >>= nIcon;
        }
        catch( css::uno::Exception& )
        {
            DBG_UNHANDLED_EXCEPTION("fwk");
        }
    }

    // c) if b) failed, analyze argument list of currently loaded document inside the frame to find the filter.
    //    It can be used to detect the right factory and these can be used to match factory to icon.
    if( nIcon == -1 )
    {
        css::uno::Reference< css::frame::XModel > xModel = xController->getModel();
        if( xModel.is() )
        {
            SvtModuleOptions::EFactory eFactory = SvtModuleOptions::ClassifyFactoryByModel(xModel);
            if (eFactory != SvtModuleOptions::EFactory::UNKNOWN_FACTORY)
                nIcon = SvtModuleOptions().GetFactoryIcon( eFactory );
        }
    }

    // d) if all steps failed, use fallback!
    if( nIcon == -1 )
    {
        nIcon = 0;
    }

    // e) set icon on container window now
    //    Don't forget SolarMutex! We use vcl directly :-(
    //    Check window pointer for right WorkWindow class too!!!
    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    {
        SolarMutexGuard aSolarGuard;
        VclPtr<vcl::Window> pWindow = VCLUnoHelper::GetWindow( xContainerWindow );
        if(
            ( pWindow            != nullptr              ) &&
            ( pWindow->GetType() == WindowType::WORKWINDOW )
            )
        {
            WorkWindow* pWorkWindow = static_cast<WorkWindow*>(pWindow.get());
            pWorkWindow->SetIcon( static_cast<sal_uInt16>(nIcon) );
        }
    }
    /* UNSAFE AREA --------------------------------------------------------------------------------------------- */
}

/*-************************************************************************************************************
    @short      helper to start/stop listening for window events on container window
    @descr      If we get a new container window, we must set it on an internal member
                and stop listening at the old one and start listening at the new one!
                But sometimes (in dispose() call!) it's necessary to stop listening without starting
                on new connections. So we split this functionality to make it easier to use.

    @seealso    method initialize()
    @seealso    method dispose()
    @onerror    We do nothing!
    @threadsafe yes
*//*-*************************************************************************************************************/
void XFrameImpl::implts_startWindowListening()
{
    checkDisposed();

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    // Make snapshot of necessary members!
    SolarMutexClearableGuard aReadLock;
    css::uno::Reference< css::awt::XWindow > xContainerWindow = m_xContainerWindow;
    rtl::Reference< OpenFileDropTargetListener > xDragDropListener = m_xDropTargetListener;
    css::uno::Reference< css::awt::XWindowListener > xWindowListener(this);
    css::uno::Reference< css::awt::XFocusListener > xFocusListener(this);
    css::uno::Reference< css::awt::XTopWindowListener > xTopWindowListener(this);
    aReadLock.clear();
    /* UNSAFE AREA --------------------------------------------------------------------------------------------- */

    if( !xContainerWindow.is() )
        return;

    xContainerWindow->addWindowListener( xWindowListener);
    xContainerWindow->addFocusListener ( xFocusListener );

    css::uno::Reference< css::awt::XTopWindow > xTopWindow( xContainerWindow, css::uno::UNO_QUERY );
    if( xTopWindow.is() )
    {
        xTopWindow->addTopWindowListener( xTopWindowListener );

        css::uno::Reference< css::awt::XToolkit2 > xToolkit = css::awt::Toolkit::create( m_xContext );
        css::uno::Reference< css::datatransfer::dnd::XDropTarget > xDropTarget = xToolkit->getDropTarget( xContainerWindow );
        if( xDropTarget.is() )
        {
            xDropTarget->addDropTargetListener( xDragDropListener );
            xDropTarget->setActive( true );
        }
    }
}

void XFrameImpl::implts_stopWindowListening()
{
    // Sometimes used by dispose()

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    // Make snapshot of necessary members!
    SolarMutexClearableGuard aReadLock;
    css::uno::Reference< css::awt::XWindow > xContainerWindow = m_xContainerWindow;
    rtl::Reference< OpenFileDropTargetListener > xDragDropListener = m_xDropTargetListener;
    css::uno::Reference< css::awt::XWindowListener > xWindowListener(this);
    css::uno::Reference< css::awt::XFocusListener > xFocusListener(this);
    css::uno::Reference< css::awt::XTopWindowListener > xTopWindowListener(this);
    aReadLock.clear();
    /* UNSAFE AREA --------------------------------------------------------------------------------------------- */

    if( !xContainerWindow.is() )
        return;

    xContainerWindow->removeWindowListener( xWindowListener);
    xContainerWindow->removeFocusListener ( xFocusListener );

    css::uno::Reference< css::awt::XTopWindow > xTopWindow( xContainerWindow, css::uno::UNO_QUERY );
    if( !xTopWindow.is() )
        return;

    xTopWindow->removeTopWindowListener( xTopWindowListener );

    css::uno::Reference< css::awt::XToolkit2 > xToolkit = css::awt::Toolkit::create( m_xContext );
    css::uno::Reference< css::datatransfer::dnd::XDropTarget > xDropTarget =
        xToolkit->getDropTarget( xContainerWindow );
    if( xDropTarget.is() )
    {
        xDropTarget->removeDropTargetListener( xDragDropListener );
        xDropTarget->setActive( false );
    }
}

/*-****************************************************************************************************
    @short      helper to force broken close() request again
    @descr      If we disagree with a close() request, and detect that all external locks are gone,
                then we must try to close this frame again.

    @seealso    XCloseable::close()
    @seealso    XFrameImpl::close()
    @seealso    XFrameImpl::removeActionLock()
    @seealso    XFrameImpl::resetActionLock()
    @seealso    m_bSelfClose
    @seealso    m_nExternalLockCount

    @threadsafe yes
*//*-*****************************************************************************************************/
void XFrameImpl::implts_checkSuicide()
{
    /* SAFE */
    SolarMutexClearableGuard aReadLock;
    // in case of lock==0 and saved state of previous close() request m_bSelfClose
    // we must force close() again. Because we had disagreed with that before.
    bool bSuicide = (m_nExternalLockCount==0 && m_bSelfClose);
    m_bSelfClose = false;
    aReadLock.clear();
    /* } SAFE */
    // Force close and deliver ownership to source of possible thrown veto exception.
    // Attention: because this method is not designed to throw such an exception we must suppress
    // it for outside code!
    try
    {
        if (bSuicide)
            close(true);
    }
    catch(const css::util::CloseVetoException&)
        {}
    catch(const css::lang::DisposedException&)
        {}
}

/** a small helper to enable/disable the menu closer at the menubar of the given frame.

    @param  xFrame
            we use its layout manager to set/reset a special callback.
            Its existence regulates visibility of this closer item.

    @param  bState
                <TRUE/> enable; <FALSE/> disable this state
 */
// static
void XFrameImpl::impl_setCloser( /*IN*/ const css::uno::Reference< css::frame::XFrame2 >& xFrame ,
                            /*IN*/       bool                                   bState  )
{
    // Note: if start module is not installed, no closer has to be shown!
    if (!SvtModuleOptions().IsModuleInstalled(SvtModuleOptions::EModule::STARTMODULE))
        return;

    try
    {
        css::uno::Reference< css::beans::XPropertySet > xFrameProps(xFrame, css::uno::UNO_QUERY_THROW);
        css::uno::Reference< css::frame::XLayoutManager > xLayoutManager;
        xFrameProps->getPropertyValue(FRAME_PROPNAME_ASCII_LAYOUTMANAGER) >>= xLayoutManager;
        css::uno::Reference< css::beans::XPropertySet > xLayoutProps(xLayoutManager, css::uno::UNO_QUERY_THROW);
        xLayoutProps->setPropertyValue(LAYOUTMANAGER_PROPNAME_MENUBARCLOSER, css::uno::Any(bState));
    }
    catch(const css::uno::RuntimeException&)
        { throw; }
    catch(const css::uno::Exception&)
        {}
}

/** it checks, which of the top level task frames must have the special menu closer for
    switching to the backing window mode.

    It analyzes the current list of visible top level frames. Only the last real document
    frame can have this symbol. Not the help frame nor the backing task itself.
    Here we do anything related to this closer. We remove it from the old frame and set it
    for the new one.
 */

void XFrameImpl::impl_checkMenuCloser()
{
    /* SAFE { */
    SolarMutexClearableGuard aReadLock;

    // Only top frames, which are part of our desktop hierarchy, can
    // do so! By the way, we need the desktop instance to have access
    // to all other top level frames too.
    css::uno::Reference< css::frame::XDesktop > xDesktop (m_xParent, css::uno::UNO_QUERY);
    css::uno::Reference< css::frame::XFramesSupplier > xTaskSupplier(xDesktop , css::uno::UNO_QUERY);
    if ( !xDesktop.is() || !xTaskSupplier.is() )
        return;

    aReadLock.clear();
    /* } SAFE */

    // Analyze the list of current open tasks.
    // Suppress search for other views to the same model.
    // It's not needed here and can be very expensive.
    FrameListAnalyzer aAnalyzer(
        xTaskSupplier,
        this,
        FrameAnalyzerFlags::Hidden | FrameAnalyzerFlags::Help | FrameAnalyzerFlags::BackingComponent);

    // specify the new frame, which must have this special state...
    css::uno::Reference< css::frame::XFrame2 > xNewCloserFrame;

    // a)
    // If there exists at least one other frame, there are two frames currently open.
    // But we can only enable this closer, if one of these two tasks includes the help module.
    // The "other frame" couldn't be the help. Because then it wouldn't be part of this "other list".
    // In such a case it will be separated to the reference aAnalyzer.m_xHelp!
    // But we must check, if we ourselves include the help.
    // Check aAnalyzer.m_bReferenceIsHelp!
    if (
        (aAnalyzer.m_lOtherVisibleFrames.size()==1)   &&
        (
            (aAnalyzer.m_bReferenceIsHelp  ) ||
            (aAnalyzer.m_bReferenceIsHidden)
        )
       )
    {
        // others[0] can't be the backing component!
        // Because it's set at the special member aAnalyzer.m_xBackingComponent ... :-)
        xNewCloserFrame.set( aAnalyzer.m_lOtherVisibleFrames[0], css::uno::UNO_QUERY_THROW );
    }

    // b)
    // There is no other frame, meaning no other document frame. The help module
    // will be handled separately and must(!) be ignored here, except if we ourselves include the help.
    else if (
        (aAnalyzer.m_lOtherVisibleFrames.empty()) &&
        (!aAnalyzer.m_bReferenceIsHelp) &&
        (!aAnalyzer.m_bReferenceIsHidden) &&
        (!aAnalyzer.m_bReferenceIsBacking)
       )
    {
        xNewCloserFrame = this;
    }

    // Look for necessary actions.
    // Only if the closer state must be moved from one frame to another one
    // or must be enabled/disabled at all.
    SolarMutexGuard aGuard;
    // Holds the only frame, which must show the special closer menu item (can be NULL!)
    static css::uno::WeakReference< css::frame::XFrame2 > s_xCloserFrame;
    css::uno::Reference< css::frame::XFrame2 > xCloserFrame (s_xCloserFrame.get(), css::uno::UNO_QUERY);
    if (xCloserFrame!=xNewCloserFrame)
    {
        if (xCloserFrame.is())
            impl_setCloser(xCloserFrame, false);
        if (xNewCloserFrame.is())
            impl_setCloser(xNewCloserFrame, true);
        s_xCloserFrame = xNewCloserFrame;
    }
}

}

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface *
com_sun_star_comp_framework_Frame_get_implementation(
    css::uno::XComponentContext *context,
    css::uno::Sequence<css::uno::Any> const &)
{
    rtl::Reference<XFrameImpl> inst = new XFrameImpl(context);
    css::uno::XInterface *acquired_inst = cppu::acquire(inst.get());

    inst->initListeners();

    return acquired_inst;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
