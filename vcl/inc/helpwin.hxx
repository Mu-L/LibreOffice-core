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

#include <vcl/toolkit/floatwin.hxx>
#include <vcl/timer.hxx>

enum class QuickHelpFlags;
struct ImplSVHelpData;

enum class HelpWinStyle
{
    Quick,
    Balloon,
};

/// A tooltip: adds tips to widgets in a floating / popup window.
class HelpTextWindow final : public FloatingWindow
{
private:
    tools::Rectangle           maHelpArea; // If next Help for the same rectangle w/ same text, then keep window

    tools::Rectangle           maTextRect; // For wrapped text in QuickHelp

    OUString            maHelpText;

    Timer               maShowTimer;
    Timer               maHideTimer;

    HelpWinStyle        meHelpWinStyle;
    QuickHelpFlags      mnStyle;

private:
    DECL_LINK( TimerHdl, Timer*, void );

    virtual void Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle&) override;
    virtual void RequestHelp( const HelpEvent& rHEvt ) override;
    virtual void ApplySettings(vcl::RenderContext& rRenderContext) override;

    virtual OUString GetText() const override;
    void ImplShow();

    virtual void        dispose() override;
public:
    HelpTextWindow(vcl::Window* pParent, const OUString& rText, HelpWinStyle eHelpWinStyle,
                   QuickHelpFlags nStyle);
    virtual             ~HelpTextWindow() override;

    const OUString&     GetHelpText() const { return maHelpText; }
    void                SetHelpText( const OUString& rHelpText );
    HelpWinStyle GetWinStyle() const { return meHelpWinStyle; }
    QuickHelpFlags      GetStyle() const { return mnStyle; }

    // only remember:
    void                SetHelpArea( const tools::Rectangle& rRect ) { maHelpArea = rRect; }

    void                ShowHelp(bool bNoDelay);

    Size                CalcOutSize() const;
    const tools::Rectangle&    GetHelpArea() const { return maHelpArea; }

    void ResetHideTimer();
};

VCL_DLLPUBLIC void ImplDestroyHelpWindow( bool bUpdateHideTime );
void ImplDestroyHelpWindow(ImplSVHelpData& rHelpData, bool bUpdateHideTime);

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
