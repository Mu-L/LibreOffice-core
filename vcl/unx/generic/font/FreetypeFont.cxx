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

#include <sal/config.h>

#include <unx/font/FreetypeFont.hxx>
#include <unx/font/FreetypeFontFace.hxx>
#include <unx/font/FreetypeFontList.hxx>
#include <unx/font/fontmanager.hxx>

#include <font/LogicalFontInstance.hxx>
#include <fontattributes.hxx>

#include <sal/log.hxx>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SIZES_H

FT_Face FreetypeFont::GetFtFace() const
{
    FT_Activate_Size( maSizeFT );

    return maFaceFT;
}

// FreetypeFont

FreetypeFont::FreetypeFont(const FreetypeFontFace& rFontFace, const vcl::font::FontSelectPattern& rFSD)
:   LogicalFontInstance(rFontFace, rFSD),
    mnPrioAntiAlias(GetDefaultAntiAliasPrio()),
    maFaceFT( nullptr ),
    maSizeFT( nullptr ),
    mbFaceOk( false )
{
    maFaceFT = rFontFace.GetFaceFT();

    // set the pixel size of the font instance
    mnWidth = rFSD.mnWidth;
    if( !mnWidth )
        mnWidth = rFSD.mnHeight;
    if (rFSD.mnHeight == 0)
    {
        SAL_WARN("vcl", "FreetypeFont divide by zero");
        mfStretch = 1.0;
        return;
    }
    mfStretch = static_cast<double>(mnWidth) / rFSD.mnHeight;
    // sanity checks (e.g. #i66394#, #i66244#, #i66537#)
    if (mnWidth < 0)
    {
        SAL_WARN("vcl", "FreetypeFont negative font width of: " << mnWidth);
        return;
    }

    SAL_WARN_IF(mfStretch > +64.0 || mfStretch < -64.0, "vcl", "FreetypeFont excessive stretch of: " << mfStretch);

    if( !maFaceFT )
        return;

    FT_New_Size( maFaceFT, &maSizeFT );
    FT_Activate_Size( maSizeFT );
    /* This might fail for color bitmap fonts, but that is fine since we will
     * not need any glyph data from FreeType in this case */
    /*FT_Error rc = */ FT_Set_Pixel_Sizes( maFaceFT, mnWidth, rFSD.mnHeight );

    mbFaceOk = true;
}

namespace
{
    std::unique_ptr<FontConfigFontOptions> GetFCFontOptions(const FontAttributes& rFontAttributes, int nSize)
    {
        return FontConfigManager::getFontOptions(rFontAttributes, nSize);
    }
}

const FontConfigFontOptions* FreetypeFont::GetFontOptions() const
{
    if (!mxFontOptions)
    {
        const FreetypeFontFace* pFontFace = GetFontFace();
        mxFontOptions = GetFCFontOptions(*pFontFace, GetFontSelectPattern().mnHeight);
        mxFontOptions->SyncPattern(pFontFace->GetFontFileName(), pFontFace->GetFontFaceIndex(),
                                   pFontFace->GetFontFaceVariation(), NeedsArtificialBold(),
                                   GetVariations());
    }
    return mxFontOptions.get();
}

FreetypeFont::~FreetypeFont()
{
    if( maSizeFT )
        FT_Done_Size( maSizeFT );

    GetFontFace()->ReleaseFaceFT();
}

bool FreetypeFont::GetAntialiasAdvice() const
{
    // TODO: also use GASP info
    return !GetFontSelectPattern().mbNonAntialiased && (mnPrioAntiAlias > 0);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
