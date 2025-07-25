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

#include <AccessibleDocumentPagePreview.hxx>
#include <AccessiblePreviewTable.hxx>
#include <AccessiblePageHeader.hxx>
#include <AccessibilityHints.hxx>
#include <AccessibleText.hxx>
#include <document.hxx>
#include <prevwsh.hxx>
#include <prevloc.hxx>
#include <drwlayer.hxx>
#include <editsrc.hxx>
#include <scresid.hxx>
#include <strings.hrc>
#include <strings.hxx>
#include <preview.hxx>
#include <postit.hxx>

#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <comphelper/sequence.hxx>

#include <tools/gen.hxx>
#include <svx/fmview.hxx>
#include <svx/svdpage.hxx>
#include <svx/svdobj.hxx>
#include <svx/AccessibleTextHelper.hxx>
#include <svx/AccessibleShape.hxx>
#include <svx/AccessibleShapeInfo.hxx>
#include <svx/IAccessibleParent.hxx>
#include <svx/IAccessibleViewForwarder.hxx>
#include <svx/ShapeTypeHandler.hxx>
#include <toolkit/helper/vclunohelper.hxx>
#include <vcl/svapp.hxx>
#include <vcl/unohelp.hxx>
#include <sfx2/docfile.hxx>

#include <vector>
#include <algorithm>
#include <memory>
#include <utility>

using namespace ::com::sun::star;
using namespace ::com::sun::star::accessibility;

typedef std::vector< uno::Reference< XAccessible > > ScXAccVector;

namespace {

struct ScAccNote
{
    OUString    maNoteText;
    tools::Rectangle   maRect;
    ScAddress   maNoteCell;
    ::accessibility::AccessibleTextHelper* mpTextHelper;
    sal_Int32   mnParaCount;
    bool    mbMarkNote;

    ScAccNote()
        : mpTextHelper(nullptr)
        , mnParaCount(0)
        , mbMarkNote(false)
    {
    }
};

}

class ScNotesChildren
{
public:
    ScNotesChildren(ScPreviewShell* pViewShell, ScAccessibleDocumentPagePreview& rAccDoc);
    ~ScNotesChildren();
    void Init(const tools::Rectangle& rVisRect, sal_Int32 nOffset);

    sal_Int32 GetChildrenCount() const { return mnParagraphs;}
    uno::Reference<XAccessible> GetChild(sal_Int32 nIndex) const;
    uno::Reference<XAccessible> GetAt(const awt::Point& rPoint) const;

    void DataChanged(const tools::Rectangle& rVisRect);

private:
    ScPreviewShell*         mpViewShell;
    ScAccessibleDocumentPagePreview& mrAccDoc;
    typedef std::vector<ScAccNote> ScAccNotes;
    mutable ScAccNotes      maNotes;
    mutable ScAccNotes      maMarks;
    sal_Int32               mnParagraphs;
    sal_Int32               mnOffset;

    ::accessibility::AccessibleTextHelper* CreateTextHelper(const OUString& rString, const tools::Rectangle& rVisRect, const ScAddress& aCellPos, bool bMarkNote, sal_Int32 nChildOffset) const;
    sal_Int32 AddNotes(const ScPreviewLocationData& rData, const tools::Rectangle& rVisRect, bool bMark, ScAccNotes& rNotes);

    static sal_Int8 CompareCell(const ScAddress& aCell1, const ScAddress& aCell2);
    static void CollectChildren(const ScAccNote& rNote, ScXAccVector& rVector);
    sal_Int32 CheckChanges(const ScPreviewLocationData& rData, const tools::Rectangle& rVisRect,
        bool bMark, ScAccNotes& rOldNotes, ScAccNotes& rNewNotes,
        ScXAccVector& rOldParas, ScXAccVector& rNewParas);

    inline ScDocument* GetDocument() const;
};

ScNotesChildren::ScNotesChildren(ScPreviewShell* pViewShell, ScAccessibleDocumentPagePreview& rAccDoc)
    : mpViewShell(pViewShell),
    mrAccDoc(rAccDoc),
    mnParagraphs(0),
    mnOffset(0)
{
}

ScNotesChildren::~ScNotesChildren()
{
    for (auto & i : maNotes)
        if (i.mpTextHelper)
        {
            delete i.mpTextHelper;
            i.mpTextHelper = nullptr;
        }
    for (auto & i : maMarks)
        if (i.mpTextHelper)
        {
            delete i.mpTextHelper;
            i.mpTextHelper = nullptr;
        }
}

::accessibility::AccessibleTextHelper* ScNotesChildren::CreateTextHelper(const OUString& rString, const tools::Rectangle& rVisRect, const ScAddress& aCellPos, bool bMarkNote, sal_Int32 nChildOffset) const
{
    ::accessibility::AccessibleTextHelper* pTextHelper = new ::accessibility::AccessibleTextHelper(std::make_unique<ScAccessibilityEditSource>(std::make_unique<ScAccessibleNoteTextData>(mpViewShell, rString, aCellPos, bMarkNote)));
    pTextHelper->SetEventSource(&mrAccDoc);
    pTextHelper->SetStartIndex(nChildOffset);
    pTextHelper->SetOffset(rVisRect.TopLeft());

    return pTextHelper;
}

sal_Int32 ScNotesChildren::AddNotes(const ScPreviewLocationData& rData, const tools::Rectangle& rVisRect, bool bMark, ScAccNotes& rNotes)
{
    sal_Int32 nCount = rData.GetNoteCountInRange(rVisRect, bMark);

    rNotes.reserve(nCount);

    sal_Int32 nParagraphs(0);
    ScDocument* pDoc = GetDocument();
    if (pDoc)
    {
        ScAccNote aNote;
        aNote.mbMarkNote = bMark;
        if (bMark)
            aNote.mnParaCount = 1;
        for (sal_Int32 nIndex = 0; nIndex < nCount; ++nIndex)
        {
            if (rData.GetNoteInRange(rVisRect, nIndex, bMark, aNote.maNoteCell, aNote.maRect))
            {
                if (bMark)
                {
                    // Document not needed, because only the cell address, but not the tablename is needed
                    aNote.maNoteText = aNote.maNoteCell.Format(ScRefFlags::VALID);
                }
                else
                {
                    if( ScPostIt* pNote = pDoc->GetNote( aNote.maNoteCell ) )
                        aNote.maNoteText = pNote->GetText();
                    aNote.mpTextHelper = CreateTextHelper(aNote.maNoteText, aNote.maRect, aNote.maNoteCell, aNote.mbMarkNote, nParagraphs + mnOffset);
                    if (aNote.mpTextHelper)
                        aNote.mnParaCount = aNote.mpTextHelper->GetChildCount();
                }
                nParagraphs += aNote.mnParaCount;
                rNotes.push_back(aNote);
            }
        }
    }
    return nParagraphs;
}

void ScNotesChildren::Init(const tools::Rectangle& rVisRect, sal_Int32 nOffset)
{
    if (mpViewShell && !mnParagraphs)
    {
        mnOffset = nOffset;
        const ScPreviewLocationData& rData = mpViewShell->GetLocationData();

        mnParagraphs = AddNotes(rData, rVisRect, false, maMarks);
        mnParagraphs += AddNotes(rData, rVisRect, true, maNotes);
    }
}

