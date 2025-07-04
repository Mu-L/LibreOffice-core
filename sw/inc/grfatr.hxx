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
#ifndef INCLUDED_SW_INC_GRFATR_HXX
#define INCLUDED_SW_INC_GRFATR_HXX

#include "hintids.hxx"
#include <tools/gen.hxx>
#include <svl/eitem.hxx>
#include <svl/intitem.hxx>
#include <vcl/GraphicAttributes.hxx>
#include <svx/grfcrop.hxx>
#include "swdllapi.h"
#include "swatrset.hxx"

enum class MirrorGraph
{
    Dont,
    Vertical,
    Horizontal,
    Both
};

class SW_DLLPUBLIC SwMirrorGrf final : public SfxEnumItem<MirrorGraph>
{
    bool m_bGrfToggle; // Flip graphics on even pages.

public:
    DECLARE_ITEM_TYPE_FUNCTION(SwMirrorGrf)
    SwMirrorGrf( MirrorGraph eMiro = MirrorGraph::Dont )
        : SfxEnumItem( RES_GRFATR_MIRRORGRF, eMiro ), m_bGrfToggle( false )
    {}

    // pure virtual methods of SfxPoolItem
    virtual SwMirrorGrf* Clone( SfxItemPool *pPool = nullptr ) const override;

    // pure virtual methods of SfxEnumItem
    virtual bool            operator==( const SfxPoolItem& ) const override;
    virtual bool GetPresentation( SfxItemPresentation ePres,
                                  MapUnit eCoreMetric,
                                  MapUnit ePresMetric,
                                  OUString &rText,
                                  const IntlWrapper& rIntl ) const override;

    virtual bool             QueryValue( css::uno::Any& rVal,
                                        sal_uInt8 nMemberId = 0 ) const override;
    virtual bool             PutValue( const css::uno::Any& rVal,
                                        sal_uInt8 nMemberId ) override;

    bool IsGrfToggle() const         { return m_bGrfToggle; }
    void SetGrfToggle( bool bNew )   { m_bGrfToggle = bNew; }
};

class SW_DLLPUBLIC SwCropGrf final : public SvxGrfCrop
{
public:
    DECLARE_ITEM_TYPE_FUNCTION(SwCropGrf)
    SwCropGrf();
    SwCropGrf(  sal_Int32 nLeft,    sal_Int32 nRight,
                sal_Int32 nTop,     sal_Int32 nBottom );

    // "pure virtual methods" of SfxPoolItem
    virtual SwCropGrf* Clone( SfxItemPool *pPool = nullptr ) const override;
};

class SAL_DLLPUBLIC_RTTI SwRotationGrf final : public SfxUInt16Item
{
private:
    Size m_aUnrotatedSize;

    // tdf#115529 check and evtl. correct value, it is in 10th
    // degrees and *has* to be in the range [0 .. 3600[
    static Degree10 checkAndCorrectValue(Degree10 nValue);

public:
    DECLARE_ITEM_TYPE_FUNCTION(SwRotationGrf)
    SwRotationGrf()
        : SfxUInt16Item( RES_GRFATR_ROTATION, 0 )
    {}
    SwRotationGrf( Degree10 nVal, const Size& rSz );

    // pure virtual methods from SfxInt16Item
    virtual SwRotationGrf* Clone( SfxItemPool *pPool = nullptr ) const override;
    virtual bool            operator==( const SfxPoolItem& ) const override;
    virtual bool GetPresentation( SfxItemPresentation ePres,
                                  MapUnit eCoreMetric,
                                  MapUnit ePresMetric,
                                  OUString &rText,
                                  const IntlWrapper& rIntl ) const override;
    virtual bool             QueryValue( css::uno::Any& rVal,
                                            sal_uInt8 nMemberId = 0 ) const override;
    virtual bool             PutValue( const css::uno::Any& rVal,
                                            sal_uInt8 nMemberId ) override;

    const Size& GetUnrotatedSize() const { return m_aUnrotatedSize; }
    Degree10 GetValue() const { return Degree10(SfxUInt16Item::GetValue()); }
    void SetValue(Degree10 d) { SfxUInt16Item::SetValue(d.get()); }
};

class SW_DLLPUBLIC SwLuminanceGrf final : public SfxInt16Item
{
public:
    DECLARE_ITEM_TYPE_FUNCTION(SwLuminanceGrf)
    SwLuminanceGrf( sal_Int16 nVal = 0 )
        : SfxInt16Item( RES_GRFATR_LUMINANCE, nVal )
    {}

