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

#include <salobj.hxx>


class WinSalObject : public SalObject
{
public:
    HWND                    mhWnd;                  // Window handle
    HWND                    mhWndChild;             // Child Window handle
    HWND                    mhLastFocusWnd;         // Child-Window, which had the last focus
    SystemEnvData           maSysData;              // SystemEnvData
    RGNDATA*                mpClipRgnData;          // ClipRegion-Data
    RGNDATA*                mpStdClipRgnData;       // Cache Standard-ClipRegion-Data
    RECT*                   mpNextClipRect;         // next ClipRegion-Rect
    bool                    mbFirstClipRect;        // Flag for first cliprect to insert
    WinSalObject*           mpNextObject;           // pointer to next object

    WinSalObject();
    virtual ~WinSalObject() override;

    virtual void                    ResetClipRegion() override;
    virtual void                    BeginSetClipRegion( sal_uInt32 nRects ) override;
    virtual void                    UnionClipRegion( tools::Long nX, tools::Long nY, tools::Long nWidth, tools::Long nHeight) override;
    virtual void                    EndSetClipRegion() override;
    virtual void                    SetPosSize( tools::Long nX, tools::Long nY, tools::Long nWidth, tools::Long nHeight ) override;
    virtual void                    Show( bool bVisible ) override;
    virtual void                    Enable( bool bEnable ) override;
    virtual void                    GrabFocus() override;
    virtual const SystemEnvData&    GetSystemData() const override;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