namespace {

struct ScParaFound
{
    sal_Int32 mnIndex;
    explicit ScParaFound(sal_Int32 nIndex) : mnIndex(nIndex) {}
    bool operator() (const ScAccNote& rNote)
    {
        bool bResult(false);
        if (rNote.mnParaCount > mnIndex)
            bResult = true;
        else
            mnIndex -= rNote.mnParaCount;
        return bResult;
    }
};

}

uno::Reference<XAccessible> ScNotesChildren::GetChild(sal_Int32 nIndex) const
{
    uno::Reference<XAccessible> xAccessible;

    if (nIndex < mnParagraphs)
    {
        if (nIndex < static_cast<sal_Int32>(maMarks.size()))
        {
            ScAccNotes::iterator aEndItr = maMarks.end();
            ScParaFound aParaFound(nIndex);
            ScAccNotes::iterator aItr = std::find_if(maMarks.begin(), aEndItr, aParaFound);
            if (aItr != aEndItr)
            {
                OSL_ENSURE((aItr->maNoteCell == maMarks[nIndex].maNoteCell) && (aItr->mbMarkNote == maMarks[nIndex].mbMarkNote), "wrong note found");
                if (!aItr->mpTextHelper)
                    aItr->mpTextHelper = CreateTextHelper(maMarks[nIndex].maNoteText, maMarks[nIndex].maRect, maMarks[nIndex].maNoteCell, maMarks[nIndex].mbMarkNote, nIndex + mnOffset); // the marks are the first and every mark has only one paragraph
                xAccessible = aItr->mpTextHelper->GetChild(aParaFound.mnIndex + aItr->mpTextHelper->GetStartIndex());
            }
            else
            {
                OSL_FAIL("wrong note found");
            }
        }
        else
        {
            nIndex -= maMarks.size();
            ScAccNotes::iterator aEndItr = maNotes.end();
            ScParaFound aParaFound(nIndex);
            ScAccNotes::iterator aItr = std::find_if(maNotes.begin(), aEndItr, aParaFound);
            if (aEndItr != aItr)
            {
                if (!aItr->mpTextHelper)
                    aItr->mpTextHelper = CreateTextHelper(aItr->maNoteText, aItr->maRect, aItr->maNoteCell, aItr->mbMarkNote, (nIndex - aParaFound.mnIndex) + mnOffset + maMarks.size());
                xAccessible = aItr->mpTextHelper->GetChild(aParaFound.mnIndex + aItr->mpTextHelper->GetStartIndex());
            }
        }
    }

    return xAccessible;
}

namespace {

struct ScPointFound
{
    tools::Rectangle maPoint;
    sal_Int32 mnParagraphs;
    explicit ScPointFound(const Point& rPoint) : maPoint(rPoint, Size(0, 0)), mnParagraphs(0) {}
    bool operator() (const ScAccNote& rNote)
    {
        bool bResult(false);
        if (maPoint.Contains(rNote.maRect))
            bResult = true;
        else
            mnParagraphs += rNote.mnParaCount;
        return bResult;
    }
};

}

uno::Reference<XAccessible> ScNotesChildren::GetAt(const awt::Point& rPoint) const
{
    uno::Reference<XAccessible> xAccessible;

    ScPointFound aPointFound(Point(rPoint.X, rPoint.Y));

    ScAccNotes::iterator aEndItr = maMarks.end();
    ScAccNotes::iterator aItr = std::find_if(maMarks.begin(), aEndItr, aPointFound);
    if (aEndItr == aItr)
    {
        aEndItr = maNotes.end();
        aItr = std::find_if(maNotes.begin(), aEndItr, aPointFound);
    }
    if (aEndItr != aItr)
    {
        if (!aItr->mpTextHelper)
            aItr->mpTextHelper = CreateTextHelper(aItr->maNoteText, aItr->maRect, aItr->maNoteCell, aItr->mbMarkNote, aPointFound.mnParagraphs + mnOffset);
        xAccessible = aItr->mpTextHelper->GetAt(rPoint);
    }

    return xAccessible;
}

sal_Int8 ScNotesChildren::CompareCell(const ScAddress& aCell1, const ScAddress& aCell2)
{
    OSL_ENSURE(aCell1.Tab() == aCell2.Tab(), "the notes should be on the same table");
    sal_Int8 nResult(0);
    if (aCell1 != aCell2)
    {
        if (aCell1.Row() == aCell2.Row())
            nResult = (aCell1.Col() < aCell2.Col()) ? -1 : 1;
        else
            nResult = (aCell1.Row() < aCell2.Row()) ? -1 : 1;
    }
    return nResult;
}

void ScNotesChildren::CollectChildren(const ScAccNote& rNote, ScXAccVector& rVector)
{
    if (rNote.mpTextHelper)
        for (sal_Int32 i = 0; i < rNote.mnParaCount; ++i)
            rVector.push_back(rNote.mpTextHelper->GetChild(i + rNote.mpTextHelper->GetStartIndex()));
}

