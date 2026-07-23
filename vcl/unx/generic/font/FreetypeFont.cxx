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
#include <font/FontMetricData.hxx>
#include <fontattributes.hxx>

#include <tools/poly.hxx>
#include <basegfx/matrix/b2dhommatrixtools.hxx>
#include <basegfx/polygon/b2dpolypolygon.hxx>

#include <sal/log.hxx>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_SIZES_H
#include FT_SYNTHESIS_H
#include FT_TRUETYPE_TABLES_H

#include <vector>

FT_Face FreetypeFont::GetFtFace() const
{
    FT_Activate_Size( maSizeFT );

    return maFaceFT;
}

// FreetypeFont

FreetypeFont::FreetypeFont(const FreetypeFontFace& rFontFace, const vcl::font::FontSelectPattern& rFSD)
:   LogicalFontInstance(rFontFace, rFSD),
    mnCos( 0x10000),
    mnSin( 0 ),
    mnPrioAntiAlias(GetDefaultAntiAliasPrio()),
    maFaceFT( nullptr ),
    maSizeFT( nullptr ),
    mbFaceOk( false )
{
    maFaceFT = rFontFace.GetFaceFT();

    if( rFSD.mnOrientation )
    {
        const double dRad = toRadians(rFSD.mnOrientation);
        mnCos = static_cast<tools::Long>( 0x10000 * cos( dRad ) + 0.5 );
        mnSin = static_cast<tools::Long>( 0x10000 * sin( dRad ) + 0.5 );
    }

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

void FreetypeFont::GetFontMetric(FontMetricDataRef const & rxTo)
{
    rxTo->FontAttributes::operator =(*GetFontFace());

    rxTo->SetOrientation(GetFontSelectPattern().mnOrientation);

    FT_Activate_Size( maSizeFT );

    rxTo->ImplCalcLineSpacing(this);
    rxTo->ImplInitBaselines(this);

    rxTo->SetSlant( 0 );
    rxTo->SetWidth( mnWidth );

    const TT_OS2* pOS2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table( maFaceFT, ft_sfnt_os2 ));
    if( pOS2 && (pOS2->version != 0xFFFF) )
    {
        // map the panose info from the OS2 table to their VCL counterparts
        switch( pOS2->panose[0] )
        {
            case 1: rxTo->SetFamilyType( FAMILY_ROMAN ); break;
            case 2: rxTo->SetFamilyType( FAMILY_SWISS ); break;
            case 3: rxTo->SetFamilyType( FAMILY_MODERN ); break;
            case 4: rxTo->SetFamilyType( FAMILY_SCRIPT ); break;
            case 5: rxTo->SetFamilyType( FAMILY_DECORATIVE ); break;
            // TODO: is it reasonable to override the attribute with DONTKNOW?
            case 0: // fall through
            default: rxTo->SetFamilyType( FAMILY_DONTKNOW ); break;
        }

        switch( pOS2->panose[3] )
        {
            case 2: // fall through
            case 3: // fall through
            case 4: // fall through
            case 5: // fall through
            case 6: // fall through
            case 7: // fall through
            case 8: rxTo->SetPitch( PITCH_VARIABLE ); break;
            case 9: rxTo->SetPitch( PITCH_FIXED ); break;
            // TODO: is it reasonable to override the attribute with DONTKNOW?
            case 0: // fall through
            case 1: // fall through
            default: rxTo->SetPitch( PITCH_DONTKNOW ); break;
        }
    }

    // initialize kashida width
    rxTo->SetMinKashida(GetKashidaWidth());
}

void FreetypeFont::ApplyGlyphTransform(bool bVertical, FT_Glyph pGlyphFT ) const
{
    // shortcut most common case
    if (!GetFontSelectPattern().mnOrientation && !bVertical)
        return;

    const FT_Size_Metrics& rMetrics = maFaceFT->size->metrics;
    FT_Vector aVector;
    FT_Matrix aMatrix;

    bool bStretched = false;

    if (!bVertical)
    {
        // straight
        aVector.x = 0;
        aVector.y = 0;
        aMatrix.xx = +mnCos;
        aMatrix.yy = +mnCos;
        aMatrix.xy = -mnSin;
        aMatrix.yx = +mnSin;
    }
    else
    {
        // left
        bStretched = (mfStretch != 1.0);
        aVector.x  = static_cast<FT_Pos>(+rMetrics.descender * mfStretch);
        aVector.y  = -rMetrics.ascender;
        aMatrix.xx = static_cast<FT_Pos>(-mnSin / mfStretch);
        aMatrix.yy = static_cast<FT_Pos>(-mnSin * mfStretch);
        aMatrix.xy = static_cast<FT_Pos>(-mnCos * mfStretch);
        aMatrix.yx = static_cast<FT_Pos>(+mnCos / mfStretch);
    }

    if( pGlyphFT->format != FT_GLYPH_FORMAT_BITMAP )
    {
        FT_Glyph_Transform( pGlyphFT, nullptr, &aVector );

        // orthogonal transforms are better handled by bitmap operations
        if( bStretched )
        {
            // apply non-orthogonal or stretch transformations
            FT_Glyph_Transform( pGlyphFT, &aMatrix, nullptr );
        }
    }
    else
    {
        // FT<=2005 ignores transforms for bitmaps, so do it manually
        FT_BitmapGlyph pBmpGlyphFT = reinterpret_cast<FT_BitmapGlyph>(pGlyphFT);
        pBmpGlyphFT->left += (aVector.x + 32) >> 6;
        pBmpGlyphFT->top  += (aVector.y + 32) >> 6;
    }
}

