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

#include <colorpicker.hxx>

#include <com/sun/star/uno/XComponentContext.hpp>
#include <com/sun/star/ui/dialogs/XExecutableDialog.hpp>
#include <com/sun/star/ui/dialogs/XAsynchronousExecutableDialog.hpp>
#include <com/sun/star/beans/XPropertyAccess.hpp>
#include <com/sun/star/lang/XInitialization.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/awt/XWindow.hpp>

#include <comphelper/propertyvalue.hxx>
#include <comphelper/compbase.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <vcl/ColorDialog.hxx>
#include <vcl/svapp.hxx>
#include <basegfx/color/bcolortools.hxx>
#include <cmath>
#include <o3tl/typed_flags_set.hxx>

using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::ui::dialogs;
using namespace ::com::sun::star::beans;
using namespace ::basegfx;

// color space conversion helpers

static void RGBtoHSV( double dR, double dG, double dB, double& dH, double& dS, double& dV )
{
    BColor result = basegfx::utils::rgb2hsv( BColor( dR, dG, dB ) );

    dH = result.getX();
    dS = result.getY();
    dV = result.getZ();
}

static void HSVtoRGB(double dH, double dS, double dV, double& dR, double& dG, double& dB )
{
    BColor result = basegfx::utils::hsv2rgb( BColor( dH, dS, dV ) );

    dR = result.getRed();
    dG = result.getGreen();
    dB = result.getBlue();
}

// CMYK values from 0 to 1
static void CMYKtoRGB( double fCyan, double fMagenta, double fYellow, double fKey, double& dR, double& dG, double& dB )
{
    fCyan = (fCyan * ( 1.0 - fKey )) + fKey;
    fMagenta = (fMagenta * ( 1.0 - fKey )) + fKey;
    fYellow = (fYellow * ( 1.0 - fKey )) + fKey;

    dR = std::clamp( 1.0 - fCyan, 0.0, 1.0 );
    dG = std::clamp( 1.0 - fMagenta, 0.0, 1.0 );
    dB = std::clamp( 1.0 - fYellow, 0.0, 1.0 );
}

// CMY results from 0 to 1
static void RGBtoCMYK( double dR, double dG, double dB, double& fCyan, double& fMagenta, double& fYellow, double& fKey )
{
    fCyan = 1 - dR;
    fMagenta = 1 - dG;
    fYellow = 1 - dB;

    //CMYK and CMY values from 0 to 1
    fKey = 1.0;
    if( fCyan < fKey ) fKey = fCyan;
    if( fMagenta < fKey ) fKey = fMagenta;
    if( fYellow < fKey ) fKey = fYellow;

    if( fKey >= 1.0 )
    {
        //Black
       fCyan = 0.0;
       fMagenta = 0.0;
       fYellow = 0.0;
    }
    else
    {
       fCyan = ( fCyan - fKey ) / ( 1.0 - fKey );
       fMagenta = ( fMagenta - fKey ) / ( 1.0 - fKey );
       fYellow = ( fYellow - fKey ) / ( 1.0 - fKey );
    }
}

void ColorPreviewControl::Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle&)
{
    rRenderContext.SetFillColor(m_aColor);
    rRenderContext.SetLineColor(m_aColor);
    rRenderContext.DrawRect(tools::Rectangle(Point(0, 0), GetOutputSizePixel()));
}

void ColorPreviewControl::SetDrawingArea(weld::DrawingArea* pDrawingArea)
{
    CustomWidgetController::SetDrawingArea(pDrawingArea);
    pDrawingArea->set_size_request(pDrawingArea->get_approximate_digit_width() * 10,
                                   pDrawingArea->get_text_height() * 2);
}

void ColorPreviewControl::SetColor(const Color& rCol)
{
    if (rCol != m_aColor)
    {
        m_aColor = rCol;
        Invalidate();
    }
}

