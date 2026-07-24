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

#include <utility>

#include <unx/font/FreetypeFontFace.hxx>
#include <unx/font/FreetypeFont.hxx>
#include <unx/font/FreetypeFontList.hxx>

#include <font/LogicalFontInstance.hxx>
#include <fontattributes.hxx>

#include <sal/log.hxx>
#include <osl/module.h>

#include <i18nlangtag/mslangid.hxx>
#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb-ot.h>

#include <tools/UnixWrappers.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifdef _WIN32
#define strncasecmp(x, y, z) _strnicmp(x, y, z)
#endif

namespace
{
const char* getLangBoost()
{
    const LanguageType eLang = Application::GetSettings().GetUILanguageTag().getLanguageType();
    if (eLang == LANGUAGE_JAPANESE)
        return "jan";
    if (MsLangId::isKorean(eLang))
        return "kor";
    if (MsLangId::isSimplifiedChinese(eLang))
        return "zhs";
    if (MsLangId::isTraditionalChinese(eLang))
        return "zht";
    return nullptr;
}
}

FreetypeFontFile::FreetypeFontFile(OString aNativeFileName)
    : maNativeFileName(std::move(aNativeFileName))
    , mpFileMap(nullptr)
    , mnFileSize(0)
    , mnRefCount(0)
    , mnLangBoost(0)
    , mnHandle(0)
{
    // boost font preference if UI language is mentioned in filename
    int nPos = maNativeFileName.lastIndexOf('_');
    if (nPos == -1 || maNativeFileName[nPos + 1] == '.')
        mnLangBoost += 0x1000; // no langinfo => good
    else
    {
        static const char* pLangBoost = nullptr;
        static bool bOnce = true;
        if (bOnce)
        {
            bOnce = false;
            pLangBoost = getLangBoost();
        }

        if (pLangBoost && !strncasecmp(pLangBoost, &maNativeFileName.getStr()[nPos + 1], 3))
            mnLangBoost += 0x2000; // matching langinfo => better
    }
}

bool FreetypeFontFile::Map()
{
    if (mnRefCount++ == 0)
    {
        const char* pFileName = maNativeFileName.getStr();
        int nFile;
        int nFD;
        int n;
        if (sscanf(pFileName, "/:FD:/%d%n", &nFD, &n) == 1 && pFileName[n] == '\0')
        {
            lseek(nFD, 0, SEEK_SET);
            nFile = wrap_dup(nFD);
        }
        else
            nFile = wrap_open(pFileName, O_RDONLY, 0);
        if (nFile < 0)
        {
            SAL_WARN("vcl.unx.freetype",
                     "open('" << maNativeFileName << "') failed: " << strerror(errno));
            return false;
        }

        struct stat aStat;
        int nRet = wrap_fstat(nFile, &aStat);
        if (nRet < 0)
        {
            SAL_WARN("vcl.unx.freetype",
                     "fstat on '" << maNativeFileName << "' failed: " << strerror(errno));
            wrap_close(nFile);
            return false;
        }
        mnFileSize = aStat.st_size;
        mpFileMap = static_cast<unsigned char*>(wrap_mmap(mnFileSize, nFile, &mnHandle));
        if (mpFileMap == MAP_FAILED)
        {
            SAL_WARN("vcl.unx.freetype",
                     "mmap of '" << maNativeFileName << "' failed: " << strerror(errno));
            mpFileMap = nullptr;
        }
        else
            SAL_INFO("vcl.unx.freetype", "mmap'ed '" << maNativeFileName << "' successfully");
        wrap_close(nFile);
    }

    return (mpFileMap != nullptr);
}

void FreetypeFontFile::Unmap()
{
    if (--mnRefCount != 0)
        return;
    assert(mnRefCount >= 0 && "how did this go negative\n");
    if (mpFileMap)
    {
        wrap_munmap(mpFileMap, mnFileSize, mnHandle);
        mpFileMap = nullptr;
    }
}