bool FreetypeFont::GetAntialiasAdvice() const
{
    // TODO: also use GASP info
    return !GetFontSelectPattern().mbNonAntialiased && (mnPrioAntiAlias > 0);
}

// outline stuff

namespace {

class PolyArgs
{
public:
                PolyArgs( tools::PolyPolygon& rPolyPoly, sal_uInt16 nMaxPoints );

    void        AddPoint( tools::Long nX, tools::Long nY, PolyFlags);
    void        ClosePolygon();

    tools::Long        GetPosX() const { return maPosition.x;}
    tools::Long        GetPosY() const { return maPosition.y;}

private:
    tools::PolyPolygon& mrPolyPoly;

    std::unique_ptr<Point[]>
                    mpPointAry;
    std::unique_ptr<PolyFlags[]>
                    mpFlagAry;

    FT_Vector       maPosition;
    sal_uInt16      mnMaxPoints;
    sal_uInt16      mnPoints;
    sal_uInt16      mnPoly;
    bool            bHasOffline;

    PolyArgs(const PolyArgs&) = delete;
    PolyArgs& operator=(const PolyArgs&) = delete;
};

}

PolyArgs::PolyArgs( tools::PolyPolygon& rPolyPoly, sal_uInt16 nMaxPoints )
:   mrPolyPoly(rPolyPoly),
    mnMaxPoints(nMaxPoints),
    mnPoints(0),
    mnPoly(0),
    bHasOffline(false)
{
    mpPointAry.reset( new Point[ mnMaxPoints ] );
    mpFlagAry.reset( new PolyFlags [ mnMaxPoints ] );
    maPosition.x = maPosition.y = 0;
}

void PolyArgs::AddPoint( tools::Long nX, tools::Long nY, PolyFlags aFlag )
{
    SAL_WARN_IF( (mnPoints >= mnMaxPoints), "vcl", "FTGlyphOutline: AddPoint overflow!" );
    if( mnPoints >= mnMaxPoints )
        return;

    maPosition.x = nX;
    maPosition.y = nY;
    mpPointAry[ mnPoints ] = Point( nX, nY );
    mpFlagAry[ mnPoints++ ]= aFlag;
    bHasOffline |= (aFlag != PolyFlags::Normal);
}

void PolyArgs::ClosePolygon()
{
    if( !mnPoly++ )
        return;

    // freetype seems to always close the polygon with an ON_CURVE point
    // PolyPoly wants to close the polygon itself => remove last point
    SAL_WARN_IF( (mnPoints < 2), "vcl", "FTGlyphOutline: PolyFinishNum failed!" );
    --mnPoints;
    SAL_WARN_IF( (mpPointAry[0]!=mpPointAry[mnPoints]), "vcl", "FTGlyphOutline: PolyFinishEq failed!" );
    SAL_WARN_IF( (mpFlagAry[0]!=PolyFlags::Normal), "vcl", "FTGlyphOutline: PolyFinishFE failed!" );
    SAL_WARN_IF( (mpFlagAry[mnPoints]!=PolyFlags::Normal), "vcl", "FTGlyphOutline: PolyFinishFS failed!" );

    tools::Polygon aPoly( mnPoints, mpPointAry.get(), (bHasOffline ? mpFlagAry.get() : nullptr) );

    // #i35928#
    // This may be an invalid polygon, e.g. the last point is a control point.
    // So close the polygon (and add the first point again) if the last point
    // is a control point or different from first.
    // #i48298#
    // Now really duplicating the first point, to close or correct the
    // polygon. Also no longer duplicating the flags, but enforcing
    // PolyFlags::Normal for the newly added last point.
    const sal_uInt16 nPolySize(aPoly.GetSize());
    if(nPolySize)
    {
        if((aPoly.HasFlags() && PolyFlags::Control == aPoly.GetFlags(nPolySize - 1))
            || (aPoly.GetPoint(nPolySize - 1) != aPoly.GetPoint(0)))
        {
            aPoly.SetSize(nPolySize + 1);
            aPoly.SetPoint(aPoly.GetPoint(0), nPolySize);

            if(aPoly.HasFlags())
            {
                aPoly.SetFlags(nPolySize, PolyFlags::Normal);
            }
        }
    }

    mrPolyPoly.Insert( aPoly );
    mnPoints = 0;
    bHasOffline = false;
}

