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

#include <drawingml/chart/typegroupconverter.hxx>

#include <com/sun/star/chart/DataLabelPlacement.hpp>
#include <com/sun/star/chart2/CartesianCoordinateSystem2d.hpp>
#include <com/sun/star/chart2/CartesianCoordinateSystem3d.hpp>
#include <com/sun/star/chart2/PolarCoordinateSystem2d.hpp>
#include <com/sun/star/chart2/PolarCoordinateSystem3d.hpp>
#include <com/sun/star/chart2/CurveStyle.hpp>
#include <com/sun/star/chart2/DataPointGeometry3D.hpp>
#include <com/sun/star/chart2/PieChartSubType.hpp>
#include <com/sun/star/chart2/StackingDirection.hpp>
#include <com/sun/star/chart2/Symbol.hpp>
#include <com/sun/star/chart2/XChartTypeContainer.hpp>
#include <com/sun/star/chart2/XCoordinateSystem.hpp>
#include <com/sun/star/chart2/XDataSeriesContainer.hpp>
#include <com/sun/star/chart2/XDiagram.hpp>
#include <com/sun/star/chart2/data/XDataSink.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>

#include <comphelper/sequence.hxx>
#include <osl/diagnose.h>
#include <drawingml/lineproperties.hxx>
#include <drawingml/chart/seriesmodel.hxx>
#include <drawingml/chart/seriesconverter.hxx>
#include <drawingml/chart/typegroupmodel.hxx>
#include <oox/core/xmlfilterbase.hxx>
#include <oox/token/namespaces.hxx>
#include <oox/token/properties.hxx>
#include <oox/token/tokens.hxx>
#include <tools/UnitConversion.hxx>

