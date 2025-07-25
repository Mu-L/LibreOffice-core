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

#include <rtl/ref.hxx>
#include <cppuhelper/weakref.hxx>
#include <utility>
#include <vcl/window.hxx>
#include <svx/svdmodel.hxx>
#include <svx/unomod.hxx>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <list>
#include <vector>
#include <accmap.hxx>
#include "acccontext.hxx"
#include "accdoc.hxx"
#include <strings.hrc>
#include "accpreview.hxx"
#include "accpage.hxx"
#include "accpara.hxx"
#include "accheaderfooter.hxx"
#include "accfootnote.hxx"
#include "acctextframe.hxx"
#include "accgraphic.hxx"
#include "accembedded.hxx"
#include "acccell.hxx"
#include "acctable.hxx"
#include <fesh.hxx>
#include <istype.hxx>
#include <rootfrm.hxx>
#include <txtfrm.hxx>
#include <hffrm.hxx>
#include <ftnfrm.hxx>
#include <cellfrm.hxx>
#include <tabfrm.hxx>
#include <pagefrm.hxx>
#include <flyfrm.hxx>
#include <ndtyp.hxx>
#include <IDocumentDrawModelAccess.hxx>
#include <svx/AccessibleShapeInfo.hxx>
#include <svx/ShapeTypeHandler.hxx>
#include <svx/SvxShapeTypes.hxx>
#include <svx/svdpage.hxx>
#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/accessibility/AccessibleRole.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/document/XShapeEventBroadcaster.hpp>
#include <cppuhelper/implbase.hxx>
#include <comphelper/interfacecontainer4.hxx>
#include <pagepreviewlayout.hxx>
#include <dcontact.hxx>
#include <svx/svdmark.hxx>
#include <doc.hxx>
#include <drawdoc.hxx>
#include <pam.hxx>
#include <ndtxt.hxx>
#include <dflyobj.hxx>
#include <prevwpage.hxx>
#include <calbck.hxx>
#include <undobj.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <tools/debug.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::accessibility;
using namespace ::sw::access;

namespace {

class SwDrawModellListener_Impl : public SfxListener,
    public ::cppu::WeakImplHelper< document::XShapeEventBroadcaster >
{
    mutable std::mutex maListenerMutex;
    ::comphelper::OInterfaceContainerHelper4<css::document::XEventListener> maEventListeners;
    std::unordered_multimap<css::uno::Reference< css::drawing::XShape >, css::uno::Reference< css::document::XShapeEventListener >> maShapeListeners;
    SdrModel *mpDrawModel;
protected:
    virtual ~SwDrawModellListener_Impl() override;

public:
    explicit SwDrawModellListener_Impl( SdrModel& rDrawModel );

    // css::document::XEventBroadcaster
    virtual void SAL_CALL addEventListener( const uno::Reference< document::XEventListener >& xListener ) override;
    virtual void SAL_CALL removeEventListener( const uno::Reference< document::XEventListener >& xListener ) override;
    // css::document::XShapeEventBroadcaster
    virtual void SAL_CALL addShapeEventListener( const css::uno::Reference< css::drawing::XShape >& xShape, const css::uno::Reference< css::document::XShapeEventListener >& xListener ) override;
    virtual void SAL_CALL removeShapeEventListener( const css::uno::Reference< css::drawing::XShape >& xShape, const css::uno::Reference< css::document::XShapeEventListener >& xListener ) override;

    virtual void        Notify( SfxBroadcaster& rBC, const SfxHint& rHint ) override;
    void Dispose();
};

}

SwDrawModellListener_Impl::SwDrawModellListener_Impl( SdrModel& rDrawModel ) :
    mpDrawModel( &rDrawModel )
{
    StartListening( *mpDrawModel );
}

SwDrawModellListener_Impl::~SwDrawModellListener_Impl()
{
    Dispose();
}

void SAL_CALL SwDrawModellListener_Impl::addEventListener( const uno::Reference< document::XEventListener >& xListener )
{
    std::unique_lock g(maListenerMutex);
    maEventListeners.addInterface( g, xListener );
}

void SAL_CALL SwDrawModellListener_Impl::removeEventListener( const uno::Reference< document::XEventListener >& xListener )
{
    std::unique_lock g(maListenerMutex);
    maEventListeners.removeInterface( g, xListener );
}

void SAL_CALL SwDrawModellListener_Impl::addShapeEventListener(
                const css::uno::Reference< css::drawing::XShape >& xShape,
                const uno::Reference< document::XShapeEventListener >& xListener )
{
    assert(xShape.is() && "no shape?");
    std::unique_lock aGuard(maListenerMutex);
    maShapeListeners.emplace(xShape, xListener);
}

void SAL_CALL SwDrawModellListener_Impl::removeShapeEventListener(
                const css::uno::Reference< css::drawing::XShape >& xShape,
                const uno::Reference< document::XShapeEventListener >& xListener )
{
    std::unique_lock aGuard(maListenerMutex);
    auto [itBegin, itEnd] = maShapeListeners.equal_range(xShape);
    for (auto it = itBegin; it != itEnd; ++it)
        if (it->second == xListener)
        {
            maShapeListeners.erase(it);
            return;
        }
}

void SwDrawModellListener_Impl::Notify( SfxBroadcaster& /*rBC*/,
        const SfxHint& rHint )
{
    // do not broadcast notifications for writer fly frames, because there
    // are no shapes that need to know about them.
    if (rHint.GetId() != SfxHintId::ThisIsAnSdrHint)
        return;
    const SdrHint *pSdrHint = static_cast<const SdrHint*>( &rHint );
    const SdrObject* pObj = pSdrHint->GetObject();
    if (pObj &&
           ( dynamic_cast< const SwFlyDrawObj* >(pObj) ||
             dynamic_cast< const SwVirtFlyDrawObj* >(pObj) ||
             pObj->GetObjIdentifier() == SdrObjKind::NewFrame ) )
    {
        return;
    }

    OSL_ENSURE( mpDrawModel, "draw model listener is disposed" );
    if( !mpDrawModel )
        return;

    document::EventObject aEvent;
    if( !SvxUnoDrawMSFactory::createEvent( mpDrawModel, pSdrHint, aEvent ) )
        return;

    {
        std::unique_lock g(maListenerMutex);
        ::comphelper::OInterfaceIteratorHelper4 aIter( g, maEventListeners );
        g.unlock();
        while( aIter.hasMoreElements() )
        {
            try
            {
                aIter.next()->notifyEvent( aEvent );
            }
            catch( uno::RuntimeException const & )
            {
                TOOLS_WARN_EXCEPTION("sw.a11y", "Runtime exception caught while notifying shape");
            }
        }
    }

    // right now, we're only handling the specific event necessary to fix this performance problem
    if (pSdrHint->GetKind() == SdrHintKind::ObjectChange)
    {
        auto pSdrObject = const_cast<SdrObject*>(pSdrHint->GetObject());
        uno::Reference<drawing::XShape> xShape(pSdrObject->getUnoShape(), uno::UNO_QUERY);
        std::unique_lock aGuard(maListenerMutex);
        auto [itBegin, itEnd] = maShapeListeners.equal_range(xShape);
        for (auto it = itBegin; it != itEnd; ++it)
            it->second->notifyShapeEvent(aEvent);
    }
}

void SwDrawModellListener_Impl::Dispose()
{
    if (mpDrawModel != nullptr) {
        EndListening( *mpDrawModel );
    }
    mpDrawModel = nullptr;
}

typedef std::pair < const SdrObject *, ::rtl::Reference < ::accessibility::AccessibleShape > > SwAccessibleObjShape_Impl;

class SwAccessibleShapeMap_Impl
{
public:

    typedef const SdrObject *                                           key_type;
    typedef unotools::WeakReference<::accessibility::AccessibleShape>   mapped_type;
    typedef std::map<key_type, mapped_type>::iterator iterator;
    typedef std::map<key_type, mapped_type>::const_iterator const_iterator;

private:

    ::accessibility::AccessibleShapeTreeInfo    maInfo;
    std::map<key_type, mapped_type> maMap;

public:

    explicit SwAccessibleShapeMap_Impl( SwAccessibleMap const *pMap )
    {
        maInfo.SetSdrView(pMap->GetShell().GetDrawView());
        maInfo.SetWindow(pMap->GetShell().GetWin());
        maInfo.SetViewForwarder( pMap );
        uno::Reference < document::XShapeEventBroadcaster > xModelBroadcaster =
            new SwDrawModellListener_Impl(
                    pMap->GetShell().getIDocumentDrawModelAccess().GetOrCreateDrawModel());
        maInfo.SetModelBroadcaster( xModelBroadcaster );
    }

    ~SwAccessibleShapeMap_Impl();

    const ::accessibility::AccessibleShapeTreeInfo& GetInfo() const { return maInfo; }

    std::unique_ptr<SwAccessibleObjShape_Impl[]> Copy( size_t& rSize,
        const SwFEShell *pFESh,
        SwAccessibleObjShape_Impl  **pSelShape ) const;

    const_iterator begin() const { return maMap.begin(); }
    const_iterator end() const { return maMap.end(); }
    bool empty() const { return maMap.empty(); }
    iterator find(const key_type& key) { return maMap.find(key); }
    template<class... Args>
    std::pair<iterator,bool> emplace(Args&&... args) { return maMap.emplace(std::forward<Args>(args)...); }
    iterator erase(const_iterator const & pos) { return maMap.erase(pos); }
};

SwAccessibleShapeMap_Impl::~SwAccessibleShapeMap_Impl()
{
    uno::Reference < document::XEventBroadcaster > xBrd( maInfo.GetModelBroadcaster() );
    if( xBrd.is() )
        static_cast < SwDrawModellListener_Impl * >( xBrd.get() )->Dispose();
}

std::unique_ptr<SwAccessibleObjShape_Impl[]>
    SwAccessibleShapeMap_Impl::Copy(
            size_t& rSize, const SwFEShell *pFESh,
            SwAccessibleObjShape_Impl **pSelStart ) const
{
    std::unique_ptr<SwAccessibleObjShape_Impl[]> pShapes;
    SwAccessibleObjShape_Impl *pSelShape = nullptr;

    size_t nSelShapes = pFESh ? pFESh->GetSelectedObjCount() : 0;
    rSize = maMap.size();

    if( rSize > 0 )
    {
        pShapes.reset(new SwAccessibleObjShape_Impl[rSize]);

        SwAccessibleObjShape_Impl *pShape = pShapes.get();
        pSelShape = &(pShapes[rSize]);
        for( const auto& rEntry : maMap )
        {
            const SdrObject *pObj = rEntry.first;
            rtl::Reference < ::accessibility::AccessibleShape > xAcc( rEntry.second );
            if( nSelShapes && pFESh && pFESh->IsObjSelected( *pObj ) )
            {
                // selected objects are inserted from the back
                --pSelShape;
                pSelShape->first = pObj;
                pSelShape->second = xAcc.get();
                --nSelShapes;
            }
            else
            {
                pShape->first = pObj;
                pShape->second = xAcc.get();
                ++pShape;
            }
        }
        assert(pSelShape == pShape);
    }

    if( pSelStart )
        *pSelStart = pSelShape;

    return pShapes;
}

struct SwAccessibleEvent_Impl
{
public:
    enum EventType { CARET_OR_STATES,
                     INVALID_CONTENT,
                     POS_CHANGED,
                     CHILD_POS_CHANGED,
                     SHAPE_SELECTION,
                     DISPOSE,
                     INVALID_ATTR };

private:
    SwRect      maOldBox;                       // the old bounds for CHILD_POS_CHANGED
                                                // and POS_CHANGED
    unotools::WeakReference < SwAccessibleContext > mxAcc;   // The object that fires the event
    SwAccessibleChild maFrameOrObj;             // the child for CHILD_POS_CHANGED and
                                                // the same as xAcc for any other
                                                // event type
    EventType   meType;                         // The event type
    AccessibleStates mnStates;                 // check states or update caret pos

public:
    const SwFrame* mpParentFrame;   // The object that fires the event
    bool IsNoXaccParentFrame() const
    {
        return CHILD_POS_CHANGED == meType && mpParentFrame != nullptr;
    }

public:
    SwAccessibleEvent_Impl( EventType eT,
                            SwAccessibleContext *pA,
                            SwAccessibleChild aFrameOrObj )
        : mxAcc( pA ),
          maFrameOrObj(std::move( aFrameOrObj )),
          meType( eT ),
          mnStates( AccessibleStates::NONE ),
          mpParentFrame( nullptr )
    {}

    SwAccessibleEvent_Impl( EventType eT,
                            SwAccessibleChild aFrameOrObj )
        : maFrameOrObj(std::move( aFrameOrObj )),
          meType( eT ),
          mnStates( AccessibleStates::NONE ),
          mpParentFrame( nullptr )
    {
        assert(SwAccessibleEvent_Impl::DISPOSE == meType &&
                "wrong event constructor, DISPOSE only");
    }

    explicit SwAccessibleEvent_Impl( EventType eT )
        : meType( eT ),
          mnStates( AccessibleStates::NONE ),
          mpParentFrame( nullptr )
    {
        assert(SwAccessibleEvent_Impl::SHAPE_SELECTION == meType &&
                "wrong event constructor, SHAPE_SELECTION only" );
    }

    SwAccessibleEvent_Impl( EventType eT,
                            SwAccessibleContext *pA,
                            SwAccessibleChild aFrameOrObj,
                            const SwRect& rR )
        : maOldBox( rR ),
          mxAcc( pA ),
          maFrameOrObj(std::move( aFrameOrObj )),
          meType( eT ),
          mnStates( AccessibleStates::NONE ),
          mpParentFrame( nullptr )
    {
        assert((SwAccessibleEvent_Impl::CHILD_POS_CHANGED == meType ||
                SwAccessibleEvent_Impl::POS_CHANGED == meType) &&
                "wrong event constructor, (CHILD_)POS_CHANGED only" );
    }

