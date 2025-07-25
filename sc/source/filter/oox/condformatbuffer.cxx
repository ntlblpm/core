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

#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <condformatbuffer.hxx>
#include <formulaparser.hxx>

#include <com/sun/star/sheet/ConditionOperator2.hpp>
#include <sal/log.hxx>
#include <osl/diagnose.h>
#include <o3tl/string_view.hxx>
#include <svl/sharedstringpool.hxx>
#include <oox/core/filterbase.hxx>
#include <oox/helper/binaryinputstream.hxx>
#include <oox/helper/attributelist.hxx>
#include <oox/token/tokens.hxx>
#include <addressconverter.hxx>
#include <biffhelper.hxx>
#include <stylesbuffer.hxx>
#include <themebuffer.hxx>

#include <colorscale.hxx>
#include <conditio.hxx>
#include <document.hxx>
#include <tokenarray.hxx>
#include <tokenuno.hxx>
#include <extlstcontext.hxx>
#include <utility>

namespace oox::xls {

using namespace ::com::sun::star::sheet;
using namespace ::com::sun::star::uno;

namespace {

const sal_Int32 BIFF12_CFRULE_TYPE_CELLIS           = 1;
const sal_Int32 BIFF12_CFRULE_TYPE_EXPRESSION       = 2;
const sal_Int32 BIFF12_CFRULE_TYPE_COLORSCALE       = 3;
const sal_Int32 BIFF12_CFRULE_TYPE_DATABAR          = 4;
const sal_Int32 BIFF12_CFRULE_TYPE_TOPTEN           = 5;
const sal_Int32 BIFF12_CFRULE_TYPE_ICONSET          = 6;

const sal_Int32 BIFF12_CFRULE_SUB_CELLIS            = 0;
const sal_Int32 BIFF12_CFRULE_SUB_EXPRESSION        = 1;
const sal_Int32 BIFF12_CFRULE_SUB_COLORSCALE        = 2;
const sal_Int32 BIFF12_CFRULE_SUB_DATABAR           = 3;
const sal_Int32 BIFF12_CFRULE_SUB_ICONSET           = 4;
const sal_Int32 BIFF12_CFRULE_SUB_TOPTEN            = 5;
const sal_Int32 BIFF12_CFRULE_SUB_UNIQUE            = 7;
const sal_Int32 BIFF12_CFRULE_SUB_TEXT              = 8;
const sal_Int32 BIFF12_CFRULE_SUB_BLANK             = 9;
const sal_Int32 BIFF12_CFRULE_SUB_NOTBLANK          = 10;
const sal_Int32 BIFF12_CFRULE_SUB_ERROR             = 11;
const sal_Int32 BIFF12_CFRULE_SUB_NOTERROR          = 12;
const sal_Int32 BIFF12_CFRULE_SUB_TODAY             = 15;
const sal_Int32 BIFF12_CFRULE_SUB_TOMORROW          = 16;
const sal_Int32 BIFF12_CFRULE_SUB_YESTERDAY         = 17;
const sal_Int32 BIFF12_CFRULE_SUB_LAST7DAYS         = 18;
const sal_Int32 BIFF12_CFRULE_SUB_LASTMONTH         = 19;
const sal_Int32 BIFF12_CFRULE_SUB_NEXTMONTH         = 20;
const sal_Int32 BIFF12_CFRULE_SUB_THISWEEK          = 21;
const sal_Int32 BIFF12_CFRULE_SUB_NEXTWEEK          = 22;
const sal_Int32 BIFF12_CFRULE_SUB_LASTWEEK          = 23;
const sal_Int32 BIFF12_CFRULE_SUB_THISMONTH         = 24;
const sal_Int32 BIFF12_CFRULE_SUB_ABOVEAVERAGE      = 25;
const sal_Int32 BIFF12_CFRULE_SUB_BELOWAVERAGE      = 26;
const sal_Int32 BIFF12_CFRULE_SUB_DUPLICATE         = 27;
const sal_Int32 BIFF12_CFRULE_SUB_EQABOVEAVERAGE    = 29;
const sal_Int32 BIFF12_CFRULE_SUB_EQBELOWAVERAGE    = 30;

const sal_Int32 BIFF12_CFRULE_TIMEOP_TODAY          = 0;
const sal_Int32 BIFF12_CFRULE_TIMEOP_YESTERDAY      = 1;
const sal_Int32 BIFF12_CFRULE_TIMEOP_LAST7DAYS      = 2;
const sal_Int32 BIFF12_CFRULE_TIMEOP_THISWEEK       = 3;
const sal_Int32 BIFF12_CFRULE_TIMEOP_LASTWEEK       = 4;
const sal_Int32 BIFF12_CFRULE_TIMEOP_LASTMONTH      = 5;
const sal_Int32 BIFF12_CFRULE_TIMEOP_TOMORROW       = 6;
const sal_Int32 BIFF12_CFRULE_TIMEOP_NEXTWEEK       = 7;
const sal_Int32 BIFF12_CFRULE_TIMEOP_NEXTMONTH      = 8;
const sal_Int32 BIFF12_CFRULE_TIMEOP_THISMONTH      = 9;

const sal_uInt16 BIFF12_CFRULE_STOPIFTRUE           = 0x0002;
const sal_uInt16 BIFF12_CFRULE_ABOVEAVERAGE         = 0x0004;
const sal_uInt16 BIFF12_CFRULE_BOTTOM               = 0x0008;
const sal_uInt16 BIFF12_CFRULE_PERCENT              = 0x0010;

bool isValue(std::u16string_view rStr, double& rVal)
{
    sal_Int32 nEnd = -1;
    rVal = rtl::math::stringToDouble(o3tl::trim(rStr), '.', ',', nullptr, &nEnd);

    return nEnd >= static_cast<sal_Int32>(rStr.size());
}

void SetCfvoData( ColorScaleRuleModelEntry* pEntry, const AttributeList& rAttribs )
{
    OUString aType = rAttribs.getString( XML_type, OUString() );
    OUString aVal = rAttribs.getString(XML_val, OUString());
    OUString aGreaterThanOrEqual = rAttribs.getString(XML_gte, OUString());

    if (!aGreaterThanOrEqual.isEmpty())
    {
        if (!aGreaterThanOrEqual.toBoolean())
            pEntry->meMode = ScConditionMode::Greater;
    }

    if (aVal != "\"\"")
    {
        double nVal = 0.0;
        bool bVal = isValue(aVal, nVal);
        if( !bVal || aType == "formula" )
        {
            pEntry->maFormula = aVal;
        }
        else
        {
            pEntry->mnVal = nVal;
        }
    }

    if (aType == "num")
    {
        pEntry->mbNum = true;
    }
    else if( aType == "min" )
    {
        pEntry->mbMin = true;
    }
    else if( aType == "max" )
    {
        pEntry->mbMax = true;
    }
    else if( aType == "percent" )
    {
        pEntry->mbPercent = true;
    }
    else if( aType == "percentile" )
    {
        pEntry->mbPercentile = true;
    }
}

}

ColorScaleRule::ColorScaleRule( const CondFormat& rFormat ):
    WorksheetHelper( rFormat ),
    mnCfvo(0),
    mnCol(0)
{
}

void ColorScaleRule::importCfvo( const AttributeList& rAttribs )
{
    if(mnCfvo >= maColorScaleRuleEntries.size())
        maColorScaleRuleEntries.emplace_back();

    SetCfvoData( &maColorScaleRuleEntries[mnCfvo], rAttribs );

    ++mnCfvo;
}

// https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.spreadsheet.indexedcolors?view=openxml-2.8.1
const ::Color IndexedColors[] = {
    0x00000000,
    0x00FFFFFF,
    0x00FF0000,
    0x0000FF00,
    0x000000FF,
    0x00FFFF00,
    0x00FF00FF,
    0x0000FFFF,
    0x00000000,
    0x00FFFFFF,
    0x00FF0000,
    0x0000FF00,
    0x000000FF,
    0x00FFFF00,
    0x00FF00FF,
    0x0000FFFF,
    0x00800000,
    0x00008000,
    0x00000080,
    0x00808000,
    0x00800080,
    0x00008080,
    0x00C0C0C0,
    0x00808080,
    0x009999FF,
    0x00993366,
    0x00FFFFCC,
    0x00CCFFFF,
    0x00660066,
    0x00FF8080,
    0x000066CC,
    0x00CCCCFF,
    0x00000080,
    0x00FF00FF,
    0x00FFFF00,
    0x0000FFFF,
    0x00800080,
    0x00800000,
    0x00008080,
    0x000000FF,
    0x0000CCFF,
    0x00CCFFFF,
    0x00CCFFCC,
    0x00FFFF99,
    0x0099CCFF,
    0x00FF99CC,
    0x00CC99FF,
    0x00FFCC99,
    0x003366FF,
    0x0033CCCC,
    0x0099CC00,
    0x00FFCC00,
    0x00FF9900,
    0x00FF6600,
    0x00666699,
    0x00969696,
    0x00003366,
    0x00339966,
    0x00003300,
    0x00333300,
    0x00993300,
    0x00993366,
    0x00333399,
    0x00333333,
    0x00000000, // System Foreground ?
    0x00000000, // System Background ?
};

namespace {

::Color importOOXColor(const AttributeList& rAttribs, const ThemeBuffer& rThemeBuffer, const GraphicHelper& rGraphicHelper)
{
    ::Color nColor;
    if( rAttribs.hasAttribute( XML_rgb ) )
        nColor = ::Color(ColorTransparency, rAttribs.getUnsignedHex( XML_rgb, UNSIGNED_RGB_TRANSPARENT ));
    else if( rAttribs.hasAttribute( XML_theme ) )
    {
        sal_uInt32 nThemeIndex = rAttribs.getUnsigned( XML_theme, 0 );

        // Excel has a bug in the mapping of index 0, 1, 2 and 3.
        if (nThemeIndex == 0)
            nThemeIndex = 1;
        else if (nThemeIndex == 1)
            nThemeIndex = 0;
        else if (nThemeIndex == 2)
            nThemeIndex = 3;
        else if (nThemeIndex == 3)
            nThemeIndex = 2;

        nColor = rThemeBuffer.getColorByIndex( nThemeIndex );
    }
    else if (rAttribs.hasAttribute(XML_indexed))
    {
        sal_uInt32 nIndexed = rAttribs.getUnsigned(XML_indexed, 0);
        if (nIndexed < std::size(IndexedColors))
            nColor = IndexedColors[nIndexed];
    }

    ::Color aColor;
    double nTint = rAttribs.getDouble(XML_tint, 0.0);
    if (nTint != 0.0)
    {
        oox::drawingml::Color aDMColor;
        aDMColor.setSrgbClr(nColor);
        aDMColor.addExcelTintTransformation(nTint);
        aColor = aDMColor.getColor(rGraphicHelper);
    }
    else
        aColor = nColor.GetRGBColor();

    return aColor;
}

}

void ColorScaleRule::importColor( const AttributeList& rAttribs )
{
    ThemeBuffer& rThemeBuffer = getTheme();
    GraphicHelper& rGraphicHelper = getBaseFilter().getGraphicHelper();
    ::Color aColor = importOOXColor(rAttribs, rThemeBuffer, rGraphicHelper);

    if(mnCol >= maColorScaleRuleEntries.size())
        maColorScaleRuleEntries.emplace_back();

    maColorScaleRuleEntries[mnCol].maColor = aColor;
    ++mnCol;
}

namespace {

ScColorScaleEntry* ConvertToModel( const ColorScaleRuleModelEntry& rEntry, ScDocument& rDoc, const ScAddress& rAddr )
{
        ScColorScaleEntry* pEntry = new ScColorScaleEntry(rEntry.mnVal, rEntry.maColor);

        if(rEntry.mbMin)
            pEntry->SetType(COLORSCALE_MIN);
        if(rEntry.mbMax)
            pEntry->SetType(COLORSCALE_MAX);
        if(rEntry.mbPercent)
            pEntry->SetType(COLORSCALE_PERCENT);
        if(rEntry.mbPercentile)
            pEntry->SetType(COLORSCALE_PERCENTILE);
        if (rEntry.mbNum)
            pEntry->SetType(COLORSCALE_VALUE);

        if(!rEntry.maFormula.isEmpty())
        {
            pEntry->SetType(COLORSCALE_FORMULA);
            pEntry->SetFormula(rEntry.maFormula, rDoc, rAddr, formula::FormulaGrammar::GRAM_ENGLISH_XL_A1);
        }

        pEntry->SetMode(rEntry.meMode);

        return pEntry;
}

}

void ColorScaleRule::AddEntries( ScColorScaleFormat* pFormat, ScDocument& rDoc, const ScAddress& rAddr )
{
    for(const ColorScaleRuleModelEntry & rEntry : maColorScaleRuleEntries)
    {
        ScColorScaleEntry* pEntry = ConvertToModel( rEntry, rDoc, rAddr );

        pFormat->AddEntry( pEntry );
    }
}

DataBarRule::DataBarRule( const CondFormat& rFormat ):
    WorksheetHelper( rFormat ),
    mxFormat(new ScDataBarFormatData)
{
    mxFormat->meAxisPosition = databar::NONE;
}

void DataBarRule::importColor( const AttributeList& rAttribs )
{
    ThemeBuffer& rThemeBuffer = getTheme();
    GraphicHelper& rGraphicHelper = getBaseFilter().getGraphicHelper();
    ::Color aColor = importOOXColor(rAttribs, rThemeBuffer, rGraphicHelper);

    mxFormat->maPositiveColor = aColor;
}

void DataBarRule::importCfvo( const AttributeList& rAttribs )
{
    ColorScaleRuleModelEntry* pEntry;
    if(!mpLowerLimit)
    {
        mpLowerLimit.reset(new ColorScaleRuleModelEntry);
        pEntry = mpLowerLimit.get();
    }
    else
    {
        mpUpperLimit.reset(new ColorScaleRuleModelEntry);
        pEntry = mpUpperLimit.get();
    }

    SetCfvoData( pEntry, rAttribs );
}

void DataBarRule::importAttribs( const AttributeList& rAttribs )
{
    mxFormat->mbOnlyBar = !rAttribs.getBool( XML_showValue, true );
    mxFormat->mnMinLength = rAttribs.getUnsigned( XML_minLength, 10);
    mxFormat->mnMaxLength = rAttribs.getUnsigned( XML_maxLength, 90);
}

void DataBarRule::SetData( ScDataBarFormat* pFormat, ScDocument& rDoc, const ScAddress& rAddr )
{
    ScColorScaleEntry* pUpperEntry = ConvertToModel(*mpUpperLimit, rDoc, rAddr);
    ScColorScaleEntry* pLowerEntry = ConvertToModel(*mpLowerLimit, rDoc, rAddr);

    mxFormat->mpUpperLimit.reset( pUpperEntry );
    mxFormat->mpLowerLimit.reset( pLowerEntry );
    pFormat->SetDataBarData(mxFormat.release());
}

IconSetRule::IconSetRule( const WorksheetHelper& rParent ):
    WorksheetHelper( rParent ),
    mxFormatData( new ScIconSetFormatData ),
    mbCustom(false)
{
}

void IconSetRule::importCfvo( const AttributeList& rAttribs )
{
    ColorScaleRuleModelEntry aNewEntry;
    SetCfvoData(&aNewEntry, rAttribs);

    maEntries.push_back(aNewEntry);
}

void IconSetRule::importAttribs( const AttributeList& rAttribs )
{
    maIconSetType = rAttribs.getString( XML_iconSet, u"3TrafficLights1"_ustr );
    mxFormatData->mbShowValue = rAttribs.getBool( XML_showValue, true );
    mxFormatData->mbReverse = rAttribs.getBool( XML_reverse, false );
    mbCustom = rAttribs.getBool(XML_custom, false);
}

void IconSetRule::importFormula(const OUString& rFormula)
{
    ColorScaleRuleModelEntry& rEntry = maEntries.back();
    double nVal = 0.0;
    if ((rEntry.mbNum || rEntry.mbPercent || rEntry.mbPercentile) && isValue(rFormula, nVal))
    {
        rEntry.mnVal = nVal;
    }
    else if (!rFormula.isEmpty())
        rEntry.maFormula = rFormula;
}

namespace {

ScIconSetType getType(std::u16string_view rName)
{
    ScIconSetType eIconSetType = IconSet_3TrafficLights1;
    const ScIconSetMap* pIconSetMap = ScIconSetFormat::g_IconSetMap;
    for(size_t i = 0; !pIconSetMap[i].aName.isEmpty(); ++i)
    {
        if(pIconSetMap[i].aName == rName)
        {
            eIconSetType = pIconSetMap[i].eType;
            break;
        }
    }

    return eIconSetType;
}

}

void IconSetRule::importIcon(const AttributeList& rAttribs)
{
    OUString aIconSet = rAttribs.getString(XML_iconSet, OUString());
    sal_Int32 nIndex = rAttribs.getInteger(XML_iconId, -1);
    if (aIconSet == "NoIcons")
    {
        nIndex = -1;
    }

    ScIconSetType eIconSetType = getType(aIconSet);
    mxFormatData->maCustomVector.emplace_back(eIconSetType, nIndex);
}

void IconSetRule::SetData( ScIconSetFormat* pFormat, ScDocument& rDoc, const ScAddress& rPos )
{
    for(const ColorScaleRuleModelEntry & rEntry : maEntries)
    {
        ScColorScaleEntry* pModelEntry = ConvertToModel( rEntry, rDoc, rPos );
        mxFormatData->m_Entries.emplace_back(pModelEntry);
    }

    mxFormatData->eIconSetType = getType(maIconSetType);
    mxFormatData->mbCustom = mbCustom;
    pFormat->SetIconSetData(mxFormatData.release());
}

CondFormatRuleModel::CondFormatRuleModel() :
    mnPriority( -1 ),
    mnType( XML_TOKEN_INVALID ),
    mnOperator( XML_TOKEN_INVALID ),
    mnTimePeriod( XML_TOKEN_INVALID ),
    mnRank( 0 ),
    mnStdDev( 0 ),
    mnDxfId( -1 ),
    mbStopIfTrue( false ),
    mbBottom( false ),
    mbPercent( false ),
    mbAboveAverage( true ),
    mbEqualAverage( false )
{
}

void CondFormatRuleModel::setBiffOperator( sal_Int32 nOperator )
{
    static const sal_Int32 spnOperators[] = {
        XML_TOKEN_INVALID, XML_between, XML_notBetween, XML_equal, XML_notEqual,
        XML_greaterThan, XML_lessThan, XML_greaterThanOrEqual, XML_lessThanOrEqual };
    mnOperator = STATIC_ARRAY_SELECT( spnOperators, nOperator, XML_TOKEN_INVALID );
}

void CondFormatRuleModel::setBiff12TextType( sal_Int32 nOperator )
{
    // note: type XML_notContainsText vs. operator XML_notContains
    static const sal_Int32 spnTypes[] = { XML_containsText, XML_notContainsText, XML_beginsWith, XML_endsWith };
    mnType = STATIC_ARRAY_SELECT( spnTypes, nOperator, XML_TOKEN_INVALID );
    static const sal_Int32 spnOperators[] = { XML_containsText, XML_notContains, XML_beginsWith, XML_endsWith };
    mnOperator = STATIC_ARRAY_SELECT( spnOperators, nOperator, XML_TOKEN_INVALID );
}

CondFormatRule::CondFormatRule( const CondFormat& rCondFormat, ScConditionalFormat* pFormat ) :
    WorksheetHelper( rCondFormat ),
    mrCondFormat( rCondFormat ),
    mpFormat(pFormat),
    mpFormatEntry(nullptr)
{
}

void CondFormatRule::importCfRule( const AttributeList& rAttribs )
{
    maModel.maText         = rAttribs.getString( XML_text, OUString() );
    maModel.mnPriority     = rAttribs.getInteger( XML_priority, -1 );
    maModel.mnType         = rAttribs.getToken( XML_type, XML_TOKEN_INVALID );
    maModel.mnOperator     = rAttribs.getToken( XML_operator, XML_TOKEN_INVALID );
    maModel.mnTimePeriod   = rAttribs.getToken( XML_timePeriod, XML_TOKEN_INVALID );
    maModel.mnRank         = rAttribs.getInteger( XML_rank, 0 );
    maModel.mnStdDev       = rAttribs.getInteger( XML_stdDev, 0 );
    maModel.mnDxfId        = rAttribs.getInteger( XML_dxfId, -1 );
    maModel.mbStopIfTrue   = rAttribs.getBool( XML_stopIfTrue, false );
    maModel.mbBottom       = rAttribs.getBool( XML_bottom, false );
    maModel.mbPercent      = rAttribs.getBool( XML_percent, false );
    maModel.mbAboveAverage = rAttribs.getBool( XML_aboveAverage, true );
    maModel.mbEqualAverage = rAttribs.getBool( XML_equalAverage, false );

    if(maModel.mnType == XML_colorScale)
    {
        //import the remaining values

    }
}

void CondFormatRule::appendFormula( const OUString& rFormula )
{
    ScAddress aBaseAddr = mrCondFormat.getRanges().GetTopLeftCorner();
    ApiTokenSequence aTokens = getFormulaParser().importFormula( aBaseAddr, rFormula );
    maModel.maFormulas.push_back( aTokens );
}

void CondFormatRule::importCfRule( SequenceInputStream& rStrm )
{
    sal_Int32 nType, nSubType, nOperator, nFmla1Size, nFmla2Size, nFmla3Size;
    sal_uInt16 nFlags;
    nType = rStrm.readInt32();
    nSubType = rStrm.readInt32();
    maModel.mnDxfId = rStrm.readInt32();
    maModel.mnPriority = rStrm.readInt32();
    nOperator = rStrm.readInt32();
    rStrm.skip( 8 );
    nFlags = rStrm.readuInt16();
    nFmla1Size = rStrm.readInt32();
    nFmla2Size = rStrm.readInt32();
    nFmla3Size = rStrm.readInt32();
    rStrm >> maModel.maText;

    /*  Import the formulas. For no obvious reason, the sizes of the formulas
        are already stored before. Nevertheless the following formulas contain
        their own sizes. */

    // first formula
    // I am not bored enough to bother simplifying these expressions
    SAL_WARN_IF( !( (nFmla1Size >= 0) || ((nFmla2Size == 0) && (nFmla3Size == 0)) ), "sc.filter", "CondFormatRule::importCfRule - missing first formula" );
    SAL_WARN_IF( !( (nFmla1Size > 0) == (rStrm.getRemaining() >= 8) ), "sc.filter", "CondFormatRule::importCfRule - formula size mismatch" );
    if( rStrm.getRemaining() >= 8 )
    {
        ScAddress aBaseAddr = mrCondFormat.getRanges().GetTopLeftCorner();
        ApiTokenSequence aTokens = getFormulaParser().importFormula( aBaseAddr, FormulaType::CondFormat, rStrm );
        maModel.maFormulas.push_back( aTokens );

        // second formula
        OSL_ENSURE( (nFmla2Size >= 0) || (nFmla3Size == 0), "CondFormatRule::importCfRule - missing second formula" );
        OSL_ENSURE( (nFmla2Size > 0) == (rStrm.getRemaining() >= 8), "CondFormatRule::importCfRule - formula size mismatch" );
        if( rStrm.getRemaining() >= 8 )
        {
            aTokens = getFormulaParser().importFormula( aBaseAddr, FormulaType::CondFormat, rStrm );
            maModel.maFormulas.push_back( aTokens );

            // third formula
            OSL_ENSURE( (nFmla3Size > 0) == (rStrm.getRemaining() >= 8), "CondFormatRule::importCfRule - formula size mismatch" );
            if( rStrm.getRemaining() >= 8 )
            {
                aTokens = getFormulaParser().importFormula( aBaseAddr, FormulaType::CondFormat, rStrm );
                maModel.maFormulas.push_back( aTokens );
            }
        }
    }

    // flags
    maModel.mbStopIfTrue   = getFlag( nFlags, BIFF12_CFRULE_STOPIFTRUE );
    maModel.mbBottom       = getFlag( nFlags, BIFF12_CFRULE_BOTTOM );
    maModel.mbPercent      = getFlag( nFlags, BIFF12_CFRULE_PERCENT );
    maModel.mbAboveAverage = getFlag( nFlags, BIFF12_CFRULE_ABOVEAVERAGE );
    // no flag for equalAverage, must be determined from subtype below...

    // Convert the type/operator settings. This is a real mess...
    switch( nType )
    {
        case BIFF12_CFRULE_TYPE_CELLIS:
            SAL_WARN_IF(
                nSubType != BIFF12_CFRULE_SUB_CELLIS, "sc.filter",
                "CondFormatRule::importCfRule - rule type/subtype mismatch");
            maModel.mnType = XML_cellIs;
            maModel.setBiffOperator( nOperator );
            OSL_ENSURE( maModel.mnOperator != XML_TOKEN_INVALID, "CondFormatRule::importCfRule - unknown operator" );
        break;
        case BIFF12_CFRULE_TYPE_EXPRESSION:
            // here we have to look at the subtype to find the real type...
            switch( nSubType )
            {
                case BIFF12_CFRULE_SUB_EXPRESSION:
                    OSL_ENSURE( nOperator == 0, "CondFormatRule::importCfRule - unexpected operator value" );
                    maModel.mnType = XML_expression;
                break;
                case BIFF12_CFRULE_SUB_UNIQUE:
                    OSL_ENSURE( nOperator == 0, "CondFormatRule::importCfRule - unexpected operator value" );
                    maModel.mnType = XML_uniqueValues;
                break;
                case BIFF12_CFRULE_SUB_TEXT:
                    maModel.setBiff12TextType( nOperator );
                    OSL_ENSURE( maModel.mnType != XML_TOKEN_INVALID, "CondFormatRule::importCfRule - unexpected operator value" );
                break;
                case BIFF12_CFRULE_SUB_BLANK:
                    OSL_ENSURE( nOperator == 0, "CondFormatRule::importCfRule - unexpected operator value" );
                    maModel.mnType = XML_containsBlanks;
                break;
                case BIFF12_CFRULE_SUB_NOTBLANK:
                    OSL_ENSURE( nOperator == 0, "CondFormatRule::importCfRule - unexpected operator value" );
                    maModel.mnType = XML_notContainsBlanks;
                break;
                case BIFF12_CFRULE_SUB_ERROR:
                    OSL_ENSURE( nOperator == 0, "CondFormatRule::importCfRule - unexpected operator value" );
                    maModel.mnType = XML_containsErrors;
                break;
                case BIFF12_CFRULE_SUB_NOTERROR:
                    OSL_ENSURE( nOperator == 0, "CondFormatRule::importCfRule - unexpected operator value" );
                    maModel.mnType = XML_notContainsErrors;
                break;
                case BIFF12_CFRULE_SUB_TODAY:
                    SAL_WARN_IF(
                        nOperator != BIFF12_CFRULE_TIMEOP_TODAY, "sc.filter",
                        "CondFormatRule::importCfRule - unexpected time operator value");
                    maModel.mnType = XML_timePeriod;
                    maModel.mnTimePeriod = XML_today;
                break;
                case BIFF12_CFRULE_SUB_TOMORROW:
                    SAL_WARN_IF(
                        nOperator != BIFF12_CFRULE_TIMEOP_TOMORROW, "sc.filter",
                        "CondFormatRule::importCfRule - unexpected time operator value");
                    maModel.mnType = XML_timePeriod;
                    maModel.mnTimePeriod = XML_tomorrow;
                break;
                case BIFF12_CFRULE_SUB_YESTERDAY:
                    SAL_WARN_IF(
                        nOperator != BIFF12_CFRULE_TIMEOP_YESTERDAY,
                        "sc.filter",
                        "CondFormatRule::importCfRule - unexpected time operator value");
                    maModel.mnType = XML_timePeriod;
                    maModel.mnTimePeriod = XML_yesterday;
                break;
                case BIFF12_CFRULE_SUB_LAST7DAYS:
                    SAL_WARN_IF(
                        nOperator != BIFF12_CFRULE_TIMEOP_LAST7DAYS,
                        "sc.filter",
                        "CondFormatRule::importCfRule - unexpected time operator value");
                    maModel.mnType = XML_timePeriod;
                    maModel.mnTimePeriod = XML_last7Days;
                break;
                case BIFF12_CFRULE_SUB_LASTMONTH:
                    SAL_WARN_IF(
                        nOperator != BIFF12_CFRULE_TIMEOP_LASTMONTH,
                        "sc.filter",
                        "CondFormatRule::importCfRule - unexpected time operator value");
                    maModel.mnType = XML_timePeriod;
                    maModel.mnTimePeriod = XML_lastMonth;
                break;
                case BIFF12_CFRULE_SUB_NEXTMONTH:
                    SAL_WARN_IF(
                        nOperator != BIFF12_CFRULE_TIMEOP_NEXTMONTH,
                        "sc.filter",
                        "CondFormatRule::importCfRule - unexpected time operator value");
                    maModel.mnType = XML_timePeriod;
                    maModel.mnTimePeriod = XML_nextMonth;
                break;
                case BIFF12_CFRULE_SUB_THISWEEK:
                    SAL_WARN_IF(
                        nOperator != BIFF12_CFRULE_TIMEOP_THISWEEK, "sc.filter",
                        "CondFormatRule::importCfRule - unexpected time operator value");
                    maModel.mnType = XML_timePeriod;
                    maModel.mnTimePeriod = XML_thisWeek;
                break;
                case BIFF12_CFRULE_SUB_NEXTWEEK:
                    SAL_WARN_IF(
                        nOperator != BIFF12_CFRULE_TIMEOP_NEXTWEEK, "sc.filter",
                        "CondFormatRule::importCfRule - unexpected time operator value");
                    maModel.mnType = XML_timePeriod;
                    maModel.mnTimePeriod = XML_nextWeek;
                break;
                case BIFF12_CFRULE_SUB_LASTWEEK:
                    SAL_WARN_IF(
                        nOperator != BIFF12_CFRULE_TIMEOP_LASTWEEK, "sc.filter",
                        "CondFormatRule::importCfRule - unexpected time operator value");
                    maModel.mnType = XML_timePeriod;
                    maModel.mnTimePeriod = XML_lastWeek;
                break;
                case BIFF12_CFRULE_SUB_THISMONTH:
                    SAL_WARN_IF(
                        nOperator != BIFF12_CFRULE_TIMEOP_THISMONTH,
                        "sc.filter",
                        "CondFormatRule::importCfRule - unexpected time operator value");
                    maModel.mnType = XML_timePeriod;
                    maModel.mnTimePeriod = XML_thisMonth;
                break;
                case BIFF12_CFRULE_SUB_ABOVEAVERAGE:
                    OSL_ENSURE( maModel.mbAboveAverage, "CondFormatRule::importCfRule - wrong above-average flag" );
                    maModel.mnType = XML_aboveAverage;
                    maModel.mnStdDev = nOperator;     // operator field used for standard deviation
                    maModel.mbAboveAverage = true;
                    maModel.mbEqualAverage = false;   // does not exist as real flag...
                break;
                case BIFF12_CFRULE_SUB_BELOWAVERAGE:
                    OSL_ENSURE( !maModel.mbAboveAverage, "CondFormatRule::importCfRule - wrong above-average flag" );
                    maModel.mnType = XML_aboveAverage;
                    maModel.mnStdDev = nOperator;     // operator field used for standard deviation
                    maModel.mbAboveAverage = false;
                    maModel.mbEqualAverage = false;   // does not exist as real flag...
                break;
                case BIFF12_CFRULE_SUB_DUPLICATE:
                    OSL_ENSURE( nOperator == 0, "CondFormatRule::importCfRule - unexpected operator value" );
                    maModel.mnType = XML_duplicateValues;
                break;
                case BIFF12_CFRULE_SUB_EQABOVEAVERAGE:
                    OSL_ENSURE( maModel.mbAboveAverage, "CondFormatRule::importCfRule - wrong above-average flag" );
                    maModel.mnType = XML_aboveAverage;
                    maModel.mnStdDev = nOperator;     // operator field used for standard deviation
                    maModel.mbAboveAverage = true;
                    maModel.mbEqualAverage = true;    // does not exist as real flag...
                break;
                case BIFF12_CFRULE_SUB_EQBELOWAVERAGE:
                    OSL_ENSURE( !maModel.mbAboveAverage, "CondFormatRule::importCfRule - wrong above-average flag" );
                    maModel.mnType = XML_aboveAverage;
                    maModel.mnStdDev = nOperator;     // operator field used for standard deviation
                    maModel.mbAboveAverage = false;
                    maModel.mbEqualAverage = true;    // does not exist as real flag...
                break;
            }
        break;
        case BIFF12_CFRULE_TYPE_COLORSCALE:
            SAL_WARN_IF(
                nSubType != BIFF12_CFRULE_SUB_COLORSCALE, "sc.filter",
                "CondFormatRule::importCfRule - rule type/subtype mismatch");
            OSL_ENSURE( nOperator == 0, "CondFormatRule::importCfRule - unexpected operator value" );
            maModel.mnType = XML_colorScale;
        break;
        case BIFF12_CFRULE_TYPE_DATABAR:
            SAL_WARN_IF(
                nSubType != BIFF12_CFRULE_SUB_DATABAR, "sc.filter",
                "CondFormatRule::importCfRule - rule type/subtype mismatch");
            OSL_ENSURE( nOperator == 0, "CondFormatRule::importCfRule - unexpected operator value" );
            maModel.mnType = XML_dataBar;
        break;
        case BIFF12_CFRULE_TYPE_TOPTEN:
            SAL_WARN_IF(
                nSubType != BIFF12_CFRULE_SUB_TOPTEN, "sc.filter",
                "CondFormatRule::importCfRule - rule type/subtype mismatch");
            maModel.mnType = XML_top10;
            maModel.mnRank = nOperator;   // operator field used for rank value
        break;
        case BIFF12_CFRULE_TYPE_ICONSET:
            SAL_WARN_IF(
                nSubType != BIFF12_CFRULE_SUB_ICONSET, "sc.filter",
                "CondFormatRule::importCfRule - rule type/subtype mismatch");
            OSL_ENSURE( nOperator == 0, "CondFormatRule::importCfRule - unexpected operator value" );
            maModel.mnType = XML_iconSet;
        break;
        default:
            OSL_FAIL( "CondFormatRule::importCfRule - unknown rule type" );
    }
}

void CondFormatRule::setFormatEntry(sal_Int32 nPriority, ScFormatEntry* pEntry)
{
    maModel.mnPriority = nPriority;
    mpFormatEntry = pEntry;
}

void CondFormatRule::finalizeImport()
{
    if (mpFormatEntry)
    {
        mpFormat->AddEntry(mpFormatEntry);
        return;
    }

    ScConditionMode eOperator = ScConditionMode::NONE;

    /*  Replacement formula for unsupported rule types (text comparison rules,
        time period rules, cell type rules). The replacement formulas below may
        contain several placeholders:
        - '#B' will be replaced by the current relative base address (may occur
            several times).
        - '#R' will be replaced by the entire range list of the conditional
            formatting (absolute addresses).
        - '#T' will be replaced by the quoted comparison text.
        - '#L' will be replaced by the length of the comparison text (from
            the 'text' attribute) used in text comparison rules.
        - '#K' will be replaced by the rank (from the 'rank' attribute) used in
            top-10 rules.
        - '#M' will be replaced by the top/bottom flag (from the 'bottom'
            attribute) used in the RANK function in top-10 rules.
        - '#C' will be replaced by one of the comparison operators <, >, <=, or
            >=, according to the 'aboveAverage' and 'equalAverage' flags.
     */
    OUString aReplaceFormula;

    switch( maModel.mnType )
    {
        case XML_cellIs:
            eOperator = CondFormatBuffer::convertToInternalOperator( maModel.mnOperator );
        break;
        case XML_duplicateValues:
            eOperator = ScConditionMode::Duplicate;
        break;
        case XML_uniqueValues:
            eOperator = ScConditionMode::NotDuplicate;
        break;
        case XML_expression:
            eOperator = ScConditionMode::Direct;
        break;
        case XML_containsText:
            OSL_ENSURE( maModel.mnOperator == XML_containsText, "CondFormatRule::finalizeImport - unexpected operator" );
            eOperator = ScConditionMode::ContainsText;
        break;
        case XML_notContainsText:
            // note: type XML_notContainsText vs. operator XML_notContains
            OSL_ENSURE( maModel.mnOperator == XML_notContains, "CondFormatRule::finalizeImport - unexpected operator" );
            eOperator = ScConditionMode::NotContainsText;
        break;
        case XML_beginsWith:
            OSL_ENSURE( maModel.mnOperator == XML_beginsWith, "CondFormatRule::finalizeImport - unexpected operator" );
            eOperator = ScConditionMode::BeginsWith;
        break;
        case XML_endsWith:
            OSL_ENSURE( maModel.mnOperator == XML_endsWith, "CondFormatRule::finalizeImport - unexpected operator" );
            eOperator = ScConditionMode::EndsWith;
        break;
        case XML_timePeriod:
        break;
        case XML_containsBlanks:
            aReplaceFormula = "LEN(TRIM(#B))=0";
        break;
        case XML_notContainsBlanks:
            aReplaceFormula = "LEN(TRIM(#B))>0";
        break;
        case XML_containsErrors:
            eOperator = ScConditionMode::Error;
        break;
        case XML_notContainsErrors:
            eOperator = ScConditionMode::NoError;
        break;
        case XML_top10:
            if(maModel.mbPercent)
            {
                if(maModel.mbBottom)
                    eOperator = ScConditionMode::BottomPercent;
                else
                    eOperator = ScConditionMode::TopPercent;
            }
            else
            {
                if(maModel.mbBottom)
                    eOperator = ScConditionMode::Bottom10;
                else
                    eOperator = ScConditionMode::Top10;
            }
        break;
        case XML_aboveAverage:
            if(maModel.mbAboveAverage)
            {
                if(maModel.mbEqualAverage)
                    eOperator = ScConditionMode::AboveEqualAverage;
                else
                    eOperator = ScConditionMode::AboveAverage;
            }
            else
            {
                if(maModel.mbEqualAverage)
                    eOperator = ScConditionMode::BelowEqualAverage;
                else
                    eOperator = ScConditionMode::BelowAverage;
            }
        break;
        case XML_colorScale:
        break;
    }

    if( !aReplaceFormula.isEmpty() )
    {
        OUString aAddress;
        sal_Int32 nStrPos = aReplaceFormula.getLength();
        while( (nStrPos = aReplaceFormula.lastIndexOf( '#', nStrPos )) >= 0 )
        {
            switch( aReplaceFormula[ nStrPos + 1 ] )
            {
                case 'B':       // current base address
                    if( aAddress.isEmpty() )
                        aAddress = FormulaProcessorBase::generateAddress2dString( mrCondFormat.getRanges().GetTopLeftCorner(), false );
                    aReplaceFormula = aReplaceFormula.replaceAt( nStrPos, 2, aAddress );
                break;
                default:
                    OSL_FAIL( "CondFormatRule::finalizeImport - unknown placeholder" );
            }
        }

        // set the replacement formula
        maModel.maFormulas.clear();
        appendFormula( aReplaceFormula );
        eOperator = ScConditionMode::Direct;
    }

    ScAddress aPos = mrCondFormat.getRanges().GetTopLeftCorner();

    if( eOperator == ScConditionMode::Error || eOperator == ScConditionMode::NoError )
    {
        ScDocument& rDoc = getScDocument();
        OUString aStyleName = getStyles().createDxfStyle( maModel.mnDxfId );
        ScCondFormatEntry* pNewEntry = new ScCondFormatEntry( eOperator, nullptr, nullptr, rDoc, aPos, aStyleName );
        mpFormat->AddEntry(pNewEntry);
    }
    else if( eOperator == ScConditionMode::BeginsWith || eOperator == ScConditionMode::EndsWith ||
            eOperator == ScConditionMode::ContainsText || eOperator == ScConditionMode::NotContainsText )
    {
        ScDocument& rDoc = getScDocument();
        ScTokenArray aTokenArray(rDoc);
        svl::SharedStringPool& rSPool = rDoc.GetSharedStringPool();
        aTokenArray.AddString(rSPool.intern(maModel.maText));
        OUString aStyleName = getStyles().createDxfStyle( maModel.mnDxfId );
        ScCondFormatEntry* pNewEntry = new ScCondFormatEntry( eOperator, &aTokenArray, nullptr, rDoc, aPos, aStyleName );
        mpFormat->AddEntry(pNewEntry);
    }
    else if( (eOperator != ScConditionMode::NONE) && !maModel.maFormulas.empty() )
    {
        ScDocument& rDoc = getScDocument();
        std::unique_ptr<ScTokenArray> pTokenArray2;
        if( maModel.maFormulas.size() >= 2)
        {
            pTokenArray2.reset(new ScTokenArray(rDoc));
            ScTokenConversion::ConvertToTokenArray(rDoc, *pTokenArray2, maModel.maFormulas[1]);
            rDoc.CheckLinkFormulaNeedingCheck(*pTokenArray2);
        }

        ScTokenArray aTokenArray(rDoc);
        OUString aStyleName = getStyles().createDxfStyle( maModel.mnDxfId );
        ScTokenConversion::ConvertToTokenArray( rDoc, aTokenArray, maModel.maFormulas[ 0 ] );
        rDoc.CheckLinkFormulaNeedingCheck( aTokenArray);
        ScCondFormatEntry* pNewEntry = new ScCondFormatEntry(eOperator,
                                            &aTokenArray, pTokenArray2.get(), rDoc, aPos, aStyleName);
        mpFormat->AddEntry(pNewEntry);
    }
    else if ( eOperator == ScConditionMode::Top10 || eOperator == ScConditionMode::Bottom10 ||
            eOperator == ScConditionMode::TopPercent || eOperator == ScConditionMode::BottomPercent )
    {
        ScDocument& rDoc = getScDocument();
        ScTokenArray aTokenArray(rDoc);
        aTokenArray.AddDouble( maModel.mnRank );
        OUString aStyleName = getStyles().createDxfStyle( maModel.mnDxfId );
        ScCondFormatEntry* pNewEntry = new ScCondFormatEntry( eOperator, &aTokenArray, nullptr, rDoc, aPos, aStyleName );
        mpFormat->AddEntry(pNewEntry);
    }
    else if( eOperator == ScConditionMode::AboveAverage || eOperator == ScConditionMode::BelowAverage ||
            eOperator == ScConditionMode::AboveEqualAverage || eOperator == ScConditionMode::BelowEqualAverage )
    {
        ScDocument& rDoc = getScDocument();
        // actually that is still unsupported
        ScTokenArray aTokenArrayDev(rDoc);
        aTokenArrayDev.AddDouble( maModel.mnStdDev );
        OUString aStyleName = getStyles().createDxfStyle( maModel.mnDxfId );
        ScCondFormatEntry* pNewEntry = new ScCondFormatEntry( eOperator, &aTokenArrayDev, nullptr, rDoc, aPos, aStyleName );
        mpFormat->AddEntry(pNewEntry);
    }
    else if( eOperator == ScConditionMode::Duplicate || eOperator == ScConditionMode::NotDuplicate )
    {
        ScDocument& rDoc = getScDocument();
        OUString aStyleName = getStyles().createDxfStyle( maModel.mnDxfId );
        ScCondFormatEntry* pNewEntry = new ScCondFormatEntry( eOperator, nullptr, nullptr, rDoc, aPos, aStyleName );
        mpFormat->AddEntry(pNewEntry);
    }
    else if( maModel.mnType == XML_timePeriod )
    {
        condformat::ScCondFormatDateType eDateType = condformat::TODAY;
        switch( maModel.mnTimePeriod )
        {
            case XML_yesterday:
                eDateType = condformat::YESTERDAY;
                break;
            case XML_today:
                eDateType = condformat::TODAY;
                break;
            case XML_tomorrow:
                eDateType = condformat::TOMORROW;
                break;
            case XML_last7Days:
                eDateType = condformat::LAST7DAYS;
                break;
            case XML_lastWeek:
                eDateType = condformat::LASTWEEK;
                break;
            case XML_thisWeek:
                eDateType = condformat::THISWEEK;
                break;
            case XML_nextWeek:
                eDateType = condformat::NEXTWEEK;
                break;
            case XML_lastMonth:
                eDateType = condformat::LASTMONTH;
                break;
            case XML_thisMonth:
                eDateType = condformat::THISMONTH;
                break;
            case XML_nextMonth:
                eDateType = condformat::NEXTMONTH;
                break;
            default:
                SAL_WARN("sc.filter", "CondFormatRule::finalizeImport - unknown time period type" );
        }

        ScDocument& rDoc = getScDocument();
        ScCondDateFormatEntry* pFormatEntry = new ScCondDateFormatEntry(rDoc);
        pFormatEntry->SetDateType(eDateType);
        OUString aStyleName = getStyles().createDxfStyle( maModel.mnDxfId );
        pFormatEntry->SetStyleName( aStyleName );

        mpFormat->AddEntry(pFormatEntry);
    }
    else if( mpColor )
    {
        ScDocument& rDoc = getScDocument();
        ScColorScaleFormat* pFormatEntry = new ScColorScaleFormat(rDoc);

        mpFormat->AddEntry(pFormatEntry);

        mpColor->AddEntries( pFormatEntry, rDoc, aPos );
    }
    else if (mpDataBar)
    {
        ScDocument& rDoc = getScDocument();
        ScDataBarFormat* pFormatEntry = new ScDataBarFormat(rDoc);

        mpFormat->AddEntry(pFormatEntry);
        mpDataBar->SetData( pFormatEntry, rDoc, aPos );

    }
    else if(mpIconSet)
    {
        ScDocument& rDoc = getScDocument();
        ScIconSetFormat* pFormatEntry = new ScIconSetFormat(rDoc);

        mpFormat->AddEntry(pFormatEntry);
        mpIconSet->SetData( pFormatEntry, rDoc, aPos );
    }
}

ColorScaleRule* CondFormatRule::getColorScale()
{
    if(!mpColor)
        mpColor.reset( new ColorScaleRule(mrCondFormat) );

    return mpColor.get();
}

DataBarRule* CondFormatRule::getDataBar()
{
    if(!mpDataBar)
        mpDataBar.reset( new DataBarRule(mrCondFormat) );

    return mpDataBar.get();
}

IconSetRule* CondFormatRule::getIconSet()
{
    if(!mpIconSet)
        mpIconSet.reset( new IconSetRule(mrCondFormat) );

    return mpIconSet.get();
}

CondFormatModel::CondFormatModel() :
    mbPivot( false )
{
}

CondFormat::CondFormat( const WorksheetHelper& rHelper ) :
    WorksheetHelper( rHelper ),
    mpFormat(nullptr),
    mbReadyForFinalize(false),
    mbOwnsFormat(true)
{
}

CondFormat::~CondFormat()
{
    if (mbOwnsFormat)
        delete mpFormat;
}

void CondFormat::importConditionalFormatting( const AttributeList& rAttribs )
{
    getAddressConverter().convertToCellRangeList( maModel.maRanges, rAttribs.getString( XML_sqref, OUString() ), getSheetIndex(), true );
    maModel.mbPivot = rAttribs.getBool( XML_pivot, false );
    mpFormat = new ScConditionalFormat(0, getScDocument());
}

std::unique_ptr<CondFormatRule> CondFormat::importCfRule( const AttributeList& rAttribs )
{
    std::unique_ptr<CondFormatRule> xRule = createRule();
    xRule->importCfRule( rAttribs );
    return xRule;
}

void CondFormat::importCondFormatting( SequenceInputStream& rStrm )
{
    BinRangeList aRanges;
    rStrm.skip( 8 );
    rStrm >> aRanges;
    getAddressConverter().convertToCellRangeList( maModel.maRanges, aRanges, getSheetIndex(), true );
    mpFormat = new ScConditionalFormat(0, getScDocument());
}

void CondFormat::importCfRule( SequenceInputStream& rStrm )
{
    std::unique_ptr<CondFormatRule> xRule = createRule();
    xRule->importCfRule( rStrm );
    insertRule( std::move(xRule) );
}

void CondFormat::finalizeImport()
{
    // probably some error in the xml if we are not ready
    if ( !mbReadyForFinalize )
        return;
    ScDocument& rDoc = getScDocument();
    mpFormat->SetRange(maModel.maRanges);
    for (auto & rPair : maRules)
        rPair.second->finalizeImport();

    if (mpFormat->size() > 0)
    {
        SCTAB nTab = maModel.maRanges.GetTopLeftCorner().Tab();
        mbOwnsFormat = false; // ownership transferred to std::unique_ptr -> ScDocument
        sal_Int32 nIndex = getScDocument().AddCondFormat(std::unique_ptr<ScConditionalFormat>(mpFormat), nTab);

        rDoc.AddCondFormatData( maModel.maRanges, nTab, nIndex );
    }
}

std::unique_ptr<CondFormatRule> CondFormat::createRule()
{
    return std::make_unique<CondFormatRule>( *this, mpFormat );
}

void CondFormat::insertRule( std::unique_ptr<CondFormatRule> xRule )
{
    if( xRule->getPriority() > 0 )
    {
        assert(&xRule->getParentCondFormat() == this);
        OSL_ENSURE( maRules.find( xRule->getPriority() ) == maRules.end(), "CondFormat::insertRule - multiple rules with equal priority" );
        maRules[ xRule->getPriority() ] = std::move(xRule);
    }
}

CondFormatBuffer::CondFormatBuffer( const WorksheetHelper& rHelper ) :
    WorksheetHelper( rHelper )
{
}

CondFormatRef CondFormatBuffer::importConditionalFormatting( const AttributeList& rAttribs )
{
    CondFormatRef xCondFmt = createCondFormat();
    xCondFmt->importConditionalFormatting( rAttribs );
    return xCondFmt;
}

namespace {

ScConditionalFormat* findFormatByRange(const ScRangeList& rRange, const ScDocument& rDoc, SCTAB nTab)
{
    ScConditionalFormatList* pList = rDoc.GetCondFormList(nTab);
    for (auto const& it : *pList)
    {
        if (it->GetRange() == rRange)
        {
            return it.get();
        }
    }

    return nullptr;
}

class ScRangeListHasher
{
public:
  size_t operator() (ScRangeList const& rRanges) const
  {
      size_t nHash = 0;
      for (size_t nIdx = 0; nIdx < rRanges.size(); ++nIdx)
          nHash += rRanges[nIdx].hashArea();
      return nHash;
  }
};

}

void CondFormatBuffer::updateImport(const ScDataBarFormatData* pTarget)
{
    for ( const auto& rRule : maCfRules )
    {
        if ( rRule && rRule->GetDataBarData() == pTarget )
            rRule->finalizeImport();
    }
}

void CondFormatBuffer::finalizeImport()
{
    deduplicateCondFormats();

    std::unordered_set<size_t> aDoneExtCFs;
    typedef std::unordered_map<ScRangeList, CondFormat*, ScRangeListHasher> RangeMap;
    RangeMap aRangeMap;
    for (auto& rxCondFormat : maCondFormats)
    {
        if (aRangeMap.contains(rxCondFormat->getRanges()))
            continue;
        aRangeMap[rxCondFormat->getRanges()] = rxCondFormat.get();
    }

    size_t nExtCFIndex = 0;
    for (const auto& rxExtCondFormat : maExtCondFormats)
    {
        ScDocument& rDoc = getScDocument();
        const ScRangeList& rRange = rxExtCondFormat->getRange();
        RangeMap::iterator it = aRangeMap.find(rRange);
        if (it != aRangeMap.end())
        {
            CondFormat& rCondFormat = *it->second;
            const std::vector<std::unique_ptr<ScFormatEntry>>& rEntries = rxExtCondFormat->getEntries();
            const std::vector<sal_Int32>& rPriorities = rxExtCondFormat->getPriorities();
            size_t nEntryIdx = 0;
            for (const auto& rxEntry : rEntries)
            {
                std::unique_ptr<CondFormatRule> xRule = rCondFormat.createRule();
                if (ScDataBarFormat *pData = dynamic_cast<ScDataBarFormat*>(rxEntry.get()))
                    updateImport(pData->GetDataBarData());
                ScFormatEntry* pNewEntry = rxEntry->Clone(rDoc);
                sal_Int32 nPriority = rPriorities[nEntryIdx];
                if (nPriority == -1)
                    nPriority = mnNonPrioritizedRuleNextPriority++;
                xRule->setFormatEntry(nPriority, pNewEntry);
                rCondFormat.insertRule(std::move(xRule));
                ++nEntryIdx;
            }

            aDoneExtCFs.insert(nExtCFIndex);
        }

        ++nExtCFIndex;
    }

    // tdf#138601 sort conditional formatting rules by their priority
    if (maCondFormats.size() > 1)
    {
        size_t minIndex;
        for (size_t i = 0; i < maCondFormats.size() - 1; ++i)
        {
            minIndex = i;
            for (size_t j = i + 1; j < maCondFormats.size(); ++j)
            {
                const CondFormat::CondFormatRuleMap& rNextRules = maCondFormats[j]->maRules;
                const CondFormat::CondFormatRuleMap& rMinRules = maCondFormats[minIndex]->maRules;
                if (rNextRules.empty() || rMinRules.empty())
                    continue;
                if (rNextRules.begin()->first < rMinRules.begin()->first)
                    minIndex = j;
            }
            if (i != minIndex)
                std::swap(maCondFormats[i], maCondFormats[minIndex]);
        }
    }

    for( const auto& rxCondFormat : maCondFormats )
        if (rxCondFormat)
            rxCondFormat->finalizeImport();

    for ( const auto& rxCfRule : maCfRules )
    {
        if ( rxCfRule )
            rxCfRule->finalizeImport();
    }

    nExtCFIndex = 0;
    for (const auto& rxExtCondFormat : maExtCondFormats)
    {
        if (aDoneExtCFs.count(nExtCFIndex))
        {
            ++nExtCFIndex;
            continue;
        }

        ScDocument& rDoc = getScDocument();
        const ScRangeList& rRange = rxExtCondFormat->getRange();
        SCTAB nTab = rRange.front().aStart.Tab();
        ScConditionalFormat* pFormat = findFormatByRange(rRange, rDoc, nTab);
        if (!pFormat)
        {
            // create new conditional format and insert it
            auto pNewFormat = std::make_unique<ScConditionalFormat>(0, rDoc);
            pFormat = pNewFormat.get();
            pNewFormat->SetRange(rRange);
            sal_uInt32 nKey = rDoc.AddCondFormat(std::move(pNewFormat), nTab);
            rDoc.AddCondFormatData(rRange, nTab, nKey);
        }

        const std::vector< std::unique_ptr<ScFormatEntry> >& rEntries = rxExtCondFormat->getEntries();
        for (const auto& rxEntry : rEntries)
        {
            pFormat->AddEntry(rxEntry->Clone(rDoc));
        }

        ++nExtCFIndex;
    }

    gnStyleIdx = 0; // Resets <extlst> <cfRule> style index.
}

/// Used for deduplicating
struct CondFormatHash
{
    size_t operator()(const CondFormatRef& x) const { return hashCode(*x); }

private:
    static size_t hashCode(const CondFormat& r)
    {
        std::size_t seed(0);
        // note that we deliberately skip the maRanges field, because, if necessary, we will merge
        // new entries into that field.
        o3tl::hash_combine(seed, r.maModel.mbPivot);
        for (const auto & rPair : r.maRules)
            o3tl::hash_combine(seed, hashCode(*rPair.second));
        return seed;
    }
    static size_t hashCode(const CondFormatRule& r)
    {
        std::size_t seed(0);
        o3tl::hash_combine(seed, hashCode(r.getRuleModel()));
        return seed;
    }
    static size_t hashCode(const CondFormatRuleModel& r)
    {
        std::size_t seed(0);
        o3tl::hash_combine(seed, r.maText);
        // we skip mnPriority, see comment in CondFormatEquals
        o3tl::hash_combine(seed, r.mnType);
        o3tl::hash_combine(seed, r.mnOperator);
        o3tl::hash_combine(seed, r.mnTimePeriod);
        o3tl::hash_combine(seed, r.mnRank);
        o3tl::hash_combine(seed, r.mnStdDev);
        // o3tl::hash_combine(seed, r.mnDxfId); if I hash this, need to hash the contents
        o3tl::hash_combine(seed, r.mbStopIfTrue);
        o3tl::hash_combine(seed, r.mbBottom);
        o3tl::hash_combine(seed, r.mbPercent);
        o3tl::hash_combine(seed, r.mbAboveAverage);
        o3tl::hash_combine(seed, r.mbEqualAverage);
        return seed;
    }
};

/// Used for deduplicating
struct CondFormatEquals
{
    const StylesBuffer& mrStyles;

