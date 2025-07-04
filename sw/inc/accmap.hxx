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
#pragma once

#include <cppuhelper/weakref.hxx>
#include <rtl/ref.hxx>
#include <osl/mutex.hxx>
#include <svx/IAccessibleViewForwarder.hxx>
#include <svx/IAccessibleParent.hxx>

#include <svx/AccessibleControlShape.hxx>
#include <o3tl/typed_flags_set.hxx>
#include <unotools/weakref.hxx>
#include <vector>
#include <memory>
#include <o3tl/sorted_vector.hxx>

class SwAccessibleParagraph;
class SwViewShell;
class SwFrame;
class SwTextFrame;
class SwPageFrame;
class SwAccessibleContext;
class SwAccessibleEventList_Impl;
class SwAccessibleEventMap_Impl;
class SdrObject;
namespace accessibility { class AccessibleShape; }
class SwAccessibleShapeMap_Impl;
struct SwAccessibleEvent_Impl;
class SwAccessibleSelectedParas_Impl;
class SwRect;
class MapMode;
class SwAccPreviewData;
class SwFEShell;
class Fraction;
struct PreviewPage;
namespace vcl { class Window; }
namespace com::sun::star::accessibility { class XAccessible; }

enum class AccessibleStates
{
    NONE                   = 0x0000,
    // real states for events
    EDITABLE               = 0x0001,
    OPAQUE                 = 0x0002,
    // pseudo states for events
    TEXT_ATTRIBUTE_CHANGED = 0x0200,
    TEXT_SELECTION_CHANGED = 0x0100,
    CARET                  = 0x0080,
    RELATION_FROM          = 0x0040,
    RELATION_TO            = 0x0020,
};
namespace o3tl
{
    template<> struct typed_flags<AccessibleStates> : is_typed_flags<AccessibleStates, 0x3e3> {};
}

using SwAccessibleContextMap
    = std::unordered_map<const SwFrame*, unotools::WeakReference<SwAccessibleContext>>;