sal_Int32 ScNotesChildren::CheckChanges(const ScPreviewLocationData& rData,
            const tools::Rectangle& rVisRect, bool bMark, ScAccNotes& rOldNotes,
            ScAccNotes& rNewNotes, ScXAccVector& rOldParas, ScXAccVector& rNewParas)
{
    sal_Int32 nCount = rData.GetNoteCountInRange(rVisRect, bMark);

    rNewNotes.reserve(nCount);

    sal_Int32 nParagraphs(0);
    ScDocument* pDoc = GetDocument();
    if (pDoc)
    {
        ScAccNote aNote;
        aNote.mbMarkNote = bMark;
        if (bMark)
            aNote.mnParaCount = 1;
        ScAccNotes::iterator aItr = rOldNotes.begin();
        ScAccNotes::iterator aEndItr = rOldNotes.end();
        bool bAddNote(false);
        for (sal_Int32 nIndex = 0; nIndex < nCount; ++nIndex)
        {
            if (rData.GetNoteInRange(rVisRect, nIndex, bMark, aNote.maNoteCell, aNote.maRect))
            {
                if (bMark)
                {
                    // Document not needed, because only the cell address, but not the tablename is needed
                    aNote.maNoteText = aNote.maNoteCell.Format(ScRefFlags::VALID);
                }
                else
                {
                    if( ScPostIt* pNote = pDoc->GetNote( aNote.maNoteCell ) )
                        aNote.maNoteText = pNote->GetText();
                }

                sal_Int8 nCompare(-1); // if there are no more old children it is always a new one
                if (aItr != aEndItr)
                    nCompare = CompareCell(aNote.maNoteCell, aItr->maNoteCell);
                if (nCompare == 0)
                {
                    if (aNote.maNoteText == aItr->maNoteText)
                    {
                        aNote.mpTextHelper = aItr->mpTextHelper;
                        if (aNote.maRect != aItr->maRect)  // set new VisArea
                        {
                            aNote.mpTextHelper->SetOffset(aNote.maRect.TopLeft());
                            aNote.mpTextHelper->UpdateChildren();
                            //OSL_ENSURE(aItr->maRect.GetSize() == aNote.maRect.GetSize(), "size should be the same, because the text is not changed");
                            // could be changed, because only a part of the note is visible
                        }
                    }
                    else
                    {
                        aNote.mpTextHelper = CreateTextHelper(aNote.maNoteText, aNote.maRect, aNote.maNoteCell, aNote.mbMarkNote, nParagraphs + mnOffset);
                        if (aNote.mpTextHelper)
                            aNote.mnParaCount = aNote.mpTextHelper->GetChildCount();
                        // collect removed children
                        CollectChildren(*aItr, rOldParas);
                        delete aItr->mpTextHelper;
                        aItr->mpTextHelper = nullptr;;
                        // collect new children
                        CollectChildren(aNote, rNewParas);
                    }
                    bAddNote = true;
                    // not necessary, because this branch should not be reached if it is the end
                    //if (aItr != aEndItr)
                    ++aItr;
                }
                else if (nCompare < 0)
                {
                    aNote.mpTextHelper = CreateTextHelper(aNote.maNoteText, aNote.maRect, aNote.maNoteCell, aNote.mbMarkNote, nParagraphs + mnOffset);
                    if (aNote.mpTextHelper)
                        aNote.mnParaCount = aNote.mpTextHelper->GetChildCount();
                    // collect new children
                    CollectChildren(aNote, rNewParas);
                    bAddNote = true;
                }
                else
                {
                    // collect removed children
                    CollectChildren(*aItr, rOldParas);
                    delete aItr->mpTextHelper;
                    aItr->mpTextHelper = nullptr;

                    // no note to add
                    // not necessary, because this branch should not be reached if it is the end
                    //if (aItr != aEndItr)
                    ++aItr;
                }
                if (bAddNote)
                {
                    nParagraphs += aNote.mnParaCount;
                    rNewNotes.push_back(aNote);
                    bAddNote = false;
                }
            }
        }
    }
    return nParagraphs;
}

namespace {

struct ScChildGone
{
    ScAccessibleDocumentPagePreview& mrAccDoc;
    explicit ScChildGone(ScAccessibleDocumentPagePreview& rAccDoc) : mrAccDoc(rAccDoc) {}
    void operator() (const uno::Reference<XAccessible>& xAccessible) const
    {
        // gone child - event
        mrAccDoc.CommitChange(AccessibleEventId::CHILD, uno::Any(xAccessible), uno::Any());
    }
};

struct ScChildNew
{
    ScAccessibleDocumentPagePreview& mrAccDoc;
    explicit ScChildNew(ScAccessibleDocumentPagePreview& rAccDoc) : mrAccDoc(rAccDoc) {}
    void operator() (const uno::Reference<XAccessible>& xAccessible) const
    {
        // new child - event
        mrAccDoc.CommitChange(AccessibleEventId::CHILD, uno::Any(), uno::Any(xAccessible));
    }
};

}

void ScNotesChildren::DataChanged(const tools::Rectangle& rVisRect)
{
    if (!mpViewShell)
        return;

    ScXAccVector aNewParas;
    ScXAccVector aOldParas;
    {
        ScAccNotes aNewMarks;
        mnParagraphs = CheckChanges(mpViewShell->GetLocationData(), rVisRect, true, maMarks, aNewMarks, aOldParas, aNewParas);
        maMarks = std::move(aNewMarks);
    }
    {
        ScAccNotes aNewNotes;
        mnParagraphs += CheckChanges(mpViewShell->GetLocationData(), rVisRect, false, maNotes, aNewNotes, aOldParas, aNewParas);
        maNotes = std::move(aNewNotes);
    }

    std::for_each(aOldParas.begin(), aOldParas.end(), ScChildGone(mrAccDoc));
    std::for_each(aNewParas.begin(), aNewParas.end(), ScChildNew(mrAccDoc));
}

inline ScDocument* ScNotesChildren::GetDocument() const
{
    ScDocument* pDoc = nullptr;
    if (mpViewShell)
        pDoc = &mpViewShell->GetDocument();
    return pDoc;
}

namespace {

class ScIAccessibleViewForwarder : public ::accessibility::IAccessibleViewForwarder
{
public:
    ScIAccessibleViewForwarder();
    ScIAccessibleViewForwarder(ScPreviewShell* pViewShell,
                                ScAccessibleDocumentPagePreview* pAccDoc,
                                const MapMode& aMapMode);

    ///=====  IAccessibleViewForwarder  ========================================

    virtual tools::Rectangle GetVisibleArea() const override;
    virtual Point LogicToPixel (const Point& rPoint) const override;
    virtual Size LogicToPixel (const Size& rSize) const override;

private:
    ScPreviewShell*                     mpViewShell;
    ScAccessibleDocumentPagePreview*    mpAccDoc;
    MapMode                             maMapMode;
};

}

ScIAccessibleViewForwarder::ScIAccessibleViewForwarder()
    : mpViewShell(nullptr), mpAccDoc(nullptr)
{
}

ScIAccessibleViewForwarder::ScIAccessibleViewForwarder(ScPreviewShell* pViewShell,
                                ScAccessibleDocumentPagePreview* pAccDoc,
                                const MapMode& aMapMode)
    : mpViewShell(pViewShell),
    mpAccDoc(pAccDoc),
    maMapMode(aMapMode)
{
}

///=====  IAccessibleViewForwarder  ========================================

tools::Rectangle ScIAccessibleViewForwarder::GetVisibleArea() const
{
    SolarMutexGuard aGuard;
    tools::Rectangle aVisRect;
    vcl::Window* pWin = mpViewShell->GetWindow();
    if (pWin)
    {
        aVisRect.SetSize(pWin->GetOutputSizePixel());
        aVisRect.SetPos(Point(0, 0));

        aVisRect = pWin->PixelToLogic(aVisRect, maMapMode);
    }

    return aVisRect;
}

Point ScIAccessibleViewForwarder::LogicToPixel (const Point& rPoint) const
{
    SolarMutexGuard aGuard;
    Point aPoint;
    vcl::Window* pWin = mpViewShell->GetWindow();
    if (pWin && mpAccDoc)
    {
        tools::Rectangle aRect(mpAccDoc->GetBoundingBoxOnScreen());
        aPoint = pWin->LogicToPixel(rPoint, maMapMode) + aRect.TopLeft();
    }

    return aPoint;
}

Size ScIAccessibleViewForwarder::LogicToPixel (const Size& rSize) const
{
    SolarMutexGuard aGuard;
    Size aSize;
    vcl::Window* pWin = mpViewShell->GetWindow();
    if (pWin)
        aSize = pWin->LogicToPixel(rSize, maMapMode);
    return aSize;
}

