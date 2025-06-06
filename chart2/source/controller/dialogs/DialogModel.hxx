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

#include <TimerTriggeredControllerLock.hxx>
#include <rtl/ustring.hxx>
#include <rtl/ref.hxx>
#include <ChartTypeTemplate.hxx>

#include <map>
#include <memory>
#include <vector>

namespace chart { class ChartModel; }
namespace com::sun::star::beans { struct PropertyValue; }
namespace com::sun::star::uno { template <class E> class Sequence; }

namespace com::sun::star::chart2 {
    class XDataSeries;
    namespace data {
        class XDataProvider;
        class XLabeledDataSequence;
    }
}

namespace chart
{
class ChartType;
class DataSeries;
struct InterpretedData;
class RangeSelectionHelper;

struct DialogModelTimeBasedInfo
{
    DialogModelTimeBasedInfo();

    bool bTimeBased;
    sal_Int32 nStart;
    sal_Int32 nEnd;
};

class DialogModel
{
public:
    explicit DialogModel( rtl::Reference<::chart::ChartModel> xChartDocument );
    ~DialogModel();

    typedef std::pair<
                OUString,
                std::pair< rtl::Reference< ::chart::DataSeries >,
                             rtl::Reference< ::chart::ChartType > > >
        tSeriesWithChartTypeByName;

    typedef std::map< OUString, OUString >
        tRolesWithRanges;

    void setTemplate(
        const rtl::Reference< ::chart::ChartTypeTemplate > & xTemplate );

    std::shared_ptr< RangeSelectionHelper > const &
        getRangeSelectionHelper() const;

    const rtl::Reference<::chart::ChartModel> &
        getChartModel() const;

    css::uno::Reference< css::chart2::data::XDataProvider >
        getDataProvider() const;

    std::vector< rtl::Reference< ::chart::ChartType > >
        getAllDataSeriesContainers() const;

    std::vector< tSeriesWithChartTypeByName >
        getAllDataSeriesWithLabel() const;

    static tRolesWithRanges getRolesWithRanges(
        const css::uno::Reference< css::chart2::XDataSeries > & xSeries,
        const OUString & aRoleOfSequenceForLabel,
        const rtl::Reference< ::chart::ChartType > & xChartType );

    enum class MoveDirection
    {
        Down, Up
    };

    void moveSeries( const rtl::Reference< DataSeries > & xSeries,
                     MoveDirection eDirection );

    /// @return the newly inserted series
    rtl::Reference<
            ::chart::DataSeries > insertSeriesAfter(
                const css::uno::Reference< css::chart2::XDataSeries > & xSeries,
                const rtl::Reference< ::chart::ChartType > & xChartType,
                bool bCreateDataCachedSequences = false );

    void deleteSeries(
        const rtl::Reference< ::chart::DataSeries > & xSeries,
        const rtl::Reference< ::chart::ChartType > & xChartType );

    css::uno::Reference< css::chart2::data::XLabeledDataSequence >
        getCategories() const;

    void setCategories( const css::uno::Reference< css::chart2::data::XLabeledDataSequence > & xCategories );

    OUString getCategoriesRange() const;

    bool isCategoryDiagram() const;

    void detectArguments(
        OUString & rOutRangeString,
        bool & rOutUseColumns, bool & rOutFirstCellAsLabel, bool & rOutHasCategories ) const;

    bool allArgumentsForRectRangeDetected() const;

    void setData( const css::uno::Sequence< css::beans::PropertyValue > & rArguments );

    void setTimeBasedRange( bool bTimeBased, sal_Int32 nStart, sal_Int32 nEnd) const;

    const DialogModelTimeBasedInfo& getTimeBasedInfo() const { return maTimeBasedInfo; }

    void startControllerLockTimer();

    static OUString ConvertRoleFromInternalToUI( const OUString & rRoleString );
    static OUString GetRoleDataLabel();

    // pass a role string (not translated) and get an index that serves for
    // relative ordering, to get e.g. x-values and y-values in the right order
    static sal_Int32 GetRoleIndexForSorting( const OUString & rInternalRoleString );

    ChartModel& getModel() const;

private:
    rtl::Reference<::chart::ChartModel>
        m_xChartDocument;

    rtl::Reference< ::chart::ChartTypeTemplate > m_xTemplate;

    mutable std::shared_ptr< RangeSelectionHelper >
        m_spRangeSelectionHelper;

    TimerTriggeredControllerLock   m_aTimerTriggeredControllerLock;

private:
    void applyInterpretedData(
        const InterpretedData & rNewData,
        const std::vector< rtl::Reference< ::chart::DataSeries > > & rSeriesToReUse );

    sal_Int32 countSeries() const;

    mutable DialogModelTimeBasedInfo maTimeBasedInfo;
};

} //  namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
