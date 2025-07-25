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

#include <config_features.h>
#include <config_fuzzers.h>

#include <sal/types.h>
#include <tools/solar.h>
#include <comphelper/processfactory.hxx>
#include <comphelper/string.hxx>
#include <comphelper/simplefileaccessinteraction.hxx>
#include <com/sun/star/embed/XStorage.hpp>
#include <com/sun/star/embed/ElementModes.hpp>
#include <com/sun/star/embed/XTransactedObject.hpp>
#include <com/sun/star/task/InteractionHandler.hpp>

#include <com/sun/star/ucb/XCommandEnvironment.hpp>
#include <svl/cintitem.hxx>
#include <svl/lngmisc.hxx>
#include <svl/urihelper.hxx>
#include <svl/numformat.hxx>
#include <svl/zforlist.hxx>
#include <svl/zformat.hxx>
#include <sfx2/linkmgr.hxx>
#include <rtl/character.hxx>
#include <unotools/charclass.hxx>

#include <ucbhelper/content.hxx>
#include <ucbhelper/commandenvironment.hxx>

#include <com/sun/star/i18n/XBreakIterator.hpp>
#include <hintids.hxx>
#include <editeng/fontitem.hxx>
#include <editeng/fhgtitem.hxx>
#include <editeng/langitem.hxx>
#include <fmtfld.hxx>
#include <fmtanchr.hxx>
#include <pam.hxx>
#include <doc.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentMarkAccess.hxx>
#include <IDocumentState.hxx>
#include <flddat.hxx>
#include <docufld.hxx>
#include <usrfld.hxx>
#include <reffld.hxx>
#include <IMark.hxx>
#include <expfld.hxx>
#include <dbfld.hxx>
#include <tox.hxx>
#include <section.hxx>
#include <ndtxt.hxx>
#include <fmtinfmt.hxx>
#include <chpfld.hxx>
#include <fmtruby.hxx>
#include <charfmt.hxx>
#include <breakit.hxx>
#include <fmtclds.hxx>
#include <poolfmt.hxx>
#include <SwStyleNameMapper.hxx>
#include <names.hxx>

#include "ww8scan.hxx"
#include "ww8par.hxx"
#include "writerhelper.hxx"
#include <o3tl/safeint.hxx>
#include <o3tl/string_view.hxx>
#include <xmloff/odffields.hxx>
#include <osl/diagnose.h>
#include <officecfg/Office/Common.hxx>

#include <algorithm>
#include <string_view>

#define MAX_FIELDLEN 64000

#define WW8_TOX_LEVEL_DELIM     ':'

using namespace ::com::sun::star;
using namespace msfilter::util;
using namespace sw::util;
using namespace sw::mark;

// Bookmarks
namespace
{
    // #120879# - helper method to identify a bookmark name to match the internal TOC bookmark naming convention
    bool IsTOCBookmarkName(const SwMarkName& rName)
    {
        return o3tl::starts_with(rName.toString(), u"_Toc") || o3tl::starts_with(rName.toString(), Concat2View(IDocumentMarkAccess::GetCrossRefHeadingBookmarkNamePrefix()+"_Toc"));
    }

    SwMarkName EnsureTOCBookmarkName(const SwMarkName& rName)
    {
        SwMarkName sTmp = rName;
        if ( IsTOCBookmarkName ( rName ) )
        {
            if ( ! rName.toString().startsWith(IDocumentMarkAccess::GetCrossRefHeadingBookmarkNamePrefix()) )
                sTmp = SwMarkName(IDocumentMarkAccess::GetCrossRefHeadingBookmarkNamePrefix() + rName.toString());
        }
        return sTmp;
    }
}

tools::Long SwWW8ImplReader::Read_Book(WW8PLCFManResult*)
{
    // should also work via pRes.nCo2OrIdx
    WW8PLCFx_Book* pB = m_xPlcxMan->GetBook();
    if( !pB )
    {
        OSL_ENSURE( pB, "WW8PLCFx_Book - Pointer does not exist" );
        return 0;
    }

    eBookStatus eB = pB->GetStatus();
    if (eB & BOOK_IGNORE)
        return 0;         // ignore bookmark

    if (pB->GetIsEnd())
    {
        m_xReffedStck->SetAttr(*m_pPaM->GetPoint(), RES_FLTR_BOOKMARK, true,
            pB->GetHandle(), (eB & BOOK_FIELD)!=0);
        return 0;
    }

    // "_Hlt*" are unnecessary
    const OUString* pName = pB->GetName();
    // Now, as we read the TOC field completely, we also need the hyperlinks inside keep available.
    // So the hidden bookmarks inside for hyperlink jumping also should be kept.
    if ( !pName ||
         pName->startsWithIgnoreAsciiCase( "_Hlt" ) )
    {
        return 0;
    }

    // do NOT call ToUpper as the bookmark name can also be a hyperlink target!

    OUString aVal;
    if( SwFltGetFlag( m_nFieldFlags, SwFltControlStack::BOOK_TO_VAR_REF ) )
    {
        // set variable for translation bookmark
        tools::Long nLen = pB->GetLen();
        if( nLen > MAX_FIELDLEN )
            nLen = MAX_FIELDLEN;

        sal_uInt64 nOldPos = m_pStrm->Tell();
        m_xSBase->WW8ReadString( *m_pStrm, aVal, pB->GetStartPos(), nLen,
                                        m_eStructCharSet );
        m_pStrm->Seek( nOldPos );

        // now here the implementation of the old "QuoteString" and
        // I hope with a better performance as before. It's also only
        // needed if the filterflags say we will convert bookmarks
        // to SetExpFields! And this the exception!

        bool bSetAsHex;
        bool bAllowCr = SwFltGetFlag(m_nFieldFlags,
            SwFltControlStack::ALLOW_FLD_CR);

        for( sal_Int32 nI = 0;
             nI < aVal.getLength() && aVal.getLength() < (MAX_FIELDLEN - 4);
             ++nI )
        {
            const sal_Unicode cChar = aVal[nI];
            switch( cChar )
            {
            case 0x0b:
            case 0x0c:
            case 0x0d:
                if( bAllowCr )
                {
                    aVal = aVal.replaceAt( nI, 1, u"\n" );
                    bSetAsHex = false;
                }
                else
                    bSetAsHex = true;
                break;

            case 0xFE:
            case 0xFF:
                bSetAsHex = true;
                break;

            default:
                bSetAsHex = 0x20 > cChar;
                break;
            }

            if( bSetAsHex )
            {
                //all Hex-Numbers with \x before
                OUString sTmp( u"\\x"_ustr );
                if( cChar < 0x10 )
                    sTmp += "0";
                sTmp += OUString::number( cChar, 16 );
                aVal = aVal.replaceAt( nI, 1 , sTmp );
                nI += sTmp.getLength() - 1;
            }
        }

        if ( aVal.getLength() > (MAX_FIELDLEN - 4))
            aVal = aVal.copy( 0, MAX_FIELDLEN - 4 );
    }

    //e.g. inserting bookmark around field result, so we need to put
    //it around the entire writer field, as we don't have the separation
    //of field and field result of word, see #i16941#
    SwPosition aStart(*m_pPaM->GetPoint());
    if (!m_aFieldStack.empty())
    {
        const WW8FieldEntry &rTest = m_aFieldStack.back();
        aStart = rTest.maStartPos;
    }

    const SwMarkName sOrigName( BookmarkToWriter(*pName) );
    m_xReffedStck->NewAttr( aStart,
                          SwFltBookmark( EnsureTOCBookmarkName( sOrigName ).toString(), aVal, pB->GetHandle(), IsTOCBookmarkName( sOrigName ) ));
    return 0;
}

tools::Long SwWW8ImplReader::Read_AtnBook(WW8PLCFManResult*)
{
    if (WW8PLCFx_AtnBook* pAtnBook = m_xPlcxMan->GetAtnBook())
    {
        if (pAtnBook->getIsEnd())
            m_xReffedStck->SetAttr(*m_pPaM->GetPoint(), RES_FLTR_ANNOTATIONMARK, true, pAtnBook->getHandle());
        else
            m_xReffedStck->NewAttr(*m_pPaM->GetPoint(), CntUInt16Item(RES_FLTR_ANNOTATIONMARK, pAtnBook->getHandle()));
    }
    return 0;
}

tools::Long SwWW8ImplReader::Read_FactoidBook(WW8PLCFManResult*)
{
    if (WW8PLCFx_FactoidBook* pFactoidBook = m_xPlcxMan->GetFactoidBook())
    {
        if (pFactoidBook->getIsEnd())
            m_xReffedStck->SetAttr(*m_pPaM->GetPoint(), RES_FLTR_RDFMARK, true, pFactoidBook->getHandle());
        else
        {
            SwFltRDFMark aMark;
            aMark.SetHandle(pFactoidBook->getHandle());
            GetSmartTagInfo(aMark);
            m_xReffedStck->NewAttr(*m_pPaM->GetPoint(), aMark);
        }
    }
    return 0;
}

//    general help methods to separate parameters

/// translate FieldParameter names into the system character set and
/// at the same time, double backslashes are converted into single ones
OUString SwWW8ImplReader::ConvertFFileName(const OUString& rOrg)
{
    OUString aName = rOrg.replaceAll("\\\\", "\\");
    aName = aName.replaceAll("%20", " ");

    // remove attached quotation marks
    if (aName.endsWith("\""))
        aName = aName.copy(0, aName.getLength()-1);

    // Need the more sophisticated url converter.
    if (!aName.isEmpty())
        aName = URIHelper::SmartRel2Abs(
            INetURLObject(m_sBaseURL), aName, Link<OUString *, bool>(), false);

    return aName;
}

namespace
{
    /// translate FieldParameter names into the
    /// system character set and makes them uppercase
    void ConvertUFName( OUString& rName )
    {
        rName = GetAppCharClass().uppercase( rName );
    }
}

static void lcl_ConvertSequenceName(OUString& rSequenceName)
{
    ConvertUFName(rSequenceName);
    if ('0' <= rSequenceName[0] && '9' >= rSequenceName[0])
        rSequenceName = "_" + rSequenceName;
}

// FindParaStart() finds 1st Parameter that follows '\' and cToken
// and returns start of this parameter or -1
static sal_Int32 FindParaStart( std::u16string_view aStr, sal_Unicode cToken, sal_Unicode cToken2 )
{
    bool bStr = false; // ignore inside a string

    for( size_t nBuf = 0; nBuf+1 < aStr.size(); nBuf++ )
    {
        if( aStr[ nBuf ] == '"' )
            bStr = !bStr;

        if(    !bStr
            && aStr[ nBuf ] == '\\'
            && (    aStr[ nBuf + 1 ] == cToken
                 || aStr[ nBuf + 1 ] == cToken2 ) )
        {
            nBuf += 2;
            // skip spaces between cToken and its parameters
            while(    nBuf < aStr.size()
                   && aStr[ nBuf ] == ' ' )
                nBuf++;
            // return start of parameters
            return nBuf < aStr.size() ? nBuf : -1;
        }
    }
    return -1;
}

// FindPara() finds the first parameter including '\' and cToken.
// A new String will be allocated (has to be deallocated by the caller)
// and everything that is part of the parameter will be returned.
static OUString FindPara( std::u16string_view aStr, sal_Unicode cToken, sal_Unicode cToken2 )
{
    sal_Int32 n2;                                          // end
    sal_Int32 n = FindParaStart( aStr, cToken, cToken2 );  // start
    if( n == -1)
        return OUString();

    if(    aStr[ n ] == '"'
        || aStr[ n ] == 132 )
    {                               // Quotationmark in front of parameter
        n++;                        // Skip quotationmark
        n2 = n;                     // search for the end starting from here
        while(     n2 < sal_Int32(aStr.size())
                && aStr[ n2 ] != 147
                && aStr[ n2 ] != '"' )
            n2++;                   // search end of parameter
    }
    else
    {                           // no quotationmarks
        n2 = n;                     // search for the end starting from here
        while(     n2 < sal_Int32(aStr.size())
                && aStr[ n2 ] != ' ' )
            n2++;                   // search end of parameter
    }
    return OUString(aStr.substr( n, n2-n ));
}

static SvxNumType GetNumTypeFromName(const OUString& rStr,
    bool bAllowPageDesc = false)
{
    SvxNumType eTyp = bAllowPageDesc ? SVX_NUM_PAGEDESC : SVX_NUM_ARABIC;
    if (rStr.isEmpty())
        return eTyp;

    if( rStr.startsWithIgnoreAsciiCase( "Arabi" ) )  // Arabisch, Arabic
        eTyp = SVX_NUM_ARABIC;
    else if( rStr.startsWith( "misch" ) )    // r"omisch
        eTyp = SVX_NUM_ROMAN_LOWER;
    else if( rStr.startsWith( "MISCH" ) )    // R"OMISCH
        eTyp = SVX_NUM_ROMAN_UPPER;
    else if( rStr.startsWithIgnoreAsciiCase( "alphabeti" ) )// alphabetisch, alphabetic
        eTyp =  ( rStr[0] == 'A' )
                ? SVX_NUM_CHARS_UPPER_LETTER_N
                : SVX_NUM_CHARS_LOWER_LETTER_N;
    else if( rStr.startsWithIgnoreAsciiCase( "roman" ) )  // us
        eTyp =  ( rStr[0] == 'R' )
                ? SVX_NUM_ROMAN_UPPER
                : SVX_NUM_ROMAN_LOWER;
    return eTyp;
}

static SvxNumType GetNumberPara(std::u16string_view aStr, bool bAllowPageDesc = false)
{
    OUString s( FindPara( aStr, '*', '*' ) );     // Type of number
    SvxNumType aType = GetNumTypeFromName( s, bAllowPageDesc );
    return aType;
}

bool SwWW8ImplReader::ForceFieldLanguage(SwField &rField, LanguageType nLang)
{
    bool bRet(false);

    const SvxLanguageItem *pLang = GetFormatAttr(RES_CHRATR_LANGUAGE);
    OSL_ENSURE(pLang, "impossible");
    LanguageType nDefault =  pLang ? pLang->GetValue() : LANGUAGE_ENGLISH_US;

    if (nLang != nDefault)
    {
        rField.SetAutomaticLanguage(false);
        rField.SetLanguage(nLang);
        bRet = true;
    }

    return bRet;
}

static OUString GetWordDefaultDateStringAsUS(SvNumberFormatter* pFormatter, LanguageType nLang)
{
    //Get the system date in the correct final language layout, convert to
    //a known language and modify the 2 digit year part to be 4 digit, and
    //convert back to the correct language layout.
    const sal_uInt32 nIndex = pFormatter->GetFormatIndex(NF_DATE_SYSTEM_SHORT, nLang);

    SvNumberformat aFormat = *(pFormatter->GetEntry(nIndex));
    aFormat.ConvertLanguage(*pFormatter, nLang, LANGUAGE_ENGLISH_US);

    OUString sParams(aFormat.GetFormatstring());
    // #i36594#
    // Fix provided by mloiseleur@openoffice.org.
    // A default date can have already 4 year digits, in some case
    const sal_Int32 pos = sParams.indexOf("YYYY");
    if ( pos == -1 )
    {
        sParams = sParams.replaceFirst("YY", "YYYY");
    }
    return sParams;
}

SvNumFormatType SwWW8ImplReader::GetTimeDatePara(std::u16string_view aStr, sal_uInt32& rFormat,
    LanguageType &rLang, int nWhichDefault, bool bHijri)
{
    bool bRTL = false;
    if (m_xPlcxMan && !m_bVer67)
    {
        SprmResult aResult = m_xPlcxMan->HasCharSprm(0x85A);
        if (aResult.pSprm && aResult.nRemainingData >= 1 && *aResult.pSprm)
            bRTL = true;
    }
    TypedWhichId<SvxLanguageItem> eLang = bRTL ? RES_CHRATR_CTL_LANGUAGE : RES_CHRATR_LANGUAGE;
    const SvxLanguageItem *pLang = GetFormatAttr(eLang);
    OSL_ENSURE(pLang, "impossible");
    rLang = pLang ? pLang->GetValue() : LANGUAGE_ENGLISH_US;

    SvNumberFormatter* pFormatter = m_rDoc.GetNumberFormatter();
    OUString sParams( FindPara( aStr, '@', '@' ) );// Date/Time
    if (sParams.isEmpty())
    {
        bool bHasTime = false;
        switch (nWhichDefault)
        {
            case ww::ePRINTDATE:
            case ww::eSAVEDATE:
                sParams = GetWordDefaultDateStringAsUS(pFormatter, rLang);
                sParams += " HH:MM:SS AM/PM";
                bHasTime = true;
                break;
            case ww::eCREATEDATE:
                sParams += "DD/MM/YYYY HH:MM:SS";
                bHasTime = true;
                break;
            default:
            case ww::eDATE:
                sParams = GetWordDefaultDateStringAsUS(pFormatter, rLang);
                break;
        }

        if (bHijri)
            sParams = "[~hijri]" + sParams;

        sal_Int32 nCheckPos = 0;
        SvNumFormatType nType = SvNumFormatType::DEFINED;
        rFormat = 0;

        pFormatter->PutandConvertEntry(sParams, nCheckPos, nType, rFormat,
                                       LANGUAGE_ENGLISH_US, rLang, false);

        return bHasTime ? SvNumFormatType::DATETIME : SvNumFormatType::DATE;
    }

    sal_uLong nFormatIdx =
        sw::ms::MSDateTimeFormatToSwFormat(sParams, pFormatter, rLang, bHijri,
                GetFib().m_lid);
    SvNumFormatType nNumFormatType = SvNumFormatType::UNDEFINED;
    if (nFormatIdx)
        nNumFormatType = pFormatter->GetType(nFormatIdx);
    rFormat = nFormatIdx;

    return nNumFormatType;
}

