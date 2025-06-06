/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include <memory>

#include <reffact.hxx>
#include <TableFillingAndNavigationTools.hxx>
#include <ZTestDialog.hxx>
#include <scresid.hxx>
#include <strings.hrc>

ScZTestDialog::ScZTestDialog(
                    SfxBindings* pSfxBindings, SfxChildWindow* pChildWindow,
                    weld::Window* pParent, ScViewData& rViewData ) :
    ScStatisticsTwoVariableDialog(
            pSfxBindings, pChildWindow, pParent, rViewData,
            u"modules/scalc/ui/ztestdialog.ui"_ustr, u"ZTestDialog"_ustr)
{
    m_xDialog->set_title(ScResId(STR_ZTEST));
}

ScZTestDialog::~ScZTestDialog()
{}

void ScZTestDialog::Close()
{
    DoClose( ScZTestDialogWrapper::GetChildWindowId() );
}

TranslateId ScZTestDialog::GetUndoNameId()
{
    return STR_ZTEST_UNDO_NAME;
}

ScRange ScZTestDialog::ApplyOutput(ScDocShell& rDocShell)
{
    AddressWalkerWriter aOutput(mOutputAddress, rDocShell, mDocument,
            formula::FormulaGrammar::mergeToGrammar( formula::FormulaGrammar::GRAM_ENGLISH, mAddressDetails.eConv));
    FormulaTemplate aTemplate(&mDocument);

    std::unique_ptr<DataRangeIterator> pVariable1Iterator;
    if (mGroupedBy == BY_COLUMN)
        pVariable1Iterator.reset(new DataRangeByColumnIterator(mVariable1Range));
    else
        pVariable1Iterator.reset(new DataRangeByRowIterator(mVariable1Range));

    std::unique_ptr<DataRangeIterator> pVariable2Iterator;
    if (mGroupedBy == BY_COLUMN)
        pVariable2Iterator.reset(new DataRangeByColumnIterator(mVariable2Range));
    else
        pVariable2Iterator.reset(new DataRangeByRowIterator(mVariable2Range));

    aTemplate.autoReplaceRange(u"%VARIABLE1_RANGE%"_ustr, pVariable1Iterator->get());
    aTemplate.autoReplaceRange(u"%VARIABLE2_RANGE%"_ustr, pVariable2Iterator->get());

    aOutput.writeBoldString(ScResId(STR_ZTEST));
    aOutput.newLine();

    // Alpha
    aOutput.writeString(ScResId(STR_LABEL_ALPHA));
    aOutput.nextColumn();
    aOutput.writeValue(0.05);
    aTemplate.autoReplaceAddress(u"%ALPHA%"_ustr, aOutput.current());
    aOutput.newLine();

    // Hypothesized mean difference
    aOutput.writeString(ScResId(STR_HYPOTHESIZED_MEAN_DIFFERENCE_LABEL));
    aOutput.nextColumn();
    aOutput.writeValue(0);
    aTemplate.autoReplaceAddress(u"%HYPOTHESIZED_MEAN_DIFFERENCE%"_ustr, aOutput.current());
    aOutput.newLine();

    // Variable Label
    aOutput.nextColumn();
    aOutput.writeBoldString(ScResId(STR_VARIABLE_1_LABEL));
    aOutput.nextColumn();
    aOutput.writeBoldString(ScResId(STR_VARIABLE_2_LABEL));
    aOutput.newLine();

    // Known Variance
    aOutput.writeString(ScResId(STR_ZTEST_KNOWN_VARIANCE));
    aOutput.nextColumn();
    aOutput.writeValue(0);
    aTemplate.autoReplaceAddress(u"%KNOWN_VARIANCE_VARIABLE1%"_ustr, aOutput.current());
    aOutput.nextColumn();
    aOutput.writeValue(0);
    aTemplate.autoReplaceAddress(u"%KNOWN_VARIANCE_VARIABLE2%"_ustr, aOutput.current());
    aOutput.newLine();

    // Mean
    aOutput.writeString(ScResId(STRID_CALC_MEAN));
    aOutput.nextColumn();
    aTemplate.setTemplate("=AVERAGE(%VARIABLE1_RANGE%)");
    aTemplate.autoReplaceAddress(u"%MEAN_VARIABLE1%"_ustr, aOutput.current());
    aOutput.writeFormula(aTemplate.getTemplate());
    aOutput.nextColumn();
    aTemplate.setTemplate("=AVERAGE(%VARIABLE2_RANGE%)");
    aTemplate.autoReplaceAddress(u"%MEAN_VARIABLE2%"_ustr, aOutput.current());
    aOutput.writeFormula(aTemplate.getTemplate());
    aOutput.newLine();

    // Observations
    aOutput.writeString(ScResId(STR_OBSERVATIONS_LABEL));
    aOutput.nextColumn();
    aTemplate.setTemplate("=COUNT(%VARIABLE1_RANGE%)");
    aOutput.writeFormula(aTemplate.getTemplate());
    aTemplate.autoReplaceAddress(u"%OBSERVATION_VARIABLE1%"_ustr, aOutput.current());
    aOutput.nextColumn();
    aTemplate.setTemplate("=COUNT(%VARIABLE2_RANGE%)");
    aOutput.writeFormula(aTemplate.getTemplate());
    aTemplate.autoReplaceAddress(u"%OBSERVATION_VARIABLE2%"_ustr, aOutput.current());
    aOutput.newLine();

    // Observed mean difference
    aOutput.writeString(ScResId(STR_OBSERVED_MEAN_DIFFERENCE_LABEL));
    aOutput.nextColumn();
    aTemplate.setTemplate("=%MEAN_VARIABLE1% - %MEAN_VARIABLE2%");
    aOutput.writeMatrixFormula(aTemplate.getTemplate());
    aTemplate.autoReplaceAddress(u"%OBSERVED_MEAN_DIFFERENCE%"_ustr, aOutput.current());
    aOutput.newLine();

    // z
    aOutput.writeString(ScResId(STR_ZTEST_Z_VALUE));
    aOutput.nextColumn();
    aTemplate.setTemplate("=(%OBSERVED_MEAN_DIFFERENCE% - %HYPOTHESIZED_MEAN_DIFFERENCE%) / SQRT( %KNOWN_VARIANCE_VARIABLE1% / %OBSERVATION_VARIABLE1% + %KNOWN_VARIANCE_VARIABLE2% / %OBSERVATION_VARIABLE2% )");
    aOutput.writeFormula(aTemplate.getTemplate());
    aTemplate.autoReplaceAddress(u"%Z_STAT%"_ustr, aOutput.current());
    aOutput.newLine();

    // P one-tail
    aOutput.writeString(ScResId(STR_ZTEST_P_ONE_TAIL));
    aOutput.nextColumn();
    aTemplate.setTemplate("=1 - NORMSDIST(ABS(%Z_STAT%))");
    aOutput.writeFormula(aTemplate.getTemplate());
    aOutput.newLine();

    // z critical one-tail
    aOutput.writeString(ScResId(STR_ZTEST_Z_CRITICAL_ONE_TAIL));
    aOutput.nextColumn();
    aTemplate.setTemplate("=-NORMSINV(%ALPHA%)");
    aOutput.writeFormula(aTemplate.getTemplate());
    aOutput.newLine();

    // P two-tail
    aOutput.writeString(ScResId(STR_ZTEST_P_TWO_TAIL));
    aOutput.nextColumn();
    aTemplate.setTemplate("=2 * NORMSDIST(-ABS(%Z_STAT%))");
    aOutput.writeFormula(aTemplate.getTemplate());
    aOutput.newLine();

    // z critical two-tail
    aOutput.writeString(ScResId(STR_ZTEST_Z_CRITICAL_TWO_TAIL));
    aOutput.nextColumn();
    aTemplate.setTemplate("=-NORMSINV(%ALPHA%/2)");
    aOutput.writeFormula(aTemplate.getTemplate());

    return ScRange(aOutput.mMinimumAddress, aOutput.mMaximumAddress);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
