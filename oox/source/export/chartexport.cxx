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

#include <oox/token/namespaces.hxx>
#include <oox/token/properties.hxx>
#include <oox/token/tokens.hxx>
#include <oox/core/xmlfilterbase.hxx>
#include <oox/export/chartexport.hxx>
#include <oox/token/relationship.hxx>
#include <oox/export/utils.hxx>
#include <drawingml/chart/typegroupconverter.hxx>
#include <basegfx/utils/gradienttools.hxx>
#include <docmodel/uno/UnoGradientTools.hxx>

#include <cstdio>
#include <limits>

#include <com/sun/star/awt/Gradient2.hpp>
#include <com/sun/star/chart/XChartDocument.hpp>
#include <com/sun/star/chart/ChartLegendPosition.hpp>
#include <com/sun/star/chart/XTwoAxisXSupplier.hpp>
#include <com/sun/star/chart/XTwoAxisYSupplier.hpp>
#include <com/sun/star/chart/XAxisZSupplier.hpp>
#include <com/sun/star/chart/ChartDataRowSource.hpp>
#include <com/sun/star/chart/X3DDisplay.hpp>
#include <com/sun/star/chart/XStatisticDisplay.hpp>
#include <com/sun/star/chart/XSecondAxisTitleSupplier.hpp>
#include <com/sun/star/chart/ChartSymbolType.hpp>
#include <com/sun/star/chart/ChartAxisMarks.hpp>
#include <com/sun/star/chart/ChartAxisLabelPosition.hpp>
#include <com/sun/star/chart/ChartAxisPosition.hpp>
#include <com/sun/star/chart/ChartSolidType.hpp>
#include <com/sun/star/chart/DataLabelPlacement.hpp>
#include <com/sun/star/chart/ErrorBarStyle.hpp>
#include <com/sun/star/chart/MissingValueTreatment.hpp>
#include <com/sun/star/chart/XDiagramPositioning.hpp>
#include <com/sun/star/chart/TimeIncrement.hpp>
#include <com/sun/star/chart/TimeInterval.hpp>
#include <com/sun/star/chart/TimeUnit.hpp>

#include <com/sun/star/chart2/RelativePosition.hpp>
#include <com/sun/star/chart2/RelativeSize.hpp>
#include <com/sun/star/chart2/XChartDocument.hpp>
#include <com/sun/star/chart2/XDiagram.hpp>
#include <com/sun/star/chart2/XCoordinateSystemContainer.hpp>
#include <com/sun/star/chart2/XRegressionCurveContainer.hpp>
#include <com/sun/star/chart2/XChartTypeContainer.hpp>
#include <com/sun/star/chart2/XDataSeriesContainer.hpp>
#include <com/sun/star/chart2/DataPointLabel.hpp>
#include <com/sun/star/chart2/XDataPointCustomLabelField.hpp>
#include <com/sun/star/chart2/DataPointCustomLabelFieldType.hpp>
#include <com/sun/star/chart2/PieChartSubType.hpp>
#include <com/sun/star/chart2/Symbol.hpp>
#include <com/sun/star/chart2/data/XDataSource.hpp>
#include <com/sun/star/chart2/data/XDataProvider.hpp>
#include <com/sun/star/chart2/data/XTextualDataSequence.hpp>
#include <com/sun/star/chart2/data/XNumericalDataSequence.hpp>
#include <com/sun/star/chart2/data/XLabeledDataSequence.hpp>
#include <com/sun/star/chart2/XAnyDescriptionAccess.hpp>
#include <com/sun/star/chart2/AxisType.hpp>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <com/sun/star/awt/XBitmap.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/lang/XServiceName.hpp>

#include <com/sun/star/table/CellAddress.hpp>
#include <com/sun/star/sheet/XFormulaParser.hpp>
#include <com/sun/star/sheet/FormulaToken.hpp>
#include <com/sun/star/sheet/AddressConvention.hpp>

#include <com/sun/star/container/XNamed.hpp>
#include <com/sun/star/embed/XVisualObject.hpp>
#include <com/sun/star/embed/Aspects.hpp>

#include <comphelper/processfactory.hxx>
#include <comphelper/random.hxx>
#include <utility>
#include <xmloff/SchXMLSeriesHelper.hxx>
#include "ColorPropertySet.hxx"

#include <svl/numformat.hxx>
#include <svl/numuno.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <sal/log.hxx>

#include <set>
#include <unordered_set>

#include <frozen/bits/defines.h>
#include <frozen/bits/elsa_std.h>
#include <frozen/unordered_map.h>

#include <o3tl/temporary.hxx>
#include <o3tl/sorted_vector.hxx>

using namespace css;
using namespace css::uno;
using namespace css::drawing;
using namespace ::oox::core;
using css::beans::PropertyValue;
using css::beans::XPropertySet;
using css::container::XNamed;
using css::table::CellAddress;
using css::sheet::XFormulaParser;
using ::oox::core::XmlFilterBase;
using ::sax_fastparser::FSHelperPtr;

namespace cssc = css::chart;

namespace oox::drawingml {

namespace {

bool isPrimaryAxes(sal_Int32 nIndex)
{
    assert(nIndex == 0 || nIndex == 1);
    return nIndex != 1;
}

class lcl_MatchesRole
{
public:
    explicit lcl_MatchesRole( OUString aRole ) :
            m_aRole(std::move( aRole ))
    {}

