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

#include <cstddef>
#include <iterator>

#include <InternalDataProvider.hxx>
#include <LabeledDataSequence.hxx>
#include <DataSource.hxx>
#include <XMLRangeHelper.hxx>
#include <CommonFunctors.hxx>
#include <UncachedDataSequence.hxx>
#include <DataSourceHelper.hxx>
#include <ChartModel.hxx>
#include <Diagram.hxx>
#include <ExplicitCategoriesProvider.hxx>
#include <BaseCoordinateSystem.hxx>
#include <DataBrowserModel.hxx>
#include <DataSeries.hxx>

#include <com/sun/star/chart2/data/XDataSequence.hpp>
#include <com/sun/star/chart/ChartDataRowSource.hpp>
#include <cppuhelper/supportsservice.hxx>
#include <comphelper/sequenceashashmap.hxx>
#include <comphelper/property.hxx>
#include <o3tl/string_view.hxx>
#include <comphelper/diagnose_ex.hxx>

#include <limits>
#include <vector>
#include <algorithm>

using namespace ::com::sun::star;

using ::com::sun::star::uno::Reference;
using ::com::sun::star::uno::Sequence;

namespace chart
{

namespace
{

constexpr OUString lcl_aCategoriesRangeName = u"categories"_ustr;
const char lcl_aCategoriesLevelRangeNamePrefix[] = "categoriesL "; //L <-> level
const char lcl_aCategoriesPointRangeNamePrefix[] = "categoriesP "; //P <-> point
constexpr OUString lcl_aCategoriesRoleName = u"categories"_ustr;
const char lcl_aLabelRangePrefix[] = "label ";
constexpr OUString lcl_aCompleteRange = u"all"_ustr;

typedef std::multimap< OUString, uno::WeakReference< chart2::data::XDataSequence > >
    lcl_tSequenceMap;

std::vector< uno::Any > lcl_StringToAnyVector( const css::uno::Sequence< OUString >& aStringSeq )
{
    std::vector< uno::Any > aResult;
    aResult.reserve(aStringSeq.getLength());
    std::transform(aStringSeq.begin(), aStringSeq.end(), std::back_inserter(aResult), CommonFunctors::makeAny());
    return aResult;
}

struct lcl_setModified
{
    void operator() ( const lcl_tSequenceMap::value_type & rMapEntry )
    {
        // convert weak reference to reference
        Reference< chart2::data::XDataSequence > xSeq( rMapEntry.second );
        if( xSeq.is())
        {
            Reference< util::XModifiable > xMod( xSeq, uno::UNO_QUERY );
            if( xMod.is())
                xMod->setModified( true );
        }
    }
};

struct lcl_internalizeSeries
{
    lcl_internalizeSeries( InternalData & rInternalData,
                           InternalDataProvider & rProvider,
                           bool bConnectToModel, bool bDataInColumns ) :
            m_rInternalData( rInternalData ),
            m_rProvider( rProvider ),
            m_bConnectToModel( bConnectToModel ),
            m_bDataInColumns( bDataInColumns )
    {}
    void operator() ( const rtl::Reference< DataSeries > & xSeries )
    {
        const std::vector< uno::Reference< chart2::data::XLabeledDataSequence > > & aOldSeriesData = xSeries->getDataSequences2();
        std::vector< uno::Reference< chart2::data::XLabeledDataSequence > > aNewSeriesData( aOldSeriesData.size() );
        for( std::size_t i=0; i<aOldSeriesData.size(); ++i )
        {
            sal_Int32 nNewIndex( m_bDataInColumns ? m_rInternalData.appendColumn() : m_rInternalData.appendRow() );
            OUString aIdentifier( OUString::number( nNewIndex ));
            //@todo: deal also with genericXDataSequence
            Reference< chart2::data::XNumericalDataSequence > xValues( aOldSeriesData[i]->getValues(), uno::UNO_QUERY );
            Reference< chart2::data::XTextualDataSequence > xLabel( aOldSeriesData[i]->getLabel(), uno::UNO_QUERY );
            Reference< chart2::data::XDataSequence > xNewValues;

            if( xValues.is() )
            {
                auto aValues( comphelper::sequenceToContainer<std::vector< double >>( xValues->getNumericalData()));
                if( m_bDataInColumns )
                    m_rInternalData.setColumnValues( nNewIndex, aValues );
                else
                    m_rInternalData.setRowValues( nNewIndex, aValues );
                if( m_bConnectToModel )
                {
                    xNewValues.set( m_rProvider.createDataSequenceByRangeRepresentation( aIdentifier ));
                    comphelper::copyProperties(
                        Reference< beans::XPropertySet >( xValues, uno::UNO_QUERY ),
                        Reference< beans::XPropertySet >( xNewValues, uno::UNO_QUERY ));
                }
            }

            if( xLabel.is() )
            {
                if( m_bDataInColumns )
                    m_rInternalData.setComplexColumnLabel( nNewIndex, lcl_StringToAnyVector( xLabel->getTextualData() ) );
                else
                    m_rInternalData.setComplexRowLabel( nNewIndex, lcl_StringToAnyVector( xLabel->getTextualData() ) );
                if( m_bConnectToModel )
                {
                    Reference< chart2::data::XDataSequence > xNewLabel(
                        m_rProvider.createDataSequenceByRangeRepresentation( lcl_aLabelRangePrefix + aIdentifier ));
                    comphelper::copyProperties(
                        Reference< beans::XPropertySet >( xLabel, uno::UNO_QUERY ),
                        Reference< beans::XPropertySet >( xNewLabel, uno::UNO_QUERY ));
                    aNewSeriesData[i].set( new LabeledDataSequence( xNewValues, xNewLabel ) );
                }
            }
            else
            {
                if( m_bConnectToModel )
                    aNewSeriesData[i].set( new LabeledDataSequence( xNewValues ) );
            }
        }
        if( m_bConnectToModel )
            xSeries->setData( aNewSeriesData );
     }

private:
    InternalData &          m_rInternalData;
    InternalDataProvider &  m_rProvider;
    bool                    m_bConnectToModel;
    bool                    m_bDataInColumns;
};

struct lcl_copyFromLevel
{
public:

    explicit lcl_copyFromLevel( sal_Int32 nLevel ) : m_nLevel( nLevel )
    {}

    uno::Any operator() ( const std::vector< uno::Any >& rVector )
    {
        uno::Any aRet;
        if( m_nLevel <  static_cast< sal_Int32 >(rVector.size()) )
            aRet = rVector[m_nLevel];
        return aRet;
    }

private:
    sal_Int32 m_nLevel;
};

struct lcl_getStringFromLevelVector
{
public:

    explicit lcl_getStringFromLevelVector( sal_Int32 nLevel ) : m_nLevel( nLevel )
    {}

    OUString operator() ( const std::vector< uno::Any >& rVector )
    {
        OUString aString;
        if( m_nLevel < static_cast< sal_Int32 >(rVector.size()) )
            aString = CommonFunctors::ToString()(rVector[m_nLevel]);
        return aString;
    }

private:
    sal_Int32 m_nLevel;
};

struct lcl_setAnyAtLevel
{
public:

    explicit lcl_setAnyAtLevel( sal_Int32 nLevel ) : m_nLevel( nLevel )
    {}

    std::vector< uno::Any > operator() ( const std::vector< uno::Any >& rVector, const uno::Any& rNewValue )
    {
        std::vector< uno::Any > aRet( rVector );
        if( m_nLevel >= static_cast< sal_Int32 >(aRet.size()) )
            aRet.resize( m_nLevel+1 );
        aRet[ m_nLevel ]=rNewValue;
        return aRet;
    }

private:
    sal_Int32 m_nLevel;
};

struct lcl_setAnyAtLevelFromStringSequence
{
public:

