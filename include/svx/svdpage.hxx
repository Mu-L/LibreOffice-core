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

#include <cppuhelper/weak.hxx>
#include <svl/stylesheetuser.hxx>
#include <vcl/prntypes.hxx>
#include <svl/itemset.hxx>
#include <svx/sdrpageuser.hxx>
#include <svx/sdr/contact/viewobjectcontactredirector.hxx>
#include <svx/sdrmasterpagedescriptor.hxx>
#include <svx/svxdllapi.h>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/drawing/XDrawPage.hpp>
#include <svx/svdobj.hxx>
#include <unotools/weakref.hxx>
#include <memory>
#include <optional>
#include <vector>
#include <deque>
#include <tools/UniqueID.hxx>

// predefines
namespace model { class Theme; }
namespace reportdesign { class OSection; }
namespace sdr::contact { class ViewContact; }
namespace sdr::annotation { class Annotation; }
class SdrPage;
class SdrModel;
class SdrPageView;
class SdrLayerAdmin;
class SdrLayerIDSet;
class Color;
class SfxStyleSheet;

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  SdrObjList
//      E3dObjList
//      SdrPage -> see continuation there

// class SdrObjList
class SVXCORE_DLLPUBLIC SdrObjList
{
friend class SdrObjListIter;
friend class SdrEditView;

private:
    SdrObjList(const SdrObjList& rSrcList) = delete;
    SdrObjList &operator=(const SdrObjList& rSrcList) = delete;

protected:
    SAL_DLLPRIVATE void RecalcRects();

private:
    /// simple ActionChildInserted forwarder to have it on a central place
    SAL_DLLPRIVATE static void impChildInserted(SdrObject const & rChild);

    // tdf#116879 Clear SdrObjList, no Undo done. Used from destructor, but also
    // from other places.
    SAL_DLLPRIVATE void impClearSdrObjList();

protected:
    // protected constructor to make clear that this class should only
    // be used as base for derivations, not naked. See getSdrModelFromSdrObjList
    // which is pure virtual to force this, too
    SAL_DLLPRIVATE SdrObjList();
    SAL_DLLPRIVATE virtual ~SdrObjList();

public:
    // SdrModel/SdrPage access on SdrObjList level
    virtual SdrPage* getSdrPageFromSdrObjList() const;
    virtual SdrObject* getSdrObjectFromSdrObjList() const;

    SAL_DLLPRIVATE void CopyObjects(const SdrObjList& rSrcList);
    static OString GetObjectRectangles(const SdrObjList& rSrcList);

    // tdf#116879 clean up everything (without Undo), plus broadcasting
    // changes. Split to this call and a private one (impClearSdrObjList)
    // that allows cleanup without broadcasting in the destructor
    void    ClearSdrObjList();

    /// recalculate order numbers / ZIndex
    void           RecalcObjOrdNums();
    bool           IsObjOrdNumsDirty() const        { return mbObjOrdNumsDirty; }
    virtual void   NbcInsertObject(SdrObject* pObj, size_t nPos=SAL_MAX_SIZE);
    virtual void   InsertObject(SdrObject* pObj, size_t nPos=SAL_MAX_SIZE);
    SAL_DLLPRIVATE void           sort( std::vector<sal_Int32>& sortOrder );

    void InsertObjectThenMakeNameUnique(SdrObject* pObj);
    void InsertObjectThenMakeNameUnique(SdrObject* pObj, std::unordered_set<rtl::OUString>& rNameSet, size_t nPos=SAL_MAX_SIZE);

    /// remove from list without delete
    virtual rtl::Reference<SdrObject> NbcRemoveObject(size_t nObjNum);
    virtual rtl::Reference<SdrObject> RemoveObject(size_t nObjNum);

    /// Replace existing object by different one.
    /// Same as Remove(old)+Insert(new) but faster because the order numbers
    /// do not have to be set dirty.
    virtual rtl::Reference<SdrObject> ReplaceObject(SdrObject* pNewObj, size_t nObjNum);

    /// Modify ZOrder of an SdrObject
    virtual SdrObject* SetObjectOrdNum(size_t nOldObjNum, size_t nNewObjNum);

    /// Modify ZOrder of an SdrObject, object must already be in the list
    SAL_DLLPRIVATE void SetExistingObjectOrdNum(SdrObject* pExistingObj, size_t nNewObjNum);

    void SetSdrObjListRectsDirty();

    const tools::Rectangle& GetAllObjSnapRect() const;
    const tools::Rectangle& GetAllObjBoundRect() const;