void ColorFieldControl::UpdateBitmap()
{
    const Size aSize(GetOutputSizePixel());

    if (mxBitmap && mxBitmap->GetOutputSizePixel() != aSize)
        mxBitmap.disposeAndClear();

    const sal_Int32 nWidth = aSize.Width();
    const sal_Int32 nHeight = aSize.Height();

    if (nWidth == 0 || nHeight == 0)
        return;

    if (!mxBitmap)
    {
        mxBitmap = VclPtr<VirtualDevice>::Create();
        mxBitmap->SetOutputSizePixel(aSize);

        maRGB_Horiz.resize( nWidth );
        maGrad_Horiz.resize( nWidth );
        maPercent_Horiz.resize( nWidth );

        sal_uInt8* pRGB = maRGB_Horiz.data();
        sal_uInt16* pGrad = maGrad_Horiz.data();
        sal_uInt16* pPercent = maPercent_Horiz.data();

        for( sal_Int32 x = 0; x < nWidth; x++ )
        {
            *pRGB++ = static_cast<sal_uInt8>((x * 256) / nWidth);
            *pGrad++ = static_cast<sal_uInt16>((x * 359) / nWidth);
            *pPercent++ = static_cast<sal_uInt16>((x * 100) / nWidth);
        }

        maRGB_Vert.resize(nHeight);
        maPercent_Vert.resize(nHeight);

        pRGB = maRGB_Vert.data();
        pPercent = maPercent_Vert.data();

        sal_Int32 y = nHeight;
        while (y--)
        {
            *pRGB++ = static_cast<sal_uInt8>((y * 256) / nHeight);
            *pPercent++ = static_cast<sal_uInt16>((y * 100) / nHeight);
        }
    }

    sal_uInt8* pRGB_Horiz = maRGB_Horiz.data();
    sal_uInt16* pGrad_Horiz = maGrad_Horiz.data();
    sal_uInt16* pPercent_Horiz = maPercent_Horiz.data();
    sal_uInt8* pRGB_Vert = maRGB_Vert.data();
    sal_uInt16* pPercent_Vert = maPercent_Vert.data();

        // this has been unlooped for performance reason, please do not merge back!

    sal_uInt16 y = nHeight,x;

    switch(meMode)
    {
        case HUE:
            while (y--)
            {
                sal_uInt16 nBri = pPercent_Vert[y];
                x = nWidth;
                while (x--)
                {
                    sal_uInt16 nSat = pPercent_Horiz[x];
                    mxBitmap->DrawPixel(Point(x,y), Color::HSBtoRGB(mnBaseValue, nSat, nBri));
                }
            }
            break;
        case SATURATION:
            while (y--)
            {
                sal_uInt16 nBri = pPercent_Vert[y];
                x = nWidth;
                while (x--)
                {
                    sal_uInt16 nHue = pGrad_Horiz[x];
                    mxBitmap->DrawPixel(Point(x,y), Color::HSBtoRGB(nHue, mnBaseValue, nBri));
                }
            }
            break;
        case BRIGHTNESS:
            while (y--)
            {
                sal_uInt16 nSat = pPercent_Vert[y];
                x = nWidth;
                while (x--)
                {
                    sal_uInt16 nHue = pGrad_Horiz[x];
                    mxBitmap->DrawPixel(Point(x,y), Color::HSBtoRGB(nHue, nSat, mnBaseValue));
                }
            }
            break;
        case RED:
        {
            Color aBitmapColor;
            aBitmapColor.SetRed(mnBaseValue);
            while (y--)
            {
                aBitmapColor.SetGreen(pRGB_Vert[y]);
                x = nWidth;
                while (x--)
                {
                    aBitmapColor.SetBlue(pRGB_Horiz[x]);
                    mxBitmap->DrawPixel(Point(x,y), aBitmapColor);
                }
            }
            break;
        }
        case GREEN:
        {
            Color aBitmapColor;
            aBitmapColor.SetGreen(mnBaseValue);
            while (y--)
            {
                aBitmapColor.SetRed(pRGB_Vert[y]);
                x = nWidth;
                while (x--)
                {
                    aBitmapColor.SetBlue(pRGB_Horiz[x]);
                    mxBitmap->DrawPixel(Point(x,y), aBitmapColor);
                }
            }
            break;
        }
        case BLUE:
        {
            Color aBitmapColor;
            aBitmapColor.SetBlue(mnBaseValue);
            while (y--)
            {
                aBitmapColor.SetGreen(pRGB_Vert[y]);
                x = nWidth;
                while (x--)
                {
                    aBitmapColor.SetRed(pRGB_Horiz[x]);
                    mxBitmap->DrawPixel(Point(x,y), aBitmapColor);
                }
            }
            break;
        }
    }
}

constexpr int nCenterOffset = 5;

const ColorMode DefaultMode = HUE;

ColorFieldControl::ColorFieldControl()
    : meMode(DefaultMode)
    , mnBaseValue(USHRT_MAX)
    , mdX(-1.0)
    , mdY(-1.0)
{
}

void ColorFieldControl::SetDrawingArea(weld::DrawingArea* pDrawingArea)
{
    CustomWidgetController::SetDrawingArea(pDrawingArea);
    pDrawingArea->set_size_request(pDrawingArea->get_approximate_digit_width() * 40,
                                   pDrawingArea->get_text_height() * 10);
}