    SwAccessibleEvent_Impl( EventType eT,
                            SwAccessibleContext *pA,
                            SwAccessibleChild aFrameOrObj,
                            const AccessibleStates _nStates )
        : mxAcc( pA ),
          maFrameOrObj(std::move( aFrameOrObj )),
          meType( eT ),
          mnStates( _nStates ),
          mpParentFrame( nullptr )
    {
        assert( SwAccessibleEvent_Impl::CARET_OR_STATES == meType &&
                "wrong event constructor, CARET_OR_STATES only" );
    }

    SwAccessibleEvent_Impl( EventType eT, const SwFrame *pParentFrame,
                SwAccessibleChild aFrameOrObj, const SwRect& rR ) :
        maOldBox( rR ),
        maFrameOrObj(std::move( aFrameOrObj )),
        meType( eT ),
        mnStates( AccessibleStates::NONE ),
        mpParentFrame( pParentFrame )
    {
        assert( SwAccessibleEvent_Impl::CHILD_POS_CHANGED == meType &&
            "wrong event constructor, CHILD_POS_CHANGED only" );
    }

    // <SetType(..)> only used in method <SwAccessibleMap::AppendEvent(..)>
    void SetType( EventType eT )
    {
        meType = eT;
    }
    EventType GetType() const
    {
        return meType;
    }

    ::rtl::Reference < SwAccessibleContext > GetContext() const
    {
        rtl::Reference < SwAccessibleContext > xAccImpl( mxAcc );
        return xAccImpl;
    }

    const SwRect& GetOldBox() const
    {
        return maOldBox;
    }
    // <SetOldBox(..)> only used in method <SwAccessibleMap::AppendEvent(..)>
    void SetOldBox( const SwRect& rOldBox )
    {
        maOldBox = rOldBox;
    }

    const SwAccessibleChild& GetFrameOrObj() const
    {
        return maFrameOrObj;
    }

    // <SetStates(..)> only used in method <SwAccessibleMap::AppendEvent(..)>
    void SetStates( AccessibleStates _nStates )
    {
        mnStates |= _nStates;
    }

    bool IsUpdateCursorPos() const
    {
        return bool(mnStates & AccessibleStates::CARET);
    }
    bool IsInvalidateStates() const
    {
        return bool(mnStates & (AccessibleStates::EDITABLE | AccessibleStates::OPAQUE));
    }
    bool IsInvalidateRelation() const
    {
        return bool(mnStates & (AccessibleStates::RELATION_FROM | AccessibleStates::RELATION_TO));
    }
    bool IsInvalidateTextSelection() const
    {
        return bool( mnStates & AccessibleStates::TEXT_SELECTION_CHANGED );
    }

    bool IsInvalidateTextAttrs() const
    {
        return bool( mnStates & AccessibleStates::TEXT_ATTRIBUTE_CHANGED );
    }

    AccessibleStates GetStates() const
    {
        return mnStates;
    }

    AccessibleStates GetAllStates() const
    {
        return mnStates;
    }
};

class SwAccessibleEventList_Impl
{
    std::list<SwAccessibleEvent_Impl> maEvents;
    bool mbFiring;

public:
    SwAccessibleEventList_Impl()
        : mbFiring( false )
    {}

    void SetFiring()
    {
        mbFiring = true;
    }
    bool IsFiring() const
    {
        return mbFiring;
    }

    void MoveMissingXAccToEnd();

    size_t size() const { return maEvents.size(); }
    std::list<SwAccessibleEvent_Impl>::iterator begin() { return maEvents.begin(); }
    std::list<SwAccessibleEvent_Impl>::iterator end() { return maEvents.end(); }
    std::list<SwAccessibleEvent_Impl>::iterator insert( const std::list<SwAccessibleEvent_Impl>::iterator& aIter,
                                                        const SwAccessibleEvent_Impl& rEvent )
    {
        return maEvents.insert( aIter, rEvent );
    }
    std::list<SwAccessibleEvent_Impl>::iterator erase( const std::list<SwAccessibleEvent_Impl>::iterator& aPos )
    {
        return maEvents.erase( aPos );
    }
};

// see comment in SwAccessibleMap::InvalidatePosOrSize()
// last case "else if(pParent)" for why this surprising hack exists
void SwAccessibleEventList_Impl::MoveMissingXAccToEnd()
{
    size_t nSize = size();
    if (nSize < 2 )
    {
        return;
    }
    SwAccessibleEventList_Impl lstEvent;
    for (auto li = begin(); li != end(); )
    {
        if (li->IsNoXaccParentFrame())
        {
            lstEvent.insert(lstEvent.end(), *li);
            li = erase(li);
        }
        else
            ++li;
    }
    assert(size() + lstEvent.size() == nSize);
    maEvents.insert(end(),lstEvent.begin(),lstEvent.end());
    assert(size() == nSize);
}

namespace {

struct SwAccessibleChildFunc
{
    bool operator()( const SwAccessibleChild& r1,
                         const SwAccessibleChild& r2 ) const
    {
        const void *p1 = r1.GetSwFrame()
                         ? static_cast < const void * >( r1.GetSwFrame())
                         : ( r1.GetDrawObject()
                             ? static_cast < const void * >( r1.GetDrawObject() )
                             : static_cast < const void * >( r1.GetWindow() ) );
        const void *p2 = r2.GetSwFrame()
                         ? static_cast < const void * >( r2.GetSwFrame())
                         : ( r2.GetDrawObject()
                             ? static_cast < const void * >( r2.GetDrawObject() )
                             : static_cast < const void * >( r2.GetWindow() ) );
        return p1 < p2;
    }
};

}

class SwAccessibleEventMap_Impl
{
public:
    typedef SwAccessibleChild                                           key_type;
    typedef std::list<SwAccessibleEvent_Impl>::iterator                 mapped_type;
    typedef SwAccessibleChildFunc                                       key_compare;
    typedef std::map<key_type,mapped_type,key_compare>::iterator        iterator;
    typedef std::map<key_type,mapped_type,key_compare>::const_iterator  const_iterator;
private:
    std::map <key_type,mapped_type,key_compare> maMap;
public:
    iterator end() { return maMap.end(); }
    iterator find(const key_type& key) { return maMap.find(key); }
    template<class... Args>
    std::pair<iterator,bool> emplace(Args&&... args) { return maMap.emplace(std::forward<Args>(args)...); }
    iterator erase(const_iterator const & pos) { return maMap.erase(pos); }
};

namespace {

struct SwAccessibleParaSelection
{
    TextFrameIndex nStartOfSelection;
    TextFrameIndex nEndOfSelection;

    SwAccessibleParaSelection(const TextFrameIndex nStartOfSelection_,
                              const TextFrameIndex nEndOfSelection_)
        : nStartOfSelection(nStartOfSelection_)
        , nEndOfSelection(nEndOfSelection_)
    {}
};

struct SwXAccWeakRefComp
{
    bool operator()( const unotools::WeakReference<SwAccessibleContext>& _rXAccWeakRef1,
                         const unotools::WeakReference<SwAccessibleContext>& _rXAccWeakRef2 ) const
    {
        return _rXAccWeakRef1.get() < _rXAccWeakRef2.get();
    }
};

}

class SwAccessibleSelectedParas_Impl
{
public:
    typedef unotools::WeakReference < SwAccessibleContext >             key_type;
    typedef SwAccessibleParaSelection                                   mapped_type;
    typedef SwXAccWeakRefComp                                           key_compare;
    typedef std::map<key_type,mapped_type,key_compare>::iterator        iterator;
    typedef std::map<key_type,mapped_type,key_compare>::const_iterator  const_iterator;
private:
    std::map<key_type,mapped_type,key_compare> maMap;
public:
    iterator begin() { return maMap.begin(); }
    iterator end() { return maMap.end(); }
    iterator find(const key_type& key) { return maMap.find(key); }
    template<class... Args>
    std::pair<iterator,bool> emplace(Args&&... args) { return maMap.emplace(std::forward<Args>(args)...); }
    iterator erase(const_iterator const & pos) { return maMap.erase(pos); }
};

// helper class that stores preview data
class SwAccPreviewData
{
    typedef std::vector<tools::Rectangle> Rectangles;
    Rectangles maPreviewRects;
    Rectangles maLogicRects;

    SwRect maVisArea;
    Fraction maScale;

    const SwPageFrame *mpSelPage;

    /** adjust logic page rectangle to its visible part

        @param _iorLogicPgSwRect
        input/output parameter - reference to the logic page rectangle, which
        has to be adjusted.

        @param _rPreviewPgSwRect
        input parameter - constant reference to the corresponding preview page
        rectangle; needed to determine the visible part of the logic page rectangle.

        @param _rPreviewWinSize
        input parameter - constant reference to the preview window size in TWIP;
        needed to determine the visible part of the logic page rectangle
    */
    static void AdjustLogicPgRectToVisibleArea( SwRect&         _iorLogicPgSwRect,
                                         const SwRect&   _rPreviewPgSwRect,
                                         const Size&     _rPreviewWinSize );

public:
    SwAccPreviewData();

    void Update( const SwAccessibleMap& rAccMap,
                 const std::vector<std::unique_ptr<PreviewPage>>& _rPreviewPages,
                 const Fraction&  _rScale,
                 const SwPageFrame* _pSelectedPageFrame,
                 const Size&      _rPreviewWinSize );

    void InvalidateSelection( const SwPageFrame* _pSelectedPageFrame );

    const SwRect& GetVisArea() const { return maVisArea;}

    /** Adjust the MapMode so that the preview page appears at the
     * proper position. rPoint identifies the page for which the
     * MapMode should be adjusted. If bFromPreview is true, rPoint is
     * a preview coordinate; else it's a document coordinate. */
    void AdjustMapMode( MapMode& rMapMode,
                        const Point& rPoint ) const;

    const SwPageFrame *GetSelPage() const { return mpSelPage; }

    void DisposePage(const SwPageFrame *pPageFrame );
};

SwAccPreviewData::SwAccPreviewData() :
    mpSelPage( nullptr )
{
}

void SwAccPreviewData::Update( const SwAccessibleMap& rAccMap,
                               const std::vector<std::unique_ptr<PreviewPage>>& _rPreviewPages,
                               const Fraction&  _rScale,
                               const SwPageFrame* _pSelectedPageFrame,
                               const Size&      _rPreviewWinSize )
{
    // store preview scaling, maximal preview page size and selected page
    maScale = _rScale;
    mpSelPage = _pSelectedPageFrame;

    // prepare loop on preview pages
    maPreviewRects.clear();
    maLogicRects.clear();
    SwAccessibleChild aPage;
    maVisArea.Clear();

    // loop on preview pages to calculate <maPreviewRects>, <maLogicRects> and
    // <maVisArea>
    for ( auto & rpPreviewPage : _rPreviewPages )
    {
        aPage = rpPreviewPage->pPage;

        // add preview page rectangle to <maPreviewRects>
        tools::Rectangle aPreviewPgRect( rpPreviewPage->aPreviewWinPos, rpPreviewPage->aPageSize );
        maPreviewRects.push_back( aPreviewPgRect );

        // add logic page rectangle to <maLogicRects>
        SwRect aLogicPgSwRect( aPage.GetBox( rAccMap ) );
        tools::Rectangle aLogicPgRect( aLogicPgSwRect.SVRect() );
        maLogicRects.push_back( aLogicPgRect );
        // union visible area with visible part of logic page rectangle
        if ( rpPreviewPage->bVisible )
        {
            if ( !rpPreviewPage->pPage->IsEmptyPage() )
            {
                AdjustLogicPgRectToVisibleArea( aLogicPgSwRect,
                                                SwRect( aPreviewPgRect ),
                                                _rPreviewWinSize );
            }
            if ( maVisArea.IsEmpty() )
                maVisArea = aLogicPgSwRect;
            else
                maVisArea.Union( aLogicPgSwRect );
        }
    }
}

void SwAccPreviewData::InvalidateSelection( const SwPageFrame* _pSelectedPageFrame )
{
    mpSelPage = _pSelectedPageFrame;
    assert(mpSelPage);
}

namespace {

struct ContainsPredicate
{
    const Point& mrPoint;
    explicit ContainsPredicate( const Point& rPoint ) : mrPoint(rPoint) {}
    bool operator() ( const tools::Rectangle& rRect ) const
    {
        return rRect.Contains( mrPoint );
    }
};

}

void SwAccPreviewData::AdjustMapMode( MapMode& rMapMode,
                                      const Point& rPoint ) const
{
    // adjust scale
    rMapMode.SetScaleX( maScale );
    rMapMode.SetScaleY( maScale );

    // find proper rectangle
    Rectangles::const_iterator aBegin = maLogicRects.begin();
    Rectangles::const_iterator aEnd = maLogicRects.end();
    Rectangles::const_iterator aFound = std::find_if( aBegin, aEnd,
                                                 ContainsPredicate( rPoint ) );

    if( aFound != aEnd )
    {
        // found! set new origin
        Point aPoint = (maPreviewRects.begin() + (aFound - aBegin))->TopLeft();
        aPoint -= (maLogicRects.begin() + (aFound-aBegin))->TopLeft();
        rMapMode.SetOrigin( aPoint );
    }
    // else: don't adjust MapMode
}

void SwAccPreviewData::DisposePage(const SwPageFrame *pPageFrame )
{
    if( mpSelPage == pPageFrame )
        mpSelPage = nullptr;
}

// adjust logic page rectangle to its visible part
void SwAccPreviewData::AdjustLogicPgRectToVisibleArea(
                            SwRect&         _iorLogicPgSwRect,
                            const SwRect&   _rPreviewPgSwRect,
                            const Size&     _rPreviewWinSize )
{
    // determine preview window rectangle
    const SwRect aPreviewWinSwRect( Point( 0, 0 ), _rPreviewWinSize );
    // calculate visible preview page rectangle
    SwRect aVisPreviewPgSwRect( _rPreviewPgSwRect );
    aVisPreviewPgSwRect.Intersection( aPreviewWinSwRect );
    // adjust logic page rectangle
    SwTwips nTmpDiff;
    // left
    nTmpDiff = aVisPreviewPgSwRect.Left() - _rPreviewPgSwRect.Left();
    _iorLogicPgSwRect.AddLeft( nTmpDiff );
    // top
    nTmpDiff = aVisPreviewPgSwRect.Top() - _rPreviewPgSwRect.Top();
    _iorLogicPgSwRect.AddTop( nTmpDiff );
    // right
    nTmpDiff = _rPreviewPgSwRect.Right() - aVisPreviewPgSwRect.Right();
    _iorLogicPgSwRect.AddRight( - nTmpDiff );
    // bottom
    nTmpDiff = _rPreviewPgSwRect.Bottom() - aVisPreviewPgSwRect.Bottom();
    _iorLogicPgSwRect.AddBottom( - nTmpDiff );
}