namespace {

struct ScShapeChild
{
    ScShapeChild()
        : mnRangeId(0)
    {
    }
    ScShapeChild(ScShapeChild const &) = delete;
    ScShapeChild(ScShapeChild &&) = default;
    ~ScShapeChild();
    ScShapeChild & operator =(ScShapeChild const &) = delete;
    ScShapeChild & operator =(ScShapeChild && other) {
        std::swap(mpAccShape, other.mpAccShape);
        mxShape = std::move(other.mxShape);
        mnRangeId = other.mnRangeId;
        return *this;
    }

    mutable rtl::Reference< ::accessibility::AccessibleShape > mpAccShape;
    css::uno::Reference< css::drawing::XShape > mxShape;
    sal_Int32 mnRangeId;
};

}

ScShapeChild::~ScShapeChild()
{
    if (mpAccShape.is())
    {
        mpAccShape->dispose();
    }
}

namespace {

struct ScShapeChildLess
{
    bool operator()(const ScShapeChild& rChild1, const ScShapeChild& rChild2) const
    {
      bool bResult(false);
      if (rChild1.mxShape.is() && rChild2.mxShape.is())
          bResult = (rChild1.mxShape.get() < rChild2.mxShape.get());
      return bResult;
    }
};

}

typedef std::vector<ScShapeChild> ScShapeChildVec;

namespace {

struct ScShapeRange
{
    ScShapeRange() = default;
    ScShapeRange(ScShapeRange const &) = delete;
    ScShapeRange(ScShapeRange &&) = default;
    ScShapeRange & operator =(ScShapeRange const &) = delete;
    ScShapeRange & operator =(ScShapeRange &&) = default;

    ScShapeChildVec maBackShapes;
    ScShapeChildVec maForeShapes; // inclusive internal shapes
    ScShapeChildVec maControls;
    ScIAccessibleViewForwarder maViewForwarder;
};

}

typedef std::vector<ScShapeRange> ScShapeRangeVec;

class ScShapeChildren : public ::accessibility::IAccessibleParent
{
public:
    ScShapeChildren(ScPreviewShell* pViewShell, ScAccessibleDocumentPagePreview* pAccDoc);

    ///=====  IAccessibleParent  ==============================================

    virtual bool ReplaceChild (
        ::accessibility::AccessibleShape* pCurrentChild,
        const css::uno::Reference< css::drawing::XShape >& _rxShape,
        const tools::Long _nIndex,
        const ::accessibility::AccessibleShapeTreeInfo& _rShapeTreeInfo
    ) override;

    ///=====  Internal  ========================================================

    void Init();

    sal_Int32 GetBackShapeCount() const;
    uno::Reference<XAccessible> GetBackShape(sal_Int32 nIndex) const;
    sal_Int32 GetForeShapeCount() const;
    uno::Reference<XAccessible> GetForeShape(sal_Int32 nIndex) const;
    sal_Int32 GetControlCount() const;
    uno::Reference<XAccessible> GetControl(sal_Int32 nIndex) const;
    uno::Reference<XAccessible> GetForegroundShapeAt(const awt::Point& rPoint) const; // inclusive controls
    uno::Reference<XAccessible> GetBackgroundShapeAt(const awt::Point& rPoint) const;

    void DataChanged();
    void VisAreaChanged() const;

private:
    ScAccessibleDocumentPagePreview* mpAccDoc;
    ScPreviewShell* mpViewShell;
    ScShapeRangeVec maShapeRanges;

    void FindChanged(ScShapeChildVec& aOld, ScShapeChildVec& aNew) const;
    void FindChanged(ScShapeRange& aOld, ScShapeRange& aNew) const;
    ::accessibility::AccessibleShape* GetAccShape(const ScShapeChild& rShape) const;
    ::accessibility::AccessibleShape* GetAccShape(const ScShapeChildVec& rShapes, sal_Int32 nIndex) const;
    void FillShapes(const tools::Rectangle& aPixelPaintRect, const MapMode& aMapMode, sal_uInt8 nRangeId);

//    void AddShape(const uno::Reference<drawing::XShape>& xShape, SdrLayerID aLayerID);
//    void RemoveShape(const uno::Reference<drawing::XShape>& xShape, SdrLayerID aLayerID);
    SdrPage* GetDrawPage() const;
};

ScShapeChildren::ScShapeChildren(ScPreviewShell* pViewShell, ScAccessibleDocumentPagePreview* pAccDoc)
    :
    mpAccDoc(pAccDoc),
    mpViewShell(pViewShell),
    maShapeRanges(SC_PREVIEW_MAXRANGES)
{
}

void ScShapeChildren::FindChanged(ScShapeChildVec& rOld, ScShapeChildVec& rNew) const
{
    ScShapeChildVec::iterator aOldItr = rOld.begin();
    ScShapeChildVec::iterator aOldEnd = rOld.end();
    ScShapeChildVec::const_iterator aNewItr = rNew.begin();
    ScShapeChildVec::const_iterator aNewEnd = rNew.end();
    uno::Reference<XAccessible> xAcc;
    while ((aNewItr != aNewEnd) && (aOldItr != aOldEnd))
    {
        if (aNewItr->mxShape.get() == aOldItr->mxShape.get())
        {
            ++aOldItr;
            ++aNewItr;
        }
        else if (aNewItr->mxShape.get() < aOldItr->mxShape.get())
        {
            xAcc = GetAccShape(*aNewItr);
            mpAccDoc->CommitChange(AccessibleEventId::CHILD, uno::Any(), uno::Any(xAcc));
            ++aNewItr;
        }
        else
        {
            xAcc = GetAccShape(*aOldItr);
            mpAccDoc->CommitChange(AccessibleEventId::CHILD, uno::Any(xAcc), uno::Any());
            ++aOldItr;
        }
    }
    while (aOldItr != aOldEnd)
    {
        xAcc = GetAccShape(*aOldItr);
        mpAccDoc->CommitChange(AccessibleEventId::CHILD, uno::Any(xAcc), uno::Any());
        ++aOldItr;
    }
    while (aNewItr != aNewEnd)
    {
        xAcc = GetAccShape(*aNewItr);
        mpAccDoc->CommitChange(AccessibleEventId::CHILD, uno::Any(), uno::Any(xAcc));
        ++aNewItr;
    }
}

void ScShapeChildren::FindChanged(ScShapeRange& rOld, ScShapeRange& rNew) const
{
    FindChanged(rOld.maBackShapes, rNew.maBackShapes);
    FindChanged(rOld.maForeShapes, rNew.maForeShapes);
    FindChanged(rOld.maControls, rNew.maControls);
}

void ScShapeChildren::DataChanged()
{
    ScShapeRangeVec aOldShapeRanges(std::move(maShapeRanges));
    maShapeRanges.clear();
    maShapeRanges.resize(SC_PREVIEW_MAXRANGES);
    Init();
    for (sal_Int32 i = 0; i < SC_PREVIEW_MAXRANGES; ++i)
    {
        FindChanged(aOldShapeRanges[i], maShapeRanges[i]);
    }
}

namespace
{
    struct ScVisAreaChanged
    {
        void operator() (const ScShapeChild& rAccShapeData) const
        {
            if (rAccShapeData.mpAccShape.is())
            {
                rAccShapeData.mpAccShape->ViewForwarderChanged();
            }
        }
    };
}