ColorFieldControl::~ColorFieldControl() { mxBitmap.disposeAndClear(); }

void ColorFieldControl::ShowPosition( const Point& rPos, bool bUpdate )
{
    if (!mxBitmap)
    {
        UpdateBitmap();
        Invalidate();
    }

    if (!mxBitmap)
        return;

    const Size aSize(mxBitmap->GetOutputSizePixel());

    tools::Long nX = rPos.X();
    tools::Long nY = rPos.Y();
    if (nX < 0)
        nX = 0;
    else if (nX >= aSize.Width())
        nX = aSize.Width() - 1;

    if (nY < 0)
        nY = 0;
    else if (nY >= aSize.Height())
        nY = aSize.Height() - 1;

    Point aPos = maPosition;
    maPosition.setX( nX - nCenterOffset );
    maPosition.setY( nY - nCenterOffset );
    Invalidate(tools::Rectangle(aPos, Size(11, 11)));
    Invalidate(tools::Rectangle(maPosition, Size(11, 11)));

    if (bUpdate)
    {
        mdX = double(nX) / double(aSize.Width() - 1.0);
        mdY = double(aSize.Height() - 1.0 - nY) / double(aSize.Height() - 1.0);
    }
}

bool ColorFieldControl::MouseButtonDown(const MouseEvent& rMEvt)
{
    CaptureMouse();
    ShowPosition(rMEvt.GetPosPixel(), true);
    Modify();
    return true;
}

bool ColorFieldControl::MouseMove(const MouseEvent& rMEvt)
{
    if (IsMouseCaptured())
    {
        ShowPosition(rMEvt.GetPosPixel(), true);
        Modify();
    }
    return true;
}

bool ColorFieldControl::MouseButtonUp(const MouseEvent&)
{
    ReleaseMouse();
    return true;
}

void ColorFieldControl::Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle&)
{
    if (!mxBitmap)
        UpdateBitmap();

    if (!mxBitmap)
        return;

    Size aSize(GetOutputSizePixel());
    rRenderContext.DrawOutDev(Point(0, 0), aSize, Point(0, 0), aSize, *mxBitmap);

    // draw circle around current color
    Point aPos(maPosition.X() + nCenterOffset, maPosition.Y() + nCenterOffset);
    Color aColor = mxBitmap->GetPixel(aPos);
    if (aColor.IsDark())
        rRenderContext.SetLineColor(COL_WHITE);
    else
        rRenderContext.SetLineColor(COL_BLACK);

    rRenderContext.SetFillColor();
    rRenderContext.DrawEllipse(::tools::Rectangle(maPosition, Size(11, 11)));
}

void ColorFieldControl::Resize()
{
    CustomWidgetController::Resize();
    UpdateBitmap();
    UpdatePosition();
}

void ColorFieldControl::Modify()
{
    maModifyHdl.Call( *this );
}

void ColorFieldControl::SetValues(sal_uInt16 nBaseValue, ColorMode eMode, double x, double y)
{
    bool bUpdateBitmap = (mnBaseValue != nBaseValue) || (meMode != eMode);
    if (!bUpdateBitmap && mdX == x && mdY == y)
        return;

    mnBaseValue = nBaseValue;
    meMode = eMode;
    mdX = x;
    mdY = y;

    if (bUpdateBitmap)
        UpdateBitmap();
    UpdatePosition();
    if (bUpdateBitmap)
        Invalidate();
}

void ColorFieldControl::UpdatePosition()
{
    Size aSize(GetOutputSizePixel());
    ShowPosition(Point(static_cast<tools::Long>(mdX * aSize.Width()), static_cast<tools::Long>((1.0 - mdY) * aSize.Height())), false);
}

ColorSliderControl::ColorSliderControl()
    : meMode( DefaultMode )
    , mnLevel( 0 )
    , mdValue( -1.0 )
{
}

void ColorSliderControl::SetDrawingArea(weld::DrawingArea* pDrawingArea)
{
    CustomWidgetController::SetDrawingArea(pDrawingArea);
    pDrawingArea->set_size_request(pDrawingArea->get_approximate_digit_width() * 3, -1);
}

ColorSliderControl::~ColorSliderControl()
{
    mxBitmap.disposeAndClear();
}