    /// reformat all text objects, e.g. when changing printer
    SAL_DLLPRIVATE void NbcReformatAllTextObjects();
    SAL_DLLPRIVATE void ReformatAllTextObjects();

    /** #103122# reformats all edge objects that are connected to other objects */
    SAL_DLLPRIVATE void ReformatAllEdgeObjects();

    /// convert attributes of the style to hard formatting
    SAL_DLLPRIVATE void BurnInStyleSheetAttributes();

    size_t GetObjCount() const;
    SdrObject* GetObj(size_t nNum) const;
    SdrObject* GetObjByName(std::u16string_view sName) const;

    /// linked page or linked group object
    virtual bool IsReadOnly() const;

    /** Makes the object list flat, i.e. the object list content are
        then tree leaves

        This method travels recursively over all group objects in this
        list, extracts the content, inserts it flat to the list and
        removes the group object afterwards.
     */
    SAL_DLLPRIVATE void FlattenGroups();

    /** Ungroup the object at the given index

        This method ungroups the content of the group object at the
        given index, i.e. the content is put flat into the object list
        (if the object at the given index is no group, this method is
        a no-op). If the group itself contains group objects, the
        operation is performed recursively, such that the content of
        the given object contains no groups afterwards.
     */
    SAL_DLLPRIVATE void UnGroupObj( size_t nObjNum );

    /** Return whether there is an explicit, user defined, object navigation
        order.  When there is one this method returns <TRUE/> and the
        GetObjectForNavigationPosition() and
        SdrObject::GetNavigationPosition() methods will return values
        different from those returned by SdrObject::GetOrdNum() and
        GetObj().
    */
    bool HasObjectNavigationOrder() const;

    /** Set the navigation position of the given object to the specified
        value.  Note that this changes the navigation position for all
        objects on or following the old or new position.
    */
    void SetObjectNavigationPosition (
        SdrObject& rObject,
        const sal_uInt32 nNewNavigationPosition);

    /** Return the object for the given navigation position.  When there is
        a user defined navigation order, i.e. mxNavigationOrder is not NULL,
        then that is used to look up the object.  Otherwise the z-order is
        used by looking up the object in maList.
        @param nNavigationPosition
            Valid values include 0 and are smaller than the number of
            objects as returned by GetObjCount().
        @return
            The returned pointer is NULL for invalid positions.
    */
    SAL_DLLPRIVATE SdrObject* GetObjectForNavigationPosition (const sal_uInt32 nNavigationPosition) const;

    /** Restore the navigation order to that defined by the z-order.
    */
    void ClearObjectNavigationOrder();

    /** Set the navigation position of all SdrObjects to their position in
        the mxNavigationOrder list.  This method returns immediately when no
        update is necessary.
        @return
            This method returns <TRUE/> when the navigation positions stored
            in SdrObjects are up to date.
            It returns <FALSE/> when the navigation positions are not valid,
            for example because no explicit navigation order has been
            defined, i.e. HasObjectNavigationOrder() would return <FALSE/>.
    */
    SAL_DLLPRIVATE bool RecalcNavigationPositions();

    /** Set the navigation order to the one defined by the given list of
        XShape objects.
        @param rxOrder
            When this is an empty reference then the navigation order is
            reset to the z-order. The preferred way to do this, however, is
            to call ClearObjectNavigationOrder().
            Otherwise this list is expected to contain all the shapes in the
            called SdrObjList.
    */
    void SetNavigationOrder (const css::uno::Reference<
                             css::container::XIndexAccess>& rxOrder);

    SAL_DLLPRIVATE virtual void dumpAsXml(xmlTextWriterPtr pWriter) const;

    typedef std::deque<rtl::Reference<SdrObject>> SdrObjectDeque;

    SdrObjectDeque::const_iterator begin() const { return maList.begin(); }
    SdrObjectDeque::const_iterator end() const { return maList.end(); }
    SdrObjectDeque::const_reverse_iterator rbegin() const { return maList.rbegin(); }
    SdrObjectDeque::const_reverse_iterator rend() const { return maList.rend(); }

private:
    tools::Rectangle    maSdrObjListOutRect;
    tools::Rectangle    maSdrObjListSnapRect;
    SdrObjectDeque maList;
    /// This list, if it exists, defines the navigation order. If it does
    /// not exist then maList defines the navigation order.
    std::optional<std::vector<unotools::WeakReference<SdrObject>>> mxNavigationOrder;
    bool                mbObjOrdNumsDirty;
    bool                mbRectsDirty;
    /// This flag is <TRUE/> when the mpNavigation list has been changed but
    /// the indices of the referenced SdrObjects still have their old values.
    bool mbIsNavigationOrderDirty;