void ScShapeChildren::VisAreaChanged() const
{
    for (auto const& shape : maShapeRanges)
    {
        ScVisAreaChanged aVisAreaChanged;
        std::for_each(shape.maBackShapes.begin(), shape.maBackShapes.end(), aVisAreaChanged);
        std::for_each(shape.maControls.begin(), shape.maControls.end(), aVisAreaChanged);
        std::for_each(shape.maForeShapes.begin(), shape.maForeShapes.end(), aVisAreaChanged);
    }
}

    ///=====  IAccessibleParent  ==============================================

bool ScShapeChildren::ReplaceChild (::accessibility::AccessibleShape* /* pCurrentChild */,
    const css::uno::Reference< css::drawing::XShape >& /* _rxShape */,
        const tools::Long /* _nIndex */, const ::accessibility::AccessibleShapeTreeInfo& /* _rShapeTreeInfo */)
{
    OSL_FAIL("should not be called in the page preview");
    return false;
}

    ///=====  Internal  ========================================================

void ScShapeChildren::Init()
{
    if(!mpViewShell)
        return;

    const ScPreviewLocationData& rData = mpViewShell->GetLocationData();
    MapMode aMapMode;
    tools::Rectangle aPixelPaintRect;
    sal_uInt8 nRangeId;
    sal_uInt16 nCount(rData.GetDrawRanges());
    for (sal_uInt16 i = 0; i < nCount; ++i)
    {
        rData.GetDrawRange(i, aPixelPaintRect, aMapMode, nRangeId);
        FillShapes(aPixelPaintRect, aMapMode, nRangeId);
    }
}

sal_Int32 ScShapeChildren::GetBackShapeCount() const
{
    sal_Int32 nCount(0);
    for (auto const& shape : maShapeRanges)
        nCount += shape.maBackShapes.size();
    return nCount;
}

uno::Reference<XAccessible> ScShapeChildren::GetBackShape(sal_Int32 nIndex) const
{
    uno::Reference<XAccessible> xAccessible;
    for (const auto& rShapeRange : maShapeRanges)
    {
        sal_Int32 nCount(rShapeRange.maBackShapes.size());
        if(nIndex < nCount)
            xAccessible = GetAccShape(rShapeRange.maBackShapes, nIndex);
        nIndex -= nCount;
        if (xAccessible.is())
            break;
    }

    if (nIndex >= 0)
        throw lang::IndexOutOfBoundsException();

    return xAccessible;
}

sal_Int32 ScShapeChildren::GetForeShapeCount() const
{
    sal_Int32 nCount(0);
    for (auto const& shape : maShapeRanges)
        nCount += shape.maForeShapes.size();
    return nCount;
}

uno::Reference<XAccessible> ScShapeChildren::GetForeShape(sal_Int32 nIndex) const
{
    uno::Reference<XAccessible> xAccessible;
    for (const auto& rShapeRange : maShapeRanges)
    {
        sal_Int32 nCount(rShapeRange.maForeShapes.size());
        if(nIndex < nCount)
            xAccessible = GetAccShape(rShapeRange.maForeShapes, nIndex);
        nIndex -= nCount;
        if (xAccessible.is())
            break;
    }

    if (nIndex >= 0)
        throw lang::IndexOutOfBoundsException();

    return xAccessible;
}

sal_Int32 ScShapeChildren::GetControlCount() const
{
    sal_Int32 nCount(0);
    for (auto const& shape : maShapeRanges)
        nCount += shape.maControls.size();
    return nCount;
}

uno::Reference<XAccessible> ScShapeChildren::GetControl(sal_Int32 nIndex) const
{
    uno::Reference<XAccessible> xAccessible;
    for (const auto& rShapeRange : maShapeRanges)
    {
        sal_Int32 nCount(rShapeRange.maControls.size());
        if(nIndex < nCount)
            xAccessible = GetAccShape(rShapeRange.maControls, nIndex);
        nIndex -= nCount;
        if (xAccessible.is())
            break;
    }

    if (nIndex >= 0)
        throw lang::IndexOutOfBoundsException();

    return xAccessible;
}

namespace {

struct ScShapePointFound
{
    Point maPoint;
    explicit ScShapePointFound(const awt::Point& rPoint)
        : maPoint(vcl::unohelper::ConvertToVCLPoint(rPoint))
    {
    }
    bool operator() (const ScShapeChild& rShape)
    {
        bool bResult(false);
        if (vcl::unohelper::ConvertToVCLRect(rShape.mpAccShape->getBounds()).Contains(maPoint))
            bResult = true;
        return bResult;
    }
};

}

uno::Reference<XAccessible> ScShapeChildren::GetForegroundShapeAt(const awt::Point& rPoint) const //inclusive Controls
{
    uno::Reference<XAccessible> xAcc;

    for(const auto& rShapeRange : maShapeRanges)
    {
        ScShapeChildVec::const_iterator aFindItr = std::find_if(rShapeRange.maForeShapes.begin(), rShapeRange.maForeShapes.end(), ScShapePointFound(rPoint));
        if (aFindItr != rShapeRange.maForeShapes.end())
            xAcc = GetAccShape(*aFindItr);
        else
        {
            ScShapeChildVec::const_iterator aCtrlItr = std::find_if(rShapeRange.maControls.begin(), rShapeRange.maControls.end(), ScShapePointFound(rPoint));
            if (aCtrlItr != rShapeRange.maControls.end())
                xAcc = GetAccShape(*aCtrlItr);
        }

        if (xAcc.is())
            break;
    }

    return xAcc;
}

uno::Reference<XAccessible> ScShapeChildren::GetBackgroundShapeAt(const awt::Point& rPoint) const
{
    uno::Reference<XAccessible> xAcc;

    for(const auto& rShapeRange : maShapeRanges)
    {
        ScShapeChildVec::const_iterator aFindItr = std::find_if(rShapeRange.maBackShapes.begin(), rShapeRange.maBackShapes.end(), ScShapePointFound(rPoint));
        if (aFindItr != rShapeRange.maBackShapes.end())
            xAcc = GetAccShape(*aFindItr);
        if (xAcc.is())
            break;
    }

    return xAcc;
}

::accessibility::AccessibleShape* ScShapeChildren::GetAccShape(const ScShapeChild& rShape) const
{
    if (!rShape.mpAccShape.is())
    {
        ::accessibility::ShapeTypeHandler& rShapeHandler = ::accessibility::ShapeTypeHandler::Instance();
        ::accessibility::AccessibleShapeInfo aShapeInfo(rShape.mxShape, mpAccDoc);

        if (mpViewShell)
        {
            ::accessibility::AccessibleShapeTreeInfo aShapeTreeInfo;
            aShapeTreeInfo.SetSdrView(mpViewShell->GetPreview()->GetDrawView());
            aShapeTreeInfo.SetController(nullptr);
            aShapeTreeInfo.SetWindow(mpViewShell->GetWindow());
            aShapeTreeInfo.SetViewForwarder(&(maShapeRanges[rShape.mnRangeId].maViewForwarder));
            rShape.mpAccShape = rShapeHandler.CreateAccessibleObject(aShapeInfo, aShapeTreeInfo);
            if (rShape.mpAccShape.is())
            {
                rShape.mpAccShape->Init();
            }
        }
    }
    return rShape.mpAccShape.get();
}