void ColorSliderControl::UpdateBitmap()
{
    Size aSize(1, GetOutputSizePixel().Height());

    if (mxBitmap && mxBitmap->GetOutputSizePixel() != aSize)
        mxBitmap.disposeAndClear();

    if (!mxBitmap)
    {
        mxBitmap = VclPtr<VirtualDevice>::Create();
        mxBitmap->SetOutputSizePixel(aSize);
    }

    const tools::Long nY = aSize.Height() - 1;

    Color aBitmapColor(maColor);

    sal_uInt16 nHue, nSat, nBri;
    maColor.RGBtoHSB(nHue, nSat, nBri);

    // this has been unlooped for performance reason, please do not merge back!

    switch (meMode)
    {
    case HUE:
        nSat = 100;
        nBri = 100;
        for (tools::Long y = 0; y <= nY; y++)
        {
            nHue = static_cast<sal_uInt16>((359 * y) / nY);
            mxBitmap->DrawPixel(Point(0, nY - y), Color::HSBtoRGB(nHue, nSat, nBri));
        }
        break;

    case SATURATION:
        nBri = std::max(sal_uInt16(32), nBri);
        for (tools::Long y = 0; y <= nY; y++)
        {
            nSat = static_cast<sal_uInt16>((100 * y) / nY);
            mxBitmap->DrawPixel(Point(0, nY - y), Color::HSBtoRGB(nHue, nSat, nBri));
        }
        break;

    case BRIGHTNESS:
        for (tools::Long y = 0; y <= nY; y++)
        {
            nBri = static_cast<sal_uInt16>((100 * y) / nY);
            mxBitmap->DrawPixel(Point(0, nY - y), Color::HSBtoRGB(nHue, nSat, nBri));
        }
        break;

    case RED:
        for (tools::Long y = 0; y <= nY; y++)
        {
            aBitmapColor.SetRed(sal_uInt8((tools::Long(255) * y) / nY));
            mxBitmap->DrawPixel(Point(0, nY - y), aBitmapColor);
        }
        break;

    case GREEN:
        for (tools::Long y = 0; y <= nY; y++)
        {
            aBitmapColor.SetGreen(sal_uInt8((tools::Long(255) * y) / nY));
            mxBitmap->DrawPixel(Point(0, nY - y), aBitmapColor);
        }
        break;

    case BLUE:
        for (tools::Long y = 0; y <= nY; y++)
        {
            aBitmapColor.SetBlue(sal_uInt8((tools::Long(255) * y) / nY));
            mxBitmap->DrawPixel(Point(0, nY - y), aBitmapColor);
        }
        break;
    }
}

void ColorSliderControl::ChangePosition(tools::Long nY)
{
    const tools::Long nHeight = GetOutputSizePixel().Height() - 1;

    if (nY < 0)
        nY = 0;
    else if (nY > nHeight)
        nY = nHeight;

    mnLevel = nY;
    mdValue = double(nHeight - nY) / double(nHeight);
}

bool ColorSliderControl::MouseButtonDown(const MouseEvent& rMEvt)
{
    CaptureMouse();
    ChangePosition(rMEvt.GetPosPixel().Y());
    Modify();
    return true;
}

bool ColorSliderControl::MouseMove(const MouseEvent& rMEvt)
{
    if (IsMouseCaptured())
    {
        ChangePosition(rMEvt.GetPosPixel().Y());
        Modify();
    }
    return true;
}

bool ColorSliderControl::MouseButtonUp(const MouseEvent&)
{
    ReleaseMouse();
    return true;
}

void ColorSliderControl::Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle&)
{
    if (!mxBitmap)
        UpdateBitmap();

    const Size aSize(GetOutputSizePixel());

    Point aPos;
    int x = aSize.Width();
    while (x--)
    {
        rRenderContext.DrawOutDev(aPos, aSize, Point(0,0), aSize, *mxBitmap);
        aPos.AdjustX(1);
    }
}

void ColorSliderControl::Resize()
{
    CustomWidgetController::Resize();
    UpdateBitmap();
}

void ColorSliderControl::Modify()
{
    maModifyHdl.Call(*this);
}

void ColorSliderControl::SetValue(const Color& rColor, ColorMode eMode, double dValue)
{
    bool bUpdateBitmap = (rColor != maColor) || (eMode != meMode);
    if( bUpdateBitmap || (mdValue != dValue))
    {
        maColor = rColor;
        mdValue = dValue;
        mnLevel = static_cast<sal_Int16>((1.0-dValue) * GetOutputSizePixel().Height());
        meMode = eMode;
        if (bUpdateBitmap)
            UpdateBitmap();
        Invalidate();
    }
}

