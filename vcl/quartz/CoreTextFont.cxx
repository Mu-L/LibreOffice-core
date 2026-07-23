/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
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
#include <sal/log.hxx>

#ifdef MACOSX
#include <osx/saldata.hxx>
#include <osx/salinst.h>
#endif
#include <font/LogicalFontInstance.hxx>
#include <impglyphitem.hxx>
#include <quartz/CoreTextFont.hxx>
#include <quartz/CoreTextFontFace.hxx>
#include <quartz/salgdi.h>
#include <quartz/utils.h>
#include <hb.h>

CoreTextFont::CoreTextFont(const CoreTextFontFace& rPFF, const vcl::font::FontSelectPattern& rFSP)
    : LogicalFontInstance(rPFF, rFSP)
    , mfFontStretch(1.0)
    , mfFontRotation(0.0)
    , mpCTFont(nullptr)
{
    double fScaledFontHeight = rFSP.mfExactHeight;

    // convert font rotation to radian
    mfFontRotation = toRadians(rFSP.mnOrientation);

    // dummy matrix so we can use CGAffineTransformConcat() below
    CGAffineTransform aMatrix = CGAffineTransformMakeTranslation(0, 0);

    // handle font stretching if any
    if ((rFSP.mnWidth != 0) && (rFSP.mnWidth != rFSP.mnHeight))
    {
        mfFontStretch = float(rFSP.mnWidth) / rFSP.mnHeight;
        aMatrix = CGAffineTransformConcat(aMatrix, CGAffineTransformMakeScale(mfFontStretch, 1.0F));
    }

    // artificial italic
    if (NeedsArtificialItalic())
        aMatrix = CGAffineTransformConcat(
            aMatrix, CGAffineTransformMake(1, 0, ARTIFICIAL_ITALIC_SKEW, 1, 0, 0));

    CTFontDescriptorRef pFontDesc = rPFF.GetFontDescriptorRef();
    mpCTFont = CTFontCreateWithFontDescriptor(pFontDesc, fScaledFontHeight, &aMatrix);
}

CoreTextFont::~CoreTextFont()
{
    if (mpCTFont)
        CFRelease(mpCTFont);
}

void CoreTextFont::GetFontMetric(FontMetricDataRef const& rxFontMetric)
{
    rxFontMetric->ImplCalcLineSpacing(this);
    rxFontMetric->ImplInitBaselines(this);

    // since FontMetricData::mnWidth is only used for stretching/squeezing fonts
    // setting this width to the pixel height of the fontsize is good enough
    // it also makes the calculation of the stretch factor simple
    rxFontMetric->SetWidth(lrint(CTFontGetSize(mpCTFont) * mfFontStretch));

    rxFontMetric->SetMinKashida(GetKashidaWidth());
}

static void MyDestroyCFDataRef(void* pUserData)
{
    CFDataRef pData = static_cast<CFDataRef>(pUserData);
    if (pData)
        CFRelease(pData);
}

hb_blob_t* CoreTextFontFace::GetHbTable(hb_tag_t nTag) const
{
    hb_blob_t* pBlob = nullptr;
    CTFontRef pFont = CTFontCreateWithFontDescriptor(mxFontDescriptor, 0.0, nullptr);

    if (!nTag)
    {
        // If nTag is 0, the whole font data is requested. CoreText does not
        // give us that, so we will construct an HarfBuzz face from CoreText
        // table data and return the blob of that face.

        auto pTags = CTFontCopyAvailableTables(pFont, kCTFontTableOptionNoOptions);
        CFIndex nTags = pTags ? CFArrayGetCount(pTags) : 0;
        if (nTags > 0)
        {
            hb_face_t* pHbFace = hb_face_builder_create();
            for (CFIndex i = 0; i < nTags; i++)
            {
                auto nTable = reinterpret_cast<intptr_t>(CFArrayGetValueAtIndex(pTags, i));
                assert(nTable);
                auto pTable = GetHbTable(nTable);
                assert(pTable);
                hb_face_builder_add_table(pHbFace, nTable, pTable);
            }
            pBlob = hb_face_reference_blob(pHbFace);

            hb_face_destroy(pHbFace);
        }
        if (pTags)
            CFRelease(pTags);
    }
    else
    {
        CFDataRef pData = CTFontCopyTable(pFont, nTag, kCTFontTableOptionNoOptions);
        const CFIndex nLength = pData ? CFDataGetLength(pData) : 0;
        if (nLength > 0)
        {
            // tdf#159529 Use macOS font table memory directly instead of copying
            // Per Apple's documentation, the CFDataRef returned by the
            // CTFontCopyTable() function is "A retained reference to the
            // font table data as a CFDataRef object. The table data is not
            // actually copied; however, the data reference must be released."
            // So, instead of making a copy of the CFDataRef's data, just use
            // the CFDataRef's data pointer for the HarfBuzz blob and release
            // the CFDataRef in the blob's destroy function.
            pBlob = hb_blob_create(reinterpret_cast<const char*>(CFDataGetBytePtr(pData)), nLength,
                                   HB_MEMORY_MODE_READONLY,
                                   const_cast<void*>(static_cast<CFTypeRef>(pData)),
                                   MyDestroyCFDataRef);
        }
        else if (pData)
            CFRelease(pData);
    }

    CFRelease(pFont);
    return pBlob;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