// Fields

// Update respective fields after loading (currently references)
void SwWW8ImplReader::UpdateFields()
{
    m_rDoc.getIDocumentState().SetUpdateExpFieldStat(true);
    m_rDoc.SetInitDBFields(true);             // Also update fields in the database
}

// Sanity check the PaM to see if it makes sense wrt sw::CalcBreaks
static bool SanityCheck(const SwPaM& rFieldPam)
{
    SwNodeOffset const nEndNode(rFieldPam.End()->GetNodeIndex());
    SwNodes const& rNodes(rFieldPam.GetPoint()->GetNodes());
    SwNode *const pFinalNode(rNodes[nEndNode]);
    if (pFinalNode->IsTextNode())
    {
        SwTextNode & rTextNode(*pFinalNode->GetTextNode());
        return (rTextNode.Len() >= rFieldPam.End()->GetContentIndex());
    }
    return true;
}

sal_uInt16 SwWW8ImplReader::End_Field()
{
    sal_uInt16 nRet = 0;
    WW8PLCFx_FLD* pF = m_xPlcxMan->GetField();
    OSL_ENSURE(pF, "WW8PLCFx_FLD - Pointer not available");
    WW8_CP nCP = 0;
    if (!pF || !pF->EndPosIsFieldEnd(nCP))
        return nRet;

    bool bUseEnhFields = officecfg::Office::Common::Filter::Microsoft::Import::ImportWWFieldsAsEnhancedFields::get();

    OSL_ENSURE(!m_aFieldStack.empty(), "Empty field stack");
    if (!m_aFieldStack.empty())
    {
        /*
        only hyperlinks currently need to be handled like this, for the other
        cases we have inserted a field not an attribute with an unknown end
        point
        */
        nRet = m_aFieldStack.back().mnFieldId;
        switch (nRet)
        {
        case ww::eFORMTEXT:
        if (bUseEnhFields && m_pPaM!=nullptr && m_pPaM->GetPoint()!=nullptr) {
            SwPosition aEndPos = *m_pPaM->GetPoint();
            SwPaM aFieldPam( m_aFieldStack.back().GetPtNode().GetNode(), m_aFieldStack.back().GetPtContent(), aEndPos.GetNode(), aEndPos.GetContentIndex());

            IDocumentMarkAccess* pMarksAccess = m_rDoc.getIDocumentMarkAccess( );
            Fieldmark *pFieldmark = SanityCheck(aFieldPam) ? pMarksAccess->makeFieldBookmark(
                        aFieldPam, m_aFieldStack.back().GetBookmarkName(), ODF_FORMTEXT,
                        aFieldPam.Start() /*same pos as start!*/ ) : nullptr;
            OSL_ENSURE(pFieldmark!=nullptr, "hmmm; why was the bookmark not created?");
            if (pFieldmark!=nullptr) {
                // adapt redline positions to inserted field mark start
                // dummy char (assume not necessary for end dummy char)
                m_xRedlineStack->MoveAttrsFieldmarkInserted(*aFieldPam.Start());
                const Fieldmark::parameter_map_t& rParametersToAdd = m_aFieldStack.back().getParameters();
                pFieldmark->GetParameters()->insert(rParametersToAdd.begin(), rParametersToAdd.end());
            }
        }
        break;
            // Doing corresponding status management for TOX field, index field, hyperlink field and page reference field
            case ww::eTOC://TOX
            case ww::eINDEX://index
                if (m_bLoadingTOXCache)
                {
                    if (m_nEmbeddedTOXLevel > 0)
                    {
                        JoinNode(*m_pPaM);
                        --m_nEmbeddedTOXLevel;
                    }
                    else
                    {
                        m_aTOXEndCps.insert(nCP);
                        m_bLoadingTOXCache = false;
                        if ( m_pPaM->End() &&
                             m_pPaM->End()->GetNode().GetTextNode() &&
                             m_pPaM->End()->GetNode().GetTextNode()->Len() == 0 )
                        {
                            JoinNode(*m_pPaM);
                        }
                        else
                        {
                            m_bCareLastParaEndInToc = true;
                        }

                        if (m_oPosAfterTOC)
                        {
                            *m_pPaM = *m_oPosAfterTOC;
                            m_oPosAfterTOC.reset();
                        }
                    }
                }
                break;
            case ww::ePAGEREF: //REF
                if (m_bLoadingTOXCache && !m_bLoadingTOXHyperlink)
                {
                    m_xCtrlStck->SetAttr(*m_pPaM->GetPoint(),RES_TXTATR_INETFMT);
                }
                break;
            case ww::eHYPERLINK:
                if (m_bLoadingTOXHyperlink)
                    m_bLoadingTOXHyperlink = false;
                m_xCtrlStck->SetAttr(*m_pPaM->GetPoint(), RES_TXTATR_INETFMT);
                break;
            case ww::eMERGEINC:
            case ww::eINCLUDETEXT:
            {
                //Move outside the section associated with this type of field
                SwPosition aRestorePos(m_aFieldStack.back().maStartPos);

                SwContentNode* pNd = aRestorePos.GetNode().GetContentNode();
                sal_Int32 nMaxValidIndex = pNd ? pNd->Len() : 0;
                if (aRestorePos.GetContentIndex() > nMaxValidIndex)
                {
                    SAL_WARN("sw.ww8", "Attempt to restore to invalid content position");
                    aRestorePos.SetContent(nMaxValidIndex);
                }

                *m_pPaM->GetPoint() = std::move(aRestorePos);
                break;
            }
            case ww::eIF: // IF-field
            {
                // conditional field parameters
                OUString fieldDefinition = m_aFieldStack.back().GetBookmarkCode();

                OUString paramCondition;
                OUString paramTrue;
                OUString paramFalse;

                // ParseIfFieldDefinition expects: IF <some condition> "true result" "false result"
                // while many fields include '\* MERGEFORMAT' after that.
                // So first trim off the switches that are not supported anyway
                sal_Int32 nLastIndex = fieldDefinition.lastIndexOf("\\*");
                sal_Int32 nOtherIndex = fieldDefinition.lastIndexOf("\\#"); //number format
                if (nOtherIndex > 0 && (nOtherIndex < nLastIndex || nLastIndex < 0))
                    nLastIndex = nOtherIndex;
                nOtherIndex = fieldDefinition.lastIndexOf("\\@"); //date format
                if (nOtherIndex > 0 && (nOtherIndex < nLastIndex || nLastIndex < 0))
                    nLastIndex = nOtherIndex;
                nOtherIndex = fieldDefinition.lastIndexOf("\\!"); //locked result
                if (nOtherIndex > 0 && (nOtherIndex < nLastIndex || nLastIndex < 0))
                    nLastIndex = nOtherIndex;
                if (nLastIndex > 0)
                    fieldDefinition = fieldDefinition.copy(0, nLastIndex);

                SwHiddenTextField::ParseIfFieldDefinition(fieldDefinition, paramCondition, paramTrue, paramFalse);

                // create new field
                SwFieldType* pFieldType = m_rDoc.getIDocumentFieldsAccess().GetSysFieldType(SwFieldIds::HiddenText);
                SwHiddenTextField aHTField(
                    static_cast<SwHiddenTextFieldType*>(pFieldType),
                    paramCondition,
                    paramTrue,
                    paramFalse,
                    SwFieldTypesEnum::ConditionalText);

                // insert new field into document
                m_rDoc.getIDocumentContentOperations().InsertPoolItem(*m_pPaM, SwFormatField(aHTField));
                break;
            }
            default:
                OUString aCode = m_aFieldStack.back().GetBookmarkCode();
                if (!aCode.isEmpty() && !o3tl::starts_with(o3tl::trim(aCode), u"SHAPE"))
                {
                    // Unhandled field with stored code
                    SwPosition aEndPos = *m_pPaM->GetPoint();
                    SwPaM aFieldPam(
                            m_aFieldStack.back().GetPtNode().GetNode(), m_aFieldStack.back().GetPtContent(),
                            aEndPos.GetNode(), aEndPos.GetContentIndex());

                    IDocumentMarkAccess* pMarksAccess = m_rDoc.getIDocumentMarkAccess( );

                    Fieldmark* pFieldmark = pMarksAccess->makeFieldBookmark(
                                aFieldPam,
                                m_aFieldStack.back().GetBookmarkName(),
                                ODF_UNHANDLED,
                                aFieldPam.Start() /*same pos as start!*/ );
                    if ( pFieldmark )
                    {
                        // adapt redline positions to inserted field mark start
                        // dummy char (assume not necessary for end dummy char)
                        m_xRedlineStack->MoveAttrsFieldmarkInserted(*aFieldPam.Start());
                        const Fieldmark::parameter_map_t& rParametersToAdd = m_aFieldStack.back().getParameters();
                        pFieldmark->GetParameters()->insert(rParametersToAdd.begin(), rParametersToAdd.end());
                        OUString sFieldId = OUString::number( m_aFieldStack.back().mnFieldId );
                        pFieldmark->GetParameters()->insert(
                                std::pair< OUString, uno::Any > (
                                    ODF_ID_PARAM,
                                    uno::Any( sFieldId ) ) );
                        pFieldmark->GetParameters()->insert(
                                std::pair< OUString, uno::Any > (
                                    ODF_CODE_PARAM,
                                    uno::Any( aCode ) ) );

                        if ( m_aFieldStack.back().mnObjLocFc > 0 )
                        {
                            // Store the OLE object as an internal link
                            OUString sOleId = "_" +
                                OUString::number( m_aFieldStack.back().mnObjLocFc );

                            rtl::Reference<SotStorage> xSrc0 = m_pStg->OpenSotStorage(SL::aObjectPool);
                            rtl::Reference<SotStorage> xSrc1 = xSrc0->OpenSotStorage( sOleId, StreamMode::READ );

                            // Store it now!
                            uno::Reference< embed::XStorage > xDocStg = GetDoc().GetDocStorage();
                            if (xDocStg.is())
                            {
                                uno::Reference< embed::XStorage > xOleStg = xDocStg->openStorageElement(
                                        u"OLELinks"_ustr, embed::ElementModes::WRITE );
                                rtl::Reference<SotStorage> xObjDst = SotStorage::OpenOLEStorage( xOleStg, sOleId );

                                if ( xObjDst.is() )
                                {
                                    xSrc1->CopyTo( xObjDst.get() );

                                    if ( !xObjDst->GetError() )
                                        xObjDst->Commit();
                                }

                                uno::Reference< embed::XTransactedObject > xTransact( xOleStg, uno::UNO_QUERY );
                                if ( xTransact.is() )
                                    xTransact->commit();
                            }

                            // Store the OLE Id as a parameter
                            pFieldmark->GetParameters()->insert(
                                    std::pair< OUString, uno::Any >(
                                        ODF_OLE_PARAM, uno::Any( sOleId ) ) );
                        }
                    }
                }

                break;
        }
        m_aFieldStack.pop_back();
    }
    return nRet;
}

static bool AcceptableNestedField(sal_uInt16 nFieldCode)
{
    switch (nFieldCode)
    {
        case ww::eINDEX:  // allow recursive field in TOC...
        case ww::eTOC: // allow recursive field in TOC...
        case ww::eMERGEINC:
        case ww::eINCLUDETEXT:
        case ww::eAUTOTEXT:
        case ww::eHYPERLINK:
        // Accept AutoTextList field as nested field.
        // Thus, the field result is imported as plain text.
        case ww::eAUTOTEXTLIST:
        // tdf#129247 CONTROL contains a nested SHAPE field in the result
        case ww::eCONTROL:
            return true;
        default:
            return false;
    }
}

WW8FieldEntry::WW8FieldEntry(SwPosition const &rPos, sal_uInt16 nFieldId) noexcept
    : maStartPos(rPos), mnFieldId(nFieldId), mnObjLocFc(0)
{
}

WW8FieldEntry::WW8FieldEntry(const WW8FieldEntry &rOther) noexcept
    : maStartPos(rOther.maStartPos), mnFieldId(rOther.mnFieldId), mnObjLocFc(rOther.mnObjLocFc)
{
}

void WW8FieldEntry::Swap(WW8FieldEntry &rOther) noexcept
{
    std::swap(maStartPos, rOther.maStartPos);
    std::swap(mnFieldId, rOther.mnFieldId);
}

WW8FieldEntry &WW8FieldEntry::operator=(const WW8FieldEntry &rOther) noexcept
{
    WW8FieldEntry aTemp(rOther);
    Swap(aTemp);
    return *this;
}


void WW8FieldEntry::SetBookmarkName(const SwMarkName& bookmarkName)
{
    msBookmarkName=bookmarkName;
}

void WW8FieldEntry::SetBookmarkType(const OUString& bookmarkType)
{
    msMarkType=bookmarkType;
}

void WW8FieldEntry::SetBookmarkCode(const OUString& bookmarkCode)
{
    msMarkCode = bookmarkCode;
}


