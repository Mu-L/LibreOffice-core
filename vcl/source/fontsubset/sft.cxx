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
#include <sal/log.hxx>
#include <tools/stream.hxx>
#include <o3tl/safeint.hxx>
#include <o3tl/string_view.hxx>
#include <osl/endian.h>
#include <osl/thread.h>
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

static sal_Int16 GetInt16(const sal_uInt8* ptr, size_t offset)
{
    return static_cast<sal_Int16>(GetUInt16(ptr, offset));
}

static sal_uInt32 GetUInt32(const sal_uInt8 *ptr, size_t offset)
{
    sal_uInt32 t;
    assert(ptr != nullptr);

    t = (ptr+offset)[0] << 24 | (ptr+offset)[1] << 16 |
        (ptr+offset)[2] << 8  | (ptr+offset)[3];

    return t;
}

static sal_Int32 GetInt32(const sal_uInt8* ptr, size_t offset)
{
    return static_cast<sal_Int32>(GetUInt32(ptr, offset));
}


/*- Public functions */

int CountTTCFonts(const char* fname)
{
    hb_blob_t* pBlob = hb_blob_create_from_file_or_fail(fname);
    if (!pBlob)
        return 0;
    unsigned int nFaces = hb_face_count(pBlob);
    hb_blob_destroy(pBlob);
    return nFaces;
}

#if !defined(_WIN32) || defined(DO_USE_TTF_ON_WIN32)
SFErrCodes OpenTTFontFile(const char* fname, sal_uInt32 facenum, TrueTypeFont** ttf)
{
    if (!fname || !*fname)
        return SFErrCodes::BadFile;

    hb_blob_t* pBlob = hb_blob_create_from_file_or_fail(fname);
    if (!pBlob)
        return SFErrCodes::BadFile;

    *ttf = new TrueTypeFont();
    SFErrCodes ret = (*ttf)->open(pBlob, facenum);
    hb_blob_destroy(pBlob);
    if (ret != SFErrCodes::Ok)
    {
        delete *ttf;
        *ttf = nullptr;
    }
    return ret;
}
#endif

