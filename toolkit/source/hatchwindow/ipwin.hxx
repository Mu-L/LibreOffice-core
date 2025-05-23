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

#include <tools/gen.hxx>
#include <vcl/window.hxx>
#include <array>

/********************** SvResizeHelper ***********************************
*************************************************************************/
class SvResizeHelper
{
    Size        aBorder;
    tools::Rectangle   aOuter;
    short       nGrab; // -1 no Grab,  0 - 7, 8 = Move, see FillHandle...
    Point       aSelPos;
    /// Initial width/height ratio when starting drag
    std::optional<double>      mofStartingRatio;
public:
    SvResizeHelper();

    short GetGrab() const
    {
        return nGrab;
    }
    void SetBorderPixel(const Size & rBorderP)
    {
        aBorder = rBorderP;
    }
    void SetOuterRectPixel(const tools::Rectangle& rRect)
    {
        aOuter = rRect;
    }
                // Clockwise, start at upper left

    std::array<tools::Rectangle,8> FillHandleRectsPixel() const;
    std::array<tools::Rectangle,4> FillMoveRectsPixel() const;
    void        Draw(vcl::RenderContext& rRenderContext);
    void        InvalidateBorder( vcl::Window * );
    bool        SelectBegin( vcl::Window *, const Point & rPos, const bool bShiftPressed );
    short       SelectMove( vcl::Window * pWin, const Point & rPos, const bool bShiftPressed );
    Point       GetTrackPosPixel( const tools::Rectangle & rRect ) const;
    tools::Rectangle   GetTrackRectPixel( const Point & rTrackPos, const bool bShiftPressed ) const;
    void        ValidateRect( tools::Rectangle & rValidate ) const;
    bool        SelectRelease( vcl::Window *, const Point & rPos, tools::Rectangle & rOutPosSize, const bool bShiftPressed );
    void        Release( vcl::Window * pWin );
};

/********************** SvResizeWindow ***********************************
*************************************************************************/
class VCLXHatchWindow;
class SvResizeWindow : public vcl::Window
{
    PointerStyle    m_aOldPointer;
    short           m_nMoveGrab;  // last pointer type
    SvResizeHelper  m_aResizer;
    bool        m_bActive;

    VCLXHatchWindow* m_pWrapper;
public:
    SvResizeWindow( vcl::Window* pParent, VCLXHatchWindow* pWrapper );

    void    SetHatchBorderPixel( const Size & rSize );

    void    SelectMouse( const Point & rPos, const bool bShiftPressed );
    virtual void    MouseButtonUp( const MouseEvent & rEvt ) override;
    virtual void    MouseMove( const MouseEvent & rEvt ) override;
    virtual void    MouseButtonDown( const MouseEvent & rEvt ) override;
    virtual void    KeyInput( const KeyEvent & rEvt ) override;
    virtual void    Resize() override;
    virtual void    Paint( vcl::RenderContext& /*rRenderContext*/, const tools::Rectangle & ) override;
    virtual bool    EventNotify( NotifyEvent& rNEvt ) override;
    virtual bool    PreNotify( NotifyEvent& rNEvt ) override;
};


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