    explicit lcl_setAnyAtLevelFromStringSequence( sal_Int32 nLevel ) : m_nLevel( nLevel )
    {}

    std::vector< uno::Any > operator() ( const std::vector< uno::Any >& rVector, const OUString& rNewValue )
    {
        std::vector< uno::Any > aRet( rVector );
        if( m_nLevel >= static_cast< sal_Int32 >(aRet.size()) )
            aRet.resize( m_nLevel+1 );
        aRet[ m_nLevel ] <<= rNewValue;
        return aRet;
    }

private:
    sal_Int32 m_nLevel;
};

struct lcl_insertAnyAtLevel
{
public:

    explicit lcl_insertAnyAtLevel( sal_Int32 nLevel ) : m_nLevel( nLevel )
    {}

    void operator() ( std::vector< uno::Any >& rVector )
    {
        if( m_nLevel >= static_cast< sal_Int32 >(rVector.size()) )
        {
            rVector.resize( m_nLevel + 1 );
        }
        else
        {
            rVector.insert( rVector.begin() + m_nLevel, uno::Any() );
        }
    }

private:
    sal_Int32 m_nLevel;
};

struct lcl_removeAnyAtLevel
{
public:

    explicit lcl_removeAnyAtLevel( sal_Int32 nLevel ) : m_nLevel( nLevel )
    {}