FreetypeFontFace::FreetypeFontFace(const FontAttributes& rDevFontAttributes,
                                   FreetypeFontFile* const pFontFile, int nFaceNum,
                                   int nFaceVariation, sal_IntPtr nFontId)
    : vcl::font::PhysicalFontFace(rDevFontAttributes)
    , maFaceFT(nullptr)
    , mpFontFile(pFontFile)
    , mnFaceNum(nFaceNum)
    , mnFaceVariation(nFaceVariation)
    , mnRefCount(0)
    , mnFontId(nFontId)
{
    // prefer font with low ID
    IncreaseQualityBy(10000 - nFontId);
    // prefer font with matching file names
    IncreaseQualityBy(mpFontFile->GetLangBoost());
}

FT_FaceRec_* FreetypeFontFace::GetFaceFT() const
{
    if (!maFaceFT && mpFontFile->Map())
    {
        FT_Error rc = FT_New_Memory_Face(GetFreetypeLibrary(), mpFontFile->GetBuffer(),
                                         mpFontFile->GetFileSize(), mnFaceNum, &maFaceFT);
        if ((rc != FT_Err_Ok) || (maFaceFT->num_glyphs <= 0))
            maFaceFT = nullptr;
    }

    ++mnRefCount;
    return maFaceFT;
}

void FreetypeFontFace::ReleaseFaceFT() const
{
    if (--mnRefCount == 0)
    {
        if (maFaceFT)
        {
            FT_Done_Face(maFaceFT);
            maFaceFT = nullptr;
        }
        mpFontFile->Unmap();
    }
    assert(mnRefCount >= 0 && "how did this go negative\n");
}

rtl::Reference<LogicalFontInstance>
FreetypeFontFace::CreateFontInstance(const vcl::font::FontSelectPattern& rFSD) const
{
    return new FreetypeFont(*this, rFSD);
}

namespace
{
hb_blob_t* CreateHbBlob(FreetypeFontFile* pFontFile)
{
    auto pFileName = pFontFile->GetFileName().getStr();
    int nFD;
    int n;
    if (sscanf(pFileName, "/:FD:/%d%n", &nFD, &n) == 1 && pFileName[n] == '\0')
    {
        if (pFontFile->Map())
            return hb_blob_create(
                reinterpret_cast<const char*>(pFontFile->GetBuffer()), pFontFile->GetFileSize(),
                HB_MEMORY_MODE_READONLY, pFontFile,
                [](void* data) { static_cast<FreetypeFontFile*>(data)->Unmap(); });
        pFontFile->Unmap();
        return hb_blob_get_empty();
    }
    return hb_blob_create_from_file(pFileName);
}
}

hb_face_t* FreetypeFontFace::GetHbFace() const
{
    if (!mpHbFace)
    {
        auto pFontFile = GetFontFile();
        auto nIndex = GetFontFaceIndex();
        auto pHbBlob = CreateHbBlob(pFontFile);
        mpHbFace = hb_face_create(pHbBlob, nIndex);
        hb_blob_destroy(pHbBlob);
    }
    return mpHbFace;
}

hb_blob_t* FreetypeFontFace::GetHbTable(hb_tag_t nTag) const
{
    return hb_face_reference_table(mpHbFace, nTag);
}

const std::vector<vcl::font::Variation>&
FreetypeFontFace::GetVariations(const LogicalFontInstance&) const
{
    if (!mxVariations)
    {
        mxVariations.emplace();
        sal_uInt32 nFaceVariation = GetFontFaceVariation();
        if (nFaceVariation)
        {
            hb_face_t* pHbFace = GetHbFace();
            unsigned int nAxes = hb_ot_var_get_axis_count(pHbFace);
            if (nAxes && nFaceVariation - 1 < hb_ot_var_get_named_instance_count(pHbFace))
            {
                std::vector<hb_ot_var_axis_info_t> aInfos(nAxes);
                hb_ot_var_get_axis_infos(pHbFace, 0, &nAxes, aInfos.data());
                std::vector<float> aCoords(nAxes);
                unsigned int nCoords = nAxes;
                if (hb_ot_var_named_instance_get_design_coords(pHbFace, nFaceVariation - 1,
                                                               &nCoords, aCoords.data()))
                {
                    mxVariations->reserve(nAxes);
                    for (unsigned int i = 0; i < nAxes; ++i)
                        mxVariations->push_back({ aInfos[i].tag, aCoords[i] });
                }
            }
        }
    }

    return *mxVariations;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
