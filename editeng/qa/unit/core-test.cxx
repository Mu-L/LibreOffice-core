/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <config_fonts.h>

#include <test/bootstrapfixture.hxx>

#include <cppunit/extensions/HelperMacros.h>

#include <editdoc.hxx>
#include <impedit.hxx>

#include <sfx2/app.hxx>
#include <svl/itempool.hxx>
#include <editeng/adjustitem.hxx>
#include <editeng/editeng.hxx>
#include <editeng/eeitem.hxx>
#include <editeng/lspcitem.hxx>
#include <editeng/svxacorr.hxx>
#include <editeng/unofield.hxx>
#include <editeng/wghtitem.hxx>
#include <editeng/postitem.hxx>
#include <editeng/section.hxx>
#include <editeng/editobj.hxx>
#include <editeng/flditem.hxx>
#include <editeng/udlnitem.hxx>
#include <svl/srchitem.hxx>
#include <svl/voiditem.hxx>
#include <editeng/fontitem.hxx>
#include <editeng/fhgtitem.hxx>

#include <com/sun/star/text/textfield/Type.hpp>

#include <memory>
#include <vector>
#include <editeng/outliner.hxx>

using namespace com::sun::star;

namespace
{
class Test : public test::BootstrapFixture
{
public:
    Test() {}

    void setUp() override
    {
        test::BootstrapFixture::setUp();
        mpItemPool = new EditEngineItemPool();
        SfxApplication::GetOrCreate();
    }

    void tearDown() override
    {
        mpItemPool.clear();
        test::BootstrapFixture::tearDown();
    }

#if HAVE_MORE_FONTS
    /// Test text portions position when percentage line spacing is set
    void testLineSpacing();
#endif

    void testConstruction();

    /// Test UNO service class that implements text field items.
    void testUnoTextFields();

    /// Test Copy/Paste with hyperlinks in text using Legacy Format
    void testHyperlinkCopyPaste();

    /// Test Copy/Paste using Legacy Format
    void testCopyPaste();

    /// Test Paste using HTML
    void testHTMLPaste();

    /// Test Paste using an HTML fragment
    void testHTMLFragmentPaste();

    /// Test Paste using an RTF
    void testRTFPaste();

    /// Test Paste preference HTML/
    void testRTFHTMLPaste();

    /// Test Copy/Paste with selective selection over multiple paragraphs
    void testMultiParaSelCopyPaste();

    /// Test Copy/Paste with Tabs
    void testTabsCopyPaste();

    /// Test hyperlinks
    void testHyperlinkSearch();

    /// Test Copy/Paste with Bold/Italic text using Legacy Format
    void testBoldItalicCopyPaste();

    /// Test Copy/Paste with Underline text using Legacy Format
    void testUnderlineCopyPaste();

    /// Test Copy/Paste with multiple paragraphs
    void testMultiParaCopyPaste();

    /// Test Copy/Paste with multiple paragraphs having Bold/Italic text
    void testParaBoldItalicCopyPaste();

    void testParaStartCopyPaste();

    void testSectionAttributes();

    void testLargeParaCopyPaste();

    void testTransliterate();

    void testTdf147196();

    void testTdf148148();

    void testSingleLine();
    void testMoveParagraph();
    void testCreateLines();
    void testTdf154248MultilineFieldWrapping();
    void testTdf151748StaleKashidaArray();
    void testTdf162803StaleKashidaArray();

    DECL_STATIC_LINK(Test, CalcFieldValueHdl, EditFieldInfo*, void);

    CPPUNIT_TEST_SUITE(Test);
#if HAVE_MORE_FONTS
    CPPUNIT_TEST(testLineSpacing);
#endif
    CPPUNIT_TEST(testConstruction);
    CPPUNIT_TEST(testUnoTextFields);
    CPPUNIT_TEST(testHyperlinkCopyPaste);
    CPPUNIT_TEST(testCopyPaste);
    CPPUNIT_TEST(testHTMLPaste);
    CPPUNIT_TEST(testHTMLFragmentPaste);
    CPPUNIT_TEST(testRTFPaste);
    CPPUNIT_TEST(testRTFHTMLPaste);
    CPPUNIT_TEST(testMultiParaSelCopyPaste);
    CPPUNIT_TEST(testTabsCopyPaste);
    CPPUNIT_TEST(testHyperlinkSearch);
    CPPUNIT_TEST(testBoldItalicCopyPaste);
    CPPUNIT_TEST(testUnderlineCopyPaste);
    CPPUNIT_TEST(testMultiParaCopyPaste);
    CPPUNIT_TEST(testParaBoldItalicCopyPaste);
    CPPUNIT_TEST(testParaStartCopyPaste);
    CPPUNIT_TEST(testSectionAttributes);
    CPPUNIT_TEST(testLargeParaCopyPaste);
    CPPUNIT_TEST(testTransliterate);
    CPPUNIT_TEST(testTdf147196);
    CPPUNIT_TEST(testTdf148148);
    CPPUNIT_TEST(testSingleLine);
    CPPUNIT_TEST(testMoveParagraph);
    CPPUNIT_TEST(testCreateLines);
    CPPUNIT_TEST(testTdf154248MultilineFieldWrapping);
    CPPUNIT_TEST(testTdf151748StaleKashidaArray);
    CPPUNIT_TEST(testTdf162803StaleKashidaArray);
    CPPUNIT_TEST_SUITE_END();

private:
    rtl::Reference<EditEngineItemPool> mpItemPool;
};

#if HAVE_MORE_FONTS
void Test::testLineSpacing()
{
    // Create EditEngine's instance
    EditEngine aEditEngine(mpItemPool.get());

    if (aEditEngine.GetRefDevice()->GetDPIY() != 96
        || aEditEngine.GetRefDevice()->GetDPIScaleFactor() != 1.0)
        return;

    // Get EditDoc for current EditEngine's instance
    EditDoc& rDoc = aEditEngine.GetEditDoc();

    // Initially no text should be there
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(), rDoc.GetParaAsString(sal_Int32(0)));

    // Set initial text
    OUString aText = u"This is multi-line paragraph"_ustr;

    sal_Int32 aTextLen = aText.getLength();
    aEditEngine.SetText(aText);

    // Assert changes - text insertion
    CPPUNIT_ASSERT_EQUAL(aTextLen, rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(aText, rDoc.GetParaAsString(sal_Int32(0)));

    // Select all paragraphs
    ESelection aSelection(0, 0, 0, aTextLen);

    auto doTest = [&](sal_uInt16 nSpace, sal_uInt16 nExpMaxAscent, sal_uInt32 nExpLineHeight) {
        std::unique_ptr<SfxItemSet> pSet(new SfxItemSet(aEditEngine.GetEmptyItemSet()));
        SvxLineSpacingItem aLineSpacing(LINE_SPACE_DEFAULT_HEIGHT, EE_PARA_SBL);
        aLineSpacing.SetPropLineSpace(nSpace);
        pSet->Put(aLineSpacing);

        // Set font
        SvxFontItem aFont(EE_CHAR_FONTINFO);
        aFont.SetFamilyName(u"Liberation Sans"_ustr);
        pSet->Put(aFont);
        SvxFontHeightItem aFontSize(240, 100, EE_CHAR_FONTHEIGHT);
        pSet->Put(aFontSize);

        CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(3), pSet->Count());

        aEditEngine.QuickSetAttribs(*pSet, aSelection);

        // Assert changes
        ParaPortion const& rParaPortion = aEditEngine.GetParaPortions().getRef(0);
        ContentNode* const pNode = rParaPortion.GetNode();
        const SvxLineSpacingItem& rLSItem = pNode->GetContentAttribs().GetItem(EE_PARA_SBL);
        CPPUNIT_ASSERT_EQUAL(SvxInterLineSpaceRule::Prop, rLSItem.GetInterLineSpaceRule());
        CPPUNIT_ASSERT_EQUAL(nSpace, rLSItem.GetPropLineSpace());

        // Check the first line
        ParagraphInfos aInfo = aEditEngine.GetParagraphInfos(0);
        CPPUNIT_ASSERT_EQUAL(nExpMaxAscent, aInfo.nFirstLineMaxAscent);
        CPPUNIT_ASSERT_EQUAL(nExpLineHeight, aEditEngine.GetLineHeight(0));
    };

    // Test first case - 60%
    doTest(60, 122, 153);

    // Force multiple lines
    aEditEngine.SetPaperSize(Size(1000, 6000));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), aEditEngine.GetLineCount(0));

    // Test second case - 150%
    doTest(150, 337, 382);

    // Test lower Word limit - 6% (factor 0.06)
    doTest(6, 12, 15);

    // Test upper Word limit - 13200% (factor 132)
    doTest(13200, 33615, 33660);
}
#endif

void Test::testConstruction()
{
    EditEngine aEngine(mpItemPool.get());

    aEngine.SetText(u"I am Edit Engine."_ustr);
}

bool includes(const uno::Sequence<OUString>& rSeq, std::u16string_view rVal)
{
    for (OUString const& s : rSeq)
        if (s == rVal)
            return true;

    return false;
}

