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
#ifndef INCLUDED_SW_INC_FMTFORDR_HXX
#define INCLUDED_SW_INC_FMTFORDR_HXX

#include <svl/eitem.hxx>
#include "hintids.hxx"
#include "format.hxx"

enum SwFillOrder
{
    SW_FILL_ORDER_BEGIN,
    ATT_TOP_DOWN = SW_FILL_ORDER_BEGIN,
    ATT_BOTTOM_UP,
    ATT_LEFT_TO_RIGHT,
    ATT_RIGHT_TO_LEFT,
    SW_FILL_ORDER_END
};

/// Some kind of fill order, mostly appears in the item set of a table line.
///
/// Doesn't seem to be used by the layout, also no UI to get or set it.
class SwFormatFillOrder final : public SfxEnumItem<SwFillOrder>
{
public:
    DECLARE_ITEM_TYPE_FUNCTION(SwFormatFillOrder)
    SwFormatFillOrder( SwFillOrder = ATT_TOP_DOWN );

    /// "Pure virtual methods" of SfxPoolItem.
    virtual SwFormatFillOrder* Clone( SfxItemPool *pPool = nullptr ) const override;
};

inline const SwFormatFillOrder &SwAttrSet::GetFillOrder(bool bInP) const
    { return Get( RES_FILL_ORDER,bInP); }

inline const SwFormatFillOrder &SwFormat::GetFillOrder(bool bInP) const
    { return m_aSet.GetFillOrder(bInP); }

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