    void operator() ( std::vector< uno::Any >& rVector )
    {
        if( m_nLevel < static_cast<sal_Int32>(rVector.size()) )
        {
            rVector.erase(rVector.begin() + m_nLevel);
        }
    }

private:
    sal_Int32 m_nLevel;
};

} // anonymous namespace

InternalDataProvider::InternalDataProvider()
    : m_bDataInColumns( true )
{}

InternalDataProvider::InternalDataProvider(
    const rtl::Reference< ChartModel > & xModel,
    bool bConnectToModel,
    bool bDefaultDataInColumns)
:   m_bDataInColumns( bDefaultDataInColumns )
{
    if (!xModel.is())
        return;
    m_xChartModel = xModel.get();
    try
    {
        rtl::Reference< Diagram > xDiagram( xModel->getFirstChartDiagram() );
        if( xDiagram.is())
        {
            //data in columns?
            {
                OUString aRangeString;
                bool bFirstCellAsLabel = true;
                bool bHasCategories = true;
                uno::Sequence< sal_Int32 > aSequenceMapping;
                const bool bSomethingDetected(
                    DataSourceHelper::detectRangeSegmentation(
                        xModel, aRangeString, aSequenceMapping, m_bDataInColumns, bFirstCellAsLabel, bHasCategories ));

                // #i120559# if no data was available, restore default
                if(!bSomethingDetected && m_bDataInColumns != bDefaultDataInColumns)
                {
                    m_bDataInColumns = bDefaultDataInColumns;
                }
            }

            // categories
            {
                std::vector< std::vector< uno::Any > > aNewCategories;//inner count is level
                {
                    ExplicitCategoriesProvider aExplicitCategoriesProvider(xModel->getFirstCoordinateSystem(), *xModel);

                    const std::vector< Reference< chart2::data::XLabeledDataSequence> >& rSplitCategoriesList( aExplicitCategoriesProvider.getSplitCategoriesList() );
                    sal_Int32 nLevelCount = rSplitCategoriesList.size();
                    for( sal_Int32 nL = 0; nL<nLevelCount; nL++ )
                    {
                        const Reference< chart2::data::XLabeledDataSequence >& xLDS( rSplitCategoriesList[nL] );
                        if( !xLDS.is() )
                            continue;
                        Sequence< uno::Any > aDataSeq;
                        Reference< chart2::data::XDataSequence > xSeq( xLDS->getValues() );
                        if( xSeq.is() )
                            aDataSeq = xSeq->getData();
                        sal_Int32 nLength = aDataSeq.getLength();
                        sal_Int32 nCatLength = static_cast< sal_Int32 >(aNewCategories.size());
                        if( nCatLength < nLength )
                            aNewCategories.resize( nLength );
                        else if( nLength < nCatLength )
                            aDataSeq.realloc( nCatLength );
                        transform( aNewCategories.begin(), aNewCategories.end(), aDataSeq.begin(),
                            aNewCategories.begin(), lcl_setAnyAtLevel(nL) );
                    }
                    if( !nLevelCount )
                    {
                        Sequence< OUString > aSimplecategories = aExplicitCategoriesProvider.getSimpleCategories();
                        sal_Int32 nLength = aSimplecategories.getLength();
                        aNewCategories.reserve( nLength );
                        for( sal_Int32 nN=0; nN<nLength; nN++)
                        {
                            aNewCategories.push_back( { uno::Any(aSimplecategories[nN]) } );
                        }
                    }
                }

                if( m_bDataInColumns )
                    m_aInternalData.setComplexRowLabels( std::move(aNewCategories) );
                else
                    m_aInternalData.setComplexColumnLabels( std::move(aNewCategories) );
                if( bConnectToModel )
                    xDiagram->setCategories(
                        new LabeledDataSequence(
                            createDataSequenceByRangeRepresentation( lcl_aCategoriesRangeName ))
                         );
            }

            // data series
            std::vector< rtl::Reference< DataSeries > > aSeriesVector( xModel->getDataSeries());
            lcl_internalizeSeries ftor( m_aInternalData, *this, bConnectToModel, m_bDataInColumns );
            for( const auto& rxScreen : aSeriesVector )
                ftor( rxScreen );
        }
    }
    catch( const uno::Exception & )
    {
        DBG_UNHANDLED_EXCEPTION("chart2");
    }
}

// copy-CTOR
InternalDataProvider::InternalDataProvider( const InternalDataProvider & rOther ) :
        impl::InternalDataProvider_Base(rOther),
        m_aSequenceMap( rOther.m_aSequenceMap ),
        m_aInternalData( rOther.m_aInternalData ),
        m_bDataInColumns( rOther.m_bDataInColumns )
{}

InternalDataProvider::~InternalDataProvider()
{}

void InternalDataProvider::setChartModel(ChartModel* pChartModel)
{
    m_xChartModel = pChartModel;
}

void InternalDataProvider::addDataSequenceToMap(
    const OUString & rRangeRepresentation,
    const Reference< chart2::data::XDataSequence > & xSequence )
{
    m_aSequenceMap.emplace(
            rRangeRepresentation,
            uno::WeakReference< chart2::data::XDataSequence >( xSequence ));
}

void InternalDataProvider::deleteMapReferences( const OUString & rRangeRepresentation )
{
    // set sequence to deleted by setting its range to an empty string
    tSequenceMapRange aRange( m_aSequenceMap.equal_range( rRangeRepresentation ));
    for( tSequenceMap::iterator aIt( aRange.first ); aIt != aRange.second; ++aIt )
    {
        Reference< chart2::data::XDataSequence > xSeq( aIt->second );
        if( xSeq.is())
        {
            Reference< container::XNamed > xNamed( xSeq, uno::UNO_QUERY );
            if( xNamed.is())
                xNamed->setName( OUString());
        }
    }
    // remove from map
    m_aSequenceMap.erase( aRange.first, aRange.second );
}

void InternalDataProvider::adaptMapReferences(
    const OUString & rOldRangeRepresentation,
    const OUString & rNewRangeRepresentation )
{
    tSequenceMapRange aRange( m_aSequenceMap.equal_range( rOldRangeRepresentation ));
    tSequenceMap aNewElements;
    for( tSequenceMap::iterator aIt( aRange.first ); aIt != aRange.second; ++aIt )
    {
        Reference< chart2::data::XDataSequence > xSeq( aIt->second );
        if( xSeq.is())
        {
            Reference< container::XNamed > xNamed( xSeq, uno::UNO_QUERY );
            if( xNamed.is())
                xNamed->setName( rNewRangeRepresentation );
        }
        aNewElements.emplace( rNewRangeRepresentation, aIt->second );
    }
    // erase map values for old index
    m_aSequenceMap.erase( aRange.first, aRange.second );
    // add new entries for values with new index
    m_aSequenceMap.insert( aNewElements.begin(), aNewElements.end() );
}

void InternalDataProvider::increaseMapReferences(
    sal_Int32 nBegin, sal_Int32 nEnd )
{
    for( sal_Int32 nIndex = nEnd - 1; nIndex >= nBegin; --nIndex )
    {
        adaptMapReferences( OUString::number( nIndex ),
                            OUString::number( nIndex + 1 ));
        adaptMapReferences( lcl_aLabelRangePrefix + OUString::number( nIndex ),
                            lcl_aLabelRangePrefix + OUString::number( nIndex + 1 ));
    }
}

void InternalDataProvider::decreaseMapReferences(
    sal_Int32 nBegin, sal_Int32 nEnd )
{
    for( sal_Int32 nIndex = nBegin; nIndex < nEnd; ++nIndex )
    {
        adaptMapReferences( OUString::number( nIndex ),
                            OUString::number( nIndex - 1 ));
        adaptMapReferences( lcl_aLabelRangePrefix + OUString::number( nIndex ),
                            lcl_aLabelRangePrefix + OUString::number( nIndex - 1 ));
    }
}

rtl::Reference< UncachedDataSequence > InternalDataProvider::createDataSequenceAndAddToMap(
    const OUString & rRangeRepresentation )
{
    rtl::Reference<UncachedDataSequence> xSeq = createDataSequenceFromArray(rRangeRepresentation, u"", u"");
    if (xSeq.is())
        return nullptr;

    xSeq.set(new UncachedDataSequence(this, rRangeRepresentation));
    addDataSequenceToMap(rRangeRepresentation, xSeq);
    return xSeq;
}

rtl::Reference<UncachedDataSequence>
InternalDataProvider::createDataSequenceFromArray( const OUString& rArrayStr, std::u16string_view rRole, std::u16string_view rRoleQualifier )
{
    if (rArrayStr.indexOf('{') != 0 || rArrayStr[rArrayStr.getLength()-1] != '}')
    {
        // Not an array string.
        return nullptr;
    }

    bool bAllNumeric = true;
    rtl::Reference<UncachedDataSequence> xSeq;

    const sal_Unicode* p = rArrayStr.getStr();
    const sal_Unicode* pEnd = p + rArrayStr.getLength();
    const sal_Unicode* pElem = nullptr;
    OUString aElem;

    std::vector<OUString> aRawElems;
    ++p; // Skip the first '{'.
    --pEnd; // Skip the last '}'.
    bool bInQuote = false;
    for (; p != pEnd; ++p)
    {
        // Skip next "" within the title text: it's an escaped double quotation mark.
        if (bInQuote && *p == '"' && *(p + 1) == '"')
        {
            if (!pElem)
                pElem = p;
            ++p;
        }
        else if (*p == '"')
        {
            bInQuote = !bInQuote;
            if (bInQuote)
            {
                // Opening quote.
                pElem = nullptr;
            }
            else
            {
                // Closing quote.
                if (pElem)
                    aElem = OUString(pElem, p-pElem);
                // Non empty string
                if (!aElem.isEmpty())
                    bAllNumeric = false;
                // Restore also escaped double quotation marks
                aRawElems.push_back(aElem.replaceAll("\"\"", "\""));
                pElem = nullptr;
                aElem.clear();

                ++p; // Skip '"'.
                if (p == pEnd)
                    break;
            }
        }
        else if (*p == ';' && !bInQuote)
        {
            // element separator.
            if (pElem)
                aElem = OUString(pElem, p-pElem);
            aRawElems.push_back(aElem);
            pElem = nullptr;
            aElem.clear();
        }
        else if (!pElem)
            pElem = p;
    }

    if (pElem)
    {
        aElem = OUString(pElem, p-pElem);
        aRawElems.push_back(aElem);
    }

    if (rRole == u"values-y" || rRole == u"values-first" || rRole == u"values-last" ||
        rRole == u"values-min" || rRole == u"values-max" || rRole == u"values-size" ||
        rRole == u"error-bars-y-positive" || rRole == u"error-bars-y-negative")
    {
        // Column values.  Append a new data column and populate it.

        std::vector<double> aValues;
        aValues.reserve(aRawElems.size());
        for (const OUString & aRawElem : aRawElems)
        {
            if (aRawElem.isEmpty())
                aValues.push_back(NAN);
            else
                aValues.push_back(aRawElem.toDouble());
        }
        sal_Int32 n = m_aInternalData.appendColumn();

        m_aInternalData.setColumnValues(n, aValues);

        OUString aRangeRep = OUString::number(n);
        xSeq.set(new UncachedDataSequence(this, aRangeRep));
        addDataSequenceToMap(aRangeRep, xSeq);
    }
    else if (rRole == u"values-x")
    {
        std::vector<double> aValues;
        aValues.reserve(aRawElems.size());
        if (bAllNumeric)
        {
            for (const OUString & aRawElem : aRawElems)
            {
                if (!aRawElem.isEmpty())
                    aValues.push_back(aRawElem.toDouble());
                else
                    aValues.push_back(NAN);
            }
        }
        else
        {
            for (size_t i = 0; i < aRawElems.size(); ++i)
                aValues.push_back(i+1);
        }

        sal_Int32 n = m_aInternalData.appendColumn();
        m_aInternalData.setColumnValues(n, aValues);

        OUString aRangeRep = OUString::number(n);
        xSeq.set(new UncachedDataSequence(this, aRangeRep));
        addDataSequenceToMap(aRangeRep, xSeq);
    }
    else if (rRole == u"categories")
    {
        // Category labels.

        // Store date categories as numbers.
        bool bStoreNumeric = rRoleQualifier == u"date";
        double fValue;
        for (size_t i = 0; i < aRawElems.size(); ++i)
        {
            if (bStoreNumeric)
            {
                bool bGetDouble = bAllNumeric && !aRawElems[i].isEmpty();
                fValue = bGetDouble ? aRawElems[i].toDouble() :
                    std::numeric_limits<double>::quiet_NaN();
            }
            std::vector<uno::Any> aLabels(1,
                bStoreNumeric ? uno::Any(fValue) : uno::Any(aRawElems[i]));
            m_aInternalData.setComplexCategoryLabel(i, std::move(aLabels));
        }

        xSeq.set(new UncachedDataSequence(this, lcl_aCategoriesRangeName));
        addDataSequenceToMap(lcl_aCategoriesRangeName, xSeq);
    }
    else if (rRole == u"label")
    {
        // Data series label.  There should be only one element.  This always
        // goes to the last data column.
        sal_Int32 nColSize = m_aInternalData.getColumnCount();
        if (!aRawElems.empty() && nColSize)
        {
            // Do not overwrite an existing label (attempted by series with no data values)
            if (!m_aInternalData.getComplexColumnLabel(nColSize-1)[0].hasValue())
            {
                std::vector<uno::Any> aLabels(1, uno::Any(aRawElems[0]));
                m_aInternalData.setComplexColumnLabel(nColSize-1, std::move(aLabels));
            }

            OUString aRangeRep = lcl_aLabelRangePrefix + OUString::number(nColSize-1);
            xSeq.set(new UncachedDataSequence(this, aRangeRep));
            addDataSequenceToMap(aRangeRep, xSeq);
        }
    }

    return xSeq;
}

Reference< chart2::data::XDataSequence > InternalDataProvider::createDataSequenceAndAddToMap(
    const OUString & rRangeRepresentation,
    const OUString & rRole )
{
    rtl::Reference< UncachedDataSequence > xSeq =
        new UncachedDataSequence( this, rRangeRepresentation, rRole );
    addDataSequenceToMap( rRangeRepresentation, xSeq );
    return xSeq;
}

// ____ XDataProvider ____
sal_Bool SAL_CALL InternalDataProvider::createDataSourcePossible( const Sequence< beans::PropertyValue >& /* aArguments */ )
{
    return true;
}

namespace
{

sal_Int32 lcl_getInnerLevelCount( const std::vector< std::vector< uno::Any > >& rLabels )
{
    sal_Int32 nCount = 1;//minimum is 1!
    for (auto const& elemLabel : rLabels)
    {
        nCount = std::max<sal_Int32>( elemLabel.size(), nCount );
    }
    return nCount;
}

}//end anonymous namespace

Reference< chart2::data::XDataSource > SAL_CALL InternalDataProvider::createDataSource(
    const Sequence< beans::PropertyValue >& aArguments )
{
    OUString aRangeRepresentation;
    bool bUseColumns = true;
    bool bFirstCellAsLabel = true;
    bool bHasCategories = true;
    uno::Sequence< sal_Int32 > aSequenceMapping;
    DataSourceHelper::readArguments( aArguments, aRangeRepresentation, aSequenceMapping, bUseColumns, bFirstCellAsLabel, bHasCategories );

    if( aRangeRepresentation == lcl_aCategoriesRangeName )
    {
        //return split complex categories if we have any:
        std::vector< Reference< chart2::data::XLabeledDataSequence > > aComplexCategories;
        const std::vector< std::vector< uno::Any > > & aCategories( m_bDataInColumns ? m_aInternalData.getComplexRowLabels() : m_aInternalData.getComplexColumnLabels());
        if( bUseColumns==m_bDataInColumns )
        {
            sal_Int32 nLevelCount = lcl_getInnerLevelCount( aCategories );
            for( sal_Int32 nL=0; nL<nLevelCount; nL++ )
                aComplexCategories.push_back( new LabeledDataSequence(
                    new UncachedDataSequence( this
                        , lcl_aCategoriesLevelRangeNamePrefix + OUString::number( nL )
                        , lcl_aCategoriesRoleName ) ) );
        }
        else
        {
            sal_Int32 nPointCount = m_bDataInColumns ? m_aInternalData.getRowCount() : m_aInternalData.getColumnCount();
            for( sal_Int32 nP=0; nP<nPointCount; nP++ )
                aComplexCategories.push_back( new LabeledDataSequence(
                    new UncachedDataSequence( this
                        , lcl_aCategoriesPointRangeNamePrefix + OUString::number( nP )
                        , lcl_aCategoriesRoleName ) ) );
        }
        //don't add the created sequences to the map as they are used temporarily only ...
        return new DataSource( comphelper::containerToSequence(aComplexCategories) );
    }

    OSL_ASSERT( aRangeRepresentation == lcl_aCompleteRange );

    std::vector< Reference< chart2::data::XLabeledDataSequence > > aResultLSeqVec;

    // categories
    if( bHasCategories )
        aResultLSeqVec.push_back(
            new LabeledDataSequence( createDataSequenceAndAddToMap( lcl_aCategoriesRangeName, lcl_aCategoriesRoleName ) ) );

    // data with labels
    std::vector< Reference< chart2::data::XLabeledDataSequence > > aDataVec;
    const sal_Int32 nCount = (bUseColumns ? m_aInternalData.getColumnCount() : m_aInternalData.getRowCount());
    aDataVec.reserve(nCount);
    for (sal_Int32 nIdx = 0; nIdx < nCount; ++nIdx)
    {
        aDataVec.push_back(
            new LabeledDataSequence(
                createDataSequenceAndAddToMap( OUString::number( nIdx )),
                createDataSequenceAndAddToMap( lcl_aLabelRangePrefix + OUString::number( nIdx ))));
    }

    // attention: this data provider has the limitation that it stores
    // internally if data comes from columns or rows. It is intended for
    // creating only one used data source.
    // @todo: add this information in the range representation strings
    m_bDataInColumns = bUseColumns;

    //reorder labeled sequences according to aSequenceMapping; ignore categories
    for( sal_Int32 nNewIndex = 0; nNewIndex < aSequenceMapping.getLength(); nNewIndex++ )
    {
        std::vector< LabeledDataSequence* >::size_type nOldIndex = aSequenceMapping[nNewIndex];
        if( nOldIndex < aDataVec.size() )
        {
            if( aDataVec[nOldIndex].is() )
            {
                aResultLSeqVec.push_back( aDataVec[nOldIndex] );
                aDataVec[nOldIndex] = nullptr;
            }
        }
    }

    //add left over data sequences to result
    for (auto const& elem : aDataVec)
    {
        if( elem.is() )
            aResultLSeqVec.push_back(elem);
    }

    return new DataSource( comphelper::containerToSequence(aResultLSeqVec) );
}

Sequence< beans::PropertyValue > SAL_CALL InternalDataProvider::detectArguments(
    const Reference< chart2::data::XDataSource >& /* xDataSource */ )
{
    Sequence< beans::PropertyValue > aArguments{
        beans::PropertyValue(
            u"CellRangeRepresentation"_ustr, -1, uno::Any( lcl_aCompleteRange ),
            beans::PropertyState_DIRECT_VALUE ),
        beans::PropertyValue(
            u"DataRowSource"_ustr, -1, uno::Any(
                m_bDataInColumns
                ? css::chart::ChartDataRowSource_COLUMNS
                : css::chart::ChartDataRowSource_ROWS ),
            beans::PropertyState_DIRECT_VALUE ),
        // internal data always contains labels and categories
        beans::PropertyValue(
            u"FirstCellAsLabel"_ustr, -1, uno::Any( true ), beans::PropertyState_DIRECT_VALUE ),
        beans::PropertyValue(
            u"HasCategories"_ustr, -1, uno::Any( true ), beans::PropertyState_DIRECT_VALUE )
    };
    // #i85913# Sequence Mapping is not needed for internal data, as it is
    // applied to the data when the data source is created.

    return aArguments;
}

sal_Bool SAL_CALL InternalDataProvider::createDataSequenceByRangeRepresentationPossible( const OUString& /* aRangeRepresentation */ )
{
    return true;
}

Reference< chart2::data::XDataSequence > SAL_CALL InternalDataProvider::createDataSequenceByRangeRepresentation(
    const OUString& aRangeRepresentation )
{
    if( aRangeRepresentation.match( lcl_aCategoriesRangeName ))
    {
        OSL_ASSERT( aRangeRepresentation == lcl_aCategoriesRangeName );//it is not expected nor implemented that only parts of the categories are really requested

        // categories
        return createDataSequenceAndAddToMap( lcl_aCategoriesRangeName, lcl_aCategoriesRoleName );
    }
    else if( aRangeRepresentation.match( lcl_aLabelRangePrefix ))
    {
        // label
        sal_Int32 nIndex = o3tl::toInt32(aRangeRepresentation.subView( strlen(lcl_aLabelRangePrefix)));
        return createDataSequenceAndAddToMap( lcl_aLabelRangePrefix + OUString::number( nIndex ));
    }
    else if ( aRangeRepresentation == "last" )
    {
        sal_Int32 nIndex = (m_bDataInColumns
                            ? m_aInternalData.getColumnCount()
                            : m_aInternalData.getRowCount()) - 1;
        return createDataSequenceAndAddToMap( OUString::number( nIndex ));
    }
    else if( !aRangeRepresentation.isEmpty())
    {
        // data
        return createDataSequenceAndAddToMap( aRangeRepresentation );
    }

    return Reference< chart2::data::XDataSequence >();
}

Reference<chart2::data::XDataSequence> SAL_CALL
InternalDataProvider::createDataSequenceByValueArray(
    const OUString& aRole, const OUString& aRangeRepresentation, const OUString& aRoleQualifier )
{
    return createDataSequenceFromArray(aRangeRepresentation, aRole, aRoleQualifier);
}

Reference< sheet::XRangeSelection > SAL_CALL InternalDataProvider::getRangeSelection()
{
    // there is no range selection component
    return Reference< sheet::XRangeSelection >();
}

// ____ XInternalDataProvider ____
sal_Bool SAL_CALL InternalDataProvider::hasDataByRangeRepresentation( const OUString& aRange )
{
    bool bResult = false;

    if( aRange.match( lcl_aCategoriesRangeName ))
    {
        OSL_ASSERT( aRange == lcl_aCategoriesRangeName );//it is not expected nor implemented that only parts of the categories are really requested
        bResult = true;
    }
    else if( aRange.match( lcl_aLabelRangePrefix ))
    {
        sal_Int32 nIndex = o3tl::toInt32(aRange.subView( strlen(lcl_aLabelRangePrefix)));
        bResult = (nIndex < (m_bDataInColumns ? m_aInternalData.getColumnCount(): m_aInternalData.getRowCount()));
    }
    else
    {
        sal_Int32 nIndex = aRange.toInt32();
        bResult = (nIndex < (m_bDataInColumns ? m_aInternalData.getColumnCount(): m_aInternalData.getRowCount()));
    }

    return bResult;
}

Sequence< uno::Any > SAL_CALL InternalDataProvider::getDataByRangeRepresentation( const OUString& aRange )
{
    Sequence< uno::Any > aResult;

    if( aRange.match( lcl_aLabelRangePrefix ) )
    {
        auto nIndex = o3tl::toUInt32(aRange.subView( strlen(lcl_aLabelRangePrefix)));
        std::vector< uno::Any > aComplexLabel = m_bDataInColumns
            ? m_aInternalData.getComplexColumnLabel( nIndex )
            : m_aInternalData.getComplexRowLabel( nIndex );
        if( !aComplexLabel.empty() )
            aResult = comphelper::containerToSequence(aComplexLabel);
    }
    else if( aRange.match( lcl_aCategoriesPointRangeNamePrefix ) )
    {
        auto nPointIndex = o3tl::toUInt32(aRange.subView( strlen(lcl_aCategoriesPointRangeNamePrefix) ));
        std::vector< uno::Any > aComplexCategory = m_bDataInColumns
            ? m_aInternalData.getComplexRowLabel( nPointIndex )
            : m_aInternalData.getComplexColumnLabel( nPointIndex );
        if( !aComplexCategory.empty() )
            aResult = comphelper::containerToSequence(aComplexCategory);
    }
    else if( aRange.match( lcl_aCategoriesLevelRangeNamePrefix ) )
    {
        sal_Int32 nLevel = o3tl::toInt32(aRange.subView( strlen(lcl_aCategoriesLevelRangeNamePrefix) ));
        const std::vector< std::vector< uno::Any > > & aCategories( m_bDataInColumns ? m_aInternalData.getComplexRowLabels() : m_aInternalData.getComplexColumnLabels());
        if( nLevel < lcl_getInnerLevelCount( aCategories ) )
            aResult = CommonFunctors::convertToSequence(aCategories, lcl_copyFromLevel(nLevel));
    }
    else if( aRange == lcl_aCategoriesRangeName )
    {
        const std::vector< std::vector< uno::Any > > & aCategories( m_bDataInColumns ? m_aInternalData.getComplexRowLabels() : m_aInternalData.getComplexColumnLabels());
        sal_Int32 nLevelCount = lcl_getInnerLevelCount( aCategories );
        if( nLevelCount == 1 )
        {
            aResult = getDataByRangeRepresentation( lcl_aCategoriesLevelRangeNamePrefix + OUString::number( 0 ) );
        }
        else
        {
            // Maybe this 'else' part and the functions is not necessary anymore.
            const Sequence< OUString > aLabels = m_bDataInColumns ? getRowDescriptions() : getColumnDescriptions();
            aResult = CommonFunctors::convertToSequence(aLabels, CommonFunctors::makeAny());
        }
    }
    else
    {
        sal_Int32 nIndex = aRange.toInt32();
        if( nIndex >= 0 )
        {
            const Sequence< double > aData = m_bDataInColumns
                                                 ? m_aInternalData.getColumnValues(nIndex)
                                                 : m_aInternalData.getRowValues(nIndex);
            if( aData.hasElements() )
                aResult = CommonFunctors::convertToSequence(aData, CommonFunctors::makeAny());
        }
    }

    return aResult;
}

void SAL_CALL InternalDataProvider::setDataByRangeRepresentation(
    const OUString& aRange, const Sequence< uno::Any >& aNewData )
{
    auto aNewVector( comphelper::sequenceToContainer<std::vector< uno::Any >>(aNewData) );
    if( aRange.match( lcl_aLabelRangePrefix ) )
    {
        sal_uInt32 nIndex = o3tl::toInt32(aRange.subView( strlen(lcl_aLabelRangePrefix)));
        if( m_bDataInColumns )
            m_aInternalData.setComplexColumnLabel( nIndex, std::move(aNewVector) );
        else
            m_aInternalData.setComplexRowLabel( nIndex, std::move(aNewVector) );
    }
    else if( aRange.match( lcl_aCategoriesPointRangeNamePrefix ) )
    {
        sal_Int32 nPointIndex = o3tl::toInt32(aRange.subView( strlen(lcl_aCategoriesLevelRangeNamePrefix)));
        if( m_bDataInColumns )
            m_aInternalData.setComplexRowLabel( nPointIndex, std::move(aNewVector) );
        else
            m_aInternalData.setComplexColumnLabel( nPointIndex, std::move(aNewVector) );
    }
    else if( aRange.match( lcl_aCategoriesLevelRangeNamePrefix ) )
    {
        sal_Int32 nLevel = o3tl::toInt32(aRange.subView( strlen(lcl_aCategoriesLevelRangeNamePrefix)));
        std::vector< std::vector< uno::Any > > aComplexCategories = m_bDataInColumns ? m_aInternalData.getComplexRowLabels() : m_aInternalData.getComplexColumnLabels();

        //ensure equal length
        if( aNewVector.size() > aComplexCategories.size() )
            aComplexCategories.resize( aNewVector.size() );
        else if( aNewVector.size() < aComplexCategories.size() )
            aNewVector.resize( aComplexCategories.size() );

        transform( aComplexCategories.begin(), aComplexCategories.end(), aNewVector.begin(),
                   aComplexCategories.begin(), lcl_setAnyAtLevel(nLevel) );

        if( m_bDataInColumns )
            m_aInternalData.setComplexRowLabels( std::move(aComplexCategories) );
        else
            m_aInternalData.setComplexColumnLabels( std::move(aComplexCategories) );
    }
    else if( aRange == lcl_aCategoriesRangeName )
    {
        std::vector< std::vector< uno::Any > > aComplexCategories;
        aComplexCategories.resize( aNewVector.size() );
        transform( aComplexCategories.begin(), aComplexCategories.end(), aNewVector.begin(),
                            aComplexCategories.begin(), lcl_setAnyAtLevel(0) );
        if( m_bDataInColumns )
            m_aInternalData.setComplexRowLabels( std::move(aComplexCategories) );
        else
            m_aInternalData.setComplexColumnLabels( std::move(aComplexCategories) );
    }
    else
    {
        sal_Int32 nIndex = aRange.toInt32();
        if( nIndex>=0 )
        {
            std::vector< double > aNewDataVec;
            aNewDataVec.reserve(aNewData.getLength());
            transform( aNewData.begin(), aNewData.end(),
                       back_inserter( aNewDataVec ), CommonFunctors::ToDouble());
            if( m_bDataInColumns )
                m_aInternalData.setColumnValues( nIndex, aNewDataVec );
            else
                m_aInternalData.setRowValues( nIndex, aNewDataVec );
        }
    }
}

void SAL_CALL InternalDataProvider::insertSequence( ::sal_Int32 nAfterIndex )
{
    if( m_bDataInColumns )
    {
        increaseMapReferences( nAfterIndex + 1, m_aInternalData.getColumnCount());
        m_aInternalData.insertColumn( nAfterIndex );
    }
    else
    {
        increaseMapReferences( nAfterIndex + 1, m_aInternalData.getRowCount());
        m_aInternalData.insertRow( nAfterIndex );
    }
}

void SAL_CALL InternalDataProvider::deleteSequence( ::sal_Int32 nAtIndex )
{
    deleteMapReferences( OUString::number( nAtIndex ));
    deleteMapReferences( lcl_aLabelRangePrefix + OUString::number( nAtIndex ));
    if( m_bDataInColumns )
    {
        decreaseMapReferences( nAtIndex + 1, m_aInternalData.getColumnCount());
        m_aInternalData.deleteColumn( nAtIndex );
    }
    else
    {
        decreaseMapReferences( nAtIndex + 1, m_aInternalData.getRowCount());
        m_aInternalData.deleteRow( nAtIndex );
    }
}

void SAL_CALL InternalDataProvider::appendSequence()
{
    if( m_bDataInColumns )
        m_aInternalData.appendColumn();
    else
        m_aInternalData.appendRow();
}

void SAL_CALL InternalDataProvider::insertComplexCategoryLevel( sal_Int32 nLevel )
{
    OSL_ENSURE( nLevel> 0, "you can only insert category levels > 0" );//the first categories level cannot be deleted, check the calling code for error
    if( nLevel>0 )
    {
        std::vector< std::vector< uno::Any > > aComplexCategories = m_bDataInColumns ? m_aInternalData.getComplexRowLabels() : m_aInternalData.getComplexColumnLabels();
        std::for_each( aComplexCategories.begin(), aComplexCategories.end(), lcl_insertAnyAtLevel(nLevel) );
        if( m_bDataInColumns )
            m_aInternalData.setComplexRowLabels( std::move(aComplexCategories) );
        else
            m_aInternalData.setComplexColumnLabels( std::move(aComplexCategories) );

        tSequenceMapRange aRange( m_aSequenceMap.equal_range( lcl_aCategoriesRangeName ));
        std::for_each( aRange.first, aRange.second, lcl_setModified());
    }
}
void SAL_CALL InternalDataProvider::deleteComplexCategoryLevel( sal_Int32 nLevel )
{
    OSL_ENSURE( nLevel>0, "you can only delete category levels > 0" );//the first categories level cannot be deleted, check the calling code for error
    if( nLevel>0 )
    {
        std::vector< std::vector< uno::Any > > aComplexCategories = m_bDataInColumns ? m_aInternalData.getComplexRowLabels() : m_aInternalData.getComplexColumnLabels();
        std::for_each( aComplexCategories.begin(), aComplexCategories.end(), lcl_removeAnyAtLevel(nLevel) );
        if( m_bDataInColumns )
            m_aInternalData.setComplexRowLabels( std::move(aComplexCategories) );
        else
            m_aInternalData.setComplexColumnLabels( std::move(aComplexCategories) );

        tSequenceMapRange aRange( m_aSequenceMap.equal_range( lcl_aCategoriesRangeName ));
        std::for_each( aRange.first, aRange.second, lcl_setModified());
    }
}

void SAL_CALL InternalDataProvider::insertDataPointForAllSequences( ::sal_Int32 nAfterIndex )
{
    sal_Int32 nMaxRep = 0;
    if( m_bDataInColumns )
    {
        m_aInternalData.insertRow( nAfterIndex );
        nMaxRep = m_aInternalData.getColumnCount();
    }
    else
    {
        m_aInternalData.insertColumn( nAfterIndex );
        nMaxRep = m_aInternalData.getRowCount();
    }

    // notify change to all affected ranges
    tSequenceMap::const_iterator aBegin( m_aSequenceMap.lower_bound( u"0"_ustr));
    tSequenceMap::const_iterator aEnd( m_aSequenceMap.upper_bound( OUString::number( nMaxRep )));
    std::for_each( aBegin, aEnd, lcl_setModified());

    tSequenceMapRange aRange( m_aSequenceMap.equal_range( lcl_aCategoriesRangeName ));
    std::for_each( aRange.first, aRange.second, lcl_setModified());
}

void SAL_CALL InternalDataProvider::deleteDataPointForAllSequences( ::sal_Int32 nAtIndex )
{
    sal_Int32 nMaxRep = 0;
    if( m_bDataInColumns )
    {
        m_aInternalData.deleteRow( nAtIndex );
        nMaxRep = m_aInternalData.getColumnCount();
    }
    else
    {
        m_aInternalData.deleteColumn( nAtIndex );
        nMaxRep = m_aInternalData.getRowCount();
    }

    // notify change to all affected ranges
    tSequenceMap::const_iterator aBegin( m_aSequenceMap.lower_bound( u"0"_ustr));
    tSequenceMap::const_iterator aEnd( m_aSequenceMap.upper_bound( OUString::number( nMaxRep )));
    std::for_each( aBegin, aEnd, lcl_setModified());

    tSequenceMapRange aRange( m_aSequenceMap.equal_range( lcl_aCategoriesRangeName ));
    std::for_each( aRange.first, aRange.second, lcl_setModified());
}

void SAL_CALL InternalDataProvider::swapDataPointWithNextOneForAllSequences( ::sal_Int32 nAtIndex )
{
    if( m_bDataInColumns )
        m_aInternalData.swapRowWithNext( nAtIndex );
    else
        m_aInternalData.swapColumnWithNext( nAtIndex );
    sal_Int32 nMaxRep = (m_bDataInColumns
                         ? m_aInternalData.getColumnCount()
                         : m_aInternalData.getRowCount());

    // notify change to all affected ranges
    tSequenceMap::const_iterator aBegin( m_aSequenceMap.lower_bound( u"0"_ustr));
    tSequenceMap::const_iterator aEnd( m_aSequenceMap.upper_bound( OUString::number( nMaxRep )));
    std::for_each( aBegin, aEnd, lcl_setModified());

    tSequenceMapRange aRange( m_aSequenceMap.equal_range( lcl_aCategoriesRangeName ));
    std::for_each( aRange.first, aRange.second, lcl_setModified());
}

void SAL_CALL InternalDataProvider::registerDataSequenceForChanges( const Reference< chart2::data::XDataSequence >& xSeq )
{
    if( xSeq.is())
        addDataSequenceToMap( xSeq->getSourceRangeRepresentation(), xSeq );
}

void SAL_CALL InternalDataProvider::insertDataSeries(::sal_Int32 nAfterIndex)
{
    // call the dialog insertion
    DataBrowserModel aDBM(m_xChartModel);
    aDBM.insertDataSeries(nAfterIndex);
}

// ____ XRangeXMLConversion ____
OUString SAL_CALL InternalDataProvider::convertRangeToXML( const OUString& aRangeRepresentation )
{
    XMLRangeHelper::CellRange aRange;
    aRange.aTableName = "local-table";

    // attention: this data provider has the limitation that it stores
    // internally if data comes from columns or rows. It is intended for
    // creating only one used data source.
    // @todo: add this information in the range representation strings
    if( aRangeRepresentation.match( lcl_aCategoriesRangeName ))
    {
        OSL_ASSERT( aRangeRepresentation == lcl_aCategoriesRangeName );//it is not expected nor implemented that only parts of the categories are really requested
        aRange.aUpperLeft.bIsEmpty = false;
        if( m_bDataInColumns )
        {
            aRange.aUpperLeft.nColumn = 0;
            aRange.aUpperLeft.nRow = 1;
            aRange.aLowerRight = aRange.aUpperLeft;
            aRange.aLowerRight.nRow = m_aInternalData.getRowCount();
        }
        else
        {
            aRange.aUpperLeft.nColumn = 1;
            aRange.aUpperLeft.nRow = 0;
            aRange.aLowerRight = aRange.aUpperLeft;
            aRange.aLowerRight.nColumn = m_aInternalData.getColumnCount();
        }
    }
    else if( aRangeRepresentation.match( lcl_aLabelRangePrefix ))
    {
        sal_Int32 nIndex = o3tl::toInt32(aRangeRepresentation.subView( strlen(lcl_aLabelRangePrefix)));
        aRange.aUpperLeft.bIsEmpty = false;
        aRange.aLowerRight.bIsEmpty = true;
        if( m_bDataInColumns )
        {
            aRange.aUpperLeft.nColumn = nIndex + 1;
            aRange.aUpperLeft.nRow = 0;
        }
        else
        {
            aRange.aUpperLeft.nColumn = 0;
            aRange.aUpperLeft.nRow = nIndex + 1;
        }
    }
    else if( aRangeRepresentation == lcl_aCompleteRange )
    {
        aRange.aUpperLeft.bIsEmpty = false;
        aRange.aLowerRight.bIsEmpty = false;
        aRange.aUpperLeft.nColumn = 0;
        aRange.aUpperLeft.nRow = 0;
        aRange.aLowerRight.nColumn = m_aInternalData.getColumnCount();
        aRange.aLowerRight.nRow = m_aInternalData.getRowCount();
    }
    else
    {
        sal_Int32 nIndex = aRangeRepresentation.toInt32();
        aRange.aUpperLeft.bIsEmpty = false;
        if( m_bDataInColumns )
        {
            aRange.aUpperLeft.nColumn = nIndex + 1;
            aRange.aUpperLeft.nRow = 1;
            aRange.aLowerRight = aRange.aUpperLeft;
            aRange.aLowerRight.nRow = m_aInternalData.getRowCount();
        }
        else
        {
            aRange.aUpperLeft.nColumn = 1;
            aRange.aUpperLeft.nRow = nIndex + 1;
            aRange.aLowerRight = aRange.aUpperLeft;
            aRange.aLowerRight.nColumn = m_aInternalData.getColumnCount();
        }
    }

    return XMLRangeHelper::getXMLStringFromCellRange( aRange );
}

OUString SAL_CALL InternalDataProvider::convertRangeFromXML( const OUString& aXMLRange )
{
    // Handle non-standards-conforming table:cell-range-address="PivotChart", see
    // <https://bugs.documentfoundation.org/show_bug.cgi?id=112783> "PIVOT CHARTS: Save produces
    // invalid file because of invalid cell address":
    if (aXMLRange == "PivotChart") {
        return u""_ustr;
    }

    static constexpr OUString aPivotTableID(u"PT@"_ustr);
    if (aXMLRange.startsWith(aPivotTableID))
        return aXMLRange.copy(aPivotTableID.getLength());

    XMLRangeHelper::CellRange aRange( XMLRangeHelper::getCellRangeFromXMLString( aXMLRange ));
    if( aRange.aUpperLeft.bIsEmpty )
    {
        OSL_ENSURE( aRange.aLowerRight.bIsEmpty, "Weird Range" );
        return OUString();
    }

    // "all"
    if( !aRange.aLowerRight.bIsEmpty &&
        ( aRange.aUpperLeft.nColumn != aRange.aLowerRight.nColumn ) &&
        ( aRange.aUpperLeft.nRow != aRange.aLowerRight.nRow ) )
        return lcl_aCompleteRange;

    // attention: this data provider has the limitation that it stores
    // internally if data comes from columns or rows. It is intended for
    // creating only one used data source.
    // @todo: add this information in the range representation strings

    // data in columns
    if( m_bDataInColumns )
    {
        if( aRange.aUpperLeft.nColumn == 0 )
            return lcl_aCategoriesRangeName;
        if( aRange.aUpperLeft.nRow == 0 )
            return lcl_aLabelRangePrefix + OUString::number( aRange.aUpperLeft.nColumn - 1 );

        return OUString::number( aRange.aUpperLeft.nColumn - 1 );
    }

    // data in rows
    if( aRange.aUpperLeft.nRow == 0 )
        return lcl_aCategoriesRangeName;
    if( aRange.aUpperLeft.nColumn == 0 )
        return lcl_aLabelRangePrefix + OUString::number( aRange.aUpperLeft.nRow - 1 );

    return OUString::number( aRange.aUpperLeft.nRow - 1 );
}

namespace
{

template< class Type >
Sequence< Sequence< Type > > lcl_convertVectorVectorToSequenceSequence( const std::vector< std::vector< Type > >& rIn )
{
    return CommonFunctors::convertToSequence(rIn, [](auto& v)
                                             { return comphelper::containerToSequence(v); });
}

template< class Type >
std::vector< std::vector< Type > > lcl_convertSequenceSequenceToVectorVector( const Sequence< Sequence< Type > >& rIn )
{
    std::vector< std::vector< Type > > aRet;
    aRet.reserve(rIn.getLength());
    std::transform(rIn.begin(), rIn.end(), std::back_inserter(aRet),
                   [](auto& s) { return comphelper::sequenceToContainer<std::vector<Type>>(s); });
    return aRet;
}

Sequence< Sequence< OUString > > lcl_convertComplexAnyVectorToStringSequence( const std::vector< std::vector< uno::Any > >& rIn )
{
    return CommonFunctors::convertToSequence(
        rIn,
        [](auto& v) { return CommonFunctors::convertToSequence(v, CommonFunctors::ToString()); });
}

std::vector< std::vector< uno::Any > > lcl_convertComplexStringSequenceToAnyVector( const Sequence< Sequence< OUString > >& rIn )
{
    std::vector< std::vector< uno::Any > > aRet;
    aRet.reserve(rIn.getLength());
    std::transform(rIn.begin(), rIn.end(), std::back_inserter(aRet),
                   [](auto& s) { return lcl_StringToAnyVector(s); });
    return aRet;
}

class SplitCategoriesProvider_ForComplexDescriptions : public SplitCategoriesProvider
{
public:

