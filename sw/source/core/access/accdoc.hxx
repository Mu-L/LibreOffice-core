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

#include "acccontext.hxx"
#include <com/sun/star/accessibility/XAccessibleSelection.hpp>
#include <com/sun/star/accessibility/XAccessibleExtendedAttributes.hpp>
#include "accselectionhelper.hxx"
#include <vcl/window.hxx>

// base class for SwAccessibleDocument (in this same header file) and
// SwAccessiblePreview
class SwAccessibleDocumentBase : public SwAccessibleContext
{
    css::uno::Reference< css::accessibility::XAccessible> mxParent;

    VclPtr<vcl::Window> mpChildWin; // protected by solar mutex

    using SwAccessibleFrame::SetVisArea;

protected:
    virtual ~SwAccessibleDocumentBase() override;

public:
    SwAccessibleDocumentBase(std::shared_ptr<SwAccessibleMap> const& pInitMap);

    void SetVisArea();

    void AddChild( vcl::Window *pWin, bool bFireEvent = true );
    void RemoveChild( vcl::Window *pWin );

    // XAccessibleContext

    // Return the number of currently visible children.
    virtual sal_Int64 SAL_CALL getAccessibleChildCount() override;

    virtual css::uno::Reference< css::accessibility::XAccessible> SAL_CALL
        getAccessibleChild (sal_Int64 nIndex) override;

    virtual css::uno::Reference< css::accessibility::XAccessible> SAL_CALL
        getAccessibleParent() override;

    virtual sal_Int64 SAL_CALL
        getAccessibleIndexInParent() override;

    virtual OUString SAL_CALL
        getAccessibleDescription() override;

    virtual OUString SAL_CALL getAccessibleName() override;

    // OAccessible
    virtual css::awt::Rectangle implGetBounds() override;

    // XAccessibleComponent
    virtual css::uno::Reference<
        css::accessibility::XAccessible > SAL_CALL getAccessibleAtPoint(
                const css::awt::Point& aPoint ) override;

    virtual css::awt::Point SAL_CALL getLocationOnScreen() override;
};

using SwAccessibleDocument_BASE = cppu::ImplInheritanceHelper<SwAccessibleDocumentBase,
                                                              css::accessibility::XAccessibleSelection,
                                                              css::accessibility::XAccessibleExtendedAttributes>;
/**
 * access to an accessible Writer document
 */
class SwAccessibleDocument : public SwAccessibleDocument_BASE
{
    // Implementation for XAccessibleSelection interface
    SwAccessibleSelectionHelper maSelectionHelper;

protected:
    // Set states for getAccessibleStateSet.
    // This derived class additionally sets MULTISELECTABLE(1)
    virtual void GetStates( sal_Int64& rStateSet ) override;

    virtual ~SwAccessibleDocument() override;

public:
    SwAccessibleDocument(std::shared_ptr<SwAccessibleMap> const& pInitMap);

    DECL_LINK( WindowChildEventListener, VclWindowEvent&, void );

    // XAccessibleSelection

    virtual void SAL_CALL selectAccessibleChild(
        sal_Int64 nChildIndex ) override;

    virtual sal_Bool SAL_CALL isAccessibleChildSelected(
        sal_Int64 nChildIndex ) override;
    virtual void SAL_CALL clearAccessibleSelection(  ) override;
    virtual void SAL_CALL selectAllAccessibleChildren(  ) override;
    virtual sal_Int64 SAL_CALL getSelectedAccessibleChildCount(  ) override;
    virtual css::uno::Reference< css::accessibility::XAccessible > SAL_CALL getSelectedAccessibleChild(
        sal_Int64 nSelectedChildIndex ) override;

    // index has to be treated as global child index.
    virtual void SAL_CALL deselectAccessibleChild(
        sal_Int64 nChildIndex ) override;

    virtual OUString SAL_CALL getExtendedAttributes() override;

    // thread safe C++ interface

    // The object is not visible any longer and should be destroyed
    virtual void Dispose(bool bRecursive, bool bCanSkipInvisible = true) override;

    // XAccessibleComponent
    sal_Int32 SAL_CALL getBackground() override;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
