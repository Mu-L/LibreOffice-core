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

/*
 * Sun Font Tools
 *
 * Author: Alexander Gelfenbain
 *
 */

#include <assert.h>

#include <stdlib.h>
#include <string.h>
#include <hb-ot.h>
#include <sft.hxx>
#include <font/TTFStructure.hxx>
#include <impfontcharmap.hxx>
#ifdef SYSTEM_LIBFIXMATH
#include <libfixmath/fix16.hpp>
#else
#include <tools/fix16.hxx>
#endif
#include <i18nlangtag/languagetag.hxx>
#include <rtl/crc.h>
#include <rtl/ustring.hxx>
#include <rtl/ustrbuf.hxx>
#include <tools/stream.hxx>
#include <o3tl/safeint.hxx>
#include <osl/endian.h>
#include <unotools/tempfile.hxx>
#include <fontsubset.hxx>

namespace vcl
{

namespace {

/*- In horizontal writing mode right sidebearing is calculated using this formula
 *- rsb = aw - (lsb + xMax - xMin) -*/

}

/*- Data access methods for data stored in big-endian format */
static sal_uInt16 GetUInt16(const sal_uInt8 *ptr, size_t offset)
{
    sal_uInt16 t;
    assert(ptr != nullptr);

    t = (ptr+offset)[0] << 8 | (ptr+offset)[1];

    return t;
}

static sal_uInt32 GetUInt32(const sal_uInt8 *ptr, size_t offset)
{
    sal_uInt32 t;
    assert(ptr != nullptr);

    t = (ptr+offset)[0] << 24 | (ptr+offset)[1] << 16 |
        (ptr+offset)[2] << 8  | (ptr+offset)[3];

    return t;
}

/*- Public functions */

TrueTypeFont::TrueTypeFont(const void* pBuffer, sal_uInt32 nLen, sal_uInt32 facenum)
{
    hb_blob_t* pBlob = hb_blob_create(static_cast<const char*>(pBuffer), nLen,
                                       HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    m_pFace = hb_face_create(pBlob, facenum);
    hb_blob_destroy(pBlob);
}

TrueTypeFont::~TrueTypeFont()
{
    hb_face_destroy(m_pFace);
    hb_font_destroy(m_pFont);
}

hb_font_t* TrueTypeFont::getFont() const
{
    if (!m_pFont)
        m_pFont = hb_font_create(m_pFace);
    return m_pFont;
}

font::RawFontData TrueTypeFont::getTable(hb_tag_t tag) const
{
    return font::RawFontData(hb_face_reference_table(m_pFace, tag));
}

sal_uInt32 TrueTypeFont::countNonEmptyGlyphs() const
{
    hb_font_t* pFont = getFont();
    sal_uInt32 nGlyphs = hb_face_get_glyph_count(m_pFace);
    sal_uInt32 nCount = 0;
    for (sal_uInt32 i = 0; i < nGlyphs; ++i)
    {
        hb_glyph_extents_t aExtents;
        if (hb_font_get_glyph_extents(pFont, i, &aExtents)
            && (aExtents.width || aExtents.height))
            ++nCount;
    }
    return nCount;
}

OUString TrueTypeFont::getName(hb_ot_name_id_t nNameID, const LanguageTag& rLang) const
{
    hb_language_t aHbLang = HB_LANGUAGE_INVALID;
    if (rLang.getLanguageType() != LANGUAGE_DONTKNOW)
    {
        auto aLanguage(rLang.getBcp47().toUtf8());
        aHbLang = hb_language_from_string(aLanguage.getStr(), aLanguage.getLength());
    }

    auto nName = hb_ot_name_get_utf16(m_pFace, nNameID, aHbLang, nullptr, nullptr);
    if (!nName)
        return OUString();
    std::vector<uint16_t> aBuf(++nName); // make space for terminating NUL
    hb_ot_name_get_utf16(m_pFace, nNameID, aHbLang, &nName, aBuf.data());
    return OUString(reinterpret_cast<sal_Unicode*>(aBuf.data()), nName);
}

OUString TrueTypeFont::getFamilyName() const
{
    OUString sFamily = getName(HB_OT_NAME_ID_FONT_FAMILY);
    if (sFamily.isEmpty())
        sFamily = getName(HB_OT_NAME_ID_POSTSCRIPT_NAME);
    return sFamily;
}

TTGlobalFontInfo TrueTypeFont::getGlobalFontInfo() const
{
    TTGlobalFontInfo info;

    info.subfamily = getName(HB_OT_NAME_ID_FONT_SUBFAMILY);

    auto aCmap = getTable(T_cmap);
    if (!aCmap.empty())
        info.microsoftSymbolEncoded = HasMicrosoftSymbolCmap(aCmap.data(), aCmap.size());

    auto aOS2 = getTable(T_OS2);
    if (aOS2.size() >= 42)
        info.typeFlags = GetUInt16(aOS2.data(), OS2_fsType_offset);

    auto aPost = getTable(T_post);
    if (aPost.size() >= 12 + sizeof(sal_uInt32))
        info.pitch  = GetUInt32(aPost.data(), POST_isFixedPitch_offset);

    auto aHead = getTable(T_head);
    if (aHead.size() >= 46)
        info.macStyle = GetUInt16(aHead.data(), HEAD_macStyle_offset);

    hb_font_t* pFont = getFont();
    info.weight = hb_style_get_value(pFont, HB_STYLE_TAG_WEIGHT);
    info.width = hb_style_get_value(pFont, HB_STYLE_TAG_WIDTH);
    info.italicAngle = hb_style_get_value(pFont, HB_STYLE_TAG_SLANT_ANGLE);

    return info;
}


static FontWeight ImplWeightToSal( float nWeight )
{
    if ( nWeight <= 100 )
        return WEIGHT_THIN;
    else if ( nWeight <= 200 )
        return WEIGHT_ULTRALIGHT;
    else if ( nWeight <= 300 )
        return WEIGHT_LIGHT;
    else if ( nWeight < 500 )
        return WEIGHT_NORMAL;
    else if ( nWeight == 500 )
        return WEIGHT_MEDIUM;
    else if ( nWeight <= 600 )
        return WEIGHT_SEMIBOLD;
    else if ( nWeight <= 700 )
        return WEIGHT_BOLD;
    else if ( nWeight <= 800 )
        return WEIGHT_ULTRABOLD;
    else
        return WEIGHT_BLACK;
}

FontWeight TrueTypeFont::analyzeFontWeight() const
{
    return ImplWeightToSal(hb_style_get_value(getFont(), HB_STYLE_TAG_WEIGHT));
}

} // namespace vcl


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