::accessibility::AccessibleShape* ScShapeChildren::GetAccShape(const ScShapeChildVec& rShapes, sal_Int32 nIndex) const
{
    return GetAccShape(rShapes[nIndex]);
}

void ScShapeChildren::FillShapes(const tools::Rectangle& aPixelPaintRect, const MapMode& aMapMode, sal_uInt8 nRangeId)
{
    OSL_ENSURE(nRangeId < maShapeRanges.size(), "this is not a valid range for draw objects");
    SdrPage* pPage = GetDrawPage();
    vcl::Window* pWin = mpViewShell->GetWindow();
    if (!(pPage && pWin))
        return;

    bool bForeAdded(false);
    bool bBackAdded(false);
    bool bControlAdded(false);
    tools::Rectangle aClippedPixelPaintRect(aPixelPaintRect);
    if (mpAccDoc)
    {
        tools::Rectangle aRect2(Point(0,0), mpAccDoc->GetBoundingBoxOnScreen().GetSize());
        aClippedPixelPaintRect = aPixelPaintRect.GetIntersection(aRect2);
    }
    maShapeRanges[nRangeId].maViewForwarder = ScIAccessibleViewForwarder(mpViewShell, mpAccDoc, aMapMode);
    for (const rtl::Reference<SdrObject>& pObj : *pPage)
    {
        uno::Reference< drawing::XShape > xShape(pObj->getUnoShape(), uno::UNO_QUERY);
        if (xShape.is())
        {
            tools::Rectangle aRect(pWin->LogicToPixel(
                tools::Rectangle(vcl::unohelper::ConvertToVCLPoint(xShape->getPosition()),
                                 vcl::unohelper::ConvertToVCLSize(xShape->getSize())), aMapMode));
            if(!aClippedPixelPaintRect.GetIntersection(aRect).IsEmpty())
            {
                ScShapeChild aShape;
                aShape.mxShape = std::move(xShape);
                aShape.mnRangeId = nRangeId;
                if (pObj->GetLayer().anyOf(SC_LAYER_INTERN, SC_LAYER_FRONT))
                {
                    maShapeRanges[nRangeId].maForeShapes.push_back(std::move(aShape));
                    bForeAdded = true;
                }
                else if (pObj->GetLayer() == SC_LAYER_BACK)
                {
                    maShapeRanges[nRangeId].maBackShapes.push_back(std::move(aShape));
                    bBackAdded = true;
                }
                else if (pObj->GetLayer() == SC_LAYER_CONTROLS)
                {
                    maShapeRanges[nRangeId].maControls.push_back(std::move(aShape));
                    bControlAdded = true;
                }
                else
                {
                    OSL_FAIL("I don't know this layer.");
                }
            }
        }
    }
    if (bForeAdded)
        std::sort(maShapeRanges[nRangeId].maForeShapes.begin(), maShapeRanges[nRangeId].maForeShapes.end(),ScShapeChildLess());
    if (bBackAdded)
        std::sort(maShapeRanges[nRangeId].maBackShapes.begin(), maShapeRanges[nRangeId].maBackShapes.end(),ScShapeChildLess());
    if (bControlAdded)
        std::sort(maShapeRanges[nRangeId].maControls.begin(), maShapeRanges[nRangeId].maControls.end(),ScShapeChildLess());
}

SdrPage* ScShapeChildren::GetDrawPage() const
{
    SCTAB nTab( mpViewShell->GetLocationData().GetPrintTab() );
    SdrPage* pDrawPage = nullptr;
    ScDocument& rDoc = mpViewShell->GetDocument();
    if (rDoc.GetDrawLayer())
    {
        ScDrawLayer* pDrawLayer = rDoc.GetDrawLayer();
        if (pDrawLayer->HasObjects() && (pDrawLayer->GetPageCount() > nTab))
            pDrawPage = pDrawLayer->GetPage(static_cast<sal_uInt16>(static_cast<sal_Int16>(nTab)));
    }
    return pDrawPage;
}

namespace {

struct ScPagePreviewCountData
{
    //  order is background shapes, header, table or notes, footer, foreground shapes, controls

    tools::Rectangle aVisRect;
    tools::Long nBackShapes;
    tools::Long nHeaders;
    tools::Long nTables;
    tools::Long nNoteParagraphs;
    tools::Long nFooters;
    tools::Long nForeShapes;
    tools::Long nControls;

    ScPagePreviewCountData( const ScPreviewLocationData& rData, const vcl::Window* pSizeWindow,
        const ScNotesChildren* pNotesChildren, const ScShapeChildren* pShapeChildren );

    tools::Long GetTotal() const
    {
        return nBackShapes + nHeaders + nTables + nNoteParagraphs + nFooters + nForeShapes + nControls;
    }
};

}

ScPagePreviewCountData::ScPagePreviewCountData( const ScPreviewLocationData& rData,
                                const vcl::Window* pSizeWindow, const ScNotesChildren* pNotesChildren,
                                const ScShapeChildren* pShapeChildren) :
    nBackShapes( 0 ),
    nHeaders( 0 ),
    nTables( 0 ),
    nNoteParagraphs( 0 ),
    nFooters( 0 ),
    nForeShapes( 0 ),
    nControls( 0 )
{
    Size aOutputSize;
    if ( pSizeWindow )
        aOutputSize = pSizeWindow->GetOutputSizePixel();
    aVisRect = tools::Rectangle( Point(), aOutputSize );

    tools::Rectangle aObjRect;

    if ( rData.GetHeaderPosition( aObjRect ) && aObjRect.Overlaps( aVisRect ) )
        nHeaders = 1;

    if ( rData.GetFooterPosition( aObjRect ) && aObjRect.Overlaps( aVisRect ) )
        nFooters = 1;

    if ( rData.HasCellsInRange( aVisRect ) )
        nTables = 1;

    //! shapes...
    nBackShapes = pShapeChildren->GetBackShapeCount();
    nForeShapes = pShapeChildren->GetForeShapeCount();
    nControls = pShapeChildren->GetControlCount();

    // there are only notes if there is no table
    if (nTables == 0)
        nNoteParagraphs = pNotesChildren->GetChildrenCount();
}

ScAccessibleDocumentPagePreview::ScAccessibleDocumentPagePreview(
        const uno::Reference<XAccessible>& rxParent, ScPreviewShell* pViewShell ) :
    ScAccessibleDocumentBase(rxParent),
    mpViewShell(pViewShell)
{
    if (pViewShell)
        pViewShell->AddAccessibilityObject(*this);

}

ScAccessibleDocumentPagePreview::~ScAccessibleDocumentPagePreview()
{
    if (!ScAccessibleDocumentBase::IsDefunc() && !rBHelper.bInDispose)
    {
        // increment refcount to prevent double call of dtor
        osl_atomic_increment( &m_refCount );
        // call dispose to inform object which have a weak reference to this object
        dispose();
    }
}

