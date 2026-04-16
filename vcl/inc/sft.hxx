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

/**
 * @file sft.hxx
 * @brief Sun Font Tools
 */

/*
 *        Generated fonts contain an XUID entry in the form of:
 *
 *                  103 0 T C1 N C2 C3
 *
 *        103 - Sun's Adobe assigned XUID number. Contact person: Alexander Gelfenbain <gelf@eng.sun.com>
 *
 *        T  - font type. 0: Type 3, 1: Type 42
 *        C1 - CRC-32 of the entire source TrueType font
 *        N  - number of glyphs in the subset
 *        C2 - CRC-32 of the array of glyph IDs used to generate the subset
 *        C3 - CRC-32 of the array of encoding numbers used to generate the subset
 *
 */

#pragma once

#include <config_options.h>
#include <rtl/ustring.hxx>
#include <tools/fontenum.hxx>
#include <vcl/dllapi.h>
#include <i18nlangtag/lang.h>
#include <i18nlangtag/languagetag.hxx>

#include <hb-ot.h>

#include <vector>
#include "font/PhysicalFontFace.hxx"
#include "fontsubset.hxx"

namespace vcl
{

#ifndef FW_THIN /* WIN32 compilation would conflict */
/** Value of the weight member of the TTGlobalFontInfo struct */
    enum WeightClass {
        FW_THIN = 100,                      /**< Thin                               */
        FW_EXTRALIGHT = 200,                /**< Extra-light (Ultra-light)          */
        FW_LIGHT = 300,                     /**< Light                              */
        FW_NORMAL = 400,                    /**< Normal (Regular)                   */
        FW_MEDIUM = 500,                    /**< Medium                             */
        FW_SEMIBOLD = 600,                  /**< Semi-bold (Demi-bold)              */
        FW_BOLD = 700,                      /**< Bold                               */
        FW_EXTRABOLD = 800,                 /**< Extra-bold (Ultra-bold)            */
        FW_BLACK = 900                      /**< Black (Heavy)                      */
    };
#endif /* FW_THIN */

/** Value of the width member of the TTGlobalFontInfo struct */
    enum WidthClass {
        FWIDTH_ULTRA_CONDENSED = 1,         /**< 50% of normal                      */
        FWIDTH_EXTRA_CONDENSED = 2,         /**< 62.5% of normal                    */
        FWIDTH_CONDENSED = 3,               /**< 75% of normal                      */
        FWIDTH_SEMI_CONDENSED = 4,          /**< 87.5% of normal                    */
        FWIDTH_NORMAL = 5,                  /**< Medium, 100%                       */
        FWIDTH_SEMI_EXPANDED = 6,           /**< 112.5% of normal                   */
        FWIDTH_EXPANDED = 7,                /**< 125% of normal                     */
        FWIDTH_EXTRA_EXPANDED = 8,          /**< 150% of normal                     */
        FWIDTH_ULTRA_EXPANDED = 9           /**< 200% of normal                     */
    };

/** Return value of TrueTypeFont::getGlobalFontInfo() */

