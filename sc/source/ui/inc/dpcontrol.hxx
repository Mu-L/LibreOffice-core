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

#include <rtl/ustring.hxx>
#include <tools/gen.hxx>
#include <tools/fract.hxx>
#include <vcl/vclptr.hxx>
#include <vcl/outdev.hxx>

class StyleSettings;
class ScDocument;

/**
 * This class takes care of physically drawing field button controls inside
 * data pilot tables.
 */
class ScDPFieldButton
{
public:
    ScDPFieldButton(OutputDevice* pOutDev, const StyleSettings& rStyle, const Fraction& rZoomY, ScDocument& rDoc);
    ~ScDPFieldButton();

    void setText(const OUString& rText);
    void setBoundingBox(const Point& rPos, const Size& rSize, bool bLayoutRTL);
    void setDrawBaseButton(bool b);
    void setDrawPopupButton(bool b);
    void setDrawPopupButtonMulti(bool b);
    void setDrawToggleButton(bool b, bool bCollapse, sal_Int32 nIndent);
    void setHasHiddenMember(bool b);
    void setPopupPressed(bool b);
    void setPopupLeft(bool b);
    void draw();

    void getPopupBoundingBox(Point& rPos, Size& rSize) const;
    void getToggleBoundingBox(Point& rPos, Size& rSize) const;

private:
    void drawPopupButton();
    void drawToggleButton();

private:
    Point                   maPos;
    Size                    maSize;
    OUString         maText;
    Fraction                maZoomY;
    ScDocument&             mrDoc;
    VclPtr<OutputDevice>    mpOutDev;
    const StyleSettings&    mrStyle;
    sal_Int32               mnToggleIndent;
    bool                    mbBaseButton;
    bool                    mbPopupButton;
    bool                    mbPopupButtonMulti;
    bool                    mbToggleButton;
    bool                    mbToggleCollapse;
    bool                    mbHasHiddenMember;
    bool                    mbPopupPressed;
    bool                    mbPopupLeft;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