void Test::testUnoTextFields()
{
    {
        // DATE
        rtl::Reference<SvxUnoTextField> xField(new SvxUnoTextField(text::textfield::Type::DATE));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.text.textfield.DateTime");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }

    {
        // URL
        rtl::Reference<SvxUnoTextField> xField(new SvxUnoTextField(text::textfield::Type::URL));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.text.textfield.URL");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }

    {
        // PAGE
        rtl::Reference<SvxUnoTextField> xField(new SvxUnoTextField(text::textfield::Type::PAGE));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.text.textfield.PageNumber");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }

    {
        // PAGES
        rtl::Reference<SvxUnoTextField> xField(new SvxUnoTextField(text::textfield::Type::PAGES));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.text.textfield.PageCount");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }

    {
        // TIME
        rtl::Reference<SvxUnoTextField> xField(new SvxUnoTextField(text::textfield::Type::TIME));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.text.textfield.DateTime");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }

    {
        // FILE
        rtl::Reference<SvxUnoTextField> xField(
            new SvxUnoTextField(text::textfield::Type::DOCINFO_TITLE));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.text.textfield.docinfo.Title");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }

    {
        // TABLE
        rtl::Reference<SvxUnoTextField> xField(new SvxUnoTextField(text::textfield::Type::TABLE));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.text.textfield.SheetName");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }

    {
        // EXTENDED TIME
        rtl::Reference<SvxUnoTextField> xField(
            new SvxUnoTextField(text::textfield::Type::EXTENDED_TIME));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.text.textfield.DateTime");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }

    {
        // EXTENDED FILE
        rtl::Reference<SvxUnoTextField> xField(
            new SvxUnoTextField(text::textfield::Type::EXTENDED_FILE));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.text.textfield.FileName");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }

    {
        // AUTHOR
        rtl::Reference<SvxUnoTextField> xField(new SvxUnoTextField(text::textfield::Type::AUTHOR));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.text.textfield.Author");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }

    {
        // MEASURE
        rtl::Reference<SvxUnoTextField> xField(new SvxUnoTextField(text::textfield::Type::MEASURE));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.text.textfield.Measure");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }

    {
        // PRESENTATION HEADER
        rtl::Reference<SvxUnoTextField> xField(
            new SvxUnoTextField(text::textfield::Type::PRESENTATION_HEADER));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.presentation.textfield.Header");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }

    {
        // PRESENTATION FOOTER
        rtl::Reference<SvxUnoTextField> xField(
            new SvxUnoTextField(text::textfield::Type::PRESENTATION_FOOTER));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.presentation.textfield.Footer");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }

    {
        // PRESENTATION DATE TIME
        rtl::Reference<SvxUnoTextField> xField(
            new SvxUnoTextField(text::textfield::Type::PRESENTATION_DATE_TIME));
        uno::Sequence<OUString> aSvcs = xField->getSupportedServiceNames();
        bool bGood = includes(aSvcs, u"com.sun.star.presentation.textfield.DateTime");
        CPPUNIT_ASSERT_MESSAGE("expected service is not present.", bGood);
    }
}

IMPL_STATIC_LINK(Test, CalcFieldValueHdl, EditFieldInfo*, pInfo, void)
{
    if (!pInfo)
        return;

    const SvxFieldItem& rField = pInfo->GetField();
    const SvxFieldData* pField = rField.GetField();
    if (const SvxURLField* pURLField = dynamic_cast<const SvxURLField*>(pField))
    {
        // URLField
        OUString aURL = pURLField->GetURL();
        switch (pURLField->GetFormat())
        {
            case SvxURLFormat::AppDefault:
            case SvxURLFormat::Repr:
            {
                pInfo->SetRepresentation(pURLField->GetRepresentation());
            }
            break;

            case SvxURLFormat::Url:
            {
                pInfo->SetRepresentation(aURL);
            }
            break;
        }
    }
    else
    {
        OSL_FAIL("Unknown Field");
        pInfo->SetRepresentation(OUString('?'));
    }
}

void Test::testHyperlinkCopyPaste()
{
    // Create Outliner instance
    Outliner aOutliner(mpItemPool.get(), OutlinerMode::TextObject);
    aOutliner.SetCalcFieldValueHdl(LINK(nullptr, Test, CalcFieldValueHdl));

    // Create EditEngine's instance
    EditEngine& aEditEngine = const_cast<EditEngine&>(aOutliner.GetEditEngine());

    // Get EditDoc for current EditEngine's instance
    EditDoc& rDoc = aEditEngine.GetEditDoc();

    // New instance must be empty - no initial text
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(), rDoc.GetParaAsString(sal_Int32(0)));

    // Get corresponding Field Item for inserting URLs in text
    // URL 1
    OUString aURL1 = u"mailto:///user@example.com"_ustr;
    OUString aRepres1 = u"user@example.com"_ustr;
    SvxURLField aURLField1( aURL1, aRepres1, SvxURLFormat::Repr );
    SvxFieldItem aField1( aURLField1, EE_FEATURE_FIELD );
    // URL 2
    OUString aURL2 = u"mailto:///example@domain.com"_ustr;
    OUString aRepres2 = u"example@domain.com"_ustr;
    SvxURLField aURLField2( aURL2, aRepres2, SvxURLFormat::Repr );
    SvxFieldItem aField2( aURLField2, EE_FEATURE_FIELD );

    // Insert initial text
    OUString aParaText = u"sampletextfortestingfeaturefields"_ustr;
    // Positions Ref      .............*13....*20..........
    sal_Int32 aTextLen = aParaText.getLength();
    aEditEngine.SetText(aParaText);

    // Assert changes
    CPPUNIT_ASSERT_EQUAL(aTextLen, rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(aParaText, rDoc.GetParaAsString(sal_Int32(0)));

    // Insert URL 1
    ContentNode* pNode = rDoc.GetObject(0);
    EditSelection aSel1(EditPaM(pNode, 13), EditPaM(pNode, 13));
    aEditEngine.InsertField(aSel1, aField1);

    // Assert Field Count
    CPPUNIT_ASSERT_EQUAL(size_t(1), aEditEngine.GetFieldInfo(0).size());

    // Insert URL 2
    EditSelection aSel2(EditPaM(pNode, 20 + 1), EditPaM(pNode, 20 + 1));
    aEditEngine.InsertField(aSel2, aField2);

    // Assert Field Count
    CPPUNIT_ASSERT_EQUAL(size_t(2), aEditEngine.GetFieldInfo(0).size());

    // Assert URL Fields and text before copy
    // Check text
    CPPUNIT_ASSERT_EQUAL( aTextLen + aRepres1.getLength() + aRepres2.getLength(), rDoc.GetTextLen() );
    CPPUNIT_ASSERT_EQUAL( u"sampletextforuser@example.comtestingexample@domain.comfeaturefields"_ustr, rDoc.GetParaAsString(sal_Int32(0)) );

    // Check Field 1
    EFieldInfo aURLFieldInfo1 = aEditEngine.GetFieldInfo(sal_Int32(0))[0];
    CPPUNIT_ASSERT_EQUAL(sal_Int32(13), aURLFieldInfo1.aPosition.nIndex);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(EE_FEATURE_FIELD), aURLFieldInfo1.pFieldItem->Which());
    SvxURLField* pURLField1 = dynamic_cast<SvxURLField*>(
        const_cast<SvxFieldData*>(aURLFieldInfo1.pFieldItem->GetField()));
    CPPUNIT_ASSERT(pURLField1);
    CPPUNIT_ASSERT_EQUAL(aURL1, pURLField1->GetURL());
    CPPUNIT_ASSERT_EQUAL(aRepres1, pURLField1->GetRepresentation());

    // Check Field 2
    EFieldInfo aURLFieldInfo2 = aEditEngine.GetFieldInfo(sal_Int32(0))[1];
    CPPUNIT_ASSERT_EQUAL(sal_Int32(21), aURLFieldInfo2.aPosition.nIndex);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(EE_FEATURE_FIELD), aURLFieldInfo2.pFieldItem->Which());
    SvxURLField* pURLField2 = dynamic_cast<SvxURLField*>(
        const_cast<SvxFieldData*>(aURLFieldInfo2.pFieldItem->GetField()));
    CPPUNIT_ASSERT(pURLField2);
    CPPUNIT_ASSERT_EQUAL(aURL2, pURLField2->GetURL());
    CPPUNIT_ASSERT_EQUAL(aRepres2, pURLField2->GetRepresentation());

    // Copy text using legacy format
    uno::Reference<datatransfer::XTransferable> xData
        = aEditEngine.CreateTransferable(ESelection(0, 10, 0, 21));

    // Paste text at the end
    aEditEngine.InsertText(xData, OUString(), rDoc.GetEndPaM(), true);

    // Assert Changes ACP, ACP: after Copy/Paste

    // Check the fields count
    CPPUNIT_ASSERT_EQUAL(size_t(3), aEditEngine.GetFieldInfo(0).size());

    // Check the updated text length
    CPPUNIT_ASSERT_EQUAL(aTextLen + 10 + aRepres1.getLength() * 2 + aRepres2.getLength(),
                         rDoc.GetTextLen());

    // Check the updated text contents
    CPPUNIT_ASSERT_EQUAL( u"sampletextforuser@example.comtestingexample@domain.comfeaturefieldsforuser@example.comtesting"_ustr, rDoc.GetParaAsString(sal_Int32(0)) );

    // Check the Fields and their values

    // Field 1
    EFieldInfo aACPURLFieldInfo1 = aEditEngine.GetFieldInfo(sal_Int32(0))[0];
    CPPUNIT_ASSERT_EQUAL(sal_Int32(13), aACPURLFieldInfo1.aPosition.nIndex);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(EE_FEATURE_FIELD), aACPURLFieldInfo1.pFieldItem->Which());
    SvxURLField* pACPURLField1 = dynamic_cast<SvxURLField*>(
        const_cast<SvxFieldData*>(aACPURLFieldInfo1.pFieldItem->GetField()));
    CPPUNIT_ASSERT(pACPURLField1);
    CPPUNIT_ASSERT_EQUAL(aURL1, pACPURLField1->GetURL());
    CPPUNIT_ASSERT_EQUAL(aRepres1, pACPURLField1->GetRepresentation());

    // Field 2
    EFieldInfo aACPURLFieldInfo2 = aEditEngine.GetFieldInfo(sal_Int32(0))[1];
    CPPUNIT_ASSERT_EQUAL(sal_Int32(21), aACPURLFieldInfo2.aPosition.nIndex);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(EE_FEATURE_FIELD), aACPURLFieldInfo2.pFieldItem->Which());
    SvxURLField* pACPURLField2 = dynamic_cast<SvxURLField*>(
        const_cast<SvxFieldData*>(aACPURLFieldInfo2.pFieldItem->GetField()));
    CPPUNIT_ASSERT(pACPURLField2);
    CPPUNIT_ASSERT_EQUAL(aURL2, pACPURLField2->GetURL());
    CPPUNIT_ASSERT_EQUAL(aRepres2, pACPURLField2->GetRepresentation());

    // Field 3
    EFieldInfo aACPURLFieldInfo3 = aEditEngine.GetFieldInfo(sal_Int32(0))[2];
    CPPUNIT_ASSERT_EQUAL(sal_Int32(38), aACPURLFieldInfo3.aPosition.nIndex);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(EE_FEATURE_FIELD), aACPURLFieldInfo3.pFieldItem->Which());
    SvxURLField* pACPURLField3 = dynamic_cast<SvxURLField*>(
        const_cast<SvxFieldData*>(aACPURLFieldInfo3.pFieldItem->GetField()));
    CPPUNIT_ASSERT(pACPURLField3);
    CPPUNIT_ASSERT_EQUAL(aURL1, pACPURLField3->GetURL());
    CPPUNIT_ASSERT_EQUAL(aRepres1, pACPURLField3->GetRepresentation());
}

void Test::testCopyPaste()
{
    // Create EditEngine's instance
    EditEngine aEditEngine(mpItemPool.get());

    // Get EditDoc for current EditEngine's instance
    EditDoc& rDoc = aEditEngine.GetEditDoc();

    // Initially no text should be there
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(), rDoc.GetParaAsString(sal_Int32(0)));

    // Set initial text
    OUString aText = u"This is custom initial text"_ustr;
    sal_Int32 aTextLen = aText.getLength();
    aEditEngine.SetText(aText);

    // Assert changes
    CPPUNIT_ASSERT_EQUAL(aTextLen, rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(aText, rDoc.GetParaAsString(sal_Int32(0)));

    // Copy initial text using legacy format
    uno::Reference<datatransfer::XTransferable> xData
        = aEditEngine.CreateTransferable(ESelection(0, 0, 0, aTextLen));

    // Paste text at the end
    aEditEngine.InsertText(xData, OUString(), rDoc.GetEndPaM(), true);

    // Assert changes
    CPPUNIT_ASSERT_EQUAL(aTextLen + aTextLen, rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(aText + aText), rDoc.GetParaAsString(sal_Int32(0)));
}

/// XTransferable implementation that provides simple HTML content.
class TestTransferable : public cppu::WeakImplHelper<datatransfer::XTransferable>
{
    std::vector<OString> m_aContent;
    std::vector<OUString> m_aMimeType;

public:
    TestTransferable(std::vector<OString>& rContent, std::vector<OUString>& rMimeType);
    uno::Any SAL_CALL getTransferData(const datatransfer::DataFlavor& rFlavor) override;
    uno::Sequence<datatransfer::DataFlavor> SAL_CALL getTransferDataFlavors() override;
    sal_Bool SAL_CALL isDataFlavorSupported(const datatransfer::DataFlavor& rFlavor) override;
};

TestTransferable::TestTransferable(std::vector<OString>& rContent, std::vector<OUString>& rMimeType)
    : m_aContent(rContent)
    , m_aMimeType(rMimeType)
{
    CPPUNIT_ASSERT_EQUAL(m_aContent.size(), m_aMimeType.size());
}

uno::Any TestTransferable::getTransferData(const datatransfer::DataFlavor& rFlavor)
{
    for (size_t nType = 0; nType < m_aMimeType.size(); ++nType)
    {
        if (rFlavor.MimeType == m_aMimeType[nType])
        {
            uno::Any aRet;
            SvMemoryStream aStream;
            aStream.WriteOString(m_aContent[nType]);
            aRet <<= uno::Sequence<sal_Int8>(static_cast<const sal_Int8*>(aStream.GetData()),
                                             aStream.GetSize());
            return aRet;
        }
    }
    return {};
}

uno::Sequence<datatransfer::DataFlavor> TestTransferable::getTransferDataFlavors()
{
    std::vector<datatransfer::DataFlavor> aFlavourVac;
    for (size_t nType = 0; nType < m_aMimeType.size(); ++nType)
    {
        datatransfer::DataFlavor aFlavor;
        aFlavor.DataType = cppu::UnoType<uno::Sequence<sal_Int8>>::get();
        aFlavor.MimeType = m_aMimeType[nType];
        aFlavor.HumanPresentableName = aFlavor.MimeType;
        aFlavourVac.push_back(aFlavor);
    }
    uno::Sequence<datatransfer::DataFlavor> aFlavors(aFlavourVac.data(), m_aMimeType.size());
    return aFlavors;
}

sal_Bool TestTransferable::isDataFlavorSupported(const datatransfer::DataFlavor& rFlavor)
{
    for (size_t nType = 0; nType < m_aMimeType.size(); ++nType)
    {
        if (rFlavor.MimeType == m_aMimeType[nType]
            && rFlavor.DataType == cppu::UnoType<uno::Sequence<sal_Int8>>::get())
            return true;
    }
    return false;
}

void Test::testHTMLPaste()
{
    // Given an empty editeng document:
    EditEngine aEditEngine(mpItemPool.get());
    EditDoc& rDoc = aEditEngine.GetEditDoc();
    //OString aHTML("<!DOCTYPE html>\n<html><body>test</body></html>"_ostr);
    std::vector<OString> aContent({ "<!DOCTYPE html>\n<html><body>test</body></html>"_ostr });
    std::vector<OUString> aMime({ u"text/html"_ustr });

    uno::Reference<datatransfer::XTransferable> xData(new TestTransferable(aContent, aMime));
    // When trying to paste HTML:
    aEditEngine.InsertText(xData, OUString(), rDoc.GetEndPaM(), true);

    // Then make sure the text gets pasted:
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: test
    // - Actual  :
    // i.e. RTF and plain text paste worked, but not HTML.
    CPPUNIT_ASSERT_EQUAL(u"test"_ustr, rDoc.GetParaAsString(static_cast<sal_Int32>(0)));
}

void Test::testHTMLFragmentPaste()
{
    // Given an empty editeng document:
    EditEngine aEditEngine(mpItemPool.get());
    EditDoc& rDoc = aEditEngine.GetEditDoc();
    std::vector<OString> aContent({ "a<b>b</b>c"_ostr });
    std::vector<OUString> aMime({ u"text/html"_ustr });

    uno::Reference<datatransfer::XTransferable> xData(new TestTransferable(aContent, aMime));

    // When trying to paste an HTML fragment:
    aEditEngine.InsertText(xData, OUString(), rDoc.GetEndPaM(), true);

    // Then make sure the text gets pasted:
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: abc
    // - Actual  :
    // i.e. a HTML fragment without a proper header was ignored on paste.
    CPPUNIT_ASSERT_EQUAL(u"abc"_ustr, rDoc.GetParaAsString(static_cast<sal_Int32>(0)));
}

void Test::testRTFPaste()
{
    // Given an empty editeng document:
    EditEngine aEditEngine(mpItemPool.get());
    EditDoc& rDoc = aEditEngine.GetEditDoc();
    std::vector<OString> aContent(
        { "{\\rtf1\\adeflang1025\\ansi{\\ul www.libreoffice.org}}"_ostr });
    std::vector<OUString> aMime({ u"text/richtext"_ustr });
    uno::Reference<datatransfer::XTransferable> xData(new TestTransferable(aContent, aMime));

    // When trying to paste HTML:
    aEditEngine.InsertText(xData, OUString(), rDoc.GetEndPaM(), true);

    // Then make sure the text gets pasted:
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: www.libreoffice.org
    // - Actual  :
    // i.e. RTF and plain text paste worked, but not HTML.
    CPPUNIT_ASSERT_EQUAL(u"www.libreoffice.org"_ustr,
                         rDoc.GetParaAsString(static_cast<sal_Int32>(0)));
}

void Test::testRTFHTMLPaste()
{
    // Given an empty editeng document:
    EditEngine aEditEngine(mpItemPool.get());
    EditDoc& rDoc = aEditEngine.GetEditDoc();
    std::vector<OString> aContent(
        { "{\\rtf1\\adeflang1025\\ansi{\\ul www.libreoffice.org}}"_ostr,
          "Version:1.0\nStartHTML:0000000121\n"
          "EndHTML:0000000596\n"
          "StartFragment:0000000519\n"
          "EndFragment:0000000579\n"
          "<!DOCTYPE html><html><head><meta http-equiv=\"content-type\" content=\"text/html; "
          "charset=utf-8\"/>"
          "<title></title><meta name=\"generator\" content=\"LibreOffice 7.4.5.1 (Windows)\"/>"
          "<style type=\"text/css\">@page { size: 21cm 29.7cm; margin: 2cm }"
          "p{ line-height: 115%; margin-bottom: 0.25cm; background: transparent }</style>"
          "</head><body lang=\"de-DE\" link=\"#000080\" vlink=\"#800000\" dir=\"ltr\">"
          "<p style=\"line-height: 100%; margin-bottom: 0cm\">abc</p></body></html>"_ostr });
    std::vector<OUString> aMime(
        { u"text/richtext"_ustr,
          u"application/x-openoffice-html-simple;windows_formatname=\"HTML Format\""_ustr });
    uno::Reference<datatransfer::XTransferable> xData(new TestTransferable(aContent, aMime));

    // When trying to paste HTML:
    aEditEngine.InsertText(xData, OUString(), rDoc.GetEndPaM(), true);

    // Then make sure the text gets pasted:
    // expected "abc"
    // wrong: www.libreoffice.org
    // i.e. HTML is preferred over HTML.
    CPPUNIT_ASSERT_EQUAL(u"abc"_ustr, rDoc.GetParaAsString(static_cast<sal_Int32>(0)));
}
void Test::testMultiParaSelCopyPaste()
{
    // Create EditEngine's instance
    EditEngine aEditEngine(mpItemPool.get());

    // Get EditDoc for current EditEngine's instance
    EditDoc& rDoc = aEditEngine.GetEditDoc();

    // Initially no text should be there
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(), rDoc.GetParaAsString(sal_Int32(0)));

    // Insert initial text
    OUString aFirstPara = u"This is first paragraph"_ustr;
    // Selection Ref       ........8..............
    OUString aSecondPara = u"This is second paragraph"_ustr;
    // Selection Ref        .............14.........
    OUString aThirdPara = u"This is third paragraph"_ustr;
    OUString aText = aFirstPara + "\n" + aSecondPara + "\n" + aThirdPara;
    sal_Int32 aTextLen = aFirstPara.getLength() + aSecondPara.getLength() + aThirdPara.getLength();
    aEditEngine.SetText( aText );
    OUString aCopyText = u"first paragraphThis is second"_ustr;
    sal_Int32 aCopyTextLen = aCopyText.getLength();

    // Assert changes
    CPPUNIT_ASSERT_EQUAL(aTextLen, rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(aFirstPara, rDoc.GetParaAsString(sal_Int32(0)));
    CPPUNIT_ASSERT_EQUAL(aSecondPara, rDoc.GetParaAsString(sal_Int32(1)));
    CPPUNIT_ASSERT_EQUAL(aThirdPara, rDoc.GetParaAsString(sal_Int32(2)));

    // Copy initial text using legacy format
    uno::Reference<datatransfer::XTransferable> xData
        = aEditEngine.CreateTransferable(ESelection(0, 8, 1, 14));

    // Paste text at the end
    aEditEngine.InsertText(xData, OUString(), rDoc.GetEndPaM(), true);

    // Assert changes
    OUString aThirdParaAfterCopyPaste = aThirdPara + "first paragraph";
    CPPUNIT_ASSERT_EQUAL( aTextLen + aCopyTextLen, rDoc.GetTextLen() );
    CPPUNIT_ASSERT_EQUAL( aFirstPara, rDoc.GetParaAsString(sal_Int32(0)) );
    CPPUNIT_ASSERT_EQUAL( aSecondPara, rDoc.GetParaAsString(sal_Int32(1)) );
    CPPUNIT_ASSERT_EQUAL( aThirdParaAfterCopyPaste, rDoc.GetParaAsString(sal_Int32(2)) );
    CPPUNIT_ASSERT_EQUAL( u"This is second"_ustr, rDoc.GetParaAsString(sal_Int32(3)) );
}

void Test::testTabsCopyPaste()
{
    // Create EditEngine's instance
    EditEngine aEditEngine(mpItemPool.get());

    // Get EditDoc for current EditEngine's instance
    EditDoc& rDoc = aEditEngine.GetEditDoc();

    // New instance must be empty - no initial text
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(), rDoc.GetParaAsString(sal_Int32(0)));

    // Get corresponding Item for inserting tabs in the text
    SfxVoidItem aTab(EE_FEATURE_TAB);

    // Insert initial text
    OUString aParaText = u"sampletextfortestingtab"_ustr;
    // Positions Ref      ......*6...............*23
    sal_Int32 aTextLen = aParaText.getLength();
    aEditEngine.SetText(aParaText);

    // Assert changes
    CPPUNIT_ASSERT_EQUAL(aTextLen, rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(aParaText, rDoc.GetParaAsString(sal_Int32(0)));

    // Insert tab 1 at desired position
    ContentNode* pNode = rDoc.GetObject(0);
    EditSelection aSel1(EditPaM(pNode, 6), EditPaM(pNode, 6));
    aEditEngine.InsertFeature(aSel1, aTab);

    // Assert changes
    CPPUNIT_ASSERT_EQUAL( aTextLen + 1, rDoc.GetTextLen() );
    CPPUNIT_ASSERT_EQUAL( u"sample\ttextfortestingtab"_ustr, rDoc.GetParaAsString(sal_Int32(0)) );

    // Insert tab 2 at desired position
    EditSelection aSel2(EditPaM(pNode, 23 + 1), EditPaM(pNode, 23 + 1));
    aEditEngine.InsertFeature(aSel2, aTab);

    // Assert changes
    CPPUNIT_ASSERT_EQUAL( aTextLen + 2, rDoc.GetTextLen() );
    CPPUNIT_ASSERT_EQUAL( u"sample\ttextfortestingtab\t"_ustr, rDoc.GetParaAsString(sal_Int32(0)) );

    // Copy text using legacy format
    uno::Reference<datatransfer::XTransferable> xData
        = aEditEngine.CreateTransferable(ESelection(0, 6, 0, aTextLen + 2));

    // Paste text at the end
    aEditEngine.InsertText(xData, OUString(), rDoc.GetEndPaM(), true);

    // Assert changes
    CPPUNIT_ASSERT_EQUAL( aTextLen + aTextLen - 6 + 4, rDoc.GetTextLen() );
    CPPUNIT_ASSERT_EQUAL( u"sample\ttextfortestingtab\t\ttextfortestingtab\t"_ustr, rDoc.GetParaAsString(sal_Int32(0)) );
}

class UrlEditEngine : public EditEngine
{
public:
    explicit UrlEditEngine(SfxItemPool* pPool)
        : EditEngine(pPool)
    {
    }

    virtual OUString CalcFieldValue(const SvxFieldItem&, sal_Int32, sal_Int32,
                                    std::optional<Color>&, std::optional<Color>&,
                                    std::optional<FontLineStyle>&) override
    {
        return u"jim@bob.com"_ustr; // a sophisticated view of value:
    }
};

// Odd accounting for hyperlink position & size etc.
// https://bugzilla.novell.com/show_bug.cgi?id=467459
void Test::testHyperlinkSearch()
{
    UrlEditEngine aEngine(mpItemPool.get());
    EditDoc& rDoc = aEngine.GetEditDoc();

    OUString aSampleText = u"Please write email to . if you find a fish(not a dog)."_ustr;
    aEngine.SetText(aSampleText);

    CPPUNIT_ASSERT_EQUAL_MESSAGE("set text", aSampleText, rDoc.GetParaAsString(sal_Int32(0)));

    ContentNode* pNode = rDoc.GetObject(0);
    EditSelection aSel(EditPaM(pNode, 22), EditPaM(pNode, 22));
    SvxURLField aURLField(u"mailto:///jim@bob.com"_ustr, u"jim@bob.com"_ustr,
                          SvxURLFormat::Repr);
    SvxFieldItem aField(aURLField, EE_FEATURE_FIELD);

    aEngine.InsertField(aSel, aField);

    OUString aContent = pNode->GetExpandedText();
    CPPUNIT_ASSERT_EQUAL_MESSAGE("get text", u"Please write email to jim@bob.com. if you find a fish(not a dog)."_ustr,
                           aContent);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("wrong length", aContent.getLength(), rDoc.GetTextLen());

    // Check expansion and positioning re-work
    CPPUNIT_ASSERT_EQUAL_MESSAGE("wrong length", aContent.getLength(), pNode->GetExpandedLen());
    for (sal_Int32 n = 0; n < aContent.getLength(); n++)
    {
        sal_Int32 nStart = n, nEnd = n;
        pNode->UnExpandPositions(nStart, nEnd);
        CPPUNIT_ASSERT_MESSAGE("out of bound start", nStart < pNode->Len());
        CPPUNIT_ASSERT_MESSAGE("out of bound end", nEnd <= pNode->Len());
    }

    static const struct
    {
        sal_Int32 mnStart, mnEnd;
        sal_Int32 mnNewStart, mnNewEnd;
    } aTrickyOnes[] = { { 0, 1, /* -> */ 0, 1 },
                        { 21, 25, /* -> */ 21, 23 }, // the field is really just one char
                        { 25, 27, /* -> */ 22, 23 },
                        { 50, 56, /* -> */ 40, 46 } };
    for (size_t n = 0; n < std::size(aTrickyOnes); n++)
    {
        sal_Int32 nStart = aTrickyOnes[n].mnStart;
        sal_Int32 nEnd = aTrickyOnes[n].mnEnd;
        pNode->UnExpandPositions(nStart, nEnd);

        CPPUNIT_ASSERT_EQUAL_MESSAGE(OString("in row " + OString::number(n)).getStr(),
                                     aTrickyOnes[n].mnNewStart, nStart);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(OString("in row " + OString::number(n)).getStr(),
                                     aTrickyOnes[n].mnNewEnd, nEnd);
    }

    SvxSearchItem aItem(1); //SID_SEARCH_ITEM);
    aItem.SetBackward(false);
    aItem.SetSelection(false);
    aItem.SetSearchString(u"fish"_ustr);
    CPPUNIT_ASSERT_MESSAGE("no fish", aEngine.HasText(aItem));
    aItem.SetSearchString(u"dog"_ustr);
    CPPUNIT_ASSERT_MESSAGE("no dog", aEngine.HasText(aItem));
}

bool hasBold(const editeng::Section& rSecAttr)
{
    return std::any_of(
        rSecAttr.maAttributes.begin(), rSecAttr.maAttributes.end(), [](const SfxPoolItem* p) {
            return p->Which() == EE_CHAR_WEIGHT
                   && static_cast<const SvxWeightItem*>(p)->GetWeight() == WEIGHT_BOLD;
        });
}

bool hasItalic(const editeng::Section& rSecAttr)
{
    return std::any_of(
        rSecAttr.maAttributes.begin(), rSecAttr.maAttributes.end(), [](const SfxPoolItem* p) {
            return p->Which() == EE_CHAR_ITALIC
                   && static_cast<const SvxPostureItem*>(p)->GetPosture() == ITALIC_NORMAL;
        });
}

void Test::testBoldItalicCopyPaste()
{
    // Create EditEngine's instance
    EditEngine aEditEngine(mpItemPool.get());

    // Get EditDoc for current EditEngine's instance
    EditDoc& rDoc = aEditEngine.GetEditDoc();

    // New instance must be empty - no initial text
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(), rDoc.GetParaAsString(sal_Int32(0)));

    // Get corresponding ItemSet for inserting Bold/Italic text
    std::unique_ptr<SfxItemSet> pSet(new SfxItemSet(aEditEngine.GetEmptyItemSet()));
    SvxWeightItem aBold(WEIGHT_BOLD, EE_CHAR_WEIGHT);
    SvxPostureItem aItalic(ITALIC_NORMAL, EE_CHAR_ITALIC);

    // Insert initial text
    OUString aParaText = u"boldeditengineitalic"_ustr;
    // Positions Ref      ..*2....*8...*13.*17
    // Bold Ref           ..[   BOLD   ]......
    // Italic Ref         ........[ ITALIC ]..
    sal_Int32 aTextLen = aParaText.getLength();
    aEditEngine.SetText(aParaText);

    // Assert changes - text insertion
    CPPUNIT_ASSERT_EQUAL(aTextLen, rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(aParaText, rDoc.GetParaAsString(sal_Int32(0)));

    // Apply Bold to appropriate selection
    pSet->Put(aBold);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(1), pSet->Count());
    aEditEngine.QuickSetAttribs(*pSet, ESelection(0, 2, 0, 14));

    // Assert changes
    std::unique_ptr<EditTextObject> pEditText1(aEditEngine.CreateTextObject());
    std::vector<editeng::Section> aAttrs1;
    pEditText1->GetAllSections(aAttrs1);
    // There should be 3 sections - woBold - wBold - woBold (w - with, wo - without)
    CPPUNIT_ASSERT_EQUAL(size_t(3), aAttrs1.size());

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[0].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[0].mnStart));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs1[0].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[0].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[1].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs1[1].mnStart));
    CPPUNIT_ASSERT_EQUAL(14, static_cast<int>(aAttrs1[1].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs1[1].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs1[1]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[2].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(14, static_cast<int>(aAttrs1[2].mnStart));
    CPPUNIT_ASSERT_EQUAL(20, static_cast<int>(aAttrs1[2].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[2].maAttributes.size()));

    // Apply Italic to appropriate selection
    pSet.reset(new SfxItemSet(aEditEngine.GetEmptyItemSet()));
    pSet->Put(aItalic);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(1), pSet->Count());
    aEditEngine.QuickSetAttribs(*pSet, ESelection(0, 8, 0, 18));

    // Assert changes
    std::unique_ptr<EditTextObject> pEditText2(aEditEngine.CreateTextObject());
    std::vector<editeng::Section> aAttrs2;
    pEditText2->GetAllSections(aAttrs2);
    // There should be 5 sections - woBold&woItalic - wBold&woItalic - wBold&wItalic - woBold&wItalic - woBold&woItalic (w - with, wo - without)
    CPPUNIT_ASSERT_EQUAL(size_t(5), aAttrs2.size());

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[0].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[0].mnStart));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs2[0].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[0].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[1].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs2[1].mnStart));
    CPPUNIT_ASSERT_EQUAL(8, static_cast<int>(aAttrs2[1].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs2[1].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs2[1]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[2].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(8, static_cast<int>(aAttrs2[2].mnStart));
    CPPUNIT_ASSERT_EQUAL(14, static_cast<int>(aAttrs2[2].mnEnd));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs2[2].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasBold(aAttrs2[2]));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasItalic(aAttrs2[2]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[3].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(14, static_cast<int>(aAttrs2[3].mnStart));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs2[3].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs2[3].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be italic.", hasItalic(aAttrs2[3]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[4].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs2[4].mnStart));
    CPPUNIT_ASSERT_EQUAL(20, static_cast<int>(aAttrs2[4].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[4].maAttributes.size()));

    // Copy text using legacy format
    uno::Reference<datatransfer::XTransferable> xData
        = aEditEngine.CreateTransferable(ESelection(0, 1, 0, aTextLen - 1));

    // Paste text at the end
    aEditEngine.InsertText(xData, OUString(), rDoc.GetEndPaM(), true);

    // Assert changes
    CPPUNIT_ASSERT_EQUAL(aTextLen + aTextLen - 2, rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(aParaText + "oldeditengineitali"),
                         rDoc.GetParaAsString(sal_Int32(0)));

    // Check updated text for appropriate Bold/Italics
    std::unique_ptr<EditTextObject> pEditText3(aEditEngine.CreateTextObject());
    std::vector<editeng::Section> aAttrs3;
    pEditText3->GetAllSections(aAttrs3);
    // There should be 9 sections - woBold&woItalic - wBold&woItalic - wBold&wItalic - woBold&wItalic - woBold&woItalic - wBold&woItalic
    // - wBold&wItalic - woBold&wItalic - woBold&woItalic(w - with, wo - without)
    CPPUNIT_ASSERT_EQUAL(size_t(9), aAttrs3.size());

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[0].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[0].mnStart));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs3[0].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[0].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[1].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs3[1].mnStart));
    CPPUNIT_ASSERT_EQUAL(8, static_cast<int>(aAttrs3[1].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[1].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs3[1]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[2].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(8, static_cast<int>(aAttrs3[2].mnStart));
    CPPUNIT_ASSERT_EQUAL(14, static_cast<int>(aAttrs3[2].mnEnd));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs3[2].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasBold(aAttrs3[2]));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasItalic(aAttrs3[2]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[3].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(14, static_cast<int>(aAttrs3[3].mnStart));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs3[3].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[3].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be italic.", hasItalic(aAttrs3[3]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[4].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs3[4].mnStart));
    CPPUNIT_ASSERT_EQUAL(21, static_cast<int>(aAttrs3[4].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[4].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[5].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(21, static_cast<int>(aAttrs3[5].mnStart));
    CPPUNIT_ASSERT_EQUAL(27, static_cast<int>(aAttrs3[5].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[5].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs3[5]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[6].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(27, static_cast<int>(aAttrs3[6].mnStart));
    CPPUNIT_ASSERT_EQUAL(33, static_cast<int>(aAttrs3[6].mnEnd));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs3[6].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasBold(aAttrs3[6]));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasItalic(aAttrs3[6]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[7].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(33, static_cast<int>(aAttrs3[7].mnStart));
    CPPUNIT_ASSERT_EQUAL(37, static_cast<int>(aAttrs3[7].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[7].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be italic.", hasItalic(aAttrs3[7]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[8].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(37, static_cast<int>(aAttrs3[8].mnStart));
    CPPUNIT_ASSERT_EQUAL(38, static_cast<int>(aAttrs3[8].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[8].maAttributes.size()));
}

// Auxiliary function to test Underline text Copy/Paste using Legacy Format
bool hasUnderline(const editeng::Section& rSecAttr)
{
    return std::any_of(
        rSecAttr.maAttributes.begin(), rSecAttr.maAttributes.end(), [](const SfxPoolItem* p) {
            return p->Which() == EE_CHAR_UNDERLINE
                   && static_cast<const SvxUnderlineItem*>(p)->GetLineStyle() == LINESTYLE_SINGLE;
        });
}

void Test::testUnderlineCopyPaste()
{
    // Create EditEngine's instance
    EditEngine aEditEngine(mpItemPool.get());

    // Get EditDoc for current EditEngine's instance
    EditDoc& rDoc = aEditEngine.GetEditDoc();

    // New instance must be empty - no initial text
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(), rDoc.GetParaAsString(sal_Int32(0)));

    // Get corresponding ItemSet for inserting Underline text
    std::unique_ptr<SfxItemSet> pSet(new SfxItemSet(aEditEngine.GetEmptyItemSet()));
    SvxUnderlineItem aULine(LINESTYLE_SINGLE, EE_CHAR_UNDERLINE);

    // Insert initial text
    OUString aParaText = u"sampletextforunderline"_ustr;
    // Positions Ref      ......*6.........*17..
    // Underline Ref      ......[UNDERLINE ]....
    sal_Int32 aTextLen = aParaText.getLength();
    aEditEngine.SetText(aParaText);

    // Apply Underline style
    pSet->Put(aULine);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(1), pSet->Count());
    aEditEngine.QuickSetAttribs(*pSet, ESelection(0, 6, 0, 18));

    // Assert changes
    std::unique_ptr<EditTextObject> pEditText1(aEditEngine.CreateTextObject());
    std::vector<editeng::Section> aAttrs1;
    pEditText1->GetAllSections(aAttrs1);

    // There should be 3 sections - woUnderline - wUnderline - woUnderline (w - with, wo - without)
    CPPUNIT_ASSERT_EQUAL(size_t(3), aAttrs1.size());

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[0].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[0].mnStart));
    CPPUNIT_ASSERT_EQUAL(6, static_cast<int>(aAttrs1[0].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[0].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[1].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(6, static_cast<int>(aAttrs1[1].mnStart));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs1[1].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs1[1].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be underlined.", hasUnderline(aAttrs1[1]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[2].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs1[2].mnStart));
    CPPUNIT_ASSERT_EQUAL(22, static_cast<int>(aAttrs1[2].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[2].maAttributes.size()));

    // Copy text using legacy format
    uno::Reference<datatransfer::XTransferable> xData
        = aEditEngine.CreateTransferable(ESelection(0, 6, 0, aTextLen - 4));

    // Paste text at the end
    aEditEngine.InsertText(xData, OUString(), rDoc.GetEndPaM(), true);

    // Assert changes
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(aTextLen + strlen("textforunder")),
                         rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(aParaText + "textforunder"), rDoc.GetParaAsString(sal_Int32(0)));

    // Check updated text for appropriate Underline
    std::unique_ptr<EditTextObject> pEditText2(aEditEngine.CreateTextObject());
    std::vector<editeng::Section> aAttrs2;
    pEditText2->GetAllSections(aAttrs2);

    // There should be 4 sections - woUnderline - wUnderline - woUnderline - wUnderline (w - with, wo - without)
    CPPUNIT_ASSERT_EQUAL(size_t(4), aAttrs2.size());

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[0].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[0].mnStart));
    CPPUNIT_ASSERT_EQUAL(6, static_cast<int>(aAttrs2[0].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[0].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[1].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(6, static_cast<int>(aAttrs2[1].mnStart));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs2[1].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs2[1].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be underlined.", hasUnderline(aAttrs2[1]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[2].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs2[2].mnStart));
    CPPUNIT_ASSERT_EQUAL(22, static_cast<int>(aAttrs2[2].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[2].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[3].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(22, static_cast<int>(aAttrs2[3].mnStart));
    CPPUNIT_ASSERT_EQUAL(34, static_cast<int>(aAttrs2[3].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs2[3].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be underlined.", hasUnderline(aAttrs2[3]));
}

void Test::testMultiParaCopyPaste()
{
    // Create EditEngine's instance
    EditEngine aEditEngine(mpItemPool.get());

    // Get EditDoc for current EditEngine's instance
    EditDoc& rDoc = aEditEngine.GetEditDoc();

    // Initially no text should be there
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(), rDoc.GetParaAsString(sal_Int32(0)));

    // Insert initial text
    OUString aFirstPara = u"This is first paragraph"_ustr;
    OUString aSecondPara = u"This is second paragraph"_ustr;
    OUString aThirdPara = u"This is third paragraph"_ustr;
    OUString aText = aFirstPara + "\n" + aSecondPara + "\n" + aThirdPara;
    sal_Int32 aTextLen = aFirstPara.getLength() + aSecondPara.getLength() + aThirdPara.getLength();
    aEditEngine.SetText(aText);
    sal_Int32 aCopyTextLen = aFirstPara.getLength() + aSecondPara.getLength();

    // Assert changes
    CPPUNIT_ASSERT_EQUAL(aTextLen, rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(aFirstPara, rDoc.GetParaAsString(sal_Int32(0)));
    CPPUNIT_ASSERT_EQUAL(aSecondPara, rDoc.GetParaAsString(sal_Int32(1)));
    CPPUNIT_ASSERT_EQUAL(aThirdPara, rDoc.GetParaAsString(sal_Int32(2)));

    // Copy initial text using legacy format
    uno::Reference<datatransfer::XTransferable> xData
        = aEditEngine.CreateTransferable(ESelection(0, 0, 1, aSecondPara.getLength()));

    // Paste text at the end
    aEditEngine.InsertText(xData, OUString(), rDoc.GetEndPaM(), true);

    // Assert changes
    OUString aThirdParaAfterCopyPaste = aThirdPara + aFirstPara;
    CPPUNIT_ASSERT_EQUAL(aTextLen + aCopyTextLen, rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(aFirstPara, rDoc.GetParaAsString(sal_Int32(0)));
    CPPUNIT_ASSERT_EQUAL(aSecondPara, rDoc.GetParaAsString(sal_Int32(1)));
    CPPUNIT_ASSERT_EQUAL(aThirdParaAfterCopyPaste, rDoc.GetParaAsString(sal_Int32(2)));
    CPPUNIT_ASSERT_EQUAL(aSecondPara, rDoc.GetParaAsString(sal_Int32(3)));
}

void Test::testParaBoldItalicCopyPaste()
{
    // Create EditEngine's instance
    EditEngine aEditEngine(mpItemPool.get());

    // Get EditDoc for current EditEngine's instance
    EditDoc& rDoc = aEditEngine.GetEditDoc();

    // Initially no text should be there
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(), rDoc.GetParaAsString(sal_Int32(0)));

    // Get corresponding ItemSet for inserting Bold/Italic text
    std::unique_ptr<SfxItemSet> pSet(new SfxItemSet(aEditEngine.GetEmptyItemSet()));
    SvxWeightItem aBold(WEIGHT_BOLD, EE_CHAR_WEIGHT);
    SvxPostureItem aItalic(ITALIC_NORMAL, EE_CHAR_ITALIC);

    // Insert initial text
    OUString aFirstPara = u"This is first paragraph"_ustr;
    // Positions Ref       .....*5.*8....*14*17...
    // Bold Ref            .....[    BOLD   ].....
    // Italic Ref          ..............[     ITA
    // Copy Ref            ........[      Copy Sel
    OUString aSecondPara = u"This is second paragraph"_ustr;
    // Positions Ref        .....*5.*8...*13..*18...
    // Bold Ref             .....[    BOLD    ].....
    // Italic Ref           LIC     ]...............
    // Copy Ref             ection       ]..........
    OUString aThirdPara = u"This is third paragraph"_ustr;
    OUString aText = aFirstPara + "\n" + aSecondPara + "\n" + aThirdPara;
    sal_Int32 aTextLen = aFirstPara.getLength() + aSecondPara.getLength() + aThirdPara.getLength();
    aEditEngine.SetText( aText );
    OUString aCopyText = u"first paragraphThis is second"_ustr;
    sal_Int32 aCopyTextLen = aCopyText.getLength();

    // Assert changes - text insertion
    CPPUNIT_ASSERT_EQUAL(aTextLen, rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(aFirstPara, rDoc.GetParaAsString(sal_Int32(0)));
    CPPUNIT_ASSERT_EQUAL(aSecondPara, rDoc.GetParaAsString(sal_Int32(1)));
    CPPUNIT_ASSERT_EQUAL(aThirdPara, rDoc.GetParaAsString(sal_Int32(2)));

    // Apply Bold to appropriate selections
    pSet->Put(aBold);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(1), pSet->Count());
    aEditEngine.QuickSetAttribs(*pSet, ESelection(0, 5, 0, 18));
    aEditEngine.QuickSetAttribs(*pSet, ESelection(1, 5, 1, 19));

    // Assert changes
    std::unique_ptr<EditTextObject> pEditText1(aEditEngine.CreateTextObject());
    std::vector<editeng::Section> aAttrs1;
    pEditText1->GetAllSections(aAttrs1);
    // There should be 7 sections - woB - wB - woB -woB -wB -woB -woB (w - with, wo - without, B - Bold, I - Italic)
    CPPUNIT_ASSERT_EQUAL(size_t(7), aAttrs1.size());

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[0].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[0].mnStart));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs1[0].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[0].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[1].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs1[1].mnStart));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs1[1].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs1[1].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs1[1]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[2].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs1[2].mnStart));
    CPPUNIT_ASSERT_EQUAL(23, static_cast<int>(aAttrs1[2].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[2].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs1[3].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[3].mnStart));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs1[3].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[3].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs1[4].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs1[4].mnStart));
    CPPUNIT_ASSERT_EQUAL(19, static_cast<int>(aAttrs1[4].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs1[4].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs1[4]));

    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs1[5].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(19, static_cast<int>(aAttrs1[5].mnStart));
    CPPUNIT_ASSERT_EQUAL(24, static_cast<int>(aAttrs1[5].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[5].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs1[6].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[6].mnStart));
    CPPUNIT_ASSERT_EQUAL(23, static_cast<int>(aAttrs1[6].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs1[6].maAttributes.size()));

    // Apply Italic to appropriate selection
    pSet.reset(new SfxItemSet(aEditEngine.GetEmptyItemSet()));
    pSet->Put(aItalic);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(1), pSet->Count());
    aEditEngine.QuickSetAttribs(*pSet, ESelection(0, 14, 1, 9));

    // Assert changes
    std::unique_ptr<EditTextObject> pEditText2(aEditEngine.CreateTextObject());
    std::vector<editeng::Section> aAttrs2;
    pEditText2->GetAllSections(aAttrs2);
    // There should be 9 sections - woB&woI - wB&woI - wB&wI -woB&wI - woB&wI - wB&wI - wB&woI - woB&woI - woB&woI (w - with, wo - without, B - Bold, I - Italic)
    CPPUNIT_ASSERT_EQUAL(size_t(9), aAttrs2.size());

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[0].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[0].mnStart));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs2[0].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[0].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[1].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs2[1].mnStart));
    CPPUNIT_ASSERT_EQUAL(14, static_cast<int>(aAttrs2[1].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs2[1].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs2[1]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[2].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(14, static_cast<int>(aAttrs2[2].mnStart));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs2[2].mnEnd));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs2[2].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasBold(aAttrs2[2]));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasItalic(aAttrs2[2]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[3].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs2[3].mnStart));
    CPPUNIT_ASSERT_EQUAL(23, static_cast<int>(aAttrs2[3].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs2[3].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be italic.", hasItalic(aAttrs2[3]));

    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs2[4].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[4].mnStart));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs2[4].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs2[4].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be italic.", hasItalic(aAttrs2[4]));

    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs2[5].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs2[5].mnStart));
    CPPUNIT_ASSERT_EQUAL(9, static_cast<int>(aAttrs2[5].mnEnd));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs2[5].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasBold(aAttrs2[5]));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasItalic(aAttrs2[5]));

    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs2[6].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(9, static_cast<int>(aAttrs2[6].mnStart));
    CPPUNIT_ASSERT_EQUAL(19, static_cast<int>(aAttrs2[6].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs2[6].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs2[6]));

    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs2[7].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(19, static_cast<int>(aAttrs2[7].mnStart));
    CPPUNIT_ASSERT_EQUAL(24, static_cast<int>(aAttrs2[7].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[7].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs2[8].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[8].mnStart));
    CPPUNIT_ASSERT_EQUAL(23, static_cast<int>(aAttrs2[8].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs2[8].maAttributes.size()));

    // Copy text using legacy format
    uno::Reference<datatransfer::XTransferable> xData
        = aEditEngine.CreateTransferable(ESelection(0, 8, 1, 14));

    // Paste text at the end
    aEditEngine.InsertText(xData, OUString(), rDoc.GetEndPaM(), true);

    // Assert changes
    OUString aThirdParaAfterCopyPaste = aThirdPara + "first paragraph";
    CPPUNIT_ASSERT_EQUAL( aTextLen + aCopyTextLen, rDoc.GetTextLen() );
    CPPUNIT_ASSERT_EQUAL( aFirstPara, rDoc.GetParaAsString(sal_Int32(0)) );
    CPPUNIT_ASSERT_EQUAL( aSecondPara, rDoc.GetParaAsString(sal_Int32(1)) );
    CPPUNIT_ASSERT_EQUAL( aThirdParaAfterCopyPaste, rDoc.GetParaAsString(sal_Int32(2)) );
    CPPUNIT_ASSERT_EQUAL( u"This is second"_ustr, rDoc.GetParaAsString(sal_Int32(3)) );

    // Check updated text for appropriate Bold/Italics
    std::unique_ptr<EditTextObject> pEditText3(aEditEngine.CreateTextObject());
    std::vector<editeng::Section> aAttrs3;
    pEditText3->GetAllSections(aAttrs3);
    // There should be 15 sections - woB&woI - wB&woI - wB&wI -woB&wI - woB&wI - wB&wI - wB&woI - woB&woI - woB&woI
    // - wB&woI - wB&wI - woB&wI - -woB&wI - wB&wI - wB&woI (w - with, wo - without, B - Bold, I - Italic)
    CPPUNIT_ASSERT_EQUAL(size_t(15), aAttrs3.size());

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[0].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[0].mnStart));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs3[0].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[0].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[1].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs3[1].mnStart));
    CPPUNIT_ASSERT_EQUAL(14, static_cast<int>(aAttrs3[1].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[1].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs3[1]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[2].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(14, static_cast<int>(aAttrs3[2].mnStart));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs3[2].mnEnd));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs3[2].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasBold(aAttrs3[2]));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasItalic(aAttrs3[2]));

    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[3].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(18, static_cast<int>(aAttrs3[3].mnStart));
    CPPUNIT_ASSERT_EQUAL(23, static_cast<int>(aAttrs3[3].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[3].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be italic.", hasItalic(aAttrs3[3]));

    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[4].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[4].mnStart));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs3[4].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[4].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be italic.", hasItalic(aAttrs3[4]));

    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[5].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs3[5].mnStart));
    CPPUNIT_ASSERT_EQUAL(9, static_cast<int>(aAttrs3[5].mnEnd));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs3[5].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasBold(aAttrs3[5]));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasItalic(aAttrs3[5]));

    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[6].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(9, static_cast<int>(aAttrs3[6].mnStart));
    CPPUNIT_ASSERT_EQUAL(19, static_cast<int>(aAttrs3[6].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[6].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs3[6]));

    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[7].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(19, static_cast<int>(aAttrs3[7].mnStart));
    CPPUNIT_ASSERT_EQUAL(24, static_cast<int>(aAttrs3[7].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[7].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs3[8].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[8].mnStart));
    CPPUNIT_ASSERT_EQUAL(23, static_cast<int>(aAttrs3[8].mnEnd));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[8].maAttributes.size()));

    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs3[9].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(23, static_cast<int>(aAttrs3[9].mnStart));
    CPPUNIT_ASSERT_EQUAL(29, static_cast<int>(aAttrs3[9].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[9].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs3[9]));

    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs3[10].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(29, static_cast<int>(aAttrs3[10].mnStart));
    CPPUNIT_ASSERT_EQUAL(33, static_cast<int>(aAttrs3[10].mnEnd));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs3[10].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasBold(aAttrs3[10]));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasItalic(aAttrs3[10]));

    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs3[11].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(33, static_cast<int>(aAttrs3[11].mnStart));
    CPPUNIT_ASSERT_EQUAL(38, static_cast<int>(aAttrs3[11].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[11].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be italic.", hasItalic(aAttrs3[11]));

    CPPUNIT_ASSERT_EQUAL(3, static_cast<int>(aAttrs3[12].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs3[12].mnStart));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs3[12].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[12].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be italic.", hasItalic(aAttrs3[12]));

    CPPUNIT_ASSERT_EQUAL(3, static_cast<int>(aAttrs3[13].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs3[13].mnStart));
    CPPUNIT_ASSERT_EQUAL(9, static_cast<int>(aAttrs3[13].mnEnd));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs3[13].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasBold(aAttrs3[13]));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasItalic(aAttrs3[13]));

    CPPUNIT_ASSERT_EQUAL(3, static_cast<int>(aAttrs3[14].mnParagraph));
    CPPUNIT_ASSERT_EQUAL(9, static_cast<int>(aAttrs3[14].mnStart));
    CPPUNIT_ASSERT_EQUAL(14, static_cast<int>(aAttrs3[14].mnEnd));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs3[14].maAttributes.size()));
    CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs3[14]));
}

