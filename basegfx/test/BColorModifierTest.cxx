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

#include <cppunit/TestAssert.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <basegfx/color/bcolor.hxx>
#include <basegfx/color/bcolormodifier.hxx>

namespace basegfx
{
class bcolormodifier : public CppUnit::TestFixture
{
    BColor maWhite;
    BColor maBlack;
    BColor maRed;
    BColor maGreen;
    BColor maBlue;
    BColor maYellow;
    BColor maMagenta;
    BColor maCyan;
    BColor maGray;

public:
    bcolormodifier()
        : maWhite(1, 1, 1)
        , maBlack(0, 0, 0)
        , maRed(1, 0, 0)
        , maGreen(0, 1, 0)
        , maBlue(0, 0, 1)
        , maYellow(1, 1, 0)
        , maMagenta(1, 0, 1)
        , maCyan(0, 1, 1)
        , maGray(.5, .5, .5)
    {
    }

    void testGray()
    {
        const basegfx::BColorModifierSharedPtr aBColorModifier
            = std::make_shared<basegfx::BColorModifier_gray>();

        CPPUNIT_ASSERT_EQUAL(maWhite, aBColorModifier->getModifiedColor(maWhite));
        CPPUNIT_ASSERT_EQUAL(maGray, aBColorModifier->getModifiedColor(maGray));
        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifier->getModifiedColor(maBlack));