namespace oox::drawingml::chart {

using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::chart2;
using namespace ::com::sun::star::chart2::data;
using namespace ::com::sun::star::uno;

namespace {

// chart type service names
const char SERVICE_CHART2_AREA[]      = "com.sun.star.chart2.AreaChartType";
const char SERVICE_CHART2_CANDLE[]    = "com.sun.star.chart2.CandleStickChartType";
const char SERVICE_CHART2_COLUMN[]    = "com.sun.star.chart2.ColumnChartType";
const char SERVICE_CHART2_LINE[]      = "com.sun.star.chart2.LineChartType";
const char SERVICE_CHART2_NET[]       = "com.sun.star.chart2.NetChartType";
const char SERVICE_CHART2_FILLEDNET[] = "com.sun.star.chart2.FilledNetChartType";
const char SERVICE_CHART2_PIE[]       = "com.sun.star.chart2.PieChartType";
const char SERVICE_CHART2_SCATTER[]   = "com.sun.star.chart2.ScatterChartType";
const char SERVICE_CHART2_BUBBLE[]    = "com.sun.star.chart2.BubbleChartType";
const char SERVICE_CHART2_SURFACE[]   = "com.sun.star.chart2.ColumnChartType";    // Todo
const char SERVICE_CHART2_FUNNEL[]    = "com.sun.star.chart2.FunnelChartType";
const char SERVICE_CHART2_HISTO[]     = "com.sun.star.chart2.HistogramChartType";

namespace csscd = css::chart::DataLabelPlacement;

const TypeGroupInfo spTypeInfos[] =
{
    // type-id          type-category         service                   varied-point-color   default label pos     polar  area2d 1stvis xcateg swap   stack  picopt
    { TYPEID_BAR,       TYPECATEGORY_BAR,     SERVICE_CHART2_COLUMN,    VARPOINTMODE_SINGLE, csscd::OUTSIDE,       false, true,  false, true,  false, true,  true  },
    { TYPEID_HORBAR,    TYPECATEGORY_BAR,     SERVICE_CHART2_COLUMN,    VARPOINTMODE_SINGLE, csscd::OUTSIDE,       false, true,  false, true,  true,  true,  true  },
    { TYPEID_HISTO,     TYPECATEGORY_HISTO,   SERVICE_CHART2_HISTO,     VARPOINTMODE_SINGLE, csscd::OUTSIDE,       false, true,  false, true,  true,  true,  true  },
    { TYPEID_LINE,      TYPECATEGORY_LINE,    SERVICE_CHART2_LINE,      VARPOINTMODE_SINGLE, csscd::RIGHT,         false, false, false, true,  false, true,  false },
    { TYPEID_AREA,      TYPECATEGORY_LINE,    SERVICE_CHART2_AREA,      VARPOINTMODE_NONE,   csscd::CENTER,        false, true,  false, true,  false, true,  false },
    { TYPEID_STOCK,     TYPECATEGORY_LINE,    SERVICE_CHART2_CANDLE,    VARPOINTMODE_NONE,   csscd::RIGHT,         false, false, false, true,  false, true,  false },
    { TYPEID_RADARLINE, TYPECATEGORY_RADAR,   SERVICE_CHART2_NET,       VARPOINTMODE_SINGLE, csscd::OUTSIDE,       true,  false, false, true,  false, false, false },
    { TYPEID_RADARAREA, TYPECATEGORY_RADAR,   SERVICE_CHART2_FILLEDNET, VARPOINTMODE_NONE,   csscd::OUTSIDE,       true,  true,  false, true,  false, false, false },
    { TYPEID_PIE,       TYPECATEGORY_PIE,     SERVICE_CHART2_PIE,       VARPOINTMODE_MULTI,  csscd::AVOID_OVERLAP, true,  true,  true,  true,  false, false, false },
    { TYPEID_DOUGHNUT,  TYPECATEGORY_PIE,     SERVICE_CHART2_PIE,       VARPOINTMODE_MULTI,  csscd::AVOID_OVERLAP, true,  true,  false, true,  false, false, false },
    { TYPEID_OFPIE,     TYPECATEGORY_PIE,     SERVICE_CHART2_PIE,       VARPOINTMODE_MULTI,  csscd::AVOID_OVERLAP, true,  true,  true,  true,  false, false, false },
    { TYPEID_SCATTER,   TYPECATEGORY_SCATTER, SERVICE_CHART2_SCATTER,   VARPOINTMODE_SINGLE, csscd::RIGHT,         false, false, false, false, false, false, false },
    { TYPEID_BUBBLE,    TYPECATEGORY_SCATTER, SERVICE_CHART2_BUBBLE,    VARPOINTMODE_SINGLE, csscd::RIGHT,         false, true,  false, false, false, false, false },
    { TYPEID_SURFACE,   TYPECATEGORY_SURFACE, SERVICE_CHART2_SURFACE,   VARPOINTMODE_NONE,   csscd::RIGHT,         false, true,  false, true,  false, false, false },
    { TYPEID_FUNNEL,    TYPECATEGORY_FUNNEL,  SERVICE_CHART2_FUNNEL,    VARPOINTMODE_SINGLE, csscd::OUTSIDE,       false, true,  false, true,  false, false, false }
};

const TypeGroupInfo saUnknownTypeInfo =
    { TYPEID_UNKNOWN,   TYPECATEGORY_BAR,     SERVICE_CHART2_COLUMN,  VARPOINTMODE_SINGLE, csscd::OUTSIDE,       false, true,  false, true,  false, true, true  };

const TypeGroupInfo& lclGetTypeInfoFromTypeId( TypeId eTypeId )
{
    for( auto const &rIt : spTypeInfos)
    {
        if( rIt.meTypeId == eTypeId )
            return rIt;
    }
    OSL_ENSURE( eTypeId == TYPEID_UNKNOWN, "lclGetTypeInfoFromTypeId - unexpected chart type identifier" );
    return saUnknownTypeInfo;
}

} // namespace

const TypeGroupInfo& GetTypeGroupInfo( TypeId eType )
{
    return lclGetTypeInfoFromTypeId(eType);
}

UpDownBarsConverter::UpDownBarsConverter( const ConverterRoot& rParent, UpDownBarsModel& rModel ) :
    ConverterBase< UpDownBarsModel >( rParent, rModel )
{
}

UpDownBarsConverter::~UpDownBarsConverter()
{
}

void UpDownBarsConverter::convertFromModel( const Reference< XChartType >& rxChartType )
{
    PropertySet aTypeProp( rxChartType );

    // upbar format
    Reference< XPropertySet > xWhitePropSet;
    if( aTypeProp.getProperty( xWhitePropSet, PROP_WhiteDay ) )
    {
        PropertySet aPropSet( xWhitePropSet );
        getFormatter().convertFrameFormatting( aPropSet, mrModel.mxUpBars, OBJECTTYPE_UPBAR );
    }

    // downbar format
    Reference< XPropertySet > xBlackPropSet;
    if( aTypeProp.getProperty( xBlackPropSet, PROP_BlackDay ) )
    {
        PropertySet aPropSet( xBlackPropSet );
        getFormatter().convertFrameFormatting( aPropSet, mrModel.mxDownBars, OBJECTTYPE_DOWNBAR );
    }
}

TypeGroupConverter::TypeGroupConverter( const ConverterRoot& rParent, TypeGroupModel& rModel ) :
    ConverterBase< TypeGroupModel >( rParent, rModel ),
    mb3dChart( false )
{
    TypeId eTypeId = TYPEID_UNKNOWN;
    switch( mrModel.mnTypeId )
    {
#define ENSURE_AXESCOUNT( min, max ) OSL_ENSURE( (min <= static_cast<int>(mrModel.maAxisIds.size())) && (static_cast<int>(mrModel.maAxisIds.size()) <= max), "TypeGroupConverter::TypeGroupConverter - invalid axes count" )
        case C_TOKEN( area3DChart ):    ENSURE_AXESCOUNT( 2, 3 ); eTypeId = TYPEID_AREA;      mb3dChart = true;   break;
        case C_TOKEN( areaChart ):      ENSURE_AXESCOUNT( 2, 2 ); eTypeId = TYPEID_AREA;      mb3dChart = false;  break;
        case C_TOKEN( bar3DChart ):     ENSURE_AXESCOUNT( 2, 3 ); eTypeId = TYPEID_BAR;       mb3dChart = true;   break;
        case C_TOKEN( barChart ):       ENSURE_AXESCOUNT( 2, 2 ); eTypeId = TYPEID_BAR;       mb3dChart = false;  break;
        case C_TOKEN( bubbleChart ):    ENSURE_AXESCOUNT( 2, 2 ); eTypeId = TYPEID_BUBBLE;    mb3dChart = false;  break;
        case C_TOKEN( doughnutChart ):  ENSURE_AXESCOUNT( 0, 0 ); eTypeId = TYPEID_DOUGHNUT;  mb3dChart = false;  break;
        case CX_TOKEN( funnel ):        ENSURE_AXESCOUNT( 2, 2 ); eTypeId = TYPEID_FUNNEL;    mb3dChart = false;  break;
        case C_TOKEN( line3DChart ):    ENSURE_AXESCOUNT( 3, 3 ); eTypeId = TYPEID_LINE;      mb3dChart = true;   break;
        case C_TOKEN( lineChart ):      ENSURE_AXESCOUNT( 2, 2 ); eTypeId = TYPEID_LINE;      mb3dChart = false;  break;
        case C_TOKEN( ofPieChart ):     ENSURE_AXESCOUNT( 0, 0 ); eTypeId = TYPEID_OFPIE;     mb3dChart = false;  break;
        case C_TOKEN( pie3DChart ):     ENSURE_AXESCOUNT( 0, 0 ); eTypeId = TYPEID_PIE;       mb3dChart = true;   break;
        case C_TOKEN( pieChart ):       ENSURE_AXESCOUNT( 0, 0 ); eTypeId = TYPEID_PIE;       mb3dChart = false;  break;
        case C_TOKEN( radarChart ):     ENSURE_AXESCOUNT( 2, 2 ); eTypeId = TYPEID_RADARLINE; mb3dChart = false;  break;
        case C_TOKEN( scatterChart ):   ENSURE_AXESCOUNT( 2, 2 ); eTypeId = TYPEID_SCATTER;   mb3dChart = false;  break;
        case C_TOKEN( stockChart ):     ENSURE_AXESCOUNT( 2, 2 ); eTypeId = TYPEID_STOCK;     mb3dChart = false;  break;
        case C_TOKEN( surface3DChart ): ENSURE_AXESCOUNT( 3, 3 ); eTypeId = TYPEID_SURFACE;   mb3dChart = true;   break;
        case C_TOKEN( surfaceChart ):   ENSURE_AXESCOUNT( 2, 3 ); eTypeId = TYPEID_SURFACE;   mb3dChart = true;   break;    // 3D bar chart from all surface charts
        default:    OSL_FAIL( "TypeGroupConverter::TypeGroupConverter - unknown chart type" );
#undef ENSURE_AXESCOUNT
    }

    // special handling for some chart types
    switch( eTypeId )
    {
        case TYPEID_BAR:
            if( mrModel.mnBarDir == XML_bar )
                eTypeId = TYPEID_HORBAR;
        break;
        case TYPEID_RADARLINE:
            if( mrModel.mnRadarStyle == XML_filled )
                eTypeId = TYPEID_RADARAREA;
        break;
        case TYPEID_SURFACE:
            // create a deep 3D bar chart from surface charts
            mrModel.mnGrouping = XML_standard;
        break;
        default:;
    }

    // set the chart type info struct for the current chart type
    maTypeInfo = lclGetTypeInfoFromTypeId( eTypeId );
}

TypeGroupConverter::~TypeGroupConverter()
{
}

bool TypeGroupConverter::isStacked() const
{
    return maTypeInfo.mbSupportsStacking && (mrModel.mnGrouping == XML_stacked);
}

bool TypeGroupConverter::isPercent() const
{
    return maTypeInfo.mbSupportsStacking && (mrModel.mnGrouping == XML_percentStacked);
}

bool TypeGroupConverter::isWall3dChart() const
{
    return mb3dChart && (maTypeInfo.meTypeCategory != TYPECATEGORY_PIE);
}

bool TypeGroupConverter::isDeep3dChart() const
{
    return isWall3dChart() && (mrModel.mnGrouping == XML_standard);
}

bool TypeGroupConverter::isSeriesFrameFormat() const
{
    return mb3dChart || maTypeInfo.mbSeriesIsFrame2d;
}

ObjectType TypeGroupConverter::getSeriesObjectType() const
{
    return mb3dChart ? OBJECTTYPE_FILLEDSERIES3D :
        (maTypeInfo.mbSeriesIsFrame2d ? OBJECTTYPE_FILLEDSERIES2D : OBJECTTYPE_LINEARSERIES2D);
}

OUString TypeGroupConverter::getSingleSeriesTitle() const
{
    OUString aSeriesTitle;
    if( !mrModel.maSeries.empty() && (maTypeInfo.mbSingleSeriesVis || (mrModel.maSeries.size() == 1)) )
        if( const TextModel* pText = mrModel.maSeries.front()->mxText.get() )
            if( const DataSequenceModel* pDataSeq = pText->mxDataSeq.get() )
                if( !pDataSeq->maData.empty() )
                    pDataSeq->maData.begin()->second >>= aSeriesTitle;
    return aSeriesTitle;
}

bool TypeGroupConverter::isSingleSeriesTitle() const
{
    if (!mrModel.maSeries.empty() && (maTypeInfo.mbSingleSeriesVis || (mrModel.maSeries.size() == 1)) &&
        mrModel.maSeries.front()->mxText.is())
        return true;

    return false;
}

Reference< XCoordinateSystem > TypeGroupConverter::createCoordinateSystem()
{
    // create the coordinate system object
    Reference< css::uno::XComponentContext > xContext = getComponentContext();
    Reference< XCoordinateSystem > xCoordSystem;
    if( maTypeInfo.mbPolarCoordSystem )
    {
        if( mb3dChart )
            xCoordSystem = css::chart2::PolarCoordinateSystem3d::create(xContext);
        else
            xCoordSystem = css::chart2::PolarCoordinateSystem2d::create(xContext);
    }
    else
    {
        if( mb3dChart )
            xCoordSystem = css::chart2::CartesianCoordinateSystem3d::create(xContext);
        else
            xCoordSystem = css::chart2::CartesianCoordinateSystem2d::create(xContext);
    }

    // swap X and Y axis
    if( maTypeInfo.mbSwappedAxesSet )
    {
        PropertySet aPropSet( xCoordSystem );
        aPropSet.setProperty( PROP_SwapXAndYAxis, true );
    }

    return xCoordSystem;
}

Reference< XLabeledDataSequence > TypeGroupConverter::createCategorySequence()
{
    sal_Int32 nMaxValues = 0;
    Reference< XLabeledDataSequence > xLabeledSeq;
    /*  Find first existing category sequence. The behaviour of Excel 2007 is
        different to Excel 2003, which always used the category sequence of the
        first series, even if it was empty. */
    for (auto const& elem : mrModel.maSeries)
    {
        if( elem->maSources.has( DataSourceType::CATEGORIES ) )
        {
            SeriesConverter aSeriesConv(*this, *elem);
            xLabeledSeq = aSeriesConv.createCategorySequence( u"categories"_ustr );
            if (xLabeledSeq.is())
                break;
        }
        else if( nMaxValues <= 0 && elem->maSources.has( DataSourceType::VALUES ) )
        {
            DataSourceModel *pValues = elem->maSources.get( DataSourceType::VALUES ).get();
            if( pValues->mxDataSeq.is() )
                nMaxValues = pValues->mxDataSeq->maData.size();
        }
    }
    /* n#839727 Create Category Sequence when none are found */
    if( !xLabeledSeq.is() && !mrModel.maSeries.empty() ) {
        if( nMaxValues < 0 )
            nMaxValues = 2;
        typedef RefVector<SeriesModel> SeriesModelVector;
        SeriesModelVector::value_type aModel = mrModel.maSeries.get(0);
        if (!aModel->maSources.has(DataSourceType::CATEGORIES))
        {
            DataSourceModel &aSrc = aModel->maSources.create( DataSourceType::CATEGORIES );
            DataSequenceModel &aSeq = aSrc.mxDataSeq.create();
            aSeq.mnPointCount = nMaxValues;
            for( sal_Int32 i = 0; i < nMaxValues; i++ )
                aSeq.maData[ i ] <<= OUString::number( i + 1 );
        }
        SeriesConverter aSeriesConv( *this,  *aModel );
        xLabeledSeq = aSeriesConv.createCategorySequence( u"categories"_ustr );
    }
    return xLabeledSeq;
}

void TypeGroupConverter::convertFromModel( const Reference< XDiagram >& rxDiagram,
        const Reference< XCoordinateSystem >& rxCoordSystem,
        sal_Int32 nAxesSetIdx,
        bool bSupportsVaryColorsByPoint )
{
    try
    {
        // create the chart type object
        OUString aService = OUString::createFromAscii( maTypeInfo.mpcServiceName );
        Reference< XChartType > xChartType( createInstance( aService ), UNO_QUERY_THROW );

        Reference< XChartTypeContainer > xChartTypeContOld( rxCoordSystem, UNO_QUERY_THROW );
        Sequence< Reference< XChartType > > xOldChartTypes( xChartTypeContOld->getChartTypes() );
        sal_Int32 nOldChartTypeIdx = -1;

        // additional properties
        PropertySet aDiaProp( rxDiagram );
        PropertySet aTypeProp( xChartType );
        switch( maTypeInfo.meTypeCategory )
        {
            case TYPECATEGORY_BAR:
            {
                Sequence< sal_Int32 > aInt32Seq{ mrModel.mnOverlap, mrModel.mnOverlap };
                aTypeProp.setProperty( PROP_OverlapSequence, aInt32Seq );
                aInt32Seq = { mrModel.mnGapWidth, mrModel.mnGapWidth };
                aTypeProp.setProperty( PROP_GapwidthSequence, aInt32Seq );
            }
            break;
            case TYPECATEGORY_PIE:
            {
                aTypeProp.setProperty( PROP_UseRings, maTypeInfo.meTypeId == TYPEID_DOUGHNUT );
                /*  #i85166# starting angle of first pie slice. 3D pie charts
                    use Y rotation setting in view3D element. Of-pie charts do
                    not support pie rotation. */
                if( !is3dChart() && (maTypeInfo.meTypeId != TYPEID_OFPIE) )
                    convertPieRotation( aDiaProp, mrModel.mnFirstAngle );

                if (maTypeInfo.meTypeId == TYPEID_OFPIE) {
                    aDiaProp.setProperty(PROP_SubPieType,
                            convertOfPieType(mrModel.mnOfPieType));
                    if (mrModel.mnSplitType == XML_auto ||
                            mrModel.mnSplitType == XML_pos) {
                        aDiaProp.setProperty(PROP_SplitPos, mrModel.mfSplitPos);
                    }
                } else {
                    aDiaProp.setProperty(PROP_SubPieType, PieChartSubType_NONE);
                }
            }
            break;
            default:;
        }

        // create converter objects for all series models
        typedef RefVector< SeriesConverter > SeriesConvVector;
        SeriesConvVector aSeries;
        for (auto const& elemSeries : mrModel.maSeries)
            aSeries.push_back( std::make_shared<SeriesConverter>(*this, *elemSeries) );

        // decide whether to use varying colors for each data point
        bool bVaryColorsByPoint = bSupportsVaryColorsByPoint && mrModel.mbVaryColors;
        switch( maTypeInfo.meVarPointMode )
        {
            case VARPOINTMODE_NONE:     bVaryColorsByPoint = false;                             break;
            case VARPOINTMODE_SINGLE:   bVaryColorsByPoint &= (mrModel.maSeries.size() == 1);   break;
            case VARPOINTMODE_MULTI:                                                            break;
        }

        /*  Stock chart needs special processing. Create one 'big' series with
            data sequences of different roles. */
        if( maTypeInfo.meTypeId == TYPEID_STOCK )
        {
            // create the data series object
            Reference< XDataSeries > xDataSeries( createInstance( u"com.sun.star.chart2.DataSeries"_ustr ), UNO_QUERY );
            Reference< XDataSink > xDataSink( xDataSeries, UNO_QUERY );
            if( xDataSink.is() )
            {
                // create a list of data sequences from all series
                ::std::vector< Reference< XLabeledDataSequence > > aLabeledSeqVec;
                OSL_ENSURE( aSeries.size() >= 3, "TypeGroupConverter::convertFromModel - too few stock chart series" );
                int nRoleIdx = (aSeries.size() == 3) ? 1 : 0;
                for( auto& rxSeriesConv : aSeries )
                {
                    // create a data sequence with a specific role
                    OUString aRole;
                    switch( nRoleIdx )
                    {
                        case 0: aRole = "values-first";  break;
                        case 1: aRole = "values-max";    break;
                        case 2: aRole = "values-min";    break;
                        case 3: aRole = "values-last";   break;
                    }
                    Reference< XLabeledDataSequence > xDataSeq = rxSeriesConv->createValueSequence( aRole );
                    if( xDataSeq.is() )
                        aLabeledSeqVec.push_back( xDataSeq );

                    ++nRoleIdx;
                    if (nRoleIdx >= 4)
                        break;
                }

                // attach labeled data sequences to series and insert series into chart type
                xDataSink->setData( comphelper::containerToSequence( aLabeledSeqVec ) );

                // formatting of high/low lines
                aTypeProp.setProperty( PROP_ShowHighLow, true );
                PropertySet aSeriesProp( xDataSeries );
                if( mrModel.mxHiLowLines.is() )
                    getFormatter().convertFrameFormatting( aSeriesProp, mrModel.mxHiLowLines, OBJECTTYPE_HILOLINE );
                else
                    // hi/low-lines cannot be switched off via "ShowHighLow" property (?)
                    aSeriesProp.setProperty( PROP_LineStyle, css::drawing::LineStyle_NONE );

                // formatting of up/down bars
                bool bUpDownBars = mrModel.mxUpDownBars.is();
                aTypeProp.setProperty( PROP_Japanese, bUpDownBars );
                aTypeProp.setProperty( PROP_ShowFirst, bUpDownBars );
                if( bUpDownBars )
                {
                    UpDownBarsConverter aUpDownConv( *this, *mrModel.mxUpDownBars );
                    aUpDownConv.convertFromModel( xChartType );
                }

                // insert the series into the chart type object
                insertDataSeries( xChartType, xDataSeries, nAxesSetIdx );
            }
        }
        else
        {
            for( sal_Int32 nCTIdx=0; nCTIdx<xOldChartTypes.getLength(); ++nCTIdx )
            {
                if ( xChartType->getChartType() == xOldChartTypes[nCTIdx]->getChartType() )
                {
                    nOldChartTypeIdx = nCTIdx;
                }
            }

            for (auto const& elem : aSeries)
            {
                SeriesConverter& rSeriesConv = *elem;
                Reference< XDataSeries > xDataSeries = rSeriesConv.createDataSeries( *this, bVaryColorsByPoint );
                insertDataSeries( nOldChartTypeIdx == -1 ? xChartType : xOldChartTypes[nOldChartTypeIdx], xDataSeries, nAxesSetIdx );

                /*  Excel does not use the value of the c:smooth element of the
                    chart type to set a default line smoothing for the data
                    series. Line smoothing is always controlled by the c:smooth
                    element of the respective data series. If the element in the
                    data series is missing, line smoothing is off, regardless of
                    the c:smooth element of the chart type. */
#if !OOX_CHART_SMOOTHED_PER_SERIES
                if( rSeriesConv.getModel().mbSmooth )
                    convertLineSmooth( aTypeProp, true );
#endif
            }
        }

        // add chart type object to coordinate system
        Reference< XChartTypeContainer > xChartTypeCont( rxCoordSystem, UNO_QUERY_THROW );
        if (nOldChartTypeIdx == -1)
        {
            xChartTypeCont->addChartType(xChartType);
        }

        // set existence of bar connector lines at diagram (only in stacked 2D bar charts)
        if( mrModel.mxSerLines.is() && !mb3dChart && (maTypeInfo.meTypeCategory == TYPECATEGORY_BAR) && (isStacked() || isPercent()) )
            aDiaProp.setProperty( PROP_ConnectBars, true );
    }
    catch( Exception& )
    {
        OSL_FAIL( "TypeGroupConverter::convertFromModel - cannot add chart type" );
    }
}

void TypeGroupConverter::convertMarker( PropertySet& rPropSet, sal_Int32 nOoxSymbol, sal_Int32 nOoxSize,
       const ModelRef< Shape >& xShapeProps ) const
{
    if( isSeriesFrameFormat() )
        return;

    namespace cssc = css::chart2;

    // symbol style
    cssc::Symbol aSymbol;
    aSymbol.Style = cssc::SymbolStyle_STANDARD;
    switch( nOoxSymbol ) // compare with XclChPropSetHelper::WriteMarkerProperties in xlchart.cxx
    {
        case XML_auto:      aSymbol.Style = cssc::SymbolStyle_AUTO; break;
        case XML_none:      aSymbol.Style = cssc::SymbolStyle_NONE; break;
        case XML_square:    aSymbol.StandardSymbol = 0;             break;  // square
        case XML_diamond:   aSymbol.StandardSymbol = 1;             break;  // diamond
        case XML_triangle:  aSymbol.StandardSymbol = 3;             break;  // arrow up
        case XML_x:         aSymbol.StandardSymbol = 10;            break;  // X, legacy bow tie
        case XML_star:      aSymbol.StandardSymbol = 12;            break;  // asterisk, legacy sand glass
        case XML_dot:       aSymbol.StandardSymbol = 4;             break;  // arrow right
        case XML_dash:      aSymbol.StandardSymbol = 13;            break;  // horizontal bar, legacy arrow down
        case XML_circle:    aSymbol.StandardSymbol = 8;             break;  // circle, legacy arrow right
        case XML_plus:      aSymbol.StandardSymbol = 11;            break;  // plus, legacy arrow left
    }

    // symbol size (points in OOXML, 1/100 mm in Chart2)
    sal_Int32 nSize = convertPointToMm100(nOoxSize);
    aSymbol.Size.Width = aSymbol.Size.Height = nSize;

    if(xShapeProps.is())
    {
        Color aFillColor = xShapeProps->getFillProperties().maFillColor;
        aSymbol.FillColor = sal_Int32(aFillColor.getColor(getFilter().getGraphicHelper()));
        // tdf#124817: if there is no fill color, use line color of the symbol
        if( aSymbol.FillColor < 0 )
        {
            Color aLineColor = xShapeProps->getLineProperties().maLineFill.maFillColor;
            aSymbol.BorderColor = sal_Int32(aLineColor.getColor(getFilter().getGraphicHelper()));
            rPropSet.setProperty(PROP_Color, aSymbol.BorderColor);
        }
        else
            rPropSet.setProperty(PROP_Color, aSymbol.FillColor);
    }

    // set the property
    rPropSet.setProperty( PROP_Symbol, aSymbol );
}

void TypeGroupConverter::convertLineSmooth( PropertySet& rPropSet, bool bOoxSmooth ) const
{
    if( !isSeriesFrameFormat() && (maTypeInfo.meTypeCategory != TYPECATEGORY_RADAR) )
    {
        namespace cssc = css::chart2;
        cssc::CurveStyle eCurveStyle = bOoxSmooth ? cssc::CurveStyle_CUBIC_SPLINES : cssc::CurveStyle_LINES;
        rPropSet.setProperty( PROP_CurveStyle, eCurveStyle );
    }
}

void TypeGroupConverter::convertBarGeometry( PropertySet& rPropSet, sal_Int32 nOoxShape ) const
{
    if( !(mb3dChart && (maTypeInfo.meTypeCategory == TYPECATEGORY_BAR)) )
        return;

    namespace cssc = css::chart2;

    sal_Int32 nGeom3d = cssc::DataPointGeometry3D::CUBOID;
    switch( nOoxShape )
    {
        case XML_box:           nGeom3d = cssc::DataPointGeometry3D::CUBOID;    break;
        case XML_cone:          nGeom3d = cssc::DataPointGeometry3D::CONE;      break;
        case XML_coneToMax:     nGeom3d = cssc::DataPointGeometry3D::CONE;      break;
        case XML_cylinder:      nGeom3d = cssc::DataPointGeometry3D::CYLINDER;  break;
        case XML_pyramid:       nGeom3d = cssc::DataPointGeometry3D::PYRAMID;   break;
        case XML_pyramidToMax:  nGeom3d = cssc::DataPointGeometry3D::PYRAMID;   break;
        default:                OSL_FAIL( "TypeGroupConverter::convertBarGeometry - unknown 3D bar shape type" );
    }
    rPropSet.setProperty( PROP_Geometry3D, nGeom3d );
}

void TypeGroupConverter::convertPieRotation( PropertySet& rPropSet, sal_Int32 nOoxAngle ) const
{
    if( maTypeInfo.meTypeCategory == TYPECATEGORY_PIE )
    {
        // map OOXML [0,360] clockwise (0deg top) to Chart2 counterclockwise (0deg left)
        sal_Int32 nAngle = (450 - nOoxAngle) % 360;
        rPropSet.setProperty( PROP_StartingAngle, nAngle );
    }
}

void TypeGroupConverter::convertPieExplosion( PropertySet& rPropSet, sal_Int32 nOoxExplosion ) const
{
    if( maTypeInfo.meTypeCategory == TYPECATEGORY_PIE )
    {
        // pie explosion restricted to 100% in Chart2, set as double in range [0,1]
        double fOffset = getLimitedValue< double >( nOoxExplosion / 100.0, 0.0, 1.0 );
        rPropSet.setProperty( PROP_Offset, fOffset );
    }
}

PieChartSubType TypeGroupConverter::convertOfPieType(sal_Int32 nOoxOfPieType ) const
{
    if( maTypeInfo.meTypeCategory == TYPECATEGORY_PIE ) {
        switch (nOoxOfPieType) {
        case XML_pie:
            return PieChartSubType_PIE;
            break;
        case XML_bar:
            return PieChartSubType_BAR;
            break;
        default:
            OSL_FAIL( "TypeGroupConverter::convertOfPieType - unknown of-pie type" );
            return PieChartSubType_NONE;
        }
    } else {
        return PieChartSubType_NONE;
    }
}

void TypeGroupConverter::moveDataToSeries(DataSourceCxModel::DataMap& raDataMap)
{
    // For chartex, move data from rDataMap to the appropriate series. In
    // chartex the data is given outside the series, in a child element of
    // <cx:chartSpace>. Pre-2016 charts have the data inside the series, and
    // SeriesModel and subsequent handling reflects this. So here we get the
    // data to the right place for processing.
    if (!raDataMap.empty()) {
        // should only happen for chartex
        for (auto const& elem : mrModel.maSeries) {
            // This ID must be present in the map
            assert(raDataMap.find(elem->mnDataId) != raDataMap.end());
            elem->maSources = *(raDataMap[elem->mnDataId]);
        }
    }
}

// private --------------------------------------------------------------------

void TypeGroupConverter::insertDataSeries( const Reference< XChartType >& rxChartType, const Reference< XDataSeries >& rxSeries, sal_Int32 nAxesSetIdx )
{
    if( !rxSeries.is() )
        return;

    PropertySet aSeriesProp( rxSeries );

    // series stacking mode
    namespace cssc = css::chart2;
    cssc::StackingDirection eStacking = cssc::StackingDirection_NO_STACKING;
    // stacked overrides deep-3d
    if( isStacked() || isPercent() )
        eStacking = cssc::StackingDirection_Y_STACKING;
    else if( isDeep3dChart() )
        eStacking = cssc::StackingDirection_Z_STACKING;
    aSeriesProp.setProperty( PROP_StackingDirection, eStacking );

    // additional series properties
    aSeriesProp.setProperty( PROP_AttachedAxisIndex, nAxesSetIdx );

    // insert series into container
    try
    {
        Reference< XDataSeriesContainer > xSeriesCont( rxChartType, UNO_QUERY_THROW );
        xSeriesCont->addDataSeries( rxSeries );
    }
    catch( Exception& )
    {
        OSL_FAIL( "TypeGroupConverter::insertDataSeries - cannot add data series" );
    }
}

} // namespace oox

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
