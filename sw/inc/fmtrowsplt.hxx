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
#ifndef INCLUDED_SW_INC_FMTROWSPLT_HXX
#define INCLUDED_SW_INC_FMTROWSPLT_HXX

#include <svl/eitem.hxx>
#include "swdllapi.h"
#include "hintids.hxx"
#include "format.hxx"

/// Controls if a table row is allowed to split or not. This is used in the item set of an
/// SwTableLine's format.
class SW_DLLPUBLIC SwFormatRowSplit final : public SfxBoolItem
{
public:
    DECLARE_ITEM_TYPE_FUNCTION(SwFormatRowSplit)
    SwFormatRowSplit( bool bSplit = true ) : SfxBoolItem( RES_ROW_SPLIT, bSplit ) {}

    // "pure virtual methods" of SfxPoolItem
    virtual SwFormatRowSplit* Clone( SfxItemPool *pPool = nullptr ) const override;
    virtual bool GetPresentation( SfxItemPresentation ePres,
                                  MapUnit eCoreMetric,
                                  MapUnit ePresMetric,
                                  OUString &rText,
                                  const IntlWrapper& rIntl ) const override;
};

inline const SwFormatRowSplit &SwAttrSet::GetRowSplit(bool bInP) const
    { return Get( RES_ROW_SPLIT,bInP); }

inline const SwFormatRowSplit &SwFormat::GetRowSplit(bool bInP) const
    { return m_aSet.GetRowSplit(bInP); }

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