    /** Insert an SdrObject into maList.  Do not modify the maList member
        directly.
        @param rObject
            The object to insert into the object list.
        @param nInsertPosition
            The given object is inserted before the object at this
            position.  Valid values include 0 (the object is inserted at the
            head of the list) and the number of objects in the list as
            returned by GetObjCount() (the object is inserted at the end of
            the list.)
    */
    SAL_DLLPRIVATE void InsertObjectIntoContainer (
        SdrObject& rObject,
        const sal_uInt32 nInsertPosition);

    /** Replace an object in the object list.
        @param rObject
            The new object that replaces the one in the list at the
            specified position.
        @param nObjectPosition
            The object at this position in the object list is replaced by
            the given object.  Valid values include 0 and are smaller than
            the number of objects in the list.
    */
    SAL_DLLPRIVATE void ReplaceObjectInContainer (
        SdrObject& rObject,
        const sal_uInt32 nObjectPosition);

    /** Remove an object from the object list.
        The object list has to contain at least one element.
        @param nObjectPosition
            The object at this position is removed from the object list.
            Valid values include 0 and are smaller than the number of
            objects in the list.
    */
    SAL_DLLPRIVATE void RemoveObjectFromContainer (
        const sal_uInt32 nObjectPosition);

    SAL_DLLPRIVATE void ImplReformatAllEdgeObjects(const SdrObjList& );
};

// Used for all methods which return a page number
#define SDRPAGE_NOTFOUND 0xFFFF

/// for the snap-to-grid in Writer
class SdrPageGridFrame
{
    tools::Rectangle m_aPaper;
    tools::Rectangle m_aUserArea;
public:
    SdrPageGridFrame(const tools::Rectangle& rPaper, const tools::Rectangle& rUser): m_aPaper(rPaper), m_aUserArea(rUser) {}
    const tools::Rectangle& GetPaperRect() const                  { return m_aPaper; }
    const tools::Rectangle& GetUserArea() const                   { return m_aUserArea; }
};

class SVXCORE_DLLPUBLIC SdrPageGridFrameList final
{
    std::vector<SdrPageGridFrame*> m_aList;

    SdrPageGridFrameList(const SdrPageGridFrameList& rSrcList) = delete;
    void           operator=(const SdrPageGridFrameList& rSrcList) = delete;
    SdrPageGridFrame* GetObject(sal_uInt16 i) const { return m_aList[i]; }

public:
    SdrPageGridFrameList()                                    {}
    ~SdrPageGridFrameList()                                            { Clear(); }
    void           Clear();
    sal_uInt16     GetCount() const                                    { return sal_uInt16(m_aList.size()); }
    void           Insert(const SdrPageGridFrame& rGF) { m_aList.push_back(new SdrPageGridFrame(rGF)); }
    SdrPageGridFrame&       operator[](sal_uInt16 nPos)                    { return *GetObject(nPos); }
    const SdrPageGridFrame& operator[](sal_uInt16 nPos) const              { return *GetObject(nPos); }
};

// class SdrPageProperties
class SVXCORE_DLLPUBLIC SdrPageProperties final : public SfxListener, public svl::StyleSheetUser
{
private:
    // data
    SdrPage& mrSdrPage;
    SfxStyleSheet* mpStyleSheet;
    std::shared_ptr<model::Theme> mpTheme;
    SfxItemSet maProperties;

    // internal helpers
    void ImpRemoveStyleSheet();
    void ImpAddStyleSheet(SfxStyleSheet& rNewStyleSheet);

    SdrPageProperties& operator=(const SdrPageProperties& rCandidate) = delete;

    void sendLOKitThemeChangedCallback();
public:
    // construct/destruct
    SdrPageProperties(SdrPage& rSdrPage);
    virtual ~SdrPageProperties() override;

    // Notify(...) from baseclass SfxListener
    virtual void Notify(SfxBroadcaster& rBC, const SfxHint& rHint) override;

    virtual bool isUsedByModel() const override;

    // data read/write
    const SfxItemSet& GetItemSet() const { return maProperties;}
    void PutItemSet(const SfxItemSet& rSet);
    void PutItem(const SfxPoolItem& rItem);
    void ClearItem(const sal_uInt16 nWhich = 0);

    // StyleSheet access
    void SetStyleSheet(SfxStyleSheet* pStyleSheet);
    SfxStyleSheet* GetStyleSheet() const { return mpStyleSheet;}

    void setTheme(std::shared_ptr<model::Theme> const& pTheme);
    std::shared_ptr<model::Theme> const& getTheme() const;