static bool AreInSameTable( const rtl::Reference< SwAccessibleContext >& rAcc,
                                  const SwFrame *pFrame )
{
    bool bRet = false;

    if( pFrame && pFrame->IsCellFrame() && rAcc.is() )
    {
        // Is it in the same table? We check that
        // by comparing the last table frame in the
        // follow chain, because that's cheaper than
        // searching the first one.
        if( rAcc->GetFrame()->IsCellFrame() )
        {
            const SwTabFrame *pTabFrame1 = rAcc->GetFrame()->FindTabFrame();
            if (pTabFrame1)
            {
                while (pTabFrame1->GetFollow())
                    pTabFrame1 = pTabFrame1->GetFollow();
            }

            const SwTabFrame *pTabFrame2 = pFrame->FindTabFrame();
            if (pTabFrame2)
            {
                while (pTabFrame2->GetFollow())
                    pTabFrame2 = pTabFrame2->GetFollow();
            }

            bRet = (pTabFrame1 == pTabFrame2);
        }
    }

    return bRet;
}

void SwAccessibleMap::FireEvent( const SwAccessibleEvent_Impl& rEvent )
{
    ::rtl::Reference < SwAccessibleContext > xAccImpl( rEvent.GetContext() );
    if (!xAccImpl.is() && rEvent.mpParentFrame != nullptr)
    {
        SwAccessibleContextMap::iterator aIter = maFrameMap.find(rEvent.mpParentFrame);
        if (aIter != maFrameMap.end())
        {
            rtl::Reference < SwAccessibleContext > xContext( (*aIter).second.get() );
            if (xContext.is() && (xContext->getAccessibleRole() == AccessibleRole::PARAGRAPH
                                  || xContext->getAccessibleRole() == AccessibleRole::BLOCK_QUOTE))
            {
                xAccImpl = xContext.get();
            }
        }
    }
    if( SwAccessibleEvent_Impl::SHAPE_SELECTION == rEvent.GetType() )
    {
        DoInvalidateShapeSelection();
    }
    else if( xAccImpl.is() && xAccImpl->GetFrame() )
    {
        if ( rEvent.GetType() != SwAccessibleEvent_Impl::DISPOSE &&
             rEvent.IsInvalidateTextAttrs() )
        {
            xAccImpl->InvalidateAttr();
        }
        switch( rEvent.GetType() )
        {
        case SwAccessibleEvent_Impl::INVALID_CONTENT:
            xAccImpl->InvalidateContent();
            break;
        case SwAccessibleEvent_Impl::POS_CHANGED:
            xAccImpl->InvalidatePosOrSize( rEvent.GetOldBox() );
            break;
        case SwAccessibleEvent_Impl::CHILD_POS_CHANGED:
            xAccImpl->InvalidateChildPosOrSize( rEvent.GetFrameOrObj(),
                                       rEvent.GetOldBox() );
            break;
        case SwAccessibleEvent_Impl::DISPOSE:
            assert(!"dispose event has been stored");
            break;
        case SwAccessibleEvent_Impl::INVALID_ATTR:
            // nothing to do here - handled above
            break;
        default:
            break;
        }
        if( SwAccessibleEvent_Impl::DISPOSE != rEvent.GetType() )
        {
            if( rEvent.IsUpdateCursorPos() )
                xAccImpl->InvalidateCursorPos();
            if( rEvent.IsInvalidateStates() )
                xAccImpl->InvalidateStates( rEvent.GetStates() );
            if( rEvent.IsInvalidateRelation() )
            {
                // both events CONTENT_FLOWS_FROM_RELATION_CHANGED and
                // CONTENT_FLOWS_TO_RELATION_CHANGED are possible
                if ( rEvent.GetAllStates() & AccessibleStates::RELATION_FROM )
                {
                    xAccImpl->InvalidateRelation(
                        AccessibleEventId::CONTENT_FLOWS_FROM_RELATION_CHANGED );
                }
                if ( rEvent.GetAllStates() & AccessibleStates::RELATION_TO )
                {
                    xAccImpl->InvalidateRelation(
                        AccessibleEventId::CONTENT_FLOWS_TO_RELATION_CHANGED );
                }
            }

            if ( rEvent.IsInvalidateTextSelection() )
            {
                xAccImpl->InvalidateTextSelection();
            }
        }
    }
}

void SwAccessibleMap::AppendEvent( const SwAccessibleEvent_Impl& rEvent )
{
    osl::MutexGuard aGuard( maEventMutex );

    if( !mpEvents )
        mpEvents.reset(new SwAccessibleEventList_Impl);
    if( !mpEventMap )
        mpEventMap.reset(new SwAccessibleEventMap_Impl);

    if( mpEvents->IsFiring() )
    {
        // While events are fired new ones are generated. They have to be fired
        // now. This does not work for DISPOSE events!
        OSL_ENSURE( rEvent.GetType() != SwAccessibleEvent_Impl::DISPOSE,
                "dispose event while firing events" );
        FireEvent( rEvent );
    }
    else
    {

        SwAccessibleEventMap_Impl::iterator aIter =
                                        mpEventMap->find( rEvent.GetFrameOrObj() );
        if( aIter != mpEventMap->end() )
        {
            SwAccessibleEvent_Impl aEvent( *(*aIter).second );
            assert( aEvent.GetType() != SwAccessibleEvent_Impl::DISPOSE &&
                    "dispose events should not be stored" );
            bool bAppendEvent = true;
            switch( rEvent.GetType() )
            {
            case SwAccessibleEvent_Impl::CARET_OR_STATES:
                // A CARET_OR_STATES event is added to any other
                // event only. It is broadcasted after any other event, so the
                // event should be put to the back.
                OSL_ENSURE( aEvent.GetType() != SwAccessibleEvent_Impl::CHILD_POS_CHANGED,
                        "invalid event combination" );
                aEvent.SetStates( rEvent.GetAllStates() );
                break;
            case SwAccessibleEvent_Impl::INVALID_CONTENT:
                // An INVALID_CONTENT event overwrites a CARET_OR_STATES
                // event (but keeps its flags) and it is contained in a
                // POS_CHANGED event.
                // Therefore, the event's type has to be adapted and the event
                // has to be put at the end.
                //
                // fdo#56031 An INVALID_CONTENT event overwrites a INVALID_ATTR
                // event and overwrites its flags
                OSL_ENSURE( aEvent.GetType() != SwAccessibleEvent_Impl::CHILD_POS_CHANGED,
                        "invalid event combination" );
                if( aEvent.GetType() == SwAccessibleEvent_Impl::CARET_OR_STATES )
                    aEvent.SetType( SwAccessibleEvent_Impl::INVALID_CONTENT );
                else if ( aEvent.GetType() == SwAccessibleEvent_Impl::INVALID_ATTR )
                {
                    aEvent.SetType( SwAccessibleEvent_Impl::INVALID_CONTENT );
                    aEvent.SetStates( rEvent.GetAllStates() );
                }

                break;
            case SwAccessibleEvent_Impl::POS_CHANGED:
                // A pos changed event overwrites CARET_STATES (keeping its
                // flags) as well as INVALID_CONTENT. The old box position
                // has to be stored however if the old event is not a
                // POS_CHANGED itself.
                OSL_ENSURE( aEvent.GetType() != SwAccessibleEvent_Impl::CHILD_POS_CHANGED,
                        "invalid event combination" );
                if( aEvent.GetType() != SwAccessibleEvent_Impl::POS_CHANGED )
                    aEvent.SetOldBox( rEvent.GetOldBox() );
                aEvent.SetType( SwAccessibleEvent_Impl::POS_CHANGED );
                break;
            case SwAccessibleEvent_Impl::CHILD_POS_CHANGED:
                // CHILD_POS_CHANGED events can only follow CHILD_POS_CHANGED
                // events. The only action that needs to be done again is
                // to put the old event to the back. The new one cannot be used,
                // because we are interested in the old frame bounds.
                OSL_ENSURE( aEvent.GetType() == SwAccessibleEvent_Impl::CHILD_POS_CHANGED,
                        "invalid event combination" );
                break;
            case SwAccessibleEvent_Impl::SHAPE_SELECTION:
                OSL_ENSURE( aEvent.GetType() == SwAccessibleEvent_Impl::SHAPE_SELECTION,
                        "invalid event combination" );
                break;
            case SwAccessibleEvent_Impl::DISPOSE:
                // DISPOSE events overwrite all others. They are not stored
                // but executed immediately to avoid broadcasting of
                // nonfunctional objects. So what needs to be done here is to
                // remove all events for the frame in question.
                bAppendEvent = false;
                break;
            case SwAccessibleEvent_Impl::INVALID_ATTR:
                // tdf#150708 if the old is CARET_OR_STATES then try updating it
                // with the additional states
                if (aEvent.GetType() == SwAccessibleEvent_Impl::CARET_OR_STATES)
                    aEvent.SetStates(rEvent.GetAllStates());
                else
                {
                    OSL_ENSURE( aEvent.GetType() == SwAccessibleEvent_Impl::INVALID_ATTR,
                            "invalid event combination" );
                }
                break;
            }
            if( bAppendEvent )
            {
                mpEvents->erase( (*aIter).second );
                (*aIter).second = mpEvents->insert( mpEvents->end(), aEvent );
            }
            else
            {
                mpEvents->erase( (*aIter).second );
                mpEventMap->erase( aIter );
            }
        }
        else if( SwAccessibleEvent_Impl::DISPOSE != rEvent.GetType() )
        {
            mpEventMap->emplace( rEvent.GetFrameOrObj(),
                    mpEvents->insert( mpEvents->end(), rEvent ) );
        }
    }
}

void SwAccessibleMap::InvalidateCursorPosition(const rtl::Reference<SwAccessibleContext>& rxAcc)
{
    assert(rxAcc.is());
    assert(rxAcc->GetFrame());
    if (GetShell().ActionPend())
    {
        SwAccessibleEvent_Impl aEvent(SwAccessibleEvent_Impl::CARET_OR_STATES, rxAcc.get(),
                                      SwAccessibleChild(rxAcc->GetFrame()),
                                      AccessibleStates::CARET);
        AppendEvent( aEvent );
    }
    else
    {
        FireEvents();
        // While firing events the current frame might have
        // been disposed because it moved out of the visible area.
        // Setting the cursor for such frames is useless and even
        // causes asserts.
        if (rxAcc->GetFrame())
            rxAcc->InvalidateCursorPos();
    }
}

void SwAccessibleMap::InvalidateShapeSelection()
{
    if (GetShell().ActionPend())
    {
        SwAccessibleEvent_Impl aEvent(
            SwAccessibleEvent_Impl::SHAPE_SELECTION );
        AppendEvent( aEvent );
    }
    else
    {
        FireEvents();
        DoInvalidateShapeSelection();
    }
}

