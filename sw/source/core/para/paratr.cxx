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

#include <hintids.hxx>
#include <unomid.h>
#include <com/sun/star/style/DropCapFormat.hpp>
#include <o3tl/any.hxx>
#include <SwStyleNameMapper.hxx>
#include <paratr.hxx>
#include <charfmt.hxx>
#include <libxml/xmlwriter.h>
#include <osl/diagnose.h>
#include <tools/UnitConversion.hxx>
#include <o3tl/hash_combine.hxx>
#include <names.hxx>

using namespace ::com::sun::star;


SfxPoolItem* SwFormatDrop::CreateDefault() { return new SwFormatDrop; }
SfxPoolItem* SwRegisterItem::CreateDefault() { return new SwRegisterItem; }
SfxPoolItem* SwNumRuleItem::CreateDefault() { return new SwNumRuleItem; }

SwFormatDrop::SwFormatDrop()
    : SfxPoolItem( RES_PARATR_DROP ),
    SwClient( nullptr ),
    m_pDefinedIn( nullptr ),
    m_nDistance( 0 ),
    m_nLines( 0 ),
    m_nChars( 0 ),
    m_bWholeWord( false )
{
    setNonShareable();
}

SwFormatDrop::SwFormatDrop( const SwFormatDrop &rCpy )
    : SfxPoolItem( RES_PARATR_DROP ),
    SwClient( rCpy.GetRegisteredInNonConst() ),
    m_pDefinedIn( nullptr ),
    m_nDistance( rCpy.GetDistance() ),
    m_nLines( rCpy.GetLines() ),
    m_nChars( rCpy.GetChars() ),
    m_bWholeWord( rCpy.GetWholeWord() )
{
    setNonShareable();
}

SwFormatDrop::~SwFormatDrop()
{
}

void SwFormatDrop::SetCharFormat( SwCharFormat *pNew )
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    assert(!pNew || !pNew->IsDefault()); // expose cases that lead to use-after-free
    // Rewire
    EndListeningAll();
    if(pNew)
        pNew->Add(*this);
}

bool SwFormatDrop::GetInfo( SwFindNearestNode& ) const
{
    return true; // Continue
}

bool SwFormatDrop::operator==( const SfxPoolItem& rAttr ) const
{
    assert(SfxPoolItem::operator==(rAttr));
    return ( m_nLines == static_cast<const SwFormatDrop&>(rAttr).GetLines() &&
             m_nChars == static_cast<const SwFormatDrop&>(rAttr).GetChars() &&
             m_nDistance ==  static_cast<const SwFormatDrop&>(rAttr).GetDistance() &&
             m_bWholeWord == static_cast<const SwFormatDrop&>(rAttr).GetWholeWord() &&
             GetCharFormat() == static_cast<const SwFormatDrop&>(rAttr).GetCharFormat() &&
             m_pDefinedIn == static_cast<const SwFormatDrop&>(rAttr).m_pDefinedIn );
}

size_t SwFormatDrop::hashCode() const
{
    std::size_t seed(0);
    o3tl::hash_combine(seed, m_nLines);
    o3tl::hash_combine(seed, m_nChars);
    o3tl::hash_combine(seed, m_nDistance);
    o3tl::hash_combine(seed, m_bWholeWord);
    o3tl::hash_combine(seed, GetCharFormat());
    // note that we cannot use m_pDefinedIn, that is updated on items already in a pool
    return seed;
}

SwFormatDrop* SwFormatDrop::Clone( SfxItemPool* ) const
{
    return new SwFormatDrop( *this );
}