    void dumpAsXml(xmlTextWriterPtr pWriter) const;
};


/**
  A SdrPage contains exactly one SdrObjList and a description of the physical
  page dimensions (size / margins).  The latter is required to "catch" objects
  during drag-and-drop.
  The SdrPage allows (via SdrObjList) inserting and removing SdrObjects,
  as well as moving them into the foreground or background.
  Also it's possible to request and directly set the order number (ZOrder)
  of SdrObjects.
*/

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  SdrPage
//      DlgEdPage
//      FmFormPage
//          ScDrawPage
//          SdPage
//          SwDPage
//      OReportPage

class SVXCORE_DLLPUBLIC SdrPage : public SdrObjList, public cppu::OWeakObject
{
    // #i9076#
    friend class SdrModel;
    friend class SvxUnoDrawPagesAccess;

    // this class uses its own UNO wrapper
    // and thus has to set mxUnoPage (it also relies on mxUnoPage not being WeakRef)
    friend class reportdesign::OSection;

    SdrPage& operator=(const SdrPage&) = delete;
    SdrPage(const SdrPage&) = delete;

    // start PageUser section
private:
    // #111111# PageUser section
    sdr::PageUserVector                                             maPageUsers;

    std::unique_ptr<sdr::contact::ViewContact> mpViewContact;

public:
    void AddPageUser(sdr::PageUser& rNewUser);
    void RemovePageUser(sdr::PageUser& rOldUser);
    const sdr::PageUserVector& GetPageUsers() const { return maPageUsers; };

    // SdrModel access on SdrPage level
    SdrModel& getSdrModelFromSdrPage() const { return mrSdrModelFromSdrPage; }

protected:
    std::unique_ptr<sdr::contact::ViewContact> CreateObjectSpecificViewContact();
public:
    const sdr::contact::ViewContact& GetViewContact() const;
    sdr::contact::ViewContact& GetViewContact();

    // #110094# DrawContact support: Methods for handling Page changes
    void ActionChanged();

private:
    // the SdrModel this page was created with, unchanged during SdrPage lifetime
    SdrModel&                   mrSdrModelFromSdrPage;

protected:
    std::vector<rtl::Reference<sdr::annotation::Annotation>> maAnnotations;

private:
    tools::Long mnWidth;       // page size
    tools::Long mnHeight;      // page size
    sal_Int32 mnBorderLeft;  // left page margin
    sal_Int32 mnBorderUpper; // top page margin
    sal_Int32 mnBorderRight; // right page margin
    sal_Int32 mnBorderLower; // bottom page margin
    UniqueID maUniqueID;
    bool mbBackgroundFullSize = false; ///< Background object to represent the whole page.

    std::unique_ptr<SdrLayerAdmin> mpLayerAdmin;
    std::unique_ptr<SdrPageProperties> mpSdrPageProperties;
    css::uno::Reference< css::uno::XInterface > mxUnoPage;

public:
    SdrPageProperties& getSdrPageProperties();
    const SdrPageProperties& getSdrPageProperties() const;
    const SdrPageProperties* getCorrectSdrPageProperties() const;

protected:
    // new MasterPageDescriptorVector
    std::unique_ptr<sdr::MasterPageDescriptor> mpMasterPageDescriptor;

    sal_uInt16          m_nPageNum;

    bool                mbMaster : 1;               // flag if this is a MasterPage
    bool                mbInserted : 1;
    bool                mbObjectsNotPersistent : 1;

    // #i93597#
    bool                mbPageBorderOnlyLeftRight : 1;

    void SetUnoPage(css::uno::Reference<css::drawing::XDrawPage> const&);
    virtual css::uno::Reference< css::uno::XInterface > createUnoPage();

    // Copying of pages is split into two parts: construction and copying of page objects,
    // because the copying might need access to fully initialized page. CloneSdrPage() is responsible
    // to call lateInit() after copy-construction of a new object. Any initialization in derived
    // classes that needs access to the page objects must be deferred to lateInit. And it must
    // call lateInit() of its parent class.
    void lateInit(const SdrPage& rSrcPage);

public:
    explicit SdrPage(SdrModel& rModel, bool bMasterPage=false);
    virtual ~SdrPage() override;

    virtual rtl::Reference<SdrPage> CloneSdrPage(SdrModel& rTargetModel) const;
    bool             IsMasterPage() const       { return mbMaster; }
    void             SetInserted(bool bNew = true);
    bool             IsInserted() const         { return mbInserted; }
    void             SetChanged();

    // derived from SdrObjList, returns this
    virtual SdrPage* getSdrPageFromSdrObjList() const override;