void Test::testParaStartCopyPaste()
{
    // Create EditEngine's instance
    EditEngine aEditEngine(mpItemPool.get());

    // Get EditDoc for current EditEngine's instance
    EditDoc& rDoc = aEditEngine.GetEditDoc();

    // Initially no text should be there
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(), rDoc.GetParaAsString(sal_Int32(0)));

    // Insert initial text
    OUString aFirstPara = u"This is first paragraph"_ustr;
    // Selection Ref       ........8..............
    OUString aSecondPara = u"This is second paragraph"_ustr;
    // Selection Ref        .............14.........
    OUString aThirdPara = u"This is third paragraph"_ustr;
    OUString aText = aFirstPara + "\n" + aSecondPara + "\n" + aThirdPara;
    sal_Int32 aTextLen = aFirstPara.getLength() + aSecondPara.getLength() + aThirdPara.getLength();
    aEditEngine.SetText( aText );
    OUString aCopyText = u"first paragraphThis is second"_ustr;
    sal_Int32 aCopyTextLen = aCopyText.getLength();

    // Assert changes
    CPPUNIT_ASSERT_EQUAL(aTextLen, rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(aFirstPara, rDoc.GetParaAsString(sal_Int32(0)));
    CPPUNIT_ASSERT_EQUAL(aSecondPara, rDoc.GetParaAsString(sal_Int32(1)));
    CPPUNIT_ASSERT_EQUAL(aThirdPara, rDoc.GetParaAsString(sal_Int32(2)));

    // Copy initial text using legacy format
    uno::Reference<datatransfer::XTransferable> xData
        = aEditEngine.CreateTransferable(ESelection(0, 8, 1, 14));

    // Paste text at the start
    aEditEngine.InsertText(xData, OUString(), rDoc.GetStartPaM(), true);

    // Assert changes
    OUString aSecondParaAfterCopyPaste = "This is second" + aFirstPara;
    CPPUNIT_ASSERT_EQUAL( aTextLen + aCopyTextLen, rDoc.GetTextLen() );
    CPPUNIT_ASSERT_EQUAL( u"first paragraph"_ustr, rDoc.GetParaAsString(sal_Int32(0)) );
    CPPUNIT_ASSERT_EQUAL( aSecondParaAfterCopyPaste, rDoc.GetParaAsString(sal_Int32(1)) );
    CPPUNIT_ASSERT_EQUAL( aSecondPara, rDoc.GetParaAsString(sal_Int32(2)) );
    CPPUNIT_ASSERT_EQUAL( aThirdPara, rDoc.GetParaAsString(sal_Int32(3)) );
}