    // pure virtual methods from SfxInt16Item
    virtual SwLuminanceGrf* Clone( SfxItemPool *pPool = nullptr ) const override;
    virtual bool GetPresentation( SfxItemPresentation ePres,
                                  MapUnit eCoreMetric,
                                  MapUnit ePresMetric,
                                  OUString &rText,
                                  const IntlWrapper& rIntl ) const override;
};

class SW_DLLPUBLIC SwContrastGrf final : public SfxInt16Item
{
public:
    DECLARE_ITEM_TYPE_FUNCTION(SwContrastGrf)
    SwContrastGrf( sal_Int16 nVal = 0 )
        : SfxInt16Item( RES_GRFATR_CONTRAST, nVal )
    {}

    // pure virtual methods from SfxInt16Item
    virtual SwContrastGrf* Clone( SfxItemPool *pPool = nullptr ) const override;
    virtual bool GetPresentation( SfxItemPresentation ePres,
                                  MapUnit eCoreMetric,
                                  MapUnit ePresMetric,
                                  OUString &rText,
                                  const IntlWrapper& rIntl ) const override;
};

class SwChannelGrf : public SfxInt16Item
{
protected:
    SwChannelGrf( sal_Int16 nVal, sal_uInt16 nWhichL )
        : SfxInt16Item( nWhichL, nVal )
    {}

    virtual SfxItemType ItemType() const override = 0;

public:
    // pure virtual methods from SfxInt16Item
    virtual bool GetPresentation( SfxItemPresentation ePres,
                                  MapUnit eCoreMetric,
                                  MapUnit ePresMetric,
                                  OUString &rText,
                                  const IntlWrapper& rIntl ) const override;
};

class SwChannelRGrf final : public SwChannelGrf
{
public:
    DECLARE_ITEM_TYPE_FUNCTION(SwChannelRGrf)
    SwChannelRGrf( sal_Int16 nVal = 0 )
        : SwChannelGrf( nVal, RES_GRFATR_CHANNELR )
    {}
    virtual SwChannelRGrf* Clone( SfxItemPool *pPool = nullptr ) const override;
};
class SwChannelGGrf final : public SwChannelGrf
{
public:
    DECLARE_ITEM_TYPE_FUNCTION(SwChannelGGrf)
    SwChannelGGrf( sal_Int16 nVal = 0 )
        : SwChannelGrf( nVal, RES_GRFATR_CHANNELG )
    {}
    virtual SwChannelGGrf* Clone( SfxItemPool *pPool = nullptr ) const override;
};
class SwChannelBGrf final : public SwChannelGrf
{
public:
    DECLARE_ITEM_TYPE_FUNCTION(SwChannelBGrf)
    SwChannelBGrf( sal_Int16 nVal = 0 )
        : SwChannelGrf( nVal, RES_GRFATR_CHANNELB )
    {}
    virtual SwChannelBGrf* Clone( SfxItemPool *pPool = nullptr ) const override;
};

class SW_DLLPUBLIC SwGammaGrf final : public SfxPoolItem
{
    double m_nValue;
public:
    DECLARE_ITEM_TYPE_FUNCTION(SwGammaGrf)
    SwGammaGrf() : SfxPoolItem( RES_GRFATR_GAMMA ), m_nValue( 1.0 )
    {}

    SwGammaGrf( const double& rVal )
        : SfxPoolItem( RES_GRFATR_GAMMA ), m_nValue( rVal )
    {}

    // pure virtual methods from SfxEnumItem
    virtual SwGammaGrf*     Clone( SfxItemPool *pPool = nullptr ) const override;
    virtual bool            operator==( const SfxPoolItem& ) const override;
    virtual bool            supportsHashCode() const override { return true; }
    virtual size_t          hashCode() const override { return static_cast<size_t>(m_nValue); }
    virtual bool GetPresentation( SfxItemPresentation ePres,
                                  MapUnit eCoreMetric,
                                  MapUnit ePresMetric,
                                  OUString &rText,
                                  const IntlWrapper& rIntl ) const override;

    virtual bool             QueryValue( css::uno::Any& rVal,
                                            sal_uInt8 nMemberId = 0 ) const override;
    virtual bool             PutValue( const css::uno::Any& rVal,
                                            sal_uInt8 nMemberId ) override;

    const double& GetValue() const              { return m_nValue; }
};

class SwInvertGrf final : public SfxBoolItem
{
public:
    DECLARE_ITEM_TYPE_FUNCTION(SwInvertGrf)
    SwInvertGrf( bool bVal = false )
        : SfxBoolItem( RES_GRFATR_INVERT, bVal )
    {}

    // pure virtual methods from SfxInt16Item
    virtual SwInvertGrf* Clone( SfxItemPool *pPool = nullptr ) const override;
    virtual bool GetPresentation( SfxItemPresentation ePres,
                                  MapUnit eCoreMetric,
                                  MapUnit ePresMetric,
                                  OUString &rText,
                                  const IntlWrapper& rIntl ) const override;
};