void SAL_CALL ScAccessibleDocumentPagePreview::disposing()
{
    SolarMutexGuard aGuard;
    if (mpTable.is())
    {
        mpTable->dispose();
        mpTable.clear();
    }
    if (mpHeader)
    {
        mpHeader->dispose();
        mpHeader.clear();
    }
    if (mpFooter)
    {
        mpFooter->dispose();
        mpFooter.clear();
    }

    if (mpViewShell)
    {
        mpViewShell->RemoveAccessibilityObject(*this);
        mpViewShell = nullptr;
    }

    // no need to Dispose the AccessibleTextHelper,
    // as long as mpNotesChildren are destructed here
    mpNotesChildren.reset();

    mpShapeChildren.reset();

    ScAccessibleDocumentBase::disposing();
}

//=====  SfxListener  =====================================================

void ScAccessibleDocumentPagePreview::Notify( SfxBroadcaster& rBC, const SfxHint& rHint )
{
    if ( rHint.GetId() == SfxHintId::ScAccWinFocusLost )
    {
        CommitFocusLost();
    }
    else if ( rHint.GetId() == SfxHintId::ScAccGridWinFocusLost )
    {
        CommitFocusLost();
    }
    else if ( rHint.GetId() == SfxHintId::ScAccWinFocusGot )
    {
        CommitFocusGained();
    }
    else if ( rHint.GetId() == SfxHintId::ScAccGridWinFocusGot )
    {
        CommitFocusGained();
    }
    else if (rHint.GetId() == SfxHintId::ScDataChanged)
    {
        // only notify if child exist, otherwise it is not necessary
        if (mpTable.is()) // if there is no table there is nothing to notify, because no one recognizes the change
        {
            CommitChange(AccessibleEventId::CHILD, uno::Any(uno::Reference<XAccessible>(mpTable)),
                         uno::Any());

            mpTable->dispose();
            mpTable.clear();
        }

        Size aOutputSize;
        vcl::Window* pSizeWindow = mpViewShell->GetWindow();
        if ( pSizeWindow )
            aOutputSize = pSizeWindow->GetOutputSizePixel();
        tools::Rectangle aVisRect( Point(), aOutputSize );
        GetNotesChildren()->DataChanged(aVisRect);

        GetShapeChildren()->DataChanged();

        const ScPreviewLocationData& rData = mpViewShell->GetLocationData();
        ScPagePreviewCountData aCount( rData, mpViewShell->GetWindow(), GetNotesChildren(), GetShapeChildren() );

        if (aCount.nTables > 0)
        {
            //! order is background shapes, header, table or notes, footer, foreground shapes, controls
            sal_Int32 nIndex (aCount.nBackShapes + aCount.nHeaders);

            mpTable = new ScAccessiblePreviewTable( this, mpViewShell, nIndex );
            mpTable->Init();

            CommitChange(AccessibleEventId::CHILD, uno::Any(),
                         uno::Any(uno::Reference<XAccessible>(mpTable)));
        }
    }
    else if (rHint.GetId() == SfxHintId::ScAccVisAreaChanged)
    {
        Size aOutputSize;
        vcl::Window* pSizeWindow = mpViewShell->GetWindow();
        if ( pSizeWindow )
            aOutputSize = pSizeWindow->GetOutputSizePixel();
        tools::Rectangle aVisRect( Point(), aOutputSize );
        GetNotesChildren()->DataChanged(aVisRect);

        GetShapeChildren()->VisAreaChanged();

        CommitChange(AccessibleEventId::VISIBLE_DATA_CHANGED, uno::Any(), uno::Any());
    }
    ScAccessibleDocumentBase::Notify(rBC, rHint);
}

//=====  XAccessibleComponent  ============================================

uno::Reference< XAccessible > SAL_CALL ScAccessibleDocumentPagePreview::getAccessibleAtPoint( const awt::Point& rPoint )
{
    uno::Reference<XAccessible> xAccessible;
    if (containsPoint(rPoint))
    {
        SolarMutexGuard aGuard;
        ensureAlive();

        if ( mpViewShell )
        {
            xAccessible = GetShapeChildren()->GetForegroundShapeAt(rPoint);
            if (!xAccessible.is())
            {
                const ScPreviewLocationData& rData = mpViewShell->GetLocationData();
                ScPagePreviewCountData aCount( rData, mpViewShell->GetWindow(), GetNotesChildren(), GetShapeChildren() );

                if ( !mpTable.is() && (aCount.nTables > 0) )
                {
                    //! order is background shapes, header, table or notes, footer, foreground shapes, controls
                    sal_Int32 nIndex (aCount.nBackShapes + aCount.nHeaders);

                    mpTable = new ScAccessiblePreviewTable( this, mpViewShell, nIndex );
                    mpTable->Init();
                }
                if (mpTable.is()
                    && vcl::unohelper::ConvertToVCLRect(mpTable->getBounds())
                           .Contains(vcl::unohelper::ConvertToVCLPoint(rPoint)))
                    xAccessible = mpTable.get();
            }
            if (!xAccessible.is())
                xAccessible = GetNotesChildren()->GetAt(rPoint);
            if (!xAccessible.is())
            {
                if (!mpHeader.is() || !mpFooter.is())
                {
                    const ScPreviewLocationData& rData = mpViewShell->GetLocationData();
                    ScPagePreviewCountData aCount( rData, mpViewShell->GetWindow(), GetNotesChildren(), GetShapeChildren() );

                    if (!mpHeader.is())
                    {
                        mpHeader = new ScAccessiblePageHeader( this, mpViewShell, true, aCount.nBackShapes + aCount.nHeaders - 1);
                    }
                    if (!mpFooter.is())
                    {
                        mpFooter = new ScAccessiblePageHeader( this, mpViewShell, false, aCount.nBackShapes + aCount.nHeaders + aCount.nTables + aCount.nNoteParagraphs + aCount.nFooters - 1 );
                    }
                }

                Point aPoint(vcl::unohelper::ConvertToVCLPoint(rPoint));

                if (vcl::unohelper::ConvertToVCLRect(mpHeader->getBounds()).Contains(aPoint))
                    xAccessible = mpHeader.get();
                else if (vcl::unohelper::ConvertToVCLRect(mpFooter->getBounds()).Contains(aPoint))
                    xAccessible = mpFooter.get();
            }
            if (!xAccessible.is())
                xAccessible = GetShapeChildren()->GetBackgroundShapeAt(rPoint);
        }
    }

    return xAccessible;
}

void SAL_CALL ScAccessibleDocumentPagePreview::grabFocus()
{
    SolarMutexGuard aGuard;
    ensureAlive();
    if (getAccessibleParent().is())
    {
        uno::Reference<XAccessibleComponent> xAccessibleComponent(getAccessibleParent()->getAccessibleContext(), uno::UNO_QUERY);
        if (xAccessibleComponent.is())
        {
            // just grab the focus for the window
            xAccessibleComponent->grabFocus();
        }
    }
}

//=====  XAccessibleContext  ==============================================