ColorPickerDialog::ColorPickerDialog(weld::Window* pParent, const Color& rColor, vcl::ColorPickerMode eDialogMode)
    : SfxDialogController(pParent, u"cui/ui/colorpickerdialog.ui"_ustr, u"ColorPicker"_ustr)
    , m_xColorField(new weld::CustomWeld(*m_xBuilder, u"colorField"_ustr, m_aColorField))
    , m_xColorSlider(new weld::CustomWeld(*m_xBuilder, u"colorSlider"_ustr, m_aColorSlider))
    , m_xColorPreview(new weld::CustomWeld(*m_xBuilder, u"preview"_ustr, m_aColorPreview))
    , m_xColorPrevious(new weld::CustomWeld(*m_xBuilder, u"previous"_ustr, m_aColorPrevious))
    , m_xFISliderLeft(m_xBuilder->weld_widget(u"leftImage"_ustr))
    , m_xFISliderRight(m_xBuilder->weld_widget(u"rightImage"_ustr))
    , m_xRBRed(m_xBuilder->weld_radio_button(u"redRadiobutton"_ustr))
    , m_xRBGreen(m_xBuilder->weld_radio_button(u"greenRadiobutton"_ustr))
    , m_xRBBlue(m_xBuilder->weld_radio_button(u"blueRadiobutton"_ustr))
    , m_xRBHue(m_xBuilder->weld_radio_button(u"hueRadiobutton"_ustr))
    , m_xRBSaturation(m_xBuilder->weld_radio_button(u"satRadiobutton"_ustr))
    , m_xRBBrightness(m_xBuilder->weld_radio_button(u"brightRadiobutton"_ustr))
    , m_xMFRed(m_xBuilder->weld_spin_button(u"redSpinbutton"_ustr))
    , m_xMFGreen(m_xBuilder->weld_spin_button(u"greenSpinbutton"_ustr))
    , m_xMFBlue(m_xBuilder->weld_spin_button(u"blueSpinbutton"_ustr))
    , m_xEDHex(new weld::HexColorControl(m_xBuilder->weld_entry(u"hexEntry"_ustr)))
    , m_xMFHue(m_xBuilder->weld_metric_spin_button(u"hueSpinbutton"_ustr, FieldUnit::DEGREE))
    , m_xMFSaturation(m_xBuilder->weld_metric_spin_button(u"satSpinbutton"_ustr, FieldUnit::PERCENT))
    , m_xMFBrightness(m_xBuilder->weld_metric_spin_button(u"brightSpinbutton"_ustr, FieldUnit::PERCENT))
    , m_xMFCyan(m_xBuilder->weld_metric_spin_button(u"cyanSpinbutton"_ustr, FieldUnit::PERCENT))
    , m_xMFMagenta(m_xBuilder->weld_metric_spin_button(u"magSpinbutton"_ustr, FieldUnit::PERCENT))
    , m_xMFYellow(m_xBuilder->weld_metric_spin_button(u"yellowSpinbutton"_ustr, FieldUnit::PERCENT))
    , m_xMFKey(m_xBuilder->weld_metric_spin_button(u"keySpinbutton"_ustr, FieldUnit::PERCENT))
    , meMode( DefaultMode )
{
    m_aColorField.SetModifyHdl( LINK( this, ColorPickerDialog, ColorFieldControlModifydl ) );
    m_aColorSlider.SetModifyHdl( LINK( this, ColorPickerDialog, ColorSliderControlModifyHdl ) );

    int nMargin = (m_xFISliderLeft->get_preferred_size().Height() + 1) / 2;
    m_xColorSlider->set_margin_top(nMargin);
    m_xColorSlider->set_margin_bottom(nMargin);

    Link<weld::MetricSpinButton&,void> aLink3( LINK( this, ColorPickerDialog, ColorModifyMetricHdl ) );
    m_xMFCyan->connect_value_changed( aLink3 );
    m_xMFMagenta->connect_value_changed( aLink3 );
    m_xMFYellow->connect_value_changed( aLink3 );
    m_xMFKey->connect_value_changed( aLink3 );

    m_xMFHue->connect_value_changed( aLink3 );
    m_xMFSaturation->connect_value_changed( aLink3 );
    m_xMFBrightness->connect_value_changed( aLink3 );

    Link<weld::SpinButton&,void> aLink4(LINK(this, ColorPickerDialog, ColorModifySpinHdl));
    m_xMFRed->connect_value_changed(aLink4);
    m_xMFGreen->connect_value_changed(aLink4);
    m_xMFBlue->connect_value_changed(aLink4);

    m_xEDHex->connect_changed(LINK(this, ColorPickerDialog, ColorModifyEditHdl));

    Link<weld::Toggleable&,void> aLink2 = LINK( this, ColorPickerDialog, ModeModifyHdl );
    m_xRBRed->connect_toggled( aLink2 );
    m_xRBGreen->connect_toggled( aLink2 );
    m_xRBBlue->connect_toggled( aLink2 );
    m_xRBHue->connect_toggled( aLink2 );
    m_xRBSaturation->connect_toggled( aLink2 );
    m_xRBBrightness->connect_toggled( aLink2 );

    if (eDialogMode == vcl::ColorPickerMode::Modify)
        m_xColorPrevious->show();

    SetColor(rColor);
}