    // #i68775# React on PageNum changes (from Model in most cases)
    void SetPageNum(sal_uInt16 nNew);
    sal_uInt16 GetPageNum() const;

    // #i93597# Allow page border definition to not be the full rectangle but to
    // use only the left and right vertical edges (reportdesigner)
    void setPageBorderOnlyLeftRight(bool bNew) { mbPageBorderOnlyLeftRight = bNew; }
    bool getPageBorderOnlyLeftRight() const { return mbPageBorderOnlyLeftRight; }

    virtual void SetSize(const Size& aSiz);
    Size GetSize() const;
    virtual void SetOrientation(Orientation eOri);
    virtual Orientation GetOrientation() const;
    tools::Long GetWidth() const;
    tools::Long GetHeight() const;
    virtual void  SetBorder(sal_Int32 nLft, sal_Int32 nUpp, sal_Int32 nRgt, sal_Int32 Lwr);
    virtual void  SetLeftBorder(sal_Int32 nBorder);
    virtual void  SetUpperBorder(sal_Int32 nBorder);
    virtual void  SetRightBorder(sal_Int32 nBorder);
    virtual void  SetLowerBorder(sal_Int32 nBorder);
    sal_Int32 GetLeftBorder() const;
    sal_Int32 GetUpperBorder() const;
    sal_Int32 GetRightBorder() const;
    sal_Int32 GetLowerBorder() const;
    sal_uInt64 GetUniqueID() const { return maUniqueID.getID(); }
    void    SetBackgroundFullSize(bool bIn);
    bool    IsBackgroundFullSize() const;

    // New MasterPage interface
    bool TRG_HasMasterPage() const { return (nullptr != mpMasterPageDescriptor); }
    void TRG_SetMasterPage(SdrPage& rNew);
    void TRG_ClearMasterPage();
    SdrPage& TRG_GetMasterPage() const;
    const SdrLayerIDSet& TRG_GetMasterPageVisibleLayers() const;
    void TRG_SetMasterPageVisibleLayers(const SdrLayerIDSet& rNew);
    sdr::contact::ViewContact& TRG_GetMasterPageDescriptorViewContact() const;

    void MakePageObjectsNamesUnique();

protected:
    void TRG_ImpMasterPageRemoved(const SdrPage& rRemovedPage);

public:
    /// changing the layers does not set the modified-flag!
    const SdrLayerAdmin& GetLayerAdmin() const;
    SdrLayerAdmin& GetLayerAdmin();

    /// for snap-to-grid in Writer, also for AlignObjects if 1 object is marked
    /// if pRect != null, then the pages that are intersected by this Rect,
    /// otherwise the visible pages
    virtual const SdrPageGridFrameList* GetGridFrameList(const SdrPageView* pPV, const tools::Rectangle* pRect) const;

    css::uno::Reference< css::uno::XInterface > const & getUnoPage();

    virtual SfxStyleSheet* GetTextStyleSheetForObject( SdrObject* pObj ) const;

    /** *deprecated* returns an averaged background color of this page */
    // #i75566# GetBackgroundColor -> GetPageBackgroundColor
    Color GetPageBackgroundColor() const;

    /** *deprecated* returns an averaged background color of this page */
    // #i75566# GetBackgroundColor -> GetPageBackgroundColor and bScreenDisplay hint value
    Color GetPageBackgroundColor( SdrPageView const * pView, bool bScreenDisplay = true) const;

    /** this method returns true if the object from the ViewObjectContact should
        be visible on this page while rendering.
        bEdit selects if visibility test is for an editing view or a final render,
        like printing.
    */
    virtual bool checkVisibility(
        const sdr::contact::ViewObjectContact& rOriginal,
        const sdr::contact::DisplayInfo& rDisplayInfo,
        bool bEdit );

    void dumpAsXml(xmlTextWriterPtr pWriter) const override;

    // Annotations
    virtual rtl::Reference<sdr::annotation::Annotation> createAnnotation();
    virtual void addAnnotation(rtl::Reference<sdr::annotation::Annotation> const& xAnnotation, int nIndex = -1);
    virtual void addAnnotationNoNotify(rtl::Reference<sdr::annotation::Annotation> const& xAnnotation, int nIndex = -1);
    virtual void removeAnnotation(rtl::Reference<sdr::annotation::Annotation> const& xAnnotation);
    virtual void removeAnnotationNoNotify(rtl::Reference<sdr::annotation::Annotation> const& xAnnotation);

    std::vector<rtl::Reference<sdr::annotation::Annotation>> const& getAnnotations() const;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