void Test::testSectionAttributes()
{
    EditEngine aEngine(mpItemPool.get());

    std::unique_ptr<SfxItemSet> pSet(new SfxItemSet(aEngine.GetEmptyItemSet()));
    SvxWeightItem aBold(WEIGHT_BOLD, EE_CHAR_WEIGHT);
    SvxPostureItem aItalic(ITALIC_NORMAL, EE_CHAR_ITALIC);

    {
        aEngine.SetText(u"aaabbbccc"_ustr);
        pSet->Put(aBold);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("There should be exactly one item.",
                                     static_cast<sal_uInt16>(1), pSet->Count());
        aEngine.QuickSetAttribs(*pSet,
                                ESelection(0, 0, 0, 6)); // 'aaabbb' - end point is not inclusive.
        pSet.reset(new SfxItemSet(aEngine.GetEmptyItemSet()));
        pSet->Put(aItalic);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("There should be exactly one item.",
                                     static_cast<sal_uInt16>(1), pSet->Count());

        aEngine.QuickSetAttribs(*pSet, ESelection(0, 3, 0, 9)); // 'bbbccc'
        std::unique_ptr<EditTextObject> pEditText(aEngine.CreateTextObject());
        CPPUNIT_ASSERT_MESSAGE("Failed to create text object.", pEditText);
        std::vector<editeng::Section> aAttrs;
        pEditText->GetAllSections(aAttrs);

        // Now, we should have a total of 3 sections.
        CPPUNIT_ASSERT_EQUAL_MESSAGE("There should be 3 sections.", static_cast<size_t>(3),
                                     aAttrs.size());

        // First section should be 0-3 of paragraph 0, and it should only have boldness applied.
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[0].mnParagraph));
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[0].mnStart));
        CPPUNIT_ASSERT_EQUAL(3, static_cast<int>(aAttrs[0].mnEnd));
        CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs[0].maAttributes.size()));
        CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs[0]));

        // Second section should be 3-6, and it should be both bold and italic.
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[1].mnParagraph));
        CPPUNIT_ASSERT_EQUAL(3, static_cast<int>(aAttrs[1].mnStart));
        CPPUNIT_ASSERT_EQUAL(6, static_cast<int>(aAttrs[1].mnEnd));
        CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs[1].maAttributes.size()));
        CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasBold(aAttrs[1]));
        CPPUNIT_ASSERT_MESSAGE("This section must be bold and italic.", hasItalic(aAttrs[1]));

        // Third section should be 6-9, and it should be only italic.
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[2].mnParagraph));
        CPPUNIT_ASSERT_EQUAL(6, static_cast<int>(aAttrs[2].mnStart));
        CPPUNIT_ASSERT_EQUAL(9, static_cast<int>(aAttrs[2].mnEnd));
        CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs[2].maAttributes.size()));
        CPPUNIT_ASSERT_MESSAGE("This section must be italic.", hasItalic(aAttrs[2]));
    }

    {
        // Set text consisting of 5 paragraphs with the 2nd and 4th paragraphs
        // being empty.
        aEngine.Clear();
        aEngine.SetText(u"one\n\ntwo\n\nthree"_ustr);
        sal_Int32 nParaCount = aEngine.GetParagraphCount();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(5), nParaCount);

        // Apply boldness to paragraphs 1, 3, 5 only. Leave 2 and 4 unformatted.
        pSet.reset(new SfxItemSet(aEngine.GetEmptyItemSet()));
        pSet->Put(aBold);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("There should be exactly one item.",
                                     static_cast<sal_uInt16>(1), pSet->Count());
        aEngine.QuickSetAttribs(*pSet, ESelection(0, 0, 0, 3));
        aEngine.QuickSetAttribs(*pSet, ESelection(2, 0, 2, 3));
        aEngine.QuickSetAttribs(*pSet, ESelection(4, 0, 4, 5));

        std::unique_ptr<EditTextObject> pEditText(aEngine.CreateTextObject());
        CPPUNIT_ASSERT_MESSAGE("Failed to create text object.", pEditText);
        std::vector<editeng::Section> aAttrs;
        pEditText->GetAllSections(aAttrs);
        CPPUNIT_ASSERT_EQUAL(size_t(5), aAttrs.size());

        // 1st, 3rd and 5th sections should correspond with 1st, 3rd and 5th paragraphs.
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[0].mnParagraph));
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[0].mnStart));
        CPPUNIT_ASSERT_EQUAL(3, static_cast<int>(aAttrs[0].mnEnd));
        CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs[0].maAttributes.size()));
        CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs[0]));

        CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(aAttrs[2].mnParagraph));
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[2].mnStart));
        CPPUNIT_ASSERT_EQUAL(3, static_cast<int>(aAttrs[2].mnEnd));
        CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs[2].maAttributes.size()));
        CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs[2]));

        CPPUNIT_ASSERT_EQUAL(4, static_cast<int>(aAttrs[4].mnParagraph));
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[4].mnStart));
        CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(aAttrs[4].mnEnd));
        CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs[4].maAttributes.size()));
        CPPUNIT_ASSERT_MESSAGE("This section must be bold.", hasBold(aAttrs[4]));

        // The 2nd and 4th paragraphs should be empty.
        CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aAttrs[1].mnParagraph));
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[1].mnStart));
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[1].mnEnd));
        CPPUNIT_ASSERT_MESSAGE("Attribute array should be empty.", aAttrs[1].maAttributes.empty());

        CPPUNIT_ASSERT_EQUAL(3, static_cast<int>(aAttrs[3].mnParagraph));
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[3].mnStart));
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[3].mnEnd));
        CPPUNIT_ASSERT_MESSAGE("Attribute array should be empty.", aAttrs[3].maAttributes.empty());
    }

    {
        aEngine.Clear();
        aEngine.SetText(u"one\ntwo"_ustr);
        CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(2), aEngine.GetParagraphCount());

        // embolden 2nd paragraph
        pSet.reset(new SfxItemSet(aEngine.GetEmptyItemSet()));
        pSet->Put(aBold);
        aEngine.QuickSetAttribs(*pSet, ESelection(1, 0, 1, 3));
        // disboldify 1st paragraph
        SvxWeightItem aNotSoBold(WEIGHT_NORMAL, EE_CHAR_WEIGHT);
        pSet->Put(aNotSoBold);
        aEngine.QuickSetAttribs(*pSet, ESelection(0, 0, 0, 3));

        // now delete & join the paragraphs - this is fdo#85496 scenario
        aEngine.QuickDelete(ESelection(0, 0, 1, 3));
        CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), aEngine.GetParagraphCount());

        std::unique_ptr<EditTextObject> pEditText(aEngine.CreateTextObject());
        CPPUNIT_ASSERT_MESSAGE("Failed to create text object.", pEditText);
        std::vector<editeng::Section> aAttrs;
        pEditText->GetAllSections(aAttrs);

        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), aAttrs.size());

        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[0].mnParagraph));
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[0].mnStart));
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(aAttrs[0].mnEnd));
        std::set<sal_uInt16> whiches;
        for (size_t i = 0; i < aAttrs[0].maAttributes.size(); ++i)
        {
            sal_uInt16 const nWhich(aAttrs[0].maAttributes[i]->Which());
            CPPUNIT_ASSERT_MESSAGE("duplicate item in text portion attributes",
                                   whiches.insert(nWhich).second);
        }
    }
}

