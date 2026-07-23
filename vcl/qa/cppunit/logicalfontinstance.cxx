/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sal/config.h>

#include <test/bootstrapfixture.hxx>
#include <config_fonts.h>
#include <cppunit/TestAssert.h>

#include <vcl/virdev.hxx>
#include <vcl/font.hxx>

#include <font/LogicalFontInstance.hxx>

class VclLogicalFontInstanceTest : public test::BootstrapFixture
{
public:
    VclLogicalFontInstanceTest()
        : BootstrapFixture(true, false)
    {
    }

    void testglyphboundrect();
    void testglyphoutline();

    CPPUNIT_TEST_SUITE(VclLogicalFontInstanceTest);
    CPPUNIT_TEST(testglyphboundrect);
    CPPUNIT_TEST(testglyphoutline);

    CPPUNIT_TEST_SUITE_END();
};

void VclLogicalFontInstanceTest::testglyphboundrect()
{
    ScopedVclPtr<VirtualDevice> device = VclPtr<VirtualDevice>::Create(DeviceFormat::WITHOUT_ALPHA);
    device->SetOutputSizePixel(Size(1000, 1000));
    vcl::Font font(u"Liberation Sans"_ustr, Size(0, 110));
    device->SetFont(font);

    const LogicalFontInstance* pFontInstance = device->GetFontInstance();

    basegfx::B2DRectangle aBoundRect;
    const auto LATIN_SMALL_LETTER_B = 0x0062;
    const auto SECTION_SIGN = 0x00A7; // UTR#50: Vertical_Orientation (vo) property value U
    pFontInstance->GetGlyphBoundRect(pFontInstance->GetGlyphIndex(LATIN_SMALL_LETTER_B), aBoundRect,
                                     false);

    CPPUNIT_ASSERT_DOUBLES_EQUAL(7.1, aBoundRect.getMinX(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(-79.7, aBoundRect.getMinY(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(49.5, aBoundRect.getWidth(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(80.8, aBoundRect.getHeight(), 0.05);

    // tdf#160436: test vertically oriented glyphs
    pFontInstance->GetGlyphBoundRect(pFontInstance->GetGlyphIndex(SECTION_SIGN), aBoundRect, true);

    CPPUNIT_ASSERT_DOUBLES_EQUAL(-79.7, aBoundRect.getMinX(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(-55.0, aBoundRect.getMinY(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(88.9, aBoundRect.getWidth(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(48.8, aBoundRect.getHeight(), 0.05);

    font.SetOrientation(900_deg10);
    device->SetFont(font);

    pFontInstance = device->GetFontInstance();

    pFontInstance->GetGlyphBoundRect(pFontInstance->GetGlyphIndex(LATIN_SMALL_LETTER_B), aBoundRect,
                                     false);

    CPPUNIT_ASSERT_DOUBLES_EQUAL(-79.7, aBoundRect.getMinX(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(-56.6, aBoundRect.getMinY(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(80.8, aBoundRect.getWidth(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(49.5, aBoundRect.getHeight(), 0.05);

    // tdf#160436: test vertically oriented glyphs
    pFontInstance->GetGlyphBoundRect(pFontInstance->GetGlyphIndex(SECTION_SIGN), aBoundRect, true);

    CPPUNIT_ASSERT_DOUBLES_EQUAL(-55.0, aBoundRect.getMinX(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(-9.2, aBoundRect.getMinY(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(48.8, aBoundRect.getWidth(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(88.9, aBoundRect.getHeight(), 0.05);

    font.SetOrientation(450_deg10);
    device->SetFont(font);

    pFontInstance = device->GetFontInstance();

    pFontInstance->GetGlyphBoundRect(pFontInstance->GetGlyphIndex(LATIN_SMALL_LETTER_B), aBoundRect,
                                     false);

    CPPUNIT_ASSERT_DOUBLES_EQUAL(-51.3, aBoundRect.getMinX(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(-96.4, aBoundRect.getMinY(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(92.1, aBoundRect.getWidth(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(92.1, aBoundRect.getHeight(), 0.05);

    // tdf#160436: test vertically oriented glyphs
    pFontInstance->GetGlyphBoundRect(pFontInstance->GetGlyphIndex(SECTION_SIGN), aBoundRect, true);

    CPPUNIT_ASSERT_DOUBLES_EQUAL(-95.3, aBoundRect.getMinX(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(-45.4, aBoundRect.getMinY(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(97.4, aBoundRect.getWidth(), 0.05);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(97.4, aBoundRect.getHeight(), 0.05);
}

void VclLogicalFontInstanceTest::testglyphoutline()
{
#if HAVE_MORE_FONTS
    ScopedVclPtr<VirtualDevice> device = VclPtr<VirtualDevice>::Create(DeviceFormat::WITHOUT_ALPHA);
    device->SetOutputSizePixel(Size(1000, 1000));
    vcl::Font font(u"Liberation Sans"_ustr, Size(0, 110));
    device->SetFont(font);

    const LogicalFontInstance* pFontInstance = device->GetFontInstance();

    const auto LATIN_SMALL_LETTER_B = 0x0062;
    const auto nGlyph = pFontInstance->GetGlyphIndex(LATIN_SMALL_LETTER_B);

    // A normal glyph has a non-empty outline in device pixels, whose extent
    // matches its bounding rectangle (both come from HarfBuzz).
    basegfx::B2DPolyPolygon aOutline;
    CPPUNIT_ASSERT(pFontInstance->GetGlyphOutline(nGlyph, aOutline, false));
    CPPUNIT_ASSERT(aOutline.count() > 0);

    basegfx::B2DRectangle aBoundRect;
    pFontInstance->GetGlyphBoundRect(nGlyph, aBoundRect, false);

    const basegfx::B2DRange aRange(aOutline.getB2DRange());
    CPPUNIT_ASSERT_DOUBLES_EQUAL(aBoundRect.getMinX(), aRange.getMinX(), 0.5);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(aBoundRect.getMinY(), aRange.getMinY(), 0.5);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(aBoundRect.getMaxX(), aRange.getMaxX(), 0.5);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(aBoundRect.getMaxY(), aRange.getMaxY(), 0.5);

    // A blank glyph (space) succeeds with an empty outline.
    basegfx::B2DPolyPolygon aSpaceOutline;
    CPPUNIT_ASSERT(
        pFontInstance->GetGlyphOutline(pFontInstance->GetGlyphIndex(0x0020), aSpaceOutline, false));
    CPPUNIT_ASSERT_EQUAL(sal_uInt32(0), aSpaceOutline.count());
#endif
}

CPPUNIT_TEST_SUITE_REGISTRATION(VclLogicalFontInstanceTest);

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
