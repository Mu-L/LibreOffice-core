/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <unotest/filters-test.hxx>
#include <test/bootstrapfixture.hxx>
#include <tools/stream.hxx>
#include <vcl/filter/ImportOutput.hxx>
#include <filter/DxfReader.hxx>

using namespace css;

/* Implementation of Filters test */

class DxfFilterTest
    : public test::FiltersTest
    , public test::BootstrapFixture
{
public:
    DxfFilterTest() : BootstrapFixture(true, false) {}

    virtual bool load(const OUString &,
        const OUString &rURL, const OUString &,
        SfxFilterFlags, SotClipboardFormatId, unsigned int) override;

    /**
     * Ensure CVEs remain unbroken
     */
    void testCVEs();

    CPPUNIT_TEST_SUITE(DxfFilterTest);
    CPPUNIT_TEST(testCVEs);
    CPPUNIT_TEST_SUITE_END();
};

bool DxfFilterTest::load(const OUString &,
    const OUString &rURL, const OUString &,
    SfxFilterFlags, SotClipboardFormatId, unsigned int)
{
    SvFileStream aFileStream(rURL, StreamMode::READ);
    ImportOutput aImportOutput;
    return ImportDxfGraphic(aFileStream, aImportOutput);
}

void DxfFilterTest::testCVEs()
{
#ifndef DISABLE_CVE_TESTS
    testDir(OUString(),
        m_directories.getURLFromSrc(u"/vcl/qa/cppunit/graphicfilter/data/dxf/"));
#endif
}

CPPUNIT_TEST_SUITE_REGISTRATION(DxfFilterTest);

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