//This method should implement the following functions:
//1.find the shape objects and set the selected state.
//2.find the Swframe objects and set the selected state.
//3.find the paragraph objects and set the selected state.
void SwAccessibleMap::InvalidateShapeInParaSelection()
{
    DBG_TESTSOLARMUTEX();

    const SwViewShell& rVSh = GetShell();
    const SwFEShell *pFESh = dynamic_cast<const SwFEShell*>(&rVSh);
    SwPaM* pCursor = pFESh ? pFESh->GetCursor( false /* ??? */ ) : nullptr;

    if( mpShapeMap )
    {
        SwAccessibleObjShape_Impl* pSelShape = nullptr;
        size_t nShapes = 0;
        std::unique_ptr<SwAccessibleObjShape_Impl[]> pShapes
            = mpShapeMap->Copy(nShapes, pFESh, &pSelShape);

        if (IsDocumentSelAll())
        {
            for (const auto& rIter : *mpShapeMap)
            {
                rtl::Reference<::accessibility::AccessibleShape> xAcc(rIter.second);
                if( xAcc.is() )
                    xAcc->SetState( AccessibleStateType::SELECTED );
            }
        }
        else
        {
            for (const auto& rIter : *mpShapeMap)
            {
                const SwFrameFormat* pFrameFormat = rIter.first ? ::FindFrameFormat(rIter.first) : nullptr;
                if( !pFrameFormat )
                    continue;

                const SwFormatAnchor& rAnchor = pFrameFormat->GetAnchor();
                const SwNode *pAnchorNode = rAnchor.GetAnchorNode();

                if(rAnchor.GetAnchorId() == RndStdIds::FLY_AT_PAGE)
                {
                    rtl::Reference <::accessibility::AccessibleShape> xAcc(rIter.second);
                    if(xAcc.is())
                        xAcc->ResetState( AccessibleStateType::SELECTED );
                    continue;
                }

                if( !pAnchorNode )
                    continue;

                if( pAnchorNode->GetTextNode() )
                {
                    sal_Int32 nIndex = rAnchor.GetAnchorContentOffset();
                    bool bMarked = false;
                    if( pCursor != nullptr )
                    {
                        const SwTextNode* pNode = pAnchorNode->GetTextNode();
                        SwTextFrame const*const pFrame(static_cast<SwTextFrame*>(pNode->getLayoutFrame(rVSh.GetLayout())));
                        SwNodeOffset nFirstNode(pFrame->GetTextNodeFirst()->GetIndex());
                        SwNodeOffset nLastNode;
                        if (sw::MergedPara const*const pMerged = pFrame->GetMergedPara())
                        {
                            nLastNode = pMerged->pLastNode->GetIndex();
                        }
                        else
                        {
                            nLastNode = nFirstNode;
                        }

                        SwNodeOffset nHere = pNode->GetIndex();

                        for(SwPaM& rTmpCursor : pCursor->GetRingContainer())
                        {
                            // ignore, if no mark
                            if( rTmpCursor.HasMark() )
                            {
                                bMarked = true;
                                // check whether nHere is 'inside' pCursor
                                SwPosition* pStart = rTmpCursor.Start();
                                SwNodeOffset nStartIndex = pStart->GetNodeIndex();
                                SwPosition* pEnd = rTmpCursor.End();
                                SwNodeOffset nEndIndex = pEnd->GetNodeIndex();
                                if ((nStartIndex <= nLastNode) && (nFirstNode <= nEndIndex))
                                {
                                    if( rAnchor.GetAnchorId() == RndStdIds::FLY_AS_CHAR )
                                    {
                                        if( ( ((nHere == nStartIndex) && (nIndex >= pStart->GetContentIndex())) || (nHere > nStartIndex) )
                                            &&( ((nHere == nEndIndex) && (nIndex < pEnd->GetContentIndex())) || (nHere < nEndIndex) ) )
                                        {
                                            rtl::Reference<::accessibility::AccessibleShape> xAcc(rIter.second);
                                            if( xAcc.is() )
                                                xAcc->SetState( AccessibleStateType::SELECTED );
                                        }
                                        else
                                        {
                                            rtl::Reference<::accessibility::AccessibleShape> xAcc(rIter.second);
                                            if( xAcc.is() )
                                                xAcc->ResetState( AccessibleStateType::SELECTED );
                                        }
                                    }
                                    else if( rAnchor.GetAnchorId() == RndStdIds::FLY_AT_PARA )
                                    {
                                        rtl::Reference<::accessibility::AccessibleShape> const xAcc(rIter.second);
                                        if (xAcc.is())
                                        {
                                            if (IsSelectFrameAnchoredAtPara(*rAnchor.GetContentAnchor(), *pStart, *pEnd))
                                            {
                                                xAcc->SetState( AccessibleStateType::SELECTED );
                                            }
                                            else
                                            {
                                                xAcc->ResetState( AccessibleStateType::SELECTED );
                                            }
                                        }
                                    }
                                    else if (rAnchor.GetAnchorId() == RndStdIds::FLY_AT_CHAR)
                                    {
                                        rtl::Reference<::accessibility::AccessibleShape> const xAcc(rIter.second);
                                        if (xAcc.is())
                                        {
                                            if (IsDestroyFrameAnchoredAtChar(*rAnchor.GetContentAnchor(), *pStart, *pEnd))
                                            {
                                                xAcc->SetState( AccessibleStateType::SELECTED );
                                            }
                                            else
                                            {
                                                xAcc->ResetState( AccessibleStateType::SELECTED );
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if( !bMarked )
                    {
                        SwAccessibleObjShape_Impl  *pShape = pShapes.get();
                        size_t nNumShapes = nShapes;
                        while (nNumShapes > 0)
                        {
                            if (pShape < pSelShape && (pShape->first == rIter.first))
                            {
                                rtl::Reference<::accessibility::AccessibleShape> xAcc(rIter.second);
                                if(xAcc.is())
                                    xAcc->ResetState( AccessibleStateType::SELECTED );
                            }
                            --nNumShapes;
                            ++pShape;
                        }
                    }
                }
            } // for loop
        }//else
    }

    //Checked for FlyFrame
    for (const auto& rIter : maFrameMap)
    {
        const SwFrame *pFrame = rIter.first;
        if(pFrame->IsFlyFrame())
        {
            rtl::Reference<SwAccessibleContext> xAcc = rIter.second;

            if(xAcc.is())
            {
                SwAccessibleFrameBase *pAccFrame = static_cast< SwAccessibleFrameBase * >(xAcc.get());
                bool bFrameChanged = pAccFrame->SetSelectedState( true );
                if (bFrameChanged)
                {
                    const SwFlyFrame *pFlyFrame = static_cast< const SwFlyFrame * >( pFrame );
                    const SwFrameFormat *pFrameFormat = pFlyFrame->GetFormat();
                    if (pFrameFormat)
                    {
                        const SwFormatAnchor& rAnchor = pFrameFormat->GetAnchor();
                        if( rAnchor.GetAnchorId() == RndStdIds::FLY_AS_CHAR )
                        {
                            uno::Reference< XAccessible > xAccParent = pAccFrame->getAccessibleParent();
                            if (xAccParent.is())
                            {
                                uno::Reference< XAccessibleContext > xAccContext = xAccParent->getAccessibleContext();
                                if(xAccContext.is() && (xAccContext->getAccessibleRole() == AccessibleRole::PARAGRAPH ||
                                                         xAccContext->getAccessibleRole() == AccessibleRole::BLOCK_QUOTE))
                                {
                                    SwAccessibleParagraph* pAccPara = static_cast< SwAccessibleParagraph *>(xAccContext.get());
                                    if(pAccFrame->IsSelectedInDoc())
                                    {
                                        m_setParaAdd.insert(pAccPara);
                                    }
                                    else if(m_setParaAdd.count(pAccPara) == 0)
                                    {
                                        m_setParaRemove.insert(pAccPara);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    std::vector<SwAccessibleContext*> vecAdd;
    std::vector<SwAccessibleContext*> vecRemove;
    //Checked for Paras.
    SwAccessibleContextMap mapTemp;
    if( pCursor != nullptr )
    {
        for(SwPaM& rTmpCursor : pCursor->GetRingContainer())
        {
            if( rTmpCursor.HasMark() )
            {
                SwNodeIndex nStartIndex( rTmpCursor.Start()->GetNode() );
                SwNodeIndex nEndIndex( rTmpCursor.End()->GetNode() );
                for (; nStartIndex <= nEndIndex; ++nStartIndex)
                {
                    SwFrame *pFrame = nullptr;
                    if(nStartIndex.GetNode().IsContentNode())
                    {
                        SwContentNode& rCNd = static_cast<SwContentNode&>(nStartIndex.GetNode());
                        pFrame = SwIterator<SwFrame, SwContentNode, sw::IteratorMode::UnwrapMulti>(rCNd).First();
                        if (mapTemp.find(pFrame) != mapTemp.end())
                        {
                            continue; // sw_redlinehide: once is enough
                        }
                    }
                    else if( nStartIndex.GetNode().IsTableNode() )
                    {
                        SwTableNode& rTable = static_cast<SwTableNode&>(nStartIndex.GetNode());
                        SwTableFormat* pFormat = rTable.GetTable().GetFrameFormat();
                        pFrame = SwIterator<SwFrame, SwTableFormat>(*pFormat).First();
                    }

                    if (pFrame)
                    {
                        SwAccessibleContextMap::iterator aIter = maFrameMap.find(pFrame);
                        if (aIter != maFrameMap.end())
                        {
                            rtl::Reference < SwAccessibleContext > xAcc = (*aIter).second;
                            bool isChanged = false;
                            if( xAcc.is() )
                            {
                                isChanged = xAcc->SetSelectedState( true );
                            }
                            if(!isChanged)
                            {
                                SwAccessibleContextMap::iterator aEraseIter = maSelectedFrameMap.find(pFrame);
                                if (aEraseIter != maSelectedFrameMap.end())
                                    maSelectedFrameMap.erase(aEraseIter);
                            }
                            else
                            {
                                vecAdd.push_back(xAcc.get());
                            }

                            mapTemp.emplace( pFrame, xAcc );
                        }
                    }
                }
            }
        }
    }
    if (!maSelectedFrameMap.empty())
    {
        SwAccessibleContextMap::iterator aIter = maSelectedFrameMap.begin();
        while (aIter != maSelectedFrameMap.end())
        {
            rtl::Reference < SwAccessibleContext > xAcc = (*aIter).second;
            if(xAcc.is())
                xAcc->SetSelectedState( false );
            ++aIter;
            vecRemove.push_back(xAcc.get());
        }
        maSelectedFrameMap.clear();
    }

    SwAccessibleContextMap::iterator aIter = mapTemp.begin();
    while( aIter != mapTemp.end() )
    {
        maSelectedFrameMap.emplace((*aIter).first, (*aIter).second);
        ++aIter;
    }
    mapTemp.clear();

    for (SwAccessibleContext* pAccPara : vecAdd)
    {
        if (pAccPara)
        {
            pAccPara->FireAccessibleEvent(AccessibleEventId::SELECTION_CHANGED, uno::Any(),
                                          uno::Any());
        }
    }
    for (SwAccessibleContext* pAccPara : vecRemove)
    {
        if (pAccPara)
        {
            pAccPara->FireAccessibleEvent(AccessibleEventId::SELECTION_CHANGED_REMOVE, uno::Any(),
                                          uno::Any());
        }
    }
}

//Merge with DoInvalidateShapeFocus
void SwAccessibleMap::DoInvalidateShapeSelection(bool bInvalidateFocusMode /*=false*/)
{
    DBG_TESTSOLARMUTEX();

    std::unique_ptr<SwAccessibleObjShape_Impl[]> pShapes;
    SwAccessibleObjShape_Impl *pSelShape = nullptr;
    size_t nShapes = 0;

    const SwViewShell& rVSh = GetShell();
    const SwFEShell *pFESh = dynamic_cast<const SwFEShell*>(&rVSh);
    const size_t nSelShapes = pFESh ? pFESh->GetSelectedObjCount() : 0;

    //when InvalidateFocus Call this function ,and the current selected shape count is not 1 ,
    //return
    if (bInvalidateFocusMode && nSelShapes != 1)
    {
        return;
    }
    if( mpShapeMap )
        pShapes = mpShapeMap->Copy( nShapes, pFESh, &pSelShape );

    if( !pShapes )
        return;

    std::vector<::rtl::Reference<::accessibility::AccessibleShape>> vecxShapeAdd;
    std::vector<::rtl::Reference<::accessibility::AccessibleShape>> vecxShapeRemove;

    vcl::Window* pWin = GetShell().GetWin();
    bool bFocused = pWin && pWin->HasFocus();
    SwAccessibleObjShape_Impl *pShape = pShapes.get();
    int nShapeCount = nShapes;
    while( nShapeCount )
    {
        if (pShape->second.is() && IsInSameLevel(pShape->first, pFESh))
        {
            if( pShape < pSelShape )
            {
                if(pShape->second->ResetState( AccessibleStateType::SELECTED ))
                {
                    vecxShapeRemove.push_back(pShape->second);
                }
                pShape->second->ResetState( AccessibleStateType::FOCUSED );
            }
        }
        --nShapeCount;
        ++pShape;
    }

    rtl::Reference<SwAccessibleContext> xDocView = GetDocumentView_(false);
    assert(xDocView.is());

    for (const auto& rpShape : vecxShapeRemove)
    {
        if (rpShape.is())
        {
            xDocView->FireAccessibleEvent(AccessibleEventId::SELECTION_CHANGED_REMOVE, uno::Any(),
                                          uno::Any(uno::Reference<XAccessible>(rpShape)));
        }
    }

    pShape = pShapes.get();

    while( nShapes )
    {
        if (pShape->second.is() && IsInSameLevel(pShape->first, pFESh))
        {
            if( pShape >= pSelShape )
            {
                //first fire focus event
                if( bFocused && 1 == nSelShapes )
                    pShape->second->SetState( AccessibleStateType::FOCUSED );
                else
                    pShape->second->ResetState( AccessibleStateType::FOCUSED );

                if(pShape->second->SetState( AccessibleStateType::SELECTED ))
                {
                    vecxShapeAdd.push_back(pShape->second);
                }
            }
        }

        --nShapes;
        ++pShape;
    }

    const unsigned int SELECTION_WITH_NUM = 10;
    if (vecxShapeAdd.size() > SELECTION_WITH_NUM )
    {
        xDocView->FireAccessibleEvent(AccessibleEventId::SELECTION_CHANGED_WITHIN, uno::Any(),
                                      uno::Any());
    }
    else
    {
        for (const auto& rpShape : vecxShapeAdd)
        {
            if (rpShape.is())
            {
                xDocView->FireAccessibleEvent(AccessibleEventId::SELECTION_CHANGED_ADD, uno::Any(),
                                              uno::Any(uno::Reference<XAccessible>(rpShape)));
            }
        }
    }

    for (const auto& rpShape : vecxShapeAdd)
    {
        ::accessibility::AccessibleShape *pAccShape = rpShape.get();
        if (pAccShape)
        {
            SdrObject *pObj = SdrObject::getSdrObjectFromXShape(pAccShape->GetXShape());
            SwFrameFormat *pFrameFormat = pObj ? FindFrameFormat( pObj ) : nullptr;
            if (pFrameFormat)
            {
                const SwFormatAnchor& rAnchor = pFrameFormat->GetAnchor();
                if( rAnchor.GetAnchorId() == RndStdIds::FLY_AS_CHAR )
                {
                    uno::Reference< XAccessible > xPara = pAccShape->getAccessibleParent();
                    if (xPara.is())
                    {
                        uno::Reference< XAccessibleContext > xParaContext = xPara->getAccessibleContext();
                        if (xParaContext.is() && (xParaContext->getAccessibleRole() == AccessibleRole::PARAGRAPH
                                                  || xParaContext->getAccessibleRole() == AccessibleRole::BLOCK_QUOTE))
                        {
                            SwAccessibleParagraph* pAccPara = static_cast< SwAccessibleParagraph *>(xPara.get());
                            if (pAccPara)
                            {
                                m_setParaAdd.insert(pAccPara);
                            }
                        }
                    }
                }
            }
        }
    }
    for (const auto& rpShape : vecxShapeRemove)
    {
        ::accessibility::AccessibleShape *pAccShape = rpShape.get();
        if (pAccShape && !pAccShape->IsDisposed())
        {
            uno::Reference< XAccessible > xPara = pAccShape->getAccessibleParent();
            uno::Reference< XAccessibleContext > xParaContext = xPara->getAccessibleContext();
            if (xParaContext.is() && (xParaContext->getAccessibleRole() == AccessibleRole::PARAGRAPH
                                      || xParaContext->getAccessibleRole() == AccessibleRole::BLOCK_QUOTE))
            {
                SwAccessibleParagraph* pAccPara = static_cast< SwAccessibleParagraph *>(xPara.get());
                if (m_setParaAdd.count(pAccPara) == 0 )
                {
                    m_setParaRemove.insert(pAccPara);
                }
            }
        }
    }
}

SwAccessibleMap::SwAccessibleMap(SwViewShell& rViewShell) :
    m_rViewShell(rViewShell),
    mbShapeSelected( false ),
    maDocName(SwAccessibleContext::GetResource(STR_ACCESS_DOC_NAME))
{
    rViewShell.GetLayout()->AddAccessibleShell();
}

SwAccessibleMap::~SwAccessibleMap()
{
    DBG_TESTSOLARMUTEX();

    rtl::Reference < SwAccessibleContext > xAcc;
    if (!maFrameMap.empty())
    {
        const SwRootFrame* pRootFrame = GetShell().GetLayout();
        SwAccessibleContextMap::iterator aIter = maFrameMap.find(pRootFrame);
        if (aIter != maFrameMap.end())
            xAcc = (*aIter).second;
        if( !xAcc.is() )
            assert(false); // let's hope this can't happen? the vcl::Window apparently owns the top-level
            //xAcc = new SwAccessibleDocument(shared_from_this());
    }

    if(xAcc.is())
    {
        SwAccessibleDocumentBase *const pAcc =
            static_cast<SwAccessibleDocumentBase *>(xAcc.get());
        pAcc->Dispose( true );
    }
#if OSL_DEBUG_LEVEL > 0 && !defined NDEBUG
    for (const auto& rIter : maFrameMap)
    {
        rtl::Reference <SwAccessibleContext> xTmp = rIter.second;
        if( xTmp.is() )
            assert(xTmp->GetMap() == nullptr); // must be disposed
    }
#endif

    assert(maFrameMap.empty() && "Frame map should be empty after disposing the root frame");
    assert((!mpShapeMap || mpShapeMap->empty()) &&
            "Object map should be empty after disposing the root frame");
    mpShapeMap.reset();
    mvShapes.clear();
    mpSelectedParas.reset();

    mpPreview.reset();

    {
        osl::MutexGuard aGuard( maEventMutex );
        assert(!mpEvents);
        assert(!mpEventMap);
        mpEventMap.reset();
        mpEvents.reset();
    }
    m_rViewShell.GetLayout()->RemoveAccessibleShell();
}

rtl::Reference<SwAccessibleContext> SwAccessibleMap::GetDocumentView_(
    bool bPagePreview )
{
    DBG_TESTSOLARMUTEX();

    rtl::Reference < SwAccessibleContext > xAcc;
    bool bSetVisArea = false;

    const SwRootFrame* pRootFrame = GetShell().GetLayout();
    SwAccessibleContextMap::iterator aIter = maFrameMap.find(pRootFrame);
    if (aIter != maFrameMap.end())
        xAcc = (*aIter).second;
    if( xAcc.is() )
    {
        bSetVisArea = true; // Set VisArea when map mutex is not locked
    }
    else
    {
        if( bPagePreview )
            xAcc = new SwAccessiblePreview(shared_from_this());
        else
            xAcc = new SwAccessibleDocument(shared_from_this());

        if (aIter != maFrameMap.end())
        {
            (*aIter).second = xAcc.get();
        }
        else
        {
            maFrameMap.emplace(pRootFrame, xAcc);
        }
    }

    if( bSetVisArea )
    {
        SwAccessibleDocumentBase *pAcc =
            static_cast< SwAccessibleDocumentBase * >( xAcc.get() );
        pAcc->SetVisArea();
    }

    return xAcc;
}

rtl::Reference<comphelper::OAccessible> SwAccessibleMap::GetDocumentView()
{
    return GetDocumentView_( false );
}

rtl::Reference<comphelper::OAccessible>
SwAccessibleMap::GetDocumentPreview(const std::vector<std::unique_ptr<PreviewPage>>& _rPreviewPages,
                                    const Fraction& _rScale, const SwPageFrame* _pSelectedPageFrame,
                                    const Size& _rPreviewWinSize)
{
    // create & update preview data object
    if( mpPreview == nullptr )
        mpPreview.reset( new SwAccPreviewData() );
    mpPreview->Update( *this, _rPreviewPages, _rScale, _pSelectedPageFrame, _rPreviewWinSize );

    rtl::Reference<SwAccessibleContext> xAcc = GetDocumentView_( true );
    return xAcc;
}

rtl::Reference<SwAccessibleContext> SwAccessibleMap::GetContextImpl(const SwFrame* pFrame,
                                                                    bool bCreate)
{
    DBG_TESTSOLARMUTEX();

    rtl::Reference < SwAccessibleContext > xAcc;

    SwAccessibleContextMap::iterator aIter = maFrameMap.find(pFrame);
    if (aIter != maFrameMap.end())
        xAcc = (*aIter).second;

    if (xAcc.is() || !bCreate)
        return xAcc;

    switch( pFrame->GetType() )
    {
    case SwFrameType::Txt:
        xAcc = new SwAccessibleParagraph(shared_from_this(),
                        static_cast< const SwTextFrame& >( *pFrame ) );
        break;
    case SwFrameType::Header:
        xAcc = new SwAccessibleHeaderFooter(shared_from_this(),
                        static_cast< const SwHeaderFrame *>( pFrame ) );
        break;
    case SwFrameType::Footer:
        xAcc = new SwAccessibleHeaderFooter(shared_from_this(),
                        static_cast< const SwFooterFrame *>( pFrame ) );
        break;
    case SwFrameType::Footnote:
        {
            const SwFootnoteFrame *pFootnoteFrame =
                static_cast < const SwFootnoteFrame * >( pFrame );
            bool bIsEndnote =
                SwAccessibleFootnote::IsEndnote( pFootnoteFrame );
            xAcc = new SwAccessibleFootnote(shared_from_this(), bIsEndnote, pFootnoteFrame);
        }
        break;
    case SwFrameType::Fly:
        {
            const SwFlyFrame *pFlyFrame =
                static_cast < const SwFlyFrame * >( pFrame );
            switch( SwAccessibleFrameBase::GetNodeType( pFlyFrame ) )
            {
            case SwNodeType::Grf:
                xAcc = new SwAccessibleGraphic(shared_from_this(), pFlyFrame );
                break;
            case SwNodeType::Ole:
                xAcc = new SwAccessibleEmbeddedObject(shared_from_this(), pFlyFrame );
                break;
            default:
                xAcc = new SwAccessibleTextFrame(shared_from_this(), *pFlyFrame );
                break;
            }
        }
        break;
    case SwFrameType::Cell:
        xAcc = new SwAccessibleCell(shared_from_this(),
                        static_cast< const SwCellFrame *>( pFrame ) );
        break;
    case SwFrameType::Tab:
        xAcc = new SwAccessibleTable(shared_from_this(),
                        static_cast< const SwTabFrame *>( pFrame ) );
        break;
    case SwFrameType::Page:
        OSL_ENSURE(GetShell().IsPreview(),
                    "accessible page frames only in PagePreview" );
        xAcc = new SwAccessiblePage(shared_from_this(), pFrame);
        break;
    default: break;
    }
    assert(xAcc.is());

    if (aIter != maFrameMap.end())
    {
        (*aIter).second = xAcc.get();
    }
    else
    {
        maFrameMap.emplace(pFrame, xAcc);
    }

    if (xAcc->HasCursor() && !AreInSameTable(mxCursorContext, pFrame))
    {
        // If the new context has the focus, and if we know
        // another context that had the focus, then the focus
        // just moves from the old context to the new one. We
        // then have to send a focus event and a caret event for
        // the old context. We have to do that now,
        // because after we have left this method, anyone might
        // call getStates for the new context and will get a
        // focused state then. Sending the focus changes event
        // after that seems to be strange. However, we cannot
        // send a focus event for the new context now, because
        // no one except us knows it. In any case, we remember
        // the new context as the one that has the focus
        // currently.
        rtl::Reference<SwAccessibleContext> xOldCursorAcc = mxCursorContext;
        mxCursorContext = xAcc.get();

        const bool bOldShapeSelected = mbShapeSelected;
        mbShapeSelected = false;

        // Invalidate focus for old object when map is not locked
        if (xOldCursorAcc.is())
            InvalidateCursorPosition(xOldCursorAcc);
        if (bOldShapeSelected)
            InvalidateShapeSelection();
    }

    return xAcc;
}

uno::Reference<XAccessible> SwAccessibleMap::GetContext(const SwFrame* pFrame, bool bCreate)
{
    return GetContextImpl(pFrame, bCreate);
}

rtl::Reference<::accessibility::AccessibleShape>
SwAccessibleMap::GetContextImpl(const SdrObject* pObj, SwAccessibleContext* pParentImpl,
                                bool bCreate)
{
    DBG_TESTSOLARMUTEX();

    if( !mpShapeMap && bCreate )
        mpShapeMap.reset(new SwAccessibleShapeMap_Impl( this ));
    if (!mpShapeMap)
        return nullptr;

    rtl::Reference<::accessibility::AccessibleShape> xAcc;
    SwAccessibleShapeMap_Impl::iterator aIter = mpShapeMap->find( pObj );
    if( aIter != mpShapeMap->end() )
        xAcc = (*aIter).second;

    if( !xAcc.is() && bCreate )
    {
        uno::Reference < drawing::XShape > xShape(
            const_cast< SdrObject * >( pObj )->getUnoShape(),
            uno::UNO_QUERY );
        if( xShape.is() )
        {
            ::accessibility::ShapeTypeHandler& rShapeTypeHandler =
                        ::accessibility::ShapeTypeHandler::Instance();

            ::accessibility::AccessibleShapeInfo aShapeInfo(
                    xShape, uno::Reference<XAccessible>(pParentImpl), this );

            xAcc = rShapeTypeHandler.CreateAccessibleObject(
                        aShapeInfo, mpShapeMap->GetInfo() );
        }
        assert(xAcc.is());
        xAcc->Init();
        if( aIter != mpShapeMap->end() )
        {
            (*aIter).second = xAcc.get();
        }
        else
        {
            mpShapeMap->emplace( pObj, xAcc );
        }
        // TODO: focus!!!
        AddGroupContext(pObj, xAcc);
    }

    return xAcc;
}

uno::Reference<XAccessible>
SwAccessibleMap::GetContext(const SdrObject* pObj, SwAccessibleContext* pParentImpl, bool bCreate)
{
    return GetContextImpl(pObj, pParentImpl, bCreate);
}

bool SwAccessibleMap::IsInSameLevel(const SdrObject* pObj, const SwFEShell* pFESh)
{
    if (pFESh)
        return pFESh->IsObjSameLevelWithMarked(pObj);
    return false;
}

void SwAccessibleMap::AddShapeContext(const SdrObject *pObj, rtl::Reference < ::accessibility::AccessibleShape > const & xAccShape)
{
    DBG_TESTSOLARMUTEX();

    if( mpShapeMap )
    {
        mpShapeMap->emplace( pObj, xAccShape );
    }
}

//Added by yanjun for sym2_6407
void SwAccessibleMap::RemoveGroupContext(const SdrObject *pParentObj)
{
    DBG_TESTSOLARMUTEX();

    // TODO: Why are sub-shapes of group shapes even added to our map?
    //       Doesn't the AccessibleShape of the top-level shape create them
    //       on demand anyway? Why does SwAccessibleMap need to know them?
    // We cannot rely on getAccessibleChild here to remove the sub-shapes
    // from mpShapes because the top-level shape may not only be disposed here
    // but also by visibility checks in svx, then it doesn't return children.
    if (mpShapeMap && pParentObj && pParentObj->IsGroupObject())
    {
        if (SdrObjList *const pChildren = pParentObj->GetSubList())
            for (const rtl::Reference<SdrObject>& pChild : *pChildren)
            {
                assert(pChild);
                RemoveContext(pChild.get());
            }
    }
}
//End

void SwAccessibleMap::AddGroupContext(const SdrObject *pParentObj, uno::Reference < XAccessible > const & xAccParent)
{
    DBG_TESTSOLARMUTEX();

    if( !mpShapeMap )
        return;

    //here get all the sub list.
    if (!pParentObj->IsGroupObject())
        return;

    if (!xAccParent.is())
        return;

    uno::Reference < XAccessibleContext > xContext = xAccParent->getAccessibleContext();
    if (!xContext.is())
        return;

    sal_Int64 nChildren = xContext->getAccessibleChildCount();
    for(sal_Int64 i = 0; i<nChildren; i++)
    {
        uno::Reference < XAccessible > xChild = xContext->getAccessibleChild(i);
        if (xChild.is())
        {
            uno::Reference < XAccessibleContext > xChildContext = xChild->getAccessibleContext();
            if (xChildContext.is())
            {
                short nRole = xChildContext->getAccessibleRole();
                if (nRole == AccessibleRole::SHAPE)
                {
                    ::accessibility::AccessibleShape* pAccShape = static_cast < ::accessibility::AccessibleShape* >( xChild.get());
                    uno::Reference < drawing::XShape > xShape = pAccShape->GetXShape();
                    if (xShape.is())
                    {
                        SdrObject* pObj = SdrObject::getSdrObjectFromXShape(xShape);
                        AddShapeContext(pObj, pAccShape);
                        AddGroupContext(pObj,xChild);
                    }
                }
            }
        }
    }
}

void SwAccessibleMap::RemoveContext( const SwFrame *pFrame )
{
    DBG_TESTSOLARMUTEX();

    SwAccessibleContextMap::iterator aIter = maFrameMap.find(pFrame);
    if (aIter == maFrameMap.end())
        return;

    maFrameMap.erase(aIter);

    SwAccessibleContextMap::iterator aSelectedIter = maSelectedFrameMap.find(pFrame);
    if (aSelectedIter != maSelectedFrameMap.end())
        maSelectedFrameMap.erase(aSelectedIter);

    // Remove reference to old caret object. Though mxCursorContext
    // is a weak reference and cleared automatically, clearing it
    // directly makes sure to not keep a non-functional object.
    rtl::Reference < SwAccessibleContext > xOldAcc( mxCursorContext );
    if( xOldAcc.is() )
    {
        SwAccessibleContext *pOldAccImpl = xOldAcc.get();
        OSL_ENSURE( pOldAccImpl->GetFrame(), "old caret context is disposed" );
        if( pOldAccImpl->GetFrame() == pFrame )
        {
            xOldAcc.clear();    // get an empty ref
            mxCursorContext = xOldAcc.get();
        }
    }
}

void SwAccessibleMap::RemoveContext( const SdrObject *pObj )
{
    DBG_TESTSOLARMUTEX();

    if( !mpShapeMap )
        return;

    SwAccessibleShapeMap_Impl::iterator aIter = mpShapeMap->find( pObj );
    if( aIter == mpShapeMap->end() )
        return;

    rtl::Reference < ::accessibility::AccessibleShape > xTempHold( (*aIter).second );
    mpShapeMap->erase( aIter );
    RemoveGroupContext(pObj);
    // The shape selection flag is not cleared, but one might do
    // so but has to make sure that the removed context is the one
    // that is selected.

    if( mpShapeMap && mpShapeMap->empty() )
    {
        mpShapeMap.reset();
    }
}

bool SwAccessibleMap::Contains(const SwFrame *pFrame) const
{
    return pFrame && maFrameMap.contains(pFrame);
}

void SwAccessibleMap::A11yDispose( const SwFrame *pFrame,
                                   const SdrObject *pObj,
                                   vcl::Window* pWindow,
                                   bool bRecursive,
                                   bool bCanSkipInvisible )
{
    DBG_TESTSOLARMUTEX();

    SwAccessibleChild aFrameOrObj( pFrame, pObj, pWindow );

    // Indeed, the following assert checks the frame's accessible flag,
    // because that's the one that is evaluated in the layout. The frame
    // might not be accessible anyway. That's the case for cell frames that
    // contain further cells.
    OSL_ENSURE( !aFrameOrObj.GetSwFrame() || aFrameOrObj.GetSwFrame()->IsAccessibleFrame(),
            "non accessible frame should be disposed" );

    if (!(aFrameOrObj.IsAccessible(GetShell().IsPreview())
               // fdo#87199 dispose the darn thing if it ever was accessible
            || Contains(pFrame)))
        return;

    ::rtl::Reference< SwAccessibleContext > xAccImpl;
    ::rtl::Reference< SwAccessibleContext > xParentAccImpl;
    ::rtl::Reference< ::accessibility::AccessibleShape > xShapeAccImpl;

    // get accessible context for frame
    // First of all look for an accessible context for a frame
    if (aFrameOrObj.GetSwFrame())
    {
        SwAccessibleContextMap::iterator aIter = maFrameMap.find(aFrameOrObj.GetSwFrame());
        if (aIter != maFrameMap.end())
            xAccImpl = (*aIter).second;
    }
    if (!xAccImpl.is())
    {
        // If there is none, look if the parent is accessible.
        const SwFrame *pParent =
                SwAccessibleFrame::GetParent( aFrameOrObj,
                                              GetShell().IsPreview());

        if( pParent )
        {
            SwAccessibleContextMap::iterator aIter = maFrameMap.find(pParent);
            if (aIter != maFrameMap.end())
                xParentAccImpl = (*aIter).second;
        }
    }
    if( !xParentAccImpl.is() && !aFrameOrObj.GetSwFrame() && mpShapeMap )
    {
        SwAccessibleShapeMap_Impl::iterator aIter =
            mpShapeMap->find( aFrameOrObj.GetDrawObject() );
        if( aIter != mpShapeMap->end() )
        {
            xShapeAccImpl = aIter->second;
        }
    }
    if (pObj && GetShell().ActionPend() &&
        (xParentAccImpl.is() || xShapeAccImpl.is()) )
    {
        // Keep a reference to the XShape to avoid that it
        // is deleted with a SwFrameFormat::SwClientNotify.
        uno::Reference < drawing::XShape > xShape(
            const_cast< SdrObject * >( pObj )->getUnoShape(),
            uno::UNO_QUERY );
        if( xShape.is() )
        {
            mvShapes.push_back( xShape );
        }
    }

    // remove events stored for the frame
    {
        osl::MutexGuard aGuard( maEventMutex );
        if( mpEvents )
        {
            SwAccessibleEventMap_Impl::iterator aIter =
                mpEventMap->find( aFrameOrObj );
            if( aIter != mpEventMap->end() )
            {
                SwAccessibleEvent_Impl aEvent(
                        SwAccessibleEvent_Impl::DISPOSE, aFrameOrObj );
                AppendEvent( aEvent );
            }
        }
    }

    // If the frame is accessible and there is a context for it, dispose
    // the frame. If the frame is no context for it but disposing should
    // take place recursive, the frame's children have to be disposed
    // anyway, so we have to create the context then.
    if( xAccImpl.is() )
    {
        xAccImpl->Dispose( bRecursive );
    }
    else if( xParentAccImpl.is() )
    {
        // If the frame is a cell frame, the table must be notified.
        // If we are in an action, a table model change event will
        // be broadcasted at the end of the action to give the table
        // a chance to generate a single table change event.

        xParentAccImpl->DisposeChild( aFrameOrObj, bRecursive, bCanSkipInvisible );
    }
    else if( xShapeAccImpl.is() )
    {
        RemoveContext( aFrameOrObj.GetDrawObject() );
        xShapeAccImpl->dispose();
    }

    if( mpPreview && pFrame && pFrame->IsPageFrame() )
        mpPreview->DisposePage( static_cast< const SwPageFrame *>( pFrame ) );
}

void SwAccessibleMap::InvalidatePosOrSize( const SwFrame *pFrame,
                                           const SdrObject *pObj,
                                           vcl::Window* pWindow,
                                           const SwRect& rOldBox )
{
    DBG_TESTSOLARMUTEX();

    SwAccessibleChild aFrameOrObj( pFrame, pObj, pWindow );
    if (!aFrameOrObj.IsAccessible(GetShell().IsPreview()))
        return;

    ::rtl::Reference< SwAccessibleContext > xAccImpl;
    ::rtl::Reference< SwAccessibleContext > xParentAccImpl;
    const SwFrame *pParent =nullptr;
    if (aFrameOrObj.GetSwFrame())
    {
        SwAccessibleContextMap::iterator aIter = maFrameMap.find(aFrameOrObj.GetSwFrame());
        if (aIter != maFrameMap.end())
        {
            // If there is an accessible object already it is
            // notified directly.
            xAccImpl = (*aIter).second;
        }
    }
    if (!xAccImpl.is())
    {
        // Otherwise we look if the parent is accessible.
        // If not, there is nothing to do.
        pParent =
            SwAccessibleFrame::GetParent( aFrameOrObj,
                                          GetShell().IsPreview());

        if (pParent)
        {
            SwAccessibleContextMap::iterator aIter = maFrameMap.find(pParent);
            if (aIter != maFrameMap.end())
                xParentAccImpl = (*aIter).second;
        }
    }

    if( xAccImpl.is() )
    {
        if (GetShell().ActionPend())
        {
            SwAccessibleEvent_Impl aEvent(
                SwAccessibleEvent_Impl::POS_CHANGED, xAccImpl.get(),
                std::move(aFrameOrObj), rOldBox );
            AppendEvent( aEvent );
        }
        else
        {
            FireEvents();
            if (xAccImpl->GetFrame()) // not if disposed by FireEvents()
            {
                xAccImpl->InvalidatePosOrSize(rOldBox);
            }
        }
    }
    else if( xParentAccImpl.is() )
    {
        if (GetShell().ActionPend())
        {
            assert(pParent);
            // tdf#99722 faster not to buffer events that won't be sent
            if (!SwAccessibleChild(pParent).IsVisibleChildrenOnly()
                || xParentAccImpl->IsShowing(rOldBox)
                || xParentAccImpl->IsShowing(*this, aFrameOrObj))
            {
                SwAccessibleEvent_Impl aEvent(
                    SwAccessibleEvent_Impl::CHILD_POS_CHANGED,
                    xParentAccImpl.get(), std::move(aFrameOrObj), rOldBox );
                AppendEvent( aEvent );
            }
        }
        else
        {
            FireEvents();
            xParentAccImpl->InvalidateChildPosOrSize( aFrameOrObj,
                                                      rOldBox );
        }
    }
    else if(pParent)
    {
/*
For child graphic and its parent paragraph,if split 2 graphic to 2 paragraph,
will delete one graphic swfrm and new create 1 graphic swfrm ,
then the new paragraph and the new graphic SwFrame will add .
but when add graphic SwFrame ,the accessible of the new Paragraph is not created yet.
so the new graphic accessible 'parent is NULL,
so run here: save the parent's SwFrame not the accessible object parent,
*/
        bool bIsValidFrame = false;
        bool bIsTextParent = false;
        if (aFrameOrObj.GetSwFrame())
        {
            if (SwFrameType::Fly == pFrame->GetType())
            {
                bIsValidFrame =true;
            }
        }
        else if(pObj)
        {
            if (SwFrameType::Txt == pParent->GetType())
            {
                bIsTextParent =true;
            }
        }
        if( bIsValidFrame || bIsTextParent )
        {
            if (GetShell().ActionPend())
            {
                SwAccessibleEvent_Impl aEvent(
                    SwAccessibleEvent_Impl::CHILD_POS_CHANGED,
                    pParent, std::move(aFrameOrObj), rOldBox );
                AppendEvent( aEvent );
            }
            else
            {
                OSL_ENSURE(false,"");
            }
        }
    }
}

void SwAccessibleMap::InvalidateContent( const SwFrame *pFrame )
{
    DBG_TESTSOLARMUTEX();

    SwAccessibleChild aFrameOrObj( pFrame );
    if (!aFrameOrObj.IsAccessible(GetShell().IsPreview()))
        return;

    rtl::Reference < SwAccessibleContext > xAcc;
    SwAccessibleContextMap::iterator aIter = maFrameMap.find(aFrameOrObj.GetSwFrame());
    if (aIter != maFrameMap.end())
        xAcc = (*aIter).second;

    if( !xAcc.is() )
        return;

    if (GetShell().ActionPend())
    {
        SwAccessibleEvent_Impl aEvent(
            SwAccessibleEvent_Impl::INVALID_CONTENT, xAcc.get(),
            std::move(aFrameOrObj) );
        AppendEvent( aEvent );
    }
    else
    {
        FireEvents();
        xAcc->InvalidateContent();
    }
}

void SwAccessibleMap::InvalidateAttr( const SwTextFrame& rTextFrame )
{
    DBG_TESTSOLARMUTEX();

    SwAccessibleChild aFrameOrObj( &rTextFrame );
    if (!aFrameOrObj.IsAccessible(GetShell().IsPreview()))
        return;

    rtl::Reference < SwAccessibleContext > xAcc;
    SwAccessibleContextMap::iterator aIter = maFrameMap.find(aFrameOrObj.GetSwFrame());
    if (aIter != maFrameMap.end())
        xAcc = (*aIter).second;

    if( !xAcc.is() )
        return;

    if (GetShell().ActionPend())
    {
        SwAccessibleEvent_Impl aEvent( SwAccessibleEvent_Impl::INVALID_ATTR,
                                       xAcc.get(), std::move(aFrameOrObj) );
        aEvent.SetStates( AccessibleStates::TEXT_ATTRIBUTE_CHANGED );
        AppendEvent( aEvent );
    }
    else
    {
        FireEvents();
        xAcc->InvalidateAttr();
    }
}

void SwAccessibleMap::InvalidateCursorPosition( const SwFrame *pFrame )
{
    DBG_TESTSOLARMUTEX();

    const SwFrame* pAccFrame = pFrame;
    bool bShapeSelected = false;
    const SwViewShell& rVSh = GetShell();
    if( auto pCSh = dynamic_cast<const SwCursorShell*>(&rVSh) )
    {
        if( pCSh->IsTableMode() )
        {
            while (pAccFrame && !pAccFrame->IsCellFrame())
                pAccFrame = pAccFrame->GetUpper();
        }
        else if( auto pFESh = dynamic_cast<const SwFEShell*>(&rVSh) )
        {
            const SwFrame *pFlyFrame = pFESh->GetSelectedFlyFrame();
            if( pFlyFrame )
            {
                OSL_ENSURE( !pFrame || pFrame->FindFlyFrame() == pFlyFrame,
                        "cursor is not contained in fly frame" );
                pAccFrame = pFlyFrame;
            }
            else if( pFESh->GetSelectedObjCount() > 0 )
            {
                bShapeSelected = true;
                pAccFrame = nullptr;
            }
        }
    }

    OSL_ENSURE(bShapeSelected || (pAccFrame && SwAccessibleChild::IsFrameAccessible(*pAccFrame, GetShell().IsPreview())),
            "frame is not accessible" );

    rtl::Reference <SwAccessibleContext> xOldAcc = mxCursorContext;
    mxCursorContext.clear();

    const bool bOldShapeSelected = mbShapeSelected;
    mbShapeSelected = bShapeSelected;

    rtl::Reference <SwAccessibleContext> xAcc;
    if (pAccFrame)
    {
        SwAccessibleContextMap::iterator aIter = maFrameMap.find(pAccFrame);
        if (aIter != maFrameMap.end())
            xAcc = (*aIter).second;
        else
        {
            SwRect rcEmpty;
            const SwTabFrame* pTabFrame = pAccFrame->FindTabFrame();
            if (pTabFrame)
            {
                InvalidatePosOrSize(pTabFrame, nullptr, nullptr, rcEmpty);
            }
            else
            {
                InvalidatePosOrSize(pAccFrame, nullptr, nullptr, rcEmpty);
            }

            aIter = maFrameMap.find(pAccFrame);
            if (aIter != maFrameMap.end())
            {
                xAcc = (*aIter).second;
            }
        }

        // For cells, some extra thoughts are necessary,
        // because invalidating the cursor for one cell
        // invalidates the cursor for all cells of the same
        // table. For this reason, we don't want to
        // invalidate the cursor for the old cursor object
        // and the new one if they are within the same table,
        // because this would result in doing the work twice.
        // Moreover, we have to make sure to invalidate the
        // cursor even if the current cell has no accessible object.
        // If the old cursor objects exists and is in the same
        // table, it's the best choice, because using it avoids
        // an unnecessary cursor invalidation cycle when creating
        // a new object for the current cell.
        if (pAccFrame->IsCellFrame())
        {
            if (xOldAcc.is() && AreInSameTable(xOldAcc, pAccFrame))
            {
                if( xAcc.is() )
                    xOldAcc = xAcc; // avoid extra invalidation
                else
                    xAcc = xOldAcc; // make sure at least one
            }
            if( !xAcc.is() )
                xAcc = GetContextImpl(pAccFrame);
        }
    }
    else if (bShapeSelected)
    {
        const SwFEShell* pFESh = static_cast<const SwFEShell*>(&rVSh);
        const SdrMarkList *pMarkList = pFESh->GetMarkList();
        if (pMarkList != nullptr && pMarkList->GetMarkCount() == 1)
        {
            SdrObject *pObj = pMarkList->GetMark( 0 )->GetMarkedSdrObj();
            ::rtl::Reference < ::accessibility::AccessibleShape > pAccShapeImpl = GetContextImpl(pObj,nullptr,false);
            if (!pAccShapeImpl.is())
            {
                while (pObj && pObj->getParentSdrObjectFromSdrObject())
                {
                    pObj = pObj->getParentSdrObjectFromSdrObject();
                }
                if (pObj != nullptr)
                {
                    const SwFrame *pParent = SwAccessibleFrame::GetParent(SwAccessibleChild(pObj), GetShell().IsPreview());
                    if( pParent )
                    {
                        ::rtl::Reference< SwAccessibleContext > xParentAccImpl = GetContextImpl(pParent,false);
                        if (!xParentAccImpl.is())
                        {
                            const SwTabFrame* pTabFrame = pParent->FindTabFrame();
                            if (pTabFrame)
                            {
                                //The Table should not add in acc.because the "pParent" is not add to acc .
                                uno::Reference< XAccessible>  xAccParentTab = GetContext(pTabFrame);//Should Create.

                                const SwFrame *pParentRoot = SwAccessibleFrame::GetParent(SwAccessibleChild(pTabFrame), GetShell().IsPreview());
                                if (pParentRoot)
                                {
                                    ::rtl::Reference< SwAccessibleContext > xParentAccImplRoot = GetContextImpl(pParentRoot,false);
                                    if(xParentAccImplRoot.is())
                                    {
                                        xParentAccImplRoot->FireAccessibleEvent(
                                            AccessibleEventId::CHILD, uno::Any(),
                                            uno::Any(xAccParentTab));
                                    }
                                }

                                //Get "pParent" acc again.
                                xParentAccImpl = GetContextImpl(pParent,false);
                            }
                            else
                            {
                                //directly create this acc para .
                                xParentAccImpl = GetContextImpl(pParent);//Should Create.

                                const SwFrame *pParentRoot = SwAccessibleFrame::GetParent(SwAccessibleChild(pParent), GetShell().IsPreview());

                                ::rtl::Reference< SwAccessibleContext > xParentAccImplRoot = GetContextImpl(pParentRoot,false);
                                if(xParentAccImplRoot.is())
                                {
                                    xParentAccImplRoot->FireAccessibleEvent(
                                        AccessibleEventId::CHILD, uno::Any(),
                                        uno::Any(uno::Reference<XAccessible>(xParentAccImpl)));
                                }
                            }
                        }
                        if (xParentAccImpl.is())
                        {
                            uno::Reference< XAccessible>  xAccShape =
                                GetContext(pObj,xParentAccImpl.get());
                            xParentAccImpl->FireAccessibleEvent(
                                AccessibleEventId::CHILD, uno::Any(), uno::Any(xAccShape));
                        }
                    }
                }
            }
        }
    }

    m_setParaAdd.clear();
    m_setParaRemove.clear();
    if( xOldAcc.is() && xOldAcc != xAcc )
        InvalidateCursorPosition( xOldAcc );
    if( bOldShapeSelected || bShapeSelected )
        InvalidateShapeSelection();
    if( xAcc.is() )
        InvalidateCursorPosition( xAcc );

    InvalidateShapeInParaSelection();

    for (SwAccessibleParagraph* pAccPara : m_setParaRemove)
    {
        if (pAccPara && !pAccPara->IsDisposed() &&
            pAccPara->getSelectedAccessibleChildCount() == 0 &&
            pAccPara->getSelectedText().getLength() == 0)
        {
            if(pAccPara->SetSelectedState(false))
            {
                pAccPara->FireAccessibleEvent(AccessibleEventId::SELECTION_CHANGED_REMOVE,
                                              uno::Any(), uno::Any());
            }
        }
    }
    for (SwAccessibleParagraph* pAccPara : m_setParaAdd)
    {
        if(pAccPara && pAccPara->SetSelectedState(true))
        {
            pAccPara->FireAccessibleEvent(AccessibleEventId::SELECTION_CHANGED, uno::Any(),
                                          uno::Any());
        }
    }
}

void SwAccessibleMap::InvalidateFocus()
{
    DBG_TESTSOLARMUTEX();

    if (GetShell().IsPreview())
    {
        rtl::Reference<SwAccessibleContext> xDocView = GetDocumentView_(true);
        assert(xDocView.is());
        xDocView->InvalidateFocus();
    }

    rtl::Reference < SwAccessibleContext > xAcc = mxCursorContext;
    if( xAcc.is() )
    {
        xAcc->InvalidateFocus();
    }
    else
    {
        DoInvalidateShapeSelection(true);
    }
}

void SwAccessibleMap::SetCursorContext(
        const ::rtl::Reference < SwAccessibleContext >& rCursorContext )
{
    DBG_TESTSOLARMUTEX();
    mxCursorContext = rCursorContext.get();
}

void SwAccessibleMap::InvalidateEditableStates( const SwFrame* _pFrame )
{
    // Start with the frame or the first upper that is accessible
    const SwFrame* pAccFrame = _pFrame;
    while (pAccFrame && !SwAccessibleChild::IsFrameAccessible(*pAccFrame, GetShell().IsPreview()))
        pAccFrame = pAccFrame->GetUpper();
    if (!pAccFrame)
        pAccFrame = GetShell().GetLayout();

    rtl::Reference<SwAccessibleContext> xAccImpl = GetContextImpl(pAccFrame);
    if (GetShell().ActionPend())
    {
        SwAccessibleEvent_Impl aEvent(SwAccessibleEvent_Impl::CARET_OR_STATES, xAccImpl.get(),
                                      SwAccessibleChild(xAccImpl->GetFrame()),
                                      AccessibleStates::EDITABLE);
        AppendEvent( aEvent );
    }
    else
    {
        FireEvents();
        xAccImpl->InvalidateStates(AccessibleStates::EDITABLE);
    }
}

void SwAccessibleMap::InvalidateRelationSet_(const SwFrame& rFrame,
                                              bool bFrom )
{
    DBG_TESTSOLARMUTEX();

    // first, see if this frame is accessible, and if so, get the respective
    if (!SwAccessibleChild::IsFrameAccessible(rFrame, GetShell().IsPreview()))
        return;

    rtl::Reference < SwAccessibleContext > xAcc;
    SwAccessibleContextMap::iterator aIter = maFrameMap.find(&rFrame);
    if (aIter != maFrameMap.end())
    {
        xAcc = (*aIter).second;
    }

    // deliver event directly, or queue event
    if( !xAcc.is() )
        return;

    if (GetShell().ActionPend())
    {
        SwAccessibleEvent_Impl aEvent( SwAccessibleEvent_Impl::CARET_OR_STATES,
                                       xAcc.get(), SwAccessibleChild(&rFrame),
                                       ( bFrom
                                         ? AccessibleStates::RELATION_FROM
                                         : AccessibleStates::RELATION_TO ) );
        AppendEvent( aEvent );
    }
    else
    {
        FireEvents();
        xAcc->InvalidateRelation( bFrom
                ? AccessibleEventId::CONTENT_FLOWS_FROM_RELATION_CHANGED
                : AccessibleEventId::CONTENT_FLOWS_TO_RELATION_CHANGED );
    }
}

void SwAccessibleMap::InvalidateRelationSet(const SwFrame& rMaster, const SwFrame& rFollow)
{
    InvalidateRelationSet_(rMaster, false);
    InvalidateRelationSet_(rFollow, true);
}

// invalidation of CONTENT_FLOW_FROM/_TO relation of a paragraph
void SwAccessibleMap::InvalidateParaFlowRelation( const SwTextFrame& _rTextFrame,
                                                  const bool _bFrom )
{
    InvalidateRelationSet_(_rTextFrame, _bFrom );
}

// invalidation of text selection of a paragraph
void SwAccessibleMap::InvalidateParaTextSelection( const SwTextFrame& _rTextFrame )
{
    DBG_TESTSOLARMUTEX();

    // first, see if this frame is accessible, and if so, get the respective
    if (!SwAccessibleChild::IsFrameAccessible(_rTextFrame, GetShell().IsPreview()))
        return;

    rtl::Reference < SwAccessibleContext > xAcc;
    SwAccessibleContextMap::iterator aIter = maFrameMap.find(&_rTextFrame);
    if (aIter != maFrameMap.end())
    {
        xAcc = (*aIter).second;
    }

    // deliver event directly, or queue event
    if( !xAcc.is() )
        return;

    if (GetShell().ActionPend())
    {
        SwAccessibleEvent_Impl aEvent(
            SwAccessibleEvent_Impl::CARET_OR_STATES,
            xAcc.get(),
            SwAccessibleChild( &_rTextFrame ),
            AccessibleStates::TEXT_SELECTION_CHANGED );
        AppendEvent( aEvent );
    }
    else
    {
        FireEvents();
        xAcc->InvalidateTextSelection();
    }
}

sal_Int32 SwAccessibleMap::GetChildIndex( const SwFrame& rParentFrame,
                                          vcl::Window& rChild ) const
{
    DBG_TESTSOLARMUTEX();

    sal_Int32 nIndex( -1 );

    if (SwAccessibleChild::IsFrameAccessible(rParentFrame, GetShell().IsPreview()))
    {
        SwAccessibleContextMap::const_iterator aIter = maFrameMap.find(&rParentFrame);
        if (aIter != maFrameMap.end())
        {
            rtl::Reference<SwAccessibleContext> xAcc = (*aIter).second;
            if (xAcc.is())
                nIndex = xAcc->GetChildIndex(const_cast<SwAccessibleMap&>(*this),
                                             SwAccessibleChild(&rChild));
        }
    }

    return nIndex;
}

void SwAccessibleMap::UpdatePreview( const std::vector<std::unique_ptr<PreviewPage>>& _rPreviewPages,
                                     const Fraction&  _rScale,
                                     const SwPageFrame* _pSelectedPageFrame,
                                     const Size&      _rPreviewWinSize )
{
    DBG_TESTSOLARMUTEX();
    assert(GetShell().IsPreview() && "no preview?");
    assert(mpPreview != nullptr && "no preview data?");

    mpPreview->Update( *this, _rPreviewPages, _rScale, _pSelectedPageFrame, _rPreviewWinSize );

    // propagate change of VisArea through the document's
    // accessibility tree; this will also send appropriate scroll
    // events
    SwAccessibleContext* pDoc =
        GetContextImpl(GetShell().GetLayout()).get();
    static_cast<SwAccessibleDocumentBase*>( pDoc )->SetVisArea();

    rtl::Reference < SwAccessibleContext > xOldAcc = mxCursorContext;
    rtl::Reference < SwAccessibleContext > xAcc;

    const SwPageFrame *pSelPage = mpPreview->GetSelPage();
    if (pSelPage)
    {
        SwAccessibleContextMap::iterator aIter = maFrameMap.find(pSelPage);
        if (aIter != maFrameMap.end())
            xAcc = (*aIter).second;
    }

    if( xOldAcc.is() && xOldAcc != xAcc )
        InvalidateCursorPosition( xOldAcc );
    if( xAcc.is() )
        InvalidateCursorPosition( xAcc );
}

void SwAccessibleMap::InvalidatePreviewSelection( sal_uInt16 nSelPage )
{
    DBG_TESTSOLARMUTEX();
    assert(GetShell().IsPreview());
    assert(mpPreview != nullptr);

    mpPreview->InvalidateSelection(GetShell().GetLayout()->GetPageByPageNum(nSelPage));

    rtl::Reference < SwAccessibleContext > xOldAcc = mxCursorContext;
    rtl::Reference < SwAccessibleContext > xAcc;

    const SwPageFrame *pSelPage = mpPreview->GetSelPage();
    if (pSelPage)
    {
        SwAccessibleContextMap::iterator aIter = maFrameMap.find(pSelPage);
        if (aIter != maFrameMap.end())
            xAcc = (*aIter).second;
    }

    if( xOldAcc.is() && xOldAcc != xAcc )
        InvalidateCursorPosition( xOldAcc );
    if( xAcc.is() )
        InvalidateCursorPosition( xAcc );
}

bool SwAccessibleMap::IsPageSelected( const SwPageFrame *pPageFrame ) const
{
    return mpPreview && mpPreview->GetSelPage() == pPageFrame;
}

void SwAccessibleMap::FireEvents()
{
    DBG_TESTSOLARMUTEX();
    {
        osl::MutexGuard aGuard( maEventMutex );
        if( mpEvents )
        {
            if (mpEvents->IsFiring())
            {
                return; // prevent recursive FireEvents()
            }

            mpEvents->SetFiring();
            mpEvents->MoveMissingXAccToEnd();
            for( auto const& aEvent : *mpEvents )
                 FireEvent(aEvent);

            mpEventMap.reset();
            mpEvents.reset();
        }
    }
    mvShapes.clear();
}

tools::Rectangle SwAccessibleMap::GetVisibleArea() const
{
    return o3tl::convert( GetVisArea().SVRect(), o3tl::Length::twip, o3tl::Length::mm100 );
}

// Convert a MM100 value relative to the document root into a pixel value
// relative to the screen!
Point SwAccessibleMap::LogicToPixel( const Point& rPoint ) const
{
    Point aPoint = o3tl::toTwips( rPoint, o3tl::Length::mm100 );
    if (const vcl::Window* pWin = GetShell().GetWin())
    {
        const MapMode aMapMode = GetMapMode(aPoint);
        aPoint = pWin->LogicToPixel( aPoint, aMapMode );
        aPoint = Point(pWin->OutputToAbsoluteScreenPixel( aPoint ));
    }

    return aPoint;
}

Size SwAccessibleMap::LogicToPixel( const Size& rSize ) const
{
    Size aSize( o3tl::toTwips( rSize, o3tl::Length::mm100 ) );
    if (const OutputDevice* pWin = GetShell().GetWin()->GetOutDev())
    {
        const MapMode aMapMode = GetMapMode(Point(0, 0));
        aSize = pWin->LogicToPixel( aSize, aMapMode );
    }

    return aSize;
}

bool SwAccessibleMap::ReplaceChild (
        ::accessibility::AccessibleShape* pCurrentChild,
        const uno::Reference< drawing::XShape >& _rxShape,
        const tools::Long /*_nIndex*/,
        const ::accessibility::AccessibleShapeTreeInfo& /*_rShapeTreeInfo*/
    )
{
    DBG_TESTSOLARMUTEX();

    const SdrObject *pObj = nullptr;
    if( mpShapeMap )
    {
        SwAccessibleShapeMap_Impl::const_iterator aIter = mpShapeMap->begin();
        SwAccessibleShapeMap_Impl::const_iterator aEndIter = mpShapeMap->end();
        while( aIter != aEndIter && !pObj )
        {
            rtl::Reference < ::accessibility::AccessibleShape > xAcc( (*aIter).second );
            if( xAcc.get() == pCurrentChild )
            {
                pObj = (*aIter).first;
            }
            ++aIter;
        }
    }
    if( !pObj )
        return false;

    uno::Reference < drawing::XShape > xShape( _rxShape );  // keep reference to shape, because
                                                            // we might be the only one that
                                                            // holds it.
    // Also get keep parent.
    uno::Reference < XAccessible > xParent( pCurrentChild->getAccessibleParent() );
    pCurrentChild = nullptr;  // will be released by dispose
    A11yDispose( nullptr, pObj, nullptr );

    if( !mpShapeMap )
        mpShapeMap.reset(new SwAccessibleShapeMap_Impl( this ));

    // create the new child
    ::accessibility::ShapeTypeHandler& rShapeTypeHandler =
                    ::accessibility::ShapeTypeHandler::Instance();
    ::accessibility::AccessibleShapeInfo aShapeInfo(
                                        xShape, xParent, this );
    rtl::Reference< ::accessibility::AccessibleShape> pReplacement(
        rShapeTypeHandler.CreateAccessibleObject (
            aShapeInfo, mpShapeMap->GetInfo() ));

    rtl::Reference < ::accessibility::AccessibleShape > xAcc( pReplacement );
    if( xAcc.is() )
    {
        pReplacement->Init();

        SwAccessibleShapeMap_Impl::iterator aIter = mpShapeMap->find( pObj );
        if( aIter != mpShapeMap->end() )
        {
            (*aIter).second = xAcc.get();
        }
        else
        {
            mpShapeMap->emplace( pObj, xAcc );
        }
    }

    SwRect aEmptyRect;
    InvalidatePosOrSize( nullptr, pObj, nullptr, aEmptyRect );

    return true;
}

//Get the accessible control shape from the model object, here model object is with XPropertySet type
::accessibility::AccessibleControlShape * SwAccessibleMap::GetAccControlShapeFromModel(css::beans::XPropertySet* pSet)
{
    if( mpShapeMap )
    {
        for (const auto& rIter : *mpShapeMap)
        {
            rtl::Reference<::accessibility::AccessibleShape> xAcc(rIter.second);
            if(xAcc && ::accessibility::ShapeTypeHandler::Instance().GetTypeId (xAcc->GetXShape()) == ::accessibility::DRAWING_CONTROL)
            {
                ::accessibility::AccessibleControlShape *pCtlAccShape = static_cast < ::accessibility::AccessibleControlShape* >(xAcc.get());
                if (pCtlAccShape->GetControlModel() == pSet)
                    return pCtlAccShape;
            }
        }
    }
    return nullptr;
}

XAccessible*
    SwAccessibleMap::GetAccessibleCaption (const css::uno::Reference< css::drawing::XShape >&)
{
    return nullptr;
}

Point SwAccessibleMap::PixelToCore( const Point& rPoint ) const
{
    Point aPoint;
    if (const OutputDevice* pWin = GetShell().GetWin()->GetOutDev())
    {
        const MapMode aMapMode = GetMapMode(rPoint);
        aPoint = pWin->PixelToLogic( rPoint, aMapMode );
    }
    return aPoint;
}

static tools::Long lcl_CorrectCoarseValue(tools::Long aCoarseValue, tools::Long aFineValue,
                                          tools::Long aRefValue, bool bToLower)
{
    tools::Long aResult = aCoarseValue;

    if (bToLower)
    {
        if (aFineValue < aRefValue)
            aResult -= 1;
    }
    else
    {
        if (aFineValue > aRefValue)
            aResult += 1;
    }

    return aResult;
}

static void lcl_CorrectRectangle(tools::Rectangle & rRect,
                                        const tools::Rectangle & rSource,
                                        const tools::Rectangle & rInGrid)
{
    rRect.SetLeft( lcl_CorrectCoarseValue(rRect.Left(), rSource.Left(),
                                          rInGrid.Left(), false) );
    rRect.SetTop( lcl_CorrectCoarseValue(rRect.Top(), rSource.Top(),
                                         rInGrid.Top(), false) );
    rRect.SetRight( lcl_CorrectCoarseValue(rRect.Right(), rSource.Right(),
                                           rInGrid.Right(), true) );
    rRect.SetBottom( lcl_CorrectCoarseValue(rRect.Bottom(), rSource.Bottom(),
                                            rInGrid.Bottom(), true) );
}

tools::Rectangle SwAccessibleMap::CoreToPixel( const SwRect& rRect ) const
{
    tools::Rectangle aRect;
    if (const OutputDevice* pWin = GetShell().GetWin()->GetOutDev())
    {
        const MapMode aMapMode = GetMapMode(rRect.TopLeft());
        aRect = pWin->LogicToPixel( rRect.SVRect(), aMapMode );

        tools::Rectangle aTmpRect = pWin->PixelToLogic( aRect, aMapMode );
        lcl_CorrectRectangle(aRect, rRect.SVRect(), aTmpRect);
    }

    return aRect;
}

/** get mapping mode for LogicToPixel and PixelToLogic conversions

    Method returns mapping mode of current output device and adjusts it,
    if the shell is in page/print preview.
    Necessary, because <PreviewAdjust(..)> changes mapping mode at current
    output device for mapping logic document positions to page preview window
    positions and vice versa and doesn't take care to recover its changes.
*/
MapMode SwAccessibleMap::GetMapMode(const Point& _rPoint) const
{
    MapMode aMapMode = GetShell().GetWin()->GetMapMode();
    if ( GetShell().IsPreview())
    {
        assert(mpPreview != nullptr);
        mpPreview->AdjustMapMode( aMapMode, _rPoint );
    }
    return aMapMode;
}

Size SwAccessibleMap::GetPreviewPageSize(sal_uInt16 const nPreviewPageNum) const
{
    assert(m_rViewShell.IsPreview());
    assert(mpPreview != nullptr);
    return m_rViewShell.PagePreviewLayout()->GetPreviewPageSizeByPageNum(nPreviewPageNum);
}

/** method to build up a new data structure of the accessible paragraphs,
    which have a selection
    Important note: method has to be used inside a mutual exclusive section
*/
std::unique_ptr<SwAccessibleSelectedParas_Impl> SwAccessibleMap::BuildSelectedParas()
{
    // no accessible contexts, no selection
    if (maFrameMap.empty())
        return nullptr;

    // get cursor as an instance of its base class <SwPaM>
    SwPaM* pCursor( nullptr );
    {
        SwCursorShell* pCursorShell = dynamic_cast<SwCursorShell*>(&GetShell());
        if ( pCursorShell )
        {
            SwFEShell* pFEShell = dynamic_cast<SwFEShell*>(pCursorShell);
            if ( !pFEShell ||
                 ( !pFEShell->IsFrameSelected() &&
                   pFEShell->GetSelectedObjCount() == 0 ) )
            {
                // get cursor without updating an existing table cursor.
                pCursor = pCursorShell->GetCursor( false );
            }
        }
    }
    // no cursor, no selection
    if ( !pCursor )
    {
        return nullptr;
    }

    std::unique_ptr<SwAccessibleSelectedParas_Impl> pRetSelectedParas;

    // loop on all cursors
    SwPaM* pRingStart = pCursor;
    do {

        // for a selection the cursor has to have a mark.
        // for safety reasons assure that point and mark are in text nodes
        if ( pCursor->HasMark() &&
             pCursor->GetPoint()->GetNode().IsTextNode() &&
             pCursor->GetMark()->GetNode().IsTextNode() )
        {
            auto [pStartPos, pEndPos] = pCursor->StartEnd(); // SwPosition*
            // loop on all text nodes inside the selection
            SwNodeIndex aIdx( pStartPos->GetNode() );
            for ( ; aIdx.GetIndex() <= pEndPos->GetNodeIndex(); ++aIdx )
            {
                SwTextNode* pTextNode( aIdx.GetNode().GetTextNode() );
                if ( pTextNode )
                {
                    // loop on all text frames registered at the text node.
                    SwIterator<SwTextFrame, SwTextNode, sw::IteratorMode::UnwrapMulti> aIter(*pTextNode);
                    for( SwTextFrame* pTextFrame = aIter.First(); pTextFrame; pTextFrame = aIter.Next() )
                    {
                            unotools::WeakReference < SwAccessibleContext > xWeakAcc;
                            SwAccessibleContextMap::iterator aMapIter = maFrameMap.find(pTextFrame);
                            if (aMapIter != maFrameMap.end())
                            {
                                xWeakAcc = (*aMapIter).second;
                                SwAccessibleParaSelection aDataEntry(
                                    sw::FrameContainsNode(*pTextFrame, pStartPos->GetNodeIndex())
                                        ? pTextFrame->MapModelToViewPos(*pStartPos)
                                        : TextFrameIndex(0),

                                    sw::FrameContainsNode(*pTextFrame, pEndPos->GetNodeIndex())
                                        ? pTextFrame->MapModelToViewPos(*pEndPos)
                                        : TextFrameIndex(COMPLETE_STRING));
                                if ( !pRetSelectedParas )
                                {
                                    pRetSelectedParas.reset(
                                            new SwAccessibleSelectedParas_Impl);
                                }
                                // sw_redlinehide: should be idempotent for multiple nodes in a merged para
                                pRetSelectedParas->emplace( xWeakAcc, aDataEntry );
                            }
                    }
                }
            }
        }

        // prepare next turn: get next cursor in ring
        pCursor = pCursor->GetNext();
    } while ( pCursor != pRingStart );

    return pRetSelectedParas;
}

void SwAccessibleMap::InvalidateTextSelectionOfAllParas()
{
    DBG_TESTSOLARMUTEX();

    // keep previously known selected paragraphs
    std::unique_ptr<SwAccessibleSelectedParas_Impl> pPrevSelectedParas( std::move(mpSelectedParas) );

    // determine currently selected paragraphs
    mpSelectedParas = BuildSelectedParas();

    // compare currently selected paragraphs with the previously selected
    // paragraphs and submit corresponding TEXT_SELECTION_CHANGED events.
    // first, search for new and changed selections.
    // on the run remove selections from previously known ones, if they are
    // also in the current ones.
    if ( mpSelectedParas )
    {
        for (const auto& rIter : *mpSelectedParas)
        {
            bool bSubmitEvent( false );
            if ( !pPrevSelectedParas )
            {
                // new selection
                bSubmitEvent = true;
            }
            else
            {
                SwAccessibleSelectedParas_Impl::iterator aPrevSelected
                    = pPrevSelectedParas->find(rIter.first);
                if ( aPrevSelected != pPrevSelectedParas->end() )
                {
                    // check, if selection has changed
                    if (rIter.second.nStartOfSelection != (*aPrevSelected).second.nStartOfSelection
                        || rIter.second.nEndOfSelection != (*aPrevSelected).second.nEndOfSelection)
                    {
                        // changed selection
                        bSubmitEvent = true;
                    }
                    pPrevSelectedParas->erase( aPrevSelected );
                }
                else
                {
                    // new selection
                    bSubmitEvent = true;
                }
            }

            if ( bSubmitEvent )
            {
                rtl::Reference<SwAccessibleContext> xAcc(rIter.first);
                if ( xAcc.is() && xAcc->GetFrame() )
                {
                    const SwTextFrame* pTextFrame = xAcc->GetFrame()->DynCastTextFrame();
                    OSL_ENSURE( pTextFrame,
                            "<SwAccessibleMap::_SubmitTextSelectionChangedEvents()> - unexpected type of frame" );
                    if ( pTextFrame )
                    {
                        InvalidateParaTextSelection( *pTextFrame );
                    }
                }
            }
        }
    }

    // second, handle previous selections - after the first step the data
    // structure of the previously known only contains the 'old' selections
    if ( !pPrevSelectedParas )
        return;

    for (const auto& rIter : *pPrevSelectedParas)
    {
        rtl::Reference<SwAccessibleContext> xAcc(rIter.first);
        if ( xAcc.is() && xAcc->GetFrame() )
        {
            const SwTextFrame* pTextFrame = xAcc->GetFrame()->DynCastTextFrame();
            OSL_ENSURE( pTextFrame,
                    "<SwAccessibleMap::_SubmitTextSelectionChangedEvents()> - unexpected type of frame" );
            if ( pTextFrame )
            {
                InvalidateParaTextSelection( *pTextFrame );
            }
        }
    }
}

const SwRect& SwAccessibleMap::GetVisArea() const
{
    assert(!GetShell().IsPreview() || (mpPreview != nullptr));

    return GetShell().IsPreview()
           ? mpPreview->GetVisArea()
           : GetShell().VisArea();
}

bool SwAccessibleMap::IsDocumentSelAll()
{
    return GetShell().GetDoc()->IsPrepareSelAll();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