void Test::testLargeParaCopyPaste()
{
    // Create EditEngine's instance
    EditEngine aEditEngine(mpItemPool.get());

    // Get EditDoc for current EditEngine's instance
    EditDoc& rDoc = aEditEngine.GetEditDoc();

    // Initially no text should be there
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(OUString(), rDoc.GetParaAsString(sal_Int32(0)));

    // Insert initial text
    OUString aFirstPara = u"This is first paragraph"_ustr;
    OUString aSecondPara = u"This is second paragraph"_ustr;
    OUString aThirdPara = u"This is third paragraph"_ustr;
    OUString aFourthPara = u"This is fourth paragraph"_ustr;
    OUString aFifthPara = u"This is fifth paragraph"_ustr;
    OUString aSixthPara = u"This is sixth paragraph"_ustr;
    //Positions Ref:       ........*8.............
    OUString aSeventhPara = u"This is seventh paragraph"_ustr;
    OUString aEighthPara = u"This is eighth paragraph"_ustr;
    //Positions Ref:        .............*13
    OUString aNinthPara = u"This is ninth paragraph"_ustr;
    OUString aTenthPara = u"This is tenth paragraph"_ustr;
    OUString aText = aFirstPara + "\n" + aSecondPara + "\n" + aThirdPara + "\n" +
        aFourthPara + "\n" + aFifthPara + "\n" + aSixthPara + "\n" + aSeventhPara + "\n" +
        aEighthPara + "\n" + aNinthPara + "\n" + aTenthPara;
    sal_Int32 aTextLen = aFirstPara.getLength() + aSecondPara.getLength() + aThirdPara.getLength() +
        aFourthPara.getLength() + aFifthPara.getLength() + aSixthPara.getLength() +
        aSeventhPara.getLength() + aEighthPara.getLength() + aNinthPara.getLength() + aTenthPara.getLength();
    aEditEngine.SetText( aText );
    OUString aCopyText = u"sixth paragraphThis is seventh paragraphThis is eighth"_ustr;
    sal_Int32 aCopyTextLen = aCopyText.getLength();

    // Assert changes
    CPPUNIT_ASSERT_EQUAL(aTextLen, rDoc.GetTextLen());
    CPPUNIT_ASSERT_EQUAL(aFirstPara, rDoc.GetParaAsString(sal_Int32(0)));
    CPPUNIT_ASSERT_EQUAL(aSecondPara, rDoc.GetParaAsString(sal_Int32(1)));
    CPPUNIT_ASSERT_EQUAL(aThirdPara, rDoc.GetParaAsString(sal_Int32(2)));
    CPPUNIT_ASSERT_EQUAL(aFourthPara, rDoc.GetParaAsString(sal_Int32(3)));
    CPPUNIT_ASSERT_EQUAL(aFifthPara, rDoc.GetParaAsString(sal_Int32(4)));
    CPPUNIT_ASSERT_EQUAL(aSixthPara, rDoc.GetParaAsString(sal_Int32(5)));
    CPPUNIT_ASSERT_EQUAL(aSeventhPara, rDoc.GetParaAsString(sal_Int32(6)));
    CPPUNIT_ASSERT_EQUAL(aEighthPara, rDoc.GetParaAsString(sal_Int32(7)));
    CPPUNIT_ASSERT_EQUAL(aNinthPara, rDoc.GetParaAsString(sal_Int32(8)));
    CPPUNIT_ASSERT_EQUAL(aTenthPara, rDoc.GetParaAsString(sal_Int32(9)));

    // Copy initial text using legacy format
    uno::Reference<datatransfer::XTransferable> xData
        = aEditEngine.CreateTransferable(ESelection(5, 8, 7, 14));

    // Paste text at the end of 4th Para
    ContentNode* pLastNode = rDoc.GetObject(3);
    aEditEngine.InsertText(xData, OUString(), EditPaM(pLastNode, pLastNode->Len()), true);

    // Assert changes
    OUString aFourthParaAfterCopyPaste = aFourthPara + "sixth paragraph";
    CPPUNIT_ASSERT_EQUAL( aTextLen + aCopyTextLen, rDoc.GetTextLen() );
    CPPUNIT_ASSERT_EQUAL( aFirstPara, rDoc.GetParaAsString(sal_Int32(0)) );
    CPPUNIT_ASSERT_EQUAL( aSecondPara, rDoc.GetParaAsString(sal_Int32(1)) );
    CPPUNIT_ASSERT_EQUAL( aThirdPara, rDoc.GetParaAsString(sal_Int32(2)) );
    CPPUNIT_ASSERT_EQUAL( aFourthParaAfterCopyPaste, rDoc.GetParaAsString(sal_Int32(3)) );
    CPPUNIT_ASSERT_EQUAL( aSeventhPara, rDoc.GetParaAsString(sal_Int32(4)) );
    CPPUNIT_ASSERT_EQUAL( u"This is eighth"_ustr, rDoc.GetParaAsString(sal_Int32(5)) );
    CPPUNIT_ASSERT_EQUAL( aFifthPara, rDoc.GetParaAsString(sal_Int32(6)) );
    CPPUNIT_ASSERT_EQUAL( aSixthPara, rDoc.GetParaAsString(sal_Int32(7)) );
    CPPUNIT_ASSERT_EQUAL( aSeventhPara, rDoc.GetParaAsString(sal_Int32(8)) );
    CPPUNIT_ASSERT_EQUAL( aEighthPara, rDoc.GetParaAsString(sal_Int32(9)) );
    CPPUNIT_ASSERT_EQUAL( aNinthPara, rDoc.GetParaAsString(sal_Int32(10)) );
    CPPUNIT_ASSERT_EQUAL( aTenthPara, rDoc.GetParaAsString(sal_Int32(11)) );
}