class SwAccessibleMap final : public ::accessibility::IAccessibleViewForwarder,
                        public ::accessibility::IAccessibleParent
                , public std::enable_shared_from_this<SwAccessibleMap>
{
    ::osl::Mutex maEventMutex;
    SwAccessibleContextMap maFrameMap;
    std::unique_ptr<SwAccessibleShapeMap_Impl> mpShapeMap;

    // The shape list is filled if an accessible shape is destroyed. It
    // simply keeps a reference to the accessible shape's XShape. These
    // references are destroyed within the EndAction when firing events.
    // There are two reason for this. First of all, a new accessible shape
    // for the XShape might be created soon. It's then cheaper if the XShape
    // still exists. The other reason are situations where an accessible shape
    // is destroyed within an SwFrameFormat::SwClientNotify. In this case, destroying
    // the XShape at the same time (indirectly by destroying the accessible
    // shape) leads to an assert, because a client of the Modify is destroyed
    // within a Modify call.
    std::vector<css::uno::Reference<css::drawing::XShape>> mvShapes;

    std::unique_ptr<SwAccessibleEventList_Impl> mpEvents;
    std::unique_ptr<SwAccessibleEventMap_Impl> mpEventMap;

    // Para Container for InvalidateCursorPosition
    o3tl::sorted_vector<SwAccessibleParagraph*> m_setParaAdd;
    o3tl::sorted_vector<SwAccessibleParagraph*> m_setParaRemove;

    // #i27301 data structure to keep information about
    // accessible paragraph, which have a selection.
    std::unique_ptr<SwAccessibleSelectedParas_Impl> mpSelectedParas;
    SwViewShell& m_rViewShell;
    /// for page preview: store preview data, VisArea, and mapping of
    /// preview-to-display coordinates
    std::unique_ptr<SwAccPreviewData> mpPreview;

    unotools::WeakReference< SwAccessibleContext > mxCursorContext;

    bool mbShapeSelected;

    void FireEvent( const SwAccessibleEvent_Impl& rEvent );

    void AppendEvent( const SwAccessibleEvent_Impl& rEvent );

    void InvalidateCursorPosition(const rtl::Reference<SwAccessibleContext>& rxAcc);
    void DoInvalidateShapeSelection(bool bInvalidateFocusMode = false);

    void InvalidateShapeSelection();

    //maSelectedFrameMap contains the old selected objects.
    SwAccessibleContextMap maSelectedFrameMap;

    OUString maDocName;

    //InvalidateShapeInParaSelection() method is responsible for the updating the selected states of the objects.
    void InvalidateShapeInParaSelection();

    void InvalidateRelationSet_(const SwFrame& rFrame, bool bFrom);

    rtl::Reference<SwAccessibleContext> GetDocumentView_(bool bPagePreview);

    /** method to build up a new data structure of the accessible paragraphs,
        which have a selection

        Important note: method has to used inside a mutual exclusive section
    */
    std::unique_ptr<SwAccessibleSelectedParas_Impl> BuildSelectedParas();

public:

    SwAccessibleMap(SwViewShell& rViewShell);
    virtual ~SwAccessibleMap() override;

    rtl::Reference<comphelper::OAccessible> GetDocumentView();

    rtl::Reference<comphelper::OAccessible>
    GetDocumentPreview(const std::vector<std::unique_ptr<PreviewPage>>& _rPreviewPages,
                       const Fraction& _rScale, const SwPageFrame* _pSelectedPageFrame,
                       const Size& _rPreviewWinSize);

    ::rtl::Reference < SwAccessibleContext > GetContextImpl(
                                                 const SwFrame *pFrame,
                                                bool bCreate = true );
    css::uno::Reference<css::accessibility::XAccessible> GetContext(
                                                 const SwFrame *pFrame,
                                                bool bCreate = true );

    ::rtl::Reference < ::accessibility::AccessibleShape > GetContextImpl(
                                        const SdrObject *pObj,
                                        SwAccessibleContext *pParentImpl,
                                        bool bCreate = true );
    css::uno::Reference<css::accessibility::XAccessible> GetContext(
                                        const SdrObject *pObj,
                                        SwAccessibleContext *pParentImpl,
                                        bool bCreate = true );

    SwViewShell& GetShell() const
    {
        return m_rViewShell;
    }
    static bool IsInSameLevel(const SdrObject* pObj, const SwFEShell* pFESh);
    void AddShapeContext(const SdrObject *pObj,
                             rtl::Reference < ::accessibility::AccessibleShape > const & xAccShape);

    void AddGroupContext(const SdrObject *pParentObj,
                    css::uno::Reference < css::accessibility::XAccessible > const & xAccParent);
    void RemoveGroupContext(const SdrObject *pParentObj);

    const SwRect& GetVisArea() const;

    /** get size of a dedicated preview page

        @param _nPreviewPageNum
        input parameter - physical page number of page visible in the page preview

        @return an object of class <Size>
    */
    Size GetPreviewPageSize( sal_uInt16 _nPreviewPageNum ) const;

    void RemoveContext( const SwFrame *pFrame );
    void RemoveContext( const SdrObject *pObj );

    // Dispose frame and its children if bRecursive is set
    void A11yDispose( const SwFrame* pFrame,
                      const SdrObject* pObj,
                      vcl::Window* pWindow,
                      bool bRecursive = false,
                      bool bCanSkipInvisible = true );

    void InvalidatePosOrSize( const SwFrame* pFrame,
                              const SdrObject* pObj,
                              vcl::Window* pWindow,
                              const SwRect& rOldFrame );

    void InvalidateContent( const SwFrame *pFrame );

    void InvalidateAttr( const SwTextFrame& rTextFrame );

    void InvalidateCursorPosition( const SwFrame *pFrame );
    void InvalidateFocus();
    void SetCursorContext(
        const ::rtl::Reference < SwAccessibleContext >& rCursorContext );

    // Invalidate state of whole tree. If an action is open, this call
    // is processed when the last action ends.
    void InvalidateEditableStates( const SwFrame* _pFrame );

    void InvalidateRelationSet(const SwFrame& rMaster, const SwFrame& rFollow);

    /** invalidation CONTENT_FLOWS_FROM/_TO relation of a paragraph

        @param _rTextFrame
        input parameter - reference to paragraph, whose CONTENT_FLOWS_FROM/_TO
        has to be invalidated.

        @param _bFrom
        input parameter - boolean indicating, if relation CONTENT_FLOWS_FROM
        (value <true>) or CONTENT_FLOWS_TO (value <false>) has to be invalidated.
    */
    void InvalidateParaFlowRelation( const SwTextFrame& _rTextFrame,
                                     const bool _bFrom );

    /** invalidation of text selection of a paragraph */
    void InvalidateParaTextSelection( const SwTextFrame& _rTextFrame );

    /** invalidation of text selection of all paragraphs */
    void InvalidateTextSelectionOfAllParas();

    sal_Int32 GetChildIndex( const SwFrame& rParentFrame,
                             vcl::Window& rChild ) const;

    // update preview data (and fire events if necessary)
    void UpdatePreview( const std::vector<std::unique_ptr<PreviewPage>>& _rPreviewPages,
                        const Fraction&  _rScale,
                        const SwPageFrame* _pSelectedPageFrame,
                        const Size&      _rPreviewWinSize );

    void InvalidatePreviewSelection( sal_uInt16 nSelPage );
    bool IsPageSelected( const SwPageFrame *pPageFrame ) const;

    void FireEvents();

    const OUString& GetDocName() const { return maDocName; }

    // IAccessibleViewForwarder

    virtual tools::Rectangle GetVisibleArea() const override;
    virtual Point LogicToPixel (const Point& rPoint) const override;
    virtual Size LogicToPixel (const Size& rSize) const override;

    // IAccessibleParent
    virtual bool ReplaceChild (
        ::accessibility::AccessibleShape* pCurrentChild,
        const css::uno::Reference< css::drawing::XShape >& _rxShape,
        const tools::Long _nIndex,
        const ::accessibility::AccessibleShapeTreeInfo& _rShapeTreeInfo
    ) override;
    virtual ::accessibility::AccessibleControlShape* GetAccControlShapeFromModel
        (css::beans::XPropertySet* pSet) override;
    virtual css::accessibility::XAccessible*   GetAccessibleCaption (
        const css::uno::Reference< css::drawing::XShape > & xShape) override;

    // additional Core/Pixel conversions for internal use; also works
    // for preview
    Point PixelToCore (const Point& rPoint) const;
    tools::Rectangle CoreToPixel (const SwRect& rRect) const;

    // is there a known accessibility impl cached for the frame
    bool Contains(const SwFrame *pFrame) const;

private:
    /** get mapping mode for LogicToPixel and PixelToLogic conversions

        Replacement method <PreviewAdjust(..)> by new method <GetMapMode>.
        Method returns mapping mode of current output device and adjusts it,
        if the shell is in page/print preview.
        Necessary, because <PreviewAdjust(..)> changes mapping mode at current
        output device for mapping logic document positions to page preview window
        positions and vice versa and doesn't take care to recover its changes.

        @param _rPoint
        input parameter - constant reference to point to determine the mapping
        mode adjustments for page/print preview.

        @return mapping mode, which is determined by the method
    */
    MapMode GetMapMode(const Point& _rPoint) const;
public:
    virtual bool IsDocumentSelAll() override;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