static int toInt( double dValue, double dRange )
{
    return static_cast< int >( std::floor((dValue * dRange) + 0.5 ) );
}

Color ColorPickerDialog::GetColor() const
{
    return Color( toInt(mdRed,255.0), toInt(mdGreen,255.0), toInt(mdBlue,255.0) );
}

void ColorPickerDialog::SetColor(const Color& rColor)
{
    if (m_xColorPrevious->get_visible())
        m_aColorPrevious.SetColor(rColor);

    mdRed = static_cast<double>(rColor.GetRed()) / 255.0;
    mdGreen = static_cast<double>(rColor.GetGreen()) / 255.0;
    mdBlue = static_cast<double>(rColor.GetBlue()) / 255.0;

    RGBtoHSV(mdRed, mdGreen, mdBlue, mdHue, mdSat, mdBri);
    RGBtoCMYK(mdRed, mdGreen, mdBlue, mdCyan, mdMagenta, mdYellow, mdKey);

    update_color();
}

void ColorPickerDialog::update_color( UpdateFlags n )
{
    sal_uInt8 nRed = toInt(mdRed,255.0);
    sal_uInt8 nGreen = toInt(mdGreen,255.0);
    sal_uInt8 nBlue = toInt(mdBlue,255.0);

    sal_uInt16 nHue = toInt(mdHue, 1.0);
    sal_uInt16 nSat = toInt(mdSat, 100.0);
    sal_uInt16 nBri = toInt(mdBri, 100.0);

    if (n & UpdateFlags::RGB) // update RGB
    {
        m_xMFRed->set_value(nRed);
        m_xMFGreen->set_value(nGreen);
        m_xMFBlue->set_value(nBlue);
    }

    if (n & UpdateFlags::CMYK) // update CMYK
    {
        m_xMFCyan->set_value(toInt(mdCyan, 100.0), FieldUnit::PERCENT);
        m_xMFMagenta->set_value(toInt(mdMagenta, 100.0), FieldUnit::PERCENT);
        m_xMFYellow->set_value(toInt(mdYellow, 100.0), FieldUnit::PERCENT);
        m_xMFKey->set_value(toInt(mdKey, 100.0), FieldUnit::PERCENT);
    }

    if (n & UpdateFlags::HSB ) // update HSB
    {
        m_xMFHue->set_value(nHue, FieldUnit::DEGREE);
        m_xMFSaturation->set_value(nSat, FieldUnit::PERCENT);
        m_xMFBrightness->set_value(nBri, FieldUnit::PERCENT);
    }

    if (n & UpdateFlags::ColorChooser ) // update Color Chooser 1
    {
        switch( meMode )
        {
        case HUE:
            m_aColorField.SetValues(nHue, meMode, mdSat, mdBri);
            break;
        case SATURATION:
            m_aColorField.SetValues(nSat, meMode, mdHue / 360.0, mdBri);
            break;
        case BRIGHTNESS:
            m_aColorField.SetValues(nBri, meMode, mdHue / 360.0, mdSat);
            break;
        case RED:
            m_aColorField.SetValues(nRed, meMode, mdBlue, mdGreen);
            break;
        case GREEN:
            m_aColorField.SetValues(nGreen, meMode, mdBlue, mdRed);
            break;
        case BLUE:
            m_aColorField.SetValues(nBlue, meMode, mdRed, mdGreen);
            break;
        }
    }

    Color aColor(nRed, nGreen, nBlue);

    if (n & UpdateFlags::ColorSlider) // update Color Chooser 2
    {
        switch (meMode)
        {
        case HUE:
            m_aColorSlider.SetValue(aColor, meMode, mdHue / 360.0);
            break;
        case SATURATION:
            m_aColorSlider.SetValue(aColor, meMode, mdSat);
            break;
        case BRIGHTNESS:
            m_aColorSlider.SetValue(aColor, meMode, mdBri);
            break;
        case RED:
            m_aColorSlider.SetValue(aColor, meMode, mdRed);
            break;
        case GREEN:
            m_aColorSlider.SetValue(aColor, meMode, mdGreen);
            break;
        case BLUE:
            m_aColorSlider.SetValue(aColor, meMode, mdBlue);
            break;
        }
    }

    if (n & UpdateFlags::Hex) // update hex
    {
        m_xFISliderLeft->set_margin_top(m_aColorSlider.GetLevel());
        m_xFISliderRight->set_margin_top(m_aColorSlider.GetLevel());
        m_xEDHex->SetColor(aColor);
    }
    m_aColorPreview.SetColor(aColor);
}