    bool operator () ( const Reference< chart2::data::XLabeledDataSequence > & xSeq ) const
    {
        if( !xSeq.is() )
            return  false;
        Reference< beans::XPropertySet > xProp( xSeq->getValues(), uno::UNO_QUERY );
        OUString aRole;

        return ( xProp.is() &&
                 (xProp->getPropertyValue( u"Role"_ustr ) >>= aRole ) &&
                 m_aRole == aRole );
    }

private:
    OUString m_aRole;
};

void outputStyleEntry(FSHelperPtr pFS, sal_Int32 nElTokenId)
{
    // Just default values for now
    pFS->startElement(FSNS(XML_cs, nElTokenId));
    pFS->singleElement(FSNS(XML_cs, XML_lnRef), XML_idx, "0");
    pFS->singleElement(FSNS(XML_cs, XML_fillRef), XML_idx, "0");
    pFS->singleElement(FSNS(XML_cs, XML_effectRef), XML_idx, "0");
    pFS->singleElement(FSNS(XML_cs, XML_fontRef), XML_idx, "minor");
    pFS->endElement(FSNS(XML_cs, nElTokenId));
}

void outputChartAreaStyleEntry(FSHelperPtr pFS)
{
    // Just default values for now
    pFS->startElement(FSNS(XML_cs, XML_chartArea), XML_mods, "allowNoFillOverride allowNoLineOverride");
    pFS->singleElement(FSNS(XML_cs, XML_lnRef), XML_idx, "0");
    pFS->singleElement(FSNS(XML_cs, XML_fillRef), XML_idx, "0");
    pFS->singleElement(FSNS(XML_cs, XML_effectRef), XML_idx, "0");

    pFS->startElement(FSNS(XML_cs, XML_fontRef), XML_idx, "minor");
    pFS->singleElement(FSNS(XML_a, XML_schemeClr), XML_val, "tx1");
    pFS->endElement(FSNS(XML_cs, XML_fontRef));

    pFS->startElement(FSNS(XML_cs, XML_spPr));

    pFS->startElement(FSNS(XML_a, XML_solidFill));
    pFS->singleElement(FSNS(XML_a, XML_schemeClr), XML_val, "bg1");
    pFS->endElement(FSNS(XML_a, XML_solidFill));

    pFS->startElement(FSNS(XML_a, XML_ln), XML_w, "9525", XML_cap, "flat",
            XML_cmpd, "sng", XML_algn, "ctr");
    pFS->startElement(FSNS(XML_a, XML_solidFill));
    pFS->startElement(FSNS(XML_a, XML_schemeClr), XML_val, "tx1");
    pFS->singleElement(FSNS(XML_a, XML_lumMod), XML_val, "15000");
    pFS->singleElement(FSNS(XML_a, XML_lumOff), XML_val, "85000");
    pFS->endElement(FSNS(XML_a, XML_schemeClr));
    pFS->endElement(FSNS(XML_a, XML_solidFill));
    pFS->singleElement(FSNS(XML_a, XML_round));
    pFS->endElement(FSNS(XML_a, XML_ln));

    pFS->endElement(FSNS(XML_cs, XML_spPr));

    pFS->endElement(FSNS(XML_cs, XML_chartArea));
}

void outputDataPointStyleEntry(FSHelperPtr pFS)
{
    pFS->startElement(FSNS(XML_cs, XML_dataPoint));
    pFS->singleElement(FSNS(XML_cs, XML_lnRef), XML_idx, "0");

    pFS->startElement(FSNS(XML_cs, XML_fillRef), XML_idx, "0");
    pFS->singleElement(FSNS(XML_cs, XML_styleClr), XML_val, "auto");
    pFS->endElement(FSNS(XML_cs, XML_fillRef));

    pFS->singleElement(FSNS(XML_cs, XML_effectRef), XML_idx, "0");

    pFS->startElement(FSNS(XML_cs, XML_fontRef), XML_idx, "minor");
    pFS->singleElement(FSNS(XML_cs, XML_schemeClr), XML_val, "tx1");
    pFS->endElement(FSNS(XML_cs, XML_fontRef));

    pFS->startElement(FSNS(XML_cs, XML_spPr));
    pFS->startElement(FSNS(XML_a, XML_solidFill));
    pFS->singleElement(FSNS(XML_a, XML_schemeClr), XML_val, "phClr");
    pFS->endElement(FSNS(XML_a, XML_solidFill));
    pFS->endElement(FSNS(XML_cs, XML_spPr));

    pFS->endElement(FSNS(XML_cs, XML_dataPoint));
}

std::vector<Sequence<Reference<chart2::XDataSeries> > > splitDataSeriesByAxis(const Reference< chart2::XChartType >& xChartType)
{
    std::vector<Sequence<Reference<chart2::XDataSeries> > > aSplitSeries;
    std::map<sal_Int32, size_t> aMapAxisToIndex;

    Reference< chart2::XDataSeriesContainer > xDSCnt(xChartType, uno::UNO_QUERY);
    if (xDSCnt.is())
    {
        sal_Int32 nAxisIndexOfFirstSeries = -1;
        const Sequence< Reference< chart2::XDataSeries > > aSeriesSeq(xDSCnt->getDataSeries());
        for (const uno::Reference<chart2::XDataSeries>& xSeries : aSeriesSeq)
        {
            Reference<beans::XPropertySet> xPropSet(xSeries, uno::UNO_QUERY);
            if (!xPropSet.is())
                continue;

            sal_Int32 nAxisIndex = -1;
            uno::Any aAny = xPropSet->getPropertyValue(u"AttachedAxisIndex"_ustr);
            aAny >>= nAxisIndex;
            size_t nVectorPos = 0;
            if (nAxisIndexOfFirstSeries == -1)
            {
                nAxisIndexOfFirstSeries = nAxisIndex;
            }

            auto it = aMapAxisToIndex.find(nAxisIndex);
            if (it == aMapAxisToIndex.end())
            {
                aSplitSeries.emplace_back();
                nVectorPos = aSplitSeries.size() - 1;
                aMapAxisToIndex.insert(std::pair<sal_Int32, size_t>(nAxisIndex, nVectorPos));
            }
            else
            {
                nVectorPos = it->second;
            }

            uno::Sequence<Reference<chart2::XDataSeries> >& rAxisSeriesSeq = aSplitSeries[nVectorPos];
            sal_Int32 nLength = rAxisSeriesSeq.getLength();
            rAxisSeriesSeq.realloc(nLength + 1);
            rAxisSeriesSeq.getArray()[nLength] = xSeries;
        }
        // if the first series attached to secondary axis, then export those series first, which are attached to primary axis
        // also the MS Office export every time in this order
        if (aSplitSeries.size() > 1 && nAxisIndexOfFirstSeries == 1)
        {
            std::swap(aSplitSeries[0], aSplitSeries[1]);
        }
    }

    return aSplitSeries;
}

}   // unnamed namespace

static Reference< chart2::data::XLabeledDataSequence > lcl_getCategories(
        const Reference< chart2::XDiagram > & xDiagram, bool *pbHasDateCategories )
{
    *pbHasDateCategories = false;
    Reference< chart2::data::XLabeledDataSequence >  xResult;
    try
    {
        Reference< chart2::XCoordinateSystemContainer > xCooSysCnt(
            xDiagram, uno::UNO_QUERY_THROW );
        const Sequence< Reference< chart2::XCoordinateSystem > > aCooSysSeq(
            xCooSysCnt->getCoordinateSystems());
        for( const auto& xCooSys : aCooSysSeq )
        {
            OSL_ASSERT( xCooSys.is());
            for( sal_Int32 nN = xCooSys->getDimension(); nN--; )
            {
                const sal_Int32 nMaxAxisIndex = xCooSys->getMaximumAxisIndexByDimension(nN);
                for(sal_Int32 nI=0; nI<=nMaxAxisIndex; ++nI)
                {
                    Reference< chart2::XAxis > xAxis = xCooSys->getAxisByDimension( nN, nI );
                    OSL_ASSERT( xAxis.is());
                    if( xAxis.is())
                    {
                        chart2::ScaleData aScaleData = xAxis->getScaleData();
                        if( aScaleData.Categories.is())
                        {
                            *pbHasDateCategories = aScaleData.AxisType == chart2::AxisType::DATE;
                            xResult.set( aScaleData.Categories );
                            break;
                        }
                    }
                }
            }
        }
    }
    catch( const uno::Exception & )
    {
        DBG_UNHANDLED_EXCEPTION("oox");
    }

    return xResult;
}

static Reference< chart2::data::XLabeledDataSequence >
    lcl_getDataSequenceByRole(
        const Sequence< Reference< chart2::data::XLabeledDataSequence > > & aLabeledSeq,
        const OUString & rRole )
{
    Reference< chart2::data::XLabeledDataSequence > aNoResult;

    const Reference< chart2::data::XLabeledDataSequence > * pBegin = aLabeledSeq.getConstArray();
    const Reference< chart2::data::XLabeledDataSequence > * pEnd = pBegin + aLabeledSeq.getLength();
    const Reference< chart2::data::XLabeledDataSequence > * pMatch =
        ::std::find_if( pBegin, pEnd, lcl_MatchesRole( rRole ));

    if( pMatch != pEnd )
        return *pMatch;

    return aNoResult;
}

static bool lcl_hasCategoryLabels( const Reference< chart2::XChartDocument >& xChartDoc )
{
    //categories are always the first sequence
    Reference< chart2::XDiagram > xDiagram( xChartDoc->getFirstDiagram());
    bool bDateCategories;
    Reference< chart2::data::XLabeledDataSequence > xCategories(
            lcl_getCategories( xDiagram, &bDateCategories ) );
    return xCategories.is();
}

static bool lcl_isCategoryAxisShifted( const Reference< chart2::XDiagram >& xDiagram )
{
    bool bCategoryPositionShifted = false;
    try
    {
        Reference< chart2::XCoordinateSystemContainer > xCooSysCnt(
            xDiagram, uno::UNO_QUERY_THROW);
        const Sequence< Reference< chart2::XCoordinateSystem > > aCooSysSeq(
            xCooSysCnt->getCoordinateSystems());
        for (const auto& xCooSys : aCooSysSeq)
        {
            OSL_ASSERT(xCooSys.is());
            if( 0 < xCooSys->getDimension() && 0 <= xCooSys->getMaximumAxisIndexByDimension(0) )
            {
                Reference< chart2::XAxis > xAxis = xCooSys->getAxisByDimension(0, 0);
                OSL_ASSERT(xAxis.is());
                if (xAxis.is())
                {
                    chart2::ScaleData aScaleData = xAxis->getScaleData();
                    bCategoryPositionShifted = aScaleData.ShiftedCategoryPosition;
                    break;
                }
            }
        }
    }
    catch (const uno::Exception&)
    {
        DBG_UNHANDLED_EXCEPTION("oox");
    }

    return bCategoryPositionShifted;
}

static sal_Int32 lcl_getCategoryAxisType( const Reference< chart2::XDiagram >& xDiagram, sal_Int32 nDimensionIndex, sal_Int32 nAxisIndex )
{
    sal_Int32 nAxisType = -1;
    try
    {
        Reference< chart2::XCoordinateSystemContainer > xCooSysCnt(
            xDiagram, uno::UNO_QUERY_THROW);
        const Sequence< Reference< chart2::XCoordinateSystem > > aCooSysSeq(
            xCooSysCnt->getCoordinateSystems());
        for( const auto& xCooSys : aCooSysSeq )
        {
            OSL_ASSERT(xCooSys.is());
            if( nDimensionIndex < xCooSys->getDimension() && nAxisIndex <= xCooSys->getMaximumAxisIndexByDimension(nDimensionIndex) )
            {
                Reference< chart2::XAxis > xAxis = xCooSys->getAxisByDimension(nDimensionIndex, nAxisIndex);
                OSL_ASSERT(xAxis.is());
                if( xAxis.is() )
                {
                    chart2::ScaleData aScaleData = xAxis->getScaleData();
                    nAxisType = aScaleData.AxisType;
                    break;
                }
            }
        }
    }
    catch (const uno::Exception&)
    {
        DBG_UNHANDLED_EXCEPTION("oox");
    }

    return nAxisType;
}

static OUString lclGetTimeUnitToken( sal_Int32 nTimeUnit )
{
    switch( nTimeUnit )
    {
        case cssc::TimeUnit::DAY:      return u"days"_ustr;
        case cssc::TimeUnit::MONTH:    return u"months"_ustr;
        case cssc::TimeUnit::YEAR:     return u"years"_ustr;
        default:                       OSL_ENSURE(false, "lclGetTimeUnitToken - unexpected time unit");
    }
    return u"days"_ustr;
}

static cssc::TimeIncrement lcl_getDateTimeIncrement( const Reference< chart2::XDiagram >& xDiagram, sal_Int32 nAxisIndex )
{
    cssc::TimeIncrement aTimeIncrement;
    try
    {
        Reference< chart2::XCoordinateSystemContainer > xCooSysCnt(
            xDiagram, uno::UNO_QUERY_THROW);
        const Sequence< Reference< chart2::XCoordinateSystem > > aCooSysSeq(
            xCooSysCnt->getCoordinateSystems());
        for( const auto& xCooSys : aCooSysSeq )
        {
            OSL_ASSERT(xCooSys.is());
            if( 0 < xCooSys->getDimension() && nAxisIndex <= xCooSys->getMaximumAxisIndexByDimension(0) )
            {
                Reference< chart2::XAxis > xAxis = xCooSys->getAxisByDimension(0, nAxisIndex);
                OSL_ASSERT(xAxis.is());
                if( xAxis.is() )
                {
                    chart2::ScaleData aScaleData = xAxis->getScaleData();
                    aTimeIncrement = aScaleData.TimeIncrement;
                    break;
                }
            }
        }
    }
    catch (const uno::Exception&)
    {
        DBG_UNHANDLED_EXCEPTION("oox");
    }

    return aTimeIncrement;
}

static bool lcl_isSeriesAttachedToFirstAxis(
    const Reference< chart2::XDataSeries > & xDataSeries )
{
    bool bResult=true;

    try
    {
        sal_Int32 nAxisIndex = 0;
        Reference< beans::XPropertySet > xProp( xDataSeries, uno::UNO_QUERY_THROW );
        xProp->getPropertyValue(u"AttachedAxisIndex"_ustr) >>= nAxisIndex;
        bResult = (0==nAxisIndex);
    }
    catch( const uno::Exception & )
    {
        DBG_UNHANDLED_EXCEPTION("oox");
    }

    return bResult;
}

static OUString lcl_flattenStringSequence( const Sequence< OUString > & rSequence )
{
    OUStringBuffer aResult;
    bool bPrecedeWithSpace = false;
    for( const auto& rString : rSequence )
    {
        if( !rString.isEmpty())
        {
            if( bPrecedeWithSpace )
                aResult.append( ' ' );
            aResult.append( rString );
            bPrecedeWithSpace = true;
        }
    }
    return aResult.makeStringAndClear();
}

static void lcl_writeChartexString(const FSHelperPtr& pFS, std::u16string_view sOut)
{
    pFS->startElement(FSNS(XML_cx, XML_tx));
    // cell range doesn't seem to be supported in chartex?
    // TODO: also handle <cx:rich>
    pFS->startElement(FSNS(XML_cx, XML_txData));
    // TODO: also handle <cx:f> <cx:v>
    pFS->startElement(FSNS(XML_cx, XML_v));
    pFS->writeEscaped(sOut);
    pFS->endElement( FSNS( XML_cx, XML_v ) );
    pFS->endElement( FSNS( XML_cx, XML_txData ) );
    pFS->endElement( FSNS( XML_cx, XML_tx ) );
}

static Sequence< OUString > lcl_getLabelSequence( const Reference< chart2::data::XDataSequence > & xLabelSeq )
{
    Sequence< OUString > aLabels;

    uno::Reference< chart2::data::XTextualDataSequence > xTextualDataSequence( xLabelSeq, uno::UNO_QUERY );
    if( xTextualDataSequence.is())
    {
        aLabels = xTextualDataSequence->getTextualData();
    }
    else if( xLabelSeq.is())
    {
        const Sequence< uno::Any > aAnies( xLabelSeq->getData());
        aLabels.realloc( aAnies.getLength());
        auto pLabels = aLabels.getArray();
        for( sal_Int32 i=0; i<aAnies.getLength(); ++i )
            aAnies[i] >>= pLabels[i];
    }

    return aLabels;
}

static void lcl_fillCategoriesIntoStringVector(
    const Reference< chart2::data::XDataSequence > & xCategories,
    ::std::vector< OUString > & rOutCategories )
{
    OSL_ASSERT( xCategories.is());
    if( !xCategories.is())
        return;
    Reference< chart2::data::XTextualDataSequence > xTextualDataSequence( xCategories, uno::UNO_QUERY );
    if( xTextualDataSequence.is())
    {
        rOutCategories.clear();
        const Sequence< OUString > aTextData( xTextualDataSequence->getTextualData());
        rOutCategories.insert( rOutCategories.end(), aTextData.begin(), aTextData.end() );
    }
    else
    {
        Sequence< uno::Any > aAnies( xCategories->getData());
        rOutCategories.resize( aAnies.getLength());
        for( sal_Int32 i=0; i<aAnies.getLength(); ++i )
            aAnies[i] >>= rOutCategories[i];
    }
}

static ::std::vector< double > lcl_getAllValuesFromSequence( const Reference< chart2::data::XDataSequence > & xSeq )
{
    ::std::vector< double > aResult;

    Reference< chart2::data::XNumericalDataSequence > xNumSeq( xSeq, uno::UNO_QUERY );
    if( xNumSeq.is())
    {
        const Sequence< double > aValues( xNumSeq->getNumericalData());
        aResult.insert( aResult.end(), aValues.begin(), aValues.end() );
    }
    else if( xSeq.is())
    {
        Sequence< uno::Any > aAnies( xSeq->getData());
        aResult.resize( aAnies.getLength(), std::numeric_limits<double>::quiet_NaN() );
        for( sal_Int32 i=0; i<aAnies.getLength(); ++i )
            aAnies[i] >>= aResult[i];
    }
    return aResult;
}

namespace
{

constexpr auto constChartTypeMap = frozen::make_unordered_map<std::u16string_view, chart::TypeId>(
{
    { u"com.sun.star.chart.BarDiagram", chart::TYPEID_BAR },
    { u"com.sun.star.chart2.ColumnChartType",  chart::TYPEID_BAR },
    { u"com.sun.star.chart.AreaDiagram",  chart::TYPEID_AREA },
    { u"com.sun.star.chart2.AreaChartType",  chart::TYPEID_AREA },
    { u"com.sun.star.chart.LineDiagram",  chart::TYPEID_LINE },
    { u"com.sun.star.chart2.LineChartType",  chart::TYPEID_LINE },
    { u"com.sun.star.chart.PieDiagram",  chart::TYPEID_PIE },
    { u"com.sun.star.chart2.PieChartType",  chart::TYPEID_PIE },
    { u"com.sun.star.chart.DonutDiagram",  chart::TYPEID_DOUGHNUT },
    { u"com.sun.star.chart2.DonutChartType",  chart::TYPEID_DOUGHNUT },
    { u"com.sun.star.chart.XYDiagram",  chart::TYPEID_SCATTER },
    { u"com.sun.star.chart2.ScatterChartType",  chart::TYPEID_SCATTER },
    { u"com.sun.star.chart.NetDiagram",  chart::TYPEID_RADARLINE },
    { u"com.sun.star.chart2.NetChartType",  chart::TYPEID_RADARLINE },
    { u"com.sun.star.chart.FilledNetDiagram",  chart::TYPEID_RADARAREA },
    { u"com.sun.star.chart2.FilledNetChartType",  chart::TYPEID_RADARAREA },
    { u"com.sun.star.chart.StockDiagram",  chart::TYPEID_STOCK },
    { u"com.sun.star.chart2.CandleStickChartType",  chart::TYPEID_STOCK },
    { u"com.sun.star.chart.BubbleDiagram",  chart::TYPEID_BUBBLE },
    { u"com.sun.star.chart2.BubbleChartType",  chart::TYPEID_BUBBLE },
    { u"com.sun.star.chart.FunnelDiagram",  chart::TYPEID_FUNNEL },
    { u"com.sun.star.chart2.FunnelChartType",  chart::TYPEID_FUNNEL },
});

} // end anonymous namespace

static sal_Int32 lcl_getChartType(std::u16string_view sChartType)
{
    auto aIterator = constChartTypeMap.find(sChartType);
    if (aIterator == constChartTypeMap.end())
        return chart::TYPEID_UNKNOWN;
    return aIterator->second;
}

static sal_Int32 lcl_generateRandomValue()
{
    return comphelper::rng::uniform_int_distribution(0, 100000000-1);
}

bool DataLabelsRange::empty() const
{
    return maLabels.empty();
}

size_t DataLabelsRange::count() const
{
    return maLabels.size();
}

bool DataLabelsRange::hasLabel(sal_Int32 nIndex) const
{
    return maLabels.find(nIndex) != maLabels.end();
}

const OUString & DataLabelsRange::getRange() const
{
    return maRange;
}

void DataLabelsRange::setRange(const OUString& rRange)
{
    maRange = rRange;
}

void DataLabelsRange::setLabel(sal_Int32 nIndex, const OUString& rText)
{
    maLabels.emplace(nIndex, rText);
}

DataLabelsRange::LabelsRangeMap::const_iterator DataLabelsRange::begin() const
{
    return maLabels.begin();
}

DataLabelsRange::LabelsRangeMap::const_iterator DataLabelsRange::end() const
{
    return maLabels.end();
}

ChartExport::ChartExport( sal_Int32 nXmlNamespace, FSHelperPtr pFS, Reference< frame::XModel > const & xModel, XmlFilterBase* pFB, DocumentType eDocumentType )
    : DrawingML( std::move(pFS), pFB, eDocumentType )
    , mnXmlNamespace( nXmlNamespace )
    , mnSeriesCount(0)
    , mxChartModel( xModel )
    , mpURLTransformer(std::make_shared<URLTransformer>())
    , mbHasCategoryLabels( false )
    , mbHasZAxis( false )
    , mbIs3DChart( false )
    , mbStacked(false)
    , mbPercent(false)
    , mbHasDateCategories(false)
{
}

void ChartExport::SetURLTranslator(const std::shared_ptr<URLTransformer>& pTransformer)
{
    mpURLTransformer = pTransformer;
}

sal_Int32 ChartExport::getChartType( )
{
    OUString sChartType = mxDiagram->getDiagramType();
    return lcl_getChartType( sChartType );
}

namespace {

uno::Sequence< beans::PropertyValue > createArguments(
    const OUString & rRangeRepresentation, bool bUseColumns)
{
    css::chart::ChartDataRowSource eRowSource = css::chart::ChartDataRowSource_ROWS;
    if (bUseColumns)
        eRowSource = css::chart::ChartDataRowSource_COLUMNS;

    uno::Sequence<beans::PropertyValue> aArguments{
        { u"DataRowSource"_ustr, -1, uno::Any(eRowSource), beans::PropertyState_DIRECT_VALUE },
        { u"FirstCellAsLabel"_ustr, -1, uno::Any(false), beans::PropertyState_DIRECT_VALUE },
        { u"HasCategories"_ustr, -1, uno::Any(false), beans::PropertyState_DIRECT_VALUE },
        { u"CellRangeRepresentation"_ustr, -1, uno::Any(rRangeRepresentation),
          beans::PropertyState_DIRECT_VALUE }
    };

    return aArguments;
}

Reference<chart2::XDataSeries> getPrimaryDataSeries(const Reference<chart2::XChartType>& xChartType)
{
    Reference< chart2::XDataSeriesContainer > xDSCnt(xChartType, uno::UNO_QUERY_THROW);

    // export dataseries for current chart-type
    const Sequence< Reference< chart2::XDataSeries > > aSeriesSeq(xDSCnt->getDataSeries());
    for (const auto& rSeries : aSeriesSeq)
    {
        Reference<chart2::XDataSeries> xSource(rSeries, uno::UNO_QUERY);
        if (xSource.is())
            return xSource;
    }

    return Reference<chart2::XDataSeries>();
}

}

Sequence< Sequence< OUString > > ChartExport::getSplitCategoriesList( const OUString& rRange )
{
    Reference< chart2::XChartDocument > xChartDoc(getModel(), uno::UNO_QUERY);
    OSL_ASSERT(xChartDoc.is());
    if (xChartDoc.is())
    {
        Reference< chart2::data::XDataProvider > xDataProvider(xChartDoc->getDataProvider());
        OSL_ENSURE(xDataProvider.is(), "No DataProvider");
        if (xDataProvider.is())
        {
            //detect whether the first series is a row or a column
            bool bSeriesUsesColumns = true;
            Reference< chart2::XDiagram > xDiagram(xChartDoc->getFirstDiagram());
            try
            {
                Reference< chart2::XCoordinateSystemContainer > xCooSysCnt(xDiagram, uno::UNO_QUERY_THROW);
                const Sequence< Reference< chart2::XCoordinateSystem > > aCooSysSeq(xCooSysCnt->getCoordinateSystems());
                for (const auto& rCooSys : aCooSysSeq)
                {
                    const Reference< chart2::XChartTypeContainer > xCTCnt(rCooSys, uno::UNO_QUERY_THROW);
                    const Sequence< Reference< chart2::XChartType > > aChartTypeSeq(xCTCnt->getChartTypes());
                    for (const auto& rChartType : aChartTypeSeq)
                    {
                        Reference< chart2::XDataSeries > xDataSeries = getPrimaryDataSeries(rChartType);
                        if (xDataSeries.is())
                        {
                            uno::Reference< chart2::data::XDataSource > xSeriesSource(xDataSeries, uno::UNO_QUERY);
                            const uno::Sequence< beans::PropertyValue > rArguments = xDataProvider->detectArguments(xSeriesSource);
                            for (const beans::PropertyValue& rProperty : rArguments)
                            {
                                if (rProperty.Name == "DataRowSource")
                                {
                                    css::chart::ChartDataRowSource eRowSource;
                                    if (rProperty.Value >>= eRowSource)
                                    {
                                        bSeriesUsesColumns = (eRowSource == css::chart::ChartDataRowSource_COLUMNS);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            catch (const uno::Exception &)
            {
                DBG_UNHANDLED_EXCEPTION("chart2");
            }
            // detect we have an inner data table or not
            if (xChartDoc->hasInternalDataProvider() && rRange == "categories")
            {
                try
                {
                    css::uno::Reference< css::chart2::XAnyDescriptionAccess > xDataAccess(xChartDoc->getDataProvider(), uno::UNO_QUERY);
                    const Sequence< Sequence< uno::Any > >aAnyCategories(bSeriesUsesColumns ? xDataAccess->getAnyRowDescriptions() : xDataAccess->getAnyColumnDescriptions());
                    auto pMax = std::max_element(aAnyCategories.begin(), aAnyCategories.end(),
                        [](const Sequence<uno::Any>& a, const Sequence<uno::Any>& b) {
                            return a.getLength() < b.getLength(); });

                    //minimum is 1!
                    if (pMax != aAnyCategories.end() && pMax->getLength() > 1)
                    {
                        sal_Int32 nLevelCount = pMax->getLength();
                        //we have complex categories
                        //sort the categories name
                        Sequence<Sequence<OUString>>aFinalSplitSource(nLevelCount);
                        auto pFinalSplitSource = aFinalSplitSource.getArray();
                        for (sal_Int32 i = 0; i < nLevelCount; i++)
                        {
                            sal_Int32 nElemLabel = 0;
                            pFinalSplitSource[nLevelCount - i - 1].realloc(aAnyCategories.getLength());
                            auto pSeq = pFinalSplitSource[nLevelCount - i - 1].getArray();
                            for (auto const& elemLabel : aAnyCategories)
                            {
                                // make sure elemLabel[i] exists!
                                if (elemLabel.getLength() > i)
                                {
                                    pSeq[nElemLabel] = elemLabel[i].get<OUString>();
                                    nElemLabel++;
                                }
                            }
                        }
                        return aFinalSplitSource;
                    }
                }
                catch (const uno::Exception &)
                {
                    DBG_UNHANDLED_EXCEPTION("oox");
                }
            }
            else
            {
                try
                {
                    uno::Reference< chart2::data::XDataSource > xCategoriesSource(xDataProvider->createDataSource(
                        createArguments(rRange, bSeriesUsesColumns)));

                    if (xCategoriesSource.is())
                    {
                        const Sequence< Reference< chart2::data::XLabeledDataSequence >> aCategories = xCategoriesSource->getDataSequences();
                        if (aCategories.getLength() > 1)
                        {
                            //we have complex categories
                            //sort the categories name
                            Sequence<Sequence<OUString>> aFinalSplitSource(aCategories.getLength());
                            std::transform(aCategories.begin(), aCategories.end(),
                                std::reverse_iterator(asNonConstRange(aFinalSplitSource).end()),
                                [](const Reference<chart2::data::XLabeledDataSequence>& xCat) {
                                    return lcl_getLabelSequence(xCat->getValues()); });
                            return aFinalSplitSource;
                        }
                    }
                }
                catch (const uno::Exception &)
                {
                    DBG_UNHANDLED_EXCEPTION("oox");
                }
            }
        }
    }

    return Sequence< Sequence< OUString>>(0);
}

OUString ChartExport::parseFormula( const OUString& rRange )
{
    OUString aResult;
    Reference< XFormulaParser > xParser;
    uno::Reference< lang::XMultiServiceFactory > xSF = GetFB()->getModelFactory();
    if( xSF.is() )
    {
        try
        {
            xParser.set( xSF->createInstance(u"com.sun.star.sheet.FormulaParser"_ustr), UNO_QUERY );
        }
        catch( Exception& )
        {
        }
    }

    SAL_WARN_IF(!xParser.is(), "oox", "creating formula parser failed");

    if( xParser.is() )
    {
        Reference< XPropertySet > xParserProps( xParser, uno::UNO_QUERY );
        // rRange is the result of a
        // css::chart2::data::XDataSequence::getSourceRangeRepresentation()
        // call that returns the range in the document's current UI notation.
        // Creating a FormulaParser defaults to the same notation, for
        // parseFormula() do not attempt to override the FormulaConvention
        // property with css::sheet::AddressConvention::OOO or some such.
        /* TODO: it would be much better to introduce a
         * getSourceRangeRepresentation(css::sheet::AddressConvention) to
         * return the ranges in a specific convention than converting them with
         * the overhead of creating an XFormulaParser for each... */
        uno::Sequence<sheet::FormulaToken> aTokens = xParser->parseFormula( rRange, CellAddress( 0, 0, 0 ) );
        if( xParserProps.is() )
        {
            xParserProps->setPropertyValue(u"FormulaConvention"_ustr, uno::Any(css::sheet::AddressConvention::XL_OOX) );
            // For referencing named ranges correctly with special excel chart syntax.
            xParserProps->setPropertyValue(u"RefConventionChartOOXML"_ustr, uno::Any(true) );
        }
        aResult = xParser->printFormula( aTokens, CellAddress( 0, 0, 0 ) );
    }
    else
    {
        //FIXME: currently just using simple converter, e.g $Sheet1.$A$1:$C$1 -> Sheet1!$A$1:$C$1
        OUString aRange( rRange );
        if( aRange.startsWith("$") )
            aRange = aRange.copy(1);
        aRange = aRange.replaceAll(".$", "!$" );
        aResult = aRange;
    }

    return aResult;
}

void ChartExport::WriteChartObj( const Reference< XShape >& xShape, sal_Int32 nID, sal_Int32 nChartCount )
{
    FSHelperPtr pFS = GetFS();

    Reference< chart2::XChartDocument > xChartDoc( getModel(), uno::UNO_QUERY );
    OSL_ASSERT( xChartDoc.is() );
    if( !xChartDoc.is() )
        return;

    // We need to get the new diagram here so we can know if this is a chartex
    // chart.
    mxNewDiagram.set( xChartDoc->getFirstDiagram());

    const bool bIsChartex = isChartexNotChartNS();

    if (bIsChartex) {
        // Do the AlternateContent header
        mpFS->startElementNS(XML_mc, XML_AlternateContent, FSNS(XML_xmlns, XML_mc),
                "http://schemas.openxmlformats.org/markup-compatibility/2006");
        mpFS->startElementNS(XML_mc, XML_Choice,
                FSNS(XML_xmlns, XML_cx2), "http://schemas.microsoft.com/office/drawing/2015/10/21/chartex",
                XML_Requires, "cx2");
    }

    Reference< XPropertySet > xShapeProps( xShape, UNO_QUERY );

    pFS->startElementNS(mnXmlNamespace, XML_graphicFrame);

    pFS->startElementNS(mnXmlNamespace, XML_nvGraphicFramePr);

    // TODO: get the correct chart name chart id
    OUString sName = u"Object 1"_ustr;
    Reference< XNamed > xNamed( xShape, UNO_QUERY );
    if (xNamed.is())
        sName = xNamed->getName();

    pFS->startElementNS( mnXmlNamespace, XML_cNvPr,
                          XML_id,     OString::number(nID),
                          XML_name,   sName);

    OUString sURL;
    if ( GetProperty( xShapeProps, u"URL"_ustr ) )
        mAny >>= sURL;
    if( !sURL.isEmpty() )
    {
        OUString sRelId = mpFB->addRelation( mpFS->getOutputStream(),
                oox::getRelationship(Relationship::HYPERLINK),
                mpURLTransformer->getTransformedString(sURL),
                mpURLTransformer->isExternalURL(sURL));

        mpFS->singleElementNS(XML_a, XML_hlinkClick, FSNS(XML_r, XML_id), sRelId);
    }

    if (bIsChartex) {
        pFS->startElement(FSNS(XML_a, XML_extLst));
        pFS->startElement(FSNS(XML_a, XML_ext), XML_uri,
            "{FF2B5EF4-FFF2-40B4-BE49-F238E27FC236}");
        pFS->singleElement(FSNS(XML_a16, XML_creationId),
                FSNS(XML_xmlns, XML_a16), "http://schemas.microsoft.com/office/drawing/2014/main",
                XML_id, "{393D7C90-AF84-3958-641C-0FEC03FE8894}");

        pFS->endElement(FSNS(XML_a, XML_ext));
        pFS->endElement(FSNS(XML_a, XML_extLst));
    }

    pFS->endElementNS(mnXmlNamespace, XML_cNvPr);

    pFS->singleElementNS(mnXmlNamespace, XML_cNvGraphicFramePr);

    if( GetDocumentType() == DOCUMENT_PPTX )
        pFS->singleElementNS(mnXmlNamespace, XML_nvPr);
    pFS->endElementNS( mnXmlNamespace, XML_nvGraphicFramePr );

    // visual chart properties
    WriteShapeTransformation( xShape, mnXmlNamespace );

    const char *sSchemaURL = bIsChartex?
        "http://schemas.microsoft.com/office/drawing/2014/chartex" :
        "http://schemas.openxmlformats.org/drawingml/2006/chart";

    // writer chart object
    pFS->startElement(FSNS(XML_a, XML_graphic));
    pFS->startElement( FSNS( XML_a, XML_graphicData ), XML_uri, sSchemaURL );
    OUString sId;
    const char* sFullPath = nullptr;
    const char* sRelativePath = nullptr;
    const char *sChartFnamePrefix = bIsChartex? "chartEx" : "chart";
    switch( GetDocumentType() )
    {
        case DOCUMENT_DOCX:
        {
            sFullPath = "word/charts/";
            sRelativePath = "charts/";
            break;
        }
        case DOCUMENT_PPTX:
        {
            sFullPath = "ppt/charts/";
            sRelativePath = "../charts/";
            break;
        }
        case DOCUMENT_XLSX:
        {
            sFullPath = "xl/charts/";
            sRelativePath = "../charts/";
            break;
        }
        default:
        {
            sFullPath = "charts/";
            sRelativePath = "charts/";
            break;
        }
    }
    OUString sFullStream = OUStringBuffer()
                            .appendAscii(sFullPath)
                            .appendAscii(sChartFnamePrefix)
                            .append(OUString::number(nChartCount) + ".xml")
                            .makeStringAndClear();
    OUString sRelativeStream = OUStringBuffer()
                            .appendAscii(sRelativePath)
                            .appendAscii(sChartFnamePrefix)
                            .append(OUString::number(nChartCount) + ".xml" )
                            .makeStringAndClear();

    const OUString sAppURL = bIsChartex?
        u"application/vnd.ms-office.chartex+xml"_ustr :
        u"application/vnd.openxmlformats-officedocument.drawingml.chart+xml"_ustr;

    const Relationship eChartRel = bIsChartex ?
        Relationship::CHARTEX :
        Relationship::CHART;

    FSHelperPtr pChart = CreateOutputStream(
            sFullStream,
            sRelativeStream,
            pFS->getOutputStream(),
            sAppURL,
            oox::getRelationship(eChartRel),
            &sId );

    XmlFilterBase* pFB = GetFB();

    if (bIsChartex) {
        // Use chartex namespace
        pFS->singleElement(  FSNS( XML_cx, XML_chart ),
                FSNS(XML_xmlns, XML_cx), pFB->getNamespaceURL(OOX_NS(cx)),
                FSNS(XML_xmlns, XML_r), pFB->getNamespaceURL(OOX_NS(officeRel)),
                FSNS(XML_r, XML_id), sId );
    } else {
        pFS->singleElement(  FSNS( XML_c, XML_chart ),
                FSNS(XML_xmlns, XML_c), pFB->getNamespaceURL(OOX_NS(dmlChart)),
                FSNS(XML_xmlns, XML_r), pFB->getNamespaceURL(OOX_NS(officeRel)),
                FSNS(XML_r, XML_id), sId );
    }

    pFS->endElement( FSNS( XML_a, XML_graphicData ) );
    pFS->endElement( FSNS( XML_a, XML_graphic ) );
    pFS->endElementNS( mnXmlNamespace, XML_graphicFrame );

    if (bIsChartex) {
        // Do the AlternateContent fallback path
        pFS->endElementNS(XML_mc, XML_Choice);
        pFS->startElementNS(XML_mc, XML_Fallback);
        pFS->startElementNS(XML_xdr, XML_sp, XML_macro, "", XML_textlink, "");
        pFS->startElementNS(XML_xdr, XML_nvSpPr);
        pFS->singleElementNS(XML_xdr, XML_cNvPr, XML_id, "0", XML_name, "");
        pFS->startElementNS(XML_xdr, XML_cNvSpPr);
        pFS->singleElementNS(XML_a, XML_spLocks, XML_noTextEdit, "1");
        pFS->endElementNS(XML_xdr, XML_cNvSpPr);
        pFS->endElementNS(XML_xdr, XML_nvSpPr);
        pFS->startElementNS(XML_xdr, XML_spPr);
        pFS->startElementNS(XML_a, XML_xfrm);
        pFS->singleElementNS(XML_a, XML_off, XML_x, "6600825", XML_y, "2533650");
        pFS->singleElementNS(XML_a, XML_ext, XML_cx, "4572000", XML_cy, "2743200");
        pFS->endElementNS(XML_a, XML_xfrm);
        pFS->startElementNS(XML_a, XML_prstGeom, XML_prst, "rect");
        pFS->singleElementNS(XML_a, XML_avLst);
        pFS->endElementNS(XML_a, XML_prstGeom);
        pFS->startElementNS(XML_a, XML_solidFill);
        pFS->singleElementNS(XML_a, XML_prstClr, XML_val, "white");
        pFS->endElementNS(XML_a, XML_solidFill);
        pFS->startElementNS(XML_a, XML_ln, XML_w, "1");
        pFS->startElementNS(XML_a, XML_solidFill);
        pFS->singleElementNS(XML_a, XML_prstClr, XML_val, "green");
        pFS->endElementNS(XML_a, XML_solidFill);
        pFS->endElementNS(XML_a, XML_ln);
        pFS->endElementNS(XML_xdr, XML_spPr);
        pFS->startElementNS(XML_xdr, XML_txBody);
        pFS->singleElementNS(XML_a, XML_bodyPr, XML_vertOverflow, "clip", XML_horzOverflow, "clip");
        pFS->singleElementNS(XML_a, XML_lstStyle);
        pFS->startElementNS(XML_a, XML_p);
        pFS->startElementNS(XML_a, XML_r);
        pFS->singleElementNS(XML_a, XML_rPr, XML_sz, "1100");
        pFS->startElementNS(XML_a, XML_t);

        const std::string_view sErrTxt("This chart isn't available in your version of Excel.\n\n"
            "Editing this shape or saving this workbook into a different file format will permanently break the chart.");
        pFS->writeEscaped( sErrTxt );

        pFS->endElementNS(XML_a, XML_t);
        pFS->endElementNS(XML_a, XML_r);
        pFS->endElementNS(XML_a, XML_p);
        pFS->endElementNS(XML_xdr, XML_txBody);
        pFS->endElementNS(XML_xdr, XML_sp);

        pFS->endElementNS(XML_mc, XML_Fallback);
        pFS->endElementNS(XML_mc, XML_AlternateContent);
    }

    SetFS( pChart );
    ExportContent();

    if (bIsChartex) {
        SetFS( pChart );
        sRelativePath ="";

        FSHelperPtr pChartFS = GetFS();

        // output style and colorstyle files

        // first style
        static constexpr char sStyleFnamePrefix[] = "style";
        OUStringBuffer sFullStreamBuf;
        sFullStreamBuf.appendAscii(sFullPath);
        sFullStreamBuf = sFullStreamBuf + sStyleFnamePrefix + OUString::number(nChartCount) + ".xml";
        sFullStream = sFullStreamBuf.makeStringAndClear();
        OUStringBuffer sRelativeStreamBuf;
        sRelativeStreamBuf.appendAscii(sRelativePath);
        sRelativeStreamBuf = sRelativeStreamBuf + sStyleFnamePrefix + OUString::number(nChartCount) + ".xml";
        sRelativeStream = sRelativeStreamBuf.makeStringAndClear();

        FSHelperPtr pStyle = CreateOutputStream(
                sFullStream,
                sRelativeStream,
                pChartFS->getOutputStream(),
                u"application/vnd.ms-office.chartstyle+xml"_ustr,
                oox::getRelationship(Relationship::CHARTSTYLE),
                &sId,
                true /* for some reason this doesn't have a header line */);

        SetFS( pStyle );
        pFS = GetFS();

        pFS->startElement(FSNS(XML_cs, XML_chartStyle),
                FSNS( XML_xmlns, XML_cs ), pFB->getNamespaceURL(OOX_NS(cs)),
                FSNS( XML_xmlns, XML_a ), pFB->getNamespaceURL(OOX_NS(dml)),
                XML_id, "419" /* no idea what this number is supposed to be */);

        outputStyleEntry(pFS, XML_axisTitle);;
        outputStyleEntry(pFS, XML_categoryAxis);
        outputChartAreaStyleEntry(pFS);
        outputStyleEntry(pFS, XML_dataLabel);
        outputDataPointStyleEntry(pFS);
        outputStyleEntry(pFS, XML_dataPoint3D);
        outputStyleEntry(pFS, XML_dataPointLine);
        outputStyleEntry(pFS, XML_dataPointMarker);
        outputStyleEntry(pFS, XML_dataPointWireframe);
        outputStyleEntry(pFS, XML_dataTable);
        outputStyleEntry(pFS, XML_downBar);
        outputStyleEntry(pFS, XML_dropLine);
        outputStyleEntry(pFS, XML_errorBar);
        outputStyleEntry(pFS, XML_floor);
        outputStyleEntry(pFS, XML_gridlineMajor);
        outputStyleEntry(pFS, XML_gridlineMinor);
        outputStyleEntry(pFS, XML_hiLoLine);
        outputStyleEntry(pFS, XML_leaderLine);
        outputStyleEntry(pFS, XML_legend);
        outputStyleEntry(pFS, XML_plotArea);
        outputStyleEntry(pFS, XML_plotArea3D);
        outputStyleEntry(pFS, XML_seriesAxis);
        outputStyleEntry(pFS, XML_seriesLine);
        outputStyleEntry(pFS, XML_title);
        outputStyleEntry(pFS, XML_trendline);
        outputStyleEntry(pFS, XML_trendlineLabel);
        outputStyleEntry(pFS, XML_upBar);
        outputStyleEntry(pFS, XML_valueAxis);
        outputStyleEntry(pFS, XML_wall);

        pFS->endElement(FSNS(XML_cs, XML_chartStyle));

        pStyle->endDocument();

        // now colorstyle
        static constexpr char sColorFnamePrefix[] = "colors";
        sFullStreamBuf = OUStringBuffer();
        sFullStreamBuf.appendAscii(sFullPath);
        sFullStreamBuf = sFullStreamBuf + sColorFnamePrefix + OUString::number(nChartCount) + ".xml";
        sFullStream = sFullStreamBuf.makeStringAndClear();
        sRelativeStreamBuf = OUStringBuffer();
        sRelativeStreamBuf.appendAscii(sRelativePath);
        sRelativeStreamBuf = sRelativeStreamBuf + sColorFnamePrefix + OUString::number(nChartCount) + ".xml";
        sRelativeStream = sRelativeStreamBuf.makeStringAndClear();

        FSHelperPtr pColorStyle = CreateOutputStream(
                sFullStream,
                sRelativeStream,
                pChartFS->getOutputStream(),
                u"application/vnd.ms-office.chartcolorstyle+xml"_ustr,
                oox::getRelationship(Relationship::CHARTCOLORSTYLE),
                &sId,
                true /* also no header line */);

        SetFS( pColorStyle );
        pFS = GetFS();

        pFS->startElement(FSNS(XML_cs, XML_colorStyle),
                FSNS( XML_xmlns, XML_cs ), pFB->getNamespaceURL(OOX_NS(cs)),
                FSNS( XML_xmlns, XML_a ), pFB->getNamespaceURL(OOX_NS(dml)),
                XML_meth, "cycle",
                XML_id, "10" /* no idea what this number is supposed to be */);

        pFS->singleElement(FSNS(XML_a, XML_schemeClr),
                XML_val, "accent1");

        pFS->endElement(FSNS(XML_cs, XML_colorStyle));

        pColorStyle->endDocument();
    }

    pChart->endDocument();
}

void ChartExport::InitRangeSegmentationProperties( const Reference< chart2::XChartDocument > & xChartDoc )
{
    if( !xChartDoc.is())
        return;

    try
    {
        Reference< chart2::data::XDataProvider > xDataProvider( xChartDoc->getDataProvider() );
        OSL_ENSURE( xDataProvider.is(), "No DataProvider" );
        if( xDataProvider.is())
        {
            mbHasCategoryLabels = lcl_hasCategoryLabels( xChartDoc );
        }
    }
    catch( const uno::Exception & )
    {
        DBG_UNHANDLED_EXCEPTION("oox");
    }
}

void ChartExport::ExportContent()
{
    Reference< chart2::XChartDocument > xChartDoc( getModel(), uno::UNO_QUERY );
    OSL_ASSERT( xChartDoc.is() );
    if( !xChartDoc.is() )
        return;
    InitRangeSegmentationProperties( xChartDoc );

    const bool bIsChartex = isChartexNotChartNS();
    ExportContent_( bIsChartex );
}

void ChartExport::ExportContent_( bool bIsChartex )
{
    Reference< css::chart::XChartDocument > xChartDoc( getModel(), uno::UNO_QUERY );
    if( xChartDoc.is())
    {
        // determine if data comes from the outside
        bool bIncludeTable = true;

        Reference< chart2::XChartDocument > xNewDoc( xChartDoc, uno::UNO_QUERY );
        if( xNewDoc.is())
        {
            // check if we have own data.  If so we must not export the complete
            // range string, as this is our only indicator for having own or
            // external data. @todo: fix this in the file format!
            Reference< lang::XServiceInfo > xDPServiceInfo( xNewDoc->getDataProvider(), uno::UNO_QUERY );
            if( ! (xDPServiceInfo.is() && xDPServiceInfo->getImplementationName() == "com.sun.star.comp.chart.InternalDataProvider" ))
            {
                bIncludeTable = false;
            }
        }
        exportChartSpace( xChartDoc, bIncludeTable, bIsChartex );
    }
    else
    {
        OSL_FAIL( "Couldn't export chart due to wrong XModel" );
    }
}

void ChartExport::exportChartSpace( const Reference< css::chart::XChartDocument >& xChartDoc,
                                    bool bIncludeTable,
                                    bool bIsChartex)
{
    FSHelperPtr pFS = GetFS();
    XmlFilterBase* pFB = GetFB();

    const sal_Int32 nChartNS = bIsChartex ? XML_cx : XML_c;

    if (bIsChartex) {
        pFS->startElement( FSNS( nChartNS, XML_chartSpace ),
                FSNS( XML_xmlns, XML_a ), pFB->getNamespaceURL(OOX_NS(dml)),
                FSNS( XML_xmlns, XML_r ), pFB->getNamespaceURL(OOX_NS(officeRel)),
                FSNS( XML_xmlns, XML_cx ), pFB->getNamespaceURL(OOX_NS(cx)));
    } else {
        pFS->startElement( FSNS( nChartNS, XML_chartSpace ),
                FSNS( XML_xmlns, XML_c ), pFB->getNamespaceURL(OOX_NS(dmlChart)),
                FSNS( XML_xmlns, XML_a ), pFB->getNamespaceURL(OOX_NS(dml)),
                FSNS( XML_xmlns, XML_r ), pFB->getNamespaceURL(OOX_NS(officeRel)));
    }

    if( !bIncludeTable )
    {
        // TODO:external data
    }
    else
    {
        Reference< XPropertySet > xPropSet(xChartDoc, UNO_QUERY);
        Any aNullDate = xPropSet->getPropertyValue("NullDate");
        util::DateTime aDate;
        if ((aNullDate >>= aDate) && (aDate.Year == 1904 && aDate.Month == 1 && aDate.Day == 1))
        {
            pFS->singleElement(FSNS(XML_c, XML_date1904), XML_val, "1");
        }
        else
        {
            pFS->singleElement(FSNS(XML_c, XML_date1904), XML_val, "0");
        }
    }

    // TODO: get the correct editing language
    if (bIsChartex) {
        // chartData
        pFS->startElement(FSNS(XML_cx, XML_chartData));

        exportExternalData(xChartDoc, true);
        exportData_chartex(xChartDoc);

        pFS->endElement(FSNS(XML_cx, XML_chartData));
    } else {
        pFS->singleElement(FSNS(XML_c, XML_lang), XML_val, "en-US");

        pFS->singleElement(FSNS(XML_c, XML_roundedCorners), XML_val, "0");
    }

    // style
    if (!bIsChartex) {
        mxDiagram.set( xChartDoc->getDiagram() );
        Reference< XPropertySet > xPropSet(mxDiagram, uno::UNO_QUERY);
        if (GetProperty(xPropSet, u"StyleIndex"_ustr)) {
            sal_Int32 nStyleIdx = -1;
            mAny >>= nStyleIdx;
            assert(nStyleIdx >= 0);
            pFS->singleElement(FSNS(XML_c, XML_style), XML_val,
                    OUString::number(nStyleIdx));
        }
    }

    //XML_chart
    exportChart(xChartDoc, bIsChartex);

    // TODO: printSettings
    // TODO: text properties
    Reference< XPropertySet > xPropSet = xChartDoc->getArea();
    if( xPropSet.is() )
        exportShapeProps( xPropSet, bIsChartex );

    // TODO for chartex
    if (!bIsChartex) {
        //XML_externalData
        exportExternalData(xChartDoc, false);
    }

    // export additional shapes in chart
    if (!bIsChartex) {
        exportAdditionalShapes(xChartDoc);
    }

    pFS->endElement( FSNS( nChartNS, XML_chartSpace ) );
}

void ChartExport::exportData_chartex( [[maybe_unused]] const Reference< css::chart::XChartDocument >& xChartDoc)
{
    Reference< chart2::XCoordinateSystemContainer > xBCooSysCnt( mxNewDiagram, uno::UNO_QUERY );
    if( ! xBCooSysCnt.is()) return;
    const Sequence< Reference< chart2::XCoordinateSystem > >
        aCooSysSeq( xBCooSysCnt->getCoordinateSystems());

    if (!aCooSysSeq.hasElements()) return;

    for( const auto& rCS : aCooSysSeq ) {
        Reference< chart2::XChartTypeContainer > xCTCnt( rCS, uno::UNO_QUERY );
        if( ! xCTCnt.is())
            continue;
        const Sequence< Reference< chart2::XChartType > > aCTSeq( xCTCnt->getChartTypes());

        for( const auto& rCT : aCTSeq ) {
            Reference< chart2::XDataSeriesContainer > xDSCnt( rCT, uno::UNO_QUERY );
            if( ! xDSCnt.is())
                return;
            Reference< chart2::XChartType > xChartType( rCT, uno::UNO_QUERY );
            if( ! xChartType.is())
                continue;

            OUString aLabelRole = xChartType->getRoleOfSequenceForSeriesLabel();

            const std::vector<Sequence<Reference<chart2::XDataSeries> > > aSplitDataSeries = splitDataSeriesByAxis(xChartType);

            for (const auto& splitDataSeries : aSplitDataSeries) {
                sal_Int32 nSeriesIndex = 0;
                for( const auto& rSeries : splitDataSeries )
                {
                    // export series
                    Reference< chart2::data::XDataSource > xSource( rSeries, uno::UNO_QUERY );
                    if( !xSource.is()) continue;

                    Sequence< Reference< chart2::data::XLabeledDataSequence > > aSeqCnt(
                        xSource->getDataSequences());

                    // search for main sequence and create a series element
                    sal_Int32 nMainSequenceIndex = -1;
                    sal_Int32 nSeriesLength = 0;
                    Reference< chart2::data::XDataSequence > xValueSeq;
                    Reference< chart2::data::XDataSequence > xLabelSeq;
                    sal_Int32 nSeqIdx=0;
                    for( ; nSeqIdx<aSeqCnt.getLength(); ++nSeqIdx )
                    {
                        Reference< chart2::data::XDataSequence > xTempValueSeq( aSeqCnt[nSeqIdx]->getValues() );
                        if( nMainSequenceIndex==-1 )
                        {
                            Reference< beans::XPropertySet > xSeqProp( xTempValueSeq, uno::UNO_QUERY );
                            OUString aRole;
                            if( xSeqProp.is())
                                xSeqProp->getPropertyValue(u"Role"_ustr) >>= aRole;
                            // "main" sequence
                            if( aRole == aLabelRole )
                            {
                                xValueSeq.set( xTempValueSeq );
                                xLabelSeq.set( aSeqCnt[nSeqIdx]->getLabel());
                                nMainSequenceIndex = nSeqIdx;
                            }
                        }
                        sal_Int32 nSequenceLength = (xTempValueSeq.is()? xTempValueSeq->getData().getLength() : sal_Int32(0));
                        if( nSeriesLength < nSequenceLength )
                            nSeriesLength = nSequenceLength;
                    }
                    FSHelperPtr pFS = GetFS();

                    // The data id needs to agree with the id in exportSeries(). See DATA_ID_COMMENT
                    pFS->startElement(FSNS(XML_cx, XML_data), XML_id, OUString::number(nSeriesIndex++));

                    // .xlsx chartex files seem to have this magical "_xlchart.v2.0" string,
                    // and no explicit data, while .docx and .pptx contain the literal data,
                    // as well as a ../embeddings file (which LO doesn't seem to produce).
                    // But there's probably a smarter way to determine which pathway to take
                    // than based on document type.
                    if (GetDocumentType() == DOCUMENT_XLSX) {
                        // Just hard-coding this for now
                        pFS->startElement(FSNS(XML_cx, XML_numDim), XML_type, "val");
                        pFS->startElement(FSNS(XML_cx, XML_f));
                        pFS->writeEscaped("_xlchart.v2.0");    // I have no idea what this
                                                                // means or what it should be in
                                                                // general
                        pFS->endElement(FSNS(XML_cx, XML_f));
                        pFS->endElement(FSNS(XML_cx, XML_numDim));
                    } else {    // PPTX, DOCX
                        OUString aCellRange = mxCategoriesValues.is() ? mxCategoriesValues->getSourceRangeRepresentation() : OUString();
#undef OUTPUT_SPLIT_CATEGORIES  // do we need this or not? TODO
#ifdef OUTPUT_SPLIT_CATEGORIES
                        const Sequence< Sequence< OUString >> aFinalSplitSource = getSplitCategoriesList(aCellRange);
#endif
                        aCellRange = parseFormula( aCellRange );

#ifdef OUTPUT_SPLIT_CATEGORIES
                        if (aFinalSplitSource.getLength() > 1) {

                            // export multi level category axis labels
                            pFS->startElement(FSNS(XML_cx, XML_strDim), XML_type, "cat");

                            pFS->startElement(FSNS(XML_cx, XML_f));
                            pFS->writeEscaped(aCellRange);
                            pFS->endElement(FSNS(XML_cx, XML_f));

                            for (const auto& rSeq : aFinalSplitSource) {
                                pFS->startElement(FSNS(XML_cx, XML_lvl),
                                        XML_ptCount, OString::number(aFinalSplitSource[0].getLength()));

                                for (sal_Int32 j = 0; j < rSeq.getLength(); j++) {
                                    if(!rSeq[j].isEmpty()) {
                                        pFS->startElement(FSNS(XML_cx, XML_pt), XML_idx, OString::number(j));
                                        pFS->writeEscaped(rSeq[j]);
                                        pFS->endElement(FSNS(XML_cx, XML_pt));
                                    }
                                }
                                pFS->endElement(FSNS(XML_cx, XML_lvl));
                            }

                            pFS->endElement(FSNS(XML_cx, XML_strDim));
                        }
                        else
#endif
                        {
                            // export single category axis labels
                            // TODO: seems like this should consider mbHasCategoryLabels
                            bool bWriteDateCategories = mbHasDateCategories;
                            OUString aNumberFormatString;
                            if (bWriteDateCategories)
                            {
                                Reference< css::chart::XAxisXSupplier > xAxisXSupp( mxDiagram, uno::UNO_QUERY );
                                if( xAxisXSupp.is())
                                {
                                    Reference< XPropertySet > xAxisProp = xAxisXSupp->getXAxis();
                                    if (GetProperty(xAxisProp, u"NumberFormat"_ustr))
                                    {
                                        sal_Int32 nKey = 0;
                                        mAny >>= nKey;
                                        aNumberFormatString = getNumberFormatCode(nKey);
                                    }
                                }
                                if (aNumberFormatString.isEmpty()) bWriteDateCategories = false;
                            }

                            // === Output the categories
                            if (bWriteDateCategories)
                            {
                                std::vector<double> aDateCategories = lcl_getAllValuesFromSequence(xValueSeq);
                                const sal_Int32 ptCount = aDateCategories.size();

                                pFS->startElement(FSNS(XML_cx, XML_numDim), XML_type, "x"); // is "x" right?
                                // TODO: check this

                                pFS->startElement(FSNS(XML_cx, XML_f));
                                pFS->writeEscaped(aCellRange);
                                pFS->endElement(FSNS(XML_cx, XML_f));

                                pFS->startElement(FSNS(XML_cx, XML_lvl),
                                        XML_ptCount, OString::number(ptCount),
                                        XML_formatCode, aNumberFormatString);

                                for (sal_Int32 i = 0; i < ptCount; i++) {
                                    if (!std::isnan(aDateCategories[i])) {
                                        pFS->startElement(FSNS(XML_cx, XML_pt), XML_idx, OString::number(i));
                                        pFS->write(OString::number(aDateCategories[i]));
                                        pFS->endElement(FSNS(XML_cx, XML_pt));
                                    }
                                }

                                pFS->endElement(FSNS(XML_cx, XML_lvl));
                                pFS->endElement(FSNS(XML_cx, XML_numDim));
                            }
                            else
                            {
                                std::vector<OUString> aCategories;
                                lcl_fillCategoriesIntoStringVector(xValueSeq, aCategories);
                                const sal_Int32 ptCount = aCategories.size();

                                // TODO: shouldn't have "cat" hard-coded here:
                                // other options are colorStr, entityId
                                pFS->startElement(FSNS(XML_cx, XML_strDim), XML_type, "cat");

                                pFS->startElement(FSNS(XML_cx, XML_f));
                                pFS->writeEscaped(aCellRange);
                                pFS->endElement(FSNS(XML_cx, XML_f));

                                pFS->startElement(FSNS(XML_cx, XML_lvl), XML_ptCount, OString::number(ptCount));

                                for (sal_Int32 i = 0; i < ptCount; i++) {
                                    pFS->startElement(FSNS(XML_cx, XML_pt), XML_idx, OString::number(i));
                                    pFS->writeEscaped(aCategories[i]);
                                    pFS->endElement(FSNS(XML_cx, XML_pt));
                                }

                                pFS->endElement(FSNS(XML_cx, XML_lvl));
                                pFS->endElement(FSNS(XML_cx, XML_strDim));
                            }

                            // === Output the values
                            pFS->startElement(FSNS(XML_cx, XML_numDim), XML_type, "val");

                            aCellRange = xValueSeq.is() ? xValueSeq->getSourceRangeRepresentation() : OUString();
                            aCellRange = parseFormula( aCellRange );
                            // TODO: need to handle XML_multiLvlStrRef according to aCellRange

                            pFS->startElement(FSNS(XML_cx, XML_f));
                            pFS->writeEscaped( aCellRange );
                            pFS->endElement( FSNS( XML_cx, XML_f ) );

                            ::std::vector< double > aValues = lcl_getAllValuesFromSequence( xValueSeq );
                            sal_Int32 ptCount = aValues.size();
                            OUString sNumberFormatString(u"General"_ustr);
                            const sal_Int32 nKey = xValueSeq.is() ? xValueSeq->getNumberFormatKeyByIndex(-1) : 0;
                            if (nKey > 0) {
                                sNumberFormatString = getNumberFormatCode(nKey);
                            }
                            pFS->startElement(FSNS(XML_cx, XML_lvl),
                                    XML_ptCount, OString::number(ptCount),
                                    XML_formatCode,  sNumberFormatString);

                            for( sal_Int32 i = 0; i < ptCount; i++ ) {

                                pFS->startElement(FSNS(XML_cx, XML_pt), XML_idx, OString::number(i));
                                pFS->write(std::isnan(aValues[i]) ?  0 : aValues[i]);
                                pFS->endElement(FSNS(XML_cx, XML_pt));
                            }

                            pFS->endElement(FSNS(XML_cx, XML_lvl));
                            pFS->endElement(FSNS(XML_cx, XML_numDim));
                        }
                    }
                    pFS->endElement(FSNS(XML_cx, XML_data));
                }
            }
        }
    }
}

void ChartExport::exportExternalData( const Reference< css::chart::XChartDocument >& xChartDoc,
        bool bIsChartex)
{
    if (bIsChartex) return; // TODO!!
    // Embedded external data is grab bagged for docx file hence adding export part of
    // external data for docx files only.
    if(GetDocumentType() != DOCUMENT_DOCX)
        return;

    OUString externalDataPath;
    Reference< beans::XPropertySet > xDocPropSet( xChartDoc->getDiagram(), uno::UNO_QUERY );
    if( xDocPropSet.is())
    {
        try
        {
            Any aAny( xDocPropSet->getPropertyValue( u"ExternalData"_ustr ));
            aAny >>= externalDataPath;
        }
        catch( beans::UnknownPropertyException & )
        {
            SAL_WARN("oox", "Required property not found in ChartDocument");
        }
    }
    if(externalDataPath.isEmpty())
        return;

    // Here adding external data entry to relationship.
    OUString relationPath = externalDataPath;
    // Converting absolute path to relative path.
    if( externalDataPath[ 0 ] != '.' && externalDataPath[ 1 ] != '.')
    {
        sal_Int32 nSepPos = externalDataPath.indexOf( '/', 0 );
        if( nSepPos > 0)
        {
            relationPath = relationPath.copy( nSepPos,  ::std::max< sal_Int32 >( externalDataPath.getLength(), 0 ) -  nSepPos );
            relationPath = ".." + relationPath;
        }
    }
    FSHelperPtr pFS = GetFS();
    OUString type = oox::getRelationship(Relationship::PACKAGE);
    if (relationPath.endsWith(".bin"))
        type = oox::getRelationship(Relationship::OLEOBJECT);

    OUString sRelId = GetFB()->addRelation(pFS->getOutputStream(),
                    type,
                    relationPath);
    pFS->singleElementNS(XML_c, XML_externalData, FSNS(XML_r, XML_id), sRelId);
}

void ChartExport::exportAdditionalShapes( const Reference< css::chart::XChartDocument >& xChartDoc )
{
    // Not used in chartex

    Reference< beans::XPropertySet > xDocPropSet(xChartDoc, uno::UNO_QUERY);
    if (!xDocPropSet.is())
        return;

    css::uno::Reference< css::drawing::XShapes > mxAdditionalShapes;
    // get a sequence of non-chart shapes
    try
    {
        Any aShapesAny = xDocPropSet->getPropertyValue(u"AdditionalShapes"_ustr);
        if( (aShapesAny >>= mxAdditionalShapes) && mxAdditionalShapes.is() )
        {
            OUString sId;
            const char* sFullPath = nullptr;
            const char* sRelativePath = nullptr;
            sal_Int32 nDrawing = getNewDrawingUniqueId();

            switch (GetDocumentType())
            {
                case DOCUMENT_DOCX:
                {
                    sFullPath = "word/drawings/drawing";
                    sRelativePath = "../drawings/drawing";
                    break;
                }
                case DOCUMENT_PPTX:
                {
                    sFullPath = "ppt/drawings/drawing";
                    sRelativePath = "../drawings/drawing";
                    break;
                }
                case DOCUMENT_XLSX:
                {
                    sFullPath = "xl/drawings/drawing";
                    sRelativePath = "../drawings/drawing";
                    break;
                }
                default:
                {
                    sFullPath = "drawings/drawing";
                    sRelativePath = "drawings/drawing";
                    break;
                }
            }
            OUString sFullStream = OUStringBuffer()
                .appendAscii(sFullPath)
                .append(OUString::number(nDrawing) + ".xml")
                .makeStringAndClear();
            OUString sRelativeStream = OUStringBuffer()
                .appendAscii(sRelativePath)
                .append(OUString::number(nDrawing) + ".xml")
                .makeStringAndClear();

            sax_fastparser::FSHelperPtr pDrawing = CreateOutputStream(
                sFullStream,
                sRelativeStream,
                GetFS()->getOutputStream(),
                u"application/vnd.openxmlformats-officedocument.drawingml.chartshapes+xml"_ustr,
                oox::getRelationship(Relationship::CHARTUSERSHAPES),
                &sId);

            GetFS()->singleElementNS(XML_c, XML_userShapes, FSNS(XML_r, XML_id), sId);

            XmlFilterBase* pFB = GetFB();
            pDrawing->startElement(FSNS(XML_c, XML_userShapes),
                FSNS(XML_xmlns, XML_cdr), pFB->getNamespaceURL(OOX_NS(dmlChartDr)),
                FSNS(XML_xmlns, XML_a), pFB->getNamespaceURL(OOX_NS(dml)),
                FSNS(XML_xmlns, XML_c), pFB->getNamespaceURL(OOX_NS(dmlChart)),
                FSNS(XML_xmlns, XML_r), pFB->getNamespaceURL(OOX_NS(officeRel)));

            const sal_Int32 nShapeCount(mxAdditionalShapes->getCount());
            for (sal_Int32 nShapeId = 0; nShapeId < nShapeCount; nShapeId++)
            {
                Reference< drawing::XShape > xShape;
                mxAdditionalShapes->getByIndex(nShapeId) >>= xShape;
                SAL_WARN_IF(!xShape.is(), "xmloff.chart", "Shape without an XShape?");
                if (!xShape.is())
                    continue;

                // TODO: absSizeAnchor: we import both (absSizeAnchor and relSizeAnchor), but there is no essential difference between them.
                pDrawing->startElement(FSNS(XML_cdr, XML_relSizeAnchor));
                uno::Reference< beans::XPropertySet > xShapeProperties(xShape, uno::UNO_QUERY);
                if( xShapeProperties.is() )
                {
                    Reference<embed::XVisualObject> xVisObject(mxChartModel, uno::UNO_QUERY);
                    awt::Size aPageSize = xVisObject->getVisualAreaSize(embed::Aspects::MSOLE_CONTENT);
                    WriteFromTo( xShape, aPageSize, pDrawing );

                    ShapeExport aExport(XML_cdr, pDrawing, nullptr, GetFB(), GetDocumentType(), nullptr, true);
                    aExport.WriteShape(xShape);
                }
                pDrawing->endElement(FSNS(XML_cdr, XML_relSizeAnchor));
            }
            pDrawing->endElement(FSNS(XML_c, XML_userShapes));
            pDrawing->endDocument();
        }
    }
    catch (const uno::Exception&)
    {
        TOOLS_INFO_EXCEPTION("xmloff.chart", "AdditionalShapes not found");
    }
}

void ChartExport::exportChart( const Reference< css::chart::XChartDocument >& xChartDoc,
        bool bIsChartex)
{
    Reference< chart2::XChartDocument > xNewDoc( xChartDoc, uno::UNO_QUERY );
    mxDiagram.set( xChartDoc->getDiagram() );
    if( xNewDoc.is()) {
        mxNewDiagram.set( xNewDoc->getFirstDiagram());
    }

    // get Properties of ChartDocument
    bool bHasMainTitle = false;
    bool bHasLegend = false;
    Reference< beans::XPropertySet > xDocPropSet( xChartDoc, uno::UNO_QUERY );
    if( xDocPropSet.is())
    {
        try
        {
            Any aAny( xDocPropSet->getPropertyValue(u"HasMainTitle"_ustr));
            aAny >>= bHasMainTitle;
            aAny = xDocPropSet->getPropertyValue(u"HasLegend"_ustr);
            aAny >>= bHasLegend;
        }
        catch( beans::UnknownPropertyException & )
        {
            SAL_WARN("oox", "Required property not found in ChartDocument");
        }
    } // if( xDocPropSet.is())

    Sequence< uno::Reference< chart2::XFormattedString > > xFormattedSubTitle;
    Reference< beans::XPropertySet > xPropSubTitle( xChartDoc->getSubTitle(), UNO_QUERY );
    if( xPropSubTitle.is())
    {
        OUString aSubTitle;
        if ((xPropSubTitle->getPropertyValue(u"String"_ustr) >>= aSubTitle) && !aSubTitle.isEmpty())
            xPropSubTitle->getPropertyValue(u"FormattedStrings"_ustr) >>= xFormattedSubTitle;
    }

    // chart element
    FSHelperPtr pFS = GetFS();

    const sal_Int32 nChartNS = bIsChartex ? XML_cx : XML_c;
    pFS->startElement(FSNS(nChartNS, XML_chart));

    // titles
    if( bHasMainTitle )
    {
        exportTitle( xChartDoc->getTitle(), bIsChartex, xFormattedSubTitle);
        if (!bIsChartex) {
            pFS->singleElement(FSNS(XML_c, XML_autoTitleDeleted), XML_val, "0");
        }
    }
    else if( xFormattedSubTitle.hasElements() )
    {
        exportTitle( xChartDoc->getSubTitle(), bIsChartex );
        if (!bIsChartex) {
            pFS->singleElement(FSNS(XML_c, XML_autoTitleDeleted), XML_val, "0");
        }
    }
    else if (!bIsChartex) {
        pFS->singleElement(FSNS(XML_c, XML_autoTitleDeleted), XML_val, "1");
    }

    InitPlotArea( );
    if( mbIs3DChart )
    {
        if (!bIsChartex) {
            exportView3D();

            // floor
            Reference< beans::XPropertySet > xFloor = mxNewDiagram->getFloor();
            if( xFloor.is() )
            {
                pFS->startElement(FSNS(XML_c, XML_floor));
                exportShapeProps( xFloor, false );
                pFS->endElement( FSNS( XML_c, XML_floor ) );
            }

            // LibreOffice doesn't distinguish between sideWall and backWall (both are using the same color).
            // It is controlled by the same Wall property.
            Reference< beans::XPropertySet > xWall = mxNewDiagram->getWall();
            if( xWall.is() )
            {
                // sideWall
                pFS->startElement(FSNS(XML_c, XML_sideWall));
                exportShapeProps( xWall, false );
                pFS->endElement( FSNS( XML_c, XML_sideWall ) );

                // backWall
                pFS->startElement(FSNS(XML_c, XML_backWall));
                exportShapeProps( xWall, false );
                pFS->endElement( FSNS( XML_c, XML_backWall ) );
            }
        }
    }
    // plot area
    exportPlotArea( xChartDoc, bIsChartex );
    // legend
    if( bHasLegend ) {
        exportLegend( xChartDoc, bIsChartex );
    }

    if (!bIsChartex) {
        uno::Reference<beans::XPropertySet> xDiagramPropSet(xChartDoc->getDiagram(), uno::UNO_QUERY);
        uno::Any aPlotVisOnly = xDiagramPropSet->getPropertyValue(u"IncludeHiddenCells"_ustr);
        bool bIncludeHiddenCells = false;
        aPlotVisOnly >>= bIncludeHiddenCells;
        pFS->singleElement(FSNS(XML_c, XML_plotVisOnly), XML_val, ToPsz10(!bIncludeHiddenCells));

        exportMissingValueTreatment(Reference<beans::XPropertySet>(mxDiagram, uno::UNO_QUERY));
    }

    pFS->endElement( FSNS( nChartNS, XML_chart ) );
}

void ChartExport::exportMissingValueTreatment(const uno::Reference<beans::XPropertySet>& xPropSet)
{
    if (!xPropSet.is())
        return;

    sal_Int32 nVal = 0;
    uno::Any aAny = xPropSet->getPropertyValue(u"MissingValueTreatment"_ustr);
    if (!(aAny >>= nVal))
        return;

    const char* pVal = nullptr;
    switch (nVal)
    {
        case cssc::MissingValueTreatment::LEAVE_GAP:
            pVal = "gap";
        break;
        case cssc::MissingValueTreatment::USE_ZERO:
            pVal = "zero";
        break;
        case cssc::MissingValueTreatment::CONTINUE:
            pVal = "span";
        break;
        default:
            SAL_WARN("oox", "unknown MissingValueTreatment value");
        break;
    }

    FSHelperPtr pFS = GetFS();
    pFS->singleElement(FSNS(XML_c, XML_dispBlanksAs), XML_val, pVal);
}

void ChartExport::exportLegend( const Reference< css::chart::XChartDocument >& xChartDoc,
        bool bIsChartex)
{
    FSHelperPtr pFS = GetFS();

    Reference< beans::XPropertySet > xProp( xChartDoc->getLegend(), uno::UNO_QUERY );
    if( xProp.is() )
    {
        if (!bIsChartex) {
            pFS->startElement(FSNS(XML_c, XML_legend));
        }

        // position
        css::chart::ChartLegendPosition aLegendPos = css::chart::ChartLegendPosition_NONE;
        try
        {
            Any aAny( xProp->getPropertyValue( u"Alignment"_ustr ));
            aAny >>= aLegendPos;
        }
        catch( beans::UnknownPropertyException & )
        {
            SAL_WARN("oox", "Property Align not found in ChartLegend");
        }

        const char* strPos = nullptr;
        switch( aLegendPos )
        {
            case css::chart::ChartLegendPosition_LEFT:
                strPos = "l";
                break;
            case css::chart::ChartLegendPosition_RIGHT:
                strPos = "r";
                break;
            case css::chart::ChartLegendPosition_TOP:
                strPos = "t";
                break;
            case css::chart::ChartLegendPosition_BOTTOM:
                strPos = "b";
                break;
            case css::chart::ChartLegendPosition_NONE:
            case css::chart::ChartLegendPosition::ChartLegendPosition_MAKE_FIXED_SIZE:
                // nothing
                break;
        }

        if (!bIsChartex) {
            if( strPos != nullptr )
            {
                pFS->singleElement(FSNS(XML_c, XML_legendPos), XML_val, strPos);
            }

            // legendEntry
            Reference<chart2::XCoordinateSystemContainer> xCooSysContainer(mxNewDiagram, UNO_QUERY_THROW);
            const Sequence<Reference<chart2::XCoordinateSystem>> xCooSysSequence(xCooSysContainer->getCoordinateSystems());

            sal_Int32 nIndex = 0;
            bool bShowLegendEntry;
            for (const auto& rCooSys : xCooSysSequence)
            {
                PropertySet aCooSysProp(rCooSys);
                bool bSwapXAndY = aCooSysProp.getBoolProperty(PROP_SwapXAndYAxis);

                Reference<chart2::XChartTypeContainer> xChartTypeContainer(rCooSys, UNO_QUERY_THROW);
                const Sequence<Reference<chart2::XChartType>> xChartTypeSequence(xChartTypeContainer->getChartTypes());
                if (!xChartTypeSequence.hasElements())
                    continue;

                for (const auto& rCT : xChartTypeSequence)
                {
                    Reference<chart2::XDataSeriesContainer> xDSCont(rCT, UNO_QUERY);
                    if (!xDSCont.is())
                        continue;

                    OUString aChartType(rCT->getChartType());
                    bool bIsPie = lcl_getChartType(aChartType) == chart::TYPEID_PIE;
                    if (bIsPie)
                    {
                        PropertySet xChartTypeProp(rCT);
                        bIsPie = !xChartTypeProp.getBoolProperty(PROP_UseRings);
                    }
                    const Sequence<Reference<chart2::XDataSeries>> aDataSeriesSeq = xDSCont->getDataSeries();
                    if (bSwapXAndY)
                        nIndex += aDataSeriesSeq.getLength() - 1;
                    for (const auto& rDataSeries : aDataSeriesSeq)
                    {
                        PropertySet aSeriesProp(rDataSeries);
                        bool bVaryColorsByPoint = aSeriesProp.getBoolProperty(PROP_VaryColorsByPoint);
                        if (bVaryColorsByPoint || bIsPie)
                        {
                            Sequence<sal_Int32> deletedLegendEntriesSeq;
                            aSeriesProp.getProperty(deletedLegendEntriesSeq, PROP_DeletedLegendEntries);
                            for (const auto& deletedLegendEntry : std::as_const(deletedLegendEntriesSeq))
                            {
                                pFS->startElement(FSNS(XML_c, XML_legendEntry));
                                pFS->singleElement(FSNS(XML_c, XML_idx), XML_val,
                                                   OString::number(nIndex + deletedLegendEntry));
                                pFS->singleElement(FSNS(XML_c, XML_delete), XML_val, "1");
                                pFS->endElement(FSNS(XML_c, XML_legendEntry));
                            }
                            Reference<chart2::data::XDataSource> xDSrc(rDataSeries, UNO_QUERY);
                            if (!xDSrc.is())
                                continue;

                            const Sequence<Reference<chart2::data::XLabeledDataSequence>> aDataSeqs = xDSrc->getDataSequences();
                            for (const auto& rDataSeq : aDataSeqs)
                            {
                                Reference<chart2::data::XDataSequence> xValues = rDataSeq->getValues();
                                if (!xValues.is())
                                    continue;

                                sal_Int32 nDataSeqSize = xValues->getData().getLength();
                                nIndex += nDataSeqSize;
                            }
                        }
                        else
                        {
                            bShowLegendEntry = aSeriesProp.getBoolProperty(PROP_ShowLegendEntry);
                            if (!bShowLegendEntry)
                            {
                                pFS->startElement(FSNS(XML_c, XML_legendEntry));
                                pFS->singleElement(FSNS(XML_c, XML_idx), XML_val,
                                                   OString::number(nIndex));
                                pFS->singleElement(FSNS(XML_c, XML_delete), XML_val, "1");
                                pFS->endElement(FSNS(XML_c, XML_legendEntry));
                            }
                            bSwapXAndY ? nIndex-- : nIndex++;
                        }
                    }
                    if (bSwapXAndY)
                        nIndex += aDataSeriesSeq.getLength() + 1;
                }
            }

            uno::Any aRelativePos = xProp->getPropertyValue(u"RelativePosition"_ustr);
            if (aRelativePos.hasValue())
            {
                pFS->startElement(FSNS(XML_c, XML_layout));
                pFS->startElement(FSNS(XML_c, XML_manualLayout));

                pFS->singleElement(FSNS(XML_c, XML_xMode), XML_val, "edge");
                pFS->singleElement(FSNS(XML_c, XML_yMode), XML_val, "edge");
                chart2::RelativePosition aPos = aRelativePos.get<chart2::RelativePosition>();

                const double x = aPos.Primary;
                const double y = aPos.Secondary;

                pFS->singleElement(FSNS(XML_c, XML_x), XML_val, OString::number(x));
                pFS->singleElement(FSNS(XML_c, XML_y), XML_val, OString::number(y));

                uno::Any aRelativeSize = xProp->getPropertyValue(u"RelativeSize"_ustr);
                if (aRelativeSize.hasValue())
                {
                    chart2::RelativeSize aSize = aRelativeSize.get<chart2::RelativeSize>();

                    const double w = aSize.Primary;
                    const double h = aSize.Secondary;

                    pFS->singleElement(FSNS(XML_c, XML_w), XML_val, OString::number(w));

                    pFS->singleElement(FSNS(XML_c, XML_h), XML_val, OString::number(h));
                }

                SAL_WARN_IF(aPos.Anchor != css::drawing::Alignment_TOP_LEFT, "oox", "unsupported anchor position");

                pFS->endElement(FSNS(XML_c, XML_manualLayout));
                pFS->endElement(FSNS(XML_c, XML_layout));
            }
        }

        const char *sOverlay = nullptr;
        if (strPos != nullptr)
        {
            uno::Any aOverlay = xProp->getPropertyValue(u"Overlay"_ustr);
            if(aOverlay.get<bool>())
                sOverlay = "1";
            else
                sOverlay = "0";
        }

        if (bIsChartex) {
            pFS->startElement(FSNS(XML_cx, XML_legend),
                    XML_pos, strPos ? strPos : "r",
                    XML_align, "ctr",   // is this supported?
                    XML_overlay, sOverlay ? sOverlay : "0");
        } else {
            pFS->singleElement(FSNS(XML_c, XML_overlay), XML_val, sOverlay);
        }

        // shape properties
        exportShapeProps( xProp, bIsChartex );

        // draw-chart:txPr text properties
        exportTextProps( xProp, bIsChartex );

        if (bIsChartex) {
            pFS->endElement( FSNS( XML_cx, XML_legend ) );
        } else {
            pFS->endElement( FSNS( XML_c, XML_legend ) );
        }
    }
}

void ChartExport::exportTitle( const Reference< XShape >& xShape, bool bIsChartex,
    const css::uno::Sequence< uno::Reference< css::chart2::XFormattedString > >& xFormattedSubTitle )
{
    Sequence< uno::Reference< chart2::XFormattedString > > xFormattedTitle;
    Reference< beans::XPropertySet > xPropSet( xShape, uno::UNO_QUERY );
    if( xPropSet.is())
    {
        OUString aTitle;
        if ((xPropSet->getPropertyValue(u"String"_ustr) >>= aTitle) && !aTitle.isEmpty())
            xPropSet->getPropertyValue(u"FormattedStrings"_ustr) >>= xFormattedTitle;
    }

    // tdf#101322: add subtitle to title
    if (xFormattedSubTitle.hasElements())
    {
        if (!xFormattedTitle.hasElements())
        {
            xFormattedTitle = xFormattedSubTitle;
        }
        else
        {
            sal_uInt32 nLength = xFormattedTitle.size();
            const OUString aLastString = xFormattedTitle.getArray()[nLength - 1]->getString();
            xFormattedTitle.getArray()[nLength - 1]->setString(aLastString + OUStringChar('\n'));
            for (const uno::Reference<chart2::XFormattedString>& rxFS : xFormattedSubTitle)
            {
                if (!rxFS->getString().isEmpty())
                {
                    xFormattedTitle.realloc(nLength + 1);
                    xFormattedTitle.getArray()[nLength++] = rxFS;
                }
            }
        }
    }

    if (!xFormattedTitle.hasElements())
        return;

    FSHelperPtr pFS = GetFS();

    if (bIsChartex) {
        pFS->startElement(FSNS(XML_cx, XML_title));
        lcl_writeChartexString(pFS, xFormattedTitle[0]->getString());
    } else {
        pFS->startElement(FSNS(XML_c, XML_title));
        pFS->startElement(FSNS(XML_c, XML_tx));
        pFS->startElement(FSNS(XML_c, XML_rich));
    }

    if (bIsChartex) {
        // shape properties
        if( xPropSet.is() )
        {
            exportShapeProps( xPropSet, bIsChartex );
        }

        pFS->startElement(FSNS(XML_cx, XML_txPr));
    }

    // TODO: bodyPr
    const char* sWritingMode = nullptr;
    bool bVertical = false;
    xPropSet->getPropertyValue(u"StackedText"_ustr) >>= bVertical;
    if( bVertical )
        sWritingMode = "wordArtVert";

    sal_Int32 nRotation = 0;
    xPropSet->getPropertyValue(u"TextRotation"_ustr) >>= nRotation;

    pFS->singleElement( FSNS( XML_a, XML_bodyPr ),
            XML_vert, sWritingMode,
            XML_rot, oox::drawingml::calcRotationValue(nRotation) );
    // TODO: lstStyle
    pFS->singleElement(FSNS(XML_a, XML_lstStyle));
    pFS->startElement(FSNS(XML_a, XML_p));

    pFS->startElement(FSNS(XML_a, XML_pPr));

    bool bDummy = false;
    sal_Int32 nDummy;
    WriteRunProperties(xPropSet, false, XML_defRPr, true, bDummy, nDummy );

    pFS->endElement( FSNS( XML_a, XML_pPr ) );

    for (const uno::Reference<chart2::XFormattedString>& rxFS : xFormattedTitle)
    {
        pFS->startElement(FSNS(XML_a, XML_r));
        bDummy = false;
        Reference< beans::XPropertySet > xRunPropSet(rxFS, uno::UNO_QUERY);
        WriteRunProperties(xRunPropSet, false, XML_rPr, true, bDummy, nDummy);
        pFS->startElement(FSNS(XML_a, XML_t));

        // the linebreak should always be at the end of the XFormattedString text
        bool bNextPara = rxFS->getString().endsWith(u"\n");
        if (!bNextPara)
            pFS->writeEscaped(rxFS->getString());
        else
        {
            sal_Int32 nEnd = rxFS->getString().lastIndexOf('\n');
            pFS->writeEscaped(rxFS->getString().replaceAt(nEnd, 1, u""));
        }
        pFS->endElement(FSNS(XML_a, XML_t));
        pFS->endElement(FSNS(XML_a, XML_r));

        if (bNextPara)
        {
            pFS->endElement(FSNS(XML_a, XML_p));

            pFS->startElement(FSNS(XML_a, XML_p));
            pFS->startElement(FSNS(XML_a, XML_pPr));
            bDummy = false;
            WriteRunProperties(xPropSet, false, XML_defRPr, true, bDummy, nDummy);
            pFS->endElement(FSNS(XML_a, XML_pPr));
        }
    }

    pFS->endElement( FSNS( XML_a, XML_p ) );

    if (bIsChartex) {
        pFS->endElement( FSNS( XML_cx, XML_txPr ) );
    } else {
        pFS->endElement( FSNS( XML_c, XML_rich ) );
        pFS->endElement( FSNS( XML_c, XML_tx ) );
    }

    uno::Any aManualLayout = xPropSet->getPropertyValue(u"RelativePosition"_ustr);
    if (aManualLayout.hasValue())
    {
        if (bIsChartex) {
            // TODO. Chartex doesn't have a manualLayout tag, but does have
            // "pos" and "align" attributes. Not sure how these correspond.
        } else {
            pFS->startElement(FSNS(XML_c, XML_layout));
            pFS->startElement(FSNS(XML_c, XML_manualLayout));
            pFS->singleElement(FSNS(XML_c, XML_xMode), XML_val, "edge");
            pFS->singleElement(FSNS(XML_c, XML_yMode), XML_val, "edge");

            Reference<embed::XVisualObject> xVisObject(mxChartModel, uno::UNO_QUERY);
            awt::Size aPageSize = xVisObject->getVisualAreaSize(embed::Aspects::MSOLE_CONTENT);

            awt::Size aSize = xShape->getSize();
            awt::Point aPos2 = xShape->getPosition();
            // rotated shapes need special handling...
            double fSin = fabs(sin(basegfx::deg2rad<100>(nRotation)));
            // remove part of height from X direction, if title is rotated down
            if( nRotation*0.01 > 180.0 )
                aPos2.X -= static_cast<sal_Int32>(fSin * aSize.Height + 0.5);
            // remove part of width from Y direction, if title is rotated up
            else if( nRotation*0.01 > 0.0 )
                aPos2.Y -= static_cast<sal_Int32>(fSin * aSize.Width + 0.5);

            double x = static_cast<double>(aPos2.X) / static_cast<double>(aPageSize.Width);
            double y = static_cast<double>(aPos2.Y) / static_cast<double>(aPageSize.Height);
            /*
            pFS->singleElement(FSNS(XML_c, XML_wMode), XML_val, "edge");
            pFS->singleElement(FSNS(XML_c, XML_hMode), XML_val, "edge");
                    */
            pFS->singleElement(FSNS(XML_c, XML_x), XML_val, OString::number(x));
            pFS->singleElement(FSNS(XML_c, XML_y), XML_val, OString::number(y));
            /*
            pFS->singleElement(FSNS(XML_c, XML_w), XML_val, "");
            pFS->singleElement(FSNS(XML_c, XML_h), XML_val, "");
                    */
            pFS->endElement(FSNS(XML_c, XML_manualLayout));
            pFS->endElement(FSNS(XML_c, XML_layout));
        }
    }

    if (!bIsChartex) {
        pFS->singleElement(FSNS(XML_c, XML_overlay), XML_val, "0");
    }

    if (!bIsChartex) {
        // shape properties
        if( xPropSet.is() )
        {
            exportShapeProps( xPropSet, bIsChartex );
        }
    }

    if (bIsChartex) {
        pFS->endElement( FSNS( XML_cx, XML_title ) );
    } else {
        pFS->endElement( FSNS( XML_c, XML_title ) );
    }
}

void ChartExport::exportPlotArea(const Reference< css::chart::XChartDocument >& xChartDoc,
        bool bIsChartex)
{
    Reference< chart2::XCoordinateSystemContainer > xBCooSysCnt( mxNewDiagram, uno::UNO_QUERY );
    if( ! xBCooSysCnt.is())
        return;

    // plot-area element

    FSHelperPtr pFS = GetFS();

    if (bIsChartex) {
        pFS->startElement(FSNS(XML_cx, XML_plotArea));
        pFS->startElement(FSNS(XML_cx, XML_plotAreaRegion));
    } else {
        pFS->startElement(FSNS(XML_c, XML_plotArea));

        Reference<beans::XPropertySet> xWall(mxNewDiagram, uno::UNO_QUERY);
        if( xWall.is() )
        {
            uno::Any aAny = xWall->getPropertyValue(u"RelativePosition"_ustr);
            if (aAny.hasValue())
            {
                chart2::RelativePosition aPos = aAny.get<chart2::RelativePosition>();
                aAny = xWall->getPropertyValue(u"RelativeSize"_ustr);
                chart2::RelativeSize aSize = aAny.get<chart2::RelativeSize>();
                uno::Reference< css::chart::XDiagramPositioning > xDiagramPositioning( xChartDoc->getDiagram(), uno::UNO_QUERY );
                exportManualLayout(aPos, aSize, xDiagramPositioning->isExcludingDiagramPositioning() );
            }
        }
    }

    // chart type
    const Sequence< Reference< chart2::XCoordinateSystem > >
        aCooSysSeq( xBCooSysCnt->getCoordinateSystems());

    // tdf#123647 Save empty chart as empty bar chart.
    if (!aCooSysSeq.hasElements())
    {
        assert(!bIsChartex);

        pFS->startElement(FSNS(XML_c, XML_barChart));
        pFS->singleElement(FSNS(XML_c, XML_barDir), XML_val, "col");
        pFS->singleElement(FSNS(XML_c, XML_grouping), XML_val, "clustered");
        pFS->singleElement(FSNS(XML_c, XML_varyColors), XML_val, "0");
        createAxes(true, false);
        pFS->endElement(FSNS(XML_c, XML_barChart));
    }

    for( const auto& rCS : aCooSysSeq )
    {
        Reference< chart2::XChartTypeContainer > xCTCnt( rCS, uno::UNO_QUERY );
        if( ! xCTCnt.is())
            continue;
        mnSeriesCount=0;
        const Sequence< Reference< chart2::XChartType > > aCTSeq( xCTCnt->getChartTypes());
        for( const auto& rCT : aCTSeq )
        {
            Reference< chart2::XDataSeriesContainer > xDSCnt( rCT, uno::UNO_QUERY );
            if( ! xDSCnt.is())
                return;
            Reference< chart2::XChartType > xChartType( rCT, uno::UNO_QUERY );
            if( ! xChartType.is())
                continue;
            // note: if xDSCnt.is() then also aCTSeq[nCTIdx]
            OUString aChartType( xChartType->getChartType());
            sal_Int32 eChartType = lcl_getChartType( aChartType );
            switch( eChartType )
            {
                case chart::TYPEID_BAR:
                    {
                        exportBarChart( xChartType );
                        break;
                    }
                case chart::TYPEID_AREA:
                    {
                        exportAreaChart( xChartType );
                        break;
                    }
                case chart::TYPEID_LINE:
                    {
                        exportLineChart( xChartType );
                        break;
                    }
                case chart::TYPEID_BUBBLE:
                    {
                        exportBubbleChart( xChartType );
                        break;
                    }
                case chart::TYPEID_FUNNEL:
                    {
                        exportFunnelChart( xChartType );
                        break;
                    }
                case chart::TYPEID_DOUGHNUT: // doesn't currently happen
                case chart::TYPEID_OFPIE:    // doesn't currently happen
                case chart::TYPEID_PIE:
                    {
                        sal_Int32 eCT = getChartType( );
                        if(eCT == chart::TYPEID_DOUGHNUT)
                        {
                            exportDoughnutChart( xChartType );
                        }
                        else
                        {

                            PropertySet xChartTypeProp(rCT);
                            chart2::PieChartSubType subtype(chart2::PieChartSubType_NONE);
                            if (!xChartTypeProp.getProperty(subtype, PROP_SubPieType))
                            {
                                subtype = chart2::PieChartSubType_NONE;
                            }
                            if (subtype != chart2::PieChartSubType_NONE)
                            {
                                const char* sSubType = "pie";   // default
                                switch (subtype) {
                                    case chart2::PieChartSubType_PIE:
                                        sSubType = "pie";
                                        break;
                                    case chart2::PieChartSubType_BAR:
                                        sSubType = "bar";
                                        break;
                                    default:
                                        assert(false);
                                }
                                double fSplitPos;
                                if (!xChartTypeProp.getProperty(fSplitPos,
                                            PROP_SplitPos)) {
                                    fSplitPos = 2;
                                }

                                exportOfPieChart(xChartType, sSubType, fSplitPos);
                            } else {
                                exportPieChart( xChartType );
                            }
                        }
                        break;
                    }
                case chart::TYPEID_RADARLINE:
                case chart::TYPEID_RADARAREA:
                    {
                        exportRadarChart( xChartType );
                        break;
                    }
                case chart::TYPEID_SCATTER:
                    {
                        exportScatterChart( xChartType );
                        break;
                    }
                case chart::TYPEID_STOCK:
                    {
                        exportStockChart( xChartType );
                        break;
                    }
                case chart::TYPEID_SURFACE:
                    {
                        exportSurfaceChart( xChartType );
                        break;
                    }
                default:
                    {
                        SAL_WARN("oox", "ChartExport::exportPlotArea -- not support chart type");
                        break;
                    }
            }

        }
    }

    if (bIsChartex) {
        pFS->endElement( FSNS( XML_cx, XML_plotAreaRegion ) );
    }

    //Axis Data
    exportAxes(bIsChartex);

    if (!bIsChartex) {
        // Data Table
        // not supported in chartex?
        exportDataTable();
    }

    // shape properties
    /*
     * Export the Plot area Shape Properties
     * eg: Fill and Outline
     */
    Reference< css::chart::X3DDisplay > xWallFloorSupplier( mxDiagram, uno::UNO_QUERY );
    // tdf#114139 For 2D charts Plot Area equivalent is Chart Wall.
    // Unfortunately LibreOffice doesn't have Plot Area equivalent for 3D charts.
    // It means that Plot Area couldn't be displayed and changed for 3D chars in LibreOffice.
    // We cannot write Wall attributes into Plot Area for 3D charts, because Wall us used as background wall.
    if( !mbIs3DChart && xWallFloorSupplier.is() )
    {
        Reference< beans::XPropertySet > xWallPropSet = xWallFloorSupplier->getWall();
        if( xWallPropSet.is() )
        {
            uno::Any aAny = xWallPropSet->getPropertyValue(u"LineStyle"_ustr);
            sal_Int32 eChartType = getChartType( );
            // Export LineStyle_NONE instead of default linestyle of PlotArea border, because LibreOffice
            // make invisible the Wall shape properties, in case of these charts. Or in the future set
            // the default LineStyle of these charts to LineStyle_NONE.
            bool noSupportWallProp = ( (eChartType == chart::TYPEID_PIE) || (eChartType == chart::TYPEID_RADARLINE) || (eChartType == chart::TYPEID_RADARAREA) );
            if ( noSupportWallProp && (aAny != drawing::LineStyle_NONE) )
            {
                xWallPropSet->setPropertyValue( u"LineStyle"_ustr, uno::Any(drawing::LineStyle_NONE) );
            }
            exportShapeProps( xWallPropSet, bIsChartex );
        }
    }

    if (bIsChartex) {
        pFS->endElement( FSNS( XML_cx, XML_plotArea ) );
    } else {
        pFS->endElement( FSNS( XML_c, XML_plotArea ) );
    }

}

void ChartExport::exportManualLayout(const css::chart2::RelativePosition& rPos,
                                     const css::chart2::RelativeSize& rSize,
                                     const bool bIsExcludingDiagramPositioning)
{
    // 2006 chart schema only
    FSHelperPtr pFS = GetFS();
    pFS->startElement(FSNS(XML_c, XML_layout));
    pFS->startElement(FSNS(XML_c, XML_manualLayout));

    // By default layoutTarget is set to "outer" and we shouldn't save it in that case
    if ( bIsExcludingDiagramPositioning )
    {
        pFS->singleElement(FSNS(XML_c, XML_layoutTarget), XML_val, "inner");
    }
    pFS->singleElement(FSNS(XML_c, XML_xMode), XML_val, "edge");
    pFS->singleElement(FSNS(XML_c, XML_yMode), XML_val, "edge");

    double x = rPos.Primary;
    double y = rPos.Secondary;
    const double w = rSize.Primary;
    const double h = rSize.Secondary;
    switch (rPos.Anchor)
    {
        case drawing::Alignment_LEFT:
            y -= (h/2);
        break;
        case drawing::Alignment_TOP_LEFT:
        break;
        case drawing::Alignment_BOTTOM_LEFT:
            y -= h;
        break;
        case drawing::Alignment_TOP:
            x -= (w/2);
        break;
        case drawing::Alignment_CENTER:
            x -= (w/2);
            y -= (h/2);
        break;
        case drawing::Alignment_BOTTOM:
            x -= (w/2);
            y -= h;
        break;
        case drawing::Alignment_TOP_RIGHT:
            x -= w;
        break;
        case drawing::Alignment_BOTTOM_RIGHT:
            x -= w;
            y -= h;
        break;
        case drawing::Alignment_RIGHT:
            y -= (h/2);
            x -= w;
        break;
        default:
            SAL_WARN("oox", "unhandled alignment case for manual layout export " << static_cast<sal_uInt16>(rPos.Anchor));
    }

    pFS->singleElement(FSNS(XML_c, XML_x), XML_val, OString::number(x));

    pFS->singleElement(FSNS(XML_c, XML_y), XML_val, OString::number(y));

    pFS->singleElement(FSNS(XML_c, XML_w), XML_val, OString::number(w));

    pFS->singleElement(FSNS(XML_c, XML_h), XML_val, OString::number(h));

    pFS->endElement(FSNS(XML_c, XML_manualLayout));
    pFS->endElement(FSNS(XML_c, XML_layout));
}

void ChartExport::exportFill( const Reference< XPropertySet >& xPropSet )
{
    // Similar to DrawingML::WriteFill, but gradient access via name
    if (!GetProperty( xPropSet, u"FillStyle"_ustr ))
        return;
    FillStyle aFillStyle(FillStyle_NONE);
    mAny >>= aFillStyle;

    // map full transparent background to no fill
    if (aFillStyle == FillStyle_SOLID && GetProperty( xPropSet, u"FillTransparence"_ustr ))
    {
        sal_Int16 nVal = 0;
        mAny >>= nVal;
        if ( nVal == 100 )
            aFillStyle = FillStyle_NONE;
    }
    OUString sFillTransparenceGradientName;
    if (aFillStyle == FillStyle_SOLID
        && GetProperty(xPropSet, u"FillTransparenceGradientName"_ustr) && (mAny >>= sFillTransparenceGradientName)
        && !sFillTransparenceGradientName.isEmpty())
    {
        awt::Gradient aTransparenceGradient;
        uno::Reference< lang::XMultiServiceFactory > xFact( getModel(), uno::UNO_QUERY );
        uno::Reference< container::XNameAccess > xTransparenceGradient(xFact->createInstance(u"com.sun.star.drawing.TransparencyGradientTable"_ustr), uno::UNO_QUERY);
        uno::Any rTransparenceValue = xTransparenceGradient->getByName(sFillTransparenceGradientName);
        rTransparenceValue >>= aTransparenceGradient;
        if (aTransparenceGradient.StartColor == 0xffffff && aTransparenceGradient.EndColor == 0xffffff)
            aFillStyle = FillStyle_NONE;
    }
    switch( aFillStyle )
    {
        case FillStyle_SOLID:
            exportSolidFill(xPropSet);
            break;
        case FillStyle_GRADIENT :
            exportGradientFill( xPropSet );
            break;
        case FillStyle_BITMAP :
            exportBitmapFill( xPropSet );
            break;
        case FillStyle_HATCH:
            exportHatch(xPropSet);
            break;
        case FillStyle_NONE:
            mpFS->singleElementNS(XML_a, XML_noFill);
            break;
        default:
            ;
    }
}

void ChartExport::exportSolidFill(const Reference< XPropertySet >& xPropSet)
{
    // Similar to DrawingML::WriteSolidFill, but gradient access via name
    // and currently no InteropGrabBag
    // get fill color
    sal_uInt32 nFillColor = 0;
    if (!GetProperty(xPropSet, u"FillColor"_ustr) || !(mAny >>= nFillColor))
        return;

    sal_Int32 nAlpha = MAX_PERCENT;
    if (GetProperty( xPropSet, u"FillTransparence"_ustr ))
    {
        sal_Int32 nTransparency = 0;
        mAny >>= nTransparency;
        // Calculate alpha value (see oox/source/drawingml/color.cxx : getTransparency())
        nAlpha = (MAX_PERCENT - ( PER_PERCENT * nTransparency ) );
    }
    // OOXML has no separate transparence gradient but uses transparency in the gradient stops.
    // So we merge transparency and color and use gradient fill in such case.
    basegfx::BGradient aTransparenceGradient;
    bool bNeedGradientFill(false);
    OUString sFillTransparenceGradientName;

    if (GetProperty(xPropSet, u"FillTransparenceGradientName"_ustr)
        && (mAny >>= sFillTransparenceGradientName)
        && !sFillTransparenceGradientName.isEmpty())
    {
        uno::Reference< lang::XMultiServiceFactory > xFact( getModel(), uno::UNO_QUERY );
        uno::Reference< container::XNameAccess > xTransparenceGradient(xFact->createInstance(u"com.sun.star.drawing.TransparencyGradientTable"_ustr), uno::UNO_QUERY);
        const uno::Any rTransparenceAny = xTransparenceGradient->getByName(sFillTransparenceGradientName);

        aTransparenceGradient = model::gradient::getFromAny(rTransparenceAny);
        basegfx::BColor aSingleColor;
        bNeedGradientFill = !aTransparenceGradient.GetColorStops().isSingleColor(aSingleColor);

        if (!bNeedGradientFill)
        {
            // Our alpha is a single gray color value.
            const sal_uInt8 nRed(aSingleColor.getRed() * 255.0);

            // drawingML alpha is a percentage on a 0..100000 scale.
            nAlpha = (255 - nRed) * oox::drawingml::MAX_PERCENT / 255;
        }
    }
    // write XML
    if (bNeedGradientFill)
    {
        // no longer create copy/PseudoColorGradient, use new API of
        // WriteGradientFill to express fix fill color
        mpFS->startElementNS(XML_a, XML_gradFill, XML_rotWithShape, "0");
        WriteGradientFill(nullptr, nFillColor, &aTransparenceGradient);
        mpFS->endElementNS(XML_a, XML_gradFill);
    }
    else
        WriteSolidFill(::Color(ColorTransparency, nFillColor & 0xffffff), nAlpha);
}

void ChartExport::exportHatch( const Reference< XPropertySet >& xPropSet )
{
    if (!xPropSet.is())
        return;

    if (GetProperty(xPropSet, u"FillHatchName"_ustr))
    {
        OUString aHatchName;
        mAny >>= aHatchName;
        uno::Reference< lang::XMultiServiceFactory > xFact( getModel(), uno::UNO_QUERY );
        uno::Reference< container::XNameAccess > xHatchTable( xFact->createInstance(u"com.sun.star.drawing.HatchTable"_ustr), uno::UNO_QUERY );
        uno::Any rValue = xHatchTable->getByName(aHatchName);
        css::drawing::Hatch aHatch;
        rValue >>= aHatch;
        WritePattFill(xPropSet, aHatch);
    }

}

void ChartExport::exportBitmapFill( const Reference< XPropertySet >& xPropSet )
{
    if( !xPropSet.is() )
        return;

    OUString sFillBitmapName;
    xPropSet->getPropertyValue(u"FillBitmapName"_ustr) >>= sFillBitmapName;

    uno::Reference< lang::XMultiServiceFactory > xFact( getModel(), uno::UNO_QUERY );
    try
    {
        uno::Reference< container::XNameAccess > xBitmapTable( xFact->createInstance(u"com.sun.star.drawing.BitmapTable"_ustr), uno::UNO_QUERY );
        uno::Any rValue = xBitmapTable->getByName( sFillBitmapName );
        if (rValue.has<uno::Reference<awt::XBitmap>>())
        {
            uno::Reference<awt::XBitmap> xBitmap = rValue.get<uno::Reference<awt::XBitmap>>();
            uno::Reference<graphic::XGraphic> xGraphic(xBitmap, uno::UNO_QUERY);
            if (xGraphic.is())
            {
                WriteXGraphicBlipFill(xPropSet, xGraphic, XML_a, true, true);
            }
        }
    }
    catch (const uno::Exception &)
    {
        TOOLS_WARN_EXCEPTION("oox", "ChartExport::exportBitmapFill");
    }
}

void ChartExport::exportGradientFill( const Reference< XPropertySet >& xPropSet )
{
    if( !xPropSet.is() )
        return;

    OUString sFillGradientName;
    xPropSet->getPropertyValue(u"FillGradientName"_ustr) >>= sFillGradientName;

    uno::Reference< lang::XMultiServiceFactory > xFact( getModel(), uno::UNO_QUERY );
    try
    {
        uno::Reference< container::XNameAccess > xGradient( xFact->createInstance(u"com.sun.star.drawing.GradientTable"_ustr), uno::UNO_QUERY );
        const uno::Any rGradientAny(xGradient->getByName( sFillGradientName ));
        const basegfx::BGradient aGradient = model::gradient::getFromAny(rGradientAny);
        basegfx::BColor aSingleColor;

        if (!aGradient.GetColorStops().isSingleColor(aSingleColor))
        {
            basegfx::BGradient aTransparenceGradient;
            mpFS->startElementNS(XML_a, XML_gradFill);
            OUString sFillTransparenceGradientName;

            if( (xPropSet->getPropertyValue(u"FillTransparenceGradientName"_ustr) >>= sFillTransparenceGradientName) && !sFillTransparenceGradientName.isEmpty())
            {
                uno::Reference< container::XNameAccess > xTransparenceGradient(xFact->createInstance(u"com.sun.star.drawing.TransparencyGradientTable"_ustr), uno::UNO_QUERY);
                const uno::Any rTransparenceAny(xTransparenceGradient->getByName(sFillTransparenceGradientName));

                aTransparenceGradient = model::gradient::getFromAny(rTransparenceAny);

                WriteGradientFill(&aGradient, 0, &aTransparenceGradient);
            }
            else if (GetProperty(xPropSet, u"FillTransparence"_ustr) )
            {
                // no longer create PseudoTransparencyGradient, use new API of
                // WriteGradientFill to express fix transparency
                sal_Int32 nTransparency = 0;
                mAny >>= nTransparency;
                // nTransparency is [0..100]%
                WriteGradientFill(&aGradient, 0, nullptr, nTransparency * 0.01);
            }
            else
            {
                WriteGradientFill(&aGradient, 0, nullptr);
            }

            mpFS->endElementNS(XML_a, XML_gradFill);
        }
    }
    catch (const uno::Exception &)
    {
        TOOLS_INFO_EXCEPTION("oox", "ChartExport::exportGradientFill");
    }
}

void ChartExport::exportDataTable( )
{
    // Not supported in chartex 2014 schema
    auto xDataTable = mxNewDiagram->getDataTable();
    if (!xDataTable.is())
        return;

    FSHelperPtr pFS = GetFS();
    uno::Reference<beans::XPropertySet> aPropSet(xDataTable, uno::UNO_QUERY);

    bool bShowVBorder = false;
    bool bShowHBorder = false;
    bool bShowOutline = false;
    bool bShowKeys = false;

    if (GetProperty(aPropSet, u"HBorder"_ustr))
        mAny >>= bShowHBorder;
    if (GetProperty(aPropSet, u"VBorder"_ustr))
        mAny >>= bShowVBorder;
    if (GetProperty(aPropSet, u"Outline"_ustr))
        mAny >>= bShowOutline;
    if (GetProperty(aPropSet, u"Keys"_ustr))
        mAny >>= bShowKeys;

    pFS->startElement(FSNS(XML_c, XML_dTable));

    if (bShowHBorder)
        pFS->singleElement(FSNS(XML_c, XML_showHorzBorder), XML_val, "1" );
    if (bShowVBorder)
        pFS->singleElement(FSNS(XML_c, XML_showVertBorder), XML_val, "1");
    if (bShowOutline)
        pFS->singleElement(FSNS(XML_c, XML_showOutline), XML_val, "1");
    if (bShowKeys)
        pFS->singleElement(FSNS(XML_c, XML_showKeys), XML_val, "1");

    exportShapeProps(aPropSet, false);
    exportTextProps(aPropSet, false);

    pFS->endElement(FSNS(XML_c, XML_dTable));
}

void ChartExport::exportAreaChart( const Reference< chart2::XChartType >& xChartType )
{
    FSHelperPtr pFS = GetFS();
    const std::vector<Sequence<Reference<chart2::XDataSeries> > > aSplitDataSeries = splitDataSeriesByAxis(xChartType);
    for (const auto& splitDataSeries : aSplitDataSeries)
    {
        if (!splitDataSeries.hasElements())
            continue;

        sal_Int32 nTypeId = XML_areaChart;
        if (mbIs3DChart)
            nTypeId = XML_area3DChart;
        pFS->startElement(FSNS(XML_c, nTypeId));

        exportGrouping();
        bool bPrimaryAxes = true;
        exportSeries_chart(xChartType, splitDataSeries, bPrimaryAxes);
        createAxes(bPrimaryAxes, false);
        //exportAxesId(bPrimaryAxes);

        pFS->endElement(FSNS(XML_c, nTypeId));
    }
}

void ChartExport::exportBarChart(const Reference< chart2::XChartType >& xChartType)
{
    sal_Int32 nTypeId = XML_barChart;
    if (mbIs3DChart)
        nTypeId = XML_bar3DChart;
    FSHelperPtr pFS = GetFS();

    const std::vector<Sequence<Reference<chart2::XDataSeries> > > aSplitDataSeries = splitDataSeriesByAxis(xChartType);
    for (const auto& splitDataSeries : aSplitDataSeries)
    {
        if (!splitDataSeries.hasElements())
            continue;

        pFS->startElement(FSNS(XML_c, nTypeId));
        // bar direction
        bool bVertical = false;
        Reference< XPropertySet > xPropSet(mxDiagram, uno::UNO_QUERY);
        if (GetProperty(xPropSet, u"Vertical"_ustr))
            mAny >>= bVertical;

        const char* bardir = bVertical ? "bar" : "col";
        pFS->singleElement(FSNS(XML_c, XML_barDir), XML_val, bardir);

        exportGrouping(true);

        exportVaryColors(xChartType);

        bool bPrimaryAxes = true;
        exportSeries_chart(xChartType, splitDataSeries, bPrimaryAxes);

        Reference< XPropertySet > xTypeProp(xChartType, uno::UNO_QUERY);

        if (xTypeProp.is() && GetProperty(xTypeProp, u"GapwidthSequence"_ustr))
        {
            uno::Sequence< sal_Int32 > aBarPositionSequence;
            mAny >>= aBarPositionSequence;
            if (aBarPositionSequence.hasElements())
            {
                sal_Int32 nGapWidth = aBarPositionSequence[0];
                pFS->singleElement(FSNS(XML_c, XML_gapWidth), XML_val, OString::number(nGapWidth));
            }
        }

        if (mbIs3DChart)
        {
            // Shape
            namespace cssc = css::chart;
            sal_Int32 nGeom3d = cssc::ChartSolidType::RECTANGULAR_SOLID;
            if (xPropSet.is() && GetProperty(xPropSet, u"SolidType"_ustr))
                mAny >>= nGeom3d;
            const char* sShapeType = nullptr;
            switch (nGeom3d)
            {
            case cssc::ChartSolidType::RECTANGULAR_SOLID:
                sShapeType = "box";
                break;
            case cssc::ChartSolidType::CONE:
                sShapeType = "cone";
                break;
            case cssc::ChartSolidType::CYLINDER:
                sShapeType = "cylinder";
                break;
            case cssc::ChartSolidType::PYRAMID:
                sShapeType = "pyramid";
                break;
            }
            pFS->singleElement(FSNS(XML_c, XML_shape), XML_val, sShapeType);
        }

        //overlap
        if (!mbIs3DChart && xTypeProp.is() && GetProperty(xTypeProp, u"OverlapSequence"_ustr))
        {
            uno::Sequence< sal_Int32 > aBarPositionSequence;
            mAny >>= aBarPositionSequence;
            if (aBarPositionSequence.hasElements())
            {
                sal_Int32 nOverlap = aBarPositionSequence[0];
                // Stacked/Percent Bar/Column chart Overlap-workaround
                // Export the Overlap value with 100% for stacked charts,
                // because the default overlap value of the Bar/Column chart is 0% and
                // LibreOffice do nothing with the overlap value in Stacked charts case,
                // unlike the MS Office, which is interpreted differently.
                if ((mbStacked || mbPercent) && nOverlap != 100)
                {
                    nOverlap = 100;
                    pFS->singleElement(FSNS(XML_c, XML_overlap), XML_val, OString::number(nOverlap));
                }
                else // Normal bar chart
                {
                    pFS->singleElement(FSNS(XML_c, XML_overlap), XML_val, OString::number(nOverlap));
                }
            }
        }

        createAxes(bPrimaryAxes, false);

        pFS->endElement(FSNS(XML_c, nTypeId));
    }
}

void ChartExport::exportBubbleChart( const Reference< chart2::XChartType >& xChartType )
{
    FSHelperPtr pFS = GetFS();
    const std::vector<Sequence<Reference<chart2::XDataSeries> > > aSplitDataSeries = splitDataSeriesByAxis(xChartType);
    for (const auto& splitDataSeries : aSplitDataSeries)
    {
        if (!splitDataSeries.hasElements())
            continue;

        pFS->startElement(FSNS(XML_c, XML_bubbleChart));

        exportVaryColors(xChartType);

        bool bPrimaryAxes = true;
        exportSeries_chart(xChartType, splitDataSeries, bPrimaryAxes);

        createAxes(bPrimaryAxes, false);

        pFS->endElement(FSNS(XML_c, XML_bubbleChart));
    }
}

void ChartExport::exportFunnelChart( const Reference< chart2::XChartType >& xChartType )
{
    FSHelperPtr pFS = GetFS();
    const std::vector<Sequence<Reference<chart2::XDataSeries> > > aSplitDataSeries = splitDataSeriesByAxis(xChartType);
    for (const auto& splitDataSeries : aSplitDataSeries)
    {
        if (!splitDataSeries.hasElements())
            continue;

        //exportVaryColors(xChartType);

        exportSeries_chartex(xChartType, splitDataSeries, "funnel");
    }
}

void ChartExport::exportDoughnutChart( const Reference< chart2::XChartType >& xChartType )
{
    FSHelperPtr pFS = GetFS();
    pFS->startElement(FSNS(XML_c, XML_doughnutChart));

    exportVaryColors(xChartType);

    bool bPrimaryAxes = true;
    exportAllSeries(xChartType, bPrimaryAxes);
    // firstSliceAng
    exportFirstSliceAng( );
    //FIXME: holeSize
    pFS->singleElement(FSNS(XML_c, XML_holeSize), XML_val, OString::number(50));

    pFS->endElement( FSNS( XML_c, XML_doughnutChart ) );
}

void ChartExport::exportOfPieChart(
        const Reference< chart2::XChartType >& xChartType,
        const char* sSubType,
        double fSplitPos)
{
    FSHelperPtr pFS = GetFS();
    pFS->startElement(FSNS(XML_c, XML_ofPieChart));

    pFS->singleElement(FSNS(XML_c, XML_ofPieType), XML_val, sSubType);

    exportVaryColors(xChartType);

    bool bPrimaryAxes = true;
    exportAllSeries(xChartType, bPrimaryAxes);

    pFS->singleElement(FSNS(XML_c, XML_splitType), XML_val, "pos");
    pFS->singleElement(FSNS(XML_c, XML_splitPos), XML_val, OString::number(fSplitPos));

    pFS->endElement( FSNS( XML_c, XML_ofPieChart ) );
}

namespace {

void writeDataLabelsRange(const FSHelperPtr& pFS, const XmlFilterBase* pFB, DataLabelsRange& rDLblsRange)
{
    if (rDLblsRange.empty())
        return;

    pFS->startElement(FSNS(XML_c, XML_extLst));
    pFS->startElement(FSNS(XML_c, XML_ext), XML_uri, "{02D57815-91ED-43cb-92C2-25804820EDAC}", FSNS(XML_xmlns, XML_c15), pFB->getNamespaceURL(OOX_NS(c15)));
    pFS->startElement(FSNS(XML_c15, XML_datalabelsRange));

    // Write cell range.
    pFS->startElement(FSNS(XML_c15, XML_f));
    pFS->writeEscaped(rDLblsRange.getRange());
    pFS->endElement(FSNS(XML_c15, XML_f));

    // Write all labels.
    pFS->startElement(FSNS(XML_c15, XML_dlblRangeCache));
    pFS->singleElement(FSNS(XML_c, XML_ptCount), XML_val, OString::number(rDLblsRange.count()));
    for (const auto& rLabelKV: rDLblsRange)
    {
        pFS->startElement(FSNS(XML_c, XML_pt), XML_idx, OString::number(rLabelKV.first));
        pFS->startElement(FSNS(XML_c, XML_v));
        pFS->writeEscaped(rLabelKV.second);
        pFS->endElement(FSNS( XML_c, XML_v ));
        pFS->endElement(FSNS(XML_c, XML_pt));
    }

    pFS->endElement(FSNS(XML_c15, XML_dlblRangeCache));

    pFS->endElement(FSNS(XML_c15, XML_datalabelsRange));
    pFS->endElement(FSNS(XML_c, XML_ext));
    pFS->endElement(FSNS(XML_c, XML_extLst));
}

}

void ChartExport::exportLineChart( const Reference< chart2::XChartType >& xChartType )
{
    FSHelperPtr pFS = GetFS();
    const std::vector<Sequence<Reference<chart2::XDataSeries> > > aSplitDataSeries = splitDataSeriesByAxis(xChartType);
    for (const auto& splitDataSeries : aSplitDataSeries)
    {
        if (!splitDataSeries.hasElements())
            continue;

        sal_Int32 nTypeId = XML_lineChart;
        if( mbIs3DChart )
            nTypeId = XML_line3DChart;
        pFS->startElement(FSNS(XML_c, nTypeId));

        exportGrouping( );

        exportVaryColors(xChartType);
        // TODO: show marker symbol in series?
        bool bPrimaryAxes = true;
        exportSeries_chart(xChartType, splitDataSeries, bPrimaryAxes);

        // show marker?
        sal_Int32 nSymbolType = css::chart::ChartSymbolType::NONE;
        Reference< XPropertySet > xPropSet( mxDiagram , uno::UNO_QUERY);
        if( GetProperty( xPropSet, u"SymbolType"_ustr ) )
            mAny >>= nSymbolType;

        if( !mbIs3DChart )
        {
            exportHiLowLines();
            exportUpDownBars(xChartType);
            const char* marker = nSymbolType == css::chart::ChartSymbolType::NONE? "0":"1";
            pFS->singleElement(FSNS(XML_c, XML_marker), XML_val, marker);
        }

        createAxes(bPrimaryAxes, true);

        pFS->endElement( FSNS( XML_c, nTypeId ) );
    }
}

void ChartExport::exportPieChart( const Reference< chart2::XChartType >& xChartType )
{
    FSHelperPtr pFS = GetFS();
    sal_Int32 nTypeId = XML_pieChart;
    if( mbIs3DChart )
        nTypeId = XML_pie3DChart;
    pFS->startElement(FSNS(XML_c, nTypeId));

    exportVaryColors(xChartType);

    bool bPrimaryAxes = true;
    exportAllSeries(xChartType, bPrimaryAxes);

    if( !mbIs3DChart )
    {
        // firstSliceAng
        exportFirstSliceAng( );
    }

    pFS->endElement( FSNS( XML_c, nTypeId ) );
}

void ChartExport::exportRadarChart( const Reference< chart2::XChartType >& xChartType)
{
    FSHelperPtr pFS = GetFS();
    pFS->startElement(FSNS(XML_c, XML_radarChart));

    // radarStyle
    sal_Int32 eChartType = getChartType( );
    const char* radarStyle = nullptr;
    if( eChartType == chart::TYPEID_RADARAREA )
        radarStyle = "filled";
    else
        radarStyle = "marker";
    pFS->singleElement(FSNS(XML_c, XML_radarStyle), XML_val, radarStyle);

    exportVaryColors(xChartType);
    bool bPrimaryAxes = true;
    exportAllSeries(xChartType, bPrimaryAxes);
    createAxes(bPrimaryAxes, false);

    pFS->endElement( FSNS( XML_c, XML_radarChart ) );
}

void ChartExport::exportScatterChartSeries( const Reference< chart2::XChartType >& xChartType,
        const css::uno::Sequence<css::uno::Reference<chart2::XDataSeries>>* pSeries)
{
    FSHelperPtr pFS = GetFS();
    pFS->startElement(FSNS(XML_c, XML_scatterChart));
    // TODO:scatterStyle

    sal_Int32 nSymbolType = css::chart::ChartSymbolType::NONE;
    Reference< XPropertySet > xPropSet( mxDiagram , uno::UNO_QUERY);
    if( GetProperty( xPropSet, u"SymbolType"_ustr ) )
        mAny >>= nSymbolType;

    const char* scatterStyle = "lineMarker";
    if (nSymbolType == css::chart::ChartSymbolType::NONE)
    {
        scatterStyle = "line";
    }

    pFS->singleElement(FSNS(XML_c, XML_scatterStyle), XML_val, scatterStyle);

    exportVaryColors(xChartType);
    // FIXME: should export xVal and yVal
    bool bPrimaryAxes = true;
    if (pSeries)
        exportSeries_chart(xChartType, *pSeries, bPrimaryAxes);
    createAxes(bPrimaryAxes, false);
    //exportAxesId(bPrimaryAxes);

    pFS->endElement( FSNS( XML_c, XML_scatterChart ) );
}

void ChartExport::exportScatterChart( const Reference< chart2::XChartType >& xChartType )
{
    const std::vector<Sequence<Reference<chart2::XDataSeries> > > aSplitDataSeries = splitDataSeriesByAxis(xChartType);
    bool bExported = false;
    for (const auto& splitDataSeries : aSplitDataSeries)
    {
        if (!splitDataSeries.hasElements())
            continue;

        bExported = true;
        exportScatterChartSeries(xChartType, &splitDataSeries);
    }
    if (!bExported)
        exportScatterChartSeries(xChartType, nullptr);
}

void ChartExport::exportStockChart( const Reference< chart2::XChartType >& xChartType )
{
    FSHelperPtr pFS = GetFS();
    const std::vector<Sequence<Reference<chart2::XDataSeries> > > aSplitDataSeries = splitDataSeriesByAxis(xChartType);
    for (const auto& splitDataSeries : aSplitDataSeries)
    {
        if (!splitDataSeries.hasElements())
            continue;

        pFS->startElement(FSNS(XML_c, XML_stockChart));

        bool bPrimaryAxes = true;
        exportCandleStickSeries(splitDataSeries, bPrimaryAxes);

        // export stock properties
        Reference< css::chart::XStatisticDisplay > xStockPropProvider(mxDiagram, uno::UNO_QUERY);
        if (xStockPropProvider.is())
        {
            exportHiLowLines();
            exportUpDownBars(xChartType);
        }

        createAxes(bPrimaryAxes, false);

        pFS->endElement(FSNS(XML_c, XML_stockChart));
    }
}

void ChartExport::exportHiLowLines()
{
    FSHelperPtr pFS = GetFS();
    // export the chart property
    Reference< css::chart::XStatisticDisplay > xChartPropProvider( mxDiagram, uno::UNO_QUERY );

    if (!xChartPropProvider.is())
        return;

    Reference< beans::XPropertySet > xStockPropSet = xChartPropProvider->getMinMaxLine();
    if( !xStockPropSet.is() )
        return;

    pFS->startElement(FSNS(XML_c, XML_hiLowLines));
    exportShapeProps( xStockPropSet, false );
    pFS->endElement( FSNS( XML_c, XML_hiLowLines ) );
}

void ChartExport::exportUpDownBars( const Reference< chart2::XChartType >& xChartType)
{
    if(xChartType->getChartType() != "com.sun.star.chart2.CandleStickChartType")
        return;

    FSHelperPtr pFS = GetFS();
    // export the chart property
    Reference< css::chart::XStatisticDisplay > xChartPropProvider( mxDiagram, uno::UNO_QUERY );
    if(!xChartPropProvider.is())
        return;

    //  updownbar
    pFS->startElement(FSNS(XML_c, XML_upDownBars));
    // TODO: gapWidth
    pFS->singleElement(FSNS(XML_c, XML_gapWidth), XML_val, OString::number(150));

    Reference< beans::XPropertySet > xChartPropSet = xChartPropProvider->getUpBar();
    if( xChartPropSet.is() )
    {
        pFS->startElement(FSNS(XML_c, XML_upBars));
        // For Linechart with UpDownBars, spPr is not getting imported
        // so no need to call the exportShapeProps() for LineChart
        if(xChartType->getChartType() == "com.sun.star.chart2.CandleStickChartType")
        {
            exportShapeProps(xChartPropSet, false);
        }
        pFS->endElement( FSNS( XML_c, XML_upBars ) );
    }
    xChartPropSet = xChartPropProvider->getDownBar();
    if( xChartPropSet.is() )
    {
        pFS->startElement(FSNS(XML_c, XML_downBars));
        if(xChartType->getChartType() == "com.sun.star.chart2.CandleStickChartType")
        {
            exportShapeProps(xChartPropSet, false);
        }
        pFS->endElement( FSNS( XML_c, XML_downBars ) );
    }
    pFS->endElement( FSNS( XML_c, XML_upDownBars ) );
}

void ChartExport::exportSurfaceChart( const Reference< chart2::XChartType >& xChartType )
{
    FSHelperPtr pFS = GetFS();
    sal_Int32 nTypeId = XML_surfaceChart;
    if( mbIs3DChart )
        nTypeId = XML_surface3DChart;
    pFS->startElement(FSNS(XML_c, nTypeId));
    exportVaryColors(xChartType);
    bool bPrimaryAxes = true;
    exportAllSeries(xChartType, bPrimaryAxes);
    createAxes(bPrimaryAxes, false);

    pFS->endElement( FSNS( XML_c, nTypeId ) );
}

void ChartExport::exportAllSeries(const Reference<chart2::XChartType>& xChartType, bool& rPrimaryAxes)
{
    Reference< chart2::XDataSeriesContainer > xDSCnt( xChartType, uno::UNO_QUERY );
    if( ! xDSCnt.is())
        return;

    // export dataseries for current chart-type
    Sequence< Reference< chart2::XDataSeries > > aSeriesSeq( xDSCnt->getDataSeries());
    exportSeries_chart(xChartType, aSeriesSeq, rPrimaryAxes);
}

void ChartExport::exportVaryColors(const Reference<chart2::XChartType>& xChartType)
{
    FSHelperPtr pFS = GetFS();
    try
    {
        Reference<chart2::XDataSeries> xDataSeries = getPrimaryDataSeries(xChartType);
        Reference<beans::XPropertySet> xDataSeriesProps(xDataSeries, uno::UNO_QUERY_THROW);
        Any aAnyVaryColors = xDataSeriesProps->getPropertyValue(u"VaryColorsByPoint"_ustr);
        bool bVaryColors = false;
        aAnyVaryColors >>= bVaryColors;
        pFS->singleElement(FSNS(XML_c, XML_varyColors), XML_val, ToPsz10(bVaryColors));
    }
    catch (...)
    {
        pFS->singleElement(FSNS(XML_c, XML_varyColors), XML_val, "0");
    }
}

void ChartExport::exportSeries_chart( const Reference<chart2::XChartType>& xChartType,
        const Sequence<Reference<chart2::XDataSeries> >& rSeriesSeq,
        bool& rPrimaryAxes)
{
    OUString aLabelRole = xChartType->getRoleOfSequenceForSeriesLabel();
    OUString aChartType( xChartType->getChartType());
    sal_Int32 eChartType = lcl_getChartType( aChartType );

    for( const auto& rSeries : rSeriesSeq )
    {
        // export series
        Reference< chart2::data::XDataSource > xSource( rSeries, uno::UNO_QUERY );
        if( xSource.is())
        {
            Reference< chart2::XDataSeries > xDataSeries( xSource, uno::UNO_QUERY );
            Sequence< Reference< chart2::data::XLabeledDataSequence > > aSeqCnt(
                xSource->getDataSequences());
            // search for main sequence and create a series element
            {
                sal_Int32 nMainSequenceIndex = -1;
                sal_Int32 nSeriesLength = 0;
                Reference< chart2::data::XDataSequence > xValuesSeq;
                Reference< chart2::data::XDataSequence > xLabelSeq;
                sal_Int32 nSeqIdx=0;
                for( ; nSeqIdx<aSeqCnt.getLength(); ++nSeqIdx )
                {
                    Reference< chart2::data::XDataSequence > xTempValueSeq( aSeqCnt[nSeqIdx]->getValues() );
                    if( nMainSequenceIndex==-1 )
                    {
                        Reference< beans::XPropertySet > xSeqProp( xTempValueSeq, uno::UNO_QUERY );
                        OUString aRole;
                        if( xSeqProp.is())
                            xSeqProp->getPropertyValue(u"Role"_ustr) >>= aRole;
                        // "main" sequence
                        if( aRole == aLabelRole )
                        {
                            xValuesSeq.set( xTempValueSeq );
                            xLabelSeq.set( aSeqCnt[nSeqIdx]->getLabel());
                            nMainSequenceIndex = nSeqIdx;
                        }
                    }
                    sal_Int32 nSequenceLength = (xTempValueSeq.is()? xTempValueSeq->getData().getLength() : sal_Int32(0));
                    if( nSeriesLength < nSequenceLength )
                        nSeriesLength = nSequenceLength;
                }

                // have found the main sequence, then xValuesSeq and
                // xLabelSeq contain those.  Otherwise both are empty
                FSHelperPtr pFS = GetFS();

                pFS->startElement(FSNS(XML_c, XML_ser));

                // TODO: idx and order
                pFS->singleElement( FSNS( XML_c, XML_idx ),
                    XML_val, OString::number(mnSeriesCount) );
                pFS->singleElement( FSNS( XML_c, XML_order ),
                    XML_val, OString::number(mnSeriesCount++) );

                // export label
                if( xLabelSeq.is() )
                    exportSeriesText( xLabelSeq, false );

                Reference<XPropertySet> xPropSet(xDataSeries, UNO_QUERY_THROW);
                if( GetProperty( xPropSet, u"AttachedAxisIndex"_ustr) )
                {
                    sal_Int32 nLocalAttachedAxis = 0;
                    mAny >>= nLocalAttachedAxis;
                    rPrimaryAxes = isPrimaryAxes(nLocalAttachedAxis);
                }

                // export shape properties
                Reference< XPropertySet > xOldPropSet = SchXMLSeriesHelper::createOldAPISeriesPropertySet(
                    rSeries, getModel() );
                if( xOldPropSet.is() )
                {
                    exportShapeProps( xOldPropSet, false );
                }

                switch( eChartType )
                {
                    case chart::TYPEID_BUBBLE:
                    case chart::TYPEID_HORBAR:
                    case chart::TYPEID_BAR:
                    {
                        pFS->singleElement(FSNS(XML_c, XML_invertIfNegative), XML_val, "0");
                    }
                    break;
                    case chart::TYPEID_LINE:
                    {
                        exportMarker(xOldPropSet);
                        break;
                    }
                    case chart::TYPEID_PIE:
                    case chart::TYPEID_DOUGHNUT:
                    {
                        if( xOldPropSet.is() && GetProperty( xOldPropSet, u"SegmentOffset"_ustr) )
                        {
                            sal_Int32 nOffset = 0;
                            mAny >>= nOffset;
                            pFS->singleElement( FSNS( XML_c, XML_explosion ),
                                XML_val, OString::number( nOffset ) );
                        }
                        break;
                    }
                    case chart::TYPEID_SCATTER:
                    {
                        exportMarker(xOldPropSet);
                        break;
                    }
                    case chart::TYPEID_RADARLINE:
                    {
                        exportMarker(xOldPropSet);
                        break;
                    }
                }

                // export data points
                exportDataPoints( uno::Reference< beans::XPropertySet >( rSeries, uno::UNO_QUERY ), nSeriesLength, eChartType );

                DataLabelsRange aDLblsRange;
                // export data labels
                exportDataLabels(rSeries, nSeriesLength, eChartType, aDLblsRange, false);

                exportTrendlines( rSeries );

                if( eChartType != chart::TYPEID_PIE &&
                        eChartType != chart::TYPEID_RADARLINE )
                {
                    //export error bars here
                    Reference< XPropertySet > xSeriesPropSet( xSource, uno::UNO_QUERY );
                    Reference< XPropertySet > xErrorBarYProps;
                    xSeriesPropSet->getPropertyValue(u"ErrorBarY"_ustr) >>= xErrorBarYProps;
                    if(xErrorBarYProps.is())
                        exportErrorBar(xErrorBarYProps, true);
                    if (eChartType != chart::TYPEID_BAR &&
                            eChartType != chart::TYPEID_HORBAR)
                    {
                        Reference< XPropertySet > xErrorBarXProps;
                        xSeriesPropSet->getPropertyValue(u"ErrorBarX"_ustr) >>= xErrorBarXProps;
                        if(xErrorBarXProps.is())
                            exportErrorBar(xErrorBarXProps, false);
                    }
                }

                // export categories
                if( eChartType != chart::TYPEID_SCATTER && eChartType != chart::TYPEID_BUBBLE && mxCategoriesValues.is() )
                    exportSeriesCategory( mxCategoriesValues );

                if( (eChartType == chart::TYPEID_SCATTER)
                    || (eChartType == chart::TYPEID_BUBBLE) )
                {
                    // export xVal
                    Reference< chart2::data::XLabeledDataSequence > xSequence( lcl_getDataSequenceByRole( aSeqCnt, u"values-x"_ustr ) );
                    if( xSequence.is() )
                    {
                        Reference< chart2::data::XDataSequence > xValues( xSequence->getValues() );
                        if( xValues.is() )
                            exportSeriesValues( xValues, XML_xVal );
                    }
                    else if( mxCategoriesValues.is() )
                        exportSeriesCategory( mxCategoriesValues, XML_xVal );
                }

                if( eChartType == chart::TYPEID_BUBBLE )
                {
                    // export yVal
                    Reference< chart2::data::XLabeledDataSequence > xSequence( lcl_getDataSequenceByRole( aSeqCnt, u"values-y"_ustr ) );
                    if( xSequence.is() )
                    {
                        Reference< chart2::data::XDataSequence > xValues( xSequence->getValues() );
                        if( xValues.is() )
                            exportSeriesValues( xValues, XML_yVal );
                    }
                }

                // export values
                if( xValuesSeq.is() )
                {
                    sal_Int32 nYValueType = XML_val;
                    if( eChartType == chart::TYPEID_SCATTER )
                        nYValueType = XML_yVal;
                    else if( eChartType == chart::TYPEID_BUBBLE )
                        nYValueType = XML_bubbleSize;
                    exportSeriesValues( xValuesSeq, nYValueType );
                }

                if( eChartType == chart::TYPEID_SCATTER
                        || eChartType == chart::TYPEID_LINE )
                    exportSmooth();

                // tdf103988: "corrupted" files with Bubble chart opening in MSO
                if( eChartType == chart::TYPEID_BUBBLE )
                    pFS->singleElement(FSNS(XML_c, XML_bubble3D), XML_val, "0");

                if (!aDLblsRange.empty())
                    writeDataLabelsRange(pFS, GetFB(), aDLblsRange);

                pFS->endElement( FSNS( XML_c, XML_ser ) );
            }
        }
    }
}

void ChartExport::exportSeries_chartex( const Reference<chart2::XChartType>& xChartType,
        const Sequence<Reference<chart2::XDataSeries> >& rSeriesSeq,
        const char* sTypeName)
{
    OUString aLabelRole = xChartType->getRoleOfSequenceForSeriesLabel();
    OUString aChartType( xChartType->getChartType());
    sal_Int32 eChartType = lcl_getChartType( aChartType );

    sal_Int32 nSeriesCnt = 0;
    for( const auto& rSeries : rSeriesSeq )
    {
        // export series
        Reference< chart2::data::XDataSource > xSource( rSeries, uno::UNO_QUERY );
        if( xSource.is())
        {
            FSHelperPtr pFS = GetFS();
            pFS->startElement(FSNS(XML_cx, XML_series), XML_layoutId, sTypeName);

            Sequence< Reference< chart2::data::XLabeledDataSequence > > aSeqCnt(
                xSource->getDataSequences());

            // search for main sequence and create a series element
            sal_Int32 nMainSequenceIndex = -1;
            sal_Int32 nSeriesLength = 0;
            Reference< chart2::data::XDataSequence > xLabelSeq;
            sal_Int32 nSeqIdx=0;
            for( ; nSeqIdx<aSeqCnt.getLength(); ++nSeqIdx )
            {
                Reference< chart2::data::XDataSequence > xTempValueSeq( aSeqCnt[nSeqIdx]->getValues() );
                if( nMainSequenceIndex==-1 )
                {
                    Reference< beans::XPropertySet > xSeqProp( xTempValueSeq, uno::UNO_QUERY );
                    OUString aRole;
                    if( xSeqProp.is())
                        xSeqProp->getPropertyValue(u"Role"_ustr) >>= aRole;
                    // "main" sequence
                    if( aRole == aLabelRole )
                    {
                        xLabelSeq.set( aSeqCnt[nSeqIdx]->getLabel());
                        nMainSequenceIndex = nSeqIdx;
                    }
                }
                sal_Int32 nSequenceLength = (xTempValueSeq.is()? xTempValueSeq->getData().getLength() : sal_Int32(0));
                if( nSeriesLength < nSequenceLength )
                    nSeriesLength = nSequenceLength;
            }

            // export label
            if( xLabelSeq.is() )
                exportSeriesText( xLabelSeq, true );

            // export shape properties
            Reference< XPropertySet > xOldPropSet = SchXMLSeriesHelper::createOldAPISeriesPropertySet(
                rSeries, getModel() );
            if( xOldPropSet.is() )
            {
                exportShapeProps( xOldPropSet, true );
            }

            DataLabelsRange aDLblsRange;
            // export data labels
            exportDataLabels(rSeries, nSeriesLength, eChartType, aDLblsRange, true);

            // dataId links to the correct data set in the <cx:chartData>. See
            // DATA_ID_COMMENT
            pFS->singleElement(FSNS(XML_cx, XML_dataId), XML_val,
                    OString::number(nSeriesCnt++));

            // layoutPr

            // axisId

            // extLst

            pFS->endElement(FSNS(XML_cx, XML_series));
        }
    }
}

void ChartExport::exportCandleStickSeries(
    const Sequence< Reference< chart2::XDataSeries > > & aSeriesSeq,
    bool& rPrimaryAxes)
{
    for( const Reference< chart2::XDataSeries >& xSeries : aSeriesSeq )
    {
        rPrimaryAxes = lcl_isSeriesAttachedToFirstAxis(xSeries);

        Reference< chart2::data::XDataSource > xSource( xSeries, uno::UNO_QUERY );
        if( xSource.is())
        {
            // export series in correct order (as we don't store roles)
            // with japanese candlesticks: open, low, high, close
            // otherwise: low, high, close
            Sequence< Reference< chart2::data::XLabeledDataSequence > > aSeqCnt(
                xSource->getDataSequences());

            const char* sSeries[] = {"values-first","values-max","values-min","values-last",nullptr};

            for( sal_Int32 idx = 0; sSeries[idx] != nullptr ; idx++ )
            {
                Reference< chart2::data::XLabeledDataSequence > xLabeledSeq( lcl_getDataSequenceByRole( aSeqCnt, OUString::createFromAscii(sSeries[idx]) ) );
                if( xLabeledSeq.is())
                {
                    Reference< chart2::data::XDataSequence > xLabelSeq( xLabeledSeq->getLabel());
                    Reference< chart2::data::XDataSequence > xValueSeq( xLabeledSeq->getValues());
                    {
                        FSHelperPtr pFS = GetFS();
                        pFS->startElement(FSNS(XML_c, XML_ser));

                        // TODO: idx and order
                        // idx attribute should start from 1 and not from 0.
                        pFS->singleElement( FSNS( XML_c, XML_idx ),
                                XML_val, OString::number(idx+1) );
                        pFS->singleElement( FSNS( XML_c, XML_order ),
                                XML_val, OString::number(idx+1) );

                        // export label
                        if( xLabelSeq.is() )
                            exportSeriesText( xLabelSeq, false );

                        // TODO:export shape properties

                        // export categories
                        if( mxCategoriesValues.is() )
                            exportSeriesCategory( mxCategoriesValues );

                        // export values
                        if( xValueSeq.is() )
                            exportSeriesValues( xValueSeq );

                        pFS->endElement( FSNS( XML_c, XML_ser ) );
                    }
                }
            }
        }
    }
}

void ChartExport::exportSeriesText( const Reference< chart2::data::XDataSequence > & xValueSeq,
        bool bIsChartex)
{
    FSHelperPtr pFS = GetFS();

    OUString aLabelString = lcl_flattenStringSequence(lcl_getLabelSequence(xValueSeq));

    if (bIsChartex) {
        lcl_writeChartexString(pFS, aLabelString);
    } else {
        pFS->startElement(FSNS(XML_c, XML_tx));

        OUString aCellRange =  xValueSeq->getSourceRangeRepresentation();
        aCellRange = parseFormula( aCellRange );
        pFS->startElement(FSNS(XML_c, XML_strRef));

        pFS->startElement(FSNS(XML_c, XML_f));
        pFS->writeEscaped( aCellRange );
        pFS->endElement( FSNS( XML_c, XML_f ) );

        pFS->startElement(FSNS(XML_c, XML_strCache));
        pFS->singleElement(FSNS(XML_c, XML_ptCount), XML_val, "1");
        pFS->startElement(FSNS(XML_c, XML_pt), XML_idx, "0");
        pFS->startElement(FSNS(XML_c, XML_v));
        pFS->writeEscaped( aLabelString );
        pFS->endElement( FSNS( XML_c, XML_v ) );
        pFS->endElement( FSNS( XML_c, XML_pt ) );
        pFS->endElement( FSNS( XML_c, XML_strCache ) );
        pFS->endElement( FSNS( XML_c, XML_strRef ) );
        pFS->endElement( FSNS( XML_c, XML_tx ) );
    }
}

void ChartExport::exportSeriesCategory( const Reference< chart2::data::XDataSequence > & xValueSeq, sal_Int32 nValueType )
{
    FSHelperPtr pFS = GetFS();
    pFS->startElement(FSNS(XML_c, nValueType));

    OUString aCellRange = xValueSeq.is() ? xValueSeq->getSourceRangeRepresentation() : OUString();
    const Sequence< Sequence< OUString >> aFinalSplitSource = (nValueType == XML_cat) ? getSplitCategoriesList(aCellRange) : Sequence< Sequence< OUString>>(0);
    aCellRange = parseFormula( aCellRange );

    if(aFinalSplitSource.getLength() > 1)
    {
        // export multi level category axis labels
        pFS->startElement(FSNS(XML_c, XML_multiLvlStrRef));

        pFS->startElement(FSNS(XML_c, XML_f));
        pFS->writeEscaped(aCellRange);
        pFS->endElement(FSNS(XML_c, XML_f));

        pFS->startElement(FSNS(XML_c, XML_multiLvlStrCache));
        pFS->singleElement(FSNS(XML_c, XML_ptCount), XML_val, OString::number(aFinalSplitSource[0].getLength()));
        for(const auto& rSeq : aFinalSplitSource)
        {
            pFS->startElement(FSNS(XML_c, XML_lvl));
            for(sal_Int32 j = 0; j < rSeq.getLength(); j++)
            {
                if(!rSeq[j].isEmpty())
                {
                    pFS->startElement(FSNS(XML_c, XML_pt), XML_idx, OString::number(j));
                    pFS->startElement(FSNS(XML_c, XML_v));
                    pFS->writeEscaped(rSeq[j]);
                    pFS->endElement(FSNS(XML_c, XML_v));
                    pFS->endElement(FSNS(XML_c, XML_pt));
                }
            }
            pFS->endElement(FSNS(XML_c, XML_lvl));
        }

        pFS->endElement(FSNS(XML_c, XML_multiLvlStrCache));
        pFS->endElement(FSNS(XML_c, XML_multiLvlStrRef));
    }
    else
    {
        // export single category axis labels
        bool bWriteDateCategories = mbHasDateCategories && (nValueType == XML_cat);
        OUString aNumberFormatString;
        if (bWriteDateCategories)
        {
            Reference< css::chart::XAxisXSupplier > xAxisXSupp( mxDiagram, uno::UNO_QUERY );
            if( xAxisXSupp.is())
            {
                Reference< XPropertySet > xAxisProp = xAxisXSupp->getXAxis();
                if (GetProperty(xAxisProp, u"NumberFormat"_ustr))
                {
                    sal_Int32 nKey = 0;
                    mAny >>= nKey;
                    aNumberFormatString = getNumberFormatCode(nKey);
                }
            }
            if (aNumberFormatString.isEmpty())
                bWriteDateCategories = false;
        }

        pFS->startElement(FSNS(XML_c, bWriteDateCategories ? XML_numRef : XML_strRef));

        pFS->startElement(FSNS(XML_c, XML_f));
        pFS->writeEscaped(aCellRange);
        pFS->endElement(FSNS(XML_c, XML_f));

        pFS->startElement(FSNS(XML_c, bWriteDateCategories ? XML_numCache : XML_strCache));
        if (bWriteDateCategories)
        {
            pFS->startElement(FSNS(XML_c, XML_formatCode));
            pFS->writeEscaped(aNumberFormatString);
            pFS->endElement(FSNS(XML_c, XML_formatCode));

            std::vector<double> aDateCategories = lcl_getAllValuesFromSequence(xValueSeq);
            const sal_Int32 ptCount = aDateCategories.size();
            pFS->singleElement(FSNS(XML_c, XML_ptCount), XML_val, OString::number(ptCount));
            for (sal_Int32 i = 0; i < ptCount; i++)
            {
                if (!std::isnan(aDateCategories[i]))
                {
                    pFS->startElement(FSNS(XML_c, XML_pt), XML_idx, OString::number(i));
                    pFS->startElement(FSNS(XML_c, XML_v));
                    pFS->write(OString::number(aDateCategories[i]));
                    pFS->endElement(FSNS(XML_c, XML_v));
                    pFS->endElement(FSNS(XML_c, XML_pt));
                }
            }
        }
        else
        {
            std::vector<OUString> aCategories;
            lcl_fillCategoriesIntoStringVector(xValueSeq, aCategories);
            const sal_Int32 ptCount = aCategories.size();
            pFS->singleElement(FSNS(XML_c, XML_ptCount), XML_val, OString::number(ptCount));
            for (sal_Int32 i = 0; i < ptCount; i++)
            {
                pFS->startElement(FSNS(XML_c, XML_pt), XML_idx, OString::number(i));
                pFS->startElement(FSNS(XML_c, XML_v));
                pFS->writeEscaped(aCategories[i]);
                pFS->endElement(FSNS(XML_c, XML_v));
                pFS->endElement(FSNS(XML_c, XML_pt));
            }
        }

        pFS->endElement(FSNS(XML_c, bWriteDateCategories ? XML_numCache : XML_strCache));
        pFS->endElement(FSNS(XML_c, bWriteDateCategories ? XML_numRef : XML_strRef));
    }

    pFS->endElement( FSNS( XML_c, nValueType ) );
}

void ChartExport::exportSeriesValues( const Reference< chart2::data::XDataSequence > & xValueSeq, sal_Int32 nValueType )
{
    FSHelperPtr pFS = GetFS();
    pFS->startElement(FSNS(XML_c, nValueType));

    OUString aCellRange = xValueSeq.is() ? xValueSeq->getSourceRangeRepresentation() : OUString();
    aCellRange = parseFormula( aCellRange );
    // TODO: need to handle XML_multiLvlStrRef according to aCellRange
    pFS->startElement(FSNS(XML_c, XML_numRef));

    pFS->startElement(FSNS(XML_c, XML_f));
    pFS->writeEscaped( aCellRange );
    pFS->endElement( FSNS( XML_c, XML_f ) );

    ::std::vector< double > aValues = lcl_getAllValuesFromSequence( xValueSeq );
    sal_Int32 ptCount = aValues.size();
    pFS->startElement(FSNS(XML_c, XML_numCache));
    pFS->startElement(FSNS(XML_c, XML_formatCode));
    OUString sNumberFormatString(u"General"_ustr);
    const sal_Int32 nKey = xValueSeq.is() ? xValueSeq->getNumberFormatKeyByIndex(-1) : 0;
    if (nKey > 0)
        sNumberFormatString = getNumberFormatCode(nKey);
    pFS->writeEscaped(sNumberFormatString);
    pFS->endElement( FSNS( XML_c, XML_formatCode ) );
    pFS->singleElement(FSNS(XML_c, XML_ptCount), XML_val, OString::number(ptCount));

    for( sal_Int32 i = 0; i < ptCount; i++ )
    {
        if (!std::isnan(aValues[i]))
        {
            pFS->startElement(FSNS(XML_c, XML_pt), XML_idx, OString::number(i));
            pFS->startElement(FSNS(XML_c, XML_v));
            pFS->write(aValues[i]);
            pFS->endElement(FSNS(XML_c, XML_v));
            pFS->endElement(FSNS(XML_c, XML_pt));
        }
    }

    pFS->endElement( FSNS( XML_c, XML_numCache ) );
    pFS->endElement( FSNS( XML_c, XML_numRef ) );
    pFS->endElement( FSNS( XML_c, nValueType ) );
}

void ChartExport::exportShapeProps( const Reference< XPropertySet >& xPropSet,
        bool bIsChartex)
{
    sal_Int32 nChartNS = bIsChartex ? XML_cx : XML_c;
    FSHelperPtr pFS = GetFS();
    pFS->startElement(FSNS(nChartNS, XML_spPr));

    exportFill( xPropSet );
    WriteOutline( xPropSet, getModel() );

    pFS->endElement( FSNS( nChartNS, XML_spPr ) );
}

void ChartExport::exportTextProps(const Reference<XPropertySet>& xPropSet,
        bool bIsChartex)
{
    FSHelperPtr pFS = GetFS();

    const sal_Int32 nChartNS = bIsChartex ? XML_cx : XML_c;
    pFS->startElement(FSNS(nChartNS, XML_txPr));

    sal_Int32 nRotation = 0;
    const char* textWordWrap = nullptr;

    if (auto xServiceInfo = uno::Reference<lang::XServiceInfo>(xPropSet, uno::UNO_QUERY))
    {
        double fMultiplier = 0.0;
        // We have at least two possible units of returned value: degrees (e.g., for data labels),
        // and 100ths of degree (e.g., for axes labels). The latter is returned as an Any wrapping
        // a sal_Int32 value (see WrappedTextRotationProperty::convertInnerToOuterValue), while
        // the former is double. So we could test the contained type to decide which multiplier to
        // use. But testing the service info should be more robust.
        if (xServiceInfo->supportsService(u"com.sun.star.chart.ChartAxis"_ustr))
            fMultiplier = -600.0;
        else if (xServiceInfo->supportsService(u"com.sun.star.chart2.DataSeries"_ustr) || xServiceInfo->supportsService(u"com.sun.star.chart2.DataPointProperties"_ustr))
        {
            fMultiplier = -60000.0;
            bool bTextWordWrap = false;
            if ((xPropSet->getPropertyValue(u"TextWordWrap"_ustr) >>= bTextWordWrap) && bTextWordWrap)
                textWordWrap = "square";
            else
                textWordWrap = "none";
        }

        if (fMultiplier)
        {
            double fTextRotation = 0.0;
            uno::Any aAny = xPropSet->getPropertyValue(u"TextRotation"_ustr);
            if (aAny.hasValue() && (aAny >>= fTextRotation))
            {
                fTextRotation *= fMultiplier;
                // The MS Office UI allows values only in range of [-90,90].
                if (fTextRotation < -5400000.0 && fTextRotation > -16200000.0)
                {
                    // Reflect the angle if the value is between 90° and 270°
                    fTextRotation += 10800000.0;
                }
                else if (fTextRotation <= -16200000.0)
                {
                    fTextRotation += 21600000.0;
                }
                nRotation = std::round(fTextRotation);
            }
        }
    }

    if (nRotation)
        pFS->singleElement(FSNS(XML_a, XML_bodyPr), XML_rot, OString::number(nRotation), XML_wrap, textWordWrap);
    else
        pFS->singleElement(FSNS(XML_a, XML_bodyPr), XML_wrap, textWordWrap);

    pFS->singleElement(FSNS(XML_a, XML_lstStyle));

    pFS->startElement(FSNS(XML_a, XML_p));
    pFS->startElement(FSNS(XML_a, XML_pPr));

    WriteRunProperties(xPropSet, false, XML_defRPr, true, o3tl::temporary(false),
                       o3tl::temporary(sal_Int32()));

    pFS->endElement(FSNS(XML_a, XML_pPr));
    pFS->endElement(FSNS(XML_a, XML_p));
    pFS->endElement(FSNS(nChartNS, XML_txPr));
}

void ChartExport::InitPlotArea( )
{
    Reference< XPropertySet > xDiagramProperties (mxDiagram, uno::UNO_QUERY);

    //    Check for supported services and then the properties provided by this service.
    Reference<lang::XServiceInfo> xServiceInfo (mxDiagram, uno::UNO_QUERY);
    if (xServiceInfo.is())
    {
        if (xServiceInfo->supportsService(u"com.sun.star.chart.ChartAxisZSupplier"_ustr))
        {
            xDiagramProperties->getPropertyValue(u"HasZAxis"_ustr) >>= mbHasZAxis;
        }
    }

    xDiagramProperties->getPropertyValue(u"Dim3D"_ustr) >>=  mbIs3DChart;

    if( mbHasCategoryLabels && mxNewDiagram.is())
    {
        Reference< chart2::data::XLabeledDataSequence > xCategories(
                lcl_getCategories( mxNewDiagram, &mbHasDateCategories ) );
        if( xCategories.is() )
        {
            mxCategoriesValues.set( xCategories->getValues() );
        }
    }
}

void ChartExport::exportAxes( bool bIsChartex )
{
    sal_Int32 nSize = maAxes.size();
    // let's export the axis types in the right order
    for ( sal_Int32 nSortIdx = AXIS_PRIMARY_X; nSortIdx <= AXIS_SECONDARY_Y; nSortIdx++ )
    {
        for ( sal_Int32 nIdx = 0; nIdx < nSize; nIdx++ )
        {
            if (nSortIdx == maAxes[nIdx].nAxisType)
                exportAxis( maAxes[nIdx], bIsChartex );
        }
    }
}

namespace {

sal_Int32 getXAxisTypeByChartType(sal_Int32 eChartType)
{
    if( (eChartType == chart::TYPEID_SCATTER)
            || (eChartType == chart::TYPEID_BUBBLE) )
        return  XML_valAx;
    else if( eChartType == chart::TYPEID_STOCK )
        return  XML_dateAx;

    return XML_catAx;
}

sal_Int32 getRealXAxisType(sal_Int32 nAxisType)
{
    if( nAxisType == chart2::AxisType::CATEGORY )
        return XML_catAx;
    else if( nAxisType == chart2::AxisType::DATE )
        return XML_dateAx;
    else if( nAxisType == chart2::AxisType::SERIES )
        return XML_serAx;

    return XML_valAx;
}

}

void ChartExport::exportAxis(const AxisIdPair& rAxisIdPair, bool bIsChartex)
{
    // get some properties from document first
    bool bHasXAxisTitle = false,
         bHasYAxisTitle = false,
         bHasZAxisTitle = false,
         bHasSecondaryXAxisTitle = false,
         bHasSecondaryYAxisTitle = false;
    bool bHasXAxisMajorGrid = false,
         bHasXAxisMinorGrid = false,
         bHasYAxisMajorGrid = false,
         bHasYAxisMinorGrid = false,
         bHasZAxisMajorGrid = false,
         bHasZAxisMinorGrid = false;

    Reference< XPropertySet > xDiagramProperties (mxDiagram, uno::UNO_QUERY);

    xDiagramProperties->getPropertyValue(u"HasXAxisTitle"_ustr) >>= bHasXAxisTitle;
    xDiagramProperties->getPropertyValue(u"HasYAxisTitle"_ustr) >>= bHasYAxisTitle;
    xDiagramProperties->getPropertyValue(u"HasZAxisTitle"_ustr) >>= bHasZAxisTitle;
    xDiagramProperties->getPropertyValue(u"HasSecondaryXAxisTitle"_ustr) >>=  bHasSecondaryXAxisTitle;
    xDiagramProperties->getPropertyValue(u"HasSecondaryYAxisTitle"_ustr) >>=  bHasSecondaryYAxisTitle;

    xDiagramProperties->getPropertyValue(u"HasXAxisGrid"_ustr) >>=  bHasXAxisMajorGrid;
    xDiagramProperties->getPropertyValue(u"HasYAxisGrid"_ustr) >>=  bHasYAxisMajorGrid;
    xDiagramProperties->getPropertyValue(u"HasZAxisGrid"_ustr) >>=  bHasZAxisMajorGrid;

    xDiagramProperties->getPropertyValue(u"HasXAxisHelpGrid"_ustr) >>=  bHasXAxisMinorGrid;
    xDiagramProperties->getPropertyValue(u"HasYAxisHelpGrid"_ustr) >>=  bHasYAxisMinorGrid;
    xDiagramProperties->getPropertyValue(u"HasZAxisHelpGrid"_ustr) >>=  bHasZAxisMinorGrid;

    Reference< XPropertySet > xAxisProp;
    Reference< drawing::XShape > xAxisTitle;
    Reference< beans::XPropertySet > xMajorGrid;
    Reference< beans::XPropertySet > xMinorGrid;
    sal_Int32 nAxisType = XML_catAx;
    const char* sAxPos = nullptr;

    switch( rAxisIdPair.nAxisType )
    {
        case AXIS_PRIMARY_X:
        {
            Reference< css::chart::XAxisXSupplier > xAxisXSupp( mxDiagram, uno::UNO_QUERY );
            if( xAxisXSupp.is())
                xAxisProp = xAxisXSupp->getXAxis();
            if( bHasXAxisTitle )
                xAxisTitle = xAxisXSupp->getXAxisTitle();
            if( bHasXAxisMajorGrid )
                xMajorGrid = xAxisXSupp->getXMainGrid();
            if( bHasXAxisMinorGrid )
                xMinorGrid = xAxisXSupp->getXHelpGrid();

            nAxisType = lcl_getCategoryAxisType(mxNewDiagram, 0, 0);
            if( nAxisType != -1 )
                nAxisType = getRealXAxisType(nAxisType);
            else
                nAxisType = getXAxisTypeByChartType( getChartType() );
            // FIXME: axPos, need to check axis direction
            sAxPos = "b";
            break;
        }
        case AXIS_PRIMARY_Y:
        {
            Reference< css::chart::XAxisYSupplier > xAxisYSupp( mxDiagram, uno::UNO_QUERY );
            if( xAxisYSupp.is())
                xAxisProp = xAxisYSupp->getYAxis();
            if( bHasYAxisTitle )
                xAxisTitle = xAxisYSupp->getYAxisTitle();
            if( bHasYAxisMajorGrid )
                xMajorGrid = xAxisYSupp->getYMainGrid();
            if( bHasYAxisMinorGrid )
                xMinorGrid = xAxisYSupp->getYHelpGrid();

            nAxisType = XML_valAx;
            // FIXME: axPos, need to check axis direction
            sAxPos = "l";
            break;
        }
        case AXIS_PRIMARY_Z:
        {
            Reference< css::chart::XAxisZSupplier > xAxisZSupp( mxDiagram, uno::UNO_QUERY );
            if( xAxisZSupp.is())
                xAxisProp = xAxisZSupp->getZAxis();
            if( bHasZAxisTitle )
                xAxisTitle = xAxisZSupp->getZAxisTitle();
            if( bHasZAxisMajorGrid )
                xMajorGrid = xAxisZSupp->getZMainGrid();
            if( bHasZAxisMinorGrid )
                xMinorGrid = xAxisZSupp->getZHelpGrid();

            sal_Int32 eChartType = getChartType( );
            if( (eChartType == chart::TYPEID_SCATTER)
                || (eChartType == chart::TYPEID_BUBBLE) )
                nAxisType = XML_valAx;
            else if( eChartType == chart::TYPEID_STOCK )
                nAxisType = XML_dateAx;
            else if( eChartType == chart::TYPEID_BAR || eChartType == chart::TYPEID_AREA )
                nAxisType = XML_serAx;
            // FIXME: axPos, need to check axis direction
            sAxPos = "b";
            break;
        }
        case AXIS_SECONDARY_X:
        {
            Reference< css::chart::XTwoAxisXSupplier > xAxisTwoXSupp( mxDiagram, uno::UNO_QUERY );
            if( xAxisTwoXSupp.is())
                xAxisProp = xAxisTwoXSupp->getSecondaryXAxis();
            if( bHasSecondaryXAxisTitle )
            {
                Reference< css::chart::XSecondAxisTitleSupplier > xAxisSupp( mxDiagram, uno::UNO_QUERY );
                xAxisTitle = xAxisSupp->getSecondXAxisTitle();
            }

            nAxisType = lcl_getCategoryAxisType(mxNewDiagram, 0, 1);
            if( nAxisType != -1 )
                nAxisType = getRealXAxisType(nAxisType);
            else
                nAxisType = getXAxisTypeByChartType( getChartType() );
            // FIXME: axPos, need to check axis direction
            sAxPos = "t";
            break;
        }
        case AXIS_SECONDARY_Y:
        {
            Reference< css::chart::XTwoAxisYSupplier > xAxisTwoYSupp( mxDiagram, uno::UNO_QUERY );
            if( xAxisTwoYSupp.is())
                xAxisProp = xAxisTwoYSupp->getSecondaryYAxis();
            if( bHasSecondaryYAxisTitle )
            {
                Reference< css::chart::XSecondAxisTitleSupplier > xAxisSupp( mxDiagram, uno::UNO_QUERY );
                xAxisTitle = xAxisSupp->getSecondYAxisTitle();
            }

            nAxisType = XML_valAx;
            // FIXME: axPos, need to check axis direction
            sAxPos = "r";
            break;
        }
    }

    if (bIsChartex) {
        exportOneAxis_chartex(xAxisProp, xAxisTitle, xMajorGrid, xMinorGrid, nAxisType,
                rAxisIdPair);
    } else {
        exportOneAxis_chart(xAxisProp, xAxisTitle, xMajorGrid, xMinorGrid, nAxisType,
                sAxPos, rAxisIdPair);
    }
}

static const char *getTickMarkLocStr(sal_Int32 nValue)
{
    const bool bInner = nValue & css::chart::ChartAxisMarks::INNER;
    const bool bOuter = nValue & css::chart::ChartAxisMarks::OUTER;
    if( bInner && bOuter ) {
        return "cross";
    } else if( bInner ) {
        return "in";
    } else if( bOuter ) {
        return "out";
    } else {
        return "none";
    }
}

void ChartExport::exportOneAxis_chart(
    const Reference< XPropertySet >& xAxisProp,
    const Reference< drawing::XShape >& xAxisTitle,
    const Reference< XPropertySet >& xMajorGrid,
    const Reference< XPropertySet >& xMinorGrid,
    sal_Int32 nAxisType,
    const char* sAxisPos,
    const AxisIdPair& rAxisIdPair)
{
    FSHelperPtr pFS = GetFS();
    pFS->startElement(FSNS(XML_c, nAxisType));
    pFS->singleElement(FSNS(XML_c, XML_axId), XML_val, OString::number(rAxisIdPair.nAxisId));

    pFS->startElement(FSNS(XML_c, XML_scaling));

    // logBase, min, max
    if(GetProperty( xAxisProp, u"Logarithmic"_ustr ) )
    {
        bool bLogarithmic = false;
        mAny >>= bLogarithmic;
        if( bLogarithmic )
        {
            // default value is 10?
            pFS->singleElement(FSNS(XML_c, XML_logBase), XML_val, OString::number(10));
        }
    }

    // orientation: minMax, maxMin
    bool bReverseDirection = false;
    if(GetProperty( xAxisProp, u"ReverseDirection"_ustr ) )
        mAny >>= bReverseDirection;

    const char* orientation = bReverseDirection ? "maxMin":"minMax";
    pFS->singleElement(FSNS(XML_c, XML_orientation), XML_val, orientation);

    bool bAutoMax = false;
    if(GetProperty( xAxisProp, u"AutoMax"_ustr ) )
        mAny >>= bAutoMax;

    if( !bAutoMax && (GetProperty( xAxisProp, u"Max"_ustr ) ) )
    {
        double dMax = 0;
        mAny >>= dMax;
        pFS->singleElement(FSNS(XML_c, XML_max), XML_val, OString::number(dMax));
    }

    bool bAutoMin = false;
    if(GetProperty( xAxisProp, u"AutoMin"_ustr ) )
        mAny >>= bAutoMin;

    if( !bAutoMin && (GetProperty( xAxisProp, u"Min"_ustr ) ) )
    {
        double dMin = 0;
        mAny >>= dMin;
        pFS->singleElement(FSNS(XML_c, XML_min), XML_val, OString::number(dMin));
    }

    pFS->endElement( FSNS( XML_c, XML_scaling ) );

    bool bVisible = true;
    if( xAxisProp.is() )
    {
        xAxisProp->getPropertyValue(u"Visible"_ustr) >>=  bVisible;
    }

    // only export each axis only once non-deleted
    auto aItInsertedPair = maExportedAxis.insert(rAxisIdPair.nAxisType);
    bool bDeleted = !aItInsertedPair.second;

    pFS->singleElement(FSNS(XML_c, XML_delete), XML_val, !bDeleted && bVisible ? "0" : "1");

    // FIXME: axPos, need to check the property "ReverseDirection"
    pFS->singleElement(FSNS(XML_c, XML_axPos), XML_val, sAxisPos);
    // major grid line
    if( xMajorGrid.is())
    {
        pFS->startElement(FSNS(XML_c, XML_majorGridlines));
        exportShapeProps( xMajorGrid, false );
        pFS->endElement( FSNS( XML_c, XML_majorGridlines ) );
    }

    // minor grid line
    if( xMinorGrid.is())
    {
        pFS->startElement(FSNS(XML_c, XML_minorGridlines));
        exportShapeProps( xMinorGrid, false );
        pFS->endElement( FSNS( XML_c, XML_minorGridlines ) );
    }

    // title
    if( xAxisTitle.is() )
        exportTitle( xAxisTitle, false );

    bool bLinkedNumFmt = true;
    if (GetProperty(xAxisProp, u"LinkNumberFormatToSource"_ustr))
        mAny >>= bLinkedNumFmt;

    OUString aNumberFormatString(u"General"_ustr);
    if (GetProperty(xAxisProp, u"NumberFormat"_ustr))
    {
        sal_Int32 nKey = 0;
        mAny >>= nKey;
        aNumberFormatString = getNumberFormatCode(nKey);
    }

    pFS->singleElement(FSNS(XML_c, XML_numFmt),
            XML_formatCode, aNumberFormatString,
            XML_sourceLinked, bLinkedNumFmt ? "1" : "0");

    // majorTickMark
    sal_Int32 nValue = 0;
    if(GetProperty( xAxisProp, u"Marks"_ustr ) )
    {
        mAny >>= nValue;
        pFS->singleElement(FSNS(XML_c, XML_majorTickMark), XML_val,
                getTickMarkLocStr(nValue));
    }
    // minorTickMark
    if(GetProperty( xAxisProp, u"HelpMarks"_ustr ) )
    {
        mAny >>= nValue;
        pFS->singleElement(FSNS(XML_c, XML_minorTickMark), XML_val,
                getTickMarkLocStr(nValue));
    }
    // tickLblPos
    const char* sTickLblPos = nullptr;
    bool bDisplayLabel = true;
    if(GetProperty( xAxisProp, u"DisplayLabels"_ustr ) )
        mAny >>= bDisplayLabel;
    if( bDisplayLabel && (GetProperty( xAxisProp, u"LabelPosition"_ustr ) ) )
    {
        css::chart::ChartAxisLabelPosition eLabelPosition = css::chart::ChartAxisLabelPosition_NEAR_AXIS;
        mAny >>= eLabelPosition;
        switch( eLabelPosition )
        {
            case css::chart::ChartAxisLabelPosition_NEAR_AXIS:
            case css::chart::ChartAxisLabelPosition_NEAR_AXIS_OTHER_SIDE:
                sTickLblPos = "nextTo";
                break;
            case css::chart::ChartAxisLabelPosition_OUTSIDE_START:
                sTickLblPos = "low";
                break;
            case css::chart::ChartAxisLabelPosition_OUTSIDE_END:
                sTickLblPos = "high";
                break;
            default:
                sTickLblPos = "nextTo";
                break;
        }
    }
    else
    {
        sTickLblPos = "none";
    }
    pFS->singleElement(FSNS(XML_c, XML_tickLblPos), XML_val, sTickLblPos);

    // shape properties
    exportShapeProps( xAxisProp, false );

    exportTextProps(xAxisProp, false);

    pFS->singleElement(FSNS(XML_c, XML_crossAx), XML_val, OString::number(rAxisIdPair.nCrossAx));

    // crosses & crossesAt
    bool bCrossesValue = false;
    const char* sCrosses = nullptr;
    // do not export the CrossoverPosition/CrossoverValue, if the axis is deleted and not visible
    if( GetProperty( xAxisProp, u"CrossoverPosition"_ustr ) && !bDeleted && bVisible )
    {
        css::chart::ChartAxisPosition ePosition( css::chart::ChartAxisPosition_ZERO );
        mAny >>= ePosition;
        switch( ePosition )
        {
            case css::chart::ChartAxisPosition_START:
                sCrosses = "min";
                break;
            case css::chart::ChartAxisPosition_END:
                sCrosses = "max";
                break;
            case css::chart::ChartAxisPosition_ZERO:
                sCrosses = "autoZero";
                break;
            default:
                bCrossesValue = true;
                break;
        }
    }

    if( bCrossesValue && GetProperty( xAxisProp, u"CrossoverValue"_ustr ) )
    {
        double dValue = 0;
        mAny >>= dValue;
        pFS->singleElement(FSNS(XML_c, XML_crossesAt), XML_val, OString::number(dValue));
    }
    else
    {
        if(sCrosses)
        {
            pFS->singleElement(FSNS(XML_c, XML_crosses), XML_val, sCrosses);
        }
    }

    if( ( nAxisType == XML_catAx )
        || ( nAxisType == XML_dateAx ) )
    {
        // FIXME: seems not support? use default value,
        const char* const isAuto = "1";
        pFS->singleElement(FSNS(XML_c, XML_auto), XML_val, isAuto);

        if( nAxisType == XML_catAx )
        {
            // FIXME: seems not support? lblAlgn
            const char* const sLblAlgn = "ctr";
            pFS->singleElement(FSNS(XML_c, XML_lblAlgn), XML_val, sLblAlgn);
        }

        // FIXME: seems not support? lblOffset
        pFS->singleElement(FSNS(XML_c, XML_lblOffset), XML_val, OString::number(100));

        // export baseTimeUnit, majorTimeUnit, minorTimeUnit of Date axis
        if( nAxisType == XML_dateAx )
        {
            sal_Int32 nAxisIndex = -1;
            if( rAxisIdPair.nAxisType == AXIS_PRIMARY_X )
                nAxisIndex = 0;
            else if( rAxisIdPair.nAxisType == AXIS_SECONDARY_X )
                nAxisIndex = 1;

            cssc::TimeIncrement aTimeIncrement = lcl_getDateTimeIncrement( mxNewDiagram, nAxisIndex );
            sal_Int32 nTimeResolution = css::chart::TimeUnit::DAY;
            if( aTimeIncrement.TimeResolution >>= nTimeResolution )
                pFS->singleElement(FSNS(XML_c, XML_baseTimeUnit), XML_val, lclGetTimeUnitToken(nTimeResolution));

            cssc::TimeInterval aInterval;
            if( aTimeIncrement.MajorTimeInterval >>= aInterval )
            {
                pFS->singleElement(FSNS(XML_c, XML_majorUnit), XML_val, OString::number(aInterval.Number));
                pFS->singleElement(FSNS(XML_c, XML_majorTimeUnit), XML_val, lclGetTimeUnitToken(aInterval.TimeUnit));
            }
            if( aTimeIncrement.MinorTimeInterval >>= aInterval )
            {
                pFS->singleElement(FSNS(XML_c, XML_minorUnit), XML_val, OString::number(aInterval.Number));
                pFS->singleElement(FSNS(XML_c, XML_minorTimeUnit), XML_val, lclGetTimeUnitToken(aInterval.TimeUnit));
            }
        }

        // FIXME: seems not support? noMultiLvlLbl
        pFS->singleElement(FSNS(XML_c, XML_noMultiLvlLbl), XML_val, OString::number(0));
    }

    // crossBetween
    if( nAxisType == XML_valAx )
    {
        if( lcl_isCategoryAxisShifted( mxNewDiagram ))
            pFS->singleElement(FSNS(XML_c, XML_crossBetween), XML_val, "between");
        else
            pFS->singleElement(FSNS(XML_c, XML_crossBetween), XML_val, "midCat");
    }

    // majorUnit
    bool bAutoStepMain = false;
    if(GetProperty( xAxisProp, u"AutoStepMain"_ustr ) )
        mAny >>= bAutoStepMain;

    if( !bAutoStepMain && (GetProperty( xAxisProp, u"StepMain"_ustr ) ) )
    {
        double dMajorUnit = 0;
        mAny >>= dMajorUnit;
        pFS->singleElement(FSNS(XML_c, XML_majorUnit), XML_val, OString::number(dMajorUnit));
    }
    // minorUnit
    bool bAutoStepHelp = false;
    if(GetProperty( xAxisProp, u"AutoStepHelp"_ustr ) )
        mAny >>= bAutoStepHelp;

    if( !bAutoStepHelp && (GetProperty( xAxisProp, u"StepHelp"_ustr ) ) )
    {
        double dMinorUnit = 0;
        mAny >>= dMinorUnit;
        if( GetProperty( xAxisProp, u"StepHelpCount"_ustr ) )
        {
            sal_Int32 dMinorUnitCount = 0;
            mAny >>= dMinorUnitCount;
            // tdf#114168 Don't save minor unit if number of step help count is 5 (which is default for MS Excel),
            // to allow proper .xlsx import. If minorUnit is set and majorUnit not, then it is impossible
            // to calculate StepHelpCount.
            if( dMinorUnitCount != 5 )
            {
                pFS->singleElement( FSNS( XML_c, XML_minorUnit ),
                    XML_val, OString::number( dMinorUnit ) );
            }
        }
    }

    if( nAxisType == XML_valAx && GetProperty( xAxisProp, u"DisplayUnits"_ustr ) )
    {
        bool bDisplayUnits = false;
        mAny >>= bDisplayUnits;
        if(bDisplayUnits)
        {
            if(GetProperty( xAxisProp, u"BuiltInUnit"_ustr ))
            {
                OUString aVal;
                mAny >>= aVal;
                if(!aVal.isEmpty())
                {
                    pFS->startElement(FSNS(XML_c, XML_dispUnits));

                    pFS->singleElement(FSNS(XML_c, XML_builtInUnit), XML_val, aVal);

                    pFS->singleElement(FSNS( XML_c, XML_dispUnitsLbl ));
                    pFS->endElement( FSNS( XML_c, XML_dispUnits ) );
                }
            }
        }
    }

    pFS->endElement( FSNS( XML_c, nAxisType ) );
}

void ChartExport::exportOneAxis_chartex(
    const Reference< XPropertySet >& xAxisProp,
    const Reference< drawing::XShape >& xAxisTitle,
    const Reference< XPropertySet >& xMajorGrid,
    const Reference< XPropertySet >& xMinorGrid,
    sal_Int32 nAxisType,
    const AxisIdPair& rAxisIdPair)
{
    FSHelperPtr pFS = GetFS();
    pFS->startElement(FSNS(XML_cx, XML_axis), XML_id, OString::number(rAxisIdPair.nAxisId));

    // The following is in the 2010 chart code above:
    //    bool bVisible = true;
    //    if( xAxisProp.is() )
    //    {
    //        xAxisProp->getPropertyValue(u"Visible"_ustr) >>=  bVisible;
    //    }
    //    // only export each axis only once non-deleted
    //    auto aItInsertedPair = maExportedAxis.insert(rAxisIdPair.nAxisType);
    //    bool bDeleted = !aItInsertedPair.second;
    //
    //    pFS->singleElement(FSNS(XML_c, XML_delete), XML_val, !bDeleted && bVisible ? "0" : "1");
    //
    // Is chartex attribute "hidden" the same as !bVisible? And what to do if
    // the axis is deleted, per above?

    // ==== catScaling/valScaling
    switch (nAxisType) {
        case XML_catAx:
            pFS->singleElement(FSNS(XML_cx, XML_catScaling) /* TODO: handle gapWidth */);
            break;
        case XML_valAx:
            {
                bool bAutoMax = false;
                double dMax = 0; // Make VS happy
                bool bMaxSpecified = false;
                if(GetProperty( xAxisProp, u"AutoMax"_ustr ) )
                    mAny >>= bAutoMax;

                if( !bAutoMax && (GetProperty( xAxisProp, u"Max"_ustr ) ) )
                {
                    mAny >>= dMax;
                    bMaxSpecified = true;
                }

                bool bAutoMin = false;
                double dMin = 0; // Make VS happy
                bool bMinSpecified = false;
                if(GetProperty( xAxisProp, u"AutoMin"_ustr ) )
                    mAny >>= bAutoMin;

                if( !bAutoMin && (GetProperty( xAxisProp, u"Min"_ustr ) ) )
                {
                    mAny >>= dMin;
                    bMinSpecified = true;
                }

                // TODO: handle majorUnit/minorUnit in the following
                if (bMaxSpecified && bMinSpecified) {
                    pFS->singleElement(FSNS(XML_cx, XML_valScaling),
                            XML_max, OString::number(dMax),
                            XML_min, OString::number(dMin));
                } else if (!bMaxSpecified && bMinSpecified) {
                    pFS->singleElement(FSNS(XML_cx, XML_valScaling),
                            XML_min, OString::number(dMin));
                } else if (bMaxSpecified && !bMinSpecified) {
                    pFS->singleElement(FSNS(XML_cx, XML_valScaling),
                            XML_max, OString::number(dMax));
                } else {
                    pFS->singleElement(FSNS(XML_cx, XML_valScaling));
                }

            }
            break;
        default:
            // shouldn't happen
            assert(false);
    }

    // ==== title
    if( xAxisTitle.is() ) {
        exportTitle( xAxisTitle, true );
    }

    // ==== units
    if (GetProperty( xAxisProp, u"DisplayUnits"_ustr ) )
    {
        bool bDisplayUnits = false;
        mAny >>= bDisplayUnits;
        if (bDisplayUnits)
        {
            if (GetProperty( xAxisProp, u"BuiltInUnit"_ustr ))
            {
                OUString aVal;
                mAny >>= aVal;
                if(!aVal.isEmpty())
                {
                    pFS->startElement(FSNS(XML_cx, XML_units));

                    pFS->startElement(FSNS(XML_cx, XML_unitsLabel));

                    lcl_writeChartexString(pFS, aVal);

                    pFS->endElement(FSNS(XML_cx, XML_unitsLabel));

                    pFS->endElement( FSNS( XML_cx, XML_units ) );
                }
            }
        }
    }

    // ==== majorGridlines
    if( xMajorGrid.is())
    {
        pFS->startElement(FSNS(XML_cx, XML_majorGridlines));
        exportShapeProps( xMajorGrid, true );
        pFS->endElement( FSNS( XML_cx, XML_majorGridlines ) );
    }

    // ==== minorGridlines
    if( xMinorGrid.is())
    {
        pFS->startElement(FSNS(XML_cx, XML_minorGridlines));
        exportShapeProps( xMinorGrid, true );
        pFS->endElement( FSNS( XML_cx, XML_minorGridlines ) );
    }

    // ==== majorTickMarks
    if (GetProperty( xAxisProp, u"Marks"_ustr ) )
    {
        sal_Int32 nValue = 0;
        mAny >>= nValue;
        pFS->singleElement(FSNS(XML_cx, XML_majorTickMarks), XML_type,
                getTickMarkLocStr(nValue));
    }

    // ==== minorTickMarks
    if (GetProperty( xAxisProp, u"HelpMarks"_ustr ) )
    {
        sal_Int32 nValue = 0;
        mAny >>= nValue;
        pFS->singleElement(FSNS(XML_cx, XML_minorTickMarks), XML_type,
                getTickMarkLocStr(nValue));
    }

    // ==== tickLabels consists of nothing but an extLst so I don't know how to
    // handle it

    // ==== numFmt
    bool bLinkedNumFmt = true;
    if (GetProperty(xAxisProp, u"LinkNumberFormatToSource"_ustr))
        mAny >>= bLinkedNumFmt;

    OUString aNumberFormatString(u"General"_ustr);
    if (GetProperty(xAxisProp, u"NumberFormat"_ustr))
    {
        sal_Int32 nKey = 0;
        mAny >>= nKey;
        aNumberFormatString = getNumberFormatCode(nKey);
    }

    // We're always outputting this, which presumably isn't necessary, but it's
    // not clear what the defaults are for determining if an explicit element is
    // needed
    pFS->singleElement(FSNS(XML_cx, XML_numFmt),
            XML_formatCode, aNumberFormatString,
            XML_sourceLinked, bLinkedNumFmt ? "1" : "0");

    // ==== spPr
    exportShapeProps( xAxisProp, true );

    // ==== txPr
    exportTextProps(xAxisProp, true);

    pFS->endElement( FSNS( XML_cx, XML_axis ) );
}

namespace {

struct LabelPlacementParam
{
    bool mbExport;
    sal_Int32 meDefault;

    std::unordered_set<sal_Int32> maAllowedValues;

    LabelPlacementParam(bool bExport, sal_Int32 nDefault) :
        mbExport(bExport),
        meDefault(nDefault),
        maAllowedValues(
          {
           css::chart::DataLabelPlacement::OUTSIDE,
           css::chart::DataLabelPlacement::INSIDE,
           css::chart::DataLabelPlacement::CENTER,
           css::chart::DataLabelPlacement::NEAR_ORIGIN,
           css::chart::DataLabelPlacement::TOP,
           css::chart::DataLabelPlacement::BOTTOM,
           css::chart::DataLabelPlacement::LEFT,
           css::chart::DataLabelPlacement::RIGHT,
           css::chart::DataLabelPlacement::AVOID_OVERLAP
          }
        )
    {}
};

const char* toOOXMLPlacement( sal_Int32 nPlacement )
{
    switch (nPlacement)
    {
        case css::chart::DataLabelPlacement::OUTSIDE:       return "outEnd";
        case css::chart::DataLabelPlacement::INSIDE:        return "inEnd";
        case css::chart::DataLabelPlacement::CENTER:        return "ctr";
        case css::chart::DataLabelPlacement::NEAR_ORIGIN:   return "inBase";
        case css::chart::DataLabelPlacement::TOP:           return "t";
        case css::chart::DataLabelPlacement::BOTTOM:        return "b";
        case css::chart::DataLabelPlacement::LEFT:          return "l";
        case css::chart::DataLabelPlacement::RIGHT:         return "r";
        case css::chart::DataLabelPlacement::CUSTOM:
        case css::chart::DataLabelPlacement::AVOID_OVERLAP: return "bestFit";
        default:
            ;
    }

    return "outEnd";
}

OUString getFieldTypeString( const chart2::DataPointCustomLabelFieldType aType )
{
    switch (aType)
    {
    case chart2::DataPointCustomLabelFieldType_CATEGORYNAME:
        return u"CATEGORYNAME"_ustr;

    case chart2::DataPointCustomLabelFieldType_SERIESNAME:
        return u"SERIESNAME"_ustr;

    case chart2::DataPointCustomLabelFieldType_VALUE:
        return u"VALUE"_ustr;

    case chart2::DataPointCustomLabelFieldType_CELLREF:
        return u"CELLREF"_ustr;

    case chart2::DataPointCustomLabelFieldType_CELLRANGE:
        return u"CELLRANGE"_ustr;

    default:
        break;
    }
    return OUString();
}

void writeRunProperties( ChartExport* pChartExport, Reference<XPropertySet> const & xPropertySet )
{
    bool bDummy = false;
    sal_Int32 nDummy;
    pChartExport->WriteRunProperties(xPropertySet, false, XML_rPr, true, bDummy, nDummy);
}

void writeCustomLabel( const FSHelperPtr& pFS, ChartExport* pChartExport,
                       const Sequence<Reference<chart2::XDataPointCustomLabelField>>& rCustomLabelFields,
                       sal_Int32 nLabelIndex, DataLabelsRange& rDLblsRange )
{
    pFS->startElement(FSNS(XML_c, XML_tx));
    pFS->startElement(FSNS(XML_c, XML_rich));

    // TODO: body properties?
    pFS->singleElement(FSNS(XML_a, XML_bodyPr));

    OUString sFieldType;
    OUString sContent;
    pFS->startElement(FSNS(XML_a, XML_p));

    for (auto& rField : rCustomLabelFields)
    {
        Reference<XPropertySet> xPropertySet(rField, UNO_QUERY);
        chart2::DataPointCustomLabelFieldType aType = rField->getFieldType();
        sFieldType.clear();
        sContent.clear();
        bool bNewParagraph = false;

        if (aType == chart2::DataPointCustomLabelFieldType_CELLRANGE &&
            rField->getDataLabelsRange())
        {
            if (rDLblsRange.getRange().isEmpty())
                rDLblsRange.setRange(rField->getCellRange());

            if (!rDLblsRange.hasLabel(nLabelIndex))
                rDLblsRange.setLabel(nLabelIndex, rField->getString());

            sContent = "[CELLRANGE]";
        }
        else
        {
            sContent = rField->getString();
        }

        if (aType == chart2::DataPointCustomLabelFieldType_NEWLINE)
            bNewParagraph = true;
        else if (aType != chart2::DataPointCustomLabelFieldType_TEXT)
            sFieldType = getFieldTypeString(aType);

        if (bNewParagraph)
        {
            pFS->endElement(FSNS(XML_a, XML_p));
            pFS->startElement(FSNS(XML_a, XML_p));
            continue;
        }

        if (sFieldType.isEmpty())
        {
            // Normal text run
            pFS->startElement(FSNS(XML_a, XML_r));
            writeRunProperties(pChartExport, xPropertySet);

            pFS->startElement(FSNS(XML_a, XML_t));
            pFS->writeEscaped(sContent);
            pFS->endElement(FSNS(XML_a, XML_t));

            pFS->endElement(FSNS(XML_a, XML_r));
        }
        else
        {
            // Field
            pFS->startElement(FSNS(XML_a, XML_fld), XML_id, rField->getGuid(), XML_type,
                              sFieldType);
            writeRunProperties(pChartExport, xPropertySet);

            pFS->startElement(FSNS(XML_a, XML_t));
            pFS->writeEscaped(sContent);
            pFS->endElement(FSNS(XML_a, XML_t));

            pFS->endElement(FSNS(XML_a, XML_fld));
        }
    }

    pFS->endElement(FSNS(XML_a, XML_p));
    pFS->endElement(FSNS(XML_c, XML_rich));
    pFS->endElement(FSNS(XML_c, XML_tx));
}

void writeLabelProperties( const FSHelperPtr& pFS, ChartExport* pChartExport,
    const uno::Reference<beans::XPropertySet>& xPropSet, const LabelPlacementParam& rLabelParam,
    sal_Int32 nLabelIndex, DataLabelsRange& rDLblsRange,
    bool bIsChartex)
{
    if (!xPropSet.is())
        return;

    const sal_Int32 nChartNS = bIsChartex ? XML_cx : XML_c;

    chart2::DataPointLabel aLabel;
    Sequence<Reference<chart2::XDataPointCustomLabelField>> aCustomLabelFields;
    sal_Int32 nLabelBorderWidth = 0;
    sal_Int32 nLabelBorderColor = 0x00FFFFFF;
    sal_Int32 nLabelFillColor = -1;

    xPropSet->getPropertyValue(u"Label"_ustr) >>= aLabel;
    xPropSet->getPropertyValue(u"CustomLabelFields"_ustr) >>= aCustomLabelFields;
    xPropSet->getPropertyValue(u"LabelBorderWidth"_ustr) >>= nLabelBorderWidth;
    xPropSet->getPropertyValue(u"LabelBorderColor"_ustr) >>= nLabelBorderColor;
    xPropSet->getPropertyValue(u"LabelFillColor"_ustr) >>= nLabelFillColor;

    if (nLabelBorderWidth > 0 || nLabelFillColor != -1)
    {
        pFS->startElement(FSNS(nChartNS, XML_spPr));

        if (nLabelFillColor != -1)
        {
            ::Color nColor(ColorTransparency, nLabelFillColor);
            if (nColor.IsTransparent())
                pChartExport->WriteSolidFill(nColor, nColor.GetAlpha());
            else
                pChartExport->WriteSolidFill(nColor);
        }

        if (nLabelBorderWidth > 0)
        {
            pFS->startElement(FSNS(XML_a, XML_ln), XML_w,
                              OString::number(convertHmmToEmu(nLabelBorderWidth)));

            if (nLabelBorderColor != -1)
            {
                ::Color nColor(ColorTransparency, nLabelBorderColor);
                if (nColor.IsTransparent())
                    pChartExport->WriteSolidFill(nColor, nColor.GetAlpha());
                else
                    pChartExport->WriteSolidFill(nColor);
            }

            pFS->endElement(FSNS(XML_a, XML_ln));
        }

        pFS->endElement(FSNS(nChartNS, XML_spPr));
    }

    pChartExport->exportTextProps(xPropSet, bIsChartex);

    if (aCustomLabelFields.hasElements())
        writeCustomLabel(pFS, pChartExport, aCustomLabelFields, nLabelIndex, rDLblsRange);

    if (!bIsChartex) {
        // In chartex label position is an attribute of cx:dataLabel
        if (rLabelParam.mbExport)
        {
            sal_Int32 nLabelPlacement = rLabelParam.meDefault;
            if (xPropSet->getPropertyValue(u"LabelPlacement"_ustr) >>= nLabelPlacement)
            {
                if (!rLabelParam.maAllowedValues.count(nLabelPlacement))
                    nLabelPlacement = rLabelParam.meDefault;
                pFS->singleElement(FSNS(XML_c, XML_dLblPos), XML_val, toOOXMLPlacement(nLabelPlacement));
            }
        }

        pFS->singleElement(FSNS(XML_c, XML_showLegendKey), XML_val, ToPsz10(aLabel.ShowLegendSymbol));
        pFS->singleElement(FSNS(XML_c, XML_showVal), XML_val, ToPsz10(aLabel.ShowNumber));
        pFS->singleElement(FSNS(XML_c, XML_showCatName), XML_val, ToPsz10(aLabel.ShowCategoryName));
        pFS->singleElement(FSNS(XML_c, XML_showSerName), XML_val, ToPsz10(aLabel.ShowSeriesName));
        pFS->singleElement(FSNS(XML_c, XML_showPercent), XML_val, ToPsz10(aLabel.ShowNumberInPercent));
    }

    // Export the text "separator" if exists
    uno::Any aAny = xPropSet->getPropertyValue(u"LabelSeparator"_ustr);
    if( aAny.hasValue() )
    {
        OUString nLabelSeparator;
        aAny >>= nLabelSeparator;
        pFS->startElement(FSNS(nChartNS, XML_separator));
        pFS->writeEscaped( nLabelSeparator );
        pFS->endElement( FSNS(nChartNS, XML_separator ) );
    }

    if (rDLblsRange.hasLabel(nLabelIndex))
    {
        pFS->startElement(FSNS(nChartNS, XML_extLst));
        // TODO: is the following correct for chartex?
        pFS->startElement(FSNS(nChartNS, XML_ext), XML_uri,
            "{CE6537A1-D6FC-4f65-9D91-7224C49458BB}", FSNS(XML_xmlns, XML_c15),
            pChartExport->GetFB()->getNamespaceURL(OOX_NS(c15)));

        pFS->singleElement(FSNS(XML_c15, XML_showDataLabelsRange), XML_val, "1");

        pFS->endElement(FSNS(nChartNS, XML_ext));
        pFS->endElement(FSNS(nChartNS, XML_extLst));
    }
}

}

void ChartExport::exportDataLabels(
    const uno::Reference<chart2::XDataSeries> & xSeries, sal_Int32 nSeriesLength, sal_Int32 eChartType,
    DataLabelsRange& rDLblsRange,
    bool bIsChartex)
{
    if (!xSeries.is() || nSeriesLength <= 0)
        return;

    uno::Reference<beans::XPropertySet> xPropSet(xSeries, uno::UNO_QUERY);
    if (!xPropSet.is())
        return;

    FSHelperPtr pFS = GetFS();

    if (bIsChartex) {
        pFS->startElement(FSNS(XML_cx, XML_dataLabels));
    } else {
        pFS->startElement(FSNS(XML_c, XML_dLbls));
    }

    bool bLinkedNumFmt = true;
    if (GetProperty(xPropSet, u"LinkNumberFormatToSource"_ustr))
        mAny >>= bLinkedNumFmt;

    chart2::DataPointLabel aLabel;
    bool bLabelIsNumberFormat = true;
    if( xPropSet->getPropertyValue(u"Label"_ustr) >>= aLabel )
        bLabelIsNumberFormat = aLabel.ShowNumber;

    if (GetProperty(xPropSet, bLabelIsNumberFormat ? u"NumberFormat"_ustr : u"PercentageNumberFormat"_ustr))
    {
        sal_Int32 nKey = 0;
        mAny >>= nKey;

        OUString aNumberFormatString = getNumberFormatCode(nKey);

        if (bIsChartex) {
            pFS->singleElement(FSNS(XML_cx, XML_numFmt),
                XML_formatCode, aNumberFormatString,
                XML_sourceLinked, ToPsz10(bLinkedNumFmt));
        } else {
            pFS->singleElement(FSNS(XML_c, XML_numFmt),
                XML_formatCode, aNumberFormatString,
                XML_sourceLinked, ToPsz10(bLinkedNumFmt));
        }
    }

    uno::Sequence<sal_Int32> aAttrLabelIndices;
    xPropSet->getPropertyValue(u"AttributedDataPoints"_ustr) >>= aAttrLabelIndices;

    // We must not export label placement property when the chart type doesn't
    // support this option in MS Office, else MS Office would think the file
    // is corrupt & refuse to open it.

    const chart::TypeGroupInfo& rInfo = chart::GetTypeGroupInfo(static_cast<chart::TypeId>(eChartType));
    LabelPlacementParam aParam(!mbIs3DChart, rInfo.mnDefLabelPos);
    switch (eChartType) // diagram chart type
    {
        case chart::TYPEID_PIE:
            if(getChartType() == chart::TYPEID_DOUGHNUT)
                aParam.mbExport = false;
            else
            // All pie charts support label placement.
            aParam.mbExport = true;
        break;
        case chart::TYPEID_AREA:
        case chart::TYPEID_RADARLINE:
        case chart::TYPEID_RADARAREA:
            // These chart types don't support label placement.
            aParam.mbExport = false;
        break;
        case chart::TYPEID_BAR:
            if (mbStacked || mbPercent)
            {
                aParam.maAllowedValues.clear();
                aParam.maAllowedValues.insert(css::chart::DataLabelPlacement::CENTER);
                aParam.maAllowedValues.insert(css::chart::DataLabelPlacement::INSIDE);
                aParam.maAllowedValues.insert(css::chart::DataLabelPlacement::NEAR_ORIGIN);
                aParam.meDefault = css::chart::DataLabelPlacement::CENTER;
            }
            else  // Clustered bar chart
            {
                aParam.maAllowedValues.clear();
                aParam.maAllowedValues.insert(css::chart::DataLabelPlacement::CENTER);
                aParam.maAllowedValues.insert(css::chart::DataLabelPlacement::INSIDE);
                aParam.maAllowedValues.insert(css::chart::DataLabelPlacement::OUTSIDE);
                aParam.maAllowedValues.insert(css::chart::DataLabelPlacement::NEAR_ORIGIN);
                aParam.meDefault = css::chart::DataLabelPlacement::OUTSIDE;
            }
        break;
        // TODO: How do chartex charts handle this?
        default:
            ;
    }

    for (const sal_Int32 nIdx : aAttrLabelIndices)
    {
        uno::Reference<beans::XPropertySet> xLabelPropSet = xSeries->getDataPointByIndex(nIdx);

        if (!xLabelPropSet.is())
            continue;

        if (bIsChartex) {
            if (aParam.mbExport)
            {
                sal_Int32 nLabelPlacement = aParam.meDefault;
                if (xPropSet->getPropertyValue(u"LabelPlacement"_ustr) >>= nLabelPlacement)
                {
                    if (!aParam.maAllowedValues.count(nLabelPlacement))
                        nLabelPlacement = aParam.meDefault;
                    pFS->startElement(FSNS(XML_cx, XML_dataLabel),
                            XML_idx, OString::number(nIdx),
                            XML_pos, toOOXMLPlacement(nLabelPlacement));
                }
            } else {
                pFS->startElement(FSNS(XML_cx, XML_dataLabel), XML_idx, OString::number(nIdx));
            }
        } else {
            pFS->startElement(FSNS(XML_c, XML_dLbl));
            pFS->singleElement(FSNS(XML_c, XML_idx), XML_val, OString::number(nIdx));

            // As far as i know there can be issues with the Positions,
            // if a piechart label use AVOID_OVERLAP placement (== BestFit)
            // because LO and MS may calculate the bestFit positions differently.
            bool bWritePosition = true;
            if (eChartType == chart::TYPEID_PIE)
            {
                sal_Int32 nLabelPlacement = aParam.meDefault;
                xLabelPropSet->getPropertyValue(u"LabelPlacement"_ustr) >>= nLabelPlacement;
                if (nLabelPlacement == css::chart::DataLabelPlacement::AVOID_OVERLAP)
                    bWritePosition = false;
            }

            // export custom position of data label
            if (bWritePosition)
            {
                chart2::RelativePosition aCustomLabelPosition;
                if( xLabelPropSet->getPropertyValue(u"CustomLabelPosition"_ustr) >>= aCustomLabelPosition )
                {
                    pFS->startElement(FSNS(XML_c, XML_layout));
                    pFS->startElement(FSNS(XML_c, XML_manualLayout));

                    pFS->singleElement(FSNS(XML_c, XML_x), XML_val, OString::number(aCustomLabelPosition.Primary));
                    pFS->singleElement(FSNS(XML_c, XML_y), XML_val, OString::number(aCustomLabelPosition.Secondary));

                    SAL_WARN_IF(aCustomLabelPosition.Anchor != css::drawing::Alignment_TOP_LEFT, "oox", "unsupported anchor position");

                    pFS->endElement(FSNS(XML_c, XML_manualLayout));
                    pFS->endElement(FSNS(XML_c, XML_layout));
                }
            }
        }

        if( GetProperty(xLabelPropSet, u"LinkNumberFormatToSource"_ustr) )
            mAny >>= bLinkedNumFmt;

        if( xLabelPropSet->getPropertyValue(u"Label"_ustr) >>= aLabel )
            bLabelIsNumberFormat = aLabel.ShowNumber;
        else
            bLabelIsNumberFormat = true;

        if (GetProperty(xLabelPropSet, bLabelIsNumberFormat ? u"NumberFormat"_ustr : u"PercentageNumberFormat"_ustr))
        {
            sal_Int32 nKey = 0;
            mAny >>= nKey;

            OUString aNumberFormatString = getNumberFormatCode(nKey);

            if (bIsChartex) {
                pFS->singleElement(FSNS(XML_cx, XML_numFmt), XML_formatCode, aNumberFormatString,
                                   XML_sourceLinked, ToPsz10(bLinkedNumFmt));
            } else {
                pFS->singleElement(FSNS(XML_c, XML_numFmt), XML_formatCode, aNumberFormatString,
                                   XML_sourceLinked, ToPsz10(bLinkedNumFmt));
            }
        }

        // Individual label property that overwrites the baseline.
        writeLabelProperties(pFS, this, xLabelPropSet, aParam, nIdx,
                rDLblsRange, bIsChartex);
        pFS->endElement(FSNS(XML_c, XML_dLbl));
    }

    // Baseline label properties for all labels.
    writeLabelProperties(pFS, this, xPropSet, aParam, -1, rDLblsRange,
            bIsChartex);

    if (!bIsChartex) {
        bool bShowLeaderLines = false;
        xPropSet->getPropertyValue(u"ShowCustomLeaderLines"_ustr) >>= bShowLeaderLines;
        pFS->singleElement(FSNS(XML_c, XML_showLeaderLines), XML_val, ToPsz10(bShowLeaderLines));

        // Export LeaderLine properties
        // TODO: import all kind of LeaderLine props (not just LineColor/LineWidth)
        if (bShowLeaderLines)
        {
            pFS->startElement(FSNS(XML_c, XML_leaderLines));
            pFS->startElement(FSNS(XML_c, XML_spPr));
            WriteOutline(xPropSet, getModel());
            pFS->endElement(FSNS(XML_c, XML_spPr));
            pFS->endElement(FSNS(XML_c, XML_leaderLines));
        }

        // Export leader line
        if( eChartType != chart::TYPEID_PIE )
        {
            pFS->startElement(FSNS(XML_c, XML_extLst));
            pFS->startElement(FSNS(XML_c, XML_ext), XML_uri, "{CE6537A1-D6FC-4f65-9D91-7224C49458BB}", FSNS(XML_xmlns, XML_c15), GetFB()->getNamespaceURL(OOX_NS(c15)));
            pFS->singleElement(FSNS(XML_c15, XML_showLeaderLines), XML_val, ToPsz10(bShowLeaderLines));
            pFS->endElement(FSNS(XML_c, XML_ext));
            pFS->endElement(FSNS(XML_c, XML_extLst));
        }
    }

    if (bIsChartex) {
        pFS->endElement(FSNS(XML_cx, XML_dataLabels));
    } else {
        pFS->endElement(FSNS(XML_c, XML_dLbls));
    }
}

void ChartExport::exportDataPoints(
    const uno::Reference< beans::XPropertySet > & xSeriesProperties,
    sal_Int32 nSeriesLength, sal_Int32 eChartType )
{
    uno::Reference< chart2::XDataSeries > xSeries( xSeriesProperties, uno::UNO_QUERY );
    bool bVaryColorsByPoint = false;
    Sequence< sal_Int32 > aDataPointSeq;
    if( xSeriesProperties.is())
    {
        Any aAny = xSeriesProperties->getPropertyValue( u"AttributedDataPoints"_ustr );
        aAny >>= aDataPointSeq;
        xSeriesProperties->getPropertyValue( u"VaryColorsByPoint"_ustr ) >>= bVaryColorsByPoint;
    }

    const sal_Int32 * pPoints = aDataPointSeq.getConstArray();
    sal_Int32 nElement;
    Reference< chart2::XColorScheme > xColorScheme;
    if( mxNewDiagram.is())
        xColorScheme.set( mxNewDiagram->getDefaultColorScheme());

    if( bVaryColorsByPoint && xColorScheme.is() )
    {
        o3tl::sorted_vector< sal_Int32 > aAttrPointSet;
        aAttrPointSet.reserve(aDataPointSeq.getLength());
        for (auto p = pPoints; p < pPoints + aDataPointSeq.getLength(); ++p)
            aAttrPointSet.insert(*p);
        const auto aEndIt = aAttrPointSet.end();
        for( nElement = 0; nElement < nSeriesLength; ++nElement )
        {
            uno::Reference< beans::XPropertySet > xPropSet;
            if( aAttrPointSet.find( nElement ) != aEndIt )
            {
                try
                {
                    xPropSet = SchXMLSeriesHelper::createOldAPIDataPointPropertySet(
                            xSeries, nElement, getModel() );
                }
                catch( const uno::Exception & )
                {
                    DBG_UNHANDLED_EXCEPTION( "oox", "Exception caught during Export of data point" );
                }
            }
            else
            {
                // property set only containing the color
                xPropSet.set( new ColorPropertySet( ColorTransparency, xColorScheme->getColorByIndex( nElement )));
            }

            if( xPropSet.is() )
            {
                FSHelperPtr pFS = GetFS();
                pFS->startElement(FSNS(XML_c, XML_dPt));
                pFS->singleElement(FSNS(XML_c, XML_idx), XML_val, OString::number(nElement));

                switch (eChartType)
                {
                    case chart::TYPEID_PIE:
                    case chart::TYPEID_DOUGHNUT:
                    {
                        if( xPropSet.is() && GetProperty( xPropSet, u"SegmentOffset"_ustr) )
                        {
                            sal_Int32 nOffset = 0;
                            mAny >>= nOffset;
                            if (nOffset)
                                pFS->singleElement( FSNS( XML_c, XML_explosion ),
                                        XML_val, OString::number( nOffset ) );
                        }
                        break;
                    }
                    default:
                        break;
                }
                exportShapeProps( xPropSet, false );

                pFS->endElement( FSNS( XML_c, XML_dPt ) );
            }
        }
    }

    // Export Data Point Property in Charts even if the VaryColors is false
    if( bVaryColorsByPoint )
        return;

    o3tl::sorted_vector< sal_Int32 > aAttrPointSet;
    aAttrPointSet.reserve(aDataPointSeq.getLength());
    for (auto p = pPoints; p < pPoints + aDataPointSeq.getLength(); ++p)
        aAttrPointSet.insert(*p);
    const auto aEndIt = aAttrPointSet.end();
    for( nElement = 0; nElement < nSeriesLength; ++nElement )
    {
        uno::Reference< beans::XPropertySet > xPropSet;
        if( aAttrPointSet.find( nElement ) != aEndIt )
        {
            try
            {
                xPropSet = SchXMLSeriesHelper::createOldAPIDataPointPropertySet(
                        xSeries, nElement, getModel() );
            }
            catch( const uno::Exception & )
            {
                DBG_UNHANDLED_EXCEPTION( "oox", "Exception caught during Export of data point" );
            }
        }

        if( xPropSet.is() )
        {
            FSHelperPtr pFS = GetFS();
            pFS->startElement(FSNS(XML_c, XML_dPt));
            pFS->singleElement(FSNS(XML_c, XML_idx), XML_val, OString::number(nElement));

            switch( eChartType )
            {
                case chart::TYPEID_BUBBLE:
                case chart::TYPEID_HORBAR:
                case chart::TYPEID_BAR:
                    pFS->singleElement(FSNS(XML_c, XML_invertIfNegative), XML_val, "0");
                    exportShapeProps(xPropSet, false);
                    break;

                case chart::TYPEID_LINE:
                case chart::TYPEID_SCATTER:
                case chart::TYPEID_RADARLINE:
                    exportMarker(xPropSet);
                    break;

                default:
                    exportShapeProps(xPropSet, false);
                    break;
            }

            pFS->endElement( FSNS( XML_c, XML_dPt ) );
        }
    }
}

// Generalized axis output
void ChartExport::createAxes(bool bPrimaryAxes, bool bCheckCombinedAxes)
{
    sal_Int32 nAxisIdx, nAxisIdy;
    bool bPrimaryAxisExists = false;
    bool bSecondaryAxisExists = false;
    // let's check which axis already exists and which axis is attached to the actual dataseries
    if (maAxes.size() >= 2)
    {
        bPrimaryAxisExists = bPrimaryAxes && maAxes[1].nAxisType == AXIS_PRIMARY_Y;
        bSecondaryAxisExists = !bPrimaryAxes && maAxes[1].nAxisType == AXIS_SECONDARY_Y;
    }
    // tdf#114181 keep axes of combined charts
    if ( bCheckCombinedAxes && ( bPrimaryAxisExists || bSecondaryAxisExists ) )
    {
        nAxisIdx = maAxes[0].nAxisId;
        nAxisIdy = maAxes[1].nAxisId;
    }
    else
    {
        nAxisIdx = lcl_generateRandomValue();
        nAxisIdy = lcl_generateRandomValue();
        AxesType eXAxis = bPrimaryAxes ? AXIS_PRIMARY_X : AXIS_SECONDARY_X;
        AxesType eYAxis = bPrimaryAxes ? AXIS_PRIMARY_Y : AXIS_SECONDARY_Y;
        maAxes.emplace_back( eXAxis, nAxisIdx, nAxisIdy );
        maAxes.emplace_back( eYAxis, nAxisIdy, nAxisIdx );
    }
    // Export IDs
    FSHelperPtr pFS = GetFS();
    pFS->singleElement(FSNS(XML_c, XML_axId), XML_val, OString::number(nAxisIdx));
    pFS->singleElement(FSNS(XML_c, XML_axId), XML_val, OString::number(nAxisIdy));
    if (mbHasZAxis)
    {
        sal_Int32 nAxisIdz = 0;
        if( isDeep3dChart() )
        {
            nAxisIdz = lcl_generateRandomValue();
            maAxes.emplace_back( AXIS_PRIMARY_Z, nAxisIdz, nAxisIdy );
        }
        pFS->singleElement(FSNS(XML_c, XML_axId), XML_val, OString::number(nAxisIdz));
    }
}

void ChartExport::exportGrouping( bool isBar )
{
    FSHelperPtr pFS = GetFS();
    Reference< XPropertySet > xPropSet( mxDiagram , uno::UNO_QUERY);
    // grouping
    if( GetProperty( xPropSet, u"Stacked"_ustr ) )
        mAny >>= mbStacked;
    if( GetProperty( xPropSet, u"Percent"_ustr ) )
        mAny >>= mbPercent;

    const char* grouping = nullptr;
    if (mbStacked)
        grouping = "stacked";
    else if (mbPercent)
        grouping = "percentStacked";
    else
    {
        if( isBar && !isDeep3dChart() )
        {
            grouping = "clustered";
        }
        else
            grouping = "standard";
    }
    pFS->singleElement(FSNS(XML_c, XML_grouping), XML_val, grouping);
}

void ChartExport::exportTrendlines( const Reference< chart2::XDataSeries >& xSeries )
{
    FSHelperPtr pFS = GetFS();
    Reference< chart2::XRegressionCurveContainer > xRegressionCurveContainer( xSeries, UNO_QUERY );
    if( !xRegressionCurveContainer.is() )
        return;

    const Sequence< Reference< chart2::XRegressionCurve > > aRegCurveSeq = xRegressionCurveContainer->getRegressionCurves();
    for( const Reference< chart2::XRegressionCurve >& xRegCurve : aRegCurveSeq )
    {
        if (!xRegCurve.is())
            continue;

        Reference< XPropertySet > xProperties( xRegCurve , uno::UNO_QUERY );

        OUString aService;
        Reference< lang::XServiceName > xServiceName( xProperties, UNO_QUERY );
        if( !xServiceName.is() )
            continue;

        aService = xServiceName->getServiceName();

        if(aService != "com.sun.star.chart2.LinearRegressionCurve" &&
                aService != "com.sun.star.chart2.ExponentialRegressionCurve" &&
                aService != "com.sun.star.chart2.LogarithmicRegressionCurve" &&
                aService != "com.sun.star.chart2.PotentialRegressionCurve" &&
                aService != "com.sun.star.chart2.PolynomialRegressionCurve" &&
                aService != "com.sun.star.chart2.MovingAverageRegressionCurve")
            continue;

        pFS->startElement(FSNS(XML_c, XML_trendline));

        OUString aName;
        xProperties->getPropertyValue(u"CurveName"_ustr) >>= aName;
        if(!aName.isEmpty())
        {
            pFS->startElement(FSNS(XML_c, XML_name));
            pFS->writeEscaped(aName);
            pFS->endElement( FSNS( XML_c, XML_name) );
        }

        exportShapeProps( xProperties, false );

        if( aService == "com.sun.star.chart2.LinearRegressionCurve" )
        {
            pFS->singleElement(FSNS(XML_c, XML_trendlineType), XML_val, "linear");
        }
        else if( aService == "com.sun.star.chart2.ExponentialRegressionCurve" )
        {
            pFS->singleElement(FSNS(XML_c, XML_trendlineType), XML_val, "exp");
        }
        else if( aService == "com.sun.star.chart2.LogarithmicRegressionCurve" )
        {
            pFS->singleElement(FSNS(XML_c, XML_trendlineType), XML_val, "log");
        }
        else if( aService == "com.sun.star.chart2.PotentialRegressionCurve" )
        {
            pFS->singleElement(FSNS(XML_c, XML_trendlineType), XML_val, "power");
        }
        else if( aService == "com.sun.star.chart2.PolynomialRegressionCurve" )
        {
            pFS->singleElement(FSNS(XML_c, XML_trendlineType), XML_val, "poly");

            sal_Int32 aDegree = 2;
            xProperties->getPropertyValue( u"PolynomialDegree"_ustr) >>= aDegree;
            pFS->singleElement(FSNS(XML_c, XML_order), XML_val, OString::number(aDegree));
        }
        else if( aService == "com.sun.star.chart2.MovingAverageRegressionCurve" )
        {
            pFS->singleElement(FSNS(XML_c, XML_trendlineType), XML_val, "movingAvg");

            sal_Int32 aPeriod = 2;
            xProperties->getPropertyValue( u"MovingAveragePeriod"_ustr) >>= aPeriod;

            pFS->singleElement(FSNS(XML_c, XML_period), XML_val, OString::number(aPeriod));
        }
        else
        {
            // should never happen
            // This would produce invalid OOXML files so we check earlier for the type
            assert(false);
        }

        double fExtrapolateForward = 0.0;
        double fExtrapolateBackward = 0.0;

        xProperties->getPropertyValue(u"ExtrapolateForward"_ustr) >>= fExtrapolateForward;
        xProperties->getPropertyValue(u"ExtrapolateBackward"_ustr) >>= fExtrapolateBackward;

        pFS->singleElement( FSNS( XML_c, XML_forward ),
                XML_val, OString::number(fExtrapolateForward) );

        pFS->singleElement( FSNS( XML_c, XML_backward ),
                XML_val, OString::number(fExtrapolateBackward) );

        bool bForceIntercept = false;
        xProperties->getPropertyValue(u"ForceIntercept"_ustr) >>= bForceIntercept;

        if (bForceIntercept)
        {
            double fInterceptValue = 0.0;
            xProperties->getPropertyValue(u"InterceptValue"_ustr) >>= fInterceptValue;

            pFS->singleElement( FSNS( XML_c, XML_intercept ),
                XML_val, OString::number(fInterceptValue) );
        }

        // Equation properties
        Reference< XPropertySet > xEquationProperties( xRegCurve->getEquationProperties() );

        // Show Equation
        bool bShowEquation = false;
        xEquationProperties->getPropertyValue(u"ShowEquation"_ustr) >>= bShowEquation;

        // Show R^2
        bool bShowCorrelationCoefficient = false;
        xEquationProperties->getPropertyValue(u"ShowCorrelationCoefficient"_ustr) >>= bShowCorrelationCoefficient;

        pFS->singleElement( FSNS( XML_c, XML_dispRSqr ),
                XML_val, ToPsz10(bShowCorrelationCoefficient) );

        pFS->singleElement(FSNS(XML_c, XML_dispEq), XML_val, ToPsz10(bShowEquation));

        pFS->endElement( FSNS( XML_c, XML_trendline ) );
    }
}

void ChartExport::exportMarker(const Reference< XPropertySet >& xPropSet)
{
    chart2::Symbol aSymbol;
    if( GetProperty( xPropSet, u"Symbol"_ustr ) )
        mAny >>= aSymbol;

    if(aSymbol.Style != chart2::SymbolStyle_STANDARD && aSymbol.Style != chart2::SymbolStyle_NONE)
        return;

    FSHelperPtr pFS = GetFS();
    pFS->startElement(FSNS(XML_c, XML_marker));

    sal_Int32 nSymbol = aSymbol.StandardSymbol;
    // TODO: more properties support for marker
    const char* pSymbolType; // no initialization here, to let compiler warn if we have a code path
                             // where it stays uninitialized
    switch( nSymbol )
    {
        case 0:
            pSymbolType = "square";
            break;
        case 1:
            pSymbolType = "diamond";
            break;
        case 2:
        case 3:
        case 4:
        case 5:
            pSymbolType = "triangle";
            break;
        case 8:
            pSymbolType = "circle";
            break;
        case 9:
            pSymbolType = "star";
            break;
        case 10:
            pSymbolType = "x"; // in MS office 2010 built in symbol marker 'X' is represented as 'x'
            break;
        case 11:
            pSymbolType = "plus";
            break;
        case 13:
            pSymbolType = "dash";
            break;
        default:
            pSymbolType = "square";
            break;
    }

    bool bSkipFormatting = false;
    if (aSymbol.Style == chart2::SymbolStyle_NONE)
    {
        bSkipFormatting = true;
        pSymbolType = "none";
    }

    pFS->singleElement(FSNS(XML_c, XML_symbol), XML_val, pSymbolType);

    if (!bSkipFormatting)
    {
        awt::Size aSymbolSize = aSymbol.Size;
        sal_Int32 nSize = std::max( aSymbolSize.Width, aSymbolSize.Height );

        nSize = nSize/250.0*7.0 + 1; // just guessed based on some test cases,
        //the value is always 1 less than the actual value.
        nSize = std::clamp( int(nSize), 2, 72 );
        pFS->singleElement(FSNS(XML_c, XML_size), XML_val, OString::number(nSize));

        pFS->startElement(FSNS(XML_c, XML_spPr));

        util::Color aColor = aSymbol.FillColor;
        if (GetProperty(xPropSet, u"Color"_ustr))
            mAny >>= aColor;

        if (aColor == -1)
        {
            pFS->singleElement(FSNS(XML_a, XML_noFill));
        }
        else
            WriteSolidFill(::Color(ColorTransparency, aColor));

        pFS->endElement( FSNS( XML_c, XML_spPr ) );
    }

    pFS->endElement( FSNS( XML_c, XML_marker ) );
}

void ChartExport::exportSmooth()
{
    FSHelperPtr pFS = GetFS();
    Reference< XPropertySet > xPropSet( mxDiagram , uno::UNO_QUERY );
    sal_Int32 nSplineType = 0;
    if( GetProperty( xPropSet, u"SplineType"_ustr ) )
        mAny >>= nSplineType;
    const char* pVal = nSplineType != 0 ? "1" : "0";
    pFS->singleElement(FSNS(XML_c, XML_smooth), XML_val, pVal);
}

void ChartExport::exportFirstSliceAng( )
{
    FSHelperPtr pFS = GetFS();
    sal_Int32 nStartingAngle = 0;
    Reference< XPropertySet > xPropSet( mxDiagram , uno::UNO_QUERY);
    if( GetProperty( xPropSet, u"StartingAngle"_ustr ) )
        mAny >>= nStartingAngle;

    // convert to ooxml angle
    nStartingAngle = (450 - nStartingAngle ) % 360;
    pFS->singleElement(FSNS(XML_c, XML_firstSliceAng), XML_val, OString::number(nStartingAngle));
}

namespace {

const char* getErrorBarStyle(sal_Int32 nErrorBarStyle)
{
    switch(nErrorBarStyle)
    {
        case cssc::ErrorBarStyle::NONE:
            return nullptr;
        case cssc::ErrorBarStyle::VARIANCE:
            break;
        case cssc::ErrorBarStyle::STANDARD_DEVIATION:
            return "stdDev";
        case cssc::ErrorBarStyle::ABSOLUTE:
            return "fixedVal";
        case cssc::ErrorBarStyle::RELATIVE:
            return "percentage";
        case cssc::ErrorBarStyle::ERROR_MARGIN:
            break;
        case cssc::ErrorBarStyle::STANDARD_ERROR:
            return "stdErr";
        case cssc::ErrorBarStyle::FROM_DATA:
            return "cust";
        default:
            assert(false && "can't happen");
    }
    return nullptr;
}

Reference< chart2::data::XDataSequence>  getLabeledSequence(
        const uno::Sequence< uno::Reference< chart2::data::XLabeledDataSequence > >& aSequences,
        bool bPositive )
{
    OUString aDirection;
    if(bPositive)
        aDirection = "positive";
    else
        aDirection = "negative";

    for( const auto& rSequence : aSequences )
    {
        if( rSequence.is())
        {
            uno::Reference< chart2::data::XDataSequence > xSequence( rSequence->getValues());
            uno::Reference< beans::XPropertySet > xSeqProp( xSequence, uno::UNO_QUERY_THROW );
            OUString aRole;
            if( ( xSeqProp->getPropertyValue( u"Role"_ustr ) >>= aRole ) &&
                    aRole.match( "error-bars" ) && aRole.indexOf(aDirection) >= 0 )
            {
                return xSequence;
            }
        }
    }

    return Reference< chart2::data::XDataSequence > ();
}

}

void ChartExport::exportErrorBar(const Reference< XPropertySet>& xErrorBarProps, bool bYError)
{
    sal_Int32 nErrorBarStyle = cssc::ErrorBarStyle::NONE;
    xErrorBarProps->getPropertyValue(u"ErrorBarStyle"_ustr) >>= nErrorBarStyle;
    const char* pErrorBarStyle = getErrorBarStyle(nErrorBarStyle);
    if(!pErrorBarStyle)
        return;

    FSHelperPtr pFS = GetFS();
    pFS->startElement(FSNS(XML_c, XML_errBars));
    pFS->singleElement(FSNS(XML_c, XML_errDir), XML_val, bYError ? "y" : "x");
    bool bPositive = false, bNegative = false;
    xErrorBarProps->getPropertyValue(u"ShowPositiveError"_ustr) >>= bPositive;
    xErrorBarProps->getPropertyValue(u"ShowNegativeError"_ustr) >>= bNegative;
    const char* pErrBarType;
    if(bPositive && bNegative)
        pErrBarType = "both";
    else if(bPositive)
        pErrBarType = "plus";
    else if(bNegative)
        pErrBarType = "minus";
    else
    {
        // what the hell should we do now?
        // at least this makes the file valid
        pErrBarType = "both";
    }
    pFS->singleElement(FSNS(XML_c, XML_errBarType), XML_val, pErrBarType);
    pFS->singleElement(FSNS(XML_c, XML_errValType), XML_val, pErrorBarStyle);
    pFS->singleElement(FSNS(XML_c, XML_noEndCap), XML_val, "0");
    if(nErrorBarStyle == cssc::ErrorBarStyle::FROM_DATA)
    {
        uno::Reference< chart2::data::XDataSource > xDataSource(xErrorBarProps, uno::UNO_QUERY);
        Sequence< Reference < chart2::data::XLabeledDataSequence > > aSequences =
            xDataSource->getDataSequences();

        if(bPositive)
        {
            exportSeriesValues(getLabeledSequence(aSequences, true), XML_plus);
        }

        if(bNegative)
        {
            exportSeriesValues(getLabeledSequence(aSequences, false), XML_minus);
        }
    }
    else
    {
        double nVal = 0.0;
        if(nErrorBarStyle == cssc::ErrorBarStyle::STANDARD_DEVIATION)
        {
            xErrorBarProps->getPropertyValue(u"Weight"_ustr) >>= nVal;
        }
        else
        {
            if(bPositive)
                xErrorBarProps->getPropertyValue(u"PositiveError"_ustr) >>= nVal;
            else
                xErrorBarProps->getPropertyValue(u"NegativeError"_ustr) >>= nVal;
        }

        pFS->singleElement(FSNS(XML_c, XML_val), XML_val, OString::number(nVal));
    }

    exportShapeProps( xErrorBarProps, false );

    pFS->endElement( FSNS( XML_c, XML_errBars) );
}

void ChartExport::exportView3D()
{
    Reference< XPropertySet > xPropSet( mxDiagram , uno::UNO_QUERY);
    if( !xPropSet.is() )
        return;
    FSHelperPtr pFS = GetFS();
    pFS->startElement(FSNS(XML_c, XML_view3D));
    sal_Int32 eChartType = getChartType( );
    // rotX
    if( GetProperty( xPropSet, u"RotationHorizontal"_ustr ) )
    {
        sal_Int32 nRotationX = 0;
        mAny >>= nRotationX;
        if( nRotationX < 0 )
        {
            if(eChartType == chart::TYPEID_PIE)
            {
            /* In OOXML we get value in 0..90 range for pie chart X rotation , whereas we expect it to be in -90..90 range,
               so we convert that during import. It is modified in View3DConverter::convertFromModel()
               here we convert it back to 0..90 as we received in import */
               nRotationX += 90;  // X rotation (map Chart2 [-179,180] to OOXML [0..90])
            }
            else
                nRotationX += 360; // X rotation (map Chart2 [-179,180] to OOXML [-90..90])
        }
        pFS->singleElement(FSNS(XML_c, XML_rotX), XML_val, OString::number(nRotationX));
    }
    // rotY
    if( GetProperty( xPropSet, u"RotationVertical"_ustr ) )
    {
        // Y rotation (map Chart2 [-179,180] to OOXML [0..359])
        if( eChartType == chart::TYPEID_PIE && GetProperty( xPropSet, u"StartingAngle"_ustr ) )
        {
         // Y rotation used as 'first pie slice angle' in 3D pie charts
            sal_Int32 nStartingAngle=0;
            mAny >>= nStartingAngle;
            // convert to ooxml angle
            nStartingAngle = (450 - nStartingAngle ) % 360;
            pFS->singleElement(FSNS(XML_c, XML_rotY), XML_val, OString::number(nStartingAngle));
        }
        else
        {
            sal_Int32 nRotationY = 0;
            mAny >>= nRotationY;
            // Y rotation (map Chart2 [-179,180] to OOXML [0..359])
            if( nRotationY < 0 )
                nRotationY += 360;
            pFS->singleElement(FSNS(XML_c, XML_rotY), XML_val, OString::number(nRotationY));
        }
    }
    // rAngAx
    if( GetProperty( xPropSet, u"RightAngledAxes"_ustr ) )
    {
        bool bRightAngled = false;
        mAny >>= bRightAngled;
        const char* sRightAngled = bRightAngled ? "1":"0";
        pFS->singleElement(FSNS(XML_c, XML_rAngAx), XML_val, sRightAngled);
    }
    // perspective
    if( GetProperty( xPropSet, u"Perspective"_ustr ) )
    {
        sal_Int32 nPerspective = 0;
        mAny >>= nPerspective;
        // map Chart2 [0,100] to OOXML [0..200]
        nPerspective *= 2;
        pFS->singleElement(FSNS(XML_c, XML_perspective), XML_val, OString::number(nPerspective));
    }
    pFS->endElement( FSNS( XML_c, XML_view3D ) );
}

bool ChartExport::isDeep3dChart()
{
    bool isDeep = false;
    if( mbIs3DChart )
    {
        Reference< XPropertySet > xPropSet( mxDiagram , uno::UNO_QUERY);
        if( GetProperty( xPropSet, u"Deep"_ustr ) )
            mAny >>= isDeep;
    }
    return isDeep;
}

bool ChartExport::isChartexNotChartNS() const
{
    Reference< chart2::XCoordinateSystemContainer > xBCooSysCnt( mxNewDiagram, uno::UNO_QUERY );
    if( ! xBCooSysCnt.is()) return false;

    // chart type
    const Sequence< Reference< chart2::XCoordinateSystem > >
        aCooSysSeq( xBCooSysCnt->getCoordinateSystems());

    for( const auto& rCS : aCooSysSeq ) {
        Reference< chart2::XChartTypeContainer > xCTCnt( rCS, uno::UNO_QUERY );
        if( ! xCTCnt.is())
            continue;
        const Sequence< Reference< chart2::XChartType > > aCTSeq( xCTCnt->getChartTypes());
        for( const auto& rCT : aCTSeq ) {
            Reference< chart2::XDataSeriesContainer > xDSCnt( rCT, uno::UNO_QUERY );
            if( ! xDSCnt.is())
                return false;
            Reference< chart2::XChartType > xChartType( rCT, uno::UNO_QUERY );
            if( ! xChartType.is())
                continue;
            // note: if xDSCnt.is() then also aCTSeq[nCTIdx]
            OUString aChartType( xChartType->getChartType());
            sal_Int32 eChartType = lcl_getChartType( aChartType );
            switch( eChartType )
            {
                case chart::TYPEID_BAR:
                case chart::TYPEID_AREA:
                case chart::TYPEID_LINE:
                case chart::TYPEID_BUBBLE:
                case chart::TYPEID_OFPIE:
                case chart::TYPEID_DOUGHNUT:
                case chart::TYPEID_PIE:
                case chart::TYPEID_RADARLINE:
                case chart::TYPEID_RADARAREA:
                case chart::TYPEID_SCATTER:
                case chart::TYPEID_STOCK:
                case chart::TYPEID_SURFACE:
                    break;
                case chart::TYPEID_FUNNEL:
                    return true;
                default:
                    assert(false);
                    break;
            }
        }
    }
    return false;
}

OUString ChartExport::getNumberFormatCode(sal_Int32 nKey) const
{
    /* XXX if this was called more than one or two times per export the two
     * SvNumberFormatter instances and NfKeywordTable should be member
     * variables and initialized only once. */

    OUString aCode(u"General"_ustr);  // init with fallback
    uno::Reference<util::XNumberFormatsSupplier> xNumberFormatsSupplier(mxChartModel, uno::UNO_QUERY_THROW);
    SvNumberFormatsSupplierObj* pSupplierObj = comphelper::getFromUnoTunnel<SvNumberFormatsSupplierObj>( xNumberFormatsSupplier);
    if (!pSupplierObj)
        return aCode;

    SvNumberFormatter* pNumberFormatter = pSupplierObj->GetNumberFormatter();
    if (!pNumberFormatter)
        return aCode;

    SvNumberFormatter aTempFormatter( comphelper::getProcessComponentContext(), LANGUAGE_ENGLISH_US);
    NfKeywordTable aKeywords;
    aTempFormatter.FillKeywordTableForExcel( aKeywords);
    aCode = pNumberFormatter->GetFormatStringForExcel( nKey, aKeywords, aTempFormatter);

    return aCode;
}

}// oox

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