    typedef struct TTGlobalFontInfo_ {
        OUString   family;            /**< family name                                             */
        OUString   subfamily;         /**< subfamily name                                          */
        sal_uInt16 macStyle = 0;      /**< macstyle bits from 'HEAD' table */
        int   weight = 0;             /**< value of WeightClass or 0 if can't be determined        */
        int   width = 0;              /**< value of WidthClass or 0 if can't be determined         */
        int   pitch = 0;              /**< 0: proportional font, otherwise: monospaced             */
        int   italicAngle = 0;        /**< in counter-clockwise degrees * 65536                    */
        bool  microsoftSymbolEncoded = false;  /**< true: MS symbol encoded */
        sal_uInt32 typeFlags = 0;     /**< type flags (copyright bits)                             */
    } TTGlobalFontInfo;



/*
  Some table OS/2 consts
  quick history:
  OpenType has been created from TrueType
  - original TrueType had an OS/2 table with a length of 68 bytes
  (cf https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6OS2.html)
  - There have been 6 versions (from version 0 to 5)
  (cf https://docs.microsoft.com/en-us/typography/opentype/otspec140/os2ver0)

  For the record:
  // From Initial TrueType version
  TYPE       NAME                       FROM BYTE
  uint16     version                    0
  int16      xAvgCharWidth              2
  uint16     usWeightClass              4
  uint16     usWidthClass               6
  uint16     fsType                     8
  int16      ySubscriptXSize           10
  int16      ySubscriptYSize           12
  int16      ySubscriptXOffset         14
  int16      ySubscriptYOffset         16
  int16      ySuperscriptXSize         18
  int16      ySuperscriptYSize         20
  int16      ySuperscriptXOffset       22
  int16      ySuperscriptYOffset       24
  int16      yStrikeoutSize            26
  int16      yStrikeoutPosition        28
  int16      sFamilyClass              30
  uint8      panose[10]                32
  uint32     ulUnicodeRange1           42
  uint32     ulUnicodeRange2           46
  uint32     ulUnicodeRange3           50
  uint32     ulUnicodeRange4           54
  Tag        achVendID                 58
  uint16     fsSelection               62
  uint16     usFirstCharIndex          64
  uint16     usLastCharIndex           66

  // From Version 0 of OpenType
  int16      sTypoAscender             68
  int16      sTypoDescender            70
  int16      sTypoLineGap              72
  uint16     usWinAscent               74
  uint16     usWinDescent              76

  => length for OpenType version 0 = 78 bytes

  // From Version 1 of OpenType
  uint32     ulCodePageRange1          78
  uint32     ulCodePageRange2          82

  => length for OpenType version 1 = 86 bytes

  // From Version 2 of OpenType
  // (idem for Versions 3 and 4)
  int16      sxHeight                  86
  int16      sCapHeight                88
  uint16     usDefaultChar             90
  uint16     usBreakChar               92
  uint16     usMaxContext              94

  => length for OpenType version 2, 3 and 4 = 96 bytes

  // From Version 5 of OpenType
  uint16     usLowerOpticalPointSize   96
  uint16     usUpperOpticalPointSize   98
  END                                 100

  => length for OS/2 table version 5 = 100 bytes

*/
constexpr int OS2_Legacy_length = 68;
constexpr int OS2_V1_length = 86;

constexpr int OS2_usWeightClass_offset = 4;
constexpr int OS2_usWidthClass_offset = 6;
constexpr int OS2_fsType_offset = 8;
constexpr int OS2_ulUnicodeRange1_offset = 42;
constexpr int OS2_ulUnicodeRange2_offset = 46;
constexpr int OS2_ulUnicodeRange3_offset = 50;
constexpr int OS2_ulUnicodeRange4_offset = 54;
constexpr int OS2_fsSelection_offset = 62;
constexpr int OS2_ulCodePageRange1_offset = 78;
constexpr int OS2_ulCodePageRange2_offset = 82;

/*
  Some table hhea consts
  cf https://docs.microsoft.com/fr-fr/typography/opentype/spec/hhea
  TYPE       NAME                       FROM BYTE
  uint16     majorVersion               0
  uint16     minorVersion               2
  FWORD      ascender                   4
  FWORD      descender                  6
  FWORD      lineGap                    8
  UFWORD     advanceWidthMax           10
  FWORD      minLeftSideBearing        12
  FWORD      minRightSideBearing       14
  FWORD      xMaxExtent                16
  int16      caretSlopeRise            18
  int16      caretSlopeRun             20
  int16      caretOffset               22
  int16      (reserved)                24
  int16      (reserved)                26
  int16      (reserved)                28
  int16      (reserved)                30
  int16      metricDataFormat          32
  uint16     numberOfHMetrics          34
  END                                  36

  => length for hhea table = 36 bytes

*/
constexpr int HHEA_Length = 36;

constexpr int HHEA_ascender_offset = 4;
constexpr int HHEA_descender_offset = 6;
constexpr int HHEA_lineGap_offset = 8;
constexpr int HHEA_caretSlopeRise_offset = 18;
constexpr int HHEA_caretSlopeRun_offset = 20;

/*
  Some table post consts
  cf https://docs.microsoft.com/fr-fr/typography/opentype/spec/post
  TYPE       NAME                       FROM BYTE
  Fixed      version                    0
  Fixed      italicAngle                4
  FWord      underlinePosition          8
  FWord      underlineThickness        10
  uint32     isFixedPitch              12
  ...

*/
constexpr int POST_italicAngle_offset = 4;
constexpr int POST_underlinePosition_offset = 8;
constexpr int POST_underlineThickness_offset = 10;
constexpr int POST_isFixedPitch_offset = 12;

/*
  Some table head consts
  cf https://docs.microsoft.com/fr-fr/typography/opentype/spec/head
  TYPE       NAME                       FROM BYTE
  uit16      majorVersion               0
  uit16      minorVersion               2
  Fixed      fontRevision               4
  uint32     checkSumAdjustment         8
  uint32     magicNumber               12 (= 0x5F0F3CF5)
  uint16     flags                     16
  uint16     unitsPerEm                18
  LONGDATETIME created                 20
  LONGDATETIME modified                28
  int16      xMin                      36
  int16      yMin                      38
  int16      xMax                      40
  int16      yMax                      42
  uint16     macStyle                  44
  uint16     lowestRecPPEM             46
  int16      fontDirectionHint         48
  int16      indexToLocFormat          50
  int16      glyphDataFormat           52

  END                                  54

  => length head table = 54 bytes
*/
constexpr int HEAD_Length = 54;

constexpr int HEAD_majorVersion_offset = 0;
constexpr int HEAD_fontRevision_offset = 4;
constexpr int HEAD_magicNumber_offset = 12;
constexpr int HEAD_flags_offset = 16;
constexpr int HEAD_unitsPerEm_offset = 18;
constexpr int HEAD_created_offset = 20;
constexpr int HEAD_xMin_offset = 36;
constexpr int HEAD_yMin_offset = 38;
constexpr int HEAD_xMax_offset = 40;
constexpr int HEAD_yMax_offset = 42;
constexpr int HEAD_macStyle_offset = 44;
constexpr int HEAD_lowestRecPPEM_offset = 46;
constexpr int HEAD_fontDirectionHint_offset = 48;
constexpr int HEAD_indexToLocFormat_offset = 50;
constexpr int HEAD_glyphDataFormat_offset = 52;

/*
  Some table maxp consts
  cf https://docs.microsoft.com/fr-fr/typography/opentype/spec/maxp
  For 0.5 version
  TYPE       NAME                       FROM BYTE
  Fixed      version                    0
  uint16     numGlyphs                  4

  For 1.0 Version
  Fixed      version                    0
  uint16     numGlyphs                  4
  uint16     maxPoints                  6
  uint16     maxContours                8
  uint16     maxCompositePoints        10
  uint16     maxCompositeContours      12
  ...

*/
constexpr int MAXP_Version1Length = 32;

constexpr int MAXP_numGlyphs_offset = 4;
constexpr int MAXP_maxPoints_offset = 6;
constexpr int MAXP_maxContours_offset = 8;
constexpr int MAXP_maxCompositePoints_offset = 10;
constexpr int MAXP_maxCompositeContours_offset = 12;

/*
  Some table glyf consts
  cf https://docs.microsoft.com/fr-fr/typography/opentype/spec/glyf
  For 0.5 version
  TYPE       NAME                       FROM BYTE
  int16      numberOfContours           0
  int16      xMin                       2
  int16      yMin                       4
  int16      xMax                       6
  int16      yMax                       8

  END                                  10

  => length glyf table = 10 bytes

*/
constexpr int GLYF_Length = 10;

constexpr int GLYF_numberOfContours_offset = 0;
constexpr int GLYF_xMin_offset = 2;
constexpr int GLYF_yMin_offset = 4;
constexpr int GLYF_xMax_offset = 6;
constexpr int GLYF_yMax_offset = 8;

class UNLESS_MERGELIBS(VCL_DLLPUBLIC) TrueTypeFont
{
    hb_face_t* m_pFace = nullptr;
    mutable hb_font_t* m_pFont = nullptr;

    hb_font_t* getFont() const;
    font::RawFontData getTable(hb_tag_t tag) const;

public:
    TrueTypeFont(const void* pBuffer, sal_uInt32 nLen, sal_uInt32 facenum);
    ~TrueTypeFont();

    bool isValid() const { return m_pFace != hb_face_get_empty(); }

    OUString getName(hb_ot_name_id_t nNameID, const LanguageTag& rLang = LanguageTag(LANGUAGE_DONTKNOW)) const;

    TTGlobalFontInfo getGlobalFontInfo() const;
    sal_uInt32 countNonEmptyGlyphs() const;
    FontWeight analyzeFontWeight() const;
};

} // namespace vcl

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