IMPL_LINK_NOARG(ColorPickerDialog, ColorFieldControlModifydl, ColorFieldControl&, void)
{
    double x = m_aColorField.GetX();
    double y = m_aColorField.GetY();

    switch( meMode )
    {
    case HUE:
        mdSat = x;
        setColorComponent( ColorComponent::Brightness, y );
        break;
    case SATURATION:
        mdHue = x * 360.0;
        setColorComponent( ColorComponent::Brightness, y );
        break;
    case BRIGHTNESS:
        mdHue = x * 360.0;
        setColorComponent( ColorComponent::Saturation, y );
        break;
    case RED:
        mdBlue = x;
        setColorComponent( ColorComponent::Green, y );
        break;
    case GREEN:
        mdBlue = x;
        setColorComponent( ColorComponent::Red, y );
        break;
    case BLUE:
        mdRed = x;
        setColorComponent( ColorComponent::Green, y );
        break;
    }

    update_color(UpdateFlags::All & ~UpdateFlags::ColorChooser);
}

IMPL_LINK_NOARG(ColorPickerDialog, ColorSliderControlModifyHdl, ColorSliderControl&, void)
{
    double dValue = m_aColorSlider.GetValue();
    switch (meMode)
    {
    case HUE:
        setColorComponent( ColorComponent::Hue, dValue * 360.0 );
        break;
    case SATURATION:
        setColorComponent( ColorComponent::Saturation, dValue );
        break;
    case BRIGHTNESS:
        setColorComponent( ColorComponent::Brightness, dValue );
        break;
    case RED:
        setColorComponent( ColorComponent::Red, dValue );
        break;
    case GREEN:
        setColorComponent( ColorComponent::Green, dValue );
        break;
    case BLUE:
        setColorComponent( ColorComponent::Blue, dValue );
        break;
    }

    update_color(UpdateFlags::All & ~UpdateFlags::ColorSlider);
}

IMPL_LINK(ColorPickerDialog, ColorModifyMetricHdl, weld::MetricSpinButton&, rEdit, void)
{
    UpdateFlags n = UpdateFlags::NONE;

    if (&rEdit == m_xMFHue.get())
    {
        setColorComponent( ColorComponent::Hue, static_cast<double>(m_xMFHue->get_value(FieldUnit::DEGREE)) );
        n = UpdateFlags::All & ~UpdateFlags::HSB;
    }
    else if (&rEdit == m_xMFSaturation.get())
    {
        setColorComponent( ColorComponent::Saturation, static_cast<double>(m_xMFSaturation->get_value(FieldUnit::PERCENT)) / 100.0 );
        n = UpdateFlags::All & ~UpdateFlags::HSB;
    }
    else if (&rEdit == m_xMFBrightness.get())
    {
        setColorComponent( ColorComponent::Brightness, static_cast<double>(m_xMFBrightness->get_value(FieldUnit::PERCENT)) / 100.0 );
        n = UpdateFlags::All & ~UpdateFlags::HSB;
    }
    else if (&rEdit == m_xMFCyan.get())
    {
        setColorComponent( ColorComponent::Cyan, static_cast<double>(m_xMFCyan->get_value(FieldUnit::PERCENT)) / 100.0 );
        n = UpdateFlags::All & ~UpdateFlags::CMYK;
    }
    else if (&rEdit == m_xMFMagenta.get())
    {
        setColorComponent( ColorComponent::Magenta, static_cast<double>(m_xMFMagenta->get_value(FieldUnit::PERCENT)) / 100.0 );
        n = UpdateFlags::All & ~UpdateFlags::CMYK;
    }
    else if (&rEdit == m_xMFYellow.get())
    {
        setColorComponent( ColorComponent::Yellow, static_cast<double>(m_xMFYellow->get_value(FieldUnit::PERCENT)) / 100.0 );
        n = UpdateFlags::All & ~UpdateFlags::CMYK;
    }
    else if (&rEdit == m_xMFKey.get())
    {
        setColorComponent( ColorComponent::Key, static_cast<double>(m_xMFKey->get_value(FieldUnit::PERCENT)) / 100.0 );
        n = UpdateFlags::All & ~UpdateFlags::CMYK;
    }

    if (n != UpdateFlags::NONE)
        update_color(n);
}

