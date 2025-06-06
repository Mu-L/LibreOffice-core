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

#include <drawinglayer/primitive2d/BufferedDecompositionPrimitive2D.hxx>
#include <drawinglayer/primitive2d/textenumsprimitive2d.hxx>
#include <basegfx/matrix/b2dhommatrix.hxx>
#include <basegfx/color/bcolor.hxx>
#include <drawinglayer/attribute/fontattribute.hxx>
#include <com/sun/star/lang/Locale.hpp>


namespace drawinglayer::primitive2d
    {
        class BaseTextStrikeoutPrimitive2D : public BufferedDecompositionPrimitive2D
        {
        private:
            /// geometric definitions
            basegfx::B2DHomMatrix                   maObjectTransformation;
            double                                  mfWidth;

            /// decoration definitions
            basegfx::BColor                         maFontColor;

        public:
            /// constructor
            BaseTextStrikeoutPrimitive2D(
                basegfx::B2DHomMatrix aObjectTransformation,
                double fWidth,
                const basegfx::BColor& rFontColor);

            /// data read access
            const basegfx::B2DHomMatrix& getObjectTransformation() const { return maObjectTransformation; }
            double getWidth() const { return mfWidth; }
            const basegfx::BColor& getFontColor() const { return maFontColor; }

            /// compare operator
            virtual bool operator==( const BasePrimitive2D& rPrimitive ) const override;
        };

} // end of namespace drawinglayer::primitive2d


namespace drawinglayer::primitive2d
    {
        class TextCharacterStrikeoutPrimitive2D final : public BaseTextStrikeoutPrimitive2D
        {
        private:
            sal_Unicode                             maStrikeoutChar;
            attribute::FontAttribute                maFontAttribute;
            css::lang::Locale                       maLocale;

            /// local decomposition.
            virtual Primitive2DReference create2DDecomposition(const geometry::ViewInformation2D& rViewInformation) const override;

        public:
            /// constructor
            TextCharacterStrikeoutPrimitive2D(
                const basegfx::B2DHomMatrix& rObjectTransformation,
                double fWidth,
                const basegfx::BColor& rFontColor,
                sal_Unicode aStrikeoutChar,
                attribute::FontAttribute aFontAttribute,
                css::lang::Locale aLocale);

            /// data read access
            sal_Unicode getStrikeoutChar() const { return maStrikeoutChar; }
            const attribute::FontAttribute& getFontAttribute() const { return maFontAttribute; }
            const css::lang::Locale& getLocale() const { return maLocale; }

            /// compare operator
            virtual bool operator==( const BasePrimitive2D& rPrimitive ) const override;

            /// provide unique ID
            virtual sal_uInt32 getPrimitive2DID() const override;
        };

} // end of namespace drawinglayer::primitive2d


namespace drawinglayer::primitive2d
    {
        class TextGeometryStrikeoutPrimitive2D final : public BaseTextStrikeoutPrimitive2D
        {
        private:
            double                                  mfHeight;
            double                                  mfOffset;
            TextStrikeout                           meTextStrikeout;

            /// local decomposition.
            virtual Primitive2DReference create2DDecomposition(const geometry::ViewInformation2D& rViewInformation) const override;

        public:
            /// constructor
            TextGeometryStrikeoutPrimitive2D(
                const basegfx::B2DHomMatrix& rObjectTransformation,
                double fWidth,
                const basegfx::BColor& rFontColor,
                double fHeight,
                double fOffset,
                TextStrikeout eTextStrikeout);

            /// data read access
            double getHeight() const { return mfHeight; }
            double getOffset() const { return mfOffset; }
            TextStrikeout getTextStrikeout() const { return meTextStrikeout; }

            /// compare operator
            virtual bool operator==( const BasePrimitive2D& rPrimitive ) const override;

            /// provide unique ID
            virtual sal_uInt32 getPrimitive2DID() const override;
        };

} // end of namespace drawinglayer::primitive2d


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
