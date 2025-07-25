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

#include <sal/config.h>

#include <font/PhysicalFontFace.hxx>
#include <font/LogicalFontInstance.hxx>

namespace vcl::pdf
{
struct BuildinFont
{
    OUString m_aName;
    OUString m_aStyleName;
    const char* m_pPSName;
    int const m_nAscent;
    int const m_nDescent;
    FontFamily const m_eFamily;
    rtl_TextEncoding const m_eCharSet;
    FontPitch const m_ePitch;
    FontWidth const m_eWidthType;
    FontWeight const m_eWeight;
    FontItalic const m_eItalic;
    int const m_aWidths[256];

    OString getNameObject() const;
    FontAttributes GetFontAttributes() const;
};

class BuildinFontInstance final : public LogicalFontInstance
{
public:
    BuildinFontInstance(const vcl::font::PhysicalFontFace&, const vcl::font::FontSelectPattern&);

    bool GetGlyphOutline(sal_GlyphId nId, basegfx::B2DPolyPolygon& rPoly, bool) const override;
};

class BuildinFontFace final : public vcl::font::PhysicalFontFace
{
    static const BuildinFont m_aBuildinFonts[14];
    const BuildinFont& mrBuildin;
    mutable FontCharMapRef m_xFontCharMap;

    rtl::Reference<LogicalFontInstance>
    CreateFontInstance(const vcl::font::FontSelectPattern& rFSD) const override;

public:
    explicit BuildinFontFace(int nId);

    const BuildinFont& GetBuildinFont() const { return mrBuildin; }
    sal_IntPtr GetFontId() const override { return reinterpret_cast<sal_IntPtr>(&mrBuildin); }
    FontCharMapRef GetFontCharMap() const override;
    bool GetFontCapabilities(vcl::FontCapabilities&) const override { return false; }

    static const BuildinFont& Get(int nId) { return m_aBuildinFonts[nId]; }
};

} // namespace vcl::pdf

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