    CondFormatEquals(const StylesBuffer& rStyles) : mrStyles(rStyles) {}

    bool operator()(const CondFormatRef& lhs, const CondFormatRef& rhs) const
    {
        if (lhs.get() == rhs.get())
            return true;
        // note that we deliberately skip the maRanges field, because, if necessary, we will merge
        // new entries into that field.
        if (lhs->maModel.mbPivot != rhs->maModel.mbPivot)
            return false;
        auto it1 = lhs->maRules.begin();
        auto it2 = rhs->maRules.begin();
        while (it1 != lhs->maRules.end() && it2 != rhs->maRules.end())
        {
            if (!equals(it1->second, it2->second))
                return false;
            ++it1;
            ++it2;
        }
        return it1 == lhs->maRules.end() && it2 == rhs->maRules.end();
    }
private:
    bool equals(const std::unique_ptr<CondFormatRule>& lhs, const std::unique_ptr<CondFormatRule>& rhs) const
    {
        if (lhs.get() == rhs.get())
            return true;
        if (!equals(lhs->getRuleModel(), rhs->getRuleModel()))
            return false;
        if (bool(lhs->mpColor) != bool(rhs->mpColor))
            return false;
        if (lhs->mpColor)
        {
            if (lhs->mpColor->getModelEntries() != rhs->mpColor->getModelEntries())
                return false;
            if (lhs->mpColor->getCfvo() != rhs->mpColor->getCfvo())
                return false;
            if (lhs->mpColor->getCol() != rhs->mpColor->getCol())
                return false;
        }
        // todo: I'm not bothering to properly check the following fields
        // because I ran out of enthusiasm, so we won't detect duplicates if they
        // contain data here.
        if (lhs->mpDataBar.get() != rhs->mpDataBar.get())
            return false;
        if (lhs->mpIconSet.get() != rhs->mpIconSet.get())
            return false;
        return true;
    }
    bool equals(const CondFormatRuleModel& lhs, const CondFormatRuleModel& rhs) const
    {
        if (lhs.maFormulas != rhs.maFormulas)
            return false;
        if (lhs.maText != rhs.maText)
            return false;
        // we skip mnPriority, because that is implicitly compared by the ordering of these objects
        if (lhs.mnType != rhs.mnType)
            return false;
        if (lhs.mnOperator != rhs.mnOperator)
            return false;
        if (lhs.mnTimePeriod != rhs.mnTimePeriod)
            return false;
        if (lhs.mnRank != rhs.mnRank)
            return false;
        if (lhs.mnStdDev != rhs.mnStdDev)
            return false;
        if (!equalsDxf(lhs.mnDxfId, rhs.mnDxfId))
            return false;
        if (lhs.mbStopIfTrue != rhs.mbStopIfTrue)
            return false;
        if (lhs.mbBottom != rhs.mbBottom)
            return false;
        if (lhs.mbPercent != rhs.mbPercent)
            return false;
        if (lhs.mbAboveAverage != rhs.mbAboveAverage)
            return false;
        if (lhs.mbEqualAverage != rhs.mbEqualAverage)
            return false;
        return true;
    }
    bool equalsDxf(sal_Int32 lhsDxfId, sal_Int32 rhsDxfId) const
    {
        if (lhsDxfId == rhsDxfId)
            return true;
        DxfRef pLhsDxf = mrStyles.getDxf(lhsDxfId);
        DxfRef pRhsDxf = mrStyles.getDxf(rhsDxfId);
        if (bool(pLhsDxf) != bool(pRhsDxf))
            return false;
        if (pLhsDxf && !equals(*pLhsDxf, *pRhsDxf))
            return false;
        return true;
    }
    static bool equals(const Dxf& lhs, const Dxf& rhs)
    {
        if (bool(lhs.mxFont) != bool(rhs.mxFont))
            return false;
        if (lhs.mxFont && lhs.mxFont->getModel() != rhs.mxFont->getModel())
            return false;
        if (bool(lhs.mxNumFmt) != bool(rhs.mxNumFmt))
            return false;
        if (lhs.mxNumFmt && lhs.mxNumFmt->getModel() != rhs.mxNumFmt->getModel())
            return false;
        if (bool(lhs.mxAlignment) != bool(rhs.mxAlignment))
            return false;
        if (lhs.mxAlignment && lhs.mxAlignment->getModel() != rhs.mxAlignment->getModel())
            return false;
        if (bool(lhs.mxProtection) != bool(rhs.mxProtection))
            return false;
        if (lhs.mxProtection && lhs.mxProtection->getModelData() != rhs.mxProtection->getModelData())
            return false;
        if (bool(lhs.mxBorder) != bool(rhs.mxBorder))
            return false;
        if (lhs.mxBorder && lhs.mxBorder->getModelData() != rhs.mxBorder->getModelData())
            return false;
        if (bool(lhs.mxFill) != bool(rhs.mxFill))
            return false;
        if (lhs.mxFill)
        {
            if (bool(lhs.mxFill->getPatternModel()) != bool(rhs.mxFill->getPatternModel()))
                return false;
            if (lhs.mxFill->getPatternModel() && *lhs.mxFill->getPatternModel() != *rhs.mxFill->getPatternModel())
                return false;
            if (bool(lhs.mxFill->getGradientModel()) != bool(rhs.mxFill->getGradientModel()))
                return false;
            if (lhs.mxFill->getGradientModel() && *lhs.mxFill->getGradientModel() != *rhs.mxFill->getGradientModel())
                return false;
        }
        return true;
    }
};

// Excel will sometimes produce files with 100k of duplicate conditional formatting
// entries, but only 10 unique ones, which will make LO freeze because our ScPatternAttr
// de-duplication is quite expensive. So rather do it here, where it is considerably cheaper.
void CondFormatBuffer::deduplicateCondFormats()
{
    // remove duplicates as we go
    std::unordered_set<CondFormatRef, CondFormatHash, CondFormatEquals> aDeduped{5, CondFormatHash(), CondFormatEquals{getStyles()}};
    for( auto it = maCondFormats.begin(); it != maCondFormats.end(); )
    {
        auto pair = aDeduped.insert(*it);
        // If the insert did not succeed, we have a duplicate, and we need to merge the ranges of this item
        // into the existing ranges list.
        if (!pair.second)
        {
            for (const auto & rRange : (*it)->maModel.maRanges)
                (*pair.first)->maModel.maRanges.push_back(rRange);
            it = maCondFormats.erase(it);
        }
        else
            ++it;
    }
}

CondFormatRef CondFormatBuffer::importCondFormatting( SequenceInputStream& rStrm )
{
    CondFormatRef xCondFmt = createCondFormat();
    xCondFmt->importCondFormatting( rStrm );
    return xCondFmt;
}

ExtCfDataBarRuleRef CondFormatBuffer::createExtCfDataBarRule(ScDataBarFormatData* pTarget)
{
    ExtCfDataBarRuleRef extRule = std::make_shared<ExtCfDataBarRule>( pTarget, *this );
    maCfRules.push_back( extRule );
    return extRule;
}

std::vector< std::unique_ptr<ExtCfCondFormat> >& CondFormatBuffer::importExtCondFormat()
{
    return maExtCondFormats;
}

std::vector<std::unique_ptr<ScFormatEntry> >& CondFormatBuffer::importExtFormatEntries()
{
    return maExtFormatEntries;
}

sal_Int32 CondFormatBuffer::convertToApiOperator( sal_Int32 nToken )
{
    switch( nToken )
    {
        case XML_between:               return ConditionOperator2::BETWEEN;
        case XML_equal:                 return ConditionOperator2::EQUAL;
        case XML_greaterThan:           return ConditionOperator2::GREATER;
        case XML_greaterThanOrEqual:    return ConditionOperator2::GREATER_EQUAL;
        case XML_lessThan:              return ConditionOperator2::LESS;
        case XML_lessThanOrEqual:       return ConditionOperator2::LESS_EQUAL;
        case XML_notBetween:            return ConditionOperator2::NOT_BETWEEN;
        case XML_notEqual:              return ConditionOperator2::NOT_EQUAL;
        case XML_duplicateValues:       return ConditionOperator2::DUPLICATE;
    }
    return ConditionOperator2::NONE;
}

ScConditionMode CondFormatBuffer::convertToInternalOperator( sal_Int32 nToken )
{
    switch( nToken )
    {
        case XML_between:               return ScConditionMode::Between;
        case XML_equal:                 return ScConditionMode::Equal;
        case XML_greaterThan:           return ScConditionMode::Greater;
        case XML_greaterThanOrEqual:    return ScConditionMode::EqGreater;
        case XML_lessThan:              return ScConditionMode::Less;
        case XML_lessThanOrEqual:       return ScConditionMode::EqLess;
        case XML_notBetween:            return ScConditionMode::NotBetween;
        case XML_notEqual:              return ScConditionMode::NotEqual;
        case XML_duplicateValues:       return ScConditionMode::Duplicate;
        case XML_uniqueValues:          return ScConditionMode::NotDuplicate;
    }
    return ScConditionMode::NONE;
}

// private --------------------------------------------------------------------

CondFormatRef CondFormatBuffer::createCondFormat()
{
    CondFormatRef xCondFmt = std::make_shared<CondFormat>( *this );
    maCondFormats.push_back( xCondFmt );
    return xCondFmt;
}

ExtCfDataBarRule::ExtCfDataBarRule(ScDataBarFormatData* pTarget, const WorksheetHelper& rParent):
    WorksheetHelper(rParent),
    mnRuleType( ExtCfDataBarRule::UNKNOWN ),
    mpTarget(pTarget)
{
}

void ExtCfDataBarRule::finalizeImport()
{
    switch ( mnRuleType )
    {
        case DATABAR:
        {
            ScDataBarFormatData* pDataBar = mpTarget;
            if( maModel.maAxisPosition == "none" )
                pDataBar->meAxisPosition = databar::NONE;
            else if( maModel.maAxisPosition == "middle" )
                pDataBar->meAxisPosition = databar::MIDDLE;
            else
                pDataBar->meAxisPosition = databar::AUTOMATIC;
            pDataBar->mbGradient = maModel.mbGradient;
            break;
        }
        case AXISCOLOR:
        {
            ScDataBarFormatData* pDataBar = mpTarget;
            pDataBar->maAxisColor = maModel.mnAxisColor;
            break;
        }
        case POSITIVEFILLCOLOR:
        {
            ScDataBarFormatData* pDataBar = mpTarget;
            pDataBar->maPositiveColor = maModel.mnPositiveColor;
            break;
        }
        case NEGATIVEFILLCOLOR:
        {
            ScDataBarFormatData* pDataBar = mpTarget;
            pDataBar->mxNegativeColor = maModel.mnNegativeColor;
            pDataBar->mbNeg = true;
            break;
        }
        case CFVO:
        {
            ScDataBarFormatData* pDataBar = mpTarget;
            ScColorScaleEntry* pEntry = nullptr;
            if(maModel.mbIsLower)
                pEntry = pDataBar->mpLowerLimit.get();
            else
                pEntry = pDataBar->mpUpperLimit.get();

            if(maModel.maColorScaleType == "min")
                pEntry->SetType(COLORSCALE_MIN);
            else if (maModel.maColorScaleType == "max")
                pEntry->SetType(COLORSCALE_MAX);
            else if (maModel.maColorScaleType == "autoMin")
                pEntry->SetType(COLORSCALE_AUTO);
            else if (maModel.maColorScaleType == "autoMax")
                pEntry->SetType(COLORSCALE_AUTO);
            else if (maModel.maColorScaleType == "percentile")
                pEntry->SetType(COLORSCALE_PERCENTILE);
            else if (maModel.maColorScaleType == "percent")
                pEntry->SetType(COLORSCALE_PERCENT);
            else if (maModel.maColorScaleType == "formula")
                pEntry->SetType(COLORSCALE_FORMULA);
            else if (maModel.maColorScaleType == "num")
            {
                pEntry->SetType(COLORSCALE_VALUE);
                if (!maModel.msScaleTypeValue.isEmpty())
                {
                    sal_Int32 nSize = 0;
                    rtl_math_ConversionStatus eStatus = rtl_math_ConversionStatus_Ok;
                    double fValue = rtl::math::stringToDouble(maModel.msScaleTypeValue, '.', '\0', &eStatus, &nSize);
                    if (eStatus == rtl_math_ConversionStatus_Ok && nSize == maModel.msScaleTypeValue.getLength())
                    {
                        pEntry->SetValue(fValue);
                    }
                }
            }
            break;
        }
        case UNKNOWN: // nothing to do
        default:
            break;
    }
}

void ExtCfDataBarRule::importDataBar( const AttributeList& rAttribs )
{
    mnRuleType = DATABAR;
    maModel.mbGradient = rAttribs.getBool( XML_gradient, true );
    maModel.maAxisPosition = rAttribs.getString( XML_axisPosition, u"automatic"_ustr );
}

void ExtCfDataBarRule::importPositiveFillColor( const AttributeList& rAttribs )
{
    mnRuleType = POSITIVEFILLCOLOR;
    ThemeBuffer& rThemeBuffer = getTheme();
    GraphicHelper& rGraphicHelper = getBaseFilter().getGraphicHelper();
    ::Color aColor = importOOXColor(rAttribs, rThemeBuffer, rGraphicHelper);
    maModel.mnPositiveColor = aColor;
}

void ExtCfDataBarRule::importNegativeFillColor( const AttributeList& rAttribs )
{
    mnRuleType = NEGATIVEFILLCOLOR;
    ThemeBuffer& rThemeBuffer = getTheme();
    GraphicHelper& rGraphicHelper = getBaseFilter().getGraphicHelper();
    ::Color aColor = importOOXColor(rAttribs, rThemeBuffer, rGraphicHelper);
    maModel.mnNegativeColor = aColor;
}

void ExtCfDataBarRule::importAxisColor( const AttributeList& rAttribs )
{
    mnRuleType = AXISCOLOR;
    ThemeBuffer& rThemeBuffer = getTheme();
    GraphicHelper& rGraphicHelper = getBaseFilter().getGraphicHelper();
    ::Color aColor = importOOXColor(rAttribs, rThemeBuffer, rGraphicHelper);
    maModel.mnAxisColor = aColor;
}

void ExtCfDataBarRule::importCfvo( const AttributeList& rAttribs )
{
    mnRuleType = CFVO;
    maModel.maColorScaleType = rAttribs.getString( XML_type, OUString() );
}

ExtCfCondFormat::ExtCfCondFormat(ScRangeList aRange, std::vector< std::unique_ptr<ScFormatEntry> >& rEntries,
                                 const std::vector<sal_Int32>* pPriorities):
    maRange(std::move(aRange))
{
    maEntries.swap(rEntries);
    if (pPriorities)
        maPriorities = *pPriorities;
    else
        maPriorities.resize(maEntries.size(), -1);
}

ExtCfCondFormat::~ExtCfCondFormat()
{
}

const ScRangeList& ExtCfCondFormat::getRange() const
{
    return maRange;
}

const std::vector< std::unique_ptr<ScFormatEntry> >& ExtCfCondFormat::getEntries() const
{
    return maEntries;
}

} // namespace oox

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