OUString lcl_translitTest(EditEngine& aEditEngine, const OUString& text, const ESelection& esel,
                          const TransliterationFlags nType)
{
    aEditEngine.SetText(text);
    aEditEngine.TransliterateText(esel, nType);
    return aEditEngine.GetText();
}

void Test::testTransliterate()
{
    // Create EditEngine's instance
    EditEngine editEng(mpItemPool.get());

    OUString sText(u"one (two) three"_ustr);
    editEng.SetText(sText);
    editEng.TransliterateText(ESelection(0, 0, 0, sText.getLength()), TransliterationFlags::TITLE_CASE);
    CPPUNIT_ASSERT_EQUAL(u"One (Two) Three"_ustr, editEng.GetText());

    using TF = TransliterationFlags;
    static constexpr OUString sText2 = u"Mary Jones met joe Smith. Time Passed."_ustr;
    int selStart = 12;
    int selEnd = 12;
    ESelection esel(0, selStart, 0, selEnd);
    ESelection eSentenSel(0, 0, 0, 25);

    /* DocumentContentOperationsManager checks if the cursor is inside of a word before transliterating,
     * but Edit Engine has no such check. Therefore, behavior is different between these two when the
     * cursor is on a word boundary. */

    /* No selection tests. Cursor between the ' ' and 'm' before 'met' - except in SENTENCE_CASE where the complete sentence is selected.*/
    CPPUNIT_ASSERT_EQUAL(u""_ustr, editEng.GetText(esel));
    CPPUNIT_ASSERT_EQUAL(u"Mary jones met joe smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, eSentenSel, TF::SENTENCE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones Met joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones MET joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::UPPERCASE_LOWERCASE));

    /* No selection tests. Cursor between the 't' and the ' ' after 'met'. */
    selStart = 14;
    selEnd = 14;
    esel = ESelection(0, selStart, 0, selEnd);
    CPPUNIT_ASSERT_EQUAL(u""_ustr, editEng.GetText(esel));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones Met joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones MET joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::UPPERCASE_LOWERCASE));

    /* No selection tests. Cursor between the 'h' and the '.' after 'Smith'. */
    selStart = 24;
    selEnd = 24;
    esel = ESelection(0, selStart, 0, selEnd);
    CPPUNIT_ASSERT_EQUAL(u""_ustr, editEng.GetText(esel));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met joe SMITH. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met joe smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::UPPERCASE_LOWERCASE));

    /* No selection tests. Cursor between the 'm' and 'e' in 'met'. */
    selStart = 12;
    selEnd = 12;
    esel = ESelection(0, selStart, 0, selEnd);
    CPPUNIT_ASSERT_EQUAL(u""_ustr, editEng.GetText(esel));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones Met joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones MET joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::UPPERCASE_LOWERCASE));

    /* Test behavior when there is a selection that does not cross a word boundary: "met" */
    selStart = 11;
    selEnd = 14;
    esel = ESelection(0, selStart, 0, selEnd);
    CPPUNIT_ASSERT_EQUAL(u"met"_ustr, editEng.GetText(esel));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones Met joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones MET joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::UPPERCASE_LOWERCASE));

    /* Test behavior when there is a selection that does not begin at a word boundary: "et" */
    selStart = 12;
    selEnd = 14;
    esel = ESelection(0, selStart, 0, selEnd);
    CPPUNIT_ASSERT_EQUAL(u"et"_ustr, editEng.GetText(esel));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones mEt joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones mET joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::UPPERCASE_LOWERCASE));

    /* Test behavior when there is a selection that ends in the middle of a word */
    selStart = 11;
    selEnd = 13;
    esel = ESelection(0, selStart, 0, selEnd);
    CPPUNIT_ASSERT_EQUAL(u"me"_ustr, editEng.GetText(esel));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones Met joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones MEt joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::UPPERCASE_LOWERCASE));

    /* Test behavior when there is a selection that crosses a word boundary: "nes met joe Sm" */
    selStart = 7;
    selEnd = 21;
    esel = ESelection(0, selStart, 0, selEnd);
    CPPUNIT_ASSERT_EQUAL(u"nes met joe Sm"_ustr, editEng.GetText(esel));
    CPPUNIT_ASSERT_EQUAL(u"Mary JoNes met joe smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::SENTENCE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary JoNes Met Joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary JoNES MET JOE SMith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met joe smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::UPPERCASE_LOWERCASE));

    /* Test behavior when there is a selection that crosses a sentence boundary: "joe Smith. Time Passed." */
    selStart = 15;
    selEnd = 38;
    esel = ESelection(0, selStart, 0, selEnd);
    editEng.SetText(sText2);
    CPPUNIT_ASSERT_EQUAL(u"joe Smith. Time Passed."_ustr, editEng.GetText(esel));

    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met Joe smith. Time passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::SENTENCE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met Joe Smith. Time Passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met JOE SMITH. TIME PASSED."_ustr, lcl_translitTest(editEng, sText2, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"Mary Jones met joe smith. time passed."_ustr, lcl_translitTest(editEng, sText2, esel, TF::UPPERCASE_LOWERCASE));

    /* Test behavior when sentence ends with a capital that is not selected: "CURRENT IS EQUAL TO 10 A" */
    selStart = 0;
    selEnd = 19;
    esel = ESelection(0, selStart, 0, selEnd);
    static constexpr OUString sText3(u"CURRENT IS EQUAL TO 10 A"_ustr);
    editEng.SetText(sText3);
    CPPUNIT_ASSERT_EQUAL(u"CURRENT IS EQUAL TO"_ustr, editEng.GetText(esel));

    CPPUNIT_ASSERT_EQUAL(u"Current is equal to 10 A"_ustr, lcl_translitTest(editEng, sText3, esel, TF::SENTENCE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"Current Is Equal To 10 A"_ustr, lcl_translitTest(editEng, sText3, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"CURRENT IS EQUAL TO 10 A"_ustr, lcl_translitTest(editEng, sText3, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"current is equal to 10 A"_ustr, lcl_translitTest(editEng, sText3, esel, TF::UPPERCASE_LOWERCASE));

}

void Test::testTdf147196()
{
    EditEngine editEng( mpItemPool.get() );
    editEng.SetText(u"2.2 Publication of information - CAA\nSection 4.2 of a CA\'s Certificate Policy and/or Certification Practice Statement SHALL state the CA\'s policy or practice on processing CAA Records for Fully Qualified Domain Names; that policy shall be consistent with these Requirements. \n\nIt shall clearly specify the set of Issuer Domain Names that the CA recognises in CAA \"issue\" or \"issuewild\" records as permitting it to issue. The CA SHALL log all actions taken, if any, consistent with its processing practice."_ustr);
    editEng.TransliterateText(ESelection(0, 0, 3, 232), TransliterationFlags::TITLE_CASE);
    CPPUNIT_ASSERT_EQUAL(u"2.2 Publication Of Information - Caa\nSection 4.2 Of A Ca\'s Certificate Policy And/Or Certification Practice Statement Shall State The Ca\'s Policy Or Practice On Processing Caa Records For Fully Qualified Domain Names; That Policy Shall Be Consistent With These Requirements. \n\nIt Shall Clearly Specify The Set Of Issuer Domain Names That The Ca Recognises In Caa \"Issue\" Or \"Issuewild\" Records As Permitting It To Issue. The Ca Shall Log All Actions Taken, If Any, Consistent With Its Processing Practice."_ustr, editEng.GetText());
}

void Test::testTdf148148()
{
    using TF = TransliterationFlags;
    EditEngine editEng(mpItemPool.get());

    /* Test what happens when node contains text but selection does not contain any text */
    int selStart = 0;
    int selEnd = 3;
    ESelection esel(0, selStart, 0, selEnd);
    static constexpr OUString sText1(u"   text"_ustr);
    editEng.SetText(sText1);
    CPPUNIT_ASSERT_EQUAL(u"   "_ustr, editEng.GetText(esel));

    CPPUNIT_ASSERT_EQUAL(u"   text"_ustr, lcl_translitTest(editEng, sText1, esel, TF::SENTENCE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"   text"_ustr, lcl_translitTest(editEng, sText1, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"   text"_ustr, lcl_translitTest(editEng, sText1, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"   text"_ustr, lcl_translitTest(editEng, sText1, esel, TF::UPPERCASE_LOWERCASE));

    selStart = 4;
    selEnd = 8;
    esel = ESelection(0, selStart, 0, selEnd);
    static constexpr OUString sText2(u"text    "_ustr);
    editEng.SetText(sText2);
    CPPUNIT_ASSERT_EQUAL(u"    "_ustr, editEng.GetText(esel));

    CPPUNIT_ASSERT_EQUAL(u"text    "_ustr, lcl_translitTest(editEng, sText2, esel, TF::SENTENCE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"text    "_ustr, lcl_translitTest(editEng, sText2, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"text    "_ustr, lcl_translitTest(editEng, sText2, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"text    "_ustr, lcl_translitTest(editEng, sText2, esel, TF::UPPERCASE_LOWERCASE));

    /* Test what happens when node contains only non-word text but selection does not contain any text */
    selStart = 0;
    selEnd = 3;
    esel = ESelection(0, selStart, 0, selEnd);
    static constexpr OUString sText3(u"   -1"_ustr);
    editEng.SetText(sText3);
    CPPUNIT_ASSERT_EQUAL(u"   "_ustr, editEng.GetText(esel));

    CPPUNIT_ASSERT_EQUAL(u"   -1"_ustr, lcl_translitTest(editEng, sText3, esel, TF::SENTENCE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"   -1"_ustr, lcl_translitTest(editEng, sText3, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"   -1"_ustr, lcl_translitTest(editEng, sText3, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"   -1"_ustr, lcl_translitTest(editEng, sText3, esel, TF::UPPERCASE_LOWERCASE));

    selStart = 2;
    selEnd = 6;
    esel = ESelection(0, selStart, 0, selEnd);
    static constexpr OUString sText4(u"-1    "_ustr);
    editEng.SetText(sText4);
    CPPUNIT_ASSERT_EQUAL(u"    "_ustr, editEng.GetText(esel));

    CPPUNIT_ASSERT_EQUAL(u"-1    "_ustr, lcl_translitTest(editEng, sText4, esel, TF::SENTENCE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"-1    "_ustr, lcl_translitTest(editEng, sText4, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"-1    "_ustr, lcl_translitTest(editEng, sText4, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"-1    "_ustr, lcl_translitTest(editEng, sText4, esel, TF::UPPERCASE_LOWERCASE));

    /* Test what happens when node and selection contains only non-word text */
    selStart = 0;
    selEnd = 5;
    esel = ESelection(0, selStart, 0, selEnd);
    static constexpr OUString sText5(u"   -1"_ustr);
    editEng.SetText(sText3);
    CPPUNIT_ASSERT_EQUAL(u"   -1"_ustr, editEng.GetText(esel));

    CPPUNIT_ASSERT_EQUAL(u"   -1"_ustr, lcl_translitTest(editEng, sText5, esel, TF::SENTENCE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"   -1"_ustr, lcl_translitTest(editEng, sText5, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"   -1"_ustr, lcl_translitTest(editEng, sText5, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"   -1"_ustr, lcl_translitTest(editEng, sText5, esel, TF::UPPERCASE_LOWERCASE));

    selStart = 0;
    selEnd = 5;
    esel = ESelection(0, selStart, 0, selEnd);
    static constexpr OUString sText6(u"-1   "_ustr);
    editEng.SetText(sText4);
    CPPUNIT_ASSERT_EQUAL(u"-1   "_ustr, editEng.GetText(esel));

    CPPUNIT_ASSERT_EQUAL(u"-1   "_ustr, lcl_translitTest(editEng, sText6, esel, TF::SENTENCE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"-1   "_ustr, lcl_translitTest(editEng, sText6, esel, TF::TITLE_CASE));
    CPPUNIT_ASSERT_EQUAL(u"-1   "_ustr, lcl_translitTest(editEng, sText6, esel, TF::LOWERCASE_UPPERCASE));
    CPPUNIT_ASSERT_EQUAL(u"-1   "_ustr, lcl_translitTest(editEng, sText6, esel, TF::UPPERCASE_LOWERCASE));


}

void Test::testSingleLine()
{
    EditEngine aEditEngine(mpItemPool.get());

    aEditEngine.SetSingleLine(true);
    aEditEngine.SetText(u"Bolivian\nSanta Cruz de la Sierra"_ustr);
    aEditEngine.QuickFormatDoc(true);
    CPPUNIT_ASSERT_EQUAL(true, aEditEngine.IsFormatted());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), aEditEngine.GetParagraphCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), aEditEngine.GetLineCount(0));
}

void Test::testMoveParagraph()
{
    EditEngine aEditEngine(mpItemPool.get());
    aEditEngine.SetPaperSize(Size(5000, 5000));
    aEditEngine.SetText(u"Paragraph 1\nParagraph 2\nParagraph 3\nParagraph 4\nParagraph 5"_ustr);

    CPPUNIT_ASSERT_EQUAL(true, aEditEngine.IsFormatted());

    CPPUNIT_ASSERT_EQUAL(sal_Int32(5), aEditEngine.GetParagraphCount());
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 1"_ustr, aEditEngine.GetText(0));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 2"_ustr, aEditEngine.GetText(1));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 3"_ustr, aEditEngine.GetText(2));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 4"_ustr, aEditEngine.GetText(3));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 5"_ustr, aEditEngine.GetText(4));

    aEditEngine.MoveParagraphs(Range(1, 1), 4); // Move paragraph 2 (index 1) -> to before index 4

    CPPUNIT_ASSERT_EQUAL(sal_Int32(5), aEditEngine.GetParagraphCount());
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 1"_ustr, aEditEngine.GetText(0));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 3"_ustr, aEditEngine.GetText(1));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 4"_ustr, aEditEngine.GetText(2));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 2"_ustr, aEditEngine.GetText(3));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 5"_ustr, aEditEngine.GetText(4));

    aEditEngine.MoveParagraphs(Range(3, 3), 1); // Move paragraph 2 (index 3) -> to before index 1

    CPPUNIT_ASSERT_EQUAL(sal_Int32(5), aEditEngine.GetParagraphCount());
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 1"_ustr, aEditEngine.GetText(0));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 2"_ustr, aEditEngine.GetText(1));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 3"_ustr, aEditEngine.GetText(2));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 4"_ustr, aEditEngine.GetText(3));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 5"_ustr, aEditEngine.GetText(4));

    aEditEngine.MoveParagraphs(Range(1, 2), 4); // Move paragraph 2, 3 -> to before index 4

    CPPUNIT_ASSERT_EQUAL(sal_Int32(5), aEditEngine.GetParagraphCount());
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 1"_ustr, aEditEngine.GetText(0));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 4"_ustr, aEditEngine.GetText(1));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 2"_ustr, aEditEngine.GetText(2));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 3"_ustr, aEditEngine.GetText(3));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 5"_ustr, aEditEngine.GetText(4));

    aEditEngine.MoveParagraphs(Range(2, 3), 1); // Move paragraph 2, 3 -> to before index 2

    CPPUNIT_ASSERT_EQUAL(sal_Int32(5), aEditEngine.GetParagraphCount());
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 1"_ustr, aEditEngine.GetText(0));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 2"_ustr, aEditEngine.GetText(1));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 3"_ustr, aEditEngine.GetText(2));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 4"_ustr, aEditEngine.GetText(3));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 5"_ustr, aEditEngine.GetText(4));

    aEditEngine.MoveParagraphs(Range(2, 4), 0); // Move paragraph 3, 4, 5 -> to before index 0

    CPPUNIT_ASSERT_EQUAL(sal_Int32(5), aEditEngine.GetParagraphCount());
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 3"_ustr, aEditEngine.GetText(0));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 4"_ustr, aEditEngine.GetText(1));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 5"_ustr, aEditEngine.GetText(2));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 1"_ustr, aEditEngine.GetText(3));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 2"_ustr, aEditEngine.GetText(4));

    aEditEngine.MoveParagraphs(Range(0, 2), 5); // Move paragraph 3, 4, 5 -> to before index 0

    CPPUNIT_ASSERT_EQUAL(sal_Int32(5), aEditEngine.GetParagraphCount());
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 1"_ustr, aEditEngine.GetText(0));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 2"_ustr, aEditEngine.GetText(1));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 3"_ustr, aEditEngine.GetText(2));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 4"_ustr, aEditEngine.GetText(3));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 5"_ustr, aEditEngine.GetText(4));

    aEditEngine.MoveParagraphs(Range(0, 0),
                               8); // Move paragraph 1 -> to before index 8 but 8 is out of bounds

    CPPUNIT_ASSERT_EQUAL(sal_Int32(5), aEditEngine.GetParagraphCount());
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 2"_ustr, aEditEngine.GetText(0));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 3"_ustr, aEditEngine.GetText(1));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 4"_ustr, aEditEngine.GetText(2));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 5"_ustr, aEditEngine.GetText(3));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 1"_ustr, aEditEngine.GetText(4));

    aEditEngine.MoveParagraphs(Range(4, 4), 0); // Move paragraph 1 -> to before index 0

    CPPUNIT_ASSERT_EQUAL(sal_Int32(5), aEditEngine.GetParagraphCount());
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 1"_ustr, aEditEngine.GetText(0));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 2"_ustr, aEditEngine.GetText(1));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 3"_ustr, aEditEngine.GetText(2));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 4"_ustr, aEditEngine.GetText(3));
    CPPUNIT_ASSERT_EQUAL(u"Paragraph 5"_ustr, aEditEngine.GetText(4));
}