// Read_Field reads a field or returns 0 if the field cannot be read,
// so that the calling function reads the field in text format.
// Returnvalue: Total length of field
tools::Long SwWW8ImplReader::Read_Field(WW8PLCFManResult* pRes)
{
    typedef eF_ResT (SwWW8ImplReader::*FNReadField)( WW8FieldDesc*, OUString& );
    constexpr sal_uInt16 eMax = 96;
    static const FNReadField aWW8FieldTab[eMax+1] =
    {
        nullptr,
        &SwWW8ImplReader::Read_F_Input,
        nullptr,
        &SwWW8ImplReader::Read_F_Ref,               // 3
        nullptr,
        nullptr,
        &SwWW8ImplReader::Read_F_Set,               // 6
        nullptr,
        &SwWW8ImplReader::Read_F_Tox,               // 8
        nullptr,
        &SwWW8ImplReader::Read_F_Styleref,          // 10
        nullptr,
        &SwWW8ImplReader::Read_F_Seq,               // 12
        &SwWW8ImplReader::Read_F_Tox,               // 13
        &SwWW8ImplReader::Read_F_DocInfo,           // 14
        &SwWW8ImplReader::Read_F_DocInfo,           // 15
        &SwWW8ImplReader::Read_F_DocInfo,           // 16
        &SwWW8ImplReader::Read_F_Author,            // 17
        &SwWW8ImplReader::Read_F_DocInfo,           // 18
        &SwWW8ImplReader::Read_F_DocInfo,           // 19
        &SwWW8ImplReader::Read_F_DocInfo,           // 20
        &SwWW8ImplReader::Read_F_DocInfo,           // 21
        &SwWW8ImplReader::Read_F_DocInfo,           // 22
        &SwWW8ImplReader::Read_F_DocInfo,           // 23
        &SwWW8ImplReader::Read_F_DocInfo,           // 24
        &SwWW8ImplReader::Read_F_DocInfo,           // 25
        &SwWW8ImplReader::Read_F_Num,               // 26
        &SwWW8ImplReader::Read_F_Num,               // 27
        &SwWW8ImplReader::Read_F_Num,               // 28
        &SwWW8ImplReader::Read_F_FileName,          // 29
        &SwWW8ImplReader::Read_F_TemplName,         // 30
        &SwWW8ImplReader::Read_F_DateTime,          // 31
        &SwWW8ImplReader::Read_F_DateTime,          // 32
        &SwWW8ImplReader::Read_F_CurPage,           // 33
        nullptr,
        nullptr,
        &SwWW8ImplReader::Read_F_IncludeText,       // 36
        &SwWW8ImplReader::Read_F_PgRef,             // 37
        &SwWW8ImplReader::Read_F_InputVar,          // 38
        &SwWW8ImplReader::Read_F_Input,             // 39
        nullptr,
        &SwWW8ImplReader::Read_F_DBNext,            // 41
        nullptr,
        nullptr,
        &SwWW8ImplReader::Read_F_DBNum,             // 44
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &SwWW8ImplReader::Read_F_Equation,          // 49
        nullptr,
        &SwWW8ImplReader::Read_F_Macro,             // 51
        &SwWW8ImplReader::Read_F_ANumber,           // 52
        &SwWW8ImplReader::Read_F_ANumber,           // 53
        &SwWW8ImplReader::Read_F_ANumber,           // 54
        nullptr,

        nullptr,                                          // 56

        &SwWW8ImplReader::Read_F_Symbol,            // 57
        &SwWW8ImplReader::Read_F_Embedd,            // 58
        &SwWW8ImplReader::Read_F_DBField,           // 59
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &SwWW8ImplReader::Read_F_DocInfo,           // 64 - DOCVARIABLE
        nullptr,
        nullptr,
        &SwWW8ImplReader::Read_F_IncludePicture,    // 67
        &SwWW8ImplReader::Read_F_IncludeText,       // 68
        nullptr,
        &SwWW8ImplReader::Read_F_FormTextBox,       // 70
        &SwWW8ImplReader::Read_F_FormCheckBox,      // 71
        &SwWW8ImplReader::Read_F_NoteReference,     // 72
        nullptr, /*&SwWW8ImplReader::Read_F_Tox*/
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &SwWW8ImplReader::Read_F_FormListBox,       // 83
        nullptr,                                          // 84
        &SwWW8ImplReader::Read_F_DocInfo,           // 85
        nullptr,                                          // 86
        &SwWW8ImplReader::Read_F_OCX,               // 87
        &SwWW8ImplReader::Read_F_Hyperlink,         // 88
        nullptr,                                          // 89
        nullptr,                                          // 90
        &SwWW8ImplReader::Read_F_HTMLControl,       // 91
        nullptr,                                          // 92
        nullptr,                                          // 93
        nullptr,                                          // 94
        &SwWW8ImplReader::Read_F_Shape,             // 95
        nullptr                                           // eMax - Dummy empty method
    };
    OSL_ENSURE( SAL_N_ELEMENTS( aWW8FieldTab ) == eMax+1, "FieldFunc table not right" );

    WW8PLCFx_FLD* pF = m_xPlcxMan->GetField();
    OSL_ENSURE(pF, "WW8PLCFx_FLD - Pointer not available");

    if (!pF || !pF->StartPosIsFieldStart())
        return 0;

    bool bNested = false;
    if (!m_aFieldStack.empty())
    {
        bNested = std::any_of(m_aFieldStack.cbegin(), m_aFieldStack.cend(),
            [](const WW8FieldEntry& aField) { return !AcceptableNestedField(aField.mnFieldId); });
    }

    WW8FieldDesc aF;
    bool bOk = pF->GetPara(pRes->nCp2OrIdx, aF);

    OSL_ENSURE(bOk, "WW8: Bad Field!");
    if (aF.nId == 33) aF.bCodeNest=false; // do not recurse into nested page fields
    bool bCodeNest = aF.bCodeNest;
    if ( aF.nId == 6 ) bCodeNest = false; // We can handle them and lose the inner data
    if (aF.nId == 70) bCodeNest = false; // need to import 0x01 in FORMTEXT

    m_aFieldStack.emplace_back(*m_pPaM->GetPoint(), aF.nId);

    if (bNested)
        return 0;

    sal_uInt16 n = (aF.nId <= eMax) ? aF.nId : eMax;
    sal_uInt32 nI = n / 32U;                     // # of sal_uInt32

    static_assert(FieldTagSize == SAL_N_ELEMENTS(m_nFieldTagAlways) &&
                  FieldTagSize == SAL_N_ELEMENTS(m_nFieldTagBad),
                  "m_nFieldTagAlways and m_nFieldTagBad should have the same FieldTagSize num of elements");

    if (nI >= FieldTagSize)
    {   // if indexes larger than 95 are needed, then a new configuration
        // item has to be added, and nFieldTagAlways/nFieldTagBad expanded!
        return aF.nLen;
    }

    sal_uInt32 nMask = 1 << ( n % 32 );          // Mask for bits

    if( m_nFieldTagAlways[nI] & nMask )       // Flag: Tag it
        return Read_F_Tag( &aF );           // Result not as text

    if( !bOk || !aF.nId )                   // Field corrupted
        return aF.nLen;                     // -> ignore

    if( aF.nId > eMax - 1)                        // WW: Nested Field
    {
        if( m_nFieldTagBad[nI] & nMask )      // Flag: Tag it when bad
            return Read_F_Tag( &aF );       // Result not as text
        else
            return aF.nLen;
    }

    //Only one type of field (hyperlink) in drawing textboxes exists
    if (aF.nId != 88 && m_xPlcxMan->GetDoingDrawTextBox())
        return aF.nLen;

    bool bHasHandler = aWW8FieldTab[aF.nId] != nullptr;
    if (aF.nId == 10) // STYLEREF
    {
        bool bHandledByChapter = false;
        sal_uInt64 nOldPos = m_pStrm->Tell();
        OUString aStr;
        aF.nLCode = m_xSBase->WW8ReadString(*m_pStrm, aStr, m_xPlcxMan->GetCpOfs() + aF.nSCode, aF.nLCode, m_eTextCharSet);
        m_pStrm->Seek(nOldPos);

        WW8ReadFieldParams aReadParam(aStr);
        sal_Int32 nRet = aReadParam.SkipToNextToken();
        if (nRet == -2 && !aReadParam.GetResult().isEmpty())
            // Single numeric argument: this can be handled by SwChapterField.
            bHandledByChapter = rtl::isAsciiDigit(aReadParam.GetResult()[0]);

        if (bHandledByChapter)
        {
            nRet = aReadParam.SkipToNextToken();
            // Handle using SwChapterField only in case there is no \[a-z]
            // switch after the field argument.
            bHasHandler = nRet < 0 || nRet == '*';
        }
    }

    // no routine available
    if (!bHasHandler || bCodeNest)
    {
        if( m_nFieldTagBad[nI] & nMask )      // Flag: Tag it when bad
            return Read_F_Tag( &aF );       // Result not as text
                                            // only read result
        if (aF.bResNest && !AcceptableNestedField(aF.nId))
            return aF.nLen;                 // Result nested -> unusable

        sal_uInt64 nOldPos = m_pStrm->Tell();
        OUString aStr;
        aF.nLCode = m_xSBase->WW8ReadString( *m_pStrm, aStr, m_xPlcxMan->GetCpOfs()+
            aF.nSCode, aF.nLCode, m_eTextCharSet );
        m_pStrm->Seek( nOldPos );

        // field codes which contain '/' or '.' are not displayed in WinWord
        // skip if it is formula field or found space before. see #i119446, #i119585.
        const sal_Int32 nDotPos = aStr.indexOf('.');
        const sal_Int32 nSlashPos = aStr.indexOf('/');
        sal_Int32 nSpacePos = aStr.indexOf( ' ', 1 );
        if ( nSpacePos<0 )
            nSpacePos = aStr.getLength();

        if ( ( aStr.getLength() <= 1 || aStr[1] != '=') &&
            (( nDotPos>=0 && nDotPos < nSpacePos ) ||
             ( nSlashPos>=0 && nSlashPos < nSpacePos )))
            return aF.nLen;
        else
        {
            // Link fields aren't supported, but they are bound to an OLE object
            // that needs to be roundtripped
            if ( aF.nId == 56 )
                m_bEmbeddObj = true;
            // Field not supported: store the field code for later use
            m_aFieldStack.back().SetBookmarkCode( aStr );

            if (aF.nId == ww::eIF)
            {
                // In MS Word, the IF field is editable and requires a manual refresh
                // so the last, saved result might not match either of the true or false options.
                // But in LO the field is automatically updated and not editable,
                // so the previous result is of no value to import since it could never be seen.
                return aF.nLen;
            }

            return aF.nLen - aF.nLRes - 1;  // skipped too many, the resulted field will be read like main text
        }
    }
    else
    {                                   // read field
        auto nOldPos = m_pStrm->Tell();
        OUString aStr;
        if ( aF.nId == 6 && aF.bCodeNest )
        {
            // TODO Extract the whole code string using the nested codes
            aF.nLCode = m_xSBase->WW8ReadString( *m_pStrm, aStr, m_xPlcxMan->GetCpOfs() +
                aF.nSCode, aF.nSRes - aF.nSCode - 1, m_eTextCharSet );
        }
        else
        {
            aF.nLCode = m_xSBase->WW8ReadString( *m_pStrm, aStr, m_xPlcxMan->GetCpOfs()+
                aF.nSCode, aF.nLCode, m_eTextCharSet );
        }

        // #i51312# - graphics inside field code not supported by Writer.
        // Thus, delete character 0x01, which stands for such a graphic.
        if (aF.nId==51) //#i56768# only do it for the MACROBUTTON field, since DropListFields need the 0x01.
        {
            aStr = aStr.replaceAll("\x01", "");
        }

        eF_ResT eRes = (this->*aWW8FieldTab[aF.nId])( &aF, aStr );
        m_pStrm->Seek(nOldPos);

        switch ( eRes )
        {
            case eF_ResT::OK:
                return aF.nLen;
            case eF_ResT::TEXT:
                // skipped too many, the resulted field will be read like main text
                // attributes can start at char 0x14 so skip one
                // char more back == "-2"
                if (aF.nLRes)
                    return aF.nLen - aF.nLRes - 2;
                else
                    return aF.nLen;
            case eF_ResT::TAGIGN:
                if ( m_nFieldTagBad[nI] & nMask ) // Flag: Tag bad
                    return Read_F_Tag( &aF );       // Tag it
                return aF.nLen;                 // or ignore
            case eF_ResT::READ_FSPA:
                return aF.nLen - aF.nLRes - 2; // position on char 1
            default:
                return aF.nLen;                     // ignore
        }
    }
}

// Tag fields

// MakeTagString() returns the position of the first CR / end of line / page break
// in pText and converts only up to this point.
// If none of these special characters is found, the function returns 0.
void SwWW8ImplReader::MakeTagString( OUString& rStr, const OUString& rOrg )
{
    bool bAllowCr = SwFltGetFlag( m_nFieldFlags, SwFltControlStack::TAGS_IN_TEXT )
                || SwFltGetFlag( m_nFieldFlags, SwFltControlStack::ALLOW_FLD_CR );
    sal_Unicode cChar;
    rStr = rOrg;

    for( sal_Int32 nI = 0;
            nI < rStr.getLength() && rStr.getLength() < (MAX_FIELDLEN - 4); ++nI )
    {
        bool bSetAsHex = false;
        cChar = rStr[ nI ];
        switch( cChar )
        {
            case 132:                       // Exchange typographical quotation marks for normal ones
            case 148:
            case 147:
                rStr = rStr.replaceAt( nI, 1, u"\"" );
                break;
            case 19:
                rStr = rStr.replaceAt( nI, 1, u"{" );
                break;  // 19..21 to {|}
            case 20:
                rStr = rStr.replaceAt( nI, 1, u"|" );
                break;
            case 21:
                rStr = rStr.replaceAt( nI, 1, u"}" );
                break;
            case '\\':                      // Tag \{|} with \ ...
            case '{':
            case '|':
            case '}':
                rStr = rStr.replaceAt( nI, 0, u"\\" );
                ++nI;
                break;
            case 0x0b:
            case 0x0c:
            case 0x0d:
                if( bAllowCr )
                    rStr = rStr.replaceAt( nI, 1, u"\n" );
                else
                    bSetAsHex = true;
                break;
            case 0xFE:
            case 0xFF:
                bSetAsHex = true;
                break;
            default:
                bSetAsHex = 0x20 > cChar;
                break;
        }

        if( bSetAsHex )
        {
            //all Hex-Numbers with \x before
            OUString sTmp( u"\\x"_ustr );
            if( cChar < 0x10 )
                sTmp += "0";
            sTmp += OUString::number( cChar, 16 );
            rStr = rStr.replaceAt( nI, 1 , sTmp );
            nI += sTmp.getLength() - 1;
        }
    }

    if( rStr.getLength() > (MAX_FIELDLEN - 4))
        rStr = rStr.copy( 0, MAX_FIELDLEN - 4 );
}

void SwWW8ImplReader::InsertTagField( const sal_uInt16 nId, const OUString& rTagText )
{
    OUString aName(u"WwFieldTag"_ustr);
    if( SwFltGetFlag( m_nFieldFlags, SwFltControlStack::TAGS_DO_ID ) ) // Number?
        aName += OUString::number( nId );                    // return it?

    if( SwFltGetFlag(m_nFieldFlags, SwFltControlStack::TAGS_IN_TEXT))
    {
        aName += rTagText;      // tag as text
        m_rDoc.getIDocumentContentOperations().InsertString(*m_pPaM, aName,
                SwInsertFlags::NOHINTEXPAND);
    }
    else
    {                                                   // tag normally

        SwFieldType* pFT = m_rDoc.getIDocumentFieldsAccess().InsertFieldType(
                                SwSetExpFieldType( &m_rDoc, UIName(aName), SwGetSetExpType::String ) );
        SwSetExpField aField( static_cast<SwSetExpFieldType*>(pFT), rTagText );                            // SUB_INVISIBLE
        SwGetSetExpType nSubType = ( SwFltGetFlag( m_nFieldFlags, SwFltControlStack::TAGS_VISIBLE ) ) ? SwGetSetExpType::None : SwGetSetExpType::Invisible;
        aField.SetSubType(nSubType | SwGetSetExpType::String);

        m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );
    }
}

WW8_CP SwWW8ImplReader::Read_F_Tag( WW8FieldDesc* pF )
{
    sal_uInt64 nOldPos = m_pStrm->Tell();

    WW8_CP nStart = pF->nSCode - 1;         // starting with 0x19
    WW8_CP nL = pF->nLen;                     // Total length with result and nest
    if( nL > MAX_FIELDLEN )
        nL = MAX_FIELDLEN;                  // MaxLength, by quoting
                                            // max. 4 times as big
    OUString sFText;
    m_xSBase->WW8ReadString( *m_pStrm, sFText,
                                m_xPlcxMan->GetCpOfs() + nStart, nL, m_eStructCharSet);

    OUString aTagText;
    MakeTagString( aTagText, sFText );
    InsertTagField( pF->nId, aTagText );

    m_pStrm->Seek( nOldPos );
    return pF->nLen;
}

//        normal fields

eF_ResT SwWW8ImplReader::Read_F_Input( WW8FieldDesc* pF, OUString& rStr )
{
    OUString aDef;
    OUString aQ;
    WW8ReadFieldParams aReadParam( rStr );
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
        case -2:
            if( aQ.isEmpty() )
                aQ = aReadParam.GetResult();
            break;
        case 'd':
        case 'D':
            if ( aReadParam.GoToTokenParam() )
                aDef = aReadParam.GetResult();
            break;
        }
    }
    if( aDef.isEmpty() )
        aDef = GetFieldResult( pF );

    if ( pF->nId != 0x01 ) // 0x01 fields have no result
    {
        SwInputField aField( static_cast<SwInputFieldType*>(m_rDoc.getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::Input )),
                            aDef, aQ, SwInputFieldSubType::Text, false );
        m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );
    }

    return eF_ResT::OK;
}

// GetFieldResult allocates a string and reads the resulted field
OUString SwWW8ImplReader::GetFieldResult( WW8FieldDesc const * pF )
{
    sal_uInt64 nOldPos = m_pStrm->Tell();

    WW8_CP nStart = pF->nSRes;              // result start
    WW8_CP nL = pF->nLRes;                    // result length
    if( !nL )
        return OUString();                  // no result

    if( nL > MAX_FIELDLEN )
        nL = MAX_FIELDLEN;                  // MaxLength, by quoting
                                            // max. 4 times as big

    OUString sRes;
    m_xSBase->WW8ReadString( *m_pStrm, sRes, m_xPlcxMan->GetCpOfs() + nStart,
                                nL, m_eStructCharSet );

    m_pStrm->Seek( nOldPos );

    //replace both CR 0x0D and VT 0x0B with LF 0x0A
    // at least in the cases where the result is added to an SwInputField
    // there must not be control characters in it
    OUStringBuffer buf(sRes.getLength());
    for (sal_Int32 i = 0; i < sRes.getLength(); ++i)
    {
        sal_Unicode const ch(sRes[i]);
        if (!linguistic::IsControlChar(ch))
        {
            buf.append(ch);
        }
        else
        {
            switch (ch)
            {
                case 0x0B:
                case '\r':
                    buf.append('\n');
                    break;
                case '\n':
                case '\t':
                    buf.append(ch);
                    break;
                default:
                    SAL_INFO("sw.ww8", "GetFieldResult(): filtering control character");
                    break;
            }
        }
    }
    return buf.makeStringAndClear();
}

/*
Bookmarks can be set with fields SET and ASK, and they can be referenced with
REF. When set, they behave like variables in writer, otherwise they behave
like normal bookmarks. We can check whether we should use a show variable
instead of a normal bookmark ref by converting to "show variable" at the end
of the document those refs which look for the content of a bookmark but whose
bookmarks were set with SET or ASK. (See SwWW8FltRefStack)

The other piece of the puzzle is that refs that point to the "location" of the
bookmark will in word actually point to the last location where the bookmark
was set with SET or ASK, not the actual bookmark. This is only noticeable when
a document sets the bookmark more than once. This is because word places the
true bookmark at the location of the last set, but the refs will display the
position of the first set before the ref.

So what we will do is

1) keep a list of all bookmarks that were set, any bookmark names mentioned
here that are referred by content will be converted to show variables.

2) create pseudo bookmarks for every position that a bookmark is set with SET
or ASK but has no existing bookmark. We can then keep a map from the original
bookmark name to the new one. As we parse the document new pseudo names will
replace the older ones, so the map always contains the bookmark of the
location that msword itself would use.

3) word's bookmarks are case insensitive, writers are not. So we need to
map case different versions together, regardless of whether they are
variables or not.

4) when a reference is (first) SET or ASK, the bookmark associated with it
is placed around the 0x14 0x15 result part of the field. We will fiddle
the placement to be the writer equivalent of directly before and after
the field, which gives the same effect and meaning, to do so we must
get any bookmarks in the field range, and begin them immediately before
the set/ask field, and end them directly afterwards. MapBookmarkVariables
returns an identifier of the bookmark attribute to close after inserting
the appropriate set/ask field.
*/
tools::Long SwWW8ImplReader::MapBookmarkVariables(const WW8FieldDesc* pF,
    OUString &rOrigName, const OUString &rData)
{
    OSL_ENSURE(m_xPlcxMan, "No pPlcxMan");
    tools::Long nNo;
    /*
    If there was no bookmark associated with this set field, then we create a
    pseudo one and insert it in the document.
    */
    sal_uInt16 nIndex;
    m_xPlcxMan->GetBook()->MapName(rOrigName);
    OUString sName = m_xPlcxMan->GetBook()->GetBookmark(
        pF->nSCode, pF->nSCode + pF->nLen, nIndex);
    if (!sName.isEmpty())
    {
        m_xPlcxMan->GetBook()->SetStatus(nIndex, BOOK_IGNORE);
        nNo = nIndex;
    }
    else
    {
        nNo = m_xReffingStck->m_aFieldVarNames.size()+1;
        sName = "WWSetBkmk" + OUString::number(nNo);
        nNo += m_xPlcxMan->GetBook()->GetIMax();
    }
    m_xReffedStck->NewAttr(*m_pPaM->GetPoint(),
        SwFltBookmark( BookmarkToWriter(sName), rData, nNo ));
    m_xReffingStck->m_aFieldVarNames[rOrigName] = sName;
    return nNo;
}

