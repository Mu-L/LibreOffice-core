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

#include <com/sun/star/accessibility/XAccessibleValue.hpp>
#include <com/sun/star/accessibility/XAccessible.hpp>
#include <vcl/accessibility/AccessibleBrowseBoxCell.hxx>

class VCL_DLLPUBLIC AccessibleCheckBoxCell final
    : public cppu::ImplInheritanceHelper<AccessibleBrowseBoxCell,
                                         css::accessibility::XAccessibleValue>
{
private:
    TriState m_eState;
    bool m_bIsTriState;

    virtual ~AccessibleCheckBoxCell() override {}

    virtual sal_Int64 implCreateStateSet() override;

public:
    AccessibleCheckBoxCell(const css::uno::Reference< css::accessibility::XAccessible >& _rxParent,
                            ::vcl::IAccessibleTableProvider& _rBrowseBox,
                            sal_Int32 _nRowPos,
                            sal_uInt16 _nColPos,
                            const TriState& _eState,
                            bool _bIsTriState);

    // XAccessibleContext
    virtual sal_Int64 SAL_CALL getAccessibleChildCount(  ) override;
    virtual css::uno::Reference< css::accessibility::XAccessible > SAL_CALL getAccessibleChild( sal_Int64 i ) override;
    virtual OUString SAL_CALL getImplementationName() override;
    virtual sal_Int64 SAL_CALL getAccessibleIndexInParent(  ) override;


    // XAccessibleValue
    virtual css::uno::Any SAL_CALL getCurrentValue(  ) override;
    virtual sal_Bool SAL_CALL setCurrentValue( const css::uno::Any& aNumber ) override;
    virtual css::uno::Any SAL_CALL getMaximumValue(  ) override;
    virtual css::uno::Any SAL_CALL getMinimumValue(  ) override;
    virtual css::uno::Any SAL_CALL getMinimumIncrement(  ) override;

    // internal
    void        SetChecked( bool _bChecked );
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