void Test::testCreateLines()
{
    ScopedVclPtrInstance<VirtualDevice> pVirtualDevice(DeviceFormat::WITHOUT_ALPHA);

    EditEngine aEditEngine(mpItemPool.get());
    aEditEngine.SetRefDevice(pVirtualDevice.get());
    aEditEngine.SetPaperSize(Size(500, 500));
    aEditEngine.SetText(u"ABC\nABC DEF ABC DEFGH"_ustr);

    CPPUNIT_ASSERT_EQUAL(true, aEditEngine.IsFormatted());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), aEditEngine.GetParagraphCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), aEditEngine.GetLineCount(0));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(5), aEditEngine.GetLineCount(1));

    ParaPortionList& rParagraphPortionList = aEditEngine.GetParaPortions();
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), rParagraphPortionList.Count());

    {
        EditLineList& rLines = rParagraphPortionList.getRef(0).GetLines();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), rLines.Count());
        EditLine const& rLine = rLines[0];

        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rLine.GetStart());
        CPPUNIT_ASSERT_EQUAL(sal_Int32(3), rLine.GetEnd());

        KernArray const& rArray = rLine.GetCharPosArray();
        CPPUNIT_ASSERT_EQUAL(size_t(3), rArray.size());

        CPPUNIT_ASSERT_EQUAL(sal_Int32(173), sal_Int32(rArray[0]));
        CPPUNIT_ASSERT_EQUAL(sal_Int32(333), sal_Int32(rArray[1]));
        CPPUNIT_ASSERT_EQUAL(sal_Int32(493), sal_Int32(rArray[2]));
    }

    {
        EditLineList& rLines = rParagraphPortionList.getRef(1).GetLines();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(5), rLines.Count());

        {
            EditLine const& rLine = rLines[0];
            CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rLine.GetStart());
            CPPUNIT_ASSERT_EQUAL(sal_Int32(4), rLine.GetEnd());
        }

        {
            EditLine const& rLine = rLines[1];
            CPPUNIT_ASSERT_EQUAL(sal_Int32(4), rLine.GetStart());
            CPPUNIT_ASSERT_EQUAL(sal_Int32(8), rLine.GetEnd());
        }

        {
            EditLine const& rLine = rLines[2];
            CPPUNIT_ASSERT_EQUAL(sal_Int32(8), rLine.GetStart());
            CPPUNIT_ASSERT_EQUAL(sal_Int32(12), rLine.GetEnd());
        }

        {
            EditLine const& rLine = rLines[3];
            CPPUNIT_ASSERT_EQUAL(sal_Int32(12), rLine.GetStart());
            CPPUNIT_ASSERT_EQUAL(sal_Int32(15), rLine.GetEnd());
        }

        {
            EditLine const& rLine = rLines[4];
            CPPUNIT_ASSERT_EQUAL(sal_Int32(15), rLine.GetStart());
            CPPUNIT_ASSERT_EQUAL(sal_Int32(17), rLine.GetEnd());
        }
    }

    // CPPUNIT_ASSERT_MESSAGE("INTENTIONALLY FALSE", false);
}