/*
Word can set a bookmark with set or with ask, such a bookmark is equivalent to
our variables, but until the end of a document we cannot be sure if a bookmark
is a variable or not, at the end we will have a list of reference names which
were set or asked, all bookmarks using the content of those bookmarks are
converted to show variables, those that reference the position of the field
can be left as references, because a bookmark is also inserted at the position
of a set or ask field, either by word, or in some special cases by the import
filter itself.
*/
SwFltStackEntry *SwWW8FltRefStack::RefToVar(const SwField* pField,
    SwFltStackEntry &rEntry)
{
    SwFltStackEntry *pRet=nullptr;
    if (pField && SwFieldIds::GetRef == pField->Which())
    {
        //Get the name of the ref field, and see if actually a variable
        const OUString sName = pField->GetPar1();
        std::map<OUString, OUString, SwWW8::ltstr>::const_iterator
            aResult = m_aFieldVarNames.find(sName);

        if (aResult != m_aFieldVarNames.end())
        {
            SwGetExpField aField( static_cast<SwGetExpFieldType*>(
                m_rDoc.getIDocumentFieldsAccess().GetSysFieldType(SwFieldIds::GetExp)), sName, SwGetSetExpType::String, 0);
            SwFormatField aTmp(aField);
            rEntry.m_pAttr.reset( aTmp.Clone() );
            pRet = &rEntry;
        }
    }
    return pRet;
}

OUString SwWW8ImplReader::GetMappedBookmark(std::u16string_view rOrigName)
{
    OUString sName(BookmarkToWriter(rOrigName));
    OSL_ENSURE(m_xPlcxMan, "no pPlcxMan");
    m_xPlcxMan->GetBook()->MapName(sName);

    //See if there has been a variable set with this name, if so get
    //the pseudo bookmark name that was set with it.
    std::map<OUString, OUString, SwWW8::ltstr>::const_iterator aResult =
            m_xReffingStck->m_aFieldVarNames.find(sName);

    return (aResult == m_xReffingStck->m_aFieldVarNames.end())
        ? sName : (*aResult).second;
}

// "ASK"
eF_ResT SwWW8ImplReader::Read_F_InputVar( WW8FieldDesc* pF, OUString& rStr )
{
    OUString sOrigName, aQ;
    OUString aDef;
    WW8ReadFieldParams aReadParam( rStr );
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
        case -2:
            if (sOrigName.isEmpty())
                sOrigName = aReadParam.GetResult();
            else if (aQ.isEmpty())
                aQ = aReadParam.GetResult();
            break;
        case 'd':
        case 'D':
            if ( aReadParam.GoToTokenParam() )
                aDef = aReadParam.GetResult();
            break;
        }
    }

    if (sOrigName.isEmpty())
        return eF_ResT::TAGIGN;  // does not make sense without textmark

    const OUString aResult(GetFieldResult(pF));

    //#i24377#, munge Default Text into title as we have only one slot
    //available for aResult and aDef otherwise
    if (!aDef.isEmpty())
    {
        if (!aQ.isEmpty())
            aQ += " - ";
        aQ += aDef;
    }

    const tools::Long nNo = MapBookmarkVariables(pF, sOrigName, aResult);

    SwSetExpFieldType* pFT = static_cast<SwSetExpFieldType*>(m_rDoc.getIDocumentFieldsAccess().InsertFieldType(
        SwSetExpFieldType(&m_rDoc, UIName(sOrigName), SwGetSetExpType::String)));
    SwSetExpField aField(pFT, aResult);
    aField.SetSubType(SwGetSetExpType::Invisible | SwGetSetExpType::String);
    aField.SetInputFlag(true);
    aField.SetPromptText( aQ );

    m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );

    m_xReffedStck->SetAttr(*m_pPaM->GetPoint(), RES_FLTR_BOOKMARK, true, nNo);
    return eF_ResT::OK;
}

// "AUTONR"
eF_ResT SwWW8ImplReader::Read_F_ANumber( WW8FieldDesc*, OUString& rStr )
{
    if( !m_pNumFieldType ){     // 1st time
        SwSetExpFieldType aT( &m_rDoc, UIName(u"AutoNr"_ustr), SwGetSetExpType::Sequence );
        m_pNumFieldType = static_cast<SwSetExpFieldType*>(m_rDoc.getIDocumentFieldsAccess().InsertFieldType( aT ));
    }
    SwSetExpField aField( m_pNumFieldType, OUString(), GetNumberPara( rStr ) );
    aField.SetValue( ++m_nFieldNum, nullptr );
    m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );
    return eF_ResT::OK;
}

// "SEQ"
eF_ResT SwWW8ImplReader::Read_F_Seq( WW8FieldDesc*, OUString& rStr )
{
    OUString aSequenceName;
    OUString aBook;
    bool bHidden    = false;
    bool bFormat    = false;
    bool bCountOn   = true;
    OUString sStart;
    SvxNumType eNumFormat = SVX_NUM_ARABIC;
    WW8ReadFieldParams aReadParam( rStr );
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
        case -2:
            if( aSequenceName.isEmpty() )
                aSequenceName = aReadParam.GetResult();
            else if( aBook.isEmpty() )
                aBook = aReadParam.GetResult();
            break;

        case 'h':
            if( !bFormat )
                bHidden = true;             // activate hidden flag
            break;

        case '*':
            bFormat = true;                 // activate format flag
            if ( aReadParam.SkipToNextToken()!=-2 )
                break;
            if ( aReadParam.GetResult()!="MERGEFORMAT" && aReadParam.GetResult()!="CHARFORMAT" )
                eNumFormat = GetNumTypeFromName( aReadParam.GetResult() );
            break;

        case 'r':
            bCountOn  = false;
            if ( aReadParam.SkipToNextToken()==-2 )
                sStart = aReadParam.GetResult();
            break;

        case 'c':
            bCountOn  = false;
            break;

        case 'n':
            bCountOn  = true;               // Increase value by one (default)
            break;

        case 's':                       // Outline Level
            //#i19682, what I have to do with this value?
            break;
        }
    }
    if (aSequenceName.isEmpty() && aBook.isEmpty())
        return eF_ResT::TAGIGN;

    SwSetExpFieldType* pFT = static_cast<SwSetExpFieldType*>(m_rDoc.getIDocumentFieldsAccess().InsertFieldType(
                        SwSetExpFieldType( &m_rDoc, UIName(aSequenceName), SwGetSetExpType::Sequence ) ) );
    SwSetExpField aField( pFT, OUString(), eNumFormat );

    //#i120654# Add bHidden for /h flag (/h: Hide the field result.)
    if (bHidden)
        aField.SetSubType(aField.GetSubType() | SwGetSetExpType::Invisible);

    if (!sStart.isEmpty())
        aField.SetFormula( aSequenceName + "=" + sStart );
    else if (!bCountOn)
        aField.SetFormula(aSequenceName);

    m_rDoc.getIDocumentContentOperations().InsertPoolItem(*m_pPaM, SwFormatField(aField));
    return eF_ResT::OK;
}

eF_ResT SwWW8ImplReader::Read_F_Styleref(WW8FieldDesc*, OUString& rString)
{
    WW8ReadFieldParams aReadParam(rString);
    sal_Int32 nRet = aReadParam.SkipToNextToken();
    if (nRet != -2)
        // \param was found, not normal text.
        return eF_ResT::TAGIGN;

    OUString aResult = aReadParam.GetResult();
    sal_Int32 nResult = aResult.toInt32();
    if (nResult < 1)
        return eF_ResT::TAGIGN;

    SwFieldType* pFieldType = m_rDoc.getIDocumentFieldsAccess().GetSysFieldType(SwFieldIds::Chapter);
    SwChapterField aField(static_cast<SwChapterFieldType*>(pFieldType), SwChapterFormat::Title);
    aField.SetLevel(nResult - 1);
    m_rDoc.getIDocumentContentOperations().InsertPoolItem(*m_pPaM, SwFormatField(aField));

    return eF_ResT::OK;
}

eF_ResT SwWW8ImplReader::Read_F_DocInfo( WW8FieldDesc* pF, OUString& rStr )
{
    SwDocInfoSubType nSub = SwDocInfoSubType::SubtypeBegin;
    // RegInfoFormat, DefaultFormat for DocInfoFields
    SwDocInfoSubType nReg = SwDocInfoSubType::SubAuthor;
    bool bDateTime = false;
    const SwDocInfoSubType nFldLock = (pF->nOpt & 0x10) ? SwDocInfoSubType::SubFixed : SwDocInfoSubType::SubtypeBegin;

    if( 85 == pF->nId )
    {
        OUString aDocProperty;
        WW8ReadFieldParams aReadParam( rStr );
        for (;;)
        {
            const sal_Int32 nRet = aReadParam.SkipToNextToken();
            if ( nRet==-1 )
                break;
            switch( nRet )
            {
                case -2:
                    if( aDocProperty.isEmpty() )
                        aDocProperty = aReadParam.GetResult();
                    break;
                case '*':
                    //Skip over MERGEFORMAT
                    (void)aReadParam.SkipToNextToken();
                    break;
            }
        }

        aDocProperty = aDocProperty.replaceAll("\"", "");

        /*
        There are up to 26 fields that may be meant by 'DocumentProperty'.
        Which of them is to be inserted here ?
        This Problem can only be solved by implementing a name matching
        method that compares the given Parameter String with the four
        possible name sets (english, german, french, spanish)
        */

        static const char* const aName10 = "\x0F"; // SW field code
        static const char* const aName11 // German
            = "TITEL";
        static const char* const aName12 // French
            = "TITRE";
        static const char* const aName13 // English
            = "TITLE";
        static const char* const aName14 // Spanish
            = "TITRO";
        static const char* const aName20 = "\x15"; // SW field code
        static const char* const aName21 // German
            = "ERSTELLDATUM";
        static const char* const aName22 // French
            = "CR\xC9\xC9";
        static const char* const aName23 // English
            = "CREATED";
        static const char* const aName24 // Spanish
            = "CREADO";
        static const char* const aName30 = "\x16"; // SW field code
        static const char* const aName31 // German
            = "ZULETZTGESPEICHERTZEIT";
        static const char* const aName32 // French
            = "DERNIERENREGISTREMENT";
        static const char* const aName33 // English
            = "SAVED";
        static const char* const aName34 // Spanish
            = "MODIFICADO";
        static const char* const aName40 = "\x17"; // SW field code
        static const char* const aName41 // German
            = "ZULETZTGEDRUCKT";
        static const char* const aName42 // French
            = "DERNI\xC8" "REIMPRESSION";
        static const char* const aName43 // English
            = "LASTPRINTED";
        static const char* const aName44 // Spanish
            = "HUPS PUPS";
        static const char* const aName50 = "\x18"; // SW field code
        static const char* const aName51 // German
            = "\xDC" "BERARBEITUNGSNUMMER";
        static const char* const aName52 // French
            = "NUM\xC9" "RODEREVISION";
        static const char* const aName53 // English
            = "REVISIONNUMBER";
        static const char* const aName54 // Spanish
            = "SNUBBEL BUBBEL";
        static const sal_uInt16 nFieldCnt  = 5;

        // additional fields are to be coded soon!

        static const sal_uInt16 nLangCnt = 4;
        static const char * const aNameSet_26[nFieldCnt][nLangCnt+1] =
        {
            {aName10, aName11, aName12, aName13, aName14},
            {aName20, aName21, aName22, aName23, aName24},
            {aName30, aName31, aName32, aName33, aName34},
            {aName40, aName41, aName42, aName43, aName44},
            {aName50, aName51, aName52, aName53, aName54}
        };

        bool bFieldFound= false;
        sal_uInt16 nFIdx;
        for(sal_uInt16 nLIdx=1; !bFieldFound && (nLangCnt > nLIdx); ++nLIdx)
        {
            for(nFIdx = 0;  !bFieldFound && (nFieldCnt  > nFIdx); ++nFIdx)
            {
                if( aDocProperty == OUString( aNameSet_26[nFIdx][nLIdx], strlen(aNameSet_26[nFIdx][nLIdx]),
                                              RTL_TEXTENCODING_MS_1252 ) )
                {
                    bFieldFound = true;
                    pF->nId   = aNameSet_26[nFIdx][0][0];
                }
            }
        }

        if( !bFieldFound )
        {
            // LO always automatically updates a DocInfo field from the File-Properties-Custom Prop
            // while MS Word requires the user to manually refresh the field (with F9).
            // In other words, Word lets the field to be out of sync with the controlling variable.
            // Marking as FIXEDFLD solves the automatic replacement problem, but of course prevents
            // Writer from making any changes, even on an F9 refresh.
            // TODO: Extend LO to allow a linked field that doesn't automatically update.
            IDocumentContentOperations& rIDCO(m_rDoc.getIDocumentContentOperations());
            const auto pType(static_cast<SwDocInfoFieldType*>(
                m_rDoc.getIDocumentFieldsAccess().GetSysFieldType(SwFieldIds::DocInfo)));
            const OUString sDisplayed = GetFieldResult(pF);
            SwDocInfoField aField(pType, SwDocInfoSubType::Custom | nReg, aDocProperty);

            // If text already matches the DocProperty var, then safe to treat as refreshable field.
            OUString sVariable = aField.ExpandField(/*bCache=*/false, nullptr);
            if (sDisplayed.getLength() != sVariable.getLength())
            {
                sal_Int32 nLen = sVariable.indexOf('\x0');
                if (nLen >= 0)
                    sVariable = sVariable.copy(0, nLen);
            }
            if (sDisplayed == sVariable)
                rIDCO.InsertPoolItem(*m_pPaM, SwFormatField(aField));
            else
            {
                // They don't match, so use a fixed field to prevent LO from altering the contents.
                SwDocInfoField aFixedField(pType, SwDocInfoSubType::Custom | SwDocInfoSubType::SubFixed | nReg, aDocProperty,
                                           sDisplayed);
                rIDCO.InsertPoolItem(*m_pPaM, SwFormatField(aFixedField));
            }

            return eF_ResT::OK;
        }
    }

    switch( pF->nId )
    {
        case 14:
            /* supports all INFO variables! */
            nSub = SwDocInfoSubType::Keys;
            break;
        case 15:
            nSub = SwDocInfoSubType::Title;
            break;
        case 16:
            nSub = SwDocInfoSubType::Subject;
            break;
        case 18:
            nSub = SwDocInfoSubType::Keys;
            break;
        case 19:
            nSub = SwDocInfoSubType::Comment;
            break;
        case 20:
            // MS Word never updates this automatically, so mark as fixed for best compatibility
            nSub = SwDocInfoSubType::Change | SwDocInfoSubType::SubFixed;
            nReg = SwDocInfoSubType::SubAuthor;
            break;
        case 21:
            // The real create date can never change, so mark as fixed for best compatibility
            nSub = SwDocInfoSubType::Create | SwDocInfoSubType::SubFixed;
            nReg = SwDocInfoSubType::SubDate;
            bDateTime = true;
            break;
        case 23:
            nSub = SwDocInfoSubType::Print | nFldLock;
            nReg = SwDocInfoSubType::SubDate;
            bDateTime = true;
            break;
        case 24:
            nSub = SwDocInfoSubType::DocNo;
            break;
        case 22:
            nSub = SwDocInfoSubType::Change | nFldLock;
            nReg = SwDocInfoSubType::SubDate;
            bDateTime = true;
            break;
        case 25:
            nSub = SwDocInfoSubType::Change | nFldLock;
            nReg = SwDocInfoSubType::SubTime;
            bDateTime = true;
            break;
        case 64: // DOCVARIABLE
            nSub = SwDocInfoSubType::Custom;
            break;
    }

    sal_uInt32 nFormat = 0;

    LanguageType nLang(LANGUAGE_SYSTEM);
    if (bDateTime)
    {
        SvNumFormatType nDT = GetTimeDatePara(rStr, nFormat, nLang, pF->nId);
        switch (nDT)
        {
            case SvNumFormatType::DATE:
                nReg = SwDocInfoSubType::SubDate;
                break;
            case SvNumFormatType::TIME:
                nReg = SwDocInfoSubType::SubTime;
                break;
            case SvNumFormatType::DATETIME:
                nReg = SwDocInfoSubType::SubDate;
                break;
            default:
                nReg = SwDocInfoSubType::SubDate;
                break;
        }
    }

    OUString aData;
    // Extract DOCVARIABLE varname
    if ( 64 == pF->nId )
    {
        WW8ReadFieldParams aReadParam( rStr );
        for (;;)
        {
            const sal_Int32 nRet = aReadParam.SkipToNextToken();
            if ( nRet==-1)
                break;
            switch( nRet )
            {
                case -2:
                    if( aData.isEmpty() )
                        aData = aReadParam.GetResult();
                    break;
                case '*':
                    //Skip over MERGEFORMAT
                    (void)aReadParam.SkipToNextToken();
                    break;
            }
        }

        aData = aData.replaceAll("\"", "");
    }

    bool bDone = false;
    if (SwDocInfoSubType::Custom == nSub)
    {
        const auto pType(static_cast<SwUserFieldType*>(
            m_rDoc.getIDocumentFieldsAccess().GetFieldType(SwFieldIds::User, aData, false)));
        if (pType)
        {
            SwUserField aField(pType, SwUserType::None, nFormat);
            if (bDateTime)
                ForceFieldLanguage(aField, nLang);
            m_rDoc.getIDocumentContentOperations().InsertPoolItem(*m_pPaM, SwFormatField(aField));
            bDone = true;
        }
    }
    if (!bDone)
    {
        const auto pType(static_cast<SwDocInfoFieldType*>(
            m_rDoc.getIDocumentFieldsAccess().GetSysFieldType(SwFieldIds::DocInfo)));
        SwDocInfoField aField(pType, nSub|nReg, aData, GetFieldResult(pF), nFormat);
        if (bDateTime)
            ForceFieldLanguage(aField, nLang);
        m_rDoc.getIDocumentContentOperations().InsertPoolItem(*m_pPaM, SwFormatField(aField));
    }

    return eF_ResT::OK;
}