sal_Int64 SAL_CALL ScAccessibleDocumentPagePreview::getAccessibleChildCount()
{
    SolarMutexGuard aGuard;
    ensureAlive();

    sal_Int64 nRet = 0;
    if ( mpViewShell )
    {
        ScPagePreviewCountData aCount( mpViewShell->GetLocationData(), mpViewShell->GetWindow(), GetNotesChildren(), GetShapeChildren() );
        nRet = aCount.GetTotal();
    }

    return nRet;
}

uno::Reference<XAccessible> SAL_CALL ScAccessibleDocumentPagePreview::getAccessibleChild(sal_Int64 nIndex)
{
    SolarMutexGuard aGuard;
    ensureAlive();
    uno::Reference<XAccessible> xAccessible;

    if ( mpViewShell )
    {
        const ScPreviewLocationData& rData = mpViewShell->GetLocationData();
        ScPagePreviewCountData aCount( rData, mpViewShell->GetWindow(), GetNotesChildren(), GetShapeChildren() );

        if ( nIndex < aCount.nBackShapes )
        {
            xAccessible = GetShapeChildren()->GetBackShape(nIndex);
        }
        else if ( nIndex < aCount.nBackShapes + aCount.nHeaders )
        {
            if ( !mpHeader.is() )
            {
                mpHeader = new ScAccessiblePageHeader( this, mpViewShell, true, nIndex );
            }

            xAccessible = mpHeader.get();
        }
        else if ( nIndex < aCount.nBackShapes + aCount.nHeaders + aCount.nTables )
        {
            if ( !mpTable.is() )
            {
                mpTable = new ScAccessiblePreviewTable( this, mpViewShell, nIndex );
                mpTable->Init();
            }
            xAccessible = mpTable.get();
        }
        else if ( nIndex < aCount.nBackShapes + aCount.nHeaders + aCount.nNoteParagraphs )
        {
            xAccessible = GetNotesChildren()->GetChild(nIndex - aCount.nBackShapes - aCount.nHeaders);
        }
        else if ( nIndex < aCount.nBackShapes + aCount.nHeaders + aCount.nTables + aCount.nNoteParagraphs + aCount.nFooters )
        {
            if ( !mpFooter.is() )
            {
                mpFooter = new ScAccessiblePageHeader( this, mpViewShell, false, nIndex );
            }
            xAccessible = mpFooter.get();
        }
        else
        {
            sal_Int64 nIdx(nIndex - (aCount.nBackShapes + aCount.nHeaders + aCount.nTables + aCount.nNoteParagraphs + aCount.nFooters));
            if (nIdx < aCount.nForeShapes)
                xAccessible = GetShapeChildren()->GetForeShape(nIdx);
            else
                xAccessible = GetShapeChildren()->GetControl(nIdx - aCount.nForeShapes);
        }
    }

    if ( !xAccessible.is() )
        throw lang::IndexOutOfBoundsException();

    return xAccessible;
}

    /// Return the set of current states.
sal_Int64 SAL_CALL ScAccessibleDocumentPagePreview::getAccessibleStateSet()
{
    SolarMutexGuard aGuard;
    sal_Int64 nParentStates = 0;
    if (getAccessibleParent().is())
    {
        uno::Reference<XAccessibleContext> xParentContext = getAccessibleParent()->getAccessibleContext();
        nParentStates = xParentContext->getAccessibleStateSet();
    }
    sal_Int64 nStateSet = 0;
    if (IsDefunc(nParentStates))
        nStateSet |= AccessibleStateType::DEFUNC;
    else
    {
        // never editable
        nStateSet |= AccessibleStateType::ENABLED;
        nStateSet |= AccessibleStateType::OPAQUE;
        if (isShowing())
            nStateSet |= AccessibleStateType::SHOWING;
        if (isVisible())
            nStateSet |= AccessibleStateType::VISIBLE;
    }
    return nStateSet;
}

OUString ScAccessibleDocumentPagePreview::createAccessibleDescription()
{
    return STR_ACC_PREVIEWDOC_DESCR;
}

OUString ScAccessibleDocumentPagePreview::createAccessibleName()
{
    OUString sName = ScResId(STR_ACC_PREVIEWDOC_NAME);
    return sName;
}

AbsoluteScreenPixelRectangle ScAccessibleDocumentPagePreview::GetBoundingBoxOnScreen()
{
    AbsoluteScreenPixelRectangle aRect;
    if (mpViewShell)
    {
        vcl::Window* pWindow = mpViewShell->GetWindow();
        if (pWindow)
            aRect = pWindow->GetWindowExtentsAbsolute();
    }
    return aRect;
}

tools::Rectangle ScAccessibleDocumentPagePreview::GetBoundingBox()
{
    tools::Rectangle aRect;
    if (mpViewShell)
    {
        vcl::Window* pWindow = mpViewShell->GetWindow();
        if (pWindow)
            aRect = pWindow->GetWindowExtentsRelative(*pWindow->GetAccessibleParentWindow());
    }
    return aRect;
}

bool ScAccessibleDocumentPagePreview::IsDefunc(sal_Int64 nParentStates)
{
    return ScAccessibleContextBase::IsDefunc() || !getAccessibleParent().is() ||
        (nParentStates & AccessibleStateType::DEFUNC);
}

ScNotesChildren* ScAccessibleDocumentPagePreview::GetNotesChildren()
{
    if (!mpNotesChildren && mpViewShell)
    {
        mpNotesChildren.reset(new ScNotesChildren(mpViewShell, *this));

        const ScPreviewLocationData& rData = mpViewShell->GetLocationData();
        ScPagePreviewCountData aCount( rData, mpViewShell->GetWindow(), GetNotesChildren(), GetShapeChildren() );

        //! order is background shapes, header, table or notes, footer, foreground shapes, controls
        mpNotesChildren->Init(aCount.aVisRect, aCount.nBackShapes + aCount.nHeaders);
    }
    return mpNotesChildren.get();
}

ScShapeChildren* ScAccessibleDocumentPagePreview::GetShapeChildren()
{
    if (!mpShapeChildren && mpViewShell)
    {
        mpShapeChildren.reset( new ScShapeChildren(mpViewShell, this) );
        mpShapeChildren->Init();
    }

    return mpShapeChildren.get();
}

OUString ScAccessibleDocumentPagePreview::getAccessibleName()
{
    SolarMutexGuard g;

    OUString aName = ScResId(STR_ACC_DOC_SPREADSHEET);
    ScDocument& rScDoc = mpViewShell->GetDocument();

    ScDocShell* pObjSh = rScDoc.GetDocumentShell();
    if (!pObjSh)
        return aName;

    OUString aFileName;
    SfxMedium* pMed = pObjSh->GetMedium();
    if (pMed)
        aFileName = pMed->GetName();

    if (aFileName.isEmpty())
        aFileName = pObjSh->GetTitle(SFX_TITLE_APINAME);

    if (!aFileName.isEmpty())
    {
        aName = aFileName + " - " + aName + ScResId(STR_ACC_DOC_PREVIEW_SUFFIX);

    }

    return aName;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