    explicit SplitCategoriesProvider_ForComplexDescriptions( const std::vector< std::vector< uno::Any > >& rComplexDescriptions )
        : m_rComplexDescriptions( rComplexDescriptions )
    {}

    virtual sal_Int32 getLevelCount() const override;
    virtual uno::Sequence< OUString > getStringsForLevel( sal_Int32 nIndex ) const override;

private:
    const std::vector< std::vector< uno::Any > >& m_rComplexDescriptions;
};

sal_Int32 SplitCategoriesProvider_ForComplexDescriptions::getLevelCount() const
{
    return lcl_getInnerLevelCount( m_rComplexDescriptions );
}
uno::Sequence< OUString > SplitCategoriesProvider_ForComplexDescriptions::getStringsForLevel( sal_Int32 nLevel ) const
{
    if( nLevel < lcl_getInnerLevelCount( m_rComplexDescriptions ) )
        return CommonFunctors::convertToSequence(m_rComplexDescriptions, lcl_getStringFromLevelVector(nLevel));
    return {};
}

}//anonymous namespace

// ____ XDateCategories ____
Sequence< double > SAL_CALL InternalDataProvider::getDateCategories()
{
    const std::vector< std::vector< uno::Any > > & aCategories( m_bDataInColumns ? m_aInternalData.getComplexRowLabels() : m_aInternalData.getComplexColumnLabels());
    sal_Int32 nCount = aCategories.size();
    Sequence< double > aDoubles( nCount );
    auto aDoublesRange = asNonConstRange(aDoubles);
    sal_Int32 nN=0;
    for (auto const& category : aCategories)
    {
        double fValue;
        if( category.empty() || !(category[0]>>=fValue) )
            fValue = std::numeric_limits<double>::quiet_NaN();
        aDoublesRange[nN++]=fValue;
    }
    return aDoubles;
}

void SAL_CALL InternalDataProvider::setDateCategories( const Sequence< double >& rDates )
{
    sal_Int32 nCount = rDates.getLength();
    std::vector< std::vector< uno::Any > > aNewCategories;
    aNewCategories.reserve(nCount);
    std::vector< uno::Any > aSingleLabel(1);

    for(sal_Int32 nN=0; nN<nCount; ++nN )
    {
        aSingleLabel[0] <<= rDates[nN];
        aNewCategories.push_back(aSingleLabel);
    }

    if( m_bDataInColumns )
        m_aInternalData.setComplexRowLabels( std::move(aNewCategories) );
    else
        m_aInternalData.setComplexColumnLabels( std::move(aNewCategories) );
}

// ____ XAnyDescriptionAccess ____
Sequence< Sequence< uno::Any > > SAL_CALL InternalDataProvider::getAnyRowDescriptions()
{
    return lcl_convertVectorVectorToSequenceSequence( m_aInternalData.getComplexRowLabels() );
}
void SAL_CALL InternalDataProvider::setAnyRowDescriptions( const Sequence< Sequence< uno::Any > >& aRowDescriptions )
{
    m_aInternalData.setComplexRowLabels( lcl_convertSequenceSequenceToVectorVector( aRowDescriptions ) );
}
Sequence< Sequence< uno::Any > > SAL_CALL InternalDataProvider::getAnyColumnDescriptions()
{
    return lcl_convertVectorVectorToSequenceSequence( m_aInternalData.getComplexColumnLabels() );
}
void SAL_CALL InternalDataProvider::setAnyColumnDescriptions( const Sequence< Sequence< uno::Any > >& aColumnDescriptions )
{
    m_aInternalData.setComplexColumnLabels( lcl_convertSequenceSequenceToVectorVector( aColumnDescriptions ) );
}

// ____ XComplexDescriptionAccess ____
Sequence< Sequence< OUString > > SAL_CALL InternalDataProvider::getComplexRowDescriptions()
{
    return lcl_convertComplexAnyVectorToStringSequence( m_aInternalData.getComplexRowLabels() );
}
void SAL_CALL InternalDataProvider::setComplexRowDescriptions( const Sequence< Sequence< OUString > >& aRowDescriptions )
{
    m_aInternalData.setComplexRowLabels( lcl_convertComplexStringSequenceToAnyVector(aRowDescriptions) );
}
Sequence< Sequence< OUString > > SAL_CALL InternalDataProvider::getComplexColumnDescriptions()
{
    return lcl_convertComplexAnyVectorToStringSequence( m_aInternalData.getComplexColumnLabels() );
}
void SAL_CALL InternalDataProvider::setComplexColumnDescriptions( const Sequence< Sequence< OUString > >& aColumnDescriptions )
{
    m_aInternalData.setComplexColumnLabels( lcl_convertComplexStringSequenceToAnyVector(aColumnDescriptions) );
}

// ____ XChartDataArray ____
Sequence< Sequence< double > > SAL_CALL InternalDataProvider::getData()
{
    return m_aInternalData.getData();
}

void SAL_CALL InternalDataProvider::setData( const Sequence< Sequence< double > >& rDataInRows )
{
    return m_aInternalData.setData( rDataInRows );
}

void SAL_CALL InternalDataProvider::setRowDescriptions( const Sequence< OUString >& aRowDescriptions )
{
    std::vector< std::vector< uno::Any > > aComplexDescriptions( aRowDescriptions.getLength() );
    transform( aComplexDescriptions.begin(), aComplexDescriptions.end(), aRowDescriptions.begin(),
               aComplexDescriptions.begin(), lcl_setAnyAtLevelFromStringSequence(0) );
    m_aInternalData.setComplexRowLabels( std::move(aComplexDescriptions) );
}

void SAL_CALL InternalDataProvider::setColumnDescriptions( const Sequence< OUString >& aColumnDescriptions )
{
    std::vector< std::vector< uno::Any > > aComplexDescriptions( aColumnDescriptions.getLength() );
    transform( aComplexDescriptions.begin(), aComplexDescriptions.end(), aColumnDescriptions.begin(),
               aComplexDescriptions.begin(), lcl_setAnyAtLevelFromStringSequence(0) );
    m_aInternalData.setComplexColumnLabels( std::move(aComplexDescriptions) );
}

Sequence< OUString > SAL_CALL InternalDataProvider::getRowDescriptions()
{
    const std::vector< std::vector< uno::Any > > & aComplexLabels( m_aInternalData.getComplexRowLabels() );
    SplitCategoriesProvider_ForComplexDescriptions aProvider( aComplexLabels );
    return ExplicitCategoriesProvider::getExplicitSimpleCategories( aProvider );
}

Sequence< OUString > SAL_CALL InternalDataProvider::getColumnDescriptions()
{
    const std::vector< std::vector< uno::Any > > & aComplexLabels( m_aInternalData.getComplexColumnLabels() );
    SplitCategoriesProvider_ForComplexDescriptions aProvider( aComplexLabels );
    return ExplicitCategoriesProvider::getExplicitSimpleCategories( aProvider );
}

// ____ XChartData (base of XChartDataArray) ____
void SAL_CALL InternalDataProvider::addChartDataChangeEventListener(
    const Reference< css::chart::XChartDataChangeEventListener >& )
{
}

void SAL_CALL InternalDataProvider::removeChartDataChangeEventListener(
    const Reference< css::chart::XChartDataChangeEventListener >& )
{
}

double SAL_CALL InternalDataProvider::getNotANumber()
{
    return std::numeric_limits<double>::quiet_NaN();
}

sal_Bool SAL_CALL InternalDataProvider::isNotANumber( double nNumber )
{
    return std::isnan( nNumber )
        || std::isinf( nNumber );
}
// lang::XInitialization:
void SAL_CALL InternalDataProvider::initialize(const uno::Sequence< uno::Any > & _aArguments)
{
    comphelper::SequenceAsHashMap aArgs(_aArguments);
    if ( aArgs.getUnpackedValueOrDefault( u"CreateDefaultData"_ustr, false ) )
            m_aInternalData.createDefaultData();
}

// ____ XCloneable ____
Reference< util::XCloneable > SAL_CALL InternalDataProvider::createClone()
{
    return Reference< util::XCloneable >( new InternalDataProvider( *this ));
}

OUString SAL_CALL InternalDataProvider::getImplementationName()
{
    // note: in xmloff this name is used to indicate usage of own data
    return u"com.sun.star.comp.chart.InternalDataProvider"_ustr;
}

sal_Bool SAL_CALL InternalDataProvider::supportsService( const OUString& rServiceName )
{
    return cppu::supportsService(this, rServiceName);
}

css::uno::Sequence< OUString > SAL_CALL InternalDataProvider::getSupportedServiceNames()
{
    return { u"com.sun.star.chart2.data.DataProvider"_ustr };
}

} //  namespace chart

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface *
com_sun_star_comp_chart_InternalDataProvider_get_implementation(css::uno::XComponentContext *,
        css::uno::Sequence<css::uno::Any> const &)
{
    return cppu::acquire(new ::chart::InternalDataProvider);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