eF_ResT SwWW8ImplReader::Read_F_Author(WW8FieldDesc* pF, OUString&)
{
        // SH: The SwAuthorField refers not to the original author but to the current user, better use DocInfo
    SwDocInfoField aField( static_cast<SwDocInfoFieldType*>(
                     m_rDoc.getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::DocInfo )),
                     SwDocInfoSubType::Create | SwDocInfoSubType::SubAuthor | SwDocInfoSubType::SubFixed,
                     OUString(), GetFieldResult(pF));
    m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );
    return eF_ResT::OK;
}

eF_ResT SwWW8ImplReader::Read_F_TemplName( WW8FieldDesc*, OUString& )
{
    SwTemplNameField aField( static_cast<SwTemplNameFieldType*>(
                     m_rDoc.getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::TemplateName )), SwFileNameFormat::Name );
    m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );
    return eF_ResT::OK;
}

// Both the date and the time fields can be used for showing a date a time or both.
eF_ResT SwWW8ImplReader::Read_F_DateTime( WW8FieldDesc*pF, OUString& rStr )
{
    bool bHijri = false;
    WW8ReadFieldParams aReadParam(rStr);
    for (;;)
    {
        const sal_Int32 nTok = aReadParam.SkipToNextToken();
        if ( nTok==-1 )
            break;
        switch (nTok)
        {
            default:
            case 'l':
            case -2:
                break;
            case 'h':
                bHijri = true;
                break;
            case 's':
                //Saka Calendar, should we do something with this ?
                break;
        }
    }

    sal_uInt32 nFormat = 0;

    LanguageType nLang(LANGUAGE_SYSTEM);
    SvNumFormatType nDT = GetTimeDatePara(rStr, nFormat, nLang, ww::eDATE, bHijri);

    if( SvNumFormatType::UNDEFINED == nDT )             // no D/T-Formatstring
    {
        if (32 == pF->nId)
        {
            nDT     = SvNumFormatType::TIME;
            nFormat = m_rDoc.GetNumberFormatter()->GetFormatIndex(
                        NF_TIME_START, LANGUAGE_SYSTEM );
        }
        else
        {
            nDT     = SvNumFormatType::DATE;
            nFormat = m_rDoc.GetNumberFormatter()->GetFormatIndex(
                        NF_DATE_START, LANGUAGE_SYSTEM );
        }
    }

    if (nDT & SvNumFormatType::DATE || nDT == SvNumFormatType::TIME)
    {
        SwDateTimeField aField(static_cast<SwDateTimeFieldType*>(
            m_rDoc.getIDocumentFieldsAccess().GetSysFieldType(SwFieldIds::DateTime)),
            nDT & SvNumFormatType::DATE ? SwDateTimeSubType::Date : SwDateTimeSubType::Time, nFormat);
        if (pF->nOpt & 0x10) // Fixed field
        {
            double fSerial;
            if (!m_rDoc.GetNumberFormatter()->IsNumberFormat(GetFieldResult(pF), nFormat, fSerial,
                                                             SvNumInputOptions::LAX_TIME))
                return eF_ResT::TEXT; // just drop the field and insert the plain text.
            aField.SetSubType(aField.GetSubType() | SwDateTimeSubType::Fixed);
            DateTime aSetDateTime(m_rDoc.GetNumberFormatter()->GetNullDate());
            aSetDateTime.AddTime(fSerial);
            aField.SetDateTime(aSetDateTime);
        }
        ForceFieldLanguage(aField, nLang);
        m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );
    }

    return eF_ResT::OK;
}

eF_ResT SwWW8ImplReader::Read_F_FileName(WW8FieldDesc*, OUString &rStr)
{
    SwFileNameFormat eType = SwFileNameFormat::Name;
    WW8ReadFieldParams aReadParam(rStr);
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch (nRet)
        {
            case 'p':
                eType = SwFileNameFormat::PathName;
                break;
            case '*':
                //Skip over MERGEFORMAT
                (void)aReadParam.SkipToNextToken();
                break;
            default:
                OSL_ENSURE(false, "unknown option in FileName field");
                break;
        }
    }

    SwFileNameField aField(
        static_cast<SwFileNameFieldType*>(m_rDoc.getIDocumentFieldsAccess().GetSysFieldType(SwFieldIds::Filename)), eType);
    m_rDoc.getIDocumentContentOperations().InsertPoolItem(*m_pPaM, SwFormatField(aField));
    return eF_ResT::OK;
}

eF_ResT SwWW8ImplReader::Read_F_Num( WW8FieldDesc* pF, OUString& rStr )
{
    SwDocStatSubType nSub = SwDocStatSubType::Page;                  // page number
    switch ( pF->nId ){
        case 27: nSub = SwDocStatSubType::Word; break;         // number of words
        case 28: nSub = SwDocStatSubType::Character; break;         // number of characters
    }
    SwDocStatField aField( static_cast<SwDocStatFieldType*>(
                         m_rDoc.getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::DocStat )), nSub,
                         GetNumberPara( rStr ) );
    m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );
    return eF_ResT::OK;
}

eF_ResT SwWW8ImplReader::Read_F_CurPage( WW8FieldDesc*, OUString& rStr )
{
    // page number
    SwPageNumberField aField( static_cast<SwPageNumberFieldType*>(
        m_rDoc.getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::PageNumber )), SwPageNumSubType::Random,
        GetNumberPara(rStr, true));

    m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );
    return eF_ResT::OK;
}

eF_ResT SwWW8ImplReader::Read_F_Symbol( WW8FieldDesc*, OUString& rStr )
{
    //e.g. #i20118#
    OUString aQ;
    OUString aName;
    sal_Int32 nSize = 0;
    WW8ReadFieldParams aReadParam( rStr );
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
        case -2:
            if( aQ.isEmpty() )
                aQ = aReadParam.GetResult();
            break;
        case 'f':
        case 'F':
            if ( aReadParam.GoToTokenParam() )
                aName = aReadParam.GetResult();
            break;
        case 's':
        case 'S':
            if ( aReadParam.GoToTokenParam() )
            {
                const OUString aSiz = aReadParam.GetResult();
                if (!aSiz.isEmpty())
                {
                    bool bFail = o3tl::checked_multiply<sal_Int32>(aSiz.toInt32(), 20, nSize); // pT -> twip
                    if (bFail)
                        nSize = -1;
                }
            }
            break;
        }
    }
    if( aQ.isEmpty() )
        return eF_ResT::TAGIGN;                      // -> no 0-char in text

    sal_Unicode const cChar = static_cast<sal_Unicode>(aQ.toInt32());
    if (!linguistic::IsControlChar(cChar) || cChar == '\r' || cChar == '\n' || cChar == '\t')
    {
        if (!aName.isEmpty())                           // Font Name set ?
        {
            SvxFontItem aFont(FAMILY_DONTKNOW, aName, OUString(),
                PITCH_DONTKNOW, RTL_TEXTENCODING_SYMBOL, RES_CHRATR_FONT);
            NewAttr(aFont);                       // new Font
        }

        if (nSize > 0)  //#i20118#
        {
            SvxFontHeightItem aSz(nSize, 100, RES_CHRATR_FONTSIZE);
            NewAttr(aSz);
        }

        m_rDoc.getIDocumentContentOperations().InsertString(*m_pPaM, OUString(cChar));

        if (nSize > 0)
            m_xCtrlStck->SetAttr(*m_pPaM->GetPoint(), RES_CHRATR_FONTSIZE);
        if (!aName.isEmpty())
            m_xCtrlStck->SetAttr(*m_pPaM->GetPoint(), RES_CHRATR_FONT);
    }
    else
    {
        m_rDoc.getIDocumentContentOperations().InsertString(*m_pPaM, u"###"_ustr);
    }

    return eF_ResT::OK;
}

// "EMBED"
eF_ResT SwWW8ImplReader::Read_F_Embedd( WW8FieldDesc*, OUString& rStr )
{
    WW8ReadFieldParams aReadParam( rStr );
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
        case -2:
            // sHost
            break;

        case 's':
            // use ObjectSize
            break;
        }
    }

    if( m_bObj && m_nPicLocFc )
        m_nObjLocFc = m_nPicLocFc;
    m_bEmbeddObj = true;
    return eF_ResT::TEXT;
}

// "SET"
eF_ResT SwWW8ImplReader::Read_F_Set( WW8FieldDesc* pF, OUString& rStr )
{
    OUString sOrigName;
    OUString sVal;
    WW8ReadFieldParams aReadParam( rStr );
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
        case -2:
            if (sOrigName.isEmpty())
                sOrigName = aReadParam.GetResult();
            else if (sVal.isEmpty())
                sVal = aReadParam.GetResult();
            break;
        }
    }

    const tools::Long nNo = MapBookmarkVariables(pF, sOrigName, sVal);

    SwFieldType* pFT = m_rDoc.getIDocumentFieldsAccess().InsertFieldType( SwSetExpFieldType( &m_rDoc, UIName(sOrigName),
        SwGetSetExpType::String ) );
    SwSetExpField aField( static_cast<SwSetExpFieldType*>(pFT), sVal, ULONG_MAX );
    aField.SetSubType(SwGetSetExpType::Invisible | SwGetSetExpType::String);

    m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );

    m_xReffedStck->SetAttr(*m_pPaM->GetPoint(), RES_FLTR_BOOKMARK, true, nNo);

    return eF_ResT::OK;
}

// "REF"
eF_ResT SwWW8ImplReader::Read_F_Ref( WW8FieldDesc*, OUString& rStr )
{                                                       // Reference - Field
    OUString sOrigBkmName;
    RefFieldFormat eFormat = RefFieldFormat::Content;

    WW8ReadFieldParams aReadParam( rStr );
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
        case -2:
            if( sOrigBkmName.isEmpty() ) // get name of bookmark
                sOrigBkmName = aReadParam.GetResult();
            break;

        /* References to numbers in Word could be either to a numbered
        paragraph or to a chapter number. However Word does not seem to
        have the capability we do, of referring to the chapter number some
        other bookmark is in. As a result, cross-references to chapter
        numbers in a word document will be cross-references to a numbered
        paragraph, being the chapter heading paragraph. As it happens, our
        cross-references to numbered paragraphs will do the right thing
        when the target is a numbered chapter heading, so there is no need
        for us to use the REF_CHAPTER bookmark format on import.
        */
        case 'n':
            eFormat = RefFieldFormat::NumberNoContext;
            break;
        case 'r':
            eFormat = RefFieldFormat::Number;
            break;
        case 'w':
            eFormat = RefFieldFormat::NumberFullContext;
            break;

        case 'p':
            eFormat = RefFieldFormat::UpDown;
            break;
        case 'h':
            break;
        default:
            // unimplemented switch: just do 'nix nought nothing'  :-)
            break;
        }
    }

    SwMarkName sBkmName(GetMappedBookmark(sOrigBkmName));

    // #i120879# add cross reference bookmark name prefix, if it
    // matches internal TOC bookmark naming convention
    if ( IsTOCBookmarkName( sBkmName ) )
    {
        sBkmName = EnsureTOCBookmarkName(sBkmName);
        // track <sBookmarkName> as referenced TOC bookmark.
        m_xReffedStck->m_aReferencedTOCBookmarks.insert( sBkmName.toString() );
    }

    SwGetRefField aField(
        static_cast<SwGetRefFieldType*>(m_rDoc.getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::GetRef )),
        std::move(sBkmName), u""_ustr, ReferencesSubtype::Bookmark, 0, 0, eFormat);

    if (eFormat == RefFieldFormat::Content)
    {
        /*
        If we are just inserting the contents of the bookmark, then it
        is possible that the bookmark is actually a variable, so we
        must store it until the end of the document to see if it was,
        in which case we'll turn it into a show variable
        */
        m_xReffingStck->NewAttr( *m_pPaM->GetPoint(), SwFormatField(aField) );
        m_xReffingStck->SetAttr( *m_pPaM->GetPoint(), RES_TXTATR_FIELD);
    }
    else
    {
        m_rDoc.getIDocumentContentOperations().InsertPoolItem(*m_pPaM, SwFormatField(aField));
    }
    return eF_ResT::OK;
}

// Note Reference - Field
eF_ResT SwWW8ImplReader::Read_F_NoteReference( WW8FieldDesc*, OUString& rStr )
{
    OUString aBkmName;
    bool bAboveBelow = false;

    WW8ReadFieldParams aReadParam( rStr );
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
        case -2:
            if( aBkmName.isEmpty() ) // get name of foot/endnote
                aBkmName = aReadParam.GetResult();
            break;
        case 'r':
            // activate flag 'Chapter Number'
            break;
        case 'p':
            bAboveBelow = true;
            break;
        case 'h':
            break;
        default:
            // unimplemented switch: just do 'nix nought nothing'  :-)
            break;
        }
    }

    // set Sequence No of corresponding Foot-/Endnote to Zero
    // (will be corrected in
    SwGetRefField aField( static_cast<SwGetRefFieldType*>(
        m_rDoc.getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::GetRef )), SwMarkName(aBkmName), u""_ustr, ReferencesSubtype::Footnote, 0, 0,
        RefFieldFormat::CategoryAndNumber );
    m_xReffingStck->NewAttr(*m_pPaM->GetPoint(), SwFormatField(aField));
    m_xReffingStck->SetAttr(*m_pPaM->GetPoint(), RES_TXTATR_FIELD);
    if (bAboveBelow)
    {
        SwGetRefField aField2( static_cast<SwGetRefFieldType*>(
            m_rDoc.getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::GetRef )), SwMarkName(aBkmName), u""_ustr, ReferencesSubtype::Footnote, 0, 0,
            RefFieldFormat::UpDown );
        m_xReffingStck->NewAttr(*m_pPaM->GetPoint(), SwFormatField(aField2));
        m_xReffingStck->SetAttr(*m_pPaM->GetPoint(), RES_TXTATR_FIELD);
    }
    return eF_ResT::OK;
}

// "PAGEREF"
eF_ResT SwWW8ImplReader::Read_F_PgRef( WW8FieldDesc*, OUString& rStr )
{
    OUString sOrigName;
    WW8ReadFieldParams aReadParam( rStr );
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        else if ( nRet == -2 && sOrigName.isEmpty() )
        {
            sOrigName = aReadParam.GetResult();
        }
    }

    const OUString sName(GetMappedBookmark(sOrigName));

    // loading page reference field in TOX
    if (m_bLoadingTOXCache)
    {
        // insert page ref representation as plain text --> return FLD_TEXT
        // if there is no hyperlink settings for current toc and referenced bookmark is available,
        // assign link to current ref area
        if (!m_bLoadingTOXHyperlink && !sName.isEmpty())
        {
            // #i120879# add cross reference bookmark name prefix, if it
            // matches internal TOC bookmark naming convention
            SwMarkName sBookmarkName;
            if ( IsTOCBookmarkName( SwMarkName(sName) ) )
            {
                sBookmarkName = EnsureTOCBookmarkName(SwMarkName(sName));
                // track <sBookmarkName> as referenced TOC bookmark.
                m_xReffedStck->m_aReferencedTOCBookmarks.insert( sBookmarkName.toString() );
            }
            else
            {
                sBookmarkName = SwMarkName(sName);
            }
            OUString sURL = "#" + sBookmarkName.toString();
            SwFormatINetFormat aURL( sURL, u""_ustr );
            static constexpr OUString sLinkStyle(u"Index Link"_ustr);
            const sal_uInt16 nPoolId =
                SwStyleNameMapper::GetPoolIdFromProgName( ProgName(sLinkStyle), SwGetPoolIdFromName::ChrFmt );
            aURL.SetVisitedFormatAndId( UIName(sLinkStyle), nPoolId);
            aURL.SetINetFormatAndId( UIName(sLinkStyle), nPoolId );
            m_xCtrlStck->NewAttr( *m_pPaM->GetPoint(), aURL );
        }
        return eF_ResT::TEXT;
    }

    // #i120879# add cross reference bookmark name prefix, if it matches
    // internal TOC bookmark naming convention
    SwMarkName sPageRefBookmarkName;
    if ( IsTOCBookmarkName( SwMarkName(sName) ) )
    {
        sPageRefBookmarkName = EnsureTOCBookmarkName(SwMarkName(sName));
        // track <sPageRefBookmarkName> as referenced TOC bookmark.
        m_xReffedStck->m_aReferencedTOCBookmarks.insert( sPageRefBookmarkName.toString() );
    }
    else
    {
        sPageRefBookmarkName = SwMarkName(sName);
    }
    SwGetRefField aField( static_cast<SwGetRefFieldType*>(m_rDoc.getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::GetRef )),
                        std::move(sPageRefBookmarkName), u""_ustr, ReferencesSubtype::Bookmark, 0, 0, RefFieldFormat::Page );
    m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );

    return eF_ResT::OK;
}