class SwTransparencyGrf final : public SfxByteItem
{
public:
    DECLARE_ITEM_TYPE_FUNCTION(SwTransparencyGrf)
    SwTransparencyGrf( sal_Int8 nVal = 0 )
        : SfxByteItem( RES_GRFATR_TRANSPARENCY, nVal )
    {}

    // pure virtual methods from SfxInt16Item
    virtual SwTransparencyGrf* Clone( SfxItemPool *pPool = nullptr ) const override;
    virtual bool GetPresentation( SfxItemPresentation ePres,
                                  MapUnit eCoreMetric,
                                  MapUnit ePresMetric,
                                  OUString &rText,
                                  const IntlWrapper& rIntl ) const override;
    virtual bool            QueryValue( css::uno::Any& rVal,
                                        sal_uInt8 nMemberId = 0 ) const override;
    virtual bool            PutValue( const css::uno::Any& rVal,
                                        sal_uInt8 nMemberId ) override;
};

// MSVC hack:
class SwDrawModeGrf_Base: public SfxEnumItem<GraphicDrawMode> {
protected:
    SwDrawModeGrf_Base(GraphicDrawMode nMode):
        SfxEnumItem(RES_GRFATR_DRAWMODE, nMode) {}

    virtual SfxItemType ItemType() const override = 0;
};

class SW_DLLPUBLIC SwDrawModeGrf final : public SwDrawModeGrf_Base
{
public:
    DECLARE_ITEM_TYPE_FUNCTION(SwDrawModeGrf)
    SwDrawModeGrf( GraphicDrawMode nMode = GraphicDrawMode::Standard )
        : SwDrawModeGrf_Base( nMode )
    {}

    // pure virtual methods of SfxPoolItem
    virtual SwDrawModeGrf*  Clone( SfxItemPool *pPool = nullptr ) const override;

    // pure virtual methods of SfxEnumItem
    virtual bool GetPresentation( SfxItemPresentation ePres,
                                  MapUnit eCoreMetric,
                                  MapUnit ePresMetric,
                                  OUString &rText,
                                  const IntlWrapper& rIntl ) const override;

    virtual bool            QueryValue( css::uno::Any& rVal,
                                        sal_uInt8 nMemberId = 0 ) const override;
    virtual bool            PutValue( const css::uno::Any& rVal,
                                        sal_uInt8 nMemberId ) override;
};

// Implementation of graphics attributes methods of SwAttr
inline const SwMirrorGrf &SwAttrSet::GetMirrorGrf(bool bInP) const
    { return Get( RES_GRFATR_MIRRORGRF,bInP); }
inline const SwCropGrf   &SwAttrSet::GetCropGrf(bool bInP) const
    { return Get( RES_GRFATR_CROPGRF,bInP); }
inline const SwRotationGrf &SwAttrSet::GetRotationGrf(bool bInP) const
    { return Get( RES_GRFATR_ROTATION,bInP); }
inline const SwLuminanceGrf &SwAttrSet::GetLuminanceGrf(bool bInP) const
    { return Get( RES_GRFATR_LUMINANCE,bInP); }
inline const SwContrastGrf &SwAttrSet::GetContrastGrf(bool bInP) const
    { return Get( RES_GRFATR_CONTRAST,bInP); }
inline const SwChannelRGrf &SwAttrSet::GetChannelRGrf(bool bInP) const
    { return Get( RES_GRFATR_CHANNELR,bInP); }
inline const SwChannelGGrf &SwAttrSet::GetChannelGGrf(bool bInP) const
    { return Get( RES_GRFATR_CHANNELG,bInP); }
inline const SwChannelBGrf &SwAttrSet::GetChannelBGrf(bool bInP) const
    { return Get( RES_GRFATR_CHANNELB,bInP); }
inline const SwGammaGrf &SwAttrSet::GetGammaGrf(bool bInP) const
    { return Get( RES_GRFATR_GAMMA,bInP); }
inline const SwInvertGrf &SwAttrSet::GetInvertGrf(bool bInP) const
    { return Get( RES_GRFATR_INVERT,bInP); }
inline const SwTransparencyGrf &SwAttrSet::GetTransparencyGrf(bool bInP) const
    { return Get( RES_GRFATR_TRANSPARENCY,bInP); }
inline const SwDrawModeGrf      &SwAttrSet::GetDrawModeGrf(bool bInP) const
    { return Get( RES_GRFATR_DRAWMODE,bInP); }

#endif // INCLUDED_SW_INC_GRFATR_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