SFErrCodes OpenTTFontBuffer(const void* pBuffer, sal_uInt32 nLen, sal_uInt32 facenum, TrueTypeFont** ttf)
{
    *ttf = new TrueTypeFont();
    if( *ttf == nullptr )
        return SFErrCodes::Memory;

    hb_blob_t* pBlob = hb_blob_create(static_cast<const char*>(pBuffer), nLen,
                                       HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    SFErrCodes ret = (*ttf)->open(pBlob, facenum);
    hb_blob_destroy(pBlob);
    if (ret != SFErrCodes::Ok)
    {
        delete *ttf;
        *ttf = nullptr;
    }
    return ret;
}

TrueTypeFont::TrueTypeFont()
    : m_nGlyphs(0xFFFFFFFF)
    , m_bMicrosoftSymbolEncoded(false)
{
}

TrueTypeFont::~TrueTypeFont()
{
    if (m_pFace)
        hb_face_destroy(m_pFace);
}

font::RawFontData TrueTypeFont::getTable(hb_tag_t tag) const
{
    return font::RawFontData(hb_face_reference_table(m_pFace, tag));
}

void CloseTTFont(TrueTypeFont* ttf) { delete ttf; }

sal_uInt32 TrueTypeFont::glyphOffset(sal_uInt32 glyphID) const
{
    if (m_aGlyphOffsets.empty()) // the T_CFF and Bitmap cases
        return 0;
    return m_aGlyphOffsets[glyphID];
}

SFErrCodes TrueTypeFont::indexGlyphData()
{
    auto aMaxp = getTable(T_maxp);
    auto aHead = getTable(T_head);
    auto aName = getTable(T_name);
    auto aCmap = getTable(T_cmap);
    if (aMaxp.empty() || aHead.empty() || aName.empty() || aCmap.empty())
        return SFErrCodes::TtFormat;

    m_nGlyphs = aMaxp.size() >= 6 ? GetUInt16(aMaxp.data(), 4) : 0;

    if (aHead.size() < HEAD_Length)
        return SFErrCodes::TtFormat;

    sal_uInt32 unitsPerEm = GetUInt16(aHead.data(), HEAD_unitsPerEm_offset);
    int indexfmt = GetInt16(aHead.data(), HEAD_indexToLocFormat_offset);

    if (((indexfmt != 0) && (indexfmt != 1)) || (unitsPerEm <= 0))
        return SFErrCodes::TtFormat;

    auto aGlyf = getTable(T_glyf);
    auto aLoca = getTable(T_loca);
    auto aCFF = getTable(T_CFF);
    if (!aGlyf.empty() && !aLoca.empty()) /* TTF or TTF-OpenType */
    {
        int k = (aLoca.size() / (indexfmt ? 4 : 2)) - 1;
        if (k < static_cast<int>(m_nGlyphs))       /* Hack for broken Chinese fonts */
            m_nGlyphs = k;

        m_aGlyphOffsets.clear();
        m_aGlyphOffsets.reserve(m_nGlyphs + 1);
        for (int i = 0; i <= static_cast<int>(m_nGlyphs); ++i)
            m_aGlyphOffsets.push_back(indexfmt ? GetUInt32(aLoca.data(), i << 2) : static_cast<sal_uInt32>(GetUInt16(aLoca.data(), i << 1)) << 1);
    }
    else if (!aCFF.empty()) /* PS-OpenType */
    {
        int k = (aCFF.size() / 2) - 1; /* set a limit here, presumably much lower than the table size, but establishes some sort of physical bound */
        if (k < static_cast<int>(m_nGlyphs))
            m_nGlyphs = k;

        m_aGlyphOffsets.clear();
    }
    else {
        // Bitmap font, accept for now.
        m_aGlyphOffsets.clear();
    }

    m_bMicrosoftSymbolEncoded = HasMicrosoftSymbolCmap(aCmap.data(), aCmap.size());

    return SFErrCodes::Ok;
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

SFErrCodes TrueTypeFont::open(hb_blob_t* pBlob, sal_uInt32 facenum)
{
    m_pFace = hb_face_create_or_fail(pBlob, facenum);

    if (!m_pFace)
        return SFErrCodes::TtFormat;

    return indexGlyphData();
}

void GetTTGlobalFontInfo(const TrueTypeFont *ttf, TTGlobalFontInfo *info)
{
    info->family = ttf->getName(HB_OT_NAME_ID_FONT_FAMILY);
    if (info->family.isEmpty())
        info->family = ttf->getName(HB_OT_NAME_ID_POSTSCRIPT_NAME);
    info->subfamily = ttf->getName(HB_OT_NAME_ID_FONT_SUBFAMILY);
    info->microsoftSymbolEncoded = ttf->IsMicrosoftSymbolEncoded();

    auto aOS2 = ttf->getTable(T_OS2);
    if (aOS2.size() >= 42)
    {
        info->weight = GetUInt16(aOS2.data(), OS2_usWeightClass_offset);
        info->width  = GetUInt16(aOS2.data(), OS2_usWidthClass_offset);
        info->typeFlags = GetUInt16(aOS2.data(), OS2_fsType_offset);
    }

    auto aPost = ttf->getTable(T_post);
    if (aPost.size() >= 12 + sizeof(sal_uInt32))
    {
        info->pitch  = GetUInt32(aPost.data(), POST_isFixedPitch_offset);
        info->italicAngle = GetInt32(aPost.data(), POST_italicAngle_offset);
    }

    auto aHead = ttf->getTable(T_head);
    if (aHead.size() >= 46)
        info->macStyle = GetUInt16(aHead.data(), HEAD_macStyle_offset);
}



template<size_t N> static void
append(std::bitset<N> & rSet, size_t const nOffset, sal_uInt32 const nValue)
{
    for (size_t i = 0; i < 32; ++i)
    {
        rSet.set(nOffset + i, (nValue & (1U << i)) != 0);
    }
}

bool getTTCoverage(
    std::optional<std::bitset<UnicodeCoverage::MAX_UC_ENUM>> &rUnicodeRange,
    std::optional<std::bitset<CodePageCoverage::MAX_CP_ENUM>> &rCodePageRange,
    const unsigned char* pTable, size_t nLength)
{
    bool bRet = false;
    // parse OS/2 header
    if (nLength >= OS2_Legacy_length)
    {
        rUnicodeRange = std::bitset<UnicodeCoverage::MAX_UC_ENUM>();
        append(*rUnicodeRange,  0, GetUInt32(pTable, OS2_ulUnicodeRange1_offset));
        append(*rUnicodeRange, 32, GetUInt32(pTable, OS2_ulUnicodeRange2_offset));
        append(*rUnicodeRange, 64, GetUInt32(pTable, OS2_ulUnicodeRange3_offset));
        append(*rUnicodeRange, 96, GetUInt32(pTable, OS2_ulUnicodeRange4_offset));
        bRet = true;
        if (nLength >= OS2_V1_length)
        {
            rCodePageRange = std::bitset<CodePageCoverage::MAX_CP_ENUM>();
            append(*rCodePageRange,  0, GetUInt32(pTable, OS2_ulCodePageRange1_offset));
            append(*rCodePageRange, 32, GetUInt32(pTable, OS2_ulCodePageRange2_offset));
        }
    }
    return bRet;
}

static FontWeight ImplWeightToSal( int nWeight )
{
    if ( nWeight <= FW_THIN )
        return WEIGHT_THIN;
    else if ( nWeight <= FW_EXTRALIGHT )
        return WEIGHT_ULTRALIGHT;
    else if ( nWeight <= FW_LIGHT )
        return WEIGHT_LIGHT;
    else if ( nWeight < FW_MEDIUM )
        return WEIGHT_NORMAL;
    else if ( nWeight == FW_MEDIUM )
        return WEIGHT_MEDIUM;
    else if ( nWeight <= FW_SEMIBOLD )
        return WEIGHT_SEMIBOLD;
    else if ( nWeight <= FW_BOLD )
        return WEIGHT_BOLD;
    else if ( nWeight <= FW_EXTRABOLD )
        return WEIGHT_ULTRABOLD;
    else
        return WEIGHT_BLACK;
}

FontWeight AnalyzeTTFWeight(const TrueTypeFont* ttf)
{
    auto aOS2 = ttf->getTable(T_OS2);
    if (aOS2.size() >= 42)
    {
        sal_uInt16 weightOS2 = GetUInt16(aOS2.data(), OS2_usWeightClass_offset);
        return ImplWeightToSal(weightOS2);
    }

    // Fallback to inferring from the style name (name ID 2).
    OUString sStyle = ttf->getName(HB_OT_NAME_ID_FONT_SUBFAMILY);

    bool bBold(false), bItalic(false);
    if (o3tl::equalsIgnoreAsciiCase(sStyle, u"Regular"))
    {
        bBold = false;
        bItalic = false;
    }
    else if (o3tl::equalsIgnoreAsciiCase(sStyle, u"Bold"))
        bBold = true;
    else if (o3tl::equalsIgnoreAsciiCase(sStyle, u"Bold Italic"))
    {
        bBold = true;
        bItalic = true;
    }
    else if (o3tl::equalsIgnoreAsciiCase(sStyle, u"Italic"))
    {
        bItalic = true;
    }
    else
    {
        SAL_WARN("vcl.fonts", "Unhandled font style: " << sStyle);
    }

    (void)bItalic; // we might need to use this in a similar scenario where
                   // italic cannot be found

    return bBold ? WEIGHT_BOLD : WEIGHT_NORMAL;
}

} // namespace vcl


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