//helper function
//For MS MacroButton field, the symbol in plain text is always "(" (0x28),
//which should be mapped according to the macro type
static bool ConvertMacroSymbol( std::u16string_view rName, OUString& rReference )
{
    bool bConverted = false;
    if( rReference == "(" )
    {
        bConverted = true;
        sal_Unicode cSymbol = sal_Unicode(); // silence false warning
        if (rName == u"CheckIt")
            cSymbol = 0xF06F;
        else if (rName == u"UncheckIt")
            cSymbol = 0xF0FE;
        else if (rName == u"ShowExample")
            cSymbol = 0xF02A;
        //else if... : todo
        else
            bConverted = false;

        if( bConverted )
            rReference = OUString(cSymbol);
    }
    return bConverted;
}

// "MACROBUTTON"
eF_ResT SwWW8ImplReader::Read_F_Macro( WW8FieldDesc*, OUString& rStr)
{
    OUString aName;
    OUString aVText;
    bool bNewVText = true;
    bool bBracket  = false;
    WW8ReadFieldParams aReadParam( rStr );

    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
        case -2:
            if( aName.isEmpty() )
                aName = aReadParam.GetResult();
            else if( aVText.isEmpty() || bBracket )
            {
                if( bBracket )
                    aVText += " ";
                aVText += aReadParam.GetResult();
                if (bNewVText)
                {
                    bBracket = (aVText[0] == '[');
                    bNewVText = false;
                }
                else if( aVText.endsWith("]") )
                    bBracket  = false;
            }
            break;
        }
    }
    if( aName.isEmpty() )
        return eF_ResT::TAGIGN;  // makes no sense without Macro-Name

    NotifyMacroEventRead();

    //try converting macro symbol according to macro name
    bool bApplyWingdings = ConvertMacroSymbol( aName, aVText );
    aName = "StarOffice.Standard.Modul1." + aName;

    SwMacroField aField( static_cast<SwMacroFieldType*>(
                    m_rDoc.getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::Macro )), aName, aVText );

    if( !bApplyWingdings )
        m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );
    else
    {
        //set Wingdings font
        sal_uInt16 i = 0;
        for ( ; i < m_xFonts->GetMax(); i++ )
        {
            FontFamily eFamily;
            OUString aFontName;
            FontPitch ePitch;
            rtl_TextEncoding eSrcCharSet;
            if( GetFontParams( i, eFamily, aFontName, ePitch, eSrcCharSet )
                && aFontName=="Wingdings" )
            {
                break;
            }
        }

        if ( i < m_xFonts->GetMax() )
        {

            SetNewFontAttr( i, true, RES_CHRATR_FONT );
            m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );
            m_xCtrlStck->SetAttr( *m_pPaM->GetPoint(), RES_CHRATR_FONT );
            ResetCharSetVars();
        }
    }

    return eF_ResT::OK;
}

bool CanUseRemoteLink(const OUString &rGrfName)
{
    bool bUseRemote = false;
    try
    {
        // Related: tdf#102499, add a default css::ucb::XCommandEnvironment
        // in order to have https protocol manage certificates correctly
        uno::Reference< task::XInteractionHandler > xIH(
            task::InteractionHandler::createWithParent(comphelper::getProcessComponentContext(), nullptr));

        uno::Reference< ucb::XProgressHandler > xProgress;
        rtl::Reference<::ucbhelper::CommandEnvironment> pCommandEnv =
              new ::ucbhelper::CommandEnvironment(new comphelper::SimpleFileAccessInteraction( xIH ), xProgress);

        ::ucbhelper::Content aCnt(rGrfName,
                                  static_cast< ucb::XCommandEnvironment* >(pCommandEnv.get()),
                                  comphelper::getProcessComponentContext());

        if ( !INetURLObject( rGrfName ).isAnyKnownWebDAVScheme() )
        {
            OUString   aTitle;
            aCnt.getPropertyValue(u"Title"_ustr) >>= aTitle;
            bUseRemote = !aTitle.isEmpty();
        }
        else
        {
            // is a link to a WebDAV resource
            // need to use MediaType to check for link usability
            OUString   aMediaType;
            aCnt.getPropertyValue(u"MediaType"_ustr) >>= aMediaType;
            bUseRemote = !aMediaType.isEmpty();
        }
    }
    catch ( ... )
    {
        // this file did not exist, so we will not set this as graphiclink
        bUseRemote = false;
    }
    return bUseRemote;
}

// "INCLUDEPICTURE"
eF_ResT SwWW8ImplReader::Read_F_IncludePicture( WW8FieldDesc*, OUString& rStr )
{
    OUString aGrfName;
    bool bEmbedded = true;

    WW8ReadFieldParams aReadParam( rStr );
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
        case -2:
            if (aGrfName.isEmpty())
                aGrfName = ConvertFFileName(aReadParam.GetResult());
            break;

        case 'd':
            bEmbedded = false;
            break;

        case 'c':// skip the converter name
            aReadParam.FindNextStringPiece();
            break;
        }
    }

    if (!bEmbedded)
        bEmbedded = !CanUseRemoteLink(aGrfName);

    if (!bEmbedded)
    {
        /*
            Special case:

            Now we write the Link into the Doc and remember the SwFlyFrameFormat.
            Since we end on return FLD_READ_FSPA below, the skip value will be set
            so that Char-1 will still be read.
            When we then call SwWW8ImplReader::ImportGraf() it will then recognize
            that we have inserted a graphic link and the suiting SwAttrSet will be
            inserted into the frame format.
        */
        SfxItemSetFixed<RES_FRMATR_BEGIN, RES_FRMATR_END-1> aFlySet( m_rDoc.GetAttrPool() );
        aFlySet.Put( SwFormatAnchor( RndStdIds::FLY_AS_CHAR ) );
        aFlySet.Put( SwFormatVertOrient( 0, text::VertOrientation::TOP, text::RelOrientation::FRAME ));
        m_pFlyFormatOfJustInsertedGraphic =
            m_rDoc.getIDocumentContentOperations().InsertGraphic(*m_pPaM,
                                                    aGrfName,
                                                    OUString(),
                                                    nullptr,          // Graphic*
                                                    &aFlySet,
                                                    nullptr, nullptr);         // SwFrameFormat*
        m_aGrfNameGenerator.SetUniqueGraphName(m_pFlyFormatOfJustInsertedGraphic,
            INetURLObject(aGrfName).GetBase());
    }
    return eF_ResT::READ_FSPA;
}

OUString wwSectionNamer::UniqueName()
{
    const OUString aName(msFileLinkSeed + OUString::number(++mnFileSectionNo));
    return mrDoc.GetUniqueSectionName(&aName);
}

// "INCLUDETEXT"
eF_ResT SwWW8ImplReader::Read_F_IncludeText( WW8FieldDesc* /*pF*/, OUString& rStr )
{
    OUString aPara;
    OUString aBook;
    WW8ReadFieldParams aReadParam( rStr );
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
            case -2:
                if( aPara.isEmpty() )
                    aPara = aReadParam.GetResult();
                else if( aBook.isEmpty() )
                    aBook = aReadParam.GetResult();
                break;
            case '*':
                //Skip over MERGEFORMAT
                (void)aReadParam.SkipToNextToken();
                break;
        }
    }
    aPara = ConvertFFileName(aPara);

    if (!aBook.isEmpty() && aBook[ 0 ] != '\\')
    {
        // Section from Source (no switch)?
        ConvertUFName(aBook);
        aPara += OUStringChar(sfx2::cTokenSeparator)
            + OUStringChar(sfx2::cTokenSeparator) + aBook;
    }

    /*
    ##509##
    What we will do is insert a section to be linked to a file, but just in
    case the file is not available we will fill in the section with the stored
    content of this winword field as a fallback.
    */
    SwPosition aTmpPos(*m_pPaM->GetPoint());

    SwSectionData aSection(SectionType::FileLink,
            UIName(m_aSectionNameGenerator.UniqueName()));
    aSection.SetLinkFileName( aPara );
    aSection.SetProtectFlag(true);

    SwSection *const pSection =
        m_rDoc.InsertSwSection(*m_pPaM, aSection, nullptr, nullptr, false);
    OSL_ENSURE(pSection, "no section inserted");
    if (!pSection)
        return eF_ResT::TEXT;
    const SwSectionNode* pSectionNode = pSection->GetFormat()->GetSectionNode();
    OSL_ENSURE(pSectionNode, "no section node!");
    if (!pSectionNode)
        return eF_ResT::TEXT;

    m_pPaM->GetPoint()->Assign( pSectionNode->GetIndex()+1 );

    //we have inserted a section before this point, so adjust pos
    //for future page/section segment insertion
    m_aSectionManager.PrependedInlineNode(aTmpPos, m_pPaM->GetPointNode());

    return eF_ResT::TEXT;
}

// "SERIALPRINT"
eF_ResT SwWW8ImplReader::Read_F_DBField( WW8FieldDesc* pF, OUString& rStr )
{
#if !HAVE_FEATURE_DBCONNECTIVITY || ENABLE_FUZZERS
    (void) pF;
    (void) rStr;
#else
    OUString aName;
    WW8ReadFieldParams aReadParam( rStr );
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
        case -2:
            if( aName.isEmpty() )
                aName = aReadParam.GetResult();
            break;
        }
    }
    SwDBFieldType aD( &m_rDoc, aName, SwDBData() );   // Database: nothing

    SwFieldType* pFT = m_rDoc.getIDocumentFieldsAccess().InsertFieldType( aD );
    SwDBField aField( static_cast<SwDBFieldType*>(pFT) );
    aField.SetFieldCode( rStr );

    OUString aResult;
    m_xSBase->WW8ReadString( *m_pStrm, aResult, m_xPlcxMan->GetCpOfs()+
                           pF->nSRes, pF->nLRes, m_eTextCharSet );

    aResult = aResult.replace( '\xb', '\n' );

    aField.InitContent(aResult);

    m_rDoc.getIDocumentContentOperations().InsertPoolItem(*m_pPaM, SwFormatField( aField ));
#endif
    return eF_ResT::OK;
}

// "NEXT"
eF_ResT SwWW8ImplReader::Read_F_DBNext( WW8FieldDesc*, OUString& )
{
#if HAVE_FEATURE_DBCONNECTIVITY && !ENABLE_FUZZERS
    SwDBNextSetFieldType aN;
    SwFieldType* pFT = m_rDoc.getIDocumentFieldsAccess().InsertFieldType( aN );
    SwDBNextSetField aField( static_cast<SwDBNextSetFieldType*>(pFT), OUString(),
                            SwDBData() );       // Database: nothing
    m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );
#endif
    return eF_ResT::OK;
}

// "DATASET"
eF_ResT SwWW8ImplReader::Read_F_DBNum( WW8FieldDesc*, OUString& )
{
#if HAVE_FEATURE_DBCONNECTIVITY && !ENABLE_FUZZERS
    SwDBSetNumberFieldType aN;
    SwFieldType* pFT = m_rDoc.getIDocumentFieldsAccess().InsertFieldType( aN );
    SwDBSetNumberField aField( static_cast<SwDBSetNumberFieldType*>(pFT),
                           SwDBData() );            // Datenbase: nothing
    m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) );
#endif
    return eF_ResT::OK;
}

/*
    EQ , only the usage for
    a. Combined Characters supported, must be exactly in the form that word
    only accepts as combined characters, i.e.
    eq \o(\s\up Y(XXX),\s\do Y(XXX))
    b. Ruby Text supported, must be in the form that word recognizes as being
    ruby text
    ...
*/
eF_ResT SwWW8ImplReader::Read_F_Equation( WW8FieldDesc*, OUString& rStr )
{
    WW8ReadFieldParams aReadParam( rStr );
    const sal_Int32 cChar = aReadParam.SkipToNextToken();
    if ('o' == cChar || 'O' == cChar)
    {
        EquationResult aResult(ParseCombinedChars(rStr));

        if (aResult.sType == "Input")
        {
            SwInputField aField( static_cast<SwInputFieldType*>(m_rDoc.getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::Input )),
                aResult.sResult, aResult.sResult, SwInputFieldSubType::Text, false );
            m_rDoc.getIDocumentContentOperations().InsertPoolItem( *m_pPaM, SwFormatField( aField ) ); // insert input field
        }
        else if (aResult.sType == "CombinedCharacters")
        {
            SwCombinedCharField aField(static_cast<SwCombinedCharFieldType*>(
                m_rDoc.getIDocumentFieldsAccess().GetSysFieldType(SwFieldIds::CombinedChars)), aResult.sType);
            m_rDoc.getIDocumentContentOperations().InsertPoolItem(*m_pPaM, SwFormatField(aField));
        }
    }
    else if ('*' == cChar)
        Read_SubF_Ruby(aReadParam);

    return eF_ResT::OK;
}

void SwWW8ImplReader::Read_SubF_Ruby( WW8ReadFieldParams& rReadParam)
{
    sal_uInt16 nJustificationCode=0;
    OUString sFontName;
    sal_uInt32 nFontSize=0;
    OUString sRuby;
    OUString sText;
    for (;;)
    {
        const sal_Int32 nRet = rReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
        case -2:
            {
                OUString sTemp = rReadParam.GetResult();
                if( sTemp.startsWithIgnoreAsciiCase( "jc" ) )
                {
                    sTemp = sTemp.copy(2);
                    nJustificationCode = o3tl::narrowing<sal_uInt16>(sTemp.toInt32());
                }
                else if( sTemp.startsWithIgnoreAsciiCase( "hps" ) )
                {
                    sTemp = sTemp.copy(3);
                    nFontSize= static_cast<sal_uInt32>(sTemp.toInt32());
                }
                else if( sTemp.startsWithIgnoreAsciiCase( "Font:" ) )
                {
                    sTemp = sTemp.copy(5);
                    sFontName = sTemp;
                }
            }
            break;
        case '*':
            break;
        case 'o':
            for (;;)
            {
                const sal_Int32 nRes = rReadParam.SkipToNextToken();
                if ( nRes==-1 )
                    break;
                if ('u' == nRes)
                {
                    if (-2 == rReadParam.SkipToNextToken() &&
                        rReadParam.GetResult().startsWithIgnoreAsciiCase("p"))
                    {
                        if (-2 == rReadParam.SkipToNextToken())
                        {
                            OUString sPart = rReadParam.GetResult();
                            sal_Int32 nBegin = sPart.indexOf('(');

                            //Word disallows brackets in this field,
                            sal_Int32 nEnd = sPart.indexOf(')');

                            if ((nBegin != -1) &&
                                (nEnd != -1) && (nBegin < nEnd))
                            {
                                sRuby = sPart.copy(nBegin+1,nEnd-nBegin-1);
                            }
                            if (-1 != nEnd)
                            {
                                nBegin = sPart.indexOf(',',nEnd);
                                if (-1 == nBegin)
                                {
                                    nBegin = sPart.indexOf(';',nEnd);
                                }
                                nEnd = sPart.lastIndexOf(')');
                            }
                            if ((nBegin != -1) && (nEnd != -1) && (nBegin < nEnd))
                            {
                                sText = sPart.copy(nBegin+1,nEnd-nBegin-1);
                                sText = sw::FilterControlChars(sText);
                            }
                        }
                    }
                }
            }
            break;
        }
    }

    //Translate and apply
    if (sRuby.isEmpty() || sText.isEmpty() || sFontName.isEmpty() || !nFontSize)
        return;

    css::text::RubyAdjust eRubyAdjust;
    switch (nJustificationCode)
    {
        case 0:
            eRubyAdjust = css::text::RubyAdjust_CENTER;
            break;
        case 1:
            eRubyAdjust = css::text::RubyAdjust_BLOCK;
            break;
        case 2:
            eRubyAdjust = css::text::RubyAdjust_INDENT_BLOCK;
            break;
        default:
        case 3:
            eRubyAdjust = css::text::RubyAdjust_LEFT;
            break;
        case 4:
            eRubyAdjust = css::text::RubyAdjust_RIGHT;
            break;
    }

    SwFormatRuby aRuby(sRuby);
    const SwCharFormat *pCharFormat=nullptr;
    //Make a guess at which of asian of western we should be setting
    assert(g_pBreakIt && g_pBreakIt->GetBreakIter().is());
    sal_uInt16 nScript = g_pBreakIt->GetBreakIter()->getScriptType(sRuby, 0);

    //Check to see if we already have a ruby charstyle that this fits
    for(const auto& rpCharFormat : m_aRubyCharFormats)
    {
        const SvxFontHeightItem &rFH =
            rpCharFormat->GetFormatAttr(
                GetWhichOfScript(RES_CHRATR_FONTSIZE,nScript));
        if (rFH.GetHeight() == nFontSize*10)
        {
            const SvxFontItem &rF = rpCharFormat->GetFormatAttr(
                GetWhichOfScript(RES_CHRATR_FONT,nScript));
            if (rF.GetFamilyName() == sFontName)
            {
                pCharFormat = rpCharFormat;
                break;
            }
        }
    }

    //Create a new char style if necessary
    if (!pCharFormat)
    {
        UIName aNm;
        //Take this as the base name
        SwStyleNameMapper::FillUIName(RES_POOLCHR_RUBYTEXT,aNm);
        aNm = UIName(aNm.toString() + OUString::number(m_aRubyCharFormats.size()+1));
        SwCharFormat *pFormat = m_rDoc.MakeCharFormat(aNm, m_rDoc.GetDfltCharFormat());
        SvxFontHeightItem aHeightItem(nFontSize*10, 100, RES_CHRATR_FONTSIZE);
        SvxFontItem aFontItem(FAMILY_DONTKNOW,sFontName,
            OUString(), PITCH_DONTKNOW, RTL_TEXTENCODING_DONTKNOW, RES_CHRATR_FONT);
        aHeightItem.SetWhich(GetWhichOfScript(RES_CHRATR_FONTSIZE,nScript));
        aFontItem.SetWhich(GetWhichOfScript(RES_CHRATR_FONT,nScript));
        pFormat->SetFormatAttr(aHeightItem);
        pFormat->SetFormatAttr(aFontItem);
        m_aRubyCharFormats.push_back(pFormat);
        pCharFormat = pFormat;
    }

    //Set the charstyle and justification
    aRuby.SetCharFormatName(pCharFormat->GetName());
    aRuby.SetCharFormatId(pCharFormat->GetPoolFormatId());
    aRuby.SetAdjustment(eRubyAdjust);

    NewAttr(aRuby);
    m_rDoc.getIDocumentContentOperations().InsertString( *m_pPaM, sText );
    m_xCtrlStck->SetAttr( *m_pPaM->GetPoint(), RES_TXTATR_CJK_RUBY );

}