        BColor aGrayedRed(77.0 / 256.0, 77.0 / 256.0, 77.0 / 256.0);
        CPPUNIT_ASSERT_EQUAL(aGrayedRed, aBColorModifier->getModifiedColor(maRed));
        BColor aGrayedGreen(151.0 / 256.0, 151.0 / 256.0, 151.0 / 256.0);
        CPPUNIT_ASSERT_EQUAL(aGrayedGreen, aBColorModifier->getModifiedColor(maGreen));
        BColor aGrayedBlue(28.0 / 256.0, 28.0 / 256.0, 28.0 / 256.0);
        CPPUNIT_ASSERT_EQUAL(aGrayedBlue, aBColorModifier->getModifiedColor(maBlue));
        // 77 + 151 = 228
        BColor aGrayedYellow(228.0 / 256.0, 228.0 / 256.0, 228.0 / 256.0);
        CPPUNIT_ASSERT_EQUAL(aGrayedYellow, aBColorModifier->getModifiedColor(maYellow));
        // 77 + 28 = 105
        BColor aGrayedMagenta(105.0 / 256.0, 105.0 / 256.0, 105.0 / 256.0);
        CPPUNIT_ASSERT_EQUAL(aGrayedMagenta, aBColorModifier->getModifiedColor(maMagenta));
        // 151 + 28 = 179
        BColor aGrayedCyan(179.0 / 256.0, 179.0 / 256.0, 179.0 / 256.0);
        CPPUNIT_ASSERT_EQUAL(aGrayedCyan, aBColorModifier->getModifiedColor(maCyan));

        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier));
        const basegfx::BColorModifierSharedPtr aBColorModifierInvert
            = std::make_shared<basegfx::BColorModifier_invert>();
        CPPUNIT_ASSERT(*aBColorModifier != *aBColorModifierInvert);

        const basegfx::BColorModifierSharedPtr aBColorModifier2
            = std::make_shared<basegfx::BColorModifier_gray>();
        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier2));
    }

    void testInvert()
    {
        const basegfx::BColorModifierSharedPtr aBColorModifier
            = std::make_shared<basegfx::BColorModifier_invert>();

        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifier->getModifiedColor(maWhite));
        CPPUNIT_ASSERT_EQUAL(maGray, aBColorModifier->getModifiedColor(maGray));
        CPPUNIT_ASSERT_EQUAL(maWhite, aBColorModifier->getModifiedColor(maBlack));
        CPPUNIT_ASSERT_EQUAL(maRed, aBColorModifier->getModifiedColor(maCyan));
        CPPUNIT_ASSERT_EQUAL(maCyan, aBColorModifier->getModifiedColor(maRed));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maMagenta));
        CPPUNIT_ASSERT_EQUAL(maMagenta, aBColorModifier->getModifiedColor(maGreen));
        CPPUNIT_ASSERT_EQUAL(maBlue, aBColorModifier->getModifiedColor(maYellow));
        CPPUNIT_ASSERT_EQUAL(maYellow, aBColorModifier->getModifiedColor(maBlue));
        CPPUNIT_ASSERT_EQUAL(BColor(.8, .7, .6),
                             aBColorModifier->getModifiedColor(BColor(.2, .3, .4)));

        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier));
        const basegfx::BColorModifierSharedPtr aBColorModifierGray
            = std::make_shared<basegfx::BColorModifier_gray>();
        CPPUNIT_ASSERT(*aBColorModifier != *aBColorModifierGray);

        const basegfx::BColorModifierSharedPtr aBColorModifier2
            = std::make_shared<basegfx::BColorModifier_invert>();
        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier2));
    }

    void testReplace()
    {
        const basegfx::BColorModifierSharedPtr aBColorModifierRed
            = std::make_shared<basegfx::BColorModifier_replace>(maRed);
        CPPUNIT_ASSERT_EQUAL(maRed, aBColorModifierRed->getModifiedColor(maCyan));
        CPPUNIT_ASSERT_EQUAL(maRed, aBColorModifierRed->getModifiedColor(maRed));
        CPPUNIT_ASSERT_EQUAL(maRed, aBColorModifierRed->getModifiedColor(maMagenta));
        CPPUNIT_ASSERT_EQUAL(maRed, aBColorModifierRed->getModifiedColor(maGreen));

        const basegfx::BColorModifierSharedPtr aBColorModifierBlack
            = std::make_shared<basegfx::BColorModifier_replace>(maBlack);

        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifierBlack->getModifiedColor(maCyan));
        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifierBlack->getModifiedColor(maRed));
        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifierBlack->getModifiedColor(maMagenta));
        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifierBlack->getModifiedColor(maGreen));

        CPPUNIT_ASSERT(aBColorModifierRed->operator==(*aBColorModifierRed));
        CPPUNIT_ASSERT(aBColorModifierBlack->operator==(*aBColorModifierBlack));
        CPPUNIT_ASSERT(*aBColorModifierRed != *aBColorModifierBlack);

        const basegfx::BColorModifierSharedPtr aBColorModifierGray
            = std::make_shared<basegfx::BColorModifier_gray>();
        CPPUNIT_ASSERT(*aBColorModifierRed != *aBColorModifierGray);
    }

    void testStack()
    {
        BColorModifierStack aStack1;
        sal_uInt32 expected = 0;
        CPPUNIT_ASSERT_EQUAL(expected, aStack1.count());
        CPPUNIT_ASSERT_EQUAL(maRed, aStack1.getModifiedColor(maRed));
        CPPUNIT_ASSERT_EQUAL(maWhite, aStack1.getModifiedColor(maWhite));
        CPPUNIT_ASSERT_EQUAL(maBlue, aStack1.getModifiedColor(maBlue));

        aStack1.push(std::make_shared<basegfx::BColorModifier_invert>());
        expected = 1;
        CPPUNIT_ASSERT_EQUAL(expected, aStack1.count());
        CPPUNIT_ASSERT_EQUAL(maCyan, aStack1.getModifiedColor(maRed));
        CPPUNIT_ASSERT_EQUAL(maBlack, aStack1.getModifiedColor(maWhite));
        CPPUNIT_ASSERT_EQUAL(maYellow, aStack1.getModifiedColor(maBlue));

        aStack1.push(std::make_shared<basegfx::BColorModifier_gray>());
        expected = 2;
        CPPUNIT_ASSERT_EQUAL(expected, aStack1.count());
        BColor aInvertedGrayedRed(1 - (77.0 / 256.0), 1 - (77.0 / 256.0), 1 - (77.0 / 256.0));
        BColor aInvertedGrayedBlue(1 - (28.0 / 256.0), 1 - (28.0 / 256.0), 1 - (28.0 / 256.0));
        CPPUNIT_ASSERT_EQUAL(aInvertedGrayedRed, aStack1.getModifiedColor(maRed));
        CPPUNIT_ASSERT_EQUAL(maBlack, aStack1.getModifiedColor(maWhite));
        CPPUNIT_ASSERT_EQUAL(aInvertedGrayedBlue, aStack1.getModifiedColor(maBlue));

        aStack1.pop();
        expected = 1;
        CPPUNIT_ASSERT_EQUAL(expected, aStack1.count());
        CPPUNIT_ASSERT_EQUAL(maCyan, aStack1.getModifiedColor(maRed));
        CPPUNIT_ASSERT_EQUAL(maBlack, aStack1.getModifiedColor(maWhite));
        CPPUNIT_ASSERT_EQUAL(maYellow, aStack1.getModifiedColor(maBlue));
    }

    void testSaturate()
    {
        const basegfx::BColorModifierSharedPtr aBColorModifier
            = std::make_shared<basegfx::BColorModifier_saturate>(0.5);

        CPPUNIT_ASSERT_EQUAL(maWhite, aBColorModifier->getModifiedColor(maWhite));
        CPPUNIT_ASSERT_EQUAL(maGray, aBColorModifier->getModifiedColor(maGray));
        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifier->getModifiedColor(maBlack));

        BColor aExpectedRed(0.6065, 0.1065, 0.1065);
        CPPUNIT_ASSERT_EQUAL(aExpectedRed, aBColorModifier->getModifiedColor(maRed));
        BColor aExpectedGreen(0.3575, 0.8575, 0.3575);
        CPPUNIT_ASSERT_EQUAL(aExpectedGreen, aBColorModifier->getModifiedColor(maGreen));
        BColor aExpectedBlue(0.036, 0.036, 0.536);
        CPPUNIT_ASSERT_EQUAL(aExpectedBlue, aBColorModifier->getModifiedColor(maBlue));
        BColor aExpectedYellow(0.964, 0.964, 0.464);
        CPPUNIT_ASSERT_EQUAL(aExpectedYellow, aBColorModifier->getModifiedColor(maYellow));
        BColor aExpectedMagenta(0.6425, 0.1425, 0.6425);
        CPPUNIT_ASSERT_EQUAL(aExpectedMagenta, aBColorModifier->getModifiedColor(maMagenta));
        BColor aExpectedCyan(0.3935, 0.8935, 0.8935);
        CPPUNIT_ASSERT_EQUAL(aExpectedCyan, aBColorModifier->getModifiedColor(maCyan));

        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier));
        const basegfx::BColorModifierSharedPtr aBColorModifierInvert
            = std::make_shared<basegfx::BColorModifier_invert>();
        CPPUNIT_ASSERT(*aBColorModifier != *aBColorModifierInvert);

        const basegfx::BColorModifierSharedPtr aBColorModifier2
            = std::make_shared<basegfx::BColorModifier_saturate>(0.5);
        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier2));
    }

    void testLuminanceToAlpha()
    {
        const basegfx::BColorModifierSharedPtr aBColorModifier
            = std::make_shared<basegfx::BColorModifier_luminance_to_alpha>();

        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifier->getModifiedColor(maWhite));
        CPPUNIT_ASSERT_EQUAL(maGray, aBColorModifier->getModifiedColor(maGray));
        CPPUNIT_ASSERT_EQUAL(maWhite, aBColorModifier->getModifiedColor(maBlack));

        BColor aExpectedRed(0.7875, 0.7875, 0.7875);
        CPPUNIT_ASSERT_EQUAL(aExpectedRed, aBColorModifier->getModifiedColor(maRed));
        BColor aExpectedGreen(0.2846, 0.2846, 0.2846);
        CPPUNIT_ASSERT_EQUAL(aExpectedGreen, aBColorModifier->getModifiedColor(maGreen));
        BColor aExpectedBlue(0.9279, 0.9279, 0.9279);
        CPPUNIT_ASSERT_EQUAL(aExpectedBlue, aBColorModifier->getModifiedColor(maBlue));
        BColor aExpectedYellow(0.0721, 0.0721, 0.0721);
        CPPUNIT_ASSERT_EQUAL(aExpectedYellow, aBColorModifier->getModifiedColor(maYellow));
        BColor aExpectedMagenta(0.7154, 0.7154, 0.7154);
        CPPUNIT_ASSERT_EQUAL(aExpectedMagenta, aBColorModifier->getModifiedColor(maMagenta));
        BColor aExpectedCyan(0.2125, 0.2125, 0.2125);
        CPPUNIT_ASSERT_EQUAL(aExpectedCyan, aBColorModifier->getModifiedColor(maCyan));

        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier));
        const basegfx::BColorModifierSharedPtr aBColorModifierInvert
            = std::make_shared<basegfx::BColorModifier_invert>();
        CPPUNIT_ASSERT(*aBColorModifier != *aBColorModifierInvert);

        const basegfx::BColorModifierSharedPtr aBColorModifier2
            = std::make_shared<basegfx::BColorModifier_luminance_to_alpha>();
        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier2));
    }

    void testHueRotate()
    {
        const basegfx::BColorModifierSharedPtr aBColorModifier
            = std::make_shared<basegfx::BColorModifier_hueRotate>(basegfx::deg2rad(180.0));

        CPPUNIT_ASSERT_EQUAL(maWhite, aBColorModifier->getModifiedColor(maWhite));
        CPPUNIT_ASSERT_EQUAL(maGray, aBColorModifier->getModifiedColor(maGray));
        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifier->getModifiedColor(maBlack));

        BColor aExpectedRed(0.0, 0.426, 0.426);
        CPPUNIT_ASSERT_EQUAL(aExpectedRed, aBColorModifier->getModifiedColor(maRed));
        BColor aExpectedGreen(1.0, 0.43, 1.0);
        CPPUNIT_ASSERT_EQUAL(aExpectedGreen, aBColorModifier->getModifiedColor(maGreen));
        BColor aExpectedBlue(0.144, 0.144, 0);
        CPPUNIT_ASSERT_EQUAL(aExpectedBlue, aBColorModifier->getModifiedColor(maBlue));
        BColor aExpectedYellow(0.856, 0.856, 1.0);
        CPPUNIT_ASSERT_EQUAL(aExpectedYellow, aBColorModifier->getModifiedColor(maYellow));
        BColor aExpectedMagenta(0.0, 0.57, 0.0);
        CPPUNIT_ASSERT_EQUAL(aExpectedMagenta, aBColorModifier->getModifiedColor(maMagenta));
        BColor aExpectedCyan(1.0, 0.574, 0.574);
        CPPUNIT_ASSERT_EQUAL(aExpectedCyan, aBColorModifier->getModifiedColor(maCyan));

        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier));
        const basegfx::BColorModifierSharedPtr aBColorModifierInvert
            = std::make_shared<basegfx::BColorModifier_invert>();
        CPPUNIT_ASSERT(*aBColorModifier != *aBColorModifierInvert);

        const basegfx::BColorModifierSharedPtr aBColorModifier2
            = std::make_shared<basegfx::BColorModifier_hueRotate>(basegfx::deg2rad(180.0));
        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier2));
    }

    void testMatrix()
    {
        // green matrix
        // clang-format off
        std::vector<double> aVector = {0.0, 0.0, 0.0, 0.0, 0.0,
                                       1.0, 1.0, 1.0, 1.0, 0.0,
                                       0.0, 0.0, 0.0, 0.0, 0.0,
                                       0.0, 0.0, 0.0, 1.0, 0.0};
        // clang-format on

        const basegfx::BColorModifierSharedPtr aBColorModifier
            = std::make_shared<basegfx::BColorModifier_matrix>(aVector);

        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maWhite));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maGray));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maBlack));

        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maRed));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maGreen));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maBlue));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maYellow));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maMagenta));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maCyan));

        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier));
        const basegfx::BColorModifierSharedPtr aBColorModifierInvert
            = std::make_shared<basegfx::BColorModifier_invert>();
        CPPUNIT_ASSERT(*aBColorModifier != *aBColorModifierInvert);

        const basegfx::BColorModifierSharedPtr aBColorModifier2
            = std::make_shared<basegfx::BColorModifier_matrix>(aVector);
        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier2));
    }

    void testMatrixShift()
    {
        // clang-format off
        std::vector<double> aVector = {0.0, 0.0, 0.0, 0.0, 0.0,
                                       0.0, 0.0, 0.0, 0.0, 1.0,
                                       0.0, 0.0, 0.0, 0.0, 0.0,
                                       0.0, 0.0, 0.0, 1.0, 0.0};
        // clang-format on

        const basegfx::BColorModifierSharedPtr aBColorModifier
            = std::make_shared<basegfx::BColorModifier_matrix>(aVector);

        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maWhite));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maGray));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maBlack));

        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maRed));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maGreen));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maBlue));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maYellow));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maMagenta));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maCyan));

        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier));
        const basegfx::BColorModifierSharedPtr aBColorModifierInvert
            = std::make_shared<basegfx::BColorModifier_invert>();
        CPPUNIT_ASSERT(*aBColorModifier != *aBColorModifierInvert);

        const basegfx::BColorModifierSharedPtr aBColorModifier2
            = std::make_shared<basegfx::BColorModifier_matrix>(aVector);
        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier2));
    }

    void testIdentityMatrix()
    {
        // clang-format off
        std::vector<double> aVector = {1.0, 0.0, 0.0, 0.0, 0.0,
                                       0.0, 1.0, 0.0, 0.0, 0.0,
                                       0.0, 0.0, 1.0, 0.0, 0.0,
                                       0.0, 1.0, 0.0, 1.0, 0.0};
        // clang-format on

        const basegfx::BColorModifierSharedPtr aBColorModifier
            = std::make_shared<basegfx::BColorModifier_matrix>(aVector);

        CPPUNIT_ASSERT_EQUAL(maWhite, aBColorModifier->getModifiedColor(maWhite));
        CPPUNIT_ASSERT_EQUAL(maGray, aBColorModifier->getModifiedColor(maGray));
        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifier->getModifiedColor(maBlack));

        CPPUNIT_ASSERT_EQUAL(maRed, aBColorModifier->getModifiedColor(maRed));
        CPPUNIT_ASSERT_EQUAL(maGreen, aBColorModifier->getModifiedColor(maGreen));
        CPPUNIT_ASSERT_EQUAL(maBlue, aBColorModifier->getModifiedColor(maBlue));
        CPPUNIT_ASSERT_EQUAL(maYellow, aBColorModifier->getModifiedColor(maYellow));
        CPPUNIT_ASSERT_EQUAL(maMagenta, aBColorModifier->getModifiedColor(maMagenta));
        CPPUNIT_ASSERT_EQUAL(maCyan, aBColorModifier->getModifiedColor(maCyan));

        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier));
        const basegfx::BColorModifierSharedPtr aBColorModifierInvert
            = std::make_shared<basegfx::BColorModifier_invert>();
        CPPUNIT_ASSERT(*aBColorModifier != *aBColorModifierInvert);

        const basegfx::BColorModifierSharedPtr aBColorModifier2
            = std::make_shared<basegfx::BColorModifier_matrix>(aVector);
        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier2));
    }

    void testBlackAndWhite()
    {
        const basegfx::BColorModifierSharedPtr aBColorModifier
            = std::make_shared<basegfx::BColorModifier_black_and_white>(0.5);

        CPPUNIT_ASSERT_EQUAL(maWhite, aBColorModifier->getModifiedColor(maWhite));
        CPPUNIT_ASSERT_EQUAL(maWhite, aBColorModifier->getModifiedColor(maGray));
        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifier->getModifiedColor(maBlack));

        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifier->getModifiedColor(maRed));
        CPPUNIT_ASSERT_EQUAL(maWhite, aBColorModifier->getModifiedColor(maGreen));
        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifier->getModifiedColor(maBlue));
        CPPUNIT_ASSERT_EQUAL(maWhite, aBColorModifier->getModifiedColor(maYellow));
        CPPUNIT_ASSERT_EQUAL(maBlack, aBColorModifier->getModifiedColor(maMagenta));
        CPPUNIT_ASSERT_EQUAL(maWhite, aBColorModifier->getModifiedColor(maCyan));

        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier));
        const basegfx::BColorModifierSharedPtr aBColorModifierInvert
            = std::make_shared<basegfx::BColorModifier_invert>();
        CPPUNIT_ASSERT(*aBColorModifier != *aBColorModifierInvert);

        const basegfx::BColorModifierSharedPtr aBColorModifier2
            = std::make_shared<basegfx::BColorModifier_black_and_white>(0.5);
        CPPUNIT_ASSERT(aBColorModifier->operator==(*aBColorModifier2));
    }

    // Verify that our shortcut gamma calculation produces reasonably accurate results
    void testGamma()
    {
        for (int i = 1; i < 10; i++)
        {
            BColorModifier_gamma g(i);
            for (int j = 0; j < 100; j++)
            {
                double inputRed = j / 100.0;
                // this is the "slow but correct" gamma calculation
                double expectedOutputRed = std::pow(inputRed, 1 / double(i));
                BColor col = g.getModifiedColor(BColor(inputRed, 0, 0));
                auto msg = OString("col is " + OString::number(inputRed) + " and gamma is "
                                   + OString::number(i));
                CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(msg.getStr(), expectedOutputRed, col.getRed(),
                                                     0.029);
            }
        }
    }

    CPPUNIT_TEST_SUITE(bcolormodifier);
    CPPUNIT_TEST(testGray);
    CPPUNIT_TEST(testInvert);
    CPPUNIT_TEST(testReplace);
    CPPUNIT_TEST(testStack);
    CPPUNIT_TEST(testSaturate);
    CPPUNIT_TEST(testLuminanceToAlpha);
    CPPUNIT_TEST(testHueRotate);
    CPPUNIT_TEST(testMatrix);
    CPPUNIT_TEST(testMatrixShift);
    CPPUNIT_TEST(testIdentityMatrix);
    CPPUNIT_TEST(testBlackAndWhite);
    CPPUNIT_TEST(testGamma);
    CPPUNIT_TEST_SUITE_END();
};

} // namespace basegfx

CPPUNIT_TEST_SUITE_REGISTRATION(basegfx::bcolormodifier);

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
