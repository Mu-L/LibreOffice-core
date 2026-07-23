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

#include <QtFont.hxx>

#include <vcl/qt/QtUtils.hxx>

#include <QtGui/QFont>

void QtFont::applyWeight(QFont& rFont, FontWeight eWeight)
{
    switch (eWeight)
    {
        case WEIGHT_THIN:
            rFont.setWeight(QFont::Thin);
            break;
        case WEIGHT_ULTRALIGHT:
            rFont.setWeight(QFont::ExtraLight);
            break;
        case WEIGHT_LIGHT:
            rFont.setWeight(QFont::Light);
            break;
        case WEIGHT_SEMILIGHT:
            [[fallthrough]];
        case WEIGHT_NORMAL:
            rFont.setWeight(QFont::Normal);
            break;
        case WEIGHT_MEDIUM:
            rFont.setWeight(QFont::Medium);
            break;
        case WEIGHT_SEMIBOLD:
            rFont.setWeight(QFont::DemiBold);
            break;
        case WEIGHT_BOLD:
            rFont.setWeight(QFont::Bold);
            break;
        case WEIGHT_ULTRABOLD:
            rFont.setWeight(QFont::ExtraBold);
            break;
        case WEIGHT_BLACK:
            rFont.setWeight(QFont::Black);
            break;
        default:
            break;
    }
}

void QtFont::applyStretch(QFont& rFont, FontWidth eWidthType)
{
    switch (eWidthType)
    {
        case WIDTH_DONTKNOW:
            rFont.setStretch(QFont::AnyStretch);
            break;
        case WIDTH_ULTRA_CONDENSED:
            rFont.setStretch(QFont::UltraCondensed);
            break;
        case WIDTH_EXTRA_CONDENSED:
            rFont.setStretch(QFont::ExtraCondensed);
            break;
        case WIDTH_CONDENSED:
            rFont.setStretch(QFont::Condensed);
            break;
        case WIDTH_SEMI_CONDENSED:
            rFont.setStretch(QFont::SemiCondensed);
            break;
        case WIDTH_NORMAL:
            rFont.setStretch(QFont::Unstretched);
            break;
        case WIDTH_SEMI_EXPANDED:
            rFont.setStretch(QFont::SemiExpanded);
            break;
        case WIDTH_EXPANDED:
            rFont.setStretch(QFont::Expanded);
            break;
        case WIDTH_EXTRA_EXPANDED:
            rFont.setStretch(QFont::ExtraExpanded);
            break;
        case WIDTH_ULTRA_EXPANDED:
            rFont.setStretch(QFont::UltraExpanded);
            break;
        default:
            break;
    }
}

void QtFont::applyStyle(QFont& rFont, FontItalic eItalic)
{
    switch (eItalic)
    {
        case ITALIC_NONE:
            rFont.setStyle(QFont::Style::StyleNormal);
            break;
        case ITALIC_OBLIQUE:
            rFont.setStyle(QFont::Style::StyleOblique);
            break;
        case ITALIC_NORMAL:
            rFont.setStyle(QFont::Style::StyleItalic);
            break;
        default:
            break;
    }
}

QtFont::QtFont(const vcl::font::PhysicalFontFace& rPFF, const vcl::font::FontSelectPattern& rFSP)
    : LogicalFontInstance(rPFF, rFSP)
{
    setFamily(toQString(rPFF.GetFamilyName()));
    applyWeight(*this, rPFF.GetWeight());
    setPixelSize(rFSP.mnHeight);
    applyStretch(*this, rPFF.GetWidthType());
    applyStyle(*this, rFSP.GetItalic());
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