//        "table of ..." fields

static void lcl_toxMatchACSwitch(SwDoc const & rDoc,
                            SwTOXBase& rBase,
                            WW8ReadFieldParams& rParam,
                            SwCaptionDisplay eCaptionType)
{
    if ( rParam.GoToTokenParam() )
    {
        SwTOXType* pType = const_cast<SwTOXType*>(rDoc.GetTOXType( TOX_ILLUSTRATIONS, 0));
        rBase.RegisterToTOXType( *pType );
        rBase.SetCaptionDisplay( eCaptionType );
        // Read Sequence Name and store in TOXBase
        OUString sSeqName( rParam.GetResult() );
        lcl_ConvertSequenceName( sSeqName );
        rBase.SetSequenceName( UIName(sSeqName) );
    }
}

static void EnsureMaxLevelForTemplates(SwTOXBase& rBase)
{
    //If the TOC contains Template entries at levels > the evaluation level
    //that was initially taken from the max normal outline level of the word TOC
    //then we cannot use that for the evaluation level because writer cuts off
    //all styles above that level, while word just cuts off the "standard"
    //outline styles, we have no option but to expand to the highest level
    //Word included.
    if ((rBase.GetLevel() != MAXLEVEL) && (SwTOXElement::Template & rBase.GetCreateType()))
    {
        for (sal_uInt16 nI = MAXLEVEL; nI > 0; --nI)
        {
            if (!rBase.GetStyleNames(nI-1).isEmpty())
            {
                rBase.SetLevel(nI);
                break;
            }
        }
    }
}

static void lcl_toxMatchTSwitch(SwWW8ImplReader const & rReader, SwTOXBase& rBase,
    WW8ReadFieldParams& rParam)
{
    if ( !rParam.GoToTokenParam() )
        return;

    OUString sParams( rParam.GetResult() );
    if( sParams.isEmpty() )
        return;

    sal_Int32 nIndex = 0;

    // Delimiters between styles and style levels appears to allow both ; and ,

    OUString sTemplate( sParams.getToken(0, ';', nIndex) );
    if( -1 == nIndex )
    {
        nIndex=0;
        sTemplate = sParams.getToken(0, ',', nIndex);
    }
    if( -1 == nIndex )
    {
        const SwFormat* pStyle = rReader.GetStyleWithOrgWWName(sTemplate);
        if( pStyle )
            sTemplate = pStyle->GetName().toString();
        // Store Style for Level 0 into TOXBase
        rBase.SetStyleNames( UIName(sTemplate), 0 );
    }
    else while( -1 != nIndex )
    {
        sal_Int32 nOldIndex=nIndex;
        sal_uInt16 nLevel = o3tl::narrowing<sal_uInt16>(
            o3tl::toInt32(o3tl::getToken(sParams, 0, ';', nIndex)));
        if( -1 == nIndex )
        {
            nIndex = nOldIndex;
            nLevel = o3tl::narrowing<sal_uInt16>(
                o3tl::toInt32(o3tl::getToken(sParams, 0, ',', nIndex)));
        }

        if( (0 < nLevel) && (MAXLEVEL >= nLevel) )
        {
            nLevel--;
            // Store Style and Level into TOXBase
            const SwFormat* pStyle
                    = rReader.GetStyleWithOrgWWName( sTemplate );

            if( pStyle )
                sTemplate = pStyle->GetName().toString();

            UIName sStyles( rBase.GetStyleNames( nLevel ) );
            if( !sStyles.isEmpty() )
                sStyles = UIName(sStyles.toString() + OUStringChar(TOX_STYLE_DELIMITER));
            sStyles = UIName(sStyles.toString() + sTemplate);
            rBase.SetStyleNames( sStyles, nLevel );
        }
        // read next style name...
        nOldIndex = nIndex;
        sTemplate = sParams.getToken(0, ';', nIndex);
        if( -1 == nIndex )
        {
            nIndex=nOldIndex;
            sTemplate = sParams.getToken(0, ',', nIndex);
        }
    }
}

sal_uInt16 wwSectionManager::CurrentSectionColCount() const
{
    sal_uInt16 nIndexCols = 1;
    if (!maSegments.empty())
        nIndexCols = maSegments.back().maSep.ccolM1 + 1;
    return nIndexCols;
}

//Will there be a new pagebreak at this position (don't know what type
//until later)
bool wwSectionManager::WillHavePageDescHere(const SwNode& rNd) const
{
    bool bRet = false;
    if (!maSegments.empty())
    {
        if (!maSegments.back().IsContinuous() &&
            maSegments.back().maStart == rNd)
        {
            bRet = true;
        }
    }
    return bRet;
}

static sal_uInt16 lcl_GetMaxValidWordTOCLevel(const SwForm &rForm)
{
    // GetFormMax() returns level + 1, hence the -1
    sal_uInt16 nRet = rForm.GetFormMax()-1;

    // If the max of this type of TOC is greater than the max of a word
    // possible toc, then clip to the word max
    if (nRet > WW8ListManager::nMaxLevel)
        nRet = WW8ListManager::nMaxLevel;

    return nRet;
}

eF_ResT SwWW8ImplReader::Read_F_Tox( WW8FieldDesc* pF, OUString& rStr )
{
    if (!m_bLoadingTOXCache)
    {
        m_bLoadingTOXCache = true;
    }
    else
    {
        // Embedded TOX --> continue reading its content, but no further TOX
        // field
        ++m_nEmbeddedTOXLevel;
        return eF_ResT::TEXT;
    }

    if (pF->nLRes < 3)
        return eF_ResT::TEXT;      // ignore (#i25440#)

    TOXTypes eTox;            // create a ToxBase
    switch( pF->nId )
    {
        case  8:
            eTox = TOX_INDEX;
            break;
        case 13:
            eTox = TOX_CONTENT;
            break;
        default:
            eTox = TOX_USER;
            break;
    }

    SwTOXElement nCreateOf = (eTox == TOX_CONTENT) ? SwTOXElement::OutlineLevel : SwTOXElement::Mark;

    sal_uInt16 nIndexCols = 1;

    const SwTOXType* pType = m_rDoc.GetTOXType( eTox, 0 );
    SwForm aOrigForm(eTox);
    std::shared_ptr<SwTOXBase> pBase = std::make_shared<SwTOXBase>( pType, aOrigForm, nCreateOf, OUString() );
    pBase->SetProtected(m_aSectionManager.CurrentSectionIsProtected());
    switch( eTox ){
    case TOX_INDEX:
        {
            SwTOIOptions eOptions = SwTOIOptions::SameEntry | SwTOIOptions::CaseSensitive;

            // We set SwTOXElement::OutlineLevel only if
            // the parameter \o is within 1 to 9
            // or the parameter \f exists
            // or NO switch parameter are given at all.
            WW8ReadFieldParams aReadParam( rStr );
            for (;;)
            {
                const sal_Int32 nRet = aReadParam.SkipToNextToken();
                if ( nRet==-1 )
                    break;
                switch( nRet )
                {
                case 'c':
                    if ( aReadParam.GoToTokenParam() )
                    {
                        const OUString sParams( aReadParam.GetResult() );
                        // if NO OUString just ignore the \c
                        if( !sParams.isEmpty() )
                        {
                            nIndexCols = o3tl::narrowing<sal_uInt16>(sParams.toInt32());
                        }
                    }
                    break;
                case 'e':
                    {
                        if ( aReadParam.GoToTokenParam() )  // if NO String just ignore the \e
                        {
                            OUString sDelimiter( aReadParam.GetResult() );
                            SwForm aForm( pBase->GetTOXForm() );

                            // Attention: if TOX_CONTENT brave
                            //            GetFormMax() returns MAXLEVEL + 1  !!
                            sal_uInt16 nEnd = aForm.GetFormMax()-1;

                            for(sal_uInt16 nLevel = 1;
                                   nLevel <= nEnd;
                                   ++nLevel)
                            {
                                // Levels count from 1
                                // Level 0 is reserved for CAPTION

                                // Insert delimiter instead of tab in front of the page number if there is one:
                                FormTokenType ePrevType = TOKEN_END;
                                FormTokenType eType;
                                // -> #i21237#
                                SwFormTokens aPattern =
                                    aForm.GetPattern(nLevel);
                                SwFormTokens::iterator aIt = aPattern.begin();
                                do
                                {
                                    eType = ++aIt == aPattern.end() ? TOKEN_END : aIt->eTokenType;

                                    if (eType == TOKEN_PAGE_NUMS)
                                    {
                                        if (TOKEN_TAB_STOP == ePrevType)
                                        {
                                            --aIt;

                                            if (!sDelimiter.isEmpty() && sDelimiter[0] == 0x09)
                                                aIt->eTabAlign = SvxTabAdjust::End;
                                            else
                                            {
                                                SwFormToken aToken(TOKEN_TEXT);
                                                aToken.sText = sDelimiter;
                                                *aIt = std::move(aToken);
                                            }
                                            aForm.SetPattern(nLevel, std::move(aPattern));
                                        }

                                        eType = TOKEN_END;
                                    }

                                    ePrevType = eType;
                                }
                                while (TOKEN_END != eType);
                                // <- #i21237#
                            }
                            pBase->SetTOXForm( aForm );
                        }
                    }
                    break;
                case 'h':
                    {
                        eOptions |= SwTOIOptions::AlphaDelimiter;
                    }
                    break;
                }
            }
            pBase->SetOptions( eOptions );
        }
        break;

    case TOX_CONTENT:
        {
            bool bIsHyperlink = false;
            // We set SwTOXElement::OutlineLevel only if
            // the parameter \o is within 1 to 9
            // or the parameter \f exists
            // or NO switch parameter are given at all.
            SwTOXElement eCreateFrom = SwTOXElement::NONE;
            sal_Int32 nMaxLevel = 0;
            WW8ReadFieldParams aReadParam( rStr );
            for (;;)
            {
                const sal_Int32 nRet = aReadParam.SkipToNextToken();
                if ( nRet==-1 )
                    break;
                switch( nRet )
                {
                case 'h':
                    bIsHyperlink = true;
                    break;
                case 'a':
                case 'c':
                    lcl_toxMatchACSwitch(m_rDoc, *pBase, aReadParam,
                                           ('c' == nRet)
                                         ? SwCaptionDisplay::Complete
                                         : SwCaptionDisplay::Text );
                    break;
                case 'o':
                    {
                        sal_Int32 nVal;
                        if( !aReadParam.GetTokenSttFromTo(nullptr, &nVal, WW8ListManager::nMaxLevel) )
                            nVal = lcl_GetMaxValidWordTOCLevel(aOrigForm);
                        if( nMaxLevel < nVal )
                            nMaxLevel = nVal;
                        eCreateFrom |= SwTOXElement::OutlineLevel;
                    }
                    break;
                case 'f':
                    eCreateFrom |= SwTOXElement::Mark;
                    break;
                case 'l':
                    {
                        sal_Int32 nVal;
                        if( aReadParam.GetTokenSttFromTo(nullptr, &nVal, WW8ListManager::nMaxLevel) )
                        {
                            if( nMaxLevel < nVal )
                                nMaxLevel = nVal;
                            eCreateFrom |= SwTOXElement::Mark;
                        }
                    }
                    break;
                case 't': // paragraphs using special styles shall
                          // provide the TOX's content
                    lcl_toxMatchTSwitch(*this, *pBase, aReadParam);
                    eCreateFrom |= SwTOXElement::Template;
                    break;
                case 'p':
                    {
                        if ( aReadParam.GoToTokenParam() )  // if NO String just ignore the \p
                        {
                            OUString sDelimiter( aReadParam.GetResult() );
                            SwForm aForm( pBase->GetTOXForm() );

                            // Attention: if TOX_CONTENT brave
                            //            GetFormMax() returns MAXLEVEL + 1  !!
                            sal_uInt16 nEnd = aForm.GetFormMax()-1;

                            for(sal_uInt16 nLevel = 1;
                                   nLevel <= nEnd;
                                   ++nLevel)
                            {
                                // Levels count from 1
                                // Level 0 is reserved for CAPTION

                                // Insert delimiter instead of tab in front of the pagenumber if there is one:
                                FormTokenType ePrevType = TOKEN_END;
                                FormTokenType eType;

                                // -> #i21237#
                                SwFormTokens aPattern = aForm.GetPattern(nLevel);
                                SwFormTokens::iterator aIt = aPattern.begin();
                                do
                                {
                                    eType = ++aIt == aPattern.end() ? TOKEN_END : aIt->eTokenType;

                                    if (eType == TOKEN_PAGE_NUMS)
                                    {
                                        if (TOKEN_TAB_STOP == ePrevType)
                                        {
                                            --aIt;

                                            SwFormToken aToken(TOKEN_TEXT);
                                            aToken.sText = sDelimiter;

                                            *aIt = std::move(aToken);
                                            aForm.SetPattern(nLevel,
                                                             std::move(aPattern));
                                        }
                                        eType = TOKEN_END;
                                    }
                                    ePrevType = eType;
                                }
                                while( TOKEN_END != eType );
                                // <- #i21237#
                            }
                            pBase->SetTOXForm( aForm );
                        }
                    }
                    break;
                case 'n': // don't print page numbers
                    {
                        // read START and END param
                        sal_Int32 nStart(0);
                        sal_Int32 nEnd(0);
                        if( !aReadParam.GetTokenSttFromTo(  &nStart, &nEnd,
                            WW8ListManager::nMaxLevel ) )
                        {
                            nStart = 1;
                            nEnd = aOrigForm.GetFormMax()-1;
                        }
                        // remove page numbers from this levels
                        SwForm aForm( pBase->GetTOXForm() );
                        if (aForm.GetFormMax() <= nEnd)
                            nEnd = aForm.GetFormMax()-1;
                        for ( sal_Int32 nLevel = nStart; nLevel<=nEnd; ++nLevel )
                        {
                            // Levels count from 1
                            // Level 0 is reserved for CAPTION

                            // Remove pagenumber and if necessary the tab in front of it:
                            FormTokenType eType;
                            // -> #i21237#
                            SwFormTokens aPattern = aForm.GetPattern(nLevel);
                            SwFormTokens::iterator aIt = aPattern.begin();
                            do
                            {
                                eType = ++aIt == aPattern.end() ? TOKEN_END : aIt->eTokenType;

                                if (eType == TOKEN_PAGE_NUMS)
                                {
                                    aIt = aPattern.erase(aIt);
                                    --aIt;
                                    if (
                                         TOKEN_TAB_STOP ==
                                         aIt->eTokenType
                                       )
                                    {
                                        aPattern.erase(aIt);
                                        aForm.SetPattern(nLevel, std::move(aPattern));
                                    }
                                    eType = TOKEN_END;
                                }
                            }
                            while (TOKEN_END != eType);
                            // <- #i21237#
                        }
                        pBase->SetTOXForm( aForm );
                    }
                    break;

                /*
                // the following switches are not (yet) supported
                // by good old StarWriter:
                case 'b':
                case 's':
                case 'd':
                    break;
                */
                }
            }

            // For loading the expression of TOC field, we need to mapping its parameters to TOX entries tokens
            // also include the hyperlinks and page references
            SwFormToken aLinkStart(TOKEN_LINK_START);
            SwFormToken aLinkEnd(TOKEN_LINK_END);
            aLinkStart.sCharStyleName = UIName("Index Link");
            aLinkEnd.sCharStyleName = UIName("Index Link");
            SwForm aForm(pBase->GetTOXForm());
            sal_uInt16 nEnd = aForm.GetFormMax()-1;

            for(sal_uInt16 nLevel = 1; nLevel <= nEnd; ++nLevel)
            {
                SwFormTokens aPattern = aForm.GetPattern(nLevel);
                if ( bIsHyperlink )
                {
                    aPattern.insert(aPattern.begin(), aLinkStart);
                }
                else
                {
                    auto aItr = std::find_if(aPattern.begin(), aPattern.end(),
                        [](const SwFormToken& rToken) { return rToken.eTokenType == TOKEN_PAGE_NUMS; });
                    if (aItr != aPattern.end())
                        aPattern.insert(aItr, aLinkStart);
                }
                aPattern.push_back(aLinkEnd);
                aForm.SetPattern(nLevel, std::move(aPattern));
            }
            pBase->SetTOXForm(aForm);

            if (!nMaxLevel)
                nMaxLevel = WW8ListManager::nMaxLevel;
            pBase->SetLevel(nMaxLevel);

            const TOXTypes eType = pBase->GetTOXType()->GetType();
            switch( eType )
            {
                case TOX_CONTENT:
                    {
                        //If we would be created from outlines, either explicitly or by default
                        //then see if we need extra styles added to the outlines
                        SwTOXElement eEffectivelyFrom = eCreateFrom != SwTOXElement::NONE ? eCreateFrom : SwTOXElement::OutlineLevel;
                        if (eEffectivelyFrom & SwTOXElement::OutlineLevel)
                        {
                            // #i19683# Insert a text token " " between the number and entry token.
                            // In an ideal world we could handle the tab stop between the number and
                            // the entry correctly, but I currently have no clue how to obtain
                            // the tab stop position. It is _not_ set at the paragraph style.
                            std::unique_ptr<SwForm> pForm;
                            for (const SwWW8StyInf & rSI : m_vColl)
                            {
                                if (rSI.IsOutlineNumbered())
                                {
                                    sal_uInt16 nStyleLevel = rSI.mnWW8OutlineLevel;
                                    const SwNumFormat& rFormat = rSI.GetOutlineNumrule()->Get( nStyleLevel );
                                    if ( SVX_NUM_NUMBER_NONE != rFormat.GetNumberingType() )
                                    {
                                        ++nStyleLevel;

                                        if ( !pForm )
                                            pForm.reset(new SwForm( pBase->GetTOXForm() ));

                                        SwFormTokens aPattern = pForm->GetPattern(nStyleLevel);
                                        SwFormTokens::iterator aIt =
                                                find_if(aPattern.begin(), aPattern.end(),
                                                SwFormTokenEqualToFormTokenType(TOKEN_ENTRY_NO));

                                        if ( aIt != aPattern.end() )
                                        {
                                            SwFormToken aNumberEntrySeparator( TOKEN_TEXT );
                                            aNumberEntrySeparator.sText = " ";
                                            aPattern.insert( ++aIt, aNumberEntrySeparator );
                                            pForm->SetPattern( nStyleLevel, std::move(aPattern) );
                                        }
                                    }
                                }
                            }
                            if ( pForm )
                            {
                                pBase->SetTOXForm( *pForm );
                            }
                        }

                        if (eCreateFrom != SwTOXElement::NONE)
                            pBase->SetCreate(eCreateFrom);
                        EnsureMaxLevelForTemplates(*pBase);
                    }
                    break;
                case TOX_ILLUSTRATIONS:
                    {
                        if( eCreateFrom == SwTOXElement::NONE )
                            eCreateFrom = SwTOXElement::Sequence;
                        pBase->SetCreate( eCreateFrom );

                        /*
                        We don't know until here if we are an illustration
                        or not, and so have being used a TOX_CONTENT so far
                        which has 10 levels, while TOX has only two, this
                        level is set only in the constructor of SwForm, so
                        create a new one and copy over anything that could
                        be set in the old one, and remove entries from the
                        pattern which do not apply to illustration indices
                        */
                        SwForm aOldForm( pBase->GetTOXForm() );
                        SwForm aNewForm( eType );
                        sal_uInt16 nNewEnd = aNewForm.GetFormMax()-1;

                        // #i21237#
                        for(sal_uInt16 nLevel = 1; nLevel <= nNewEnd; ++nLevel)
                        {
                            SwFormTokens aPattern = aOldForm.GetPattern(nLevel);
                            SwFormTokens::iterator new_end =
                                remove_if(aPattern.begin(), aPattern.end(), SwFormTokenEqualToFormTokenType(TOKEN_ENTRY_NO));
                            aPattern.erase(new_end, aPattern.end() ); // table index imported with wrong page number format
                            aForm.SetPattern( nLevel, std::move(aPattern) );
                            aForm.SetTemplate( nLevel, aOldForm.GetTemplate(nLevel) );
                        }

                        pBase->SetTOXForm( aNewForm );
                    }
                    break;
                default:
                    OSL_ENSURE(false, "Unhandled toc options!");
                    break;
            }
        }
        break;
    case TOX_USER:
        break;
    default:
        OSL_ENSURE(false, "Unhandled toc options!");
        break;
    } // ToxBase fertig

    // #i21237# - propagate tab stops from paragraph styles used in TOX to patterns of the TOX
    pBase->AdjustTabStops( m_rDoc );

    //#i10028# inserting a toc implicitly acts like a parabreak in word and writer
    if ( m_pPaM->End() &&
         m_pPaM->End()->GetNode().GetTextNode() &&
         m_pPaM->End()->GetNode().GetTextNode()->Len() != 0 )
    {
        m_bCareFirstParaEndInToc = true;
    }

    if (m_pPaM->GetPoint()->GetContentIndex())
        FinalizeTextNode(*m_pPaM->GetPoint());

    const SwPosition* pPos = m_pPaM->GetPoint();

    SwFltTOX aFltTOX(std::move(pBase));

    // test if there is already a break item on this node
    if(SwContentNode* pNd = pPos->GetNode().GetContentNode())
    {
        const SfxItemSet* pSet = pNd->GetpSwAttrSet();
        if( pSet )
        {
            if (SfxItemState::SET == pSet->GetItemState(RES_BREAK, false))
                aFltTOX.SetHadBreakItem(true);
            if (SfxItemState::SET == pSet->GetItemState(RES_PAGEDESC, false))
                aFltTOX.SetHadPageDescItem(true);
        }
    }

    //Will there be a new pagebreak at this position (don't know what type
    //until later)
    if (m_aSectionManager.WillHavePageDescHere(pPos->GetNode()))
        aFltTOX.SetHadPageDescItem(true);

    // Set start in stack
    m_xReffedStck->NewAttr( *pPos, aFltTOX );

    m_rDoc.InsertTableOf(*m_pPaM->GetPoint(), aFltTOX.GetBase());

    //The TOC field representation contents should be inserted into TOC section, but not after TOC section.
    //So we need update the document position when loading TOC representation and after loading TOC;
    m_oPosAfterTOC.emplace(*m_pPaM, m_pPaM);
    (*m_pPaM).Move(fnMoveBackward);
    SwPaM aRegion(*m_pPaM, m_pPaM);

    OSL_ENSURE(SwDoc::GetCurTOX(*aRegion.GetPoint()), "Misunderstood how toc works");
    if (SwTOXBase* pBase2 = SwDoc::GetCurTOX(*aRegion.GetPoint()))
    {
        pBase2->SetMSTOCExpression(rStr);

        if ( nIndexCols > 1 )
        {
            // Set the column number for index
            SfxItemSetFixed<RES_COL, RES_COL> aSet( m_rDoc.GetAttrPool() );
            SwFormatCol aCol;
            aCol.Init( nIndexCols, 708, USHRT_MAX );
            aSet.Put( aCol );
            pBase2->SetAttrSet( aSet );
        }

        // inserting a toc inserts a section before this point, so adjust pos
        // for future page/section segment insertion
        m_aSectionManager.PrependedInlineNode( *m_oPosAfterTOC->GetPoint(), aRegion.GetPointNode() );
    }

    // Set end in stack
    m_xReffedStck->SetAttr( *pPos, RES_FLTR_TOX );

    if (!m_aApos.back()) //a para end in apo doesn't count
        m_bWasParaEnd = true;

    //Return FLD_TEXT, instead of FLD_OK
    //FLD_TEXT means the following content, commonly indicate the field representation content should be parsed
    //FLD_OK means the current field loading is finished. The rest part should be ignored.
    return eF_ResT::TEXT;
}