bool SwFormatDrop::QueryValue( uno::Any& rVal, sal_uInt8 nMemberId ) const
{
    switch(nMemberId&~CONVERT_TWIPS)
    {
        case MID_DROPCAP_LINES : rVal <<= static_cast<sal_Int16>(m_nLines); break;
        case MID_DROPCAP_COUNT : rVal <<= static_cast<sal_Int16>(m_nChars); break;
        case MID_DROPCAP_DISTANCE : rVal <<= static_cast<sal_Int16>(convertTwipToMm100(m_nDistance)); break;
        case MID_DROPCAP_FORMAT:
        {
            style::DropCapFormat aDrop;
            aDrop.Lines = m_nLines   ;
            aDrop.Count = m_nChars   ;
            aDrop.Distance  = convertTwipToMm100(m_nDistance);
            rVal <<= aDrop;
        }
        break;
        case MID_DROPCAP_WHOLE_WORD:
            rVal <<= m_bWholeWord;
        break;
        case MID_DROPCAP_CHAR_STYLE_NAME :
        {
            ProgName sName;
            if(GetCharFormat())
                sName = SwStyleNameMapper::GetProgName(
                        GetCharFormat()->GetName(), SwGetPoolIdFromName::ChrFmt );
            rVal <<= sName.toString();
        }
        break;
    }
    return true;
}

bool SwFormatDrop::PutValue( const uno::Any& rVal, sal_uInt8 nMemberId )
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    switch(nMemberId&~CONVERT_TWIPS)
    {
        case MID_DROPCAP_LINES :
        {
            sal_Int8 nTemp = 0;
            rVal >>= nTemp;
            if(nTemp >=1 && nTemp < 0x7f)
                m_nLines = static_cast<sal_uInt8>(nTemp);
        }
        break;
        case MID_DROPCAP_COUNT :
        {
            sal_Int16 nTemp = 0;
            rVal >>= nTemp;
            if(nTemp >=1 && nTemp < 0x7f)
                m_nChars = static_cast<sal_uInt8>(nTemp);
        }
        break;
        case MID_DROPCAP_DISTANCE :
        {
            sal_Int16 nVal = 0;
            if ( rVal >>= nVal )
                m_nDistance = o3tl::toTwips(nVal, o3tl::Length::mm100);
            else
                return false;
            break;
        }
        case MID_DROPCAP_FORMAT:
        {
            if(rVal.getValueType()  == ::cppu::UnoType<style::DropCapFormat>::get())
            {
                auto pDrop = o3tl::doAccess<style::DropCapFormat>(rVal);
                m_nLines      = pDrop->Lines;
                m_nChars      = pDrop->Count;
                m_nDistance   = o3tl::toTwips(pDrop->Distance, o3tl::Length::mm100);
            }
        }
        break;
        case MID_DROPCAP_WHOLE_WORD:
            m_bWholeWord = *o3tl::doAccess<bool>(rVal);
        break;
        case MID_DROPCAP_CHAR_STYLE_NAME :
            OSL_FAIL("char format cannot be set in PutValue()!");
        break;
    }
    return true;
}

SwRegisterItem* SwRegisterItem::Clone( SfxItemPool * ) const
{
    return new SwRegisterItem( *this );
}

SwNumRuleItem* SwNumRuleItem::Clone( SfxItemPool * ) const
{
    return new SwNumRuleItem( *this );
}

bool SwNumRuleItem::operator==( const SfxPoolItem& rAttr ) const
{
    assert(SfxPoolItem::operator==(rAttr));

    return GetValue() == static_cast<const SwNumRuleItem&>(rAttr).GetValue();
}

bool    SwNumRuleItem::QueryValue( uno::Any& rVal, sal_uInt8 ) const
{
    ProgName sRet = SwStyleNameMapper::GetProgName(GetValue(), SwGetPoolIdFromName::NumRule );
    rVal <<= sRet.toString();
    return true;
}

bool    SwNumRuleItem::PutValue( const uno::Any& rVal, sal_uInt8 )
{
    OUString uName;
    rVal >>= uName;
    SetValue(SwStyleNameMapper::GetUIName(ProgName(uName), SwGetPoolIdFromName::NumRule).toString());
    return true;
}

void SwNumRuleItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SwNumRuleItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("whichId"), BAD_CAST(OString::number(Which()).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("value"), BAD_CAST(GetValue().toString().toUtf8().getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}

SwParaConnectBorderItem* SwParaConnectBorderItem::Clone( SfxItemPool * ) const
{
    return new SwParaConnectBorderItem( *this );
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