IMPL_LINK_NOARG(ColorPickerDialog, ColorModifyEditHdl, weld::Entry&, void)
{
    UpdateFlags n = UpdateFlags::NONE;

    Color aColor = m_xEDHex->GetColor();

    if (aColor != COL_AUTO && aColor != GetColor())
    {
        mdRed = static_cast<double>(aColor.GetRed()) / 255.0;
        mdGreen = static_cast<double>(aColor.GetGreen()) / 255.0;
        mdBlue = static_cast<double>(aColor.GetBlue()) / 255.0;

        RGBtoHSV( mdRed, mdGreen, mdBlue, mdHue, mdSat, mdBri );
        RGBtoCMYK( mdRed, mdGreen, mdBlue, mdCyan, mdMagenta, mdYellow, mdKey );
        n = UpdateFlags::All & ~UpdateFlags::Hex;
    }

    if (n != UpdateFlags::NONE)
        update_color(n);
}

IMPL_LINK(ColorPickerDialog, ColorModifySpinHdl, weld::SpinButton&, rEdit, void)
{
    UpdateFlags n = UpdateFlags::NONE;

    if (&rEdit == m_xMFRed.get())
    {
        setColorComponent( ColorComponent::Red, static_cast<double>(m_xMFRed->get_value()) / 255.0 );
        n = UpdateFlags::All & ~UpdateFlags::RGB;
    }
    else if (&rEdit == m_xMFGreen.get())
    {
        setColorComponent( ColorComponent::Green, static_cast<double>(m_xMFGreen->get_value()) / 255.0 );
        n = UpdateFlags::All & ~UpdateFlags::RGB;
    }
    else if (&rEdit == m_xMFBlue.get())
    {
        setColorComponent( ColorComponent::Blue, static_cast<double>(m_xMFBlue->get_value()) / 255.0 );
        n = UpdateFlags::All & ~UpdateFlags::RGB;
    }

    if (n != UpdateFlags::NONE)
        update_color(n);
}


IMPL_LINK_NOARG(ColorPickerDialog, ModeModifyHdl, weld::Toggleable&, void)
{
    ColorMode eMode = HUE;

    if (m_xRBRed->get_active())
    {
        eMode = RED;
    }
    else if (m_xRBGreen->get_active())
    {
        eMode = GREEN;
    }
    else if (m_xRBBlue->get_active())
    {
        eMode = BLUE;
    }
    else if (m_xRBSaturation->get_active())
    {
        eMode = SATURATION;
    }
    else if (m_xRBBrightness->get_active())
    {
        eMode = BRIGHTNESS;
    }

    if (meMode != eMode)
    {
        meMode = eMode;
        update_color(UpdateFlags::ColorChooser | UpdateFlags::ColorSlider);
    }
}

void ColorPickerDialog::setColorComponent( ColorComponent nComp, double dValue )
{
    switch( nComp )
    {
    case ColorComponent::Red:
        mdRed = dValue;
        break;
    case ColorComponent::Green:
        mdGreen = dValue;
        break;
    case ColorComponent::Blue:
        mdBlue = dValue;
        break;
    case ColorComponent::Hue:
        mdHue = dValue;
        break;
    case ColorComponent::Saturation:
        mdSat = dValue;
        break;
    case ColorComponent::Brightness:
        mdBri = dValue;
        break;
    case ColorComponent::Cyan:
        mdCyan = dValue;
        break;
    case ColorComponent::Yellow:
        mdYellow = dValue;
        break;
    case ColorComponent::Magenta:
        mdMagenta = dValue;
        break;
    case ColorComponent::Key:
        mdKey = dValue;
        break;
    }

    if (nComp == ColorComponent::Red || nComp == ColorComponent::Green || nComp == ColorComponent::Blue)
    {
        RGBtoHSV( mdRed, mdGreen, mdBlue, mdHue, mdSat, mdBri );
        RGBtoCMYK( mdRed, mdGreen, mdBlue, mdCyan, mdMagenta, mdYellow, mdKey );
    }
    else if (nComp == ColorComponent::Hue || nComp == ColorComponent::Saturation || nComp == ColorComponent::Brightness)
    {
        HSVtoRGB( mdHue, mdSat, mdBri, mdRed, mdGreen, mdBlue );
        RGBtoCMYK( mdRed, mdGreen, mdBlue, mdCyan, mdMagenta, mdYellow, mdKey );
    }
    else
    {
        CMYKtoRGB( mdCyan, mdMagenta, mdYellow, mdKey, mdRed, mdGreen, mdBlue );
        RGBtoHSV( mdRed, mdGreen, mdBlue, mdHue, mdSat, mdBri );
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
