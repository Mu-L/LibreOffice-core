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

#include "transporttypes.hxx"
#include "SchXMLChartContext.hxx"
#include <xmloff/xmlictxt.hxx>
#include <xmloff/SchXMLImportHelper.hxx>
#include <xmloff/xmlstyle.hxx>

#include <vector>

namespace com::sun::star {
    namespace chart2 {
        class XChartDocument;
        class XDataSeries;
    }
}

// class for child contexts: series, data point and statistics objects
class SchXMLSeries2Context : public SvXMLImportContext
{
private:
    SchXMLImportHelper& mrImportHelper;
    css::uno::Reference< css::chart2::XChartDocument > mxNewDoc;
    ::std::vector< SchXMLAxis >& mrAxes;
    ::std::vector< DataRowPointStyle >& mrStyleVector;
    ::std::vector< RegressionStyle >& mrRegressionStyleVector;
    css::uno::Reference< css::chart2::XDataSeries > m_xSeries;
    sal_Int32 mnSeriesIndex;
    sal_Int32 mnDataPointIndex;
    bool m_bStockHasVolume;

    GlobalSeriesImportInfo& m_rGlobalSeriesImportInfo;

    SchXMLAxis* mpAttachedAxis;
    sal_Int32 mnAttachedAxis;
    OUString msAutoStyleName;
    ::std::vector< OUString > maDomainAddresses;
    OUString maGlobalChartTypeName;
    OUString maSeriesChartTypeName;
    OUString m_aSeriesRange;
    bool            m_bHasDomainContext;
    tSchXMLLSequencesPerIndex & mrLSequencesPerIndex;
    tSchXMLLSequencesPerIndex maPostponedSequences;
    bool& mrGlobalChartTypeUsedBySeries;
    bool mbSymbolSizeIsMissingInFile;
    css::awt::Size maChartSize;
    // We let the series manage the DataRowPointStyle-struct of its data label
    DataRowPointStyle mDataLabel;

public:
    SchXMLSeries2Context( SchXMLImportHelper& rImpHelper,
                          SvXMLImport& rImport,
                          const css::uno::Reference< css::chart2::XChartDocument > & xNewDoc,
                          std::vector< SchXMLAxis >& rAxes,
                          ::std::vector< DataRowPointStyle >& rStyleVector,
                          ::std::vector< RegressionStyle >& rRegressionStyleVector,
                          sal_Int32 nSeriesIndex,
                          bool bStockHasVolume,
                          GlobalSeriesImportInfo& rGlobalSeriesImportInfo,
                          const OUString & aGlobalChartTypeName,
                          tSchXMLLSequencesPerIndex & rLSequencesPerIndex,
                          bool& rGlobalChartTypeUsedBySeries,
                          const css::awt::Size & rChartSize );
    virtual ~SchXMLSeries2Context() override;

    virtual css::uno::Reference< css::xml::sax::XFastContextHandler > SAL_CALL createFastChildContext(
        sal_Int32 nElement,
        const css::uno::Reference< css::xml::sax::XFastAttributeList >& AttrList ) override;
    virtual void SAL_CALL startFastElement(
        sal_Int32 nElement,
        const css::uno::Reference< css::xml::sax::XFastAttributeList >& xAttrList ) override;
    virtual void SAL_CALL endFastElement(sal_Int32 nElement) override;

    static void initSeriesPropertySets( SeriesDefaultsAndStyles& rSeriesDefaultsAndStyles
        , const css::uno::Reference< css::frame::XModel >& xChartModel );

    static void setDefaultsToSeries( SeriesDefaultsAndStyles& rSeriesDefaultsAndStyles );

    static void setStylesToSeries( SeriesDefaultsAndStyles& rSeriesDefaultsAndStyles
        , const SvXMLStylesContext* pStylesCtxt
        , const SvXMLStyleContext*& rpStyle
        , OUString& rCurrStyleName
        , const SchXMLImportHelper& rImportHelper
        , const SvXMLImport& rImport
        , bool bIsStockChart
        , tSchXMLLSequencesPerIndex & rInOutLSequencesPerIndex );

    static void setStylesToStatisticsObjects( SeriesDefaultsAndStyles& rSeriesDefaultsAndStyles
        , const SvXMLStylesContext* pStylesCtxt
        , const SvXMLStyleContext*& rpStyle
        , OUString &rCurrStyleName );

    static void setStylesToRegressionCurves(
                    SeriesDefaultsAndStyles& rSeriesDefaultsAndStyles,
                    const SvXMLStylesContext* pStylesCtxt,
                    const SvXMLStyleContext*& rpStyle,
                    OUString const &rCurrStyleName );

    static void setStylesToDataPoints( SeriesDefaultsAndStyles& rSeriesDefaultsAndStyles
        , const SvXMLStylesContext* pStylesCtxt
        , const SvXMLStyleContext*& rpStyle
        , OUString& rCurrStyleName
        , const SchXMLImportHelper& rImportHelper
        , const SvXMLImport& rImport
        , bool bIsStockChart, bool bIsDonutChart, bool bSwitchOffLinesForScatter );

    static void switchSeriesLinesOff( ::std::vector< DataRowPointStyle >& rSeriesStyleVector );
};

// INCLUDED_XMLOFF_SOURCE_CHART_SCHXMLSERIES2CONTEXT_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