void Test::testTdf154248MultilineFieldWrapping()
{
    // If field wrapping changes, this test may need to be updated

    // Create Outliner instance
    Outliner aOutliner(mpItemPool.get(), OutlinerMode::TextObject);
    aOutliner.SetCalcFieldValueHdl(LINK(nullptr, Test, CalcFieldValueHdl));

    // Create EditEngine's instance
    EditEngine& aEditEngine = const_cast<EditEngine&>(aOutliner.GetEditEngine());
    aEditEngine.SetPaperSize(Size(2000, 2000));
    aEditEngine.SetText(u"ABC  DEF ABC DEFGH"_ustr);
    // Positions Ref     ....*4............

    // Get Field Item for inserting URLs in text
    SvxURLField aURLField(u"http://not.a.real.link"_ustr,
                          u"Really long hyperlink text that won't fit in 1 line, no matter what."_ustr,
                          SvxURLFormat::Repr);
    SvxFieldItem aField(aURLField, EE_FEATURE_FIELD);

    // Insert URL
    EditDoc& rDoc = aEditEngine.GetEditDoc();
    ContentNode* pNode = rDoc.GetObject(0);
    EditSelection aSel(EditPaM(pNode, 4), EditPaM(pNode, 4));
    aEditEngine.InsertField(aSel, aField);

    // Assert Field Count
    CPPUNIT_ASSERT_EQUAL(size_t(1), aEditEngine.GetFieldInfo(0).size());

    aEditEngine.QuickFormatDoc(false);
    CPPUNIT_ASSERT_EQUAL(true, aEditEngine.IsFormatted());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), aEditEngine.GetParagraphCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), aEditEngine.GetLineCount(0));

    ParaPortionList& rParagraphPortionList = aEditEngine.GetParaPortions();
    EditLineList& rLines = rParagraphPortionList.getRef(0).GetLines();
    {
        EditLine const& rLine = rLines[0];
        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rLine.GetStart());
        // Line 1 contains the beginning of the link.
        CPPUNIT_ASSERT_EQUAL(sal_Int32(5), rLine.GetEnd());
    }

    {
        EditLine const& rLine = rLines[1];
        CPPUNIT_ASSERT_EQUAL(sal_Int32(5), rLine.GetStart());
        //Line 2 contains the end of the link and the text that follows it
        CPPUNIT_ASSERT_EQUAL(sal_Int32(14), rLine.GetEnd());
    }

    {
        EditLine const& rLine = rLines[2];
        //line 3 contains the last word that does not fit in line 2
        CPPUNIT_ASSERT_EQUAL(sal_Int32(14), rLine.GetStart());
        CPPUNIT_ASSERT_EQUAL(sal_Int32(19), rLine.GetEnd());
    }
}

// tdf#151748: Verify that editeng produces an empty kashida array if the line does not have room
void Test::testTdf151748StaleKashidaArray()
{
    ScopedVclPtrInstance<VirtualDevice> pVirtualDevice(DeviceFormat::WITHOUT_ALPHA);

    EditEngine aEditEngine(mpItemPool.get());
    aEditEngine.SetRefDevice(pVirtualDevice.get());
    aEditEngine.SetPaperSize(Size(1600, 500));
    aEditEngine.SetDefaultHorizontalTextDirection(EEHorizontalTextDirection::R2L);
    aEditEngine.SetText(u"خط تخوردگی وتوسسسسط"_ustr);

    CPPUNIT_ASSERT_EQUAL(true, aEditEngine.IsFormatted());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), aEditEngine.GetParagraphCount());

    SfxItemSet aSet{ aEditEngine.GetParaAttribs(0) };
    aSet.Put(SvxAdjustItem{ SvxAdjust::Block, EE_PARA_JUST });
    aEditEngine.SetParaAttribs(0, aSet);

    CPPUNIT_ASSERT_EQUAL(SvxAdjust::Block, aEditEngine.GetParaAttrib(0, EE_PARA_JUST).GetAdjust());
    CPPUNIT_ASSERT_EQUAL(true, aEditEngine.IsFormatted());

    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), aEditEngine.GetLineCount(0));

    // Initial state: Check that a kashida array has been created
    {
        ParaPortionList& rParagraphPortionList = aEditEngine.GetParaPortions();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), rParagraphPortionList.Count());

        EditLineList& rLines = rParagraphPortionList.getRef(0).GetLines();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(2), rLines.Count());
        EditLine const& rLine = rLines[0];

        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rLine.GetStart());
        CPPUNIT_ASSERT_EQUAL(sal_Int32(11), rLine.GetEnd());

        std::vector<sal_Bool> const& rArray = rLine.GetKashidaArray();
        CPPUNIT_ASSERT_EQUAL(size_t(19), rArray.size());
    }

    // Resize the paper so there is no longer room for kashida
    aEditEngine.SetPaperSize(Size(1400, 500));

    // Follow-up state: Check that the kashida array has been cleared
    {
        ParaPortionList& rParagraphPortionList = aEditEngine.GetParaPortions();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), rParagraphPortionList.Count());

        EditLineList& rLines = rParagraphPortionList.getRef(0).GetLines();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(2), rLines.Count());
        EditLine const& rLine = rLines[0];

        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rLine.GetStart());
        CPPUNIT_ASSERT_EQUAL(sal_Int32(11), rLine.GetEnd());

        std::vector<sal_Bool> const& rArray = rLine.GetKashidaArray();

        // Since there is no room for kashida, the kashida array should be empty.
        // Without the bug fix, this will be 17:
        CPPUNIT_ASSERT_EQUAL(size_t(0), rArray.size());
    }
}

// tdf#162803: Verify that editeng clears stale kashida data during layout
void Test::testTdf162803StaleKashidaArray()
{
    ScopedVclPtrInstance<VirtualDevice> pVirtualDevice(DeviceFormat::WITHOUT_ALPHA);

    EditEngine aEditEngine(mpItemPool.get());
    aEditEngine.SetRefDevice(pVirtualDevice.get());
    aEditEngine.SetPaperSize(Size(1600, 500));
    aEditEngine.SetDefaultHorizontalTextDirection(EEHorizontalTextDirection::R2L);
    aEditEngine.SetText(u"خط تخوردگی وتوسسسسط"_ustr);

    CPPUNIT_ASSERT_EQUAL(true, aEditEngine.IsFormatted());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), aEditEngine.GetParagraphCount());

    SfxItemSet aSet{ aEditEngine.GetParaAttribs(0) };
    aSet.Put(SvxAdjustItem{ SvxAdjust::Block, EE_PARA_JUST });
    aEditEngine.SetParaAttribs(0, aSet);

    CPPUNIT_ASSERT_EQUAL(SvxAdjust::Block, aEditEngine.GetParaAttrib(0, EE_PARA_JUST).GetAdjust());
    CPPUNIT_ASSERT_EQUAL(true, aEditEngine.IsFormatted());

    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), aEditEngine.GetLineCount(0));

    // Initial state: Check that a kashida array has been created
    {
        ParaPortionList& rParagraphPortionList = aEditEngine.GetParaPortions();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), rParagraphPortionList.Count());

        EditLineList& rLines = rParagraphPortionList.getRef(0).GetLines();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(2), rLines.Count());
        EditLine const& rLine = rLines[0];

        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rLine.GetStart());
        CPPUNIT_ASSERT_EQUAL(sal_Int32(11), rLine.GetEnd());

        std::vector<sal_Bool> const& rArray = rLine.GetKashidaArray();
        CPPUNIT_ASSERT_EQUAL(size_t(19), rArray.size());
    }

    // Resize the paper so the entire text fits on a single line
    aEditEngine.SetPaperSize(Size(4000, 500));

    // Follow-up state: Check that the kashida array has been cleared
    {
        ParaPortionList& rParagraphPortionList = aEditEngine.GetParaPortions();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), rParagraphPortionList.Count());

        EditLineList& rLines = rParagraphPortionList.getRef(0).GetLines();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), rLines.Count());
        EditLine const& rLine = rLines[0];

        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), rLine.GetStart());
        CPPUNIT_ASSERT_EQUAL(sal_Int32(19), rLine.GetEnd());

        std::vector<sal_Bool> const& rArray = rLine.GetKashidaArray();

        // Since there is no room for kashida, the kashida array should be empty.
        // Without the bug fix, this will be 17:
        CPPUNIT_ASSERT_EQUAL(size_t(0), rArray.size());
    }
}

CPPUNIT_TEST_SUITE_REGISTRATION(Test);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