eF_ResT SwWW8ImplReader::Read_F_Shape(WW8FieldDesc* /*pF*/, OUString& /*rStr*/)
{
    /*
    #i3958# 0x8 followed by 0x1 where the shape is the 0x8 and its anchoring
    to be ignored followed by a 0x1 with an empty drawing. Detect in inserting
    the drawing that we are in the Shape field and respond accordingly
    */
    return eF_ResT::TEXT;
 }

eF_ResT SwWW8ImplReader::Read_F_Hyperlink( WW8FieldDesc* /*pF*/, OUString& rStr )
{
    OUString sURL, sTarget, sMark;

    //HYPERLINK "filename" [switches]
    rStr = comphelper::string::stripEnd(rStr, 1);

    bool bOptions = false;
    WW8ReadFieldParams aReadParam( rStr );
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
            case -2:
                if (sURL.isEmpty() && !bOptions)
                    sURL = ConvertFFileName(aReadParam.GetResult());
                break;

            case 'n':
                sTarget = "_blank";
                bOptions = true;
                break;

            case 'l':
                bOptions = true;
                if ( aReadParam.SkipToNextToken()==-2 )
                {
                    sMark = aReadParam.GetResult();
                    if( sMark.endsWith("\""))
                    {
                        sMark = sMark.copy( 0, sMark.getLength() - 1 );
                    }
                    // #120879# add cross reference bookmark name prefix, if it matches internal TOC bookmark naming convention
                    if ( IsTOCBookmarkName( SwMarkName(sMark) ) )
                    {
                        sMark = EnsureTOCBookmarkName(SwMarkName(sMark)).toString();
                        // track <sMark> as referenced TOC bookmark.
                        m_xReffedStck->m_aReferencedTOCBookmarks.insert( sMark );
                    }

                    if (m_bLoadingTOXCache)
                    {
                        m_bLoadingTOXHyperlink = true; //on loading a TOC field nested hyperlink field
                    }
                }
                break;
            case 't':
                bOptions = true;
                if ( aReadParam.SkipToNextToken()==-2 )
                    sTarget = aReadParam.GetResult();
                break;
            case 'h':
            case 'm':
                OSL_ENSURE( false, "Analysis still missing - unknown data" );
                [[fallthrough]];
            case 's':   //worthless fake anchor option
                bOptions = true;
                break;
        }
    }

    // use the result
    OSL_ENSURE(!sURL.isEmpty() || !sMark.isEmpty(), "WW8: Empty URL");

    if( !sMark.isEmpty() )
        sURL += "#" + sMark;

    SwFormatINetFormat aURL(sURL, sTarget);
    // If on loading TOC field, change the default style into the "index link"
    if (m_bLoadingTOXCache)
    {
        OUString sLinkStyle(u"Index Link"_ustr);
        sal_uInt16 nPoolId =
            SwStyleNameMapper::GetPoolIdFromProgName( ProgName(sLinkStyle), SwGetPoolIdFromName::ChrFmt );
        aURL.SetVisitedFormatAndId( UIName(sLinkStyle), nPoolId );
        aURL.SetINetFormatAndId( UIName(sLinkStyle), nPoolId );
    }

    //As an attribute this needs to be closed, and that'll happen from
    //EndExtSprm in conjunction with the maFieldStack. If there are flyfrms
    //between the start and begin, their hyperlinks will be set at that time
    //as well.
    m_xCtrlStck->NewAttr( *m_pPaM->GetPoint(), aURL );
    return eF_ResT::TEXT;
}

static void lcl_ImportTox(SwDoc &rDoc, SwPaM const &rPaM, const OUString &rStr, bool bIdx)
{
    TOXTypes eTox = ( !bIdx ) ? TOX_CONTENT : TOX_INDEX;    // Default

    sal_uInt16 nLevel = 1;

    OUString sFieldText;
    WW8ReadFieldParams aReadParam(rStr);
    for (;;)
    {
        const sal_Int32 nRet = aReadParam.SkipToNextToken();
        if ( nRet==-1 )
            break;
        switch( nRet )
        {
        case -2:
            if( sFieldText.isEmpty() )
            {
                // PrimaryKey without ":", 2nd after
                sFieldText = aReadParam.GetResult();
            }
            break;

        case 'f':
            if ( aReadParam.GoToTokenParam() )
            {
                const OUString sParams( aReadParam.GetResult() );
                if( sParams[0]!='C' && sParams[0]!='c' )
                    eTox = TOX_USER;
            }
            break;

        case 'l':
            if ( aReadParam.GoToTokenParam() )
            {
                const OUString sParams( aReadParam.GetResult() );
                // if NO String just ignore the \l
                if( !sParams.isEmpty() && sParams[0]>'0' && sParams[0]<='9' )
                {
                    nLevel = o3tl::narrowing<sal_uInt16>(sParams.toInt32());
                }
            }
            break;
        }
    }

    OSL_ENSURE( rDoc.GetTOXTypeCount( eTox ), "Doc.GetTOXTypeCount() == 0  :-(" );

    const SwTOXType* pT = rDoc.GetTOXType( eTox, 0 );
    SwTOXMark aM( pT );

    if( eTox != TOX_INDEX )
        aM.SetLevel( nLevel );
    else
    {
        sal_Int32 nFnd = sFieldText.indexOf( WW8_TOX_LEVEL_DELIM );
        if( -1 != nFnd )  // it exist levels
        {
            aM.SetPrimaryKey( sFieldText.copy( 0, nFnd ) );
            sal_Int32 nScndFnd = sFieldText.indexOf( WW8_TOX_LEVEL_DELIM, nFnd+1 );
            if( -1 != nScndFnd )
            {
                aM.SetSecondaryKey(  sFieldText.copy( nFnd+1, nScndFnd - nFnd - 1 ));
                nFnd = nScndFnd;
            }
            sFieldText = sFieldText.copy( nFnd+1 );
        }
    }

    if (!sFieldText.isEmpty())
    {
        aM.SetAlternativeText( sFieldText );
        rDoc.getIDocumentContentOperations().InsertPoolItem( rPaM, aM );
    }
}

void SwWW8ImplReader::ImportTox( int nFieldId, const OUString& aStr )
{
    bool bIdx = (nFieldId != 9);
    lcl_ImportTox(m_rDoc, *m_pPaM, aStr, bIdx);
}

void SwWW8ImplReader::Read_FieldVanish( sal_uInt16, const sal_uInt8*, short nLen )
{
    //Meaningless in a style
    if (m_pCurrentColl || !m_xPlcxMan)
        return;

    const int nChunk = 64;  //number of characters to read at one time

    // Careful: MEMICMP doesn't work with fieldnames including umlauts!
    const static char * const aFieldNames[] = {  "\x06""INHALT", "\x02""XE", // dt.
                                                 "\x02""TC"  };              // us
    const static sal_uInt8  aFieldId[] = { 9, 4, 9 };

    if( nLen < 0 )
    {
        m_bIgnoreText = false;
        return;
    }

    // our method was called from
    // ''Skip attributes of field contents'' loop within ReadTextAttr()
    if( m_bIgnoreText )
        return;

    m_bIgnoreText = true;
    sal_uInt64 nOldPos = m_pStrm->Tell();

    WW8_CP nStartCp = m_xPlcxMan->Where() + m_xPlcxMan->GetCpOfs();

    OUString sFieldName;
    sal_Int32 nFieldLen = m_xSBase->WW8ReadString( *m_pStrm, sFieldName, nStartCp,
        nChunk, m_eStructCharSet );
    nStartCp+=nFieldLen;

    sal_Int32 nC = 0;
    //If the first chunk did not start with a field start then
    //reset the stream position and give up
    if( !nFieldLen || sFieldName[nC]!=0x13 ) // Field Start Mark
    {
        // If Field End Mark found
        if( nFieldLen && sFieldName[nC]==0x15 )
            m_bIgnoreText = false;
        m_pStrm->Seek( nOldPos );
        return;                 // no field found
    }

    sal_Int32 nFnd;
    //If this chunk does not contain a field end, keep reading chunks
    //until we find one, or we run out of text,
    for (;;)
    {
        nFnd = sFieldName.indexOf(0x15);
        //found field end, we can stop now
        if (nFnd != -1)
            break;
        OUString sTemp;
        nFieldLen = m_xSBase->WW8ReadString( *m_pStrm, sTemp,
                                           nStartCp, nChunk, m_eStructCharSet );
        sFieldName+=sTemp;
        nStartCp+=nFieldLen;
        if (!nFieldLen)
            break;
    }

    m_pStrm->Seek( nOldPos );

    //if we have no 0x15 give up, otherwise erase everything from the 0x15
    //onwards
    if (nFnd<0)
        return;

    sFieldName = sFieldName.copy(0, nFnd);

    nC++;
    while ( sFieldName[nC]==' ' )
        nC++;

    for( int i = 0; i < 3; i++ )
    {
        const char* pName = aFieldNames[i];
        const sal_Int32 nNameLen = static_cast<sal_Int32>(*pName++);
        if( sFieldName.matchIgnoreAsciiCaseAsciiL( pName, nNameLen, nC ) )
        {
            ImportTox( aFieldId[i], sFieldName.copy( nC + nNameLen ) );
            break;                  // no duplicates allowed
        }
    }
    m_bIgnoreText = true;
    m_pStrm->Seek( nOldPos );
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