extern "C" {

// TODO: wait till all compilers accept that calling conventions
// for functions are the same independent of implementation constness,
// then uncomment the const-tokens in the function interfaces below
static int FT_move_to( const FT_Vector* p0, void* vpPolyArgs )
{
    PolyArgs& rA = *static_cast<PolyArgs*>(vpPolyArgs);

    // move_to implies a new polygon => finish old polygon first
    rA.ClosePolygon();

    rA.AddPoint( p0->x, p0->y, PolyFlags::Normal );
    return 0;
}

static int FT_line_to( const FT_Vector* p1, void* vpPolyArgs )
{
    PolyArgs& rA = *static_cast<PolyArgs*>(vpPolyArgs);
    rA.AddPoint( p1->x, p1->y, PolyFlags::Normal );
    return 0;
}

static int FT_conic_to( const FT_Vector* p1, const FT_Vector* p2, void* vpPolyArgs )
{
    PolyArgs& rA = *static_cast<PolyArgs*>(vpPolyArgs);

    // VCL's Polygon only knows cubic beziers
    const tools::Long nX1 = (2 * rA.GetPosX() + 4 * p1->x + 3) / 6;
    const tools::Long nY1 = (2 * rA.GetPosY() + 4 * p1->y + 3) / 6;
    rA.AddPoint( nX1, nY1, PolyFlags::Control );

    const tools::Long nX2 = (2 * p2->x + 4 * p1->x + 3) / 6;
    const tools::Long nY2 = (2 * p2->y + 4 * p1->y + 3) / 6;
    rA.AddPoint( nX2, nY2, PolyFlags::Control );

    rA.AddPoint( p2->x, p2->y, PolyFlags::Normal );
    return 0;
}

static int FT_cubic_to( const FT_Vector* p1, const FT_Vector* p2, const FT_Vector* p3, void* vpPolyArgs )
{
    PolyArgs& rA = *static_cast<PolyArgs*>(vpPolyArgs);
    rA.AddPoint( p1->x, p1->y, PolyFlags::Control );
    rA.AddPoint( p2->x, p2->y, PolyFlags::Control );
    rA.AddPoint( p3->x, p3->y, PolyFlags::Normal );
    return 0;
}

} // extern "C"

bool FreetypeFont::GetGlyphOutline(sal_GlyphId nId, basegfx::B2DPolyPolygon& rB2DPolyPoly, bool bIsVertical) const
{
    if( maSizeFT )
        FT_Activate_Size( maSizeFT );

    rB2DPolyPoly.clear();

    FT_Int nLoadFlags = FT_LOAD_DEFAULT | FT_LOAD_IGNORE_TRANSFORM;

#ifdef FT_LOAD_TARGET_LIGHT
    // enable "light hinting" if available
    nLoadFlags |= FT_LOAD_TARGET_LIGHT;
#endif

    FT_Error rc = FT_Load_Glyph(maFaceFT, nId, nLoadFlags);
    if( rc != FT_Err_Ok )
        return false;

    if (NeedsArtificialBold())
        FT_GlyphSlot_Embolden(maFaceFT->glyph);

    FT_Glyph pGlyphFT;
    rc = FT_Get_Glyph( maFaceFT->glyph, &pGlyphFT );
    if( rc != FT_Err_Ok )
        return false;

    if( pGlyphFT->format != FT_GLYPH_FORMAT_OUTLINE )
    {
        FT_Done_Glyph( pGlyphFT );
        return false;
    }

    if (NeedsArtificialItalic())
    {
        FT_Matrix aMatrix;
        aMatrix.xx = aMatrix.yy = ARTIFICIAL_ITALIC_MATRIX_XX;
        aMatrix.xy = ARTIFICIAL_ITALIC_MATRIX_XY;
        aMatrix.yx = 0;
        FT_Glyph_Transform( pGlyphFT, &aMatrix, nullptr );
    }

    FT_Outline& rOutline = reinterpret_cast<FT_OutlineGlyphRec*>(pGlyphFT)->outline;
    if( !rOutline.n_points )    // blank glyphs are ok
    {
        FT_Done_Glyph( pGlyphFT );
        return true;
    }

    tools::Long nMaxPoints = 1 + rOutline.n_points * 3;
    tools::PolyPolygon aToolPolyPolygon;
    PolyArgs aPolyArg( aToolPolyPolygon, nMaxPoints );

    ApplyGlyphTransform(bIsVertical, pGlyphFT);

    FT_Outline_Funcs aFuncs;
    aFuncs.move_to  = &FT_move_to;
    aFuncs.line_to  = &FT_line_to;
    aFuncs.conic_to = &FT_conic_to;
    aFuncs.cubic_to = &FT_cubic_to;
    aFuncs.shift    = 0;
    aFuncs.delta    = 0;
    FT_Outline_Decompose( &rOutline, &aFuncs, static_cast<void*>(&aPolyArg) );
    aPolyArg.ClosePolygon();    // close last polygon
    FT_Done_Glyph( pGlyphFT );

    // convert to basegfx polypolygon
    // TODO: get rid of the intermediate tools polypolygon
    rB2DPolyPoly = aToolPolyPolygon.getB2DPolyPolygon();
    rB2DPolyPoly.transform(basegfx::utils::createScaleB2DHomMatrix( +0x1p-6, -0x1p-6 ));

    return true;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
